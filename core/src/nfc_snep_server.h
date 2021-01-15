/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#ifndef NFC_SNEP_SERVER_H
#define NFC_SNEP_SERVER_H

#include "nfc_types_p.h"
#include "nfc_peer_service.h"

/*
 * SNEP server accespts NDEF message from SNEP client.
 */
typedef enum nfc_snep_server_state {
    NFC_SNEP_SERVER_LISTENING,
    NFC_SNEP_SERVER_RECEIVING
} NFC_SNEP_SERVER_STATE;

typedef struct nfc_snep_server_priv NfcSnepServerPriv;
typedef struct nfc_snep_server {
    NfcPeerService service;
    NfcSnepServerPriv* priv;
    NFC_SNEP_SERVER_STATE state;
    NfcNdefRec* ndef;
} NfcSnepServer;

typedef
void
(*NfcSnepServerFunc)(
    NfcSnepServer* snep,
    void* user_data);

NfcSnepServer*
nfc_snep_server_new(
    void)
    NFCD_INTERNAL;

gulong
nfc_snep_server_add_state_changed_handler(
    NfcSnepServer* snep,
    NfcSnepServerFunc func,
    void* user_data)
    NFCD_INTERNAL;

gulong
nfc_snep_server_add_ndef_changed_handler(
    NfcSnepServer* snep,
    NfcSnepServerFunc func,
    void* user_data)
    NFCD_INTERNAL;

void
nfc_snep_server_remove_handler(
    NfcSnepServer* snep,
    gulong id)
    NFCD_INTERNAL;

void
nfc_snep_server_remove_handlers(
    NfcSnepServer* snep,
    gulong* ids,
    guint count)
    NFCD_INTERNAL;

#define nfc_snep_server_remove_all_handlers(snep,ids) \
    nfc_snep_server_remove_handlers(snep, ids, G_N_ELEMENTS(ids))

#endif /* NFC_SNEP_SERVER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
