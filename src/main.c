/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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

#include "internal/nfc_manager_i.h"

#include "dbus_handlers/plugin.h"
#include "dbus_log/plugin.h"
#include "dbus_neard/plugin.h"
#include "dbus_service/plugin.h"
#include "settings/plugin.h"

#include <gutil_log.h>
#include <gutil_strv.h>

#include <gio/gio.h>
#include <glib-unix.h>

#include <locale.h>

static const NfcPluginDesc* const nfcd_builtin_plugins[] = {
    &NFC_PLUGIN_DESC(dbus_log),
    &NFC_PLUGIN_DESC(dbus_handlers),
    &NFC_PLUGIN_DESC(dbus_neard),
    &NFC_PLUGIN_DESC(dbus_service),
    &NFC_PLUGIN_DESC(settings),
    NULL
};

static char** nfcd_enable_plugins = NULL;
static char** nfcd_disable_plugins = NULL;
static GLogProc nfcd_forward_log_func;
static FILE* nfcd_log_file = NULL;

typedef struct nfcd_opt {
    char* plugin_dir;
    gboolean dont_unload;
} NfcdOpt;

#define DEFAULT_PLUGIN_DIR "/usr/lib/nfcd/plugins"

#define RET_OK      (0)
#define RET_CMDLINE (1)
#define RET_ERR     (2)

static
gboolean
nfcd_signal(
    gpointer data)
{
    NfcManager* nfc = NFC_MANAGER(data);
    if (!nfc->stopped) {
        GINFO("Signal caught, shutting down...");
        nfc_manager_stop(nfc, 0);
    }
    return G_SOURCE_CONTINUE;
}

static
void
nfcd_stopped(
    NfcManager* manager,
    void* loop)
{
    g_main_loop_quit(loop);
}

static
int
nfcd_run(
    NfcdOpt* opts)
{
    int ret = RET_ERR;
    const NfcPluginsInfo plugins_info = {
        .builtins = nfcd_builtin_plugins,
        .plugin_dir = opts->plugin_dir ? opts->plugin_dir : DEFAULT_PLUGIN_DIR,
        .enable = (const char**)nfcd_enable_plugins,
        .disable = (const char**)nfcd_disable_plugins,
        .flags = opts->dont_unload ? NFC_PLUGINS_DONT_UNLOAD : 0
    };
    NfcManager* nfc = nfc_manager_new(&plugins_info);

    if (nfc_manager_start(nfc)) {
        if (!nfc->stopped) {
            GMainLoop* loop = g_main_loop_new(NULL, FALSE);
            guint sigterm = g_unix_signal_add(SIGTERM, nfcd_signal, nfc);
            guint sigint = g_unix_signal_add(SIGINT, nfcd_signal, nfc);
            gulong stop_id = nfc_manager_add_stopped_handler(nfc,
                nfcd_stopped, loop);

            g_main_loop_run(loop);

            nfc_manager_remove_handler(nfc, stop_id);
            nfc_manager_stop(nfc, 0);

            g_source_remove(sigterm);
            g_source_remove(sigint);
            g_main_loop_unref(loop);
        }
        ret = RET_OK;
    }
    nfc_manager_unref(nfc);
    return ret;
}

void
nfcd_log(
    const char* name,
    int level,
    const char* format,
    va_list va)
{
    if (nfcd_forward_log_func) {
        nfcd_forward_log_func(name, level, format, va);
    }
    if (nfcd_log_file) {
        if (gutil_log_timestamp) {
            char t[32];
            time_t now;
            time(&now);
            strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S", localtime(&now));
            fputs(t, nfcd_log_file);
            fputc(' ', nfcd_log_file);
        }
        if (name && name[0]) {
            fputc('[', nfcd_log_file);
            fputs(name, nfcd_log_file);
            fputs("] ", nfcd_log_file);
        }
        vfprintf(nfcd_log_file, format, va);
        fputs("\n", nfcd_log_file);
        fflush(nfcd_log_file);
    }
}

static
void
nfcd_opt_enable_disable_plugins(
    const gchar* value,
    char*** list,
    char*** alt_list)
{
    char** plugins = g_strsplit(value, ",", -1);
    char** ptr = plugins;

    while (*ptr) {
        const char* str = g_strstrip(*ptr++);

        if (*str) {
            if (!gutil_strv_contains(*list, str)) {
                *list = gutil_strv_add(*list, str);
            }
            *alt_list = gutil_strv_remove_at(*alt_list,
                gutil_strv_find(*alt_list, str), TRUE);
        }
    }
    g_strfreev(plugins);
}

static
gboolean
nfcd_opt_enable_plugins(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    nfcd_opt_enable_disable_plugins(value, &nfcd_enable_plugins,
        &nfcd_disable_plugins);
    return TRUE;
}

static
gboolean
nfcd_opt_disable_plugins(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    nfcd_opt_enable_disable_plugins(value, &nfcd_disable_plugins,
        &nfcd_enable_plugins);
    return TRUE;
}

static
gboolean
nfcd_opt_log_file(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    FILE* f = fopen(value, "w");
    if (f) {
        if (nfcd_log_file) fclose(nfcd_log_file);
        nfcd_log_file = f;
        if (!nfcd_forward_log_func) {
            nfcd_forward_log_func = gutil_log_func;
            gutil_log_func = nfcd_log;
        }
        return TRUE;
    } else {
        *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
            "Failed to open %s for writing", value);
        return FALSE;
    }
}

static
gboolean
nfcd_opt_debug(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    gutil_log_default.level = (gutil_log_default.level < GLOG_LEVEL_DEBUG) ?
        GLOG_LEVEL_DEBUG : GLOG_LEVEL_VERBOSE;
    return TRUE;
}

static
gboolean
nfcd_opt_parse(
    NfcdOpt* opt,
    int argc,
    char* argv[])
{
    GOptionEntry entries[] = {
        { "plugin-dir", 'p', 0, G_OPTION_ARG_FILENAME, &opt->plugin_dir,
          "Plugin directory [" DEFAULT_PLUGIN_DIR "]", "DIR" },
        { "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          nfcd_opt_debug, "Enable verbose log (repeat to increase verbosity)" },
        { "log-file", 'l', 0, G_OPTION_ARG_CALLBACK, nfcd_opt_log_file,
          "Write log to a file", "FILE"},
        { "enable", 'e', 0, G_OPTION_ARG_CALLBACK, nfcd_opt_enable_plugins,
          "Enable plugins (repeatable)", "PLUGINS"},
        { "disable", 'd', 0, G_OPTION_ARG_CALLBACK, nfcd_opt_disable_plugins,
          "Disable plugins (repeatable)", "PLUGINS"},
        { "dont-unload", 'U', 0, G_OPTION_ARG_NONE, &opt->dont_unload,
          "Don't unload external plugins on exit", NULL },
        { NULL }
    };
    GOptionContext* options = g_option_context_new("- NFC daemon");
    GError* error = NULL;
    gboolean ok;

    g_option_context_add_main_entries(options, entries, NULL);
    ok = g_option_context_parse(options, &argc, &argv, &error);
    if (!ok) {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
    }
    g_option_context_free(options);
    return ok;
}

static
void
nfcd_opt_init(
    NfcdOpt* opts)
{
    memset(opts, 0, sizeof(*opts));
}

static
void
nfcd_opt_cleanup(
    NfcdOpt* opts)
{
    if (nfcd_log_file) {
        fclose(nfcd_log_file);
    }
    g_free(opts->plugin_dir);
    g_strfreev(nfcd_enable_plugins);
    g_strfreev(nfcd_disable_plugins);
}

int main(int argc, char* argv[])
{
    int ret;
    NfcdOpt opt;

    gutil_log_default.name = "nfcd";
    setlocale(LC_ALL, "");
    nfcd_opt_init(&opt);
    if (nfcd_opt_parse(&opt, argc, argv)) {
        GINFO("Starting");
        ret = nfcd_run(&opt);
        GINFO("Exiting");
    } else {
        ret = RET_CMDLINE;
    }
    nfcd_opt_cleanup(&opt);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
