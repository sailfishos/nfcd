/*
 * Copyright (C) 2022 Jolla Ltd.
 * Copyright (C) 2022 Slava Monich <slava.monich@jolla.com>
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

#include "settings/settings_plugin.h"
#include "settings/plugin.h"
#include "internal/nfc_manager_i.h"

#include <nfc_plugin_impl.h>
#include <nfc_config.h>

#include <glib/gstdio.h>

#ifdef HAVE_DBUSACCESS
#include <dbusaccess_policy.h>
#include <dbusaccess_peer.h>
static DA_ACCESS test_access = DA_ACCESS_ALLOW;
#define test_allow_calls() (test_access = DA_ACCESS_ALLOW)
#define test_deny_calls() (test_access = DA_ACCESS_DENY)
#else
#define test_allow_calls()
#define test_deny_calls()
#endif

#include <gutil_idlepool.h>

#include "test_common.h"
#include "test_dbus.h"

#define TMP_DIR_TEMPLATE                 "test_XXXXXX"
#define TEST_PLUGIN_NAME                 "test"
#define TEST_DBUS_NEARD_PLUGIN_NAME      "dbus_neard"

#define SETTINGS_CONFIG_DEFAULTS_FILE    "defaults.conf"
#define SETTINGS_CONFIG_DEFAULTS_DIR     "defaults.d"
#define SETTINGS_STORAGE_FILE            "settings"
#define SETTINGS_STORAGE_DIR_PERM        0700
#define SETTINGS_GROUP                   "Settings"
#define SETTINGS_KEY_ENABLED             "Enabled"
#define SETTINGS_KEY_ALWAYS_ON           "AlwaysOn"

#define SETTINGS_DBUS_PATH               "/"
#define SETTINGS_DBUS_INTERFACE          "org.sailfishos.nfc.Settings"
#define SETTINGS_DBUS_INTERFACE_VERSION  (2)

#define SETTINGS_ERROR_(e)               "org.sailfishos.nfc.settings.Error." e
#define SETTINGS_ERROR_ACCESS_DENIED     SETTINGS_ERROR_("AccessDenied")
#define SETTINGS_ERROR_UNKNOWN_PLUGIN    SETTINGS_ERROR_("UnknownPlugin")
#define SETTINGS_ERROR_UNKNOWN_KEY       SETTINGS_ERROR_("UnknownKey")
#define SETTINGS_ERROR_UNKNOWN_VALUE     SETTINGS_ERROR_("UnknownValue")
#define SETTINGS_ERROR_FAILED            SETTINGS_ERROR_("Failed")

typedef struct test_bus_name {
    guint id;
    guint acquire_id;
    char* name;
    SettingsPlugin* plugin;
    GBusAcquiredCallback bus_acquired;
    GBusNameAcquiredCallback name_acquired;
    GBusNameLostCallback name_lost;
} TestBusName;

typedef struct test_data {
    const char* default_config_dir;
    const char* default_storage_dir;
    char* config_dir;
    char* storage_dir;
    char* storage_file;
    GMainLoop* loop;
    NfcManager* manager;
    GDBusConnection* client; /* Owned by TestDBus */
    guint flags;

#define TEST_ENABLED_CHANGED_SIGNAL_FLAG 0x01
#define TEST_PLUGIN_VALUE_CHANGED_SIGNAL_FLAG 0x02

} TestData;

typedef
void
TestFunc(
    TestData* test);

static TestOpt test_opt;
static TestBusName* test_bus_name;
static GDBusConnection* test_server;
static SettingsPlugin* test_plugin;
static GUtilIdlePool* test_pool;
static const char* dbus_sender = ":1.0";

static NfcPlugin* test_plugin_create(void);
static NfcPlugin* test_dbus_neard_plugin_create(void);
static const NfcPluginDesc NFC_PLUGIN_DESC(test) = {
    TEST_PLUGIN_NAME, "Test", NFC_CORE_VERSION,
    test_plugin_create, NULL, 0
};
static const NfcPluginDesc NFC_PLUGIN_DESC(dbus_neard) = {
    TEST_DBUS_NEARD_PLUGIN_NAME, "Dummy neard D-Bus plugin", NFC_CORE_VERSION,
    test_dbus_neard_plugin_create, NULL, 0
};

static
void
test_data_init_with_plugins(
    TestData* test,
    const char* config,
    TestFunc prestart,
    const NfcPluginDesc* const plugins[])
{
    NfcPluginsInfo pi;
    SettingsPluginClass* klass = g_type_class_ref(SETTINGS_PLUGIN_TYPE);

    memset(test, 0, sizeof(*test));
    memset(&pi, 0, sizeof(pi));

    g_assert(klass);
    test->config_dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    test->storage_dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    test->storage_file = g_build_filename(test->storage_dir,
        SETTINGS_STORAGE_FILE, NULL);
    test->default_config_dir = klass->config_dir;
    test->default_storage_dir = klass->storage_dir;
    klass->config_dir = test->config_dir;
    klass->storage_dir = test->storage_dir;
    g_type_class_unref(klass);

    if (config) {
        GDEBUG("%s\n%s", test->storage_file, config);
        g_assert(g_file_set_contents(test->storage_file, config, -1, NULL));
    }

    if (prestart) {
        prestart(test);
    }

    pi.builtins = plugins;
    g_assert((test->manager = nfc_manager_new(&pi)) != NULL);
    test->loop = g_main_loop_new(NULL, TRUE);
    test_pool = gutil_idle_pool_new();
}

static
void
test_data_init4(
    TestData* test,
    const char* config,
    TestFunc prestart)
{
    static const NfcPluginDesc* const test_plugins4[] = {
        &NFC_PLUGIN_DESC(settings),
        &NFC_PLUGIN_DESC(test),
        &NFC_PLUGIN_DESC(dbus_neard),
        NULL
    };

    test_data_init_with_plugins(test, config, prestart, test_plugins4);
}

static
void
test_data_init3(
    TestData* test,
    const char* config,
    TestFunc prestart)
{
    static const NfcPluginDesc* const test_plugins3[] = {
        &NFC_PLUGIN_DESC(settings),
        &NFC_PLUGIN_DESC(test),
        NULL
    };

    test_data_init_with_plugins(test, config, prestart, test_plugins3);
}

static
void
test_data_init2(
    TestData* test,
    const char* config)
{
    test_data_init3(test, config, NULL);
}

static
void
test_data_init(
    TestData* test,
    const char* config)
{
    static const NfcPluginDesc* const test_plugins[] = {
        &NFC_PLUGIN_DESC(settings),
        NULL
    };

    test_data_init_with_plugins(test, config, NULL, test_plugins);
}

static
void
test_data_cleanup(
    TestData* test)
{
    SettingsPluginClass* klass = g_type_class_ref(SETTINGS_PLUGIN_TYPE);
    char* config = NULL;

    klass->config_dir = test->default_config_dir;
    klass->storage_dir = test->default_storage_dir;
    g_type_class_unref(klass);

    test_server = NULL;
    gutil_idle_pool_destroy(test_pool);
    test_pool = NULL;
    nfc_manager_stop(test->manager, 0);
    nfc_manager_unref(test->manager);
    g_main_loop_unref(test->loop);

    /* Dump the config file if it's present */
    if (g_file_get_contents(test->storage_file, &config, NULL, NULL)) {
        GDEBUG("%s\n%s", test->storage_file, config);
        g_free(config);
    }

    /* And delete the temporary files */
    g_assert_cmpint(test_rmdir(test->config_dir), == ,0);
    g_assert_cmpint(test_rmdir(test->storage_dir), == ,0);
    g_free(test->storage_file);
    g_free(test->storage_dir);
    g_free(test->config_dir);
    memset(test, 0, sizeof(*test));
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
test_dbus_call(
    TestData* test,
    GDBusConnection* client,
    const char* method,
    GVariant* parameters,
    GAsyncReadyCallback callback)
{
    g_dbus_connection_call(client, NULL, SETTINGS_DBUS_PATH,
        SETTINGS_DBUS_INTERFACE, method, parameters, NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, test);
}

static
void
test_call(
    TestData* test,
    GDBusConnection* client,
    const char* method,
    GAsyncReadyCallback callback)
{
    test_dbus_call(test, client, method, NULL, callback);
}

static
void
test_call_set_enabled(
    TestData* test,
    GDBusConnection* client,
    gboolean enabled,
    GAsyncReadyCallback callback)
{
    test_dbus_call(test, client, "SetEnabled",
        g_variant_new("(b)", enabled), callback);
}

static
void
test_call_get_plugin_settings(
    TestData* test,
    GDBusConnection* client,
    const char* plugin,
    GAsyncReadyCallback callback)
{
    test_dbus_call(test, client, "GetPluginSettings",
        g_variant_new("(s)", plugin), callback);
}

static
void
test_call_get_plugin_value(
    TestData* test,
    GDBusConnection* client,
    const char* plugin,
    const char* key,
    GAsyncReadyCallback callback)
{
    test_dbus_call(test, client, "GetPluginValue",
        g_variant_new("(ss)", plugin, key), callback);
}

static
void
test_call_set_plugin_value(
    TestData* test,
    GDBusConnection* client,
    const char* plugin,
    const char* key,
    GVariant* value,
    GAsyncReadyCallback callback)
{
    test_dbus_call(test, client, "SetPluginValue",
        g_variant_new("(ss@v)", plugin, key, value), callback);
}

static
void
test_done_with_error(
    GObject* object,
    GAsyncResult* result,
    TestData* test,
    const char* expected_error)
{
    GError* error = NULL;
    char* remote_error;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error));
    g_assert(error);
    g_assert(g_dbus_error_is_remote_error(error));
    remote_error = g_dbus_error_get_remote_error(error);
    GDEBUG("%s", remote_error);
    g_assert_cmpstr(remote_error, == ,expected_error);
    g_error_free(error);
    g_free(remote_error);
    test_quit_later(test->loop);
}

static
void
test_done_access_denied(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    test_done_with_error(object, result, (TestData*) user_data,
        SETTINGS_ERROR_ACCESS_DENIED);
}

static
void
test_get_plugin_string_value_check(
    GDBusConnection* client,
    GAsyncResult* result,
    TestData* test,
    const char* expected_value)
{
    GError* error = NULL;
    GVariant* svalue = NULL;
    GVariant* value = NULL;
    GVariant* var = g_dbus_connection_call_finish(client, result, &error);

    g_assert(!error);
    g_assert(var);
    g_variant_get(var, "(@v)", &value);
    g_assert(g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT));
    svalue = g_variant_get_variant(value);
    g_assert(g_variant_is_of_type(svalue, G_VARIANT_TYPE_STRING));
    GDEBUG("%s", g_variant_get_string(svalue, NULL));
    g_assert_cmpstr(g_variant_get_string(svalue, NULL), == ,expected_value);

    g_variant_unref(svalue);
    g_variant_unref(value);
    g_variant_unref(var);
}

static
void
test_get_plugin_boolean_value_check(
    GDBusConnection* client,
    GAsyncResult* result,
    TestData* test,
    gboolean expected_value)
{
    GError* error = NULL;
    GVariant* bvalue = NULL;
    GVariant* value = NULL;
    GVariant* var = g_dbus_connection_call_finish(client, result, &error);

    g_assert(!error);
    g_assert(var);
    g_variant_get(var, "(@v)", &value);
    g_assert(g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT));
    bvalue = g_variant_get_variant(value);
    g_assert(g_variant_is_of_type(bvalue, G_VARIANT_TYPE_BOOLEAN));
    GDEBUG("%s", g_variant_get_boolean(bvalue) ? "true" : "false");
    g_assert_cmpint(g_variant_get_boolean(bvalue), == ,expected_value);

    g_variant_unref(bvalue);
    g_variant_unref(value);
    g_variant_unref(var);
}

static
void
test_get_plugin_string_value_done(
    GObject* client,
    GAsyncResult* result,
    TestData* test,
    const char* expected_value)
{
    test_get_plugin_string_value_check(G_DBUS_CONNECTION(client), result,
        test, expected_value);
    test_quit_later(test->loop);
}

static
void
test_get_plugin_boolean_value_done(
    GObject* client,
    GAsyncResult* result,
    TestData* test,
    gboolean expected_value)
{
    test_get_plugin_boolean_value_check(G_DBUS_CONNECTION(client), result,
        test, expected_value);
    test_quit_later(test->loop);
}

static
void
test_get_enabled_check(
    GDBusConnection* client,
    GAsyncResult* result,
    TestData* test,
    gboolean expected)
{
    gboolean enabled = 0;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(client, result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(b)", &enabled);
    GDEBUG("enabled=%d", enabled);
    g_assert(enabled == expected);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_enabled_done(
    GObject* client,
    GAsyncResult* result,
    TestData* test,
    gboolean expected)
{
    test_get_enabled_check(G_DBUS_CONNECTION(client), result, test, expected);
    test_quit_later(test->loop);
}

static
void
test_call_ok_check(
    GDBusConnection* client,
    GAsyncResult* result)
{
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(client, result, &error);

    g_assert(!error);
    g_assert(var);
    g_variant_unref(var);
}

static
void
test_call_ok_done(
    GObject* client,
    GAsyncResult* result,
    TestData* test)
{
    test_call_ok_check(G_DBUS_CONNECTION(client), result);
    test_quit_later(test->loop);
}

static
void
test_access_denied(
    TestDBusStartFunc start)
{
    TestData test;
    TestDBus* dbus;

    test_deny_calls();
    test_data_init(&test, NULL);
    dbus = test_dbus_new2(test_start, start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

static
void
test_done_unknown_plugin(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    test_done_with_error(object, result, (TestData*) user_data,
        SETTINGS_ERROR_UNKNOWN_PLUGIN);
}

static
void
test_done_unknown_key(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    test_done_with_error(object, result, (TestData*) user_data,
        SETTINGS_ERROR_UNKNOWN_KEY);
}

static
void
test_done_failed(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    test_done_with_error(object, result, (TestData*) user_data,
        SETTINGS_ERROR_FAILED);
}

static
void
test_check_config_value(
    GKeyFile* config,
    const char* group,
    const char* key,
    const char* expected_value)
{
    char* value;

    value = g_key_file_get_value(config, group, key, NULL);
    g_assert_cmpstr(value, == ,expected_value);
    g_free(value);
}

static
void
test_check_config_file_value(
    TestData* test,
    const char* group,
    const char* key,
    const char* expected_value)
{
    GKeyFile* config = g_key_file_new();

    g_assert(g_key_file_load_from_file(config, test->storage_file, 0, NULL));
    test_check_config_value(config, group, key, expected_value);
    g_key_file_unref(config);
}

static
void
test_normal_run(
    void (*init)(TestData* test, const char* config),
    const char* config,
    TestDBusStartFunc start)
{
    TestData test;
    TestDBus* dbus;

    test_allow_calls();
    init(&test, config);
    dbus = test_dbus_new2(test_start, start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

static
void
test_normal(
    TestDBusStartFunc start)
{
    test_normal_run(test_data_init, NULL, start);
}

static
void
test_normal2(
    const char* config,
    TestDBusStartFunc start)
{
    test_normal_run(test_data_init2, config, start);
}

static
void
test_normal3(
    const char* config,
    TestFunc prestart,
    TestDBusStartFunc start)
{
    TestData test;
    TestDBus* dbus;

    test_allow_calls();
    test_data_init3(&test, config, prestart);
    dbus = test_dbus_new2(test_start, start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

static
void
test_normal4(
    const char* config,
    TestFunc prestart,
    TestDBusStartFunc start)
{
    TestData test;
    TestDBus* dbus;

    test_allow_calls();
    test_data_init4(&test, config, prestart);
    dbus = test_dbus_new2(test_start, start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Test plugin
 *==========================================================================*/

typedef NfcPluginClass TestPluginClass;
typedef struct test_plugin {
    NfcPlugin plugin;
    char* value;
    char* value2;
} TestPlugin;

#define TEST_PLUGIN_KEY "key"
#define TEST_PLUGIN_KEY2 "key2" /* Doesn't have a default */
#define TEST_PLUGIN_DEFAULT_VALUE "value"
#define TEST_PLUGIN_NON_DEFAULT_VALUE "non-default"
static void test_plugin_config_init(NfcConfigurableInterface* iface);
G_DEFINE_TYPE_WITH_CODE(TestPlugin, test_plugin, NFC_TYPE_PLUGIN,
G_IMPLEMENT_INTERFACE(NFC_TYPE_CONFIGURABLE, test_plugin_config_init))
#define TEST_TYPE_PLUGIN test_plugin_get_type()
#define TEST_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_PLUGIN, TestPlugin))
#define TEST_CONFIG_VALUE_CHANGED_NAME "test-plugin-config-value-changed"
enum test_plugin_signal {
     TEST_CONFIG_VALUE_CHANGED,
     TEST_PLUGIN_SIGNALS
};
static guint test_plugin_signals[TEST_PLUGIN_SIGNALS] = { 0 };

static
NfcPlugin*
test_plugin_create(
    void)
{
    return g_object_new(TEST_TYPE_PLUGIN, NULL);
}

static
gboolean
test_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    return TRUE;
}

static
void
test_plugin_init(
    TestPlugin* self)
{
    self->value = g_strdup(TEST_PLUGIN_DEFAULT_VALUE);
}

static
void
test_plugin_finalize(
    GObject* plugin)
{
    TestPlugin* self = TEST_PLUGIN(plugin);

    g_free(self->value);
    g_free(self->value2);
    G_OBJECT_CLASS(test_plugin_parent_class)->finalize(plugin);
}

static
void
test_plugin_class_init(
    NfcPluginClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = test_plugin_finalize;
    klass->start = test_plugin_start;
    test_plugin_signals[TEST_CONFIG_VALUE_CHANGED] =
        g_signal_new(TEST_CONFIG_VALUE_CHANGED_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST |
            G_SIGNAL_DETAILED, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VARIANT);
}

static
const char* const*
test_plugin_config_get_keys(
    NfcConfigurable* config)
{
    static const char* const test_plugin_keys[] = {
        TEST_PLUGIN_KEY, TEST_PLUGIN_KEY2, NULL
    };

    return test_plugin_keys;
}

static
GVariant*
test_plugin_config_get_value(
    NfcConfigurable* config,
    const char* key)
{
    TestPlugin* self = TEST_PLUGIN(config);

    /* OK to return a floating reference */
    if (!g_strcmp0(key, TEST_PLUGIN_KEY)) {
        return g_variant_new_string(self->value);
    } else if (!g_strcmp0(key, TEST_PLUGIN_KEY2)) {
        if (self->value2) {
            return g_variant_new_string(self->value2);
        }
    }
    return NULL;
}

static
gboolean
test_plugin_config_set_value(
    NfcConfigurable* config,
    const char* key,
    GVariant* value)
{
    TestPlugin* self = TEST_PLUGIN(config);
    gboolean ok = FALSE;

    if (!g_strcmp0(key, TEST_PLUGIN_KEY)) {
        const char* newval = TEST_PLUGIN_DEFAULT_VALUE;

        if (!value) {
            ok = TRUE;
        } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            newval = g_variant_get_string(value, NULL);
            ok = TRUE;
        }

        if (ok && g_strcmp0(self->value, newval)) {
            GDEBUG("%s: %s => %s", key, self->value, newval);
            g_free(self->value);
            self->value = g_strdup(newval);
            g_signal_emit(self, test_plugin_signals
                [TEST_CONFIG_VALUE_CHANGED], g_quark_from_string(key),
                key, value);
        }
    } else if (!g_strcmp0(key, TEST_PLUGIN_KEY2)) {
        const char* newval = NULL;

        if (!value) {
            ok = TRUE;
        } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            newval = g_variant_get_string(value, NULL);
            ok = TRUE;
        }

        if (ok && g_strcmp0(self->value, newval)) {
            GDEBUG("%s: %s => %s", key, self->value2, newval);
            g_free(self->value2);
            self->value2 = g_strdup(newval);
            g_signal_emit(self, test_plugin_signals
                [TEST_CONFIG_VALUE_CHANGED], g_quark_from_string(key),
                key, value);
        }
    }
    return ok;
}

static
gulong
test_plugin_config_add_change_handler(
    NfcConfigurable* config,
    const char* key,
    NfcConfigChangeFunc func,
    void* user_data)
{
    return g_signal_connect_closure_by_id(TEST_PLUGIN(config),
        test_plugin_signals[TEST_CONFIG_VALUE_CHANGED],
        key ? g_quark_from_string(key) : 0,
        g_cclosure_new(G_CALLBACK(func), user_data, NULL), FALSE);
}

static
void
test_plugin_config_init(
    NfcConfigurableInterface* iface)
{
    iface->get_keys = test_plugin_config_get_keys;
    iface->get_value = test_plugin_config_get_value;
    iface->set_value = test_plugin_config_set_value;
    iface->add_change_handler = test_plugin_config_add_change_handler;
}

/*==========================================================================*
 * Dummy dbus_neard plugin (to test migration)
 *==========================================================================*/

typedef NfcPluginClass TestDBusNeardPluginClass;
typedef struct test_dbus_neard_plugin {
    NfcPlugin plugin;
    gboolean value;
} TestDBusNeardPlugin;

#define DBUS_NEARD_PLUGIN_KEY "BluetoothStaticHandover"
#define DBUS_NEARD_PLUGIN_DEFAULT_VALUE FALSE

static void test_dbus_neard_plugin_config_init(NfcConfigurableInterface* intf);
G_DEFINE_TYPE_WITH_CODE(TestDBusNeardPlugin, test_dbus_neard_plugin,
NFC_TYPE_PLUGIN, G_IMPLEMENT_INTERFACE(NFC_TYPE_CONFIGURABLE,
test_dbus_neard_plugin_config_init))
#define TEST_TYPE_DBUS_NEARD_PLUGIN (test_dbus_neard_plugin_get_type())
#define TEST_DBUS_NEARD_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_DBUS_NEARD_PLUGIN, TestDBusNeardPlugin))
#define TEST_DBUS_NEARD_CONFIG_VALUE_CHANGED_NAME \
        "test-dbus_neard-plugin-config-value-changed"
enum test_dbus_neard_plugin_signal {
     TEST_DBUS_NEARD_CONFIG_VALUE_CHANGED,
     TEST_DBUS_NEARD_SIGNALS
};
static guint test_dbus_neard_plugin_signals[TEST_DBUS_NEARD_SIGNALS] = { 0 };

static
NfcPlugin*
test_dbus_neard_plugin_create(
    void)
{
    return g_object_new(TEST_TYPE_DBUS_NEARD_PLUGIN, NULL);
}

static
void
test_dbus_neard_plugin_init(
    TestDBusNeardPlugin* self)
{
    self->value = DBUS_NEARD_PLUGIN_DEFAULT_VALUE;
}

static
void
test_dbus_neard_plugin_class_init(
    NfcPluginClass* klass)
{
    klass->start = test_plugin_start; /* Reusable */
    test_dbus_neard_plugin_signals[TEST_DBUS_NEARD_CONFIG_VALUE_CHANGED] =
        g_signal_new(TEST_DBUS_NEARD_CONFIG_VALUE_CHANGED_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST |
            G_SIGNAL_DETAILED, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VARIANT);
}

static
const char* const*
test_dbus_neard_config_get_keys(
    NfcConfigurable* config)
{
    static const char* const test_dbus_neard_plugin_keys[] = {
        DBUS_NEARD_PLUGIN_KEY, NULL
    };

    return test_dbus_neard_plugin_keys;
}

static
GVariant*
test_dbus_neard_config_get_value(
    NfcConfigurable* config,
    const char* key)
{
    /* OK to return a floating reference */
    if (!g_strcmp0(key, DBUS_NEARD_PLUGIN_KEY)) {
        return g_variant_new_boolean(TEST_DBUS_NEARD_PLUGIN(config)->value);
    }
    return NULL;
}

static
gboolean
test_dbus_neard_config_set_value(
    NfcConfigurable* config,
    const char* key,
    GVariant* value)
{
    gboolean ok = FALSE;

    if (!g_strcmp0(key, DBUS_NEARD_PLUGIN_KEY)) {
        TestDBusNeardPlugin* self = TEST_DBUS_NEARD_PLUGIN(config);
        gboolean newval = DBUS_NEARD_PLUGIN_DEFAULT_VALUE;

        if (!value) {
            ok = TRUE;
        } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
            newval = g_variant_get_boolean(value);
            ok = TRUE;
        }

        if (ok && self->value != newval) {
            GDEBUG("%s: %s => %s", key, self->value ? "true" : "false",
                newval ? "true" : "false");
            self->value = newval;
            g_signal_emit(self, test_dbus_neard_plugin_signals
                [TEST_DBUS_NEARD_CONFIG_VALUE_CHANGED],
                g_quark_from_string(key), key, value);
        }
    }
    return ok;
}

static
gulong
test_dbus_neard_config_add_change_handler(
    NfcConfigurable* config,
    const char* key,
    NfcConfigChangeFunc func,
    void* user_data)
{
    return g_signal_connect_closure_by_id(TEST_DBUS_NEARD_PLUGIN(config),
        test_dbus_neard_plugin_signals[TEST_DBUS_NEARD_CONFIG_VALUE_CHANGED],
        key ? g_quark_from_string(key) : 0,
        g_cclosure_new(G_CALLBACK(func), user_data, NULL), FALSE);
}

static
void
test_dbus_neard_plugin_config_init(
    NfcConfigurableInterface* iface)
{
    iface->get_keys = test_dbus_neard_config_get_keys;
    iface->get_value = test_dbus_neard_config_get_value;
    iface->set_value = test_dbus_neard_config_set_value;
    iface->add_change_handler = test_dbus_neard_config_add_change_handler;
}

/*==========================================================================*
 * Stubs
 *==========================================================================*/

#define TEST_NAME_OWN_ID (1)
#define TEST_NAME_WATCH_ID (2)

static
gboolean
test_bus_acquired(
    gpointer user_data)
{
    TestBusName* data = user_data;

    g_assert(test_server);
    data->acquire_id = 0;
    data->bus_acquired(test_server, data->name, data->plugin);
    data->name_acquired(test_server, data->name, data->plugin);
    return G_SOURCE_REMOVE;
}

guint
settings_plugin_name_own(
    SettingsPlugin* plugin,
    const char* name,
    GBusAcquiredCallback bus_acquired,
    GBusNameAcquiredCallback name_acquired,
    GBusNameLostCallback name_lost)
{
    TestBusName* data = g_new(TestBusName, 1);

    data->plugin = test_plugin = plugin;
    data->id = TEST_NAME_OWN_ID;
    data->name = g_strdup(name);
    data->bus_acquired = bus_acquired;
    data->name_acquired = name_acquired;
    data->name_lost = name_lost;
    data->acquire_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
        test_bus_acquired, data, NULL);

    g_assert(!test_bus_name); /* Only one is expected */
    test_bus_name = data;
    return data->id;
}

void
settings_plugin_name_unown(
    guint id)
{
    TestBusName* data = test_bus_name;

    g_assert(test_plugin);
    g_assert(test_bus_name);

    g_assert(data->plugin == test_plugin);
    g_assert_cmpint(data->id, == ,id);
    if (data->acquire_id) {
        g_source_remove(data->acquire_id);
    }
    g_free(data->name);
    g_free(data);

    test_bus_name = NULL;
}

const gchar*
g_dbus_method_invocation_get_sender(
    GDBusMethodInvocation* call)
{
    return dbus_sender;
}

guint
g_bus_watch_name_on_connection(
    GDBusConnection* connection,
    const gchar* name,
    GBusNameWatcherFlags flags,
    GBusNameAppearedCallback name_appeared_handler,
    GBusNameVanishedCallback name_vanished_handler,
    gpointer user_data,
    GDestroyNotify user_data_free_func)
{
    g_assert_cmpstr(name, == ,dbus_sender);
    return TEST_NAME_WATCH_ID;
}

void
g_bus_unwatch_name(
    guint watcher_id)
{
    g_assert_cmpuint(watcher_id, == ,TEST_NAME_WATCH_ID);
}

#ifdef HAVE_DBUSACCESS

struct da_policy {
    gint refcount;
};

DAPeer*
da_peer_get(
    DA_BUS bus,
    const char* name)
{
    gsize name_len = strlen(name);
    DAPeer* peer = g_malloc0(sizeof(DAPeer) + name_len + 1);
    char* name_copy = (char*)(peer + 1);

    memcpy(name_copy, name, name_len);
    peer->name = name_copy;
    gutil_idle_pool_add(test_pool, peer, g_free);
    return peer;
}

DAPolicy*
da_policy_new_full(
    const char* spec,
    const DA_ACTION* actions)
{
    DAPolicy* policy = g_new0(DAPolicy, 1);

    g_atomic_int_set(&policy->refcount, 1);
    return policy;
}

void
da_policy_unref(
    DAPolicy* policy)
{
    if (policy && g_atomic_int_dec_and_test(&policy->refcount)) {
        g_free(policy);
    }
}

DA_ACCESS
da_policy_check(
    const DAPolicy* policy,
    const DACred* cred,
    guint action,
    const char* arg,
    DA_ACCESS def)
{
    GDEBUG("%s action %u", (test_access == DA_ACCESS_ALLOW) ?
        "Allowing" : "Not allowing", action);
    return test_access;
}

#endif /* HAVE_DBUSACCESS */

/*==========================================================================*
 * name_lost
 *==========================================================================*/

static
void
test_name_lost_done(
    NfcManager* manager,
    void* user_data)
{
    TestData* test = user_data;

    GDEBUG("Done");
    test_quit_later(test->loop);
}

static
void
test_name_lost_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test_data)
{
    TestData* test = test_data;
    TestBusName* name = test_bus_name;
    gulong id = nfc_manager_add_stopped_handler(test->manager,
        test_name_lost_done, test);

    g_assert(test_bus_name);
    g_assert(test_server);
    name->name_lost(test_server, name->name, name->plugin);

    /* test_name_lost_done() is already invoked by now */
    nfc_manager_remove_handler(test->manager, id);
}

static
void
test_name_lost(
    void)
{
    test_normal(test_name_lost_start);
}

/*==========================================================================*
 * defaults/load
 *==========================================================================*/

static
void
test_defaults_load_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    test_get_plugin_string_value_done(object, result, user_data, "foo");
}

static
void
test_defaults_load_changed(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;
    GKeyFile* conf = g_key_file_new();
    GDBusConnection* client = G_DBUS_CONNECTION(object);
    GVariant* var = g_dbus_connection_call_finish(client, result, &error);

    g_assert(test->manager->enabled);
    g_assert(var);
    g_assert(!error);
    g_variant_unref(var);

    /* Make sure the new value is saved */
    g_assert(g_key_file_load_from_file(conf, test->storage_file, 0, NULL));
    g_assert(g_key_file_get_boolean(conf, SETTINGS_GROUP, SETTINGS_KEY_ENABLED,
        NULL));
    g_key_file_unref(conf);

    /* And query the plugin's value */
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        test_defaults_load_done);
}

static
void
test_defaults_load_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    /* Verify that defaults have been applied */
    g_assert(!test->manager->enabled);

    /* Enable it */
    test_call_set_enabled(test, client, TRUE, test_defaults_load_changed);
}

static
void
test_defaults_load_prestart(
    TestData* test)
{
    char* defaults_file = g_build_filename(test->config_dir,
        SETTINGS_CONFIG_DEFAULTS_FILE, NULL);
    char* override_dir = g_build_filename(test->config_dir,
        SETTINGS_CONFIG_DEFAULTS_DIR, NULL);
    static const char defaults[] =
        "[" SETTINGS_GROUP "]\n"
        SETTINGS_KEY_ENABLED "=false\n"
        "[" TEST_PLUGIN_NAME "]\n"
        TEST_PLUGIN_KEY "='foo'\n";

    /* Create empty override directory */
    g_assert_cmpint(g_mkdir(override_dir, SETTINGS_STORAGE_DIR_PERM), == ,0);

    /* Write the defaults file */
    GDEBUG("%s\n%s", defaults_file, defaults);
    g_assert(g_file_set_contents(defaults_file, defaults, -1, NULL));

    g_free(defaults_file);
    g_free(override_dir);
}

static
void
test_defaults_load(
    void)
{
    test_normal3(NULL, test_defaults_load_prestart,
        test_defaults_load_start);
}

/*==========================================================================*
 * defaults/override
 *==========================================================================*/

static
void
test_defaults_override_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    /* Since all values are default, there was need to save the settings */
    g_assert(!g_file_test(test->storage_file, G_FILE_TEST_EXISTS));
    test_get_plugin_string_value_done(object, result, test, "bar");
}

static
void
test_defaults_override_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    /* Verify the state */
    g_assert(test->manager->enabled);
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        test_defaults_override_done);
}

static
void
test_defaults_override_prestart(
    TestData* test)
{
    char* defaults_file = g_build_filename(test->config_dir,
        SETTINGS_CONFIG_DEFAULTS_FILE, NULL);
    char* override_dir = g_build_filename(test->config_dir,
        SETTINGS_CONFIG_DEFAULTS_DIR, NULL);
    char* override_file = g_build_filename(override_dir, "override", NULL);
    static const char defaults[] =
        "[" SETTINGS_GROUP "]\n"
        SETTINGS_KEY_ENABLED "=false\n"
        "[" TEST_PLUGIN_NAME "]\n"
        TEST_PLUGIN_KEY "='foo'\n"
        "invalid-key=false\n";
    static const char override[] =
        "[" SETTINGS_GROUP "]\n"
        SETTINGS_KEY_ENABLED "=true\n"
        "[" TEST_PLUGIN_NAME "]\n"
        TEST_PLUGIN_KEY "='bar'\n"
        "[whatever]\n"
        "something=false\n";

    GDEBUG("%s\n%s", defaults_file, defaults);
    GDEBUG("%s\n%s", override_file, override);
    g_assert_cmpint(g_mkdir(override_dir, SETTINGS_STORAGE_DIR_PERM), == ,0);
    g_assert(g_file_set_contents(defaults_file, defaults, -1, NULL));
    g_assert(g_file_set_contents(override_file, override, -1, NULL));

    g_free(defaults_file);
    g_free(override_dir);
    g_free(override_file);
}

static
void
test_defaults_override(
    void)
{
    test_normal3(NULL, test_defaults_override_prestart,
        test_defaults_override_start);
}

/*==========================================================================*
 * defaults/no_override
 *==========================================================================*/

static
void
test_defaults_no_override_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    /* Since all values are default, there was need to save the settings */
    g_assert(!g_file_test(test->storage_file, G_FILE_TEST_EXISTS));
    test_get_plugin_string_value_done(object, result, test, "foo");
}

static
void
test_defaults_no_override_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    /* Verify the state */
    g_assert(!test->manager->enabled);
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        test_defaults_no_override_done);
}

static
void
test_defaults_no_override_prestart(
    TestData* test)
{
    char* defaults_file = g_build_filename(test->config_dir,
        SETTINGS_CONFIG_DEFAULTS_FILE, NULL);
    char* override_dir = g_build_filename(test->config_dir,
        SETTINGS_CONFIG_DEFAULTS_DIR, NULL);
    char* override_file = g_build_filename(override_dir, "override", NULL);
    char* rogue_file = g_build_filename(override_dir, "junkfile", NULL);
    char* rogue_dir = g_build_filename(override_dir, "junkdir", NULL);
    static const char defaults[] =
        "[" SETTINGS_GROUP "]\n"
        SETTINGS_KEY_ENABLED "=false\n"
        "[" TEST_PLUGIN_NAME "]\n"
        TEST_PLUGIN_KEY "='foo'\n";
    static const char override[] =
        "[" SETTINGS_GROUP "]\n"
        "invalid-key=false\n"
        "[" TEST_PLUGIN_NAME "]\n"
        "invalid-key=false\n";

    GDEBUG("%s\n%s", defaults_file, defaults);
    GDEBUG("%s\n%s", override_file, override);
    g_assert_cmpint(g_mkdir(override_dir, SETTINGS_STORAGE_DIR_PERM), == ,0);
    g_assert_cmpint(g_mkdir(rogue_dir, SETTINGS_STORAGE_DIR_PERM), == ,0);
    g_assert(g_file_set_contents(defaults_file, defaults, -1, NULL));
    g_assert(g_file_set_contents(override_file, override, -1, NULL));
    g_assert(g_file_set_contents(rogue_file, "junk", -1, NULL));

    g_free(defaults_file);
    g_free(override_dir);
    g_free(override_file);
    g_free(rogue_file);
    g_free(rogue_dir);
}

static
void
test_defaults_no_override(
    void)
{
    test_normal3(NULL, test_defaults_no_override_prestart,
        test_defaults_no_override_start);
}

/*==========================================================================*
 * config/load
 *==========================================================================*/

static
void
test_config_load_done(
    GObject* client,
    GAsyncResult* result,
    gpointer test)
{
    /* Value is taken from the config */
    test_get_plugin_string_value_done(client, result, (TestData*) test, "foo");
}

static
void
test_config_load_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_assert(!test->manager->enabled);
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        test_config_load_done);
}

static
void
test_config_load_prestart(
    TestData* test)
{
    char* defaults_file = g_build_filename(test->config_dir,
        SETTINGS_CONFIG_DEFAULTS_FILE, NULL);
    static const char defaults[] =
        "[" SETTINGS_GROUP "]\n"
        SETTINGS_KEY_ENABLED "=true\n"
        "[" TEST_PLUGIN_NAME "]\n"
        TEST_PLUGIN_KEY "='bar'\n";

    GDEBUG("%s\n%s", defaults_file, defaults);
    g_assert(g_file_set_contents(defaults_file, defaults, -1, NULL));
    g_free(defaults_file);
}

static
void
test_config_load(
    void)
{
    test_normal3("[" SETTINGS_GROUP "]\n"
        SETTINGS_KEY_ENABLED "=false\n"
        "[" TEST_PLUGIN_NAME "]\n"
        TEST_PLUGIN_KEY "='foo'\n",
        test_config_load_prestart, test_config_load_start);
}

/*==========================================================================*
 * config/save
 *==========================================================================*/

static
void
test_config_save_done(
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GKeyFile* config = g_key_file_new();
    GError* error = NULL;

    g_assert(!test->manager->enabled);
    test_call_ok_done(client, result, test);

    /* Verify that the "Enabled" value has been saved */
    g_assert(g_key_file_load_from_file(config, test->storage_file, 0, NULL));
    g_assert(!g_key_file_get_boolean(config, SETTINGS_GROUP,
        SETTINGS_KEY_ENABLED, &error));
    g_assert(!error);

    g_key_file_unref(config);
}

static
void
test_config_save_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_assert(test->manager->enabled);
    test_call_set_enabled(test, client, FALSE, test_config_save_done);
}

static
void
test_config_save(
    void)
{
    test_normal2(NULL, test_config_save_start);
}

/*==========================================================================*
 * migrate
 *==========================================================================*/

static
void
test_migrate_done(
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GKeyFile* config = g_key_file_new();
    GError* error = NULL;

    test_get_plugin_boolean_value_done(client, result, test,
        DBUS_NEARD_PLUGIN_DEFAULT_VALUE);

    /* Verify that the "BluetoothStaticHandover" value has been migrated */
    g_assert(g_key_file_load_from_file(config, test->storage_file, 0, NULL));
    g_assert_cmpint(g_key_file_get_boolean(config, TEST_DBUS_NEARD_PLUGIN_NAME,
        DBUS_NEARD_PLUGIN_KEY, &error), == ,DBUS_NEARD_PLUGIN_DEFAULT_VALUE);
    g_assert(!error);

    g_key_file_unref(config);
}

static
void
test_migrate_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, TEST_DBUS_NEARD_PLUGIN_NAME,
        DBUS_NEARD_PLUGIN_KEY, test_migrate_done);
}

static
void
test_migrate(
    void)
{
    test_normal4(NULL, NULL, test_migrate_start);
}

/*==========================================================================*
 * no_migrate
 *==========================================================================*/

static
void
test_no_migrate_done(
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GKeyFile* config = g_key_file_new();
    GError* error = NULL;

    test_get_plugin_boolean_value_done(client, result, test,
        !DBUS_NEARD_PLUGIN_DEFAULT_VALUE);

    /* Verify that the "BluetoothStaticHandover" value stays unchanged */
    g_assert(g_key_file_load_from_file(config, test->storage_file, 0, NULL));
    g_assert_cmpint(g_key_file_get_boolean(config, TEST_DBUS_NEARD_PLUGIN_NAME,
        DBUS_NEARD_PLUGIN_KEY, &error), == ,!DBUS_NEARD_PLUGIN_DEFAULT_VALUE);
    g_assert(!error);

    g_key_file_unref(config);
}

static
void
test_no_migrate_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, TEST_DBUS_NEARD_PLUGIN_NAME,
        DBUS_NEARD_PLUGIN_KEY, test_no_migrate_done);
}

static
void
test_no_migrate(
    void)
{
    test_normal4("[" TEST_DBUS_NEARD_PLUGIN_NAME "]\n"
        DBUS_NEARD_PLUGIN_KEY "=true\n", NULL, test_no_migrate_start);
}

/*==========================================================================*
 * get_all/ok
 *==========================================================================*/

static
void
test_get_all_ok_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    gboolean enabled = FALSE;
    GError* error = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(ib)", &version, &enabled);
    GDEBUG("version=%d, enabled=%d", version, enabled);
    g_assert_cmpint(version, >= ,SETTINGS_DBUS_INTERFACE_VERSION);
    g_assert(enabled);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_all_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetAll", test_get_all_ok_done);
}

static
void
test_get_all_ok(
    void)
{
    test_normal(test_get_all_ok_start);
}

/*==========================================================================*
 * get_all/access_denied
 *==========================================================================*/

static
void
test_get_all_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetAll", test_done_access_denied);
}

static
void
test_get_all_access_denied(
    void)
{
    test_access_denied(test_get_all_access_denied_start);
}

/*==========================================================================*
 * get_interface_version/ok
 *==========================================================================*/

static
void
test_get_interface_version_ok_done(
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
    g_assert_cmpint(version, >= ,SETTINGS_DBUS_INTERFACE_VERSION);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_interface_version_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetInterfaceVersion",
        test_get_interface_version_ok_done);
}

static
void
test_get_interface_version_ok(
    void)
{
    test_normal(test_get_interface_version_ok_start);
}

/*==========================================================================*
 * get_interface_version/access_denied
 *==========================================================================*/

static
void
test_get_interface_version_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetInterfaceVersion", test_done_access_denied);
}

static
void
test_get_interface_version_access_denied(
    void)
{
    test_access_denied(test_get_interface_version_access_denied_start);
}

/*==========================================================================*
 * get_enabled/ok
 *==========================================================================*/

static
void
test_get_enabled_ok_done(
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    test_get_enabled_done(client, result, (TestData*) user_data, TRUE);
}

static
void
test_get_enabled_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetEnabled", test_get_enabled_ok_done);
}

static
void
test_get_enabled_ok(
    void)
{
    test_normal(test_get_enabled_ok_start);
}

/*==========================================================================*
 * get_enabled/access_denied
 *==========================================================================*/

static
void
test_get_enabled_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetEnabled", test_done_access_denied);
}

static
void
test_get_enabled_access_denied(
    void)
{
    test_access_denied(test_get_enabled_access_denied_start);
}

/*==========================================================================*
 * set_enabled/ok
 *==========================================================================*/

#define TEST_SET_ENABLED_VALUE FALSE

static
void
test_set_enabled_ok_signal(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean enabled;

    g_variant_get(args, "(b)", &enabled);
    g_assert_cmpint(enabled, == ,TEST_SET_ENABLED_VALUE);
    GDEBUG("%s %s", name, enabled ? "true" : "false");

    /* test_set_enabled_ok_done will check this flags */
    g_assert_cmpint(test->flags, == ,0);
    test->flags |= TEST_ENABLED_CHANGED_SIGNAL_FLAG;
}

static
void
test_set_enabled_ok_done(
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    g_assert(!test->manager->enabled);
    g_assert_cmpint(test->flags, == ,TEST_ENABLED_CHANGED_SIGNAL_FLAG);
    test_call_ok_done(client, result, test);
}

static
void
test_set_enabled_ok_repeat(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GDBusConnection* client = G_DBUS_CONNECTION(object);

    g_assert(!test->manager->enabled);
    g_assert_cmpint(test->flags, == ,TEST_ENABLED_CHANGED_SIGNAL_FLAG);
    test_call_ok_check(client, result);

    /* Second time around there won't be any signals */
    test_call_set_enabled(test, client, TEST_SET_ENABLED_VALUE,
        test_set_enabled_ok_done);
}

static
void
test_set_enabled_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        SETTINGS_DBUS_INTERFACE, "EnabledChanged",
        SETTINGS_DBUS_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
        test_set_enabled_ok_signal, test, NULL));

    g_assert(test->manager->enabled);
    test_call_set_enabled(test, client, TEST_SET_ENABLED_VALUE,
        test_set_enabled_ok_repeat);
}

static
void
test_set_enabled_ok(
    void)
{
    test_normal(test_set_enabled_ok_start);
}

/*==========================================================================*
 * set_enabled/access_denied
 *==========================================================================*/

static
void
test_set_enabled_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_set_enabled(test, client, TRUE, test_done_access_denied);
}

static
void
test_set_enabled_access_denied(
    void)
{
    test_access_denied(test_set_enabled_access_denied_start);
}

/*==========================================================================*
 * get_all2/ok
 *==========================================================================*/

static
void
test_get_all2_ok_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;
    gint version = 0;
    gboolean enabled = FALSE;
    GVariant* settings = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(ib@a(sa{sv}))", &version, &enabled, &settings);
    GDEBUG("version=%d, enabled=%d, %d plugins", version, enabled, (int)
        g_variant_n_children(settings));
    g_assert_cmpint(version, >= ,SETTINGS_DBUS_INTERFACE_VERSION);
    g_assert(enabled);
    g_assert(g_variant_is_container(settings));
    g_assert_cmpint(g_variant_n_children(settings), == ,0);
    g_variant_unref(settings);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_all2_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetAll2", test_get_all2_ok_done);
}

static
void
test_get_all2_ok(
    void)
{
    test_normal(test_get_all2_ok_start);
}

/*==========================================================================*
 * get_all2/access_denied
 *==========================================================================*/

static
void
test_get_all2_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetAll2", test_done_access_denied);
}

static
void
test_get_all2_access_denied(
    void)
{
    test_access_denied(test_get_all2_access_denied_start);
}

/*==========================================================================*
 * get_all_plugin_settings/empty
 *==========================================================================*/

static
void
test_get_all_plugin_settings_empty_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;
    GVariant* settings = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(@a(sa{sv}))", &settings);
    g_assert(g_variant_is_container(settings));
    GDEBUG("%d plugins", (int) g_variant_n_children(settings));
    g_assert_cmpint(g_variant_n_children(settings), == ,0);
    g_variant_unref(settings);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_all_plugin_settings_empty_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetAllPluginSettings",
        test_get_all_plugin_settings_empty_done);
}

static
void
test_get_all_plugin_settings_empty(
    void)
{
    test_normal(test_get_all_plugin_settings_empty_start);
}

/*==========================================================================*
 * get_all_plugin_settings/non_empty
 *==========================================================================*/

static
void
test_get_all_plugin_settings_non_empty_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    const char* name = NULL;
    GError* error = NULL;
    GVariant* plugins = NULL;
    GVariant* plugin = NULL;
    GVariant* settings = NULL;
    GVariant* value = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(var);
    g_assert(!error);
    g_variant_get(var, "(@a(sa{sv}))", &plugins);
    g_assert(g_variant_is_container(plugins));
    GDEBUG("%d plugin", (int) g_variant_n_children(plugins));
    g_assert_cmpint(g_variant_n_children(plugins), == ,1);
    g_assert((plugin = g_variant_get_child_value(plugins, 0)) != NULL);
    g_variant_get(plugin, "(&s@a{sv})", &name, &settings);
    g_assert(settings);
    g_assert_cmpstr(name, == ,TEST_PLUGIN_NAME);
    g_assert_cmpint(g_variant_n_children(settings), == ,1);
    g_variant_lookup(settings, TEST_PLUGIN_KEY, "@s", &value);
    g_assert(g_variant_is_of_type(value, G_VARIANT_TYPE_STRING));
    GDEBUG("%s: %s = %s", name, TEST_PLUGIN_KEY,
        g_variant_get_string(value, NULL));
    g_assert_cmpstr(TEST_PLUGIN_DEFAULT_VALUE, == ,
        g_variant_get_string(value, NULL));

    g_variant_unref(settings);
    g_variant_unref(plugin);
    g_variant_unref(plugins);
    g_variant_unref(value);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_all_plugin_settings_non_empty_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetAllPluginSettings",
        test_get_all_plugin_settings_non_empty_done);
}

static
void
test_get_all_plugin_settings_non_empty(
    void)
{
    test_normal2(NULL, test_get_all_plugin_settings_non_empty_start);
}

/*==========================================================================*
 * get_all_plugin_settings/access_denied
 *==========================================================================*/

static
void
test_get_all_plugin_settings_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call(test, client, "GetAllPluginSettings", test_done_access_denied);
}

static
void
test_get_all_plugin_settings_access_denied(
    void)
{
    test_access_denied(test_get_all_plugin_settings_access_denied_start);
}

/*==========================================================================*
 * get_plugin_settings/ok
 *==========================================================================*/

static
void
test_get_plugin_settings_ok_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;
    GVariant* settings = NULL;
    GVariant* value = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
      result, &error);

    g_assert(!error);
    g_assert(var);
    g_variant_get(var, "(@a{sv})", &settings);
    g_assert(settings);
    g_assert_cmpint(g_variant_n_children(settings), == ,1);
    g_variant_lookup(settings, TEST_PLUGIN_KEY, "@s", &value);
    g_assert(g_variant_is_of_type(value, G_VARIANT_TYPE_STRING));
    GDEBUG("%s = %s", TEST_PLUGIN_KEY, g_variant_get_string(value, NULL));
    g_assert_cmpstr(TEST_PLUGIN_DEFAULT_VALUE, == ,
        g_variant_get_string(value, NULL));

    g_variant_unref(settings);
    g_variant_unref(value);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_plugin_settings_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_settings(test, client, TEST_PLUGIN_NAME,
        test_get_plugin_settings_ok_done);
}

static
void
test_get_plugin_settings_ok(
    void)
{
    test_normal2(NULL, test_get_plugin_settings_ok_start);
}

/*==========================================================================*
 * get_plugin_settings/access_denied
 *==========================================================================*/

static
void
test_get_plugin_settings_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_settings(test, client, "x", test_done_access_denied);
}

static
void
test_get_plugin_settings_access_denied(
    void)
{
    test_access_denied(test_get_plugin_settings_access_denied_start);
}

/*==========================================================================*
 * get_plugin_settings/unknown_plugin
 *==========================================================================*/

static
void
test_get_plugin_settings_unknown_plugin_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_settings(test, client, "x", test_done_unknown_plugin);
}

static
void
test_get_plugin_settings_unknown_plugin(
    void)
{
    test_normal(test_get_plugin_settings_unknown_plugin_start);
}

/*==========================================================================*
 * get_plugin_value/default
 *==========================================================================*/

static
void
test_get_plugin_value_default_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    test_get_plugin_string_value_done(object, result, user_data,
        TEST_PLUGIN_DEFAULT_VALUE);
}

static
void
test_get_plugin_value_default_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        test_get_plugin_value_default_done);
}

static
void
test_get_plugin_value_default(
    void)
{
    test_normal2(NULL, test_get_plugin_value_default_start);
}

/*==========================================================================*
 * get_plugin_value/load
 *==========================================================================*/

static
void
test_get_plugin_value_load_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    test_get_plugin_string_value_done(object, result, user_data,
        TEST_PLUGIN_NON_DEFAULT_VALUE);
}

static
void
test_get_plugin_value_load_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        test_get_plugin_value_load_done);
}

static
void
test_get_plugin_value_load(
    void)
{
    /* N.B. Unquoted value is interpreted as a string */
    test_normal2("[" TEST_PLUGIN_NAME "]\n"
        TEST_PLUGIN_KEY "=" TEST_PLUGIN_NON_DEFAULT_VALUE "\n",
        test_get_plugin_value_load_start);
}

/*==========================================================================*
 * get_plugin_value/load_error
 *==========================================================================*/

static
void
test_get_plugin_value_load_error_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        test_get_plugin_value_default_done);
}

static
void
test_get_plugin_value_load_error(
    void)
{
    test_normal2("aaaaa", test_get_plugin_value_load_error_start);
}

/*==========================================================================*
 * get_plugin_value/access_denied
 *==========================================================================*/

static
void
test_get_plugin_value_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, "x", "y", test_done_access_denied);
}

static
void
test_get_plugin_value_access_denied(
    void)
{
    test_access_denied(test_get_plugin_value_access_denied_start);
}

/*==========================================================================*
 * get_plugin_value/unknown_plugin
 *==========================================================================*/

static
void
test_get_plugin_value_unknown_plugin_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, "x", "y",
        test_done_unknown_plugin);
}

static
void
test_get_plugin_value_unknown_plugin(
    void)
{
    test_normal2(NULL, test_get_plugin_value_unknown_plugin_start);
}

/*==========================================================================*
 * get_plugin_value/unknown_key
 *==========================================================================*/

static
void
test_get_plugin_value_unknown_key_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, "y",
        test_done_unknown_key);
}

static
void
test_get_plugin_value_unknown_key(
    void)
{
    test_normal2(NULL, test_get_plugin_value_unknown_key_start);
}

/*==========================================================================*
 * get_plugin_value/unknown_value
 *==========================================================================*/

static
void
test_get_plugin_value_unknown_value_done(
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    test_done_with_error(client, result, (TestData*) user_data,
        SETTINGS_ERROR_UNKNOWN_VALUE);
}

static
void
test_get_plugin_value_unknown_value_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_get_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY2,
        test_get_plugin_value_unknown_value_done);
}

static
void
test_get_plugin_value_unknown_value(
    void)
{
    test_normal2(NULL, test_get_plugin_value_unknown_value_start);
}

/*==========================================================================*
 * set_plugin_value/ok
 *==========================================================================*/

#define TEST_SET_PLUGIN_VALUE_OK_NEW_VALUE "foo"

static
void
test_set_plugin_value_ok_signal(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;
    const char* plugin = NULL;
    const char* key = NULL;
    GVariant* value = NULL;
    GVariant* string = NULL;

    g_variant_get(args, "(&s&s@v)", &plugin, &key, &value);
    g_assert_cmpstr(plugin, == ,TEST_PLUGIN_NAME);
    g_assert_cmpstr(key, == ,TEST_PLUGIN_KEY);
    g_assert(g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT));
    string = g_variant_get_variant(value);
    g_assert(g_variant_is_of_type(string, G_VARIANT_TYPE_STRING));
    GDEBUG("%s=%s", key, g_variant_get_string(string, NULL));
    g_assert_cmpstr(g_variant_get_string(string, NULL), == ,
        TEST_SET_PLUGIN_VALUE_OK_NEW_VALUE);

    g_variant_unref(value);
    g_variant_unref(string);

    /* test_set_plugin_value_ok_done will check this flags */
    g_assert_cmpint(test->flags, == ,0);
    test->flags |= TEST_PLUGIN_VALUE_CHANGED_SIGNAL_FLAG;
}

static
void
test_set_plugin_value_ok_check_config(
    TestData* test)
{
    test_check_config_file_value(test, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        "'" TEST_SET_PLUGIN_VALUE_OK_NEW_VALUE "'");
}

static
void
test_set_plugin_value_ok_done(
    GObject* client,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    /* Make sure the new value is still there */
    test_set_plugin_value_ok_check_config(test);
    test_call_ok_done(client, result, test);
}

static
void
test_set_plugin_value_ok_repeat(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    GDBusConnection* client = G_DBUS_CONNECTION(object);
    TestData* test = user_data;

    test_call_ok_check(client, result);

    /* We must have received the signal */
    g_assert_cmpint(test->flags, == ,TEST_PLUGIN_VALUE_CHANGED_SIGNAL_FLAG);

    /* Make sure the new value is saved */
    test_check_config_file_value(test, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        "'" TEST_SET_PLUGIN_VALUE_OK_NEW_VALUE "'");

    /* There won't be any signals if we're settings the save value again */
    test_call_set_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        g_variant_new_variant(g_variant_new_string(
        TEST_SET_PLUGIN_VALUE_OK_NEW_VALUE)),
        test_set_plugin_value_ok_done);
}

static
void
test_set_plugin_value_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        SETTINGS_DBUS_INTERFACE, "PluginValueChanged",
        SETTINGS_DBUS_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
        test_set_plugin_value_ok_signal, test, NULL));
    test_call_set_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        g_variant_new_variant(g_variant_new_string(
        TEST_SET_PLUGIN_VALUE_OK_NEW_VALUE)),
        test_set_plugin_value_ok_repeat);
}

static
void
test_set_plugin_value_ok(
    void)
{
    test_normal2(NULL, test_set_plugin_value_ok_start);
}

/*==========================================================================*
 * set_plugin_value/access_denied
 *==========================================================================*/

static
void
test_set_plugin_value_access_denied_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_set_plugin_value(test, client, "x", "y",
        g_variant_new_variant(g_variant_new_boolean(TRUE)),
        test_done_access_denied);
}

static
void
test_set_plugin_value_access_denied(
    void)
{
    test_access_denied(test_set_plugin_value_access_denied_start);
}

/*==========================================================================*
 * set_plugin_value/unknown_plugin
 *==========================================================================*/

static
void
test_set_plugin_value_unknown_plugin_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_set_plugin_value(test, client, "x", "y",
        g_variant_new_variant(g_variant_new_boolean(TRUE)),
        test_done_unknown_plugin);
}

static
void
test_set_plugin_value_unknown_plugin(
    void)
{
    test_normal(test_set_plugin_value_unknown_plugin_start);
}

/*==========================================================================*
 * set_plugin_value/unknown_key
 *==========================================================================*/

static
void
test_set_plugin_value_unknown_key_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_set_plugin_value(test, client, TEST_PLUGIN_NAME, "y",
        g_variant_new_variant(g_variant_new_boolean(TRUE)),
        test_done_unknown_key);
}

static
void
test_set_plugin_value_unknown_key(
    void)
{
    test_normal2(NULL, test_set_plugin_value_unknown_key_start);
}

/*==========================================================================*
 * set_plugin_value/invalid_type
 *==========================================================================*/

static
void
test_set_plugin_value_invalid_type_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* test)
{
    test_call_set_plugin_value(test, client, TEST_PLUGIN_NAME, TEST_PLUGIN_KEY,
        g_variant_new_variant(g_variant_new_boolean(TRUE)), test_done_failed);
}

static
void
test_set_plugin_value_invalid_type(
    void)
{
    test_normal2(NULL, test_set_plugin_value_invalid_type_start);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/settings/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("name_lost"), test_name_lost);
    g_test_add_func(TEST_("defaults/load"), test_defaults_load);
    g_test_add_func(TEST_("defaults/override"), test_defaults_override);
    g_test_add_func(TEST_("defaults/no_override"), test_defaults_no_override);
    g_test_add_func(TEST_("config/load"), test_config_load);
    g_test_add_func(TEST_("config/save"), test_config_save);
    g_test_add_func(TEST_("migrate"), test_migrate);
    g_test_add_func(TEST_("no_migrate"), test_no_migrate);
    g_test_add_func(TEST_("get_all/ok"), test_get_all_ok);
    g_test_add_func(TEST_("get_all/access_denied"),
        test_get_all_access_denied);
    g_test_add_func(TEST_("get_interface_version/ok"),
        test_get_interface_version_ok);
    g_test_add_func(TEST_("get_interface_version/access_denied"),
        test_get_interface_version_access_denied);
    g_test_add_func(TEST_("get_enabled/ok"), test_get_enabled_ok);
    g_test_add_func(TEST_("get_enabled/access_denied"),
        test_get_enabled_access_denied);
    g_test_add_func(TEST_("set_enabled/ok"), test_set_enabled_ok);
    g_test_add_func(TEST_("set_enabled/access_denied"),
        test_set_enabled_access_denied);
    g_test_add_func(TEST_("get_all2/ok"), test_get_all2_ok);
    g_test_add_func(TEST_("get_all2/access_denied"),
        test_get_all2_access_denied);
    g_test_add_func(TEST_("get_all_plugin_settings/empty"),
        test_get_all_plugin_settings_empty);
    g_test_add_func(TEST_("get_all_plugin_settings/non_empty"),
        test_get_all_plugin_settings_non_empty);
    g_test_add_func(TEST_("get_all_plugin_settings/access_denied"),
        test_get_all_plugin_settings_access_denied);
    g_test_add_func(TEST_("get_plugin_settings/ok"),
        test_get_plugin_settings_ok);
    g_test_add_func(TEST_("get_plugin_settings/access_denied"),
        test_get_plugin_settings_access_denied);
    g_test_add_func(TEST_("get_plugin_settings/unknown_plugin"),
        test_get_plugin_settings_unknown_plugin);
    g_test_add_func(TEST_("get_plugin_value/default"),
        test_get_plugin_value_default);
    g_test_add_func(TEST_("get_plugin_value/load"),
        test_get_plugin_value_load);
    g_test_add_func(TEST_("get_plugin_value/load_error"),
        test_get_plugin_value_load_error);
    g_test_add_func(TEST_("get_plugin_value/access_denied"),
        test_get_plugin_value_access_denied);
    g_test_add_func(TEST_("get_plugin_value/unknown_plugin"),
        test_get_plugin_value_unknown_plugin);
    g_test_add_func(TEST_("get_plugin_value/unknown_key"),
        test_get_plugin_value_unknown_key);
    g_test_add_func(TEST_("get_plugin_value/unknown_value"),
        test_get_plugin_value_unknown_value);
    g_test_add_func(TEST_("set_plugin_value/ok"),
        test_set_plugin_value_ok);
    g_test_add_func(TEST_("set_plugin_value/access_denied"),
        test_set_plugin_value_access_denied);
    g_test_add_func(TEST_("set_plugin_value/unknown_plugin"),
        test_set_plugin_value_unknown_plugin);
    g_test_add_func(TEST_("set_plugin_value/unknown_key"),
        test_set_plugin_value_unknown_key);
    g_test_add_func(TEST_("set_plugin_value/invalid_type"),
        test_set_plugin_value_invalid_type);
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
