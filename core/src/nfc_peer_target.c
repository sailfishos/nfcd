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

#include "nfc_peer_p.h"
#include "nfc_initiator_p.h"
#include "nfc_llc.h"
#include "nfc_llc_io.h"

#define GLOG_MODULE_NAME NFC_PEER_LOG_MODULE
#include <gutil_log.h>

typedef struct nfc_peer_target {
    NfcPeer peer;
    NfcLlcIo* llc_io;
    NfcInitiator* initiator;
    gulong gone_id;
} NfcPeerTarget;

#define THIS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), THIS_TYPE, NfcPeerTarget))
#define THIS_TYPE (nfc_peer_target_get_type())
#define PARENT_TYPE NFC_TYPE_PEER
#define PARENT_CLASS (nfc_peer_target_parent_class)

typedef NfcPeerClass NfcPeerTargetClass;
G_DEFINE_TYPE(NfcPeerTarget, nfc_peer_target, PARENT_TYPE)

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
nfc_peer_target_gone(
    NfcInitiator* initiator,
    void* user_data)
{
    /* NfcInitiator makes sure that this signal is only issued once */
    nfc_peer_gone(NFC_PEER(user_data));
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcPeer*
nfc_peer_new_target(
    NfcInitiator* initiator,
    NFC_TECHNOLOGY technology,
    const NfcParamNfcDepTarget* nfc_dep,
    NfcPeerServices* services)
{
    if (G_LIKELY(initiator) && G_LIKELY(nfc_dep)) {
        NfcPeerTarget* self = g_object_new(THIS_TYPE, NULL);
        NfcPeer* peer = &self->peer;

        self->initiator = nfc_initiator_ref(initiator);
        self->llc_io = nfc_llc_io_target_new(initiator);
        if (nfc_peer_init_base(peer, self->llc_io, &nfc_dep->atr_req_g,
            services, technology, NFC_PEER_FLAGS_NONE)) {
            peer->present = initiator->present;
            self->gone_id = nfc_initiator_add_gone_handler(initiator,
                nfc_peer_target_gone, self);
            return peer;
        }
        g_object_unref(self);
    }
    return NULL;
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
void
nfc_peer_target_deactivate(
    NfcPeer* peer)
{
    NfcPeerTarget* self = THIS(peer);

    nfc_initiator_deactivate(self->initiator);
    NFC_PEER_CLASS(PARENT_CLASS)->deactivate(peer);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_peer_target_init(
    NfcPeerTarget* self)
{
}

static
void
nfc_peer_target_finalize(
    GObject* object)
{
    NfcPeerTarget* self = THIS(object);

    nfc_llc_io_unref(self->llc_io);
    nfc_initiator_remove_handler(self->initiator, self->gone_id);
    nfc_initiator_unref(self->initiator);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_peer_target_class_init(
    NfcPeerTargetClass* klass)
{
    klass->deactivate = nfc_peer_target_deactivate;
    G_OBJECT_CLASS(klass)->finalize = nfc_peer_target_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
