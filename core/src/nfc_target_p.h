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

#ifndef NFC_TARGET_PRIVATE_H
#define NFC_TARGET_PRIVATE_H

#include "nfc_types_p.h"

#include <nfc_target_impl.h>

/* Add _ prefix so that they don't get exported to plugins */
#define nfc_target_deactivate _nfc_target_deactivate
#define nfc_target_generate_id _nfc_target_generate_id
#define nfc_target_add_sequence_handler _nfc_target_add_sequence_handler
#define nfc_target_add_gone_handler _nfc_target_add_gone_handler
#define nfc_target_remove_handler _nfc_target_remove_handler
#define nfc_target_remove_handlers _nfc_target_remove_handlers
#define nfc_target_sequence_new _nfc_target_sequence_new
#define nfc_target_sequence_free _nfc_target_sequence_free

typedef
void
(*NfcTargetFunc)(
    NfcTarget* target,
    void* user_data);

void
nfc_target_deactivate(
    NfcTarget* target);

guint
nfc_target_generate_id(
    NfcTarget* target);

gulong
nfc_target_add_sequence_handler(
    NfcTarget* target,
    NfcTargetFunc func,
    void* user_data);

gulong
nfc_target_add_gone_handler(
    NfcTarget* target,
    NfcTargetFunc func,
    void* user_data);

void
nfc_target_remove_handler(
    NfcTarget* target,
    gulong id);

void
nfc_target_remove_handlers(
    NfcTarget* target,
    gulong* ids,
    guint count);

/*
 * Several transmissions may have to be performed one after another,
 * RECORD SELECT for type 2 tags is an example of that. That's done
 * by allocating and holding a reference to NfcTargetTransmitSequence
 * object. As long as NfcTargetTransmitSequence is alive, NfcTarget
 * will only perform transmissions that belong to this sequence.
 */

NfcTargetSequence*
nfc_target_sequence_new(
    NfcTarget* target);

void
nfc_target_sequence_free(
    NfcTargetSequence* seq);

#endif /* NFC_TARGET_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
