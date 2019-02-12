/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Bogdan Pankovsky <b.pankovsky@omprussia.ru>
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
#include "nfc_log.h"

#include <gutil_misc.h>

/* NFCForum-TS-RTD_TEXT_1.0 */
/* standard field structure
status byte*1 byte* encoding*n - bytes* text*
*/
struct nfc_ndef_rec_t_priv {
    char* language;
    char* text;
};

typedef NfcNdefRecClass NfcNdefRecTClass;
G_DEFINE_TYPE(NfcNdefRecT, nfc_ndef_rec_t, NFC_TYPE_NDEF_REC)

const GUtilData nfc_ndef_rec_type_t = { (const guint8*) "T", 1 };

#define LANGUAGE_LENGTH_MASK 0x3f
#define ENCODING_UTF16_BIT 0x80 /* 0 - utf-8;  1 - utf-16*/

static
GBytes*
nfc_ndef_rec_t_build(
  const char* text,
  const char* language,
  NFC_NDEF_REC_T_ENCODING enc)
{
    GByteArray* buf = g_byte_array_new();
    const guint8 language_length = strlen(language);
    const guint8 text_length = strlen(text);
    GError* err = NULL;
    guint8 status_byte = language_length & LANGUAGE_LENGTH_MASK; /* encoding by default utf-8 */
    status_byte = status_byte | ( enc == NFC_NDEF_REC_T_ENCODING_UTF8 ? 0 : ENCODING_UTF16_BIT );

    g_byte_array_append(buf, &status_byte, 1);
    g_byte_array_append(buf, (const guint8*)language, language_length);

    if (enc == NFC_NDEF_REC_T_ENCODING_UTF16BE ) {
        gsize enc_length;
        gchar* text_encoded;
        text_encoded = g_convert(
                          text,
                          text_length,
                          "UTF-16BE",
                          "UTF-8",
                          NULL,
                          &enc_length,
                          &err);

        g_byte_array_append(buf, (const guint8*) text_encoded, enc_length);
        g_free(text_encoded);
    } else if (enc == NFC_NDEF_REC_T_ENCODING_UTF16LE) {
        static const guint8 BOM[2] = {0xff , 0xfe};
        gsize enc_length;
        gchar* text_encoded;
        text_encoded = g_convert(
                          text,
                          text_length,
                          "UTF-16LE",
                          "UTF-8",
                          NULL,
                          &enc_length,
                          &err);

        g_byte_array_append(buf, BOM , 2); /* BOM */
        g_byte_array_append(buf, (const guint8*) text_encoded, enc_length);
        g_free(text_encoded);
    } else if (enc == NFC_NDEF_REC_T_ENCODING_UTF8) {
        g_byte_array_append(buf, (const guint8*) text, text_length);
    } else {
        GDEBUG ("Unable to encode tag: Wrong encoding\n");
    }

    if (err) {
        /* Report error to user, and free error */
        GDEBUG ("Unable to encode tag: %s\n", err->message);
        g_error_free (err);
    }

    return g_byte_array_free_to_bytes(buf);
}

static
gchar*
nfc_ndef_rec_t_convert_to_utf8(
  const GUtilData* payload)
{
    /* nfc_ndef_payload() makes sure that payload length > 0 */
    const guint8 status_byte = payload->bytes[0];
    const gsize language_length = status_byte & LANGUAGE_LENGTH_MASK;
    const glong len = payload->size - 1 - language_length;

    gchar *parsedtext = NULL;
    const gchar* text = (gchar*)(payload->bytes + 1 + language_length);

    if (!(status_byte & ENCODING_UTF16_BIT)) {
        if (g_utf8_validate(text, len , NULL)) {
            parsedtext = g_strndup(text, len);
        }
    } else {
        /* convert to UTF8 */
        GError* err = NULL;
        const guint8 bom_byte_first = payload->bytes[1 + language_length];
        const guint8 bom_byte_second = payload->bytes[2 + language_length];

        if (bom_byte_first == 0xff && bom_byte_second == 0xfe) {
            parsedtext = g_convert(
                        text + 2,
                        len - 4,
                        "UTF-8",
                        "UTF-16LE",
                        NULL,
                        NULL,
                        &err);
        } else if (bom_byte_first == 0xfe && bom_byte_second == 0xff) {
            parsedtext = g_convert(
                        text + 2,
                        len - 4,
                        "UTF-8",
                        "UTF-16BE",
                        NULL,
                        NULL,
                        &err);
        } else {
            parsedtext = g_convert(
                        text,
                        len,
                        "UTF-8",
                        "UTF-16BE",
                        NULL,
                        NULL,
                        &err);
        }
        if (err) {
            /* Report error to user, and free error*/
            GDEBUG ("Unable to encode tag: %s\n", err->message);
            g_error_free (err);
            g_free(parsedtext);
            parsedtext = NULL;
        }
    }

    return parsedtext;
}

static
NfcNdefRecT*
nfc_ndef_rec_t_parse(
    const GUtilData* payload)
{
    NfcNdefRecT* self = g_object_new(NFC_TYPE_NDEF_REC_T, NULL);
    NfcNdefRecTPriv* priv = self->priv;
    guint8 len = payload->size - 1;
    gsize language_length = payload->bytes[0] & LANGUAGE_LENGTH_MASK;
    char* text = NULL;

    if (language_length < len) {
        text = nfc_ndef_rec_t_convert_to_utf8(payload);
    }

    if (text) {
      self->text = priv->text = text;
    } else {
      self->text = priv->text = NULL;
    }

    if (language_length > 0 ) {
        gchar* parsed_language = g_strndup(
                          (const gchar*)(payload->bytes + 1),
                          language_length);
        self->language = priv->language = parsed_language;
        len -= language_length;
    } else {
        GDEBUG("Missing language");
        self->language= priv->language = NULL;
    }

    return self;
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
        NfcNdefRecT* self = nfc_ndef_rec_t_parse(&payload);
        if(self){
            nfc_ndef_rec_initialize(&self->rec, NFC_NDEF_RTD_TEXT, ndef);
            return self;
        }
    }
    return NULL;
}

NfcNdefRecT*
nfc_ndef_rec_t_new_enc(
    const char* text,
    const char* language,
    NFC_NDEF_REC_T_ENCODING enc)
{
    if (G_LIKELY(language)) {
        GUtilData payload;

        GBytes* payload_bytes = nfc_ndef_rec_t_build(text,language, enc);
        NfcNdefRecT* self = NFC_NDEF_REC_T(nfc_ndef_rec_new_well_known
            (NFC_TYPE_NDEF_REC_T, NFC_NDEF_RTD_TEXT, &nfc_ndef_rec_type_t,
                 gutil_data_from_bytes(&payload, payload_bytes)));
        NfcNdefRecTPriv* priv = self->priv;
        self->language = priv->language = g_strdup(language);
        self->text = priv->text = g_strdup(text);

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

    g_free(priv->language);
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
