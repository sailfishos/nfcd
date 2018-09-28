/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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

#ifndef NFC_NDEF_PRIVATE_H
#define NFC_NDEF_PRIVATE_H

#include "nfc_types_p.h"

#include <nfc_ndef.h>

/* Add _ prefix so that they don't get exported */
#define nfc_ndef_payload _nfc_ndef_payload
#define nfc_ndef_rec_initialize _nfc_ndef_rec_initialize
#define nfc_ndef_rec_u_new_from_data _nfc_ndef_rec_u_new_from_data
#define nfc_ndef_rec_new_well_known _nfc_ndef_rec_new_well_known
#define nfc_ndef_rec_type_u _nfc_ndef_rec_type_u

typedef struct nfc_ndef_rec_class {
    GObjectClass parent;
} NfcNdefRecClass;

/* Pre-parsed NDEF record */
typedef struct nfc_ndef_data {
    GUtilData rec;
    guint type_offset;
    guint type_length;
    guint id_length;
    guint payload_length;
} NfcNdefData;

#define NFC_NDEF_HDR_MB       (0x80)
#define NFC_NDEF_HDR_ME       (0x40)
#define NFC_NDEF_HDR_CF       (0x20)
#define NFC_NDEF_HDR_SR       (0x10)
#define NFC_NDEF_HDR_IL       (0x08)
#define NFC_NDEF_HDR_TNF_MASK (0x07)

extern const GUtilData nfc_ndef_rec_type_u; /* "U" */

gboolean
nfc_ndef_type(
    const NfcNdefData* data,
    GUtilData* type);

gboolean
nfc_ndef_payload(
    const NfcNdefData* data,
    GUtilData* payload);

NfcNdefRec*
nfc_ndef_rec_initialize(
    NfcNdefRec* rec,
    NFC_NDEF_RTD rtd,
    const NfcNdefData* ndef);

NfcNdefRec*
nfc_ndef_rec_new_well_known(
    GType gtype,
    NFC_NDEF_RTD rtd,
    const GUtilData* type,
    const GUtilData* payload);

NfcNdefRecU*
nfc_ndef_rec_u_new_from_data(
    const NfcNdefData* ndef);

#endif /* NFC_NDEF_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
