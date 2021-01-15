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

#ifndef NFC_PEER_CONNECTION_PRIVATE_H
#define NFC_PEER_CONNECTION_PRIVATE_H

#include "nfc_types_p.h"

#include <nfc_peer_connection.h>

typedef struct nfc_peer_connection_llcp_state {
    /*
     * NFCForum-TS-LLCP_1.1
     * 5.6 Connection-oriented Transport Mode Procedures
     * 5.6.1 Data Link Connection State Variables
     */
    guint8 vs;          /* Send State Variable V(S) */
    guint8 vsa;         /* Send Acknowledgement State Variable V(SA) */
    guint8 vr;          /* Receive State Variable V(R) */
    guint8 vra;         /* Receive Acknowledgement State Variable V(RA) */

    /*
     * 5.6.2 Data Link Connection Parameters
     */
    guint8 rwr;         /* Remote Receive Window Size, RW(R) */
    guint16 rmiu;       /* Remote Maximum Information Unit size for I PDUs */
} NfcPeerConnectionLlcpState;

#define LLCP_CONN_KEY(lsap,rsap)  GINT_TO_POINTER(\
    ((((guint16)(lsap)) & 0x3f) << 10) | \
     (((guint16)(rsap)) & 0x3f))

gpointer
nfc_peer_connection_key(
    NfcPeerConnection* pc)
    NFCD_INTERNAL;

void
nfc_peer_connection_set_llc(
    NfcPeerConnection* pc,
    NfcLlc* llc)
    NFCD_INTERNAL;

const NfcLlcParam* const*
nfc_peer_connection_lp(
    NfcPeerConnection* pc)
    NFCD_INTERNAL;

NfcPeerConnectionLlcpState*
nfc_peer_connection_ps(
    NfcPeerConnection* pc)
    NFCD_INTERNAL;

void
nfc_peer_connection_apply_remote_params(
    NfcPeerConnection* pc,
    const NfcLlcParam* const* params)
    NFCD_INTERNAL;

void
nfc_peer_connection_set_state(
    NfcPeerConnection* pc,
    NFC_LLC_CO_STATE state)
    NFCD_INTERNAL;

void
nfc_peer_connection_accept(
    NfcPeerConnection* pc)
    NFCD_INTERNAL;

void
nfc_peer_connection_data_received(
    NfcPeerConnection* pc,
    const void* data,
    guint len)
    NFCD_INTERNAL;

void
nfc_peer_connection_flush(
    NfcPeerConnection* pc)
    NFCD_INTERNAL;

#endif /* NFC_PEER_CONNECTION_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
