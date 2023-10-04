/*
 * Copyright (C) 2019-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2019-2022 Jolla Ltd.
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
#include "nfc_log.h"
#include "nfc_system.h"

#include <gutil_misc.h>

/* NFCForum-SmartPoster_RTD_1.0 */

typedef struct nfc_ndef_media_priv {
    NfcNdefMedia pub;
    char* type;
    guint8* data;
} NfcNdefMediaPriv;

struct nfc_ndef_rec_sp_priv {
    char* uri;
    char* title;
    char* lang;
    char* type;
    NfcNdefMediaPriv* icon;
};

#define THIS(obj) NFC_NDEF_REC_SP(obj)
#define THIS_TYPE NFC_TYPE_NDEF_REC_SP
#define PARENT_TYPE NFC_TYPE_NDEF_REC
#define PARENT_CLASS nfc_ndef_rec_sp_parent_class

typedef NfcNdefRecClass NfcNdefRecSpClass;
G_DEFINE_TYPE(NfcNdefRecSp, nfc_ndef_rec_sp, PARENT_TYPE)

const GUtilData nfc_ndef_rec_type_sp = { (const guint8*) "Sp", 2 };

/* Local types */
static const GUtilData nfc_ndef_rec_sp_type_act = { (const guint8*) "act", 3 };
static const GUtilData nfc_ndef_rec_sp_type_s = { (const guint8*) "s", 1 };
static const GUtilData nfc_ndef_rec_sp_type_t = { (const guint8*) "t", 1 };

static
NfcNdefMediaPriv*
nfc_ndef_rec_sp_media_new(
    NfcNdefRec* rec)
{
    NfcNdefMediaPriv* media = g_slice_new0(NfcNdefMediaPriv);

    media->pub.data.size = rec->payload.size;
    media->pub.data.bytes = media->data = gutil_memdup
        (rec->payload.bytes, rec->payload.size);
    media->pub.type = media->type = g_strndup
        ((char*)rec->type.bytes, rec->type.size);
    return media;
}

static
NfcNdefRec*
nfc_ndef_rec_sp_append_well_known(
    NfcNdefRec* last,
    const GUtilData* type,
    const GUtilData* data)
{
    NfcNdefRec* rec = nfc_ndef_rec_new_well_known(THIS_TYPE,
        NFC_NDEF_RTD_UNKNOWN, type, data);
    nfc_ndef_rec_clear_flags(rec, NFC_NDEF_REC_FLAG_FIRST);
    nfc_ndef_rec_clear_flags(last, NFC_NDEF_REC_FLAG_LAST);
    last->next = rec;
    return rec;
}

static
GBytes*
nfc_ndef_rec_sp_payload_new(
    NfcNdefRecSpPriv* priv,
    const char* uri,
    const char* title,
    const char* lang,
    const char* type,
    guint size,
    NFC_NDEF_SP_ACT act,
    const NfcNdefMedia* icon)
{
    GByteArray* buf = g_byte_array_new();
    NfcNdefRecU* rec_U = nfc_ndef_rec_u_new(uri);
    NfcNdefRec* last = &rec_U->rec;
    NfcNdefRec* rec_T;
    NfcNdefRec* rec_act;
    NfcNdefRec* rec_s;
    NfcNdefRec* rec_t;
    NfcNdefRec* rec_icon;

    priv->uri = nfc_ndef_rec_u_steal_uri(rec_U);
    if (title) {
        NfcNdefRecT* trec = nfc_ndef_rec_t_new(title, lang);

        rec_T = &trec->rec;
        priv->title = nfc_ndef_rec_t_steal_text(trec);
        priv->lang = nfc_ndef_rec_t_steal_lang(trec);
        nfc_ndef_rec_clear_flags(rec_T, NFC_NDEF_REC_FLAG_FIRST);
        nfc_ndef_rec_clear_flags(last, NFC_NDEF_REC_FLAG_LAST);
        last->next = rec_T;
        last = rec_T;
    } else {
        rec_T = NULL;
    }
    if (act != NFC_NDEF_SP_ACT_DEFAULT) {
        const guint8 value = (guint8)act;
        GUtilData data;

        data.bytes = &value;
        data.size = sizeof(value);
        rec_act = last = nfc_ndef_rec_sp_append_well_known(last,
            &nfc_ndef_rec_sp_type_act, &data);
    } else {
        rec_act = NULL;
    }
    if (size) {
        const guint32 value = GUINT32_TO_BE(size);
        GUtilData data;

        data.bytes = (guint8*)&value;
        data.size = sizeof(value);
        rec_s = last = nfc_ndef_rec_sp_append_well_known(last,
            &nfc_ndef_rec_sp_type_s, &data);
    } else {
        rec_s = NULL;
    }
    if (type) {
        GUtilData data;

        data.bytes = (guint8*)type;
        data.size = strlen(type);
        priv->type = gutil_memdup(type, data.size + 1);
        rec_t = last = nfc_ndef_rec_sp_append_well_known(last,
            &nfc_ndef_rec_sp_type_t, &data);
    } else {
        rec_t = NULL;
    }
    if (icon && icon->type) {
        GUtilData media_type;

        media_type.bytes = (guint8*)icon->type;
        media_type.size = strlen(icon->type);
        rec_icon = nfc_ndef_rec_new_mediatype(&media_type, &icon->data);
        priv->icon = nfc_ndef_rec_sp_media_new(rec_icon);
        nfc_ndef_rec_clear_flags(rec_icon, NFC_NDEF_REC_FLAG_FIRST);
        nfc_ndef_rec_clear_flags(last, NFC_NDEF_REC_FLAG_LAST);
        last->next = rec_icon;
        last = rec_icon;
    } else {
        rec_icon = NULL;
    }

    g_byte_array_append(buf, rec_U->rec.raw.bytes, rec_U->rec.raw.size);
    if (rec_T) {
        g_byte_array_append(buf, rec_T->raw.bytes, rec_T->raw.size);
    }
    if (rec_act) {
        g_byte_array_append(buf, rec_act->raw.bytes, rec_act->raw.size);
    }
    if (rec_s) {
        g_byte_array_append(buf, rec_s->raw.bytes, rec_s->raw.size);
    }
    if (rec_t) {
        g_byte_array_append(buf, rec_t->raw.bytes, rec_t->raw.size);
    }
    if (rec_icon) {
        g_byte_array_append(buf, rec_icon->raw.bytes, rec_icon->raw.size);
    }

    nfc_ndef_rec_unref(&rec_U->rec);
    return g_byte_array_free_to_bytes(buf);
}

static
gboolean
nfc_ndef_rec_sp_parse(
    NfcNdefRecSp* self)
{
    /* The content of a Smart Poster payload is an NDEF message */
    NfcNdefRec* content = nfc_ndef_rec_new(&self->rec.payload);
    NfcNdefRecSpPriv* priv = self->priv;
    NfcLanguage* lang = NULL;
    NfcNdefRecU* uri = NULL;
    NfcNdefRec* type = NULL;
    NfcNdefRec* icon = NULL;
    GSList* title = NULL;
    NfcNdefRec* ndef;
    gboolean ok = FALSE;

    /* Examine the content */
    for (ndef = content; ndef; ndef = ndef->next) {
        if (NFC_IS_NDEF_REC_U(ndef)) {
            /* 3.3.1 The URI Record */
            if (uri) {
                /* There MUST NOT be more than one URI record */
                GWARN("SmartPoster NDEF contains multiple URI records");
                ok = FALSE;
                break;
            } else {
                uri = NFC_NDEF_REC_U(ndef);
                ok = TRUE;
            }
        } else if (NFC_IS_NDEF_REC_T(ndef)) {
            /* 3.3.2 The Title Record */
            if (title) {
                /* More than one title - need to choose */
                if (!lang) {
                    lang = nfc_system_language();
                }
                if (lang) {
                    title = g_slist_insert_sorted_with_data(title, ndef,
                        nfc_ndef_rec_t_lang_compare, lang);
                } else {
                    title = g_slist_append(title, ndef);
                }
            } else {
                /* First title */
                title = g_slist_append(NULL, ndef);
            }
        } else if (ndef->tnf == NFC_NDEF_TNF_MEDIA_TYPE) {
            static const GUtilData image = { (const guint8*) "image/", 6 };
            static const GUtilData video = { (const guint8*) "video/", 6 };

            if (ndef->payload.size > 0 && !icon &&
                nfc_ndef_valid_mediatype(&ndef->type, FALSE) &&
                (gutil_data_has_prefix(&ndef->type, &image) ||
                 gutil_data_has_prefix(&ndef->type, &video))) {
                /* 3.3.4 The Icon Record */
                icon = ndef;
            }
        } else if (ndef->tnf == NFC_NDEF_TNF_WELL_KNOWN) {
            if (gutil_data_equal(&ndef->type, &nfc_ndef_rec_sp_type_act)) {
                /* 3.3.3 The Recommended Action Record */
                if (ndef->payload.size == 1 &&
                    self->act == NFC_NDEF_SP_ACT_DEFAULT) {
                    switch (ndef->payload.bytes[0]) {
                    /* Table 2. Action Record Values */
                    case 0: self->act = NFC_NDEF_SP_ACT_OPEN; break;
                    case 1: self->act = NFC_NDEF_SP_ACT_SAVE; break;
                    case 2: self->act = NFC_NDEF_SP_ACT_EDIT; break;
                    default:
                        GWARN("Unsupport SmartPoster action %u", (guint)
                            ndef->payload.bytes[0]);
                        break;
                    }
                }
            } else if (gutil_data_equal(&ndef->type, &nfc_ndef_rec_sp_type_s)) {
                /* 3.3.5 The Size Record */
                if (ndef->payload.size == 4 && !self->size) {
                    /* Table 3. The Size Record Layout */
                    self->size =
                        ((((guint32)ndef->payload.bytes[0]) << 24) |
                         (((guint32)ndef->payload.bytes[1]) << 16) |
                         (((guint32)ndef->payload.bytes[2]) << 8) |
                          ((guint32)ndef->payload.bytes[3]));
                }
            } else if (gutil_data_equal(&ndef->type, &nfc_ndef_rec_sp_type_t)) {
                /* 3.3.6 The Type Record */
                if (!type && nfc_ndef_valid_mediatype(&ndef->payload, FALSE)) {
                    type = ndef;
                }
            } else {
                GWARN("Unsupported SmartPoster NDEF record \"%.*s\"", (int)
                    ndef->type.size, ndef->type.bytes);
            }
        } else {
            GWARN("Unsupported SmartPoster NDEF record");
        }
    }

    /* URI record is the only required one. */
    if (uri) {
        /* ok is FALSE if more than one URI record is found. */
        if (ok) {
            self->uri = priv->uri = nfc_ndef_rec_u_steal_uri(uri);
            if (title) {
                NfcNdefRecT* trec = NFC_NDEF_REC_T(title->data);

                self->lang = priv->lang = nfc_ndef_rec_t_steal_lang(trec);
                self->title = priv->title = nfc_ndef_rec_t_steal_text(trec);
            }
            if (type) {
                self->type = priv->type = g_strndup
                    ((char*)type->payload.bytes, type->payload.size);
            }
            if (icon) {
                NfcNdefMediaPriv* media = nfc_ndef_rec_sp_media_new(icon);

                self->icon = &media->pub;
                priv->icon = media;
            }
        }
    } else {
        GWARN("SmartPoster NDEF is missing URI record");
    }

    g_free(lang);
    g_slist_free(title);
    nfc_ndef_rec_unref(content);
    return ok;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcNdefRecSp*
nfc_ndef_rec_sp_new_from_data(
    const NfcNdefData* ndef)
{
    GUtilData payload;

    if (nfc_ndef_payload(ndef, &payload)) {
        NfcNdefRecSp* self = g_object_new(THIS_TYPE, NULL);
        NfcNdefRec* rec = &self->rec;

        nfc_ndef_rec_initialize(rec, NFC_NDEF_RTD_SMART_POSTER, ndef);
        if (nfc_ndef_rec_sp_parse(self)) {
            return self;
        }
        nfc_ndef_rec_unref(rec);
    }
    return NULL;
}

NfcNdefRecSp*
nfc_ndef_rec_sp_new(
    const char* uri,
    const char* title,
    const char* lang,
    const char* type,
    guint size,
    NFC_NDEF_SP_ACT act,
    const NfcNdefMedia* icon)  /* Since 1.0.18 */
{
    if (G_LIKELY(uri)) {
        GBytes* payload_bytes;
        GUtilData payload;
        NfcNdefRecSpPriv priv;
        NfcNdefRecSp* self;

        memset(&priv, 0, sizeof(priv));
        payload_bytes = nfc_ndef_rec_sp_payload_new(&priv, uri,
            title, lang, type, size, act, icon);
        self = THIS(nfc_ndef_rec_new_well_known(THIS_TYPE,
            NFC_NDEF_RTD_SMART_POSTER, &nfc_ndef_rec_type_sp,
            gutil_data_from_bytes(&payload, payload_bytes)));

        *(self->priv) = priv;
        self->uri = priv.uri;
        self->title = priv.title;
        self->lang = priv.lang;
        self->type = priv.type;
        self->size = size;
        self->act = act;
        if (priv.icon) {
            self->icon = &priv.icon->pub;
        }
        g_bytes_unref(payload_bytes);
        return self;
    }
    return NULL;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_ndef_rec_sp_init(
    NfcNdefRecSp* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcNdefRecSpPriv);
    self->act = NFC_NDEF_SP_ACT_DEFAULT;
}

static
void
nfc_ndef_rec_sp_finalize(
    GObject* object)
{
    NfcNdefRecSp* self = THIS(object);
    NfcNdefRecSpPriv* priv = self->priv;
    NfcNdefMediaPriv* icon = priv->icon;

    g_free(priv->uri);
    g_free(priv->title);
    g_free(priv->lang);
    g_free(priv->type);
    if (icon) {
        g_free(icon->type);
        g_free(icon->data);
        g_slice_free1(sizeof(*icon), icon);
    }
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_ndef_rec_sp_class_init(
    NfcNdefRecSpClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcNdefRecSpPriv));
    G_OBJECT_CLASS(klass)->finalize = nfc_ndef_rec_sp_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
