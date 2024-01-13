/*
 * Copyright (C) 2023 Slava Monich <slava@monich.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfc_initiator_p.h"
#include "nfc_adapter.h"
#include "nfc_host.h"

#include "internal/nfc_manager_i.h"

#include <gutil_log.h>
#include <gutil_misc.h>
#include <gutil_idlepool.h>

#include "dbus_service/dbus_service.h"

#include "test_common.h"
#include "test_adapter.h"
#include "test_initiator.h"
#include "test_dbus.h"
#include "test_dbus_name.h"

#define NFC_HOST_INTERFACE "org.sailfishos.nfc.Host"
#define NFC_HOST_INTERFACE_VERSION  (1)

static TestOpt test_opt;

#define TEST_DBUS_TIMEOUT \
    ((test_opt.flags & TEST_FLAG_DEBUG) ? -1 : TEST_TIMEOUT_MS)

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    NfcInitiator* initiator;
    DBusServiceAdapter* service;
    GDBusConnection* server;
    GDBusConnection* client;
    GUtilIdlePool* pool;
} TestData;

static
void
test_data_init(
    TestData* test)
{
    NfcPluginsInfo pi;

    memset(test, 0, sizeof(*test));
    memset(&pi, 0, sizeof(pi));
    g_assert((test->manager = nfc_manager_new(&pi)) != NULL);
    g_assert((test->adapter = test_adapter_new()) != NULL);
    test->adapter->supported_modes |= NFC_MODE_READER_WRITER |
        NFC_MODE_CARD_EMULATION;
    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    test->loop = g_main_loop_new(NULL, TRUE);
    test->pool = gutil_idle_pool_new();
}

static
void
test_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->client = client);
    g_object_ref(test->server = server);
    test_name_own_set_connection(server);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(nfc_manager_start(test->manager));
}

static
void
test_activate(
    TestData* test)
{
    GDEBUG("Simulating host activation");
    test->initiator = test_initiator_new();
    test->initiator->protocol = NFC_PROTOCOL_T4A_TAG;
    g_assert(nfc_adapter_add_host(test->adapter, test->initiator));
}

static
void
test_data_cleanup(
    TestData* test)
{
    test_name_own_set_connection(NULL);
    nfc_manager_stop(test->manager, 0);
    gutil_object_unref(test->client);
    gutil_object_unref(test->server);
    nfc_initiator_unref(test->initiator);
    nfc_adapter_unref(test->adapter);
    nfc_manager_unref(test->manager);
    dbus_service_adapter_free(test->service);
    gutil_idle_pool_destroy(test->pool);
    g_main_loop_unref(test->loop);
}

static
const char*
test_host_path(
    TestData* test)
{
    NfcHost* host = nfc_adapter_hosts(test->adapter)[0];
    char* path;

    g_assert(test->service);
    g_assert(host);
    path = g_strconcat(dbus_service_adapter_path(test->service), "/",
        host->name, NULL);
    gutil_idle_pool_add(test->pool, path, g_free);
    return path;
}

static
void
test_host_call(
    TestData* test,
    const char* method,
    GVariant* args,
    GAsyncReadyCallback callback)
{
    g_dbus_connection_call(test->client, NULL, test_host_path(test),
        NFC_HOST_INTERFACE, method, args, NULL, G_DBUS_CALL_FLAGS_NONE,
        TEST_DBUS_TIMEOUT, NULL, callback, test);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    dbus_service_host_free(NULL);
}

/*==========================================================================*
 * get_all
 *==========================================================================*/

static
void
test_get_all_done(
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    gboolean present = FALSE;
    guint tech = NFC_TECHNOLOGY_UNKNOWN;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(client),
        result, &error);

    g_assert(var);
    g_variant_get(var, "(ibu)", &version, &present, &tech);
    g_variant_unref(var);

    GDEBUG("version=%d, present=%d, tech=0x%02x", version, present, tech);
    g_assert_cmpint(version, >= ,NFC_HOST_INTERFACE_VERSION);
    g_assert_cmpuint(tech, == ,NFC_TECHNOLOGY_A);
    g_assert(present);

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

    test_activate(test);
    test_host_call(test, "GetAll", NULL, test_get_all_done);
}


static
void
test_get_all(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_all_start, &test);
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
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(client),
        result, &error);

    g_assert(var);
    g_variant_get(var, "(i)", &version);
    g_variant_unref(var);

    GDEBUG("version=%d", version);
    g_assert_cmpint(version, >= ,NFC_HOST_INTERFACE_VERSION);

    test_quit_later(test->loop);
}

static
void
test_get_interface_version_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_activate(test);
    test_host_call(test, "GetInterfaceVersion", NULL,
        test_get_interface_version_done);
}


static
void
test_get_interface_version(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_interface_version_start, &test);
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
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean present = FALSE;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(client),
        result, &error);

    g_assert(var);
    g_variant_get(var, "(b)", &present);
    g_variant_unref(var);

    GDEBUG("present=%d", present);
    g_assert(present);

    test_quit_later(test->loop);
}

static
void
test_get_present_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_activate(test);
    test_host_call(test, "GetPresent", NULL, test_get_present_done);
}


static
void
test_get_present(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_present_start, &test);
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
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint tech = NFC_TECHNOLOGY_UNKNOWN;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(client),
        result, &error);

    g_assert(var);
    g_variant_get(var, "(u)", &tech);
    g_variant_unref(var);

    GDEBUG("tech=0x%02x", tech);
    g_assert_cmpuint(tech, == ,NFC_TECHNOLOGY_A);

    test_quit_later(test->loop);
}

static
void
test_get_technology_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_activate(test);
    test_host_call(test, "GetTechnology", NULL, test_get_technology_done);
}


static
void
test_get_technology(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_technology_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * deactivate
 *==========================================================================*/

static
void
test_deactivate_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;

    GDEBUG("%s deactivated", path);
    test_quit_later(test->loop);
}

static
void
test_deactivate_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_activate(test);

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_HOST_INTERFACE, "Removed", test_host_path(test), NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_deactivate_handler,
        test, NULL));

    test_host_call(test, "Deactivate", NULL, NULL);
}


static
void
test_deactivate(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_deactivate_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/host/" name

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
