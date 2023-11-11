/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2019 Jolla Ltd.
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

#include "dbus_handlers.h"

static const char dbus_handlers_type_mediatype_handler_group [] =
    "MediaType-Handler";
static const char dbus_handlers_type_mediatype_listener_group [] =
    "MediaType-Listener";
static const char dbus_handlers_type_mediatype_key[] = "MediaType";

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
        if (ndef_valid_mediatype(&type, TRUE)) {
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
gboolean
dbus_handlers_type_mediatype_supported_record(
    NfcNdefRec* ndef)
{
    return ndef->tnf == NDEF_TNF_MEDIA_TYPE &&
        ndef_valid_mediatype(&ndef->type, FALSE);
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
GVariant*
dbus_handlers_type_mediatype_ndef_payload_as_variant(
    NfcNdefRec* ndef)
{
    /* We need to hold a reference to our NfcNdefRec until newly created
     * variant is freed. */
    const GUtilData* payload = &ndef->payload;
    return g_variant_new_from_data(G_VARIANT_TYPE("ay"),
        payload->size ? payload->bytes : NULL, payload->size, TRUE,
        payload->size ? (GDestroyNotify) ndef_rec_unref : NULL,
        payload->size ? ndef_rec_ref(ndef) : NULL);
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
    .priority = DBUS_HANDLER_PRIORITY_DEFAULT,
    .buddy = &dbus_handlers_type_mediatype_exact,
    .supported_record = dbus_handlers_type_mediatype_supported_record,
    .new_handler_config = dbus_handlers_type_mediatype_wildcard_new_handler,
    .new_listener_config = dbus_handlers_type_mediatype_wildcard_new_listener,
    .free_handler_config = dbus_handlers_free_handler_config,
    .free_listener_config = dbus_handlers_free_listener_config,
    .handler_args = dbus_handlers_type_mediatype_handler_args,
    .listener_args = dbus_handlers_type_mediatype_listener_args
};

const DBusHandlerType dbus_handlers_type_mediatype_exact = {
    .name = "MediaType (exact)",
    .priority = DBUS_HANDLER_PRIORITY_DEFAULT,
    .buddy = &dbus_handlers_type_mediatype_wildcard,
    .supported_record = dbus_handlers_type_mediatype_supported_record,
    .new_handler_config = dbus_handlers_type_mediatype_exact_new_handler,
    .new_listener_config = dbus_handlers_type_mediatype_exact_new_listener,
    .free_handler_config = dbus_handlers_free_handler_config,
    .free_listener_config = dbus_handlers_free_listener_config,
    .handler_args = dbus_handlers_type_mediatype_handler_args,
    .listener_args = dbus_handlers_type_mediatype_listener_args
};

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
