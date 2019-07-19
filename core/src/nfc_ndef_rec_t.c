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
 *   2. Neither the names of the copyright holders nor the names of its
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

#include "nfc_ndef_p.h"
#include "nfc_util.h"
#include "nfc_system.h"
#include "nfc_log.h"

#include <gutil_misc.h>

/* NFCForum-TS-RTD_TEXT_1.0 */

struct nfc_ndef_rec_t_priv {
    char* lang;
    char* text;
};

typedef NfcNdefRecClass NfcNdefRecTClass;
G_DEFINE_TYPE(NfcNdefRecT, nfc_ndef_rec_t, NFC_TYPE_NDEF_REC)

const GUtilData nfc_ndef_rec_type_t = { (const guint8*) "T", 1 };

#define STATUS_LANG_LEN_MASK (0x3f)
#define STATUS_ENC_UTF16 (0x80) /* Otherwise UTF-8 */

static const char ENC_UTF8[] = "UTF-8";
static const char ENC_UTF16_LE[] = "UTF-16LE";
static const char ENC_UTF16_BE[] = "UTF-16BE";

/* UTF-16 Byte Order Marks */
static const guint8 UTF16_BOM_LE[] = {0xff, 0xfe};
static const guint8 UTF16_BOM_BE[] = {0xfe, 0xff};

static
GBytes*
nfc_ndef_rec_t_build(
    const char* text,
    const char* lang,
    NFC_NDEF_REC_T_ENC enc)
{
    const guint8 lang_len = strlen(lang);
    const gsize text_len = strlen(text);
    const guint8 status_byte = (lang_len & STATUS_LANG_LEN_MASK) |
        ((enc == NFC_NDEF_REC_T_ENC_UTF8) ? 0 : STATUS_ENC_UTF16);
    const guint8* bom = NULL;
    const void* enc_text = NULL;
    void* enc_text_tmp = NULL;
    gsize enc_text_len;
    gsize bom_len = 0;
    GError* err = NULL;

    switch (enc) {
    case NFC_NDEF_REC_T_ENC_UTF8:
        enc_text = text;
        enc_text_len = text_len;
        break;
    case NFC_NDEF_REC_T_ENC_UTF16BE:
         enc_text = enc_text_tmp = g_convert(text, text_len, ENC_UTF16_BE,
            ENC_UTF8, NULL, &enc_text_len, &err);
         break;
    case NFC_NDEF_REC_T_ENC_UTF16LE:
        bom = UTF16_BOM_LE;
        bom_len = sizeof(UTF16_BOM_LE);
        enc_text = enc_text_tmp = g_convert(text, text_len, ENC_UTF16_LE,
            ENC_UTF8, NULL, &enc_text_len, &err);
        break;
    }

    if (enc_text) {
        GByteArray* buf = g_byte_array_sized_new(1 + lang_len + bom_len +
            enc_text_len);

        g_byte_array_append(buf, &status_byte, 1);
        g_byte_array_append(buf, (const guint8*)lang, lang_len);
        if (bom) g_byte_array_append(buf, bom, bom_len);
        g_byte_array_append(buf, enc_text, enc_text_len);
        g_free(enc_text_tmp);
        return g_byte_array_free_to_bytes(buf);
    } else {
        if (err) {
            GWARN("Failed to encode Text record: %s", err->message);
            g_error_free(err);
        } else {
            GWARN("Failed to encode Text record");
        }
        return NULL;
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcNdefRecT*
nfc_ndef_rec_t_new_from_data(
    const NfcNdefData* ndef)
{
    GUtilData payload;

    if (nfc_ndef_payload(ndef, &payload)) {
        const guint8 status_byte = payload.bytes[0];
        const guint lang_len = (status_byte & STATUS_LANG_LEN_MASK);
        const char* lang = (char*)payload.bytes + 1;

        if ((lang_len < payload.size) && /* Empty or ASCII (at least UTF-8) */
            (!lang_len || g_utf8_validate(lang, lang_len, NULL))) {
            const char* text = (char*)payload.bytes + lang_len + 1;
            const guint text_len = payload.size - lang_len - 1;
            const char* utf8;
            gsize utf8_len;
            char* utf8_buf;

            if (status_byte & STATUS_ENC_UTF16) {
                GError* err = NULL;
                if (text_len >= sizeof(UTF16_BOM_BE) &&
                    !memcmp(text, UTF16_BOM_BE, sizeof(UTF16_BOM_BE))) {
                    utf8_buf = g_convert(text + sizeof(UTF16_BOM_BE),
                        text_len - sizeof(UTF16_BOM_BE), ENC_UTF8,
                        ENC_UTF16_BE, NULL, &utf8_len, &err);
                } else if (text_len >= sizeof(UTF16_BOM_LE) &&
                    !memcmp(text, UTF16_BOM_LE, sizeof(UTF16_BOM_LE))) {
                    utf8_buf = g_convert(text + sizeof(UTF16_BOM_LE),
                        text_len - sizeof(UTF16_BOM_LE), ENC_UTF8,
                        ENC_UTF16_LE, NULL, &utf8_len, &err);
                } else {
                    /*
                     * 3.4 UTF-16 Byte Order
                     *
                     * ... If the BOM is omitted, the byte order shall be
                     * big-endian (UTF-16 BE).
                     */
                    utf8_buf = g_convert(text, text_len, ENC_UTF8,
                        ENC_UTF16_BE, NULL, &utf8_len, &err);
                }
                if (err) {
                    GWARN("Failed to decode Text record: %s", err->message);
                    g_free(utf8_buf); /* Should be NULL already */
                    g_error_free(err);
                    utf8 = NULL;
                } else {
                    utf8 = utf8_buf;
                }
            } else if (!text_len) {
                utf8 = "";
                utf8_buf = NULL;
            } else if (g_utf8_validate(text, text_len, NULL)) {
                utf8 = utf8_buf = g_strndup(text, text_len);
                utf8_len = text_len;
            } else {
                utf8 = NULL;
            }

            if (utf8) {
                NfcNdefRecT* self = g_object_new(NFC_TYPE_NDEF_REC_T, NULL);
                NfcNdefRecTPriv* priv = self->priv;

                nfc_ndef_rec_initialize(&self->rec, NFC_NDEF_RTD_TEXT, ndef);
                self->text = utf8;
                priv->text = utf8_buf;
                if (lang_len) {
                    self->lang = priv->lang = g_strndup(lang, lang_len);
                } else {
                    self->lang = "";
                }
                return self;
            }
        }
    }
    return NULL;
}

NfcNdefRecT*
nfc_ndef_rec_t_new_enc(
    const char* text,
    const char* lang,
    NFC_NDEF_REC_T_ENC enc)
{
    GBytes* payload_bytes;
    char* lang_tmp = NULL;
    static const char lang_default[] = "en";
    static const char text_default[] = "";

    if (!lang) {
        NfcLanguage* system = nfc_system_language();

        if (system) {
            lang = lang_tmp = system->territory ?
                g_strconcat(system->language, "-", system->territory, NULL) :
                g_strdup(system->language);
            g_free(system);
            GDEBUG("System language: %s", lang);
        }
    }

    payload_bytes = nfc_ndef_rec_t_build(text ? text : text_default,
        lang ? lang : lang_default, enc);
    if (payload_bytes) {
        GUtilData payload;
        NfcNdefRecT* self = NFC_NDEF_REC_T(nfc_ndef_rec_new_well_known
            (NFC_TYPE_NDEF_REC_T, NFC_NDEF_RTD_TEXT, &nfc_ndef_rec_type_t,
                 gutil_data_from_bytes(&payload, payload_bytes)));
        NfcNdefRecTPriv* priv = self->priv;

        /* Avoid unnecessary allocations */
        if (lang) {
            self->lang = priv->lang = (lang_tmp ? lang_tmp : g_strdup(lang));
        } else {
            self->lang = lang_default;
        }
        if (text) {
            self->text = priv->text = g_strdup(text);
        } else {
            self->text = text_default;
        }
        g_bytes_unref(payload_bytes);
        return self;
    }
    g_free(lang_tmp);
    return NULL;
}

NFC_LANG_MATCH
nfc_ndef_rec_t_lang_match(
    NfcNdefRecT* rec,
    const NfcLanguage* lang) /* Since 1.0.15 */
{
    NFC_LANG_MATCH match = NFC_LANG_MATCH_NONE;

    if (G_LIKELY(rec) && G_LIKELY(lang) && G_LIKELY(lang->language)) {
        const char* sep = strchr(rec->lang, '-');

        if (sep) {
            const gsize lang_len = sep - rec->lang;

            if (strlen(lang->language) == lang_len &&
                !g_ascii_strncasecmp(rec->lang, lang->language, lang_len)) {
                match |= NFC_LANG_MATCH_LANGUAGE;
            }
            if (lang->territory && lang->territory[0] &&
                !g_ascii_strcasecmp(sep + 1, lang->territory)) {
                match |= NFC_LANG_MATCH_TERRITORY;
            }
        } else {
            if (!g_ascii_strcasecmp(rec->lang, lang->language)) {
                match |= NFC_LANG_MATCH_LANGUAGE;
            }
        }
    }
    return match;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_ndef_rec_t_init(
    NfcNdefRecT* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NFC_TYPE_NDEF_REC_T,
        NfcNdefRecTPriv);
}

static
void
nfc_ndef_rec_t_finalize(
    GObject* object)
{
    NfcNdefRecT* self = NFC_NDEF_REC_T(object);
    NfcNdefRecTPriv* priv = self->priv;

    g_free(priv->lang);
    g_free(priv->text);
    G_OBJECT_CLASS(nfc_ndef_rec_t_parent_class)->finalize(object);
}

static
void
nfc_ndef_rec_t_class_init(
    NfcNdefRecTClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcNdefRecTPriv));
    G_OBJECT_CLASS(klass)->finalize = nfc_ndef_rec_t_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
