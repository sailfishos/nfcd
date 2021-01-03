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

#ifndef NFC_CRC_H
#define NFC_CRC_H

#include "nfc_types.h"

G_BEGIN_DECLS

typedef
guint16
(*NfcCrc16Func)(
    const guint8* data,
    gsize len);

void
nfc_crc_append_le16(
    NfcCrc16Func fn,
    guint8* data,
    gsize size)
    NFCD_EXPORT;

gboolean
nfc_crc_check_le16_tail(
    NfcCrc16Func fn,
    const guint8* data,
    gsize len)
    NFCD_EXPORT;

/* CRC_A [ISO/IEC_13239] */

guint16
nfc_crc_a(
    const guint8* data,
    gsize len)
    NFCD_EXPORT;

void
nfc_crc_a_append(
    guint8* data,
    gsize size)
    NFCD_EXPORT;

gboolean
nfc_crc_a_check_tail(
    const guint8* data,
    gsize len)
    NFCD_EXPORT;

/* CRC_B [ISO/IEC_13239] */

guint16
nfc_crc_b(
    const guint8* data,
    gsize len)
    NFCD_EXPORT;

void
nfc_crc_b_append(
    guint8* data,
    gsize size)
    NFCD_EXPORT;

gboolean
nfc_crc_b_check_tail(
    const guint8* data,
    gsize len)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_CRC_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
