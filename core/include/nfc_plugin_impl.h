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

#ifndef NFC_PLUGIN_IMPL_H
#define NFC_PLUGIN_IMPL_H

#include "nfc_plugin.h"
#include "nfc_version.h"

/* Internal API for use by NfcPlugin implemenations */

G_BEGIN_DECLS

typedef struct nfc_plugin_class {
    GObjectClass parent;

    gboolean (*start)(NfcPlugin* plugin, NfcManager* manager);
    void (*stop)(NfcPlugin* plugin);

    /* Padding for future expansion */
    void (*_reserved1)(void);
    void (*_reserved2)(void);
    void (*_reserved3)(void);
    void (*_reserved4)(void);
    void (*_reserved5)(void);
} NfcPluginClass;

GType nfc_plugin_get_type(void);
#define NFC_TYPE_PLUGIN (nfc_plugin_get_type())
#define NFC_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_PLUGIN, NfcPlugin))

/*
 * NFC_PLUGIN_DEFINE - simple way to define NfcPluginDesc with a single
 * or no log module, and no flags.
 */
#ifdef GLOG_MODULE_NAME
#define NFC_PLUGIN_DESC_LOG(name) _nfc_plugin_##name##_logs
#define NFC_PLUGIN_DEFINE(name, description, init) \
    static GLogModule* const NFC_PLUGIN_DESC_LOG(name)[] = \
        { &GLOG_MODULE_NAME, NULL }; \
    const NfcPluginDesc NFC_PLUGIN_DESC(name) NFC_PLUGIN_DESC_ATTR = \
        { #name, description, NFC_CORE_VERSION, init, \
          NFC_PLUGIN_DESC_LOG(name), 0 };
#else
#define NFC_PLUGIN_DEFINE(name, description, init) \
    const NfcPluginDesc NFC_PLUGIN_DESC(name) NFC_PLUGIN_DESC_ATTR = \
        { #name, description, NFC_CORE_VERSION, init, NULL, 0 };
#endif

/*
 * NFC_PLUGIN_DEFINE2 - more sophisticated way to define NfcPluginDesc,
 * short of building the initializer with bare handls.
 */
#define NFC_PLUGIN_DEFINE2(name, description, init, logs, flags) \
    const NfcPluginDesc NFC_PLUGIN_DESC(name) NFC_PLUGIN_DESC_ATTR = \
        { #name, description, NFC_CORE_VERSION, init, logs, flags };

G_END_DECLS

#endif /* NFC_PLUGIN_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
