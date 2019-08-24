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

#include "org.sailfishos.nfc.Daemon.h"
#include "org.sailfishos.nfc.Adapter.h"
#include "org.sailfishos.nfc.Tag.h"
#include "org.sailfishos.nfc.NDEF.h"

#include <gutil_misc.h>
#include <gutil_log.h>

#include <glib-unix.h>

#define NFC_BUS G_BUS_TYPE_SYSTEM
#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"

#define RET_OK (0)
#define RET_ERR (1)

typedef struct app_data {
    char** tags;
    const char* fname;
    GMainLoop* loop;
    gboolean stopped;
} AppData;

static
gboolean
read_ndef_signal(
    gpointer user_data)
{
    AppData* app = user_data;

    if (!app->stopped) {
        app->stopped = TRUE;
        GDEBUG("Signal caught, shutting down...");
        g_main_loop_quit(app->loop);
    }
    return G_SOURCE_CONTINUE;
}

static
void
read_ndef_hexdump(
    const guint8* data,
    gsize len)
{
    const guint8* ptr = data;
    guint off = 0;

    while (len > 0) {
        char buf[GUTIL_HEXDUMP_BUFSIZE];
        const guint consumed = gutil_hexdump(buf, ptr + off, len);

        printf("  %04X: %s\n", off, buf);
        len -= consumed;
        off += consumed;
    }
}

static
int
read_ndef_from_path(
    AppData* app,
    const char* path,
    int index,
    FILE* out)
{
    int ret = RET_ERR;
    GError* error = NULL;
    OrgSailfishosNfcNDEF* ndef =
        org_sailfishos_nfc_ndef_proxy_new_for_bus_sync(NFC_BUS,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
            path, NULL, &error);

    GDEBUG("NDEF record %s", path);
    if (ndef) {
        GVariant* raw_data = NULL;

        if (org_sailfishos_nfc_ndef_call_get_raw_data_sync(ndef, &raw_data,
            NULL, &error)) {
            gsize n = 0;
            const guint8* data = g_variant_get_fixed_array(raw_data, &n, 1);

            if (out) {
                if (fwrite(data, 1, n, out) != n) {
                    GERR("Failed to write data to file");
                }
            }

            if (index >= 0) {
                printf("NDEF #%d:\n", index);
                read_ndef_hexdump(data, n);
            } else {
                if (n > 0) {
                    read_ndef_hexdump(data, n);
                } else {
                    printf("Empty NDEF record.\n");
                }
            }
            g_variant_unref(raw_data);
        } else {
            GERR("%s: %s", path, GERRMSG(error));
            g_error_free(error);
        }
        g_object_unref(ndef);
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
read_ndef_from_tag(
    AppData* app,
    OrgSailfishosNfcTag* tag)
{
    int ret = RET_ERR;
    GError* error = NULL;
    char** ndefs = NULL;

    if (org_sailfishos_nfc_tag_call_get_ndef_records_sync(tag, &ndefs,
        NULL, &error)) {
        if (ndefs && ndefs[0]) {
            char* const* ptr;
            int i, n = g_strv_length(ndefs);
            FILE* f = NULL;

            if (app->fname) {
                f = fopen(app->fname, "w");
                if (f) {
                    GDEBUG("Writing %s", app->fname);
                } else {
                    GERR("Failed to open %s for writing", app->fname);
                }
            }

            for (i = 0, ptr = ndefs; *ptr; ptr++, i++) {
                read_ndef_from_path(app, *ptr, (n > 1) ? i : -1, f);
            }

            if (f) {
                printf("Wrote %s\n", app->fname);
                fclose(f);
            }
        } else {
            printf("No NDEF records found.\n");
        }
        g_strfreev(ndefs);
    } else {
        GERR("%s: %s", g_dbus_proxy_get_object_path(G_DBUS_PROXY(tag)),
            GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
gboolean
read_ndef_tags_changed(
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
read_ndef_from_adapter(
    AppData* app,
    OrgSailfishosNfcAdapter* adapter)
{
    int ret = RET_ERR;
    GError* error = NULL;
    gulong signal_id = g_signal_connect(adapter, "tags-changed",
        G_CALLBACK(read_ndef_tags_changed), app);

    if (org_sailfishos_nfc_adapter_call_get_tags_sync(adapter, &app->tags,
        NULL, &error)) {
        if (!app->tags || !app->tags[0]) {
            g_strfreev(app->tags);
            guint sigterm = g_unix_signal_add(SIGTERM, read_ndef_signal, app);
            guint sigint = g_unix_signal_add(SIGINT, read_ndef_signal, app);

            app->tags = NULL;
            GINFO("Waiting for tag...");
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
                ret = read_ndef_from_tag(app, tag);
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
read_ndef_from_adapter_path(
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
        ret = read_ndef_from_adapter(app, adapter);
        g_object_unref(adapter);
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
read_ndef(
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
                ret = read_ndef_from_adapter_path(app, adapters[0]);
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

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean verbose = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { NULL }
    };
    GOptionContext* options = g_option_context_new("[FILE]");
    GError* error = NULL;

    g_option_context_add_main_entries(options, entries, NULL);
    g_option_context_set_summary(options, "Reads NDEF record from a tag "
        "and optionally saves it to file.");
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if (argc > 2) {
            char* help = g_option_context_get_help(options, TRUE, NULL);
            gsize len = strlen(help);

            while (len > 0 && help[len - 1] == '\n') help[len--] = 0;
            printf("%s\n", help);
            g_free(help);
        } else {
            AppData app;

            gutil_log_timestamp = FALSE;
            gutil_log_default.level = verbose ?
                GLOG_LEVEL_VERBOSE :
                GLOG_LEVEL_INFO;

            memset(&app, 0, sizeof(app));
            if (argc > 1) {
                app.fname = argv[1];
            }
            ret = read_ndef(&app);
            g_strfreev(app.tags);
        }
    } else {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
    }
    g_option_context_free(options);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
