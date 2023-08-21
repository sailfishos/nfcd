/*
 * Copyright (C) 2020-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2020 Jolla Ltd.
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

#include "dbus_service.h"
#include "dbus_service/org.sailfishos.nfc.Peer.h"

#include <nfc_peer.h>
#include <nfc_peer_service_impl.h>
#include <nfc_peer_socket.h>

#include <gutil_macros.h>
#include <gutil_misc.h>

#include <gio/gunixfdlist.h>

enum {
    PEER_INITIALIZED,
    PEER_WELL_KNOWN_SERVICES_CHANGED,
    PEER_EVENT_COUNT
};

enum {
    CALL_GET_ALL,
    CALL_GET_INTERFACE_VERSION,
    CALL_GET_PRESENT,
    CALL_GET_TECHNOLOGY,
    CALL_GET_INTERFACES,
    CALL_GET_WELL_KNOWN_SERVICES,
    CALL_DEACTIVATE,
    CALL_CONNECT_ACCESS_POINT,
    CALL_CONNECT_SERVICE_NAME,
    CALL_COUNT
};

typedef struct dbus_service_peer_call DBusServicePeerCall;
typedef struct dbus_service_peer_priv DBusServicePeerPriv;

typedef
void
(*DBusServicePeerCallFunc)(
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self);

struct dbus_service_peer_call {
    DBusServicePeerCall* next;
    GDBusMethodInvocation* invocation;
    DBusServicePeerCallFunc func;
};

typedef struct dbus_service_peer_call_queue {
    DBusServicePeerCall* first;
    DBusServicePeerCall* last;
} DBusServicePeerCallQueue;

typedef
void
(*DBusServicePeerAsyncConnectCompleteFunc)(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    GUnixFDList* fdl,
    GVariant* fd);

typedef struct dbus_service_peer_async_connect {
    OrgSailfishosNfcPeer* iface;
    GDBusMethodInvocation* call;
    NfcPeerConnection* connection;
    DBusServicePeerAsyncConnectCompleteFunc complete;
} DBusServicePeerAsyncConnect;

struct dbus_service_peer_priv {
    DBusServicePeer pub;
    char* path;
    DBusServicePeerCallQueue queue;
    OrgSailfishosNfcPeer* iface;
    gulong call_id[CALL_COUNT];
    gulong peer_event_id[PEER_EVENT_COUNT];
    NfcPeerService* peer_client;
};

#define NFC_DBUS_PEER_INTERFACE "org.sailfishos.nfc.Peer"
#define NFC_DBUS_PEER_INTERFACE_VERSION  (1)

static const char* const dbus_service_peer_default_interfaces[] = {
    NFC_DBUS_PEER_INTERFACE, NULL
};

static inline DBusServicePeerPriv* dbus_service_peer_cast(DBusServicePeer* pub)
    { return G_LIKELY(pub) ? G_CAST(pub, DBusServicePeerPriv, pub) : NULL; }

/*==========================================================================*
 * Peer client
 *==========================================================================*/

typedef NfcPeerServiceClass DBusServicePeerClientClass;
typedef struct dbus_service_peer_client {
    NfcPeerService service;
} DBusServicePeerClient;

#define PARENT_TYPE NFC_TYPE_PEER_SERVICE
#define THIS_TYPE dbus_service_peer_client_get_type()

G_DEFINE_TYPE(DBusServicePeerClient, dbus_service_peer_client, PARENT_TYPE)

static
NfcPeerConnection*
dbus_service_peer_client_new_connect(
    NfcPeerService* self,
    guint8 rsap,
    const char* name)
{
    return (NfcPeerConnection*)nfc_peer_socket_new_connect(self, rsap, name);
}

static
void
dbus_service_peer_client_init(
    DBusServicePeerClient* self)
{
}

static
void
dbus_service_peer_client_class_init(
    DBusServicePeerClientClass* klass)
{
    klass->new_connect = dbus_service_peer_client_new_connect;
}

static
NfcPeerService*
dbus_service_peer_client_get(
    DBusServicePeerPriv* self)
{
    if (!self->peer_client) {
        DBusServicePeerClient* client = g_object_new(THIS_TYPE, NULL);

        self->peer_client = NFC_PEER_SERVICE(client);
        nfc_peer_service_init_base(self->peer_client, NULL);
        if (!nfc_peer_register_service(self->pub.peer, self->peer_client)) {
            nfc_peer_service_unref(self->peer_client);
            self->peer_client = NULL;
        }
    }
    return self->peer_client;
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
dbus_service_peer_free_call(
    DBusServicePeerCall* call)
{
    g_object_unref(call->invocation);
    g_slice_free(DBusServicePeerCall, call);
}

static
void
dbus_service_peer_queue_call(
    DBusServicePeerCallQueue* queue,
    GDBusMethodInvocation* invocation,
    DBusServicePeerCallFunc func)
{
    DBusServicePeerCall* call = g_slice_new0(DBusServicePeerCall);

    g_object_ref(call->invocation = invocation);
    call->func = func;
    if (queue->last) {
        queue->last->next = call;
    } else {
        queue->first = call;
    }
    queue->last = call;
}

static
DBusServicePeerCall*
dbus_service_peer_dequeue_call(
    DBusServicePeerCallQueue* queue)
{
    DBusServicePeerCall* call = queue->first;

    if (call) {
        queue->first = call->next;
        if (!queue->first) {
            queue->last = NULL;
        }
    }
    return call;
}

static
gboolean
dbus_service_peer_handle_call(
    DBusServicePeerPriv* self,
    GDBusMethodInvocation* call,
    DBusServicePeerCallFunc func)
{
    NfcPeer* peer = self->pub.peer;

    if (peer->flags & NFC_PEER_FLAG_INITIALIZED) {
        func(call, self);
    } else {
        dbus_service_peer_queue_call(&self->queue, call, func);
    }
    return TRUE;
}

static
void
dbus_service_peer_complete_pending_calls(
    DBusServicePeerPriv* self)
{
    DBusServicePeerCall* call;

    while ((call = dbus_service_peer_dequeue_call(&self->queue)) != NULL) {
        call->func(call->invocation, self);
        dbus_service_peer_free_call(call);
    }
}

/*==========================================================================*
 * NfcPeer events
 *==========================================================================*/

static
void
dbus_service_peer_well_known_services_changed(
    NfcPeer* peer,
    void* user_data)
{
    DBusServicePeerPriv* self = user_data;

    org_sailfishos_nfc_peer_emit_well_known_services_changed
        (self->iface, peer->wks);
}

static
void
dbus_service_peer_initialized(
    NfcPeer* peer,
    void* user_data)
{
    DBusServicePeerPriv* self = user_data;

    nfc_peer_remove_handlers(peer, self->peer_event_id + PEER_INITIALIZED, 1);
    dbus_service_peer_complete_pending_calls(self);
    self->peer_event_id[PEER_WELL_KNOWN_SERVICES_CHANGED] =
        nfc_peer_add_wks_changed_handler(peer,
            dbus_service_peer_well_known_services_changed, self);
}

/*==========================================================================*
 * Async call context
 *==========================================================================*/

static
DBusServicePeerAsyncConnect*
dbus_service_peer_async_connect_new(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    DBusServicePeerAsyncConnectCompleteFunc complete)
{
    DBusServicePeerAsyncConnect* connect =
        g_slice_new0(DBusServicePeerAsyncConnect);

    g_object_ref(connect->iface = iface);
    g_object_ref(connect->call = call);
    connect->complete = complete;
    return connect;
}

static
void
dbus_service_peer_async_connect_error(
    DBusServicePeerAsyncConnect* connect,
    DBusServiceError error,
    const char* message)
{
    g_dbus_method_invocation_return_error_literal(connect->call,
        DBUS_SERVICE_ERROR, error, message ? message :
        "Data link connection failed");
    g_object_unref(connect->call);
    connect->call = NULL;
}

static
void
dbus_service_peer_async_connect_failed(
    DBusServicePeerAsyncConnect* connect,
    const char* message)
{
    dbus_service_peer_async_connect_error(connect,
        DBUS_SERVICE_ERROR_FAILED, NULL);
}

static
void
dbus_service_peer_async_connect_free(
    DBusServicePeerAsyncConnect* connect)
{
    nfc_peer_connection_unref(connect->connection);
    if (connect->call) dbus_service_peer_async_connect_failed(connect, NULL);
    g_object_unref(connect->iface);
    g_slice_free1(sizeof(*connect), connect);
}

static
void
dbus_service_peer_async_connect_free1(
    void* async)
{
    dbus_service_peer_async_connect_free(async);
}

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

/* GetAll */

static
void
dbus_service_peer_complete_get_all(
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    NfcPeer* peer = self->pub.peer;

    org_sailfishos_nfc_peer_complete_get_all(self->iface, call,
        NFC_DBUS_PEER_INTERFACE_VERSION, peer->present, peer->technology,
        dbus_service_peer_default_interfaces, peer->wks);
}

static
gboolean
dbus_service_peer_handle_get_all(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    /* Queue the call if the peer is not initialized yet */
    return dbus_service_peer_handle_call(self, call,
        dbus_service_peer_complete_get_all);
}

/* GetInterfaceVersion */

static
gboolean
dbus_service_peer_handle_get_interface_version(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    org_sailfishos_nfc_peer_complete_get_interface_version(iface, call,
        NFC_DBUS_PEER_INTERFACE_VERSION);
    return TRUE;
}

/* GetPresent */

static
gboolean
dbus_service_peer_handle_get_present(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    org_sailfishos_nfc_peer_complete_get_present(iface, call,
        self->pub.peer->present);
    return TRUE;
}

/* GetTechnology */

static
gboolean
dbus_service_peer_handle_get_technology(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    org_sailfishos_nfc_peer_complete_get_technology(iface, call,
        self->pub.peer->technology);
    return TRUE;
}

/* GetInterfaces */

static
gboolean
dbus_service_peer_handle_get_interfaces(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    org_sailfishos_nfc_peer_complete_get_interfaces(self->iface, call,
        dbus_service_peer_default_interfaces);
    return TRUE;
}

/* GetWellKnownServices */

static
void
dbus_service_peer_complete_get_well_known_services(
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    org_sailfishos_nfc_peer_complete_get_well_known_services(self->iface,
        call, self->pub.peer->wks);
}

static
gboolean
dbus_service_peer_handle_get_well_known_services(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    /* Queue the call if the peer is not initialized yet */
    return dbus_service_peer_handle_call(self, call,
        dbus_service_peer_complete_get_well_known_services);
}

/* Deactivate */

static
gboolean
dbus_service_peer_handle_deactivate(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    DBusServicePeerPriv* self)
{
    nfc_peer_deactivate(self->pub.peer);
    org_sailfishos_nfc_peer_complete_deactivate(iface, call);
    return TRUE;
}

/* ConnectAccessPoint */

static
void
dbus_service_peer_async_connect_complete(
    NfcPeer* peer,
    NfcPeerConnection* connection,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data)
{
    DBusServicePeerAsyncConnect* connect = user_data;
    NfcPeerSocket* socket = NFC_PEER_SOCKET(connection);

    /*
     * Note: dbus_service_peer_async_connect_free() will still
     * complete the call with a default error, even if we miss all
     * the cases in this switch.
     */
    switch (result) {
    case NFC_PEER_CONNECT_OK:
        GDEBUG("Data link connection established");
        connect->complete(connect->iface, connect->call, socket->fdl,
            g_variant_new_handle(0));
        g_object_unref(connect->call);
        connect->call = NULL;
        break;
    case NFC_PEER_CONNECT_CANCELLED:
    case NFC_PEER_CONNECT_NO_SERVICE:
        GDEBUG("Data link connection refused (no service)");
        dbus_service_peer_async_connect_error(connect,
            DBUS_SERVICE_ERROR_NO_SERVICE, "No such service");
        break;
    case NFC_PEER_CONNECT_REJECTED:
        GDEBUG("Data link connection rejected");
        dbus_service_peer_async_connect_error(connect,
            DBUS_SERVICE_ERROR_NO_SERVICE, "Connection rejected");
        break;
    case NFC_PEER_CONNECT_DUP:
    case NFC_PEER_CONNECT_FAILED:
        GDEBUG("Data link connection failed");
        dbus_service_peer_async_connect_failed(connect, NULL);
        break;
    }
}

static
gboolean
dbus_service_peer_handle_connect_access_point(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    GUnixFDList* fdlist,
    guint rsap,
    DBusServicePeerPriv* self)
{
    DBusServicePeerAsyncConnect* connect =
        dbus_service_peer_async_connect_new(iface, call,
            org_sailfishos_nfc_peer_complete_connect_access_point);

    GDEBUG("Connecting to SAP %u", rsap);
    connect->connection =
        nfc_peer_connection_ref(nfc_peer_connect(self->pub.peer,
            dbus_service_peer_client_get(self), rsap, 
            dbus_service_peer_async_connect_complete,
            dbus_service_peer_async_connect_free1, connect));
    if (!connect->connection) {
        dbus_service_peer_async_connect_failed(connect,
            "Failed to set up data link connection");
        dbus_service_peer_async_connect_free(connect);
    }
    return TRUE;
}

/* ConnectServiceName */

static
gboolean
dbus_service_peer_handle_connect_service_name(
    OrgSailfishosNfcPeer* iface,
    GDBusMethodInvocation* call,
    GUnixFDList* fdlist,
    const char* sn,
    DBusServicePeerPriv* self)
{
    DBusServicePeerAsyncConnect* connect =
        dbus_service_peer_async_connect_new(iface, call,
            org_sailfishos_nfc_peer_complete_connect_service_name);

    GDEBUG("Connecting to \"%s\"", sn);
    connect->connection =
        nfc_peer_connection_ref(nfc_peer_connect_sn(self->pub.peer,
            dbus_service_peer_client_get(self), sn, 
            dbus_service_peer_async_connect_complete,
            dbus_service_peer_async_connect_free1, connect));
    if (!connect->connection) {
        dbus_service_peer_async_connect_failed(connect,
            "Failed to set up data link connection");
        dbus_service_peer_async_connect_free(connect);
    }
    return TRUE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_peer_free_unexported(
    DBusServicePeerPriv* self)
{
    DBusServicePeerCall* call;
    DBusServicePeer* pub = &self->pub;

    nfc_peer_remove_all_handlers(pub->peer, self->peer_event_id);
    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);

    /* Cancel pending calls if there are any */
    while ((call = dbus_service_peer_dequeue_call(&self->queue)) != NULL) {
        g_dbus_method_invocation_return_error_literal(call->invocation,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_ABORTED, "Object is gone");
        dbus_service_peer_free_call(call);
    }

    nfc_peer_service_unref(self->peer_client);
    nfc_peer_unref(pub->peer);
    g_object_unref(pub->connection);
    g_object_unref(self->iface);
    g_free(self->path);
    g_free(self);
}

DBusServicePeer*
dbus_service_peer_new(
    NfcPeer* peer,
    const char* parent_path,
    GDBusConnection* connection)
{
    DBusServicePeerPriv* self = g_new0(DBusServicePeerPriv, 1);
    DBusServicePeer* pub = &self->pub;
    GError* error = NULL;

    g_object_ref(pub->connection = connection);
    pub->path = self->path = g_strconcat(parent_path, "/", peer->name, NULL);
    pub->peer = nfc_peer_ref(peer);
    self->iface = org_sailfishos_nfc_peer_skeleton_new();

    /* D-Bus calls */
    self->call_id[CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_service_peer_handle_get_all), self);
    self->call_id[CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_service_peer_handle_get_interface_version), self);
    self->call_id[CALL_GET_PRESENT] =
        g_signal_connect(self->iface, "handle-get-present",
        G_CALLBACK(dbus_service_peer_handle_get_present), self);
    self->call_id[CALL_GET_TECHNOLOGY] =
        g_signal_connect(self->iface, "handle-get-technology",
        G_CALLBACK(dbus_service_peer_handle_get_technology), self);
    self->call_id[CALL_GET_INTERFACES] =
        g_signal_connect(self->iface, "handle-get-interfaces",
        G_CALLBACK(dbus_service_peer_handle_get_interfaces), self);
    self->call_id[CALL_GET_WELL_KNOWN_SERVICES] =
        g_signal_connect(self->iface, "handle-get-well-known-services",
        G_CALLBACK(dbus_service_peer_handle_get_well_known_services), self);
    self->call_id[CALL_DEACTIVATE] =
        g_signal_connect(self->iface, "handle-deactivate",
        G_CALLBACK(dbus_service_peer_handle_deactivate), self);
    self->call_id[CALL_CONNECT_ACCESS_POINT] =
        g_signal_connect(self->iface, "handle-connect-access-point",
        G_CALLBACK(dbus_service_peer_handle_connect_access_point), self);
    self->call_id[CALL_CONNECT_SERVICE_NAME] =
        g_signal_connect(self->iface, "handle-connect-service-name",
        G_CALLBACK(dbus_service_peer_handle_connect_service_name), self);

    if (peer->present && !(peer->flags & NFC_PEER_FLAG_INITIALIZED)) {
        /* Have to wait until the peer is initialized */
        self->peer_event_id[PEER_INITIALIZED] =
            nfc_peer_add_initialized_handler(peer,
                dbus_service_peer_initialized, self);
    }
    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, self->path, &error)) {
        GDEBUG("Created D-Bus object %s (Peer)", self->path);
        return pub;
    } else {
        GERR("%s: %s", self->path, GERRMSG(error));
        g_error_free(error);
        dbus_service_peer_free_unexported(self);
        return NULL;
    }
}

void
dbus_service_peer_free(
    DBusServicePeer* pub)
{
    if (pub) {
        DBusServicePeerPriv* self = dbus_service_peer_cast(pub);

        GDEBUG("Removing D-Bus object %s (Peer)", self->path);
        org_sailfishos_nfc_peer_emit_removed(self->iface);
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON
            (self->iface));
        dbus_service_peer_free_unexported(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
