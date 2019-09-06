/*
 * Copyright (C) 2019 Jolla Ltd.
 * Copyright (C) 2019 Slava Monich <slava.monich@jolla.com>
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

#ifndef NFC_TAG_T4_H
#define NFC_TAG_T4_H

#include "nfc_tag.h"

/* Type 4 tag */

/* Since 1.0.20 */

typedef struct nfc_tag_t4_priv NfcTagType4Priv;

struct nfc_tag_t4 {
    NfcTag tag;
    NfcTagType4Priv* priv;
};

struct nfc_param_iso_dep_poll_a {
    guint fsc;     /* FSC (FSDI converted to bytes) */
    GUtilData t1;  /* T1 to Tk (aka historical bytes) */
};

GType nfc_tag_t4_get_type();
#define NFC_TYPE_TAG_T4 (nfc_tag_t4_get_type())
#define NFC_IS_TAG_T4(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, NFC_TYPE_TAG_T4)
#define NFC_TAG_T4(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), NFC_TYPE_TAG_T4, \
        NfcTagType4))

GType nfc_tag_t4a_get_type();
#define NFC_TYPE_TAG_T4A (nfc_tag_t4a_get_type())
#define NFC_IS_TAG_T4A(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, NFC_TYPE_TAG_T4A)
#define NFC_TAG_T4A(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), NFC_TYPE_TAG_T4A, \
        NfcTagType4a))

GType nfc_tag_t4b_get_type();
#define NFC_TYPE_TAG_T4B (nfc_tag_t4b_get_type())
#define NFC_IS_TAG_T4B(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, NFC_TYPE_TAG_T4B)
#define NFC_TAG_T4B(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), NFC_TYPE_TAG_T4B, \
        NfcTagType4b))

/* Status bytes defined in ISO/IEC 7816-4 */

#define ISO_SW_OK (0x9000) /* Normal completion */
#define ISO_SW_SUCCESS(sw) (((sw) & 0xff00) == ISO_SW_OK)

/*
 * SW1 value '60', as well as any value different from '9X' and '6X'
 * are invalid according to ISO/IEC 7816-4. We use zero to indicate
 * low level (non-protocol) I/O error.
 */
#define ISO_SW_IO_ERR (0)

typedef
void
(*NfcTagType4ResponseFunc)(
    NfcTagType4* tag,
    guint sw,  /* 16 bits (SW1 << 8)|SW2 */
    const void* data,
    guint len,
    void* user_data);

guint
nfc_isodep_transmit(
    NfcTagType4* tag,
    guint8 cla,             /* Class byte */
    guint8 ins,             /* Instruction byte */
    guint8 p1,              /* Parameter byte 1 */
    guint8 p2,              /* Parameter byte 2 */
    const GUtilData* data,  /* Command data */
    guint le,               /* Expected length, zero if none */
    NfcTargetSequence* seq,
    NfcTagType4ResponseFunc resp,
    GDestroyNotify destroy,
    void* user_data);

#endif /* NFC_TAG_T4_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
