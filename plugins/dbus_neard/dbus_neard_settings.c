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
#include "dbus_neard/org.sailfishos.neard.Settings.h"

#include <nfc_config.h>

#ifdef HAVE_DBUSACCESS
#include <dbusaccess_policy.h>
#include <dbusaccess_peer.h>
#endif

#include <gutil_macros.h>
#include <gutil_misc.h>

#include <sys/stat.h>
#include <errno.h>

enum {
    NEARD_SETTINGS_DBUS_CALL_GET_ALL,
    NEARD_SETTINGS_DBUS_CALL_GET_INTERFACE_VERSION,
    NEARD_SETTINGS_DBUS_CALL_GET_BT_STATIC_HANDOVER,
    NEARD_SETTINGS_DBUS_CALL_SET_BT_STATIC_HANDOVER,
    NEARD_SETTINGS_DBUS_CALL_COUNT
};

typedef struct dbus_neard_settings {
    GDBusConnection* bus;
    OrgSailfishosNeardSettings* iface;
    gulong dbus_call_id[NEARD_SETTINGS_DBUS_CALL_COUNT];
    NfcConfigurable* config;
    gulong change_id;
#ifdef HAVE_DBUSACCESS
    DAPolicy* policy;
#endif
} DBusNeardSettings;

#define NEARD_SETTINGS_DBUS_PATH               "/"
#define NEARD_SETTINGS_DBUS_INTERFACE_VERSION  (1)

#ifdef HAVE_DBUSACCESS

typedef enum dbus_neard_settings_action {
    NEARD_SETTINGS_ACTION_GET_ALL = 1,
    NEARD_SETTINGS_ACTION_GET_INTERFACE_VERSION,
    NEARD_SETTINGS_ACTION_GET_BT_STATIC_HANDOVER,
    NEARD_SETTINGS_ACTION_SET_BT_STATIC_HANDOVER,
} NEARD_SETTINGS_ACTION;

static const DA_ACTION dbus_neard_settings_policy_actions[] = {
    { "GetAll", NEARD_SETTINGS_ACTION_GET_ALL, 0 },
    { "GetInterfaceVersion", NEARD_SETTINGS_ACTION_GET_INTERFACE_VERSION, 0 },
    { "GetBluetoothStaticHandover",
       NEARD_SETTINGS_ACTION_GET_BT_STATIC_HANDOVER, 0 },
    { "SetBluetoothStaticHandover",
       NEARD_SETTINGS_ACTION_SET_BT_STATIC_HANDOVER, 0 },
    { NULL }
};

#define NEARD_SETTINGS_DEFAULT_ACCESS_GET_ALL                 DA_ACCESS_ALLOW
#define NEARD_SETTINGS_DEFAULT_ACCESS_GET_INTERFACE_VERSION   DA_ACCESS_ALLOW
#define NEARD_SETTINGS_DEFAULT_ACCESS_GET_BT_STATIC_HANDOVER  DA_ACCESS_ALLOW
#define NEARD_SETTINGS_DEFAULT_ACCESS_SET_BT_STATIC_HANDOVER  DA_ACCESS_DENY

static const char dbus_neard_settings_default_policy[] =
    DA_POLICY_VERSION ";group(privileged)=allow";

/*
 * Note: if settings_plugin_access_allowed() returns FALSE, it completes
 * the call with error.
 */
static
gboolean
dbus_neard_settings_access_check(
    DBusNeardSettings* self,
    GDBusMethodInvocation* call,
    NEARD_SETTINGS_ACTION action,
    DA_ACCESS def)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    DAPeer* peer = da_peer_get(DBUS_NEARD_DA_BUS, sender);

    /*
     * If we get no peer information from dbus-daemon, it means that
     * the peer is gone so it doesn't really matter what we do in this
     * case - the reply will be dropped anyway.
     */
    if (peer && da_policy_check(self->policy, &peer->cred, action, 0, def) ==
        DA_ACCESS_ALLOW) {
        return TRUE;
    }
    g_dbus_method_invocation_return_error_literal(call, DBUS_NEARD_ERROR,
        DBUS_NEARD_ERROR_ACCESS_DENIED, "D-Bus access denied");
    return FALSE;
}

#define dbus_neard_settings_access_allowed(self,call,X) \
    dbus_neard_settings_access_check(self, call, NEARD_SETTINGS_ACTION_##X, \
        NEARD_SETTINGS_DEFAULT_ACCESS_##X)

#else /* HAVE_DBUSACCESS */

/* No access control (other than the one provided by dbus-daemon) */
#define dbus_neard_settings_access_allowed(self,call,action) (TRUE)

#endif /* HAVE_DBUSACCESS */

static
gboolean
dbus_neard_settings_get_boolean(
    DBusNeardSettings* self,
    const char* key,
    gboolean defval)
{
    gboolean result = defval;
    GVariant* value = nfc_config_get_value(self->config, key);

    if (value) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
            result = g_variant_get_boolean(value);
        }
        g_variant_unref(value);
    }
    return result;
}

static
gboolean
dbus_neard_settings_bt_static_handover(
    DBusNeardSettings* self)
{
    return dbus_neard_settings_get_boolean(self,
        NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER,
        NEARD_SETTINGS_DEFAULT_BT_STATIC_HANDOVER);
}

static
void
dbus_neard_settings_changed(
    NfcConfigurable* config,
    const char* key,
    GVariant* value,
    void* user_data)
{
    DBusNeardSettings* self = user_data;

    if (self->iface && !strcmp(NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER, key) &&
        g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
        org_sailfishos_neard_settings_emit_bluetooth_static_handover_changed
            (self->iface, g_variant_get_boolean(value));
    }
}

static
void
dbus_neard_settings_start_emitting_events(
    DBusNeardSettings* self)
{
    /*
     * We don't emit BluetoothStaticHandoverChanged events until the
     * current values have been requested at least once. Otherwise it
     * unnecessary gets emitted e.g. when the initial value is set at
     * startup.
     */
    if (!self->change_id) {
        self->change_id = nfc_config_add_change_handler(self->config, NULL,
            dbus_neard_settings_changed, self);
    }
}

static
gboolean
dbus_neard_settings_handle_get_all(
    OrgSailfishosNeardSettings* iface,
    GDBusMethodInvocation* call,
    DBusNeardSettings* self)
{
    if (dbus_neard_settings_access_allowed(self, call, GET_ALL)) {
        dbus_neard_settings_start_emitting_events(self);
        org_sailfishos_neard_settings_complete_get_all(iface, call,
            NEARD_SETTINGS_DBUS_INTERFACE_VERSION,
            dbus_neard_settings_bt_static_handover(self));
    }
    return TRUE;
}

static
gboolean
dbus_neard_settings_handle_get_interface_version(
    OrgSailfishosNeardSettings* iface,
    GDBusMethodInvocation* call,
    DBusNeardSettings* self)
{
    if (dbus_neard_settings_access_allowed(self, call, GET_INTERFACE_VERSION)) {
        dbus_neard_settings_start_emitting_events(self);
        org_sailfishos_neard_settings_complete_get_interface_version(iface,
            call, NEARD_SETTINGS_DBUS_INTERFACE_VERSION);
    }
    return TRUE;
}

static
gboolean
dbus_neard_settings_handle_get_bt_static_handover(
    OrgSailfishosNeardSettings* iface,
    GDBusMethodInvocation* call,
    DBusNeardSettings* self)
{
    if (dbus_neard_settings_access_allowed(self, call,
        GET_BT_STATIC_HANDOVER)) {
        dbus_neard_settings_start_emitting_events(self);
        org_sailfishos_neard_settings_complete_get_bluetooth_static_handover
            (iface, call, dbus_neard_settings_bt_static_handover(self));
    }
    return TRUE;
}

static
gboolean
dbus_neard_settings_handle_set_bt_static_handover(
    OrgSailfishosNeardSettings* iface,
    GDBusMethodInvocation* call,
    gboolean enabled,
    DBusNeardSettings* self)
{
    if (dbus_neard_settings_access_allowed(self, call,
        SET_BT_STATIC_HANDOVER)) {
        dbus_neard_settings_start_emitting_events(self);
        nfc_config_set_value(self->config,
            NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER,
            g_variant_new_boolean(enabled));
        org_sailfishos_neard_settings_complete_set_bluetooth_static_handover
            (iface, call);
    }
    return TRUE;
}

static
void
dbus_neard_settings_unexport(
    DBusNeardSettings* self)
{
    if (self->iface) {
        g_dbus_interface_skeleton_unexport
            (G_DBUS_INTERFACE_SKELETON(self->iface));
        gutil_disconnect_handlers(self->iface, self->dbus_call_id,
            G_N_ELEMENTS(self->dbus_call_id));
        g_object_unref(self->iface);
        self->iface = NULL;
    }
}

DBusNeardSettings*
dbus_neard_settings_new(
    NfcConfigurable* config)
{
    GError* error = NULL;
    DBusNeardSettings* self = g_new0(DBusNeardSettings, 1);
    GDBusConnection* bus = g_bus_get_sync(DBUS_NEARD_BUS_TYPE, NULL, &error);

    self->config = config;
    if (bus) {
        OrgSailfishosNeardSettings* iface;

        /* Attach D-Bus call handlers */
        self->iface = iface = org_sailfishos_neard_settings_skeleton_new();
        self->dbus_call_id[NEARD_SETTINGS_DBUS_CALL_GET_ALL] =
            g_signal_connect(iface, "handle-get-all",
                G_CALLBACK(dbus_neard_settings_handle_get_all), self);
        self->dbus_call_id[NEARD_SETTINGS_DBUS_CALL_GET_INTERFACE_VERSION] =
            g_signal_connect(iface, "handle-get-interface-version",
                G_CALLBACK(dbus_neard_settings_handle_get_interface_version),
                self);
        self->dbus_call_id[NEARD_SETTINGS_DBUS_CALL_GET_BT_STATIC_HANDOVER] =
            g_signal_connect(iface, "handle-get-bluetooth-static-handover",
                G_CALLBACK(dbus_neard_settings_handle_get_bt_static_handover),
                self);
        self->dbus_call_id[NEARD_SETTINGS_DBUS_CALL_SET_BT_STATIC_HANDOVER] =
            g_signal_connect(iface, "handle-set-bluetooth-static-handover",
                G_CALLBACK(dbus_neard_settings_handle_set_bt_static_handover),
                self);

        /* Export the D-Bus object */
        if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(iface),
            bus, NEARD_SETTINGS_DBUS_PATH, &error)) {
            dbus_neard_settings_unexport(self);
            g_object_unref(self->bus);
            self->bus = NULL;
        }
    }

    if (error) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }

#ifdef HAVE_DBUSACCESS
    self->policy = da_policy_new_full(dbus_neard_settings_default_policy,
        dbus_neard_settings_policy_actions);
#endif

    return self;
}

void
dbus_neard_settings_free(
    DBusNeardSettings* self)
{
    if (self) {
        nfc_config_remove_handler(self->config, self->change_id);
        dbus_neard_settings_unexport(self);
#ifdef HAVE_DBUSACCESS
        da_policy_unref(self->policy);
#endif
        if (self->bus) {
            g_object_unref(self->bus);
        }
        g_free(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
