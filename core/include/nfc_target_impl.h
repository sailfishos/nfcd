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

#ifndef NFC_TARGET_IMPL_H
#define NFC_TARGET_IMPL_H

#include "nfc_target.h"

/* Internal API for use by NfcTarget implemenations */

G_BEGIN_DECLS

typedef struct nfc_target_class {
    GObjectClass parent;

    /* Base class makes sure that there are no overlapping transmissions.
     * When transmission completes, nfc_target_transmit_done() is called
     * by the derived class. */
    gboolean (*transmit)(NfcTarget* target, const void* data, guint len);
    void (*cancel_transmit)(NfcTarget* target);

    /* This should deactivate the target. When the target gets deactivated,
     * subclass calls nfc_target_gone() to update the 'present' flag. */
    void (*deactivate)(NfcTarget* target);

    /* These base implementations emit signals, must always be called. */
    void (*sequence_changed)(NfcTarget* target);
    void (*gone)(NfcTarget* target);

    /* Reactivates the same target (re-selects the interface etc.)
     * When reactivation completes successfully, derived class calls
     * nfc_target_reactivated(), otherwise nfc_target_gone(). Timeout is
     * handled by the base class, derived class doesn't need to bother.
     * Reactivation isn't cancellable, it either succeeds or fails. */
    gboolean (*reactivate)(NfcTarget* target);  /* Since 1.0.27 */

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
} NfcTargetClass;

#define NFC_TARGET_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), \
        NFC_TYPE_TARGET, NfcTargetClass)

void
nfc_target_transmit_done(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len)
    NFCD_EXPORT;

void
nfc_target_reactivated(
    NfcTarget* target) /* Since 1.0.27 */
    NFCD_EXPORT;

void
nfc_target_gone(
    NfcTarget* target)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_TARGET_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
