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

#include "nfc_plugin_p.h"
#include "nfc_plugins.h"
#include "nfc_log.h"

#include <gutil_strv.h>
#include <gutil_idlepool.h>

#include <stdlib.h>
#include <dlfcn.h>

typedef struct nfc_plugin_data {
    NfcPlugin* plugin;
    gboolean started;
} NfcPluginData;

typedef struct nfc_plugin_handle {
    GUtilIdlePool* pool;
    void* handle;
} NfcPluginHandle;

struct nfc_plugins {
    GSList* plugins;
    GUtilIdlePool* pool;
};

static
void
nfc_plugins_free_plugin_data(
    NfcPluginData* data)
{
    if (data->started) {
        nfc_plugin_stop(data->plugin);
    }
    nfc_plugin_unref(data->plugin);
    g_free(data);
}

static
void
nfc_plugins_free_plugin_data1(
    gpointer data)
{
    nfc_plugins_free_plugin_data(data);
}

static
int
nfc_plugins_compare_file_names(
    const void* p1,
    const void *p2)
{
    return strcmp(*(char**)p1, *(char**)p2);
}

static
GStrV*
nfc_plugins_scan_plugin_dir(
    const char* plugin_dir)
{
    GStrV* files = NULL;
    GDir* dir = g_dir_open(plugin_dir, 0, NULL);

    if (dir) {
	const char* file;

        while ((file = g_dir_read_name(dir)) != NULL) {
            if (!g_str_has_prefix(file, "lib") &&
                g_str_has_suffix(file, ".so")) {
                files = gutil_strv_add(files, file);
            }
        }
        g_dir_close(dir);

        /* Sort file names to guarantee precedence order in case of conflict */
        if (files) {
            qsort(files, gutil_strv_length(files), sizeof(char*),
                nfc_plugins_compare_file_names);
        }
    }
    return files;
}

static
const NfcPluginData*
nfc_plugins_find(
    NfcPlugins* self,
    const char* name)
{
    GSList* l = self->plugins;

    for (l = self->plugins; l; l = l->next) {
        NfcPluginData* data = l->data;
        const NfcPluginDesc* desc = data->plugin->desc;

        if (!g_strcmp0(desc->name, name)) {
            return data;
        }
    }
    return NULL;
}

static
void
nfc_plugins_unload_library(
    gpointer data)
{
    NfcPluginHandle* h = data;

    dlclose(h->handle);
    gutil_idle_pool_unref(h->pool);
    g_free(h);
}

static
void
nfc_plugins_unload_plugin(
    gpointer data,
    GObject* dead_obj)
{
    NfcPluginHandle* h = data;

    /* We need to unload the library on a fresh stack, because the current
     * stack may contain functions from the library we are going to unload. */
    gutil_idle_pool_add(h->pool, h, nfc_plugins_unload_library);
}

static
NfcPlugin*
nfc_plugins_create_plugin(
    NfcPlugins* self,
    const NfcPluginDesc* desc,
    const GStrV* enable,
    const GStrV* disable,
    void* handle)
{
    gboolean load;
    NfcPlugin* plugin = NULL;

    if (gutil_strv_contains(disable, desc->name)) {
        if (!(desc->flags & NFC_PLUGIN_FLAG_DISABLED)) {
            GINFO("Plugin \"%s\" is disabled", desc->name);
        }
        load = FALSE;
    } else if (gutil_strv_contains(enable, desc->name)) {
        if (desc->flags & NFC_PLUGIN_FLAG_DISABLED) {
            GINFO("Plugin \"%s\" is enabled", desc->name);
        }
        load = TRUE;
    } else {
        load = !(desc->flags & NFC_PLUGIN_FLAG_DISABLED);
    }

    if (load) {
        GASSERT(desc->create);
        if (desc->create) {
            plugin = desc->create();
            if (plugin) {
                NfcPluginData* plugin_data = g_new0(NfcPluginData, 1);

                plugin->desc = desc;
                if (handle) {
                    NfcPluginHandle* handle_data = g_new(NfcPluginHandle, 1);

                    /* We can't unload the library before plugin is gone. */
                    handle_data->pool = gutil_idle_pool_ref(self->pool);
                    handle_data->handle = handle;
                    g_object_weak_ref(G_OBJECT(plugin),
                        nfc_plugins_unload_plugin, handle_data);
                }
                plugin_data->plugin = plugin;
                self->plugins = g_slist_append(self->plugins, plugin_data);
            } else {
                GERR("Plugin \"%s\" failed to initialize", desc->name);
            }
        }
    }
    return plugin;
}

static
gboolean
nfc_plugins_validate_plugin(
    NfcPlugins* self,
    const NfcPluginDesc* desc,
    const char* path)
{
    if (!desc->name) {
        GWARN("Invalid plugin %s (ignored)", path);
    } else if (nfc_plugins_find(self, desc->name)) {
        GWARN("Duplicate plugin \"%s\" from %s (ignored)", desc->name, path);
    } else if (desc->nfc_core_version > NFC_CORE_VERSION) {
        GWARN("Plugin %s requries nfcd %d.%d.%d (ignored)", path,
            NFC_VERSION_GET_MAJOR(desc->nfc_core_version),
            NFC_VERSION_GET_MINOR(desc->nfc_core_version),
            NFC_VERSION_GET_NANO(desc->nfc_core_version));
    } else {
        return TRUE;
    }
    return FALSE;
}

NfcPlugins*
nfc_plugins_new(
    const NfcPluginsInfo* pi)
{
    NfcPlugins* self = g_new0(NfcPlugins, 1);
    const GStrV* enable = (GStrV*)pi->enable;
    const GStrV* disable = (GStrV*)pi->disable;

    self->pool = gutil_idle_pool_new();

    /* Load external plugins */
    if (pi->plugin_dir) {
        GStrV* files = nfc_plugins_scan_plugin_dir(pi->plugin_dir);

        if (files) {
            char** ptr = files;

            while (*ptr) {
                const char* file = *ptr++;
                char* path = g_build_filename(pi->plugin_dir, file, NULL);
                void* handle = dlopen(path, RTLD_NOW);

                if (handle) {
                    const char* sym = G_STRINGIFY(NFC_PLUGIN_DESC_SYMBOL);
                    const NfcPluginDesc* desc = dlsym(handle, sym);
                    NfcPlugin* plugin = NULL;

                    if (desc) {
                        if (nfc_plugins_validate_plugin(self, desc, path)) {
                            /* Drop the handle if we are not supposed to
                             * unload the libraries (useful e.g. if running
                             * under valgrind) */
                            plugin = nfc_plugins_create_plugin(self, desc,
                                enable, disable, (pi->flags &
                                    NFC_PLUGINS_DONT_UNLOAD) ? NULL : handle);
                            if (plugin) {
                                GDEBUG("Loaded plugin \"%s\" from %s",
                                    desc->name, path);
                            }
                        }
                    } else {
                        GERR("Symbol \"%s\" not found in %s", sym, path);
                    }
                    if (!plugin) {
                        dlclose(handle);
                    }
                } else {
                    GERR("Failed to load %s: %s", path, dlerror());
                }
                g_free(path);
            }
            g_strfreev(files);
        }
    }

    if (pi->builtins) {
        const NfcPluginDesc* const* pdesc;

        for (pdesc = pi->builtins; *pdesc; pdesc++) {
            const NfcPluginDesc* desc = *pdesc;

            /* External plugins take precedence over builtins */
            GASSERT(desc->name);
            if (nfc_plugins_find(self, desc->name)) {
                GINFO("Builtin plugin \"%s\" is replaced by external",
                    desc->name);
            } else {
                nfc_plugins_create_plugin(self, desc, enable, disable, NULL);
            }
        }
    }

    return self;
}

void
nfc_plugins_free(
    NfcPlugins* self)
{
    if (G_LIKELY(self)) {
        g_slist_free_full(self->plugins, nfc_plugins_free_plugin_data1);
        gutil_idle_pool_destroy(self->pool);
        g_free(self);
    }
}

gboolean
nfc_plugins_start(
    NfcPlugins* self,
    NfcManager* manager)
{
    if (G_LIKELY(self)) {
        GSList* l = self->plugins;
        gboolean ok = TRUE;

        while (l) {
            GSList* next = l->next;
            NfcPluginData* data = l->data;

            data->started = nfc_plugin_start(data->plugin, manager);
            if (!data->started) {
                const NfcPluginDesc* desc = data->plugin->desc;

                if (desc->flags & NFC_PLUGIN_FLAG_MUST_START) {
                    ok = FALSE;
                }

                gutil_log(GLOG_MODULE_CURRENT,
                    (desc->flags & NFC_PLUGIN_FLAG_MUST_START) ? 
                    GLOG_LEVEL_ERR : GLOG_LEVEL_WARN,
                   "Plugin \"%s\" failed to start", desc->name);

                nfc_plugins_free_plugin_data(data);
                self->plugins = g_slist_delete_link(self->plugins, l);
            }
            l = next;
        }
        return ok;
    }
    return FALSE;
}

void
nfc_plugins_stop(
    NfcPlugins* self)
{
    if (G_LIKELY(self)) {
        GSList* l;

        for (l = self->plugins; l; l = l->next) {
            NfcPluginData* data = l->data;

            if (data->started) {
                data->started = FALSE;
                nfc_plugin_stop(data->plugin);
            }
        }
    }
}

static
void
nfc_plugins_list_free(
    gpointer data)
{
    NfcPlugin** list = data;

    while (*list) {
        nfc_plugin_unref(*list++);
    }
    g_free(data);
}

NfcPlugin* const*
nfc_plugins_list(
    NfcPlugins* self)
{
    NfcPlugin** list = NULL;

    if (G_LIKELY(self)) {
        const guint len = g_slist_length(self->plugins);
        guint i;
        GSList* l;

        list = g_new(NfcPlugin*, len + 1);
        for (l = self->plugins, i = 0; l; l = l->next, i++) {
            NfcPluginData* data = l->data;

            list[i] = nfc_plugin_ref(data->plugin);
        }
        list[i] = NULL;
        gutil_idle_pool_add(self->pool, list, nfc_plugins_list_free);
    }
    return list;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
