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

#include "nfc_tag_t4_p.h"
#include "nfc_tag_p.h"
#include "nfc_target_p.h"
#include "nfc_log.h"

typedef struct nfc_isodep_tx {
    NfcTagType4* t4;
    NfcTagType4ResponseFunc resp;
    GDestroyNotify destroy;
    void* user_data;
} NfcIsoDepTx;

struct nfc_tag_t4_priv {
    guint mtu;  /* FSC (Type 4A) or FSD (Type 4B) */
    GByteArray* buf;
    guint init_id;
};

G_DEFINE_ABSTRACT_TYPE(NfcTagType4, nfc_tag_t4, NFC_TYPE_TAG)

/* ISO/IEC 7816-4 */

#define ISO_MF (0x3F00)

#define ISO_CLA (0x00) /* Basic channel */

#define ISO_SHORT_FID_MASK (0x1f) /* Short File ID mask */

#define ISO_INS_SELECT (0xA4)

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
/* File control information */
#define ISO_P2_RESPONSE_FCI (0x00)      /* Return FCI template */
#define ISO_P2_RESPONSE_FCP (0x04)      /* Return FCP template */
#define ISO_P2_RESPONSE_FMD (0x08)      /* Return FMD template */
#define ISO_P2_RESPONSE_NONE (0x0C)     /* No response data */

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
gboolean
nfc_tag_t4_build_apdu(
    GByteArray* buf,
    guint8 cla,          /* Class byte */
    guint8 ins,          /* Instruction byte */
    guint8 p1,           /* Parameter byte 1 */
    guint8 p2,           /* Parameter byte 2 */
    guint len,           /* Command data length */
    const void* data,    /* Command data */
    guint exp)           /* Expected length */
{
    /*
     * Command APDU encoding options (ISO/IEC 7816-4):
     *
     * Case 1:  |CLA|INS|P1|P2|                                 n = 4
     * Case 2s: |CLA|INS|P1|P2|LE |                             n = 5
     * Case 3s: |CLA|INS|P1|P2|LC |...BODY...|                  n = 6..260
     * Case 4s: |CLA|INS|P1|P2|LC |...BODY...|LE |              n = 7..261
     * Case 2e: |CLA|INS|P1|P2|00 |LE1|LE2|                     n = 7
     * Case 3e: |CLA|INS|P1|P2|00 |LC1|LC2|...BODY...|          n = 8..65542
     * Case 4e: |CLA|INS|P1|P2|00 |LC1|LC2|...BODY...|LE1|LE2|  n = 10..65544
     *
     * LE, LE1, LE2 may be 0x00, 0x00|0x00 (means the maximum, 256 or 65536)
     * LC must not be 0x00 and LC1|LC2 must not be 0x00|0x00
     */
    if (len <= 0xffff && exp <= 0x10000) {
        g_byte_array_set_size(buf, 4);
        buf->data[0] = cla;
        buf->data[1] = ins;
        buf->data[2] = p1;
        buf->data[3] = p2;
        if (len > 0) {
            if (len < 0x100) {
                /* Short Lc field */
                guint8 lc = (guint8)len;

                g_byte_array_append(buf, &lc, 1);
            } else {
                /* Extended Lc field */
                guint8 lc[3];

                lc[0] = 0;
                lc[1] = (guint8)(len >> 8);
                lc[2] = (guint8)len;
                g_byte_array_append(buf, lc, sizeof(lc));
            }
            g_byte_array_append(buf, data, len);
        }
        if (exp > 0) {
            if (exp <= 0x100) {
                /* Short Le field */
                guint8 le = (exp == 0x100) ? 0 : ((guint8)exp);

                g_byte_array_append(buf, &le, 1);
            } else {
                /* Extended Le field */
                guint8 le[2];

                if (exp == 0x10000) {
                    le[0] = le[1] = 0;
                } else {
                    le[0] = (guint8)(exp >> 8);
                    le[1] = (guint8)exp;
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

static
void
nfc_tag_t4_tx_free(
    NfcIsoDepTx* tx)
{
    GDestroyNotify destroy = tx->destroy;

    if (destroy) {
        tx->destroy = NULL;
        destroy(tx->user_data);
    }
    g_slice_free1(sizeof(*tx), tx);
}

static
void
nfc_tag_t4_tx_free1(
    void* data)
{
    nfc_tag_t4_tx_free((NfcIsoDepTx*)data);
}

static
void
nfc_tag_t4_tx_resp(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    NfcIsoDepTx* tx = user_data;

    if (status == NFC_TRANSMIT_STATUS_OK) {
        if (len < 2) {
            GWARN("Type 4 response too short, %u bytes(s)", len);
            tx->resp(tx->t4, ISO_SW_IO_ERR, NULL, 0, tx->user_data);
        } else if (len > 0x10000) {
            GWARN("Type 4 response too long, %u bytes(s)", len);
            tx->resp(tx->t4, ISO_SW_IO_ERR, NULL, 0, tx->user_data);
        } else {
            const guint8* sw = ((guint8*)data) + len - 2;

            tx->resp(tx->t4, (((guint)sw[0]) << 8) | sw[1],  data, len - 2,
                tx->user_data);
        }
    } else {
        tx->resp(tx->t4, ISO_SW_IO_ERR, NULL, 0, tx->user_data);
    }
}

static
guint
nfc_isodep_submit(
    NfcTagType4* self,
    guint8 cla,             /* Class byte */
    guint8 ins,             /* Instruction byte */
    guint8 p1,              /* Parameter byte 1 */
    guint8 p2,              /* Parameter byte 2 */
    const GUtilData* data,  /* Command data */
    guint le,               /* Expected length */
    NfcTargetSequence* seq,
    NfcTagType4ResponseFunc resp,
    GDestroyNotify destroy,
    void* user_data)
{
    NfcTagType4Priv* priv = self->priv;
    GByteArray* buf = priv->buf;
    const void* bytes;
    guint len;

    if (data) {
        bytes = data->bytes;
        len = data->size;
    } else {
        bytes = NULL;
        len = 0;
    }

    if (nfc_tag_t4_build_apdu(buf, cla, ins, p1, p2, len, bytes, le)) {
        NfcTag* tag = &self->tag;
        NfcIsoDepTx* tx = g_slice_new0(NfcIsoDepTx);
        guint id;

        tx->t4 = self;
        tx->resp = resp;
        tx->destroy = destroy;
        tx->user_data = user_data;
        id = nfc_target_transmit(tag->target, buf->data, buf->len, seq,
            resp ? nfc_tag_t4_tx_resp : NULL, nfc_tag_t4_tx_free1, tx);
        if (id) {
            return id;
        } else {
            tx->destroy = NULL;
            nfc_tag_t4_tx_free(tx);
        }
    }
    return 0;
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

void
nfc_tag_t4_init_base(
    NfcTagType4* self,
    NfcTarget* target,
    guint mtu)
{
    NfcTagType4Priv* priv = self->priv;

    nfc_tag_init_base(&self->tag, target);
    priv->mtu = mtu;

    /*
     * For more information in NDEF application and fetching NDEF
     * from a Type 4 tag, see NFCForum-TS-Type-4-Tag_2.0 section
     * 5.1 NDEF Management.
     *
     * Better leave the default application selected and ignore this
     * NDEF stuff for now until this feature is requested and even more
     * importantly we have a real card to test against.
     */
#pragma message("TODO: Read NDEF")
    nfc_tag_set_initialized(&self->tag);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

guint
nfc_isodep_transmit(
    NfcTagType4* self,
    guint8 cla,             /* Class byte */
    guint8 ins,             /* Instruction byte */
    guint8 p1,              /* Parameter byte 1 */
    guint8 p2,              /* Parameter byte 2 */
    const GUtilData* data,  /* Command data */
    guint le,               /* Expected length, zero if none */
    NfcTargetSequence* seq,
    NfcTagType4ResponseFunc resp,
    GDestroyNotify destroy,
    void* user_data)
{
    return G_LIKELY(self) ? nfc_isodep_submit(self, cla, ins, p1, p2,
        data, le, seq, resp, destroy, user_data) : 0;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_tag_t4_init(
    NfcTagType4* self)
{
    NfcTagType4Priv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        NFC_TYPE_TAG_T4, NfcTagType4Priv);

    self->priv = priv;
    priv->buf = g_byte_array_sized_new(12);
}

static
void
nfc_tag_t4_finalize(
    GObject* object)
{
    NfcTagType4* self = NFC_TAG_T4(object);
    NfcTagType4Priv* priv = self->priv;

    nfc_target_cancel_transmit(self->tag.target, priv->init_id);
    g_byte_array_free(priv->buf, TRUE);
    G_OBJECT_CLASS(nfc_tag_t4_parent_class)->finalize(object);
}

static
void
nfc_tag_t4_class_init(
    NfcTagType4Class* klass)
{
    g_type_class_add_private(klass, sizeof(NfcTagType4Priv));
    G_OBJECT_CLASS(klass)->finalize = nfc_tag_t4_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
