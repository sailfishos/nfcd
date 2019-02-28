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
#include "org.sailfishos.nfc.TagType2.h"

#include "nfc_ndef.h"

#include <gutil_misc.h>
#include <gutil_strv.h>
#include <gutil_log.h>

#include <glib-unix.h>

#define NFC_BUS G_BUS_TYPE_SYSTEM
#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"
#define NFC_DBUS_TAG_T2_INTERFACE "org.sailfishos.nfc.TagType2"

#define RET_OK (0)
#define RET_ERR (1)

typedef struct app_data {
    char** tags;
    GUtilData ndef;
    GMainLoop* loop;
    gboolean stopped;
} AppData;

static
gboolean
write_ndef_signal(
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
write_ndef_debug_hexdump(
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
guint
write_ndef_data_diff(
    const guint8* data1,
    const guint8* data2,
    gsize size)
{
    while (size > 0 && data1[size - 1] == data2[size - 1]) {
        size--;
    }
    return size;
}

static
int
write_ndef_to_type2_tag(
    AppData* app,
    OrgSailfishosNfcTagType2* t2)
{
    int ret = RET_ERR;
    GError* error = NULL;
    GVariant* contents = NULL;

    if (org_sailfishos_nfc_tag_type2_call_read_all_data_sync(t2, &contents,
        NULL, &error)) {
        gsize size = 0;
        const void* read_data = g_variant_get_fixed_array(contents, &size, 1);

        GDEBUG("Read %u bytes:", (guint)size);
        write_ndef_debug_hexdump(read_data, size);
        if (size > 0) {
            const GUtilData* ndef = &app->ndef;
            guint tlv_size;

            /* Add space for type, length (up to 3 bytes) and terminator */
            tlv_size = ndef->size + 3;
            if (ndef->size >= 0xff) {
                tlv_size += 2; /* Will use three consecutive bytes format */
            }
            if (size >= tlv_size) {
                guint8* data = g_malloc(size);
                guint written = 0;
                guint bytes_to_write;
                guint i = 0;

                /* Build TLV block */
                data[i++] = 0x03; /* NDEF Message */
                if (ndef->size < 0xff) {
                    /* One byte format */
                    data[i++] = (guint8)ndef->size;
                } else {
                    /* Three consecutive bytes format */
                    data[i++] = 0xff;
                    data[i++] = (guint8)(ndef->size >> 8);
                    data[i++] = (guint8)ndef->size;
                }
                memcpy(data + i, ndef->bytes, ndef->size);
                i += ndef->size;
                data[i++] = 0xfe; /* Terminator */
                memset(data + i, 0, size - i);
                bytes_to_write = write_ndef_data_diff(data, read_data, size);

                if (!bytes_to_write) {
                    printf("Nothing to write.\n");
                } else {
                    GDEBUG("Writing %u bytes:", bytes_to_write);
                    write_ndef_debug_hexdump(data, size);
                    if (org_sailfishos_nfc_tag_type2_call_write_data_sync(t2,
                        0, g_variant_new_from_data(G_VARIANT_TYPE("ay"),
                        data, bytes_to_write, TRUE, g_free, data), &written,
                        NULL, &error)) {
                        printf("%u bytes written.\n", written);
                        ret = RET_OK;
                    } else {
                        GERR("%s: %s",
                            g_dbus_proxy_get_object_path(G_DBUS_PROXY(t2)),
                            GERRMSG(error));
                        g_error_free(error);
                    }
                }
                g_free(data);
            } else {
                GERR("%s: NDEF is too big (%u > %u)",
                    g_dbus_proxy_get_object_path(G_DBUS_PROXY(t2)),
                    tlv_size, (guint)size);
            }
        } else {
            GERR("%s: no data was read, giving up",
                 g_dbus_proxy_get_object_path(G_DBUS_PROXY(t2)));
        }
        g_variant_unref(contents);
    } else {
        GERR("%s: %s", g_dbus_proxy_get_object_path(G_DBUS_PROXY(t2)),
            GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
write_ndef_to_tag(
    AppData* app,
    OrgSailfishosNfcTag* tag)
{
    int ret = RET_ERR;
    GError* error = NULL;
    char** ifaces = NULL;
    const char* path = g_dbus_proxy_get_object_path(G_DBUS_PROXY(tag));

    if (org_sailfishos_nfc_tag_call_get_interfaces_sync(tag, &ifaces,
        NULL, &error)) {
        if (gutil_strv_find(ifaces, NFC_DBUS_TAG_T2_INTERFACE) >= 0) {
            OrgSailfishosNfcTagType2* t2 =
                org_sailfishos_nfc_tag_type2_proxy_new_for_bus_sync(NFC_BUS,
                    G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
                    path, NULL, &error);

            GDEBUG("Type 2 tag %s", path);
            if (t2) {
                ret = write_ndef_to_type2_tag(app, t2);
                g_object_unref(t2);
            } else {
                GERR("%s: %s", path, GERRMSG(error));
                g_error_free(error);
            }
        } else {
            printf("Not a Type 2 tag.\n");
        }
        g_strfreev(ifaces);
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
gboolean
write_ndef_tags_changed(
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
write_ndef_to_adapter(
    AppData* app,
    OrgSailfishosNfcAdapter* adapter)
{
    int ret = RET_ERR;
    GError* error = NULL;
    gulong signal_id = g_signal_connect(adapter, "tags-changed",
        G_CALLBACK(write_ndef_tags_changed), app);

    if (org_sailfishos_nfc_adapter_call_get_tags_sync(adapter, &app->tags,
        NULL, &error)) {
        if (!app->tags || !app->tags[0]) {
            g_strfreev(app->tags);
            guint sigterm = g_unix_signal_add(SIGTERM, write_ndef_signal, app);
            guint sigint = g_unix_signal_add(SIGINT, write_ndef_signal, app);

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
                ret = write_ndef_to_tag(app, tag);
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
write_ndef_to_adapter_path(
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
        ret = write_ndef_to_adapter(app, adapter);
        g_object_unref(adapter);
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
write_ndef(
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
                ret = write_ndef_to_adapter_path(app, adapters[0]);
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
    char* uri = NULL;
    char* text = NULL;
    gboolean verbose = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "uri", 'u', 0, G_OPTION_ARG_STRING, &uri,
          "Write URI record", "URI" },
        { "text", 't', 0, G_OPTION_ARG_STRING, &text,
          "Write Text record", "TEXT" },
        { NULL }
    };
    GOptionContext* options = g_option_context_new("[FILE]");
    GError* error = NULL;

    g_option_context_add_main_entries(options, entries, NULL);
    g_option_context_set_summary(options, "Writes NDEF record to tag.");
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if ((!uri && !text && argc != 2) || (uri && text)) {
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
            if (uri || text) {
                NfcNdefRec* rec = NULL;

                if (uri) {
                    NfcNdefRecU* u = nfc_ndef_rec_u_new(uri);

                    if (u) {
                        rec = &u->rec;
                    }
                } else {
                    NfcNdefRecT* t = nfc_ndef_rec_t_new(text, NULL);

                    if (t) {
                        rec = &t->rec;
                    }
                }

                if (rec) {
                    app.ndef = rec->raw;
                }

                ret = write_ndef(&app);
                nfc_ndef_rec_unref(rec);
            } else {
                const char* file = (argc > 1) ? argv[1] : NULL;
                gchar* contents;
                gsize size;

                if (g_file_get_contents(file, &contents, &size, &error)) {
                    if (size > 0xffff) {
                        fprintf(stderr, "File too big (%u bytes)\n",
                            (guint)size);
                    } else {
                        app.ndef.bytes = (void*)contents;
                        app.ndef.size = size;
                        ret = write_ndef(&app);
                    }
                    g_free(contents);
                } else {
                    fprintf(stderr, "%s\n", GERRMSG(error));
                    g_error_free(error);
                }
            }
            g_strfreev(app.tags);
        }
    } else {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
    }
    g_free(uri);
    g_free(text);
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
