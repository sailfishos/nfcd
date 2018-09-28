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

#include "dbus_handlers.h"
#include "plugin.h"

#include <nfc_adapter.h>
#include <nfc_manager.h>
#include <nfc_plugin_impl.h>

#include <gutil_misc.h>

#include <stdlib.h>

GLOG_MODULE_DEFINE("dbus-handlers");

enum {
    EVENT_ADAPTER_ADDED,
    EVENT_ADAPTER_REMOVED,
    EVENT_COUNT
};

typedef NfcPluginClass DBusHandlersPluginClass;
typedef struct dbus_handlers_plugin {
    NfcPlugin parent;
    GDBusConnection* connection;
    GHashTable* adapters;
    NfcManager* manager;
    DBusHandlers* handlers;
    gulong event_id[EVENT_COUNT];
} DBusHandlersPlugin;

G_DEFINE_TYPE(DBusHandlersPlugin, dbus_handlers_plugin, NFC_TYPE_PLUGIN)
#define DBUS_HANDLERS_TYPE_PLUGIN (dbus_handlers_plugin_get_type())
#define DBUS_HANDLERS_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUS_HANDLERS_TYPE_PLUGIN, DBusHandlersPlugin))

#define DBUS_HANDLERS_CONFIG_DIR "/etc/nfcd/ndef-handlers"

static
void
dbus_handlers_plugin_free_adapter(
    void* adapter)
{
    dbus_handlers_adapter_free((DBusHandlersAdapter*)adapter);
}

static
void
dbus_handlers_adapter_add(
    DBusHandlersPlugin* self,
    NfcAdapter* adapter)
{
    g_hash_table_replace(self->adapters, g_strdup(adapter->name),
        dbus_handlers_adapter_new(adapter, self->handlers));
}

/*==========================================================================*
 * NfcManager events
 *==========================================================================*/

static
void
dbus_handlers_adapter_added(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    dbus_handlers_adapter_add(DBUS_HANDLERS_PLUGIN(plugin), adapter);
}

static
void
dbus_handlers_adapter_removed(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* plugin)
{
    DBusHandlersPlugin* self = DBUS_HANDLERS_PLUGIN(plugin);

    g_hash_table_remove(self->adapters, (void*)adapter->name);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
gboolean
dbus_handlers_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    GError* error = NULL;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    GVERBOSE("Starting");
    if (bus) {
        DBusHandlersPlugin* self = DBUS_HANDLERS_PLUGIN(plugin);
        NfcAdapter** adapters = manager->adapters;

        self->manager = nfc_manager_ref(manager);
        self->handlers = dbus_handlers_new(bus, DBUS_HANDLERS_CONFIG_DIR);
        g_object_ref(self->connection = bus);

        /* Existing adapters */
        if (adapters) {
            while (*adapters) {
                dbus_handlers_adapter_add(self, *adapters++);
            }
        }

        /* NfcManager events */
        self->event_id[EVENT_ADAPTER_ADDED] =
            nfc_manager_add_adapter_added_handler(manager,
                dbus_handlers_adapter_added, self);
        self->event_id[EVENT_ADAPTER_REMOVED] =
            nfc_manager_add_adapter_removed_handler(manager,
                dbus_handlers_adapter_removed, self);

        return TRUE;
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
        return FALSE;
    }
}

static
void
dbus_handlers_plugin_stop(
    NfcPlugin* plugin)
{
    DBusHandlersPlugin* self = DBUS_HANDLERS_PLUGIN(plugin);

    GVERBOSE("Stopping");
    g_hash_table_remove_all(self->adapters);

    dbus_handlers_free(self->handlers);
    self->handlers = NULL;

    g_object_unref(self->connection);
    self->connection = NULL;

    nfc_manager_remove_all_handlers(self->manager, self->event_id);
    nfc_manager_unref(self->manager);
    self->manager = NULL;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
dbus_handlers_plugin_init(
    DBusHandlersPlugin* self)
{
    self->adapters = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, dbus_handlers_plugin_free_adapter);
}

static
void
dbus_handlers_plugin_finalize(
    GObject* plugin)
{
    DBusHandlersPlugin* self = DBUS_HANDLERS_PLUGIN(plugin);

    g_hash_table_destroy(self->adapters);
    G_OBJECT_CLASS(dbus_handlers_plugin_parent_class)->finalize(plugin);
}

static
void
dbus_handlers_plugin_class_init(
    NfcPluginClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = dbus_handlers_plugin_finalize;
    klass->start = dbus_handlers_plugin_start;
    klass->stop = dbus_handlers_plugin_stop;
}

static
NfcPlugin*
dbus_handlers_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(DBUS_HANDLERS_TYPE_PLUGIN, NULL);
}

NFC_PLUGIN_DEFINE(dbus_handlers, "NDEF handling over D-Bus",
    dbus_handlers_plugin_create)

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
