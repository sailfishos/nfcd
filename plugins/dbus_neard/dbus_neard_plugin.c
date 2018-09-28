/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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

#include <gutil_misc.h>

GLOG_MODULE_DEFINE("dbus-neard");

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
} DBusNeardPlugin;

G_DEFINE_TYPE(DBusNeardPlugin, dbus_neard_plugin, NFC_TYPE_PLUGIN)
#define DBUS_NEARD_TYPE_PLUGIN (dbus_neard_plugin_get_type())
#define DBUS_NEARD_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUS_NEARD_TYPE_PLUGIN, DBusNeardPlugin))

#define NEARD_SERVICE "org.neard"

static
void
dbus_neard_plugin_create_adapter(
    DBusNeardPlugin* self,
    NfcAdapter* adapter)
{
    g_hash_table_replace(self->adapters, (void*)adapter->name,
        dbus_neard_adapter_new(adapter, self->object_manager));
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
    dbus_neard_plugin_create_adapter(DBUS_NEARD_PLUGIN(plugin), adapter);
}

static
void
dbus_neard_adapter_removed(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    DBusNeardPlugin* self = DBUS_NEARD_PLUGIN(plugin);

    g_hash_table_remove(self->adapters, (void*)adapter->name);
}

static
void
dbus_neard_plugin_name_acquired(
    GDBusConnection* bus,
    const gchar* name,
    gpointer plugin)
{
    DBusNeardPlugin* self = DBUS_NEARD_PLUGIN(plugin);

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
    DBusNeardPlugin* self = DBUS_NEARD_PLUGIN(plugin);

    GERR("'%s' service already running or access denied", name);
    g_dbus_object_manager_server_set_connection(self->object_manager, NULL);
    /* Tell daemon to exit */
    nfc_manager_stop(self->manager, NFC_MANAGER_PLUGIN_ERROR);
}

static
gboolean
dbus_neard_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    DBusNeardPlugin* self = DBUS_NEARD_PLUGIN(plugin);
    NfcAdapter** adapters;

    GVERBOSE("Starting");
    self->object_manager = g_dbus_object_manager_server_new("/");

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
    self->own_name_id = g_bus_own_name(G_BUS_TYPE_SYSTEM, NEARD_SERVICE,
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

    return TRUE;
}

static
void
dbus_neard_plugin_stop(
    NfcPlugin* plugin)
{
    DBusNeardPlugin* self = DBUS_NEARD_PLUGIN(plugin);

    GVERBOSE("Stopping");
    g_hash_table_remove_all(self->adapters);
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
    self->adapters = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, dbus_neard_plugin_free_adapter);
}

static
void
dbus_neard_plugin_finalize(
    GObject* plugin)
{
    DBusNeardPlugin* self = DBUS_NEARD_PLUGIN(plugin);

    g_hash_table_destroy(self->adapters);
    G_OBJECT_CLASS(dbus_neard_plugin_parent_class)->finalize(plugin);
}

static
void
dbus_neard_plugin_class_init(
    NfcPluginClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = dbus_neard_plugin_finalize;
    klass->start = dbus_neard_plugin_start;
    klass->stop = dbus_neard_plugin_stop;
}

static
NfcPlugin*
dbus_neard_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(DBUS_NEARD_TYPE_PLUGIN, NULL);
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
