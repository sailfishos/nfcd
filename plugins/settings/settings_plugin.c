/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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

#include "plugin.h"
#include "settings/org.sailfishos.nfc.Settings.h"

#include <nfc_manager.h>
#include <nfc_adapter.h>
#include <nfc_plugin_impl.h>

#include <gio/gio.h>

#include <mce_display.h>
#include <mce_log.h>

#include <dbusaccess_policy.h>
#include <dbusaccess_peer.h>

#include <gutil_misc.h>

#include <sys/stat.h>
#include <errno.h>

#define GLOG_MODULE_NAME settings_log
#include <gutil_log.h>

GLOG_MODULE_DEFINE("settings");

enum {
    SETTINGS_DBUS_CALL_GET_ALL,
    SETTINGS_DBUS_CALL_GET_INTERFACE_VERSION,
    SETTINGS_DBUS_CALL_GET_ENABLED,
    SETTINGS_DBUS_CALL_SET_ENABLED,
    SETTINGS_DBUS_CALL_COUNT
};

enum {
    DISPLAY_VALID,
    DISPLAY_STATE,
    MCE_EVENT_COUNT
};

enum {
    NFC_ADAPTER_ADDED,
    NFC_EVENT_COUNT
};

typedef NfcPluginClass SettingsPluginClass;
typedef struct settings_plugin {
    NfcPlugin parent;
    NfcManager* manager;
    MceDisplay* display;
    OrgSailfishosNfcSettings* iface;
    DAPolicy* policy;
    char* storage_dir;
    char* storage_file;
    guint own_name_id;
    gulong dbus_call_id[SETTINGS_DBUS_CALL_COUNT];
    gulong mce_event_id[MCE_EVENT_COUNT];
    gulong nfc_event_id[NFC_EVENT_COUNT];
    gboolean nfc_enabled;
    gboolean nfc_always_on;
    NFC_MODE auto_mode;
} SettingsPlugin;

G_DEFINE_TYPE(SettingsPlugin, settings_plugin, NFC_TYPE_PLUGIN)
#define SETTINGS_TYPE_PLUGIN (settings_plugin_get_type())
#define SETTINGS_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        SETTINGS_TYPE_PLUGIN, SettingsPlugin))

#define SETTINGS_G_BUS                   G_BUS_TYPE_SYSTEM
#define SETTINGS_DA_BUS                  DA_BUS_SYSTEM
#define SETTINGS_ERROR_(error)           SETTINGS_DBUS_SERVICE ".Error." error
#define SETTINGS_DBUS_SERVICE            "org.sailfishos.nfc.settings"
#define SETTINGS_DBUS_PATH               "/"
#define SETTINGS_DBUS_INTERFACE_VERSION  (1)

#define SETTINGS_STORAGE_DIR             "/var/lib/nfcd"
#define SETTINGS_STORAGE_FILE            "settings"
#define SETTINGS_STORAGE_DIR_PERM        0700
#define SETTINGS_STORAGE_FILE_PERM       0600
#define SETTINGS_GROUP                   "Settings"
#define SETTINGS_KEY_ENABLED             "Enabled"
#define SETTINGS_KEY_ALWAYS_ON           "AlwaysOn"

typedef enum settings_error {
    SETTINGS_ERROR_ACCESS_DENIED,        /* AccessDenied */
    SETTINGS_NUM_ERRORS
} SETTINGS_ERROR;

#define SETTINGS_DBUS_ERROR (settings_plugin_error_quark())

typedef enum nfc_settings_action {
    SETTINGS_ACTION_GET_ALL = 1,
    SETTINGS_ACTION_GET_INTERFACE_VERSION,
    SETTINGS_ACTION_GET_ENABLED,
    SETTINGS_ACTION_SET_ENABLED,
} SETTINGS_ACTION;

static const DA_ACTION settings_policy_actions[] = {
    { "GetAll", SETTINGS_ACTION_GET_ALL, 0 },
    { "GetInterfaceVersion", SETTINGS_ACTION_GET_INTERFACE_VERSION, 0 },
    { "GetEnabled", SETTINGS_ACTION_GET_ENABLED, 0 },
    { "SetEnabled", SETTINGS_ACTION_SET_ENABLED, 0 },
    { NULL }
};

#define SETTINGS_DEFAULT_ACCESS_GET_ALL               DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_GET_INTERFACE_VERSION DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_GET_ENABLED           DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_SET_ENABLED           DA_ACCESS_DENY

static const char settings_default_policy[] =
    DA_POLICY_VERSION ";group(privileged)=allow";

#define mce_display_on(display) \
    ((display) && (display)->valid && (display)->state != MCE_DISPLAY_STATE_OFF)

static
GKeyFile*
settings_plugin_load_config(
    SettingsPlugin* self)
{
    GKeyFile* config = g_key_file_new();

    g_key_file_load_from_file(config, self->storage_file, 0, NULL);
    return config;
}

static
void
settings_plugin_save_config(
    SettingsPlugin* self,
    GKeyFile* config)
{
    if (!g_mkdir_with_parents(self->storage_dir, SETTINGS_STORAGE_DIR_PERM)) {
        GError* error = NULL;
        gsize len;
        gchar* data = g_key_file_to_data(config, &len, NULL);

        if (g_file_set_contents(self->storage_file, data, len, &error)) {
            if (chmod(self->storage_file, SETTINGS_STORAGE_FILE_PERM) < 0) {
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
settings_plugin_get_boolean(
    GKeyFile* config,
    const char* key,
    gboolean defval)
{
    GError* error = NULL;
    gboolean val = g_key_file_get_boolean(config, SETTINGS_GROUP, key, &error);

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
settings_plugin_nfc_enabled(
    GKeyFile* config)
{
    return settings_plugin_get_boolean(config, SETTINGS_KEY_ENABLED, TRUE);
}

static
gboolean
settings_plugin_nfc_always_on(
    GKeyFile* config)
{
    return settings_plugin_get_boolean(config, SETTINGS_KEY_ALWAYS_ON, FALSE);
}

static
void
settings_plugin_update_config(
    SettingsPlugin* self)
{
    GKeyFile* config = settings_plugin_load_config(self);
    const gboolean enabled = settings_plugin_nfc_enabled(config);
    const gboolean always_on = settings_plugin_nfc_always_on(config);
    gboolean save = FALSE;

    if (enabled != self->nfc_enabled) {
        save = TRUE;
        g_key_file_set_boolean(config, SETTINGS_GROUP, SETTINGS_KEY_ENABLED,
            self->nfc_enabled);
    }

    if (always_on != self->nfc_always_on) {
        save = TRUE;
        g_key_file_set_boolean(config, SETTINGS_GROUP, SETTINGS_KEY_ALWAYS_ON,
            self->nfc_always_on);
    }

    if (save) {
        settings_plugin_save_config(self, config);
    }

    g_key_file_unref(config);
}

static
GQuark
settings_plugin_error_quark()
{
    static volatile gsize settings_error_quark_value = 0;
    static const GDBusErrorEntry errors[] = {
        { SETTINGS_ERROR_ACCESS_DENIED, SETTINGS_ERROR_("AccessDenied") },
    };

    g_dbus_error_register_error_domain("dbus-nfc-settings-error-quark",
        &settings_error_quark_value, errors, G_N_ELEMENTS(errors));
    return (GQuark)settings_error_quark_value;
}

static
gboolean
settings_plugin_access_allowed(
    SettingsPlugin* self,
    GDBusMethodInvocation* call,
    SETTINGS_ACTION action,
    DA_ACCESS def)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    DAPeer* peer = da_peer_get(SETTINGS_DA_BUS, sender);

    /* If we get no peer information from dbus-daemon, it means that
     * the peer is gone so it doesn't really matter what we do in this
     * case - the reply will be dropped anyway. */
    if (peer && da_policy_check(self->policy, &peer->cred, action, 0, def) ==
        DA_ACCESS_ALLOW) {
        return TRUE;
    }
    g_dbus_method_invocation_return_error_literal(call, SETTINGS_DBUS_ERROR,
        SETTINGS_ERROR_ACCESS_DENIED, "D-Bus access denied");
    return FALSE;
}

static
gboolean
settings_plugin_adapter_enabled(
    SettingsPlugin* self)
{
    return self->nfc_enabled &&
        (self->nfc_always_on || mce_display_on(self->display));
}

static
void
settings_plugin_adapter_added(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(plugin);
    const gboolean enable = settings_plugin_adapter_enabled(self);

    if (enable && self->auto_mode != NFC_MODE_NONE) {
        nfc_adapter_request_power(adapter, TRUE);
        nfc_adapter_request_mode(adapter, self->auto_mode);
    }
}

static
void
settings_plugin_update_nfc_state(
    SettingsPlugin* self)
{
    const gboolean enable = settings_plugin_adapter_enabled(self);

    nfc_manager_set_enabled(self->manager, enable);
    if (enable && self->auto_mode != NFC_MODE_NONE) {
        nfc_manager_request_power(self->manager, TRUE);
        nfc_manager_request_mode(self->manager, self->auto_mode);
    }
}

static
void
settings_plugin_display_state_handler(
    MceDisplay* display,
    gpointer plugin)
{
    settings_plugin_update_nfc_state(SETTINGS_PLUGIN(plugin));
}

static
void
settings_plugin_drop_display(
    SettingsPlugin* self)
{
    if (self->display) {
        mce_display_remove_all_handlers(self->display, self->mce_event_id);
        mce_display_unref(self->display);
        self->display = NULL;
    }
}

static
void
settings_plugin_set_nfc_enabled(
    SettingsPlugin* self,
    gboolean enabled)
{
    if (self->nfc_enabled != enabled) {
        self->nfc_enabled = enabled;
        /* We only need to follow display state when NFC is enabled */
        if (enabled) {
            GINFO("NFC enabled");
            if (!self->nfc_always_on) {
                self->display = mce_display_new();
                self->mce_event_id[DISPLAY_VALID] =
                    mce_display_add_valid_changed_handler(self->display,
                        settings_plugin_display_state_handler, self);
                self->mce_event_id[DISPLAY_STATE] =
                    mce_display_add_state_changed_handler(self->display,
                        settings_plugin_display_state_handler, self);
            }
        } else {
            GINFO("NFC disabled");
            settings_plugin_drop_display(self);
        }
        org_sailfishos_nfc_settings_emit_enabled_changed(self->iface, enabled);
        settings_plugin_update_nfc_state(self);
        settings_plugin_update_config(self);
    }
}

static
gboolean
settings_plugin_dbus_handle_get_all(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(user_data);

    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_GET_ALL, SETTINGS_DEFAULT_ACCESS_GET_ALL)) {
        org_sailfishos_nfc_settings_complete_get_all(iface, call,
            SETTINGS_DBUS_INTERFACE_VERSION, self->nfc_enabled);
    }
    return TRUE;
}

static
gboolean
settings_plugin_dbus_handle_get_interface_version(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(user_data);

    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_GET_INTERFACE_VERSION,
        SETTINGS_DEFAULT_ACCESS_GET_INTERFACE_VERSION)) {
        org_sailfishos_nfc_settings_complete_get_interface_version(iface, call,
            SETTINGS_DBUS_INTERFACE_VERSION);
    }
    return TRUE;
}

static
gboolean
settings_plugin_dbus_handle_get_enabled(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(user_data);

    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_GET_ENABLED, SETTINGS_DEFAULT_ACCESS_GET_ENABLED)) {
        org_sailfishos_nfc_settings_complete_get_enabled(iface, call,
            self->nfc_enabled);
    }
    return TRUE;
}

static
gboolean
settings_plugin_dbus_handle_set_enabled(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    gboolean enabled,
    gpointer user_data)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(user_data);

    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_SET_ENABLED, SETTINGS_DEFAULT_ACCESS_SET_ENABLED)) {
        settings_plugin_set_nfc_enabled(self, enabled);
        org_sailfishos_nfc_settings_complete_set_enabled(iface, call);
    }
    return TRUE;
}

static
void
settings_plugin_dbus_connected(
    GDBusConnection* connection,
    const gchar* name,
    gpointer plugin)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(plugin);
    GError* error = NULL;

    GDEBUG("Acquired service name '%s'", name);
    if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, SETTINGS_DBUS_PATH, &error)) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
}

static
void
settings_plugin_dbus_name_acquired(
    GDBusConnection* connection,
    const gchar* name,
    gpointer plugin)
{
    GDEBUG("Acquired service name '%s'", name);
}

static
void
settings_plugin_dbus_name_lost(
    GDBusConnection* connection,
    const gchar* name,
    gpointer plugin)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(plugin);

    GERR("'%s' service already running or access denied", name);
    /* Tell daemon to exit */
    nfc_manager_stop(self->manager, NFC_MANAGER_PLUGIN_ERROR);
}

static
gboolean
settings_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(plugin);
    GKeyFile* config;

    GVERBOSE("Starting");
    self->manager = nfc_manager_ref(manager);
    self->nfc_event_id[NFC_ADAPTER_ADDED] =
        nfc_manager_add_adapter_added_handler(manager,
            settings_plugin_adapter_added, self);

    self->iface = org_sailfishos_nfc_settings_skeleton_new();
    self->dbus_call_id[SETTINGS_DBUS_CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(settings_plugin_dbus_handle_get_all), self);
    self->dbus_call_id[SETTINGS_DBUS_CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(settings_plugin_dbus_handle_get_interface_version), self);
    self->dbus_call_id[SETTINGS_DBUS_CALL_GET_ENABLED] =
        g_signal_connect(self->iface, "handle-get-enabled",
        G_CALLBACK(settings_plugin_dbus_handle_get_enabled), self);
    self->dbus_call_id[SETTINGS_DBUS_CALL_SET_ENABLED] =
        g_signal_connect(self->iface, "handle-set-enabled",
        G_CALLBACK(settings_plugin_dbus_handle_set_enabled), self);

    self->own_name_id = g_bus_own_name(SETTINGS_G_BUS, SETTINGS_DBUS_SERVICE,
        G_BUS_NAME_OWNER_FLAGS_REPLACE, settings_plugin_dbus_connected,
        settings_plugin_dbus_name_acquired, settings_plugin_dbus_name_lost,
        self, NULL);

    config = settings_plugin_load_config(self);
    self->nfc_always_on = settings_plugin_nfc_always_on(config);
    settings_plugin_set_nfc_enabled(self, settings_plugin_nfc_enabled(config));
    settings_plugin_update_nfc_state(self);
    g_key_file_unref(config);
    return TRUE;
}

static
void
settings_plugin_stop(
    NfcPlugin* plugin)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(plugin);

    GVERBOSE("Stopping");
    settings_plugin_drop_display(self);
    if (self->own_name_id) {
        g_bus_unown_name(self->own_name_id);
        self->own_name_id = 0;
    }
    if (self->iface) {
        g_dbus_interface_skeleton_unexport
            (G_DBUS_INTERFACE_SKELETON(self->iface));
        gutil_disconnect_handlers(self->iface, self->dbus_call_id,
            G_N_ELEMENTS(self->dbus_call_id));
        g_object_unref(self->iface);
        self->iface = NULL;
    }
    if (self->manager) {
        nfc_manager_remove_all_handlers(self->manager, self->nfc_event_id);
        nfc_manager_unref(self->manager);
        self->manager = NULL;
    }
}

static
void
settings_plugin_init(
    SettingsPlugin* self)
{
    self->storage_dir = g_strdup(SETTINGS_STORAGE_DIR);
    self->storage_file = g_build_filename(self->storage_dir,
        SETTINGS_STORAGE_FILE, NULL);
    self->policy = da_policy_new_full(settings_default_policy,
        settings_policy_actions);
    self->auto_mode = (NFC_MODE_P2P_INITIATOR | NFC_MODE_READER_WRITER |
        NFC_MODE_P2P_TARGET | NFC_MODE_CARD_EMILATION);
}

static
void
settings_plugin_finalize(
    GObject* plugin)
{
    SettingsPlugin* self = SETTINGS_PLUGIN(plugin);

    da_policy_unref(self->policy);
    g_free(self->storage_dir);
    g_free(self->storage_file);
    G_OBJECT_CLASS(settings_plugin_parent_class)->finalize(plugin);
}

static
void
settings_plugin_class_init(
    NfcPluginClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = settings_plugin_finalize;
    klass->start = settings_plugin_start;
    klass->stop = settings_plugin_stop;
}

static
NfcPlugin*
settings_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(SETTINGS_TYPE_PLUGIN, NULL);
}

static GLogModule* const settings_plugin_logs[] = {
    &GLOG_MODULE_NAME,
    &MCE_LOG_MODULE,
    NULL
};

NFC_PLUGIN_DEFINE2(settings, "Settings storage and D-Bus interface",
    settings_plugin_create, settings_plugin_logs, 0)

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
