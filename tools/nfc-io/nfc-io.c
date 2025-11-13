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
#include "org.sailfishos.nfc.Tag.h"

#include <gutil_log.h>
#include <gutil_misc.h>

#include <glib-unix.h>

#define NFC_BUS G_BUS_TYPE_SYSTEM
#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"

#define RET_OK (0)
#define RET_ERR (1)

typedef struct app_data {
    char** tags;
    GBytes* data;
    GMainLoop* loop;
    gboolean stopped;
} AppData;

static
gboolean
nfcio_signal(
    gpointer user_data)
{
    AppData* app = user_data;

    if (!app->stopped) {
        GDEBUG("Signal caught, shutting down...");
        g_main_loop_quit(app->loop);
    }
    return G_SOURCE_CONTINUE;
}

static
gboolean
nfcio_tags_changed(
    OrgSailfishosNfcAdapter* adapter,
    const gchar* const* tags,
    AppData* app)
{
    if (tags[0]) {
        if (!app->tags) {
            app->tags = g_strdupv((char**)tags);
        }
        GDEBUG("Tag detected");
        g_main_loop_quit(app->loop);
    }
    return TRUE;
}

static
int
nfcio_tag(
    AppData* app,
    OrgSailfishosNfcTag* tag)
{
    int ret = RET_ERR;
    GError* error = NULL;
    int version = 0;
    const char* path = g_dbus_proxy_get_object_path(G_DBUS_PROXY(tag));

    if (org_sailfishos_nfc_tag_call_get_interface_version_sync(tag,
        &version, NULL, &error)) {
        /* Transceive appeared in org.sailfishos.nfc.Tag v4 */
        if (version >= 4) {
            GVariant* resp = NULL;

            GDEBUG("Sending %u byte(s)", (guint)g_bytes_get_size(app->data));
            GDEBUG_DUMP_BYTES(app->data);
            if (org_sailfishos_nfc_tag_call_transceive_sync(tag,
                g_variant_new_from_bytes(G_VARIANT_TYPE_BYTESTRING,
                app->data, TRUE), &resp, NULL, &error)) {
                gsize size = 0;
                const guint8* data = g_variant_get_fixed_array(resp, &size, 1);

                if (size > 0) {
                    char* hex = gutil_bin2hex(data, size, TRUE);

                    GDEBUG("Received %u byte(s)", (guint) size);
                    GDEBUG_DUMP(data, size);
                    printf("%s\n", hex);
                    g_free(hex);
                }
                g_variant_unref(resp);
            } else {
                GERR("%s: %s", path, GERRMSG(error));
                g_error_free(error);
            }
        } else {
            GERR("Transceive is not supported by this version of nfcd");
        }
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
nfcio_adapter(
    AppData* app,
    OrgSailfishosNfcAdapter* adapter)
{
    int ret = RET_ERR;
    GError* error = NULL;
    gulong signal_id = g_signal_connect(adapter, "tags-changed",
        G_CALLBACK(nfcio_tags_changed), app);

    if (org_sailfishos_nfc_adapter_call_get_tags_sync(adapter, &app->tags,
        NULL, &error)) {
        if (!app->tags || !app->tags[0]) {
            guint sigterm = g_unix_signal_add(SIGTERM, nfcio_signal, app);
            guint sigint = g_unix_signal_add(SIGINT, nfcio_signal, app);

            g_strfreev(app->tags);
            app->tags = NULL;
            GINFO("Waiting for NFC tag...");
            app->loop = g_main_loop_new(NULL, FALSE);
            g_main_loop_run(app->loop);
            g_source_remove(sigterm);
            g_source_remove(sigint);
            g_main_loop_unref(app->loop);
            app->loop = NULL;
        }

        /* Don't need the signal anymore */
        g_signal_handler_disconnect(adapter, signal_id);
        signal_id = 0;

        if (app->tags && app->tags[0]) {
            const char* path = app->tags[0];
            OrgSailfishosNfcTag* tag =
                org_sailfishos_nfc_tag_proxy_new_for_bus_sync(NFC_BUS,
                    G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
                    path, NULL, &error);

            GDEBUG("Tag %s", path);
            if (tag) {
                ret = nfcio_tag(app, tag);
                g_object_unref(tag);
            } else {
                GERR("%s: %s", path, GERRMSG(error));
                g_error_free(error);
            }
        } else {
            GINFO("Giving up...");
        }
    } else {
        GERR("%s: %s", g_dbus_proxy_get_object_path(G_DBUS_PROXY(adapter)),
            GERRMSG(error));
        g_error_free(error);
    }
    if (signal_id) {
        g_signal_handler_disconnect(adapter, signal_id);
    }
    return ret;
}

static
int
nfcio_run(
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

        if (org_sailfishos_nfc_daemon_call_get_adapters_sync(daemon, &adapters,
            NULL, &error)) {
            if (adapters && adapters[0]) {
                const char* path = adapters[0];
                OrgSailfishosNfcAdapter* adapter =
                    org_sailfishos_nfc_adapter_proxy_new_for_bus_sync(NFC_BUS,
                        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
                        path, NULL, &error);

                GDEBUG("NFC adapter %s", path);
                if (adapter) {
                    ret = nfcio_adapter(app, adapter);
                    g_object_unref(adapter);
                } else {
                    GERR("%s: %s", path, GERRMSG(error));
                    g_error_free(error);
                }
            } else {
                GERR("No NFC adapters found.");
            }
            g_strfreev(adapters);
        } else {
            GERR("%s", GERRMSG(error));
            g_error_free(error);
        }
        g_object_unref(daemon);
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
gboolean
nfcio_opt_verbose(
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
nfcio_opt_quiet(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    gutil_log_default.level = GLOG_LEVEL_NONE;
    return TRUE;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    GOptionEntry entries[] = {
        { "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          nfcio_opt_verbose, "Enable verbose output" },
        { "quiet", 'q', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          nfcio_opt_quiet, "Be quiet" },
        { NULL }
    };
    GOptionContext* opts = g_option_context_new("DATA");
    GError* error = NULL;

    gutil_log_timestamp = FALSE;
    g_option_context_add_main_entries(opts, entries, NULL);
    g_option_context_set_summary(opts, "Exchanges raw data with an NFC tag.\n\n"
        "The data are parsed and printed as hex.");
    if (g_option_context_parse(opts, &argc, &argv, &error)) {
        AppData app;

        memset(&app, 0, sizeof(app));
        if (argc == 2 && (app.data = gutil_hex2bytes(argv[1], -1))) {
            ret = nfcio_run(&app);
            g_bytes_unref(app.data);
            g_strfreev(app.tags);
        } else {
            char* help = g_option_context_get_help(opts, TRUE, NULL);

            fprintf(stderr, "%s", help);
            g_free(help);
        }
    } else {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
    }
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
