/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_target_impl.h"
#include "nfc_target_p.h"
#include "nfc_log.h"

#include <gutil_macros.h>
#include <gutil_misc.h>

#define DEFAULT_TRANSMIT_TIMEOUT_MS (500)
#define DEFAULT_REACTIVATION_TIMEOUT_MS (1500)

typedef struct nfc_target_request NfcTargetRequest;
typedef struct nfc_target_request_type {
    const char* name;
    gboolean (*submit)(NfcTargetRequest* req);
    void (*cancel)(NfcTargetRequest* req);
    void (*abandon)(NfcTargetRequest* req);
    guint (*timeout_ms)(NfcTargetRequest* req);
    void (*transmit_done)(NfcTargetRequest* req, NFC_TRANSMIT_STATUS status,
        const void* data, guint len);
    void (*reactivated)(NfcTargetRequest* req);
    void (*timed_out)(NfcTargetRequest* req);
    void (*failed)(NfcTargetRequest* req);
    void (*free)(NfcTargetRequest* req);
} NfcTargetRequestType;

struct nfc_target_request {
    NfcTargetRequest* next;
    NfcTargetSequence* seq;
    const NfcTargetRequestType* type;
    NfcTarget* target;
    guint id;
    guint timeout;
    GDestroyNotify destroy;
    void* user_data;
};

typedef struct nfc_target_transmit_request {
    NfcTargetRequest request;
    const void* data;
    void* copied_data;
    guint len;
    NfcTargetTransmitFunc complete;
} NfcTargetTransmitRequest;

typedef struct nfc_target_reactivate_request {
    NfcTargetRequest request;
    NfcTargetReactivateFunc callback;
} NfcTargetReactivateRequest;

typedef struct nfc_target_request_queue {
    NfcTargetRequest* first;
    NfcTargetRequest* last;
} NfcTargetRequestQueue;

struct nfc_target_sequence {
    NfcTargetSequence* next;
    gint refcount;
    NfcTarget* target;
};

typedef struct nfc_target_sequence_queue {
    NfcTargetSequence* first;
    NfcTargetSequence* last;
} NfcTargetSequenceQueue;

struct nfc_target_priv {
    guint last_req_id;
    guint continue_id;
    NfcTargetRequest* req_active;
    NfcTargetSequenceQueue seq_queue;
    NfcTargetRequestQueue req_queue;
    guint tx_timeout_ms;
    guint ra_timeout_ms;
    gboolean reactivating;
};

G_DEFINE_ABSTRACT_TYPE(NfcTarget, nfc_target, G_TYPE_OBJECT)
#define NFC_TARGET_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), \
        NFC_TYPE_TARGET, NfcTargetClass)

enum nfc_target_signal {
    SIGNAL_SEQUENCE,
    SIGNAL_GONE,
    SIGNAL_COUNT
};

#define SIGNAL_SEQUENCE_NAME    "nfc-target-sequence"
#define SIGNAL_GONE_NAME        "nfc-target-gone"

static guint nfc_target_signals[SIGNAL_COUNT] = { 0 };

static
void
nfc_target_schedule_next_request(
    NfcTarget* self);

static
void
nfc_target_set_sequence(
    NfcTarget* self,
    NfcTargetSequence* seq);

/*==========================================================================*
 * Transmit request
 *==========================================================================*/

static inline
NfcTargetTransmitRequest*
nfc_target_transmit_request_cast(
    NfcTargetRequest* req)
{
    return G_CAST(req, NfcTargetTransmitRequest, request);
}

static
gboolean
nfc_target_transmit_request_submit(
    NfcTargetRequest* req)
{
    NfcTarget* target = req->target;
    NfcTargetTransmitRequest* tx = nfc_target_transmit_request_cast(req);

    return NFC_TARGET_GET_CLASS(target)->transmit(target, tx->data, tx->len);
}

static
void
nfc_target_transmit_request_cancel(
    NfcTargetRequest* req)
{
    NFC_TARGET_GET_CLASS(req->target)->cancel_transmit(req->target);
}

static
void
nfc_target_transmit_request_abandon(
    NfcTargetRequest* req)
{
    nfc_target_transmit_request_cast(req)->complete = NULL;
}

static
guint
nfc_target_transmit_request_timeout_ms(
    NfcTargetRequest* req)
{
    return req->target->priv->tx_timeout_ms;
}

static
void
nfc_target_transmit_request_done(
    NfcTargetRequest* req,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len)
{
    NfcTargetTransmitRequest* tx = nfc_target_transmit_request_cast(req);
    NfcTargetTransmitFunc complete = tx->complete;

    if (complete) {
        tx->complete = NULL;
        complete(req->target, status, data, len, req->user_data);
    }
}

static
void
nfc_target_transmit_request_failed(
    NfcTargetRequest* req)
{
    nfc_target_transmit_request_done(req, NFC_TRANSMIT_STATUS_ERROR, NULL, 0);
}

static
void
nfc_target_transmit_request_timed_out(
    NfcTargetRequest* req)
{
    nfc_target_transmit_request_done(req, NFC_TRANSMIT_STATUS_TIMEOUT, NULL, 0);
}

static
void
nfc_target_transmit_request_free(
    NfcTargetRequest* req)
{
    NfcTargetTransmitRequest* tx = nfc_target_transmit_request_cast(req);

    g_free(tx->copied_data);
    g_slice_free1(sizeof(*tx), tx);
}

static
NfcTargetTransmitRequest*
nfc_target_transmit_request_new(
    NfcTarget* target,
    const void* data,
    guint len,
    NfcTargetSequence* seq,
    NfcTargetTransmitFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    static const NfcTargetRequestType transmit_request_type = {
        "Transmit",
        nfc_target_transmit_request_submit,
        nfc_target_transmit_request_cancel,
        nfc_target_transmit_request_abandon,
        nfc_target_transmit_request_timeout_ms,
        nfc_target_transmit_request_done,
        nfc_target_transmit_request_failed, /* Not expected to be called */
        nfc_target_transmit_request_timed_out,
        nfc_target_transmit_request_failed,
        nfc_target_transmit_request_free
    };

    NfcTargetTransmitRequest* tx = g_slice_new0(NfcTargetTransmitRequest);
    NfcTargetRequest* req = &tx->request;

    GASSERT(!seq || seq->target == target);
    if (seq && seq->target == target) {
        req->seq = nfc_target_sequence_ref(seq);
    }

    req->id = nfc_target_generate_id(target);
    req->type = &transmit_request_type;
    req->target = target;
    req->destroy = destroy;
    req->user_data = user_data;
    tx->data = data;
    tx->len = len;
    tx->complete = complete;
    return tx;
};

/*==========================================================================*
 * Reactivate request
 *==========================================================================*/

static
void
nfc_target_request_nop(
    NfcTargetRequest* req)
{
}

static inline
NfcTargetReactivateRequest*
nfc_target_reactivate_request_cast(
    NfcTargetRequest* req)
{
    return G_CAST(req, NfcTargetReactivateRequest, request);
}

static
gboolean
nfc_target_reactivate_request_submit(
    NfcTargetRequest* req)
{
    NfcTarget* target = req->target;

    if (NFC_TARGET_GET_CLASS(target)->reactivate(target)) {
        NfcTargetPriv* priv = target->priv;

        GASSERT(!priv->reactivating);
        priv->reactivating = TRUE;
        return TRUE;
    }
    return FALSE;
}

static
void
nfc_target_reactivate_request_abandon(
    NfcTargetRequest* req)
{
    nfc_target_reactivate_request_cast(req)->callback = NULL;
}

static
guint
nfc_target_reactivate_request_timeout_ms(
    NfcTargetRequest* req)
{
    return req->target->priv->ra_timeout_ms;
}

static
void
nfc_target_reactivate_request_unexpected_call(
    NfcTargetRequest* req,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len)
{
    /* Not expected to be called */
}

static
void
nfc_target_reactivate_request_done(
    NfcTargetRequest* req,
    NFC_REACTIVATE_STATUS status)
{
    NfcTargetReactivateRequest* re = nfc_target_reactivate_request_cast(req);
    NfcTargetReactivateFunc cb = re->callback;
    NfcTargetPriv* priv = req->target->priv;

    /*
     * Note that priv->reactivating is going to be FALSE if
     * reactivation failed to start but we still invoke the
     * callback because nfc_target_reactivate() has returned
     * or is going to return TRUE.
     */
    priv->reactivating = FALSE;
    if (cb) {
        re->callback = NULL;
        cb(req->target, status, req->user_data);
    }
}

static
void
nfc_target_reactivate_request_ok(
    NfcTargetRequest* req)
{
    nfc_target_reactivate_request_done(req, NFC_REACTIVATE_STATUS_SUCCESS);
}

static
void
nfc_target_reactivate_request_timed_out(
    NfcTargetRequest* req)
{
    nfc_target_reactivate_request_done(req, NFC_REACTIVATE_STATUS_TIMEOUT);
    NFC_TARGET_GET_CLASS(req->target)->deactivate(req->target);
}

static
void
nfc_target_reactivate_request_failed(
    NfcTargetRequest* req)
{
    nfc_target_reactivate_request_done(req, NFC_REACTIVATE_STATUS_GONE);
}

static
void
nfc_target_reactivate_request_free(
    NfcTargetRequest* req)
{
    NfcTargetReactivateRequest* re = nfc_target_reactivate_request_cast(req);

    GASSERT(!req->target->priv->reactivating);
    g_slice_free1(sizeof(*re), re);
}

static
NfcTargetRequest*
nfc_target_reactivate_request_new(
    NfcTarget* target,
    NfcTargetSequence* seq,
    NfcTargetReactivateFunc cb,
    GDestroyNotify destroy,
    void* user_data)
{
    static const NfcTargetRequestType reactivate_request_type = {
        "Reactivate",
        nfc_target_reactivate_request_submit,
        nfc_target_request_nop,
        nfc_target_reactivate_request_abandon,
        nfc_target_reactivate_request_timeout_ms,
        nfc_target_reactivate_request_unexpected_call,
        nfc_target_reactivate_request_ok,
        nfc_target_reactivate_request_timed_out,
        nfc_target_reactivate_request_failed,
        nfc_target_reactivate_request_free
    };

    NfcTargetReactivateRequest* re = g_slice_new0(NfcTargetReactivateRequest);
    NfcTargetRequest* req = &re->request;

    GASSERT(!seq || seq->target == target);
    if (seq && seq->target == target) {
        req->seq = nfc_target_sequence_ref(seq);
    }

    req->id = nfc_target_generate_id(target);
    req->type = &reactivate_request_type;
    req->target = target;
    req->destroy = destroy;
    req->user_data = user_data;
    re->callback = cb;
    return req;
};

/*==========================================================================*
 * Sequence
 *==========================================================================*/

static
void
nfc_target_sequence_dealloc(
    NfcTargetSequence* self)
{
    NfcTarget* target = self->target;

    if (target) {
        NfcTargetPriv* priv = target->priv;
        NfcTargetSequenceQueue* queue = &priv->seq_queue;

        if (queue->first == self) {
            /* Typically sequences are being deallocated in the order
             * they were created and that's optimal */
            if (!(queue->first = self->next)) {
                queue->last = NULL;
            }
        } else {
            /* In any case it must be on the list */
            NfcTargetSequence* prev = queue->first;

            /* And there shouldn't be too many sequences in the list */
            while (prev->next != self) {
                prev = prev->next;
            }

            if (!(prev->next = self->next)) {
                queue->last = prev;
            }
        }
        if (target->sequence == self) {
            NfcTargetRequest* req = priv->req_queue.first;

            /*
             * The last reference to the current sequence is gone.
             * We need to clear the pointer to it.
             *
             * Also, we need to ensure that requests are processed
             * in the order they were submitted. If the next request
             * doesn't belong to a sequence, then we will clear the
             * current sequence and submit the request anyway (even
             * though there may be some sequences in the queue).
             */
            nfc_target_set_sequence(target, req ? req->seq : queue->first);

            /* Finished sequence can unblock some requests */
            nfc_target_schedule_next_request(target);
        }
        self->target = NULL;
        self->next = NULL;
    }
    g_slice_free(NfcTargetSequence, self);
}

NfcTargetSequence*
nfc_target_sequence_ref(
    NfcTargetSequence* self)
{
    if (G_LIKELY(self)) {
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
nfc_target_sequence_unref(
    NfcTargetSequence* self)
{
    if (G_LIKELY(self)) {
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            nfc_target_sequence_dealloc(self);
        }
    }
}

static
void
nfc_target_set_sequence(
    NfcTarget* self,
    NfcTargetSequence* seq)
{
    if (self->sequence != seq) {
        self->sequence = seq;
        nfc_target_sequence_ref(seq);
        NFC_TARGET_GET_CLASS(self)->sequence_changed(self);
        nfc_target_sequence_unref(seq);
    }
}

NfcTargetSequence*
nfc_target_sequence_new(
    NfcTarget* target)
{
    if (G_LIKELY(target)) {
        NfcTargetSequence* self = g_slice_new0(NfcTargetSequence);
        NfcTargetPriv* priv = target->priv;
        NfcTargetSequenceQueue* queue = &priv->seq_queue;

        g_atomic_int_set(&self->refcount, 1);
        self->target = target;

        /* Insert it to the queue */
        if (queue->last) {
            queue->last->next = self;
        } else {
            GASSERT(!queue->first);
            queue->first = self;
        }
        queue->last = self;

        /* If there's no active sequence yet, this one automatically
         * becomes active even though it doesn't yet have any requests
         * associated with it */
        if (!target->sequence) {
            nfc_target_set_sequence(target, self);
        }
        return self;
    }
    return NULL;
}

void
nfc_target_sequence_free(
    NfcTargetSequence* self)
{
    nfc_target_sequence_unref(self);
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
nfc_target_free_request(
    NfcTargetRequest* req)
{
    const NfcTargetRequestType* rt = req->type;

    nfc_target_sequence_unref(req->seq);
    if (req->timeout) {
        g_source_remove(req->timeout);
    }
    if (req->destroy) {
        req->destroy(req->user_data);
    }
    rt->free(req);
}

static
void
nfc_target_fail_request(
    NfcTarget* self,
    NfcTargetRequest* req)
{
    const NfcTargetRequestType* rt = req->type;

    rt->failed(req);
    nfc_target_free_request(req);
}

static
gboolean
nfc_target_request_timeout(
    gpointer user_data)
{
    NfcTargetRequest* req = user_data;
    const NfcTargetRequestType* rt = req->type;
    NfcTarget* self = nfc_target_ref(req->target);
    NfcTargetPriv* priv = self->priv;

    GDEBUG("%s request timed out", rt->name);
    GASSERT(req->timeout);
    req->timeout = 0;

    GASSERT(req == priv->req_active);
    priv->req_active = NULL;
    rt->cancel(req);
    rt->timed_out(req);
    nfc_target_free_request(req);
    nfc_target_schedule_next_request(self);
    nfc_target_unref(self);
    return G_SOURCE_REMOVE;
}

static
NfcTargetRequest*
nfc_target_transmit_find_req(
    NfcTargetRequestQueue* queue,
    NfcTargetSequence* seq)
{
    NfcTargetRequest* req = queue->first;

    while ((req && req->seq != seq)) {
        req = req->next;
    }
    return req;
}

static
void
nfc_target_transmit_queue_req(
    NfcTargetRequestQueue* queue,
    NfcTargetRequest* req)
{
    if (queue->last) {
        queue->last->next = req;
    } else {
        GASSERT(!queue->first);
        queue->first = req;
    }
    queue->last = req;
}

static
NfcTargetRequest*
nfc_target_transmit_dequeue_req(
    NfcTarget* self)
{
    NfcTargetPriv* priv = self->priv;
    NfcTargetRequestQueue* queue = &priv->req_queue;
    NfcTargetRequest* req = queue->first;
    NfcTargetSequence* seq = self->sequence;

    if (req) {
        if (!seq || req->seq == seq) {
            if (!(queue->first = req->next)) {
                queue->last = NULL;
            }
            req->next = NULL;
        } else {
            NfcTargetRequest* prev = req;

            /* Scan the queue */
            req = req->next;
            while (req) {
                if (!seq || req->seq == seq) {
                    if (!(prev->next = req->next)) {
                        queue->last = prev;
                    }
                    req->next = NULL;
                    break;
                }
                prev = req;
                req = req->next;
            }
        }
    }
    return req;
}

static
gboolean
nfc_target_submit_request(
    NfcTarget* self,
    NfcTargetRequest* req)
{
    NfcTargetPriv* priv = self->priv;
    const NfcTargetRequestType* rt = req->type;

    priv->req_active = req;
    if (!self->sequence && req->seq) {
        nfc_target_set_sequence(self, req->seq);
    }
    if (rt->submit(req)) {
        const guint ms = rt->timeout_ms(req);

        if (ms) {
            GASSERT(!req->timeout);
            req->timeout = g_timeout_add(ms, nfc_target_request_timeout, req);
        }
        return TRUE;
    } else {
        priv->req_active = NULL;
        return FALSE;
    }
}

static
void
nfc_target_submit_next_request(
    NfcTarget* self)
{
    NfcTargetPriv* priv = self->priv;

    if (!priv->req_active) {
        NfcTargetRequest* req = nfc_target_transmit_dequeue_req(self);

        nfc_target_ref(self);
        while (req) {
            if (nfc_target_submit_request(self, req)) {
                /* Request submitted, wait for completion */
                nfc_target_unref(self);
                return;
            }
            nfc_target_fail_request(self, req);
            req = nfc_target_transmit_dequeue_req(self);
        }
        nfc_target_unref(self);
    }
}

static
gboolean
nfc_target_next_transmit(
    gpointer user_data)
{
    NfcTarget* self = NFC_TARGET(user_data);
    NfcTargetPriv* priv = self->priv;

    priv->continue_id = 0;
    nfc_target_submit_next_request(self);
    return G_SOURCE_REMOVE;
}

static
void
nfc_target_schedule_next_request(
    NfcTarget* self)
{
    NfcTargetPriv* priv = self->priv;

    if (priv->req_queue.first && !priv->continue_id) {
        priv->continue_id = g_idle_add(nfc_target_next_transmit, self);
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcTarget*
nfc_target_ref(
    NfcTarget* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(NFC_TARGET(self));
    }
    return self;
}

void
nfc_target_unref(
    NfcTarget* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(NFC_TARGET(self));
    }
}

guint
nfc_target_generate_id(
    NfcTarget* self)
{
    guint id = 0;

    if (G_LIKELY(self)) {
        NfcTargetPriv* priv = self->priv;

        /*
         * This could be made more sophisticated to actually guarantee that
         * the returned id is not being used, but it seems so unlikely that
         * we ever wrap around 32-bit counter and bump into an id which is
         * still active... So let's keep it simple. Just make sure that we
         * never return zero.
         */
        id = ++(priv->last_req_id);
        if (!id) {
            id = ++(priv->last_req_id);
        }
    }
    return id;
}

guint
nfc_target_transmit(
    NfcTarget* self,
    const void* data,
    guint len,
    NfcTargetSequence* seq,
    NfcTargetTransmitFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    guint id = 0;

    if (G_LIKELY(self)) {
        NfcTargetPriv* priv = self->priv;
        NfcTargetTransmitRequest* tx = nfc_target_transmit_request_new(self,
            data, len, seq, complete, destroy, user_data);
        NfcTargetRequest* req = &tx->request;

        /* Request id to return */
        id = req->id;

        /* Check if the request can be submitted right away */
        if (!priv->req_active && (req->seq == self->sequence)) {
            /*
             * The data will be copied by the transmit method, no need
             * to make another copy and attach it to the request.
             */
            if (!nfc_target_submit_request(self, req)) {
                nfc_target_set_sequence(self, NULL);
                id = 0;
                req->destroy = NULL;
                nfc_target_free_request(req);
            }
        } else {
            /*
             * Can't pass the data pointer to the transmit implementation
             * right away, make a copy.
             */
            tx->data = tx->copied_data = g_memdup(data, len);
            nfc_target_transmit_queue_req(&priv->req_queue, req);
        }
    }
    return id;
}

gboolean
nfc_target_cancel_transmit(
    NfcTarget* self,
    guint id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        NfcTargetPriv* priv = self->priv;
        NfcTargetRequest* req = priv->req_active;

        if (req && req->id == id) {
            const NfcTargetRequestType* rt = req->type;

            priv->req_active = NULL;
            rt->abandon(req);
            rt->cancel(req);
            nfc_target_free_request(req);
            nfc_target_schedule_next_request(self);
            return TRUE;
        } else {
            NfcTargetRequestQueue* queue = &priv->req_queue;

            req = queue->first;
            if (req) {
                if (req->id == id) {
                    const NfcTargetRequestType* rt = req->type;

                    /* The first queued request is cancelled */
                    rt->abandon(req);
                    if (!(queue->first = req->next)) {
                        queue->last = NULL;
                    }
                    nfc_target_free_request(req);
                    return TRUE;
                } else {
                    NfcTargetRequest* prev = req;

                    /* Scan the queue */
                    req = req->next;
                    while (req) {
                        if (req->id == id) {
                            const NfcTargetRequestType* rt = req->type;

                            rt->abandon(req);
                            if (!(prev->next = req->next)) {
                                queue->last = prev;
                            }
                            nfc_target_free_request(req);
                            return TRUE;
                        }
                        prev = req;
                        req = req->next;
                    }
                }
            }
        }
    }
    return FALSE;
}

void
nfc_target_set_transmit_timeout(
    NfcTarget* self,
    int ms) /* Since 1.0.37 */
{
    if (G_LIKELY(self)) {
        NfcTargetPriv* priv = self->priv;

        if (ms < 0) {
            priv->tx_timeout_ms = DEFAULT_TRANSMIT_TIMEOUT_MS;
        } else {
            priv->tx_timeout_ms = ms;
            if (priv->tx_timeout_ms) {
                GDEBUG("Transmission timeout %u ms", priv->tx_timeout_ms);
            } else {
                GDEBUG("No transmission timeout");
            }
        }
    }
}

void
nfc_target_deactivate(
    NfcTarget* self)
{
    if (G_LIKELY(self) && self->present) {
        NFC_TARGET_GET_CLASS(self)->deactivate(self);
    }
}

gboolean
nfc_target_can_reactivate(
    NfcTarget* self)
{
    return G_LIKELY(self) && !self->priv->reactivating &&
        NFC_TARGET_GET_CLASS(self)->reactivate;
}

gboolean
nfc_target_reactivate(
    NfcTarget* self,
    NfcTargetSequence* seq,
    NfcTargetReactivateFunc func,
    void* user_data)
{
    if (G_LIKELY(self)) {
        NfcTargetPriv* priv = self->priv;
        NfcTargetClass* klass = NFC_TARGET_GET_CLASS(self);

        if (!priv->reactivating && klass->reactivate) {
            NfcTargetRequest* req = nfc_target_reactivate_request_new(self,
                seq, func, NULL, user_data);

            nfc_target_transmit_queue_req(&priv->req_queue, req);
            nfc_target_submit_next_request(self);
            return TRUE;
        }
    }
    return FALSE;
}

void
nfc_target_set_reactivate_timeout(
    NfcTarget* self,
    guint ms)
{
    if (G_LIKELY(self)) {
        self->priv->ra_timeout_ms = ms;
    }
}

gulong
nfc_target_add_sequence_handler(
    NfcTarget* self,
    NfcTargetFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_SEQUENCE_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_target_add_gone_handler(
    NfcTarget* self,
    NfcTargetFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_GONE_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_target_remove_handler(
    NfcTarget* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_target_remove_handlers(
    NfcTarget* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

void
nfc_target_reactivated(
    NfcTarget* self)
{
    if (G_LIKELY(self)) {
        NfcTargetPriv* priv = self->priv;
        NfcTargetRequest* req = priv->req_active;

        GASSERT(req);
        if (req) {
            const NfcTargetRequestType* rt = req->type;

            nfc_target_ref(self);
            priv->req_active = NULL;
            rt->reactivated(req);
            nfc_target_free_request(req);
            nfc_target_schedule_next_request(self);
            nfc_target_unref(self);
        }
    }
}

void
nfc_target_transmit_done(
    NfcTarget* self,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len)
{
    if (G_LIKELY(self)) {
        NfcTargetPriv* priv = self->priv;
        NfcTargetRequest* req = priv->req_active;

        GASSERT(req);
        if (req) {
            const NfcTargetRequestType* rt = req->type;

            nfc_target_ref(self);
            priv->req_active = NULL;
            rt->transmit_done(req, status, data, len);
            nfc_target_free_request(req);
            nfc_target_schedule_next_request(self);
            nfc_target_unref(self);
        }
    }
}

void
nfc_target_gone(
    NfcTarget* self)
{
    if (G_LIKELY(self) && self->present) {
        self->present = FALSE;
        NFC_TARGET_GET_CLASS(self)->gone(self);
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
gboolean
nfc_target_default_transmit(
    NfcTarget* self,
    const void* data,
    guint len)
{
    return FALSE;
}

static
void
nfc_target_nop(
    NfcTarget* self)
{
}

static
void
nfc_target_sequence_changed(
    NfcTarget* self)
{
    g_signal_emit(self, nfc_target_signals[SIGNAL_SEQUENCE], 0);
}

static
void
nfc_target_gone_handler(
    NfcTarget* self)
{
    g_signal_emit(self, nfc_target_signals[SIGNAL_GONE], 0);
}

static
void
nfc_target_init(
    NfcTarget* self)
{
    NfcTargetPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NFC_TYPE_TARGET,
        NfcTargetPriv);

    /* When target is created, it must be present, right? */
    self->present = TRUE;
    self->priv = priv;
    priv->ra_timeout_ms = DEFAULT_REACTIVATION_TIMEOUT_MS;
    priv->tx_timeout_ms = DEFAULT_TRANSMIT_TIMEOUT_MS;
}

static
void
nfc_target_dispose(
    GObject* object)
{
    NfcTarget* self = NFC_TARGET(object);
    NfcTargetPriv* priv = self->priv;
    NfcTargetRequestQueue* queue = &priv->req_queue;

    if (priv->continue_id) {
        g_source_remove(priv->continue_id);
        priv->continue_id = 0;
    }
    if (priv->req_active) {
        NfcTargetRequest* req = priv->req_active;
        const NfcTargetRequestType* rt = req->type;

        priv->req_active = NULL;
        rt->cancel(req);
        nfc_target_fail_request(self, req);
    }
    while (queue->first) {
        NfcTargetRequest* req = queue->first;

        if (!(queue->first = req->next)) {
            queue->last = NULL;
        }
        req->next = NULL;
        nfc_target_fail_request(self, req);
    }
    G_OBJECT_CLASS(nfc_target_parent_class)->dispose(object);
}

static
void
nfc_target_finalize(
    GObject* object)
{
    NfcTarget* self = NFC_TARGET(object);
    NfcTargetPriv* priv = self->priv;
    NfcTargetSequenceQueue* queue = &priv->seq_queue;
    NfcTargetSequence* seq = queue->first;

    /*
     * NfcTargetSequence can (theoretically) survive longer than
     * the associated NfcTarget, clear stale pointers.
     */
    while (seq) {
        NfcTargetSequence* next = seq->next;

        seq->target = NULL;
        seq->next = NULL;
        seq = next;
    }
    queue->first = queue->last = NULL;
    G_OBJECT_CLASS(nfc_target_parent_class)->finalize(object);
}

static
void
nfc_target_class_init(
    NfcTargetClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(NfcTargetPriv));
    klass->transmit = nfc_target_default_transmit;
    klass->cancel_transmit = nfc_target_nop;
    klass->deactivate = nfc_target_nop;
    klass->sequence_changed = nfc_target_sequence_changed;
    klass->gone = nfc_target_gone_handler;
    object_class->dispose = nfc_target_dispose;
    object_class->finalize = nfc_target_finalize;
    nfc_target_signals[SIGNAL_GONE] =
        g_signal_new(SIGNAL_GONE_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_target_signals[SIGNAL_SEQUENCE] =
        g_signal_new(SIGNAL_SEQUENCE_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
