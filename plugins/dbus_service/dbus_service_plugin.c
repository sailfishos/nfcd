/*
 * Copyright (C) 2018-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2021 Jolla Ltd.
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

#include "dbus_service.h"
#include "dbus_service/org.sailfishos.nfc.Daemon.h"
#include "plugin.h"

#include <nfc_core.h>
#include <nfc_adapter.h>
#include <nfc_manager.h>
#include <nfc_peer_service.h>
#include <nfc_plugin_impl.h>

#include <gutil_idlepool.h>
#include <gutil_macros.h>
#include <gutil_misc.h>

#include <stdlib.h>

GLOG_MODULE_DEFINE("dbus-service");

/* x(DBUS_CALL,dbus_call,dbus-call) */
#define DBUS_CALLS(x) \
    x(GET_ALL, get_all, get-all) \
    x(GET_INTERFACE_VERSION, get_interface_version, get-interface-version) \
    x(GET_ADAPTERS, get_adapters, get-adapters) \
    x(GET_ALL2, get_all2, get-all2) \
    x(GET_DAEMON_VERSION, get_daemon_version, get-daemon-version) \
    x(GET_ALL3, get_all3, get-all3) \
    x(GET_MODE, get_mode, get-mode) \
    x(REQUEST_MODE, request_mode, request-mode) \
    x(RELEASE_MODE, release_mode, release-mode) \
    x(REGISTER_LOCAL_SERVICE, register_local_service, \
      register-local-service) \
    x(UNREGISTER_LOCAL_SERVICE, unregister_local_service, \
      unregister-local-service) \
    x(GET_ALL4, get_all4, get-all4) \
    x(GET_TECHS, get_techs, get-techs) \
    x(REQUEST_TECHS, request_techs, request-techs) \
    x(RELEASE_TECHS, release_techs, release-techs) \
    x(REGISTER_LOCAL_HOST_SERVICE, register_local_host_service, \
      register-local-host-service) \
    x(UNREGISTER_LOCAL_HOST_SERVICE, unregister_local_host_service, \
      unregister-local-host-service) \
    x(REGISTER_LOCAL_HOST_APP, register_local_host_app, \
      register-local-host-app) \
    x(UNREGISTER_LOCAL_HOST_APP, unregister_local_host_app, \
      unregister-local-host-app) \
    x(REGISTER_LOCAL_HOST_SERVICE2, register_local_host_service2, \
      register-local-host-service2)

enum {
    EVENT_ADAPTER_ADDED,
    EVENT_ADAPTER_REMOVED,
    EVENT_MODE_CHANGED,
    EVENT_TECHS_CHANGED,
    EVENT_COUNT
};

enum {
    #define DEFINE_ENUM(CALL,call,name) CALL_##CALL,
    DBUS_CALLS(DEFINE_ENUM)
    #undef DEFINE_ENUM
    CALL_COUNT
};

typedef struct dbus_service_client {
    char* dbus_name;
    guint watch_id;
    DBusServicePlugin* plugin;
    GHashTable* peer_services;  /* objpath => DBusServiceLocal */
    GHashTable* host_services;  /* objpath => DBusServiceLocalHost */
    GHashTable* host_apps;      /* objpath => DBusServiceLocalApp */
    GHashTable* mode_requests;  /* id => NfcModeRequest */
    GHashTable* tech_requests;  /* id => NfcTechRequest */
} DBusServiceClient;

typedef NfcPluginClass DBusServicePluginClass;
struct dbus_service_plugin {
    NfcPlugin parent;
    guint own_name_id;
    guint last_request_id;
    GUtilIdlePool* pool;
    GDBusConnection* connection;
    GHashTable* adapters;
    GHashTable* clients;
    NfcManager* manager;
    OrgSailfishosNfcDaemon* iface;
    gulong event_id[EVENT_COUNT];
    gulong call_id[CALL_COUNT];
};

#define PARENT_TYPE NFC_TYPE_PLUGIN
#define PARENT_CLASS dbus_service_plugin_parent_class
#define THIS_TYPE dbus_service_plugin_get_type()
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, THIS_TYPE, DBusServicePlugin)

G_DEFINE_TYPE(DBusServicePlugin, dbus_service_plugin, PARENT_TYPE)

#define NFC_BUS         G_BUS_TYPE_SYSTEM
#define NFC_SERVICE     "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"

#define NFC_DBUS_PLUGIN_INTERFACE_VERSION  (5)

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
void
dbus_service_plugin_peer_service_destroy(
    gpointer user_data)
{
    DBusServiceLocal* obj = user_data;
    DBusServicePlugin* plugin = obj->plugin;
    NfcPeerService* service = &obj->service;

    obj->plugin = NULL;
    nfc_manager_unregister_service(plugin->manager, service);
    nfc_peer_service_disconnect_all(service);
    nfc_peer_service_unref(service);
}

static
void
dbus_service_plugin_host_service_destroy(
    gpointer user_data)
{
    DBusServiceLocalHost* obj = user_data;
    DBusServicePlugin* plugin = obj->plugin;
    NfcHostService* service = &obj->service;

    obj->plugin = NULL;
    nfc_manager_unregister_host_service(plugin->manager, service);
    nfc_host_service_unref(service);
}

static
void
dbus_service_plugin_host_app_destroy(
    gpointer user_data)
{
    DBusServiceLocalApp* obj = user_data;
    DBusServicePlugin* plugin = obj->plugin;
    NfcHostApp* app = &obj->app;

    obj->plugin = NULL;
    nfc_manager_unregister_host_app(plugin->manager, app);
    nfc_host_app_unref(app);
}

static
void
dbus_service_plugin_client_destroy(
    void* value)
{
    DBusServiceClient* client = value;

    if (client->peer_services) {
        g_hash_table_destroy(client->peer_services);
    }
    if (client->host_services) {
        g_hash_table_destroy(client->host_services);
    }
    if (client->host_apps) {
        g_hash_table_destroy(client->host_apps);
    }
    if (client->mode_requests) {
        g_hash_table_destroy(client->mode_requests);
    }
    if (client->tech_requests) {
        g_hash_table_destroy(client->tech_requests);
    }
    g_bus_unwatch_name(client->watch_id);
    g_free(client->dbus_name);
    gutil_slice_free(client);
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

static
void
dbus_service_plugin_client_gone(
    GDBusConnection* bus,
    const char* name,
    gpointer plugin)
{
    DBusServicePlugin* self = THIS(plugin);

    GDEBUG("Name '%s' has disappeared", name);
    g_hash_table_remove(self->clients, name);
}

static
DBusServiceClient*
dbus_service_plugin_client_new(
    DBusServicePlugin* self,
    const char* dbus_name)
{
    DBusServiceClient* client = g_slice_new0(DBusServiceClient);

    client->dbus_name = g_strdup(dbus_name);
    client->watch_id = g_bus_watch_name_on_connection(self->connection,
        client->dbus_name, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
        dbus_service_plugin_client_gone, self, NULL);
    return client;
}

static
DBusServiceClient*
dbus_service_plugin_client_get(
    DBusServicePlugin* self,
    const char* dbus_name)
{
    DBusServiceClient* client = NULL;

    if (self->clients) {
        client = g_hash_table_lookup(self->clients, dbus_name);
    } else {
        self->clients = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
            dbus_service_plugin_client_destroy);
    }
    if (!client) {
        client = dbus_service_plugin_client_new(self, dbus_name);
        g_hash_table_insert(self->clients, client->dbus_name, client);
    }
    return client;
}

static
DBusServiceLocal*
dbus_service_plugin_register_local_peer_service(
    DBusServicePlugin* self,
    const char* peer_name,
    const char* obj_path,
    const char* dbus_name)
{
    DBusServiceLocal* obj = dbus_service_local_new(self->connection,
        obj_path, peer_name, dbus_name);

    if (obj) {
        NfcPeerService* service = &obj->service;

        if (nfc_manager_register_service(self->manager, service)) {
            DBusServiceClient* client = dbus_service_plugin_client_get
                (self, dbus_name);

            if (!client->peer_services) {
                client->peer_services =
                    g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                        dbus_service_plugin_peer_service_destroy);
            }
            g_hash_table_insert(client->peer_services, (gpointer)
                obj->obj_path, obj);
            obj->plugin = self;
            return obj;
        }
        nfc_peer_service_unref(service);
    }
    return NULL;
}

static
DBusServiceLocalHost*
dbus_service_plugin_register_local_host_service(
    DBusServicePlugin* self,
    const char* name,
    const char* obj_path,
    const char* dbus_name,
    int version)
{
    DBusServiceLocalHost* obj = dbus_service_local_host_new(self->connection,
        obj_path, name, dbus_name, version);

    if (obj) {
        NfcHostService* service = &obj->service;

        if (nfc_manager_register_host_service(self->manager, service)) {
            DBusServiceClient* client = dbus_service_plugin_client_get
                (self, dbus_name);

            if (!client->host_services) {
                client->host_services =
                    g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                        dbus_service_plugin_host_service_destroy);
            }
            g_hash_table_insert(client->host_services, (gpointer)
                obj->obj_path, obj);
            obj->plugin = self;
            return obj;
        }
        nfc_host_service_unref(service);
    }
    return NULL;
}

static
DBusServiceLocalApp*
dbus_service_plugin_register_local_host_app(
    DBusServicePlugin* self,
    GDBusConnection* connection,
    const char* name,
    const GUtilData* aid,
    NFC_HOST_APP_FLAGS flags,
    const char* obj_path,
    const char* dbus_name)
{
    DBusServiceLocalApp* obj = dbus_service_local_app_new(self->connection,
        obj_path, name, aid, flags, dbus_name);

    if (obj) {
        NfcHostApp* app = &obj->app;

        if (nfc_manager_register_host_app(self->manager, app)) {
            DBusServiceClient* client = dbus_service_plugin_client_get
                (self, dbus_name);

            if (!client->host_apps) {
                client->host_apps =
                    g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                        dbus_service_plugin_host_app_destroy);
            }
            g_hash_table_insert(client->host_apps, (gpointer)
                obj->obj_path, obj);
            obj->plugin = self;
            return obj;
        }
        nfc_host_app_unref(app);
    }
    return NULL;
}

static
guint
dbus_service_plugin_next_request_id(
    DBusServicePlugin* self,
    const char* sender)
{
    DBusServiceClient* client = dbus_service_plugin_client_get(self, sender);
    gconstpointer key;

    self->last_request_id++;
    key = GUINT_TO_POINTER(self->last_request_id);

    while ((client->mode_requests &&
            g_hash_table_contains(client->mode_requests, key)) ||
           (client->tech_requests &&
            g_hash_table_contains(client->tech_requests, key)) ||
           !self->last_request_id) {
        self->last_request_id++;
        key = GUINT_TO_POINTER(self->last_request_id);
    }

    return self->last_request_id;
}

/*==========================================================================*
 * NfcManager events
 *==========================================================================*/

static
void
dbus_service_plugin_event_adapter_added(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    DBusServicePlugin* self = THIS(plugin);

    if (self->connection) {
        if (dbus_service_plugin_create_adapter(self, adapter)) {
            dbus_service_plugin_adapters_changed(self);
        }
    }
}

static
void
dbus_service_plugin_event_adapter_removed(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    DBusServicePlugin* self = THIS(plugin);

    if (g_hash_table_remove(self->adapters, (void*)adapter->name)) {
        dbus_service_plugin_adapters_changed(self);
    }
}

static
void
dbus_service_plugin_event_mode_changed(
    NfcManager* manager,
    void* plugin)
{
    DBusServicePlugin* self = THIS(plugin);

    org_sailfishos_nfc_daemon_emit_mode_changed(self->iface,
        self->manager->mode);
}

static
void
dbus_service_plugin_event_techs_changed(
    NfcManager* manager,
    void* plugin)
{
    DBusServicePlugin* self = THIS(plugin);

    org_sailfishos_nfc_daemon_emit_techs_changed(self->iface,
        self->manager->techs);
}

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

static
gboolean
dbus_service_plugin_handle_get_all(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    DBusServicePlugin* self)
{
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
    DBusServicePlugin* self)
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
    DBusServicePlugin* self)
{
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
    DBusServicePlugin* self)
{
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
    DBusServicePlugin* self)
{
    org_sailfishos_nfc_daemon_complete_get_daemon_version(iface, call,
        nfc_core_version());
    return TRUE;
}

/* Interface version 3 */

static
gboolean
dbus_service_plugin_handle_get_all3(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    DBusServicePlugin* self)
{
    org_sailfishos_nfc_daemon_complete_get_all3(iface, call,
        NFC_DBUS_PLUGIN_INTERFACE_VERSION,
        dbus_service_plugin_get_adapter_paths(self),
        nfc_core_version(), self->manager->mode);
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_get_mode(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    DBusServicePlugin* self)
{
    org_sailfishos_nfc_daemon_complete_get_mode(iface, call,
        self->manager->mode);
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_request_mode(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    guint enable,
    guint disable,
    DBusServicePlugin* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    DBusServiceClient* client = dbus_service_plugin_client_get(self, sender);
    NfcModeRequest* req = nfc_manager_mode_request_new(self->manager,
        enable, disable);
    guint id = dbus_service_plugin_next_request_id(self, sender);

    if (!client->mode_requests) {
        client->mode_requests = g_hash_table_new_full(g_direct_hash,
            g_direct_equal, NULL, (GDestroyNotify)
            nfc_manager_mode_request_free);
    }
    g_hash_table_insert(client->mode_requests, GUINT_TO_POINTER(id), req);
    GDEBUG("Mode request 0x%02x/0x%02x => %s/%u", enable, disable,
        sender, id);

    org_sailfishos_nfc_daemon_complete_request_mode(iface, call, id);
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_release_mode(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    guint id,
    DBusServicePlugin* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    gboolean released = FALSE;

    if (self->clients) {
        DBusServiceClient* client = g_hash_table_lookup(self->clients, sender);

        released = (client && client->mode_requests &&
            g_hash_table_remove(client->mode_requests, GUINT_TO_POINTER(id)));
    }
    if (released) {
        GDEBUG("Mode request %s/%u released", sender, id);
        org_sailfishos_nfc_daemon_complete_release_mode(iface, call);
    } else {
        GDEBUG("Mode request %s/%u not found", sender, id);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_FOUND,
                "Invalid mode request %s/%u", sender, id);
    }
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_register_local_service(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    const char* obj_path,
    const char* sn,
    DBusServicePlugin* self)
{
    DBusServiceLocal* local = NULL;
    const char* sender = g_dbus_method_invocation_get_sender(call);

    if (self->clients) {
        DBusServiceClient* client = g_hash_table_lookup(self->clients, sender);

        if (client && client->peer_services) {
            local = g_hash_table_lookup(client->peer_services, obj_path);
        }
    }
    if (local) {
        GWARN("Duplicate service %s%s", sender, obj_path);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_ALREADY_EXISTS,
            "Service '%s' already registered", obj_path);
    } else {
        local = dbus_service_plugin_register_local_peer_service(self, sn,
            obj_path, sender);
        if (local) {
            GDEBUG("Registered service %s%s (SAP %u)", sender, obj_path,
                local->service.sap);
            org_sailfishos_nfc_daemon_complete_register_local_service(iface,
                call, local->service.sap);
        } else {
            g_dbus_method_invocation_return_error(call,
                DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
                "Failed to register service %s%s", sender, obj_path);
        }
    }
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_unregister_local_service(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    const char* obj_path,
    DBusServicePlugin* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    gboolean removed = FALSE;

    if (self->clients) {
        DBusServiceClient* client = g_hash_table_lookup(self->clients, sender);

        removed = (client && client->peer_services &&
            g_hash_table_remove(client->peer_services, obj_path));
    }
    if (removed) {
        GDEBUG("Unregistered service %s%s", sender, obj_path);
        org_sailfishos_nfc_daemon_complete_unregister_local_service
            (iface, call);
    } else {
        GDEBUG("Service %s%s is not registered", sender, obj_path);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_FOUND,
                "Service %s%s is not registered", sender, obj_path);
    }
    return TRUE;
}

/* Interface version 4 */

static
gboolean
dbus_service_plugin_handle_get_all4(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    DBusServicePlugin* self)
{
    NfcManager* manager = self->manager;

    org_sailfishos_nfc_daemon_complete_get_all4(iface, call,
        NFC_DBUS_PLUGIN_INTERFACE_VERSION,
        dbus_service_plugin_get_adapter_paths(self),
        nfc_core_version(), manager->mode, manager->techs);
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_get_techs(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    DBusServicePlugin* self)
{
    org_sailfishos_nfc_daemon_complete_get_techs(iface, call,
        self->manager->techs);
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_request_techs(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    guint enable,
    guint disable,
    DBusServicePlugin* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    DBusServiceClient* client = dbus_service_plugin_client_get(self, sender);
    NfcTechRequest* req = nfc_manager_tech_request_new(self->manager,
        enable, disable);
    guint id = dbus_service_plugin_next_request_id(self, sender);

    if (!client->tech_requests) {
        client->tech_requests = g_hash_table_new_full(g_direct_hash,
            g_direct_equal, NULL, (GDestroyNotify)
            nfc_manager_tech_request_free);
    }
    g_hash_table_insert(client->tech_requests, GUINT_TO_POINTER(id), req);
    GDEBUG("Tech request 0x%02x/0x%02x => %s/%u", enable, disable,
        sender, id);

    org_sailfishos_nfc_daemon_complete_request_techs(iface, call, id);
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_release_techs(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    guint id,
    DBusServicePlugin* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    gboolean released = FALSE;

    if (self->clients) {
        DBusServiceClient* client = g_hash_table_lookup(self->clients, sender);

        released = (client && client->tech_requests &&
            g_hash_table_remove(client->tech_requests, GUINT_TO_POINTER(id)));
    }
    if (released) {
        GDEBUG("Tech request %s/%u released", sender, id);
        org_sailfishos_nfc_daemon_complete_release_techs(iface, call);
    } else {
        GDEBUG("Tech request %s/%u not found", sender, id);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_FOUND,
                "Invalid tech request %s/%u", sender, id);
    }
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_register_local_host_service_impl(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    const char* obj_path,
    const char* name,
    gint version,
    DBusServicePlugin* self,
    void (*complete)(OrgSailfishosNfcDaemon* iface, GDBusMethodInvocation* call))
{
    DBusServiceLocalHost* obj = NULL;
    const char* sender = g_dbus_method_invocation_get_sender(call);

    if (self->clients) {
        DBusServiceClient* client = g_hash_table_lookup(self->clients, sender);

        if (client && client->host_services) {
            obj = g_hash_table_lookup(client->host_services, obj_path);
        }
    }
    if (obj) {
        GWARN("Duplicate host service %s%s", sender, obj_path);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_ALREADY_EXISTS,
            "Host service '%s' is already registered", obj_path);
    } else {
        obj = dbus_service_plugin_register_local_host_service(self, name,
            obj_path, sender, version);
        if (obj) {
            GDEBUG("Host service '%s' %s%s", name, sender, obj_path);
            complete(iface, call);
        } else {
            g_dbus_method_invocation_return_error(call,
                DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
                "Failed to register host service %s%s", sender, obj_path);
        }
    }
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_register_local_host_service(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    const char* obj_path,
    const char* name,
    DBusServicePlugin* self)
{
    return dbus_service_plugin_handle_register_local_host_service_impl(iface,
        call, obj_path, name, 1, self,
        org_sailfishos_nfc_daemon_complete_register_local_host_service);
}

static
gboolean
dbus_service_plugin_handle_register_local_host_service2(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    const char* obj_path,
    const char* name,
    gint version,
    DBusServicePlugin* self)
{
    return dbus_service_plugin_handle_register_local_host_service_impl(iface,
        call, obj_path, name, version, self,
        org_sailfishos_nfc_daemon_complete_register_local_host_service2);
}

static
gboolean
dbus_service_plugin_handle_unregister_local_host_service(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    const char* obj_path,
    DBusServicePlugin* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    gboolean removed = FALSE;

    if (self->clients) {
        DBusServiceClient* client = g_hash_table_lookup(self->clients, sender);

        removed = (client && client->host_services &&
            g_hash_table_remove(client->host_services, obj_path));
    }
    if (removed) {
        GDEBUG("Unregistered host service %s%s", sender, obj_path);
        org_sailfishos_nfc_daemon_complete_unregister_local_host_service
            (iface, call);
    } else {
        GDEBUG("Host service %s%s is not registered", sender, obj_path);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_FOUND,
                "Host service %s%s is not registered", sender, obj_path);
    }
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_register_local_host_app(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    const char* obj_path,
    const char* name,
    GVariant* aid_var,
    guint flags,
    DBusServicePlugin* self)
{
    DBusServiceLocalApp* obj = NULL;
    const char* sender = g_dbus_method_invocation_get_sender(call);
    GUtilData aid;

    aid.size = g_variant_get_size(aid_var);
    aid.bytes = g_variant_get_data(aid_var);
    if (self->clients) {
        DBusServiceClient* client = g_hash_table_lookup(self->clients, sender);

        if (client && client->host_apps) {
            obj = g_hash_table_lookup(client->host_apps, obj_path);
        }
    }
    if (obj) {
        GWARN("Duplicate app %s%s", sender, obj_path);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_ALREADY_EXISTS,
            "App '%s' is already registered", obj_path);
    } else {
        obj = dbus_service_plugin_register_local_host_app(self,
            self->connection, name, &aid, flags, obj_path, sender);
        if (obj) {
            GDEBUG("App '%s' %s%s", name, sender, obj_path);
            org_sailfishos_nfc_daemon_complete_register_local_host_app
                (iface, call);
        } else {
            g_dbus_method_invocation_return_error(call,
                DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
                "Failed to register app %s%s", sender, obj_path);
        }
    }
    return TRUE;
}

static
gboolean
dbus_service_plugin_handle_unregister_local_host_app(
    OrgSailfishosNfcDaemon* iface,
    GDBusMethodInvocation* call,
    const char* obj_path,
    DBusServicePlugin* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    gboolean removed = FALSE;

    if (self->clients) {
        DBusServiceClient* client = g_hash_table_lookup(self->clients, sender);

        removed = (client && client->host_apps &&
            g_hash_table_remove(client->host_apps, obj_path));
    }
    if (removed) {
        GDEBUG("App %s%s is unregistered", sender, obj_path);
        org_sailfishos_nfc_daemon_complete_unregister_local_host_app
            (iface, call);
    } else {
        GDEBUG("App %s%s is not registered", sender, obj_path);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_FOUND,
                "App %s%s is not registered", sender, obj_path);
    }
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
    DBusServicePlugin* self = THIS(plugin);
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
    DBusServicePlugin* self = THIS(plugin);

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
    DBusServicePlugin* self = THIS(plugin);

    GVERBOSE("Starting");
    self->manager = nfc_manager_ref(manager);
    self->iface = org_sailfishos_nfc_daemon_skeleton_new();
    self->own_name_id = g_bus_own_name(NFC_BUS, NFC_SERVICE,
        G_BUS_NAME_OWNER_FLAGS_REPLACE, dbus_service_plugin_bus_connected,
        dbus_service_plugin_name_acquired, dbus_service_plugin_name_lost,
        self, NULL);

    /* NfcManager events */
    self->event_id[EVENT_ADAPTER_ADDED] =
        nfc_manager_add_adapter_added_handler(manager,
            dbus_service_plugin_event_adapter_added, self);
    self->event_id[EVENT_ADAPTER_REMOVED] =
        nfc_manager_add_adapter_removed_handler(manager,
            dbus_service_plugin_event_adapter_removed, self);
    self->event_id[EVENT_MODE_CHANGED] =
        nfc_manager_add_mode_changed_handler(manager,
            dbus_service_plugin_event_mode_changed, self);
    self->event_id[EVENT_TECHS_CHANGED] =
        nfc_manager_add_techs_changed_handler(manager,
            dbus_service_plugin_event_techs_changed, self);

    /* Hook up D-Bus calls */
    #define CONNECT_HANDLER(CALL,call,name) self->call_id[CALL_##CALL] = \
        g_signal_connect(self->iface, "handle-"#name, \
        G_CALLBACK(dbus_service_plugin_handle_##call), self);
    DBUS_CALLS(CONNECT_HANDLER)
    #undef CONNECT_HANDLER

    return TRUE;
}

static
void
dbus_service_plugin_stop(
    NfcPlugin* plugin)
{
    DBusServicePlugin* self = THIS(plugin);

    GVERBOSE("Stopping");
    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);
    g_hash_table_remove_all(self->adapters);
    g_bus_unown_name(self->own_name_id);
    if (self->connection) {
        g_dbus_interface_skeleton_unexport
            (G_DBUS_INTERFACE_SKELETON(self->iface));
        g_object_unref(self->connection);
        self->connection = NULL;
    }
    g_object_unref(self->iface);
    nfc_manager_remove_all_handlers(self->manager, self->event_id);
    nfc_manager_unref(self->manager);
}

DBusServicePeer*
dbus_service_plugin_find_peer(
    DBusServicePlugin* self,
    NfcPeer* peer)
{
    GHashTableIter it;
    gpointer value;

    g_hash_table_iter_init(&it, self->adapters);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        DBusServicePeer* dbus_peer = dbus_service_adapter_find_peer
            ((DBusServiceAdapter*)value, peer);

        if (dbus_peer) {
            return dbus_peer;
        }
    }
    return NULL;
}

DBusServiceHost*
dbus_service_plugin_find_host(
    DBusServicePlugin* self,
    NfcHost* host)
{
    GHashTableIter it;
    gpointer value;

    g_hash_table_iter_init(&it, self->adapters);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        DBusServiceHost* dbus_host = dbus_service_adapter_find_host
            ((DBusServiceAdapter*)value, host);

        if (dbus_host) {
            return dbus_host;
        }
    }
    return NULL;
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
    DBusServicePlugin* self = THIS(plugin);

    if (self->clients) {
        g_hash_table_destroy(self->clients);
    }
    g_hash_table_destroy(self->adapters);
    gutil_idle_pool_destroy(self->pool);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(plugin);
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
    return g_object_new(THIS_TYPE, NULL);
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
