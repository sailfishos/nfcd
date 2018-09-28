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

#include "plugin.h"

#include <nfc_manager.h>
#include <nfc_plugin_impl.h>

#include <dbuslog_server_gio.h>

#define GLOG_MODULE_NAME dbus_log_log
#include <gutil_log.h>

GLOG_MODULE_DEFINE("dbus-log");

enum {
    DBUSLOG_EVENT_CATEGORY_ENABLED,
    DBUSLOG_EVENT_CATEGORY_DISABLED,
    DBUSLOG_EVENT_CATEGORY_LEVEL_CHANGED,
    DBUSLOG_EVENT_DEFAULT_LEVEL_CHANGED,
    DBUSLOG_EVENT_COUNT
};

/* Hold a reference to the plugin while we are using its GLogModule */
typedef struct dbus_log_plugin_category {
    NfcPlugin* plugin;
    GLogModule* log;
} DBusLogPluginCategory;

typedef NfcPluginClass DBusLogPluginClass;
typedef struct dbus_log_plugin {
    NfcPlugin parent;
    DBusLogServer* logserver;
    gulong event_id[DBUSLOG_EVENT_COUNT];
    GLogProc2 default_func;
    GHashTable* log_modules;
} DBusLogPlugin;

G_DEFINE_TYPE(DBusLogPlugin, dbus_log_plugin, NFC_TYPE_PLUGIN)
#define DBUS_LOG_TYPE_PLUGIN (dbus_log_plugin_get_type())
#define DBUS_LOG_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUS_LOG_TYPE_PLUGIN, DBusLogPlugin))

static DBusLogPlugin* dbus_log_plugin_active;

static
DBusLogPluginCategory*
dbus_log_plugin_category_new(
    NfcPlugin* plugin,
    GLogModule* log)
{
    DBusLogPluginCategory* cat = g_new(DBusLogPluginCategory, 1);

    cat->plugin = nfc_plugin_ref(plugin);
    cat->log = log;
    return cat;
}

static
void
dbus_log_plugin_category_free(
    gpointer data)
{
    DBusLogPluginCategory* cat = data;

    nfc_plugin_unref(cat->plugin);
    g_free(cat);
}

static
DBUSLOG_LEVEL
dbus_log_plugin_convert_to_dbus_level(
    int level)
{
    switch (level) {
    case GLOG_LEVEL_ALWAYS:
        return DBUSLOG_LEVEL_ALWAYS;
    case GLOG_LEVEL_ERR:
        return DBUSLOG_LEVEL_ERROR;
    case GLOG_LEVEL_WARN:
        return DBUSLOG_LEVEL_WARNING;
    case GLOG_LEVEL_INFO:
        return DBUSLOG_LEVEL_INFO;
    case GLOG_LEVEL_DEBUG:
        return DBUSLOG_LEVEL_DEBUG;
    case GLOG_LEVEL_VERBOSE:
        return DBUSLOG_LEVEL_VERBOSE;
    default:
        return DBUSLOG_LEVEL_UNDEFINED;
    }
}

static
int
dbus_log_plugin_convert_from_dbus_level(
    DBUSLOG_LEVEL level)
{
    switch (level) {
    case DBUSLOG_LEVEL_ALWAYS:
        return GLOG_LEVEL_ALWAYS;
    case DBUSLOG_LEVEL_ERROR:
        return GLOG_LEVEL_ERR;
    case DBUSLOG_LEVEL_WARNING:
        return GLOG_LEVEL_WARN;
    case DBUSLOG_LEVEL_INFO:
        return GLOG_LEVEL_INFO;
    case DBUSLOG_LEVEL_DEBUG:
        return GLOG_LEVEL_DEBUG;
    case DBUSLOG_LEVEL_VERBOSE:
        return GLOG_LEVEL_VERBOSE;
    default:
        return GLOG_LEVEL_NONE;
    }
}

static
void
dbus_log_plugin_func(
    DBusLogPlugin* self,
    const GLogModule* log,
    int level,
    const char* format,
    va_list va)
{
    dbus_log_server_logv(self->logserver,
        dbus_log_plugin_convert_to_dbus_level(level), log->name, format, va);
    if (self->default_func) {
        self->default_func(log, level, format, va);
    }
}

static
void
dbus_log_plugin_hook(
    const GLogModule* log,
    int level,
    const char* format,
    va_list va)
{
    GASSERT(dbus_log_plugin_active);
    if (dbus_log_plugin_active) {
        dbus_log_plugin_func(dbus_log_plugin_active, log, level, format, va);
    }
}

static
void
dbus_log_plugin_add_category(
    DBusLogPlugin* self,
    NfcPlugin* plugin,
    GLogModule* log)
{
    gulong flags = 0;

    GDEBUG("Adding \"%s\"", log->name);
    g_hash_table_replace(self->log_modules, g_strdup(log->name),
        dbus_log_plugin_category_new(plugin, log));
    if (!(log->flags & GLOG_FLAG_DISABLE)) {
        flags |= (DBUSLOG_CATEGORY_FLAG_ENABLED |
            DBUSLOG_CATEGORY_FLAG_ENABLED_BY_DEFAULT);
    }
    if (log->flags & GLOG_FLAG_HIDE_NAME) {
        flags |= DBUSLOG_CATEGORY_FLAG_HIDE_NAME;
    }
    dbus_log_server_add_category(self->logserver, log->name,
        dbus_log_plugin_convert_to_dbus_level(log->level), flags);
}

/*==========================================================================*
 * Events
 *==========================================================================*/

static
void
dbus_log_plugin_category_enabled(
    DBusLogServer* server,
    const char* name,
    gpointer user_data)
{
    DBusLogPlugin* self = DBUS_LOG_PLUGIN(user_data);
    DBusLogPluginCategory* cat = g_hash_table_lookup(self->log_modules, name);

    GASSERT(cat);
    if (cat) {
        cat->log->flags &= ~GLOG_FLAG_DISABLE;
    }
}

static
void
dbus_log_plugin_category_disabled(
    DBusLogServer* server,
    const char* name,
    gpointer user_data)
{
    DBusLogPlugin* self = DBUS_LOG_PLUGIN(user_data);
    DBusLogPluginCategory* cat = g_hash_table_lookup(self->log_modules, name);

    GASSERT(cat);
    if (cat) {
        cat->log->flags |= GLOG_FLAG_DISABLE;
    }
}

static
void
dbus_log_plugin_category_level_changed(
    DBusLogServer* server,
    const char* name,
    DBUSLOG_LEVEL dbus_level,
    gpointer user_data)
{
    DBusLogPlugin* self = DBUS_LOG_PLUGIN(user_data);
    const int level = dbus_log_plugin_convert_from_dbus_level(dbus_level);
    DBusLogPluginCategory* cat = g_hash_table_lookup(self->log_modules, name);

    GASSERT(cat);
    if (cat && level != GLOG_LEVEL_NONE) {
        cat->log->level = level;
    }
}

static
void
dbus_log_plugin_default_level_changed(
    DBusLogServer* server,
    DBUSLOG_LEVEL dbus_level,
    gpointer user_data)
{
    const int level = dbus_log_plugin_convert_from_dbus_level(dbus_level);

    if (level != GLOG_LEVEL_NONE) {
        gutil_log_default.level = level;
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
gboolean
dbus_log_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    DBusLogPlugin* self = DBUS_LOG_PLUGIN(plugin);
    NfcPlugin* const* plugins = nfc_manager_plugins(manager);
    guint i;

    GVERBOSE("Starting");
    dbus_log_plugin_add_category(self, NULL, &gutil_log_default);
    dbus_log_plugin_add_category(self, NULL, &NFC_CORE_LOG_MODULE);
    /* dbus_log_plugin_add_category(self, &DBUSACCESS_LOG_MODULE); */
    for (i = 0; plugins[i]; i++) {
        NfcPlugin* plugin = plugins[i];
        const NfcPluginDesc* desc = plugin->desc;
        GLogModule* const* logs = desc->log;

        if (logs) {
            while (*logs) {
                dbus_log_plugin_add_category(self, plugin, *logs++);
            }
        }
    }

    self->event_id[DBUSLOG_EVENT_CATEGORY_ENABLED] =
        dbus_log_server_add_category_enabled_handler(self->logserver,
	    dbus_log_plugin_category_enabled, self);
    self->event_id[DBUSLOG_EVENT_CATEGORY_DISABLED] =
        dbus_log_server_add_category_disabled_handler(self->logserver,
	    dbus_log_plugin_category_disabled, self);
    self->event_id[DBUSLOG_EVENT_CATEGORY_LEVEL_CHANGED] =
        dbus_log_server_add_category_level_handler(self->logserver,
            dbus_log_plugin_category_level_changed, self);
    self->event_id[DBUSLOG_EVENT_DEFAULT_LEVEL_CHANGED] =
        dbus_log_server_add_default_level_handler(self->logserver,
            dbus_log_plugin_default_level_changed, self);

    dbus_log_plugin_active = self;
    self->default_func = gutil_log_func2;
    gutil_log_func2 = dbus_log_plugin_hook;
    dbus_log_server_set_default_level(self->logserver,
        dbus_log_plugin_convert_to_dbus_level(gutil_log_default.level));
    dbus_log_server_start(self->logserver);
    return TRUE;
}

static
void
dbus_log_plugin_stop(
    NfcPlugin* plugin)
{
    DBusLogPlugin* self = DBUS_LOG_PLUGIN(plugin);

    GVERBOSE("Stopping");
    dbus_log_server_stop(self->logserver);
    dbus_log_server_remove_all_handlers(self->logserver, self->event_id);
    g_hash_table_remove_all(self->log_modules);
    if (dbus_log_plugin_active == self) {
        dbus_log_plugin_active = NULL;
        gutil_log_func2 = self->default_func;
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
dbus_log_plugin_init(
    DBusLogPlugin* self)
{
    self->default_func = gutil_log_func2;
    self->logserver = dbus_log_server_new(G_BUS_TYPE_SYSTEM, NULL, "/");
    dbus_log_server_set_default_level(self->logserver, DBUSLOG_LEVEL_DEBUG);
    self->parent.desc = &NFC_PLUGIN_DESC(dbus_log);
    self->log_modules = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, dbus_log_plugin_category_free);
}

static
void
dbus_log_plugin_finalize(
    GObject* plugin)
{
    DBusLogPlugin* self = DBUS_LOG_PLUGIN(plugin);

    dbus_log_server_unref(self->logserver);
    g_hash_table_destroy(self->log_modules);
    G_OBJECT_CLASS(dbus_log_plugin_parent_class)->finalize(plugin);
}

static
void
dbus_log_plugin_class_init(
    NfcPluginClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = dbus_log_plugin_finalize;
    klass->start = dbus_log_plugin_start;
    klass->stop = dbus_log_plugin_stop;
}

static
NfcPlugin*
dbus_log_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(DBUS_LOG_TYPE_PLUGIN, NULL);
}

NFC_PLUGIN_DEFINE(dbus_log, "Logging over D-Bus", dbus_log_plugin_create)

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
