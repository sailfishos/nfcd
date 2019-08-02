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

#include "nfc_system.h"

#define dbus_handlers_type_text_find_record(rec) \
    dbus_handlers_config_find_record(rec, \
    dbus_handlers_type_text_supported_record)

static
gboolean
dbus_handlers_type_text_supported_record(
    NfcNdefRec* ndef)
{
    return G_TYPE_CHECK_INSTANCE_TYPE(ndef, NFC_TYPE_NDEF_REC_T);
}

static
NfcNdefRecT*
dbus_handlers_type_text_pick_record(
    NfcNdefRec* ndef)
{
    NfcNdefRec* next = dbus_handlers_type_text_find_record(ndef->next);

    /* No need for anything complicated if there's only one record */
    if (next) {
        NfcLanguage* lang = nfc_system_language();

        if (lang) {
            GSList* list = g_slist_append(NULL, ndef);
            NfcNdefRec* best;

            do {
                list = g_slist_insert_sorted_with_data(list, next,
                    nfc_ndef_rec_t_lang_compare, lang);
                next = dbus_handlers_type_text_find_record(next->next);
            } while (next);

            best = list->data;
            g_slist_free(list);
            g_free(lang);
            return NFC_NDEF_REC_T(best);
        }
    }

    /* Just pick the first one */
    return NFC_NDEF_REC_T(ndef);
}

static
DBusHandlerConfig*
dbus_handlers_type_text_new_handler_config(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    return dbus_handlers_new_handler_config(file, "Text-Handler");
}

static
DBusListenerConfig*
dbus_handlers_type_text_new_listener_config(
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    return dbus_handlers_new_listener_config(file, "Text-Listener");
}

static
GVariant*
dbus_handlers_type_text_handler_args(
    NfcNdefRec* ndef)
{
    NfcNdefRecT* t = dbus_handlers_type_text_pick_record(ndef);

    return g_variant_new ("(s)", t->text);
}

static
GVariant*
dbus_handlers_type_text_listener_args(
    gboolean handled,
    NfcNdefRec* ndef)
{
    NfcNdefRecT* t = dbus_handlers_type_text_pick_record(ndef);

    return g_variant_new ("(bs)", handled, t->text);
}

const DBusHandlerType dbus_handlers_type_text = {
    .name = "Text",
    .priority = DBUS_HANDLER_PRIORITY_DEFAULT,
    .supported_record = dbus_handlers_type_text_supported_record,
    .new_handler_config = dbus_handlers_type_text_new_handler_config,
    .new_listener_config = dbus_handlers_type_text_new_listener_config,
    .free_handler_config = dbus_handlers_free_handler_config,
    .free_listener_config = dbus_handlers_free_listener_config,
    .handler_args = dbus_handlers_type_text_handler_args,
    .listener_args = dbus_handlers_type_text_listener_args
};

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
