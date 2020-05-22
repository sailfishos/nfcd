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

#ifndef NFC_TARGET_H
#define NFC_TARGET_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct nfc_target_priv NfcTargetPriv;

struct nfc_target {
    GObject object;
    NfcTargetPriv* priv;
    NFC_TECHNOLOGY technology;
    NFC_PROTOCOL protocol;

    /* This one-way flag is set to FALSE when target disappears. */
    gboolean present;

    /*
     * Several transmissions may have to be performed one after
     * another, RECORD SELECT for type 2 tags is an example of that.
     * Implementation must not insert any internal transmission in
     * the middle of a transmission sequence, not even a presence
     * check.
     */
    NfcTargetSequence* sequence;
};

GType nfc_target_get_type(void);
#define NFC_TYPE_TARGET (nfc_target_get_type())
#define NFC_TARGET(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_TARGET, NfcTarget))

typedef
void
(*NfcTargetFunc)(
    NfcTarget* target,
    void* user_data);

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

gulong
nfc_target_add_sequence_handler(
    NfcTarget* target,
    NfcTargetFunc func,
    void* user_data); /* Since 1.0.17 */

void
nfc_target_remove_handler(
    NfcTarget* target,
    gulong id); /* Since 1.0.17 */

void
nfc_target_remove_handlers(
    NfcTarget* target,
    gulong* ids,
    guint count); /* Since 1.0.17 */

#define nfc_target_remove_all_handlers(target,ids) \
    nfc_target_remove_handlers(target, ids, G_N_ELEMENTS(ids))

/*
 * Sometimes it's necessary to guarantee that several transmissions
 * are performed one after another (and nothing happens in between).
 * That's done by allocating and holding a reference to NfcTargetSequence
 * object. As long as NfcTargetSequence is alive, NfcTarget will only
 * perform transmissions associated with this sequence.
 */

NfcTargetSequence*
nfc_target_sequence_new(
    NfcTarget* target); /* Since 1.0.17 */

void
nfc_target_sequence_free(
    NfcTargetSequence* seq); /* Since 1.0.17 */

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

#endif /* NFC_TARGET_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
