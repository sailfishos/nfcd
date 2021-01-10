/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "nfc_llc.h"
#include "nfc_peer_service.h"
#include "nfc_peer_socket.h"
#include "nfc_peer_socket_impl.h"

#define GLOG_MODULE_NAME NFC_PEER_LOG_MODULE
#include <gutil_log.h>
#include <gutil_macros.h>

#include <gio/gunixfdlist.h>

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

struct nfc_peer_socket_priv {
    GIOChannel* io_channel;
    GList* write_queue;
    guint read_watch_id;
    guint write_watch_id;
    guint write_pos;
    int fd;
};

/* NOTE: we can exceed this limit, but by no more than MIU. There's no
 * need to be overly strict about it. */
#define DEFAULT_MAX_SEND_QUEUE (128*1024)

#define THIS(obj) NFC_PEER_SOCKET(obj)
#define THIS_TYPE NFC_TYPE_PEER_SOCKET
#define PARENT_TYPE NFC_TYPE_PEER_CONNECTION
#define PARENT_CLASS (nfc_peer_socket_parent_class)

G_DEFINE_TYPE(NfcPeerSocket, nfc_peer_socket, PARENT_TYPE)

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
nfc_peer_socket_shutdown(
    NfcPeerSocket* self)
{
    NfcPeerSocketPriv* priv = self->priv;

    if (priv->read_watch_id) {
        g_source_remove(priv->read_watch_id);
        priv->read_watch_id = 0;
    }
    if (priv->write_watch_id) {
        g_source_remove(priv->write_watch_id);
        priv->write_watch_id = 0;
    }
    if (priv->io_channel) {
        shutdown(priv->fd, SHUT_RDWR);
        g_io_channel_shutdown(priv->io_channel, FALSE, NULL);
        g_io_channel_unref(priv->io_channel);
        priv->io_channel = NULL;
        priv->fd = -1;
    }
}

static
gboolean
nfc_peer_socket_read_bytes(
    NfcPeerSocket* self,
    gchar* buf,
    gsize count,
    gsize* bytes_read)
{
    GError* error = NULL;
    NfcPeerSocketPriv* priv = self->priv;
    NfcPeerConnection* conn =  &self->connection;
    GIOStatus status = g_io_channel_read_chars(priv->io_channel, buf,
        count, bytes_read, &error);

    if (error) {
        GDEBUG("Connection %u:%u read failed: %s", conn->service->sap,
            conn->rsap, GERRMSG(error));
        priv->read_watch_id = 0;
        nfc_peer_socket_shutdown(self);
        nfc_peer_connection_disconnect(conn);
        g_error_free(error);
        return FALSE;
    } else if (status == G_IO_STATUS_EOF) {
        GDEBUG("Connection %u:%u hung up", conn->service->sap, conn->rsap);
        priv->read_watch_id = 0;
        nfc_peer_socket_shutdown(self);
        nfc_peer_connection_disconnect(conn);
        return FALSE;
    } else {
        GVERBOSE("Connection %u:%u read %u bytes", conn->service->sap,
            conn->rsap, (guint)(*bytes_read));
        return TRUE;
    }
}

static
gboolean
nfc_peer_socket_read(
    NfcPeerSocket* self)
{
    NfcPeerConnection* conn = &self->connection;
    const guint rmiu = nfc_peer_connection_rmiu(conn);
    void* buf = g_malloc(rmiu);
    gsize bytes_read;

    if (nfc_peer_socket_read_bytes(self, buf, rmiu, &bytes_read)) {
        GBytes* bytes = g_bytes_new_take(buf, bytes_read);
        const gboolean sent =  nfc_peer_connection_send(conn, bytes);

        g_bytes_unref(bytes);
        /* Stop reading when we hit the queue size limit */
        return sent && (self->connection.bytes_queued <= self->max_send_queue);
    } else {
        g_free(buf);
    }
    return FALSE;
}

static
gboolean
nfc_peer_socket_read_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer user_data)
{
    NfcPeerSocket* self = THIS(user_data);
    gboolean result;

    g_object_ref(self);
    if ((condition & G_IO_IN) && nfc_peer_socket_read(self)) {
        result = G_SOURCE_CONTINUE;
    } else {
        NfcPeerSocketPriv* priv = self->priv;

        priv->read_watch_id = 0;
        result = G_SOURCE_REMOVE;
    }
    g_object_unref(self);
    return result;
}

static
void
nfc_peer_socket_read_check(
    NfcPeerSocket* self)
{
    NfcPeerConnection* conn = &self->connection;
    NfcPeerSocketPriv* priv = self->priv;

    if (priv->io_channel && !priv->read_watch_id &&
        conn->bytes_queued <= self->max_send_queue &&
        conn->state <= NFC_LLC_CO_ACTIVE) {
        priv->read_watch_id = g_io_add_watch(priv->io_channel,
            G_IO_IN | G_IO_ERR | G_IO_HUP, nfc_peer_socket_read_callback,
            self);
    }
}

static
gboolean
nfc_peer_socket_write_bytes(
    NfcPeerSocket* self,
    const void* buf,
    gssize count,
    gsize* bytes_written,
    GError** error)
{
    NfcPeerSocketPriv* priv = self->priv;
    NfcPeerConnection* conn =  &self->connection;
    const GIOStatus status = g_io_channel_write_chars(priv->io_channel,
        buf, count, bytes_written, error);

    if (status == G_IO_STATUS_NORMAL || status == G_IO_STATUS_AGAIN) {
        GVERBOSE("Connection %u:%u wrote %u bytes", conn->service->sap,
            conn->rsap, (guint)(*bytes_written));
        return TRUE;
    } else {
        GASSERT(*error);
        GDEBUG("Connection %u:%u write failed: %s", conn->service->sap,
            conn->rsap, GERRMSG(*error));
        nfc_peer_connection_disconnect(conn);
        return FALSE;
    }
}

static
gboolean
nfc_peer_socket_write(
    NfcPeerSocket* self,
    GError** error)
{
    NfcPeerSocketPriv* priv = self->priv;

    if (priv->write_queue) {
        GList* first = g_list_first(priv->write_queue);
        GBytes* bytes = first->data;
        gsize len, bytes_written = 0;
        const guint8* data = g_bytes_get_data(bytes, &len);

        GASSERT(priv->write_pos < len);
        if (!nfc_peer_socket_write_bytes(self, data + priv->write_pos,
            len - priv->write_pos, &bytes_written, error)) {
            return FALSE;
        }
        priv->write_pos += bytes_written;
        GASSERT(priv->write_pos <= len);
        if (priv->write_pos < len) {
            /* Will have to wait */
            return TRUE;
        }

        /* Done with this one */
        priv->write_pos = 0;
        priv->write_queue = g_list_delete_link(priv->write_queue, first);
        g_bytes_unref(bytes);
        if (priv->write_queue) {
            /* Have more */
            return TRUE;
        }
    }

    GVERBOSE("Connection %u:%u has no more data to write",
        self->connection.service->sap, self->connection.rsap);
    return TRUE;
}

static
gboolean
nfc_peer_socket_write_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer user_data)
{
    NfcPeerSocket* self = THIS(user_data);
    NfcPeerSocketPriv* priv = self->priv;
    gboolean result = G_SOURCE_REMOVE;
    GError* error = NULL;

    g_object_ref(self);
    if ((condition & G_IO_OUT) &&
        nfc_peer_socket_write(self, &error) &&
        priv->write_queue) {
        result = G_SOURCE_CONTINUE;
    } else {
        priv->write_watch_id = 0;
        result = G_SOURCE_REMOVE;
    }
    if (error) {
        GERR("Connection %u:%u write failed: %s",
            self->connection.service->sap, self->connection.rsap,
            GERRMSG(error));
        priv->write_watch_id = 0;
        nfc_peer_socket_shutdown(self);
        g_error_free(error);
    }
    g_object_unref(self);
    return result;
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

gboolean
nfc_peer_socket_init_connect(
    NfcPeerSocket* self,
    NfcPeerService* service,
    guint8 rsap,
    const char* name)
{
    if (G_LIKELY(service)) {
        int fd[2];

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0) {
            NfcPeerConnection* conn = &self->connection;
            NfcPeerSocketPriv* priv = self->priv;

            nfc_peer_connection_init_connect(conn, service, rsap, name);
            self->fdl = g_unix_fd_list_new_from_array(fd, 1);
            priv->fd = fd[1];
            return TRUE;
        }
        GERR("Connection %u:%u failed to create socket pair: %s",
            service->sap, rsap, strerror(errno));
    }
    return FALSE;
}

gboolean
nfc_peer_socket_init_accept(
    NfcPeerSocket* self,
    NfcPeerService* service,
    guint8 rsap)
{
    if (G_LIKELY(service)) {
        int fd[2];

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0) {
            NfcPeerConnection* conn = &self->connection;
            NfcPeerSocketPriv* priv = self->priv;

            nfc_peer_connection_init_accept(conn, service, rsap);
            self->fdl = g_unix_fd_list_new_from_array(fd, 1);
            priv->fd = fd[1];
            return TRUE;
        }
        GERR("Connection %u:%u failed to create socket pair: %s",
            service->sap, rsap, strerror(errno));
    }
    return FALSE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcPeerSocket*
nfc_peer_socket_new_connect(
    NfcPeerService* service,
    guint8 rsap,
    const char* name)
{
    NfcPeerSocket* self = g_object_new(THIS_TYPE, NULL);

    if (nfc_peer_socket_init_connect(self, service, rsap, name)) {
        return self;
    } else {
        g_object_unref(THIS(self));
        return NULL;
    }
}

NfcPeerSocket*
nfc_peer_socket_new_accept(
    NfcPeerService* service,
    guint8 rsap)
{
    NfcPeerSocket* self = g_object_new(THIS_TYPE, NULL);

    if (nfc_peer_socket_init_accept(self, service, rsap)) {
        return self;
    } else {
        g_object_unref(THIS(self));
        return NULL;
    }
}

int
nfc_peer_socket_fd(
    NfcPeerSocket* self)
{
    return (G_LIKELY(self) && self->fdl) ?
        g_unix_fd_list_peek_fds(self->fdl, NULL)[0] : -1;
}

void
nfc_peer_socket_set_max_send_queue(
    NfcPeerSocket* self,
    gsize max_send_queue)
{
    if (G_LIKELY(self) && (self->max_send_queue != max_send_queue)) {
        self->max_send_queue = max_send_queue;
        nfc_peer_socket_read_check(self);
    }
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
void
nfc_peer_socket_state_changed(
    NfcPeerConnection* conn)
{
    if (conn->state == NFC_LLC_CO_ACTIVE) {
        NfcPeerSocket* self = THIS(conn);
        NfcPeerSocketPriv* priv = self->priv;

        GASSERT(!priv->io_channel); /* We can become active only once */
        priv->io_channel = g_io_channel_unix_new(priv->fd);
        if (priv->io_channel) {
            g_io_channel_set_flags(priv->io_channel, G_IO_FLAG_NONBLOCK, NULL);
            g_io_channel_set_encoding(priv->io_channel, NULL, NULL);
            g_io_channel_set_buffered(priv->io_channel, FALSE);
            g_io_channel_set_close_on_unref(priv->io_channel, TRUE);
            nfc_peer_socket_read_check(self);
        }
    }
    NFC_PEER_CONNECTION_CLASS(PARENT_CLASS)->state_changed(conn);
}

static
void
nfc_peer_socket_data_received(
    NfcPeerConnection* conn,
    const void* data,
    guint len)
{
    NfcPeerSocket* self = THIS(conn);
    NfcPeerSocketPriv* priv = self->priv;

    if (len > 0 && priv->io_channel) {
        priv->write_queue = g_list_append(priv->write_queue,
            g_bytes_new(data, len));
        if (!priv->write_watch_id) {
            GVERBOSE("Connection %u:%u scheduling write",
                conn->service->sap, conn->rsap);
            priv->write_watch_id = g_io_add_watch(priv->io_channel,
                G_IO_OUT | G_IO_ERR | G_IO_HUP, nfc_peer_socket_write_callback,
                self);
        }
    }
    NFC_PEER_CONNECTION_CLASS(PARENT_CLASS)->data_received(conn, data, len);
}

static
void
nfc_peer_socket_data_dequeued(
    NfcPeerConnection* conn)
{
    nfc_peer_socket_read_check(THIS(conn));
    NFC_PEER_CONNECTION_CLASS(PARENT_CLASS)->data_dequeued(conn);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_peer_socket_init(
    NfcPeerSocket* self)
{
    NfcPeerSocketPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcPeerSocketPriv);

    self->max_send_queue = DEFAULT_MAX_SEND_QUEUE;
    self->priv = priv;
    priv->fd = -1;
}

static
void
nfc_peer_socket_finalize(
    GObject* object)
{
    NfcPeerSocket* self = THIS(object);
    NfcPeerSocketPriv* priv = self->priv;

    nfc_peer_socket_shutdown(self);
    g_list_free_full(priv->write_queue, (GDestroyNotify) g_bytes_unref);
    if (self->fdl) {
        g_object_unref(self->fdl);
    }
    if (priv->fd >= 0) {
        close(priv->fd);
    }
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_peer_socket_class_init(
    NfcPeerSocketClass* klass)
{
    NfcPeerConnectionClass* connection = NFC_PEER_CONNECTION_CLASS(klass);

    g_type_class_add_private(klass, sizeof(NfcPeerSocketPriv));
    connection->state_changed = nfc_peer_socket_state_changed;
    connection->data_received = nfc_peer_socket_data_received;
    connection->data_dequeued = nfc_peer_socket_data_dequeued;
    G_OBJECT_CLASS(klass)->finalize = nfc_peer_socket_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
