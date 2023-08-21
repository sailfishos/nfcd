/*
 * Copyright (C) 2023 Slava Monich <slava@monich.com>
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

#include "nfc_host_p.h"
#include "nfc_host_app_p.h"
#include "nfc_host_service_p.h"
#include "nfc_initiator_p.h"
#include "nfc_util.h"
#include "nfc_tag_t4.h"

#define GLOG_MODULE_NAME NFC_HOST_LOG_MODULE
#include <gutil_log.h>
#include <gutil_macros.h>
#include <gutil_misc.h>
#include <gutil_objv.h>
#include <gutil_weakref.h>

GLOG_MODULE_DEFINE2("nfc-host", NFC_CORE_LOG_MODULE);

enum {
    INITIATOR_GONE,
    INITIATOR_REACTIVATED,
    INITIATOR_TRANSMISSION,
    INITIATOR_EVENT_COUNT
};

typedef struct nfc_host_apdu NfcHostApdu;
typedef struct nfc_host_apdu_processor NfcHostApduProcessor;

struct nfc_host_priv {
    char* name;
    gulong event_id[INITIATOR_EVENT_COUNT];
    NfcHostApp** apps;
    NfcHostService** services;
    NfcHostApduProcessor* processors;
    NfcHostApdu* apdu;
    GSList* pending_ops;
    GUtilWeakRef* ref;
};

#define THIS(obj) NFC_HOST(obj)
#define THIS_TYPE NFC_TYPE_HOST
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS nfc_host_parent_class

typedef GObjectClass NfcHostClass;
G_DEFINE_TYPE(NfcHost, nfc_host, PARENT_TYPE)

enum nfc_host_signal {
    SIGNAL_APP_CHANGED,
    SIGNAL_GONE,
    SIGNAL_COUNT
};

#define SIGNAL_APP_CHANGED_NAME "nfc-host-app-changed"
#define SIGNAL_GONE_NAME        "nfc-host-gone"

static guint nfc_host_signals[SIGNAL_COUNT] = { 0 };

typedef
void
(*NfcHostCallbackFunc)(
    NfcHost* self);

typedef
guint
(*NfcHostStartServiceFunc)(
    NfcHostService* service,
    NfcHost* host,
    NfcHostServiceBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy);

typedef
guint
(*NfcHostStartAppFunc)(
    NfcHostApp* app,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy);

typedef
void
(*NfcHostOpCancelFunc)(
    GObject* obj,
    guint id);

typedef
void
(*NfcHostCompleteBool)(
    NfcHost* host,
    GObject* obj,
    gboolean result);

typedef
void
(*NfcHostAppCompleteBool)(
    NfcHost* host,
    NfcHostApp* app,
    gboolean result);

typedef
void
(*NfcHostServiceCompleteBool)(
    NfcHost* host,
    NfcHostService* service,
    gboolean result);

typedef struct nfc_host_app_response_sent {
    NfcHostApp* app;
    NfcTransmission* tx;
    NfcHostAppBoolFunc sent;
    void* user_data;
} NfcHostAppResponseSent;

typedef struct nfc_host_service_response_sent {
    NfcHostService* service;
    NfcTransmission* tx;
    NfcHostServiceBoolFunc sent;
    void* user_data;
} NfcHostServiceResponseSent;

typedef struct nfc_host_op {
    gint ref_count;
    NfcHostOpCancelFunc cancel;
    GUtilWeakRef* host_ref;
    GObject* obj;
    gboolean completed;
    GCallback complete;
    const char* name;
    guint id;
} NfcHostOp;

/* Processors are freed with a plain g_free() */

struct nfc_host_apdu_processor {
    gboolean (*process)(NfcHostApduProcessor*);
    NfcHostApduProcessor* next;
    NfcHost* host;  /* Not a reference */
};

typedef struct nfc_host_service_apdu_processor {
    NfcHostApduProcessor processor;
    NfcHostService* service; /* Not a reference */
} NfcHostServiceApduProcessor;

struct nfc_host_apdu {
    NfcHostApduProcessor* processor;
    NfcTransmission* tx;
    NfcApdu apdu;
};

static
void
nfc_host_process_apdu(
    NfcHost* self);

/*==========================================================================*
 * NfcHostOp
 *==========================================================================*/

#define nfc_host_op_destroy ((GDestroyNotify) nfc_host_op_unref)

static
NfcHostOp*
nfc_host_op_ref(
    NfcHostOp* op)
{
    g_atomic_int_inc(&op->ref_count);
    return op;
}

static
void
nfc_host_op_unref(
    NfcHostOp* op)
{
    if (g_atomic_int_dec_and_test(&op->ref_count)) {
        NfcHost* self = gutil_weakref_get(op->host_ref);

        if (self) {
            NfcHostPriv* priv = self->priv;

            priv->pending_ops = g_slist_remove(priv->pending_ops, op);
            nfc_host_unref(self);
        }
        gutil_object_unref(op->obj);
        gutil_weakref_unref(op->host_ref);
        gutil_slice_free(op);
    }
}

static
gboolean
nfc_host_op_start(
    NfcHostPriv* priv,
    NfcHostOp* op,
    guint id)
{
    /* Returns TRUE if the op either starts or completes (i.e. doesn't fail) */
    if (id) {
        /*
         * If operation has completed synchronously, the op pointer may
         * already be (and typically is) invalid.
         */
        if (id != NFCD_ID_SYNC) {
            op->id = id;
        }
        return TRUE;
    } else {
        /*
         * The op may have already been removed from the list of pending ops
         * by the completion routine (if the op has completed synchronously)
         */
        return op->completed;
    }
}

static
NfcHostOp*
nfc_host_op_new(
    NfcHost* self,
    NfcHostOpCancelFunc cancel,
    GCallback complete,
    const char* name,
    GObject* obj)
{
    NfcHostPriv* priv = self->priv;
    NfcHostOp* op = g_slice_new0(NfcHostOp);

    op->cancel = cancel;
    op->host_ref = gutil_weakref_ref(priv->ref);
    op->complete = complete;
    op->name = name;
    g_object_ref(op->obj = obj);
    g_atomic_int_set(&op->ref_count, 1);
    priv->pending_ops = g_slist_append(priv->pending_ops, op);
    return op;
}

static
void
nfc_host_op_complete_bool(
    NfcHostOp* op,
    gboolean result)
{
    NfcHost* self = gutil_weakref_get(op->host_ref);

    op->completed = TRUE;
    op->id = 0;
    if (self) {
        NfcHostPriv* priv = self->priv;
        NfcHostCompleteBool complete = (NfcHostCompleteBool) op->complete;

        priv->pending_ops = g_slist_remove(priv->pending_ops, op);
        complete(self, op->obj, result);
        nfc_host_unref(self);
    }
}

static
gboolean
nfc_host_op_fail_bool_cb(
    gpointer user_data)
{
    nfc_host_op_complete_bool(user_data, FALSE);
    return G_SOURCE_REMOVE;
}

static
void
nfc_host_op_fail_bool_async(
    NfcHostOp* op)
{
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, nfc_host_op_fail_bool_cb,
        nfc_host_op_ref(op), nfc_host_op_destroy);
}

static
void
nfc_host_op_app_complete_bool(
    NfcHostApp* app,
    gboolean result,
    void* user_data)
{
    nfc_host_op_complete_bool(user_data, result);
}

static
void
nfc_host_op_service_complete_bool(
    NfcHostService* service,
    gboolean result,
    void* user_data)
{
    nfc_host_op_complete_bool(user_data, result);
}

static
NfcHostOp*
nfc_host_service_op_new(
    NfcHost* self,
    NfcHostService* service,
    GCallback complete)
{
    return nfc_host_op_new(self, (NfcHostOpCancelFunc) nfc_host_service_cancel,
        complete, service->name, G_OBJECT(service));
}

static
NfcHostOp*
nfc_host_app_op_new(
    NfcHost* self,
    NfcHostApp* app,
    GCallback complete)
{
    return nfc_host_op_new(self, (NfcHostOpCancelFunc) nfc_host_app_cancel,
        complete, app->name, G_OBJECT(app));
}

static
NfcHostOp*
nfc_host_service_op_new_bool(
    NfcHost* self,
    NfcHostService* service,
    NfcHostServiceCompleteBool complete)
{
    return nfc_host_service_op_new(self, service, G_CALLBACK(complete));
}

static
NfcHostOp*
nfc_host_app_op_new_bool(
    NfcHost* self,
    NfcHostApp* app,
    NfcHostAppCompleteBool complete)
{
    return nfc_host_app_op_new(self, app, G_CALLBACK(complete));
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
nfc_host_app_selected(
    NfcHost* self,
    NfcHostApp* app)
{
    if (self->app != app) {
        self->app = app;
        g_signal_emit(self, nfc_host_signals[SIGNAL_APP_CHANGED], 0);
    }
}

static
NfcHostApp*
nfc_host_app_by_aid(
    NfcHostPriv* priv,
    const GUtilData* aid)
{
    NfcHostApp* const* apps = priv->apps;

    if (apps) {
        while (*apps) {
            NfcHostApp* app = *apps++;

            if (gutil_data_equal(aid, &app->aid)) {
                return app;
            }
        }
    }
    return NULL;
}

static
gboolean
nfc_host_is_select_app_apdu(
    const NfcApdu* apdu)
{

    /*
     * ISO/IEC 7816-4
     *
     * 8.2.2.2 Application selection using AID as DF name
     *
     * In a multi-application environment, the card shall respond positively
     * to a SELECT command specifying any application identifier (AID, see
     * 8.2.1.2) as DF name. The interface device may thus explicitly select
     * an application without previously checking the presence of the
     * application in the card.
     *
     * The card shall support a SELECT command with CLA INS P1 P2 set to
     * '00A4 0400' for the first selection with a given and preferably
     * complete application identifier in the command data field (see
     * Table 39). Depending on whether the application is present or not,
     * the card shall either complete or abort the command.
     */
    return apdu->cla == ISO_CLA &&
        apdu->ins == ISO_INS_SELECT &&
        apdu->p1 == ISO_P1_SELECT_DF_BY_NAME &&
        apdu->p2 == ISO_P2_SELECT_FILE_FIRST;
}

static
NfcHostApdu*
nfc_host_apdu_new(
    const NfcApdu* apdu,
    NfcTransmission* tx,
    NfcHostApduProcessor* processor)
{
    gsize total = sizeof(NfcHostApdu) + apdu->data.size;
    NfcHostApdu* out = g_malloc(total);
    void* data = out + 1;

#if GUTIL_LOG_DEBUG
    if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
        char* tmp = NULL;

        GDEBUG("C-APDU %02X %02X %02X %02X %s%s%04X",
            apdu->cla, apdu->ins, apdu->p1, apdu->p2,
            apdu->data.size ? (tmp = gutil_data2hex(&apdu->data, TRUE)) : "",
            apdu->data.size ? " " : "", apdu->le);
        g_free(tmp);
    }
#endif

    memcpy(data, apdu->data.bytes, apdu->data.size);
    out->tx = nfc_transmission_ref(tx);
    out->apdu = *apdu;
    out->apdu.data.bytes = data;
    out->processor = processor;
    return out;
}

static
void
nfc_host_apdu_free(
    NfcHostApdu* apdu)
{
    nfc_transmission_unref(apdu->tx);
    g_free(apdu);
}

static
void
nfc_host_drop_apdu(
    NfcHostPriv* priv)
{
    if (priv->apdu) {
        nfc_host_apdu_free(priv->apdu);
        priv->apdu = NULL;
    }
}

static
GBytes*
nfc_host_app_response_data(
    guint sw, /* 16 bits (SW1 << 8)|SW2 */
    const GUtilData* data)
{
    guchar* buf = g_malloc(data->size + 2);

    if (data->size) {
        memcpy(buf, data->bytes, data->size);
    }
    buf[data->size] = (guchar)(sw >> 8); /* SW1 */
    buf[data->size + 1] = (guchar)sw;    /* SW2 */
    return g_bytes_new_take(buf, data->size + 2);
}

static
void
nfc_host_app_response_sent(
    NfcTransmission* tx,
    gboolean ok,
    void* user_data)
{
    NfcHostAppResponseSent* data = user_data;

    data->sent(data->app, ok, data->user_data);
    nfc_transmission_unref(data->tx);
    nfc_host_app_unref(data->app);
    gutil_slice_free(data);
}

static
NfcHostAppResponseSent*
nfc_host_app_response_new(
    NfcHostApp* app,
    NfcTransmission* tx,
    NfcHostAppBoolFunc fn,
    void* user_data)
{
    NfcHostAppResponseSent* data = g_slice_new(NfcHostAppResponseSent);

    data->app = nfc_host_app_ref(app);
    data->tx = nfc_transmission_ref(tx);
    data->sent = fn;
    data->user_data = user_data;
    return data;
}

static
void
nfc_host_app_respond(
    NfcHostApp* app,
    NfcTransmission* tx,
    const NfcHostAppResponse* resp)
{
    /* Caller is supposed to make sure that resp isn't NULL */
    GBytes* bytes = nfc_host_app_response_data(resp->sw, &resp->data);

    if (resp->sent) {
        nfc_transmission_respond_bytes(tx, bytes, nfc_host_app_response_sent,
            nfc_host_app_response_new(app, tx, resp->sent, resp->user_data));
    } else {
        nfc_transmission_respond_bytes(tx, bytes, NULL, NULL);
    }
    g_bytes_unref(bytes);
}

static
void
nfc_host_respond_sw(
    NfcTransmission* tx,
    guint sw /* (SW1 << 8) | SW2 */)
{
    guchar resp[2];

    resp[0] = (guchar) (sw >> 8);
    resp[1] = (guchar) sw;
    nfc_transmission_respond(tx, resp, sizeof(resp), NULL, NULL);
}

static
void
nfc_host_service_response_sent(
    NfcTransmission* tx,
    gboolean ok,
    void* user_data)
{
    NfcHostServiceResponseSent* data = user_data;

    data->sent(data->service, ok, data->user_data);
    nfc_transmission_unref(data->tx);
    nfc_host_service_unref(data->service);
    gutil_slice_free(data);
}

static
NfcHostServiceResponseSent*
nfc_host_service_response_new(
    NfcHostService* service,
    NfcTransmission* tx,
    NfcHostServiceBoolFunc fn,
    void* user_data)
{
    NfcHostServiceResponseSent* data = g_slice_new(NfcHostServiceResponseSent);

    data->service = nfc_host_service_ref(service);
    data->tx = nfc_transmission_ref(tx);
    data->sent = fn;
    data->user_data = user_data;
    return data;
}

static
void
nfc_host_service_respond(
    NfcHostService* service,
    NfcTransmission* tx,
    const NfcHostServiceResponse* resp)
{
    /* Caller is supposed to make sure that resp isn't NULL */
    GBytes* bytes = nfc_host_app_response_data(resp->sw, &resp->data);

    if (resp->sent) {
        nfc_transmission_respond_bytes(tx, bytes,
            nfc_host_service_response_sent, nfc_host_service_response_new
                (service, tx, resp->sent, resp->user_data));
    } else {
        nfc_transmission_respond_bytes(tx, bytes, NULL, NULL);
    }
    g_bytes_unref(bytes);
}

static
void
nfc_host_app_select_complete(
    NfcHost* self,
    NfcHostApp* app,
    gboolean ok)
{
    NfcHostPriv* priv = self->priv;
    NfcHostApdu* apdu = priv->apdu;

    if (ok) {
        GDEBUG("%s selected for %s", app->name, self->name);
        nfc_host_app_selected(self, app);
        if (apdu) {
            const guint sw = ISO_SW_OK;

            GDEBUG("APDU processed internally => %04X", sw);
            nfc_host_respond_sw(apdu->tx, sw);
            nfc_host_drop_apdu(priv);
        }
        nfc_host_process_apdu(self);
    } else {
        GDEBUG("%s failed selection for %s", app->name, self->name);
        if (apdu) {
            const guint sw = 0x6a00;  /* Error (No information given) */

            GDEBUG("APDU processed internally => %04X", sw);
            nfc_host_respond_sw(apdu->tx, sw);
            nfc_host_drop_apdu(priv);
        }
    }
}

static
void
nfc_host_process_apdu(
    NfcHost* self)
{
    NfcHostPriv* priv = self->priv;
    NfcHostApdu* apdu = priv->apdu;

    if (apdu && !priv->pending_ops) {
        while (apdu->processor &&
            !apdu->processor->process(apdu->processor)) {
            apdu->processor = apdu->processor->next;
        }

        /* The processor may have dropped the apdu */
        apdu = priv->apdu;
        if (apdu && !priv->pending_ops) {
            guint sw = 0x6a00; /* Error (No precise diagnosis) */

            /* Internal processing of SELECT */
            if (nfc_host_is_select_app_apdu(&apdu->apdu)) {
                const GUtilData* aid = &apdu->apdu.data;
                NfcHostApp* app = nfc_host_app_by_aid(priv, aid);

                if (app) {
#if GUTIL_LOG_DEBUG
                    if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
                        char* hex = gutil_data2hex(&app->aid, TRUE);

                        GDEBUG((app == self->app) ?
                           "App %s is already selected" :
                           "Selecting app %s", hex);
                        g_free(hex);
                    }
#endif
                    if (app == self->app) {
                        sw = ISO_SW_OK; /* Success */
                        GDEBUG("APDU processed internally => %04X", sw);
                    } else {
                        NfcHostOp* op;

                        if (self->app) {
                            NfcHostApp* prev_app = self->app;

                            /* Notify the current app that it's deselected */
                            nfc_host_app_selected(self, NULL);
                            nfc_host_app_deselect(prev_app, self);
                        }

                        op = nfc_host_app_op_new_bool(self, app,
                            nfc_host_app_select_complete);

                        if (!nfc_host_op_start(priv, op,
                            nfc_host_app_select(app, self,
                            nfc_host_op_app_complete_bool, op,
                            nfc_host_op_destroy))) {
                            nfc_host_op_fail_bool_async(op);
                            nfc_host_op_unref(op);
                        }
                        return;
                    }
                } else {
 #if GUTIL_LOG_DEBUG
                    if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
                        char* hex = gutil_data2hex(aid, TRUE);

                        GDEBUG("App %s not found", hex);
                        g_free(hex);
                    }
#endif
                    sw = 0x6A82; /* Error (File or application not found) */
               }
            } else {
                GDEBUG("APDU not handled");
                sw = (apdu->apdu.cla == ISO_CLA) ?
                    0x6a00 : /* Error (No precise diagnosis) */
                    0x6e00;  /* Error (Class not supported) */

            }

            nfc_host_respond_sw(apdu->tx, sw);
            nfc_host_drop_apdu(priv);
        }
    }
}

static
void
nfc_host_op_app_complete_resp(
    NfcHostApp* app,
    const NfcHostAppResponse* resp,
    void* user_data)
{
    NfcHostOp* op = user_data;
    NfcHost* self = gutil_weakref_get(op->host_ref);

    op->completed = TRUE;
    op->id = 0;
    if (self) {
        NfcHostPriv* priv = self->priv;
        NfcHostApdu* apdu = priv->apdu;

        priv->pending_ops = g_slist_remove(priv->pending_ops, op);

        /* Is APDU still around? */
        if (apdu) {
            if (resp) {
                GDEBUG("APDU processed by %s app", app->name);
                nfc_host_app_respond(app, apdu->tx, resp);
                nfc_host_drop_apdu(priv);
            } else {
                GDEBUG("%s app refused to process APDU", app->name);
                apdu->processor = apdu->processor->next;
                nfc_host_process_apdu(self);
            }
        }
        nfc_host_unref(self);
    }
}

static
gboolean
nfc_host_apdu_process_app(
    NfcHostApduProcessor* processor)
{
    NfcHost* self = processor->host;
    NfcHostPriv* priv = self->priv;
    NfcHostApdu* apdu = priv->apdu;

    if (self->app && !nfc_host_is_select_app_apdu(&apdu->apdu)) {
        NfcHostOp* op = nfc_host_app_op_new(self, self->app, NULL);

        if (nfc_host_op_start(priv, op,
            nfc_host_app_process(self->app, self, &apdu->apdu,
            nfc_host_op_app_complete_resp, op,
            nfc_host_op_destroy))) {
            return TRUE;
        }
        nfc_host_op_unref(op);
    }
    return FALSE;
}

static
NfcHostApduProcessor*
nfc_host_app_apdu_processor_new(
    NfcHost* host)
{
    NfcHostApduProcessor* ap = g_new0(NfcHostApduProcessor, 1);

    ap->process = nfc_host_apdu_process_app;
    ap->host = host;
    return ap;
}

static
void
nfc_host_op_service_complete_resp(
    NfcHostService* service,
    const NfcHostServiceResponse* resp,
    void* user_data)
{
    NfcHostOp* op = user_data;
    NfcHost* self = gutil_weakref_get(op->host_ref);

    op->completed = TRUE;
    op->id = 0;
    if (self) {
        NfcHostPriv* priv = self->priv;
        NfcHostApdu* apdu = priv->apdu;

        priv->pending_ops = g_slist_remove(priv->pending_ops, op);

        /* Is APDU still around? */
        if (apdu) {
            if (resp) {
                GDEBUG("APDU processed by %s service", service->name);
                nfc_host_service_respond(service, apdu->tx, resp);
                nfc_host_drop_apdu(priv);
            } else {
                GDEBUG("%s service refused to process APDU", service->name);
                apdu->processor = apdu->processor->next;
                nfc_host_process_apdu(self);
            }
        }
        nfc_host_unref(self);
    }
}

static
gboolean
nfc_host_apdu_process_service(
    NfcHostApduProcessor* processor)
{
    NfcHostServiceApduProcessor* sap = G_CAST(processor,
        NfcHostServiceApduProcessor, processor);
    NfcHost* self = processor->host;
    NfcHostPriv* priv = self->priv;
    NfcHostApdu* apdu = priv->apdu;
    NfcHostOp* op = nfc_host_service_op_new(self, sap->service, NULL);

    if (nfc_host_op_start(priv, op,
        nfc_host_service_process(sap->service, self, &apdu->apdu,
        nfc_host_op_service_complete_resp, op,
        nfc_host_op_destroy))) {
        return TRUE;
    } else {
        nfc_host_op_unref(op);
        return FALSE;
    }
}

static
NfcHostApduProcessor*
nfc_host_service_apdu_processor_new(
    NfcHost* host,
    NfcHostService* service)
{
    NfcHostServiceApduProcessor* sap = g_new0(NfcHostServiceApduProcessor, 1);
    NfcHostApduProcessor* ap = &sap->processor;

    sap->service = service;
    ap->process = nfc_host_apdu_process_service;
    ap->host = host;
    return ap;
}

static
void
nfc_host_app_implicit_select_complete(
    NfcHost* self,
    NfcHostApp* app,
    gboolean ok)
{
    if (ok) {
        GDEBUG("%s app implicitly selected for %s", app->name, self->name);
        nfc_host_app_selected(self, app);
    } else {
        NfcHostPriv* priv = self->priv;
        NfcHostApp* const* apps = priv->apps;
        gboolean found_this_app = FALSE;

        GDEBUG("%s app failed implicit selection for %s", app->name,
            self->name);

        /* Try to find another app for implicit selection */
        while (*apps) {
            NfcHostApp* a = *apps++;

            if (a == app) {
                found_this_app = TRUE;
            } else if (found_this_app &&
                (a->flags & NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION)) {
                NfcHostOp* op = nfc_host_app_op_new_bool(self, a,
                    nfc_host_app_implicit_select_complete);

                if (nfc_host_op_start(priv, op,
                    nfc_host_app_implicit_select(a, self,
                    nfc_host_op_app_complete_bool, op,
                    nfc_host_op_destroy))) {
                    break;
                } else {
                    nfc_host_op_fail_bool_async(op);
                    nfc_host_op_unref(op);
                }
            }
        }
    }

    nfc_host_process_apdu(self);
}

static
void
nfc_host_app_start_complete(
    NfcHost* self,
    NfcHostApp* app,
    gboolean ok)
{
    NfcHostPriv* priv = self->priv;

    if (ok) {
        GDEBUG("%s app started", app->name);
    } else {
        GDEBUG("%s app failed to start", app->name);
        priv->apps = (NfcHostApp**) gutil_objv_remove((GObject**)
            priv->apps, G_OBJECT(app), FALSE);
    }

    if (!priv->pending_ops) {
        NfcHostApp* select = NULL;
        NfcHostApp* const* apps = priv->apps;

        /* All apps have started, see if we can select one implicitly */
        if (apps) {
            while (*apps) {
                NfcHostApp* app = *apps++;

                if (app->flags & NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION) {
                    select = app;
                    break;
                }
            }
        }

        if (select) {
            NfcHostOp* op = nfc_host_app_op_new_bool(self, select,
                nfc_host_app_implicit_select_complete);

            if (!nfc_host_op_start(priv, op,
                nfc_host_app_implicit_select(select, self,
                nfc_host_op_app_complete_bool, op,
                nfc_host_op_destroy))) {
                nfc_host_op_fail_bool_async(op);
                nfc_host_op_unref(op);
            }
        }

        nfc_host_process_apdu(self);
    }
}

static
void
nfc_host_init_apps(
    NfcHost* self,
    const char* what,
    NfcHostStartAppFunc start_app)
{
    NfcHostPriv* priv = self->priv;

    if (!priv->pending_ops) {
        /* All services have been initialized, start (or restart) the apps */
        if (priv->apps) {
            NfcHostApp* const* ptr = priv->apps;
            GQueue ops;
            GList* l;

            /* Create the ops but don't start them yet */
            g_queue_init(&ops);
            while (*ptr) {
                g_queue_push_tail(&ops, nfc_host_app_op_new_bool(self,
                    *ptr++, nfc_host_app_start_complete));
            }

            /* Actually do something */
            for (l = ops.head; l; l = l->next) {
                NfcHostOp* op = l->data;
                NfcHostApp* app = NFC_HOST_APP(op->obj);

                GDEBUG("%s %s app %s", self->name, what, app->name);
                if (!nfc_host_op_start(priv, op, start_app(app, self,
                    nfc_host_op_app_complete_bool, op,
                    nfc_host_op_destroy))) {
                    nfc_host_op_fail_bool_async(op);
                    nfc_host_op_unref(op);
                }
            }

            g_queue_clear(&ops);
        }
    }

    nfc_host_process_apdu(self);
}

static
void
nfc_host_start_apps(
    NfcHost* self)
{
    nfc_host_init_apps(self, "starting", nfc_host_app_start);
}

static
void
nfc_host_restart_apps(
    NfcHost* self)
{
    nfc_host_init_apps(self, "restarting", nfc_host_app_restart);
}

static
void
nfc_host_service_start_complete(
    NfcHost* self,
    NfcHostService* service,
    gboolean ok)
{
    if (ok) {
        GDEBUG("%s service started for %s", service->name, self->name);
    } else {
        NfcHostPriv* priv = self->priv;

        GDEBUG("%s service failed to start for %s", service->name, self->name);
        priv->services = (NfcHostService**) gutil_objv_remove((GObject**)
            priv->services, G_OBJECT(service), FALSE);
    }

    nfc_host_start_apps(self);
}

static
void
nfc_host_service_restart_complete(
    NfcHost* self,
    NfcHostService* service,
    gboolean ok)
{
    if (ok) {
        GDEBUG("%s service restarted for %s", service->name, self->name);
    } else {
        NfcHostPriv* priv = self->priv;

        GDEBUG("%s service failed to restart for %s", service->name,
            self->name);
        priv->services = (NfcHostService**) gutil_objv_remove((GObject**)
            priv->services, G_OBJECT(service), FALSE);
    }

    nfc_host_restart_apps(self);
}

static
gboolean
nfc_host_transmission_handler(
    NfcInitiator* initiator,
    NfcTransmission* tx,
    const GUtilData* data,
    void* user_data)
{
    NfcHost* self = THIS(user_data);
    NfcHostPriv* priv = self->priv;

    GASSERT(!priv->apdu);
    if (!priv->apdu) {
        NfcApdu apdu;

        /* Refuse to handle unparceable APDUs */
        if (nfc_apdu_decode(&apdu, data)) {
            priv->apdu = nfc_host_apdu_new(&apdu, tx, priv->processors);
            nfc_host_ref(self);
            nfc_host_process_apdu(self);
            nfc_host_unref(self);
            return TRUE;
        }
    }
    return FALSE;
}

static
void
nfc_host_cancel_all(
    NfcHostPriv* priv)
{
    while (priv->pending_ops) {
        GSList* l = priv->pending_ops;
        NfcHostOp* op = l->data;
        guint id = op->id;

        priv->pending_ops = l->next;
        g_slist_free1(l);
        op->id = 0;
        op->cancel(op->obj, id);
        /* NfcHostOp will be freed by nfc_host_op_destroy */
    }
}

static
void
nfc_host_init_services(
    NfcHost* self,
    const char* what,
    NfcHostStartServiceFunc start_service,
    NfcHostServiceCompleteBool complete_start_service,
    NfcHostCallbackFunc start_apps)
{
    /* Caller checks the argument for NULL */
    NfcHostPriv* priv = self->priv;

    if (priv->services) {
        NfcHostService* const* ptr = priv->services;
        GQueue ops;
        GList* l;

        /* Create the ops but don't start them yet */
        g_queue_init(&ops);
        while (*ptr) {
            g_queue_push_tail(&ops, nfc_host_service_op_new_bool(self,
                *ptr++, complete_start_service));
        }

        /* Actually start (or restart) the services */
        for (l = ops.head; l; l = l->next) {
            NfcHostOp* op = l->data;
            NfcHostService* service = NFC_HOST_SERVICE(op->obj);

            GDEBUG("%s %s service %s", self->name, what, service->name);
            if (!nfc_host_op_start(priv, op, start_service(service, self,
                nfc_host_op_service_complete_bool, op,
                nfc_host_op_destroy))) {
                nfc_host_op_fail_bool_async(op);
                nfc_host_op_unref(op);
            }
        }

        g_queue_clear(&ops);
    }

    /* If there are no services, start (or restart) the apps */
    start_apps(self);
}

static
void
nfc_host_reactivated(
    NfcInitiator* initiator,
    void* user_data)
{
    NfcHost* self = THIS(user_data);
    NfcHostPriv* priv = self->priv;

    nfc_host_ref(self);
    nfc_host_cancel_all(priv);
    nfc_host_init_services(self, "restarting", nfc_host_service_restart,
        nfc_host_service_restart_complete, nfc_host_restart_apps);
    nfc_host_unref(self);
}

static
void
nfc_host_gone(
    NfcInitiator* initiator,
    void* user_data)
{
    NfcHost* self = THIS(user_data);
    NfcHostPriv* priv = self->priv;

    nfc_host_ref(self);
    nfc_host_cancel_all(priv);
    /* Remove the handler which we no longer need (and clear its id) */
    nfc_initiator_remove_handlers(self->initiator, priv->event_id +
        INITIATOR_GONE, 1);
    g_signal_emit(self, nfc_host_signals[SIGNAL_GONE], 0);
    nfc_host_unref(self);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcHost*
nfc_host_ref(
    NfcHost* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_host_unref(
    NfcHost* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

void
nfc_host_deactivate(
    NfcHost* self)
{
    if (G_LIKELY(self)) {
        nfc_initiator_deactivate(self->initiator);
    }
}

gulong
nfc_host_add_app_changed_handler(
    NfcHost* self,
    NfcHostFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_APP_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_host_add_gone_handler(
    NfcHost* self,
    NfcHostFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_GONE_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_host_remove_handler(
    NfcHost* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_host_remove_handlers(
    NfcHost* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

NfcHost*
nfc_host_new(
    const char* name,
    NfcInitiator* initiator,
    NfcHostService* const* services,
    NfcHostApp* const* apps)
{
    /* Caller checks the arguments */
    NfcHost* self = g_object_new(THIS_TYPE, NULL);
    NfcHostPriv* priv = self->priv;

    self->name = priv->name = g_strdup(name);
    self->initiator = nfc_initiator_ref(initiator);

    /* Make copies of everything */
    priv->services = (NfcHostService**) gutil_objv_copy((GObject**) services);
    priv->apps = (NfcHostApp**) gutil_objv_copy((GObject**) apps);

    /*
     * Service APDU processors in reversed order (the last registered services
     * gets its chance first).
     */
    if (priv->services) {
        NfcHostService* const* ptr = priv->services;

        while (*ptr) {
            NfcHostApduProcessor* sp =
                nfc_host_service_apdu_processor_new(self, *ptr++);

            sp->next = priv->processors;
            priv->processors = sp;
        }
    }

    /* Selected app is first to process incoming APDUs */
    if (priv->apps && priv->apps[0]) {
        NfcHostApduProcessor* ap = nfc_host_app_apdu_processor_new(self);

        ap->next = priv->processors;
        priv->processors = ap;
    }

    /* Register event handlers */
    priv->event_id[INITIATOR_GONE] =
        nfc_initiator_add_gone_handler(initiator,
            nfc_host_gone, self);
    priv->event_id[INITIATOR_REACTIVATED] =
        nfc_initiator_add_reactivated_handler(initiator,
            nfc_host_reactivated, self);
    priv->event_id[INITIATOR_TRANSMISSION] =
        nfc_initiator_add_transmission_handler(initiator,
            nfc_host_transmission_handler, self);

    return self;
}

void
nfc_host_start(
    NfcHost* self)
{
    nfc_host_init_services(self, "starting", nfc_host_service_start,
        nfc_host_service_start_complete, nfc_host_start_apps);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_host_init(
    NfcHost* self)
{
    NfcHostPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcHostPriv);

    self->priv = priv;
    priv->ref = gutil_weakref_new(self);
}

static
void
nfc_host_finalize(
    GObject* object)
{
    NfcHost* self = THIS(object);
    NfcHostPriv* priv = self->priv;

    while (priv->processors) {
        NfcHostApduProcessor* ap = priv->processors;

        priv->processors = ap->next;
        g_free(ap);
    }

    nfc_host_cancel_all(priv);
    gutil_objv_free((GObject**) priv->apps);
    gutil_objv_free((GObject**) priv->services);
    gutil_weakref_unref(priv->ref);
    nfc_host_drop_apdu(priv);
    nfc_initiator_remove_all_handlers(self->initiator, priv->event_id);
    nfc_initiator_unref(self->initiator);
    g_free(priv->name);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_host_class_init(
    NfcHostClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    g_type_class_add_private(klass, sizeof(NfcHostPriv));
    nfc_host_signals[SIGNAL_APP_CHANGED] =
        g_signal_new(SIGNAL_APP_CHANGED_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    nfc_host_signals[SIGNAL_GONE] =
        g_signal_new(SIGNAL_GONE_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    G_OBJECT_CLASS(klass)->finalize = nfc_host_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
