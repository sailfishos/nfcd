/*
 * Copyright (C) 2018-2022 Jolla Ltd.
 * Copyright (C) 2018-2022 Slava Monich <slava.monich@jolla.com>
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

#include "settings_plugin.h"
#include "settings/org.sailfishos.nfc.Settings.h"
#include "plugin.h"

#include <nfc_config.h>
#include <nfc_manager.h>

#ifdef HAVE_DBUSACCESS
#include <dbusaccess_policy.h>
#include <dbusaccess_peer.h>
#endif

#include <gutil_macros.h>
#include <gutil_misc.h>
#include <gutil_strv.h>

#include <sys/stat.h>
#include <errno.h>

#define GLOG_MODULE_NAME settings_log
#include <gutil_log.h>

GLOG_MODULE_DEFINE("settings");

/*
 * NfcConfigurable interface is utilized to configure plugins.
 * Plugin configuration is stored in /var/lib/nfcd/settings file
 * alongside with the global configuration, plugin names being
 * used as section names. Configuration values are converted to
 * strings with g_variant_print() and back to GVariants with
 * g_variant_parse().
 *
 * Non-parseable values are interpreted as strings.
 *
 * Read-only defaults are loaded from /etc/nfcd/defaults.conf file
 * and whatever else is found in /etc/nfcd/defaults.d directory.
 * Those can be used for providing device-specific initial values.
 */

enum {
    SETTINGS_DBUS_CALL_GET_ALL,
    SETTINGS_DBUS_CALL_GET_INTERFACE_VERSION,
    SETTINGS_DBUS_CALL_GET_ENABLED,
    SETTINGS_DBUS_CALL_SET_ENABLED,
    SETTINGS_DBUS_CALL_GET_ALL2,
    SETTINGS_DBUS_CALL_GET_ALL_PLUGIN_SETTINGS,
    SETTINGS_DBUS_CALL_GET_PLUGIN_SETTINGS,
    SETTINGS_DBUS_CALL_GET_PLUGIN_VALUE,
    SETTINGS_DBUS_CALL_SET_PLUGIN_VALUE,
    SETTINGS_DBUS_CALL_COUNT
};

typedef struct settings_plugin {
    NfcPlugin parent;
    NfcManager* manager;
    GHashTable* plugins; /* char* => SettingsPluginConfig */
    GStrV* order; /* Sorted keys in plugins table */
    OrgSailfishosNfcSettings* iface;
#ifdef HAVE_DBUSACCESS
    DAPolicy* policy;
#endif
    GKeyFile* defaults;
    char* storage_file;
    guint own_name_id;
    gulong dbus_call_id[SETTINGS_DBUS_CALL_COUNT];
    gboolean nfc_enabled;
} SettingsPlugin;

typedef struct settings_plugin_config {
    const char* name;
    SettingsPlugin* settings;
    NfcConfigurable* config;
    gulong change_id;
} SettingsPluginConfig;

G_DEFINE_TYPE(SettingsPlugin, settings_plugin, NFC_TYPE_PLUGIN)
#define THIS_TYPE SETTINGS_PLUGIN_TYPE
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, THIS_TYPE, SettingsPlugin)
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, THIS_TYPE, \
    SettingsPluginClass)

#define SETTINGS_ERROR_(error)           SETTINGS_DBUS_SERVICE ".Error." error
#define SETTINGS_DBUS_SERVICE            "org.sailfishos.nfc.settings"
#define SETTINGS_DBUS_PATH               "/"
#define SETTINGS_DBUS_INTERFACE_VERSION  (2)

#define SETTINGS_CONFIG_DIR              "/etc/nfcd"
#define SETTINGS_CONFIG_DEFAULTS_FILE    "defaults.conf"
#define SETTINGS_CONFIG_DEFAULTS_DIR     "defaults.d"

#define SETTINGS_STORAGE_DIR             "/var/lib/nfcd"
#define SETTINGS_STORAGE_FILE            "settings"
#define SETTINGS_STORAGE_DIR_PERM        0700
#define SETTINGS_STORAGE_FILE_PERM       0600
#define SETTINGS_GROUP                   "Settings"
#define SETTINGS_KEY_ENABLED             "Enabled"
#define SETTINGS_KEY_ALWAYS_ON           "AlwaysOn"

typedef enum settings_error {
    SETTINGS_ERROR_ACCESS_DENIED,        /* AccessDenied */
    SETTINGS_ERROR_FAILED,               /* Failed */
    SETTINGS_ERROR_UNKNOWN_PLUGIN,       /* UnknownPlugin */
    SETTINGS_ERROR_UNKNOWN_KEY,          /* UnknownKey */
    SETTINGS_NUM_ERRORS
} SETTINGS_ERROR;

#define SETTINGS_DBUS_ERROR (settings_plugin_error_quark())

#ifdef HAVE_DBUSACCESS

typedef enum nfc_settings_action {
    SETTINGS_ACTION_GET_ALL = 1,
    SETTINGS_ACTION_GET_INTERFACE_VERSION,
    SETTINGS_ACTION_GET_ENABLED,
    SETTINGS_ACTION_SET_ENABLED,
    SETTINGS_ACTION_GET_ALL2,
    SETTINGS_ACTION_GET_ALL_PLUGIN_SETTINGS,
    SETTINGS_ACTION_GET_PLUGIN_SETTINGS,
    SETTINGS_ACTION_GET_PLUGIN_VALUE,
    SETTINGS_ACTION_SET_PLUGIN_VALUE
} SETTINGS_ACTION;

static const DA_ACTION settings_policy_actions[] = {
    { "GetAll", SETTINGS_ACTION_GET_ALL, 0 },
    { "GetInterfaceVersion", SETTINGS_ACTION_GET_INTERFACE_VERSION, 0 },
    { "GetEnabled", SETTINGS_ACTION_GET_ENABLED, 0 },
    { "SetEnabled", SETTINGS_ACTION_SET_ENABLED, 0 },
    { "GetAll2", SETTINGS_ACTION_GET_ALL2, 0 },
    { "GetAllPluginSettings", SETTINGS_ACTION_GET_ALL_PLUGIN_SETTINGS, 0 },
    { "GetPluginSettings", SETTINGS_ACTION_GET_PLUGIN_SETTINGS, 0 },
    { "GetPluginValue", SETTINGS_ACTION_GET_PLUGIN_VALUE, 0 },
    { "SetPluginValue", SETTINGS_ACTION_SET_PLUGIN_VALUE, 1 },
    { NULL }
};

#define SETTINGS_DEFAULT_ACCESS_GET_ALL                 DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_GET_INTERFACE_VERSION   DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_GET_ENABLED             DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_SET_ENABLED             DA_ACCESS_DENY
#define SETTINGS_DEFAULT_ACCESS_GET_ALL2                DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_GET_ALL_PLUGIN_SETTINGS DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_GET_PLUGIN_SETTINGS     DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_GET_PLUGIN_VALUE        DA_ACCESS_ALLOW
#define SETTINGS_DEFAULT_ACCESS_SET_PLUGIN_VALUE        DA_ACCESS_DENY

static const char settings_default_policy[] =
    DA_POLICY_VERSION ";group(privileged)=allow";

#endif /* HAVE_DBUSACCESS */

static
void
settings_plugin_merge_config_group(
    GKeyFile* dest,
    GKeyFile* src,
    const char* group)
{
    gsize i, n = 0;
    char** keys = g_key_file_get_keys(src, group, &n, NULL);

    for (i = 0; i < n; i++) {
        const char* key = keys[i];
        char* value = g_key_file_get_value(src, group, key, NULL);

        g_key_file_set_value(dest, group, key, value);
        g_free(value);
    }

    g_strfreev(keys);
}

static
void
settings_plugin_merge_defaults(
    SettingsPlugin* self,
    GKeyFile* src)
{
    gsize i, n = 0;
    char** groups = g_key_file_get_groups(src, &n);

    for (i = 0; i < n; i++) {
        const char* group = groups[i];

        if (!strcmp(group, SETTINGS_GROUP) ||
            g_hash_table_contains(self->plugins, group)) {
            settings_plugin_merge_config_group(self->defaults, src, group);
        } else {
            GDEBUG("Skipping defaults group [%s]", group);
        }
    }
    g_strfreev(groups);
}

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
    const char* storage_dir = GET_THIS_CLASS(self)->storage_dir;

    if (!g_mkdir_with_parents(storage_dir, SETTINGS_STORAGE_DIR_PERM)) {
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
        GWARN("Failed to create directory %s", storage_dir);
    }
}

static
gboolean
settings_plugin_get_boolean(
    SettingsPlugin* self,
    GKeyFile* config,
    const char* key,
    gboolean defval)
{
    GError* error = NULL;
    gboolean val = g_key_file_get_boolean(config, SETTINGS_GROUP, key, &error);

    if (error) {
        g_clear_error(&error);
        val = g_key_file_get_boolean(self->defaults, SETTINGS_GROUP, key,
            &error);
        if (error) {
            g_error_free(error);
            /* Default */
            return defval;
        }
    }
    return val;
}

static
gboolean
settings_plugin_nfc_enabled(
    SettingsPlugin* self,
    GKeyFile* config)
{
    return settings_plugin_get_boolean(self, config,
        SETTINGS_KEY_ENABLED, TRUE);
}

static
gboolean
settings_plugin_nfc_always_on(
    SettingsPlugin* self,
    GKeyFile* config)
{
    return settings_plugin_get_boolean(self, config,
        SETTINGS_KEY_ALWAYS_ON, FALSE);
}

static
gboolean
settings_plugin_update_boolean(
    SettingsPlugin* self,
    GKeyFile* config,
    const char* key,
    gboolean value)
{
    const char* group = SETTINGS_GROUP;
    GError* error = NULL;
    gboolean default_value = g_key_file_get_boolean(self->defaults, group,
        key, &error);
    gboolean have_default = !error;
    gboolean config_value;

    g_clear_error(&error);
    config_value = g_key_file_get_boolean(config, group, key, &error);
    if (error) {
        g_error_free(error);
        /* Save it only if it's not the default value */
        if (!have_default || value != default_value) {
            g_key_file_set_boolean(config, group, key, value);
            return TRUE;
        }
    } else if (have_default && value == default_value) {
        /* Remove the default value from the config */
        g_key_file_remove_key(config, group, key, NULL);
        return TRUE;
    } else if (config_value != value) {
        /* Not a default and doesn't match the config - save it */
        g_key_file_set_boolean(config, group, key, value);
        return TRUE;
    }
    return FALSE;
}

static
gboolean
settings_plugin_update_settings(
    SettingsPlugin* self,
    GKeyFile* config)
{
    GStrV* plugins = self->order;
    gboolean save = FALSE;

    if (settings_plugin_update_boolean(self, config, SETTINGS_KEY_ENABLED,
        self->nfc_enabled)) {
        save = TRUE;
    }

    /* Check plugin configs */
    if (plugins) {
        while (*plugins) {
            const char* group = *plugins++;
            const SettingsPluginConfig* pc = g_hash_table_lookup(self->plugins,
                group);
            const char* const* keys = nfc_config_get_keys(pc->config);

            if (keys) {
                const char* const* ptr = keys;

                while (*ptr) {
                    const char* key = *ptr++;
                    GVariant* value = nfc_config_get_value(pc->config, key);
                    char* str = g_key_file_get_string(config, group, key, NULL);
                    char* defval = g_key_file_get_string(self->defaults,
                        group, key, NULL);
                    char* sval = NULL;

                    if (value) {
                        sval = g_variant_print(value, FALSE);
                        g_variant_unref(value);
                    }

                    if (sval && defval && !strcmp(sval, defval)) {
                        /* Don't store the default value */
                        GVERBOSE_("[%s] %s %s => (default)", group,
                            key, sval);
                        if (g_key_file_remove_key(config, group, key, NULL)) {
                            save = TRUE;
                        }
                    } else if (g_strcmp0(sval, str)) {
                        GVERBOSE_("[%s] %s %s => %s", group, key, str, sval);
                        if (sval) {
                            g_key_file_set_string(config, group, key, sval);
                            save = TRUE;
                        } else if (g_key_file_remove_key(config, group, key,
                            NULL)) {
                            save = TRUE;
                        }
                    }

                    g_free(defval);
                    g_free(sval);
                    g_free(str);
                }
            }
        }
    }

    return save;
}

static
void
settings_plugin_update_config(
    SettingsPlugin* self)
{
    GKeyFile* config = settings_plugin_load_config(self);

    if (settings_plugin_update_settings(self, config)) {
        settings_plugin_save_config(self, config);
    }

    g_key_file_unref(config);
}

static
gboolean
settings_plugin_is_valid_key(
    NfcConfigurable* config,
    const char* key)
{
    return gutil_strv_contains((const GStrV*)nfc_config_get_keys(config), key);
}

static
GVariant* /* floating */
settings_plugin_config_variant(
    NfcConfigurable* config)
{
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    const char* const* ptr = nfc_config_get_keys(config);

    if (ptr) {
        while (*ptr) {
            const char* key = *ptr++;
            GVariant* value = nfc_config_get_value(config, key);

            if (value) {
                g_variant_builder_add(&b, "{sv}", key, value);
                g_variant_unref(value);
            }
        }
    }
    return g_variant_builder_end(&b);
}

static
GVariant* /* floating */
settings_plugin_get_all_plugin_settings(
    SettingsPlugin* self)
{
    GVariantBuilder b;
    char* const* ptr = self->order;

    g_variant_builder_init(&b, G_VARIANT_TYPE("a(sa{sv})"));
    if (ptr) {
        while (*ptr) {
            const char* name = *ptr++;
            const SettingsPluginConfig* pc = g_hash_table_lookup(self->plugins,
                name);

            g_variant_builder_add(&b, "(s@a{sv})", name,
                settings_plugin_config_variant(pc->config));
        }
    }

    return g_variant_builder_end(&b);
}

static
GQuark
settings_plugin_error_quark()
{
    static volatile gsize settings_error_quark_value = 0;
    static const GDBusErrorEntry errors[] = {
        { SETTINGS_ERROR_ACCESS_DENIED, SETTINGS_ERROR_("AccessDenied") },
        { SETTINGS_ERROR_FAILED, SETTINGS_ERROR_("Failed") },
        { SETTINGS_ERROR_UNKNOWN_PLUGIN, SETTINGS_ERROR_("UnknownPlugin") },
        { SETTINGS_ERROR_UNKNOWN_KEY, SETTINGS_ERROR_("UnknownKey") }
    };

    g_dbus_error_register_error_domain("dbus-nfc-settings-error-quark",
        &settings_error_quark_value, errors, G_N_ELEMENTS(errors));
    return (GQuark)settings_error_quark_value;
}

#ifdef HAVE_DBUSACCESS

/*
 * Note: if settings_plugin_access_allowed() returns FALSE, it completes
 * the call with AccessDenied error.
 */
static
gboolean
settings_plugin_access_allowed1(
    SettingsPlugin* self,
    GDBusMethodInvocation* call,
    SETTINGS_ACTION action,
    const char* arg,
    DA_ACCESS def)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    DAPeer* peer = da_peer_get(SETTINGS_DA_BUS, sender);

    /* If we get no peer information from dbus-daemon, it means that
     * the peer is gone so it doesn't really matter what we do in this
     * case - the reply will be dropped anyway. */
    if (peer && da_policy_check(self->policy, &peer->cred, action, arg, def) ==
        DA_ACCESS_ALLOW) {
        return TRUE;
    }
    g_dbus_method_invocation_return_error_literal(call, SETTINGS_DBUS_ERROR,
        SETTINGS_ERROR_ACCESS_DENIED, "D-Bus access denied");
    return FALSE;
}

#else

/* No access control (other than the one provided by dbus-daemon) */
#define settings_plugin_access_allowed1(self,call,action,arg,def) (TRUE)

#endif /* HAVE_DBUSACCESS */

#define settings_plugin_access_allowed(self,call,action,def) \
    settings_plugin_access_allowed1(self, call, action, NULL, def)

static
void
settings_plugin_set_nfc_enabled(
    SettingsPlugin* self,
    gboolean enabled)
{
    if (self->nfc_enabled != enabled) {
        self->nfc_enabled = enabled;
        GINFO("NFC %s", enabled ? "enabled" : "disabled");
        org_sailfishos_nfc_settings_emit_enabled_changed(self->iface, enabled);
        nfc_manager_set_enabled(self->manager, enabled);
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
    SettingsPlugin* self = THIS(user_data);

    /*
     * N.B. If settings_plugin_access_allowed() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
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
    /*
     * N.B. If settings_plugin_access_allowed() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
    if (settings_plugin_access_allowed(THIS(user_data), call,
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
    SettingsPlugin* self = THIS(user_data);

    /*
     * N.B. If settings_plugin_access_allowed() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
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
    SettingsPlugin* self = THIS(user_data);

    /*
     * N.B. If settings_plugin_access_allowed() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_SET_ENABLED, SETTINGS_DEFAULT_ACCESS_SET_ENABLED)) {
        settings_plugin_set_nfc_enabled(self, enabled);
        org_sailfishos_nfc_settings_complete_set_enabled(iface, call);
    }
    return TRUE;
}

static
gboolean
settings_plugin_dbus_handle_get_all2(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    SettingsPlugin* self = THIS(user_data);

    /*
     * N.B. If settings_plugin_access_allowed() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_GET_ALL2, SETTINGS_DEFAULT_ACCESS_GET_ALL2)) {
        org_sailfishos_nfc_settings_complete_get_all2(iface, call,
            SETTINGS_DBUS_INTERFACE_VERSION, self->nfc_enabled,
            settings_plugin_get_all_plugin_settings(self));
    }
    return TRUE;
}

static
gboolean
settings_plugin_dbus_handle_get_all_plugin_settings(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    SettingsPlugin* self = THIS(user_data);

    /*
     * N.B. If settings_plugin_access_allowed() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_GET_ALL_PLUGIN_SETTINGS,
        SETTINGS_DEFAULT_ACCESS_GET_ALL_PLUGIN_SETTINGS)) {
        org_sailfishos_nfc_settings_complete_get_all_plugin_settings(iface,
            call, settings_plugin_get_all_plugin_settings(self));
    }
    return TRUE;
}

static
gboolean
settings_plugin_dbus_handle_get_plugin_settings(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    const char* plugin,
    gpointer user_data)
{
    SettingsPlugin* self = THIS(user_data);

    /*
     * N.B. If settings_plugin_access_allowed() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_GET_PLUGIN_SETTINGS,
        SETTINGS_DEFAULT_ACCESS_GET_PLUGIN_SETTINGS)) {
        const SettingsPluginConfig* pc = g_hash_table_lookup(self->plugins,
            plugin);

        if (pc) {
            org_sailfishos_nfc_settings_complete_get_plugin_settings(iface,
              call, settings_plugin_config_variant(pc->config));
        } else {
            g_dbus_method_invocation_return_error_literal(call,
                SETTINGS_DBUS_ERROR, SETTINGS_ERROR_UNKNOWN_PLUGIN, plugin);
        }
    }
    return TRUE;
}

static
gboolean
settings_plugin_dbus_handle_get_plugin_value(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    const char* plugin,
    const char* key,
    gpointer user_data)
{
    SettingsPlugin* self = THIS(user_data);

    /*
     * N.B. If settings_plugin_access_allowed() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
    if (settings_plugin_access_allowed(self, call,
        SETTINGS_ACTION_GET_PLUGIN_VALUE,
        SETTINGS_DEFAULT_ACCESS_GET_PLUGIN_VALUE)) {
        const SettingsPluginConfig* pc = g_hash_table_lookup(self->plugins,
            plugin);

        if (pc) {
            GVariant* value = nfc_config_get_value(pc->config, key);

            if (value) {
                org_sailfishos_nfc_settings_complete_get_plugin_value(iface,
                    call, g_variant_new_variant(value));
                g_variant_unref(value);
            } else {
                /* What else could be wrong? */
                g_dbus_method_invocation_return_error_literal(call,
                    SETTINGS_DBUS_ERROR, SETTINGS_ERROR_UNKNOWN_KEY, key);
            }
        } else {
            g_dbus_method_invocation_return_error_literal(call,
                SETTINGS_DBUS_ERROR, SETTINGS_ERROR_UNKNOWN_PLUGIN, plugin);
        }
    }
    return TRUE;
}

static
gboolean
settings_plugin_dbus_handle_set_plugin_value(
    OrgSailfishosNfcSettings* iface,
    GDBusMethodInvocation* call,
    const char* plugin,
    const char* key,
    GVariant* var,
    gpointer user_data)
{
    SettingsPlugin* self = THIS(user_data);

    /*
     * N.B. If settings_plugin_access_allowed1() denies the access,
     * it completes the call with AccessDenied error and returns FALSE.
     */
    if (settings_plugin_access_allowed1(self, call,
        SETTINGS_ACTION_SET_PLUGIN_VALUE, plugin,
        SETTINGS_DEFAULT_ACCESS_SET_PLUGIN_VALUE)) {
        const SettingsPluginConfig* pc = g_hash_table_lookup(self->plugins,
            plugin);

        if (pc) {
            GVariant* value =
                g_variant_is_of_type(var, G_VARIANT_TYPE_VARIANT) ?
                    g_variant_get_variant(var) : g_variant_ref_sink(var);

            if (nfc_config_set_value(pc->config, key, value)) {
                org_sailfishos_nfc_settings_complete_set_plugin_value(iface,
                    call);
            } else if (!settings_plugin_is_valid_key(pc->config, key)) {
                g_dbus_method_invocation_return_error_literal(call,
                    SETTINGS_DBUS_ERROR, SETTINGS_ERROR_UNKNOWN_KEY, key);
            } else {
                g_dbus_method_invocation_return_error_literal(call,
                    SETTINGS_DBUS_ERROR, SETTINGS_ERROR_FAILED, key);
            }
            g_variant_unref(value);
        } else {
            g_dbus_method_invocation_return_error_literal(call,
                SETTINGS_DBUS_ERROR, SETTINGS_ERROR_UNKNOWN_PLUGIN, plugin);
        }
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
    SettingsPlugin* self = THIS(plugin);
    GError* error = NULL;

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
    SettingsPlugin* self = THIS(plugin);

    GERR("'%s' service already running or access denied", name);
    /* Tell daemon to exit */
    nfc_manager_stop(self->manager, NFC_MANAGER_PLUGIN_ERROR);
}

static
void
settings_plugin_config_changed(
    NfcConfigurable* config,
    const char* key,
    GVariant* value,
    void* user_data)
{
    SettingsPluginConfig* pc = user_data;
    SettingsPlugin* self = pc->settings;

    org_sailfishos_nfc_settings_emit_plugin_value_changed(self->iface,
        pc->name, key, g_variant_new_variant(value));
    settings_plugin_update_config(self);
}

static
void
settings_plugin_config_free(
    gpointer user_data)
{
    SettingsPluginConfig* pc = user_data;

    nfc_config_remove_handler(pc->config, pc->change_id);
    gutil_slice_free(pc);
}

static
void
settings_plugin_load_defaults(
    SettingsPlugin* self)
{
    const char* config_dir = GET_THIS_CLASS(self)->config_dir;
    char* defaults_file = g_build_filename(config_dir,
        SETTINGS_CONFIG_DEFAULTS_FILE, NULL);
    char* defaults_dir_name = g_build_filename(config_dir,
        SETTINGS_CONFIG_DEFAULTS_DIR, NULL);
    GDir* defaults_dir = g_dir_open(defaults_dir_name, 0, NULL);

    /*
     * Note that this is done twice - before and after loading all plugins.
     * First time only the global defaults (i.e. the [Settings] section) get
     * pulled in.
     */
    g_key_file_load_from_file(self->defaults, defaults_file, 0, NULL);
    if (defaults_dir) {
        const char* name;
        GPtrArray* buf = g_ptr_array_new();

        while ((name = g_dir_read_name(defaults_dir)) != NULL) {
            char* path = g_build_filename(defaults_dir_name, name, NULL);

            if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
                g_ptr_array_add(buf, path);
            } else {
                g_free(path);
            }
        }

        if (buf->len) {
            GKeyFile* overwrite = g_key_file_new();
            char** names;
            char** ptr;

            /* NULL-terminate the list */
            g_ptr_array_add(buf, NULL);

            /* Sort the names */
            ptr = names = gutil_strv_sort((char**)
                g_ptr_array_free(buf, FALSE), TRUE);
            while (*ptr) {
                const char* file = *ptr++;

                if (g_key_file_load_from_file(overwrite, file, 0, NULL)) {
                    settings_plugin_merge_defaults(self, overwrite);
                }
            }
            g_strfreev(names);
            g_key_file_unref(overwrite);
        } else {
            g_ptr_array_free(buf, TRUE);
        }
        g_dir_close(defaults_dir);
    }

    g_free(defaults_file);
    g_free(defaults_dir_name);
}

static
void
settings_plugin_started(
    NfcPlugin* plugin)
{
    SettingsPlugin* self = THIS(plugin);
    NfcPlugin* const* plugins = nfc_manager_plugins(self->manager);
    NfcPlugin* const* ptr = plugins;
    GPtrArray* buf = g_ptr_array_new();
    GKeyFile* config = settings_plugin_load_config(self);
    char** names;

    /* All functional plugins have been successfully started */
    while (*ptr) {
        NfcPlugin* p = *ptr++;

        if (NFC_IS_CONFIGURABLE(p)) {
            char* name = g_strdup(p->desc->name);
            SettingsPluginConfig* pc = g_slice_new(SettingsPluginConfig);

            GDEBUG("Plugin '%s' is configurable", name);
            pc->name = name;
            pc->settings = self;
            pc->config = NFC_CONFIGURABLE(p);
            g_hash_table_insert(self->plugins, name, pc);
            g_ptr_array_add(buf, name);
        }
    }

    /* NULL-terminate and sort the array */
    g_ptr_array_add(buf, NULL);
    self->order = gutil_strv_sort((char**)
        g_ptr_array_free(buf, FALSE), TRUE);

    /* Apply the initial configuration and register change listeners */
    settings_plugin_load_defaults(self);
    names = self->order;
    while (*names) {
        const char* name = *names++;
        SettingsPluginConfig* pc = g_hash_table_lookup(self->plugins, name);
        const char* const* keys = nfc_config_get_keys(pc->config);

        if (keys) {
            while (*keys) {
                const char* key = *keys++;
                char* str = g_key_file_get_string(config, name, key, NULL);

                if (!str) {
                    str = g_key_file_get_string(self->defaults, name, key,
                        NULL);
                }
                if (str) {
                    GVariant* var = g_variant_parse(NULL, str, NULL, NULL,
                        NULL);

                    if (!var) {
                        /* Interpret unparseable values as strings */
                        GDEBUG("Unable to parse [%s] %s=%s", name, key, str);
                        var = g_variant_ref_sink(g_variant_new_string(str));
                    }
                    nfc_config_set_value(pc->config, key, var);
                    g_variant_unref(var);
                    g_free(str);
                }
            }
        }

        /* Now we can listen for changes */
        pc->change_id = nfc_config_add_change_handler(pc->config, NULL,
            settings_plugin_config_changed, pc);
    }

    /* Apply global values */
    self->nfc_enabled = settings_plugin_nfc_enabled(self, config);
    GINFO("NFC %s", self->nfc_enabled ? "enabled" : "disabled");
    nfc_manager_set_enabled(self->manager, self->nfc_enabled);

    if (settings_plugin_nfc_always_on(self, config)) {
        nfc_manager_request_power(self->manager, TRUE);
    }

    /* Check the config (mostly for dbus_neard migration) */
    if (settings_plugin_update_settings(self, config)) {
        settings_plugin_save_config(self, config);
    }

    g_key_file_unref(config);
}

static
gboolean
settings_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    SettingsPlugin* self = THIS(plugin);

    GVERBOSE("Starting");
    self->manager = nfc_manager_ref(manager);
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
    self->dbus_call_id[SETTINGS_DBUS_CALL_GET_ALL2] =
        g_signal_connect(self->iface, "handle-get-all2",
        G_CALLBACK(settings_plugin_dbus_handle_get_all2), self);
    self->dbus_call_id[SETTINGS_DBUS_CALL_GET_ALL_PLUGIN_SETTINGS] =
        g_signal_connect(self->iface, "handle-get-all-plugin-settings",
        G_CALLBACK(settings_plugin_dbus_handle_get_all_plugin_settings), self);
    self->dbus_call_id[SETTINGS_DBUS_CALL_GET_PLUGIN_SETTINGS] =
        g_signal_connect(self->iface, "handle-get-plugin-settings",
        G_CALLBACK(settings_plugin_dbus_handle_get_plugin_settings), self);
    self->dbus_call_id[SETTINGS_DBUS_CALL_GET_PLUGIN_VALUE] =
        g_signal_connect(self->iface, "handle-get-plugin-value",
        G_CALLBACK(settings_plugin_dbus_handle_get_plugin_value), self);
    self->dbus_call_id[SETTINGS_DBUS_CALL_SET_PLUGIN_VALUE] =
        g_signal_connect(self->iface, "handle-set-plugin-value",
        G_CALLBACK(settings_plugin_dbus_handle_set_plugin_value), self);

    self->own_name_id = settings_plugin_name_own(self, SETTINGS_DBUS_SERVICE,
        settings_plugin_dbus_connected, settings_plugin_dbus_name_acquired,
        settings_plugin_dbus_name_lost);
    return TRUE;
}

static
void
settings_plugin_stop(
    NfcPlugin* plugin)
{
    SettingsPlugin* self = THIS(plugin);

    GVERBOSE("Stopping");
    g_hash_table_remove_all(self->plugins);
    if (self->own_name_id) {
        settings_plugin_name_unown(self->own_name_id);
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
        nfc_manager_unref(self->manager);
        self->manager = NULL;
    }
}

static
void
settings_plugin_init(
    SettingsPlugin* self)
{
    self->defaults = g_key_file_new();
    self->storage_file = g_build_filename(GET_THIS_CLASS(self)->storage_dir,
        SETTINGS_STORAGE_FILE, NULL);
    self->plugins = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, settings_plugin_config_free);
#ifdef HAVE_DBUSACCESS
    self->policy = da_policy_new_full(settings_default_policy,
        settings_policy_actions);
#endif
}

static
void
settings_plugin_finalize(
    GObject* plugin)
{
    SettingsPlugin* self = THIS(plugin);

#ifdef HAVE_DBUSACCESS
    da_policy_unref(self->policy);
#endif
    g_free(self->storage_file);
    g_strfreev(self->order);
    g_key_file_unref(self->defaults);
    g_hash_table_destroy(self->plugins);
    G_OBJECT_CLASS(settings_plugin_parent_class)->finalize(plugin);
}

static
void
settings_plugin_class_init(
    SettingsPluginClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    NfcPluginClass* plugin_class = NFC_PLUGIN_CLASS(klass);

    object_class->finalize = settings_plugin_finalize;
    plugin_class->start = settings_plugin_start;
    plugin_class->stop = settings_plugin_stop;
    plugin_class->started = settings_plugin_started;
    klass->storage_dir = SETTINGS_STORAGE_DIR;
    klass->config_dir = SETTINGS_CONFIG_DIR;
}

static
NfcPlugin*
settings_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(THIS_TYPE, NULL);
}

NFC_PLUGIN_DEFINE(settings, "Settings storage and D-Bus interface",
    settings_plugin_create)

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
