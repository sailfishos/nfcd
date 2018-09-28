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

#include <nfc_adapter.h>
#include <nfc_tag.h>

enum {
    EVENT_TAG_ADDED,
    EVENT_TAG_REMOVED,
    EVENT_COUNT
};

struct dbus_handlers_adapter {
    NfcAdapter* adapter;
    DBusHandlers* handlers;
    GHashTable* tags;
    gulong event_id[EVENT_COUNT];
};

static
void
dbus_handlers_adapter_free_tag(
    void* tag)
{
    dbus_handlers_tag_free((DBusHandlersTag*)tag);
}

static
void
dbus_handlers_adapter_tag_add(
    DBusHandlersAdapter* self,
    NfcTag* tag)
{
    g_hash_table_replace(self->tags, g_strdup(tag->name),
        dbus_handlers_tag_new(tag, self->handlers));
}

/*==========================================================================*
 * NfcAdapter events
 *==========================================================================*/

static
void
dbus_handlers_adapter_tag_added(
    NfcAdapter* adapter,
    NfcTag* tag,
    void* user_data)
{
    dbus_handlers_adapter_tag_add((DBusHandlersAdapter*)user_data, tag);
}

static
void
dbus_handlers_adapter_tag_removed(
    NfcAdapter* adapter,
    NfcTag* tag,
    void* user_data)
{
    DBusHandlersAdapter* self = user_data;

    g_hash_table_remove(self->tags, (void*)tag->name);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

DBusHandlersAdapter*
dbus_handlers_adapter_new(
    NfcAdapter* adapter,
    DBusHandlers* handlers)
{
    DBusHandlersAdapter* self = g_new0(DBusHandlersAdapter, 1);
    NfcTag** tags = adapter->tags;

    self->handlers = handlers;
    self->adapter = nfc_adapter_ref(adapter);
    self->tags = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, dbus_handlers_adapter_free_tag);

    /* Existing tags */
    if (tags) {
        while (*tags) {
            dbus_handlers_adapter_tag_add(self, *tags++);
        }
    }

    /* NfcAdapter events */
    self->event_id[EVENT_TAG_ADDED] =
        nfc_adapter_add_tag_added_handler(adapter,
            dbus_handlers_adapter_tag_added, self);
    self->event_id[EVENT_TAG_REMOVED] =
        nfc_adapter_add_tag_removed_handler(adapter,
            dbus_handlers_adapter_tag_removed, self);

    return self;
}

void
dbus_handlers_adapter_free(
    DBusHandlersAdapter* self)
{
    if (self) {
        g_hash_table_destroy(self->tags);
        nfc_adapter_remove_all_handlers(self->adapter, self->event_id);
        nfc_adapter_unref(self->adapter);
        g_free(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
