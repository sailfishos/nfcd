/*
 * Copyright (C) 2018-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2020 Jolla Ltd.
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
#include "dbus_service/org.sailfishos.nfc.Adapter.h"

#include <nfc_adapter.h>
#include <nfc_host.h>
#include <nfc_peer.h>
#include <nfc_tag.h>

#include <gutil_idlepool.h>
#include <gutil_macros.h>
#include <gutil_misc.h>

#include <stdlib.h>

/* x(DBUS_CALL,dbus_call,dbus-call) */
#define DBUS_CALLS(x) \
    x(GET_ALL, get_all, get-all) \
    x(GET_INTERFACE_VERSION, get_interface_version, get-interface-version) \
    x(GET_ENABLED, get_enabled, get-enabled) \
    x(GET_POWERED, get_powered, get-powered) \
    x(GET_SUPPORTED_MODES, get_supported_modes, get-supported-modes) \
    x(GET_MODE, get_mode, get-mode) \
    x(GET_TARGET_PRESENT, get_target_present, get-target-present) \
    x(GET_TAGS, get_tags, get-tags) \
    x(GET_ALL2, get_all2, get-all2) \
    x(GET_PEERS, get_peers, get-peers) \
    x(GET_ALL3, get_all3, get-all3) \
    x(GET_HOSTS, get_hosts, get-hosts) \
    x(GET_SUPPORTED_TECHS, get_supported_techs, get-supported-techs) \
    x(GET_ALL4, get_all4, get-all4) \
    x(GET_PARAMS, get_params, get-params) \
    x(REQUEST_PARAMS, request_params, request-params) \
    x(RELEASE_PARAMS, release_params, release-params)

enum {
    EVENT_ENABLED_CHANGED,
    EVENT_POWERED_CHANGED,
    EVENT_MODE_CHANGED,
    EVENT_TARGET_PRESENCE,
    EVENT_TAG_ADDED,
    EVENT_TAG_REMOVED,
    EVENT_PEER_ADDED,
    EVENT_PEER_REMOVED,
    EVENT_HOST_ADDED,
    EVENT_HOST_REMOVED,
    EVENT_PARAM_CHANGED,
    EVENT_COUNT
};

enum {
    #define DEFINE_ENUM(CALL,call,name) CALL_##CALL,
    DBUS_CALLS(DEFINE_ENUM)
    #undef DEFINE_ENUM
    CALL_COUNT
};

typedef struct dbus_service_adapter_client {
    char* dbus_name;
    guint watch_id;
    DBusServiceAdapter* service;
    GHashTable* param_requests;  /* id => NfcAdapterParamRequest */
} DBusServiceAdapterClient;

struct dbus_service_adapter {
    char* path;
    GDBusConnection* connection;
    OrgSailfishosNfcAdapter* iface;
    GUtilIdlePool* pool;
    GHashTable* tags;
    GHashTable* peers;
    GHashTable* hosts;
    NfcAdapter* adapter;
    GHashTable* clients;
    guint last_request_id;
    gulong event_id[EVENT_COUNT];
    gulong call_id[CALL_COUNT];
};

#define NFC_DBUS_ADAPTER_INTERFACE_VERSION  (4)

static
void
dbus_service_adapter_client_gone(
    GDBusConnection* bus,
    const char* name,
    gpointer service)
{
    DBusServiceAdapter* self = service;

    GDEBUG("Name '%s' has disappeared", name);
    g_hash_table_remove(self->clients, name);
}

static
DBusServiceAdapterClient*
dbus_service_adapter_client_new(
    DBusServiceAdapter* self,
    const char* dbus_name)
{
    DBusServiceAdapterClient* client = g_slice_new0(DBusServiceAdapterClient);

    client->dbus_name = g_strdup(dbus_name);
    client->watch_id = g_bus_watch_name_on_connection(self->connection,
        client->dbus_name, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
        dbus_service_adapter_client_gone, self, NULL);
    return client;
}

static
void
dbus_service_adapter_client_destroy(
    gpointer value)
{
    DBusServiceAdapterClient* client = value;

    if (client->param_requests) {
        g_hash_table_destroy(client->param_requests);
    }
    g_bus_unwatch_name(client->watch_id);
    g_free(client->dbus_name);
    gutil_slice_free(client);
}

static
DBusServiceAdapterClient*
dbus_service_adapter_client_get(
    DBusServiceAdapter* self,
    const char* dbus_name)
{
    DBusServiceAdapterClient* client = NULL;

    if (self->clients) {
        client = g_hash_table_lookup(self->clients, dbus_name);
    } else {
        self->clients = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
            dbus_service_adapter_client_destroy);
    }
    if (!client) {
        client = dbus_service_adapter_client_new(self, dbus_name);
        g_hash_table_insert(self->clients, client->dbus_name, client);
    }
    return client;
}

static
int
dbus_service_adapter_compare_strings(
    const void* p1,
    const void* p2)
{
    return strcmp(*(char* const*)p1, *(char* const*)p2);
}

static
gboolean
dbus_service_adapter_create_tag(
    DBusServiceAdapter* self,
    NfcTag* tag)
{
    DBusServiceTag* dbus = dbus_service_tag_new(tag, self->path,
        self->connection);

    if (dbus) {
        g_hash_table_replace(self->tags, g_strdup(tag->name), dbus);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
gboolean
dbus_service_adapter_create_peer(
    DBusServiceAdapter* self,
    NfcPeer* peer)
{
    DBusServicePeer* dbus = dbus_service_peer_new(peer, self->path,
        self->connection);

    if (dbus) {
        g_hash_table_replace(self->peers, g_strdup(peer->name), dbus);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
gboolean
dbus_service_adapter_create_host(
    DBusServiceAdapter* self,
    NfcHost* host)
{
    DBusServiceHost* dbus = dbus_service_host_new(host, self->path,
        self->connection);

    if (dbus) {
        g_hash_table_replace(self->hosts, g_strdup(host->name), dbus);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
const char*
dbus_service_adapter_get_tag_name(
    gpointer value)
{
    return ((DBusServiceTag*)value)->path;
}

static
const char*
dbus_service_adapter_get_peer_name(
    gpointer value)
{
    return ((DBusServicePeer*)value)->path;
}

static
const char*
dbus_service_adapter_get_host_name(
    gpointer value)
{
    return ((DBusServiceHost*)value)->path;
}

static
const char* const*
dbus_service_adapter_get_paths(
    GHashTable* table,
    GUtilIdlePool* pool,
    const char* (*get_name)(gpointer))
{
    const char** out = g_new(const char*, g_hash_table_size(table) + 1);
    GHashTableIter it;
    gpointer value;
    int n = 0;

    g_hash_table_iter_init(&it, table);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        out[n++] = get_name(value);
    }
    out[n] = NULL;
    qsort(out, n, sizeof(char*), dbus_service_adapter_compare_strings);

    /* Deallocated by the idle pool (actual strings are owned by tags) */
    gutil_idle_pool_add(pool, out, g_free);
    return out;
}

static
const char* const*
dbus_service_adapter_get_tag_paths(
    DBusServiceAdapter* self)
{
    return dbus_service_adapter_get_paths(self->tags, self->pool,
        dbus_service_adapter_get_tag_name);
}

static
void
dbus_service_adapter_tags_changed(
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_emit_tags_changed(self->iface,
        dbus_service_adapter_get_tag_paths(self));
}

static
const char* const*
dbus_service_adapter_get_peer_paths(
    DBusServiceAdapter* self)
{
    return dbus_service_adapter_get_paths(self->peers, self->pool,
        dbus_service_adapter_get_peer_name);
}

static
void
dbus_service_adapter_peers_changed(
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_emit_peers_changed(self->iface,
        dbus_service_adapter_get_peer_paths(self));
}

static
const char* const*
dbus_service_adapter_get_host_paths(
    DBusServiceAdapter* self)
{
    return dbus_service_adapter_get_paths(self->hosts, self->pool,
        dbus_service_adapter_get_host_name);
}

static
void
dbus_service_adapter_hosts_changed(
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_emit_hosts_changed(self->iface,
        dbus_service_adapter_get_host_paths(self));
}

static
GVariant*
dbus_service_adapter_get_param_value(
    NfcAdapter* adapter,
    NFC_ADAPTER_PARAM id)
{
    NfcAdapterParamValue* v = nfc_adapter_param_get(adapter, id);
    GVariant* var = NULL;

    if (v) {
        const char* name = nfc_adapter_param_name(id);

        if (name) {
            switch (id) {
            case NFC_ADAPTER_PARAM_NONE:
            case NFC_ADAPTER_PARAM_COUNT:
                /* These are not real ids */
                break;
            case NFC_ADAPTER_PARAM_T4_NDEF:
                /* b */
                var = g_variant_new_boolean(v->b);
                break;
            case NFC_ADAPTER_PARAM_LA_NFCID1:
                /* nfcid1 */
                switch (v->nfcid1.len) {
                case 0: case 4: case 7: case 10:
                    var = dbus_service_dup_byte_array_as_variant
                        (v->nfcid1.bytes, v->nfcid1.len);
                    break;
                }
                break;
            case NFC_ADAPTER_PARAM_LI_A_HB:
                /* hb */
                var = dbus_service_dup_byte_array_as_variant(v->hb.bytes,
                    v->hb.len);
                break;
            }
        }
        g_free(v);
    }
    return var;
}

static
GVariant*
dbus_service_adapter_get_params(
    NfcAdapter* adapter)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

    if (G_LIKELY(adapter)) {
        const NFC_ADAPTER_PARAM* ids = nfc_adapter_param_list(adapter);
        NFC_ADAPTER_PARAM id;

        while ((id = *ids++) != NFC_ADAPTER_PARAM_NONE) {
            const char* name = nfc_adapter_param_name(id);

            if (name) {
                dbus_service_dict_add_value(&builder, name,
                    dbus_service_adapter_get_param_value(adapter, id));
            }
        }
    }
    return g_variant_builder_end(&builder);
}

static
NfcAdapterParamRequest*
dbus_service_adapter_param_request_new(
    NfcAdapter* adapter,
    GVariant* dict,
    gboolean reset)
{
    GArray* params = g_array_new(FALSE, FALSE, sizeof(NfcAdapterParam));
    const NfcAdapterParam** req_params = NULL;
    NfcAdapterParamRequest* req;
    GVariantIter it;
    GVariant* entry;
    guint i;

    g_variant_iter_init(&it, dict);
    while ((entry = g_variant_iter_next_value(&it)) != NULL) {
        if (g_variant_n_children(entry) == 2) {
            GVariant* tmp = NULL;
            GVariant* s = g_variant_get_child_value(entry, 0);
            GVariant* val = g_variant_get_child_value(entry, 1);
            const char* name = g_variant_get_string(s, NULL);
            GVariant* v = (g_variant_is_of_type(val, G_VARIANT_TYPE_VARIANT)) ?
                (tmp = g_variant_get_variant(val)) : val;
            NfcAdapterParam p;

            memset(&p, 0, sizeof(p));
            switch (p.id = nfc_adapter_param_id(name)) {
            case NFC_ADAPTER_PARAM_NONE:
            case NFC_ADAPTER_PARAM_COUNT:
                /* These are not real ids */
                break;
            case NFC_ADAPTER_PARAM_T4_NDEF:
                /* b */
                if (g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN)) {
                    p.value.b = g_variant_get_boolean(v);
                    g_array_append_vals(params, &p, 1);
                }
                break;
            case NFC_ADAPTER_PARAM_LA_NFCID1:
                /* nfcid1 */
                if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTESTRING)) {
                    NfcId1* nfcid1 = &p.value.nfcid1;

                    /* Empty NFCID1 means dynamic */
                    switch ((nfcid1->len = g_variant_get_size(v))) {
                    case 0: case 4: case 7: case 10:
                        memcpy(nfcid1->bytes, g_variant_get_data(v),
                            nfcid1->len);
                        g_array_append_vals(params, &p, 1);
                        break;
                    }
                }
                break;
            case NFC_ADAPTER_PARAM_LI_A_HB:
                /* hb */
                if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTESTRING)) {
                    const gsize len = g_variant_get_size(v);
                    NfcAtsHb* hb = &p.value.hb;

                    if (len <= sizeof(hb->bytes)) {
                        hb->len = len;
                        memcpy(hb->bytes, g_variant_get_data(v), len);
                        g_array_append_vals(params, &p, 1);
                    }
                }
                break;
            }
            if (tmp) {
                g_variant_unref(tmp);
            }
            g_variant_unref(val);
            g_variant_unref(s);
        }
        g_variant_unref(entry);
    }

    req_params = g_new(const NfcAdapterParam*, params->len + 1);
    for (i = 0; i < params->len; i++) {
        req_params[i] = ((NfcAdapterParam*) params->data) + i;
    }
    req_params[i] = NULL;
    req = nfc_adapter_param_request_new(adapter, req_params, reset);
    g_array_free(params, TRUE);
    g_free(req_params);
    return req;
}

/*==========================================================================*
 * NfcAdapter events
 *==========================================================================*/

static
void
dbus_service_adapter_enabled_changed(
    NfcAdapter* adapter,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    org_sailfishos_nfc_adapter_emit_enabled_changed(self->iface,
        self->adapter->enabled);
}

static
void
dbus_service_adapter_powered_changed(
    NfcAdapter* adapter,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    org_sailfishos_nfc_adapter_emit_powered_changed(self->iface,
        self->adapter->powered);
}

static
void
dbus_service_adapter_mode_changed(
    NfcAdapter* adapter,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    org_sailfishos_nfc_adapter_emit_mode_changed(self->iface,
        self->adapter->mode);
}

static
void
dbus_service_adapter_target_present_changed(
    NfcAdapter* adapter,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    org_sailfishos_nfc_adapter_emit_target_present_changed(self->iface,
        self->adapter->target_present);
}

static
void
dbus_service_adapter_tag_added(
    NfcAdapter* adapter,
    NfcTag* tag,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    if (dbus_service_adapter_create_tag(self, tag)) {
        dbus_service_adapter_tags_changed(self);
    }
}

static
void
dbus_service_adapter_tag_removed(
    NfcAdapter* adapter,
    NfcTag* tag,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    if (g_hash_table_remove(self->tags, (void*)tag->name)) {
        dbus_service_adapter_tags_changed(self);
    }
}

static
void
dbus_service_adapter_peer_added(
    NfcAdapter* adapter,
    NfcPeer* peer,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    if (dbus_service_adapter_create_peer(self, peer)) {
        dbus_service_adapter_peers_changed(self);
    }
}

static
void
dbus_service_adapter_peer_removed(
    NfcAdapter* adapter,
    NfcPeer* peer,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    if (g_hash_table_remove(self->peers, (void*)peer->name)) {
        dbus_service_adapter_peers_changed(self);
    }
}

static
void
dbus_service_adapter_host_added(
    NfcAdapter* adapter,
    NfcHost* host,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    if (dbus_service_adapter_create_host(self, host)) {
        dbus_service_adapter_hosts_changed(self);
    }
}

static
void
dbus_service_adapter_host_removed(
    NfcAdapter* adapter,
    NfcHost* host,
    void* user_data)
{
    DBusServiceAdapter* self = user_data;

    if (g_hash_table_remove(self->hosts, (void*)host->name)) {
        dbus_service_adapter_hosts_changed(self);
    }
}

static
void
dbus_service_adapter_param_changed(
    NfcAdapter* adapter,
    NFC_ADAPTER_PARAM id,
    void* user_data)
{
    const char* name = nfc_adapter_param_name(id);

    if (name) {
        DBusServiceAdapter* self = user_data;
        GVariant* v = dbus_service_adapter_get_param_value(self->adapter, id);

        if (v) {
            org_sailfishos_nfc_adapter_emit_param_changed(self->iface, name,
                g_variant_new_variant(v));
        }
    }
}

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

/* GetAll */

static
gboolean
dbus_service_adapter_handle_get_all(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    NfcAdapter* adapter = self->adapter;

    org_sailfishos_nfc_adapter_complete_get_all(iface, call,
        NFC_DBUS_ADAPTER_INTERFACE_VERSION, adapter->enabled, adapter->powered,
        adapter->supported_modes, adapter->mode, adapter->target_present,
        dbus_service_adapter_get_tag_paths(self));
    return TRUE;
}

/* GetInterfaceVersion */

static
gboolean
dbus_service_adapter_handle_get_interface_version(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_interface_version(iface, call,
        NFC_DBUS_ADAPTER_INTERFACE_VERSION);
    return TRUE;
}

/* GetEnabled */

static
gboolean
dbus_service_adapter_handle_get_enabled(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_enabled(iface, call,
        self->adapter->enabled);
    return TRUE;
}

/* GetPowered */

static
gboolean
dbus_service_adapter_handle_get_powered(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_powered(iface, call,
        self->adapter->powered);
    return TRUE;
}

/* GetSupportedModes */

static
gboolean
dbus_service_adapter_handle_get_supported_modes(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_supported_modes(iface, call,
        self->adapter->supported_modes);
    return TRUE;
}

/* GetMode */

static
gboolean
dbus_service_adapter_handle_get_mode(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_mode(iface, call,
        self->adapter->mode);
    return TRUE;
}

/* GetTargetPresent */

static
gboolean
dbus_service_adapter_handle_get_target_present(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_target_present(iface, call,
        self->adapter->target_present);
    return TRUE;
}

/* GetTags */

static
gboolean
dbus_service_adapter_handle_get_tags(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_tags(iface, call,
        dbus_service_adapter_get_tag_paths(self));
    return TRUE;
}

/* Interface version 2 */

/* GetAll2 */

static
gboolean
dbus_service_adapter_handle_get_all2(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    NfcAdapter* adapter = self->adapter;

    org_sailfishos_nfc_adapter_complete_get_all2(iface, call,
        NFC_DBUS_ADAPTER_INTERFACE_VERSION, adapter->enabled, adapter->powered,
        adapter->supported_modes, adapter->mode, adapter->target_present,
        dbus_service_adapter_get_tag_paths(self),
        dbus_service_adapter_get_peer_paths(self));
    return TRUE;
}

/* GetPeers */

static
gboolean
dbus_service_adapter_handle_get_peers(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_peers(iface, call,
        dbus_service_adapter_get_peer_paths(self));
    return TRUE;
}

/* Interface version 3 */

/* GetAll3 */

static
gboolean
dbus_service_adapter_handle_get_all3(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    NfcAdapter* adapter = self->adapter;

    org_sailfishos_nfc_adapter_complete_get_all3(iface, call,
        NFC_DBUS_ADAPTER_INTERFACE_VERSION, adapter->enabled, adapter->powered,
        adapter->supported_modes, adapter->mode, adapter->target_present,
        dbus_service_adapter_get_tag_paths(self),
        dbus_service_adapter_get_peer_paths(self),
        dbus_service_adapter_get_host_paths(self),
        nfc_adapter_get_supported_techs(adapter));
    return TRUE;
}

/* GetHosts */

static
gboolean
dbus_service_adapter_handle_get_hosts(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_hosts(iface, call,
        dbus_service_adapter_get_host_paths(self));
    return TRUE;
}

/* GetSupportedTechs */

static
gboolean
dbus_service_adapter_handle_get_supported_techs(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_supported_techs(iface, call,
        nfc_adapter_get_supported_techs(self->adapter));
    return TRUE;
}

/* Interface version 4 */

/* GetAll4 */

static
gboolean
dbus_service_adapter_handle_get_all4(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    NfcAdapter* adapter = self->adapter;

    org_sailfishos_nfc_adapter_complete_get_all4(iface, call,
        NFC_DBUS_ADAPTER_INTERFACE_VERSION, adapter->enabled, adapter->powered,
        adapter->supported_modes, adapter->mode, adapter->target_present,
        dbus_service_adapter_get_tag_paths(self),
        dbus_service_adapter_get_peer_paths(self),
        dbus_service_adapter_get_host_paths(self),
        nfc_adapter_get_supported_techs(adapter),
        dbus_service_adapter_get_params(adapter));
    return TRUE;
}

/* GetParams */

static
gboolean
dbus_service_adapter_handle_get_params(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_complete_get_params(iface, call,
        dbus_service_adapter_get_params(self->adapter));
    return TRUE;
}

/* RequestParams */

static
gboolean
dbus_service_adapter_handle_request_params(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    GVariant* dict,
    gboolean reset,
    DBusServiceAdapter* self)
{
    gpointer key;
    const char* sender = g_dbus_method_invocation_get_sender(call);
    NfcAdapterParamRequest* req = dbus_service_adapter_param_request_new
        (self->adapter, dict, reset);
    DBusServiceAdapterClient* client = dbus_service_adapter_client_get
        (self, sender);

    /* Lazily allocate request table */
    if (!client->param_requests) {
        client->param_requests = g_hash_table_new_full(g_direct_hash,
            g_direct_equal, NULL, (GDestroyNotify)
            nfc_adapter_param_request_free);
    }

    /* Generate unique id for this request */
    self->last_request_id++;
    key = GUINT_TO_POINTER(self->last_request_id);
    while ((client->param_requests &&
        g_hash_table_contains(client->param_requests, key)) ||
        !self->last_request_id) {
        self->last_request_id++;
        key = GUINT_TO_POINTER(self->last_request_id);
    }

    GDEBUG("Param request %s/%u", sender, self->last_request_id);
    g_hash_table_insert(client->param_requests, key, req);
    org_sailfishos_nfc_adapter_complete_request_params(iface, call,
        self->last_request_id);
    return TRUE;
}

/* ReleaseParams */

static
gboolean
dbus_service_adapter_handle_release_params(
    OrgSailfishosNfcAdapter* iface,
    GDBusMethodInvocation* call,
    guint id,
    DBusServiceAdapter* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    gboolean released = FALSE;

    if (self->clients) {
        DBusServiceAdapterClient* client = g_hash_table_lookup(self->clients,
            sender);

        released = (client && client->param_requests &&
            g_hash_table_remove(client->param_requests, GUINT_TO_POINTER(id)));
    }

    if (released) {
        GDEBUG("Param request %s/%u released", sender, id);
        org_sailfishos_nfc_adapter_complete_release_params(iface, call);
    } else {
        GDEBUG("Param request %s/%u not found", sender, id);
        g_dbus_method_invocation_return_error(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_FOUND,
                "Invalid param request %s/%u", sender, id);
    }
    return TRUE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_adapter_free_unexported(
    DBusServiceAdapter* self)
{
    if (self->clients) {
        g_hash_table_destroy(self->clients);
    }
    g_hash_table_destroy(self->tags);
    g_hash_table_destroy(self->peers);
    g_hash_table_destroy(self->hosts);

    nfc_adapter_remove_all_handlers(self->adapter, self->event_id);
    nfc_adapter_unref(self->adapter);

    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);
    g_object_unref(self->iface);
    g_object_unref(self->connection);

    gutil_idle_pool_destroy(self->pool);

    g_free(self->path);
    g_free(self);
}

DBusServicePeer*
dbus_service_adapter_find_peer(
    DBusServiceAdapter* self,
    NfcPeer* peer)
{
    if (G_LIKELY(self)) {
        GHashTableIter it;
        gpointer value;

        g_hash_table_iter_init(&it, self->peers);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            DBusServicePeer* dbus_peer = value;

            if (dbus_peer->peer == peer) {
                return dbus_peer;
            }
        }
    }
    return NULL;
}

DBusServiceHost*
dbus_service_adapter_find_host(
    DBusServiceAdapter* self,
    NfcHost* host)
{
    if (G_LIKELY(self)) {
        GHashTableIter it;
        gpointer value;

        g_hash_table_iter_init(&it, self->hosts);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            DBusServiceHost* dbus_host = value;

            if (dbus_host->host == host) {
                return dbus_host;
            }
        }
    }
    return NULL;
}

const char*
dbus_service_adapter_path(
    DBusServiceAdapter* self)
{
    return self->path;
}

DBusServiceAdapter*
dbus_service_adapter_new(
    NfcAdapter* adapter,
    GDBusConnection* connection)
{
    DBusServiceAdapter* self = g_new0(DBusServiceAdapter, 1);
    NfcTag** tags;
    NfcPeer** peers;
    NfcHost** hosts;
    GError* error = NULL;

    g_object_ref(self->connection = connection);
    self->path = g_strconcat("/", adapter->name, NULL);
    self->adapter = nfc_adapter_ref(adapter);
    self->pool = gutil_idle_pool_new();
    self->iface = org_sailfishos_nfc_adapter_skeleton_new();
    self->tags = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) dbus_service_tag_free);
    self->peers = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) dbus_service_peer_free);
    self->hosts = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) dbus_service_host_free);

    /* NfcAdapter events */
    self->event_id[EVENT_ENABLED_CHANGED] =
        nfc_adapter_add_enabled_changed_handler(adapter,
            dbus_service_adapter_enabled_changed, self);
    self->event_id[EVENT_POWERED_CHANGED] =
        nfc_adapter_add_powered_changed_handler(adapter,
            dbus_service_adapter_powered_changed, self);
    self->event_id[EVENT_MODE_CHANGED] =
        nfc_adapter_add_mode_changed_handler(adapter,
            dbus_service_adapter_mode_changed, self);
    self->event_id[EVENT_TARGET_PRESENCE] =
        nfc_adapter_add_target_presence_handler(adapter,
            dbus_service_adapter_target_present_changed, self);
    self->event_id[EVENT_TAG_ADDED] =
        nfc_adapter_add_tag_added_handler(adapter,
            dbus_service_adapter_tag_added, self);
    self->event_id[EVENT_TAG_REMOVED] =
        nfc_adapter_add_tag_removed_handler(adapter,
            dbus_service_adapter_tag_removed, self);
    self->event_id[EVENT_PEER_ADDED] =
        nfc_adapter_add_peer_added_handler(adapter,
            dbus_service_adapter_peer_added, self);
    self->event_id[EVENT_PEER_REMOVED] =
        nfc_adapter_add_peer_removed_handler(adapter,
            dbus_service_adapter_peer_removed, self);
    self->event_id[EVENT_HOST_ADDED] =
        nfc_adapter_add_host_added_handler(adapter,
            dbus_service_adapter_host_added, self);
    self->event_id[EVENT_HOST_REMOVED] =
        nfc_adapter_add_host_removed_handler(adapter,
            dbus_service_adapter_host_removed, self);
    self->event_id[EVENT_PARAM_CHANGED] =
        nfc_adapter_add_param_changed_handler(adapter, NFC_ADAPTER_PARAM_ALL,
            dbus_service_adapter_param_changed, self);

    /* Hook up D-Bus calls */
    #define CONNECT_HANDLER(CALL,call,name) self->call_id[CALL_##CALL] = \
        g_signal_connect(self->iface, "handle-"#name, \
        G_CALLBACK(dbus_service_adapter_handle_##call), self);
    DBUS_CALLS(CONNECT_HANDLER)
    #undef CONNECT_HANDLER

    /* Initialize D-Bus context for existing tags and peers (usually none) */
    for (tags = adapter->tags; *tags; tags++) {
        dbus_service_adapter_create_tag(self, *tags);
    }
    for (peers = nfc_adapter_peers(adapter); *peers; peers++) {
        dbus_service_adapter_create_peer(self, *peers);
    }
    for (hosts = nfc_adapter_hosts(adapter); *hosts; hosts++) {
        dbus_service_adapter_create_host(self, *hosts);
    }

    /* Export the interface */
    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, self->path, &error)) {
        GDEBUG("Created D-Bus object %s", self->path);
        return self;
    } else {
        GERR("%s: %s", self->path, GERRMSG(error));
        g_error_free(error);
        dbus_service_adapter_free_unexported(self);
        return NULL;
    }
}

void
dbus_service_adapter_free(
    DBusServiceAdapter* self)
{
    if (self) {
        GDEBUG("Removing D-Bus object %s", self->path);
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON
            (self->iface));
        dbus_service_adapter_free_unexported(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
