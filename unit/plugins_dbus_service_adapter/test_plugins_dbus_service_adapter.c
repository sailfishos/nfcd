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
#include "test_target.h"
#include "test_dbus.h"

#include "dbus_service/dbus_service.h"

#include "internal/nfc_manager_i.h"
#include "nfc_plugins.h"
#include "nfc_adapter_p.h"
#include "nfc_adapter_impl.h"
#include "nfc_target_impl.h"
#include "nfc_tag.h"

#define NFC_ADAPTER_INTERFACE "org.sailfishos.nfc.Adapter"

static TestOpt test_opt;

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    DBusServiceAdapter* service;
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
    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    test->loop = g_main_loop_new(NULL, TRUE);
}

static
void
test_data_cleanup(
    TestData* test)
{
    nfc_adapter_unref(test->adapter);
    nfc_manager_unref(test->manager);
    dbus_service_adapter_free(test->service);
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
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    g_dbus_connection_call(client, NULL,
        dbus_service_adapter_path(test->service), NFC_ADAPTER_INTERFACE,
        method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, test);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    dbus_service_adapter_free(NULL);
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
    const char* path;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    path = dbus_service_adapter_path(test->service);
    g_assert(path);
    g_assert(path[0] == '/');
    g_assert_cmpstr(path + 1, ==, test->adapter->name);

    /* Can't register two D-Bus objects for the same path */
    g_assert(!dbus_service_adapter_new(test->adapter, server));
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
    dbus = test_dbus_new(test_basic_start, &test);
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
    gboolean enabled = FALSE, powered = TRUE, target_present = FALSE;
    guint modes, mode;
    gchar** tags = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(ibbuub^ao)", &version, &enabled, &powered,
        &modes, &mode, &target_present, &tags);
    GDEBUG("version=%d, enabled=%d, powered=%d, modes=%0x04X, mode=0x%04X, "
        "target_present=%d, %u tags", version, enabled, powered, modes, mode,
         target_present, g_strv_length(tags));
    g_assert(version >= 1);
    g_assert(enabled);
    g_assert(!powered);
    g_assert(!target_present);
    g_variant_unref(var);
    g_strfreev(tags);
    test_quit_later(test->loop);
}

static
void
test_get_all_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetAll", test_get_all_done);
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
    g_assert(version >= 1);
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
 * get_enabled
 *==========================================================================*/

static
void
test_get_enabled_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean enabled = FALSE;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(b)", &enabled);
    GDEBUG("enabled=%d", enabled);
    g_assert(enabled);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_enabled_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetEnabled", test_get_enabled_done);
}

static
void
test_get_enabled(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_enabled_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_powered
 *==========================================================================*/

static
void
test_get_powered_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean powered = FALSE;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(b)", &powered);
    GDEBUG("powered=%d", powered);
    g_assert(!powered);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_powered_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetPowered", test_get_powered_done);
}

static
void
test_get_powered(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_powered_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_supported_modes
 *==========================================================================*/

static
void
test_get_supported_modes_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint modes = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &modes);
    GDEBUG("modes=0x%04X", modes);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_supported_modes_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetSupportedModes", test_get_supported_modes_done);
}

static
void
test_get_supported_modes(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_supported_modes_start, &test);
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
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &mode);
    GDEBUG("mode=0x%04X", mode);
    g_assert(mode == 0);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_mode_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetMode", test_get_mode_done);
}

static
void
test_get_mode(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_mode_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_target_present
 *==========================================================================*/

static
void
test_get_target_present_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean target_present = TRUE;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(b)", &target_present);
    GDEBUG("target_present=%d", target_present);
    g_assert(!target_present);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_target_present_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetTargetPresent", test_get_target_present_done);
}

static
void
test_get_target_present(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_target_present_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_tags
 *==========================================================================*/

static
void
test_get_tags_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** tags = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(^ao)", &tags);
    g_assert(tags);
    GDEBUG("%u tag(s)", g_strv_length(tags));
    g_assert(g_strv_length(tags) == 2);
    g_variant_unref(var);
    g_strfreev(tags);
    test_quit_later_n(test->loop, 100); /* Allow everything to complete */
}

static
void
test_get_tags_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    NfcTarget* target;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);

    /* Add second tag after creating DBusServiceAdapter */
    target = test_target_new();
    g_assert(nfc_adapter_add_other_tag(test->adapter, target));
    nfc_target_unref(target);

    g_dbus_connection_call(client, NULL,
        dbus_service_adapter_path(test->service), NFC_ADAPTER_INTERFACE,
        "GetTags", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
        test_get_tags_done, test);
}

static
void
test_get_tags(
    void)
{
    TestData test;
    TestDBus* dbus;
    NfcTarget* target;

    test_data_init(&test);

    /* Add one tag before creating DBusServiceAdapter */
    target = test_target_new();
    g_assert(nfc_adapter_add_other_tag(test.adapter, target));
    nfc_target_unref(target);

    dbus = test_dbus_new(test_get_tags_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * enabled_changed
 *==========================================================================*/

static
void
test_enabled_changed_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean enabled = TRUE;

    g_variant_get(args, "(b)", &enabled);
    GDEBUG("enabled=%d", enabled);
    g_assert(!enabled);
    test_quit_later(test->loop);
}

static
void
test_enabled_changed_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_ADAPTER_INTERFACE, "EnabledChanged",
        dbus_service_adapter_path(test->service), NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_enabled_changed_handler,
        test, NULL));

    /* Disable the adapter */
    nfc_adapter_set_enabled(test->adapter, FALSE);
}

static
void
test_enabled_changed(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_enabled_changed_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * powered_changed
 *==========================================================================*/

static
void
test_powered_changed_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean powered = TRUE;

    g_variant_get(args, "(b)", &powered);
    GDEBUG("powered=%d", powered);
    g_assert(powered);
    test_quit_later(test->loop);
}

static
void
test_powered_changed_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_ADAPTER_INTERFACE, "PoweredChanged",
        dbus_service_adapter_path(test->service), NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_powered_changed_handler,
        test, NULL));

    /* Power up the adapter */
    g_assert(!test->adapter->powered);
    nfc_adapter_power_notify(test->adapter, TRUE, FALSE);
}

static
void
test_powered_changed(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_powered_changed_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * mode_changed
 *==========================================================================*/

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
    guint mode = 0;

    g_variant_get(args, "(u)", &mode);
    GDEBUG("mode=0x%04X", mode);
    g_assert(mode == NFC_MODE_READER_WRITER);
    test_quit_later(test->loop);
}

static
void
test_mode_changed_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_ADAPTER_INTERFACE, "ModeChanged",
        dbus_service_adapter_path(test->service), NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_mode_changed_handler,
        test, NULL));

    /* Change the adapter mode */
    g_assert(!test->adapter->mode);
    nfc_adapter_mode_notify(test->adapter, NFC_MODE_READER_WRITER, FALSE);
}

static
void
test_mode_changed(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_mode_changed_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * tag_added
 *==========================================================================*/

static
void
test_tag_added_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** tags = NULL;

    g_variant_get(args, "(^ao)", &tags);
    g_assert(tags);
    GDEBUG("%u tag(s)", g_strv_length(tags));
    g_assert(g_strv_length(tags) == 1);
    g_strfreev(tags);
    test_quit_later(test->loop);
}

static
void
test_tag_added_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    NfcTarget* target;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_ADAPTER_INTERFACE, "TagsChanged",
        dbus_service_adapter_path(test->service), NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_tag_added_handler,
        test, NULL));

    /* Add a tag */
    target = test_target_new();
    g_assert(nfc_adapter_add_other_tag(test->adapter, target));
    nfc_target_unref(target);
}

static
void
test_tag_added(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_tag_added_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * tag_removed
 *==========================================================================*/

static
void
test_tag_removed_handler(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** tags = NULL;

    g_variant_get(args, "(^ao)", &tags);
    g_assert(tags);
    GDEBUG("%u tag(s)", g_strv_length(tags));
    g_assert(g_strv_length(tags) == 0);
    g_strfreev(tags);
    test_quit_later(test->loop);
}

static
void
test_tag_removed_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    NfcTarget* target;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_ADAPTER_INTERFACE, "TagsChanged",
        dbus_service_adapter_path(test->service), NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_tag_removed_handler,
        test, NULL));

    /* Remove the tag */
    g_assert(test->adapter->tags[0]);
    target = nfc_target_ref(test->adapter->tags[0]->target);
    nfc_target_gone(target);
    nfc_target_unref(target);
}

static
void
test_tag_removed(
    void)
{
    TestData test;
    TestDBus* dbus;
    NfcTarget* target;

    test_data_init(&test);

    target = test_target_new();
    g_assert(nfc_adapter_add_other_tag(test.adapter, target));
    nfc_target_unref(target);

    dbus = test_dbus_new(test_tag_removed_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/adapter/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_enabled"), test_get_enabled);
    g_test_add_func(TEST_("get_powered"), test_get_powered);
    g_test_add_func(TEST_("get_supported_modes"), test_get_supported_modes);
    g_test_add_func(TEST_("get_mode"), test_get_mode);
    g_test_add_func(TEST_("get_target_present"), test_get_target_present);
    g_test_add_func(TEST_("get_tags"), test_get_tags);
    g_test_add_func(TEST_("enabled_changed"), test_enabled_changed);
    g_test_add_func(TEST_("powered_changed"), test_powered_changed);
    g_test_add_func(TEST_("mode_changed"), test_mode_changed);
    g_test_add_func(TEST_("tag_added"), test_tag_added);
    g_test_add_func(TEST_("tag_removed"), test_tag_removed);
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
