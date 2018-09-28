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

#ifndef NFC_ADAPTER_IMPL_H
#define NFC_ADAPTER_IMPL_H

#include <nfc_adapter.h>

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

    /* Padding for future expansion */
    void (*_reserved1)(void);
    void (*_reserved2)(void);
    void (*_reserved3)(void);
    void (*_reserved4)(void);
    void (*_reserved5)(void);
    void (*_reserved6)(void);
    void (*_reserved7)(void);
    void (*_reserved8)(void);
    void (*_reserved9)(void);
    void (*_reserved10)(void);
} NfcAdapterClass;

void
nfc_adapter_mode_notify(
    NfcAdapter* adapter,
    NFC_MODE mode,
    gboolean requested);

void
nfc_adapter_power_notify(
    NfcAdapter* adapter,
    gboolean on,
    gboolean requested);

void
nfc_adapter_target_notify(
    NfcAdapter* adapter,
    gboolean present);

G_END_DECLS

#endif /* NFC_ADAPTER_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
