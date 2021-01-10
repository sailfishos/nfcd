/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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

#ifndef NFC_TYPES_PRIVATE_H
#define NFC_TYPES_PRIVATE_H

/* Need to define NFCD_EXPORT before including <nfc_types.h> */
#define NFCD_EXPORT __attribute__((visibility ("default")))

/* Now pull in the public types */
#include <nfc_types.h>

/* Types */
typedef struct nfc_llc NfcLlc;
typedef struct nfc_llc_io NfcLlcIo;
typedef struct nfc_llc_param NfcLlcParam;
typedef struct nfc_peer_services NfcPeerServices;

/*
 * SAP:
 *
 * 00h..0Fh  Well-Known Service access points
 * 10h..1Fh  Named services advertised by SDP
 * 20h..3Fh  Unnamed services that are NOT advertised by SDP
 */

#define NFC_LLC_SAP_MASK (0x3f)     /* 6 bit */
#define NFC_LLC_SAP_COUNT (NFC_LLC_SAP_MASK + 1)
#define NFC_LLC_SAP_WKS_MASK (0x0f) /* Well-Known Services */
#define NFC_LLC_SAP_NAMED (0x10)    /* First named service */
#define NFC_LLC_SAP_UNNAMED (0x20)  /* First unnamed service */
#define NFC_LLC_SAP_MAX NFC_LLC_SAP_MASK /* Maximum SAP value */

/* Macros */
#define NFCD_INTERNAL G_GNUC_INTERNAL

/* Internal log module */
extern GLogModule nfc_dump_log NFCD_INTERNAL;

#endif /* NFC_TYPES_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
