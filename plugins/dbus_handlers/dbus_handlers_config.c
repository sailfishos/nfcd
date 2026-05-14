/*
 * Copyright (C) 2018-2026 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2019 Open Mobile Platform LLC.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "dbus_handlers.h"

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
dbus_handlers_config_add2(
    DBusHandlerConfigList* list,
    DBusHandlerConfig* entry)
{
    const DBusHandlerType* type = entry->type;

    if (!list->last) {
        /* The first entry */
        GASSERT(!list->first);
        entry->next = NULL;
        list->first = list->last = entry;
    } else if (list->last->type->priority >= type->priority) {
        /* Becomes last in the list */
        entry->next = NULL;
        list->last->next = entry;
        list->last = entry;
    } else if (list->first->type->priority < type->priority) {
        /* Becomes first in the list */
        entry->next = list->first;
        list->first = entry;
    } else {
        /* Gets inserted somewhere in the middle */
        DBusHandlerConfig* ptr = list->first;

        /*
         * Priority of the last entry is smaller than ours (checked
         * above), therefore ptr->next can't be NULL.
         */
        while (ptr->next->type->priority >= type->priority) {
            ptr = ptr->next;
        }

        entry->next = ptr->next;
        ptr->next = entry;
    }
}

static
void
dbus_handlers_config_add(
    DBusHandlerConfigList* handlers,
    DBusHandlerConfigList* listeners,
    const DBusHandlerType* type,
    GKeyFile* file,
    NfcNdefRec* ndef)
{
    NfcNdefRec* rec = dbus_handlers_config_find_supported_record(ndef, type);

    if (rec) {
        DBusHandlerConfig* handler = type->new_handler_config(file, rec);
        DBusHandlerConfig* listener = type->new_listener_config(file, rec);

        if (handler) {
            handler->type = type;
            dbus_handlers_config_add2(handlers, handler);
        }
        if (listener) {
            listener->type = type;
            dbus_handlers_config_add2(listeners, listener);
        }
    }
}

static
DBusHandlersConfig*
dbus_handlers_config_load_types(
    const char* dir,
    GSList* types,
    NfcNdefRec* ndef)
{
    GStrV* files = dbus_handlers_config_files(dir);
    DBusHandlersConfig* config = NULL;

    if (files) {
        char** ptr = files;
        DBusHandlerConfigList handlers;
        DBusHandlerConfigList listeners;
        GString* path = g_string_new(dir);
        const guint baselen = path->len + 1;
        GSList* keyfiles = NULL;

        g_string_append_c(path, G_DIR_SEPARATOR);
        memset(&handlers, 0, sizeof(handlers));
        memset(&listeners, 0, sizeof(listeners));
        while (*ptr) {
            const char* fname = *ptr++;
            GKeyFile* kf = g_key_file_new();

            g_string_set_size(path, baselen);
            g_string_append(path, fname);
            if (g_key_file_load_from_file(kf, path->str, 0, NULL)) {
                keyfiles = g_slist_append(keyfiles, kf);
            } else {
                g_key_file_unref(kf);
            }
        }

        if (keyfiles) {
            GSList* l;

            for (l = types; l; l = l->next) {
                GSList* k;

                for (k = keyfiles; k; k = k->next) {
                    dbus_handlers_config_add(&handlers, &listeners,
                        (const DBusHandlerType*)l->data,
                        (GKeyFile*)k->data, ndef);
                }
            }
            g_slist_free_full(keyfiles, (GDestroyNotify)g_key_file_unref);
        }
        g_strfreev(files);
        g_string_free(path, TRUE);
        if (handlers.first || listeners.first) {
            config = g_slice_new0(DBusHandlersConfig);
            config->handlers = handlers.first;
            config->listeners = listeners.first;
        }
    }

    return config;
}

static
void
dbus_handlers_free_config_list(
    DBusHandlerConfig* config)
{
    while (config) {
        DBusHandlerConfig* next = config->next;

        config->next = NULL;
        config->type->free_config(config);
        config = next;
    }
}

/*==========================================================================*
 * Handler type helpers
 *==========================================================================*/

DBusHandlerConfig*
dbus_handlers_config_new(
    GKeyFile* file,
    const char* group)
{
    DBusConfig dbus;

    memset(&dbus, 0, sizeof(dbus));
    if (dbus_handlers_config_parse_dbus(&dbus, file, group)) {
        DBusHandlerConfig* config = g_slice_new0(DBusHandlerConfig);

        config->dbus = dbus;
        return config;
    }
    return NULL;
}

void
dbus_handlers_config_free1(
    DBusHandlerConfig* handler)
{
    DBusConfig* dbus = &handler->dbus;

    g_free(dbus->service);
    g_free(dbus->path);
    g_free(dbus->iface);
    g_slice_free(DBusHandlerConfig, handler);
}

NfcNdefRec*
dbus_handlers_config_find_record(
    NfcNdefRec* ndef,
    gboolean (*check)(NfcNdefRec* ndef))
{
    while (ndef) {
        if (check(ndef)) {
            return ndef;
        }
        ndef = ndef->next;
    }
    return NULL;
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

    if (service && g_dbus_is_name(service)) {
        char* iface_method = dbus_handlers_config_get_string(file, group,
            config_key_method);

        if (iface_method) {
            char* dot = strrchr(iface_method, '.');

            if (dot) {
                const char* method = dot + 1;

                if (!g_dbus_is_member_name(method)) {
                    GWARN("Not a valid method name: \"%s\"", method);
                } else {
                    dot[0] = 0;
                    if (!g_dbus_is_interface_name(iface_method)) {
                        GWARN("Not a valid interface name: \"%s\"",
                            iface_method);
                    } else {
                        char* path = dbus_handlers_config_get_string(file,
                            group, config_key_path);

                        if (path && !g_variant_is_object_path(path)) {
                            GWARN("Not a valid path name: \"%s\"", path);
                        } else {
                            config->service = service;
                            config->path = path ? path :
                                g_strdup(config_default_path);
                            config->iface = iface_method;
                            config->method = method;
                            return TRUE;
                        }
                        g_free(path);
                    }
                }
            }
            g_free(iface_method);
        }
    } else if (service) {
        GWARN("Not a valid service name: \"%s\"", service);
    }
    g_free(service);
    return FALSE;
}

DBusHandlersConfig*
dbus_handlers_config_load(
    const char* dir,
    NfcNdefRec* ndef)
{
    DBusHandlersConfig* config = NULL;

    if (dir && ndef) {
        /*
         * dbus_handlers_type_generic doesn't need to be here.
         * It's a special case - we always try it and it's always
         * the last one. Only non-trivial handlers are here.
         *
         * Also, there's no need to have both dbus_handlers_type_mediatype
         * handlers in this array. They are buddies - when one matches,
         * the other one gets added too. This way we don't have to call
         * the same matching function twice.
         *
         * And it must be dbus_handlers_type_mediatype_exact rather than
         * dbus_handlers_type_mediatype_wildcard for exact matches to be
         * handled first.
         */
        static const DBusHandlerType* available_types[] = {
            &dbus_handlers_type_sp,
            &dbus_handlers_type_uri,
            &dbus_handlers_type_text,
            &dbus_handlers_type_mediatype_exact
        };
        GSList* types = NULL;
        guint remaining_count = G_N_ELEMENTS(available_types);
        const DBusHandlerType* remaining_types[G_N_ELEMENTS(available_types)];
        NfcNdefRec* rec;

        /*
         * Add relevant types in the order in which their NDEF records
         * appear on the tag.
         */
        memcpy(remaining_types, available_types, sizeof(remaining_types));
        for(rec = ndef; rec && remaining_count; rec = rec->next) {
            guint i;

            for (i = 0; i < G_N_ELEMENTS(remaining_types); i++) {
                const DBusHandlerType* type = remaining_types[i];

                if (type && type->supported_record(rec)) {
                    types = g_slist_append(types, (gpointer)type);
                    if (type->buddy) {
                        /* Buddies share the recognizer function */
                        types = g_slist_append(types, (gpointer)type->buddy);
                    }
                    remaining_types[i] = NULL;
                    remaining_count--;
                }
            }
        }

        types = g_slist_append(types, (gpointer)&dbus_handlers_type_generic);
        config = dbus_handlers_config_load_types(dir, types, ndef);
        g_slist_free(types);
    }
    return config;
}

void
dbus_handlers_config_free(
    DBusHandlersConfig* self)
{
    if (self) {
        dbus_handlers_free_config_list(self->handlers);
        dbus_handlers_free_config_list(self->listeners);
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
