/*
 * Copyright (C) 2020-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2020 Jolla Ltd.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfc_peer_p.h"
#include "nfc_llc.h"
#include "nfc_llc_param.h"
#include "nfc_peer_services.h"
#include "nfc_snep_server.h"

#include <nfcdef.h>

#define GLOG_MODULE_NAME NFC_PEER_LOG_MODULE
#include <gutil_log.h>
#include <gutil_misc.h>

GLOG_MODULE_DEFINE2("nfc-peer", NFC_CORE_LOG_MODULE);

enum {
    LLC_EVENT_STATE,
    LLC_EVENT_IDLE,
    LLC_EVENT_WKS,
    LLC_EVENT_COUNT
};

enum {
    SNEP_EVENT_NDEF,
    SNEP_EVENT_STATE,
    SNEP_EVENT_COUNT
};

typedef struct nfc_peer_connect {
    NfcPeer* peer;
    NfcPeerConnectFunc complete;
    void* user_data;
    GDestroyNotify destroy;
} NfcPeerConnect;

struct nfc_peer_priv {
    NfcLlc* llc;
    NfcPeerServices* services;
    NfcSnepServer* snep;
    char* name;
    gulong llc_event_id[LLC_EVENT_COUNT];
    gulong snep_event_id[SNEP_EVENT_COUNT];
    gboolean ndef_reception_started;
};

#define THIS(obj) NFC_PEER(obj)
#define THIS_TYPE NFC_TYPE_PEER
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS nfc_peer_parent_class
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, NFC_TYPE_PEER, \
        NfcPeerClass)

G_DEFINE_ABSTRACT_TYPE(NfcPeer, nfc_peer, PARENT_TYPE)

enum nfc_peer_signal {
    SIGNAL_WKS_CHANGED,
    SIGNAL_NDEF_CHANGED,
    SIGNAL_INITIALIZED,
    SIGNAL_GONE,
    SIGNAL_COUNT
};

#define SIGNAL_WKS_CHANGED_NAME     "nfc-peer-wks-changed"
#define SIGNAL_NDEF_CHANGED_NAME    "nfc-peer-ndef-changed"
#define SIGNAL_INITIALIZED_NAME     "nfc-peer-initialized"
#define SIGNAL_GONE_NAME            "nfc-peer-gone"

static guint nfc_peer_signals[SIGNAL_COUNT] = { 0 };
static const guint8 LLCP_MAGIC[] = { 0x46, 0x66, 0x6d };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
NfcPeerConnect*
nfc_peer_connect_new(
    NfcPeer* peer,
    NfcPeerConnectFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    NfcPeerConnect* connect = g_slice_new(NfcPeerConnect);

    g_object_ref(connect->peer = peer);
    connect->complete = complete;
    connect->user_data = user_data;
    connect->destroy = destroy;
    return connect;
}

static
void
nfc_peer_connect_free(
    NfcPeerConnect* connect)
{
    if (connect->destroy) {
        connect->destroy(connect->user_data);
    }
    g_object_unref(connect->peer);
    g_slice_free1(sizeof(*connect), connect);
}

static
void
nfc_peer_connect_complete(
    NfcPeerConnection* pc,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data)
{
    NfcPeerConnect* connect = user_data;

    if (connect->complete) {
        connect->complete(connect->peer, pc, result, connect->user_data);
    }
}

static
void
nfc_peer_disconnect_handlers(
    NfcPeer* self)
{
    NfcPeerPriv* priv = self->priv;

    nfc_snep_server_remove_all_handlers(priv->snep, priv->snep_event_id);
    nfc_llc_remove_all_handlers(priv->llc, priv->llc_event_id);
}

static
void
nfc_peer_initialized(
    NfcPeer* self)
{
    NfcPeerPriv* priv = self->priv;

    /*
     * Once we are initialized, we are no longer interested in LLC_EVENT_IDLE
     * but we need to keep the the remaining ones (WKS and STATE) registered.
     */
    if (priv->llc_event_id[LLC_EVENT_IDLE]) {
        nfc_llc_remove_handler(priv->llc, priv->llc_event_id[LLC_EVENT_IDLE]);
        priv->llc_event_id[LLC_EVENT_IDLE] = 0;
    }
    nfc_snep_server_remove_all_handlers(priv->snep, priv->snep_event_id);
    if (!(self->flags & NFC_PEER_FLAG_INITIALIZED)) {
        GDEBUG("Peer initialized");
        self->flags |= NFC_PEER_FLAG_INITIALIZED;
        g_signal_emit(self, nfc_peer_signals[SIGNAL_INITIALIZED], 0);

        /* Notify the services */
        nfc_peer_services_peer_arrived(priv->services, self);
    }
}

static
void
nfc_peer_check_ndef_reception_state(
    NfcPeer* self)
{
    NfcPeerPriv* priv = self->priv;
    NfcSnepServer* snep = priv->snep;
    NfcLlc* llc = priv->llc;

    if (snep->ndef || llc->idle) {
        /* Either NDEF has been received or nothing seems to be coming */
        ndef_rec_unref(self->ndef);
        self->ndef = ndef_rec_ref(snep->ndef);
        nfc_peer_initialized(self);
    }
}

static
void
nfc_peer_snep_event(
    NfcSnepServer* snep,
    void* user_data)
{
    nfc_peer_check_ndef_reception_state(NFC_PEER(user_data));
}

static
void
nfc_peer_llc_idle_changed(
    NfcLlc* llc,
    void* user_data)
{
    nfc_peer_check_ndef_reception_state(NFC_PEER(user_data));
}

static
void
nfc_peer_maybe_start_ndef_reception(
    NfcPeer* self)
{
    NfcPeerPriv* priv = self->priv;

    if (!priv->ndef_reception_started) {
        NfcSnepServer* snep = priv->snep;
        NfcLlc* llc = priv->llc;

        priv->ndef_reception_started = TRUE;
        if (snep->ndef || llc->idle) {
            /* Either we already have an NDEF (unlikely) or nothing
             * is happening (more likely). In either case, there seems
             * to be nothing to wait for. */
            ndef_rec_unref(self->ndef);
            self->ndef = ndef_rec_ref(snep->ndef);
            nfc_peer_initialized(self);
        } else {
            /* Wait until NDEF arrives */
            GDEBUG("Waiting for NDEF...");
            priv->snep_event_id[SNEP_EVENT_NDEF] =
                nfc_snep_server_add_ndef_changed_handler(snep,
                    nfc_peer_snep_event, self);
            priv->snep_event_id[SNEP_EVENT_STATE] =
                nfc_snep_server_add_ndef_changed_handler(snep,
                    nfc_peer_snep_event, self);
            priv->llc_event_id[LLC_EVENT_IDLE] =
                nfc_llc_add_idle_changed_handler(priv->llc,
                    nfc_peer_llc_idle_changed, self);
        }
    }
}

static
void
nfc_peer_llc_state_changed(
    NfcLlc* llc,
    void* user_data)
{
    NfcPeer* self = NFC_PEER(user_data);

    nfc_peer_ref(self);
    if (llc->state == NFC_LLC_STATE_ACTIVE) {
        nfc_peer_maybe_start_ndef_reception(self);
    } else if (llc->state != NFC_LLC_STATE_START) {
        /* Some kind of LLCP error has occurred, deactivate the interface */
        nfc_peer_disconnect_handlers(self);
        nfc_peer_deactivate(self);
    }
    nfc_peer_unref(self);
}

static
void
nfc_peer_wks_changed(
    NfcLlc* llc,
    void* user_data)
{
    NfcPeer* self = NFC_PEER(user_data);

    self->wks = llc->wks;
    g_signal_emit(self, nfc_peer_signals[SIGNAL_WKS_CHANGED], 0);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcPeer*
nfc_peer_ref(
    NfcPeer* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_peer_unref(
    NfcPeer* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

void
nfc_peer_deactivate(
    NfcPeer* self)
{
    if (G_LIKELY(self)) {
        GET_THIS_CLASS(self)->deactivate(self);
    }
}

gulong
nfc_peer_add_wks_changed_handler(
    NfcPeer* self,
    NfcPeerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_WKS_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_peer_add_ndef_changed_handler(
    NfcPeer* self,
    NfcPeerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_NDEF_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_peer_add_initialized_handler(
    NfcPeer* self,
    NfcPeerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_INITIALIZED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_peer_add_gone_handler(
    NfcPeer* self,
    NfcPeerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_GONE_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_peer_remove_handler(
    NfcPeer* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_peer_remove_handlers(
    NfcPeer* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

gboolean
nfc_peer_register_service(
    NfcPeer* self,
    NfcPeerService* service)
{
    if (G_LIKELY(self)) {
        NfcPeerPriv* priv = self->priv;

        return nfc_peer_services_add(priv->services, service);
    }
    return FALSE;
}

void
nfc_peer_unregister_service(
    NfcPeer* self,
    NfcPeerService* service)
{
    if (G_LIKELY(self)) {
        NfcPeerPriv* priv = self->priv;

        nfc_peer_services_remove(priv->services, service);
    }
}

NfcPeerConnection*
nfc_peer_connect(
    NfcPeer* self,
    NfcPeerService* service,
    guint rsap,
    NfcPeerConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(self)) {
        NfcPeerPriv* priv = self->priv;
        NfcPeerConnect* connect = nfc_peer_connect_new(self, complete,
            user_data, destroy);
        NfcPeerConnection* pc = nfc_llc_connect(priv->llc, service, rsap,
            nfc_peer_connect_complete, (GDestroyNotify) nfc_peer_connect_free,
            connect);

        if (pc) {
            return pc;
        }
        connect->destroy = NULL;
        nfc_peer_connect_free(connect);
    }
    return NULL;
}

NfcPeerConnection*
nfc_peer_connect_sn(
    NfcPeer* self,
    NfcPeerService* service,
    const char* sn,
    NfcPeerConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(self)) {
        NfcPeerPriv* priv = self->priv;
        NfcPeerConnect* connect = nfc_peer_connect_new(self, complete,
            user_data, destroy);
        NfcPeerConnection* pc = nfc_llc_connect_sn(priv->llc, service, sn,
            nfc_peer_connect_complete, (GDestroyNotify) nfc_peer_connect_free,
            connect);

        if (pc) {
            return pc;
        }
        connect->destroy = NULL;
        nfc_peer_connect_free(connect);
    }
    return NULL;
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

gboolean
nfc_peer_init_base(
    NfcPeer* self,
    NfcLlcIo* llc_io,
    const GUtilData* gb, /* ATR_RES/ATR_REQ General Bytes */
    NfcPeerServices* services,
    NFC_TECHNOLOGY technology,
    NFC_PEER_FLAGS flags)
{
    /*
     * Check LLCP Magic Number
     *
     * NFCForum-TS-LLCP_1.1
     * 6.2.3.1 Link Activation procedure for the Initiator
     */
    if (gb->size >= sizeof(LLCP_MAGIC) &&
       !memcmp(gb->bytes, LLCP_MAGIC, sizeof(LLCP_MAGIC))) {
        NfcPeerPriv* priv = self->priv;
        GUtilData tlvs;
        NfcLlcParam** params;
        NfcLlc* llc;

        GDEBUG("NFC-DEP %s", (flags & NFC_PEER_FLAG_INITIATOR) ?
            "Initiator" : "Target");

        priv->services = services ?
            nfc_peer_services_copy(services) :
            nfc_peer_services_new();

        tlvs.bytes = gb->bytes + sizeof(LLCP_MAGIC);
        tlvs.size = gb->size - sizeof(LLCP_MAGIC);
        params = nfc_llc_param_decode(&tlvs);
        nfc_peer_services_add(priv->services, NFC_PEER_SERVICE(priv->snep));
        priv->llc = llc = nfc_llc_new(llc_io, priv->services,
            nfc_llc_param_constify(params));
        priv->llc_event_id[LLC_EVENT_STATE] =
            nfc_llc_add_state_changed_handler(llc,
                nfc_peer_llc_state_changed, self);
        priv->llc_event_id[LLC_EVENT_WKS] =
            nfc_llc_add_wks_changed_handler(llc,
                nfc_peer_wks_changed, self);
        nfc_llc_param_free(params);
        self->wks = llc->wks;
        self->technology = technology;
        self->flags = flags;
        switch (llc->state) {
        case NFC_LLC_STATE_START:
        case NFC_LLC_STATE_ACTIVE:
            return TRUE;
        case NFC_LLC_STATE_ERROR:
        case NFC_LLC_STATE_PEER_LOST:
            break;
        }
    } else {
        GDEBUG("No LLCP magic found");
    }
    return FALSE;
}

void
nfc_peer_set_name(
    NfcPeer* self,
    const char* name)
{
    NfcPeerPriv* priv = self->priv;

    g_free(priv->name);
    self->name = priv->name = g_strdup(name);
}

void
nfc_peer_gone(
    NfcPeer* self)
{
    nfc_peer_ref(self);
    GET_THIS_CLASS(self)->gone(self);
    nfc_peer_unref(self);
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
void
nfc_peer_nop(
    NfcPeer* self)
{
}

static
void
nfc_peer_default_gone(
    NfcPeer* self)
{
    /* Must only be invoked once per lifetime */
    GASSERT(self->present);
    self->present = FALSE;
    if (self->flags & NFC_PEER_FLAG_INITIALIZED) {
        NfcPeerPriv* priv = self->priv;

        /* Notify the services */
        nfc_peer_services_peer_left(priv->services, self);
    }
    g_signal_emit(self, nfc_peer_signals[SIGNAL_GONE], 0);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_peer_init(
    NfcPeer* self)
{
    NfcPeerPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcPeerPriv);

    self->priv = priv;
    priv->snep = nfc_snep_server_new();
}

static
void
nfc_peer_finalize(
    GObject* object)
{
    NfcPeer* self = THIS(object);
    NfcPeerPriv* priv = self->priv;
    NfcSnepServer* snep = priv->snep;

    ndef_rec_unref(self->ndef);
    nfc_peer_disconnect_handlers(self);
    nfc_peer_service_unref(&snep->service);
    nfc_peer_services_unref(priv->services);
    nfc_llc_free(priv->llc);
    g_free(priv->name);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_peer_class_init(
    NfcPeerClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    g_type_class_add_private(klass, sizeof(NfcPeerPriv));
    klass->deactivate = nfc_peer_nop;
    klass->gone = nfc_peer_default_gone;
    G_OBJECT_CLASS(klass)->finalize = nfc_peer_finalize;
    nfc_peer_signals[SIGNAL_WKS_CHANGED] =
        g_signal_new(SIGNAL_WKS_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_peer_signals[SIGNAL_NDEF_CHANGED] =
        g_signal_new(SIGNAL_NDEF_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_peer_signals[SIGNAL_INITIALIZED] =
        g_signal_new(SIGNAL_INITIALIZED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_peer_signals[SIGNAL_GONE] =
        g_signal_new(SIGNAL_GONE_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
