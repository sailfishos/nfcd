/*
 * Copyright (C) 2025 Slava Monich <slava@monich.com>
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

#include "org.sailfishos.nfc.Daemon.h"
#include "org.sailfishos.nfc.Adapter.h"

#include <gutil_log.h>
#include <gutil_misc.h>

#include <glib-unix.h>

#define NFC_BUS G_BUS_TYPE_SYSTEM
#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"

#define RET_OK (0)
#define RET_CMDLINE (1)
#define RET_ERR (2)

typedef enum app_flags {
    APP_NO_FLAGS = 0,
    APP_FLAG_RESET = 0x01,
    APP_FLAG_SET_T4_NDEF = 0x02,
    APP_FLAG_SET_LA_NFCID1 = 0x04
} APP_FLAGS;

#define APP_FLAG_SET_ANY (\
    APP_FLAG_RESET | \
    APP_FLAG_SET_T4_NDEF | \
    APP_FLAG_SET_LA_NFCID1)

typedef struct app_data {
    APP_FLAGS flags;
    gboolean t4_ndef;
    GUtilData* nfcid1;
    gboolean reset;
} AppData;

static
gboolean
app_quit_signal(
    gpointer loop)
{
    GDEBUG("Signal caught, shutting down...");
    g_main_loop_quit(loop);
    return G_SOURCE_CONTINUE;
}

static
void
app_param_changed(
    OrgSailfishosNfcAdapter* adapter,
    const gchar* name,
    GVariant* var,
    AppData* app)
{
    char* value = g_variant_print(var, TRUE);

    GDEBUG("%s => %s", name, value);
    g_free(value);
}

static
int
app_run_with_adapter(
    AppData* app,
    OrgSailfishosNfcAdapter* adapter)
{
    int ret = RET_ERR;
    GError* error = NULL;
    const char* path = g_dbus_proxy_get_object_path(G_DBUS_PROXY(adapter));
    const char* name = path + 1;

    if (app->flags & APP_FLAG_SET_ANY) {
        GVariantBuilder builder;
        guint id = 0;

        g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
        if (app->flags & APP_FLAG_SET_T4_NDEF) {
            g_variant_builder_add(&builder, "{sv}", "T4_NDEF",
                g_variant_new_boolean(app->t4_ndef));
        }
        if (app->flags & APP_FLAG_SET_LA_NFCID1) {
            const char* name = "LA_NFCID1";

            if (app->nfcid1 && app->nfcid1->size) {
                g_variant_builder_add(&builder, "{sv}", name,
                    g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                        app->nfcid1->bytes, app->nfcid1->size, 1));
            } else {
                g_variant_builder_add(&builder, "{sv}", name,
                    g_variant_new_from_data(G_VARIANT_TYPE_BYTESTRING,
                        NULL, 0, TRUE, NULL, NULL));
            }
        }
        if (org_sailfishos_nfc_adapter_call_request_params_sync(adapter,
            g_variant_builder_end(&builder), app->reset, &id, NULL, &error)) {
            GMainLoop* loop = g_main_loop_new(NULL, FALSE);
            guint sigterm = g_unix_signal_add(SIGTERM, app_quit_signal, loop);
            guint sigint = g_unix_signal_add(SIGINT, app_quit_signal, loop);
            gulong signal_id = g_signal_connect(adapter, "param-changed",
                G_CALLBACK(app_param_changed), app);

            GDEBUG("Request id %u", id);
            g_main_loop_run(loop);
            g_signal_handler_disconnect(adapter, signal_id);
            g_source_remove(sigterm);
            g_source_remove(sigint);
            g_main_loop_unref(loop);
        } else {
            GERR("%s: %s", name, GERRMSG(error));
            g_error_free(error);
        }
    } else {
        GVariant* params = NULL;

        /* Print the values */
        if (org_sailfishos_nfc_adapter_call_get_params_sync(adapter, &params,
            NULL, &error)) {
            GVariantIter it;
            GVariant* param;

            GINFO("%s:", name);
            g_variant_iter_init(&it, params);
            while ((param = g_variant_iter_next_value(&it)) != NULL) {
                if (g_variant_n_children(param) == 2) {
                    GVariant* s = g_variant_get_child_value(param, 0);
                    GVariant* v = g_variant_get_child_value(param, 1);
                    char* value = g_variant_print(v, TRUE);

                    GINFO("%s = %s", g_variant_get_string(s, NULL), value);
                    g_variant_unref(s);
                    g_variant_unref(v);
                    g_free(value);
                }
                g_variant_unref(param);
            }
            g_variant_unref(params);
        } else {
            GERR("%s: %s", name, GERRMSG(error));
            g_error_free(error);
        }
    }
    return ret;
}

static
int
app_run_with_adapter_path(
    AppData* app,
    const char* path)
{
    int ret = RET_ERR;
    GError* error = NULL;
    OrgSailfishosNfcAdapter* adapter =
        org_sailfishos_nfc_adapter_proxy_new_for_bus_sync(NFC_BUS,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
            path, NULL, &error);

    GDEBUG("NFC adapter %s", path);
    if (adapter) {
        ret = app_run_with_adapter(app, adapter);
        g_object_unref(adapter);
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
app_run(
    AppData* app)
{
    int ret = RET_ERR;
    GError* error = NULL;
    OrgSailfishosNfcDaemon* daemon =
        org_sailfishos_nfc_daemon_proxy_new_for_bus_sync(NFC_BUS,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
            NFC_DAEMON_PATH, NULL, &error);

    if (daemon) {
        char** adapters = NULL;

        if (org_sailfishos_nfc_daemon_call_get_adapters_sync(daemon,
            &adapters, NULL, &error)) {
            if (adapters && adapters[0]) {
                ret = app_run_with_adapter_path(app, adapters[0]);
            } else {
                GERR("No NFC adapters found.");
            }
            g_strfreev(adapters);
        }
        g_object_unref(daemon);
    }

    if (error) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }

    return ret;
}

static
gboolean
opt_t4_ndef(
    const char* name,
    const char* value,
    gpointer data,
    GError** error)
{
    AppData* app = data;

    if (value) {
        if (!g_ascii_strcasecmp(value, "on") ||
            !g_ascii_strcasecmp(value, "true")) {
            app->t4_ndef = TRUE;
        } else if (!g_ascii_strcasecmp(value, "off") ||
            !g_ascii_strcasecmp(value, "false")) {
            app->t4_ndef = FALSE;
        } else {
            g_propagate_error(error, g_error_new(G_OPTION_ERROR,
                G_OPTION_ERROR_BAD_VALUE, "Invalid option '%s'", value));
            return FALSE;
        }
    } else {
        app->t4_ndef = TRUE;
    }
    app->flags |= APP_FLAG_SET_T4_NDEF;
    return TRUE;
}

static
gboolean
opt_nfcid1(
    const char* name,
    const char* value,
    gpointer data,
    GError** error)
{
    AppData* app = data;

    if (value) {
        GBytes* nfcid1 = gutil_hex2bytes(value, -1);

        if (nfcid1) {
            gsize size;
            const void* bytes = g_bytes_get_data(nfcid1, &size);

            g_free(app->nfcid1);
            app->nfcid1 = gutil_data_new(bytes, size);
            g_bytes_unref(nfcid1);
        } else {
            g_propagate_error(error, g_error_new(G_OPTION_ERROR,
                G_OPTION_ERROR_BAD_VALUE, "Invalid hex data '%s'", value));
            return FALSE;
        }
    } else {
        g_free(app->nfcid1);
        app->nfcid1 = gutil_data_new(NULL, 0);
    }
    app->flags |= APP_FLAG_SET_LA_NFCID1;
    return TRUE;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean verbose = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "t4-ndef", 0, G_OPTION_FLAG_OPTIONAL_ARG,
           G_OPTION_ARG_CALLBACK, &opt_t4_ndef,
          "Request NDEF from Type4 tags", "[on|off]" },
        { "nfcid1", 0, G_OPTION_FLAG_OPTIONAL_ARG,
           G_OPTION_ARG_CALLBACK, &opt_nfcid1,
          "NFCID1 in NFC-A Listen mode", "hex" },
        { NULL }
    };
    AppData app;
    GOptionContext* opts = g_option_context_new(NULL);
    GOptionGroup* group = g_option_group_new("", "", "", &app, NULL);
    GError* error = NULL;

    memset(&app, 0, sizeof(app));
    g_option_group_add_entries(group, entries);
    g_option_context_set_main_group(opts, group);
    g_option_context_set_summary(opts, "Tests NFC adapter parameter API.");
    if (g_option_context_parse(opts, &argc, &argv, &error) && argc == 1) {

        gutil_log_timestamp = FALSE;
        gutil_log_default.level = verbose ?
            GLOG_LEVEL_VERBOSE :
            GLOG_LEVEL_INFO;

        ret = app_run(&app);
    } else {
        ret = RET_CMDLINE;
        if (error) {
            fprintf(stderr, "%s\n", GERRMSG(error));
            g_error_free(error);
        } else {
            char* help = g_option_context_get_help(opts, TRUE, NULL);

            printf("%s", help);
            g_free(help);
        }
    }
    g_free(app.nfcid1);
    g_option_context_free(opts);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
