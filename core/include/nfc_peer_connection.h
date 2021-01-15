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

#ifndef NFC_PEER_CONNECTION_H
#define NFC_PEER_CONNECTION_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

/* Since 1.1.0 */

typedef enum nfc_llc_co_state {
    NFC_LLC_CO_CONNECTING,      /* CONNECT sent, waiting for CC */
    NFC_LLC_CO_ACCEPTING,       /* CONNECT received, CC not sent */
    NFC_LLC_CO_ABANDONED,       /* CONNECT sent, will disconnect */
    NFC_LLC_CO_ACTIVE,          /* Connection established */
    NFC_LLC_CO_DISCONNECTING,   /* DISC sent, waiting for DM */
    NFC_LLC_CO_DEAD             /* Final state */
} NFC_LLC_CO_STATE;

typedef struct nfc_peer_connection_priv NfcPeerConnectionPriv;
struct nfc_peer_connection {
    GObject object;
    NfcPeerConnectionPriv* priv;
    NfcPeerService* service;        /* Local service */
    NFC_LLC_CO_STATE state;         /* Connection state */
    const char* name;               /* Remote service name, if known */
    gsize bytes_queued;             /* Bytes currently queued */
    guint64 bytes_sent;             /* Bytes sent (passed to LLCP level) */
    guint64 bytes_received;         /* Bytes received */
    guint8 rsap;                    /* Remote SAP */
};

GType nfc_peer_connection_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_PEER_CONNECTION (nfc_peer_connection_get_type())
#define NFC_PEER_CONNECTION(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_PEER_CONNECTION, NfcPeerConnection))

typedef
void
(*NfcPeerConnectionFunc)(
    NfcPeerConnection* pc,
    void* user_data);

NfcPeerConnection*
nfc_peer_connection_ref(
    NfcPeerConnection* pc)
    NFCD_EXPORT;

void
nfc_peer_connection_unref(
    NfcPeerConnection* pc)
    NFCD_EXPORT;

guint
nfc_peer_connection_rmiu(
    NfcPeerConnection* pc)
    NFCD_EXPORT;

gboolean
nfc_peer_connection_send(
    NfcPeerConnection* pc,
    GBytes* bytes)
    NFCD_EXPORT;

void
nfc_peer_connection_disconnect(
    NfcPeerConnection* pc)
    NFCD_EXPORT;

gboolean
nfc_peer_connection_cancel(
    NfcPeerConnection* pc)
    NFCD_EXPORT;

gulong
nfc_peer_connection_add_state_changed_handler(
    NfcPeerConnection* pc,
    NfcPeerConnectionFunc func,
    void* user_data);
    NFCD_EXPORT

void
nfc_peer_connection_remove_handler(
    NfcPeerConnection* pc,
    gulong id)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_PEER_CONNECTION_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
