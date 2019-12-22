/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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

#include "dbus_service.h"
#include "dbus_service/org.sailfishos.nfc.Daemon.h"
#include "plugin.h"

#include <nfc_core.h>
#include <nfc_adapter.h>
#include <nfc_manager.h>
#include <nfc_plugin_impl.h>

#include <gutil_idlepool.h>
#include <gutil_misc.h>
#include <gutil_strv.h>

#include <stdlib.h>

GLOG_MODULE_DEFINE("dbus-service");

enum {
    EVENT_ADAPTER_ADDED,
    EVENT_ADAPTER_REMOVED,
    EVENT_COUNT
};

enum {
    CALL_GET_ALL,
    CALL_GET_INTERFACE_VERSION,
    CALL_GET_ADAPTERS,
    CALL_GET_ALL2,
    CALL_GET_DAEMON_VERSION,
    CALL_COUNT
};

typedef NfcPluginClass DBusServicePluginClass;
struct dbus_service_plugin {
    NfcPlugin parent;
    guint own_name_id;
    GUtilIdlePool* pool;
    GDBusConnection* connection;
    GHashTable* adapters;
    NfcManager* manager;
    OrgSailfishosNfcDaemon* iface;
    gulong event_id[EVENT_COUNT];
    gulong call_id[CALL_COUNT];
};

G_DEFINE_TYPE(DBusServicePlugin, dbus_service_plugin, NFC_TYPE_PLUGIN)
#define DBUS_SERVICE_TYPE_PLUGIN (dbus_service_plugin_get_type())
#define DBUS_SERVICE_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUS_SERVICE_TYPE_PLUGIN, DBusServicePlugin))

#define NFC_SERVICE     "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"

#define NFC_DBUS_PLUGIN_INTERFACE_VERSION  (2)

static
gboolean
dbus_service_plugin_create_adapter(
    DBusServicePlugin* self,
    NfcAdapter* adapter)
{
    DBusServiceAdapter* dbus =
        dbus_service_adapter_new(adapter, self->connection);

    if (dbus) {
        g_hash_table_replace(self->adapters, g_strdup(adapter->name), dbus);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
void
dbus_service_plugin_free_adapter(
    void* adapter)
{
    dbus_service_adapter_free((DBusServiceAdapter*)adapter);
}

static
int
dbus_service_plugin_compare_strings(
    const void* p1,
    const void* p2)
{
    return strcmp(*(char* const*)p1, *(char* const*)p2);
}

static
const char* const*
dbus_service_plugin_get_adapter_paths(
    DBusServicePlugin* self)
{
    const char** out = g_new(const char*, g_hash_table_size(self->adapters)+1);
    GHashTableIter it;
    gpointer value;
    int n = 0;

    g_hash_table_iter_init(&it, self->adapters);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        out[n++] = dbus_service_adapter_path((DBusServiceAdapter*)value);
    }
    out[n] = NULL;
    qsort(out, n, sizeof(char*), dbus_service_plugin_compare_strings);

    /* Deallocated by the idle pool (actual strings are owned by tags) */
    gutil_idle_pool_add(self->pool, out, g_free);
    return out;
}

static
void
dbus_service_plugin_adapters_changed(
    DBusServicePlugin* self)
{
    org_sailfishos_nfc_daemon_emit_adapters_changed(self->iface,
        dbus_service_plugin_get_adapter_paths(self));
}

/*==========================================================================*
 * NfcManager events
 *==========================================================================*/

static
void
dbus_service_adapter_added(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(plugin);

    if (self->connection) {
        if (dbus_service_plugin_create_adapter(self, adapter)) {
            dbus_service_plugin_adapters_changed(self);
        }
    }
}

static
void
dbus_service_adapter_removed(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(plugin);

    if (g_hash_table_remove(self->adapters, (void*)adapter->name)) {
        dbus_service_plugin_adapters_changed(self);
    }
}

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

static
gboolean
dbus_service_plugin_handle_get_all(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(user_data);

    org_sailfishos_nfc_daemon_complete_get_all(iface, call,
        NFC_DBUS_PLUGIN_INTERFACE_VERSION,
        dbus_service_plugin_get_adapter_paths(self));
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_get_interface_version(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    org_sailfishos_nfc_daemon_complete_get_interface_version(iface, call,
        NFC_DBUS_PLUGIN_INTERFACE_VERSION);
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_get_adapters(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(user_data);

    org_sailfishos_nfc_daemon_complete_get_adapters(iface, call,
        dbus_service_plugin_get_adapter_paths(self));
    return TRUE;
}

/* Interface version 2 */

static
gboolean
dbus_service_plugin_handle_get_all2(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(user_data);

    org_sailfishos_nfc_daemon_complete_get_all2(iface, call,
        NFC_DBUS_PLUGIN_INTERFACE_VERSION,
        dbus_service_plugin_get_adapter_paths(self),
        nfc_core_version());
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_get_daemon_version(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    org_sailfishos_nfc_daemon_complete_get_daemon_version(iface, call,
        nfc_core_version());
    return TRUE;
}

/*==========================================================================*
 * Name watching
 *==========================================================================*/

static
void
dbus_service_plugin_bus_connected(
    GDBusConnection* connection,
    const gchar* name,
    gpointer plugin)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(plugin);
    GError* error = NULL;

    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->iface),
        connection, NFC_DAEMON_PATH, &error)) {
        NfcAdapter** adapters;

        g_object_ref(self->connection = connection);
        /* Register initial set of adapters (if any) */
        for (adapters = self->manager->adapters; *adapters; adapters++) {
            dbus_service_plugin_create_adapter(self, *adapters);
        }
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
        /* Tell daemon to exit */
        nfc_manager_stop(self->manager, NFC_MANAGER_PLUGIN_ERROR);
    }
}

static
void
dbus_service_plugin_name_acquired(
    GDBusConnection* connection,
    const gchar* name,
    gpointer plugin)
{
    GDEBUG("Acquired service name '%s'", name);
}

static
void
dbus_service_plugin_name_lost(
    GDBusConnection* bus,
    const gchar* name,
    gpointer plugin)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(plugin);

    GERR("'%s' service already running or access denied", name);
    /* Tell daemon to exit */
    nfc_manager_stop(self->manager, NFC_MANAGER_PLUGIN_ERROR);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
gboolean
dbus_service_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(plugin);

    GVERBOSE("Starting");
    self->manager = nfc_manager_ref(manager);
    self->iface = org_sailfishos_nfc_daemon_skeleton_new();
    self->own_name_id = dbus_service_name_own(self, NFC_SERVICE,
        dbus_service_plugin_bus_connected, dbus_service_plugin_name_acquired,
        dbus_service_plugin_name_lost);

    /* NfcManager events */
    self->event_id[EVENT_ADAPTER_ADDED] =
        nfc_manager_add_adapter_added_handler(manager,
            dbus_service_adapter_added, self);
    self->event_id[EVENT_ADAPTER_REMOVED] =
        nfc_manager_add_adapter_removed_handler(manager,
            dbus_service_adapter_removed, self);

    /* D-Bus calls */
    self->call_id[CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_service_plugin_handle_get_all), self);
    self->call_id[CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_service_plugin_handle_get_interface_version), self);
    self->call_id[CALL_GET_ADAPTERS] =
        g_signal_connect(self->iface, "handle-get-adapters",
        G_CALLBACK(dbus_service_plugin_handle_get_adapters), self);
    self->call_id[CALL_GET_ALL2] =
        g_signal_connect(self->iface, "handle-get-all2",
        G_CALLBACK(dbus_service_plugin_handle_get_all2), self);
    self->call_id[CALL_GET_DAEMON_VERSION] =
        g_signal_connect(self->iface, "handle-get-daemon-version",
        G_CALLBACK(dbus_service_plugin_handle_get_daemon_version), self);

    return TRUE;
}

static
void
dbus_service_plugin_stop(
    NfcPlugin* plugin)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(plugin);

    GVERBOSE("Stopping");
    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);
    g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(self->iface));
    g_hash_table_remove_all(self->adapters);
    g_object_unref(self->iface);
    dbus_service_name_unown(self->own_name_id);
    nfc_manager_remove_all_handlers(self->manager, self->event_id);
    nfc_manager_unref(self->manager);
    if (self->connection) {
        g_object_unref(self->connection);
        self->connection = NULL;
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
dbus_service_plugin_init(
    DBusServicePlugin* self)
{
    self->pool = gutil_idle_pool_new();
    self->adapters = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, dbus_service_plugin_free_adapter);
}

static
void
dbus_service_plugin_finalize(
    GObject* plugin)
{
    DBusServicePlugin* self = DBUS_SERVICE_PLUGIN(plugin);

    gutil_idle_pool_destroy(self->pool);
    g_hash_table_destroy(self->adapters);
    G_OBJECT_CLASS(dbus_service_plugin_parent_class)->finalize(plugin);
}

static
void
dbus_service_plugin_class_init(
    NfcPluginClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = dbus_service_plugin_finalize;
    klass->start = dbus_service_plugin_start;
    klass->stop = dbus_service_plugin_stop;
}

static
NfcPlugin*
dbus_service_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(DBUS_SERVICE_TYPE_PLUGIN, NULL);
}

NFC_PLUGIN_DEFINE(dbus_service, "org.sailfishos.nfc D-Bus interface",
    dbus_service_plugin_create)

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
