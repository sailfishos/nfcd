/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2021 Jolla Ltd.
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

#include "nfc_manager_p.h"
#include "internal/nfc_manager_i.h"
#include "nfc_adapter_p.h"
#include "nfc_host_app.h"
#include "nfc_host_service.h"
#include "nfc_peer_service.h"
#include "nfc_peer_services.h"
#include "nfc_plugins.h"
#include "nfc_log.h"

#include <gutil_misc.h>
#include <gutil_macros.h>
#include <gutil_objv.h>
#include <gutil_weakref.h>

#include <stdlib.h>

GLOG_MODULE_DEFINE("nfc-core");

#define NFC_ADAPTER_NAME_FORMAT "nfc%u"

struct nfc_mode_request {
    NfcManager* manager;
    NFC_MODE enable;
    NFC_MODE disable;
};

struct nfc_tech_request {
    NfcManager* manager;
    NFC_TECHNOLOGY enable;
    NFC_TECHNOLOGY disable;
};

struct nfc_manager_priv {
    GUtilWeakRef* ref;
    NfcPlugins* plugins;
    NfcPeerServices* peer_services;
    NfcHostService** host_services;
    NfcHostApp** host_apps;
    NfcModeRequest* p2p_request;
    NfcModeRequest* host_request;
    GHashTable* adapters;
    guint next_adapter_index;
    gboolean requested_power;
    NFC_MODE default_mode;
    GQueue mode_requests;
    GQueue tech_requests;
};

#define THIS(obj) NFC_MANAGER(obj)
#define THIS_TYPE NFC_TYPE_MANAGER
#define PARENT_TYPE G_TYPE_OBJECT
#define PARENT_CLASS nfc_manager_parent_class

typedef GObjectClass NfcManagerClass;
G_DEFINE_TYPE(NfcManager, nfc_manager, PARENT_TYPE)

enum nfc_manager_signal {
    SIGNAL_ADAPTER_ADDED,
    SIGNAL_ADAPTER_REMOVED,
    SIGNAL_SERVICE_REGISTERED,
    SIGNAL_SERVICE_UNREGISTERED,
    SIGNAL_ENABLED_CHANGED,
    SIGNAL_MODE_CHANGED,
    SIGNAL_TECHS_CHANGED,
    SIGNAL_STOPPED,
    SIGNAL_COUNT
};

#define SIGNAL_ADAPTER_ADDED_NAME         "nfc-manager-adapter-added"
#define SIGNAL_ADAPTER_REMOVED_NAME       "nfc-manager-adapter-removed"
#define SIGNAL_SERVICE_REGISTERED_NAME    "nfc-manager-service-registered"
#define SIGNAL_SERVICE_UNREGISTERED_NAME  "nfc-manager-service-unregistered"
#define SIGNAL_ENABLED_CHANGED_NAME       "nfc-manager-enabled-changed"
#define SIGNAL_MODE_CHANGED_NAME          "nfc-manager-mode-changed"
#define SIGNAL_TECHS_CHANGED_NAME         "nfc-manager-techs-changed"
#define SIGNAL_STOPPED_NAME               "nfc-manager-stopped"

static guint nfc_manager_signals[SIGNAL_COUNT] = { 0 };

#define NEW_SIGNAL(name,type) nfc_manager_signals[SIGNAL_##name] = \
    g_signal_new(SIGNAL_##name##_NAME, type, G_SIGNAL_RUN_FIRST, \
    0, NULL, NULL, NULL, G_TYPE_NONE, 0)
#define NEW_OBJECT_SIGNAL(name,type) nfc_manager_signals[SIGNAL_##name] = \
    g_signal_new(SIGNAL_##name##_NAME, type, G_SIGNAL_RUN_FIRST, \
    0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT)

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
int
nfc_manager_compare_adapters(
    const void* p1,
    const void* p2)
{
    NfcAdapter* a1 = *(NfcAdapter* const*)p1;
    NfcAdapter* a2 = *(NfcAdapter* const*)p2;
    return strcmp(a1->name, a2->name);
}

static
NfcAdapter**
nfc_manager_get_adapters(
    NfcManagerPriv* priv,
    NfcAdapter* (*ref)(NfcAdapter*))
{
    const guint count = g_hash_table_size(priv->adapters);
    NfcAdapter** out = g_new(NfcAdapter*, count + 1);
    NfcAdapter** ptr = out;
    GHashTableIter iter;
    gpointer value;

    g_hash_table_iter_init(&iter, priv->adapters);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        NfcAdapter* adapter = value;
        *ptr++ = ref ? ref(adapter) : adapter;
    }
    *ptr = NULL;

    /* Sort adapters by name */
    qsort(out, count, sizeof(NfcAdapter*), nfc_manager_compare_adapters);
    return out;
}

static
NfcAdapter**
nfc_manager_adapters(
    NfcManagerPriv* priv)
{
    return nfc_manager_get_adapters(priv, NULL);
}

static
NfcAdapter**
nfc_manager_ref_adapters(
    NfcManagerPriv* priv)
{
    if (g_hash_table_size(priv->adapters) > 0) {
        return nfc_manager_get_adapters(priv, nfc_adapter_ref);
    } else {
        return NULL;
    }
}

static
void
nfc_manager_unref_adapters(
    NfcAdapter** adapters)
{
    NfcAdapter** ptr = adapters;

    /* Caller checks adapters for NULL */
    while (*ptr) nfc_adapter_unref(*ptr++);
    g_free(adapters);
}

static
void
nfc_manager_foreach_adapter(
    NfcManager* self,
    void (*fn)(NfcManager*, NfcAdapter*))
{
    NfcAdapter** adapters = nfc_manager_ref_adapters(self->priv);

    if (adapters) {
        NfcAdapter** ptr = adapters;

        while (*ptr) {
            fn(self, *ptr++);
        }
        nfc_manager_unref_adapters(adapters);
    }
}

static
void
nfc_manager_update_adapter_mode(
    NfcManager* manager,
    NfcAdapter* adapter)
{
    nfc_adapter_request_mode(adapter, manager->mode);
}

static
void
nfc_manager_update_mode_cb(
    gpointer list_data,
    gpointer user_data)
{
    const NfcModeRequest* req = list_data;
    NfcManager* self = THIS(user_data);

    self->mode = (self->mode & ~req->disable) | req->enable;
}

static
gboolean
nfc_manager_update_mode(
    NfcManager* self)
{
    NfcManagerPriv* priv = self->priv;
    const NFC_MODE prev_mode = self->mode;

    self->mode = priv->default_mode;
    g_queue_foreach(&priv->mode_requests, nfc_manager_update_mode_cb, self);
    if (self->mode != prev_mode) {
        GDEBUG("NFC mode 0x%02x", self->mode);
        g_signal_emit(self, nfc_manager_signals[SIGNAL_MODE_CHANGED], 0);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
NFC_TECHNOLOGY
nfc_manager_get_supported_techs(
    NfcManagerPriv* priv)
{
    NFC_TECHNOLOGY techs = NFC_TECHNOLOGY_UNKNOWN;
    GHashTableIter it;
    gpointer adapter;

    g_hash_table_iter_init(&it, priv->adapters);
    while (g_hash_table_iter_next(&it, NULL, &adapter)) {
        techs |= nfc_adapter_get_supported_techs(NFC_ADAPTER(adapter));
    }

    return techs;
}

static
void
nfc_manager_update_adapter_techs(
    NfcManager* manager,
    NfcAdapter* adapter)
{
    nfc_adapter_set_allowed_techs(adapter, manager->techs);
}

static
void
nfc_manager_update_techs_cb(
    gpointer list_data,
    gpointer user_data)
{
    const NfcTechRequest* req = list_data;
    NfcManager* self = THIS(user_data);

    self->techs = (self->techs & ~req->disable) | req->enable;
}

static
gboolean
nfc_manager_update_techs(
    NfcManager* self)
{
    NfcManagerPriv* priv = self->priv;
    const NFC_TECHNOLOGY prev = self->techs;
    const NFC_TECHNOLOGY supported = nfc_manager_get_supported_techs(priv);

    self->techs = supported;
    g_queue_foreach(&priv->tech_requests, nfc_manager_update_techs_cb, self);
    self->techs &= supported;
    if (self->techs != prev) {
        GDEBUG("NFC techs 0x%02x", self->techs);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
NfcModeRequest*
nfc_manager_mode_request_new_internal(
    NfcManager* self,
    gboolean internal,
    NFC_MODE enable,
    NFC_MODE disable)
{
    NfcManagerPriv* priv = self->priv;
    NfcModeRequest* req = g_slice_new(NfcModeRequest);

    req->manager = internal ? NULL : nfc_manager_ref(self);
    req->enable = enable;
    req->disable = disable;
    g_queue_push_tail(&priv->mode_requests, req);
    if (nfc_manager_update_mode(self)) {
        nfc_manager_foreach_adapter(self, nfc_manager_update_adapter_mode);
    }
    return req;
}

static
void
nfc_manager_mode_request_free_internal(
    NfcManager* self,
    NfcModeRequest* req)
{
    NfcManagerPriv* priv = self->priv;

    g_queue_remove(&priv->mode_requests, req);

    /* Update the effective mode */
    if (nfc_manager_update_mode(self)) {
        nfc_manager_foreach_adapter(self, nfc_manager_update_adapter_mode);
    }

    nfc_manager_unref(req->manager); /* Can be NULL */
    gutil_slice_free(req);
}

static
void
nfc_manager_release_internal_p2p_mode_request(
    NfcManager* self)
{
    NfcManagerPriv* priv = self->priv;
    NfcModeRequest* req = priv->p2p_request;

    if (req) {
        /*
         * Since nfc_manager_mode_request_free_internal() may emit signals,
         * we need to NULLify the pointer beforehand.
         */
        priv->p2p_request = NULL;
        nfc_manager_mode_request_free_internal(self, req);
    }
}

static
void
nfc_manager_release_internal_host_mode_request(
    NfcManager* self)
{
    NfcManagerPriv* priv = self->priv;
    NfcModeRequest* req = priv->host_request;

    if (req) {
        /*
         * Since nfc_manager_mode_request_free_internal() may emit signals,
         * we need to NULLify the pointer beforehand.
         */
        priv->host_request = NULL;
        nfc_manager_mode_request_free_internal(self, req);
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcManager*
nfc_manager_ref(
    NfcManager* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_manager_unref(
    NfcManager* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

NfcPlugin* const*
nfc_manager_plugins(
    NfcManager* self)
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        return nfc_plugins_list(priv->plugins);
    }
    return NULL;
}

NfcAdapter*
nfc_manager_get_adapter(
    NfcManager* self,
    const char* name)
{
    if (G_LIKELY(self) && G_LIKELY(name)) {
        NfcManagerPriv* priv = self->priv;

        return g_hash_table_lookup(priv->adapters, name);
    }
    return NULL;
}

const char*
nfc_manager_add_adapter(
    NfcManager* self,
    NfcAdapter* adapter)
{
    if (G_LIKELY(self) && G_LIKELY(adapter)) {
        /* Name is assigned to adapter only once in its lifetime */
        if (!adapter->name) {
            NfcManagerPriv* priv = self->priv;
            gboolean techs_changed;
            char* name = g_strdup_printf(NFC_ADAPTER_NAME_FORMAT,
                priv->next_adapter_index);

            priv->next_adapter_index++;
            while (g_hash_table_contains(priv->adapters, name)) {
                /* This is rather unlikely... */
                g_free(name);
                name = g_strdup_printf(NFC_ADAPTER_NAME_FORMAT,
                    priv->next_adapter_index);
                priv->next_adapter_index++;
            }

            nfc_adapter_set_manager_ref(adapter, priv->ref);
            nfc_adapter_set_name(adapter, name);
            nfc_adapter_set_enabled(adapter, self->enabled);
            nfc_adapter_request_mode(adapter, self->mode);
            nfc_adapter_request_power(adapter, priv->requested_power);
            g_hash_table_insert(priv->adapters, name, nfc_adapter_ref(adapter));
            g_free(self->adapters);
            self->adapters = nfc_manager_adapters(priv);
            techs_changed = nfc_manager_update_techs(self);
            nfc_adapter_set_allowed_techs(adapter, self->techs);
            g_signal_emit(self, nfc_manager_signals
                [SIGNAL_ADAPTER_ADDED], 0, adapter);
            if (techs_changed) {
                g_signal_emit(self, nfc_manager_signals
                    [SIGNAL_TECHS_CHANGED], 0);
            }
        }
        return adapter->name;
    }
    return NULL;
}

void
nfc_manager_remove_adapter(
    NfcManager* self,
    const char* name)
{
    if (G_LIKELY(self) && G_LIKELY(name)) {
        NfcManagerPriv* priv = self->priv;
        NfcAdapter* adapter = g_hash_table_lookup(priv->adapters, name);

        if (adapter) {
            gboolean techs_changed;

            nfc_adapter_ref(adapter);
            g_hash_table_remove(priv->adapters, name);
            g_free(self->adapters);
            self->adapters = nfc_manager_adapters(priv);
            techs_changed = nfc_manager_update_techs(self);
            g_signal_emit(self, nfc_manager_signals
                [SIGNAL_ADAPTER_REMOVED], 0, adapter);
            if (techs_changed) {
                g_signal_emit(self, nfc_manager_signals
                    [SIGNAL_TECHS_CHANGED], 0);
            }
            nfc_adapter_unref(adapter);
        }
    }
}

void
nfc_manager_set_enabled(
    NfcManager* self,
    gboolean enabled)
{
    if (G_LIKELY(self) && self->enabled != enabled) {
        NfcAdapter** adapters;

        self->enabled = enabled;
        g_signal_emit(self, nfc_manager_signals[SIGNAL_ENABLED_CHANGED], 0);
        adapters = nfc_manager_ref_adapters(self->priv);
        if (adapters) {
            NfcAdapter** ptr = adapters;

            while (*ptr) {
                nfc_adapter_set_enabled(*ptr++, self->enabled);
            }
            nfc_manager_unref_adapters(adapters);
        }
    }
}

void
nfc_manager_request_power(
    NfcManager* self,
    gboolean on)
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;
        NfcAdapter** adapters = nfc_manager_ref_adapters(self->priv);

        priv->requested_power = on;
        if (adapters) {
            NfcAdapter** ptr = adapters;

            while (*ptr) {
                nfc_adapter_request_power(*ptr++, on);
            }
            nfc_manager_unref_adapters(adapters);
        }
    }
}

void
nfc_manager_request_mode(
    NfcManager* self,
    NFC_MODE mode)
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        if (priv->default_mode != mode) {
            GDEBUG("Default mode 0x%02x", mode);
            priv->default_mode = mode;
            if (nfc_manager_update_mode(self)) {
                nfc_manager_foreach_adapter(self,
                    nfc_manager_update_adapter_mode);
            }
        }
    }
}

void
nfc_manager_stop(
    NfcManager* self,
    int error)
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        /* Only the first error gets stored */
        if (error && !self->error) {
            self->error = error;
        }

        if (!self->stopped) {
            self->stopped = TRUE;
            nfc_plugins_stop(priv->plugins);
            g_signal_emit(self, nfc_manager_signals[SIGNAL_STOPPED], 0);
        }
    }
}

gboolean
nfc_manager_register_service(
    NfcManager* self,
    NfcPeerService* service) /* Since 1.1.0 */
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        if (nfc_peer_services_add(priv->peer_services, service)) {
            nfc_peer_service_ref(service);
            self->services = priv->peer_services->list;
            if (!priv->p2p_request) {
                priv->p2p_request = nfc_manager_mode_request_new_internal(self,
                    TRUE, NFC_MODES_P2P, NFC_MODE_NONE);
            }
            g_signal_emit(self, nfc_manager_signals
                [SIGNAL_SERVICE_REGISTERED], 0, service);
            nfc_peer_service_unref(service);
            return TRUE;
        }
    }
    return FALSE;
}

void
nfc_manager_unregister_service(
    NfcManager* self,
    NfcPeerService* service) /* Since 1.1.0 */
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        nfc_peer_service_ref(service);
        if (nfc_peer_services_remove(priv->peer_services, service)) {
            self->services = priv->peer_services->list;
            if (!priv->peer_services->list[0]) {
                nfc_manager_release_internal_p2p_mode_request(self);
            }
            g_signal_emit(self, nfc_manager_signals
                [SIGNAL_SERVICE_UNREGISTERED], 0, service);
        }
        nfc_peer_service_unref(service);
    }
}

gboolean
nfc_manager_register_host_service(
    NfcManager* self,
    NfcHostService* service) /* Since 1.2.0 */
{
    if (G_LIKELY(self) && G_LIKELY(service)) {
        NfcManagerPriv* priv = self->priv;
        GObject** objv = (GObject**) priv->host_services;
        GObject* obj = G_OBJECT(service);

        if (!gutil_objv_contains(objv, obj)) {
            GDEBUG("Registered service '%s'", service->name);
            priv->host_services = (NfcHostService**)
                gutil_objv_add(objv, obj);
            if (!priv->host_request) {
                priv->host_request =
                    nfc_manager_mode_request_new_internal(self, TRUE,
                        NFC_MODE_CARD_EMULATION, NFC_MODE_NONE);
            }
            return TRUE;
        }
    }
    return FALSE;
}

void
nfc_manager_unregister_host_service(
    NfcManager* self,
    NfcHostService* service) /* Since 1.2.0 */
{
    if (G_LIKELY(self) && G_LIKELY(service)) {
        NfcManagerPriv* priv = self->priv;
        GObject** objv = (GObject**) priv->host_services;
        GObject* obj = G_OBJECT(service);

        if (gutil_objv_contains(objv, obj)) {
            GDEBUG("Unregistered service '%s'", service->name);
            priv->host_services = (NfcHostService**)
                gutil_objv_remove(objv, obj, FALSE);
            if (gutil_ptrv_is_empty(priv->host_apps) &&
                gutil_ptrv_is_empty(priv->host_services)) {
                nfc_manager_release_internal_host_mode_request(self);
            }
        }
    }
}

gboolean
nfc_manager_register_host_app(
    NfcManager* self,
    NfcHostApp* app) /* Since 1.2.0 */
{
    if (G_LIKELY(self) && G_LIKELY(app)) {
        NfcManagerPriv* priv = self->priv;
        GObject** objv = (GObject**) priv->host_apps;
        GObject* obj = G_OBJECT(app);

        if (!gutil_objv_contains(objv, obj)) {
            priv->host_apps = (NfcHostApp**) gutil_objv_add(objv, obj);
 #if GUTIL_LOG_DEBUG
            if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
                char* aid = gutil_data2hex(&app->aid, FALSE);

                GDEBUG("Registered app '%s' %s", app->name, aid);
                g_free(aid);
            }
#endif
            if (!priv->host_request) {
                priv->host_request =
                    nfc_manager_mode_request_new_internal(self, TRUE,
                        NFC_MODE_CARD_EMULATION, NFC_MODE_NONE);
            }
            return TRUE;
        }
    }
    return FALSE;
}

void
nfc_manager_unregister_host_app(
    NfcManager* self,
    NfcHostApp* app) /* Since 1.2.0 */
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;
        GObject** objv = (GObject**) priv->host_apps;
        GObject* obj = G_OBJECT(app);

        if (gutil_objv_contains(objv, obj)) {
            GDEBUG("Unregistered app '%s'", app->name);
            priv->host_apps = (NfcHostApp**)
                gutil_objv_remove(objv, obj, FALSE);
            if (gutil_ptrv_is_empty(priv->host_apps) &&
                gutil_ptrv_is_empty(priv->host_services)) {
                nfc_manager_release_internal_host_mode_request(self);
            }
        }
    }
}

gulong
nfc_manager_add_adapter_added_handler(
    NfcManager* self,
    NfcManagerAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ADAPTER_ADDED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_adapter_removed_handler(
    NfcManager* self,
    NfcManagerAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ADAPTER_REMOVED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_enabled_changed_handler(
    NfcManager* self,
    NfcManagerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ENABLED_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_mode_changed_handler(
    NfcManager* self,
    NfcManagerFunc func,
    void* user_data) /* Since 1.1.0 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_MODE_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_service_registered_handler(
    NfcManager* self,
    NfcManagerServiceFunc func,
    void* user_data) /* Since 1.1.1 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_SERVICE_REGISTERED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_service_unregistered_handler(
    NfcManager* self,
    NfcManagerServiceFunc func,
    void* user_data) /* Since 1.1.1 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_SERVICE_UNREGISTERED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_techs_changed_handler(
    NfcManager* self,
    NfcManagerFunc func,
    void* user_data) /* Since 1.2.0 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_TECHS_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_manager_add_stopped_handler(
    NfcManager* self,
    NfcManagerFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_STOPPED_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_manager_remove_handler(
    NfcManager* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_manager_remove_handlers(
    NfcManager* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

NfcModeRequest*
nfc_manager_mode_request_new(
    NfcManager* self,
    NFC_MODE enable,
    NFC_MODE disable) /* Since 1.1.0 */
{
    return (G_LIKELY(self) && G_LIKELY(enable || disable)) ?
        nfc_manager_mode_request_new_internal(self, FALSE, enable, disable) :
        NULL;
}

void
nfc_manager_mode_request_free(
    NfcModeRequest* req) /* Since 1.1.0 */
{
    if (G_LIKELY(req)) {
        nfc_manager_mode_request_free_internal(req->manager, req);
    }
}

NfcTechRequest*
nfc_manager_tech_request_new(
    NfcManager* self,
    NFC_TECHNOLOGY enable,
    NFC_TECHNOLOGY disable) /* Since 1.2.0 */
{
    if (G_LIKELY(self) && G_LIKELY(enable || disable)) {
        NfcManagerPriv* priv = self->priv;
        NfcTechRequest* req = g_slice_new0(NfcTechRequest);

        req->manager = nfc_manager_ref(self);
        req->enable = enable;
        req->disable = disable;
        g_queue_push_tail(&priv->tech_requests, req);

        if (nfc_manager_update_techs(self)) {
            g_signal_emit(self, nfc_manager_signals[SIGNAL_TECHS_CHANGED], 0);
            nfc_manager_foreach_adapter(self,
                nfc_manager_update_adapter_techs);
        }
        return req;
    }
    return NULL;
}

void
nfc_manager_tech_request_free(
    NfcTechRequest* req) /* Since 1.2.0 */
{
    if (G_LIKELY(req)) {
        NfcManager* self = req->manager;
        NfcManagerPriv* priv = self->priv;

        g_queue_remove(&priv->tech_requests, req);

        if (nfc_manager_update_techs(self)) {
            g_signal_emit(self, nfc_manager_signals[SIGNAL_TECHS_CHANGED], 0);
            nfc_manager_foreach_adapter(self,
                nfc_manager_update_adapter_techs);
        }
        nfc_manager_unref(req->manager);
        gutil_slice_free(req);
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

NfcManager*
nfc_manager_new(
    const NfcPluginsInfo* pi)
{
    NfcManager* self = g_object_new(THIS_TYPE, NULL);
    NfcManagerPriv* priv = self->priv;

    priv->plugins = nfc_plugins_new(pi);
    return self;
}

gboolean
nfc_manager_start(
    NfcManager* self)
{
    if (G_LIKELY(self)) {
        NfcManagerPriv* priv = self->priv;

        return nfc_plugins_start(priv->plugins, self);
    }
    return FALSE;
}

NfcPeerServices*
nfc_manager_peer_services(
    NfcManager* self)
{
    return G_LIKELY(self) ? self->priv->peer_services : NULL;
}

NfcHostService* const*
nfc_manager_host_services(
    NfcManager* self)
{
    return G_LIKELY(self) ? self->priv->host_services : NULL;
}

NfcHostApp* const*
nfc_manager_host_apps(
    NfcManager* self)
{
    return G_LIKELY(self) ? self->priv->host_apps : NULL;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_manager_init(
    NfcManager* self)
{
    NfcManagerPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcManagerPriv);

    priv->ref = gutil_weakref_new(self);
    priv->peer_services = nfc_peer_services_new();
    priv->adapters = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, g_object_unref);
    self->priv = priv;
    self->adapters = g_new0(NfcAdapter*, 1);
    self->enabled = TRUE;
    self->mode = priv->default_mode = NFC_MODE_READER_WRITER;
    self->llcp_version = NFC_LLCP_VERSION_1_1;
    self->services = priv->peer_services->list;
    g_queue_init(&priv->mode_requests);
    g_queue_init(&priv->tech_requests);
}

static
void
nfc_manager_finalize(
    GObject* object)
{
    NfcManager* self = THIS(object);
    NfcManagerPriv* priv = self->priv;

    gutil_weakref_unref(priv->ref);
    nfc_plugins_free(priv->plugins);
    gutil_objv_free((GObject**) priv->host_services);
    gutil_objv_free((GObject**) priv->host_apps);
    nfc_manager_release_internal_p2p_mode_request(self);
    nfc_manager_release_internal_host_mode_request(self);
    nfc_peer_services_unref(priv->peer_services);
    g_hash_table_destroy(priv->adapters);
    g_free(self->adapters);
    G_OBJECT_CLASS(nfc_manager_parent_class)->finalize(object);
}

static
void
nfc_manager_class_init(
    NfcManagerClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(NfcManagerPriv));
    object_class->finalize = nfc_manager_finalize;

    NEW_SIGNAL(ENABLED_CHANGED, type);
    NEW_SIGNAL(MODE_CHANGED, type);
    NEW_SIGNAL(TECHS_CHANGED, type);
    NEW_SIGNAL(STOPPED, type);
    NEW_OBJECT_SIGNAL(ADAPTER_ADDED, type);
    NEW_OBJECT_SIGNAL(ADAPTER_REMOVED, type);
    NEW_OBJECT_SIGNAL(SERVICE_REGISTERED, type);
    NEW_OBJECT_SIGNAL(SERVICE_UNREGISTERED, type);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
