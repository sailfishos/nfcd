/*
 * Copyright (C) 2019-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2019 Jolla Ltd.
 * Copyright (C) 2019 Open Mobile Platform LLC.
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

static
gboolean
dbus_handlers_type_sp_supported_record(
    NdefRec* ndef)
{
    return NDEF_IS_REC_SP(ndef);
}

static
gboolean
dbus_handlers_type_sp_match(
    GKeyFile* file,
    const char* group,
    NdefRecSp* rec)
{
    char* pattern = dbus_handlers_config_get_string(file, group, "URI");
    gboolean match = (!pattern || g_pattern_match_simple(pattern, rec->uri));

    g_free(pattern);
    return match;
}

static
DBusHandlerConfig*
dbus_handlers_type_sp_new_handler_config(
    GKeyFile* file,
    NdefRec* ndef)
{
    static const char group[] = "SmartPoster-Handler";

    return dbus_handlers_type_sp_match(file, group, NDEF_REC_SP(ndef)) ?
        dbus_handlers_new_handler_config(file, group) : NULL;
}

static
DBusListenerConfig*
dbus_handlers_type_sp_new_listener_config(
    GKeyFile* file,
    NdefRec* ndef)
{
    static const char group[] = "SmartPoster-Listener";

    return dbus_handlers_type_sp_match(file, group, NDEF_REC_SP(ndef)) ?
        dbus_handlers_new_listener_config(file, group) : NULL;
}

static
GVariant*
dbus_handlers_type_sp_handler_args(
    NdefRec* ndef)
{
    NdefRecSp* sp = NDEF_REC_SP(ndef);
    const NdefMedia* icon = sp->icon;
    const char* icon_type;
    GVariant* icon_data;

    if (icon) {
        icon_type = icon->type ? icon->type : "";
        icon_data = g_variant_new_from_data(G_VARIANT_TYPE("ay"),
            icon->data.bytes, icon->data.size, TRUE,
            g_object_unref, g_object_ref(sp));
    } else {
        icon_type = "";
        icon_data = g_variant_new_from_data(G_VARIANT_TYPE("ay"),
            NULL, 0, TRUE, NULL, NULL);
    }

    return g_variant_new("(sssui(s@ay))", sp->uri, sp->title ? sp->title : "",
        sp->type ? sp->type : "", sp->size, sp->act, icon_type, icon_data);
}

static
GVariant*
dbus_handlers_type_sp_listener_args(
    gboolean handled,
    NdefRec* ndef)
{
    NdefRecSp* sp = NDEF_REC_SP(ndef);
    const NdefMedia* icon = sp->icon;
    const char* icon_type;
    GVariant* icon_data;

    if (icon) {
        icon_type = icon->type ? icon->type : "";
        icon_data = g_variant_new_from_data(G_VARIANT_TYPE("ay"),
            icon->data.bytes, icon->data.size, TRUE,
            g_object_unref, g_object_ref(sp));
    } else {
        icon_type = "";
        icon_data = g_variant_new_from_data(G_VARIANT_TYPE("ay"),
            NULL, 0, TRUE, NULL, NULL);
    }

    return g_variant_new("(bsssui(s@ay))", handled, sp->uri,
        sp->title ? sp->title : "", sp->type ? sp->type : "",
        sp->size, sp->act, icon_type, icon_data);
}

const DBusHandlerType dbus_handlers_type_sp = {
    .name = "SmartPoster",
    .priority = DBUS_HANDLER_PRIORITY_DEFAULT,
    .supported_record = dbus_handlers_type_sp_supported_record,
    .new_handler_config = dbus_handlers_type_sp_new_handler_config,
    .new_listener_config = dbus_handlers_type_sp_new_listener_config,
    .free_handler_config = dbus_handlers_free_handler_config,
    .free_listener_config = dbus_handlers_free_listener_config,
    .handler_args = dbus_handlers_type_sp_handler_args,
    .listener_args = dbus_handlers_type_sp_listener_args
};

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
