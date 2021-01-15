/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
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

#ifndef NFC_PEER_H
#define NFC_PEER_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

/* Since 1.1.0 */

typedef struct nfc_peer_priv NfcPeerPriv;

typedef enum nfc_peer_flags {
    NFC_PEER_FLAGS_NONE = 0x00,
    NFC_PEER_FLAG_INITIALIZED = 0x01,
    NFC_PEER_FLAG_INITIATOR = 0x02
} NFC_PEER_FLAGS;

struct nfc_peer {
    GObject object;
    NfcPeerPriv* priv;
    const char* name;
    gboolean present;
    NFC_TECHNOLOGY technology;
    NFC_PEER_FLAGS flags;
    guint wks;  /* Remote Well-Known Services (mask) */
    NfcNdefRec* ndef; /* Received via SNEP */
};

GType nfc_peer_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_PEER (nfc_peer_get_type())
#define NFC_IS_PEER(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, NFC_TYPE_PEER)
#define NFC_PEER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), NFC_TYPE_PEER, \
        NfcPeer))

/*
 * NFC-DEP Initiator and Target parameters.
 * Contain relevant parts of ATR_RES/ATR_REQ (General bytes, what else?).
 *
 * NFCForum-TS-LLCP_1.1
 * 6.2.3.1 Link Activation procedure for the Initiator
 * 6.2.3.2 Link Activation procedure for the Target
 */
struct nfc_param_nfc_dep_initator {
    GUtilData atr_res_g; /* ATR_RES General Bytes */
};

struct nfc_param_nfc_dep_target {
    GUtilData atr_req_g; /* ATR_REQ General Bytes */
};

typedef
void
(*NfcPeerFunc)(
    NfcPeer* peer,
    void* user_data);

gulong
nfc_peer_add_initialized_handler(
    NfcPeer* peer,
    NfcPeerFunc func,
    void* user_data)
    NFCD_EXPORT;

NfcPeer*
nfc_peer_ref(
    NfcPeer* peer)
    NFCD_EXPORT;

void
nfc_peer_unref(
    NfcPeer* peer)
    NFCD_EXPORT;

void
nfc_peer_deactivate(
    NfcPeer* peer)
    NFCD_EXPORT;

gulong
nfc_peer_add_wks_changed_handler(
    NfcPeer* peer,
    NfcPeerFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_peer_add_ndef_changed_handler(
    NfcPeer* peer,
    NfcPeerFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_peer_add_initialized_handler(
    NfcPeer* peer,
    NfcPeerFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_peer_add_gone_handler(
    NfcPeer* peer,
    NfcPeerFunc func,
    void* user_data)
    NFCD_EXPORT;

void
nfc_peer_remove_handler(
    NfcPeer* peer,
    gulong id)
    NFCD_EXPORT;

void
nfc_peer_remove_handlers(
    NfcPeer* peer,
    gulong* ids,
    guint count)
    NFCD_EXPORT;

#define nfc_peer_remove_all_handlers(peer,ids) \
    nfc_peer_remove_handlers(peer, ids, G_N_ELEMENTS(ids))

gboolean
nfc_peer_register_service(
    NfcPeer* peer,
    NfcPeerService* service)
    NFCD_EXPORT;

void
nfc_peer_unregister_service(
    NfcPeer* peer,
    NfcPeerService* service)
    NFCD_EXPORT;

/*
 * Functions below return a NfcPeerConnection pointer, not a reference.
 * In other words, if the caller needs to keep this pointer, it needs
 * to add its own reference. If is only guaranteed that this pointer
 * stays alive until return to the event loop, or until the next call
 * to NFC core, whichever happens first.
 */

typedef
void
(*NfcPeerConnectFunc)(
    NfcPeer* peer,
    NfcPeerConnection* connection,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data);

NfcPeerConnection*
nfc_peer_connect(
    NfcPeer* peer,
    NfcPeerService* service,
    guint rsap,
    NfcPeerConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
    NFCD_EXPORT;

NfcPeerConnection*
nfc_peer_connect_sn(
    NfcPeer* peer,
    NfcPeerService* service,
    const char* sn,
    NfcPeerConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_PEER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
