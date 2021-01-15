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
#include "nfc_llc_param.h"
#include "nfc_peer_connection_impl.h"
#include "nfc_peer_connection_p.h"
#include "nfc_peer_service_p.h"

#define GLOG_MODULE_NAME NFC_PEER_LOG_MODULE
#include <gutil_log.h>
#include <gutil_idlepool.h>
#include <gutil_macros.h>

#define NFC_LLC_LOCAL_RW NFC_LLC_RW_MAX
#define NFC_LLC_LOCAL_MIU NFC_LLC_MIU_MAX

struct nfc_peer_connection_priv {
    char* name;
    NfcLlc* llc;
    GUtilIdlePool* pool;
    NfcPeerConnectionLlcpState ps;
    NfcLlcParam miu_param;
    NfcLlcParam rw_param;
    const NfcLlcParam* lp[3];
    guint send_off;
    GList* send_queue;
    GByteArray* send_buf;
    gboolean disc_sent;
};

#define THIS(obj) NFC_PEER_CONNECTION(obj)
#define THIS_TYPE NFC_TYPE_PEER_CONNECTION
#define PARENT_CLASS (nfc_peer_connection_parent_class)

G_DEFINE_ABSTRACT_TYPE(NfcPeerConnection, nfc_peer_connection, G_TYPE_OBJECT)
#define NFC_PEER_CONNECTION_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), \
        THIS_TYPE, NfcPeerConnectionClass)

enum nfc_peer_connection_signal {
    SIGNAL_STATE_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_STATE_CHANGED_NAME "nfc-llc-connection-state-changed"

static guint nfc_peer_connection_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

#if GUTIL_LOG_DEBUG
static
const char*
nfc_peer_connection_state_name(
    NfcPeerConnectionPriv* priv,
    NFC_LLC_CO_STATE state)
{
    char* tmp;

    switch (state) {
    case NFC_LLC_CO_CONNECTING: return "CONNECTING";
    case NFC_LLC_CO_ACCEPTING: return "ACCEPTING";
    case NFC_LLC_CO_ABANDONED: return "ABANDONED";
    case NFC_LLC_CO_ACTIVE: return "ACTIVE";
    case NFC_LLC_CO_DISCONNECTING: return "DISCONNECTING";
    case NFC_LLC_CO_DEAD: return "DEAD";
    }
    tmp = g_strdup_printf("%d (?)", state);
    gutil_idle_pool_add(priv->pool, tmp, g_free);
    return tmp;
}
#endif /* GUTIL_LOG_DEBUG */

static
gboolean
nfc_peer_connection_can_send(
    NfcPeerConnection* self)
{
    NfcPeerConnectionPriv* priv = self->priv;
    const NfcPeerConnectionLlcpState* ps = &priv->ps;

    /*
     * NFCForum-TS-LLCP_1.1
     * 5.6 Connection-oriented Transport Mode Procedures
     * 5.6.4.1 Sending I PDUs
     *
     * While the send state variable V(S) is equal to the send
     * acknowledge state variable V(SA) plus the remote receive
     * window size RW(R), the LLC SHALL NOT send an I PDU on that
     * data link connection.
     */
    return ps->vs != ((ps->vsa + ps->rwr) & 0x0f);
}

static
void
nfc_peer_connection_submit_i_pdu(
    NfcPeerConnection* self,
    const void* data,
    guint len)
{
    NfcPeerConnectionPriv* priv = self->priv;

    nfc_llc_submit_i_pdu(priv->llc, self, data, len);
    GASSERT(self->bytes_queued >= len);
    self->bytes_queued -= len;
    self->bytes_sent += len;
}

static
gboolean
nfc_peer_connection_drop_queued_data(
    NfcPeerConnection* self)
{
    NfcPeerConnectionPriv* priv = self->priv;

    if (priv->send_queue) {
        GASSERT(self->bytes_queued);
        self->bytes_queued = 0;
        g_list_free_full(priv->send_queue, (GDestroyNotify) g_bytes_unref);
        priv->send_queue = NULL;
        return TRUE;
    }
    return FALSE;
}

static
inline
void
nfc_peer_connection_data_dequeued(
    NfcPeerConnection* self)
{
    NFC_PEER_CONNECTION_GET_CLASS(self)->data_dequeued(self);
}

static
void
nfc_peer_connection_disconnect_internal(
    NfcPeerConnection* self,
    gboolean flush)
{
    NfcPeerConnectionPriv* priv = self->priv;
    NfcPeerService* service = self->service;
    gboolean data_dropped = FALSE;

    switch (self->state) {
    case NFC_LLC_CO_CONNECTING:
        GDEBUG("Abandoning %u:%u", service->sap, self->rsap);
        data_dropped = nfc_peer_connection_drop_queued_data(self);
        nfc_peer_connection_set_state(self, NFC_LLC_CO_ABANDONED);
        break;
    case NFC_LLC_CO_ACCEPTING:
        GDEBUG("Connection %u:%u cancelled", service->sap, self->rsap);
        data_dropped = nfc_peer_connection_drop_queued_data(self);
        NFC_PEER_CONNECTION_GET_CLASS(self)->accept_cancelled(self);
        nfc_peer_connection_set_state(self, NFC_LLC_CO_DEAD);
        break;
    case NFC_LLC_CO_ACTIVE:
        nfc_llc_ack(priv->llc, self, TRUE);
        GDEBUG("Disconnecting %u:%u", service->sap, self->rsap);
        if (!flush) {
            data_dropped = nfc_peer_connection_drop_queued_data(self);
        }
        if (!priv->send_queue) {
            nfc_llc_submit_disc_pdu(priv->llc, self->rsap, service->sap);
            priv->disc_sent = TRUE;
        }
        nfc_peer_connection_set_state(self, NFC_LLC_CO_DISCONNECTING);
        break;
    case NFC_LLC_CO_ABANDONED:
    case NFC_LLC_CO_DISCONNECTING:
    case NFC_LLC_CO_DEAD:
        /* Nothing to do */
        break;
    }

    if (data_dropped) {
        nfc_peer_connection_data_dequeued(self);
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcPeerConnection*
nfc_peer_connection_ref(
    NfcPeerConnection* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_peer_connection_unref(
    NfcPeerConnection* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

gboolean
nfc_peer_connection_send(
    NfcPeerConnection* self,
    GBytes* bytes)
{
    if (G_LIKELY(self)) {
        NfcPeerConnectionPriv* priv = self->priv;
        const gsize size = bytes ? g_bytes_get_size(bytes) : 0;

        switch (self->state) {
        case NFC_LLC_CO_ACCEPTING:
        case NFC_LLC_CO_CONNECTING:
        case NFC_LLC_CO_ACTIVE:
            if (size > 0) {
                self->bytes_queued += size;
                priv->send_queue = g_list_append(priv->send_queue,
                    g_bytes_ref(bytes));
                if (self->state == NFC_LLC_CO_ACTIVE) {
                    nfc_peer_connection_flush(self);
                }
            }
            return TRUE;
        case NFC_LLC_CO_DISCONNECTING:
        case NFC_LLC_CO_ABANDONED:
        case NFC_LLC_CO_DEAD:
            break;
        }
    }
    return FALSE;
}

void
nfc_peer_connection_disconnect(
    NfcPeerConnection* self)
{
    if (G_LIKELY(self)) {
        nfc_peer_connection_disconnect_internal(self, TRUE);
    }
}

gboolean
nfc_peer_connection_cancel(
    NfcPeerConnection* self)
{
    gboolean cancelled = FALSE;

    if (G_LIKELY(self)) {
        NfcPeerConnectionPriv* priv = self->priv;

        cancelled = nfc_llc_cancel_connect_request(priv->llc, self);
        nfc_peer_connection_disconnect_internal(self, FALSE);
    }
    return cancelled;
}

gulong
nfc_peer_connection_add_state_changed_handler(
    NfcPeerConnection* self,
    NfcPeerConnectionFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_STATE_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_peer_connection_remove_handler(
    NfcPeerConnection* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

/*==========================================================================*
 * Internal interface
 *
 * These functions may assume that NfcPeerConnection pointer is not NULL.
 * The caller checks that.
 *==========================================================================*/

void
nfc_peer_connection_init_connect(
    NfcPeerConnection* self,
    NfcPeerService* service,
    guint8 rsap,
    const char* name)
{
    NfcPeerConnectionPriv* priv = self->priv;

    self->state = NFC_LLC_CO_CONNECTING;
    self->name = priv->name = g_strdup(name);
    self->service = nfc_peer_service_ref(service);
    self->rsap = rsap;
    nfc_peer_service_connection_created(service, self);
    GDEBUG("Connection %u:%u %s", service->sap, self->rsap,
        nfc_peer_connection_state_name(priv, self->state));
}

void
nfc_peer_connection_init_accept(
    NfcPeerConnection* self,
    NfcPeerService* service,
    guint8 rsap)
{
    NfcPeerConnectionPriv* priv = self->priv;

    self->state = NFC_LLC_CO_ACCEPTING;
    self->service = nfc_peer_service_ref(service);
    self->rsap = rsap;
    nfc_peer_service_connection_created(service, self);
    GDEBUG("Connection %u:%u %s", service->sap, self->rsap,
        nfc_peer_connection_state_name(priv, self->state));
}

guint
nfc_peer_connection_rmiu(
    NfcPeerConnection* self)
{
    return G_LIKELY(self) ? self->priv->ps.rmiu : 0;
}

gpointer
nfc_peer_connection_key(
    NfcPeerConnection* self)
{
    return self ? LLCP_CONN_KEY(self->service->sap, self->rsap) : NULL;
}

void
nfc_peer_connection_set_llc(
    NfcPeerConnection* self,
    NfcLlc* llc)
{
    self->priv->llc = llc;
}

const NfcLlcParam* const*
nfc_peer_connection_lp(
    NfcPeerConnection* self)
{
    return self->priv->lp;
}

NfcPeerConnectionLlcpState*
nfc_peer_connection_ps(
    NfcPeerConnection* self)
{
    return &self->priv->ps;
}

void
nfc_peer_connection_apply_remote_params(
    NfcPeerConnection* self,
    const NfcLlcParam* const* params)
{
    if (G_LIKELY(params)) {
        NfcPeerConnectionPriv* priv = self->priv;
        NfcPeerConnectionLlcpState* ps = &priv->ps;
        const NfcLlcParam* const* ptr = params;

        while (*ptr) {
            const NfcLlcParam* param = *ptr++;

            switch (param->type) {
            case NFC_LLC_PARAM_MIUX:
                ps->rmiu = param->value.miu;
                GDEBUG("  MIU(R): %u bytes", ps->rmiu);
                break;
            case NFC_LLC_PARAM_RW:
                ps->rwr = param->value.rw;
                GDEBUG("  RW(R): %u", ps->rwr);
                /* The rest is irrelevant */
                /* fallthrough */
            case NFC_LLC_PARAM_VERSION:
            case NFC_LLC_PARAM_WKS:
            case NFC_LLC_PARAM_LTO:
            case NFC_LLC_PARAM_SN:
            case NFC_LLC_PARAM_OPT:
            case NFC_LLC_PARAM_SDREQ:
            case NFC_LLC_PARAM_SDRES:
                break;
            }
        }
    }
}

void
nfc_peer_connection_accept(
    NfcPeerConnection* self)
{
    NFC_PEER_CONNECTION_GET_CLASS(self)->accept(self);
}

void
nfc_peer_connection_data_received(
    NfcPeerConnection* self,
    const void* data,
    guint len)
{
    self->bytes_received += len;
    NFC_PEER_CONNECTION_GET_CLASS(self)->data_received(self, data, len);
}

void
nfc_peer_connection_set_state(
    NfcPeerConnection* self,
    NFC_LLC_CO_STATE state)
{
    if (G_LIKELY(self) && self->state != state &&
        self->state != NFC_LLC_CO_DEAD /* Terminal state */) {
        nfc_peer_connection_ref(self);
        GDEBUG("Connection %u:%u %s -> %s", self->service->sap, self->rsap,
            nfc_peer_connection_state_name(self->priv, self->state),
            nfc_peer_connection_state_name(self->priv, state));
        self->state = state;
        NFC_PEER_CONNECTION_GET_CLASS(self)->state_changed(self);
        nfc_peer_connection_unref(self);
    }
}

void
nfc_peer_connection_accepted(
    NfcPeerConnection* self)
{
    GASSERT(self->state == NFC_LLC_CO_ACCEPTING);
    if (G_LIKELY(self->state == NFC_LLC_CO_ACCEPTING)) {
        NfcPeerConnectionPriv* priv = self->priv;

        GDEBUG("Connection %u:%u accepted", self->service->sap, self->rsap);
        nfc_llc_submit_cc_pdu(priv->llc, self);
        nfc_peer_connection_set_state(self, NFC_LLC_CO_ACTIVE);
    }
}

void
nfc_peer_connection_rejected(
    NfcPeerConnection* self)
{
    GASSERT(self->state == NFC_LLC_CO_ACCEPTING);
    if (G_LIKELY(self->state == NFC_LLC_CO_ACCEPTING)) {
        NfcPeerConnectionPriv* priv = self->priv;
        NfcPeerService* service = self->service;

        GDEBUG("Connection %u:%u rejected", service->sap, self->rsap);
        nfc_llc_submit_dm_pdu(priv->llc, self->rsap, service->sap,
            NFC_LLC_DM_REJECT);
        nfc_peer_connection_set_state(self, NFC_LLC_CO_DEAD);
    }
}

void
nfc_peer_connection_flush(
    NfcPeerConnection* self)
{
    NfcPeerConnectionPriv* priv = self->priv;
    gboolean submitted = FALSE;

    /*
     * If nfc_llc_i_pdu_queued return TRUE (i.e. an I-frame is already
     * queued for this connection), just continue to accumulate the data.
     * This function will be called again when I-frame is dequeued.
     */
    while (priv->send_queue &&
           nfc_peer_connection_can_send(self) &&
           !nfc_llc_i_pdu_queued(priv->llc, self)) {
        const NfcPeerConnectionLlcpState* ps = &priv->ps;
        GList* first = g_list_first(priv->send_queue);
        GBytes* block = first->data;
        gsize block_size;
        const guint8* block_data = g_bytes_get_data(block, &block_size);
        const guint8* ptr = block_data + priv->send_off;
        guint remaining = block_size - priv->send_off;

        if (remaining > ps->rmiu) {
            /* Send a full I frame and still have some data left in this
             * buffer, shift the offset by MIU */
            priv->send_off += ps->rmiu;
            nfc_peer_connection_submit_i_pdu(self, ptr, ps->rmiu);
            submitted = TRUE;
        } else if (remaining == ps->rmiu || !priv->send_queue->next) {
            /* Send the remaining data in this block as a single I PDU */
            priv->send_off = 0;
            priv->send_queue = g_list_delete_link(priv->send_queue,
                priv->send_queue);
            nfc_peer_connection_submit_i_pdu(self, ptr, remaining);
            submitted = TRUE;
            g_bytes_unref(block);
        } else {
            GByteArray* buf = priv->send_buf;

            /* Have to concatenate blocks */
            g_byte_array_set_size(buf, 0);
            g_byte_array_append(buf, ptr, remaining);
            g_bytes_unref(block);
            priv->send_off = 0;
            priv->send_queue = g_list_delete_link(priv->send_queue,
                priv->send_queue);

            while (buf->len < ps->rmiu && priv->send_queue) {
                remaining = ps->rmiu - buf->len;
                block = priv->send_queue->data;
                block_data = g_bytes_get_data(block, &block_size);
                if (block_size <= remaining) {
                    g_byte_array_append(buf, block_data, block_size);
                    g_bytes_unref(block);
                    priv->send_queue = g_list_delete_link(priv->send_queue,
                        priv->send_queue);
                } else {
                    priv->send_off = remaining;
                    g_byte_array_append(buf, block_data, remaining);
                }
            }
            nfc_peer_connection_submit_i_pdu(self, buf->data, buf->len);
            submitted = TRUE;
        }
    }

    if (!priv->send_queue &&
        self->state == NFC_LLC_CO_DISCONNECTING &&
        !priv->disc_sent) {
        NfcPeerService* service = self->service;

        /* Done flushing */
        nfc_llc_submit_disc_pdu(priv->llc, self->rsap, service->sap);
        priv->disc_sent = TRUE;
    }

    if (submitted) {
        nfc_peer_connection_data_dequeued(self);
    }
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
void
nfc_peer_connection_default_state_changed(
    NfcPeerConnection* self)
{
    NfcPeerConnectionPriv* priv = self->priv;
    const gboolean data_dropped = (self->state == NFC_LLC_CO_DEAD) &&
        nfc_peer_connection_drop_queued_data(self);

    g_signal_emit(self, nfc_peer_connection_signals[SIGNAL_STATE_CHANGED], 0);
    switch (self->state) {
    case NFC_LLC_CO_DEAD:
        /* Notify LLC that we have died */
        nfc_llc_connection_dead(priv->llc, self);
        nfc_peer_service_connection_dead(self->service, self);
        if (data_dropped) {
            nfc_peer_connection_data_dequeued(self);
        }
        break;
    case NFC_LLC_CO_ACTIVE:
        /* Send queued data */
        nfc_peer_connection_flush(self);
        break;
    case NFC_LLC_CO_CONNECTING:
    case NFC_LLC_CO_ACCEPTING:
    case NFC_LLC_CO_ABANDONED:
    case NFC_LLC_CO_DISCONNECTING:
        break;
    }
}

static
void
nfc_peer_connection_nop(
    NfcPeerConnection* self)
{
}

static
void
nfc_peer_connection_default_data_received(
    NfcPeerConnection* self,
    const void* data,
    guint len)
{
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_peer_connection_init(
    NfcPeerConnection* self)
{
    NfcPeerConnectionPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcPeerConnectionPriv);
    NfcPeerConnectionLlcpState* ps = &priv->ps;
    NfcLlcParam* miu = &priv->miu_param;
    NfcLlcParam* rw = &priv->rw_param;

    self->priv = priv;
    priv->send_buf = g_byte_array_new();

    /* Set up local parameters */
    miu->type = NFC_LLC_PARAM_MIUX;
    rw->type = NFC_LLC_PARAM_RW;
    priv->lp[0] = miu;
    priv->lp[1] = rw;

    /* Default (maximum) values for local parameters */
    miu->value.miu = NFC_LLC_LOCAL_MIU;
    rw->value.rw = NFC_LLC_LOCAL_RW;

    /* Default values for remote parameters */
    ps->rmiu = NFC_LLC_MIU_DEFAULT;
    ps->rwr = NFC_LLC_RW_DEFAULT;
}

static
void
nfc_peer_connection_finalize(
    GObject* object)
{
    NfcPeerConnection* self = THIS(object);
    NfcPeerConnectionPriv* priv = self->priv;

    if (self->service && self->state != NFC_LLC_CO_DEAD) {
        nfc_peer_service_connection_dead(self->service, self);
    }
    nfc_peer_connection_drop_queued_data(self);
    nfc_peer_service_unref(self->service);
    gutil_idle_pool_destroy(priv->pool);
    g_byte_array_free(priv->send_buf, TRUE);
    g_free(priv->name);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_peer_connection_class_init(
    NfcPeerConnectionClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcPeerConnectionPriv));
    klass->accept = nfc_peer_connection_accepted;
    klass->accept_cancelled = nfc_peer_connection_nop;
    klass->state_changed = nfc_peer_connection_default_state_changed;
    klass->data_received = nfc_peer_connection_default_data_received;
    klass->data_dequeued = nfc_peer_connection_nop;
    G_OBJECT_CLASS(klass)->finalize = nfc_peer_connection_finalize;
    nfc_peer_connection_signals[SIGNAL_STATE_CHANGED] =
        g_signal_new(SIGNAL_STATE_CHANGED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
