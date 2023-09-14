/*
 * Copyright (C) 2019-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2019-2021 Jolla Ltd.
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

#include "nfc_types_p.h"
#include "internal/nfc_manager_i.h"
#include "nfc_adapter.h"
#include "nfc_version.h"

#include "dbus_service/dbus_service.h"
#include "dbus_service/plugin.h"

#include "test_common.h"
#include "test_adapter.h"
#include "test_dbus.h"
#include "test_dbus_name.h"

#include <gutil_misc.h>

#define NFC_DAEMON_PATH "/"
#define NFC_DAEMON_INTERFACE "org.sailfishos.nfc.Daemon"
#define NFC_DAEMON_INTERFACE_VERSION  (3)

static TestOpt test_opt;
static const char* dbus_sender = ":1.0";

#define TEST_DBUS_TIMEOUT \
    ((test_opt.flags & TEST_FLAG_DEBUG) ? -1 : TEST_TIMEOUT_MS)

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    GDBusConnection* client; /* Owned by TestDBus */
} TestData;

static
void
test_data_init2(
    TestData* test,
    gboolean add_adapter)
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
    if (add_adapter) {
        g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    }
    test->loop = g_main_loop_new(NULL, TRUE);
}

static
void
test_data_init(
    TestData* test)
{
    test_data_init2(test, TRUE);
}

static
void
test_data_cleanup(
    TestData* test)
{
    nfc_adapter_unref(test->adapter);
    nfc_manager_stop(test->manager, 0);
    nfc_manager_unref(test->manager);
    g_main_loop_unref(test->loop);
}

static
void
test_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_name_own_set_connection(server);
    test->client = client;
    g_assert(nfc_manager_start(test->manager));
}

static
DBusServicePlugin*
test_dbus_service_plugin(
    TestData* test)
{
    NfcPlugin* const* plugins = nfc_manager_plugins(test->manager);

    g_assert_cmpuint(gutil_ptrv_length(plugins), == ,1);
    return (DBusServicePlugin*) plugins[0];
}

static
void
test_call(
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
test_call_register_local_service(
    TestData* test,
    const char* path,
    const char* name,
    GAsyncReadyCallback callback)
{
    test_call(test, "RegisterLocalService",
        g_variant_new ("(os)", path, name),
        callback);
}

static
void
test_call_unregister_local_service(
    TestData* test,
    const char* path,
    GAsyncReadyCallback callback)
{
    test_call(test, "UnregisterLocalService",
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
 * no_peers
 *==========================================================================*/

static
void
test_no_peers_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    DBusServicePlugin* test_plugin = test_dbus_service_plugin(test);

    g_assert(!dbus_service_plugin_find_peer(test_plugin, NULL));
    test_quit_later(test->loop);
}

static
void
test_no_peers(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_no_peers_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
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
    gchar** adapters = NULL;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(i^ao)", &version, &adapters);
    GDEBUG("version=%d, %u adapter", version, g_strv_length(adapters));
    g_assert(version >= NFC_DAEMON_INTERFACE_VERSION);
    g_assert(g_strv_length(adapters) == 1);
    g_variant_unref(var);
    g_strfreev(adapters);
    test_quit_later(test->loop);
}

static
void
test_get_all_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetAll", NULL, test_get_all_done);
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
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(i)", &version);
    GDEBUG("version=%d", version);
    g_assert(version >= NFC_DAEMON_INTERFACE_VERSION);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_interface_version_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetInterfaceVersion", NULL,
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
 * get_adapters
 *==========================================================================*/

static
void
test_get_adapters_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** adapters = NULL;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(^ao)", &adapters);
    GDEBUG("%u adapter", g_strv_length(adapters));
    g_assert(g_strv_length(adapters) == 1);
    g_variant_unref(var);
    g_strfreev(adapters);
    test_quit_later(test->loop);
}

static
void
test_get_adapters_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetAdapters", NULL,
        test_get_adapters_done);
}

static
void
test_get_adapters(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_adapters_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_all2
 *==========================================================================*/

static
void
test_get_all2_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version, core_version = 0;
    gchar** adapters = NULL;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(i^aoi)", &version, &adapters, &core_version);
    GDEBUG("version=%d, %u adapter, core_version=%d", version,
        g_strv_length(adapters), core_version);
    g_assert(version >= NFC_DAEMON_INTERFACE_VERSION);
    g_assert(g_strv_length(adapters) == 1);
    g_assert(core_version == NFC_CORE_VERSION);
    g_variant_unref(var);
    g_strfreev(adapters);
    test_quit_later(test->loop);
}

static
void
test_get_all2_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetAll2", NULL, test_get_all2_done);
}

static
void
test_get_all2(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_all2_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_daemon_version
 *==========================================================================*/

static
void
test_get_daemon_version_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(i)", &version);
    GDEBUG("version=0x%08x", version);
    g_assert(version == NFC_CORE_VERSION);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_daemon_version_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetDaemonVersion", NULL,
        test_get_daemon_version_done);
}

static
void
test_get_daemon_version(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_daemon_version_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * register_service
 *==========================================================================*/

static const char test_register_service_path[] = "/test";
static const char test_register_service_name[] = "test";

static
void
test_register_service_unregister_done(
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
    test_quit_later(test->loop);
}

static
void
test_register_service_fail(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error));
    g_assert(g_error_matches(error, DBUS_SERVICE_ERROR,
        DBUS_SERVICE_ERROR_ALREADY_EXISTS));
    g_error_free(error);

    /* Unregister it */
    test_call_unregister_local_service(test, test_register_service_path,
        test_register_service_unregister_done);
}

static
void
test_register_service_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;
    guint sap = 0;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_get(ret, "(u)", &sap);
    g_variant_unref(ret);
    GDEBUG("sap=%u", sap);
    g_assert(sap);

    /* Second call will fail */
    test_call_register_local_service(test, test_register_service_path,
        test_register_service_name, test_register_service_fail);
}

static
void
test_register_service_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->client = client;
    test_call_register_local_service(test, test_register_service_path,
        test_register_service_name, test_register_service_done);
}

static
void
test_register_service(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_register_service_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * unregister_service_error
 *==========================================================================*/

static
void
test_unregister_svc_err_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error));
    g_assert(g_error_matches(error, DBUS_SERVICE_ERROR,
        DBUS_SERVICE_ERROR_NOT_FOUND));
    g_error_free(error);
    test_quit_later(test->loop);
}

static
void
test_unregister_svc_err_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_unregister_local_service((TestData*) test, "/none",
        test_unregister_svc_err_done);
}

static
void
test_unregister_svc_err(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_unregister_svc_err_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * adapter_added
 *==========================================================================*/

static
void
test_adapter_added_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** adapters = NULL;

    g_variant_get(args, "(^ao)", &adapters);
    g_assert(adapters);
    GDEBUG("%u adapters(s)", g_strv_length(adapters));
    g_assert(g_strv_length(adapters) == 1);
    g_strfreev(adapters);
    test_quit_later(test->loop);
}

static
void
test_adapter_added_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_DAEMON_INTERFACE, "AdaptersChanged", NFC_DAEMON_PATH,
        NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_adapter_added_handler,
        test, NULL));
    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
}

static
void
test_adapter_added(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init2(&test, FALSE);
    dbus = test_dbus_new2(test_start, test_adapter_added_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * adapter_removed
 *==========================================================================*/

static
void
test_adapter_removed_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** adapters = NULL;

    g_variant_get(args, "(^ao)", &adapters);
    g_assert(adapters);
    GDEBUG("%u adapters(s)", g_strv_length(adapters));
    g_assert(g_strv_length(adapters) == 0);
    g_strfreev(adapters);
    test_quit_later(test->loop);
}

static
void
test_adapter_removed_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_DAEMON_INTERFACE, "AdaptersChanged", NFC_DAEMON_PATH,
        NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_adapter_removed_handler,
        test, NULL));
    nfc_manager_remove_adapter(test->manager, test->adapter->name);
}

static
void
test_adapter_removed(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_adapter_removed_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/plugin/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("no_peers"), test_no_peers);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_adapters"), test_get_adapters);
    g_test_add_func(TEST_("get_all2"), test_get_all2);
    g_test_add_func(TEST_("get_daemon_version"), test_get_daemon_version);
    g_test_add_func(TEST_("register_service"), test_register_service);
    g_test_add_func(TEST_("unregister_service_error"), test_unregister_svc_err);
    g_test_add_func(TEST_("adapter_added"), test_adapter_added);
    g_test_add_func(TEST_("adapter_removed"), test_adapter_removed);
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
