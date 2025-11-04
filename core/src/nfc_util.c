/*
 * Copyright (C) 2018-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2020 Jolla Ltd.
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

#include "nfc_util.h"
#include "nfc_system.h"
#include "nfc_log.h"

#include <gutil_misc.h>
#include <gutil_macros.h>

/* sub-module, to turn prefix off */
GLogModule nfc_dump_log = {
    .name = "nfc.dump",
    .parent = &GLOG_MODULE_NAME,
    .max_level = GLOG_LEVEL_MAX,
    .level = GLOG_LEVEL_INHERIT,
    .flags = GLOG_FLAG_HIDE_NAME
};

void
nfc_hexdump(
    const void* data,
    int len)
{
    const int level = GLOG_LEVEL_VERBOSE;
    GLogModule* log = &nfc_dump_log;

    if (gutil_log_enabled(log, level)) {
        const guint8* ptr = data;
        guint off = 0;

        while (len > 0) {
            char buf[GUTIL_HEXDUMP_BUFSIZE];
            const guint consumed = gutil_hexdump(buf, ptr + off, len);

            gutil_log(log, level, "  %04X: %s", off, buf);
            len -= consumed;
            off += consumed;
        }
    }
}

void
nfc_hexdump_data(
    const GUtilData* data)
{
    if (G_LIKELY(data)) {
        nfc_hexdump(data->bytes, data->size);
    }
}

/*
 * Command APDU encoding options (ISO/IEC 7816-4):
 *
 * Case 1:  |CLA|INS|P1|P2|                                n = 4
 * Case 2s: |CLA|INS|P1|P2|LE|                             n = 5
 * Case 3s: |CLA|INS|P1|P2|LC|...BODY...|                  n = 6..260
 * Case 4s: |CLA|INS|P1|P2|LC|...BODY...|LE|               n = 7..261
 * Case 2e: |CLA|INS|P1|P2|00|LE1|LE2|                     n = 7
 * Case 3e: |CLA|INS|P1|P2|00|LC1|LC2|...BODY...|          n = 8..65542
 * Case 4e: |CLA|INS|P1|P2|00|LC1|LC2|...BODY...|LE1|LE2|  n = 10..65544
 *
 * LE, LE1, LE2 may be 0x00, 0x00|0x00 (means the maximum, 256 or 65536)
 * LC must not be 0x00 and LC1|LC2 must not be 0x00|0x00
 */

gboolean
nfc_apdu_encode(
    GByteArray* buf,
    const NfcApdu* apdu)
{
    const GUtilData* data = &apdu->data;

    if (data->size <= 0xffff && apdu->le <= 0x10000) {
        g_byte_array_set_size(buf, 4);
        buf->data[0] = apdu->cla;
        buf->data[1] = apdu->ins;
        buf->data[2] = apdu->p1;
        buf->data[3] = apdu->p2;
        if (data->size > 0) {
            if (data->size <= 0xff) {
                /* Cases 3s and 4s */
                guint8 lc = (guint8) data->size;

                g_byte_array_append(buf, &lc, 1);
            } else {
                /* Cases 3e and 4e */
                guint8 lc[3];

                lc[0] = 0;
                lc[1] = (guint8) (data->size >> 8);
                lc[2] = (guint8) data->size;
                g_byte_array_append(buf, lc, sizeof(lc));
            }
            g_byte_array_append(buf, data->bytes, (guint) data->size);
        }
        if (apdu->le > 0) {
            if (apdu->le <= 0x100 && data->size <= 0xff) {
                /* Cases 2s and 4s */
                guint8 le = (apdu->le == 0x100) ? 0 : ((guint8) apdu->le);

                g_byte_array_append(buf, &le, 1);
            } else {
                /* Cases 4e and 2e */
                guint8 le[2];

                if (apdu->le == 0x10000) {
                    le[0] = le[1] = 0;
                } else {
                    le[0] = (guint8) (apdu->le >> 8);
                    le[1] = (guint8) apdu->le;
                }
                if (!data->size) {
                    /* Case 2e */
                    g_byte_array_set_size(buf, 5);
                    buf->data[4] = 0;
                }
                g_byte_array_append(buf, le, sizeof(le));
            }
        }
        return TRUE;
    } else {
        g_byte_array_set_size(buf, 0);
        return FALSE;
    }
}

gboolean
nfc_apdu_decode(
    NfcApdu* apdu,
    const GUtilData* data)
{
    if (data->size < 4) {
        /* Not enough data */
        return FALSE;
    } else if (data->size == 4) {
        /* Case 1:  |CLA|INS|P1|P2| */
        apdu->cla = data->bytes[0];
        apdu->ins = data->bytes[1];
        apdu->p1 = data->bytes[2];
        apdu->p2 = data->bytes[3];
        memset(&apdu->data, 0, sizeof(apdu->data));
        apdu->le = 0;
        return TRUE;
    } else if (data->size == 5) {
        const guint le = data->bytes[4];

        /* Case 2s: |CLA|INS|P1|P2|LE| */
        apdu->cla = data->bytes[0];
        apdu->ins = data->bytes[1];
        apdu->p1 = data->bytes[2];
        apdu->p2 = data->bytes[3];
        apdu->le = le ? le : 0x100;
        memset(&apdu->data, 0, sizeof(apdu->data));
        return TRUE;
    } else if (data->bytes[4] == 0) {
        if (data->size == 7) {
            const guint le1 = data->bytes[5];
            const guint le2 = data->bytes[6];

            /* Case 2e: |CLA|INS|P1|P2|00|LE1|LE2| */
            apdu->cla = data->bytes[0];
            apdu->ins = data->bytes[1];
            apdu->p1 = data->bytes[2];
            apdu->p2 = data->bytes[3];
            apdu->le = (le1 || le2) ? ((le1 << 8) | le2) : 0x10000;
            memset(&apdu->data, 0, sizeof(apdu->data));
            return TRUE;
        } else if (data->size <= 65544) {
            const guint lc = ((guint) data->bytes[5] << 8) | data->bytes[6];

            if (data->size == lc + 7) {
                /* Case 3e: |CLA|INS|P1|P2|00|LC1|LC2|...BODY...| */
                apdu->le = 0;
            } else if (data->size == lc + 9) {
                const guint le1 = data->bytes[data->size - 2];
                const guint le2 = data->bytes[data->size - 1];

               /* Case 4e: |CLA|INS|P1|P2|00|LC1|LC2|...BODY...|LE1|LE2| */
                apdu->le = (le1 || le2) ? ((le1 << 8) | le2) : 0x10000;
            } else {
                /* Broken APDU */
                return FALSE;
            }

            apdu->cla = data->bytes[0];
            apdu->ins = data->bytes[1];
            apdu->p1 = data->bytes[2];
            apdu->p2 = data->bytes[3];
            apdu->data.bytes = data->bytes + 7;
            apdu->data.size = lc;
            return TRUE;
        } else {
            /* Too much data */
            return FALSE;
        }
    } else if (data->size <= 261) {
        const guint lc = data->bytes[4];

        if (data->size == lc + 5) {
            /* Case 3s: |CLA|INS|P1|P2|LC|...BODY...| */
            apdu->le = 0;
        } else if (data->size == lc + 6) {
            const guint le = data->bytes[data->size - 1];

            /* Case 4s: |CLA|INS|P1|P2|LC|...BODY...|LE| */
            apdu->le = le ? le : 0x100;
        } else {
            /* Broken APDU */
            return FALSE;
        }

        apdu->cla = data->bytes[0];
        apdu->ins = data->bytes[1];
        apdu->p1 = data->bytes[2];
        apdu->p2 = data->bytes[3];
        apdu->data.bytes = data->bytes + 5;
        apdu->data.size = lc;
        return TRUE;
    } else {
        /* Broken APDU */
        return FALSE;
    }
}

GBytes*
nfc_apdu_response_new(
    guint sw, /* 16 bits (SW1 << 8)|SW2 */
    const GUtilData* data)
{
    guchar* buf;
    guchar* ptr;

    if (data && data->size) {
        buf = g_malloc(data->size + 2);
        memcpy(buf, data->bytes, data->size);
        ptr = buf + data->size;
    } else {
        ptr = buf = g_malloc(2);
    }

    *ptr++ = (guchar)(sw >> 8); /* SW1 */
    *ptr++ = (guchar)sw;        /* SW2 */
    return g_bytes_new_take(buf, ptr - buf);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
