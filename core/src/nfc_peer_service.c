/*
 * Copyright (C) 2020-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2020-2021 Jolla Ltd.
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

#include "nfc_peer_connection_p.h"
#include "nfc_peer_service_impl.h"
#include "nfc_peer_service_p.h"
#include "nfc_llc.h"

#define GLOG_MODULE_NAME NFC_PEER_LOG_MODULE
#include <gutil_log.h>

struct nfc_peer_service_priv {
    char* name;
    NfcPeerConnection** conns;
};

#define THIS(obj) NFC_PEER_SERVICE(obj)
#define THIS_TYPE NFC_TYPE_PEER_SERVICE
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS nfc_peer_service_parent_class
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, THIS_TYPE, \
        NfcPeerServiceClass)

G_DEFINE_ABSTRACT_TYPE(NfcPeerService, nfc_peer_service, PARENT_TYPE)

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcPeerService*
nfc_peer_service_ref(
    NfcPeerService* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_peer_service_unref(
    NfcPeerService* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

void
nfc_peer_service_disconnect_all(
    NfcPeerService* self)
{
    if (G_LIKELY(self)) {
        NfcPeerServicePriv* priv = self->priv;

        if (priv->conns) {
            NfcPeerConnection** ptr = priv->conns;
            NfcPeerConnection** tmp;
            guint n = 0;

            /* Temporarily bump references */
            for (ptr = priv->conns; *ptr++; n++);
            tmp = g_new(NfcPeerConnection*, n + 1);
            for (n = 0, ptr = priv->conns; *ptr; n++, ptr++) {
                tmp[n] = nfc_peer_connection_ref(*ptr);
            }
            tmp[n] = NULL;

            /* Disconnect all connections and release temporary refs */
            for (ptr = tmp; *ptr; ptr++) {
                nfc_peer_connection_disconnect(*ptr);
                nfc_peer_connection_unref(*ptr);
            }

            g_free(tmp);
        }
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

void
nfc_peer_service_init_base(
    NfcPeerService* self,
    const char* name)
{
    NfcPeerServicePriv* priv = self->priv;

    GASSERT(!self->name);
    if (name) {
        if (!strcmp(name, NFC_LLC_NAME_SNEP)) {
            self->name = NFC_LLC_NAME_SNEP;
            self->sap = NFC_LLC_SAP_SNEP;
        } else {
            self->name = priv->name = g_strdup(name);
        }
    }
}

NfcPeerConnection*
nfc_peer_service_new_connect(
    NfcPeerService* self,
    guint8 rsap,
    const char* name)
{
    NfcPeerConnection* pc = GET_THIS_CLASS(self)->new_connect(self,
        rsap, name);

    /* Make sure the state is right */
    nfc_peer_connection_set_state(pc, NFC_LLC_CO_CONNECTING);
    return pc;
}

NfcPeerConnection*
nfc_peer_service_new_accept(
    NfcPeerService* self,
    guint8 rsap)
{
    NfcPeerConnection* pc = GET_THIS_CLASS(self)->new_accept(self, rsap);

    /* Make sure the state is right */
    nfc_peer_connection_set_state(pc, NFC_LLC_CO_ACCEPTING);
    return pc;
}

void
nfc_peer_service_connection_created(
    NfcPeerService* self,
    NfcPeerConnection* connection)
{
    NfcPeerServicePriv* priv = self->priv;
    guint n = 0;

    if (priv->conns) {
        NfcPeerConnection** ptr = priv->conns;

        while (*ptr++) n++;
    }
    priv->conns = g_renew(NfcPeerConnection*, priv->conns, n + 2);
    priv->conns[n] = connection;
    priv->conns[n + 1] = NULL;
}

void
nfc_peer_service_connection_dead(
    NfcPeerService* self,
    NfcPeerConnection* pc)
{
    NfcPeerServicePriv* priv = self->priv;
    int pos = -1;
    guint n = 0;

    if (priv->conns) {
        NfcPeerConnection** ptr = priv->conns;

        while (*ptr) {
            if (*ptr++ == pc) {
                pos = n;
            }
            n++;
        }
    }
    if (pos == 0 && n == 1) {
        g_free(priv->conns);
        priv->conns = NULL;
    } else if (pos >= 0) {
        memmove(priv->conns + pos, priv->conns + pos + 1,
            sizeof(NfcPeerConnection*) * (n - pos) /* Copy NULL too */);
        priv->conns = g_renew(NfcPeerConnection*, priv->conns, n);
    }
}

void
nfc_peer_service_peer_arrived(
    NfcPeerService* self,
    NfcPeer* peer)
{
    GET_THIS_CLASS(self)->peer_arrived(self, peer);
}

void
nfc_peer_service_peer_left(
    NfcPeerService* self,
    NfcPeer* peer)
{
    GET_THIS_CLASS(self)->peer_left(self, peer);
}

void
nfc_peer_service_datagram_received(
    NfcPeerService* self,
    guint8 ssap,
    const void* data,
    guint len)
{
    GET_THIS_CLASS(self)->datagram_received(self, ssap, data, len);
}

/*==========================================================================*
 * Default methods
 *==========================================================================*/

static
void
nfc_peer_service_default_peer_callback(
    NfcPeerService* self,
    NfcPeer* peer)
{
}

static
NfcPeerConnection*
nfc_peer_service_default_new_connect(
    NfcPeerService* self,
    guint8 rsap,
    const char* name)
{
    return NULL;
}

static
NfcPeerConnection*
nfc_peer_service_default_new_accept(
    NfcPeerService* self,
    guint8 rsap)
{
    return NULL;
}

static
void
nfc_peer_service_default_datagram_received(
    NfcPeerService* service,
    guint8 ssap,
    const void* data,
    guint len)
{
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_peer_service_init(
    NfcPeerService* self)
{
    NfcPeerServicePriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcPeerServicePriv);

    self->priv = priv;
}

static
void
nfc_peer_service_finalize(
    GObject* object)
{
    NfcPeerService* self = NFC_PEER_SERVICE(object);
    NfcPeerServicePriv* priv = self->priv;

    g_free(priv->name);
    g_free(priv->conns);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_peer_service_class_init(
    NfcPeerServiceClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcPeerServicePriv));
    klass->peer_arrived = nfc_peer_service_default_peer_callback;
    klass->peer_left = nfc_peer_service_default_peer_callback;
    klass->new_connect = nfc_peer_service_default_new_connect;
    klass->new_accept = nfc_peer_service_default_new_accept;
    klass->datagram_received = nfc_peer_service_default_datagram_received;
    G_OBJECT_CLASS(klass)->finalize = nfc_peer_service_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
