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
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfc_initiator_p.h"
#include "nfc_initiator_impl.h"
#include "nfc_adapter.h"
#include "nfc_host.h"
#include "nfc_util.h"

#include "internal/nfc_manager_i.h"

#include <gutil_log.h>
#include <gutil_misc.h>

#include "dbus_service/plugin.h"
#include "dbus_service/dbus_service.h"
#include "dbus_service/dbus_service_util.h"
#include "dbus_service/org.sailfishos.nfc.LocalHostService.h"

#include "test_common.h"
#include "test_adapter.h"
#include "test_initiator.h"
#include "test_dbus.h"
#include "test_dbus_name.h"

#define NFC_DAEMON_PATH "/"
#define NFC_DAEMON_INTERFACE "org.sailfishos.nfc.Daemon"

#define TEST_DBUS_TIMEOUT \
    ((test_opt.flags & TEST_FLAG_DEBUG) ? -1 : TEST_TIMEOUT_MS)

static const char test_host_service_path[] = "/test_host";
static const char test_host_service_name[] = "TestHost";

static TestOpt test_opt;
static const char* dbus_sender = ":1.0";

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    NfcInitiator* initiator;
    OrgSailfishosNfcLocalHostService* service;
    GDBusConnection* server;
    GDBusConnection* client;
    gulong done_id;
} TestData;

static
void
test_done(
    NfcInitiator* initiator,
    void* loop)
{
    GDEBUG("Done");
    test_quit_later_n(loop, 1);
}

static
void
test_data_init(
    TestData* test)
{
    NfcPluginsInfo pi;
    static const NfcPluginDesc* const test_builtin_plugins[] = {
        &NFC_PLUGIN_DESC(dbus_service),
        NULL
    };

    memset(test, 0, sizeof(*test));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = test_builtin_plugins;
    g_assert((test->manager = nfc_manager_new(&pi)) != NULL);
    g_assert((test->adapter = test_adapter_new()) != NULL);
    test->adapter->supported_modes |= NFC_MODE_READER_WRITER |
        NFC_MODE_CARD_EMULATION;
    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    test->loop = g_main_loop_new(NULL, TRUE);
    test->service = org_sailfishos_nfc_local_host_service_skeleton_new();
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
    g_assert(nfc_manager_start(test->manager));
}

static
void
test_started(
    TestData* test,
    GDBusConnection* client,
    GDBusConnection* server)
{
    GError* error = NULL;

    g_assert(test->client == client);
    g_assert(test->server == server);
    g_assert(g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (test->service), client, test_host_service_path, &error));
}

static
void
test_activate(
    TestData* test,
    const TestTx* tx_list,
    gsize tx_count,
    gboolean stay_alive)
{
    GDEBUG("Simulating host activation");
    test->initiator = test_initiator_new_with_tx2(tx_list, tx_count,
        stay_alive);
    if (!stay_alive) {
        test->done_id = nfc_initiator_add_gone_handler(test->initiator,
            test_done, test->loop);
    }
    g_assert(nfc_adapter_add_host(test->adapter, test->initiator));
}

static
void
test_data_cleanup(
    TestData* test)
{
    test_name_own_set_connection(NULL);
    gutil_disconnect_handlers(test->initiator, &test->done_id, 1);
    nfc_manager_stop(test->manager, 0);
    g_dbus_interface_skeleton_unexport
        (G_DBUS_INTERFACE_SKELETON(test->service));
    g_object_unref(test->service);
    g_object_unref(test->client);
    g_object_unref(test->server);
    nfc_initiator_unref(test->initiator);
    nfc_adapter_unref(test->adapter);
    nfc_manager_unref(test->manager);
    g_main_loop_unref(test->loop);
}

static
gboolean
test_initiator_deactivate_cb(
    void* initiator)
{
    nfc_initiator_deactivate(NFC_INITIATOR(initiator));
    return G_SOURCE_REMOVE;
}

static
void
test_initiator_deactivate_later(
    NfcInitiator* initiator)
{
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, test_initiator_deactivate_cb,
        nfc_initiator_ref(initiator), g_object_unref);
}

static
void
test_assert_host_path(
    TestData* test,
    const char* path)
{
    NfcHost* host = nfc_adapter_hosts(test->adapter)[0];
    char* host_path;

    g_assert(host);
    host_path = g_strconcat("/nfc0/", host->name, NULL);
    g_assert_cmpstr(host_path, == ,path);
    g_free(host_path);
}

static
gboolean
test_handle_start(
    OrgSailfishosNfcLocalHostService* service,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    /* Generic Start handler */
    GDEBUG("Host %s arrived", host);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_service_complete_start(service, call);
    return TRUE;
}

static
void
test_client_call(
    TestData* test,
    const char* method,
    GVariant* args,
    GAsyncReadyCallback callback)
{
    /* Generic call without arguments */
    g_dbus_connection_call(test->client, NULL,
        NFC_DAEMON_PATH, NFC_DAEMON_INTERFACE, method, args, NULL,
        G_DBUS_CALL_FLAGS_NONE, TEST_DBUS_TIMEOUT, NULL, callback, test);
}

static
void
test_call_register_local_host_service(
    TestData* test,
    const char* path,
    const char* name,
    GAsyncReadyCallback callback)
{
    test_client_call(test, "RegisterLocalHostService",
         g_variant_new ("(os)", path, name),
         callback);
}

static
void
test_call_unregister_local_host_service(
    TestData* test,
    const char* path,
    GAsyncReadyCallback callback)
{
    test_client_call(test, "UnregisterLocalHostService",
        g_variant_new ("(o)", path),
        callback);
}

/*==========================================================================*
 * Stubs
 *==========================================================================*/

const gchar*
g_dbus_method_invocation_get_sender(
    GDBusMethodInvocation* call)
{
    return dbus_sender;
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic_unregistered(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);

    GDEBUG("%s has been unregistered", test_host_service_name);
    test_initiator_deactivate_later(test->initiator);
}

static
gboolean
test_basic_handle_restart(
    OrgSailfishosNfcLocalHostService* service,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    GDEBUG("Host %s reactivated", host);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_service_complete_restart(service, call);
    test_call_unregister_local_host_service(test, test_host_service_path,
        test_basic_unregistered);
    return TRUE;
}

static
gboolean
test_basic_handle_start(
    OrgSailfishosNfcLocalHostService* service,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    GDEBUG("Host %s arrived", host);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_service_complete_start(service, call);
    GDEBUG("Simulating reactivation");
    nfc_initiator_reactivated(test->initiator);
    return TRUE;
}

static
void
test_basic_registered(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);

    g_signal_connect(test->service, "handle-start",
        G_CALLBACK(test_basic_handle_start), test);
    g_signal_connect(test->service, "handle-restart",
        G_CALLBACK(test_basic_handle_restart), test);

    GDEBUG("%s has been registered", test_host_service_name);
    test_activate(test, NULL, 0, FALSE);
}

static
void
test_basic_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_started(test, client, server);
    test_call_register_local_host_service(test, test_host_service_path,
        test_host_service_name, test_basic_registered);
}

static
void
test_basic(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_basic_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * no_process
 *==========================================================================*/

static
void
test_no_process_registered(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    static const guchar cmd_apdu[] = {
        0x90, 0x5a, 0x00, 0x00, 0x03, 0x14, 0x20, 0xef, 0x00
    };
    static const guchar resp_err[] = { 0x6e, 0x00 };
    static const TestTx tx[] = {
        { /* APDU not handled */
            { TEST_ARRAY_AND_SIZE(cmd_apdu) },
            { TEST_ARRAY_AND_SIZE(resp_err) }
        }
    };

    TestData* test = user_data;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);

    g_signal_connect(test->service, "handle-start",
        G_CALLBACK(test_handle_start), test);

    test_activate(test, TEST_ARRAY_AND_COUNT(tx), FALSE);
}

static
void
test_no_process_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_started(test, client, server);
    test_call_register_local_host_service(test, test_host_service_path,
        test_host_service_name, test_no_process_registered);
}

static
void
test_no_process(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_no_process_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * process
 *==========================================================================*/

#define TEST_RESPONSE_ID (42)
static const guchar test_process_cmd[] = {
    0x90, 0x5a, 0x00, 0x00, 0x03, 0x14, 0x20, 0xef, 0x00
};
static const guchar test_process_resp[] = {
    0x90, 0x00
};

static
gboolean
test_process_handle_response_status(
    OrgSailfishosNfcLocalHostService* service,
    GDBusMethodInvocation* call,
    guint response_id,
    gboolean ok,
    TestData* test)
{
    GDEBUG("Response delivered");
    g_assert_cmpuint(response_id, == ,TEST_RESPONSE_ID);
    g_assert(ok);
    org_sailfishos_nfc_local_host_service_complete_response_status
        (service, call);
    test_quit_later_n(test->loop, 1);
    return TRUE;
}

static
gboolean
test_process_handle_apdu(
    OrgSailfishosNfcLocalHostService* service,
    GDBusMethodInvocation* call,
    const char* host,
    guchar cla,
    guchar ins,
    guchar p1,
    guchar p2,
    GVariant* data,
    guint le,
    TestData* test)
{
    NfcApdu apdu;
    GByteArray* buf = g_byte_array_new();

    /* Process handler */
    GDEBUG("Host %s handling APDU", host);
    test_assert_host_path(test, host);

    /* Validate APDU */
    apdu.cla = cla;
    apdu.ins = ins;
    apdu.p1 = p1;
    apdu.p2 = p2;
    apdu.data.bytes = g_variant_get_data(data);
    apdu.data.size = g_variant_get_size(data);
    apdu.le = le;
    g_assert(nfc_apdu_encode(buf, &apdu));
    g_assert_cmpuint(buf->len, == ,sizeof(test_process_cmd));
    g_assert(!memcmp(buf->data, test_process_cmd, buf->len));
    g_byte_array_free(buf, TRUE);

    org_sailfishos_nfc_local_host_service_complete_process(service, call,
        dbus_service_dup_byte_array_as_variant(NULL, 0),
        test_process_resp[0], test_process_resp[1], TEST_RESPONSE_ID);
    return TRUE;
}

static
void
test_process_registered(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(test_process_cmd) },
            { TEST_ARRAY_AND_SIZE(test_process_resp) }
        }
    };

    TestData* test = user_data;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);

    g_signal_connect(test->service, "handle-start",
        G_CALLBACK(test_handle_start), test);
    g_signal_connect(test->service, "handle-process",
        G_CALLBACK(test_process_handle_apdu), test);
    g_signal_connect(test->service, "handle-response-status",
        G_CALLBACK(test_process_handle_response_status), test);

    test_activate(test, TEST_ARRAY_AND_COUNT(tx), TRUE);
}

static
void
test_process_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_started(test, client, server);
    test_call_register_local_host_service(test, test_host_service_path,
        test_host_service_name, test_process_registered);
}

static
void
test_process(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_process_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/local_host/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("no_process"), test_no_process);
    g_test_add_func(TEST_("process"), test_process);
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
