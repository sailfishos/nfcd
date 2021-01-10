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

#define GLOG_MODULE_NAME NFC_LLC_LOG_MODULE
#include <gutil_log.h>
#include <gutil_misc.h>

#define THIS(obj) NFC_LLC_IO(obj)
G_DEFINE_ABSTRACT_TYPE(NfcLlcIo, nfc_llc_io, G_TYPE_OBJECT)
#define NFC_LLC_IO_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), \
        NFC_TYPE_LLC_IO, NfcLlcIoClass)

enum nfc_llc_io_signal {
    SIGNAL_CAN_SEND,
    SIGNAL_RECEIVE,
    SIGNAL_ERROR,
    SIGNAL_COUNT
};

#define SIGNAL_ERROR_NAME       "nfc-llc-io-error"
#define SIGNAL_CAN_SEND_NAME    "nfc-llc-io-can-send"
#define SIGNAL_RECEIVE_NAME     "nfc-llc-io-receive"

static guint nfc_llc_io_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

void
nfc_llc_io_error(
    NfcLlcIo* self)
{
    if (!self->error) {
        self->error = TRUE;
        self->can_send = FALSE;
        g_signal_emit(self, nfc_llc_io_signals[SIGNAL_ERROR], 0);
    }
}

void
nfc_llc_io_can_send(
    NfcLlcIo* self)
{
    if (!self->can_send && !self->error) {
        self->can_send = TRUE;
        g_signal_emit(self, nfc_llc_io_signals[SIGNAL_CAN_SEND], 0);
    }
}

gboolean
nfc_llc_io_receive(
    NfcLlcIo* self,
    const GUtilData* data)
{
    gboolean ret = FALSE;

    g_signal_emit(self, nfc_llc_io_signals[SIGNAL_RECEIVE], 0, data, &ret);
    return ret;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcLlcIo*
nfc_llc_io_ref(
    NfcLlcIo* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_llc_io_unref(
    NfcLlcIo* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

gboolean
nfc_llc_io_start(
    NfcLlcIo* self)
{
    return G_LIKELY(self) && NFC_LLC_IO_GET_CLASS(self)->start(self);
}

gboolean
nfc_llc_io_send(
    NfcLlcIo* self,
    GBytes* data)
{
    if (G_LIKELY(self)) {
        GASSERT(self->can_send);
        if (self->can_send) {
            return NFC_LLC_IO_GET_CLASS(self)->send(self, data);
        }
    }
    return FALSE;
}

gulong
nfc_llc_io_add_can_send_handler(
    NfcLlcIo* self,
    NfcLlcIoFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_CAN_SEND_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_llc_io_add_receive_handler(
    NfcLlcIo* self,
    NfcLlcIoReceiveFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_RECEIVE_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_llc_io_add_error_handler(
    NfcLlcIo* self,
    NfcLlcIoFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ERROR_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_llc_io_remove_handlers(
    NfcLlcIo* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_llc_io_init(
    NfcLlcIo* self)
{
}

static
void
nfc_llc_io_class_init(
    NfcLlcIoClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    nfc_llc_io_signals[SIGNAL_CAN_SEND] =
        g_signal_new(SIGNAL_CAN_SEND_NAME, type, G_SIGNAL_RUN_FIRST, 0,
            NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_llc_io_signals[SIGNAL_RECEIVE] =
        g_signal_new(SIGNAL_RECEIVE_NAME, type, G_SIGNAL_RUN_LAST, 0,
            g_signal_accumulator_true_handled, NULL, NULL,
            G_TYPE_BOOLEAN, 1, G_TYPE_POINTER);
    nfc_llc_io_signals[SIGNAL_ERROR] =
        g_signal_new(SIGNAL_ERROR_NAME, type, G_SIGNAL_RUN_FIRST, 0,
            NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
