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
#include "dbus_service/org.sailfishos.nfc.LocalHostApp.h"

#include "test_common.h"
#include "test_adapter.h"
#include "test_initiator.h"
#include "test_dbus.h"
#include "test_dbus_name.h"

#define NFC_DAEMON_PATH "/"
#define NFC_DAEMON_INTERFACE "org.sailfishos.nfc.Daemon"

#define TEST_DBUS_TIMEOUT \
    ((test_opt.flags & TEST_FLAG_DEBUG) ? -1 : TEST_TIMEOUT_MS)

typedef enum test_activate_flags {
    TEST_ACTIVATE_FLAGS_NONE = 0,
    TEST_ACTIVATE_KEEP_INITIATOR_ALIVE = 0x01,
    TEST_ACTIVATE_EXIT_WHEN_GONE = 0x02
} TEST_ACTIVATE_FLAGS;

static const char test_host_app_path[] = "/test_app";
static const char test_host_app_name[] = "TestApp";
static const guint8 test_host_app_aid_bytes[] = {
    0x01, 0x02, 0x03, 0x04
};
static const GUtilData test_host_app_aid = {
    TEST_ARRAY_AND_SIZE(test_host_app_aid_bytes)
};
static const guchar test_resp_ok[] = {
    0x90, 0x00
};

static TestOpt test_opt;
static const char* dbus_sender = ":1.0";

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    NfcInitiator* initiator;
    OrgSailfishosNfcLocalHostApp* app;
    GDBusConnection* server;
    GDBusConnection* client;
    gulong done_id;
    void* ext;
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
TestData*
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
    test->app = org_sailfishos_nfc_local_host_app_skeleton_new();
    return test;
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
        (test->app), client, test_host_app_path, &error));
}

static
void
test_activate(
    TestData* test,
    const TestTx* tx_list,
    gsize tx_count,
    TEST_ACTIVATE_FLAGS flags)
{
    GDEBUG("Simulating host activation");
    test->initiator = test_initiator_new_with_tx2(tx_list, tx_count,
        (flags & TEST_ACTIVATE_KEEP_INITIATOR_ALIVE) != 0);
    if (flags & TEST_ACTIVATE_EXIT_WHEN_GONE) {
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
    g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(test->app));
    g_object_unref(test->app);
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
gboolean
test_handle_start(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    /* Generic Start handler */
    GDEBUG("Host %s arrived", host);
    org_sailfishos_nfc_local_host_app_complete_start(app, call);
    return TRUE;
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
test_dont_handle_process(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    guchar cla,
    guchar ins,
    guchar p1,
    guchar p2,
    GVariant* data,
    guint le,
    void* user_data)
{
    g_assert_not_reached();
    return FALSE;
}
static
gboolean
test_dont_handle_select(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    void* user_data)
{
    /* The signa1ture suits implicit_select, select and deselect */
    g_assert_not_reached();
    return FALSE;
}

static
gboolean
test_dont_handle_response_status(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    guint response_id,
    gboolean ok,
    void* user_data)
{
    g_assert_not_reached();
    return FALSE;
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
test_call_register_local_host_app(
    TestData* test,
    const char* path,
    const char* name,
    const GUtilData* aid,
    NFC_HOST_APP_FLAGS flags,
    GAsyncReadyCallback callback)
{
    test_client_call(test, "RegisterLocalHostApp",
        g_variant_new("(os@ayu)", path, name,
        gutil_data_copy_as_variant(aid), flags),
        callback);
}

static
void
test_call_unregister_local_host_app(
    TestData* test,
    const char* path,
    GAsyncReadyCallback callback)
{
    test_client_call(test, "UnregisterLocalHostApp",
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

typedef struct test_data_ext_basic {
    guint start_count;
    guint restart_count;
    guint implicit_select_count;
} TestDataExtBasic;

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

    GDEBUG("%s has been unregistered", test_host_app_name);
    test_initiator_deactivate_later(test->initiator);
}

static
gboolean
test_basic_handle_implicit_select(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtBasic* ext = test->ext;

    GDEBUG("%s implicitly selected for %s", test_host_app_name, host);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_implicit_select(app, call);
    /* We should get implicitly selected only once, after restart */
    ext->implicit_select_count++;
    test_call_unregister_local_host_app(test, test_host_app_path,
        test_basic_unregistered);
    return TRUE;
}

static
gboolean
test_basic_handle_restart(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtBasic* ext = test->ext;

    GDEBUG("Host %s reactivated", host);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_restart(app, call);
    ext->restart_count++;
    return TRUE;
}

static
gboolean
test_basic_handle_start(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtBasic* ext = test->ext;

    GDEBUG("Host %s arrived", host);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_start(app, call);
    ext->start_count++;

    /* Restart the app before it gets implicitly selected */
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

    g_signal_connect(test->app, "handle-start",
        G_CALLBACK(test_basic_handle_start), test);
    g_signal_connect(test->app, "handle-restart",
        G_CALLBACK(test_basic_handle_restart), test);
    g_signal_connect(test->app, "handle-implicit-select",
        G_CALLBACK(test_basic_handle_implicit_select), test);

    GDEBUG("%s has been registered", test_host_app_name);
    test_activate(test, NULL, 0, TEST_ACTIVATE_EXIT_WHEN_GONE);
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
    test_call_register_local_host_app(test, test_host_app_path,
        test_host_app_name, &test_host_app_aid,
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        test_basic_registered);
}

static
void
test_basic(
    void)
{
    TestData test;
    TestDBus* dbus;
    TestDataExtBasic ext;

    memset(&ext, 0, sizeof(ext));
    test_data_init(&test)->ext = &ext;
    dbus = test_dbus_new2(test_start, test_basic_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);

    g_assert_cmpuint(ext.start_count, == ,1);
    g_assert_cmpuint(ext.restart_count, == ,1);
    g_assert_cmpuint(ext.implicit_select_count, == ,1);
}

/*==========================================================================*
 * process
 *==========================================================================*/

typedef struct test_data_ext_process {
    guint start_count;
} TestDataExtProcess;

static const guchar test_process_apdu_select_app[] = {
    0x00, 0xA4, 0x04, 0x00, 0x04, 0x01, 0x02, 0x03,
    0x04, 0x00
};
static const guchar test_process_apdu_select_file[] = {
    0x00, 0xa4, 0x00, 0x0c, 0x02, 0xe1, 0x03
};

#define TEST_RESPONSE_ID (42)

static
gboolean
test_process_handle_response_status(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    guint response_id,
    gboolean ok,
    TestData* test)
{
    GDEBUG("Response delivered");
    g_assert_cmpuint(response_id, == ,TEST_RESPONSE_ID);
    g_assert(ok);
    org_sailfishos_nfc_local_host_app_complete_response_status(app, call);
    test_initiator_deactivate_later(test->initiator);
    return TRUE;
}

static
gboolean
test_process_handle_apdu(
    OrgSailfishosNfcLocalHostApp* app,
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
    g_assert_cmpuint(buf->len, == ,sizeof(test_process_apdu_select_file));
    g_assert(!memcmp(buf->data, test_process_apdu_select_file, buf->len));
    g_byte_array_free(buf, TRUE);

    org_sailfishos_nfc_local_host_app_complete_process(app, call,
        dbus_service_dup_byte_array_as_variant(NULL, 0),
        test_resp_ok[0], test_resp_ok[1], TEST_RESPONSE_ID);
    return TRUE;
}

static
gboolean
test_process_handle_select(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtProcess* ext = test->ext;

    GDEBUG("%s selected for %s", test_host_app_name, host);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_start(app, call);
    ext->start_count++;
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
            { TEST_ARRAY_AND_SIZE(test_process_apdu_select_app) },
            { TEST_ARRAY_AND_SIZE(test_resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(test_process_apdu_select_file) },
            { TEST_ARRAY_AND_SIZE(test_resp_ok) }
        }
    };

    TestData* test = user_data;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);

    GDEBUG("%s has been registered", test_host_app_name);
    test_activate(test, TEST_ARRAY_AND_COUNT(tx),
        TEST_ACTIVATE_KEEP_INITIATOR_ALIVE |
        TEST_ACTIVATE_EXIT_WHEN_GONE);
    test->done_id = nfc_initiator_add_gone_handler(test->initiator,
        test_done, test->loop);
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

    g_signal_connect(test->app, "handle-start",
        G_CALLBACK(test_handle_start), test);
    g_signal_connect(test->app, "handle-select",
        G_CALLBACK(test_process_handle_select), test);
    g_signal_connect(test->app, "handle-process",
        G_CALLBACK(test_process_handle_apdu), test);
    g_signal_connect(test->app, "handle-response-status",
        G_CALLBACK(test_process_handle_response_status), test);

    test_call_register_local_host_app(test, test_host_app_path,
        test_host_app_name, &test_host_app_aid, NFC_HOST_APP_FLAGS_NONE,
        test_process_registered);
}

static
void
test_process(
    void)
{
    TestData test;
    TestDBus* dbus;
    TestDataExtProcess ext;

    memset(&ext, 0, sizeof(ext));
    test_data_init(&test)->ext = &ext;
    dbus = test_dbus_new2(test_start, test_process_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);

    g_assert_cmpuint(ext.start_count, == ,1);
}

/*==========================================================================*
 * switch
 *==========================================================================*/

typedef struct test_data_ext_switch {
    OrgSailfishosNfcLocalHostApp* app2;
    guint start1_count;
    guint start2_count;
    guint implicit_select_count;
    guint deselect_count;
    guint select_count;
    guint process_count;
} TestDataExtSwitch;

static const char test_host_app2_path[] = "/test_app2";
static const char test_host_app2_name[] = "TestApp2";
static const guint8 test_host_app2_aid_bytes[] = {
    0x05, 0x06, 0x07, 0x08
};
static const GUtilData test_host_app2_aid = {
    TEST_ARRAY_AND_SIZE(test_host_app2_aid_bytes)
};
static const guchar test_switch_apdu_select_app1[] = {
    0x00, 0xA4, 0x04, 0x00, 0x04, 0x01, 0x02, 0x03,
    0x04, 0x00
};
static const guchar test_switch_apdu_select_app2[] = {
    0x00, 0xA4, 0x04, 0x00, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x00
};
static const guchar test_switch_apdu_select_file[] = {
    0x00, 0xa4, 0x00, 0x0c, 0x02, 0xe1, 0x03
};

static
gboolean
test_switch_handle_stop(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    /* Stop actually completes the test (host is gone by now) */
    GDEBUG("%s is stopped", test_host_app_name);
    g_assert(!nfc_adapter_hosts(test->adapter)[0]);
    org_sailfishos_nfc_local_host_app_complete_stop(app, call);
    test_quit_later_n(test->loop, 1);
    return TRUE;
}

static
gboolean
test_switch_handle_apdu(
    OrgSailfishosNfcLocalHostApp* app,
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
    TestDataExtSwitch* ext = test->ext;

    /* Process handler */
    GDEBUG("Handling APDU");
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
    g_assert_cmpuint(buf->len, == ,sizeof(test_switch_apdu_select_file));
    g_assert(!memcmp(buf->data, test_switch_apdu_select_file, buf->len));
    g_byte_array_free(buf, TRUE);

    org_sailfishos_nfc_local_host_app_complete_process(app, call,
        dbus_service_dup_byte_array_as_variant(NULL, 0),
        test_resp_ok[0], test_resp_ok[1], 0);
    ext->process_count++;
    return TRUE;
}

static
gboolean
test_switch_handle_select(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtSwitch* ext = test->ext;

    GDEBUG("%s implicitly selected", test_host_app2_name);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_implicit_select(app, call);
    ext->select_count++;
    return TRUE;
}

static
gboolean
test_switch_handle_deselect(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtSwitch* ext = test->ext;

    GDEBUG("%s deselected", test_host_app2_name);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_start(app, call);
    ext->deselect_count++;
    return TRUE;
}

static
gboolean
test_switch_handle_implicit_select(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtSwitch* ext = test->ext;

    GDEBUG("%s implicitly selected", test_host_app_name);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_implicit_select(app, call);
    ext->implicit_select_count++;
    return TRUE;
}

static
gboolean
test_switch_handle_start1(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtSwitch* ext = test->ext;

    GDEBUG("%s started", test_host_app_name);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_start(app, call);
    ext->start1_count++;
    return TRUE;
}

static
gboolean
test_switch_handle_start2(
    OrgSailfishosNfcLocalHostApp* app,
    GDBusMethodInvocation* call,
    const char* host,
    TestData* test)
{
    TestDataExtSwitch* ext = test->ext;

    GDEBUG("%s started", test_host_app2_name);
    test_assert_host_path(test, host);
    org_sailfishos_nfc_local_host_app_complete_start(app, call);
    ext->start2_count++;
    return TRUE;
}

static
void
test_switch_registered(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    /*
     * We have two switch, 01020304 get selected implicitly, then 05060708
     * gets selected explicitly and handles the transaction.
     */
    static const TestTx tx[] = {
        {
            /* The first select is a noop */
            { TEST_ARRAY_AND_SIZE(test_switch_apdu_select_app1) },
            { TEST_ARRAY_AND_SIZE(test_resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(test_switch_apdu_select_app2) },
            { TEST_ARRAY_AND_SIZE(test_resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(test_switch_apdu_select_file) },
            { TEST_ARRAY_AND_SIZE(test_resp_ok) }
        }
    };

    TestData* test = user_data;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);

    GDEBUG("Apps have been registered");
    test_activate(test, TEST_ARRAY_AND_COUNT(tx), TEST_ACTIVATE_FLAGS_NONE);
}

static
void
test_switch_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    TestDataExtSwitch* ext = test->ext;
    GError* error = NULL;

    /* First app gets implicitly selected and then deselected */
    g_signal_connect(test->app, "handle-start",
        G_CALLBACK(test_switch_handle_start1), test);
    g_signal_connect(test->app, "handle-implicit-select",
        G_CALLBACK(test_switch_handle_implicit_select), test);
    g_signal_connect(test->app, "handle-select",
        G_CALLBACK(test_dont_handle_select), NULL);
    g_signal_connect(test->app, "handle-deselect",
        G_CALLBACK(test_switch_handle_deselect), test);
    g_signal_connect(test->app, "handle-process",
        G_CALLBACK(test_dont_handle_process), NULL);
    g_signal_connect(test->app, "handle-response-status",
        G_CALLBACK(test_dont_handle_response_status), NULL);
    g_signal_connect(test->app, "handle-stop",
        G_CALLBACK(test_switch_handle_stop), test);

    /* Second app gets explicitly selected and handles the APDU */
    g_signal_connect(ext->app2, "handle-start",
        G_CALLBACK(test_switch_handle_start2), test);
    g_signal_connect(ext->app2, "handle-implicit-select",
        G_CALLBACK(test_dont_handle_select), NULL);
    g_signal_connect(ext->app2, "handle-select",
        G_CALLBACK(test_switch_handle_select), test);
    g_signal_connect(ext->app2, "handle-deselect",
        G_CALLBACK(test_dont_handle_select), NULL);
    g_signal_connect(ext->app2, "handle-process",
        G_CALLBACK(test_switch_handle_apdu), test);

    test_started(test, client, server);
    g_assert(g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (ext->app2), client, test_host_app2_path, &error));

    test_call_register_local_host_app(test, test_host_app_path,
        test_host_app_name, &test_host_app_aid,
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        NULL);
    test_call_register_local_host_app(test, test_host_app2_path,
        test_host_app2_name, &test_host_app2_aid,
        NFC_HOST_APP_FLAGS_NONE, test_switch_registered);
}

static
void
test_switch(
    void)
{
    TestData test;
    TestDBus* dbus;
    TestDataExtSwitch ext;

    memset(&ext, 0, sizeof(ext));
    ext.app2 = org_sailfishos_nfc_local_host_app_skeleton_new();
    test_data_init(&test)->ext = &ext;
    dbus = test_dbus_new2(test_start, test_switch_start, &test);
    test_run(&test_opt, test.loop);

    g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(ext.app2));
    g_object_unref(ext.app2);
    test_data_cleanup(&test);
    test_dbus_free(dbus);

    g_assert_cmpuint(ext.start1_count, == ,1);
    g_assert_cmpuint(ext.start2_count, == ,1);
    g_assert_cmpuint(ext.implicit_select_count, == ,1);
    g_assert_cmpuint(ext.select_count, == ,1);
    g_assert_cmpuint(ext.deselect_count, == ,1);
    g_assert_cmpuint(ext.process_count, == ,1);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/local_app/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("process"), test_process);
    g_test_add_func(TEST_("switch"), test_switch);
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
