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

#include "nfc_plugin_p.h"

struct nfc_plugin_priv {
    gboolean started;
};

G_DEFINE_ABSTRACT_TYPE(NfcPlugin, nfc_plugin, G_TYPE_OBJECT)
#define NFC_PLUGIN_GET_CLASS(obj) \
        G_TYPE_INSTANCE_GET_CLASS((obj), NFC_TYPE_PLUGIN, \
        NfcPluginClass)

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcPlugin*
nfc_plugin_ref(
    NfcPlugin* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(NFC_PLUGIN(self));
    }
    return self;
}

void
nfc_plugin_unref(
    NfcPlugin* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(NFC_PLUGIN(self));
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
    if (G_LIKELY(self)) {
        NfcPluginPriv* priv = self->priv;

        /* Don't start twice */
        if (priv->started) {
            /* Already started */
            return TRUE;
        } else if (NFC_PLUGIN_GET_CLASS(self)->start(self, manager)) {
            /* Started successfully */
            priv->started = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

void
nfc_plugin_stop(
    NfcPlugin* self)
{
    if (G_LIKELY(self)) {
        NfcPluginPriv* priv = self->priv;

        /* Only stop if started */
        if (priv->started) {
            NFC_PLUGIN_GET_CLASS(self)->stop(self);
            priv->started = FALSE;
        }
    }
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
nfc_plugin_default_stop(
    NfcPlugin* self)
{
}

static
void
nfc_plugin_init(
    NfcPlugin* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NFC_TYPE_PLUGIN,
        NfcPluginPriv);
}

static
void
nfc_plugin_dispose(
    GObject* object)
{
    NfcPlugin* self = NFC_PLUGIN(object);

    nfc_plugin_stop(self);
    G_OBJECT_CLASS(nfc_plugin_parent_class)->dispose(object);
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
    klass->stop = nfc_plugin_default_stop;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
