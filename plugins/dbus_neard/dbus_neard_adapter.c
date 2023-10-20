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

#include "dbus_neard.h"
#include "dbus_neard/org.neard.Adapter.h"

#include <nfc_adapter.h>
#include <nfc_tag.h>

#include <gutil_strv.h>
#include <gutil_macros.h>
#include <gutil_misc.h>

enum {
    ADAPTER_TAG_ADDED,
    ADAPTER_TAG_REMOVED,
    ADAPTER_POWER_CHANGED,
    ADAPTER_POWER_REQUESTED,
    ADAPTER_MODE_CHANGED,
    ADAPTER_ENABLED_CHANGED,
    ADAPTER_EVENT_COUNT
};

enum {
    NEARD_NOTIFY_POWERED,
    NEARD_CALL_START_POLL_LOOP,
    NEARD_CALL_STOP_POLL_LOOP,
    NEARD_EVENT_COUNT
};

struct dbus_neard_adapter {
    char* path;
    OrgNeardAdapter* iface;
    GDBusObjectManagerServer* object_manager;
    DBusNeardManager* agent_manager;
    GHashTable* tags;
    NfcAdapter* adapter;
    gulong nfc_event_id[ADAPTER_EVENT_COUNT];
    gulong neard_event_id[NEARD_EVENT_COUNT];
};

static const char dbus_neard_adapter_mode_idle[] = "Idle";
static const char dbus_neard_adapter_mode_initiator[] = "Initiator";
static const char dbus_neard_adapter_mode_target[] = "Target";
static const char dbus_neard_adapter_mode_dual[] = "Dual";

static
void
dbus_neard_adapter_create_tag(
    DBusNeardAdapter* self,
    NfcTag* tag)
{
    g_hash_table_replace(self->tags, (void*)tag->name,
        dbus_neard_tag_new(tag, self->path, self->object_manager,
            self->agent_manager));
}

static
void
dbus_neard_adapter_free_tag(
    void* tag)
{
    dbus_neard_tag_free((DBusNeardTag*)tag);
}

static
void
dbus_neard_adapter_tag_added(
    NfcAdapter* adapter,
    NfcTag* tag,
    void* user_data)
{
    dbus_neard_adapter_create_tag(user_data, tag);
}

static
void
dbus_neard_adapter_tag_removed(
    NfcAdapter* adapter,
    NfcTag* tag,
    void* user_data)
{
    DBusNeardAdapter* self = user_data;

    g_hash_table_remove(self->tags, (void*)tag->name);
}

static
const char*
dbus_neard_adapter_mode(
    NfcAdapter* adapter)
{
    const gboolean polling = (adapter->mode &
        (NFC_MODE_P2P_INITIATOR | NFC_MODE_READER_WRITER)) != 0;
    const gboolean listening = (adapter->mode &
        (NFC_MODE_P2P_TARGET | NFC_MODE_CARD_EMULATION)) != 0;

    if (polling && listening) {
        return dbus_neard_adapter_mode_dual;
    } else if (polling) {
        return dbus_neard_adapter_mode_initiator;
    } else if (listening) {
        return dbus_neard_adapter_mode_target;
    } else {
        return dbus_neard_adapter_mode_idle;
    }
}

static
void
dbus_neard_adapter_power_requested_changed(
    NfcAdapter* adapter,
    void* user_data)
{
    DBusNeardAdapter* self = user_data;

    if (org_neard_adapter_get_powered(self->iface) !=
        adapter->power_requested) {
        GDEBUG("Power requested: %s", adapter->power_requested ? "on" : "off");
        org_neard_adapter_set_powered(self->iface, adapter->power_requested);
    }
}

static
void
dbus_neard_adapter_mode_changed(
    NfcAdapter* adapter,
    void* user_data)
{
    DBusNeardAdapter* self = user_data;
    const char* str = dbus_neard_adapter_mode(adapter);
    const gboolean polling = (adapter->mode != NFC_MODE_NONE);

    if (g_strcmp0(org_neard_adapter_get_mode(self->iface), str)) {
        GDEBUG("Mode: %d (%s)", adapter->mode, str);
        org_neard_adapter_set_mode(self->iface, str);
    }

    if (org_neard_adapter_get_polling(self->iface) != polling) {
        GDEBUG("Polling: %s", polling ? "true" : "false");
        org_neard_adapter_set_polling(self->iface, polling);
    }
}

static
void
dbus_neard_adapter_enabled_changed(
    NfcAdapter* adapter,
    void* user_data)
{
    DBusNeardAdapter* self = user_data;

    if (org_neard_adapter_get_enabled(self->iface) != adapter->enabled) {
        GDEBUG("Enabled: %s", adapter->enabled ? "true" : "false");
        org_neard_adapter_set_enabled(self->iface, adapter->enabled);
    }
}

static
void
dbus_neard_adapter_notify_powered(
    OrgNeardAdapter* iface,
    GParamSpec* param,
    gpointer user_data)
{
    DBusNeardAdapter* self = user_data;
    const gboolean on = org_neard_adapter_get_powered(iface);

    GDEBUG("Powered: %s", on ? "on" : "off");
    nfc_adapter_request_power(self->adapter, on);
}

static
gboolean
dbus_neard_adapter_handle_start_poll_loop(
    OrgNeardAdapter* iface,
    GDBusMethodInvocation* call,
    const char* name,
    gpointer user_data)
{
    DBusNeardAdapter* self = user_data;
    NFC_MODE mode = (NFC_MODE_P2P_INITIATOR | NFC_MODE_READER_WRITER);

    GDEBUG("StartPollLoop: %s", name);

    if (!g_strcmp0(name, dbus_neard_adapter_mode_idle)) {
        mode = NFC_MODE_NONE;
    } else if (!g_strcmp0(name, dbus_neard_adapter_mode_initiator)) {
        mode = (NFC_MODE_P2P_INITIATOR | NFC_MODE_READER_WRITER);
    } else if (!g_strcmp0(name, dbus_neard_adapter_mode_target)) {
        mode = (NFC_MODE_P2P_TARGET | NFC_MODE_CARD_EMULATION);
    } else if (!g_strcmp0(name, dbus_neard_adapter_mode_dual)) {
        mode = (NFC_MODE_P2P_INITIATOR | NFC_MODE_READER_WRITER |
            NFC_MODE_P2P_TARGET | NFC_MODE_CARD_EMULATION);
    } else {
        GWARN("Invalid poll mode \"%s\", assuming \"%s\"", name,
            dbus_neard_adapter_mode_initiator);
    }

    nfc_adapter_request_mode(self->adapter, mode);
    org_neard_adapter_complete_start_poll_loop(iface, call);
    return TRUE;
}

static
gboolean
dbus_neard_adapter_handle_stop_poll_loop(
    OrgNeardAdapter* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    DBusNeardAdapter* self = user_data;

    if (getenv("NFCD_NO_STOP_POLL_LOOP")) {
        GDEBUG("Avoiding StopPollLoop");
        return FALSE;
    }

    GDEBUG("StopPollLoop");
    nfc_adapter_request_mode(self->adapter, NFC_MODE_NONE);
    org_neard_adapter_complete_stop_poll_loop(iface, call);
    return TRUE;
}

DBusNeardAdapter*
dbus_neard_adapter_new(
    NfcAdapter* adapter,
    GDBusObjectManagerServer* object_manager,
    DBusNeardManager* agent_manager)
{
    DBusNeardAdapter* self = g_new0(DBusNeardAdapter, 1);
    GDBusObjectSkeleton* object;
    NfcTag** tags;
    char** protocols = NULL;

    self->path = g_strconcat("/", adapter->name, NULL);
    self->object_manager = g_object_ref(object_manager);
    self->adapter = nfc_adapter_ref(adapter);
    self->iface = org_neard_adapter_skeleton_new();
    self->agent_manager = dbus_neard_manager_ref(agent_manager);
    self->tags = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, dbus_neard_adapter_free_tag);

    object = g_dbus_object_skeleton_new(self->path);
    g_dbus_object_skeleton_add_interface(object,
        G_DBUS_INTERFACE_SKELETON(self->iface));

    self->nfc_event_id[ADAPTER_TAG_ADDED] =
        nfc_adapter_add_tag_added_handler(adapter,
            dbus_neard_adapter_tag_added, self);
    self->nfc_event_id[ADAPTER_TAG_REMOVED] =
        nfc_adapter_add_tag_removed_handler(adapter,
            dbus_neard_adapter_tag_removed, self);
    self->nfc_event_id[ADAPTER_POWER_REQUESTED] =
        nfc_adapter_add_power_requested_handler(adapter,
            dbus_neard_adapter_power_requested_changed, self);
    self->nfc_event_id[ADAPTER_MODE_CHANGED] =
        nfc_adapter_add_mode_changed_handler(adapter,
            dbus_neard_adapter_mode_changed, self);
    self->nfc_event_id[ADAPTER_ENABLED_CHANGED] =
        nfc_adapter_add_enabled_changed_handler(adapter,
            dbus_neard_adapter_enabled_changed, self);

    /* Configure properties before registering handlers */
    if (adapter->supported_tags &
        (NFC_TAG_TYPE_MIFARE_CLASSIC | NFC_TAG_TYPE_MIFARE_ULTRALIGHT)) {
        protocols = gutil_strv_add(protocols, NEARD_PROTOCOL_MIFARE);
    }
    if (adapter->supported_tags & NFC_TAG_TYPE_FELICA) {
        protocols = gutil_strv_add(protocols, NEARD_PROTOCOL_FELICA);
    }
    if (adapter->supported_protocols &
        (NFC_PROTOCOL_T4A_TAG | NFC_PROTOCOL_T4B_TAG)) {
        protocols = gutil_strv_add(protocols, NEARD_PROTOCOL_ISO_DEP);
    }
    if (adapter->supported_protocols & NFC_PROTOCOL_NFC_DEP) {
        protocols = gutil_strv_add(protocols, NEARD_PROTOCOL_NFC_DEP);
    }
    if (protocols) {
        org_neard_adapter_set_protocols(self->iface,
            (const gchar* const*)protocols);
        g_strfreev(protocols);
    }

    org_neard_adapter_set_enabled(self->iface, adapter->enabled);
    org_neard_adapter_set_powered(self->iface, adapter->power_requested);
    org_neard_adapter_set_mode(self->iface, dbus_neard_adapter_mode(adapter));
    org_neard_adapter_set_polling(self->iface,
        adapter->mode != NFC_MODE_NONE);

    self->neard_event_id[NEARD_NOTIFY_POWERED] =
        g_signal_connect(self->iface, "notify::powered",
        G_CALLBACK(dbus_neard_adapter_notify_powered), self);
    self->neard_event_id[NEARD_CALL_START_POLL_LOOP] =
        g_signal_connect(self->iface, "handle-start-poll-loop",
        G_CALLBACK(dbus_neard_adapter_handle_start_poll_loop), self);
    self->neard_event_id[NEARD_CALL_STOP_POLL_LOOP] =
        g_signal_connect(self->iface, "handle-stop-poll-loop",
        G_CALLBACK(dbus_neard_adapter_handle_stop_poll_loop), self);

    g_dbus_object_manager_server_export(object_manager, object);
    g_object_unref(object);
    GDEBUG("Created neard D-Bus object for adapter %s", self->path);

    /* Register initial set of tags (if any) */
    for (tags = adapter->tags; *tags; tags++) {
        dbus_neard_adapter_create_tag(self, *tags);
    }
    return self;
}

void
dbus_neard_adapter_free(
    DBusNeardAdapter* self)
{
    GDEBUG("Removing neard D-Bus object for adapter %s", self->path);
    dbus_neard_manager_unref(self->agent_manager);
    g_dbus_object_manager_server_unexport(self->object_manager, self->path);
    g_object_unref(self->object_manager);
    g_hash_table_destroy(self->tags);

    nfc_adapter_remove_all_handlers(self->adapter, self->nfc_event_id);
    nfc_adapter_unref(self->adapter);

    gutil_disconnect_handlers(self->iface, self->neard_event_id,
        G_N_ELEMENTS(self->neard_event_id));
    g_object_unref(self->iface);

    g_free(self->path);
    g_free(self);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
