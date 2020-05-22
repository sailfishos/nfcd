/*
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2019-2020 Slava Monich <slava.monich@jolla.com>
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

#include "test_common.h"
#include "test_adapter.h"
#include "test_dbus.h"

#include "dbus_service/dbus_service.h"
#include "dbus_service/plugin.h"

#include "internal/nfc_manager_i.h"
#include "nfc_adapter.h"
#include "nfc_version.h"

#define NFC_DAEMON_INTERFACE "org.sailfishos.nfc.Daemon"

static TestOpt test_opt;
static GDBusConnection* test_server;

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
} TestData;

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
    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    test->loop = g_main_loop_new(NULL, TRUE);
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

    test_server = server;
    g_assert(nfc_manager_start(test->manager));
}

static
void
test_call(
    TestData* test,
    GDBusConnection* client,
    const char* method,
    GAsyncReadyCallback callback)
{
    g_dbus_connection_call(client, NULL, "/", NFC_DAEMON_INTERFACE,
        method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, test);
}

/*==========================================================================*
 * Stubs
 *==========================================================================*/

#define TEST_NAME_OWN_ID (1)

typedef struct test_bus_acquired_data {
    char* name;
    DBusServicePlugin* plugin;
    GBusAcquiredCallback bus_acquired;
    GBusNameAcquiredCallback name_acquired;
} TestBusAcquiredData;

static
gboolean
test_bus_acquired(
    gpointer user_data)
{
    TestBusAcquiredData* data = user_data;

    data->bus_acquired(test_server, data->name, data->plugin);
    data->name_acquired(test_server, data->name, data->plugin);
    return G_SOURCE_REMOVE;
}

static
void
test_bus_acquired_free(
    gpointer user_data)
{
    TestBusAcquiredData* data = user_data;

    g_free(data->name);
    g_free(data);
}

guint
dbus_service_name_own(
    DBusServicePlugin* plugin,
    const char* name,
    GBusAcquiredCallback bus_acquired,
    GBusNameAcquiredCallback name_acquired,
    GBusNameLostCallback name_lost)
{
    TestBusAcquiredData* data = g_new(TestBusAcquiredData, 1);

    data->plugin = plugin;
    data->name = g_strdup(name);
    data->bus_acquired = bus_acquired;
    data->name_acquired = name_acquired;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, test_bus_acquired, data,
        test_bus_acquired_free);
    return TEST_NAME_OWN_ID;
}

void
dbus_service_name_unown(
    guint id)
{
    g_assert(id == TEST_NAME_OWN_ID);
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
    g_assert(version >= 1);
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
    test_call((TestData*)test, client, "GetAll", test_get_all_done);
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
    test_server = NULL;
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
    g_assert(version >= 1);
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
    test_call((TestData*)test, client, "GetInterfaceVersion",
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
    test_call((TestData*)test, client, "GetAdapters",
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
    test_server = NULL;
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
    g_assert(version >= 1);
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
    test_call((TestData*)test, client, "GetAll2", test_get_all2_done);
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
    test_server = NULL;
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
    GDEBUG("version=%d", version);
    g_assert(version >= 1);
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
    test_call((TestData*)test, client, "GetDaemonVersion",
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
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/plugin/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_adapters"), test_get_adapters);
    g_test_add_func(TEST_("get_all2"), test_get_all2);
    g_test_add_func(TEST_("get_daemon_version"), test_get_daemon_version);
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
