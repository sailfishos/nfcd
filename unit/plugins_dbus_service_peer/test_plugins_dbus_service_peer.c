/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "nfc_types_p.h"
#include "nfc_adapter.h"
#include "nfc_initiator.h"
#include "nfc_plugins.h"
#include "nfc_peer.h"
#include "nfc_peer_service.h"
#include "internal/nfc_manager_i.h"

#include "dbus_service/dbus_service.h"

#include "test_common.h"
#include "test_adapter.h"
#include "test_initiator.h"
#include "test_dbus.h"

#define NFC_PEER_INTERFACE "org.sailfishos.nfc.Peer"
#define NFC_PEER_INTERFACE_VERSION  (1)
#define NFC_PEER_DEFAULT_WKS \
    ((1 << NFC_LLC_SAP_SDP) | \
     (1 << NFC_LLC_SAP_SNEP) | \
     0x01)

static TestOpt test_opt;

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    NfcPeer* peer;
    DBusServicePeer* service;
} TestData;

static
void
test_data_init(
    TestData* test)
{
    static const guint8 atr_req_general_bytes [] = {
        0x46, 0x66, 0x6d, 0x01, 0x01, 0x11, 0x02, 0x02,
        0x07, 0xff, 0x03, 0x02, 0x00, 0x13, 0x04, 0x01,
        0xff
    };
    static const NfcParamNfcDepTarget peer_target_param = {
        { TEST_ARRAY_AND_SIZE(atr_req_general_bytes) }
    };
    static const guint8 symm_data[] = { 0x00, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        }
    };
    NfcPluginsInfo pi;
    NfcInitiator* initiator;

    memset(test, 0, sizeof(*test));
    memset(&pi, 0, sizeof(pi));
    g_assert((test->manager = nfc_manager_new(&pi)) != NULL);
    g_assert((test->adapter = test_adapter_new()) != NULL);
    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    initiator = test_initiator_new_with_tx2(TEST_ARRAY_AND_COUNT(tx), TRUE);
    test->peer = nfc_peer_ref(nfc_adapter_add_peer_target_a(test->adapter,
        initiator, NULL, &peer_target_param));
    g_assert(test->peer);
    nfc_initiator_unref(initiator);
    test->loop = g_main_loop_new(NULL, TRUE);
}

static
void
test_data_cleanup(
    TestData* test)
{
    dbus_service_peer_free(test->service);
    nfc_peer_unref(test->peer);
    nfc_adapter_unref(test->adapter);
    nfc_manager_unref(test->manager);
    g_main_loop_unref(test->loop);
}

static
void
test_start_and_get(
    TestData* test,
    GDBusConnection* client,
    GDBusConnection* server,
    const char* method,
    GAsyncReadyCallback callback)
{
    test->service = dbus_service_peer_new(test->peer, "/nfc0", server);
    g_assert(test->service);
    g_dbus_connection_call(client, NULL, test->service->path,
        NFC_PEER_INTERFACE, method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
        NULL, callback, test);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    dbus_service_peer_free(NULL);
}

/*==========================================================================*
 * get_all
 *==========================================================================*/

static
void
test_get_all_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    gboolean present = FALSE;
    gchar** ifaces = NULL;
    guint tech, wks;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(ibu^asu)", &version, &present, &tech, &ifaces, &wks);
    GDEBUG("version=%d, present=%d, tech=%u, %u interface(s), wks=0x%02x",
        version, present, tech, g_strv_length(ifaces), wks);
    g_assert(version >= NFC_PEER_INTERFACE_VERSION);
    g_assert(present);
    g_assert_cmpuint(g_strv_length(ifaces), > ,0);
    g_assert_cmpstr(ifaces[0], == ,NFC_PEER_INTERFACE);
    g_assert_cmpuint(tech, == ,NFC_TECHNOLOGY_A);
    g_assert_cmpuint(wks, == ,NFC_PEER_DEFAULT_WKS);
    g_strfreev(ifaces);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_all_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_start_and_get(test, client, server, "GetAll", test_get_all_done);
}

static
void
test_get_all(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_all_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_interface_version
 *==========================================================================*/

static
void
test_get_interface_version_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(i)", &version);
    GDEBUG("version=%d", version);
    g_assert_cmpint(version, >= ,NFC_PEER_INTERFACE_VERSION);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_interface_version_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetInterfaceVersion", test_get_interface_version_done);
}

static
void
test_get_interface_version(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_interface_version_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_present
 *==========================================================================*/

static
void
test_get_present_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean present = FALSE;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(b)", &present);
    GDEBUG("present=%d", present);
    g_assert(present);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_present_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetPresent", test_get_present_done);
}

static
void
test_get_present(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_present_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_technology
 *==========================================================================*/

static
void
test_get_technology_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint tech = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &tech);
    GDEBUG("tech=%u", tech);
    g_assert_cmpuint(tech, == ,NFC_TECHNOLOGY_A);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_technology_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetTechnology", test_get_technology_done);
}

static
void
test_get_technology(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_technology_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_interfaces
 *==========================================================================*/

static
void
test_get_interfaces_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** ifaces = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(^as)", &ifaces);
    g_assert(ifaces);
    GDEBUG("%u interface(s)", g_strv_length(ifaces));
    g_assert_cmpuint(g_strv_length(ifaces), > ,0);
    g_assert_cmpstr(ifaces[0], == ,NFC_PEER_INTERFACE);
    g_strfreev(ifaces);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_interfaces_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetInterfaces", test_get_interfaces_done);
}

static
void
test_get_interfaces(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_interfaces_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_wks
 *==========================================================================*/

static
void
test_get_wks_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint wks = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &wks);
    GDEBUG("wks=0x%02x", wks);
    g_assert_cmpuint(wks, == ,NFC_PEER_DEFAULT_WKS);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_wks_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetWellKnownServices", test_get_wks_done);
}

static
void
test_get_wks(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_wks_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * deactivate
 *==========================================================================*/

static
void
test_deactivate_removed(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;

    GDEBUG("%s removed", path);
    g_assert(!test->peer->present);
    test_quit_later(test->loop);
}

static
void
test_deactivate_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_unref(var);
    GDEBUG("%s deactivated", test->service->path);
    g_assert(!test->peer->present);
    dbus_service_peer_free(test->service);
    test->service = NULL;
}

static
void
test_get_deactivate_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->service = dbus_service_peer_new(test->peer, "/nfc0", server);
    g_assert(test->service);
    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_PEER_INTERFACE, "Removed", test->service->path, NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_deactivate_removed,
        test, NULL));
    g_dbus_connection_call(client, NULL, test->service->path,
        NFC_PEER_INTERFACE, "Deactivate", NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, test_deactivate_done, test);
}

static
void
test_deactivate(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_deactivate_start, &test);
    test_run(&test_opt, test.loop);
    g_assert(!test.peer->present);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/peer/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_present"), test_get_present);
    g_test_add_func(TEST_("get_technology"), test_get_technology);
    g_test_add_func(TEST_("get_interfaces"), test_get_interfaces);
    g_test_add_func(TEST_("get_wks"), test_get_wks);
    g_test_add_func(TEST_("deactivate"), test_deactivate);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
