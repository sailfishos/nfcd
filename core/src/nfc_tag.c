/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "nfc_tag_p.h"
#include "nfc_target_p.h"
#include "nfc_ndef.h"
#include "nfc_log.h"

#include <gutil_misc.h>

struct nfc_tag_priv {
    char* name;
    gulong gone_id;
};

G_DEFINE_TYPE(NfcTag, nfc_tag, G_TYPE_OBJECT)

enum nfc_tag_signal {
    SIGNAL_INITIALIZED,
    SIGNAL_GONE,
    SIGNAL_COUNT
};

#define SIGNAL_INITIALIZED_NAME "nfc-tag-initialized"
#define SIGNAL_GONE_NAME        "nfc-tag-gone"

static guint nfc_tag_signals[SIGNAL_COUNT] = { 0 };

static
void
nfc_tag_gone(
    NfcTarget* target,
    void* user_data)
{
    NfcTag* self = NFC_TAG(user_data);

    /* NfcTarget makes sure that this signal is only issued once */
    GASSERT(self->present);
    self->present = FALSE;
    g_signal_emit(self, nfc_tag_signals[SIGNAL_GONE], 0);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcTag*
nfc_tag_new(
    NfcTarget* target)
{
    if (G_LIKELY(target)) {
        NfcTag* self = g_object_new(NFC_TYPE_TAG, NULL);

        nfc_tag_init_base(self, target);
        return self;
    }
    return NULL;
}

NfcTag*
nfc_tag_ref(
    NfcTag* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(NFC_TAG(self));
    }
    return self;
}

void
nfc_tag_unref(
    NfcTag* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(NFC_TAG(self));
    }
}

void
nfc_tag_deactivate(
    NfcTag* self)
{
    if (G_LIKELY(self)) {
        nfc_target_deactivate(self->target);
    }
}

gulong
nfc_tag_add_initialized_handler(
    NfcTag* self,
    NfcTagFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_INITIALIZED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_tag_add_gone_handler(
    NfcTag* self,
    NfcTagFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_GONE_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_tag_remove_handler(
    NfcTag* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_tag_remove_handlers(
    NfcTag* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

void
nfc_tag_init_base(
    NfcTag* self,
    NfcTarget* target)
{
    NfcTagPriv* priv = self->priv;

    GASSERT(target);
    GASSERT(!self->target);
    GASSERT(!priv->gone_id);
    self->present = target->present;
    self->target = nfc_target_ref(target);
    priv->gone_id = nfc_target_add_gone_handler(target, nfc_tag_gone, self);
}

void
nfc_tag_set_name(
    NfcTag* self,
    const char* name)
{
    NfcTagPriv* priv = self->priv;

    g_free(priv->name);
    self->name = priv->name = g_strdup(name);
}

void
nfc_tag_set_initialized(
    NfcTag* self)
{
    if (!(self->flags & NFC_TAG_FLAG_INITIALIZED)) {
        self->flags |= NFC_TAG_FLAG_INITIALIZED;
        g_signal_emit(self, nfc_tag_signals[SIGNAL_INITIALIZED], 0);
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_tag_init(
    NfcTag* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NFC_TYPE_TAG, NfcTagPriv);
}

static
void
nfc_tag_finalize(
    GObject* object)
{
    NfcTag* self = NFC_TAG(object);
    NfcTagPriv* priv = self->priv;

    nfc_target_remove_handler(self->target, priv->gone_id);
    nfc_target_unref(self->target);
    nfc_ndef_rec_unref(self->ndef);
    g_free(priv->name);
    G_OBJECT_CLASS(nfc_tag_parent_class)->finalize(object);
}

static
void
nfc_tag_class_init(
    NfcTagClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcTagPriv));
    G_OBJECT_CLASS(klass)->finalize = nfc_tag_finalize;
    nfc_tag_signals[SIGNAL_INITIALIZED] =
        g_signal_new(SIGNAL_INITIALIZED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_tag_signals[SIGNAL_GONE] =
        g_signal_new(SIGNAL_GONE_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
