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

#include "nfc_types_p.h"
#include "nfc_ndef.h"

/* Bridge between legacy nfcd ndef API and libnfcdef */

GType
nfc_ndef_rec_get_type()
{
    return ndef_rec_get_type();
}

GType nfc_ndef_rec_u_get_type()
{
    return ndef_rec_u_get_type();
}

GType nfc_ndef_rec_t_get_type()
{
    return ndef_rec_t_get_type();
}

GType nfc_ndef_rec_sp_get_type()
{
    return ndef_rec_sp_get_type();
}

NfcNdefRec*
nfc_ndef_rec_new(
    const GUtilData* block)
{
    return ndef_rec_new(block);
}

NfcNdefRec*
nfc_ndef_rec_new_tlv(
    const GUtilData* tlv)
{
    return ndef_rec_new_from_tlv(tlv);
}

NfcNdefRec*
nfc_ndef_rec_new_mediatype(
    const GUtilData* type,
    const GUtilData* payload) /* Since 1.1.18 */
{
    return ndef_rec_new_mediatype(type, payload);
}

NfcNdefRec*
nfc_ndef_rec_ref(
    NfcNdefRec* rec)
{
    return ndef_rec_ref(rec);
}

void
nfc_ndef_rec_unref(
    NfcNdefRec* rec)
{
    ndef_rec_unref(rec);
}

NfcNdefRecU*
nfc_ndef_rec_u_new(
    const char* uri)
{
    return ndef_rec_u_new(uri);
}

NfcNdefRecT*
nfc_ndef_rec_t_new_enc(
    const char* text,
    const char* lang,
    NFC_NDEF_REC_T_ENC enc)
{
    return ndef_rec_t_new_enc(text, lang, enc);
}

NFC_LANG_MATCH
nfc_ndef_rec_t_lang_match(
    NfcNdefRecT* rec,
    const NfcLanguage* lang) /* Since 1.0.15 */
{
    return ndef_rec_t_lang_match(rec, lang);
}

gint
nfc_ndef_rec_t_lang_compare(
    gconstpointer a,
    gconstpointer b,
    gpointer user_data) /* Since 1.0.18 */
{
    return ndef_rec_t_lang_compare(a, b, user_data);
}

NfcNdefRecSp*
nfc_ndef_rec_sp_new(
    const char* uri,
    const char* title,
    const char* lang,
    const char* type,
    guint size,
    NFC_NDEF_SP_ACT act,
    const NfcNdefMedia* icon) /* Since 1.0.18 */
{
    return ndef_rec_sp_new(uri, title, lang, type, size, act, icon);
}

gboolean
nfc_ndef_valid_mediatype(
    const GUtilData* type,
    gboolean wildcard) /* Since 1.0.18 */
{
    return ndef_valid_mediatype(type, wildcard);
}

NfcLanguage*
nfc_system_language(
    void) /* Since 1.0.15 */
{
    return ndef_system_language();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
