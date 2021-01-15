/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
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
#include "org.sailfishos.nfc.LocalService.h"

#include <gutil_misc.h>
#include <gutil_strv.h>
#include <gutil_log.h>

#include <glib-unix.h>
#include <gio/gunixfdlist.h>

#include <sys/socket.h>

#define NFC_BUS G_BUS_TYPE_SYSTEM
#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"

#define RET_OK (0)
#define RET_CMDLINE (1)
#define RET_ERR (2)

typedef struct app_data {
    gboolean multiple;
    GMainLoop* loop;
    const char* path;
    const char* sn;
    gboolean stopped;
    int output_fd;
    GIOChannel* llc_io;
    GIOChannel* stdin_io;
    GIOChannel* output_io;
    guint llc_read_id;
    guint stdin_read_id;
    gint64 start_time;
    gint64 bytes_received;
} AppData;

enum service_calls {
    CALL_ACCEPT,
    SIGNAL_PEER_ARRIVED,
    SIGNAL_PEER_LEFT,
    CALL_COUNT
};

static
gboolean
nfcdep_signal(
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
GIOChannel*
nfcdep_channel_new(
    int fd)
{
    GIOChannel* io = g_io_channel_unix_new(fd);

    if (io) {
        g_io_channel_set_flags(io, G_IO_FLAG_NONBLOCK, NULL);
        g_io_channel_set_encoding(io, NULL, NULL);
        g_io_channel_set_buffered(io, FALSE);
    }
    return io;
}

static
gboolean
nfcdep_read(
    AppData* app,
    const char* what,
    GIOChannel* in,
    GIOChannel* out)
{
    char buf[512];
    gsize bytes_read = 0;
    GError* error = NULL;
    GIOStatus status = g_io_channel_read_chars(in, buf, sizeof(buf),
        &bytes_read, &error);

    if (error) {
        GDEBUG("%s read failed: %s", what, GERRMSG(error));
        g_error_free(error);
        return FALSE;
    } else if (status == G_IO_STATUS_EOF) {
        GDEBUG("%s hung up", what);
        return FALSE;
    } else {
        GVERBOSE("%s produced %u bytes", what, (guint)bytes_read);
        app->bytes_received += bytes_read;
        if (bytes_read) {
            gsize written = 0, total = 0;

            while (total < bytes_read && g_io_channel_write_chars(out,
                buf + total, bytes_read - total, &written, &error) ==
                G_IO_STATUS_NORMAL) {
                GVERBOSE("Written %u bytes", (guint) written);
                total += written;
                written = 0;
            }
            if (error) {
                GDEBUG("Write failed: %s", GERRMSG(error));
                g_error_free(error);
                return FALSE;
            }
        }
        return TRUE;
    }
}

static
void
nfcdep_close_connection(
    AppData* app)
{
    if (app->llc_io) {
        gint64 end = g_get_real_time();

        GDEBUG("%ld bytes received", (long) app->bytes_received);
        if (end > app->start_time) {
            GDEBUG("%ld bytes/sec", (long)(app->bytes_received *
                G_TIME_SPAN_SECOND / (end - app->start_time)));
        }
        if (app->llc_read_id) {
            g_source_remove(app->llc_read_id);
            app->llc_read_id = 0;
        }
        g_io_channel_unref(app->llc_io);
        app->llc_io = NULL;
    }
    if (app->stdin_io) {
        if (app->stdin_read_id) {
            g_source_remove(app->stdin_read_id);
            app->stdin_read_id = 0;
        }
        g_io_channel_unref(app->stdin_io);
        app->stdin_io = NULL;
    }
}

static
gboolean
nfcdep_llc_read_cb(
    GIOChannel* source,
    GIOCondition condition,
    gpointer user_data)
{
    AppData* app = user_data;

    if (nfcdep_read(app, "Peer", app->llc_io, app->output_io)) {
        return G_SOURCE_CONTINUE;
    } else if (app->multiple) {
        app->llc_read_id = 0;
        nfcdep_close_connection(app);
        return G_SOURCE_REMOVE;
    } else {
        g_main_loop_quit(app->loop);
        return G_SOURCE_CONTINUE;
    }
}

static
gboolean
nfcdep_stdin_read_cb(
    GIOChannel* source,
    GIOCondition condition,
    gpointer user_data)
{
    AppData* app = user_data;

    if (nfcdep_read(app, "Standard input", app->stdin_io, app->llc_io)) {
        return G_SOURCE_CONTINUE;
    } else {
        app->stdin_read_id = 0;
        g_main_loop_quit(app->loop);
        return G_SOURCE_REMOVE;
    }
}

static
gboolean
nfcdep_accept(
    AppData* app,
    int fd)
{
    app->llc_io = nfcdep_channel_new(fd);
    if (app->llc_io) {
        app->stdin_io = nfcdep_channel_new(STDIN_FILENO);
        if (app->stdin_io) {
            app->llc_read_id = g_io_add_watch(app->llc_io,
                G_IO_IN | G_IO_ERR | G_IO_HUP, nfcdep_llc_read_cb, app);
            app->stdin_read_id = g_io_add_watch(app->stdin_io,
                G_IO_IN | G_IO_ERR | G_IO_HUP, nfcdep_stdin_read_cb, app);
            g_io_channel_set_close_on_unref(app->llc_io, TRUE);
            app->start_time = g_get_real_time();
            app->bytes_received = 0;
            return TRUE;
        }
        g_io_channel_unref(app->llc_io);
        app->llc_io = NULL;
    }
    return FALSE;
}

static
gboolean
nfcdep_handle_peer_arrived(
    OrgSailfishosNfcLocalService* service,
    GDBusMethodInvocation* call,
    const char* path,
    AppData* app)
{
    GDEBUG("Peer %s arrived", path);
    org_sailfishos_nfc_local_service_complete_peer_arrived(service, call);
    return TRUE;
}

static
gboolean
nfcdep_handle_peer_left(
    OrgSailfishosNfcLocalService* service,
    GDBusMethodInvocation* call,
    const char* path,
    AppData* app)
{
    GDEBUG("Peer %s left", path);
    org_sailfishos_nfc_local_service_complete_peer_left(service, call);
    return TRUE;
}

static
gboolean
nfcdep_handle_accept(
    OrgSailfishosNfcLocalService* service,
    GDBusMethodInvocation* call,
    GUnixFDList* fdl,
    guint rsap,
    GVariant* var,
    AppData* app)
{
    gboolean ok = FALSE;

    if (app->llc_io) {
        GDEBUG("Refusing connection from %u", rsap);
    } else {
        int fd = g_unix_fd_list_get(fdl, 0, NULL);

        if (nfcdep_accept(app, fd)) {
            GDEBUG("Accepted connection from %u (fd %d)", rsap, fd);
            ok = TRUE;
        } else {
            close(fd);
            GERR("Failed to set up connection");
        }
    }
    org_sailfishos_nfc_local_service_complete_accept(service, call, NULL, ok);
    return TRUE;
}

static
int
nfcdep_run_service(
    AppData* app,
    OrgSailfishosNfcDaemon* daemon)
{
    guint sap;
    GError* error = NULL;

    if (org_sailfishos_nfc_daemon_call_register_local_service_sync(daemon,
        app->path, app->sn, &sap, NULL, &error)) {

        GDEBUG("Registered sap %u", sap);
        app->output_io = nfcdep_channel_new(app->output_fd);
        if (app->output_io) {
            guint sigterm = g_unix_signal_add(SIGTERM, nfcdep_signal, app);
            guint sigint = g_unix_signal_add(SIGINT, nfcdep_signal, app);

            app->loop = g_main_loop_new(NULL, FALSE);
            g_main_loop_run(app->loop);
            g_source_remove(sigterm);
            g_source_remove(sigint);
            g_io_channel_unref(app->output_io);
            app->output_io = NULL;
        }
        nfcdep_close_connection(app);
        g_main_loop_unref(app->loop);
        app->loop = NULL;
        return RET_OK;
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
        return RET_ERR;
    }
}

static
int
nfcdep_run(
    AppData* app)
{
    int ret = RET_ERR;
    GError* error = NULL;
    GDBusConnection* conn = g_bus_get_sync(NFC_BUS, NULL, NULL);
    OrgSailfishosNfcDaemon* daemon =
        org_sailfishos_nfc_daemon_proxy_new_sync(conn,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
            NFC_DAEMON_PATH, NULL, &error);

    if (daemon) {
        OrgSailfishosNfcLocalService* service =
            org_sailfishos_nfc_local_service_skeleton_new();
        GDBusInterfaceSkeleton* skel = G_DBUS_INTERFACE_SKELETON(service);
        gulong call_ids[CALL_COUNT];

        call_ids[CALL_ACCEPT] = g_signal_connect(service,
            "handle-accept", G_CALLBACK(nfcdep_handle_accept), app);
        call_ids[SIGNAL_PEER_ARRIVED] = g_signal_connect(service,
            "handle-peer-arrived", G_CALLBACK(nfcdep_handle_peer_arrived), app);
        call_ids[SIGNAL_PEER_LEFT] = g_signal_connect(service,
            "handle-peer-left", G_CALLBACK(nfcdep_handle_peer_left), app);

        if (g_dbus_interface_skeleton_export(skel, conn, app->path, &error)) {
            nfcdep_run_service(app, daemon);
            g_dbus_interface_skeleton_unexport(skel);
        } else {
            GERR("%s", GERRMSG(error));
            g_error_free(error);
        }
        gutil_disconnect_handlers(service, call_ids, G_N_ELEMENTS(call_ids));
        g_object_unref(service);
        g_object_unref(daemon);
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
    g_object_unref(conn);
    return ret;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean multiple = FALSE;
    gboolean verbose = FALSE;
    char* out_file = NULL;
    GOptionEntry entries[] = {
        { "output", 'o', 0, G_OPTION_ARG_FILENAME, &out_file,
          "Write output to FILE", "FILE" },
        { "multiple", 'm', 0, G_OPTION_ARG_NONE, &multiple,
          "Multiple connections", NULL },
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { NULL }
    };
    GOptionContext* opts = g_option_context_new("SAP");
    GError* error = NULL;

    g_option_context_add_main_entries(opts, entries, NULL);
    g_option_context_set_summary(opts, "Waits for NFC peer to connect.");
    if (g_option_context_parse(opts, &argc, &argv, &error) && argc == 2) {
        AppData app;

        gutil_log_timestamp = FALSE;
        gutil_log_default.level = verbose ?
            GLOG_LEVEL_VERBOSE :
            GLOG_LEVEL_INFO;

        memset(&app, 0, sizeof(app));
        app.sn = argv[1];
        app.path = "/test";
        app.multiple = multiple;
        if (out_file) {
            app.output_fd = open(out_file, O_RDWR | O_CREAT);
        } else {
            app.output_fd = dup(STDOUT_FILENO);
        }
        if (app.output_fd >= 0) {
            ret = nfcdep_run(&app);
            close(app.output_fd);
        } else {
            GERR("Failed to open %s: %s", out_file, strerror(errno));
        }
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
    g_option_context_free(opts);
    g_free(out_file);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
