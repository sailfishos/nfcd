/*
 * Copyright (C) 2020-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2020-2021 Jolla Ltd.
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

#include "nfc_initiator_p.h"
#include "nfc_initiator_impl.h"
#include "nfc_log.h"

#include <gutil_macros.h>
#include <gutil_misc.h>

struct nfc_initiator_priv {
    NfcTransmission* current; /* Pointer */
    NfcTransmission* next;    /* Reference */
    GBytes* next_data;
    gboolean deactivated;
};

#define THIS(obj) NFC_INITIATOR(obj)
#define THIS_TYPE NFC_TYPE_INITIATOR
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS nfc_initiator_parent_class
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, THIS_TYPE, \
        NfcInitiatorClass)

G_DEFINE_ABSTRACT_TYPE(NfcInitiator, nfc_initiator, PARENT_TYPE)

enum nfc_initiator_signal {
    SIGNAL_REACTIVATED,
    SIGNAL_TRANSMISSION,
    SIGNAL_GONE,
    SIGNAL_COUNT
};

#define SIGNAL_REACTIVATED_NAME   "nfc-initiator-reactivated"
#define SIGNAL_TRANSMISSION_NAME  "nfc-initiator-transmission"
#define SIGNAL_GONE_NAME          "nfc-initiator-gone"

static guint nfc_initiator_signals[SIGNAL_COUNT] = { 0 };

struct nfc_transmission {
    NfcInitiator* owner;
    NfcTransmissionDoneFunc done;
    void* user_data;
    gboolean responded;
    gint ref_count;
};

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
inline
gboolean
nfc_initiator_can_deactivate(
    NfcInitiator* self)
{
    return self->present && !self->priv->deactivated;
}

static
void
nfc_initiator_do_deactivate(
    NfcInitiator* self)
{
    /* Caller has checked nfc_initiator_can_deactivate() */
    self->priv->deactivated = TRUE;
    GET_THIS_CLASS(self)->deactivate(self);
}

static
void
nfc_initiator_drop_transactions(
    NfcInitiatorPriv* priv)
{
    if (priv->next_data) {
        g_bytes_unref(priv->next_data);
        priv->next_data = NULL;
    }
    if (priv->next) {
        priv->next->owner = NULL;
        /* This is a reference */
        nfc_transmission_unref(priv->next);
        priv->next = NULL;
    }
    if (priv->current) {
        NfcTransmission* current = priv->current;

        /* And this is just a pointer */
        priv->current = NULL;
        current->owner = NULL;
        if (current->responded && current->done) {
            NfcTransmissionDoneFunc done = current->done;

            /* Make sure completion callback is not invoked twice */
            current->done = NULL;
            nfc_transmission_ref(current);
            done(current, FALSE, current->user_data);
            nfc_transmission_unref(current);
        }
    }
}

/*==========================================================================*
 * Transmission API
 *==========================================================================*/

static
NfcTransmission*
nfc_transmission_new(
    NfcInitiator* initiator)
{
    NfcTransmission* self = g_slice_new0(NfcTransmission);

    self->owner = initiator;
    g_atomic_int_set(&self->ref_count, 1);
    return self;
}

static
void
nfc_transmission_free(
    NfcTransmission* self)
{
    NfcInitiator* owner = self->owner;

    if (owner) {
        NfcInitiatorPriv* priv = owner->priv;

        /*
         * Clear the pointer. Note that priv->next is an internal reference
         * meaning that NfcTransmission pointed to by priv->next can't get
         * here before pointer is cleared and reference is released.
         * Therefore, there's no need to check priv->next.
         */
        if (priv->current == self) {
            priv->current = NULL;
            if (!self->responded && nfc_initiator_can_deactivate(owner)) {
                /* Transmission was dropped without responding */
                GDEBUG("Transmission dropped, deactivating");
                nfc_initiator_do_deactivate(owner);
            }
        }
    }
    gutil_slice_free(self);
}

NfcTransmission*
nfc_transmission_ref(
    NfcTransmission* self)
{
    if (G_LIKELY(self)) {
        g_atomic_int_inc(&self->ref_count);
    }
    return self;
}

void
nfc_transmission_unref(
    NfcTransmission* self)
{
    if (G_LIKELY(self)) {
        if (g_atomic_int_dec_and_test(&self->ref_count)) {
            nfc_transmission_free(self);
        }
    }
}

gboolean
nfc_transmission_respond(
    NfcTransmission* self,
    const void* data,
    guint len,
    NfcTransmissionDoneFunc done,
    void* user_data)
{
    if (G_LIKELY(self && !self->responded)) {
        NfcInitiator* owner = self->owner;

        self->responded = TRUE;
        if (owner) {
            self->done = done;
            self->user_data = user_data;
            nfc_transmission_ref(self);
            if (GET_THIS_CLASS(owner)->respond(owner, data, len)) {
                nfc_transmission_unref(self);
                return TRUE;
            }
            self->done = NULL;
            nfc_transmission_unref(self);
        }
    }
    return FALSE;
}

gboolean
nfc_transmission_respond_bytes(
    NfcTransmission* self,
    GBytes* data,
    NfcTransmissionDoneFunc done,
    void* user_data)
{
    if (G_LIKELY(self && !self->responded)) {
        NfcInitiator* owner = self->owner;

        self->responded = TRUE;
        if (owner) {
            self->done = done;
            self->user_data = user_data;
            nfc_transmission_ref(self);
            if (GET_THIS_CLASS(owner)->respond_bytes(owner, data)) {
                nfc_transmission_unref(self);
                return TRUE;
            }
            self->done = NULL;
            nfc_transmission_unref(self);
        }
    }
    return FALSE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcInitiator*
nfc_initiator_ref(
    NfcInitiator* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_initiator_unref(
    NfcInitiator* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

void
nfc_initiator_deactivate(
    NfcInitiator* self)
{
    if (G_LIKELY(self) && nfc_initiator_can_deactivate(self)) {
        nfc_initiator_do_deactivate(self);
    }
}

gulong
nfc_initiator_add_transmission_handler(
    NfcInitiator* self,
    NfcTransmissionHandlerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_TRANSMISSION_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_initiator_add_gone_handler(
    NfcInitiator* self,
    NfcInitiatorFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_GONE_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_initiator_add_reactivated_handler(
    NfcInitiator* self,
    NfcInitiatorFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_REACTIVATED_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_initiator_remove_handler(
    NfcInitiator* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_initiator_remove_handlers(
    NfcInitiator* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

static
void
nfc_initiator_handle_transmission(
    NfcInitiator* self,
    NfcTransmission* tx,
    const GUtilData* data)
{
    gboolean handled = FALSE;

    g_signal_emit(self, nfc_initiator_signals[SIGNAL_TRANSMISSION], 0, tx,
        data, &handled);
    if (!handled && nfc_initiator_can_deactivate(self)) {
        /* Signal wasn't handled, drop the link */
        GDEBUG("Incoming transmission not handled, deactivating");
        nfc_initiator_do_deactivate(self);
    }
}

void
nfc_initiator_transmit(
    NfcInitiator* self,
    const void* bytes,
    guint size)
{
    if (G_LIKELY(self)) {
        NfcInitiatorPriv* priv = self->priv;

        if (priv->current) {
            /*
             * This can only legitimately happen if we have already responded
             * to the current transmission, but haven't yet got a confirmation
             * that our response has been sent.
             */
            if (!priv->current->responded || priv->next) {
                /*
                 * Stray transmission, should we ignore it? Let's see if
                 * that ever happens in real life. For now, treat it as an
                 * unrecoverable error and deactivate the RF interface.
                 */
                if (nfc_initiator_can_deactivate(self)) {
                    GDEBUG("Unexpected transmission, deactivating");
                    nfc_initiator_do_deactivate(self);
                }
            } else {
                /*
                 * Let's queue it until the current response has been sent.
                 * There may be some room for optimization here (we could
                 * start processing the next transmission before the previous
                 * one has fully completed).
                 */
                priv->next = nfc_transmission_new(self);
                priv->next_data = g_bytes_new(bytes, size); /* Copy the data */
            }
        } else {
            GUtilData data;
            NfcTransmission* tx = nfc_transmission_new(self);

            /* Fresh new transmission coming in, notify the handler */
            priv->current = tx;
            data.bytes = bytes;
            data.size = size;
            nfc_initiator_handle_transmission(self, priv->current, &data);

            /*
             * If the handler doesn't reference it, this will deallocate
             * the transmission and wipe the priv->current pointer. On the
             * other hand, priv->current can already be NULL if we have
             * been deactivated by now.
             */
            nfc_transmission_unref(tx);
        }
    }
}

void
nfc_initiator_response_sent(
    NfcInitiator* self,
    NFC_TRANSMIT_STATUS status)
{
    if (G_LIKELY(self)) {
        NfcInitiatorPriv* priv = self->priv;
        NfcTransmission* t = priv->current;
        NfcTransmission* next = priv->next; /* This was a reference */
        GBytes* bytes = priv->next_data;

        /* The next transmission (if any) becomes the current one */
        priv->current = next;
        priv->next_data= NULL;
        priv->next = NULL;

        if (t && t->done) {
            NfcTransmissionDoneFunc done = t->done;

            /* Make sure completion callback is not invoked twice */
            t->done = NULL;

            /* Let the handler know that response has been sent */
            nfc_transmission_ref(t);
            done(t, status == NFC_TRANSMIT_STATUS_OK, t->user_data);
            nfc_transmission_unref(t);
        }

        if (next) {
            GUtilData data;

            data.bytes = g_bytes_get_data(bytes, &data.size);
            nfc_initiator_handle_transmission(self, next, &data);
            nfc_transmission_unref(next); /* Release our reference */
        }

        if (bytes) {
            g_bytes_unref(bytes);
        }
    }
}

void
nfc_initiator_gone(
    NfcInitiator* self)
{
    if (G_LIKELY(self) && self->present) {
        self->present = FALSE;
        nfc_initiator_ref(self);
        nfc_initiator_drop_transactions(self->priv);
        GET_THIS_CLASS(self)->gone(self);
        nfc_initiator_unref(self);
    }
}

void
nfc_initiator_reactivated(
    NfcInitiator* self) /* Since 1.2.0 */
{
    if (G_LIKELY(self) && self->present) {
        nfc_initiator_ref(self);
        g_signal_emit(self, nfc_initiator_signals[SIGNAL_REACTIVATED], 0);
        nfc_initiator_unref(self);
    }
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
gboolean
nfc_initiator_default_respond(
    NfcInitiator* self,
    const void* data,
    guint len)
{
    return FALSE;
}

static
gboolean
nfc_initiator_default_respond_bytes(
    NfcInitiator* self,
    GBytes* bytes)
{
    if (bytes) {
        gsize len = 0;
        const void* data = g_bytes_get_data(bytes, &len);

        return GET_THIS_CLASS(self)->respond(self, data, (guint) len);
    } else {
        return GET_THIS_CLASS(self)->respond(self, NULL, 0);
    }
}

static
void
nfc_initiator_nop(
    NfcInitiator* self)
{
}

static
void
nfc_initiator_default_gone(
    NfcInitiator* self)
{
    g_signal_emit(self, nfc_initiator_signals[SIGNAL_GONE], 0);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_initiator_init(
    NfcInitiator* self)
{
    NfcInitiatorPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcInitiatorPriv);

    /* When initiator is created, it must be present, right? */
    self->present = TRUE;
    self->priv = priv;
}

static
void
nfc_initiator_finalize(
    GObject* object)
{
    nfc_initiator_drop_transactions(THIS(object)->priv);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_initiator_class_init(
    NfcInitiatorClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    g_type_class_add_private(klass, sizeof(NfcInitiatorPriv));
    klass->respond = nfc_initiator_default_respond;
    klass->respond_bytes = nfc_initiator_default_respond_bytes;
    klass->deactivate = nfc_initiator_nop;
    klass->gone = nfc_initiator_default_gone;
    G_OBJECT_CLASS(klass)->finalize = nfc_initiator_finalize;
    nfc_initiator_signals[SIGNAL_REACTIVATED] =
        g_signal_new(SIGNAL_REACTIVATED_NAME, type, G_SIGNAL_RUN_FIRST, 0,
            NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_initiator_signals[SIGNAL_TRANSMISSION] =
        g_signal_new(SIGNAL_TRANSMISSION_NAME, type, G_SIGNAL_RUN_LAST, 0,
            g_signal_accumulator_true_handled, NULL, NULL,
            G_TYPE_BOOLEAN, 2, G_TYPE_POINTER, G_TYPE_POINTER);
    nfc_initiator_signals[SIGNAL_GONE] =
        g_signal_new(SIGNAL_GONE_NAME, type, G_SIGNAL_RUN_FIRST, 0,
            NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
