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

static
GVariant*
dbus_handlers_type_generic_ndef_to_variant(
    NfcNdefRec* ndef)
{
    GByteArray* buf = g_byte_array_new();
    gsize size;
    void* data;

    while (ndef) {
        g_byte_array_append(buf, ndef->raw.bytes, ndef->raw.size);
        ndef = ndef->next;
    }

    size = buf->len;
    data = g_byte_array_free(buf, FALSE);
    return g_variant_new_from_data(G_VARIANT_TYPE("ay"), data, size, TRUE,
        g_free, data);
}

static
gboolean
dbus_handlers_type_generic_supported_record(
    NfcNdefRec* ndef)
{
    return TRUE;
}

static
DBusHandlerConfig*
dbus_handlers_type_generic_new_handler_config(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    return dbus_handlers_new_handler_config(file, "Handler");
}

static
DBusListenerConfig*
dbus_handlers_type_generic_new_listener_config(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    return dbus_handlers_new_listener_config(file, "Listener");
}

static
GVariant*
dbus_handlers_type_generic_handler_args(
    NfcNdefRec* ndef)
{
    return g_variant_new ("(@ay)",
        dbus_handlers_type_generic_ndef_to_variant(ndef));
}

static
GVariant*
dbus_handlers_type_generic_listener_args(
    gboolean handled,
    NfcNdefRec* ndef)
{
    return g_variant_new ("(b@ay)", handled,
        dbus_handlers_type_generic_ndef_to_variant(ndef));
}

const DBusHandlerType dbus_handlers_type_generic = {
    .name = "generic",
    .priority = DBUS_HANDLER_PRIORITY_LOW,
    .supported_record = dbus_handlers_type_generic_supported_record,
    .new_handler_config = dbus_handlers_type_generic_new_handler_config,
    .new_listener_config = dbus_handlers_type_generic_new_listener_config,
    .free_handler_config = dbus_handlers_free_handler_config,
    .free_listener_config = dbus_handlers_free_listener_config,
    .handler_args = dbus_handlers_type_generic_handler_args,
    .listener_args = dbus_handlers_type_generic_listener_args
};

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
