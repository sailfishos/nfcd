/*
 * Copyright (C) 2022-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2022 Jolla Ltd.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#ifndef NFC_CONFIG_H
#define NFC_CONFIG_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

/* This API exists since 1.1.10 */

/*
 * NFC_CONFIGURABLE interface is supposed be implemented by NfcPlugins
 * to expose their configurable options to nfcd core.
 *
 * GVariants returned by NfcConfigurable::get_value() could be either
 * floating or non-floating refs. nfc_config_get_value() passes those
 * through g_variant_take_ref() to convert them to full references and
 * always returns a full reference (or NULL). However, if you call
 * NfcConfigurable::get_value() directly, you have to do that dance
 * yourself.
 *
 * Similarly, nfc_config_set_value() handles floating GVariants, but
 * NfcConfigurable::set_value() shouldn't be expected to do the same
 * thing, if called directly.
 *
 * NULL value passed to NfcConfigurable::set_value() must be interpreted
 * as the default value.
 */

GType nfc_configurable_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_CONFIGURABLE (nfc_configurable_get_type())
#define NFC_CONFIGURABLE(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, \
        NFC_TYPE_CONFIGURABLE, NfcConfigurable)
#define NFC_IS_CONFIGURABLE(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, \
        NFC_TYPE_CONFIGURABLE)
#define NFC_CONFIGURABLE_GET_IFACE(obj) G_TYPE_INSTANCE_GET_INTERFACE(obj, \
        NFC_TYPE_CONFIGURABLE, NfcConfigurableInterface)

typedef struct nfc_configurable_interface NfcConfigurableInterface;

typedef
void
(*NfcConfigChangeFunc)(
    NfcConfigurable* config,
    const char* key,
    GVariant* value,
    void* user_data);

struct nfc_configurable_interface {
    GTypeInterface parent;

    const char* const* (*get_keys)(NfcConfigurable* conf);
    GVariant* (*get_value)(NfcConfigurable* config, const char* key);
    gboolean (*set_value)(NfcConfigurable* config, const char* key,
        GVariant* value);
    gulong (*add_change_handler)(NfcConfigurable* config, const char* key,
        NfcConfigChangeFunc func, void* user_data);
    void (*remove_handler)(NfcConfigurable* config, gulong id);

    /* Padding for future expansion */
    void (*_reserved1)(void);
    void (*_reserved2)(void);
    void (*_reserved3)(void);
    void (*_reserved4)(void);
    void (*_reserved5)(void);
};

const char* const*
nfc_config_get_keys(
    NfcConfigurable* config)
    NFCD_EXPORT;

GVariant*
nfc_config_get_value(
    NfcConfigurable* config,
    const char* key)
    NFCD_EXPORT;

gboolean
nfc_config_set_value(
    NfcConfigurable* config,
    const char* key,
    GVariant* value)
    NFCD_EXPORT;

gulong
nfc_config_add_change_handler(
    NfcConfigurable* config,
    const char* key,
    NfcConfigChangeFunc func,
    void* user_data)
    NFCD_EXPORT;

void
nfc_config_remove_handler(
    NfcConfigurable* config,
    gulong id)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_CONFIG_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
