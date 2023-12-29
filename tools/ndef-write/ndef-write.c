/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
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

#include "org.sailfishos.nfc.Daemon.h"
#include "org.sailfishos.nfc.Adapter.h"
#include "org.sailfishos.nfc.Tag.h"
#include "org.sailfishos.nfc.TagType2.h"

#include <nfcdef.h>

#include <gutil_misc.h>
#include <gutil_strv.h>
#include <gutil_log.h>

#include <glib-unix.h>

#include <magic.h>

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

typedef struct app_opts {
    char* media_type;
    gboolean media_type_rec;
} AppOpts;

static
gboolean
write_ndef_signal(
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
                    g_free(data);
                } else {
                    GVariant* data_variant = g_variant_ref_sink
                        (g_variant_new_from_data(G_VARIANT_TYPE("ay"),
                            data, bytes_to_write, TRUE, g_free, data));
                    GDEBUG("Writing %u bytes:", bytes_to_write);
                    write_ndef_debug_hexdump(data, size);
                    if (org_sailfishos_nfc_tag_type2_call_write_data_sync(t2,
                        0, data_variant, &written, NULL, &error)) {
                        printf("%u bytes written.\n", written);
                        ret = RET_OK;
                    } else {
                        GERR("%s: %s",
                            g_dbus_proxy_get_object_path(G_DBUS_PROXY(t2)),
                            GERRMSG(error));
                        g_error_free(error);
                    }
                    g_variant_unref(data_variant);
                }
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

static
NdefRec*
ndef_uri_proc(
    const char* uri,
    const GUtilData* data)
{
    NdefRecU* u = ndef_rec_u_new(uri);

    return u ? &u->rec : NULL;
}

static
NdefRec*
ndef_text_proc(
    const char* text,
    const GUtilData* data)
{
    NdefRecT* t = ndef_rec_t_new(text, NULL);

    return t ? &t->rec : NULL;
}

static
NdefRec*
ndef_sp_proc(
    const char* spec,
    const GUtilData* data)
{
    NdefRec* rec = NULL;
    const char* ptr = spec;
    gboolean backslash = FALSE;
    GStrV* params = NULL;
    GString* buf = g_string_new("");
    guint n;

    while (*ptr) {
        if (backslash) {
            backslash = FALSE;
            switch (*ptr) {
            case 'a': g_string_append_c(buf, '\a'); break;
            case 'b': g_string_append_c(buf, '\b'); break;
            case 'e': g_string_append_c(buf, '\e'); break;
            case 'f': g_string_append_c(buf, '\f'); break;
            case 'n': g_string_append_c(buf, '\n'); break;
            case 'r': g_string_append_c(buf, '\r'); break;
            case 't': g_string_append_c(buf, '\t'); break;
            case 'v': g_string_append_c(buf, '\v'); break;
            case '\\': g_string_append_c(buf, '\\'); break;
            case '\'': g_string_append_c(buf, '\''); break;
            case '"': g_string_append_c(buf, '\"'); break;
            case '?': g_string_append_c(buf, '\?'); break;
            case ',': g_string_append_c(buf, ','); break;
            default:
                /* Could support more but is it worth the trouble? */
                g_string_append_c(buf, '\\');
                g_string_append_c(buf, *ptr);
                break;
            }
        } else if (*ptr == '\\') {
            backslash = TRUE;
        } else if (*ptr == ',') {
            params = gutil_strv_add(params, buf->str);
            g_string_set_size(buf, 0);
        } else {
            g_string_append_c(buf, *ptr);
        }
        ptr++;
    }
    if (backslash) {
        g_string_append_c(buf, '\\');
    }
    params = gutil_strv_add(params, buf->str);
    n = gutil_strv_length(params);

    /* URL, title, action, type, size, path */
    if (n >= 1 && n <= 6) {
        gboolean ok = TRUE;
        int act = NDEF_SP_ACT_DEFAULT;
        int size = 0;
        NdefMedia media;
        GMappedFile* icon_map = NULL;
        const NdefMedia* icon = NULL;
        magic_t magic = (magic_t)0;

        memset(&media, 0, sizeof(media));
        if (n > 2) {
            const char* val = params[2];

            if (val[0] && !gutil_parse_int(val, 0, &act)) {
                fprintf(stderr, "Can't parse action '%s'\n", val);
                ok = FALSE;
            }
        }
        if (ok && n > 4) {
            const char* val = params[4];

            /* Well, it's actually unsigned int but it doesn't really matter */
            if (val[0] && (!gutil_parse_int(val, 0, &size) || size < 0)) {
                fprintf(stderr, "Can't parse size '%s'\n", val);
                ok = FALSE;
            }
        }
        if (ok && n == 6) {
            const char* fname = params[5];
            GError* error = NULL;

            icon_map = g_mapped_file_new(fname, FALSE, &error);
            if (icon_map) {
                media.data.bytes = (void*)g_mapped_file_get_contents(icon_map);
                media.data.size = g_mapped_file_get_length(icon_map);
                magic = magic_open(MAGIC_MIME_TYPE);
                if (magic) {
                    if (magic_load(magic, NULL) == 0) {
                        media.type = magic_buffer(magic, media.data.bytes,
                            media.data.size);
                        GDEBUG("Detected type %s", media.type);
                    }
                }
                icon = &media;
            } else {
                fprintf(stderr, "%s\n", GERRMSG(error));
                g_error_free(error);
                ok = FALSE;
            }
        }
        if (ok) {
            const char* title = (n > 1) ? params[1] : NULL;
            const char* type = (n > 3) ? params[3] : NULL;
            NdefRecSp* sp = ndef_rec_sp_new(params[0], title, NULL,
                type, size, act, icon);

            if (sp) {
                rec = &sp->rec;
            }
        }
        if (magic) {
            magic_close(magic);
        }
        if (icon_map) {
            g_mapped_file_unref(icon_map);
        }
    }

    g_string_free(buf, TRUE);
    g_strfreev(params);
    return rec;
}

static
NdefRec*
ndef_mt_proc(
    const char* type,
    const GUtilData* data)
{
    NdefRec* rec = NULL;
    magic_t magic = (magic_t)0;

    if (!type) {
        magic = magic_open(MAGIC_MIME_TYPE);
        if (magic) {
            if (magic_load(magic, NULL) == 0) {
                type = magic_buffer(magic, data->bytes, data->size);
                if (type) {
                    GDEBUG("Detected type %s", type);
                } else {
                    fprintf(stderr, "Failed to generate media type\n");
                }
            }
        }
    }
    if (type) {
        GUtilData mediatype;

        rec = ndef_rec_new_mediatype(gutil_data_from_string(&mediatype, type),
            data);
    }
    if (magic) {
        magic_close(magic);
    }
    return rec;
}

static
gboolean
parse_media_type(
    const char* name,
    const char* value,
    gpointer data,
    GError** error)
{
    AppOpts* opts = data;

    if (value && value[0]) {
        if (!ndef_valid_mediatype_str(value, FALSE)) {
            g_propagate_error(error, g_error_new(G_OPTION_ERROR,
                G_OPTION_ERROR_BAD_VALUE, "Invalid media type '%s'", value));
            return FALSE;
        }
        g_free(opts->media_type);
        opts->media_type = g_strdup(value);
        /* Fall through to return TRUE */
    }
    opts->media_type_rec = TRUE;
    return TRUE;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    char* uri = NULL;
    char* text = NULL;
    char* sp = NULL;
    gboolean empty = FALSE;
    gboolean verbose = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "empty", 'e', 0, G_OPTION_ARG_NONE, &empty,
          "Write empty NDEF record", NULL },
        { "uri", 'u', 0, G_OPTION_ARG_STRING, &uri,
          "Write URI record", "URI" },
        { "text", 't', 0, G_OPTION_ARG_STRING, &text,
          "Write Text record", "TEXT" },
        { "sp", 's', 0, G_OPTION_ARG_STRING, &sp,
          "Write SmartPoster record", "SPEC" },
        { "media", 'm', G_OPTION_FLAG_OPTIONAL_ARG,
          G_OPTION_ARG_CALLBACK, &parse_media_type,
          "Write MediaType record containing FILE", "TYPE" },
        { NULL }
    };
    AppOpts opts;
    GOptionContext* options = g_option_context_new("[FILE]");
    GOptionGroup* group = g_option_group_new("", "", "", &opts, NULL);
    GError* error = NULL;

    memset(&opts, 0, sizeof(opts));
    g_option_group_add_entries(group, entries);
    g_option_context_set_main_group(options, group);
    g_option_context_set_summary(options, "Writes NDEF record to a tag.\n\n"
        "SmartPoster SPEC is a comma-separated sequence of URL, title,\n"
        "action, type, size and path to the icon file. Embedded commas\n"
        "can be escaped with a backslash.\n\n"
        "TYPE for a MediaType record can be omitted or left empty, in\n"
        "which case the program will attempt to automatically determine\n"
        "media type from the FILE contents.");

    if (g_option_context_parse(options, &argc, &argv, &error) && argc < 3) {
        NdefRec* (*ndef_proc)(const char*, const GUtilData*) = NULL;
        const char* file = argc == 2 ? argv[1] : NULL;
        const char* ndef_spec = NULL;
        const char* type = NULL;
        int gen_ndef = empty;
        int need_file = 0;

        if (uri) {
            ndef_proc = ndef_uri_proc;
            ndef_spec = uri;
            type = "URI";
            gen_ndef++;
        }
        if (text) {
            ndef_proc = ndef_text_proc;
            ndef_spec = text;
            type = "Text";
            gen_ndef++;
        }
        if (sp) {
            ndef_proc = ndef_sp_proc;
            ndef_spec = sp;
            type = "SmartPoster";
            gen_ndef++;
        }
        if (opts.media_type_rec) {
            ndef_proc = ndef_mt_proc;
            ndef_spec = opts.media_type;
            type = "MediaType";
            gen_ndef++;
            need_file++;
        }

        /* No spec - need the file. No file - need exactly one spec */
        if ((!gen_ndef && file) || (gen_ndef == 1 && need_file == !!file)) {
            GMappedFile* map = file ? g_mapped_file_new(file, FALSE, &error) :
                NULL;

            if (!file || map) {
                AppData app;
                GUtilData mapdata;
                const GUtilData* data;

                memset(&app, 0, sizeof(app));
                gutil_log_timestamp = FALSE;
                gutil_log_default.level = verbose ?
                    GLOG_LEVEL_VERBOSE :
                    GLOG_LEVEL_INFO;

                if (map) {
                    mapdata.bytes = (void*) g_mapped_file_get_contents(map);
                    mapdata.size = g_mapped_file_get_length(map);
                    data = &mapdata;
                } else {
                    memset(&mapdata, 0, sizeof(mapdata));
                    data = NULL;
                }

                if (ndef_proc) {
                    NdefRec* rec = ndef_proc(ndef_spec, data);

                    if (rec) {
                        app.ndef = rec->raw;
                        ret = write_ndef(&app);
                        ndef_rec_unref(rec);
                    } else {
                        fprintf(stderr, "Failed to generate %s record\n",
                            type);
                    }
                } else if (empty) {
                    ret = write_ndef(&app);
                } else if (mapdata.size > 0xffff) {
                    fprintf(stderr, "File too big (%u bytes)\n", (guint)
                        mapdata.size);
                } else {
                    app.ndef = mapdata;
                    ret = write_ndef(&app);
                }

                if (map) {
                    g_mapped_file_unref(map);
                }
                g_strfreev(app.tags);
            } else if (error) {
                fprintf(stderr, "%s\n", GERRMSG(error));
                g_error_free(error);
            }
        } else {
            char* help = g_option_context_get_help(options, TRUE, NULL);
            gsize len = strlen(help);

            while (len > 0 && help[len - 1] == '\n') help[len--] = 0;
            printf("%s\n", help);
            g_free(help);
        }
    } else {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
    }
    g_free(uri);
    g_free(text);
    g_free(sp);
    g_free(opts.media_type);
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
