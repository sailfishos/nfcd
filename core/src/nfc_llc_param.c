/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_llc_param.h"

#include <gutil_macros.h>

/*
 * NFCForum-TS-LLCP_1.1
 * Section 4.4 LLC Parameter Format
 */

GByteArray*
nfc_llc_param_encode(
    const NfcLlcParam* const* params,
    GByteArray* dest,
    guint maxlen)
{
    if (params) {
        const NfcLlcParam* const* ptr = params;

        if (!dest) dest = g_byte_array_new();
        while (*ptr) {
            const NfcLlcParam* param = *ptr++;
            const NfcLlcParamValue* value = &param->value;
            const guint len = dest->len;
            guint8* tlv;
            guint l, v;

            switch (param->type) {
            /* 4.5.1 Version Number, VERSION */
            case NFC_LLC_PARAM_VERSION:
                g_byte_array_set_size(dest, len + 3);
                tlv = dest->data + len;
                tlv[0] = (guint8)param->type;
                tlv[1] = 0x01;
                tlv[2] = value->version;
                break;
            /* 4.5.2 Maximum Information Unit Extension, MIUX */
            case NFC_LLC_PARAM_MIUX:
                g_byte_array_set_size(dest, len + 4);
                tlv = dest->data + len;
                v = MAX(value->miu, NFC_LLC_MIU_MIN) - NFC_LLC_MIU_MIN;
                tlv[0] = (guint8)param->type;
                tlv[1] = 0x02;
                tlv[2] = (v >> 8) & 0x07;
                tlv[3] = (guint8)v;
                break;
            /* 4.5.3 Well-Known Service List, WKS */
            case NFC_LLC_PARAM_WKS:
                g_byte_array_set_size(dest, len + 4);
                tlv = dest->data + len;
                tlv[0] = (guint8)param->type;
                tlv[1] = 0x02;
                tlv[2] = (value->wks >> 8) & 0x07;
                tlv[3] = (guint8)value->wks;
                break;
            /* 4.5.4 Link Timeout, LTO */
            case NFC_LLC_PARAM_LTO:
                g_byte_array_set_size(dest, len + 3);
                tlv = dest->data + len;
                v = value->lto / 10;
                tlv[0] = (guint8)param->type;
                tlv[1] = 0x01;
                tlv[2] = (guint8)MIN(v, 0xff);
                break;
            /* 4.5.5 Receive Window Size, RW */
            case NFC_LLC_PARAM_RW:
                g_byte_array_set_size(dest, len + 3);
                tlv = dest->data + len;
                tlv[0] = (guint8)param->type;
                tlv[1] = 0x01;
                tlv[2] = (guint8)MIN(value->rw, 0x0f);
                break;
            /* 4.5.6 Service Name, SN */
            case NFC_LLC_PARAM_SN:
                l = (guint)(value->sn ? strlen(value->sn) : 0);
                l = MIN(l, 0xff);
                g_byte_array_set_size(dest, len + 2 + l);
                tlv = dest->data + len;
                tlv[0] = (guint8)param->type;
                tlv[1] = (guint8)l;
                if (l) memcpy(tlv + 2, value->sn, l);
                break;
            /* 4.5.7 Option, OPT */
            case NFC_LLC_PARAM_OPT:
                g_byte_array_set_size(dest, len + 3);
                tlv = dest->data + len;
                tlv[0] = (guint8)param->type;
                tlv[1] = 0x01;
                tlv[2] = (guint8)value->opt;
                break;
            /* 4.5.8 Service Discovery Request, SDREQ */
            case NFC_LLC_PARAM_SDREQ:
                l = (guint)(value->sdreq.uri ? strlen(value->sdreq.uri) : 0);
                l = MIN(l, 0xfe);
                g_byte_array_set_size(dest, len + 3 + l);
                tlv = dest->data + len;
                tlv[0] = (guint8)param->type;
                tlv[1] = (guint8)(l + 1);
                tlv[2] = value->sdreq.tid;
                if (l) memcpy(tlv + 3, value->sdreq.uri, l);
                break;
            /* 4.5.9 Service Discovery Response, SDRES */
            case NFC_LLC_PARAM_SDRES:
                g_byte_array_set_size(dest, len + 4);
                tlv = dest->data + len;
                tlv[0] = (guint8)param->type;
                tlv[1] = 0x02;
                tlv[2] = value->sdres.tid;
                tlv[3] = value->sdres.sap;
                break;
            }
            if (maxlen && dest->len >= maxlen) {
                if (dest->len > maxlen) {
                    g_byte_array_set_size(dest, len);
                }
                break;
            }
        }
    }
    return dest;
}

NfcLlcParam**
nfc_llc_param_decode(
    const GUtilData* tlvs)
{
    NfcLlcParam** params = NULL;

    if (tlvs) {
        const guint8* ptr = tlvs->bytes;
        const guint8* end = ptr + tlvs->size;
        GPtrArray* list = g_ptr_array_new();

        while (ptr + 1 < end && ptr + (ptr[1] + 1) < end) {
            const guint t = ptr[0];
            const guint l = ptr[1];
            const guint8* v = ptr + 2;
            NfcLlcParam* param = NULL;
            char* buf;

            switch (t) {
            /* 4.5.1 Version Number, VERSION */
            case NFC_LLC_PARAM_VERSION:
                if (l == 1) {
                    param = g_new0(NfcLlcParam, 1);
                    param->value.version = v[0];
                }
                break;
            /* 4.5.2 Maximum Information Unit Extension, MIUX */
            case NFC_LLC_PARAM_MIUX:
                if (l == 2) {
                    const guint miux = (((((guint)v[0]) << 8) | v[1]) & 0x7ff);

                    param = g_new0(NfcLlcParam, 1);
                    param->value.miu = miux + NFC_LLC_MIU_MIN;
                }
                break;
            /* 4.5.3 Well-Known Service List, WKS */
            case NFC_LLC_PARAM_WKS:
                if (l == 2) {
                    param = g_new0(NfcLlcParam, 1);
                    param->value.wks = ((((guint)v[0]) << 8) | v[1]);
                }
                break;
            /* 4.5.4 Link Timeout, LTO */
            case NFC_LLC_PARAM_LTO:
                /*
                 * The LTO parameter value SHALL be an 8-bit unsigned
                 * integer that specifies the link timeout value in
                 * multiples of 10 milliseconds.
                 *
                 * If no LTO parameter is transmitted or if the LTO
                 * parameter value is zero, the default link timeout
                 * value of 100 milliseconds SHALL be used.
                 */
                if (l == 1) {
                    param = g_new0(NfcLlcParam, 1);
                    param->value.lto = v[0] ? (10 * (guint)v[0]) :
                        NFC_LLC_LTO_DEFAULT;
                }
                break;
            /* 4.5.5 Receive Window Size, RW */
            case NFC_LLC_PARAM_RW:
                if (l == 1) {
                    param = g_new0(NfcLlcParam, 1);
                    param->value.rw = (v[0] & 0x0f);
                }
                break;
            /* 4.5.6 Service Name, SN */
            case NFC_LLC_PARAM_SN:
                param = g_malloc0(G_ALIGN8(sizeof(NfcLlcParam)) + l + 1);
                buf = ((char*)param) + G_ALIGN8(sizeof(NfcLlcParam));
                param->value.sn = buf;
                memcpy(buf, v, l);
                break;
            /* 4.5.7 Option, OPT */
            case NFC_LLC_PARAM_OPT:
                if (l == 1) {
                    param = g_new0(NfcLlcParam, 1);
                    param->value.opt = v[0];
                }
                break;
            /* 4.5.8 Service Discovery Request, SDREQ */
            case NFC_LLC_PARAM_SDREQ:
                if (l >= 1) {
                    param = g_malloc0(G_ALIGN8(sizeof(NfcLlcParam)) + l);
                    buf = ((char*)param) + G_ALIGN8(sizeof(NfcLlcParam));
                    param->value.sdreq.tid = v[0];
                    param->value.sdreq.uri = buf;
                    memcpy(buf, v + 1, l - 1);
                }
                break;
            /* 4.5.9 Service Discovery Response, SDRES */
            case NFC_LLC_PARAM_SDRES:
                if (l == 2) {
                    param = g_new0(NfcLlcParam, 1);
                    param->value.sdres.tid = v[0];
                    param->value.sdres.sap = (v[1] & 0x3f);
                }
                break;
            }
            if (param) {
                param->type = t;
                g_ptr_array_add(list, param);
            }
            /* Advance to the next block */
            ptr += l + 2;
        }
        g_ptr_array_add(list, NULL);
        params = (NfcLlcParam**)g_ptr_array_free(list, FALSE);
    }
    return params;
}

NfcLlcParam**
nfc_llc_param_decode_bytes(
    const void* data,
    guint size)
{
    if (data) {
        GUtilData tlvs;

        tlvs.bytes = data;
        tlvs.size = size;
        return nfc_llc_param_decode(&tlvs);
    } else {
        return NULL;
    }
}

guint
nfc_llc_param_count(
    const NfcLlcParam* const* params)
{
    guint count = 0;

    if (params) {
        const NfcLlcParam* const* ptr = params;

        while (*ptr) {
            ptr++;
            count++;
        }
    }
    return count;
}

const NfcLlcParam*
nfc_llc_param_find(
    const NfcLlcParam* const* params,
    NFC_LLC_PARAM_TYPE type)
{
    if (params) {
        const NfcLlcParam* const* ptr = params;

        while (*ptr) {
            const NfcLlcParam* param = *ptr++;

            if (param->type == type) {
                return param;
            }
        }
    }
    return NULL;
}

void
nfc_llc_param_free(
    NfcLlcParam** params)
{
    if (params) {
        NfcLlcParam** ptr = params;

        while (*ptr) {
            g_free(*ptr++);
        }
        g_free(params);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
