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

#include "nfc_llc_io_impl.h"
#include "nfc_target_p.h"

#define GLOG_MODULE_NAME NFC_LLC_LOG_MODULE
#include <gutil_log.h>

#define DEFAULT_POLL_PERIOD (100) /* ms */

typedef struct nfc_llc_io_initiator {
    NfcLlcIo io;
    NfcTarget* target;
    guint poll_period;
    guint poll_id;
    guint tx_id;
} NfcLlcIoInitiator;

#define THIS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        THIS_TYPE, NfcLlcIoInitiator))
#define THIS_TYPE (nfc_llc_io_initiator_get_type())
#define PARENT_TYPE NFC_TYPE_LLC_IO
#define PARENT_CLASS (nfc_llc_io_initiator_parent_class)

GType nfc_llc_io_initiator_get_type(void) NFCD_INTERNAL;
typedef NfcLlcIoClass NfcLlcIoInitiatorClass;
G_DEFINE_TYPE(NfcLlcIoInitiator, nfc_llc_io_initiator, PARENT_TYPE)

static
gboolean
nfc_llc_io_initiator_send_symm(
    NfcLlcIoInitiator* self);

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
gboolean
nfc_llc_io_initiator_poll(
    gpointer user_data)
{
    NfcLlcIoInitiator* self = THIS(user_data);

    /* Polling is happening with can_send being TRUE */
    GASSERT(self->io.can_send);
    GASSERT(self->poll_id);
    self->poll_id = 0;
    GDEBUG("< SYMM (poll)");
    nfc_llc_io_initiator_send_symm(self);
    return G_SOURCE_REMOVE;
}

static
void
nfc_llc_io_initiator_symm_transmit_done(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    NfcLlcIoInitiator* self = THIS(user_data);
    NfcLlcIo* io = &self->io;

    GASSERT(!io->can_send);
    GASSERT(self->tx_id);
    self->tx_id = 0;
    io->can_send = TRUE; /* Don't issue a signal just yet */
    nfc_llc_io_ref(io);
    if (status == NFC_TRANSMIT_STATUS_OK) {
        GUtilData received;

        received.bytes = data;
        received.size = len;
        if (nfc_llc_io_receive(io, &received)) {
            if (!self->tx_id) {
                /* Something else might be coming, don't wait */
                GDEBUG("< SYMM");
                nfc_llc_io_initiator_send_symm(self);
            }
        } else if (!self->tx_id) {
            /* Nothing is expected to arrive urgently, start polling. */
            self->poll_id = g_timeout_add(self->poll_period,
                nfc_llc_io_initiator_poll, self);
            nfc_llc_io_can_send(io);
        }
    } else {
        nfc_llc_io_error(io);
    }
    nfc_llc_io_unref(io);
}

static
void
nfc_llc_io_initiator_pdu_transmit_done(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    NfcLlcIoInitiator* self = THIS(user_data);
    NfcLlcIo* io = &self->io;

    GASSERT(!io->can_send);
    GASSERT(self->tx_id);
    self->tx_id = 0;
    io->can_send = TRUE; /* Don't issue a signal just yet */
    nfc_llc_io_ref(io);
    if (status == NFC_TRANSMIT_STATUS_OK) {
        GUtilData received;

        received.bytes = data;
        received.size = len;
        nfc_llc_io_receive(io, &received);
        if (!self->tx_id) {
            GDEBUG("< SYMM");
            nfc_llc_io_initiator_send_symm(self);
        }
    } else {
        nfc_llc_io_error(io);
    }
    nfc_llc_io_unref(io);
}

static
gboolean
nfc_llc_io_initiator_send_symm(
    NfcLlcIoInitiator* self)
{
    static const guint8 SYMM[] = { 0x00, 0x00 };
    NfcLlcIo* io = &self->io;

    GASSERT(!self->tx_id);
    io->can_send = FALSE;
    self->tx_id = nfc_target_transmit(self->target, SYMM, sizeof(SYMM), NULL,
        nfc_llc_io_initiator_symm_transmit_done, NULL, self);
    if (self->tx_id) {
        return TRUE;
    } else {
        nfc_llc_io_error(io);
        return FALSE;
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcLlcIo*
nfc_llc_io_initiator_new(
    NfcTarget* target)
{
    if (G_LIKELY(target)) {
        NfcLlcIoInitiator* self = g_object_new(THIS_TYPE, NULL);

        self->target = nfc_target_ref(target);
        self->poll_period = DEFAULT_POLL_PERIOD;
        return &self->io;
    }
    return NULL;
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
gboolean
nfc_llc_io_initiator_start(
    NfcLlcIo* io)
{
    return nfc_llc_io_initiator_send_symm(THIS(io));
}

static
gboolean
nfc_llc_io_initiator_send(
    NfcLlcIo* io,
    GBytes* send)
{
    gsize size;
    const guint8* data = g_bytes_get_data(send, &size);
    NfcLlcIoInitiator* self = THIS(io);

    GASSERT(io->can_send);
    if (self->poll_id) {
        /* Cancel scheduled polling */
        g_source_remove(self->poll_id);
        self->poll_id = 0;
    }

    io->can_send = FALSE;
    self->tx_id = nfc_target_transmit(self->target, data, size, NULL,
        nfc_llc_io_initiator_pdu_transmit_done, NULL, self);
    if (self->tx_id) {
        return TRUE;
    } else {
        nfc_llc_io_error(io);
        return FALSE;
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_llc_io_initiator_init(
    NfcLlcIoInitiator* self)
{
    self->io.can_send = TRUE;
}

static
void
nfc_llc_io_initiator_finalize(
    GObject* object)
{
    NfcLlcIoInitiator* self = THIS(object);

    if (self->poll_id) {
        g_source_remove(self->poll_id);
    }
    nfc_target_cancel_transmit(self->target, self->tx_id);
    nfc_target_unref(self->target);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_llc_io_initiator_class_init(
    NfcLlcIoInitiatorClass* klass)
{
    klass->start = nfc_llc_io_initiator_start;
    klass->send = nfc_llc_io_initiator_send;
    G_OBJECT_CLASS(klass)->finalize = nfc_llc_io_initiator_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
