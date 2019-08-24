/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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

#ifndef NFC_NDEF_H
#define NFC_NDEF_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum nfc_ndef_rec_flags {
    NFC_NDEF_REC_FLAGS_NONE = 0x00,
    NFC_NDEF_REC_FLAG_FIRST = 0x01,     /* MB */
    NFC_NDEF_REC_FLAG_LAST = 0x02       /* ME */
} NFC_NDEF_REC_FLAGS;

/* Known record types (RTD = Record Type Definition) */
typedef enum nfc_ndef_rtd {
    NFC_NDEF_RTD_UNKNOWN,
    NFC_NDEF_RTD_URI,                   /* "U" */
    NFC_NDEF_RTD_TEXT,                  /* "T" */
    NFC_NDEF_RTD_SMART_POSTER           /* "Sp" */
} NFC_NDEF_RTD;

/* TNF = Type name format */
typedef enum nfc_ndef_tnf {
    NFC_NDEF_TNF_EMPTY,
    NFC_NDEF_TNF_WELL_KNOWN,
    NFC_NDEF_TNF_MEDIA_TYPE,
    NFC_NDEF_TNF_ABSOLUTE_URI,
    NFC_NDEF_TNF_EXTERNAL
} NFC_NDEF_TNF;

#define NFC_NDEF_TNF_MAX NFC_NDEF_TNF_EXTERNAL

typedef struct nfc_ndef_rec_priv NfcNdefRecPriv;

struct nfc_ndef_rec {
    GObject object;
    NfcNdefRecPriv* priv;
    NfcNdefRec* next;
    NFC_NDEF_TNF tnf;
    NFC_NDEF_RTD rtd;
    NFC_NDEF_REC_FLAGS flags;
    GUtilData raw;
    GUtilData type;
    GUtilData id;
    GUtilData payload;
};

GType nfc_ndef_rec_get_type(void);
#define NFC_TYPE_NDEF_REC (nfc_ndef_rec_get_type())
#define NFC_NDEF_REC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_NDEF_REC, NfcNdefRec))

NfcNdefRec*
nfc_ndef_rec_new(
   const GUtilData* block);

NfcNdefRec*
nfc_ndef_rec_new_tlv(
   const GUtilData* tlv);

NfcNdefRec*
nfc_ndef_rec_ref(
    NfcNdefRec* rec);

void
nfc_ndef_rec_unref(
    NfcNdefRec* rec);

/* URI */

typedef struct nfc_ndef_rec_u_priv NfcNdefRecUPriv;

typedef struct nfc_ndef_rec_u {
    NfcNdefRec rec;
    NfcNdefRecUPriv* priv;
    const char* uri;
} NfcNdefRecU;

GType nfc_ndef_rec_u_get_type(void);
#define NFC_TYPE_NDEF_REC_U (nfc_ndef_rec_u_get_type())
#define NFC_NDEF_REC_U(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_NDEF_REC_U, NfcNdefRecU))
#define NFC_IS_NDEF_REC_U(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, \
        NFC_TYPE_NDEF_REC_U)

NfcNdefRecU*
nfc_ndef_rec_u_new(
    const char* uri);

/* Text */

typedef struct nfc_ndef_rec_t_priv NfcNdefRecTPriv;

typedef struct nfc_ndef_rec_t {
    NfcNdefRec rec;
    NfcNdefRecTPriv* priv;
    const char* lang;
    const char* text;
} NfcNdefRecT;

GType nfc_ndef_rec_t_get_type(void);
#define NFC_TYPE_NDEF_REC_T (nfc_ndef_rec_t_get_type())
#define NFC_NDEF_REC_T(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_NDEF_REC_T, NfcNdefRecT))
#define NFC_IS_NDEF_REC_T(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, \
        NFC_TYPE_NDEF_REC_T)

typedef enum nfc_ndef_rec_t_enc {
    NFC_NDEF_REC_T_ENC_UTF8,
    NFC_NDEF_REC_T_ENC_UTF16BE,
    NFC_NDEF_REC_T_ENC_UTF16LE
} NFC_NDEF_REC_T_ENC;

typedef enum nfc_lang_match {
    NFC_LANG_MATCH_NONE = 0x00,
    NFC_LANG_MATCH_TERRITORY = 0x01,
    NFC_LANG_MATCH_LANGUAGE = 0x02,
    NFC_LANG_MATCH_FULL = NFC_LANG_MATCH_LANGUAGE | NFC_LANG_MATCH_TERRITORY
} NFC_LANG_MATCH; /* Since 1.0.15 */

NfcNdefRecT*
nfc_ndef_rec_t_new_enc(
    const char* text,
    const char* lang,
    NFC_NDEF_REC_T_ENC enc);

#define nfc_ndef_rec_t_new(text, lang) \
    nfc_ndef_rec_t_new_enc(text, lang, NFC_NDEF_REC_T_ENC_UTF8)

NFC_LANG_MATCH
nfc_ndef_rec_t_lang_match(
    NfcNdefRecT* rec,
    const NfcLanguage* lang); /* Since 1.0.15 */

gint
nfc_ndef_rec_t_lang_compare(
    gconstpointer a,   /* NfcNdefRecT* */
    gconstpointer b,   /* NfcNdefRecT* */
    gpointer user_data /* NfcLanguage* */); /* Since 1.0.18 */

/* Smart poster */

typedef enum nfc_ndef_sp_act {
    NFC_NDEF_SP_ACT_DEFAULT = -1, /* No action record */
    NFC_NDEF_SP_ACT_OPEN,         /* Perform the action */
    NFC_NDEF_SP_ACT_SAVE,         /* Save for later */
    NFC_NDEF_SP_ACT_EDIT          /* Open for editing */
} NFC_NDEF_SP_ACT;

typedef struct nfc_ndef_rec_sp_priv NfcNdefRecSpPriv;

typedef struct nfc_ndef_media {
    GUtilData data;
    const char* type;
} NfcNdefMedia;

typedef struct nfc_ndef_rec_sp {
    NfcNdefRec rec;
    NfcNdefRecSpPriv* priv;
    const char* uri;
    const char* title;
    const char* lang;
    const char* type;
    guint size;
    NFC_NDEF_SP_ACT act;
    const NfcNdefMedia* icon;
} NfcNdefRecSp;

GType nfc_ndef_rec_sp_get_type(void);
#define NFC_TYPE_NDEF_REC_SP (nfc_ndef_rec_sp_get_type())
#define NFC_NDEF_REC_SP(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_NDEF_REC_SP, NfcNdefRecSp))
#define NFC_IS_NDEF_REC_SP(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, \
        NFC_TYPE_NDEF_REC_SP)

NfcNdefRecSp*
nfc_ndef_rec_sp_new(
    const char* uri,
    const char* title,
    const char* lang,
    const char* type,
    guint size,
    NFC_NDEF_SP_ACT act,
    const NfcNdefMedia* icon); /* Since 1.0.18 */

/* Utilities */

gboolean
nfc_ndef_valid_mediatype(
    const GUtilData* type,
    gboolean wildcard); /* Since 1.0.18 */

/* These are not yet implemented: */

typedef struct nfc_ndef_rec_hs NfcNdefRecHs;  /* Handover select */
typedef struct nfc_ndef_rec_hr NfcNdefRecHr;  /* Handover request */
typedef struct nfc_ndef_rec_Hc NfcNdefRecHc;  /* Handover carrier */

G_END_DECLS

#endif /* NFC_NDEF_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
