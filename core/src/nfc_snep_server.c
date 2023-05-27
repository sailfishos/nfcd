/*
 * Copyright (C) 2020-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2020 Jolla Ltd.
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

#include "nfc_snep_server.h"
#include "nfc_peer_connection_impl.h"
#include "nfc_peer_connection_p.h"
#include "nfc_peer_service_impl.h"
#include "nfc_peer_service_p.h"
#include "nfc_ndef.h"
#include "nfc_llc.h"

#define GLOG_MODULE_NAME NFC_SNEP_LOG_MODULE
#include <gutil_log.h>
#include <gutil_misc.h>

GLOG_MODULE_DEFINE2("snep", NFC_CORE_LOG_MODULE);

/*
 * NFCForum-TS-SNEP_1.0
 *
 * Table 2: Request Field Values
 */
typedef enum snep_request_code {
    SNEP_REQUEST_CONTINUE = 0x00,
    SNEP_REQUEST_GET = 0x01,
    SNEP_REQUEST_PUT = 0x02,
    SNEP_REQUEST_REJECT = 0x7f
} SNEP_REQUEST_CODE;

/*
 * Table 3: Response Field Values
 */
typedef enum snep_response_code {
    SNEP_RESPONSE_CONTINUE = 0x80,
    SNEP_RESPONSE_SUCCESS = 0x81,
    SNEP_RESPONSE_NOT_FOUND = 0xc0,
    SNEP_RESPONSE_EXCESS_DATA = 0xc1,
    SNEP_RESPONSE_BAD_REQUEST = 0xc2,
    SNEP_RESPONSE_NOT_IMPLEMENTED = 0xe0,
    SNEP_RESPONSE_UNSUPPORTED_VERSION = 0xe1,
    SNEP_RESPONSE_REJECT = 0xff
} SNEP_RESPONSE_CODE;

#define SNEP_MAJOR_VERSION (1)
#define SNEP_VERSION (0x10) /* (MAJOR << 4) | MINOR */

typedef struct nfc_snep_server_connection {
    NfcPeerConnection connection;
    GByteArray* buf;
    guint ndef_length;
} NfcSnepServerConnection;

typedef NfcPeerConnectionClass NfcSnepServerConnectionClass;
GType nfc_snep_server_connection_get_type(void) NFCD_INTERNAL;
G_DEFINE_TYPE(NfcSnepServerConnection, nfc_snep_server_connection, \
        NFC_TYPE_PEER_CONNECTION)
#define NFC_TYPE_SNEP_SERVER_CONNECTION (nfc_snep_server_connection_get_type())
#define NFC_SNEP_SERVER_CONNECTION(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_SNEP_SERVER_CONNECTION, NfcSnepServerConnection))

struct nfc_snep_server_priv {
    int connection_count;
};

typedef NfcPeerServiceClass NfcSnepServerClass;
GType nfc_snep_server_get_type(void) NFCD_INTERNAL;
G_DEFINE_TYPE(NfcSnepServer, nfc_snep_server, NFC_TYPE_PEER_SERVICE)
#define NFC_TYPE_SNEP_SERVER (nfc_snep_server_get_type())
#define NFC_SNEP_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_SNEP_SERVER, NfcSnepServer))

enum nfc_snep_server_signal {
    SIGNAL_STATE_CHANGED,
    SIGNAL_NDEF_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_STATE_CHANGED_NAME "nfc-snep-server-state-changed"
#define SIGNAL_NDEF_CHANGED_NAME "nfc-snep-server-ndef-changed"

static guint nfc_snep_server_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
nfc_snep_server_response(
    NfcPeerConnection* conn,
    SNEP_RESPONSE_CODE code)
{
    const guint size = 6;
    guint8* data = g_malloc(size);
    GBytes* pkt = g_bytes_new_take(data, size);

    memset(data, 0, size);
    data[0] = SNEP_VERSION;        /* Version */
    data[1] = code;                /* Response */
    nfc_peer_connection_send(conn, pkt);
    g_bytes_unref(pkt);
}

static
void
nfc_snep_server_set_state(
    NfcSnepServer* self,
    NFC_SNEP_SERVER_STATE state)
{
    if (self->state != state) {
        self->state = state;
        g_signal_emit(self, nfc_snep_server_signals[SIGNAL_STATE_CHANGED], 0);
    }
}

static
void
nfc_snep_server_update_connection_count(
    NfcSnepServer* self,
    int change)
{
    NfcSnepServerPriv* priv = self->priv;
    const int prev_count = priv->connection_count;

    priv->connection_count += change;
    if (change > 0) {
        if (!prev_count) {
            nfc_snep_server_set_state(self, NFC_SNEP_SERVER_RECEIVING);
        }
    } else {
        if (!priv->connection_count) {
            nfc_snep_server_set_state(self, NFC_SNEP_SERVER_LISTENING);
        }
    }
}

/*==========================================================================*
 * Connection
 *==========================================================================*/

static
void
nfc_snep_server_connection_receive_ndef(
    NfcSnepServerConnection* self,
    const void* data,
    guint len)
{
    NfcPeerConnection* conn = &self->connection;
    NfcSnepServer* snep = NFC_SNEP_SERVER(conn->service);
    GByteArray* buf = self->buf;

    if ((buf->len + len) > self->ndef_length) {
        GWARN("Broken SNEP Response (%u > %u)", buf->len + len,
            self->ndef_length);
        nfc_peer_connection_disconnect(conn);
    } else {
        g_byte_array_append(buf, data, len);
        GDEBUG("Received %u bytes", buf->len);
        if (buf->len == self->ndef_length) {
            GUtilData ndef_data;
            NfcNdefRec* prev_ndef;

            /* Done with receiving NDEF. Parse it. */
            ndef_data.bytes = buf->data;
            ndef_data.size = buf->len;
            prev_ndef = snep->ndef;
            snep->ndef = nfc_ndef_rec_new(&ndef_data);

            /* Need to actually compare NDEFs? */
            if (prev_ndef != snep->ndef) {
                g_signal_emit(snep, nfc_snep_server_signals
                    [SIGNAL_NDEF_CHANGED], 0);
            }
            nfc_ndef_rec_unref(prev_ndef);

            /* Done, terminate the connection */
            nfc_peer_connection_disconnect(conn);
        }
    }
}

static
void
nfc_snep_server_connection_data_received(
    NfcPeerConnection* conn,
    const void* data,
    guint len)
{
    NfcSnepServerConnection* self = NFC_SNEP_SERVER_CONNECTION(conn);

    if (self->buf) {
        /* Receiving fragmented message */
        nfc_snep_server_connection_receive_ndef(self, data, len);
    } else if (len >= 6) {
        /*
         * NFCForum-TS-SNEP_1.0
         * 2.1. SNEP Communication Protocol
         *
         * In order for the receiver of a fragmented SNEP message to
         * determine the number of octets that are to be received with
         * subsequent fragments, the first fragment SHALL include at
         * least the entire SNEP message header.
         */
        const guint8* pkt = data;
        const guint version = pkt[0];
        const SNEP_REQUEST_CODE op = pkt[1];
        const guint v1 = (version >> 4);

        GDEBUG("SNEP Version %u.%u", v1, version & 0x0f);
        if (v1 != SNEP_MAJOR_VERSION) {
            GDEBUG("Unsupported SNEP Version %u", v1);
            nfc_snep_server_response(conn, SNEP_RESPONSE_UNSUPPORTED_VERSION);
            nfc_peer_connection_disconnect(conn);
        } else if (op == SNEP_REQUEST_GET) {
            /*
             * 6.1. Functional Description
             *
             * The default server SHALL NOT accept Get requests.
             * The appropriate response for a Get request message
             * is Not Implemented.
             */
            GDEBUG("NDEF Get not accepted");
            nfc_snep_server_response(conn, SNEP_RESPONSE_NOT_IMPLEMENTED);
            nfc_peer_connection_disconnect(conn);
        } else if (op != SNEP_REQUEST_PUT) {
            GDEBUG("Unsupported SNEP Request 0x%02x", op);
            nfc_snep_server_response(conn, SNEP_RESPONSE_BAD_REQUEST);
            nfc_peer_connection_disconnect(conn);
        } else {
            /*
             * 3.1.3. Length Field
             *
             * The Length field specifies the total length in octets of
             * the Information field. The Length field is four octets
             * representing a 32-bit unsigned integer. Transmission
             * order SHALL be most significant octet first.
             */
            self->ndef_length =
                (((guint32)pkt[2]) << 24) |
                (((guint32)pkt[3]) << 16) |
                (((guint32)pkt[4]) << 8) |
                ((guint32)pkt[5]);
            GDEBUG("NDEF Put %u bytes", self->ndef_length);
            self->buf = g_byte_array_sized_new(self->ndef_length);
            nfc_snep_server_connection_receive_ndef(self, pkt + 6, len - 6);
            if (self->buf->len < self->ndef_length) {
                /*
                 * 5.1. Continue
                 *
                 * The server received the first fragment of a fragmented
                 * SNEP request message and is able to receive the remaining
                 * fragments. The server indicates its ability to receive
                 * the remaining fragments and successfully reassemble the
                 * complete SNEP request message. This response code SHALL
                 * only be sent after receipt of the first fragment of a
                 * fragmented SNEP request message. An information field
                 * SHALL NOT be transmitted with this response.
                 */
                nfc_snep_server_response(conn, SNEP_RESPONSE_CONTINUE);
            }
        }
    } else {
        GWARN("Not enough bytes for SNEP header (%u)", len);
        nfc_peer_connection_disconnect(conn);
    }
}

static
void
nfc_snep_server_connection_init(
    NfcSnepServerConnection* self)
{
}

static
void
nfc_snep_server_connection_finalize(
    GObject* object)
{
    NfcSnepServerConnection* self = NFC_SNEP_SERVER_CONNECTION(object);
    NfcSnepServer* snep = NFC_SNEP_SERVER(self->connection.service);

    nfc_snep_server_update_connection_count(snep, -1);
    if (self->buf) {
        g_byte_array_free(self->buf, TRUE);
    }
    G_OBJECT_CLASS(nfc_snep_server_connection_parent_class)->finalize(object);
}

static
void
nfc_snep_server_connection_class_init(
    NfcSnepServerConnectionClass* klass)
{
    klass->data_received = nfc_snep_server_connection_data_received;
    G_OBJECT_CLASS(klass)->finalize = nfc_snep_server_connection_finalize;
}

static
NfcPeerConnection*
nfc_snep_server_connection_new(
    NfcSnepServer* snep,
    guint8 rsap)
{
    NfcSnepServerConnection* self = g_object_new
        (NFC_TYPE_SNEP_SERVER_CONNECTION, NULL);
    NfcPeerConnection* conn = &self->connection;

    GDEBUG("Accepting incoming SNEP connection");
    nfc_peer_connection_init_accept(conn, &snep->service, rsap);
    nfc_snep_server_update_connection_count(snep, 1);
    return conn;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcSnepServer*
nfc_snep_server_new(
    void)
{
    NfcSnepServer* self = g_object_new(NFC_TYPE_SNEP_SERVER, NULL);

    nfc_peer_service_init_base(&self->service, NFC_LLC_NAME_SNEP);
    return self;
}

gulong
nfc_snep_server_add_state_changed_handler(
    NfcSnepServer* self,
    NfcSnepServerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_STATE_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_snep_server_add_ndef_changed_handler(
    NfcSnepServer* self,
    NfcSnepServerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_NDEF_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_snep_server_remove_handler(
    NfcSnepServer* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_snep_server_remove_handlers(
    NfcSnepServer* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
NfcPeerConnection*
nfc_snep_server_new_accept(
    NfcPeerService* service,
    guint8 rsap)
{
    return nfc_snep_server_connection_new(NFC_SNEP_SERVER(service), rsap);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_snep_server_init(
    NfcSnepServer* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NFC_TYPE_SNEP_SERVER,
        NfcSnepServerPriv);
    self->state = NFC_SNEP_SERVER_LISTENING;
}

static
void
nfc_snep_server_finalize(
    GObject* object)
{
    NfcSnepServer* self = NFC_SNEP_SERVER(object);

    nfc_ndef_rec_unref(self->ndef);
    G_OBJECT_CLASS(nfc_snep_server_parent_class)->finalize(object);
}

static
void
nfc_snep_server_class_init(
    NfcSnepServerClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    g_type_class_add_private(klass, sizeof(NfcSnepServerPriv));
    klass->new_accept = nfc_snep_server_new_accept;
    G_OBJECT_CLASS(klass)->finalize = nfc_snep_server_finalize;
    nfc_snep_server_signals[SIGNAL_STATE_CHANGED] =
        g_signal_new(SIGNAL_STATE_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_snep_server_signals[SIGNAL_NDEF_CHANGED] =
        g_signal_new(SIGNAL_NDEF_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
