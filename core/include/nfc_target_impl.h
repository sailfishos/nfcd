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

#ifndef NFC_TARGET_IMPL_H
#define NFC_TARGET_IMPL_H

#include <nfc_target.h>

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
} NfcTargetClass;

#define NFC_TARGET_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), \
        NFC_TYPE_TARGET, NfcTargetClass)

typedef
void
(*NfcTargetTransmitFunc)(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data);

NfcTarget*
nfc_target_ref(
    NfcTarget* target);

void
nfc_target_unref(
    NfcTarget* target);

void
nfc_target_transmit_done(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len);

void
nfc_target_gone(
    NfcTarget* target);

/*
 * These functions can be used for sending internal requests (e.g. presence
 * check) to take advantage of queueing provided by NfcTarget:
 */

guint
nfc_target_transmit(
    NfcTarget* target,
    const void* data,
    guint len,
    NfcTargetSequence* seq,
    NfcTargetTransmitFunc complete,
    GDestroyNotify destroy,
    void* user_data);

gboolean
nfc_target_cancel_transmit(
    NfcTarget* target,
    guint id);

G_END_DECLS

#endif /* NFC_TARGET_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
