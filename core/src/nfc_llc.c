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
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfc_llc.h"
#include "nfc_llc_io.h"
#include "nfc_llc_param.h"
#include "nfc_peer_connection_p.h"
#include "nfc_peer_service_p.h"
#include "nfc_peer_services.h"

#define GLOG_MODULE_NAME NFC_LLC_LOG_MODULE
#include <gutil_log.h>
#include <gutil_idlepool.h>
#include <gutil_misc.h>
#include <gutil_macros.h>

#include <glib-object.h>
#include <stdlib.h>

GLOG_MODULE_DEFINE2("llc", NFC_CORE_LOG_MODULE);

typedef enum llcp_ptype {
    LLCP_PTYPE_SYMM = 0x00,
    LLCP_PTYPE_PAX = 0x01,
    LLCP_PTYPE_AGF = 0x02,
    LLCP_PTYPE_UI = 0x03,
    LLCP_PTYPE_CONNECT = 0x04,
    LLCP_PTYPE_DISC = 0x05,
    LLCP_PTYPE_CC = 0x06,
    LLCP_PTYPE_DM = 0x07,
    LLCP_PTYPE_FRMR = 0x08,
    LLCP_PTYPE_SNL = 0x09, /* LLCP 1.1 */
    /* Reserved 0x0a*/
    /* Reserved 0x0b*/
    LLCP_PTYPE_I = 0x0c,
    LLCP_PTYPE_RR = 0x0d,
    LLCP_PTYPE_RNR = 0x0e
} LLCP_PTYPE;

typedef enum llc_frmr_flags {
    NFC_LLC_FRMR_S = 0x01,
    NFC_LLC_FRMR_R = 0x02,
    NFC_LLC_FRMR_I = 0x04,
    NFC_LLC_FRMR_W = 0x08
} NFC_LLC_FRMR_FLAGS;

#define LLCP_MAKE_HDR(dsap,ptype,ssap) \
    (((((guint16)(dsap)) & 0x3f) << 10) | \
     (((guint16)(ptype)) << 6) /* Assuming PTYPE is within range */| \
     (((guint16)(ssap)) & 0x3f))
#define LLCP_GET_DSAP(hdr) ((guint8)((hdr) >> 10))
#define LLCP_GET_PTYPE(hrd) ((LLCP_PTYPE)(((hdr) >> 6) & 0x0f))
#define LLCP_GET_SSAP(hdr)  ((guint8)((hdr) & 0x3f))

enum nfc_llc_io_events {
    LLC_IO_EVENT_CAN_SEND,
    LLC_IO_EVENT_RECEIVE,
    LLC_IO_EVENT_ERROR,
    LLC_IO_EVENT_COUNT
};

typedef struct nfc_llc_connect_req {
    NfcPeerConnection* connection;
    NfcLlcConnectFunc complete;
    GDestroyNotify destroy;
    void* user_data;
} NfcLlcConnectReq;

typedef struct nfc_llc_object {
    GObject object;
    NfcLlc pub;
    NfcLlcIo* io;
    gulong io_event[LLC_IO_EVENT_COUNT];
    GUtilIdlePool* pool;
    NfcPeerServices* services;
    guint8 version;
    guint miu;
    guint lto;
    guint packets_handled;
    GList* pdu_queue;
    GSList* connect_queue;
    GHashTable* conn_table;
} NfcLlcObject;

typedef GObjectClass NfcLlcObjectClass;
GType nfc_llc_object_get_type(void) NFCD_INTERNAL;
G_DEFINE_TYPE(NfcLlcObject, nfc_llc_object, G_TYPE_OBJECT)

#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, THIS_TYPE, NfcLlcObject)
#define THIS_TYPE nfc_llc_object_get_type()
#define PARENT_CLASS nfc_llc_object_parent_class

typedef enum nfc_llc_signal {
    SIGNAL_STATE_CHANGED,
    SIGNAL_IDLE_CHANGED,
    SIGNAL_WKS_CHANGED,
    SIGNAL_COUNT
} NFC_LLC_SIGNAL;

typedef struct nfc_llc_closure {
    GCClosure cclosure;
    NfcLlcFunc func;
    void* user_data;
} NfcLlcClosure;

#define nfc_llc_closure_new() ((NfcLlcClosure*) \
    g_closure_new_simple(sizeof(NfcLlcClosure), NULL))

#define SIGNAL_STATE_CHANGED_NAME "nfc-llc-state-changed"
#define SIGNAL_IDLE_CHANGED_NAME  "nfc-llc-idle-changed"
#define SIGNAL_WKS_CHANGED_NAME   "nfc-llc-wks-changed"

static guint nfc_llc_signals[SIGNAL_COUNT] = { 0 };

static
void
nfc_llc_send_next_pdu(
    NfcLlcObject* self);

static
gboolean
nfc_llc_handle_pdu(
    NfcLlcObject* self,
    const void* data,
    gsize len);

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
inline
NfcLlcObject*
nfc_llc_object_cast(
    NfcLlc* llc)
{
    return llc ? THIS(G_CAST(llc,NfcLlcObject,pub)) : NULL;
}

static
void
nfc_llc_closure_callback(
    NfcLlcObject* self,
    NfcLlcClosure* closure)
{
    closure->func(&self->pub, closure->user_data);
}

static
gulong
nfc_llc_add_handler(
    NfcLlc* llc,
    NFC_LLC_SIGNAL signal,
    NfcLlcFunc func,
    void* user_data)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self) && G_LIKELY(func)) {
        NfcLlcClosure* closure = nfc_llc_closure_new();
        GCClosure* cc = &closure->cclosure;

        cc->closure.data = closure;
        cc->callback = G_CALLBACK(nfc_llc_closure_callback);
        closure->func = func;
        closure->user_data = user_data;
        return g_signal_connect_closure_by_id(self, nfc_llc_signals
            [signal], 0, &cc->closure, FALSE);
    }
    return 0;
}

static
void
nfc_llc_connection_destroy(
    gpointer user_data)
{
    NfcPeerConnection* conn = NFC_PEER_CONNECTION(user_data);

    nfc_peer_connection_set_llc(conn, NULL);
    nfc_peer_connection_unref(conn);
}

static
void
nfc_llc_abort_all_connections(
    NfcLlcObject* self)
{
    GHashTableIter it;
    GSList* conns = NULL;
    GSList* l;
    gpointer value;

    g_hash_table_iter_init(&it, self->conn_table);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        conns = g_slist_append(conns, nfc_peer_connection_ref(value));
    }
    for (l = conns; l; l = l->next) {
        nfc_peer_connection_set_state(NFC_PEER_CONNECTION(l->data),
            NFC_LLC_CO_DEAD);
    }
    g_slist_free_full(conns, g_object_unref);
}

#if GUTIL_LOG_DEBUG
static
const char*
nfc_llc_state_name(
    NfcLlcObject* self,
    NFC_LLC_STATE state)
{
    char* tmp;

    switch (state) {
    case NFC_LLC_STATE_START: return "START";
    case NFC_LLC_STATE_ACTIVE: return "ACTIVE";
    case NFC_LLC_STATE_ERROR: return "ERROR";
    case NFC_LLC_STATE_PEER_LOST: return "PEER_LOST";
    }
    tmp = g_strdup_printf("%d (?)", state);
    gutil_idle_pool_add(self->pool, tmp, g_free);
    return tmp;
}
#endif /* GUTIL_LOG_DEBUG */

static
NfcLlcConnectReq*
nfc_llc_connect_req_new(
    NfcLlcObject* self,
    NfcPeerService* service,
    guint8 rsap,
    const char* rname,
    NfcLlcConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    NfcPeerConnection* pc = nfc_peer_service_new_connect(service, rsap, rname);

    if (pc) {
        NfcLlcConnectReq* req = g_slice_new(NfcLlcConnectReq);

        nfc_peer_connection_set_llc(pc, &self->pub);
        req->connection = pc;
        req->complete = complete;
        req->destroy = destroy;
        req->user_data = user_data;
        return req;
    }
    return NULL;
}

static
void
nfc_llc_connect_req_free(
    NfcLlcConnectReq* req)
{
    if (req->destroy) {
        req->destroy(req->user_data);
    }
    nfc_peer_connection_unref(req->connection);
    g_slice_free1(sizeof(*req), req);
}

static
GBytes*
nfc_llc_dequeue_pdu(
    NfcLlcObject* self)
{
    GList* first = g_list_first(self->pdu_queue);

    if (first) {
        GBytes* bytes = first->data;

        self->pdu_queue = g_list_delete_link(self->pdu_queue, first);
        return bytes;
    }
    return NULL;
}

static
guint
nfc_llc_apply_params(
    NfcLlcObject* self,
    const NfcLlcParam* const* params)
{
    guint mask = 0;

    if (params) {
        NfcLlc* llc = &self->pub;
        const NfcLlcParam* const* ptr = params;

        while (*ptr) {
            const NfcLlcParam* param = *ptr++;
            const NfcLlcParamValue* value = &param->value;

            switch (param->type) {
            case NFC_LLC_PARAM_VERSION:
                if (self->version != value->version) {
                    self->version = value->version;
                    mask |= (1 << param->type);
                }
                GDEBUG("  Version: %u.%u", self->version >> 4,
                    self->version & 0x0f);
                break;
            case NFC_LLC_PARAM_MIUX:
                if (self->miu != value->miu) {
                    self->miu = value->miu;
                    mask |= (1 << param->type);
                }
                GDEBUG("  MIU: %u bytes", self->miu);
                break;
            case NFC_LLC_PARAM_WKS:
                if (llc->wks != value->wks) {
                    llc->wks = value->wks;
                    mask |= (1 << param->type);
                }
                GDEBUG("  WKS: 0x%04x", llc->wks);
                break;
            case NFC_LLC_PARAM_LTO:
                if (self->lto != value->lto) {
                    self->lto = value->lto;
                    mask |= (1 << param->type);
                }
                GDEBUG("  Link Timeout: %u ms", self->lto);
                break;
            default:
                break;
            }
        }
    }
    return mask;
}

static
GByteArray*
nfc_llc_pdu_new(
    guint8 dsap,
    LLCP_PTYPE ptype,
    guint8 ssap)
{
    const guint hdr = LLCP_MAKE_HDR(dsap, ptype, ssap);
    GByteArray* pdu = g_byte_array_new();

    g_byte_array_set_size(pdu, 2);
    pdu->data[0] = (guint8)(hdr >> 8);
    pdu->data[1] = (guint8)hdr;
    return pdu;
}

static
void
nfc_llc_submit(
    NfcLlcObject* self,
    GBytes* pdu)
{
    self->pdu_queue = g_list_append(self->pdu_queue, g_bytes_ref(pdu));
    if (self->io->can_send) {
        nfc_llc_send_next_pdu(self);
    }
}

static
void
nfc_llc_submit_frmr(
    NfcLlcObject* self,
    const guint8 dsap,
    const guint8 ssap,
    NFC_LLC_FRMR_FLAGS flags,
    LLCP_PTYPE ptype,
    guint8 seq,
    NfcPeerConnection* conn)
{
    const guint hdr = LLCP_MAKE_HDR(dsap, LLCP_PTYPE_FRMR, ssap);
    const guint size = 6;
    guint8* pkt = g_malloc(size);
    GBytes* pdu = g_bytes_new_take(pkt, size);

    pkt[0] = (guint8)(hdr >> 8);
    pkt[1] = (guint8)hdr;
    pkt[2] = (guint8)((flags << 4) | ptype);
    pkt[3] = seq;
    if (conn) {
        const NfcPeerConnectionLlcpState* ps = nfc_peer_connection_ps(conn);

        pkt[4] = (guint8)((ps->vs << 4) | ps->vr);
        pkt[5] = (guint8)((ps->vsa << 4) | ps->vra);
    } else {
        pkt[4] = pkt[5] = 0;
    }
    nfc_llc_submit(self, pdu);
    g_bytes_unref(pdu);
}

static
inline
void
nfc_llc_submit_frmr_i(
    NfcLlcObject* self,
    const guint8 dsap,
    const guint8 ssap,
    guint8 ptype)
{
    nfc_llc_submit_frmr(self, dsap, ssap, NFC_LLC_FRMR_I, ptype, 0, NULL);
}

static
void
nfc_llc_submit_connect(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    const NfcLlcParam* const* params)
{
    GByteArray* bytes = nfc_llc_pdu_new(dsap, LLCP_PTYPE_CONNECT, ssap);
    GBytes* pdu;

    nfc_llc_param_encode(params, bytes, self->miu);
    pdu = g_byte_array_free_to_bytes(bytes);
    nfc_llc_submit(self, pdu);
    g_bytes_unref(pdu);
}

static
void
nfc_llc_submit_next_connect(
    NfcLlcObject* self)
{
    if (self->connect_queue) {
        NfcLlcConnectReq* req = self->connect_queue->data;
        NfcPeerConnection* conn = req->connection;
        NfcPeerService* service = conn->service;
        const NfcLlcParam* const* lp = nfc_peer_connection_lp(conn);
        const guint8 lsap = service->sap;

        if (conn->name) {
            NfcLlcParam sn;
            const guint n = nfc_llc_param_count(lp);
            const NfcLlcParam** params = g_new(const NfcLlcParam*, n + 2);
            guint i;

            /* Add SN parameter */
            memset(&sn, 0, sizeof(sn));
            sn.type = NFC_LLC_PARAM_SN;
            sn.value.sn = conn->name;
            for (i = 0; i < n; i++) {
                params[i] = lp[i];
            }
            params[i++] = &sn;
            params[i] = NULL;

            /* Submit CONNECT */
            nfc_llc_submit_connect(self, NFC_LLC_SAP_SDP, lsap, params);
            g_free(params);
        } else {
            /* CONNECT to the particular SAP */
            nfc_llc_submit_connect(self, conn->rsap, lsap, lp);
        }
    }
}

static
void
nfc_llc_submit_disc(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap)
{
    const guint hdr = LLCP_MAKE_HDR(dsap, LLCP_PTYPE_DISC, ssap);
    const guint size = 2;
    guint8* pkt = g_malloc(size);
    GBytes* pdu = g_bytes_new_take(pkt, size);

    pkt[0] = (guint8)(hdr >> 8);
    pkt[1] = (guint8)hdr;
    nfc_llc_submit(self, pdu);
    g_bytes_unref(pdu);
}

static
void
nfc_llc_submit_dm(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    NFC_LLC_DM_REASON reason)
{
    const guint hdr = LLCP_MAKE_HDR(dsap, LLCP_PTYPE_DM, ssap);
    const guint size = 3;
    guint8* pkt = g_malloc(size);
    GBytes* pdu = g_bytes_new_take(pkt, size);

    pkt[0] = (guint8)(hdr >> 8);
    pkt[1] = (guint8)hdr;
    pkt[2] = reason;
    nfc_llc_submit(self, pdu);
    g_bytes_unref(pdu);
}

static
void
nfc_llc_ack_internal(
    NfcLlcObject* self,
    NfcPeerConnection* conn,
    gboolean last)
{
    NfcPeerConnectionLlcpState* ps = nfc_peer_connection_ps(conn);

    /*
     * 5.6.1.4 Receive Acknowledgement State Variable V(RA)
     *
     * The receive acknowledgement state variable V(RA) SHALL
     * denote the most recently sent N(R) value for a specific
     * data link connection.
     */
    if (conn->state == NFC_LLC_CO_ACTIVE && ps->vra != ps->vr) {
        NfcPeerService* service = conn->service;
        const LLCP_PTYPE ptype = last ? LLCP_PTYPE_RNR : LLCP_PTYPE_RR;
        guint8 dsap = conn->rsap;
        guint8 ssap = service->sap;
        const guint hdr = LLCP_MAKE_HDR(dsap, ptype, ssap);
        const guint size = 3;
        guint8* pkt = g_malloc(size);
        GBytes* pdu = g_bytes_new_take(pkt, size);

        /* Ack the last PDU */
        ps->vra = ps->vr;
        pkt[0] = (guint8)(hdr >> 8);
        pkt[1] = (guint8)hdr;
        pkt[2] = ps->vra;
        nfc_llc_submit(self, pdu);
        g_bytes_unref(pdu);
    }
}

static
void
nfc_llc_handle_connect(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    const void* plist,
    guint plen)
{
    NfcPeerService* service = NULL;
    NfcLlcParam** params = nfc_llc_param_decode_bytes(plist, plen);

    if (dsap == NFC_LLC_SAP_SDP) {
        /*
         * NFCForum-TS-LLCP_1.1
         * 4.5.6 Service Name, SN
         *
         * The service name (SN) parameter MAY be transmitted with a
         * CONNECT PDU to the well-known destination service access
         * point address 01h and SHALL then indicate that the sending
         * LLC intends to establish a data link connection with the
         * named service registered in the remote service environment.
         * ...
         * If the service name parameter is transmitted with a CONNECT
         * PDU to a destination service access point other than 01h, it
         * SHALL be ignored.
         */
        const NfcLlcParam* sn_param = nfc_llc_param_find
            (nfc_llc_param_constify(params), NFC_LLC_PARAM_SN);

        /* Why would we access connection to SDP SAP (without a name) ? */
        if (sn_param) {
            const char* sn = sn_param->value.sn;

            GDEBUG("  SN: \"%s\"", sn);
            service = nfc_peer_services_find_sn(self->services, sn);
            if (service) {
                /* Resolved SAP */
                GDEBUG("  SAP: %u", service->sap);
                dsap = service->sap;
            } else {
                GDEBUG("Service \"%s\" NOT FOUND", sn);
            }
        } else {
            GDEBUG("Rejecting connection to SDP SAP");
        }
    } else {
        service = nfc_peer_services_find_sap(self->services, dsap);
        if (!service) {
            GDEBUG("No service at SAP %u", dsap);
        }
    }
    if (service) {
        /* Check for existing connection */
        const gpointer key = LLCP_CONN_KEY(dsap, ssap);
        NfcPeerConnection* conn = g_hash_table_lookup(self->conn_table, key);

        if (conn) {
            /*
             * NFCForum-TS-LLCP_1.1
             * 5.6.3 Connection Establishment
             *
             * If the local LLC receives a CONNECT PDU and is unable to
             * process the connection request, it SHALL return a DM PDU
             * with the appropriate reason code (cf. Table 4 in Section
             * 4.3.8) to the remote LLC at the earliest opportunity.
             *
             * But what is the appropriate reason code here (CONNECT for
             * already connected SAP)? Table 4 in Section 4.3.8 doesn't
             * have a code which would exactly match this situation.
             */
            GWARN("Duplicate connection %u:%u", ssap, dsap);
            nfc_llc_submit_dm(self, ssap, dsap, NFC_LLC_DM_REJECT);
        } else {
            /* Set up new connection */
            conn = nfc_peer_service_new_accept(service, ssap);
            if (conn) {
                if (conn->state != NFC_LLC_CO_DEAD) {
                    g_hash_table_insert(self->conn_table, key, conn);
                    nfc_peer_connection_set_llc(conn, &self->pub);
                    nfc_peer_connection_apply_remote_params(conn,
                        nfc_llc_param_constify(params));
                    nfc_peer_connection_accept(conn);
                } else {
                    /* Stillborn connection */
                    nfc_llc_submit_dm(self, ssap, dsap, NFC_LLC_DM_REJECT);
                    nfc_peer_connection_unref(conn);
                }
            } else {
                nfc_llc_submit_dm(self, ssap, dsap, NFC_LLC_DM_REJECT);
            }
        }
    } else {
        nfc_llc_submit_dm(self, ssap, dsap, NFC_LLC_DM_NO_SERVICE);
    }
    nfc_llc_param_free(params);
}

static
void
nfc_llc_handle_cc(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    const void* plist,
    guint plen)
{
    NfcLlcConnectReq* req = self->connect_queue ?
        self->connect_queue->data : NULL;

    if (req) {
        NfcPeerConnection* conn = req->connection;
        NfcPeerService* service = conn->service;

        if (service->sap == dsap && (!conn->rsap || conn->rsap == ssap)) {
            gpointer key;

            /* Done with this request */
            self->connect_queue = g_slist_delete_link
                (self->connect_queue, self->connect_queue);

            /* Update remote SAP (key depends on it) */
            conn->rsap = ssap;
            key = nfc_peer_connection_key(conn);

            if (plen > 0) {
                NfcLlcParam** cp = nfc_llc_param_decode_bytes(plist, plen);

                /* Apply connection parameters */
                if (cp) {
                    nfc_peer_connection_apply_remote_params(conn,
                        nfc_llc_param_constify(cp));
                    nfc_llc_param_free(cp);
                }
            }

            /* Complete the request */
            if (req->complete) {
                NfcLlcConnectFunc complete = req->complete;

                req->complete = NULL;
                complete(conn, (conn->state == NFC_LLC_CO_CONNECTING) ?
                    NFC_PEER_CONNECT_OK : NFC_PEER_CONNECT_CANCELLED,
                    req->user_data);
            }

            switch (conn->state) {
            case NFC_LLC_CO_CONNECTING:
                /* Upgrade connection's state to ACTIVE */
                g_hash_table_insert(self->conn_table, key,
                    nfc_peer_connection_ref(conn));
                nfc_peer_connection_set_state(conn, NFC_LLC_CO_ACTIVE);
                break;
            case NFC_LLC_CO_ABANDONED:
                /* We changed our mind (still need to keep connection
                 * in the table for the time being) */
                g_hash_table_insert(self->conn_table, key,
                    nfc_peer_connection_ref(conn));
                GDEBUG("Abandoned %u:%u", service->sap, conn->rsap);
                nfc_llc_submit_disc(self, conn->rsap, service->sap);
                break;
            case NFC_LLC_CO_DISCONNECTING:
            case NFC_LLC_CO_DEAD:
            case NFC_LLC_CO_ACCEPTING:
            case NFC_LLC_CO_ACTIVE:
                break;
            }
            nfc_llc_connect_req_free(req);
            nfc_llc_submit_next_connect(self);
            return;
        }
    }
    GWARN("Unexpected CC");
    nfc_llc_submit_frmr_i(self, ssap, 0, LLCP_PTYPE_CC);
}

static
void
nfc_llc_handle_disc(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap)
{
    const gpointer key = LLCP_CONN_KEY(dsap, ssap);
    NfcPeerConnection* conn = g_hash_table_lookup(self->conn_table, key);

    if (conn) {
        /*
         * NFCForum-TS-LLCP_1.1
         * 5.6.6 Connection Termination
         *
         * ...
         * When receiving a DISC PDU, the LLC SHALL return a DM PDU
         * and pass a disconnect indication to the service access point
         * for that data link connection. The data link connection SHALL
         * then be closed.
         */
        nfc_peer_connection_set_state(conn, NFC_LLC_CO_DEAD);
        GASSERT(!g_hash_table_contains(self->conn_table, key));
        nfc_llc_submit_dm(self, ssap, dsap, NFC_LLC_DM_DISC_RECEIVED);
    } else {
        GWARN("Non-existent connection %u:%u", dsap, ssap);
        nfc_llc_submit_frmr_i(self, ssap, dsap, LLCP_PTYPE_DISC);
    }
}

static
void
nfc_llc_handle_dm(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    guint reason)
{
    const gpointer key = LLCP_CONN_KEY(dsap, ssap);
    NfcPeerConnection* conn = g_hash_table_lookup(self->conn_table, key);

    if (conn) {
        nfc_peer_connection_set_state(conn, NFC_LLC_CO_DEAD);
        GASSERT(!g_hash_table_contains(self->conn_table, key));
        return;
    } else if (self->connect_queue) {
        NfcLlcConnectReq* req = self->connect_queue->data;
        NfcPeerConnection* conn = req->connection;
        NfcPeerService* service = conn->service;

        if (dsap == service->sap) {
            /*
             * NFCForum-TS-LLCP_1.1
             * 5.6 Connection-oriented Transport Mode Procedures
             * 5.6.3 Connection Establishment
             * ...
             * If the LLC receives a DM PDU with a DSAP value equal
             * to the SSAP value of a sent but not yet acknowledged
             * CONNECT PDU, it SHALL abandon connection establishment
             * and report the reason to the service layer.
             */
            self->connect_queue = g_slist_delete_link
                (self->connect_queue, self->connect_queue);

            /* Complete the request */
            if (req->complete) {
                NfcLlcConnectFunc complete = req->complete;
                NFC_PEER_CONNECT_RESULT result;

                if (conn->state == NFC_LLC_CO_ABANDONED) {
                    result = NFC_PEER_CONNECT_CANCELLED;
                } else {
                    result = NFC_PEER_CONNECT_FAILED;
                    switch (reason) {
                    case NFC_LLC_DM_NO_SERVICE:
                        result = NFC_PEER_CONNECT_NO_SERVICE;
                        break;
                    case NFC_LLC_DM_REJECT:
                        result = NFC_PEER_CONNECT_REJECTED;
                        break;
                    case NFC_LLC_DM_DISC_RECEIVED:
                    case NFC_LLC_DM_NOT_CONNECTED:
                        /* NFC_PEER_CONNECT_FAILED */
                        break;
                    }
                }
                req->complete = NULL;
                complete(conn, result, req->user_data);
            }
            nfc_peer_connection_set_state(conn, NFC_LLC_CO_DEAD);
            nfc_llc_connect_req_free(req);
            nfc_llc_submit_next_connect(self);
            return;
        }
    }
    GWARN("Non-existent connection %u:%u", dsap, ssap);
    nfc_llc_submit_frmr_i(self, ssap, dsap, LLCP_PTYPE_DM);
}

static
void
nfc_llc_handle_frmr(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    LLCP_PTYPE ptype)
{
    const gpointer key = LLCP_CONN_KEY(dsap, ssap);
    NfcPeerConnection* conn = g_hash_table_lookup(self->conn_table, key);

    /* Do we need anything more sophisticated than that? */
    if (conn) {
        nfc_peer_connection_set_state(conn, NFC_LLC_CO_DEAD);
        GASSERT(!g_hash_table_contains(self->conn_table, key));
        return;
    } else if (self->connect_queue && ptype == LLCP_PTYPE_CONNECT) {
        NfcLlcConnectReq* req = self->connect_queue->data;
        NfcPeerConnection* conn = req->connection;
        NfcPeerService* service = conn->service;

        if (service->sap == dsap && (!conn->rsap || conn->rsap == ssap)) {
            /* Abort pending connection */
            self->connect_queue = g_slist_delete_link
                (self->connect_queue, self->connect_queue);

            /* Complete the request */
            if (req->complete) {
                NfcLlcConnectFunc complete = req->complete;

                req->complete = NULL;
                complete(conn, NFC_PEER_CONNECT_REJECTED, req->user_data);
            }
            nfc_peer_connection_set_state(conn, NFC_LLC_CO_DEAD);
            nfc_llc_connect_req_free(req);
            nfc_llc_submit_next_connect(self);
        }
    }
}

static
void
nfc_llc_handle_snl(
    NfcLlcObject* self,
    const void* plist,
    guint plen)
{
    GByteArray* pdu_bytes = nfc_llc_pdu_new
        (NFC_LLC_SAP_SDP, LLCP_PTYPE_SNL, NFC_LLC_SAP_SDP);
    GPtrArray* resp_list = g_ptr_array_new_with_free_func(g_free);
    NfcLlcParam** params = nfc_llc_param_decode_bytes(plist, plen);
    NfcLlcParam** ptr = params;
    GBytes* pdu;

    /* Resolve names */
    while (*ptr) {
        const NfcLlcParam* param = *ptr++;

        if (param->type == NFC_LLC_PARAM_SDREQ) {
            const NfcLlcParamSdReq* sdreq = &param->value.sdreq;
            NfcLlcParam* resp_param = g_new(NfcLlcParam, 1);
            NfcLlcParamSdRes* sdres = &resp_param->value.sdres;
            NfcPeerService* svc = nfc_peer_services_find_sn
                (self->services, sdreq->uri);

            resp_param->type = NFC_LLC_PARAM_SDRES;
            sdres->tid = sdreq->tid;
            if (svc) {
                sdres->sap = svc->sap;
                GDEBUG("  \"%s\" => %u", sdreq->uri, sdres->sap);
            } else if (!g_strcmp0(sdreq->uri, NFC_LLC_NAME_SDP)) {
                sdres->sap = NFC_LLC_SAP_SDP;
                GDEBUG("  \"%s\" => %u (built-in)", sdreq->uri, sdres->sap);
            } else {
                sdres->sap = 0;
                GDEBUG("  \"%s\" (unknown)", sdreq->uri);
            }
            g_ptr_array_add(resp_list, resp_param);
        }
    }
    nfc_llc_param_free(params);

    /* Encode response parameters */
    g_ptr_array_add(resp_list, NULL);
    nfc_llc_param_encode((const NfcLlcParam**)resp_list->pdata,
        pdu_bytes, self->miu);
    g_ptr_array_free(resp_list, TRUE);

    /* Submit the packet */
    pdu = g_byte_array_free_to_bytes(pdu_bytes);
    nfc_llc_submit(self, pdu);
    g_bytes_unref(pdu);
}

static
void
nfc_llc_handle_pax(
    NfcLlcObject* self,
    const void* plist,
    guint plen)
{
    NfcLlcParam** params = nfc_llc_param_decode_bytes(plist, plen);
    guint change = nfc_llc_apply_params(self, nfc_llc_param_constify(params));

    nfc_llc_param_free(params);
    /* Signal the change */
    if (change & (1 << NFC_LLC_PARAM_WKS)) {
        g_signal_emit(self, nfc_llc_signals[SIGNAL_WKS_CHANGED], 0);
    }
}

static
gboolean
nfc_llc_handle_agf(
    NfcLlcObject* self,
    const void* data,
    guint size)
{
    const guint8* pkt = data;
    const guint8* end = pkt + size;

    while ((pkt + 1) < end) {
        const guint len = ((guint)pkt[0]) + pkt[1];

        /* Eat the length */
        pkt += 2;

        /* Ignore empty PDUs */
        if (len) {
            /* Make sure we are within the bounds */
            if ((pkt + len) > end) {
                GWARN("Broken AFG frame");
                return FALSE;
            } else {
                /* Handle encapsulated PDU */
                GDEBUG("Handling encapsulated PDU (%u bytes)", len);
                if (!nfc_llc_handle_pdu(self, pkt, len)) {
                    return FALSE;
                }
            }
            pkt += len;
        } else {
            GDEBUG("Skipping empty encapsulated PDU");
        }
    }
    return (pkt == end);
}

static
void
nfc_llc_handle_ui(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    const void* data,
    guint len)
{
    NfcPeerService* svc = nfc_peer_services_find_sap(self->services, dsap);

    if (svc) {
        nfc_peer_service_datagram_received(svc, ssap, data, len);
    } else {
        GDEBUG("No service at SAP %u", dsap);
        nfc_llc_submit_frmr_i(self, ssap, dsap, LLCP_PTYPE_UI);
    }
}

static
void
nfc_llc_handle_i(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    guint8 seq,
    const void* data,
    guint len)
{
    const gpointer key = LLCP_CONN_KEY(dsap, ssap);
    NfcPeerConnection* conn = g_hash_table_lookup(self->conn_table, key);

    if (conn) {
        NfcPeerConnectionLlcpState* ps = nfc_peer_connection_ps(conn);
        const guint8 ns = seq >> 4;
        const guint8 nr = seq & 0x0f;

        /*
         * NFCForum-TS-LLCP_1.1
         * 5.6 Connection-oriented Transport Mode Procedures
         *
         * 5.6.1.2 Send Acknowledgement State Variable V(SA)
         *
         * The send acknowledgement state variable V(SA) SHALL denote
         * the most recently received N(R) value for a specific data
         * link connection.
         */
        ps->vsa = nr;

        /*
         * 5.6.4.2 Receiving I PDUs
         *
         * When an I PDU is received with the send sequence number N(S)
         * equal to the receive state variable V(R), the LLC SHALL pass
         * the service data unit, contained in the information field,
         * to the service access point and increment by one its receive
         * state variable, V(R).
         *
         */
        if (ps->vr == ns) {
            ps->vr = ((ps->vr + 1) & 0x0f);
            nfc_peer_connection_ref(conn);
            nfc_peer_connection_data_received(conn, data, len);
            nfc_llc_ack_internal(self, conn, FALSE);
            nfc_peer_connection_unref(conn);
        } else {
            nfc_llc_submit_frmr(self, ssap, dsap, NFC_LLC_FRMR_S,
                LLCP_PTYPE_I, seq, conn);
        }
        nfc_peer_connection_flush(conn);
    } else {
        nfc_llc_submit_frmr_i(self, ssap, dsap, LLCP_PTYPE_I);
    }
}

static
void
nfc_llc_handle_rr(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    guint8 nr)
{
    const gpointer key = LLCP_CONN_KEY(dsap, ssap);
    NfcPeerConnection* conn = g_hash_table_lookup(self->conn_table, key);

    if (conn) {
        /*
         * NFCForum-TS-LLCP_1.1
         * 5.6 Connection-oriented Transport Mode Procedures
         *
         * 5.6.1.2 Send Acknowledgement State Variable V(SA)
         *
         * The send acknowledgement state variable V(SA) SHALL denote
         * the most recently received N(R) value for a specific data
         * link connection.
         */
        nfc_peer_connection_ps(conn)->vsa = nr;
        nfc_peer_connection_flush(conn);
    } else {
        nfc_llc_submit_frmr_i(self, ssap, dsap, LLCP_PTYPE_RR);
    }
}

static
void
nfc_llc_handle_rnr(
    NfcLlcObject* self,
    guint8 dsap,
    guint8 ssap,
    guint8 nr)
{
    const gpointer key = LLCP_CONN_KEY(dsap, ssap);
    NfcPeerConnection* conn = g_hash_table_lookup(self->conn_table, key);

    if (conn) {
        /*
         * NFCForum-TS-LLCP_1.1
         * 5.6 Connection-oriented Transport Mode Procedures
         *
         * 5.6.1.2 Send Acknowledgement State Variable V(SA)
         *
         * The send acknowledgement state variable V(SA) SHALL denote
         * the most recently received N(R) value for a specific data
         * link connection.
         */
        nfc_peer_connection_ps(conn)->vsa = nr;
        nfc_peer_connection_flush(conn);
#pragma message("TODO: Suspend sending?")
    } else {
        nfc_llc_submit_frmr_i(self, ssap, dsap, LLCP_PTYPE_RNR);
    }
}

static
gboolean
nfc_llc_handle_pdu(
    NfcLlcObject* self,
    const void* data,
    gsize len)
{
    if (len >= 2) {
        const guint8* pkt = data;
        const guint hdr = (((guint)(pkt[0])) << 8) | pkt[1];
        const guint8 dsap = LLCP_GET_DSAP(hdr);
        const LLCP_PTYPE ptype = LLCP_GET_PTYPE(hdr);
        const guint8 ssap = LLCP_GET_SSAP(hdr);

        switch (ptype) {
        case LLCP_PTYPE_SYMM:
            if (len == 2 && dsap == 0 && ssap == 0) {
                GDEBUG("> SYMM");
                return TRUE;
            }
            GDEBUG("> SYMM (malformed?)");
            return FALSE;
        case LLCP_PTYPE_PAX:
            self->packets_handled++;
            if (!dsap && !ssap) {
                GDEBUG("> PAX");
                nfc_llc_handle_pax(self, pkt + 2, len - 2);
            } else {
                GDEBUG("> PAX %u:%u (malformed?)", ssap, dsap);
                nfc_llc_submit_frmr_i(self, ssap, dsap, LLCP_PTYPE_PAX);
            }
            return TRUE;
        case LLCP_PTYPE_AGF:
            if (!dsap && !ssap) {
                self->packets_handled++;
                GDEBUG("> AGF");
                return nfc_llc_handle_agf(self, pkt + 2, len - 2);
            }
            GDEBUG("> AGF (malformed?)");
            return FALSE;
        case LLCP_PTYPE_UI:
            self->packets_handled++;
            GDEBUG("> UI %u:%u (%u bytes)", ssap, dsap, (guint) len - 2);
            nfc_llc_handle_ui(self, dsap, ssap, pkt + 2, len - 2);
            return TRUE;
            break;
        case LLCP_PTYPE_CONNECT:
            self->packets_handled++;
            GDEBUG("> CONNECT %u:%u", ssap, dsap);
            nfc_llc_handle_connect(self, dsap, ssap, pkt + 2, len - 2);
            return TRUE;
        case LLCP_PTYPE_DISC:
            if (len == 2) {
                self->packets_handled++;
                GDEBUG("> DISC %u:%u", ssap, dsap);
                nfc_llc_handle_disc(self, dsap, ssap);
                return TRUE;
            }
            GDEBUG("> DISC (malformed?)");
            return FALSE;
        case LLCP_PTYPE_CC:
            self->packets_handled++;
            GDEBUG("> CC %u:%u", ssap, dsap);
            nfc_llc_handle_cc(self, dsap, ssap, pkt + 2, len - 2);
            return TRUE;
        case LLCP_PTYPE_DM:
            if (len == 3) {
                const guint8 reason = pkt[2];

                self->packets_handled++;
                GDEBUG("> DM %u:%u (0x%02x)", ssap, dsap, reason);
                nfc_llc_handle_dm(self, dsap, ssap, reason);
                return TRUE;
            }
            GDEBUG("> DM %u:%u (malformed?)", ssap, dsap);
            return FALSE;
        case LLCP_PTYPE_FRMR:
            if (len == 6) {
                self->packets_handled++;
                GDEBUG("> FRMR %u:%u (0x%02x)", ssap, dsap, pkt[2] & 0x0f);
                nfc_llc_handle_frmr(self, dsap, ssap, pkt[2] & 0x0f);
                return TRUE;
            }
            GDEBUG("> FRMR %u:%u (malformed?)", ssap, dsap);
            return FALSE;
        case LLCP_PTYPE_SNL:
            self->packets_handled++;
            if (dsap == NFC_LLC_SAP_SDP && ssap == NFC_LLC_SAP_SDP) {
                GDEBUG("> SNL");
                nfc_llc_handle_snl(self, pkt + 2, len - 2);
            } else {
                GDEBUG("> SNL %u:%u (malformed?)", ssap, dsap);
                nfc_llc_submit_frmr_i(self, ssap, dsap, LLCP_PTYPE_SNL);
            }
            return TRUE;
        case LLCP_PTYPE_I:
            if (len >= 3) {
                const guint8 seq = pkt[2];

                self->packets_handled++;
                GDEBUG("> I %u:%u (0x%02x, %u bytes)", ssap, dsap, seq, (guint)
                    len - 3);
                nfc_llc_handle_i(self, dsap, ssap, pkt[2], pkt + 3, len - 3);
                return TRUE;
            }
            GDEBUG("> I %u:%u (malformed?)", ssap, dsap);
            return FALSE;
        case LLCP_PTYPE_RR:
            if (len == 3) {
                const guint8 nr = pkt[2];

                self->packets_handled++;
                GDEBUG("> RR %u:%u (0x%02x)", ssap, dsap, nr);
                nfc_llc_handle_rr(self, dsap, ssap, nr & 0xff);
                return TRUE;
            }
            GDEBUG("> RR %u:%u (malformed?)", ssap, dsap);
            return FALSE;
        case LLCP_PTYPE_RNR:
            if (len == 3) {
                const guint8 nr = pkt[2];

                self->packets_handled++;
                GDEBUG("> RNR %u:%u (0x%02x)", ssap, dsap, nr);
                nfc_llc_handle_rnr(self, dsap, ssap, nr & 0xff);
                return TRUE;
            }
            GDEBUG("> RNR %u:%u (malformed?)", ssap, dsap);
            return FALSE;
        }
        GWARN("Packet 0x%x not handled", ptype);
        return FALSE;
    } else {
        GWARN("Single byte LLCP packet received, bailing out");
        return FALSE;
    }
}

static
NfcPeerConnection*
nfc_llc_connect_internal(
    NfcLlcObject* self,
    NfcPeerService* service,
    guint rsap,
    const char* rname,
    NfcLlcConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(service)) {
        NfcLlcConnectReq* req = nfc_llc_connect_req_new
            (self, service, rsap, rname, complete, user_data, destroy);

        if (req) {
            const gboolean do_connect = !self->connect_queue;

            self->connect_queue = g_slist_append(self->connect_queue, req);
            if (do_connect) {
                nfc_llc_submit_next_connect(self);
            }
            return req->connection;
        }
    }
    return NULL;
}

static
void
nfc_llc_set_state(
    NfcLlcObject* self,
    NFC_LLC_STATE state)
{
    NfcLlc* llc = &self->pub;

    if (llc->state != state) {
        GDEBUG("LLCP state %s -> %s",
            nfc_llc_state_name(self, llc->state),
            nfc_llc_state_name(self, state));
        llc->state = state;
        g_signal_emit(self, nfc_llc_signals[SIGNAL_STATE_CHANGED], 0);
    }
}

static
void
nfc_llc_set_idle(
    NfcLlcObject* self,
    gboolean idle)
{
    NfcLlc* llc = &self->pub;

    if (llc->idle != idle) {
        GDEBUG("LLCP %s", idle ? "idle" : "busy");
        llc->idle = idle;
        g_signal_emit(self, nfc_llc_signals[SIGNAL_IDLE_CHANGED], 0);
    }
}

static
void
nfc_llc_can_send(
    NfcLlcIo* io,
    gpointer user_data)
{
    nfc_llc_send_next_pdu(THIS(user_data));
}

static
gboolean
nfc_llc_receive(
    NfcLlcIo* io,
    const GUtilData* data,
    gpointer user_data)
{
    NfcLlcObject* self = THIS(user_data);
    const guint packets_handled = self->packets_handled;

    GASSERT(self->pub.state < NFC_LLC_STATE_ERROR);
    if (data->size > 0) {
        if (nfc_llc_handle_pdu(self, data->bytes, data->size)) {
            if (self->pub.state == NFC_LLC_STATE_START) {
                /* Peer is talking to us! */
                nfc_llc_set_state(self, NFC_LLC_STATE_ACTIVE);
            }
        } else {
            /* Protocol error */
            GWARN("LLC protocol error");
            nfc_llc_set_state(self, NFC_LLC_STATE_ERROR);
            return LLC_IO_IGNORE;
        }
    }
    if (self->io->can_send) {
        nfc_llc_send_next_pdu(self);
    }
    if (self->packets_handled == packets_handled && io->can_send) {
        nfc_llc_set_idle(self, !self->pdu_queue && !self->connect_queue);
        return LLC_IO_IGNORE;
    } else {
        nfc_llc_set_idle(self, FALSE);
        return LLC_IO_EXPECT_MORE;
    }
}

static
void
nfc_llc_error(
    NfcLlcIo* io,
    gpointer user_data)
{
    GDEBUG("LLC transmit failed");
    nfc_llc_set_state(THIS(user_data), NFC_LLC_STATE_PEER_LOST);
}

static
void
nfc_llc_send_next_pdu(
    NfcLlcObject* self)
{
    GBytes* packet = nfc_llc_dequeue_pdu(self);

    if (packet) {
        gsize pktsize;
        const guint8* pkt = g_bytes_get_data(packet, &pktsize);
        const guint hdr = (((guint)(pkt[0])) << 8) | pkt[1];

#if GUTIL_LOG_DEBUG
        if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
            const guint8 dsap = LLCP_GET_DSAP(hdr);
            const guint8 ssap = LLCP_GET_SSAP(hdr);

            switch (LLCP_GET_PTYPE(hdr)) {
            case LLCP_PTYPE_SYMM:
                /* These are actually sent (and logged) by NfcLlcIo */
                GDEBUG("< SYMM");
                break;
            case LLCP_PTYPE_PAX:
                GDEBUG("< PAX");
                break;
            case LLCP_PTYPE_AGF:
                GDEBUG("< AGF");
                break;
            case LLCP_PTYPE_UI:
                GDEBUG("< UI %u:%u", ssap, dsap);
                break;
            case LLCP_PTYPE_CONNECT:
                GDEBUG("< CONNECT %u:%u", ssap, dsap);
                break;
            case LLCP_PTYPE_DISC:
                GDEBUG("< DISC %u:%u", ssap, dsap);
                break;
            case LLCP_PTYPE_CC:
                GDEBUG("< CC %u:%u", ssap, dsap);
                break;
            case LLCP_PTYPE_DM:
                GDEBUG("< DM %u:%u (0x%02x)", ssap, dsap, pkt[2]);
                break;
            case LLCP_PTYPE_FRMR:
                GDEBUG("< FRMR %u:%u (0x%02x)", ssap, dsap, (pkt[2] & 0x0f));
                break;
            case LLCP_PTYPE_SNL:
                GDEBUG("< SNL");
                break;
            case LLCP_PTYPE_I:
                GDEBUG("< I %u:%u (%u bytes)", ssap, dsap, (guint)pktsize - 3);
                break;
            case LLCP_PTYPE_RR:
                GDEBUG("< RR %u:%u (0x%02x)", ssap, dsap, pkt[2]);
                break;
            case LLCP_PTYPE_RNR:
                GDEBUG("< RNR %u:%u", ssap, dsap);
                break;
            }
        }
#endif /* GUTIL_LOG_DEBUG */

        if (nfc_llc_io_send(self->io, packet)) {
            if (LLCP_GET_PTYPE(hdr) == LLCP_PTYPE_I) {
                const guint8 dsap = LLCP_GET_DSAP(hdr);
                const guint8 ssap = LLCP_GET_SSAP(hdr);
                NfcPeerConnection* conn = g_hash_table_lookup(self->conn_table,
                    LLCP_CONN_KEY(ssap, dsap) /* SSAP and DSAP reversed */);

                if (conn) {
                    nfc_peer_connection_flush(conn);
                }
            }
        } else {
            GDEBUG("LLC transmit failed");
            nfc_llc_set_state(self, NFC_LLC_STATE_PEER_LOST);
        }
        g_bytes_unref(packet);
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcLlc*
nfc_llc_new(
    NfcLlcIo* io,
    NfcPeerServices* services,
    const NfcLlcParam* const* params)
{
    NfcLlc* llc = NULL;

    if (G_LIKELY(io)) {
        NfcLlcObject* self = g_object_new(THIS_TYPE, NULL);

        GDEBUG("Initializing");
        llc = &self->pub;
        self->io = io;
        self->services = nfc_peer_services_ref(services);

        /* Apply parameters provided by the MAC layer */
        nfc_llc_apply_params(self, params);

        /*
         * PAX PDU exchange is defined in LLCP spec but SHALL NOT be used.
         * OK :)
         *
         * NFCForum-TS-LLCP_1.1
         *
         * 5.2 Link Activation Procedure
         * 5.2.1 Exchange of PAX PDU
         *
         * Operating in the Initiator role:
         *
         * The local LLC SHALL send a PAX PDU to the remote LLC that
         * includes all required LLC parameters not exchanged during
         * the MAC link activation. The local LLC SHALL then await
         * receipt of a PAX PDU from the remote LLC. Upon receipt of
         * the PAX PDU, the local LLC SHALL perform the version number
         * agreement procedure defined in Section 5.2.2.
         *
         * 6.2.3.1 Link Activation procedure for the Initiator
         *
         * All LLC parameters defined in Section 4.5 Table 6 for use
         * in PAX PDUs that are to be exchanged SHALL be included as
         * TLVs beginning at the fourth octet of the ATR_REQ General
         * Bytes field. The PAX PDU exchange described in the LLC link
         * activation procedure (cf. Section 5.2) SHALL NOT be used.
         */

        /* Start the conversation */
        self->io = nfc_llc_io_ref(io);
        if (nfc_llc_io_start(io)) {
            self->io_event[LLC_IO_EVENT_CAN_SEND] =
                nfc_llc_io_add_can_send_handler(io, nfc_llc_can_send, self);
            self->io_event[LLC_IO_EVENT_RECEIVE] =
                nfc_llc_io_add_receive_handler(io, nfc_llc_receive, self);
            self->io_event[LLC_IO_EVENT_ERROR] =
                nfc_llc_io_add_error_handler(io, nfc_llc_error, self);
        } else {
            llc->idle = TRUE;
            llc->state = NFC_LLC_STATE_PEER_LOST;
        }
    }
    return llc;
}

void
nfc_llc_free(
    NfcLlc* llc)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self)) {
        nfc_llc_abort_all_connections(self);
        nfc_llc_io_remove_all_handlers(self->io, self->io_event);
        g_object_unref(self);
    }
}

NfcPeerConnection*
nfc_llc_connect(
    NfcLlc* llc,
    NfcPeerService* service,
    guint rsap,
    NfcLlcConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    return G_LIKELY(self) ? nfc_llc_connect_internal
        (self, service, rsap, NULL, complete, user_data, destroy) : NULL;
}

NfcPeerConnection*
nfc_llc_connect_sn(
    NfcLlc* llc,
    NfcPeerService* service,
    const char* sn,
    NfcLlcConnectFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    return G_LIKELY(self) ? nfc_llc_connect_internal
        (self, service, 0, sn, complete, user_data, destroy) : NULL;
}

gulong
nfc_llc_add_state_changed_handler(
    NfcLlc* llc,
    NfcLlcFunc func,
    void* user_data)
{
    return nfc_llc_add_handler(llc, SIGNAL_STATE_CHANGED, func, user_data);
}

gulong
nfc_llc_add_idle_changed_handler(
    NfcLlc* llc,
    NfcLlcFunc func,
    void* user_data)
{
    return nfc_llc_add_handler(llc, SIGNAL_IDLE_CHANGED, func, user_data);
}

gulong
nfc_llc_add_wks_changed_handler(
    NfcLlc* llc,
    NfcLlcFunc func,
    void* user_data)
{
    return nfc_llc_add_handler(llc, SIGNAL_WKS_CHANGED, func, user_data);
}

void
nfc_llc_remove_handler(
    NfcLlc* llc,
    gulong id)
{
    if (G_LIKELY(id)) {
        NfcLlcObject* self = nfc_llc_object_cast(llc);

        if (G_LIKELY(self)) {
            g_signal_handler_disconnect(self, id);
        }
    }
}

void
nfc_llc_remove_handlers(
    NfcLlc* llc,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(nfc_llc_object_cast(llc), ids, count);
}

void
nfc_llc_connection_dead(
    NfcLlc* llc,
    NfcPeerConnection* conn)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self) && G_LIKELY(conn)) {
        g_hash_table_remove(self->conn_table, nfc_peer_connection_key(conn));
    }
}

gboolean
nfc_llc_cancel_connect_request(
    NfcLlc* llc,
    NfcPeerConnection* conn)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);
    gboolean cancelled = FALSE;

    if (G_LIKELY(self) && G_LIKELY(conn)) {
        GSList* l;

        for (l = self->connect_queue; l; l = g_slist_next(l)) {
            NfcLlcConnectReq* req = l->data;

            if (req->connection == conn) {
                req->complete = NULL;
                cancelled = TRUE;
                break;
            }
        }
    }
    return cancelled;
}

void
nfc_llc_ack(
    NfcLlc* llc,
    NfcPeerConnection* conn,
    gboolean last)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self) && G_LIKELY(conn)) {
        nfc_llc_ack_internal(self, conn, last);
    }
}

gboolean
nfc_llc_i_pdu_queued(
    NfcLlc* llc,
    NfcPeerConnection* conn)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self) && G_LIKELY(conn)) {
        GList* l = g_list_first(self->pdu_queue);

        if (l) {
            const guint8 dsap = conn->rsap;
            const guint8 ssap = conn->service->sap;
            const guint hdr = LLCP_MAKE_HDR(dsap, LLCP_PTYPE_I, ssap);

            do {
                const guint8* pkt = g_bytes_get_data((GBytes*)l->data, NULL);

                if (((((guint)(pkt[0])) << 8) | pkt[1]) == hdr) {
                    return TRUE;
                }
                l = l->next;
            } while (l);
        }
    }
    return FALSE;
}

void
nfc_llc_submit_dm_pdu(
    NfcLlc* llc,
    guint8 dsap,
    guint8 ssap,
    NFC_LLC_DM_REASON reason)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self)) {
        nfc_llc_submit_dm(self, dsap, ssap, reason);
    }
}

void
nfc_llc_submit_cc_pdu(
    NfcLlc* llc,
    NfcPeerConnection* conn)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self) && G_LIKELY(conn)) {
        NfcPeerService* service = conn->service;
        const NfcLlcParam* const* lp = nfc_peer_connection_lp(conn);
        const guint8 dsap = conn->rsap;
        const guint8 ssap = service->sap;
        GByteArray* bytes = nfc_llc_pdu_new(dsap, LLCP_PTYPE_CC, ssap);
        GBytes* pdu;

        nfc_llc_param_encode(lp, bytes, nfc_peer_connection_rmiu(conn));
        pdu = g_byte_array_free_to_bytes(bytes);
        nfc_llc_submit(self, pdu);
        g_bytes_unref(pdu);
    }
}

void
nfc_llc_submit_i_pdu(
    NfcLlc* llc,
    NfcPeerConnection* conn,
    const void* data,
    guint len)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self) && G_LIKELY(conn)) {
        NfcPeerConnectionLlcpState* ps = nfc_peer_connection_ps(conn);
        NfcPeerService* service = conn->service;
        const guint8 dsap = conn->rsap;
        const guint8 ssap = service->sap;
        const guint hdr = LLCP_MAKE_HDR(dsap, LLCP_PTYPE_I, ssap);
        const guint size = 3 + len;
        guint8* pkt = g_malloc(size);
        GBytes* pdu = g_bytes_new_take(pkt, size);

        pkt[0] = (guint8)(hdr >> 8);
        pkt[1] = (guint8)hdr;
        pkt[2] = (guint8)((ps->vs << 4) /* N(S) */ | ps->vr /* N(R) */);
        memcpy(pkt + 3, data, len);

        /*
         * NFCForum-TS-LLCP_1.1
         * 5.6 Connection-oriented Transport Mode Procedures
         *
         * 5.6.1.1 Send State Variable V(S)
         *
         * The send state variable V(S) SHALL denote the sequence number,
         * modulo-16, of the next in-sequence I PDU to be sent on a specific
         * data link connection. The value of the send state variable V(S)
         * SHALL be incremented by one following each successive I PDU
         * transmission on the associated data link connection.
         */
        ps->vs = ((ps->vs + 1) & 0x0f);

        /*
         * 5.6.1.4 Receive Acknowledgement State Variable V(RA)
         *
         * The receive acknowledgement state variable V(RA) SHALL denote
         * the most recently sent N(R) value for a specific data link
         * connection.
         */
        ps->vra = ps->vr;

        nfc_llc_submit(self, pdu);
        g_bytes_unref(pdu);
    }
}

void
nfc_llc_submit_disc_pdu(
    NfcLlc* llc,
    guint8 dsap,
    guint8 ssap)
{
    NfcLlcObject* self = nfc_llc_object_cast(llc);

    if (G_LIKELY(self)) {
        nfc_llc_submit_disc(self, dsap, ssap);
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_llc_object_init(
    NfcLlcObject* self)
{
    NfcLlc* llc = &self->pub;

    llc->state = NFC_LLC_STATE_START;
    self->miu = NFC_LLC_MIU_DEFAULT;
    self->lto = NFC_LLC_LTO_DEFAULT;
    self->pool = gutil_idle_pool_new();
    self->conn_table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
        NULL, nfc_llc_connection_destroy);
}

static
void
nfc_llc_object_finalize(
    GObject* object)
{
    NfcLlcObject* self = THIS(object);

    nfc_llc_abort_all_connections(self);
    nfc_peer_services_unref(self->services);
    nfc_llc_io_remove_all_handlers(self->io, self->io_event);
    nfc_llc_io_unref(self->io);
    g_hash_table_unref(self->conn_table);
    g_list_free_full(self->pdu_queue, (GDestroyNotify) g_bytes_unref);
    g_slist_free_full(self->connect_queue, (GDestroyNotify)
        nfc_llc_connect_req_free);
    gutil_idle_pool_destroy(self->pool);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_llc_object_class_init(
    NfcLlcObjectClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    G_OBJECT_CLASS(klass)->finalize = nfc_llc_object_finalize;
    nfc_llc_signals[SIGNAL_STATE_CHANGED] =
        g_signal_new(SIGNAL_STATE_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_llc_signals[SIGNAL_IDLE_CHANGED] =
        g_signal_new(SIGNAL_IDLE_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    nfc_llc_signals[SIGNAL_WKS_CHANGED] =
        g_signal_new(SIGNAL_WKS_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
