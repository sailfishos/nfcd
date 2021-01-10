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

#ifndef NFC_LLC_IO_H
#define NFC_LLC_IO_H

#include "nfc_types_p.h"

#include <glib-object.h>

/*
 * LLC I/O API
 *
 * I/O modules reponsible for the symmetry procedure, i.e. sending
 * SYMM packets in the Initiator mode to request data from the peer.
 * It also does the polling when necessary.
 *
 * Basically, it hides the difference between Target and Initiator roles.
 *
 * If can_transmit is FALSE, the client needs to ways 
 * to be invoked. can_transmit field is updated before invoking the callback.
 */

struct nfc_llc_io {
    GObject object;
    gboolean error;
    gboolean can_send;
};

GType nfc_llc_io_get_type(void) NFCD_INTERNAL;
#define NFC_TYPE_LLC_IO (nfc_llc_io_get_type())
#define NFC_LLC_IO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_LLC_IO, NfcLlcIo))

#define LLC_IO_EXPECT_MORE (TRUE)
#define LLC_IO_IGNORE (FALSE)

typedef
gboolean
(*NfcLlcIoReceiveFunc)(
    NfcLlcIo* io,
    const GUtilData* data,
    gpointer user_data);

typedef
void
(*NfcLlcIoFunc)(
    NfcLlcIo* io,
    gpointer user_data);

NfcLlcIo*
nfc_llc_io_ref(
    NfcLlcIo* io)
    NFCD_INTERNAL;

void
nfc_llc_io_unref(
    NfcLlcIo* io)
    NFCD_INTERNAL;

gboolean
nfc_llc_io_start(
    NfcLlcIo* io)
    NFCD_INTERNAL;

gboolean
nfc_llc_io_send(
    NfcLlcIo* io,
    GBytes* data)
    NFCD_INTERNAL;

gulong
nfc_llc_io_add_can_send_handler(
    NfcLlcIo* io,
    NfcLlcIoFunc func,
    void* user_data)
    NFCD_INTERNAL;

gulong
nfc_llc_io_add_receive_handler(
    NfcLlcIo* io,
    NfcLlcIoReceiveFunc func,
    void* user_data)
    NFCD_INTERNAL;

gulong
nfc_llc_io_add_error_handler(
    NfcLlcIo* io,
    NfcLlcIoFunc func,
    void* user_data)
    NFCD_INTERNAL;

void
nfc_llc_io_remove_handlers(
    NfcLlcIo* io,
    gulong* ids,
    guint count)
    NFCD_INTERNAL;

#define nfc_llc_io_remove_all_handlers(io,ids) \
    nfc_llc_io_remove_handlers(io, ids, G_N_ELEMENTS(ids))

/* Initiator-side I/O */

NfcLlcIo*
nfc_llc_io_initiator_new(
    NfcTarget* target)
    NFCD_INTERNAL;

/* Target-side I/O */

NfcLlcIo*
nfc_llc_io_target_new(
    NfcInitiator* initiator)
    NFCD_INTERNAL;

#endif /* NFC_LLC_IO_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
