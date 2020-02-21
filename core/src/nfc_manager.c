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

#include "internal/nfc_manager_i.h"
#include "nfc_adapter_p.h"
#include "nfc_plugins.h"
#include "nfc_log.h"

#include <gutil_misc.h>

#include <stdlib.h>

GLOG_MODULE_DEFINE("nfc-core");

#define NFC_ADAPTER_NAME_FORMAT "nfc%u"

struct nfc_manager_priv {
    NfcPlugins* plugins;
    GHashTable* adapters;
    guint next_adapter_index;
};

typedef GObjectClass NfcManagerClass;
G_DEFINE_TYPE(NfcManager, nfc_manager, G_TYPE_OBJECT)

enum nfc_manager_signal {
    SIGNAL_ADAPTER_ADDED,
    SIGNAL_ADAPTER_REMOVED,
    SIGNAL_ENABLED_CHANGED,
    SIGNAL_STOPPED,
    SIGNAL_COUNT
};

#define SIGNAL_ADAPTER_ADDED_NAME       "nfc-manager-adapter-added"
#define SIGNAL_ADAPTER_REMOVED_NAME     "nfc-manager-adapter-removed"
#define SIGNAL_ENABLED_CHANGED_NAME     "nfc-manager-enabled-changed"
#define SIGNAL_STOPPED_NAME             "nfc-manager-stopped"

static guint nfc_manager_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
int
nfc_manager_compare_adapters(
    const void* p1,
    const void* p2)
{
    NfcAdapter* a1 = *(NfcAdapter* const*)p1;
    NfcAdapter* a2 = *(NfcAdapter* const*)p2;
    return strcmp(a1->name, a2->name);
}

static
NfcAdapter**
nfc_manager_get_adapters(
    NfcManagerPriv* priv,
    NfcAdapter* (*ref)(NfcAdapter*))
{
    const guint count = g_hash_table_size(priv->adapters);
    NfcAdapter** out = g_new(NfcAdapter*, count + 1);
    NfcAdapter** ptr = out;
    GHashTableIter iter;
    gpointer value;

    g_hash_table_iter_init(&iter, priv->adapters);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        NfcAdapter* adapter = value;
        *ptr++ = ref ? ref(adapter) : adapter;
    }
    *ptr = NULL;

    /* Sort adapters by name */
    qsort(out, count, sizeof(NfcAdapter*), nfc_manager_compare_adapters);
    return out;
}

static
NfcAdapter**
nfc_manager_adapters(
    NfcManagerPriv* priv)
{
    return nfc_manager_get_adapters(priv, NULL);
}

static
NfcAdapter**
nfc_manager_ref_adapters(
    NfcManagerPriv* priv)
{
    if (g_hash_table_size(priv->adapters) > 0) {
        return nfc_manager_get_adapters(priv, nfc_adapter_ref);
    } else {
        return NULL;
    }
}

static
void
nfc_manager_unref_adapters(
    NfcAdapter** adapters)
{
    NfcAdapter** ptr = adapters;

    /* Caller checks adapters for NULL */
    while (*ptr) nfc_adapter_unref(*ptr++);
    g_free(adapters);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcManager*
nfc_manager_ref(
    NfcManager* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(NFC_MANAGER(self));
    }
    return self;
}

void
nfc_manager_unref(
    NfcManager* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(NFC_MANAGER(self));
    }
}

NfcPlugin* const*
nfc_manager_plugins(
    NfcManager* self)
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        return nfc_plugins_list(priv->plugins);
    }
    return NULL;
}

NfcAdapter*
nfc_manager_get_adapter(
    NfcManager* self,
    const char* name)
{
    if (G_LIKELY(self) && G_LIKELY(name)) {
        NfcManagerPriv* priv = self->priv;

        return g_hash_table_lookup(priv->adapters, name);
    }
    return NULL;
}

const char*
nfc_manager_add_adapter(
    NfcManager* self,
    NfcAdapter* adapter)
{
    if (G_LIKELY(self) && G_LIKELY(adapter)) {
        /* Name is assigned to adapter only once in its lifetime */
        if (!adapter->name) {
            NfcManagerPriv* priv = self->priv;
            char* name = g_strdup_printf(NFC_ADAPTER_NAME_FORMAT,
                priv->next_adapter_index);

            priv->next_adapter_index++;
            while (g_hash_table_contains(priv->adapters, name)) {
                /* This is rather unlikely... */
                g_free(name);
                name = g_strdup_printf(NFC_ADAPTER_NAME_FORMAT,
                    priv->next_adapter_index);
                priv->next_adapter_index++;
            }

            nfc_adapter_set_name(adapter, name);
            nfc_adapter_set_enabled(adapter, self->enabled);
            g_hash_table_insert(priv->adapters, name, nfc_adapter_ref(adapter));
            g_free(self->adapters);
            self->adapters = nfc_manager_adapters(priv);
            g_signal_emit(self, nfc_manager_signals
                [SIGNAL_ADAPTER_ADDED], 0, adapter);
        }
        return adapter->name;
    }
    return NULL;
}

void
nfc_manager_remove_adapter(
    NfcManager* self,
    const char* name)
{
    if (G_LIKELY(self) && G_LIKELY(name)) {
        NfcManagerPriv* priv = self->priv;
        NfcAdapter* adapter = g_hash_table_lookup(priv->adapters, name);

        if (adapter) {
            nfc_adapter_ref(adapter);
            g_hash_table_remove(priv->adapters, name);
            g_free(self->adapters);
            self->adapters = nfc_manager_adapters(priv);
            g_signal_emit(self, nfc_manager_signals
                [SIGNAL_ADAPTER_REMOVED], 0, adapter);
            nfc_adapter_unref(adapter);
        }
    }
}

void
nfc_manager_set_enabled(
    NfcManager* self,
    gboolean enabled)
{
    if (G_LIKELY(self) && self->enabled != enabled) {
        NfcAdapter** adapters;

        self->enabled = enabled;
        g_signal_emit(self, nfc_manager_signals[SIGNAL_ENABLED_CHANGED], 0);
        adapters = nfc_manager_ref_adapters(self->priv);
        if (adapters) {
            NfcAdapter** ptr = adapters;

            while (*ptr) {
                nfc_adapter_set_enabled(*ptr++, self->enabled);
            }
            nfc_manager_unref_adapters(adapters);
        }
    }
}

void
nfc_manager_request_power(
    NfcManager* self,
    gboolean on)
{
    if (G_LIKELY(self)) {
        NfcAdapter** adapters = nfc_manager_ref_adapters(self->priv);

        if (adapters) {
            NfcAdapter** ptr = adapters;

            while (*ptr) {
                nfc_adapter_request_power(*ptr++, on);
            }
            nfc_manager_unref_adapters(adapters);
        }
    }
}

void
nfc_manager_request_mode(
    NfcManager* self,
    NFC_MODE mode)
{
    if (G_LIKELY(self)) {
        NfcAdapter** adapters = nfc_manager_ref_adapters(self->priv);

        if (adapters) {
            NfcAdapter** ptr = adapters;

            while (*ptr) {
                nfc_adapter_request_mode(*ptr++, mode);
            }
            nfc_manager_unref_adapters(adapters);
        }
    }
}

void
nfc_manager_stop(
    NfcManager* self,
    int error)
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        /* Only the first error gets stored */
        if (error && !self->error) {
            self->error = error;
        }

        if (!self->stopped) {
            self->stopped = TRUE;
            nfc_plugins_stop(priv->plugins);
            g_signal_emit(self, nfc_manager_signals[SIGNAL_STOPPED], 0);
        }
    }
}

gulong
nfc_manager_add_adapter_added_handler(
    NfcManager* self,
    NfcManagerAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ADAPTER_ADDED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_adapter_removed_handler(
    NfcManager* self,
    NfcManagerAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ADAPTER_REMOVED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_enabled_changed_handler(
    NfcManager* self,
    NfcManagerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ENABLED_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_stopped_handler(
    NfcManager* self,
    NfcManagerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_STOPPED_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_manager_remove_handler(
    NfcManager* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_manager_remove_handlers(
    NfcManager* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

NfcManager*
nfc_manager_new(
    const NfcPluginsInfo* pi)
{
    NfcManager* self = g_object_new(NFC_TYPE_MANAGER, NULL);
    NfcManagerPriv* priv = self->priv;

    priv->plugins = nfc_plugins_new(pi);
    return self;
}

gboolean
nfc_manager_start(
    NfcManager* self)
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        return nfc_plugins_start(priv->plugins, self);
    }
    return FALSE;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_manager_init(
    NfcManager* self)
{
    NfcManagerPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NFC_TYPE_MANAGER,
        NfcManagerPriv);

    self->priv = priv;
    self->adapters = g_new0(NfcAdapter*, 1);
    self->enabled = TRUE;
    priv->adapters = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, g_object_unref);
}

static
void
nfc_manager_finalize(
    GObject* object)
{
    NfcManager* self = NFC_MANAGER(object);
    NfcManagerPriv* priv = self->priv;

    nfc_plugins_free(priv->plugins);
    g_hash_table_destroy(priv->adapters);
    g_free(self->adapters);
    G_OBJECT_CLASS(nfc_manager_parent_class)->finalize(object);
}

static
void
nfc_manager_class_init(
    NfcManagerClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(NfcManagerPriv));
    object_class->finalize = nfc_manager_finalize;

    nfc_manager_signals[SIGNAL_ADAPTER_ADDED] =
        g_signal_new(SIGNAL_ADAPTER_ADDED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
            G_TYPE_OBJECT);
    nfc_manager_signals[SIGNAL_ADAPTER_REMOVED] =
        g_signal_new(SIGNAL_ADAPTER_REMOVED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
            G_TYPE_OBJECT);
    nfc_manager_signals[SIGNAL_ENABLED_CHANGED] =
        g_signal_new(SIGNAL_ENABLED_CHANGED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_manager_signals[SIGNAL_STOPPED] =
        g_signal_new(SIGNAL_STOPPED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
