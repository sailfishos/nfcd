/*
 * Copyright (C) 2021-2022 Jolla Ltd.
 * Copyright (C) 2021-2022 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

#include "dbus_neard.h"
#include "dbus_neard/org.neard.Manager.h"
#include "dbus_neard/org.neard.HandoverAgent.h"

#include <nfc_ndef.h>

#include <gutil_misc.h>
#include <gutil_macros.h>

enum {
    NEARD_CALL_REGISTER_HANDOVER_AGENT,
    NEARD_CALL_UNREGISTER_HANDOVER_AGENT,
    NEARD_CALL_COUNT
};

struct dbus_neard_manager {
    int refcount;
    OrgNeardManager* iface;
    gulong neard_calls[NEARD_CALL_COUNT];
    GHashTable* agents; /* carrier => DBusNeardHandoverAgent */
    const DBusNeardOptions* options;
};

typedef struct dbus_neard_handover_agent {
    DBusNeardManager* manager;
    OrgNeardHandoverAgent* proxy;
    char* peer;
    char* path;
    const char* carrier;
    guint watch;
} DBusNeardHandoverAgent;

typedef struct dbus_neard_handover_call {
    OrgNeardManager* iface;
    const char* carrier;
} DBusNeardHandoverCall;

static const char NEARD_MANAGER_PATH[] = "/";
static const char BLUETOOTH_CARRIER[] = "bluetooth";
static const char WIFI_CARRIER[] = "wifi";

static
void
dbus_neard_manager_drop_agent(
    DBusNeardHandoverAgent* agent)
{
    if (agent->watch) {
        g_bus_unwatch_name(agent->watch);
        agent->watch = 0;
    }
    if (agent->proxy) {
        g_object_unref(agent->proxy);
        agent->proxy = NULL;
    }
}

static
void
dbus_neard_manager_peer_vanished(
    GDBusConnection* connection,
    const char* name,
    gpointer user_data)
{
    DBusNeardHandoverAgent* agent = user_data;
    DBusNeardManager* manager = agent->manager;

    GDEBUG("Handover agent %s is gone", name);
    dbus_neard_manager_drop_agent(agent);
    g_hash_table_remove(manager->agents, agent->carrier);
}

static
DBusNeardHandoverAgent*
dbus_neard_manager_new_agent(
    DBusNeardManager* manager,
    GDBusMethodInvocation* call,
    const char* path,
    const char* carrier,
    GError** error)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    OrgNeardHandoverAgent* proxy =
        org_neard_handover_agent_proxy_new_for_bus_sync(DBUS_NEARD_BUS_TYPE,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, sender, path, NULL,
            error);

    if (proxy) {
        DBusNeardHandoverAgent* agent = g_new0(DBusNeardHandoverAgent, 1);

        GDEBUG("Registered %s handover agent %s at %s", carrier, path, sender);
        agent->manager = manager;
        agent->peer = g_strdup(sender);
        agent->path = g_strdup(path);
        agent->carrier = carrier; /* static pointer */
        agent->proxy = proxy;
        agent->watch = g_bus_watch_name(DBUS_NEARD_BUS_TYPE, sender,
            G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
            dbus_neard_manager_peer_vanished, agent, NULL);
        return agent;
    } else {
        return NULL;
    }
}

static
void
dbus_neard_manager_release_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    GError* error = NULL;

    if (org_neard_handover_agent_call_release_finish(
        ORG_NEARD_HANDOVER_AGENT(proxy), result, &error)) {
        GDEBUG("Release OK");
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
}

static
void
dbus_neard_manager_push_oob_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    GError* error = NULL;
    DBusNeardHandoverCall* call = user_data;

    if (org_neard_handover_agent_call_push_oob_finish(
        ORG_NEARD_HANDOVER_AGENT(proxy), result, &error)) {
        GDEBUG("Handover OK");
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }

    /* This signal can be used by the UI to notify the user */
    org_neard_manager_emit_static_handover_completed(call->iface,
        call->carrier, !error);

    /* Free the call context */
    g_object_unref(call->iface);
    gutil_slice_free(call);
}

static
void
dbus_neard_manager_free_agent(
    void* user_data)
{
    DBusNeardHandoverAgent* agent = user_data;

    if (agent->watch) g_bus_unwatch_name(agent->watch);
    if (agent->proxy) {
        /* D-Bus task will hold its own reference to the proxy */
        org_neard_handover_agent_call_release(agent->proxy, NULL,
            dbus_neard_manager_release_done, NULL);
        g_object_unref(agent->proxy);
    }
    g_free(agent->peer);
    g_free(agent->path);
    g_free(agent);
}

static
gboolean
dbus_neard_manager_parse_Hs(
    NfcNdefRec* ndef,
    GUtilData* cdr)
{
    static const GUtilData type_Hs = { (const guint8*)"Hs", 2 };
    static const GUtilData type_ac = { (const guint8*)"ac", 2 };

    if ((ndef->flags & (NFC_NDEF_REC_FLAG_FIRST | NFC_NDEF_REC_FLAG_LAST)) ==
        NFC_NDEF_REC_FLAG_FIRST && ndef->tnf == NFC_NDEF_TNF_WELL_KNOWN &&
        gutil_data_equal(&ndef->type, &type_Hs) && ndef->payload.size > 1 &&
        (ndef->payload.bytes[0] & 0xf0) == 0x10 /* MAJOR_VERSION */) {
        /* The Handover Select Record, MAJOR_VERSION = 1 */
        GUtilData ac;

        /* Expecting "ac" record*/
        ac.size = ndef->payload.size - 1;
        ac.bytes = ndef->payload.bytes + 1;
        if (ac.size > 6 &&
            ac.bytes[0] == 0xd1 &&
            ac.bytes[1] == type_ac.size &&
            ac.size == 3 + type_ac.size + ac.bytes[2] &&
            !memcmp(ac.bytes + 3, type_ac.bytes, type_ac.size)) {
            /* Alternative Carrier Record inside Hs */
            GUtilData ac_payload;

            ac_payload.size = ac.bytes[2];
            ac_payload.bytes = ac.bytes + (3 + type_ac.size);
            if (ac_payload.size > 1 &&
               (1 + ac_payload.bytes[1]) <= ac_payload.size) {
                /* CARRIER_DATA_REFERENCE */
                cdr->size = ac_payload.bytes[1];
                cdr->bytes = ac_payload.bytes + 2;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static
gboolean
dbus_neard_manager_parse_bluetooth_oob(
    NfcNdefRec* ndef,
    const GUtilData* cdr,
    GUtilData* eir)
{
    static const GUtilData type_bluetooth_oob = {
        (const guint8*)"application/vnd.bluetooth.ep.oob", 32
    };

    if (ndef->tnf == NFC_NDEF_TNF_MEDIA_TYPE &&
        gutil_data_equal(&ndef->type, &type_bluetooth_oob) &&
        gutil_data_equal(&ndef->id, cdr) &&
        ndef->payload.size >= 8) {
        *eir = ndef->payload;
        return ((eir->bytes[0] + (((guint)eir->bytes[1]) << 8)) == eir->size);
    }
    return FALSE;
}

static
const char*
dbus_neard_manager_valid_carrier(
    const char* carrier)
{
    static const char* valid_carriers[] = { BLUETOOTH_CARRIER, WIFI_CARRIER };
    gsize i;

    for (i = 0; i < G_N_ELEMENTS(valid_carriers); i++) {
        if (!g_strcmp0(carrier, valid_carriers[i])) {
            return valid_carriers[i];
        }
    }
    return NULL;
}

static
void
dbus_neard_manager_invalid_args(
    GDBusMethodInvocation* call,
    const char* message)
{
    g_dbus_method_invocation_return_error_literal(call, DBUS_NEARD_ERROR,
        DBUS_NEARD_ERROR_INVALID_ARGS, message);
}

static
void
dbus_neard_manager_invalid_carrier(
    GDBusMethodInvocation* call,
    const char* carrier)
{
    GDEBUG("Invalid carrier '%s'", carrier);
    g_dbus_method_invocation_return_error(call, DBUS_NEARD_ERROR,
        DBUS_NEARD_ERROR_INVALID_ARGS, "Invalid carrier '%s'", carrier);
}

static
gboolean
dbus_neard_manager_register_handover_agent(
    OrgNeardManager* iface,
    GDBusMethodInvocation* call,
    const char* path,
    const char* carrier,
    gpointer user_data)
{
    const char* valid_carrier = dbus_neard_manager_valid_carrier(carrier);

    if (!valid_carrier) {
        dbus_neard_manager_invalid_carrier(call, carrier);
    } else {
        GError* error = NULL;
        DBusNeardManager* self = user_data;
        DBusNeardHandoverAgent* agent = dbus_neard_manager_new_agent(self,
            call, path, valid_carrier, &error);

        if (agent) {
            g_hash_table_replace(self->agents, (gpointer)valid_carrier, agent);
            org_neard_manager_complete_register_handover_agent(iface, call);
        } else {
            g_dbus_method_invocation_return_gerror(call, error);
            g_error_free(error);
        }
    }
    return TRUE;
}

static
gboolean
dbus_neard_manager_unregister_handover_agent(
    OrgNeardManager* iface,
    GDBusMethodInvocation* call,
    const char* path,
    const char* carrier,
    gpointer user_data)
{
    const char* valid_carrier = dbus_neard_manager_valid_carrier(carrier);

    if (!valid_carrier) {
        dbus_neard_manager_invalid_carrier(call, carrier);
    } else {
        DBusNeardManager* self = user_data;
        DBusNeardHandoverAgent* agent = g_hash_table_lookup
            (self->agents, valid_carrier);

        if (agent) {
            const char* sender = g_dbus_method_invocation_get_sender(call);

            if (!g_strcmp0(sender, agent->peer)) {
                GDEBUG("Unregistered %s handover agent %s at %s", carrier,
                    path, sender);
                dbus_neard_manager_drop_agent(agent);
                g_hash_table_remove(self->agents, valid_carrier);
                org_neard_manager_complete_unregister_handover_agent(iface,
                    call);
            } else {
                dbus_neard_manager_invalid_args(call, "Invalid sender");
            }
        } else {
            dbus_neard_manager_invalid_args(call, "No such agent");
        }
    }
    return TRUE;
}

static
void
dbus_neard_manager_free(
    DBusNeardManager* self)
{
    g_hash_table_destroy(self->agents);
    gutil_disconnect_handlers(self->iface, self->neard_calls,
        G_N_ELEMENTS(self->neard_calls));
    g_object_unref(self->iface);
    g_free(self);
}

DBusNeardManager*
dbus_neard_manager_new(
    const DBusNeardOptions* options)
{
    GError* error = NULL;
    DBusNeardManager* self = g_new0(DBusNeardManager, 1);
    GDBusConnection* bus;

    g_atomic_int_set(&self->refcount, 1);
    self->options = options;
    self->agents = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
        dbus_neard_manager_free_agent);
    self->iface = org_neard_manager_skeleton_new();
    self->neard_calls[NEARD_CALL_REGISTER_HANDOVER_AGENT] =
        g_signal_connect(self->iface, "handle-register-handover-agent",
            G_CALLBACK(dbus_neard_manager_register_handover_agent),
            self);
    self->neard_calls[NEARD_CALL_UNREGISTER_HANDOVER_AGENT] =
        g_signal_connect(self->iface, "handle-unregister-handover-agent",
            G_CALLBACK(dbus_neard_manager_unregister_handover_agent),
            self);

    bus = g_bus_get_sync(DBUS_NEARD_BUS_TYPE, NULL, &error);
    if (bus && g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), bus, NEARD_MANAGER_PATH, &error)) {
        GDEBUG("Created Agent Manager object at %s", NEARD_MANAGER_PATH);
    } else {
        dbus_neard_manager_free(self);
        self = NULL;
    }
    if (bus) g_object_unref(bus);
    if (error) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
    return self;
}

DBusNeardManager*
dbus_neard_manager_ref(
    DBusNeardManager* self)
{
    if (G_LIKELY(self)) {
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
dbus_neard_manager_unref(
    DBusNeardManager* self)
{
    if (G_LIKELY(self)) {
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON
                (self->iface));
            dbus_neard_manager_free(self);
        }
    }
}

void
dbus_neard_manager_handle_ndef(
    DBusNeardManager* self,
    NfcNdefRec* ndef)
{
    if (G_LIKELY(self) && self->options->bt_static_handover) {
        DBusNeardHandoverAgent* agent = g_hash_table_lookup(self->agents,
            BLUETOOTH_CARRIER);

        if (agent) {
            GUtilData cdr, eir;

            if (ndef && ndef->next &&
                dbus_neard_manager_parse_Hs(ndef, &cdr) &&
                dbus_neard_manager_parse_bluetooth_oob(ndef->next,&cdr,&eir)) {
                DBusNeardHandoverCall* call;
                GVariantBuilder builder;

                g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
                g_variant_builder_add(&builder, "{sv}", "EIR",
                    g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                        eir.bytes, eir.size, 1));

                call = g_slice_new(DBusNeardHandoverCall);
                call->carrier = agent->carrier; /* Static string */
                g_object_ref(call->iface = self->iface);

                GDEBUG("Calling %s handover agent", agent->carrier);
                org_neard_handover_agent_call_push_oob(agent->proxy,
                    g_variant_builder_end(&builder), NULL,
                    dbus_neard_manager_push_oob_done, call);
            }
        } else {
            GDEBUG("No %s handover agent", BLUETOOTH_CARRIER);
        }
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
