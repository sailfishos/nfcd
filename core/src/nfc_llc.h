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

#ifndef NFC_LLC_H
#define NFC_LLC_H

#include "nfc_types_p.h"

typedef enum nfc_llc_flags {
    NFC_LLC_FLAGS_NONE = 0x00,
    NFC_LLC_FLAG_INITIATOR = 0x01  /* Otherwise Target */
} NFC_LLC_FLAGS;

/*
 * NFCForum-TS-LLCP_1.1
 * 4.3.8 Disconnected Mode (DM)
 * Table 4: Disconnected Mode Reasons
 */
typedef enum nfc_llc_dm_reason {
    /* The LLC has received a DISC PDU and is now logically disconnected
     * from the data link connection. */
    NFC_LLC_DM_DISC_RECEIVED = 0x00,
    /* The LLC has received a connection-oriented PDU but the target
     * service access point has no active connection. */
    NFC_LLC_DM_NOT_CONNECTED = 0x01,
    /* The remote LLC has received a CONNECT PDU and there is no service
     * bound to the specified target service access point. */
    NFC_LLC_DM_NO_SERVICE = 0x02,
    /* The remote LLC has processed a CONNECT PDU and the request to
     * connect was rejected by the service layer. */
    NFC_LLC_DM_REJECT = 0x03
} NFC_LLC_DM_REASON;

/*
 * LLC Link Management state machine:
 *
 *           +=======+
 *     +---> | ERROR | <---+
 *     |     +=======+     |
 *  protocol            protocol
 *   error               error
 *     |                   |
 * +-------+           +--------+
 * | START | -- ok --> | ACTIVE |
 * +-------+           +--------+
 *     |                   |
 *  transmit            transmit
 *   error               error
 *     |   +===========+   |
 *     +-> | PEER_LOST | <-+
 *         +===========+
 */
typedef enum nfc_llc_state {
    NFC_LLC_STATE_START,        /* Initial state */
    NFC_LLC_STATE_ACTIVE,       /* Functional state */
    NFC_LLC_STATE_ERROR,        /* Terminal state */
    NFC_LLC_STATE_PEER_LOST     /* Terminal state */
} NFC_LLC_STATE;

struct nfc_llc {
    NFC_LLC_STATE state;
    gboolean idle;
    guint wks; /* Remote well-known services (mask) */
};

typedef
void
(*NfcLlcFunc)(
    NfcLlc* llc,
    void* user_data);

typedef
void
(*NfcLlcConnectFunc)(
    NfcPeerConnection* conn,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data);

NfcLlc*
nfc_llc_new(
    NfcLlcIo* io,
    NfcPeerServices* services,
    const NfcLlcParam* const* params)
    NFCD_INTERNAL;

void
nfc_llc_free(
    NfcLlc* llc)
    NFCD_INTERNAL;

NfcPeerConnection*
nfc_llc_connect(
    NfcLlc* llc,
    NfcPeerService* service,
    guint rsap,
    NfcLlcConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
    NFCD_INTERNAL;

NfcPeerConnection*
nfc_llc_connect_sn(
    NfcLlc* llc,
    NfcPeerService* service,
    const char* sn,
    NfcLlcConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
    NFCD_INTERNAL;

gulong
nfc_llc_add_state_changed_handler(
    NfcLlc* llc,
    NfcLlcFunc func,
    void* user_data)
    NFCD_INTERNAL;

gulong
nfc_llc_add_idle_changed_handler(
    NfcLlc* llc,
    NfcLlcFunc func,
    void* user_data)
    NFCD_INTERNAL;

gulong
nfc_llc_add_wks_changed_handler(
    NfcLlc* llc,
    NfcLlcFunc func,
    void* user_data)
    NFCD_INTERNAL;

void
nfc_llc_remove_handler(
    NfcLlc* llc,
    gulong id)
    NFCD_INTERNAL;

void
nfc_llc_remove_handlers(
    NfcLlc* llc,
    gulong* ids,
    guint count)
    NFCD_INTERNAL;

void
nfc_llc_ack(
    NfcLlc* llc,
    NfcPeerConnection* conn,
    gboolean last)
    NFCD_INTERNAL;

gboolean
nfc_llc_i_pdu_queued(
    NfcLlc* llc,
    NfcPeerConnection* conn)
    NFCD_INTERNAL;

void
nfc_llc_submit_dm_pdu(
    NfcLlc* llc,
    guint8 dsap,
    guint8 ssap,
    NFC_LLC_DM_REASON reason)
    NFCD_INTERNAL;

void
nfc_llc_submit_disc_pdu(
    NfcLlc* llc,
    guint8 dsap,
    guint8 ssap)
    NFCD_INTERNAL;

void
nfc_llc_submit_cc_pdu(
    NfcLlc* llc,
    NfcPeerConnection* conn)
    NFCD_INTERNAL;

void
nfc_llc_submit_i_pdu(
    NfcLlc* llc,
    NfcPeerConnection* conn,
    const void* data,
    guint len)
    NFCD_INTERNAL;

void
nfc_llc_connection_dead(
    NfcLlc* llc,
    NfcPeerConnection* conn)
    NFCD_INTERNAL;

gboolean
nfc_llc_cancel_connect_request(
    NfcLlc* llc,
    NfcPeerConnection* conn)
    NFCD_INTERNAL;

#define nfc_llc_remove_all_handlers(llc,ids) \
    nfc_llc_remove_handlers(llc, ids, G_N_ELEMENTS(ids))

#endif /* NFC_LLC_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
