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

#include <nfc_tag.h>

struct dbus_handlers_tag {
    NfcTag* tag;
    DBusHandlers* handlers;
    gulong init_id;
};

static
void
dbus_handlers_tag_initialized(
    DBusHandlersTag* self)
{
    NfcTag* tag = self->tag;

    GDEBUG("%s is initialized", tag->name);
    if (tag->ndef) {
        dbus_handlers_run(self->handlers, tag->ndef);
    }
}

static
void
dbus_handlers_tag_initialized_event(
    NfcTag* tag,
    void* user_data)
{
    DBusHandlersTag* self = user_data;

    nfc_tag_remove_handler(self->tag, self->init_id);
    self->init_id = 0;

    dbus_handlers_tag_initialized(self);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

DBusHandlersTag*
dbus_handlers_tag_new(
    NfcTag* tag,
    DBusHandlers* handlers)
{
    DBusHandlersTag* self = g_new0(DBusHandlersTag, 1);

    self->handlers = handlers;
    self->tag = nfc_tag_ref(tag);
    if (tag->flags & NFC_TAG_FLAG_INITIALIZED) {
        dbus_handlers_tag_initialized(self);
    } else {
        self->init_id = nfc_tag_add_initialized_handler(tag,
            dbus_handlers_tag_initialized_event, self);
    }
    return self;
}

void
dbus_handlers_tag_free(
    DBusHandlersTag* self)
{
    if (self) {
        nfc_tag_remove_handler(self->tag, self->init_id);
        nfc_tag_unref(self->tag);
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
