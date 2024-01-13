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

#include "nfc_host_app_p.h"
#include "nfc_host_app_impl.h"

#define GLOG_MODULE_NAME NFC_HOST_LOG_MODULE
#include <gutil_log.h>
#include <gutil_macros.h>
#include <gutil_misc.h>
#include <gutil_weakref.h>

struct nfc_host_app_priv {
    GUtilWeakRef* self_ref;
    GWeakRef service_ref;
    guint8* aid;
    char* name;
};

#define THIS(obj) NFC_HOST_APP(obj)
#define THIS_TYPE NFC_TYPE_HOST_APP
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS nfc_host_app_parent_class
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, THIS_TYPE, \
        NfcHostAppClass)

G_DEFINE_ABSTRACT_TYPE(NfcHostApp, nfc_host_app, PARENT_TYPE)

/*==========================================================================*
 * Interface
 *==========================================================================*/

void
nfc_host_app_init_base(
    NfcHostApp* self,
    const GUtilData* aid,
    const char* name,
    NFC_HOST_APP_FLAGS flags)
{
    NfcHostAppPriv* priv = self->priv;

    if (aid) {
        self->aid.bytes = priv->aid = gutil_memdup(aid->bytes, aid->size);
        self->aid.size = aid->size;
    }

    if (name) {
        self->name = priv->name = g_strdup(name);
    } else if (aid) {
        GString* buf = g_string_sized_new(aid->size * 2);
        guint i;

        for (i = 0; i < aid->size; i++) {
            g_string_append_printf(buf, "%02X", aid->bytes[i]);
        }
        self->name = priv->name = g_string_free(buf, FALSE);
    } else {
        self->name = "?";
    }

    self->flags = flags;
}

NfcHostApp*
nfc_host_app_ref(
    NfcHostApp* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_host_app_unref(
    NfcHostApp* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

guint
nfc_host_app_start(
    NfcHostApp* self,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    /* Caller is supposed to check the arguments */
    return GET_THIS_CLASS(self)->start(self, host, complete,
        user_data, destroy);
}

guint
nfc_host_app_restart(
    NfcHostApp* self,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    /* Caller is supposed to check the arguments */
    return GET_THIS_CLASS(self)->restart(self, host, complete,
        user_data, destroy);
}

guint
nfc_host_app_implicit_select(
    NfcHostApp* self,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    /* Caller is supposed to check the arguments */
    return GET_THIS_CLASS(self)->implicit_select(self, host, complete,
        user_data, destroy);
}

guint
nfc_host_app_select(
    NfcHostApp* self,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    /* Caller is supposed to check the arguments */
    return GET_THIS_CLASS(self)->select(self, host, complete,
        user_data, destroy);
}

void
nfc_host_app_deselect(
    NfcHostApp* self,
    NfcHost* host)
{
    /* Caller is supposed to check the arguments */
    return GET_THIS_CLASS(self)->deselect(self, host);
}

guint
nfc_host_app_process(
    NfcHostApp* self,
    NfcHost* host,
    const NfcApdu* apdu,
    NfcHostAppResponseFunc resp,
    void* user_data,
    GDestroyNotify destroy)
{
    /* Caller is supposed to check the arguments */
    return GET_THIS_CLASS(self)->process(self, host, apdu, resp,
        user_data, destroy);
}

void
nfc_host_app_cancel(
    NfcHostApp* self,
    guint id)
{
    /* Caller checks the arguments */
    GET_THIS_CLASS(self)->cancel(self, id);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
guint
nfc_host_app_default_fail(
    NfcHostApp* self,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    return NFCD_ID_FAIL;
}

static
guint
nfc_host_app_default_restart(
    NfcHostApp* self,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    GET_THIS_CLASS(self)->deselect(self, host);
    return NFCD_ID_SYNC;
}

static
void
nfc_host_app_default(
    NfcHostApp* self,
    NfcHost* host)
{
}

static
guint
nfc_host_app_default_process(
    NfcHostApp* app,
    NfcHost* host,
    const NfcApdu* apdu,
    NfcHostAppResponseFunc resp,
    void* user_data,
    GDestroyNotify destroy)
{
    return NFCD_ID_FAIL;
}

static
void
nfc_host_app_default_cancel(
    NfcHostApp* self,
    guint id)
{
}

static
void
nfc_host_app_init(
    NfcHostApp* self)
{
    NfcHostAppPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcHostAppPriv);

    self->priv = priv;
    priv->self_ref = gutil_weakref_new(self);
    g_weak_ref_init(&priv->service_ref, NULL);
}

static
void
nfc_host_app_finalize(
    GObject* object)
{
    NfcHostApp* self = THIS(object);
    NfcHostAppPriv* priv = self->priv;

    g_free(priv->aid);
    g_free(priv->name);
    g_weak_ref_clear(&priv->service_ref);
    gutil_weakref_unref(priv->self_ref);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_host_app_class_init(
    NfcHostAppClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcHostAppPriv));
    klass->start = nfc_host_app_default_fail;
    klass->restart = nfc_host_app_default_restart;
    klass->implicit_select = nfc_host_app_default_fail;
    klass->select = nfc_host_app_default_fail;
    klass->deselect = nfc_host_app_default;
    klass->process = nfc_host_app_default_process;
    klass->cancel = nfc_host_app_default_cancel;
    G_OBJECT_CLASS(klass)->finalize = nfc_host_app_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
