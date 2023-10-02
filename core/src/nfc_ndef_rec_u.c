/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2020 Jolla Ltd.
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

#include <gutil_misc.h>

/* NFCForum-TS-RTD_URI_1.0 */

struct nfc_ndef_rec_u_priv {
    char* uri;
};

#define THIS(obj) NFC_NDEF_REC_U(obj)
#define THIS_TYPE NFC_TYPE_NDEF_REC_U
#define PARENT_TYPE NFC_TYPE_NDEF_REC
#define PARENT_CLASS nfc_ndef_rec_u_parent_class

typedef NfcNdefRecClass NfcNdefRecUClass;
G_DEFINE_TYPE(NfcNdefRecU, nfc_ndef_rec_u, PARENT_TYPE)

const GUtilData nfc_ndef_rec_type_u = { (const guint8*) "U", 1 };

/* Table 3 */
static const GUtilData nfc_ndef_rec_u_abbreviation_table[] = {
    /* 0x00 */ { NULL, 0 },
    /* 0x01 */ { (const guint8*) "http://www.", 11 },
    /* 0x02 */ { (const guint8*) "https://www.", 12 },
    /* 0x03 */ { (const guint8*) "http://", 7 },
    /* 0x04 */ { (const guint8*) "https://", 8 },
    /* 0x05 */ { (const guint8*) "tel:", 4 },
    /* 0x06 */ { (const guint8*) "mailto:", 7 },
    /* 0x07 */ { (const guint8*) "ftp://anonymous:anonymous@", 26 },
    /* 0x08 */ { (const guint8*) "ftp://ftp.", 10 },
    /* 0x09 */ { (const guint8*) "ftps://", 7 },
    /* 0x0A */ { (const guint8*) "sftp://", 7 },
    /* 0x0B */ { (const guint8*) "smb://", 6 },
    /* 0x0C */ { (const guint8*) "nfs://", 6 },
    /* 0x0D */ { (const guint8*) "ftp://", 6 },
    /* 0x0E */ { (const guint8*) "dav://", 6 },
    /* 0x0F */ { (const guint8*) "news:", 5 },
    /* 0x10 */ { (const guint8*) "telnet://", 9 },
    /* 0x11 */ { (const guint8*) "imap:", 5 },
    /* 0x12 */ { (const guint8*) "rtsp://", 7 },
    /* 0x13 */ { (const guint8*) "urn:", 4 },
    /* 0x14 */ { (const guint8*) "pop:", 4 },
    /* 0x15 */ { (const guint8*) "sip:", 4 },
    /* 0x16 */ { (const guint8*) "sips:", 5 },
    /* 0x17 */ { (const guint8*) "tftp:", 5 },
    /* 0x18 */ { (const guint8*) "btspp://", 8 },
    /* 0x19 */ { (const guint8*) "btl2cap://", 10 },
    /* 0x1A */ { (const guint8*) "btgoep://", 9 },
    /* 0x1B */ { (const guint8*) "tcpobex://", 10 },
    /* 0x1C */ { (const guint8*) "irdaobex://", 11 },
    /* 0x1D */ { (const guint8*) "file://", 7 },
    /* 0x1E */ { (const guint8*) "urn:epc:id:", 11 },
    /* 0x1F */ { (const guint8*) "urn:epc:tag:", 12 },
    /* 0x20 */ { (const guint8*) "urn:epc:pat:", 12 },
    /* 0x21 */ { (const guint8*) "urn:epc:raw:", 12 },
    /* 0x22 */ { (const guint8*) "urn:epc:", 8 },
    /* 0x23 */ { (const guint8*) "urn:nfc:", 7 },
};

static
GBytes*
nfc_ndef_rec_u_build(
    const char* uri)
{
    GByteArray* buf = g_byte_array_new();
    gsize len = strlen(uri);
    guint8 i;

    /* Skip the first one, the one that means "no abbreviation" */
    for (i = 1; i < G_N_ELEMENTS(nfc_ndef_rec_u_abbreviation_table); i++) {
        const GUtilData* abbr = nfc_ndef_rec_u_abbreviation_table + i;

        if (len >= abbr->size && !memcmp(uri, abbr->bytes, abbr->size)) {
            g_byte_array_append(buf, &i, 1);
            uri += abbr->size;
            len -= abbr->size;
            break;
        }
    }

    if (!buf->len) {
        /* No abbreviation */
        i = 0;
        g_byte_array_append(buf, &i, 1);
    }

    /* Append the rest */
    g_byte_array_append(buf, (const guint8*)uri, len);
    return g_byte_array_free_to_bytes(buf);
}

static
char*
nfc_ndef_rec_u_parse(
    const GUtilData* payload)
{
    /* nfc_ndef_payload() makes sure that payload length > 0 */
    const guint8 prefix_id = payload->bytes[0];

    if (prefix_id < G_N_ELEMENTS(nfc_ndef_rec_u_abbreviation_table)) {
        const GUtilData* abbr = nfc_ndef_rec_u_abbreviation_table + prefix_id;
        guint len = abbr->size + payload->size - 1;
        char* uri = g_malloc(len + 1);

        if (abbr->size) {
            memcpy(uri, abbr->bytes, abbr->size);
        }
        memcpy(uri + abbr->size, payload->bytes + 1, payload->size - 1);
        uri[len] = 0;
        return uri;
    } else {
        GDEBUG("Unknown URI Record prefix 0x02%x", prefix_id);
        return NULL;
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcNdefRecU*
nfc_ndef_rec_u_new(
    const char* uri)
{
    if (G_LIKELY(uri)) {
        GUtilData payload;
        GBytes* payload_bytes = nfc_ndef_rec_u_build(uri);
        NfcNdefRecU* self = THIS(nfc_ndef_rec_new_well_known(THIS_TYPE,
            NFC_NDEF_RTD_URI, &nfc_ndef_rec_type_u,
            gutil_data_from_bytes(&payload, payload_bytes)));
        NfcNdefRecUPriv* priv = self->priv;

        self->uri = priv->uri = g_strdup(uri);
        g_bytes_unref(payload_bytes);
        return self;
    }
    return NULL;
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

NfcNdefRecU*
nfc_ndef_rec_u_new_from_data(
    const NfcNdefData* ndef)
{
    GUtilData payload;

    if (nfc_ndef_payload(ndef, &payload)) {
        char* uri = nfc_ndef_rec_u_parse(&payload);

        if (uri) {
            NfcNdefRecU* self = g_object_new(THIS_TYPE, NULL);
            NfcNdefRecUPriv* priv = self->priv;

            nfc_ndef_rec_initialize(&self->rec, NFC_NDEF_RTD_URI, ndef);
            self->uri = priv->uri = uri;
            return self;
        }
    }
    return NULL;
}

char*
nfc_ndef_rec_u_steal_uri(
    NfcNdefRecU* self)
{
    char* uri = NULL;

    if (G_LIKELY(self)) {
        NfcNdefRecUPriv* priv = self->priv;

        uri = priv->uri;
        self->uri = priv->uri = NULL;
    }
    return uri;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_ndef_rec_u_init(
    NfcNdefRecU* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE, NfcNdefRecUPriv);
}

static
void
nfc_ndef_rec_u_finalize(
    GObject* object)
{
    NfcNdefRecU* self = THIS(object);
    NfcNdefRecUPriv* priv = self->priv;

    g_free(priv->uri);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_ndef_rec_u_class_init(
    NfcNdefRecUClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcNdefRecUPriv));
    G_OBJECT_CLASS(klass)->finalize = nfc_ndef_rec_u_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
