/*
 * Copyright (C) 2023-2024 Slava Monich <slava@monich.com>
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

#include "nfc_host_app.h"
#include "org.sailfishos.nfc.Daemon.h"
#include "org.sailfishos.nfc.LocalHostApp.h"

#include <nfcdef.h>

#include <gutil_misc.h>
#include <gutil_strv.h>
#include <gutil_log.h>

#include <glib-unix.h>

#include <locale.h>
#include <magic.h>

#define NFC_BUS G_BUS_TYPE_SYSTEM
#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"
#define NFC_DAEMON_MIN_INTERFACE_VERSION 4
#define LAST_RESPONSE_ID (1)

static const guchar ndef_aid[] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };

/*
 * [NFCForum-TS-Type-4-Tag_2.0]
 *
 * Data Structure of the Capability Container File
 *
 * +======================================================================+
 * | Offset | Size | Description                                          |
 * +======================================================================+
 * | 0      | 2    | CCLEN (total length, 0x000F-0xFFFE bytes)            |
 * | 2      | 1    | Mapping Version (major/minor 4 bits each)            |
 * | 3      | 2    | MLe (Maximum R-APDU data size, 0x000F..0xFFFF bytes) |
 * | 5      | 2    | MLc (Maximum C-APDU data size, 0x0001..0xFFFF bytes) |
 * | 7      | 8    | NDEF File Control TLV (see below)                    |
 * | 15     |-     | Zero, one, or more TLV blocks                        |
 * +======================================================================+
 *
 * NDEF File Control TLV:
 *
 * +==============================================================+
 * | Offset | Size | Description                                  |
 * +==============================================================+
 * | 0      | 1    | T = 4                                        |
 * | 1      | 1    | L = 6                                        |
 * | 2      | 2    | File Identifier                              |
 * | 4      | 2    | Maximum NDEF file size, 0x0005..0xFFFE       |
 * | 6      | 1    | NDEF file read access condition (0x00)       |
 * | 7      | 1    | NDEF file write access condition (0x00|0xFF) |
 * +==============================================================+
 */

static const guchar cc_ef[] = { 0xe1, 0x03 };
static const guchar cc_data_template[] = {
    0x00, 0x0f, 0x20, 0xff, 0xff, 0xff, 0xff,      /* CC header 7 bytes */
    0x04, 0x06, 0xe1, 0x04, 0xff, 0xfe, 0x00, 0xff /* NDEF File Control TLV */
                /*  fid */  /* size */
};
#define CC_NDEF_TLV_OFFSET  (7)
#define CC_NDEF_FID_OFFSET  (CC_NDEF_TLV_OFFSET + 2)
#define CC_NDEF_SIZE_OFFSET (CC_NDEF_TLV_OFFSET + 4)

#define ISO_CLA (0x00)
#define ISO_INS_SELECT (0xa4)
#define ISO_INS_READ_BINARY (0xb0)
#define ISO_P1_SELECT_BY_ID (0x00)
#define ISO_P2_SELECT_FILE_FIRST (0x00)
#define ISO_P2_RESPONSE_NONE (0x0c)

/* x(DBUS_CALL,dbus_call,dbus-call) */
#define APP_INTERFACE_VERSION 1
#define APP_DBUS_CALLS(x) \
    x(GET_INTERFACE_VERSION,get_interface_version,get-interface-version) \
    x(START,start,start) \
    x(RESTART,restart,restart) \
    x(STOP,stop,stop) \
    x(IMPLICIT_SELECT,implicit_select,implicit-select) \
    x(SELECT,select,select) \
    x(DESELECT,deselect,deselect) \
    x(PROCESS,process,process) \
    x(RESPONSE_STATUS,response_status,response-status)

enum {
    #define DEFINE_ENUM(CALL,call,name) APP_CALL_##CALL,
    APP_DBUS_CALLS(DEFINE_ENUM)
    #undef DEFINE_ENUM
    APP_CALL_COUNT
};

#define RET_OK (0)
#define RET_CMDLINE (1)
#define RET_ERR (2)

typedef enum ndef_share_flags {
    NDEF_SHARE_FLAGS_NONE = 0,
    NDEF_SHARE_FLAG_NFC_A = 0x01,
    NDEF_SHARE_FLAG_READER_OFF = 0x02,
    NDEF_SHARE_FLAG_KEEP_SHARING = 0x04
} NDEF_SHARE_FLAGS;

typedef struct elem_file {
    const char* name;
    GUtilData fid;
    GBytes* data;
    gboolean last;
} ElemFile;

typedef struct ndef_share {
    const char* path;
    const char* name;
    ElemFile ef[2];
    const ElemFile* selected_file;
    GMainLoop* loop;
    NDEF_SHARE_FLAGS flags;
    gboolean stopped;
    int ret;
} NdefShare;

typedef struct ndef_share_opts {
    char* media_type;
    gboolean media_type_rec;
} NdefShareOpts;

#define g_variant_new_empty_bytestring() g_variant_new_from_data\
    (G_VARIANT_TYPE_BYTESTRING, NULL, 0, TRUE, NULL, NULL)

static
gboolean
ndef_share_signal(
    gpointer user_data)
{
    NdefShare* app = user_data;

    if (!app->stopped) {
        GDEBUG("\nSignal caught, exiting...");
        g_main_loop_quit(app->loop);
    }
    return G_SOURCE_CONTINUE;
}

static
void
ndef_share_init_cc_file(
    ElemFile* ef,
    gsize ndef_size)
{
    const gsize size = sizeof(cc_data_template);
    guchar* file = gutil_memdup(cc_data_template, size);

    file[CC_NDEF_SIZE_OFFSET] = (guchar)((ndef_size >> 8) & 0x7f);
    file[CC_NDEF_SIZE_OFFSET + 1] = (guchar)ndef_size;

    ef->data = g_bytes_new_take(file, size);
    ef->fid.bytes = cc_ef;
    ef->fid.size = sizeof(cc_ef);
    ef->name = "NDEF Capability Container";
    ef->last = FALSE;
}

static
const ElemFile*
ndef_share_init_ndef_file(
    ElemFile* ef,
    const GUtilData* ndef)
{
    const gsize size = MIN(ndef->size, 0xfffe);
    const gsize total = size + 2;
    guchar* file = g_malloc(total);

    file[0] = (guchar) (size >> 8);
    file[1] = (guchar) size;
    memcpy(file + 2, ndef->bytes, size);

    ef->data = g_bytes_new_take(file, total);
    ef->fid.bytes = cc_data_template + CC_NDEF_FID_OFFSET;
    ef->fid.size = 2;
    ef->name = "NDEF";
    ef->last = TRUE;
    return ef;
}

static
void
ndef_share_respond_empty(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    guint sw /* (SW1 << 8) | SW2 */)
{
    GDEBUG("< %04X", sw);
    org_sailfishos_nfc_local_host_app_complete_process(service, call,
        g_variant_new_empty_bytestring(), (guchar)(sw >> 8), (guchar) sw, 0);
}

static
void
ndef_share_respond_empty_ok(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call)
{
    /* 9000 - Normal processing */
    ndef_share_respond_empty(service, call, 0x9000);
}

static
gboolean
ndef_share_read_binary(
    NdefShare* app,
    const ElemFile* ef,
    guint off,
    guint le,
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call)
{
    gsize size;
    const guchar* data = g_bytes_get_data(ef->data, &size);

    if (off >= size) {
        GDEBUG("Reading %s", ef->name);
        ndef_share_respond_empty_ok(service, call);
    } else {
        guint count = size - off;

        if (le && count > le) {
            count = le;
        }

        GDEBUG("Reading %s [%u..%u]", ef->name, off, off + count - 1);

#if GUTIL_LOG_DEBUG
        if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
            char* tmp = NULL;

            GDEBUG("< %s%s9000",
                count ? (tmp = gutil_bin2hex(data + off, count, TRUE)) : "",
                count ? " " : "");
            g_free(tmp);
        }
#endif

        /* 9000 - Normal processing */
        org_sailfishos_nfc_local_host_app_complete_process(service, call,
           g_variant_new_from_data(G_VARIANT_TYPE_BYTESTRING, data + off,
           count, TRUE, (GDestroyNotify) g_bytes_unref, g_bytes_ref(ef->data)),
           0x90, 0x00,  (ef->last && off + count == size) ?
           LAST_RESPONSE_ID : 0);
    }
    return TRUE;
}

static
gboolean
ndef_share_process_read_binary(
    NdefShare* app,
    guchar p1,
    guchar p2,
    guint le,
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call)
{
    /*
     * If bit 1 of INS is set to 0 and bit 8 of P1 to 0, then P1-P2
     * (fifteen bits) encodes an offset from zero to 32767.
     */
    if (!(p1 & 0x80) && app->selected_file) {
        return ndef_share_read_binary(app, app->selected_file,
            ((guint) p1 << 8) | p2, le, service, call);
    } else {
        /* 6F00 - Failure (No precise diagnosis) */
        ndef_share_respond_empty(service, call, 0x6f00);
        return TRUE;
    }
}

static
void
ndef_share_reset(
    NdefShare* app)
{
    /* Reset the state */
    app->selected_file = NULL;
}

static
gboolean
ndef_share_process_select(
    NdefShare* app,
    guchar p1,
    guchar p2,
    const GUtilData* fid,
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call)
{
    /* 6F00 - Failure (No precise diagnosis) */
    guint sw = 0x6f00;

    if (p1 == ISO_P1_SELECT_BY_ID &&
        p2 == (ISO_P2_SELECT_FILE_FIRST | ISO_P2_RESPONSE_NONE)) {
        guint i;

        /* 6A82 - File or application not found */
        sw = 0x6a82;

        for (i = 0; i < G_N_ELEMENTS(app->ef); i++) {
            const ElemFile* ef = app->ef + i;

            if (gutil_data_equal(fid, &ef->fid)) {
                if (app->selected_file != ef) {
                    app->selected_file = ef;
                    GDEBUG("Selected %s", ef->name);
                }
                /* 9000 - Normal processing */
                sw = 0x9000;
                break;
            }
        }
    }

    ndef_share_respond_empty(service, call, sw);
    return TRUE;
}

static
gboolean
ndef_share_handle_get_interface_version(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    NdefShare* app)
{
    org_sailfishos_nfc_local_host_app_complete_get_interface_version(service,
        call, APP_INTERFACE_VERSION);
    return TRUE;
}

static
gboolean
ndef_share_handle_start(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    const char* host,
    NdefShare* app)
{
    GINFO("Host %s arrived", host);
    ndef_share_reset(app);
    org_sailfishos_nfc_local_host_app_complete_start(service, call);
    return TRUE;
}

static
gboolean
ndef_share_handle_restart(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    const char* host,
    NdefShare* app)
{
    GINFO("Host %s has been restarted", host);
    ndef_share_reset(app);
    org_sailfishos_nfc_local_host_app_complete_restart(service, call);
    return TRUE;
}

static
gboolean
ndef_share_handle_stop(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    const char* host,
    NdefShare* app)
{
    GINFO("Host %s left", host);
    org_sailfishos_nfc_local_host_app_complete_stop(service, call);
    return TRUE;
}

static
gboolean
ndef_share_handle_implicit_select(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    const char* host,
    NdefShare* app)
{
    GINFO("%s implicitly selected for %s", app->name, host);
    org_sailfishos_nfc_local_host_app_complete_implicit_select(service, call);
    return TRUE;
}

static
gboolean
ndef_share_handle_select(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    const char* host,
    NdefShare* app)
{
    GINFO("%s selected for %s", app->name, host);
    org_sailfishos_nfc_local_host_app_complete_select(service, call);
    return TRUE;
}

static
gboolean
ndef_share_handle_deselect(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    const char* host,
    NdefShare* app)
{
    GINFO("Deselected for %s", host);
    org_sailfishos_nfc_local_host_app_complete_deselect(service, call);
    return TRUE;
}

static
gboolean
ndef_share_handle_process(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    const char* host,
    guchar cla,
    guchar ins,
    guchar p1,
    guchar p2,
    GVariant* data_var,
    guint le,
    NdefShare* app)
{
    GUtilData data;

    data.size = g_variant_get_size(data_var);
    data.bytes = g_variant_get_data(data_var);

#if GUTIL_LOG_DEBUG
    if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
        char* tmp = NULL;

        GDEBUG("> %02X %02X %02X %02X %s%s%04X", cla, ins, p1, p2,
            data.size ? (tmp = gutil_data2hex(&data, TRUE)) : "",
            data.size ? " " : "", le);
        g_free(tmp);
    }
#endif

    if (cla == ISO_CLA) {
        if (ins == ISO_INS_SELECT) {
            return ndef_share_process_select(app, p1, p2, &data,
                service, call);
        } else if (ins == ISO_INS_READ_BINARY) {
            return ndef_share_process_read_binary(app, p1, p2, le,
                service, call);
        }
    }

    /* 6F00 - Failure (No precise diagnosis) */
    ndef_share_respond_empty(service, call, 0x6f00);
    return TRUE;
}

static
gboolean
ndef_share_handle_response_status(
    OrgSailfishosNfcLocalHostApp* service,
    GDBusMethodInvocation* call,
    guint response_id,
    gboolean ok,
    NdefShare* app)
{
    GASSERT(response_id == LAST_RESPONSE_ID);
    if (ok) {
        if (!(app->flags & NDEF_SHARE_FLAG_KEEP_SHARING)) {
            GDEBUG("Response sent, exiting...");
            g_main_loop_quit(app->loop);
            app->ret = RET_OK;
        } else {
            GDEBUG("Response sent");
        }
    } else {
        GWARN("Failed to deliver response");
    }
    org_sailfishos_nfc_local_host_app_complete_response_status(service, call);
    return TRUE;
}

static
void
ndef_share_daemon_done(
    GDBusConnection* bus,
    const char* name,
    gpointer user_data)
{
    NdefShare* app = user_data;

    GWARN("%s has disappeared", name);
    g_main_loop_quit(app->loop);
}

static
void
ndef_share_run_app(
    NdefShare* app)
{
    guint sigterm = g_unix_signal_add(SIGTERM, ndef_share_signal, app);
    guint sigint = g_unix_signal_add(SIGINT, ndef_share_signal, app);

    app->loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(app->loop);
    g_source_remove(sigterm);
    g_source_remove(sigint);
    g_main_loop_unref(app->loop);
    app->loop = NULL;
}

static
void
ndef_share_run_with_daemon(
    NdefShare* app,
    OrgSailfishosNfcDaemon* daemon)
{
    GError* error = NULL;
    GDBusConnection* conn = g_bus_get_sync(NFC_BUS, NULL, &error);
    OrgSailfishosNfcLocalHostApp* service =
        org_sailfishos_nfc_local_host_app_skeleton_new();
    NFC_HOST_APP_FLAGS app_flags = NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION;
    gulong call_id[APP_CALL_COUNT];

    #define CONNECT_HANDLER(CALL,call,name) call_id[APP_CALL_##CALL] = \
        g_signal_connect(service, "handle-"#name, \
        G_CALLBACK(ndef_share_handle_##call), app);
    memset(call_id, 0, sizeof(call_id));
    APP_DBUS_CALLS(CONNECT_HANDLER)
    #undef CONNECT_HANDLER

    if (conn) {
        GDBusInterfaceSkeleton* skel = G_DBUS_INTERFACE_SKELETON(service);

        if (g_dbus_interface_skeleton_export(skel, conn, app->path, &error) &&
            org_sailfishos_nfc_daemon_call_register_local_host_app_sync(daemon,
                app->path, app->name, g_variant_new_from_data
                    (G_VARIANT_TYPE_BYTESTRING, ndef_aid, sizeof(ndef_aid),
                         TRUE, NULL, NULL), app_flags, NULL, &error)) {
            guint nfcd_watch_id = g_bus_watch_name(NFC_BUS, NFC_SERVICE,
                G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, ndef_share_daemon_done,
                app, NULL);

            GINFO("%s has been registered", app->name);

            if (app->flags & NDEF_SHARE_FLAG_READER_OFF) {
                guint req_id = 0;

                if (org_sailfishos_nfc_daemon_call_request_mode_sync(daemon,
                    NFC_MODE_CARD_EMULATION, NFC_MODE_READER_WRITER,
                    &req_id, NULL, &error)) {
                    GDEBUG("Reader mode has been turned off");
                } else {
                    GERR("%s", GERRMSG(error));
                    g_error_free(error);
                    error = NULL;
                }
            }

            if (app->flags & NDEF_SHARE_FLAG_NFC_A) {
                guint req_id = 0;

                if (org_sailfishos_nfc_daemon_call_request_techs_sync(daemon,
                    NFC_TECHNOLOGY_A, -1, &req_id, NULL, &error)) {
                    GDEBUG("NFC-A technology has been forced");
                } else {
                    GERR("%s", GERRMSG(error));
                    g_error_free(error);
                    error = NULL;
                }
            }

            ndef_share_run_app(app);
            g_bus_unwatch_name(nfcd_watch_id);
        }
        g_dbus_interface_skeleton_unexport(skel);
        g_object_unref(conn);
    }

    if (error) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }

    gutil_disconnect_handlers(service, call_id, G_N_ELEMENTS(call_id));
    g_object_unref(service);
}

static
void
ndef_share_run(
    NdefShare* app)
{
    GError* error = NULL;
    OrgSailfishosNfcDaemon* daemon =
        org_sailfishos_nfc_daemon_proxy_new_for_bus_sync(NFC_BUS,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
            NFC_DAEMON_PATH, NULL, &error);

    if (daemon) {
        gint version = 0;

        /* Enable CE mode */
        if (org_sailfishos_nfc_daemon_call_get_interface_version_sync(daemon,
            &version, NULL, &error)) {
            if (version >= NFC_DAEMON_MIN_INTERFACE_VERSION) {
                ndef_share_run_with_daemon(app, daemon);
            } else {
                GERR("NFC deamon is too old");
            }
        }
        g_object_unref(daemon);
    }
    if (error) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
}

static
int
ndef_share_rec(
    const GUtilData* ndef,
    NDEF_SHARE_FLAGS flags)
{
    NdefShare app;
    const ElemFile* ndef_ef;
    guint i;

#if GUTIL_LOG_DEBUG
    if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
        gsize len = ndef->size;
        guint off = 0;

        GDEBUG("NDEF:");
        while (len > 0) {
            char buf[GUTIL_HEXDUMP_BUFSIZE];
            const guint n = gutil_hexdump(buf, ndef->bytes + off, len);

            GDEBUG("  %04X: %s", off, buf);
            len -= n;
            off += n;
        }
    }
#endif

    memset(&app, 0, sizeof(app));
    app.ret = RET_ERR;
    app.path = "/ndefshare";
    app.name = "NDEF Tag Application";
    app.flags = flags;
    ndef_ef = ndef_share_init_ndef_file(app.ef + 0, ndef);
    ndef_share_init_cc_file(app.ef + 1, g_bytes_get_size(ndef_ef->data));

    ndef_share_run(&app);

    for (i = 0; i < G_N_ELEMENTS(app.ef); i++) {
        g_bytes_unref(app.ef[i].data);
    }
    return app.ret;
}

static
gboolean
parse_media_type(
    const char* name,
    const char* value,
    gpointer data,
    GError** error)
{
    NdefShareOpts* opts = data;

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

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    char* uri = NULL;
    char* text = NULL;
    gboolean verbose = FALSE, reader_off = FALSE, keep_sharing = FALSE;
    NdefShareOpts opts;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "uri", 'u', 0, G_OPTION_ARG_STRING, &uri,
          "Share URI record", "URI" },
        { "text", 't', 0, G_OPTION_ARG_STRING, &text,
          "Share Text record", "TEXT" },
        { "media", 'm', G_OPTION_FLAG_OPTIONAL_ARG,
           G_OPTION_ARG_CALLBACK, &parse_media_type,
          "Share MediaType record containing FILE", "TYPE" },
        { "reader-off", 'r', 0, G_OPTION_ARG_NONE, &reader_off,
          "Turn reader mode off", NULL },
        { "keep-sharing", 'k', 0, G_OPTION_ARG_NONE, &keep_sharing,
          "Do not exit after successful sharing", NULL },
        { NULL }
    };
    GOptionContext* options = g_option_context_new(NULL);
    GOptionGroup* group = g_option_group_new("", "", "", &opts, NULL);
    GError* error = NULL;

    memset(&opts, 0, sizeof(opts));
    setlocale(LC_ALL, "en_US.UTF-8");
    g_option_group_add_entries(group, entries);
    g_option_context_set_main_group(options, group);
    g_option_context_set_summary(options, "Shares an NDEF message by "
        "emulating a Type 4 tag.\n\n"
        "TYPE for a MediaType record can be omitted or left empty, in\n"
        "which case the program will attempt to automatically determine\n"
        "media type from the FILE contents.");

    if (g_option_context_parse(options, &argc, &argv, &error) && argc < 3) {
        NdefRec* (*ndef_proc)(const char*, const GUtilData*) = NULL;
        const char* file = argc == 2 ? argv[1] : NULL;
        const char* ndef_spec = NULL;
        const char* type = NULL;
        int gen_ndef = 0, need_file = 0;

        gutil_log_timestamp = FALSE;
        gutil_log_default.level = verbose ?
            GLOG_LEVEL_VERBOSE :
            GLOG_LEVEL_INFO;

        /* What is requested */
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
                GUtilData mapdata;
                const GUtilData* data;
                /*
                 * Force NFC-A to avoid T4A becoming T4B (and therefore
                 * being considered a different tag) after reactivation.
                 */
                NDEF_SHARE_FLAGS flags = NDEF_SHARE_FLAG_NFC_A;

                if (reader_off) flags |= NDEF_SHARE_FLAG_READER_OFF;
                if (keep_sharing) flags |= NDEF_SHARE_FLAG_KEEP_SHARING;

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
                        ret = ndef_share_rec(&rec->raw, flags);
                        ndef_rec_unref(rec);
                    } else {
                        fprintf(stderr, "Failed to generate %s record\n",
                            type);
                    }
                } else if (mapdata.size > 0xfffe) {
                    fprintf(stderr, "File too big (%u bytes)\n", (guint)
                        mapdata.size);
                } else if (!mapdata.size) {
                    fprintf(stderr, "Nothing to share\n");
                } else {
                    ret = ndef_share_rec(&mapdata, flags);
                }

                if (map) {
                    g_mapped_file_unref(map);
                }
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
