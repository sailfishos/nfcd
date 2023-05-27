/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2022 Jolla Ltd.
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

#include "nfc_plugin_p.h"
#include "nfc_plugin_impl.h"

struct nfc_plugin_priv {
    gboolean started;
};

#define THIS(obj) NFC_PLUGIN(obj)
#define THIS_TYPE NFC_TYPE_PLUGIN
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS nfc_plugin_parent_class
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, THIS_TYPE, \
        NfcPluginClass)

G_DEFINE_ABSTRACT_TYPE(NfcPlugin, nfc_plugin, PARENT_TYPE)

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcPlugin*
nfc_plugin_ref(
    NfcPlugin* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_plugin_unref(
    NfcPlugin* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

gboolean
nfc_plugin_start(
    NfcPlugin* self,
    NfcManager* manager)
{
    /* Caller checks plugin pointer for NULL */
    NfcPluginPriv* priv = self->priv;

    /* Don't start twice */
    if (priv->started) {
        /* Already started */
        return TRUE;
    } else if (GET_THIS_CLASS(self)->start(self, manager)) {
        /* Started successfully */
        priv->started = TRUE;
        return TRUE;
    } else {
        return FALSE;
    }
}

void
nfc_plugin_stop(
    NfcPlugin* self)
{
    /* Caller checks plugin pointer for NULL */
    NfcPluginPriv* priv = self->priv;

    /* Only stop if started */
    if (priv->started) {
        GET_THIS_CLASS(self)->stop(self);
        priv->started = FALSE;
    }
}

void
nfc_plugin_started(
    NfcPlugin* self)
{
    /* Caller checks plugin pointer for NULL */
    GET_THIS_CLASS(self)->started(self);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
gboolean
nfc_plugin_default_start(
    NfcPlugin* self,
    NfcManager* manager)
{
    return FALSE;
}

static
void
nfc_plugin_default_nop(
    NfcPlugin* self)
{
}

static
void
nfc_plugin_init(
    NfcPlugin* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE, NfcPluginPriv);
}

static
void
nfc_plugin_dispose(
    GObject* object)
{
    NfcPlugin* self = THIS(object);

    nfc_plugin_stop(self);
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
}

static
void
nfc_plugin_class_init(
    NfcPluginClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(NfcPluginPriv));
    object_class->dispose = nfc_plugin_dispose;
    klass->start = nfc_plugin_default_start;
    klass->stop = nfc_plugin_default_nop;
    klass->started = nfc_plugin_default_nop;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
