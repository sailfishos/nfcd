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

#include "dbus_handlers.h"

#include <nfc_ndef.h>

static const char dbus_handlers_type_uri_key[] = "URI";

static
DBusHandlerConfig*
dbus_handlers_type_uri_new_handler_config(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    const char* group = "URI-Handler";
    NfcNdefRecU* u = NFC_NDEF_REC_U(ndef);
    char* pattern = dbus_handlers_config_get_string(file, group,
        dbus_handlers_type_uri_key);

    if (!pattern || g_pattern_match_simple(pattern, u->uri)) {
        return dbus_handlers_new_handler_config(file, group);
    }
    return NULL;
}

static
DBusListenerConfig*
dbus_handlers_type_uri_new_listener_config(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    const char* group = "URI-Listener";
    NfcNdefRecU* u = NFC_NDEF_REC_U(ndef);
    char* pattern = dbus_handlers_config_get_string(file, group,
        dbus_handlers_type_uri_key);

    if (!pattern || g_pattern_match_simple(pattern, u->uri)) {
        return dbus_handlers_new_listener_config(file, group);
    }
    return NULL;
}

static
GVariant*
dbus_handlers_type_uri_handler_args(
    NfcNdefRec* ndef)
{
    NfcNdefRecU* u = NFC_NDEF_REC_U(ndef);

    return g_variant_new ("(s)", u->uri);
}

static
GVariant*
dbus_handlers_type_uri_listener_args(
    gboolean handled,
    NfcNdefRec* ndef)
{
    NfcNdefRecU* u = NFC_NDEF_REC_U(ndef);

    return g_variant_new ("(bs)", handled, u->uri);
}

const DBusHandlerType dbus_handlers_type_uri = {
    .name = "URI",
    .new_handler_config = dbus_handlers_type_uri_new_handler_config,
    .new_listener_config = dbus_handlers_type_uri_new_listener_config,
    .free_handler_config = dbus_handlers_free_handler_config,
    .free_listener_config = dbus_handlers_free_listener_config,
    .handler_args = dbus_handlers_type_uri_handler_args,
    .listener_args = dbus_handlers_type_uri_listener_args
};

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
