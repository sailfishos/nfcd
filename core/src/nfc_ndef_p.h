/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018 Bogdan Pankovsky <b.pankovsky@omprussia.ru>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#ifndef NFC_NDEF_PRIVATE_H
#define NFC_NDEF_PRIVATE_H

#include "nfc_types_p.h"

#include <nfc_ndef.h>

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

extern const GUtilData nfc_ndef_rec_type_u NFCD_INTERNAL; /* "U" */
extern const GUtilData nfc_ndef_rec_type_t NFCD_INTERNAL; /* "T" */
extern const GUtilData nfc_ndef_rec_type_sp NFCD_INTERNAL; /* "Sp" */

gboolean
nfc_ndef_type(
    const NfcNdefData* data,
    GUtilData* type)
    NFCD_INTERNAL;

gboolean
nfc_ndef_payload(
    const NfcNdefData* data,
    GUtilData* payload)
    NFCD_INTERNAL;

NfcNdefRec*
nfc_ndef_rec_initialize(
    NfcNdefRec* rec,
    NFC_NDEF_RTD rtd,
    const NfcNdefData* ndef)
    NFCD_INTERNAL;

void
nfc_ndef_rec_clear_flags(
    NfcNdefRec* rec,
    NFC_NDEF_REC_FLAGS flags)
    NFCD_INTERNAL;

NfcNdefRec*
nfc_ndef_rec_new_well_known(
    GType gtype,
    NFC_NDEF_RTD rtd,
    const GUtilData* type,
    const GUtilData* payload)
    NFCD_INTERNAL;

NfcNdefRecU*
nfc_ndef_rec_u_new_from_data(
    const NfcNdefData* ndef)
    NFCD_INTERNAL;

char*
nfc_ndef_rec_u_steal_uri(
    NfcNdefRecU* ndef)
    NFCD_INTERNAL;

NfcNdefRecT*
nfc_ndef_rec_t_new_from_data(
    const NfcNdefData* ndef)
    NFCD_INTERNAL;

char*
nfc_ndef_rec_t_steal_lang(
    NfcNdefRecT* self)
    NFCD_INTERNAL;

char*
nfc_ndef_rec_t_steal_text(
    NfcNdefRecT* self)
    NFCD_INTERNAL;

NfcNdefRecSp*
nfc_ndef_rec_sp_new_from_data(
    const NfcNdefData* ndef)
    NFCD_INTERNAL;

#endif /* NFC_NDEF_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
