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

#include "nfc_llc_io_impl.h"
#include "nfc_initiator_p.h"

#define GLOG_MODULE_NAME NFC_LLC_LOG_MODULE
#include <gutil_log.h>

typedef struct nfc_llc_io_target {
    NfcLlcIo io;
    NfcInitiator* initiator;
    NfcTransmission* transmission;
    gulong tx_handler_id;
} NfcLlcIoTarget;

#define THIS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        THIS_TYPE, NfcLlcIoTarget))
#define THIS_TYPE (nfc_llc_io_target_get_type())
#define PARENT_TYPE NFC_TYPE_LLC_IO
#define PARENT_CLASS (nfc_llc_io_target_parent_class)

GType nfc_llc_io_target_get_type(void) NFCD_INTERNAL;
typedef NfcLlcIoClass NfcLlcIoTargetClass;
G_DEFINE_TYPE(NfcLlcIoTarget, nfc_llc_io_target, PARENT_TYPE)

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
nfc_llc_io_target_response_sent(
    NfcTransmission* transmission,
    gboolean ok,
    void* user_data)
{
    NfcLlcIoTarget* self = THIS(user_data);
    NfcLlcIo* io = &self->io;

    GASSERT(transmission == self->transmission);
    nfc_transmission_unref(self->transmission);
    self->transmission = NULL;
    if (!ok) {
        nfc_llc_io_error(io);
    }
}

static
gboolean
nfc_llc_io_target_transmission_handler(
    NfcInitiator* initiator,
    NfcTransmission* transmission,
    const GUtilData* data,
    void* user_data)
{
    NfcLlcIoTarget* self = THIS(user_data);

    GASSERT(!self->transmission);
    if (!self->transmission) {
        NfcLlcIo* io = &self->io;

        self->transmission = nfc_transmission_ref(transmission);
        if (data) {
            io->can_send = TRUE;
            nfc_llc_io_receive(io, data);
        } else {
            nfc_llc_io_can_send(io);
        }
        /* nfc_llc_io_target_send() sets can_send to FALSE */
        if (self->transmission && io->can_send) {
            static const guint8 SYMM[] = { 0x00, 0x00 };

            /* LLC isn't sending anything, respond with a SYMM */
            GDEBUG("< SYMM");
            io->can_send = FALSE;
            if (nfc_transmission_respond(transmission, SYMM, sizeof(SYMM),
                nfc_llc_io_target_response_sent, self)) {
            } else {
                nfc_llc_io_error(io);
            }
        }
        return TRUE;
    }
    return FALSE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcLlcIo*
nfc_llc_io_target_new(
    NfcInitiator* initiator)
{
    if (G_LIKELY(initiator)) {
        NfcLlcIoTarget* self = g_object_new(THIS_TYPE, NULL);

        self->initiator = nfc_initiator_ref(initiator);
        self->tx_handler_id = nfc_initiator_add_transmission_handler(initiator,
            nfc_llc_io_target_transmission_handler, self);
        return &self->io;
    }
    return NULL;
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
gboolean
nfc_llc_io_target_start(
    NfcLlcIo* io)
{
    return TRUE;
}

static
gboolean
nfc_llc_io_target_send(
    NfcLlcIo* io,
    GBytes* send)
{
    NfcLlcIoTarget* self = THIS(io);
    gsize size;
    gconstpointer data = g_bytes_get_data(send, &size);

    io->can_send = FALSE;
    if (nfc_transmission_respond(self->transmission, data, (guint)size,
        nfc_llc_io_target_response_sent, self)) {
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
nfc_llc_io_target_init(
    NfcLlcIoTarget* self)
{
}

static
void
nfc_llc_io_target_finalize(
    GObject* object)
{
    NfcLlcIoTarget* self = THIS(object);

    nfc_transmission_unref(self->transmission);
    nfc_initiator_remove_handler(self->initiator, self->tx_handler_id);
    nfc_initiator_unref(self->initiator);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_llc_io_target_class_init(
    NfcLlcIoTargetClass* klass)
{
    klass->start = nfc_llc_io_target_start;
    klass->send = nfc_llc_io_target_send;
    G_OBJECT_CLASS(klass)->finalize = nfc_llc_io_target_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
