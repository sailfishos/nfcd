/*
 * Copyright (C) 2018-2022 Jolla Ltd.
 * Copyright (C) 2018-2022 Slava Monich <slava.monich@jolla.com>
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

#include "dbus_neard.h"
#include "plugin.h"

#include <nfc_adapter.h>
#include <nfc_manager.h>
#include <nfc_plugin_impl.h>
#include <nfc_config.h>

#include <gutil_misc.h>

#include <glib/gstdio.h>

GLOG_MODULE_DEFINE("dbus-neard");

#define NEARD_SERVICE "org.neard"

enum {
    MANAGER_ADAPTER_ADDED,
    MANAGER_ADAPTER_REMOVED,
    MANAGER_EVENT_COUNT
};

typedef NfcPluginClass DBusNeardPluginClass;
typedef struct dbus_neard_plugin {
    NfcPlugin parent;
    guint own_name_id;
    GDBusObjectManagerServer* object_manager;
    GHashTable* adapters;
    NfcManager* manager;
    gulong event_id[MANAGER_EVENT_COUNT];
    DBusNeardManager* agent_manager;
    DBusNeardSettings* settings;
    DBusNeardOptions options;
} DBusNeardPlugin;

static
void
dbus_neard_plugin_config_init(
    NfcConfigurableInterface* iface);

GType dbus_neard_plugin_get_type() G_GNUC_INTERNAL;
G_DEFINE_TYPE_WITH_CODE(DBusNeardPlugin, dbus_neard_plugin, NFC_TYPE_PLUGIN,
G_IMPLEMENT_INTERFACE(NFC_TYPE_CONFIGURABLE, dbus_neard_plugin_config_init))
#define THIS_TYPE dbus_neard_plugin_get_type()
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), THIS_TYPE, DBusNeardPlugin)

enum neard_plugin_signal {
     SIGNAL_CONFIG_VALUE_CHANGED,
     SIGNAL_COUNT
};

#define SIGNAL_CONFIG_VALUE_CHANGED_NAME "neard-plugin-config-value-changed"

static guint dbus_neard_plugin_signals[SIGNAL_COUNT] = { 0 };

/*
 * Originally, neard settings were stored in this file. Now it's only
 * used for migration purposes.
 */
#define NEARD_SETTINGS_FILE     "/var/lib/nfcd/neard"
#define NEARD_SETTINGS_GROUP    "Settings"

static
void
dbus_neard_plugin_create_adapter(
    DBusNeardPlugin* self,
    NfcAdapter* adapter)
{
    g_hash_table_replace(self->adapters, (void*)adapter->name,
        dbus_neard_adapter_new(adapter, self->object_manager,
            self->agent_manager));
}

static
void
dbus_neard_plugin_free_adapter(
    void* adapter)
{
    dbus_neard_adapter_free((DBusNeardAdapter*)adapter);
}

static
void
dbus_neard_adapter_added(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    dbus_neard_plugin_create_adapter(THIS(plugin), adapter);
}

static
void
dbus_neard_adapter_removed(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    DBusNeardPlugin* self = THIS(plugin);

    g_hash_table_remove(self->adapters, (void*)adapter->name);
}

static
void
dbus_neard_plugin_name_acquired(
    GDBusConnection* bus,
    const gchar* name,
    gpointer plugin)
{
    DBusNeardPlugin* self = THIS(plugin);

    GDEBUG("Acquired service name '%s'", name);
    g_dbus_object_manager_server_set_connection(self->object_manager, bus);
}

static
void
dbus_neard_plugin_name_lost(
    GDBusConnection* bus,
    const gchar* name,
    gpointer plugin)
{
    DBusNeardPlugin* self = THIS(plugin);

    GERR("'%s' service already running or access denied", name);
    g_dbus_object_manager_server_set_connection(self->object_manager, NULL);
    /* Tell daemon to exit */
    nfc_manager_stop(self->manager, NFC_MANAGER_PLUGIN_ERROR);
}

/*==========================================================================*
 * NfcConfigurable
 *==========================================================================*/

static
const char* const*
dbus_neard_plugin_config_get_keys(
    NfcConfigurable* config)
{
    static const char* const dbus_neard_plugin_keys[] = {
        NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER,
        NULL
    };

    return dbus_neard_plugin_keys;
}

static
GVariant*
dbus_neard_plugin_config_get_value(
    NfcConfigurable* config,
    const char* key)
{
    const DBusNeardOptions* options = &THIS(config)->options;

    if (!g_strcmp0(NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER, key)) {
        /* OK to return a floating reference */
        return g_variant_new_boolean(options->bt_static_handover);
    } else {
        return NULL;
    }
}

static
gboolean
dbus_neard_plugin_config_set_value(
    NfcConfigurable* config,
    const char* key,
    GVariant* value)
{
    DBusNeardPlugin* self = THIS(config);
    DBusNeardOptions* options = &self->options;
    gboolean ok = FALSE;

    if (!g_strcmp0(key, NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER)) {
        gboolean newval = NEARD_SETTINGS_DEFAULT_BT_STATIC_HANDOVER;

        if (!value) {
            ok = TRUE;
        } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
            newval = g_variant_get_boolean(value);
            ok = TRUE;
        }

        if (ok && options->bt_static_handover != newval) {
            GDEBUG("%s %s", key, newval ? "on" : "off");
            options->bt_static_handover = newval;
            g_signal_emit(self, dbus_neard_plugin_signals
                [SIGNAL_CONFIG_VALUE_CHANGED], g_quark_from_string(key),
                key, value);
        }
    }
    return ok;
}

static
gulong
dbus_neard_plugin_config_add_change_handler(
    NfcConfigurable* config,
    const char* key,
    NfcConfigChangeFunc func,
    void* user_data)
{
    return g_signal_connect_closure_by_id(THIS(config),
        dbus_neard_plugin_signals[SIGNAL_CONFIG_VALUE_CHANGED],
        key ? g_quark_from_string(key) : 0,
        g_cclosure_new(G_CALLBACK(func), user_data, NULL), FALSE);
}

static
void
dbus_neard_plugin_config_init(
    NfcConfigurableInterface* iface)
{
    iface->get_keys = dbus_neard_plugin_config_get_keys;
    iface->get_value = dbus_neard_plugin_config_get_value;
    iface->set_value = dbus_neard_plugin_config_set_value;
    iface->add_change_handler = dbus_neard_plugin_config_add_change_handler;
}

/*==========================================================================*
 * NfcPlugin
 *==========================================================================*/

static
gboolean
dbus_neard_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    const char* legacy_config_file = NEARD_SETTINGS_FILE;
    DBusNeardPlugin* self = THIS(plugin);
    DBusNeardOptions* options = &self->options;
    NfcAdapter** adapters;

    GVERBOSE("Starting");

    /* Migrate the config */
    if (g_file_test(legacy_config_file, G_FILE_TEST_EXISTS)) {
        GKeyFile* keyfile = g_key_file_new();

        GINFO("Migrating %s", legacy_config_file);
        if (g_key_file_load_from_file(keyfile, legacy_config_file, 0, NULL)) {
            GError* error = NULL;
            const gboolean value = g_key_file_get_boolean(keyfile,
                NEARD_SETTINGS_GROUP, NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER,
                &error);

            if (!error) {
                options->bt_static_handover = value;
            } else {
                g_error_free(error);
                /* Keep the default value */
            }
        }
        g_unlink(legacy_config_file);
        g_key_file_unref(keyfile);
    }

    self->object_manager = g_dbus_object_manager_server_new("/");
    self->agent_manager = dbus_neard_manager_new(options);

    /* HACK to work around GDBusObjectManagerServer not allowing to manage
     * the root path. See https://bugzilla.gnome.org/show_bug.cgi?id=761810
     * The workaround
     */
#if !GLIB_CHECK_VERSION(2,49,2)
    if (glib_check_version(2,49,2)) {
        struct {
            GMutex lock;
            GDBusConnection* connection;
            gchar* object_path;
            gchar* object_path_ending_in_slash;
        }* priv = (void*)self->object_manager->priv;
        if (!priv->connection && priv->object_path &&
            priv->object_path[0] == '/' &&
            priv->object_path[1] == 0 &&
            priv->object_path_ending_in_slash &&
            priv->object_path_ending_in_slash[0] == '/' &&
            priv->object_path_ending_in_slash[1] == '/' &&
            priv->object_path_ending_in_slash[2] == 0) {
            priv->object_path_ending_in_slash[1] = 0;
        }
    }
#endif

    self->manager = nfc_manager_ref(manager);
    self->own_name_id = g_bus_own_name(DBUS_NEARD_BUS_TYPE, NEARD_SERVICE,
        G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL, dbus_neard_plugin_name_acquired,
        dbus_neard_plugin_name_lost, self, NULL);

    self->event_id[MANAGER_ADAPTER_ADDED] =
        nfc_manager_add_adapter_added_handler(manager,
            dbus_neard_adapter_added, self);
    self->event_id[MANAGER_ADAPTER_REMOVED] =
        nfc_manager_add_adapter_removed_handler(manager,
            dbus_neard_adapter_removed, self);

    /* Register initial set of adapters (if any) */
    for (adapters = manager->adapters; *adapters; adapters++) {
        dbus_neard_plugin_create_adapter(self, *adapters);
    }

    self->settings = dbus_neard_settings_new(NFC_CONFIGURABLE(self));
    return TRUE;
}

static
void
dbus_neard_plugin_stop(
    NfcPlugin* plugin)
{
    DBusNeardPlugin* self = THIS(plugin);

    GVERBOSE("Stopping");
    dbus_neard_settings_free(self->settings);
    g_hash_table_remove_all(self->adapters);
    if (self->agent_manager) {
        dbus_neard_manager_unref(self->agent_manager);
        self->agent_manager = NULL;
    }
    if (self->object_manager) {
        g_object_unref(self->object_manager);
        self->object_manager = NULL;
    }
    if (self->own_name_id) {
        g_bus_unown_name(self->own_name_id);
        self->own_name_id = 0;
    }
    if (self->manager) {
        nfc_manager_remove_all_handlers(self->manager, self->event_id);
        nfc_manager_unref(self->manager);
        self->manager = NULL;
    }
}

static
void
dbus_neard_plugin_init(
    DBusNeardPlugin* self)
{
    DBusNeardOptions* options = &self->options;

    options->bt_static_handover = NEARD_SETTINGS_DEFAULT_BT_STATIC_HANDOVER;
    self->adapters = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, dbus_neard_plugin_free_adapter);
}

static
void
dbus_neard_plugin_finalize(
    GObject* plugin)
{
    DBusNeardPlugin* self = THIS(plugin);

    g_hash_table_destroy(self->adapters);
    G_OBJECT_CLASS(dbus_neard_plugin_parent_class)->finalize(plugin);
}

static
void
dbus_neard_plugin_class_init(
    NfcPluginClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    G_OBJECT_CLASS(klass)->finalize = dbus_neard_plugin_finalize;
    klass->start = dbus_neard_plugin_start;
    klass->stop = dbus_neard_plugin_stop;

    dbus_neard_plugin_signals[SIGNAL_CONFIG_VALUE_CHANGED] =
        g_signal_new(SIGNAL_CONFIG_VALUE_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VARIANT);
}

static
NfcPlugin*
dbus_neard_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(THIS_TYPE, NULL);
}

NFC_PLUGIN_DEFINE(dbus_neard, "org.neard D-Bus interface",
    dbus_neard_plugin_create)

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
