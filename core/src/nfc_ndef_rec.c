/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2022 Jolla Ltd.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfc_ndef_p.h"
#include "nfc_util.h"
#include "nfc_tlv.h"
#include "nfc_log.h"

#include <gutil_misc.h>

struct nfc_ndef_rec_priv {
    guint8* data;
};

#define THIS(obj) NFC_NDEF_REC(obj)
#define THIS_TYPE NFC_TYPE_NDEF_REC
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS (nfc_ndef_rec_parent_class)

G_DEFINE_TYPE(NfcNdefRec, nfc_ndef_rec, PARENT_TYPE)

static
NfcNdefRec*
nfc_ndef_rec_alloc(
    const NfcNdefData* ndef)
{
    if (ndef->rec.size) {
        const NFC_NDEF_TNF tnf = ndef->rec.bytes[0] & NFC_NDEF_HDR_TNF_MASK;

        /* Handle known types */
        if (tnf == NFC_NDEF_TNF_WELL_KNOWN) {
            GUtilData type;

            nfc_ndef_type(ndef, &type);
            if (gutil_data_equal(&type, &nfc_ndef_rec_type_u)) {
                NfcNdefRecU* uri_rec = nfc_ndef_rec_u_new_from_data(ndef);

                if (uri_rec) {
                    /* URI Record */
                    GDEBUG("URI Record: %s", uri_rec->uri);
                    return THIS(uri_rec);
                }
            } else if (gutil_data_equal(&type, &nfc_ndef_rec_type_t)) {
                NfcNdefRecT* text_rec = nfc_ndef_rec_t_new_from_data(ndef);

                if (text_rec) {
                    /* TEXT Record */
                    GVERBOSE("Locale: %s", nfc_system_locale());
                    GVERBOSE("Language: %s", text_rec->lang);
                    GDEBUG("Text Record: %s", text_rec->text);
                    return THIS(text_rec);
                }
            } else if (gutil_data_equal(&type, &nfc_ndef_rec_type_sp)) {
                NfcNdefRecSp* sp_rec = nfc_ndef_rec_sp_new_from_data(ndef);

                if (sp_rec) {
                    /* SmartPoster Record */
                    GVERBOSE("SmartPoster URI: %s", sp_rec->uri);
                    return THIS(sp_rec);
                }
            }
        }

        /* Generic record */
        return nfc_ndef_rec_initialize(g_object_new(THIS_TYPE, NULL),
            NFC_NDEF_RTD_UNKNOWN, ndef);
    } else {
        /* Special case - Empty NDEF */
        return g_object_new(THIS_TYPE, NULL);
    }
}

static
gboolean
nfc_ndef_rec_parse(
    GUtilData* block,
    NfcNdefData* ndef)
{
    if (block->size < 3) {
        /* At least 3 bytes is required for anything meaningful */
        GDEBUG("Block is too short to be an NDEF record");
        return FALSE;
    } else {
        const guint8 hdr = block->bytes[0];
        guint total_len = 1;

        memset(ndef, 0, sizeof(*ndef));
        ndef->type_length = block->bytes[1];

        /* Type */
        total_len += 1 + ndef->type_length;
        ndef->type_offset = 2;

        /* Payload length */
        if (hdr & NFC_NDEF_HDR_SR) {
            /* Short record */
            ndef->payload_length = block->bytes[ndef->type_offset++];
            total_len += 1 + ndef->payload_length;
        } else {
            /* 4 bytes for length */
            ndef->payload_length =
                (((guint)block->bytes[ndef->type_offset]) << 24) |
                (((guint)block->bytes[ndef->type_offset + 1]) << 16) |
                (((guint)block->bytes[ndef->type_offset + 2]) << 8) |
                ((guint)block->bytes[ndef->type_offset + 3]);
            total_len += 4 + ndef->payload_length;
            ndef->type_offset += 4;
        }

        /* ID Length */
        if (hdr & NFC_NDEF_HDR_IL) {
            ndef->id_length = block->bytes[ndef->type_offset++];
            total_len += 1 + ndef->id_length;
        }

        /* Check for overflow */
        if (ndef->payload_length < 0x80000000 && total_len <= block->size) {
            /* Cut the garbage if there is any */
            ndef->rec.bytes = block->bytes;
            ndef->rec.size = total_len;
            block->bytes += total_len;
            block->size -= total_len;
            return TRUE;
        } else {
            GDEBUG("Garbage (lengths don't add up)");
        }
        return FALSE;
    }
}

static
NfcNdefRec*
nfc_ndef_rec_new_from_data(
    GType gtype,
    NFC_NDEF_TNF tnf,
    NFC_NDEF_RTD rtd,
    const GUtilData* type,
    const GUtilData* payload)
{
    /* type and payload pointers are checked by the caller */
    if (gtype &&
#if GLIB_SIZEOF_SIZE_T > 4
        payload->size <= 0xffffffff &&
#endif
        type->size <= 0xff) {
        NfcNdefData ndef;
        NfcNdefRec* rec;
        guint8 hdr = NFC_NDEF_HDR_MB | NFC_NDEF_HDR_ME |
            (tnf & NFC_NDEF_HDR_TNF_MASK);
        const guint8 type_length = type->size;
        const gboolean sr = (payload->size <= 0xff); /* Short Record) */
        GByteArray* buf = g_byte_array_sized_new((sr ? 3 : 6) + type_length +
            payload->size);

        memset(&ndef, 0, sizeof(ndef));
        ndef.type_length = type_length;
        ndef.payload_length = payload->size;

        /* Header, TYPE LENGTH and PAYLOAD LENGTH */
        if (sr) {
            guint8 payload_len = (guint8)payload->size;

            /*
             * If the SR flag is set, the PAYLOAD_LENGTH field is a single
             * octet representing an 8-bit unsigned integer.
             */
            hdr |= NFC_NDEF_HDR_SR;
            g_byte_array_append(buf, &hdr, 1);
            g_byte_array_append(buf, &type_length, 1);
            g_byte_array_append(buf, &payload_len, 1);
        } else {
            guint8 payload_length[4];

            /*
             * If the SR flag is clear, the PAYLOAD_LENGTH field is four
             * octets representing a 32-bit unsigned integer. Transmission
             * order of the octets is MSB-first.
             */
            payload_length[0] = (guint8)(payload->size >> 24);
            payload_length[1] = (guint8)(payload->size >> 16);
            payload_length[2] = (guint8)(payload->size >> 8);
            payload_length[3] = (guint8)payload->size;
            g_byte_array_append(buf, &hdr, 1);
            g_byte_array_append(buf, &type_length, 1);
            g_byte_array_append(buf, payload_length, 4);
        }

        /* TYPE */
        ndef.type_offset = buf->len;
        g_byte_array_append(buf, type->bytes, type->size);

        /* PAYLOAD */
        g_byte_array_append(buf, payload->bytes, payload->size);

        /* Allocate the object */
        ndef.rec.bytes = buf->data;
        ndef.rec.size = buf->len;
        rec = nfc_ndef_rec_initialize(g_object_new(gtype, NULL), rtd, &ndef);

        g_byte_array_free(buf, TRUE);
        return rec;
    } else {
        return NULL;
    }
}

static
guint8
nfc_ndef_rec_map_flags(
    NFC_NDEF_REC_FLAGS flags)
{
    guint8 ndef_flags = 0;

    if (flags & NFC_NDEF_REC_FLAG_FIRST) {
        ndef_flags |= NFC_NDEF_HDR_MB;
    }
    if (flags & NFC_NDEF_REC_FLAG_LAST) {
        ndef_flags |= NFC_NDEF_HDR_ME;
    }
    return ndef_flags;
}

/* See RFC 2045, section 5.1 "Syntax of the Content-Type Header Field" */

static
gboolean
nfc_ndef_is_token_char(
    guint8 c)
{
    /*  token := 1*<any (US-ASCII) CHAR except SPACE, CTLs, or tspecials> */
    if (c < 0x80) {
        static const guint32 token_chars[] = {
            0x00000000, /* ................................ */
            0x03ff6cfa, /*  !"#$%&'()*+,-./0123456789:;<=>? */
            0xc7fffffe, /* @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_ */
            0x7fffffff  /* `abcdefghijklmnopqrstuvwxyz{|}~. */
        };
        if (token_chars[c/32] & (1 << (c % 32))) {
            return TRUE;
        }
    }
    return FALSE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcNdefRec*
nfc_ndef_rec_new(
    const GUtilData* block)
{
    NfcNdefRec* first = NULL;

    if (G_LIKELY(block)) {
        NfcNdefData ndef;

        memset(&ndef, 0, sizeof(ndef));
        if (G_LIKELY(block->size)) {
            GUtilData data = *block;
            NfcNdefRec* last = NULL;

            while (data.size > 0 && nfc_ndef_rec_parse(&data, &ndef)) {
                GASSERT(ndef.rec.size);
                if (ndef.rec.bytes[0] & NFC_NDEF_HDR_CF) {
                    /* Who needs those anyway? */
                    GWARN("Chunked records are not supported");
                } else {
                    NfcNdefRec* rec;

                    GDEBUG("NDEF:");
                    nfc_hexdump_data(&ndef.rec);
                    rec = nfc_ndef_rec_alloc(&ndef);
                    if (last) {
                        last->next = rec;
                        last = rec;
                    } else {
                        first = last = rec;
                    }
                }
            }
        } else {
            /* Special case - Empty NDEF */
            GDEBUG("Empty NDEF");
            first = nfc_ndef_rec_alloc(&ndef);
        }
    }
    return first;
}

NfcNdefRec*
nfc_ndef_rec_new_tlv(
    const GUtilData* tlv)
{
    NfcNdefRec* first = NULL;

    if (G_LIKELY(tlv)) {
        GUtilData buf = *tlv, value;
        NfcNdefRec* last = NULL;
        guint type;

        while ((type = nfc_tlv_next(&buf, &value)) > 0) {
            if (type == TLV_NDEF_MESSAGE) {
                NfcNdefRec* rec = nfc_ndef_rec_new(&value);

                if (rec) {
                    if (last) {
                        last->next = rec;
                    } else {
                        first = rec;
                    }
                    /* nfc_ndef_rec_new() can return a chain */
                    last = rec;
                    while (last->next) {
                        last = last->next;
                    }
                }
            }
        }
    }
    return first;
}

NfcNdefRec*
nfc_ndef_rec_new_mediatype(
    const GUtilData* type,
    const GUtilData* payload) /* Since 1.1.18 */
{
    if (nfc_ndef_valid_mediatype(type, FALSE)) {
        static const GUtilData no_payload = { NULL, 0 };

        return nfc_ndef_rec_new_from_data(THIS_TYPE, NFC_NDEF_TNF_MEDIA_TYPE,
            NFC_NDEF_RTD_UNKNOWN, type, payload ? payload : &no_payload);
    }
    return NULL;
}

NfcNdefRec*
nfc_ndef_rec_ref(
    NfcNdefRec* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_ndef_rec_unref(
    NfcNdefRec* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

gboolean
nfc_ndef_valid_mediatype(
    const GUtilData* type,
    gboolean wildcard) /* Since 1.0.18 */
{
    if (type) {
        guint i = 0;

        if (type->size > 0) {
            if (type->bytes[i] == (guint8)'*') {
                if (wildcard) {
                    i++;
                } else {
                    return FALSE;
                }
            } else {
                while (i < type->size &&
                    nfc_ndef_is_token_char(type->bytes[i])) {
                    i++;
                }
            }
        }
        if (i > 0 && (i + 1) < type->size && type->bytes[i] == (guint8)'/') {
            i++;
            if ((i + 1) == type->size && type->bytes[i] == (guint8)'*') {
                return wildcard;
            } else {
                while (i < type->size &&
                    nfc_ndef_is_token_char(type->bytes[i])) {
                    i++;
                }
                if (i == type->size) {
                    return !wildcard;
                }
            }
        }
    }
    return FALSE;
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

gboolean
nfc_ndef_type(
    const NfcNdefData* ndef,
    GUtilData* type)
{
    if (ndef && ndef->type_length) {
        type->bytes = ndef->rec.bytes + ndef->type_offset;
        type->size = ndef->type_length;
        return TRUE;
    } else {
        type->bytes = NULL;
        type->size = 0;
        return FALSE;
    }
}

gboolean
nfc_ndef_payload(
    const NfcNdefData* ndef,
    GUtilData* payload)
{
    if (ndef && ndef->payload_length) {
        payload->bytes = ndef->rec.bytes + ndef->type_offset +
            ndef->type_length + ndef->id_length;
        payload->size = ndef->payload_length;
        return TRUE;
    } else {
        payload->bytes = NULL;
        payload->size = 0;
        return FALSE;
    }
}

NfcNdefRec*
nfc_ndef_rec_new_well_known(
    GType gtype,
    NFC_NDEF_RTD rtd,
    const GUtilData* type,
    const GUtilData* payload)
{
    return nfc_ndef_rec_new_from_data(gtype, NFC_NDEF_TNF_WELL_KNOWN,
        rtd, type, payload);
}

NfcNdefRec*
nfc_ndef_rec_initialize(
    NfcNdefRec* self,
    NFC_NDEF_RTD rtd,
    const NfcNdefData* ndef)
{
    if (self && ndef) {
        NfcNdefRecPriv* priv = self->priv;
        const GUtilData* rec = &ndef->rec;
        const guint hdr = rec->bytes[0];
        const guint8 tnf = (hdr & NFC_NDEF_HDR_TNF_MASK);

        if (tnf < NFC_NDEF_TNF_MAX) {
            self->tnf = tnf;
        }
        if (hdr & NFC_NDEF_HDR_MB) {
            self->flags |= NFC_NDEF_REC_FLAG_FIRST;
        }
        if (hdr & NFC_NDEF_HDR_ME) {
            self->flags |= NFC_NDEF_REC_FLAG_LAST;
        }
        self->rtd = rtd;
        self->raw.bytes = priv->data = gutil_memdup(rec->bytes, rec->size);
        self->raw.size = rec->size;
        self->type.bytes = self->raw.bytes + ndef->type_offset;
        self->type.size = ndef->type_length;
        if (ndef->id_length > 0) {
            self->id.bytes = self->type.bytes + ndef->type_length;
            self->id.size = ndef->id_length;
        }
        if (ndef->payload_length) {
            self->payload.size = ndef->payload_length;
            self->payload.bytes = self->type.bytes + ndef->type_length +
                ndef->id_length;
        }
    }
    return self;
}

void
nfc_ndef_rec_clear_flags(
    NfcNdefRec* self,
    NFC_NDEF_REC_FLAGS flags)
{
    self->flags &= ~flags;
    self->priv->data[0] &= ~nfc_ndef_rec_map_flags(flags);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_ndef_rec_init(
    NfcNdefRec* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE, NfcNdefRecPriv);
}

static
void
nfc_ndef_rec_finalize(
    GObject* object)
{
    NfcNdefRec* self = THIS(object);
    NfcNdefRecPriv* priv = self->priv;

    g_free(priv->data);
    nfc_ndef_rec_unref(self->next);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_ndef_rec_class_init(
    NfcNdefRecClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcNdefRecPriv));
    G_OBJECT_CLASS(klass)->finalize = nfc_ndef_rec_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
