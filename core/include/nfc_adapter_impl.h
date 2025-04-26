/*
 * Copyright (C) 2018-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2021 Jolla Ltd.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#ifndef NFC_ADAPTER_IMPL_H
#define NFC_ADAPTER_IMPL_H

#include "nfc_adapter.h"

/* Internal API for use by NfcAdapter implemenations */

G_BEGIN_DECLS

typedef struct nfc_adapter_class {
    GObjectClass parent;

    /* Requests are asynchronous but the base class makes sure that multiple
     * requests don't get submitted in parallel */
    gboolean (*submit_power_request)(NfcAdapter* adapter, gboolean on);
    void (*cancel_power_request)(NfcAdapter* adapter);
    gboolean (*submit_mode_request)(NfcAdapter* adapter, NFC_MODE mode);
    void (*cancel_mode_request)(NfcAdapter* adapter);

    /* Since 1.2.0 */
    NFC_TECHNOLOGY (*get_supported_techs)(NfcAdapter* adapter);
    void (*set_allowed_techs)(NfcAdapter* adapter, NFC_TECHNOLOGY techs);

    /* Since 1.2.2 */
    const NFC_ADAPTER_PARAM* (*list_params)(NfcAdapter* adapter);
    NfcAdapterParamValue* (*get_param)(NfcAdapter* adapter,
        NFC_ADAPTER_PARAM id); /* Caller frees the result with g_free() */
    /*
     * Setting multiple parameters at once is more efficient because
     * doing it one by one would require full (or at least partial)
     * reinitialization in order to apply each paramter. Reset obviously
     * requires reinitialization too, at least if some parameters were in
     * a non-default state. This API allows to do a lot of that in one shot.
     */
    void (*set_params)(NfcAdapter* adapter,
        const NfcAdapterParam* const* params, /* NULL terminated list */
        gboolean reset); /* Reset all params that are not being set */

    /* Padding for future expansion */
    void (*_reserved1)(void);
    void (*_reserved2)(void);
    void (*_reserved3)(void);
    void (*_reserved4)(void);
    void (*_reserved5)(void);
} NfcAdapterClass;

#define NFC_ADAPTER_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), \
        NFC_TYPE_ADAPTER, NfcAdapterClass)

void
nfc_adapter_mode_notify(
    NfcAdapter* adapter,
    NFC_MODE mode,
    gboolean requested)
    NFCD_EXPORT;

void
nfc_adapter_power_notify(
    NfcAdapter* adapter,
    gboolean on,
    gboolean requested)
    NFCD_EXPORT;

void
nfc_adapter_target_notify(
    NfcAdapter* adapter,
    gboolean present)
    NFCD_EXPORT;

void
nfc_adapter_param_change_notify(
    NfcAdapter* adapter,
    NFC_ADAPTER_PARAM id) /* Since 1.2.2 */
    NFCD_EXPORT;

NFC_ADAPTER_PARAM*
nfc_adapter_param_list_merge(
    const NFC_ADAPTER_PARAM* params,
    ...) /* Since 1.2.2 */
    G_GNUC_NULL_TERMINATED
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_ADAPTER_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
