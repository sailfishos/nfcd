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
#define NFC_DAEMON_INTERFACE_VERSION  (4)

static TestOpt test_opt;
static const char* dbus_sender = ":1.0";
static const char test_host_service_path[] = "/test_host";
static const char test_host_service_name[] = "TestHost";

#define TEST_DBUS_TIMEOUT \
    ((test_opt.flags & TEST_FLAG_DEBUG) ? -1 : TEST_TIMEOUT_MS)

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    GDBusConnection* client; /* Owned by TestDBus */
    void* ext;
} TestData;

static
TestData*
test_data_init3(
    TestData* test,
    NfcAdapter* adapter, /* Consumes the reference */
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
    g_assert((test->adapter = adapter) != NULL);
    if (add_adapter) {
        g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    }
    test->loop = g_main_loop_new(NULL, TRUE);
    return test;
}

static
TestData*
test_data_init2(
    TestData* test,
    gboolean add_adapter)
{
    return test_data_init3(test, test_adapter_new(), add_adapter);
}

static
TestData*
test_data_init(
    TestData* test)
{
    return test_data_init2(test, TRUE);
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
        G_DBUS_CALL_FLAGS_NONE, TEST_TIMEOUT_MS, NULL, callback, test);
}

static
void
test_call_request_mode(
    TestData* test,
    NFC_MODE enable,
    NFC_MODE disable,
    GAsyncReadyCallback callback)
{
    test_call(test, "RequestMode", g_variant_new ("(uu)",
        enable, disable), callback);
}

static
void
test_call_release_mode(
    TestData* test,
    guint id,
    GAsyncReadyCallback callback)
{
    test_call(test, "ReleaseMode",
        g_variant_new ("(u)", id),
        callback);
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

static
void
test_call_request_techs(
    TestData* test,
    NFC_TECHNOLOGY enable,
    NFC_TECHNOLOGY disable,
    GAsyncReadyCallback callback)
{
    test_call(test, "RequestTechs",
        g_variant_new ("(uu)", enable, disable),
        callback);
}

static
void
test_call_release_techs(
    TestData* test,
    guint id,
    GAsyncReadyCallback callback)
{
    test_call(test, "ReleaseTechs",
        g_variant_new ("(u)", id),
        callback);
}

static
void
test_call_register_local_host_service(
    TestData* test,
    const char* path,
    const char* name,
    GAsyncReadyCallback callback)
{
    test_call(test, "RegisterLocalHostService",
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
    test_call(test, "UnregisterLocalHostService",
        g_variant_new ("(o)", path),
        callback);
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
    test_call(test, "RegisterLocalHostApp",
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
    test_call(test, "UnregisterLocalHostApp",
        g_variant_new ("(o)", path),
        callback);
}

static
void
test_signal_subscribe(
    TestData* test,
    const char* name,
    GDBusSignalCallback handler)
{
    g_assert(g_dbus_connection_signal_subscribe(test->client, NULL,
        NFC_DAEMON_INTERFACE, name, NFC_DAEMON_PATH, NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, handler, test, NULL));
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
test_basic_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    DBusServicePlugin* test_plugin = test_dbus_service_plugin(test);

    g_assert(!dbus_service_plugin_find_peer(test_plugin, NULL));
    g_assert(!dbus_service_plugin_find_host(test_plugin, NULL));
    test_quit_later(test->loop);
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
 * stop
 *==========================================================================*/

static
void
test_stop_done(
    NfcManager* self,
    void* loop)
{
    test_quit_later((GMainLoop*)loop);
}

static
void
test_stop_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_assert(nfc_manager_add_stopped_handler(test->manager, test_stop_done,
        test->loop));
    test_name_own_set_connection(NULL);
}

static
void
test_stop(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_stop_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * client_gone
 *==========================================================================*/

static
void
test_client_gone_fail(
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
gboolean
test_client_gone_try_unregister(
   gpointer test)
{
    test_call_unregister_local_host_service(test, test_host_service_path,
        test_client_gone_fail);
    return G_SOURCE_REMOVE;
}

static
void
test_client_gone_ok(
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

    /* Simulate disappearence of the client */
    test_name_watch_vanish(dbus_sender);

    /* This call will fail because the client is gone */
    g_idle_add(test_client_gone_try_unregister, test);
}

static
void
test_client_gone_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->client = client;
    test_call_register_local_host_service(test, test_host_service_path,
        test_host_service_name, test_client_gone_ok);
}

static
void
test_client_gone(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    test.adapter->supported_modes |= NFC_MODE_CARD_EMULATION;
    dbus = test_dbus_new2(test_start, test_client_gone_start, &test);
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
    g_variant_unref(var);

    GDEBUG("version=%d, %u adapter", version, g_strv_length(adapters));
    g_assert(version >= NFC_DAEMON_INTERFACE_VERSION);
    g_assert(g_strv_length(adapters) == 1);
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
    g_variant_unref(var);

    GDEBUG("version=%d", version);
    g_assert(version >= NFC_DAEMON_INTERFACE_VERSION);
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
    g_variant_unref(var);

    GDEBUG("%u adapter", g_strv_length(adapters));
    g_assert(g_strv_length(adapters) == 1);
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
    g_variant_unref(var);

    GDEBUG("version=%d, %u adapter, core_version=%d", version,
        g_strv_length(adapters), core_version);
    g_assert_cmpint(version, >= ,NFC_DAEMON_INTERFACE_VERSION);
    g_assert_cmpuint(g_strv_length(adapters), == ,1);
    g_assert_cmpint(core_version, == ,NFC_CORE_VERSION);
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
    g_variant_unref(var);

    GDEBUG("version=0x%08x", version);
    g_assert(version == NFC_CORE_VERSION);
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
 * get_all3
 *==========================================================================*/

static
void
test_get_all3_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version, core_version = 0;
    gchar** adapters = NULL;
    GError* error = NULL;
    guint mode = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(i^aoiu)", &version, &adapters, &core_version, &mode);
    g_variant_unref(var);

    GDEBUG("version=%d, %u adapter, core_version=%d, mode=0x%02x", version,
        g_strv_length(adapters), core_version, mode);
    g_assert_cmpint(version, >= ,NFC_DAEMON_INTERFACE_VERSION);
    g_assert_cmpuint(g_strv_length(adapters), == ,1);
    g_assert_cmpint(core_version, == ,NFC_CORE_VERSION);
    g_assert_cmpint(mode, == ,NFC_MODE_READER_WRITER);
    g_strfreev(adapters);
    test_quit_later(test->loop);
}

static
void
test_get_all3_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetAll3", NULL, test_get_all3_done);
}

static
void
test_get_all3(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_all3_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_mode
 *==========================================================================*/

static
void
test_get_mode_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint mode = 0;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(u)", &mode);
    g_variant_unref(var);

    GDEBUG("mode=0x%02x", mode);
    g_assert_cmpint(mode, == ,NFC_MODE_READER_WRITER);
    test_quit_later(test->loop);
}

static
void
test_get_mode_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetMode", NULL, test_get_mode_done);
}

static
void
test_get_mode(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_mode_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * request_mode
 *==========================================================================*/

typedef struct test_data_ext_request_mode {
    guint mode_changed_count;
    NFC_MODE mode_changed;
    guint req_id;
} TestDataExtRequestMode;

static
void
test_mode_changed_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRequestMode* ext = test->ext;
    guint mode = NFC_MODE_NONE;

    g_variant_get(args, "(u)", &mode);
    GDEBUG("mode => 0x%02x", mode);
    ext->mode_changed = mode;
    ext->mode_changed_count++;
}

static
void
test_request_mode_release_fail(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRequestMode* ext = test->ext;
    GError* error = NULL;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error));
    g_assert(g_error_matches(error, DBUS_SERVICE_ERROR,
        DBUS_SERVICE_ERROR_NOT_FOUND));
    g_error_free(error);

    g_assert_cmpuint(ext->mode_changed_count, == ,2);
    test_quit_later(test->loop);
}

static
void
test_request_mode_release_ok(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRequestMode* ext = test->ext;
    NfcManager* manager = test->manager;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);
    g_assert(!(manager->mode & NFC_MODES_P2P));
    g_assert(manager->mode & NFC_MODE_READER_WRITER);
    g_assert_cmpint(ext->mode_changed, == ,manager->mode);
    g_assert_cmpuint(ext->mode_changed_count, == ,2);

    /* Try again with the same id (and fail) */
    test_call_release_mode(test, ext->req_id, test_request_mode_release_fail);
}

static
void
test_request_mode_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRequestMode* ext = test->ext;
    NfcManager* manager = test->manager;
    guint id = 0;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(u)", &id);
    g_variant_unref(var);

    GDEBUG("request id=%u", id);
    g_assert(id);
    g_assert(manager->mode & NFC_MODES_P2P);
    g_assert(!(manager->mode & NFC_MODE_READER_WRITER));
    g_assert_cmpint(ext->mode_changed, == ,manager->mode);
    g_assert_cmpuint(ext->mode_changed_count, == ,1);
    g_assert_cmpuint(ext->req_id, == ,0);
    ext->req_id = id;

    /* Release the request */
    test_call_release_mode(test, ext->req_id, test_request_mode_release_ok);
}

static
void
test_request_mode_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    NfcManager* manager = test->manager;

    test->client = client;
    test_signal_subscribe(test, "ModeChanged", test_mode_changed_handler);
    g_assert(!(manager->mode & NFC_MODES_P2P));
    g_assert(manager->mode & NFC_MODE_READER_WRITER);
    test_call_request_mode(test, NFC_MODES_P2P, NFC_MODE_READER_WRITER,
        test_request_mode_done);
}

static
void
test_request_mode(
    void)
{
    TestDataExtRequestMode ext;
    TestData test;
    TestDBus* dbus;

    memset(&ext, 0, sizeof(ext));
    test_data_init(&test)->ext = &ext;
    dbus = test_dbus_new2(test_start, test_request_mode_start, &test);
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
    void* user_data)
{
    test_call_unregister_local_service((TestData*) user_data, "/none",
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

    test->client = client;
    test_signal_subscribe(test, "AdaptersChanged", test_adapter_added_handler);
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

    test->client = client;
    test_signal_subscribe(test, "AdaptersChanged",
        test_adapter_removed_handler);
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
 * get_all4
 *==========================================================================*/

static
void
test_get_all4_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version, core_version = 0;
    gchar** adapters = NULL;
    GError* error = NULL;
    guint mode = 0, techs = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(i^aoiuu)", &version, &adapters, &core_version,
        &mode, &techs);
    g_variant_unref(var);

    GDEBUG("version=%d, %u adapter, core_version=%d, mode=0x%02x, mode=0x%02x",
        version, g_strv_length(adapters), core_version, mode, techs);
    g_assert_cmpint(version, >= ,NFC_DAEMON_INTERFACE_VERSION);
    g_assert_cmpuint(g_strv_length(adapters), == ,1);
    g_assert_cmpint(core_version, == ,NFC_CORE_VERSION);
    g_assert_cmpuint(mode, == ,NFC_MODE_READER_WRITER);
    g_assert_cmpuint(techs, == ,NFC_TECHNOLOGY_A|NFC_TECHNOLOGY_B);
    g_strfreev(adapters);
    test_quit_later(test->loop);
}

static
void
test_get_all4_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetAll4", NULL, test_get_all4_done);
}

static
void
test_get_all4(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_all4_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_techs
 *==========================================================================*/

static
void
test_get_techs_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint techs = 0;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(u)", &techs);
    g_variant_unref(var);

    GDEBUG("techs=0x%02x", techs);
    g_assert_cmpuint(techs, == ,NFC_TECHNOLOGY_A|NFC_TECHNOLOGY_B);
    test_quit_later(test->loop);
}

static
void
test_get_techs_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call((TestData*)test, "GetTechs", NULL, test_get_techs_done);
}

static
void
test_get_techs(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_get_techs_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * request_techs
 *==========================================================================*/

typedef struct test_data_ext_request_techs {
    guint techs_changed_count;
    NFC_TECHNOLOGY techs_changed;
    guint req_id;
} TestDataExtRequestTechs;

static
void
test_techs_changed_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRequestTechs* ext = test->ext;
    guint techs = NFC_TECHNOLOGY_UNKNOWN;

    g_variant_get(args, "(u)", &techs);
    GDEBUG("techs => 0x%02x", techs);
    ext->techs_changed = techs;
    ext->techs_changed_count++;
}

static
void
test_request_techs_release_fail(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRequestTechs* ext = test->ext;
    GError* error = NULL;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error));
    g_assert(g_error_matches(error, DBUS_SERVICE_ERROR,
        DBUS_SERVICE_ERROR_NOT_FOUND));
    g_error_free(error);

    g_assert_cmpuint(ext->techs_changed_count, == ,2);
    test_quit_later(test->loop);
}

static
void
test_request_techs_release_ok(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRequestTechs* ext = test->ext;
    NfcManager* manager = test->manager;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);
    g_assert(manager->techs & NFC_TECHNOLOGY_B);
    g_assert_cmpint(ext->techs_changed, == ,manager->techs);
    g_assert_cmpuint(ext->techs_changed_count, == ,2);

    /* Try again with the same id (and fail) */
    test_call_release_techs(test, ext->req_id, test_request_techs_release_fail);
}

static
void
test_request_techs_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRequestTechs* ext = test->ext;
    NfcManager* manager = test->manager;
    guint id = 0;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(u)", &id);
    g_variant_unref(var);

    GDEBUG("request id=%u", id);
    g_assert(id);
    g_assert_cmpint(manager->techs, == ,NFC_TECHNOLOGY_A);
    g_assert_cmpint(ext->techs_changed, == ,manager->techs);
    g_assert_cmpuint(ext->techs_changed_count, == ,1);
    g_assert_cmpuint(ext->req_id, == ,0);
    ext->req_id = id;

    /* Release the request */
    test_call_release_techs(test, ext->req_id, test_request_techs_release_ok);
}

static
void
test_request_techs_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    NfcManager* manager = test->manager;

    test->client = client;
    test_signal_subscribe(test, "TechsChanged", test_techs_changed_handler);
    g_assert(manager->techs & NFC_TECHNOLOGY_B);
    g_assert(!(manager->techs & NFC_TECHNOLOGY_F));

    /* Leave only NFC-A */
    test_call_request_techs(test, NFC_TECHNOLOGY_A, -1,
        test_request_techs_done);
}

static
void
test_request_techs(
    void)
{
    TestDataExtRequestTechs ext;
    TestData test;
    TestDBus* dbus;

    memset(&ext, 0, sizeof(ext));
    test_data_init(&test)->ext = &ext;
    dbus = test_dbus_new2(test_start, test_request_techs_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * register_host_service
 *==========================================================================*/

typedef struct test_data_ext_register_host_service {
    guint mode_changed_count;
    NFC_MODE mode_changed;
} TestDataExtRegisterHostService;

static
void
test_register_host_service_mode_changed_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRegisterHostService* ext = test->ext;
    guint mode = NFC_MODE_NONE;

    g_variant_get(args, "(u)", &mode);
    GDEBUG("mode => 0x%02x", mode);
    ext->mode_changed = mode;
    ext->mode_changed_count++;
}

static
void
test_register_host_service_unregister_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRegisterHostService* ext = test->ext;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);

    /* Mode has changed the second time after unregistration */
    g_assert_cmpuint(ext->mode_changed_count, == ,2);
    test_quit_later(test->loop);
}

static
void
test_register_host_service_fail(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRegisterHostService* ext = test->ext;
    GError* error = NULL;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error));
    g_assert(g_error_matches(error, DBUS_SERVICE_ERROR,
        DBUS_SERVICE_ERROR_ALREADY_EXISTS));
    g_error_free(error);

    /* Mode hasn't changed (change count is still 1) */
    g_assert_cmpuint(ext->mode_changed_count, == ,1);
    g_assert(ext->mode_changed & NFC_MODE_CARD_EMULATION);

    /* Unregister it */
    test_call_unregister_local_host_service(test, test_host_service_path,
        test_register_host_service_unregister_done);
}

static
void
test_register_host_service_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    TestDataExtRegisterHostService* ext = test->ext;
    GError* error = NULL;
    GVariant* ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
       result, &error);

    g_assert(ret);
    g_variant_unref(ret);

    /* Mode has changed (CE mode has been enabled) */
    g_assert_cmpuint(ext->mode_changed_count, == ,1);
    g_assert(ext->mode_changed & NFC_MODE_CARD_EMULATION);

    /* Second time it will fail */
    test_call_register_local_host_service(test, test_host_service_path,
        test_host_service_name, test_register_host_service_fail);
}

static
void
test_register_host_service_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->client = client;
    test_signal_subscribe(test, "ModeChanged",
        test_register_host_service_mode_changed_handler);
    test_call_register_local_host_service(test, test_host_service_path,
        test_host_service_name, test_register_host_service_done);
}

static
void
test_register_host_service(
    void)
{
    TestDataExtRegisterHostService ext;
    TestData test;
    TestDBus* dbus;

    memset(&ext, 0, sizeof(ext));
    test_data_init(&test)->ext = &ext;
    test.adapter->supported_modes |= NFC_MODE_CARD_EMULATION;
    dbus = test_dbus_new2(test_start, test_register_host_service_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * register_host_app
 *==========================================================================*/

static const char test_host_app_path[] = "/test_app";
static const char test_host_app_name[] = "TestApp";
static const guint8 test_host_app_aid_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
static const GUtilData test_host_app_aid = {
    TEST_ARRAY_AND_SIZE(test_host_app_aid_bytes)
};

static
void
test_register_host_app_unregister_done(
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
test_register_host_app_fail(
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
    test_call_unregister_local_host_app(test, test_host_app_path,
        test_register_host_app_unregister_done);
}

static
void
test_register_host_app_done(
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

    /* Second time it will fail */
    test_call_register_local_host_app(test, test_host_app_path,
        test_host_app_name, &test_host_app_aid, NFC_HOST_APP_FLAGS_NONE,
        test_register_host_app_fail);
}

static
void
test_register_host_app_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->client = client;
    test_call_register_local_host_app(test, test_host_app_path,
        test_host_app_name, &test_host_app_aid, NFC_HOST_APP_FLAGS_NONE,
        test_register_host_app_done);
}

static
void
test_register_host_app(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new2(test_start, test_register_host_app_start, &test);
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
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("stop"), test_stop);
    g_test_add_func(TEST_("client_gone"), test_client_gone);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_adapters"), test_get_adapters);
    g_test_add_func(TEST_("get_all2"), test_get_all2);
    g_test_add_func(TEST_("get_daemon_version"), test_get_daemon_version);
    g_test_add_func(TEST_("get_all3"), test_get_all3);
    g_test_add_func(TEST_("get_mode"), test_get_mode);
    g_test_add_func(TEST_("request_mode"), test_request_mode);
    g_test_add_func(TEST_("register_service"), test_register_service);
    g_test_add_func(TEST_("unregister_service_error"), test_unregister_svc_err);
    g_test_add_func(TEST_("adapter_added"), test_adapter_added);
    g_test_add_func(TEST_("adapter_removed"), test_adapter_removed);
    g_test_add_func(TEST_("get_all4"), test_get_all4);
    g_test_add_func(TEST_("get_techs"), test_get_techs);
    g_test_add_func(TEST_("request_techs"), test_request_techs);
    g_test_add_func(TEST_("register_host_service"), test_register_host_service);
    g_test_add_func(TEST_("register_host_app"), test_register_host_app);
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
