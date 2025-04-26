/*
 * Copyright (C) 2019-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2019-2021 Jolla Ltd.
 * Copyright (C) 2020 Open Mobile Platform LLC.
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

#include "nfc_tag_p.h"
#include "nfc_tag_t4_p.h"
#include "nfc_target_p.h"
#include "nfc_util.h"
#include "nfc_log.h"

#include <nfcdef.h>

#include <gutil_macros.h>

typedef struct nfc_isodep_tx {
    NfcTagType4* t4;
    NfcTagType4ResponseFunc resp;
    GDestroyNotify destroy;
    void* user_data;
} NfcIsoDepTx;

typedef struct nfc_iso_dep_ndef_read {
    NfcTagType4* t4;
    guint8 fid[2];
    guint data_len;
    guint max_read;
    GByteArray* data;
} NfcIsoDepNdefRead;

struct nfc_tag_t4_priv {
    guint mtu;  /* FSC (Type 4A) or FSD (Type 4B) */
    GByteArray* buf;
    NfcTargetSequence* init_seq;
    NfcIsoDepNdefRead* init_read;
    guint init_id;
    NfcParamIsoDep* iso_dep; /* Since 1.0.39 */
};

typedef struct nfc_isodep_reset_data {
    NfcTagType4* t4;
    NfcTagType4ResetRespFunc resp;
    GDestroyNotify destroy;
    void* user_data;
} NfcIsoDepResetData;

#define THIS(obj) NFC_TAG_T4(obj)
#define THIS_TYPE NFC_TYPE_TAG_T4
#define PARENT_TYPE NFC_TYPE_TAG
#define PARENT_CLASS nfc_tag_t4_parent_class

G_DEFINE_ABSTRACT_TYPE(NfcTagType4, nfc_tag_t4, PARENT_TYPE)

/*
 * NFCForum-TS-Type-4-Tag_2.0
 * Section 5.4.2. NDEF Tag Application Select Procedure
 */

static const guint8 ndef_aid[] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };
static const guint8 ndef_cc_ef[] = { 0xE1, 0x03 };
static const GUtilData ndef_aid_data = { ndef_aid, sizeof(ndef_aid) };
static const GUtilData ndef_cc_ef_data = { ndef_cc_ef, sizeof(ndef_cc_ef) };

#define ISO_SW_NDEF_NOT_FOUND (0x6a82)
#define NDEF_CC_LEN (15)
#define NDEF_DATA_OFFSET (2)

/*==========================================================================*
 * Implementation
 *==========================================================================*/

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
    NfcApdu apdu;

    apdu.cla = cla;
    apdu.ins = ins;
    apdu.p1 = p1;
    apdu.p2 = p2;
    apdu.le = le;
    if (data) {
        apdu.data = *data;
    } else {
        memset(&apdu.data, 0, sizeof(apdu.data));
    }

    if (nfc_apdu_encode(buf, &apdu)) {
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

static
NfcIsoDepNdefRead*
nfc_iso_dep_ndef_read_new(
    NfcTagType4* self,
    const guint8* cc /* At least 15 bytes */)
{
    /* See Table 4: Data Structure of the Capability Container File */
    /* And Section 5.1.2.1 NDEF File Control TLV */
    if ((cc[2] >> 4) == 2 /* We expect Version 2 of the spec */ &&
        cc[7] == 4 && cc[8] == 6 /* File Control TLV, T = 4, L = 6 */) {
        const guint8* v = cc + 9; /* V part of File Control TLV */

        /* Check NDEF file read access condition */
        if (v[4] == 0 /* read access granted */) {
            /*
             * 5.1.2.1 NDEF File Control TLV
             *
             * ...
             * [RQ_T4T_NDA_016] File Identifier, 2 bytes. Indicates a valid
             * NDEF file. The valid ranges are 0001h to E101h, E104h to 3EFFh,
             * 3F01h to 3FFEh and 4000h to FFFEh. The values 0000h, E102h,
             * E103h, 3F00h and 3FFFh are reserved (see [ISO/IEC_7816-4])
             * and FFFFh is RFU.
             */
            const guint fid = ((((guint)(v[0])) << 8) | v[1]);

            if ((fid >= 0x0001 && fid <= 0xE101) ||
                (fid >= 0xE104 && fid <= 0x3EFF) ||
                (fid >= 0x3F01 && fid <= 0x3FFE) ||
                (fid >= 0x4000 && fid <= 0xFFFE)) {
                const guint max_read = ((((guint)(cc[3])) << 8) | cc[4]);

                /* The valid values for MLe are 000Fh-FFFFh */
                if (max_read >= 0x000f) {
                    NfcIsoDepNdefRead* read = g_slice_new0(NfcIsoDepNdefRead);

                    read->max_read = max_read;
                    read->fid[0] = v[0];
                    read->fid[1] = v[1];
                    read->t4 = self;
                    GDEBUG("NDEF file: %04X", fid);
                    GVERBOSE("Max read: %u bytes", read->max_read);
                    return read;
                } else {
                    GDEBUG("MLe too small (%u)", max_read);
                }
            } else {
                GDEBUG("Invalid NDEF file id %04X", fid);
            }
        } else {
            GDEBUG("NDEF read not allowed");
        }
    } else {
        GDEBUG("Unexpected structure of NDEF Capability Container");
    }
    return NULL;
}

static
void
nfc_iso_dep_ndef_read_free(
    NfcIsoDepNdefRead* read)
{
    if (read) {
        if (read->data) {
            g_byte_array_free(read->data, TRUE);
        }
        g_slice_free1(sizeof(*read), read);
    }
}

static
void
nfc_tag_t4_initialized(
    NfcTagType4* self)
{
    NfcTagType4Priv* priv = self->priv;
    NfcTag* tag = &self->tag;

    GASSERT(!priv->init_id);
    g_object_ref(self);
    nfc_target_sequence_unref(priv->init_seq);
    nfc_iso_dep_ndef_read_free(priv->init_read);
    priv->init_seq = NULL;
    priv->init_read = NULL;
    nfc_tag_set_initialized(tag);
    g_object_unref(self);
}

static
void
nfc_tag_t4_init_done(
    NfcTarget* target,
    NFC_REACTIVATE_STATUS status,
    void* tag)
{
    NfcTagType4* self = THIS(tag);

    /*
     * Still mark the tag as initialized even if reactivation times out,
     * to give the dbus_handlers plugin a chance to run its NDEF handlers.
     */
    nfc_tag_t4_initialized(self);
    nfc_tag_unref(&self->tag);
}

static
void
nfc_tag_t4_ndef_read_done(
    NfcTagType4* self)
{
    NfcTagType4Priv* priv = self->priv;

    /*
     * The initialization sequence has completed, release it. It must be
     * done now, to avoid blocking presence checks in case if reactivation
     * times out.
     */
    GDEBUG("Reactivating Type 4 tag");
    nfc_tag_ref(&self->tag);
    if (!nfc_target_reactivate(self->tag.target, priv->init_seq,
        nfc_tag_t4_init_done, NULL, self)) {
        GDEBUG("Oops. Failed to reactivate, leaving the tag as is");
        nfc_tag_t4_initialized(self);
        nfc_tag_unref(&self->tag);
    }
}

static
guint
nfc_isodep_init_read_binary(
    NfcTagType4* self,
    guint offset,
    guint le,
    NfcTagType4ResponseFunc resp)
{
    return nfc_isodep_submit(self, ISO_CLA, ISO_INS_READ_BINARY,
        (guint8)(offset >> 8), (guint8)offset, NULL, le,
        self->priv->init_seq, resp, NULL, NULL);
}

static
void
nfc_tag_t4_init_read_ndef_data_resp(
    NfcTagType4* self,
    guint sw,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType4Priv* priv = self->priv;

    if (sw == ISO_SW_OK) {
        if (len > 0) {
            NfcIsoDepNdefRead* read = priv->init_read;
            GByteArray* buf = read->data;

            g_byte_array_append(buf, data, len);
            if (buf->len < read->data_len) {
                const guint remaining = read->data_len - buf->len;

                if ((priv->init_id = nfc_isodep_init_read_binary(self,
                    buf->len + NDEF_DATA_OFFSET,
                    MIN(remaining, read->max_read),
                    nfc_tag_t4_init_read_ndef_data_resp)) != 0) {
                    return;
                }
            } else {
                GUtilData ndef;

                /* Parse the NDEF */
                ndef.bytes = buf->data;
                ndef.size = buf->len;
                self->tag.ndef = ndef_rec_new(&ndef);
            }
        } else {
            GDEBUG("Empty NDEF read");
        }
    } else if (sw != ISO_SW_IO_ERR) {
        GDEBUG("NDEF read error %04X", sw);
    } else {
        GDEBUG("NDEF read I/O error");
    }
    priv->init_id = 0;
    nfc_tag_t4_ndef_read_done(self);
}

static
void
nfc_tag_t4_init_read_ndef_len_resp(
    NfcTagType4* self,
    guint sw,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType4Priv* priv = self->priv;

    if (sw == ISO_SW_OK) {
        if (len == NDEF_DATA_OFFSET) {
            NfcIsoDepNdefRead* read = priv->init_read;
            const guint8* bytes = data;

            read->data_len = ((((guint)(bytes[0])) << 8) | bytes[1]);
            if (read->data_len > 0) {
                GDEBUG("Reading %u bytes of NDEF data", read->data_len);
                read->data = g_byte_array_sized_new(read->data_len);
                if ((priv->init_id = nfc_isodep_init_read_binary(self,
                    NDEF_DATA_OFFSET, MIN(read->data_len, read->max_read),
                    nfc_tag_t4_init_read_ndef_data_resp)) != 0) {
                    return;
                }
            } else {
                GDEBUG("NDEF is empty");
            }
        } else {
            GDEBUG("Unexpected number of bytes from NDEF file (%u)", len);
        }
    } else if (sw != ISO_SW_IO_ERR) {
        GDEBUG("NDEF read error %04X", sw);
    } else {
        GDEBUG("NDEF read I/O error");
    }
    priv->init_id = 0;
    nfc_tag_t4_ndef_read_done(self);
}

static
void
nfc_tag_t4_init_select_ndef_resp(
    NfcTagType4* self,
    guint sw,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType4Priv* priv = self->priv;

    if (sw == ISO_SW_OK) {
        NfcIsoDepNdefRead* read = priv->init_read;

        GDEBUG("Selected %02X%02X", read->fid[0], read->fid[1]);
        /* Read first 2 bytes of the NDEF file (record size) */
        if ((priv->init_id = nfc_isodep_init_read_binary(self, 0,
            NDEF_DATA_OFFSET, nfc_tag_t4_init_read_ndef_len_resp)) != 0) {
            return;
        }
    } else if (sw != ISO_SW_IO_ERR) {
        GDEBUG("NDEF file selection error %04X", sw);
    } else {
        GDEBUG("NDEF file selection I/O error");
    }
    priv->init_id = 0;
    nfc_tag_t4_ndef_read_done(self);
}

static
void
nfc_tag_t4_init_read_ndef_cc_resp(
    NfcTagType4* self,
    guint sw,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType4Priv* priv = self->priv;

    if (sw == ISO_SW_OK) {
        if (len < NDEF_CC_LEN) {
            GDEBUG("Not enough data for NDEF Capability Container");
        } else {
            GDEBUG("NDEF Capability Container");
            nfc_hexdump(data, len);
            priv->init_read = nfc_iso_dep_ndef_read_new(self, data);
            if (priv->init_read) {
                GUtilData fid;

                /*
                 * Table 18: NDEF Select Command C-APDU
                 * 00A4000C02xxxx
                 */
                fid.bytes = priv->init_read->fid;
                fid.size = 2;
                if ((priv->init_id = nfc_isodep_submit(self, ISO_CLA,
                    ISO_INS_SELECT, ISO_P1_SELECT_BY_ID,
                    ISO_P2_SELECT_FILE_FIRST | ISO_P2_RESPONSE_NONE,
                    &fid, 0, priv->init_seq,
                    nfc_tag_t4_init_select_ndef_resp, NULL, NULL)) != 0) {
                    return;
                }
            }
        }
    } else if (sw != ISO_SW_IO_ERR) {
        GDEBUG("NDEF Capability Container read error %04X", sw);
    } else {
        GDEBUG("NDEF Capability Container read I/O error");
    }
    priv->init_id = 0;
    nfc_tag_t4_ndef_read_done(self);
}

static
void
nfc_tag_t4_init_select_ndef_cc_resp(
    NfcTagType4* self,
    guint sw,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType4Priv* priv = self->priv;

    /*
     * NFCForum-TS-Type-4-Tag_2.0
     * Table 14: Capability Container Select Command -
     *           Detailed R-APDU Field Description
     *
     * 90h 00h   Command completed
     * 6Ah 82h   Capability container not found
     */
    if (sw == ISO_SW_OK) {
        GVERBOSE("NDEF Capability Container selected");
        /* Read first 15 bytes of CC */
        if ((priv->init_id = nfc_isodep_init_read_binary(self, 0, NDEF_CC_LEN,
            nfc_tag_t4_init_read_ndef_cc_resp)) != 0) {
            return;
        }
    } else if (sw == ISO_SW_NDEF_NOT_FOUND) {
        GDEBUG("NDEF Capability Container not found");
    } else if (sw != ISO_SW_IO_ERR) {
        GDEBUG("NDEF Capability Container selection error %04X", sw);
    } else {
        GDEBUG("NDEF Capability Container selection I/O error");
    }
    priv->init_id = 0;
    nfc_tag_t4_ndef_read_done(self);
}

static
void
nfc_tag_t4_init_select_ndef_app_resp(
    NfcTagType4* self,
    guint sw,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType4Priv* priv = self->priv;

    /*
     * NFCForum-TS-Type-4-Tag_2.0
     * Table 11: NDEF Tag Application Select -
     *           Detailed R-APDU Field Description
     *
     * 90h 00h   Command completed
     * 6Ah 82h   NDEF Tag Application not found
     */
    if (sw == ISO_SW_OK) {
        GDEBUG("Found NDEF Tag Application");
        /*
         * Table 12: Capability Container Select Command C-APDU
         * 00A4000C02E103
         */
        if ((priv->init_id = nfc_isodep_submit(self, ISO_CLA,
            ISO_INS_SELECT, ISO_P1_SELECT_BY_ID,
            ISO_P2_SELECT_FILE_FIRST | ISO_P2_RESPONSE_NONE,
            &ndef_cc_ef_data, 0, priv->init_seq,
            nfc_tag_t4_init_select_ndef_cc_resp, NULL, NULL)) != 0) {
            return;
        }
    } else if (sw == ISO_SW_NDEF_NOT_FOUND) {
        GDEBUG("NDEF Tag Application not found");
    } else if (sw != ISO_SW_IO_ERR) {
        GDEBUG("NDEF Tag Application selection error %04X", sw);
    } else {
        GDEBUG("NDEF Tag Application selection I/O error");
    }

    /* No need to reinitialize the tag in this case */
    priv->init_id = 0;
    nfc_tag_t4_initialized(self);
}

static
void
nfc_tag_t4_reset_data_free(
    NfcIsoDepResetData* rst)
{
    GDestroyNotify destroy = rst->destroy;

    if (destroy) {
        rst->destroy = NULL;
        destroy(rst->user_data);
    }
    g_slice_free1(sizeof(*rst), rst);
}

static
void
nfc_tag_t4_reset_data_free1(
    void* data)
{
    nfc_tag_t4_reset_data_free((NfcIsoDepResetData*)data);
}

static
void
nfc_tag_t4_reset_data_resp(
    NfcTarget* target,
    NFC_REACTIVATE_STATUS status,
    void* user_data)
{
    NfcIsoDepResetData* rst = user_data;

    if (rst->resp) {
        /* Result is FALSE in case of tag was gone or reactivation timed out */
        rst->resp(rst->t4, status == NFC_REACTIVATE_STATUS_SUCCESS,
            rst->user_data);
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

void
nfc_tag_t4_init_base(
    NfcTagType4* self,
    NfcTarget* target,
    guint mtu,
    gboolean read_ndef,
    const NfcParamPoll* poll,
    const NfcParamIsoDep* iso_dep)
{
    NfcTag* tag = &self->tag;
    NfcTagType4Priv* priv = self->priv;

    nfc_tag_init_base(tag, target, poll);
    priv->mtu = mtu;

    if (iso_dep) {
        const gsize aligned_size = G_ALIGN8(sizeof(*iso_dep));
        const GUtilData* src;
        gsize size;

        /*
         * Allocate the whole thing (including additional data) from a
         * single memory block and adjust the pointers.
         */
        switch (target->technology) {
        case NFC_TECHNOLOGY_A:
            src = &iso_dep->a.t1;
            size = src->size ? (aligned_size + src->size) : sizeof(*iso_dep);
            *(priv->iso_dep = g_malloc0(size)) = *iso_dep;
            if (src->bytes) {
                guint8* dest = (guint8*)priv->iso_dep + aligned_size;

                memcpy(dest, src->bytes, src->size);
                priv->iso_dep->a.t1.bytes = dest;
            }
            self->iso_dep = priv->iso_dep;
            break;
        case NFC_TECHNOLOGY_B:
            src = &iso_dep->b.hlr;
            size = src->size ? (aligned_size + src->size) : sizeof(*iso_dep);
            *(priv->iso_dep = g_malloc0(size)) = *iso_dep;
            if (src->bytes) {
                guint8* dest = (guint8*)priv->iso_dep + aligned_size;

                memcpy(dest, src->bytes, src->size);
                priv->iso_dep->b.hlr.bytes = dest;
            }
            self->iso_dep = priv->iso_dep;
            break;
        case NFC_TECHNOLOGY_F:
        case NFC_TECHNOLOGY_UNKNOWN:
            break;
        }
    }

    /*
     * Reactivation is required in order to reset the card back to its
     * pristine initial state (with default application selected). Since
     * we have no reliable way to determine the default application, the
     * most reliable (and perhaps the only) way to re-select the default
     * application is to reinitialize the card from scratch. Besides,
     * selection of a non-default application may be an irreversible
     * action (which of course depends on how the card is programmed).
     */
    if (read_ndef && nfc_target_can_reactivate(tag->target)) {
        priv->init_seq = nfc_target_sequence_new(target);

        /*
         * NFCForum-TS-Type-4-Tag_2.0
         * Section 5.4.2. NDEF Tag Application Select Procedure
         *
         * Table 9: NDEF Tag Application Select C-APDU
         * 00A4040007D276000085010100
         */
        if ((priv->init_id = nfc_isodep_submit(self, ISO_CLA, ISO_INS_SELECT,
            ISO_P1_SELECT_DF_BY_NAME, ISO_P2_SELECT_FILE_FIRST, &ndef_aid_data,
            0x100, priv->init_seq, nfc_tag_t4_init_select_ndef_app_resp,
            NULL, NULL)) != 0) {
            return;
        }
    }
    nfc_tag_t4_initialized(self);
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

gboolean
nfc_isodep_reset(
    NfcTagType4* self,
    NfcTargetSequence* seq,
    NfcTagType4ResetRespFunc resp,
    GDestroyNotify destroy,
    void* user_data) /* Since 1.0.44 */
{
    if (G_LIKELY(self)) {
        NfcTag* tag = &self->tag;

        if (G_LIKELY(tag) && nfc_target_can_reactivate(tag->target)) {
            NfcIsoDepResetData* rst = g_slice_new0(NfcIsoDepResetData);

            rst->t4 = self;
            rst->resp = resp;
            rst->destroy = destroy;
            rst->user_data = user_data;

            if (nfc_target_reactivate(tag->target, seq, resp ?
                nfc_tag_t4_reset_data_resp : NULL, nfc_tag_t4_reset_data_free1,
                rst)) {
                return TRUE;
            } else {
                /*
                * Should never get here, 'cause nfc_target_reactivate() will
                * always return TRUE in case if nfc_target_can_reactivate()
                * succeeds.
                */
                rst->destroy = NULL;
                nfc_tag_t4_reset_data_free(rst);
            }
        }
    }
    return FALSE;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_tag_t4_init(
    NfcTagType4* self)
{
    NfcTagType4Priv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcTagType4Priv);

    self->priv = priv;
    priv->buf = g_byte_array_sized_new(12);
}

static
void
nfc_tag_t4_finalize(
    GObject* object)
{
    NfcTagType4* self = THIS(object);
    NfcTagType4Priv* priv = self->priv;

    nfc_target_cancel_transmit(self->tag.target, priv->init_id);
    nfc_target_sequence_unref(priv->init_seq);
    nfc_iso_dep_ndef_read_free(priv->init_read);
    g_byte_array_free(priv->buf, TRUE);
    g_free(priv->iso_dep);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
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
