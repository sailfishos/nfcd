/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
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

#include "dbus_service.h"
#include "dbus_service_util.h"
#include "dbus_service/org.sailfishos.nfc.LocalService.h"

#include <nfc_peer_connection_impl.h>
#include <nfc_peer_service_impl.h>
#include <nfc_peer_socket_impl.h>
#include <nfc_tag.h>

typedef NfcPeerServiceClass DBusServiceLocalObjectClass;
typedef struct dbus_service_local_object {
    DBusServiceLocal pub;
    OrgSailfishosNfcLocalService* proxy;
    char* peer_path;
    char* dbus_name;
    char* obj_path;
    guint watch_id;
} DBusServiceLocalObject;

#define DBUS_SERVICE_TYPE_LOCAL_OBJECT (dbus_service_local_object_get_type())
G_DEFINE_TYPE(DBusServiceLocalObject, dbus_service_local_object, \
        NFC_TYPE_PEER_SERVICE)
#define DBUS_SERVICE_LOCAL_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj,\
        DBUS_SERVICE_TYPE_LOCAL_OBJECT, DBusServiceLocalObject))

typedef NfcPeerSocketClass DBusServiceConnectionClass;
typedef struct dbus_service_connection {
    NfcPeerSocket socket;
    GCancellable* cancel_accept;
    OrgSailfishosNfcLocalService* proxy;
} DBusServiceConnection;

#define DBUS_SERVICE_TYPE_CONNECTION (dbus_service_connection_get_type())
G_DEFINE_TYPE(DBusServiceConnection, dbus_service_connection, \
        NFC_TYPE_PEER_SOCKET)
#define DBUS_SERVICE_CONNECTION(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj,\
        DBUS_SERVICE_TYPE_CONNECTION, DBusServiceConnection))

#define LOCAL_SERVICE_INTERFACE  "org.sailfishos.nfc.LocalService"
#define PEER_ARRIVED             "PeerArrived"
#define PEER_LEFT                "PeerLeft"
#define DATAGRAM_RECEIVED        "DatagramReceived"

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
GDBusConnection*
dbus_service_local_connection(
    DBusServiceLocalObject* self)
{
    return g_dbus_proxy_get_connection(G_DBUS_PROXY(self->proxy));
}

static
void
dbus_service_local_peer_notify(
    DBusServiceLocalObject* self,
    const char* method,
    const char* peer_path)
{
    GDBusConnection* connection = dbus_service_local_connection(self);
    GDBusMessage* message = g_dbus_message_new_method_call(self->dbus_name,
        self->obj_path, LOCAL_SERVICE_INTERFACE, method);

    /*
     * Generated stub doesn't allow setting "no-reply-expected" flag,
     * we have to build and send D-Bus message manually.
     */
    g_dbus_message_set_flags(message, g_dbus_message_get_flags(message) |
        G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    g_dbus_message_set_body(message, g_variant_new("(o)", peer_path));
    g_dbus_connection_send_message(connection, message,
        G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, NULL);
    g_object_unref(message);
}

static
void
dbus_service_local_peer_left_notify(
    DBusServiceLocalObject* self)
{
    if (self->peer_path) {
        dbus_service_local_peer_notify(self, PEER_LEFT, self->peer_path);
        g_free(self->peer_path);
        self->peer_path = NULL;
    }
}

/*==========================================================================*
 * Connection
 *==========================================================================*/

static
void
dbus_service_connection_state_changed(
    NfcPeerConnection* connection)
{
    DBusServiceConnection* self = DBUS_SERVICE_CONNECTION(connection);

    if (self->cancel_accept && connection->state != NFC_LLC_CO_ACCEPTING) {
        GDEBUG("Cancelling Accept D-Bus call");
        g_cancellable_cancel(self->cancel_accept);
        g_object_unref(self->cancel_accept);
        self->cancel_accept = NULL;
    }
    NFC_PEER_CONNECTION_CLASS(dbus_service_connection_parent_class)->
        state_changed(connection);
}

static
void
dbus_service_connection_accept_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    DBusServiceConnection* self = DBUS_SERVICE_CONNECTION(user_data);
    NfcPeerConnection* connection = NFC_PEER_CONNECTION(self);
    gboolean accepted = FALSE;
    GError* error = NULL;

    if (self->cancel_accept) {
        g_object_unref(self->cancel_accept);
        self->cancel_accept = NULL;
    }
    if (org_sailfishos_nfc_local_service_call_accept_finish(self->proxy,
        &accepted, NULL, result, &error)) {
        if (accepted) {
            nfc_peer_connection_accepted(connection);
        } else {
            nfc_peer_connection_rejected(connection);
        }
    } else {
        GDEBUG("%s", GERRMSG(error));
        g_error_free(error);
        nfc_peer_connection_rejected(connection);
    }
    nfc_peer_connection_unref(connection);
}

static
void
dbus_service_connection_accept(
    NfcPeerConnection* conn)
{
    DBusServiceConnection* self = DBUS_SERVICE_CONNECTION(conn);
    NfcPeerSocket* socket = &self->socket;

    /* Ask D-Bus client to accept the connection */
    nfc_peer_connection_ref(conn);
    self->cancel_accept = g_cancellable_new();
    org_sailfishos_nfc_local_service_call_accept(self->proxy, conn->rsap,
        g_variant_new_handle(0), socket->fdl, self->cancel_accept,
        dbus_service_connection_accept_done, self);
}

static
void
dbus_service_connection_finalize(
    GObject* object)
{
    DBusServiceConnection* self = DBUS_SERVICE_CONNECTION(object);

    /*
     * self->cancel_accept must be NULL because this object remains
     * referenced for the entire duration of the Accept call.
     */
    GASSERT(!self->cancel_accept);
    if (self->proxy) {
        g_object_unref(self->proxy);
    }
    G_OBJECT_CLASS(dbus_service_connection_parent_class)->finalize(object);
}

static
void
dbus_service_connection_init(
    DBusServiceConnection* self)
{
}

static
void
dbus_service_connection_class_init(
    DBusServiceConnectionClass* klass)
{
    NfcPeerConnectionClass* connection = NFC_PEER_CONNECTION_CLASS(klass);

    connection->accept = dbus_service_connection_accept;
    connection->state_changed = dbus_service_connection_state_changed;
    G_OBJECT_CLASS(klass)->finalize = dbus_service_connection_finalize;
}

static
NfcPeerConnection*
dbus_service_connection_new(
    OrgSailfishosNfcLocalService* proxy,
    NfcPeerService* service,
    guint8 rsap)
{
    DBusServiceConnection* self = g_object_new
        (DBUS_SERVICE_TYPE_CONNECTION, NULL);

    if (nfc_peer_socket_init_accept(&self->socket, service, rsap)) {
        g_object_ref(self->proxy = proxy);
        return NFC_PEER_CONNECTION(self);
    } else {
        g_object_unref(self);
        return NULL;
    }
}

/*==========================================================================*
 * Service
 *==========================================================================*/

static
void
dbus_service_local_peer_arrived(
    NfcPeerService* service,
    NfcPeer* peer)
{
    DBusServiceLocalObject* self = DBUS_SERVICE_LOCAL_OBJECT(service);
    DBusServicePlugin* plugin = self->pub.plugin;
    DBusServicePeer* dbus_peer = dbus_service_plugin_find_peer(plugin, peer);
    const char* path = dbus_peer ? dbus_peer->path : NULL;

    dbus_service_local_peer_left_notify(self);
    if (path) {
        dbus_service_local_peer_notify(self, PEER_ARRIVED, path);
        self->peer_path = g_strdup(path);
    }
    NFC_PEER_SERVICE_CLASS(dbus_service_local_object_parent_class)->
        peer_arrived(service, peer);
}

static
void
dbus_service_local_peer_left(
   NfcPeerService* service,
   NfcPeer* peer)
{
    dbus_service_local_peer_left_notify(DBUS_SERVICE_LOCAL_OBJECT(service));
    NFC_PEER_SERVICE_CLASS(dbus_service_local_object_parent_class)->
        peer_left(service, peer);
}

static
NfcPeerConnection*
dbus_service_local_new_accept(
    NfcPeerService* service,
    guint8 rsap)
{
    DBusServiceLocalObject* self = DBUS_SERVICE_LOCAL_OBJECT(service);

    return dbus_service_connection_new(self->proxy, service, rsap);
}

static
void
dbus_service_local_datagram_received(
    NfcPeerService* service,
    guint8 rsap,
    const void* data,
    guint len)
{
    DBusServiceLocalObject* self = DBUS_SERVICE_LOCAL_OBJECT(service);
    GDBusConnection* connection = dbus_service_local_connection(self);
    GDBusMessage* message = g_dbus_message_new_method_call(self->dbus_name,
        self->obj_path, LOCAL_SERVICE_INTERFACE, DATAGRAM_RECEIVED);

    GDEBUG("Datagram %u byte(s) for %s%s", len,
        self->dbus_name, self->obj_path);

    /*
     * Generated stub doesn't allow setting "no-reply-expected" flag,
     * we have to build and send D-Bus message manually.
     */
    g_dbus_message_set_flags(message, g_dbus_message_get_flags(message) |
        G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    g_dbus_message_set_body(message, g_variant_new("(u@ay)",
        rsap, dbus_service_dup_byte_array_as_variant(data, len)));
    g_dbus_connection_send_message(connection, message,
        G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, NULL);
    g_object_unref(message);
}

static
void
dbus_service_local_finalize(
    GObject* object)
{
    DBusServiceLocalObject* self = DBUS_SERVICE_LOCAL_OBJECT(object);

    if (self->watch_id) {
        g_bus_unwatch_name(self->watch_id);
    }
    g_free(self->peer_path);
    g_free(self->obj_path);
    g_free(self->dbus_name);
    g_object_unref(self->proxy);
    G_OBJECT_CLASS(dbus_service_local_object_parent_class)->finalize(object);
}

static
void
dbus_service_local_object_init(
    DBusServiceLocalObject* self)
{
}

static
void
dbus_service_local_object_class_init(
    DBusServiceLocalObjectClass* klass)
{
    klass->peer_arrived = dbus_service_local_peer_arrived;
    klass->peer_left = dbus_service_local_peer_left;
    klass->new_accept = dbus_service_local_new_accept;
    klass->datagram_received = dbus_service_local_datagram_received;
    G_OBJECT_CLASS(klass)->finalize = dbus_service_local_finalize;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

DBusServiceLocal*
dbus_service_local_new(
    GDBusConnection* connection,
    const char* obj_path,
    const char* peer_name,
    const char* dbus_name)
{
    GError* error = NULL;
    OrgSailfishosNfcLocalService* proxy = /* This won't actually block */
        org_sailfishos_nfc_local_service_proxy_new_sync(connection,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
            G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
            dbus_name, obj_path, NULL, &error);

    if (proxy) {
        DBusServiceLocalObject* self = g_object_new
            (DBUS_SERVICE_TYPE_LOCAL_OBJECT, NULL);
        DBusServiceLocal* local = &self->pub;
        NfcPeerService* service = &local->service;

        nfc_peer_service_init_base(service, peer_name);
        local->obj_path = self->obj_path = g_strdup(obj_path);
        local->dbus_name = self->dbus_name = g_strdup(dbus_name);
        self->proxy = proxy;
        return local;
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
