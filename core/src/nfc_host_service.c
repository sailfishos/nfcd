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

#include "nfc_host_service_p.h"
#include "nfc_host_service_impl.h"

#define GLOG_MODULE_NAME NFC_HOST_LOG_MODULE
#include <gutil_log.h>
#include <gutil_macros.h>

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

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
guint
nfc_host_service_default_start(
    NfcHostService* service,
    NfcHost* host,
    NfcHostServiceBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    if (complete) {
        complete(service, TRUE, user_data);
    }
    if (destroy) {
        destroy(user_data);
    }
    return NFCD_ID_SYNC;
}

static
guint
nfc_host_service_default_process(
    NfcHostService* service,
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
    NfcHostService* service,
    guint id)
{
}

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
    G_OBJECT_CLASS(klass)->finalize = nfc_host_service_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
