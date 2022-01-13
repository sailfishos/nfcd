/*
 * Copyright (C) 2022 Jolla Ltd.
 * Copyright (C) 2022 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_config.h"

G_DEFINE_INTERFACE(NfcConfigurable, nfc_configurable, G_TYPE_OBJECT)

/*==========================================================================*
 * Interface
 *==========================================================================*/

const char* const*
nfc_config_get_keys(
    NfcConfigurable* self)
{
    return G_LIKELY(self) ?
        NFC_CONFIGURABLE_GET_IFACE(self)->get_keys(self) :
        NULL;
}

GVariant*
nfc_config_get_value(
    NfcConfigurable* self,
    const char* key)
{
    if (G_LIKELY(self) && G_LIKELY(key)) {
        GVariant* val = NFC_CONFIGURABLE_GET_IFACE(self)->get_value(self, key);

        if (val) {
            /* Make sure we return a full reference */
            return g_variant_take_ref(val);
        }
    }
    return NULL;
}

gboolean
nfc_config_set_value(
    NfcConfigurable* self,
    const char* key,
    GVariant* value)
{
    if (G_LIKELY(self) && G_LIKELY(key)) {
        gboolean ok;

        /* Sink weird floating references before calling set_value() */
        if (value) g_variant_ref_sink(value);
        ok = NFC_CONFIGURABLE_GET_IFACE(self)->set_value(self, key, value);
        if (value) g_variant_unref(value);
        return ok;
    } else {
        return FALSE;
    }
}

gulong
nfc_config_add_change_handler(
    NfcConfigurable* self,
    const char* key,
    NfcConfigChangeFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ?
        NFC_CONFIGURABLE_GET_IFACE(self)->add_change_handler(self, key,
        func, user_data) : 0;
}

void
nfc_config_remove_handler(
    NfcConfigurable* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        NFC_CONFIGURABLE_GET_IFACE(self)->remove_handler(self, id);
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
const char* const*
nfc_configurable_default_get_keys(
    NfcConfigurable* self)
{
    static const char* none[] = { NULL };

    return none;
}

static
GVariant*
nfc_configurable_default_get_value(
    NfcConfigurable* self,
    const char* key)
{
    return NULL;
}

static
gboolean
nfc_configurable_default_set_value(
    NfcConfigurable* self,
    const char* key,
    GVariant* value)
{
    return FALSE;
}

static
gulong
nfc_configurable_default_add_change_handler(
    NfcConfigurable* self,
    const char* key,
    NfcConfigChangeFunc func,
    void* user_data)
{
    return 0;
}

static
void
nfc_configurable_default_remove_handler(
    NfcConfigurable* self,
    gulong id)
{
    g_signal_handler_disconnect(self, id);
}

static
void
nfc_configurable_default_init(
    NfcConfigurableInterface* iface)
{
    iface->get_keys = nfc_configurable_default_get_keys;
    iface->get_value = nfc_configurable_default_get_value;
    iface->set_value = nfc_configurable_default_set_value;
    iface->add_change_handler = nfc_configurable_default_add_change_handler;
    iface->remove_handler = nfc_configurable_default_remove_handler;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
