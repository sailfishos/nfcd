/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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

#ifndef NFC_PLUGIN_H
#define NFC_PLUGIN_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct nfc_plugin_priv NfcPluginPriv;

typedef enum nfc_plugin_flags {
    NFC_PLUGIN_FLAGS_NONE = 0x00,
    NFC_PLUGIN_FLAG_MUST_START = 0x01,
    NFC_PLUGIN_FLAG_DISABLED = 0x02   /* Disabled by default */
} NFC_PLUGIN_FLAGS;

struct nfc_plugin_desc {
    const char* name;
    const char* description;
    int nfc_core_version;
    NfcPlugin* (*create)(void);
    GLogModule* const* log;
    NFC_PLUGIN_FLAGS flags;
};

struct nfc_plugin {
    GObject object;
    NfcPluginPriv* priv;
    const NfcPluginDesc* desc;
};

GType nfc_plugin_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_PLUGIN (nfc_plugin_get_type())
#define NFC_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_PLUGIN, NfcPlugin))

NfcPlugin*
nfc_plugin_ref(
    NfcPlugin* plugin)
    NFCD_EXPORT;

void
nfc_plugin_unref(
    NfcPlugin* plugin)
    NFCD_EXPORT;

/*
 * NFC_PLUGIN_DECLARE - declares NfcPluginDesc in a way compatible with
 * NFC_PLUGIN_DEFINE* macros from nfc_plugin_impl.h
 */

#define NFC_PLUGIN_DESC_SYMBOL nfc_plugin_desc

#ifdef NFC_PLUGIN_EXTERNAL
   /* External (binary) plugin */
#  define NFC_PLUGIN_DESC(name) NFC_PLUGIN_DESC_SYMBOL
#  define NFC_PLUGIN_DESC_ATTR __attribute__ ((visibility("default")))
#else
   /* Internal (builtin) plugin */
#  define NFC_PLUGIN_DESC(name) _nfc_plugin_##name
#  define NFC_PLUGIN_DESC_ATTR
#endif

#define NFC_PLUGIN_DECLARE(name) \
    extern const NfcPluginDesc NFC_PLUGIN_DESC(name) NFC_PLUGIN_DESC_ATTR;

G_END_DECLS

#endif /* NFC_PLUGIN_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
