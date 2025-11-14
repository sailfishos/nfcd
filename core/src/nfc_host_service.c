/*
 * Copyright (C) 2023-2025 Slava Monich <slava@monich.com>
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

#include "nfc_host_service_p.h"
#include "nfc_host_service_impl.h"
#include "nfc_host_p.h"
#include "nfc_util.h"

#define GLOG_MODULE_NAME NFC_HOST_LOG_MODULE
#include <gutil_log.h>
#include <gutil_macros.h>
#include <gutil_misc.h>

struct nfc_host_service_priv {
    char* name;
};

#define THIS(obj) NFC_HOST_SERVICE(obj)
#define THIS_TYPE NFC_TYPE_HOST_SERVICE
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS nfc_host_service_parent_class
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, THIS_TYPE, \
        NfcHostServiceClass)

G_DEFINE_ABSTRACT_TYPE(NfcHostService, nfc_host_service, PARENT_TYPE)

typedef struct nfc_host_service_default_transceive_response_data {
    NfcHostServiceTransceiveResponseFunc resp;
    GDestroyNotify destroy;
    void* user_data;
} NfcHostServiceDefaultTransceiveResponseData;

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcHostService*
nfc_host_service_ref(
    NfcHostService* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_host_service_unref(
    NfcHostService* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

void
nfc_host_service_init_base(
    NfcHostService* self,
    const char* name)
{
    if (G_LIKELY(self)) {
        NfcHostServicePriv* priv = self->priv;

        g_free(priv->name);
        self->name = priv->name = g_strdup(name);
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

guint
nfc_host_service_start(
    NfcHostService* self,
    NfcHost* host,
    NfcHostServiceBoolFunc complete,
    void* data,
    GDestroyNotify destroy)
{
    /* Caller is supposed to check the argument for NULL */
    return GET_THIS_CLASS(self)->start(self, host, complete, data, destroy);
}

guint
nfc_host_service_restart(
    NfcHostService* self,
    NfcHost* host,
    NfcHostServiceBoolFunc complete,
    void* data,
    GDestroyNotify destroy)
{
    /* Caller is supposed to check the argument for NULL */
    return GET_THIS_CLASS(self)->restart(self, host, complete, data, destroy);
}

guint
nfc_host_service_process(
    NfcHostService* self,
    NfcHost* host,
    const NfcApdu* apdu,
    NfcHostServiceResponseFunc resp,
    void* user_data,
    GDestroyNotify destroy)
{
    /* Caller is supposed to check the argument for NULL */
    return GET_THIS_CLASS(self)->process(self, host, apdu, resp,
        user_data, destroy);
}

void
nfc_host_service_cancel(
    NfcHostService* self,
    guint id)
{
    /* Caller is supposed to check the argument for NULL */
    return GET_THIS_CLASS(self)->cancel(self, id);
}

guint
nfc_host_service_transceive(
    NfcHostService* self,
    NfcHost* host,
    const GUtilData* data,
    NfcHostServiceTransceiveResponseFunc resp,
    void* user_data,
    GDestroyNotify destroy)
{
    NfcHostServiceClass* klass = GET_THIS_CLASS(self);

    /* Caller is supposed to check the argument for NULL */
    return klass->transceive(self, host, data, resp, user_data, destroy);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_host_service_default_transceive_resp(
    NfcHostService* self,
    const NfcHostServiceResponse* resp,
    void* user_data)
{
    const NfcHostServiceDefaultTransceiveResponseData* data = user_data;

    if (data->resp) {
        if (resp) {
            NfcHostServiceTransceiveResponse tx_resp;

            /* Need to copy the response data */
            tx_resp.data = nfc_apdu_response_new(resp->sw, &resp->data);
            tx_resp.sent = resp->sent;
            tx_resp.user_data = resp->user_data;
            data->resp(self, &tx_resp, data->user_data);
            g_bytes_unref(tx_resp.data);
        } else {
            data->resp(self, NULL, data->user_data);
        }
    }
}

static
void
nfc_host_service_default_transceive_destroy(
    gpointer ptr)
{
    NfcHostServiceDefaultTransceiveResponseData* data = ptr;

    if (data->destroy) {
        data->destroy(data->user_data);
    }
    g_free(data);
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
guint
nfc_host_service_default_start(
    NfcHostService* self,
    NfcHost* host,
    NfcHostServiceBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    if (complete) {
        complete(self, TRUE, user_data);
    }
    if (destroy) {
        destroy(user_data);
    }
    return NFCD_ID_SYNC;
}

static
guint
nfc_host_service_default_process(
    NfcHostService* self,
    NfcHost* host,
    const NfcApdu* apdu,
    NfcHostServiceResponseFunc resp,
    void* user_data,
    GDestroyNotify destroy)
{
    return NFCD_ID_FAIL;
}

static
void
nfc_host_service_default_cancel(
    NfcHostService* self,
    guint id)
{
}

static
guint
nfc_host_service_default_transceive(
    NfcHostService* self,
    NfcHost* host,
    const GUtilData* data,
    NfcHostServiceTransceiveResponseFunc resp,
    void* user_data,
    GDestroyNotify destroy)
{
    NfcApdu apdu;

    if (nfc_apdu_decode(&apdu, data)) {
        guint ret;
        NfcHostServiceDefaultTransceiveResponseData* data =
            g_new0(NfcHostServiceDefaultTransceiveResponseData, 1);

        data->resp = resp;
        data->destroy = destroy;
        data->user_data = user_data;
        ret = nfc_host_service_process(self, host, &apdu,
            nfc_host_service_default_transceive_resp, data,
            nfc_host_service_default_transceive_destroy);
        if (ret) {
            return ret;
        }
        g_free(data);
    }
    return NFCD_ID_FAIL;
}

/*==========================================================================*
 * GObject
 *==========================================================================*/

static
void
nfc_host_service_init(
    NfcHostService* self)
{
    NfcHostServicePriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcHostServicePriv);

    self->priv = priv;
}

static
void
nfc_host_service_finalize(
    GObject* object)
{
    NfcHostService* self = THIS(object);
    NfcHostServicePriv* priv = self->priv;

    g_free(priv->name);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_host_service_class_init(
    NfcHostServiceClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcHostServicePriv));
    klass->start = nfc_host_service_default_start;
    klass->restart = nfc_host_service_default_start;
    klass->process = nfc_host_service_default_process;
    klass->cancel = nfc_host_service_default_cancel;
    klass->transceive = nfc_host_service_default_transceive;
    G_OBJECT_CLASS(klass)->finalize = nfc_host_service_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
