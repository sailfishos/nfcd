/*
 * Copyright (C) 2020-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2020 Jolla Ltd.
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

#ifndef NFC_INITIATOR_PRIVATE_H
#define NFC_INITIATOR_PRIVATE_H

#include "nfc_types_p.h"

#include <nfc_initiator.h>

typedef struct nfc_transmission NfcTransmission;

typedef
void
(*NfcInitiatorFunc)(
    NfcInitiator* initiator,
    void* user_data);

typedef
gboolean
(*NfcTransmissionHandlerFunc)(
    NfcInitiator* initiator,
    NfcTransmission* transmission,
    const GUtilData* data,
    void* user_data);

typedef
void
(*NfcTransmissionDoneFunc)(
    NfcTransmission* transmission,
    gboolean ok,
    void* user_data);

void
nfc_initiator_deactivate(
    NfcInitiator* initiator)
    NFCD_INTERNAL;

gulong
nfc_initiator_add_transmission_handler(
    NfcInitiator* initiator,
    NfcTransmissionHandlerFunc func,
    void* user_data)
    NFCD_INTERNAL;

gulong
nfc_initiator_add_gone_handler(
    NfcInitiator* initiator,
    NfcInitiatorFunc func,
    void* user_data)
    NFCD_INTERNAL;

gulong
nfc_initiator_add_reactivated_handler(
    NfcInitiator* initiator,
    NfcInitiatorFunc func,
    void* user_data)
    NFCD_INTERNAL;

void
nfc_initiator_remove_handler(
    NfcInitiator* initiator,
    gulong id)
    NFCD_INTERNAL;

void
nfc_initiator_remove_handlers(
    NfcInitiator* initiator,
    gulong* ids,
    guint count)
    NFCD_INTERNAL;

#define nfc_initiator_remove_all_handlers(initiator,ids) \
    nfc_initiator_remove_handlers(initiator, ids, G_N_ELEMENTS(ids))

/*
 * Incoming transmission API
 */

NfcTransmission*
nfc_transmission_ref(
    NfcTransmission* transmission)
    NFCD_INTERNAL;

void
nfc_transmission_unref(
    NfcTransmission* transmission)
    NFCD_INTERNAL;

gboolean
nfc_transmission_respond(
    NfcTransmission* transmission,
    const void* data,
    guint len,
    NfcTransmissionDoneFunc done,
    void* user_data)
    NFCD_INTERNAL;

gboolean
nfc_transmission_respond_bytes(
    NfcTransmission* transmission,
    GBytes* data,
    NfcTransmissionDoneFunc done,
    void* user_data)
    NFCD_INTERNAL;

#endif /* NFC_INITIATOR_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
