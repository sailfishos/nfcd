/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2021 Jolla Ltd.
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

/* ISO/IEC 7816-4 */

#define ISO_MF (0x3F00)

#define ISO_CLA (0x00) /* Basic channel */

#define ISO_SHORT_FID_MASK (0x1f) /* Short File ID mask */

/* Instruction byte */
#define ISO_INS_SELECT (0xA4)
#define ISO_INS_READ_BINARY (0xB0)

/* Selection by file identifier */
#define ISO_P1_SELECT_BY_ID (0x00)      /* Select MF, DF or EF */
#define ISO_P1_SELECT_CHILD_DF (0x01)   /* Select child DF */
#define ISO_P1_SELECT_CHILD_EF (0x02)   /* Select EF under current DF */
#define ISO_P1_SELECT_PARENT_DF (0x03)  /* Select parent DF of current DF */
/* Selection by DF name */
#define ISO_P1_SELECT_DF_BY_NAME (0x04) /* Select by DF name */
/* Selection by path */
#define ISO_P1_SELECT_ABS_PATH (0x08)   /* Select from the MF */
#define ISO_P1_SELECT_REL_PATH (0x09)   /* Select from the current DF */

/* File occurrence */
#define ISO_P2_SELECT_FILE_FIRST (0x00) /* First or only occurrence */
#define ISO_P2_SELECT_FILE_LAST (0x01)  /* Last occurrence */
#define ISO_P2_SELECT_FILE_NEXT (0x02)  /* Next occurrence */
#define ISO_P2_SELECT_FILE_PREV (0x03)  /* Previous occurrence */
#define ISO_P2_SELECT_FILE_MASK (0x03)  /* Mask of the above */
/* File control information */
#define ISO_P2_RESPONSE_FCI (0x00)      /* Return FCI template */
#define ISO_P2_RESPONSE_FCP (0x04)      /* Return FCP template */
#define ISO_P2_RESPONSE_FMD (0x08)      /* Return FMD template */
#define ISO_P2_RESPONSE_NONE (0x0C)     /* No response data */
#define ISO_P2_RESPONSE_MASK (0x0C)     /* Mask of the above */

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
