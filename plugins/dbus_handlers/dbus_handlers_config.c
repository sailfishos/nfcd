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

#include <nfc_ndef.h>

#include <gutil_strv.h>

#include <stdlib.h>

static const char config_section_common[] = "Common";
static const char config_key_service[] = "Service";
static const char config_key_method[] = "Method";
static const char config_key_path[] = "Path";
static const char config_default_path[] = "/";

typedef struct dbus_handler_config_list {
    DBusHandlerConfig* first;
    DBusHandlerConfig* last;
} DBusHandlerConfigList;

typedef struct dbus_listener_config_list {
    DBusListenerConfig* first;
    DBusListenerConfig* last;
} DBusListenerConfigList;

static
int
dbus_handlers_config_compare_file_names(
    const void* p1,
    const void *p2)
{
    return strcmp(*(char**)p1, *(char**)p2);
}

static
GStrV*
dbus_handlers_config_files(
    const char* plugin_dir)
{
    GStrV* files = NULL;
    GDir* dir = g_dir_open(plugin_dir, 0, NULL);

    if (dir) {
	const char* file;

        while ((file = g_dir_read_name(dir)) != NULL) {
            if (g_str_has_suffix(file, ".conf")) {
                files = gutil_strv_add(files, file);
            }
        }
        g_dir_close(dir);

        if (files) {
            qsort(files, gutil_strv_length(files), sizeof(char*),
                dbus_handlers_config_compare_file_names);
        }
    }
    return files;
}

static
void
dbus_handlers_config_add_handler(
    DBusHandlerConfigList* list,
    DBusHandlerConfig* handler)
{
    handler->next = NULL;
    if (list->last) {
        list->last->next = handler;
    } else {
        GASSERT(!list->first);
        list->first = handler;
    }
    list->last = handler;
}

static
void
dbus_handlers_config_add_handlers(
    DBusHandlerConfigList* list,
    DBusHandlerConfigList* list2)
{
    if (list->last) {
        list->last->next = list2->first;
    } else {
        GASSERT(!list->first);
        list->first = list2->first;
    }
    list->last = list2->last;
    list2->first = list2->last = NULL;
}

static
void
dbus_handlers_config_add_listener(
    DBusListenerConfigList* list,
    DBusListenerConfig* listener)
{
    listener->next = NULL;
    if (list->last) {
        list->last->next = listener;
    } else {
        GASSERT(!list->first);
        list->first = listener;
    }
    list->last = listener;
}

static
void
dbus_handlers_config_add_listeners(
    DBusListenerConfigList* list,
    DBusListenerConfigList* list2)
{
    if (list->last) {
        list->last->next = list2->first;
    } else {
        GASSERT(!list->first);
        list->first = list2->first;
    }
    list->last = list2->last;
    list2->first = list2->last = NULL;
}

static
void
dbus_handlers_config_add(
    DBusHandlerConfigList* handlers,
    DBusListenerConfigList* listeners,
    const DBusHandlerType* type,
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    DBusHandlerConfig* handler = type->new_handler_config(file, ndef);
    DBusListenerConfig* listener = type->new_listener_config(file, ndef);

    if (handler) {
        handler->type = type;
        dbus_handlers_config_add_handler(handlers, handler);
    }
    if (listener) {
        listener->type = type;
        dbus_handlers_config_add_listener(listeners, listener);
    }
}

static
DBusHandlersConfig*
dbus_handlers_config_load_types(
    const char* dir,
    const DBusHandlerType* type,
    const DBusHandlerType* type2,
    NfcNdefRec* ndef)
{
    GStrV* files = dbus_handlers_config_files(dir);
    DBusHandlersConfig* config = NULL;
    GKeyFile* k = g_key_file_new();

    if (files) {
        char** ptr = files;
        DBusHandlerConfigList handlers, handlers2, handlers3;
        DBusListenerConfigList listeners, listeners2, listeners3;
        GString* path = g_string_new(dir);
        const guint baselen = path->len + 1;

        g_string_append_c(path, G_DIR_SEPARATOR);
        memset(&handlers, 0, sizeof(handlers));
        memset(&handlers2, 0, sizeof(handlers2));
        memset(&handlers3, 0, sizeof(handlers3));
        memset(&listeners, 0, sizeof(listeners));
        memset(&listeners2, 0, sizeof(listeners2));
        memset(&listeners3, 0, sizeof(listeners3));
        while (*ptr) {
            const char* fname = *ptr++;

            g_string_set_size(path, baselen);
            g_string_append(path, fname);
            if (g_key_file_load_from_file(k, path->str, 0, NULL)) {
                if (type) {
                    dbus_handlers_config_add(&handlers, &listeners,
                        type, k, ndef);
                }
                if (type2) {
                    dbus_handlers_config_add(&handlers2, &listeners2,
                        type2, k, ndef);
                }
                dbus_handlers_config_add(&handlers3, &listeners3,
                    &dbus_handlers_type_generic, k, ndef);
            }
        }
        g_strfreev(files);
        g_string_free(path, TRUE);

        dbus_handlers_config_add_handlers(&handlers, &handlers2);
        dbus_handlers_config_add_handlers(&handlers, &handlers3);
        dbus_handlers_config_add_listeners(&listeners, &listeners2);
        dbus_handlers_config_add_listeners(&listeners, &listeners3);
        if (handlers.first || listeners.first) {
            config = g_slice_new0(DBusHandlersConfig);
            config->handlers = handlers.first;
            config->listeners = listeners.first;
        }
    }

    g_key_file_unref(k);
    return config;
}

char*
dbus_handlers_config_get_string(
    GKeyFile* file,
    const char* group,
    const char* key)
{
    char* value = g_key_file_get_string(file, group, key, NULL);

    if (value) {
        return value;
    } else {
        return g_key_file_get_string(file, config_section_common, key, NULL);
    }
}

/*==========================================================================*
 * Handler type helpers
 *==========================================================================*/

DBusHandlerConfig*
dbus_handlers_new_handler_config(
    GKeyFile* file,
    const char* group)
{
    DBusConfig dbus;

    memset(&dbus, 0, sizeof(dbus));
    if (dbus_handlers_config_parse_dbus(&dbus, file, group)) {
        DBusHandlerConfig* handler = g_slice_new0(DBusHandlerConfig);

        handler->dbus = dbus;
        return handler;
    }
    return NULL;
}

DBusListenerConfig*
dbus_handlers_new_listener_config(
    GKeyFile* file,
    const char* group)
{
    DBusConfig dbus;

    memset(&dbus, 0, sizeof(dbus));
    if (dbus_handlers_config_parse_dbus(&dbus, file, group)) {
        DBusListenerConfig* listener = g_slice_new0(DBusListenerConfig);

        listener->dbus = dbus;
        return listener;
    }
    return NULL;
}

static
void
dbus_handlers_free_dbus_config(
    const DBusConfig* config)
{
    g_free(config->service);
    g_free(config->path);
    g_free(config->iface);
}

void
dbus_handlers_free_handler_config(
    DBusHandlerConfig* handler)
{
    dbus_handlers_free_dbus_config(&handler->dbus);
    g_slice_free(DBusHandlerConfig, handler);
}

void
dbus_handlers_free_listener_config(
    DBusListenerConfig* listener)
{
    dbus_handlers_free_dbus_config(&listener->dbus);
    g_slice_free(DBusListenerConfig, listener);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

gboolean
dbus_handlers_config_parse_dbus(
    DBusConfig* config,
    GKeyFile* file,
    const char* group)
{
    char* service = dbus_handlers_config_get_string(file, group,
        config_key_service);

    if (service) {
        char* iface_method = dbus_handlers_config_get_string(file, group,
            config_key_method);

        if (iface_method) {
            char* dot = strrchr(iface_method, '.');

            if (dot) {
                char* path = dbus_handlers_config_get_string(file, group,
                    config_key_path);

                config->service = service;
                config->path = path ? path : g_strdup(config_default_path);
                config->iface = iface_method;
                dot[0] = 0;
                config->method = dot + 1;
                return TRUE;
            }
            g_free(iface_method);
        }
        g_free(service);
    }
    return FALSE;
}

DBusHandlersConfig*
dbus_handlers_config_load(
    const char* dir,
    NfcNdefRec* ndef)
{
    if (dir && ndef) {
        const DBusHandlerType* type = NULL;
        const DBusHandlerType* type2 = NULL;

        if (NFC_IS_NFC_NDEF_REC_U(ndef)) {
            type = &dbus_handlers_type_uri;
        } else if (dbus_handlers_type_mediatype_record(ndef)) {
            type = &dbus_handlers_type_mediatype_exact;
            type2 = &dbus_handlers_type_mediatype_wildcard;
        }
        return dbus_handlers_config_load_types(dir, type, type2, ndef);
    }
    return NULL;
}

void
dbus_handlers_config_free(
    DBusHandlersConfig* self)
{
    if (self) {
        while (self->handlers) {
            DBusHandlerConfig* handler = self->handlers;
            const DBusHandlerType* type = handler->type;

            self->handlers = handler->next;
            handler->next = NULL;
            type->free_handler_config(handler);
        }
        while (self->listeners) {
            DBusListenerConfig* listener = self->listeners;
            const DBusHandlerType* type = listener->type;

            self->listeners = listener->next;
            listener->next = NULL;
            type->free_listener_config(listener);
        }
        g_slice_free(DBusHandlersConfig, self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
