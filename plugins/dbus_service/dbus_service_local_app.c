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

#include "dbus_service.h"
#include "dbus_service_util.h"
#include "dbus_service/org.sailfishos.nfc.LocalHostApp.h"

#include <nfc_host.h>
#include <nfc_host_app_impl.h>

#include <gutil_macros.h>
#include <gutil_misc.h>

#include <unistd.h>

typedef NfcHostAppClass DBusServiceLocalAppObjectClass;
typedef struct dbus_service_local_app_object {
    DBusServiceLocalApp pub;
    OrgSailfishosNfcLocalHostApp* proxy;
    GHashTable* calls;  /* id => DBusServiceLocalAppObjectCall */
    guint last_call_id;
    NfcHost* host;
    gulong host_gone_id;
    char* host_path;
    char* dbus_name;
    char* obj_path;
} DBusServiceLocalAppObject;

#define PARENT_TYPE NFC_TYPE_HOST_APP
#define PARENT_CLASS dbus_service_local_app_object_parent_class
#define THIS_TYPE dbus_service_local_app_object_get_type()
#define THIS_CLASS(klass) NFC_HOST_APP_CLASS(klass)
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, THIS_TYPE, \
        DBusServiceLocalAppObject)

G_DEFINE_TYPE(DBusServiceLocalAppObject, dbus_service_local_app_object, \
        PARENT_TYPE)

#define LOCAL_APP_INTERFACE  "org.sailfishos.nfc.LocalHostApp"
#define STOP_CALL            "Stop"
#define DESELECT_CALL        "Deselect"
#define RESPONSE_STATUS_CALL "ResponseStatus"

typedef struct dbus_service_local_app_object_call {
    guint id;
    gint ref_count;
    DBusServiceLocalAppObject* obj;
    GCancellable* cancel;
    GCallback complete;
    void* user_data;
    GDestroyNotify destroy;
} DBusServiceLocalAppObjectCall;

typedef struct dbus_service_local_app_object_response {
    DBusServiceLocalAppObject* obj;
    guint response_id;
} DBusServiceLocalAppObjectReponse;

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
GDBusConnection*
dbus_service_local_app_connection(
    DBusServiceLocalAppObject* self)
{
    return g_dbus_proxy_get_connection(G_DBUS_PROXY(self->proxy));
}

static
void
dbus_service_local_app_object_notify(
    DBusServiceLocalAppObject* self,
    const char* method,
    GVariant* args)
{
    GDBusConnection* connection = dbus_service_local_app_connection(self);
    GDBusMessage* message = g_dbus_message_new_method_call(self->dbus_name,
        self->obj_path, LOCAL_APP_INTERFACE, method);

    /* Generated stub doesn't allow setting "no-reply-expected" flag */
    g_dbus_message_set_flags(message, g_dbus_message_get_flags(message) |
        G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    g_dbus_message_set_body(message, args);
    g_dbus_connection_send_message(connection, message,
        G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, NULL);
    g_object_unref(message);
}

static
void
dbus_service_local_app_object_notify_path(
    DBusServiceLocalAppObject* self,
    const char* method,
    const char* path)
{
    dbus_service_local_app_object_notify(self, method,
        g_variant_new("(o)", path));
}

static
void
dbus_service_local_app_object_stop_notify(
    DBusServiceLocalAppObject* self)
{
    if (self->host_path) {
        dbus_service_local_app_object_notify_path(self, STOP_CALL,
            self->host_path);
        g_free(self->host_path);
        self->host_path = NULL;
    }
}

static
void
dbus_service_local_app_object_drop_host(
    DBusServiceLocalAppObject* self)
{
    if (self->host) {
        nfc_host_remove_handler(self->host, self->host_gone_id);
        nfc_host_unref(self->host);
        self->host = NULL;
        self->host_gone_id = 0;
    }
}

static
void
dbus_service_local_app_object_host_gone(
    NfcHost* host,
    void* user_data)
{
    DBusServiceLocalAppObject* self = THIS(user_data);

    dbus_service_local_app_object_stop_notify(self);
    dbus_service_local_app_object_drop_host(self);
}

static
DBusServiceLocalAppObjectCall*
dbus_service_local_app_object_call_ref(
    DBusServiceLocalAppObjectCall* call)
{
    /* The argument is never NULL */
    g_atomic_int_inc(&call->ref_count);
    return call;
}

static
void
dbus_service_local_app_object_call_unref(
    DBusServiceLocalAppObjectCall* call)
{
    /* The argument is never NULL */
    if (g_atomic_int_dec_and_test(&call->ref_count)) {
        DBusServiceLocalAppObject* self = call->obj;

        if (call->destroy) {
            call->destroy(call->user_data);
        }
        g_object_unref(self);
        gutil_object_unref(call->cancel);
        gutil_slice_free(call);
    }
}

static
DBusServiceLocalAppObjectCall*
dbus_service_local_app_object_call_new(
    DBusServiceLocalAppObject* self,
    GCallback complete,
    void* user_data,
    GDestroyNotify destroy)
{
    DBusServiceLocalAppObjectCall* call =
        g_slice_new(DBusServiceLocalAppObjectCall);

    self->last_call_id++;
    if (self->calls) {
        while (g_hash_table_contains(self->calls, GUINT_TO_POINTER
            (self->last_call_id)) ||
            !dbus_service_valid_id(self->last_call_id)) {
            self->last_call_id++;
        }
    } else {
        self->calls = g_hash_table_new_full(g_direct_hash, g_direct_equal,
            NULL, (GDestroyNotify) dbus_service_local_app_object_call_unref);
        while (!dbus_service_valid_id(self->last_call_id)) {
            self->last_call_id++;
        }
    }

    call->id = self->last_call_id;
    g_atomic_int_set(&call->ref_count, 1);
    g_object_ref(call->obj = self);
    call->cancel = g_cancellable_new();
    call->complete = complete;
    call->user_data = user_data;
    call->destroy = destroy;
    g_hash_table_insert(self->calls, GUINT_TO_POINTER(call->id), call);
    return call;
}

static
DBusServiceLocalAppObjectReponse*
dbus_service_local_app_object_response_new(
    DBusServiceLocalAppObject* obj,
    guint response_id)
{
    DBusServiceLocalAppObjectReponse* data =
        g_slice_new(DBusServiceLocalAppObjectReponse);

    g_object_ref(data->obj = obj);
    data->response_id = response_id;
    return data;
}

static
void
dbus_service_local_app_object_response_complete(
    NfcHostApp* app,
    gboolean result,
    void* user_data)
{
    DBusServiceLocalAppObjectReponse* data = user_data;
    DBusServiceLocalAppObject* self = data->obj;

    dbus_service_local_app_object_notify(self, RESPONSE_STATUS_CALL,
        g_variant_new("(ub)", data->response_id, result));
    g_object_unref(data->obj);
    gutil_slice_free(data);
}

static
gboolean
dbus_service_local_app_object_call_done(
    DBusServiceLocalAppObjectCall* call)
{
    if (call->cancel) {
        g_object_unref(call->cancel);
        call->cancel = NULL;
    }
    if (call->id) {
        gconstpointer key = GUINT_TO_POINTER(call->id);

        call->id = 0;
        g_hash_table_remove(call->obj->calls, key);
        return TRUE;
    } else {
        /* Cancelled */
        return FALSE;
    }
}

static
void
dbus_service_local_app_object_call_done_bool(
    DBusServiceLocalAppObjectCall* call,
    const char* name,
    GAsyncResult* result,
    gboolean (*finish)(
        OrgSailfishosNfcLocalHostApp *proxy,
        GAsyncResult* result,
        GError** error))
{
    DBusServiceLocalAppObject* self = call->obj;
    NfcHostAppBoolFunc cb = (NfcHostAppBoolFunc)call->complete;
    GError* error = NULL;
    gboolean completed = dbus_service_local_app_object_call_done(call);
    gboolean ok = finish(self->proxy, result, &error);

    if (error) {
        GDEBUG("%s%s %s %s", self->dbus_name, self->obj_path, name,
            GERRMSG(error));
        g_error_free(error);
    }
    if (completed && cb) {
        cb(NFC_HOST_APP(self), ok, call->user_data);
    }
    dbus_service_local_app_object_call_unref(call);
}

static
void
dbus_service_local_app_object_start_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    dbus_service_local_app_object_call_done_bool(user_data, "start",
        result, org_sailfishos_nfc_local_host_app_call_start_finish);
}

static
void
dbus_service_local_app_object_restart_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    dbus_service_local_app_object_call_done_bool(user_data, "restart",
        result, org_sailfishos_nfc_local_host_app_call_restart_finish);
}

static
guint
dbus_service_local_app_object_start(
    NfcHostApp* app,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    DBusServiceLocalAppObject* self = THIS(app);
    DBusServicePlugin* plugin = self->pub.plugin;
    DBusServiceHost* dbus_host = dbus_service_plugin_find_host(plugin, host);

    /* Stop notify has no effect if there is no current host */
    dbus_service_local_app_object_stop_notify(self);
    dbus_service_local_app_object_drop_host(self);
    if (dbus_host) {
        DBusServiceLocalAppObjectCall* call =
            dbus_service_local_app_object_call_new(self,
                G_CALLBACK(complete), user_data, destroy);
        const uint id = call->id;

        self->host_path = g_strdup(dbus_host->path);
        self->host = nfc_host_ref(host);
        self->host_gone_id = nfc_host_add_gone_handler(host,
            dbus_service_local_app_object_host_gone, self);

        org_sailfishos_nfc_local_host_app_call_start(self->proxy,
            self->host_path, call->cancel,
            dbus_service_local_app_object_start_done,
            dbus_service_local_app_object_call_ref(call));
        return id;
    }
    return NFCD_ID_FAIL;
}

static
guint
dbus_service_local_app_object_restart(
    NfcHostApp* app,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    DBusServiceLocalAppObject* self = THIS(app);
    DBusServicePlugin* plugin = self->pub.plugin;
    DBusServiceHost* dbus_host = dbus_service_plugin_find_host(plugin, host);

    if (dbus_host) {
        DBusServiceLocalAppObjectCall* call =
            dbus_service_local_app_object_call_new(self,
                G_CALLBACK(complete), user_data, destroy);
        const uint id = call->id;

        org_sailfishos_nfc_local_host_app_call_restart(self->proxy,
            self->host_path, call->cancel,
            dbus_service_local_app_object_restart_done,
            dbus_service_local_app_object_call_ref(call));
        return id;
    }
    return NFCD_ID_FAIL;
}

static
void
dbus_service_local_app_object_implicit_select_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    dbus_service_local_app_object_call_done_bool(user_data,
        "implicit select", result,
        org_sailfishos_nfc_local_host_app_call_implicit_select_finish);
}

static
guint
dbus_service_local_app_object_implicit_select(
    NfcHostApp* app,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    DBusServiceLocalAppObject* self = THIS(app);

    if (self->host_path) {
        DBusServiceLocalAppObjectCall* call =
            dbus_service_local_app_object_call_new(self,
                G_CALLBACK(complete), user_data, destroy);
        const uint id = call->id;

        org_sailfishos_nfc_local_host_app_call_implicit_select(self->proxy,
            self->host_path, call->cancel,
            dbus_service_local_app_object_implicit_select_done,
            dbus_service_local_app_object_call_ref(call));
        return id;
    }
    return NFCD_ID_FAIL;
}

static
void
dbus_service_local_app_object_select_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    dbus_service_local_app_object_call_done_bool(user_data, "select",
        result, org_sailfishos_nfc_local_host_app_call_select_finish);
}

static
guint
dbus_service_local_app_object_select(
    NfcHostApp* app,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    DBusServiceLocalAppObject* self = THIS(app);
    DBusServiceLocalAppObjectCall* call =
        dbus_service_local_app_object_call_new(self,
            G_CALLBACK(complete), user_data, destroy);
    const uint id = call->id;

    org_sailfishos_nfc_local_host_app_call_select(self->proxy,
        self->host_path, call->cancel,
        dbus_service_local_app_object_select_done,
        dbus_service_local_app_object_call_ref(call));
    return id;
}

static
void
dbus_service_local_app_object_deselect(
    NfcHostApp* app,
    NfcHost* host)
{
    DBusServiceLocalAppObject* self = THIS(app);
    DBusServicePlugin* plugin = self->pub.plugin;
    DBusServiceHost* dbus_host = dbus_service_plugin_find_host(plugin, host);

    if (dbus_host) {
        dbus_service_local_app_object_notify_path(self, DESELECT_CALL,
            dbus_host->path);
    }
}

static
void
dbus_service_local_app_object_process_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    DBusServiceLocalAppObjectCall* call = user_data;
    DBusServiceLocalAppObject* self = call->obj;
    NfcHostAppResponseFunc cb = (NfcHostAppResponseFunc)call->complete;
    GError* error = NULL;
    gboolean completed = dbus_service_local_app_object_call_done(call);
    guchar sw1, sw2;
    guint response_id = 0;
    GVariant* resp_var = NULL;

    if (org_sailfishos_nfc_local_host_app_call_process_finish(self->proxy,
        &resp_var, &sw1, &sw2, &response_id, result, &error)) {
        const gsize len = g_variant_get_size(resp_var);

#if GUTIL_LOG_DEBUG
        if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
            char* tmp = NULL;

            GDEBUG("R-APDU %s%s%02X%02X",
                len ? (tmp = gutil_bin2hex(g_variant_get_data(resp_var),
                len, TRUE)) : "", len ? " " : "", sw1, sw2);
            g_free(tmp);
        }
#endif
        if (completed && cb) {
            NfcHostAppResponse resp;

            memset(&resp, 0, sizeof(resp));
            resp.sw = (((guint)sw1) << 8) | sw2;
            resp.data.size = len;
            resp.data.bytes = g_variant_get_data(resp_var);

            if (response_id) {
                GDEBUG("Response id %u", response_id);
                resp.sent = dbus_service_local_app_object_response_complete;
                resp.user_data = dbus_service_local_app_object_response_new
                    (self, response_id);
            }

            cb(NFC_HOST_APP(self), &resp, call->user_data);
        }
        g_variant_unref(resp_var);
    } else {
        GDEBUG("%s%s process %s", self->dbus_name, self->obj_path,
            GERRMSG(error));
        g_error_free(error);
        if (completed && cb) {
            cb(NFC_HOST_APP(self), NULL, call->user_data);
        }
    }

    dbus_service_local_app_object_call_unref(call);
}

static
guint
dbus_service_local_app_object_process(
    NfcHostApp* app,
    NfcHost* host,
    const NfcApdu* apdu,
    NfcHostAppResponseFunc resp,
    void* user_data,
    GDestroyNotify destroy)
{
    DBusServiceLocalAppObject* self = THIS(app);
    DBusServiceLocalAppObjectCall* call =
        dbus_service_local_app_object_call_new(self,
            G_CALLBACK(resp), user_data, destroy);
    const uint id = call->id;

    org_sailfishos_nfc_local_host_app_call_process(self->proxy,
        self->host_path, apdu->cla, apdu->ins, apdu->p1, apdu->p2,
        gutil_data_copy_as_variant(&apdu->data), apdu->le, call->cancel,
        dbus_service_local_app_object_process_done,
        dbus_service_local_app_object_call_ref(call));
    return id;
}

static
void
dbus_service_local_app_object_cancel(
    NfcHostApp* app,
    guint id)
{
    DBusServiceLocalAppObject* self = THIS(app);
    DBusServiceLocalAppObjectCall* call = self->calls ?
        g_hash_table_lookup(self->calls, GUINT_TO_POINTER(id)) : NULL;

    if (call) {
        if (call->cancel) {
            g_cancellable_cancel(call->cancel);
            g_object_unref(call->cancel);
            call->cancel = NULL;
        }
        call->id = 0;
        call->complete = NULL;
        g_hash_table_remove(self->calls, GUINT_TO_POINTER(id));
    }
}

static
void
dbus_service_local_app_object_finalize(
    GObject* object)
{
    DBusServiceLocalAppObject* self = THIS(object);

    if (self->calls) {
        g_hash_table_destroy(self->calls);
    }
    dbus_service_local_app_object_drop_host(self);
    g_free(self->host_path);
    g_free(self->dbus_name);
    g_free(self->obj_path);
    gutil_object_unref(self->proxy);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
dbus_service_local_app_object_init(
    DBusServiceLocalAppObject* self)
{
}

static
void
dbus_service_local_app_object_class_init(
    DBusServiceLocalAppObjectClass* klass)
{
    NfcHostAppClass* service_class = THIS_CLASS(klass);

    service_class->start = dbus_service_local_app_object_start;
    service_class->restart = dbus_service_local_app_object_restart;
    service_class->implicit_select =
        dbus_service_local_app_object_implicit_select;
    service_class->select = dbus_service_local_app_object_select;
    service_class->deselect = dbus_service_local_app_object_deselect;
    service_class->process = dbus_service_local_app_object_process;
    service_class->cancel = dbus_service_local_app_object_cancel;
    G_OBJECT_CLASS(klass)->finalize = dbus_service_local_app_object_finalize;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

DBusServiceLocalApp*
dbus_service_local_app_new(
    GDBusConnection* connection,
    const char* obj_path,
    const char* name,
    const GUtilData* aid,
    NFC_HOST_APP_FLAGS flags,
    const char* dbus_name)
{
    GError* error = NULL;
    OrgSailfishosNfcLocalHostApp* proxy = /* This won't actually block */
        org_sailfishos_nfc_local_host_app_proxy_new_sync(connection,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
            G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
            dbus_name, obj_path, NULL, &error);

    if (proxy) {
        DBusServiceLocalAppObject* self = g_object_new(THIS_TYPE, NULL);
        DBusServiceLocalApp* pub = &self->pub;
        NfcHostApp* app = &pub->app;

        nfc_host_app_init_base(app, aid, name, flags);
        pub->obj_path = self->obj_path = g_strdup(obj_path);
        pub->dbus_name = self->dbus_name = g_strdup(dbus_name);
        self->proxy = proxy;
        return pub;
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
