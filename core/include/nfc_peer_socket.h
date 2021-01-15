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

#ifndef NFC_PEER_SOCKET_H
#define NFC_PEER_SOCKET_H

#include "nfc_peer_connection.h"

#include <gio/gio.h>

G_BEGIN_DECLS

/* Since 1.1.0 */

/*
 * NfcPeerSocket essentially provides two ways of writing the data.
 * Calling nfc_peer_connection_send() will work, and writing data
 * to public file descriptor will also work. File descriptor
 * writes will be internally (and asynchronously) translated
 * into nfc_peer_connection_send() calls.
 *
 * The file descritor is exposed as a ref-countable GUnixFDList.
 * That list always contains exactly one descriptor.
 *
 * It doesn't make sense to use both methods though, because chunks
 * of data will end up being unpredictably mixed up with each other.
 * The preferred way of writing the data and preserving the integrity
 * of the stream is to use the file descriptor.
 *
 * Note that max_send_queue is not a hard limit, the actual amount of
 * data buffered at NfcPeerService level may exceed the limit by one MIU.
 * That's in addition to buffering happening in other places down the stack.
 */
typedef struct nfc_peer_socket_priv NfcPeerSocketPriv;
struct nfc_peer_socket {
    NfcPeerConnection connection;
    NfcPeerSocketPriv* priv;
    GUnixFDList* fdl;
    gsize max_send_queue;
};

GType nfc_peer_socket_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_PEER_SOCKET (nfc_peer_socket_get_type())
#define NFC_PEER_SOCKET(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_PEER_SOCKET, NfcPeerSocket))

NfcPeerSocket*
nfc_peer_socket_new_connect(
    NfcPeerService* service,
    guint8 rsap,
    const char* name)
    G_GNUC_WARN_UNUSED_RESULT
    NFCD_EXPORT;

NfcPeerSocket*
nfc_peer_socket_new_accept(
    NfcPeerService* service,
    guint8 rsap)
    G_GNUC_WARN_UNUSED_RESULT
    NFCD_EXPORT;

int
nfc_peer_socket_fd(
    NfcPeerSocket* socket)
    NFCD_EXPORT;

void
nfc_peer_socket_set_max_send_queue(
    NfcPeerSocket* socket,
    gsize max_send_queue)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_PEER_SOCKET_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
