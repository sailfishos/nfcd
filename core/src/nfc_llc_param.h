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

#ifndef NFC_LLC_PARAM_H
#define NFC_LLC_PARAM_H

#include "nfc_types_p.h"

#define NFC_LLC_MIU_MIN     (128)
#define NFC_LLC_MIU_MAX     (0x7ff + NFC_LLC_MIU_MIN)
#define NFC_LLC_MIU_DEFAULT NFC_LLC_MIU_MIN
#define NFC_LLC_LTO_DEFAULT (100) /* milliseconds */
#define NFC_LLC_RW_DEFAULT  (1)
#define NFC_LLC_RW_MAX      (0xf)

typedef enum nfc_llc_param_type {
    NFC_LLC_PARAM_VERSION = 1,
    NFC_LLC_PARAM_MIUX = 2,
    NFC_LLC_PARAM_WKS = 3,
    NFC_LLC_PARAM_LTO = 4,
    NFC_LLC_PARAM_RW = 5,
    NFC_LLC_PARAM_SN = 6,
    NFC_LLC_PARAM_OPT = 7,
    NFC_LLC_PARAM_SDREQ = 8,    /* LLCP 1.1 */
    NFC_LLC_PARAM_SDRES = 9     /* LLCP 1.1 */
} NFC_LLC_PARAM_TYPE;

typedef enum nfc_llc_opt {
    NFC_LLC_OPT_NONE = 0x00,
    NFC_LLC_OPT_CL = 0x01,      /* Connectionless link service */
    NFC_LLC_OPT_CO = 0x02       /* Connection-oriented link service */
} NFC_LLC_OPT;

typedef struct nfc_llc_param_sdreq {
    guint8 tid;
    const char* uri;
} NfcLlcParamSdReq;

typedef struct nfc_llc_param_sdres {
    guint8 tid;
    guint8 sap;
} NfcLlcParamSdRes;

typedef union nfc_llc_param_value {
    guint8 version;
    guint miu; /* MIUX + NFC_LLC_MIU_MIN */
    guint wks;
    guint lto; /* milliseconds */
    guint8 rw;
    NFC_LLC_OPT opt;
    const char* sn;
    NfcLlcParamSdReq sdreq;
    NfcLlcParamSdRes sdres;
} NfcLlcParamValue;

struct nfc_llc_param {
    NFC_LLC_PARAM_TYPE type;
    NfcLlcParamValue value;
};

GByteArray*
nfc_llc_param_encode(
    const NfcLlcParam* const* params,
    GByteArray* dest,
    guint maxlen)
    NFCD_INTERNAL;

NfcLlcParam**
nfc_llc_param_decode(
    const GUtilData* tlvs)
    NFCD_INTERNAL;

NfcLlcParam**
nfc_llc_param_decode_bytes(
    const void* data,
    guint size)
    NFCD_INTERNAL;

guint
nfc_llc_param_count(
    const NfcLlcParam* const* params)
    NFCD_INTERNAL;

const NfcLlcParam*
nfc_llc_param_find(
    const NfcLlcParam* const* params,
    NFC_LLC_PARAM_TYPE type)
    NFCD_INTERNAL;

void
nfc_llc_param_free(
    NfcLlcParam** list)
    NFCD_INTERNAL;

static inline const NfcLlcParam** nfc_llc_param_constify(NfcLlcParam** params)
    { return (const NfcLlcParam**)params; }

#endif /* NFC_LLC_PARAM_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
