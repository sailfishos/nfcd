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

#ifndef NFC_PEER_SERVICE_H
#define NFC_PEER_SERVICE_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

/* Since 1.1.0 */

typedef struct nfc_peer_service_priv NfcPeerServicePriv;
struct nfc_peer_service {
    GObject object;
    NfcPeerServicePriv* priv;
    const char* name;
    guint8 sap;
};

/* Well-known LLCP SAP values */
#define NFC_LLC_SAP_SDP (0x01)    /* urn:nfc:sn:sdp */
#define NFC_LLC_SAP_SNEP (0x04)   /* urn:nfc:sn:snep */

/* Well-known names */
#define NFC_LLC_NAME_SDP            "urn:nfc:sn:sdp"
#define NFC_LLC_NAME_SNEP           "urn:nfc:sn:snep"

GType nfc_peer_service_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_PEER_SERVICE (nfc_peer_service_get_type())
#define NFC_PEER_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_PEER_SERVICE, NfcPeerService))

NfcPeerService*
nfc_peer_service_ref(
    NfcPeerService* service)
    NFCD_EXPORT;

void
nfc_peer_service_unref(
    NfcPeerService* service)
    NFCD_EXPORT;

void
nfc_peer_service_disconnect_all(
    NfcPeerService* service)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_PEER_SERVICE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
