/*
 * Copyright (C) 2021 Jolla Ltd.
 * Copyright (C) 2021 Slava Monich <slava.monich@jolla.com>
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

typedef struct dbus_neard_settings_priv {
    DBusNeardSettings pub;
    char* storage_dir;
    char* storage_file;
    GDBusConnection* bus;
    OrgSailfishosNeardSettings* iface;
    gulong dbus_call_id[NEARD_SETTINGS_DBUS_CALL_COUNT];
#ifdef HAVE_DBUSACCESS
    DAPolicy* policy;
#endif
} DBusNeardSettingsPriv;

#define NEARD_SETTINGS_DBUS_PATH               "/"
#define NEARD_SETTINGS_DBUS_INTERFACE_VERSION  (1)

#define NEARD_SETTINGS_DIR                     "/var/lib/nfcd"
#define NEARD_SETTINGS_FILE                    "neard"
#define NEARD_SETTINGS_DIR_PERM                0700
#define NEARD_SETTINGS_FILE_PERM               0600
#define NEARD_SETTINGS_GROUP                   "Settings"
#define NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER  "BluetoothStaticHandover"

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
    DBusNeardSettingsPriv* self,
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
GKeyFile*
dbus_neard_settings_load_config(
    DBusNeardSettingsPriv* self)
{
    GKeyFile* config = g_key_file_new();

    g_key_file_load_from_file(config, self->storage_file, 0, NULL);
    return config;
}

static
void
dbus_neard_settings_save_config(
    DBusNeardSettingsPriv* self,
    GKeyFile* config)
{
    if (!g_mkdir_with_parents(self->storage_dir, NEARD_SETTINGS_DIR_PERM)) {
        GError* error = NULL;
        gsize len;
        gchar* data = g_key_file_to_data(config, &len, NULL);

        if (g_file_set_contents(self->storage_file, data, len, &error)) {
            if (chmod(self->storage_file, NEARD_SETTINGS_FILE_PERM) < 0) {
                GWARN("Failed to set %s permissions: %s", self->storage_file,
                    strerror(errno));
            } else {
                GDEBUG("Wrote %s", self->storage_file);
            }
        } else {
            GWARN("%s", GERRMSG(error));
            g_error_free(error);
        }
        g_free(data);
    } else {
        GWARN("Failed to create directory %s", self->storage_dir);
    }
}

static
gboolean
dbus_neard_settings_get_boolean(
    GKeyFile* config,
    const char* key,
    gboolean defval)
{
    GError* error = NULL;
    const gboolean val = g_key_file_get_boolean(config, NEARD_SETTINGS_GROUP,
        key, &error);

    if (error) {
        g_error_free(error);
        /* Default */
        return defval;
    } else {
        return val;
    }
}

static
gboolean
dbus_neard_settings_bt_static_handover(
    GKeyFile* config)
{
    return dbus_neard_settings_get_boolean(config,
        NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER, FALSE);
}

static
void
dbus_neard_settings_update_config(
    DBusNeardSettingsPriv* self)
{
    DBusNeardSettings* pub = &self->pub;
    GKeyFile* config = dbus_neard_settings_load_config(self);
    gboolean bt_handover = dbus_neard_settings_bt_static_handover(config);
    gboolean save = FALSE;

    if (bt_handover != pub->bt_static_handover) {
        save = TRUE;
        g_key_file_set_boolean(config, NEARD_SETTINGS_GROUP,
            NEARD_SETTINGS_KEY_BT_STATIC_HANDOVER, pub->bt_static_handover);
    }

    if (save) {
        dbus_neard_settings_save_config(self, config);
    }

    g_key_file_unref(config);
}

static
void
dbus_neard_settings_set_bt_static_handover(
    DBusNeardSettingsPriv* self,
    gboolean enabled)
{
    DBusNeardSettings* pub = &self->pub;

    if (pub->bt_static_handover != enabled) {
        pub->bt_static_handover = enabled;
        GINFO("Bluetooth handover %s", enabled ? "enabled" : "disabled");
        if (self->iface) {
            org_sailfishos_neard_settings_emit_bluetooth_static_handover_changed
                (self->iface, enabled);
        }
        dbus_neard_settings_update_config(self);
    }
}

static
gboolean
dbus_neard_settings_handle_get_all(
    OrgSailfishosNeardSettings* iface,
    GDBusMethodInvocation* call,
    DBusNeardSettingsPriv* self)
{
    if (dbus_neard_settings_access_allowed(self, call, GET_ALL)) {
        org_sailfishos_neard_settings_complete_get_all(iface, call,
            NEARD_SETTINGS_DBUS_INTERFACE_VERSION,
            self->pub.bt_static_handover);
    }
    return TRUE;
}

static
gboolean
dbus_neard_settings_handle_get_interface_version(
    OrgSailfishosNeardSettings* iface,
    GDBusMethodInvocation* call,
    DBusNeardSettingsPriv* self)
{
    if (dbus_neard_settings_access_allowed(self, call, GET_INTERFACE_VERSION)) {
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
    DBusNeardSettingsPriv* self)
{
    if (dbus_neard_settings_access_allowed(self, call,
        GET_BT_STATIC_HANDOVER)) {
        org_sailfishos_neard_settings_complete_get_bluetooth_static_handover
            (iface, call, self->pub.bt_static_handover);
    }
    return TRUE;
}

static
gboolean
dbus_neard_settings_handle_set_bt_static_handover(
    OrgSailfishosNeardSettings* iface,
    GDBusMethodInvocation* call,
    gboolean enabled,
    DBusNeardSettingsPriv* self)
{
    if (dbus_neard_settings_access_allowed(self, call,
        SET_BT_STATIC_HANDOVER)) {
        dbus_neard_settings_set_bt_static_handover(self, enabled);
        org_sailfishos_neard_settings_complete_set_bluetooth_static_handover
            (iface, call);
    }
    return TRUE;
}

static
void
dbus_neard_settings_unexport(
    DBusNeardSettingsPriv* self)
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
    void)
{
    GError* error = NULL;
    DBusNeardSettingsPriv* self = g_new0(DBusNeardSettingsPriv, 1);
    DBusNeardSettings* pub = &self->pub;
    GDBusConnection* bus = g_bus_get_sync(DBUS_NEARD_BUS_TYPE, NULL, &error);
    GKeyFile* config;

    self->storage_dir = g_strdup(NEARD_SETTINGS_DIR);
    self->storage_file = g_build_filename(self->storage_dir,
        NEARD_SETTINGS_FILE, NULL);

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

    /* Load current value(s) from the config file */
    config = dbus_neard_settings_load_config(self);
    pub->bt_static_handover = dbus_neard_settings_bt_static_handover(config);
    g_key_file_unref(config);

    return pub;
}

void
dbus_neard_settings_free(
    DBusNeardSettings* pub)
{
    if (pub) {
        DBusNeardSettingsPriv* self = G_CAST(pub, DBusNeardSettingsPriv, pub);

        dbus_neard_settings_unexport(self);
#ifdef HAVE_DBUSACCESS
        da_policy_unref(self->policy);
#endif
        if (self->bus) {
            g_object_unref(self->bus);
        }
        g_free(self->storage_dir);
        g_free(self->storage_file);
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
