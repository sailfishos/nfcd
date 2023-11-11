/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2021 Jolla Ltd.
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

#ifndef NFC_NDEF_H
#define NFC_NDEF_H

#include "nfc_types.h"

/*
 * This API exists for backward compatibility only. Since version 1.1.19
 * all these calls are forwarded to libnfcdef. New plugins should directly
 * use libnfcdef API.
 */

#include <nfcdef.h>

G_BEGIN_DECLS

typedef struct nfc_ndef_media NfcNdefMedia;
typedef struct nfc_ndef_rec_Hc NfcNdefRecHc;
typedef struct nfc_ndef_rec_hr NfcNdefRecHr;
typedef struct nfc_ndef_rec_hs NfcNdefRecHs;
typedef struct nfc_ndef_rec_sp NfcNdefRecSp;
typedef struct nfc_ndef_rec_t NfcNdefRecT;
typedef struct nfc_ndef_rec_u NfcNdefRecU;

typedef enum nfc_ndef_rec_flags NFC_NDEF_REC_FLAGS;
#define NFC_NDEF_REC_FLAGS_NONE NDEF_REC_FLAGS_NONE
#define NFC_NDEF_REC_FLAG_FIRST NDEF_REC_FLAG_FIRST
#define NFC_NDEF_REC_FLAG_LAST NDEF_REC_FLAG_LAST

typedef enum nfc_ndef_rtd NFC_NDEF_RTD;
#define NFC_NDEF_RTD_UNKNOWN NDEF_RTD_UNKNOWN
#define NFC_NDEF_RTD_URI NDEF_RTD_URI
#define NFC_NDEF_RTD_TEXT NDEF_RTD_TEXT
#define NFC_NDEF_RTD_SMART_POSTER  NDEF_RTD_SMART_POSTER

typedef enum nfc_ndef_tnf NFC_NDEF_TNF;
#define NFC_NDEF_TNF_EMPTY NDEF_TNF_EMPTY
#define NFC_NDEF_TNF_WELL_KNOWN NDEF_TNF_WELL_KNOWN
#define NFC_NDEF_TNF_MEDIA_TYPE NDEF_TNF_MEDIA_TYPE
#define NFC_NDEF_TNF_ABSOLUTE_URI NDEF_TNF_ABSOLUTE_URI
#define NFC_NDEF_TNF_EXTERNAL NDEF_TNF_EXTERNAL
#define NFC_NDEF_TNF_MAX NDEF_TNF_MAX

G_DEPRECATED_FOR(ndef_rec_get_type)
GType nfc_ndef_rec_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_NDEF_REC (nfc_ndef_rec_get_type())
#define NFC_NDEF_REC(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        NFC_TYPE_NDEF_REC, NfcNdefRec))

G_DEPRECATED_FOR(ndef_rec_new)
NfcNdefRec*
nfc_ndef_rec_new(
    const GUtilData* block)
    NFCD_EXPORT;

G_DEPRECATED_FOR(ndef_rec_new_from_tlv)
NfcNdefRec*
nfc_ndef_rec_new_tlv(
    const GUtilData* tlv)
    NFCD_EXPORT;

G_DEPRECATED_FOR(ndef_rec_new_mediatype)
NfcNdefRec*
nfc_ndef_rec_new_mediatype(
    const GUtilData* type,
    const GUtilData* payload) /* Since 1.1.18 */
    NFCD_EXPORT;

G_DEPRECATED_FOR(ndef_rec_ref)
NfcNdefRec*
nfc_ndef_rec_ref(
    NfcNdefRec* rec)
    NFCD_EXPORT;

G_DEPRECATED_FOR(ndef_rec_unref)
void
nfc_ndef_rec_unref(
    NfcNdefRec* rec)
    NFCD_EXPORT;

/* URI */

G_DEPRECATED_FOR(ndef_rec_u_get_type)
GType nfc_ndef_rec_u_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_NDEF_REC_U NDEF_TYPE_REC_U
#define NFC_NDEF_REC_U(obj) NDEF_REC_U(obj)
#define NFC_IS_NDEF_REC_U(obj) NDEF_IS_REC_U(obj)

G_DEPRECATED_FOR(ndef_rec_u_new)
NfcNdefRecU*
nfc_ndef_rec_u_new(
    const char* uri)
    NFCD_EXPORT;

/* Text */

G_DEPRECATED_FOR(ndef_rec_t_get_type)
GType nfc_ndef_rec_t_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_NDEF_REC_T NDEF_TYPE_REC_T
#define NFC_NDEF_REC_T(obj) NDEF_REC_T(obj)
#define NFC_IS_NDEF_REC_T(obj) NDEF_IS_REC_T(obj)

typedef enum nfc_ndef_rec_t_enc NFC_NDEF_REC_T_ENC;
#define NFC_NDEF_REC_T_ENC_UTF8 NDEF_REC_T_ENC_UTF8
#define NFC_NDEF_REC_T_ENC_UTF16BE NDEF_REC_T_ENC_UTF16BE
#define NFC_NDEF_REC_T_ENC_UTF16LE NDEF_REC_T_ENC_UTF16LE

typedef enum nfc_lang_match NFC_LANG_MATCH; /* Since 1.0.15 */
#define NFC_LANG_MATCH_NONE NDEF_LANG_MATCH_NONE
#define NFC_LANG_MATCH_TERRITORY NDEF_LANG_MATCH_TERRITORY
#define NFC_LANG_MATCH_LANGUAGE NDEF_LANG_MATCH_LANGUAGE
#define NFC_LANG_MATCH_FULL NDEF_LANG_MATCH_FULL

G_DEPRECATED_FOR(ndef_rec_t_new_enc)
NfcNdefRecT*
nfc_ndef_rec_t_new_enc(
    const char* text,
    const char* lang,
    NFC_NDEF_REC_T_ENC enc)
    NFCD_EXPORT;

#define nfc_ndef_rec_t_new(text, lang) \
    nfc_ndef_rec_t_new_enc(text, lang, NDEF_REC_T_ENC_UTF8)

G_DEPRECATED_FOR(ndef_rec_t_lang_match)
NFC_LANG_MATCH
nfc_ndef_rec_t_lang_match(
    NfcNdefRecT* rec,
    const NfcLanguage* lang) /* Since 1.0.15 */
    NFCD_EXPORT;

G_DEPRECATED_FOR(ndef_rec_t_lang_compare)
gint
nfc_ndef_rec_t_lang_compare(
    gconstpointer a,   /* NfcNdefRecT* */
    gconstpointer b,   /* NfcNdefRecT* */
    gpointer user_data /* NfcLanguage* */) /* Since 1.0.18 */
    NFCD_EXPORT;

/* Smart poster */

typedef enum nfc_ndef_sp_act NFC_NDEF_SP_ACT;
#define  NFC_NDEF_SP_ACT_DEFAULT NDEF_SP_ACT_DEFAULT
#define  NFC_NDEF_SP_ACT_OPEN NDEF_SP_ACT_OPEN
#define  NFC_NDEF_SP_ACT_SAVE NDEF_SP_ACT_SAVE
#define  NFC_NDEF_SP_ACT_EDIT NDEF_SP_ACT_EDIT

G_DEPRECATED_FOR(ndef_rec_sp_get_type)
GType nfc_ndef_rec_sp_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_NDEF_REC_SP NDEF_TYPE_REC_SP
#define NFC_NDEF_REC_SP(obj) NDEF_REC_SP(obj)
#define NFC_IS_NDEF_REC_SP(obj) NDEF_IS_REC_SP(obj)

G_DEPRECATED_FOR()
NfcNdefRecSp*
nfc_ndef_rec_sp_new(
    const char* uri,
    const char* title,
    const char* lang,
    const char* type,
    guint size,
    NFC_NDEF_SP_ACT act,
    const NfcNdefMedia* icon) /* Since 1.0.18 */
    NFCD_EXPORT;

/* Utilities */

G_DEPRECATED_FOR(ndef_valid_mediatype)
gboolean
nfc_ndef_valid_mediatype(
    const GUtilData* type,
    gboolean wildcard) /* Since 1.0.18 */
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_NDEF_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
