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

#include "nfc_crc.h"

/*
 * Appends 16 bit CRC in Little Endian byte order. The buffer is supposed
 * to have 2 more bytes available.
 */
void
nfc_crc_append_le16(
    NfcCrc16Func fn,
    guint8* data,
    gsize len)
{
    const guint16 crc = fn(data, len);

    data[len] = (crc & 0xff);
    data[len + 1] = ((crc >> 8) & 0xff);
}

/*
 * Checks 16 bit CRC in Little Endian byte order, stored at the end of
 * the data buffer. The size does not include CRC itself, i.e. the buffer
 * has 2 more bytes after that.
 */
gboolean
nfc_crc_check_le16_tail(
    NfcCrc16Func fn,
    const guint8* data,
    gsize len)
{
    const guint16 crc = ((guint16)data[len] | ((guint16)data[len + 1] << 8));
    const guint16 expected = fn(data, len);

    return (crc == expected);
}

/* Implementation of 16-bit CRC algorithm defined in [ISO/IEC_13239] */
static
guint16
nfc_crc16_iso13239(
    guint16 crc,
    const guint8* data,
    gsize len)
{
    while (len > 0) {
        guint16 b = (*data++) ^ (crc & 0xFF);

        b ^= (b << 4) & 0xff;
        crc = (crc >> 8) ^ (b << 8) ^ (b << 3) ^ (b >> 4);
        len--;
    }
    return crc;
}


/*
 * NFCForum-TS-DigitalProtocol-1.0
 * Section 4.4 "Data and Payload Format"
 *
 * 4.4.1.2 The CRC_A MUST be calculated as defined in [ISO/IEC_13239],
 * but the initial register content MUST be 6363h and the register
 * content MUST not be inverted after calculation. CRC_A1 is the LSB
 * and CRC_A2 is the MSB.
 */
guint16
nfc_crc_a(
    const guint8* data,
    gsize len)
{
    return nfc_crc16_iso13239(0x6363, data, len);
}

void
nfc_crc_a_append(
    guint8* data,
    gsize len)
{
    nfc_crc_append_le16(nfc_crc_a, data, len);
}

gboolean
nfc_crc_a_check_tail(
    const guint8* data,
    gsize len)
{
    return nfc_crc_check_le16_tail(nfc_crc_a, data, len);
}

/*
 * NFCForum-TS-DigitalProtocol-1.0
 * Section 5.4 "Data and Payload Format"
 *
 * 5.4.1.3 The CRC_B MUST be calculated as defined in [ISO/IEC_13239].
 * The initial register content MUST be all ones (FFFFh). CRC_B1 is
 * the LSB and CRC_B2 is the MSB.
 */
guint16
nfc_crc_b(
    const guint8* data,
    gsize len)
{
    return nfc_crc16_iso13239(0xffff, data, len);
}

void
nfc_crc_b_append(
    guint8* data,
    gsize len)
{
    nfc_crc_append_le16(nfc_crc_b, data, len);
}

gboolean
nfc_crc_b_check_tail(
    const guint8* data,
    gsize len)
{
    return nfc_crc_check_le16_tail(nfc_crc_b, data, len);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
