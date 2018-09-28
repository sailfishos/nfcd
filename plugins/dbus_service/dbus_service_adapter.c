/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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

#include "dbus_service.h"
#include "dbus_service/org.sailfishos.nfc.Adapter.h"

#include <nfc_adapter.h>
#include <nfc_tag.h>

#include <gutil_idlepool.h>
#include <gutil_misc.h>

#include <stdlib.h>

enum {
    EVENT_ENABLED_CHANGED,
    EVENT_POWERED_CHANGED,
    EVENT_MODE_CHANGED,
    EVENT_TARGET_PRESENCE,
    EVENT_TAG_ADDED,
    EVENT_TAG_REMOVED,
    EVENT_COUNT
};

enum {
    CALL_GET_ALL,
    CALL_GET_INTERFACE_VERSION,
    CALL_GET_ENABLED,
    CALL_GET_POWERED,
    CALL_GET_SUPPORTED_MODES,
    CALL_GET_MODE,
    CALL_GET_TARGET_PRESENT,
    CALL_GET_TAGS,
    CALL_COUNT
};

struct dbus_service_adapter {
    char* path;
    GDBusConnection* connection;
    OrgSailfishosNfcAdapter* iface;
    GUtilIdlePool* pool;
    GHashTable* tags;
    NfcAdapter* adapter;
    gulong event_id[EVENT_COUNT];
    gulong call_id[CALL_COUNT];
};

#define NFC_DBUS_ADAPTER_INTERFACE_VERSION  (1)

static
gboolean
dbus_service_adapter_create_tag(
    DBusServiceAdapter* self,
    NfcTag* tag)
{
    DBusServiceTag* dbus =
        dbus_service_tag_new(tag, self->path, self->connection);

    if (dbus) {
        g_hash_table_replace(self->tags, g_strdup(tag->name), dbus);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
void
dbus_service_adapter_free_tag(
    void* tag)
{
    dbus_service_tag_free((DBusServiceTag*)tag);
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
const char* const*
dbus_service_adapter_get_tag_paths(
    DBusServiceAdapter* self)
{
    const char** out = g_new(const char*, g_hash_table_size(self->tags) + 1);
    GHashTableIter it;
    gpointer value;
    int n = 0;

    g_hash_table_iter_init(&it, self->tags);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        out[n++] = dbus_service_tag_path((DBusServiceTag*)value);
    }
    out[n] = NULL;
    qsort(out, n, sizeof(char*), dbus_service_adapter_compare_strings);

    /* Deallocated by the idle pool (actual strings are owned by tags) */
    gutil_idle_pool_add(self->pool, out, g_free);
    return out;
}

static
void
dbus_service_adapter_tags_changed(
    DBusServiceAdapter* self)
{
    org_sailfishos_nfc_adapter_emit_tags_changed(self->iface,
        dbus_service_adapter_get_tag_paths(self));
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

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

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

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_adapter_free_unexported(
    DBusServiceAdapter* self)
{
    g_hash_table_destroy(self->tags);

    nfc_adapter_remove_all_handlers(self->adapter, self->event_id);
    nfc_adapter_unref(self->adapter);

    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);
    g_object_unref(self->iface);
    g_object_unref(self->connection);

    gutil_idle_pool_drain(self->pool);
    gutil_idle_pool_unref(self->pool);

    g_free(self->path);
    g_free(self);
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
    GError* error = NULL;

    g_object_ref(self->connection = connection);
    self->path = g_strconcat("/", adapter->name, NULL);
    self->adapter = nfc_adapter_ref(adapter);
    self->pool = gutil_idle_pool_new();
    self->iface = org_sailfishos_nfc_adapter_skeleton_new();
    self->tags = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, dbus_service_adapter_free_tag);

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

    /* D-Bus calls */
    self->call_id[CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_service_adapter_handle_get_all), self);
    self->call_id[CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_service_adapter_handle_get_interface_version), self);
    self->call_id[CALL_GET_ENABLED] =
        g_signal_connect(self->iface, "handle-get-enabled",
        G_CALLBACK(dbus_service_adapter_handle_get_enabled), self);
    self->call_id[CALL_GET_POWERED] =
        g_signal_connect(self->iface, "handle-get-powered",
        G_CALLBACK(dbus_service_adapter_handle_get_powered), self);
    self->call_id[CALL_GET_SUPPORTED_MODES] =
        g_signal_connect(self->iface, "handle-get-supported-modes",
        G_CALLBACK(dbus_service_adapter_handle_get_supported_modes), self);
    self->call_id[CALL_GET_MODE] =
        g_signal_connect(self->iface, "handle-get-mode",
        G_CALLBACK(dbus_service_adapter_handle_get_mode), self);
    self->call_id[CALL_GET_TARGET_PRESENT] =
        g_signal_connect(self->iface, "handle-get-target-present",
        G_CALLBACK(dbus_service_adapter_handle_get_target_present), self);
    self->call_id[CALL_GET_TAGS] =
        g_signal_connect(self->iface, "handle-get-tags",
        G_CALLBACK(dbus_service_adapter_handle_get_tags), self);

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
