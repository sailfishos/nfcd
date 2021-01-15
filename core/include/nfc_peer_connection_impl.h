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

#ifndef NFC_PEER_CONNECTION_IMPL_H
#define NFC_PEER_CONNECTION_IMPL_H

#include "nfc_peer_connection.h"

G_BEGIN_DECLS

/* Since 1.1.0 */

/* Internal API for use by classes derived from NfcPeerConnection */

typedef struct nfc_peer_connection_class {
    GObjectClass parent;

    void (*accept)(NfcPeerConnection* conn);
    void (*accept_cancelled)(NfcPeerConnection* conn);
    void (*state_changed)(NfcPeerConnection* conn);
    void (*data_received)(NfcPeerConnection* conn, const void* data, guint len);
    void (*data_dequeued)(NfcPeerConnection* conn);

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
} NfcPeerConnectionClass;

#define NFC_PEER_CONNECTION_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), \
        NFC_TYPE_PEER_CONNECTION, NfcPeerConnectionClass)

void
nfc_peer_connection_init_connect(
    NfcPeerConnection* pc,
    NfcPeerService* ps,
    guint8 rsap,
    const char* name)
    NFCD_EXPORT;

void
nfc_peer_connection_init_accept(
    NfcPeerConnection* pc,
    NfcPeerService* ps,
    guint8 rsap)
    NFCD_EXPORT;

/* ACCEPTING => ACTIVE */
void
nfc_peer_connection_accepted(
    NfcPeerConnection* pc)
    NFCD_EXPORT;

/* ACCEPTING => DEAD */
void
nfc_peer_connection_rejected(
    NfcPeerConnection* pc)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_PEER_CONNECTION_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
