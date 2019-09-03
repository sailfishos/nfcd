/*
 * Copyright (C) 2019 Jolla Ltd.
 * Copyright (C) 2019 Slava Monich <slava.monich@jolla.com>
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
#include "org.sailfishos.nfc.IsoDep.h"

#include <gutil_misc.h>
#include <gutil_strv.h>
#include <gutil_log.h>

#include <glib-unix.h>

#define NFC_BUS G_BUS_TYPE_SYSTEM
#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"
#define NFC_DBUS_ISODEP_INTERFACE "org.sailfishos.nfc.IsoDep"

#define RET_ERR (-1)
#define RET_IOERR (0)

typedef struct app_data {
    char** tags;
    guint cla;
    guint ins;
    guint p1;
    guint p2;
    GBytes* data;
    guint le;
    GMainLoop* loop;
    gboolean stopped;
} AppData;

static
gboolean
isodep_signal(
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
void
isodep_debug_hexdump(
    const void* data,
    gsize len)
{
    const guint8* ptr = data;
    guint off = 0;

    while (len > 0) {
        char buf[GUTIL_HEXDUMP_BUFSIZE];
        const guint consumed = gutil_hexdump(buf, ptr + off, len);

        GDEBUG("  %04X: %s", off, buf);
        len -= consumed;
        off += consumed;
    }
}

static
gboolean
isodep_tags_changed(
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
isodep_submit(
    AppData* app,
    OrgSailfishosNfcIsoDep* isodep)
{
    int ret = RET_ERR;
    GError* error = NULL;
    GVariant* result = NULL;
    guchar sw1, sw2;
    const void* data;
    gsize len;

    if (app->data) {
        data = g_bytes_get_data(app->data, &len);
    } else {
        data = &data;
        len = 0;
    }

    if (org_sailfishos_nfc_iso_dep_call_transmit_sync(isodep, app->cla,
        app->ins, app->p1, app->p2,
        g_variant_new_from_data(G_VARIANT_TYPE("ay"), data, len,
        TRUE, NULL, NULL), app->le, &result, &sw1, &sw2, NULL, &error)) {
        gsize n = 0;
        const guint8* data = g_variant_get_fixed_array(result, &n, 1);

        printf("SW: %02X%02X\n", sw1, sw2);
        if (n) {
            printf("Data: %u byte(s)\n", (guint)n);
        }
        isodep_debug_hexdump(data, n);
        g_variant_unref(result);
        ret = (((guint)sw1) << 8) | sw2;
    } else {
        GERR("%s: %s", g_dbus_proxy_get_object_path(G_DBUS_PROXY(isodep)),
            GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
isodep_tag(
    AppData* app,
    OrgSailfishosNfcTag* tag)
{
    int ret = RET_ERR;
    GError* error = NULL;
    char** ifaces = NULL;
    const char* path = g_dbus_proxy_get_object_path(G_DBUS_PROXY(tag));

    if (org_sailfishos_nfc_tag_call_get_interfaces_sync(tag, &ifaces,
        NULL, &error)) {
        if (gutil_strv_find(ifaces, NFC_DBUS_ISODEP_INTERFACE) >= 0) {
            OrgSailfishosNfcIsoDep* isodep =
                org_sailfishos_nfc_iso_dep_proxy_new_for_bus_sync(NFC_BUS,
                    G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
                    path, NULL, &error);

            if (isodep) {
                ret = isodep_submit(app, isodep);
                g_object_unref(isodep);
            } else {
                GERR("%s: %s", path, GERRMSG(error));
                g_error_free(error);
            }
        } else {
            printf("Not an ISO-DEP tag.\n");
        }
        g_strfreev(ifaces);
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
isodep_adapter(
    AppData* app,
    OrgSailfishosNfcAdapter* adapter)
{
    int ret = RET_ERR;
    GError* error = NULL;
    gulong signal_id = g_signal_connect(adapter, "tags-changed",
        G_CALLBACK(isodep_tags_changed), app);

    if (org_sailfishos_nfc_adapter_call_get_tags_sync(adapter, &app->tags,
        NULL, &error)) {
        if (!app->tags || !app->tags[0]) {
            g_strfreev(app->tags);
            guint sigterm = g_unix_signal_add(SIGTERM, isodep_signal, app);
            guint sigint = g_unix_signal_add(SIGINT, isodep_signal, app);

            app->tags = NULL;
            GINFO("Waiting for ISO-DEP tag...");
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
                ret = isodep_tag(app, tag);
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
isodep_adapter_path(
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
        ret = isodep_adapter(app, adapter);
        g_object_unref(adapter);
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
isodep_run(
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
                ret = isodep_adapter_path(app, adapters[0]);
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
isodep_parse_hex_word(
    const char* str,
    guint* out)
{
    /* Length 1 or 2, 3 or 4 is expected */
    if (str[0]) {
        const gsize len = strlen(str);

        if (len < 5) {
            char buf[5];
            guint16 result;

            /* Pad with zeros if necessary */
            if (len < 4) {
                guint i = 0;

                switch (len) {
                case 3: buf[i++] = '0';
                case 2: buf[i++] = '0';
                case 1: buf[i++] = '0';
                default: buf[i] = 0;
                }
                strcpy(buf + i, str);
                str = buf;
            }

            if (gutil_hex2bin(str, 4, &result)) {
                *out = GINT16_FROM_BE(result);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static
gboolean
isodep_parse_hex_byte(
    const char* str,
    guint* out)
{
    /* Length 1 or 2 is expected */
    if (str[0] && (!str[1] || !str[2])) {
        char buf[3];
        guint8 result;

        /* Pad with zero if necessary */
        if (!str[1]) {
            buf[0] = '0';
            buf[1] = str[0];
            buf[2] = 0;
            str = buf;
        }

        if (gutil_hex2bin(str, 2, &result)) {
            *out = result;
            return TRUE;
        }
    }
    return FALSE;
}

static
gboolean
isodep_parse_args(
    AppData* app,
    int argc,
    char** argv)
{
    /* CLA ISO P1 P2 [DATA [LE]] */
    if (argc >= 5 && argc <= 7 &&
        isodep_parse_hex_byte(argv[1], &app->cla) &&
        isodep_parse_hex_byte(argv[2], &app->ins) &&
        isodep_parse_hex_byte(argv[3], &app->p1) &&
        isodep_parse_hex_byte(argv[4], &app->p2)) {
        if (argc == 5) {
            return TRUE;
        } else {
            const char* data = argv[5];

            app->data = gutil_hex2bytes(data, -1);
            if (argc == 6) {
                if (app->data) {
                    return TRUE;
                }
            } else if (app->data || !data[0]) {
                const char* le = argv[6];

                if (!strcmp(le, "00")) {
                    app->le = 0x100;
                    return TRUE;
                } else if (!strcmp(le, "0000")) {
                    app->le = 0x10000;
                    return TRUE;
                } else if (isodep_parse_hex_word(argv[6], &app->le)) {
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
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
    GOptionContext* opts = g_option_context_new("CLA INS P1 P2 [DATA [LE]]");
    GError* error = NULL;

    g_option_context_add_main_entries(opts, entries, NULL);
    g_option_context_set_summary(opts, "Sends APDU via ISO-DEP protocol.");
    if (g_option_context_parse(opts, &argc, &argv, &error)) {
        AppData app;

        gutil_log_timestamp = FALSE;
        gutil_log_default.level = verbose ?
            GLOG_LEVEL_VERBOSE :
            GLOG_LEVEL_INFO;

        memset(&app, 0, sizeof(app));
        if (!isodep_parse_args(&app, argc, argv)) {
            char* help = g_option_context_get_help(opts, TRUE, NULL);

            printf("%s", help);
            g_free(help);
        } else {
            ret = isodep_run(&app);
        }
        if (app.data) {
            g_bytes_unref(app.data);
        }
        g_strfreev(app.tags);
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
