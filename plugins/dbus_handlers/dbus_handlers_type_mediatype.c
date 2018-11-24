/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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

#include "dbus_handlers.h"

#include <nfc_ndef.h>

static const char dbus_handlers_type_mediatype_handler_group [] =
    "MediaType-Handler";
static const char dbus_handlers_type_mediatype_listener_group [] =
    "MediaType-Listener";
static const char dbus_handlers_type_mediatype_key[] = "MediaType";

/* See RFC 2045, section 5.1 "Syntax of the Content-Type Header Field" */

static
gboolean
dbus_handlers_type_mediatype_is_token_char(
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

static
gboolean
dbus_handlers_type_mediatype_is_valid_mediatype(
    const GUtilData* type,
    gboolean wildcard)
{
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
                dbus_handlers_type_mediatype_is_token_char(type->bytes[i])) {
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
                dbus_handlers_type_mediatype_is_token_char(type->bytes[i])) {
                i++;
            }
            if (i == type->size) {
                return !wildcard;
            }
        }
    }
    return FALSE;
}

static
gboolean
dbus_handlers_type_mediatype_match_wildcard(
    GKeyFile* file,
    NfcNdefRec* ndef,
    const char* group)
{
    char* pattern = dbus_handlers_config_get_string(file, group,
        dbus_handlers_type_mediatype_key);

    if (pattern) {
        gboolean match = FALSE;
        GUtilData type;

        type.bytes = (void*)pattern;
        type.size = strlen(pattern);
        if (dbus_handlers_type_mediatype_is_valid_mediatype(&type, TRUE)) {
            GPatternSpec* spec = g_pattern_spec_new(pattern);

            match = g_pattern_match(spec, ndef->type.size,
                (const char*)ndef->type.bytes, NULL);
            g_pattern_spec_free(spec);
        }
        g_free(pattern);
        return match;
    } else {
        return TRUE;
    }
}

static
gboolean
dbus_handlers_type_mediatype_match_exact(
    GKeyFile* file,
    NfcNdefRec* ndef,
    const char* group)
{
    char* mediatype = dbus_handlers_config_get_string(file, group,
        dbus_handlers_type_mediatype_key);

    if (mediatype) {
        const gsize len = strlen(mediatype);
        const gboolean match = (ndef->type.size == len &&
            !g_ascii_strncasecmp(mediatype, (char*)ndef->type.bytes, len));

        g_free(mediatype);
        return match;
    } else {
        return FALSE;
    }
}

static
DBusHandlerConfig*
dbus_handlers_type_mediatype_new_handler(
    GKeyFile* file,
    NfcNdefRec* ndef,
    gboolean (*match)(GKeyFile* file, NfcNdefRec* ndef, const char* group),
    const char* group)
{
    if (match(file, ndef, group)) {
        return dbus_handlers_new_handler_config(file, group);
    }
    return NULL;
}

static
DBusListenerConfig*
dbus_handlers_type_mediatype_new_listener(
    GKeyFile* file,
    NfcNdefRec* ndef,
    gboolean (*match)(GKeyFile* file, NfcNdefRec* ndef, const char* group),
    const char* group)
{
    if (match(file, ndef, group)) {
        return dbus_handlers_new_listener_config(file, group);
    }
    return NULL;
}

static
DBusHandlerConfig*
dbus_handlers_type_mediatype_wildcard_new_handler(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    return dbus_handlers_type_mediatype_new_handler(file, ndef,
        dbus_handlers_type_mediatype_match_wildcard,
        dbus_handlers_type_mediatype_handler_group);
}

static
DBusListenerConfig*
dbus_handlers_type_mediatype_wildcard_new_listener(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    return dbus_handlers_type_mediatype_new_listener(file, ndef,
        dbus_handlers_type_mediatype_match_wildcard,
        dbus_handlers_type_mediatype_listener_group);
}

static
DBusHandlerConfig*
dbus_handlers_type_mediatype_exact_new_handler(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    return dbus_handlers_type_mediatype_new_handler(file, ndef,
        dbus_handlers_type_mediatype_match_exact,
        dbus_handlers_type_mediatype_handler_group);
}

static
DBusListenerConfig*
dbus_handlers_type_mediatype_exact_new_listener(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    return dbus_handlers_type_mediatype_new_listener(file, ndef,
        dbus_handlers_type_mediatype_match_exact,
        dbus_handlers_type_mediatype_listener_group);
}

static
void
dbus_handlers_type_mediatype_ndef_unref_rec(
    gpointer ndef)
{
    nfc_ndef_rec_unref((NfcNdefRec*)ndef);
}

static
GVariant*
dbus_handlers_type_mediatype_ndef_payload_as_variant(
    NfcNdefRec* ndef)
{
    /* We need to hold a reference to our NfcNdefRec until newly created
     * variant is freed. */
    const GUtilData* payload = &ndef->payload;
    return g_variant_new_from_data(G_VARIANT_TYPE("ay"),
        payload->size ? payload->bytes : NULL, payload->size, TRUE,
        payload->size ? dbus_handlers_type_mediatype_ndef_unref_rec : NULL,
        payload->size ? nfc_ndef_rec_ref(ndef) : NULL);
}

static
GVariant*
dbus_handlers_type_mediatype_handler_args(
    NfcNdefRec* ndef)
{
    char* mediatype = g_strndup((char*)ndef->type.bytes, ndef->type.size);
    GVariant* ret = g_variant_new ("(s@ay)", mediatype,
        dbus_handlers_type_mediatype_ndef_payload_as_variant(ndef));

    g_free(mediatype);
    return ret;
}

static
GVariant*
dbus_handlers_type_mediatype_listener_args(
    gboolean handled,
    NfcNdefRec* ndef)
{
    char* mediatype = g_strndup((char*)ndef->type.bytes, ndef->type.size);
    GVariant* ret = g_variant_new ("(bs@ay)", handled, mediatype,
        dbus_handlers_type_mediatype_ndef_payload_as_variant(ndef));

    g_free(mediatype);
    return ret;
}

const DBusHandlerType dbus_handlers_type_mediatype_wildcard = {
    .name = "MediaType (wildcard)",
    .new_handler_config = dbus_handlers_type_mediatype_wildcard_new_handler,
    .new_listener_config = dbus_handlers_type_mediatype_wildcard_new_listener,
    .free_handler_config = dbus_handlers_free_handler_config,
    .free_listener_config = dbus_handlers_free_listener_config,
    .handler_args = dbus_handlers_type_mediatype_handler_args,
    .listener_args = dbus_handlers_type_mediatype_listener_args
};

const DBusHandlerType dbus_handlers_type_mediatype_exact = {
    .name = "MediaType (exact)",
    .new_handler_config = dbus_handlers_type_mediatype_exact_new_handler,
    .new_listener_config = dbus_handlers_type_mediatype_exact_new_listener,
    .free_handler_config = dbus_handlers_free_handler_config,
    .free_listener_config = dbus_handlers_free_listener_config,
    .handler_args = dbus_handlers_type_mediatype_handler_args,
    .listener_args = dbus_handlers_type_mediatype_listener_args
};

gboolean
dbus_handlers_type_mediatype_record(
    NfcNdefRec* ndef)
{
    return ndef && ndef->tnf == NFC_NDEF_TNF_MEDIA_TYPE &&
        dbus_handlers_type_mediatype_is_valid_mediatype(&ndef->type, FALSE);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
