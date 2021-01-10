/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_types.h"
#include "org.sailfishos.nfc.Daemon.h"
#include "org.sailfishos.nfc.Adapter.h"
#include "org.sailfishos.nfc.Peer.h"

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
    char** peers;
    GMainLoop* loop;
    guint sap;
    const char* sn;
    const char* input_name;
    int input_fd;
    gboolean reading_file;
    gboolean stopped;
    GIOChannel* llc_io;
    GIOChannel* input_io;
    GIOChannel* stdout_io;
    guint llc_read_id;
    guint local_read_id;
    gulong written;
} AppData;

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
nfcdep_write(
    AppData* app,
    const char* buf,
    gsize size,
    GIOChannel* out)
{
    gsize written = 0, total = 0;

    while (total < size) {
        GError* error = NULL;
        GIOStatus status = g_io_channel_write_chars(out, buf + total,
            size - total, &written, &error);

        if (status == G_IO_STATUS_AGAIN) {
            /* Need to block */
            g_io_channel_set_flags(out, 0, NULL);
            status = g_io_channel_write_chars(out, buf + total,
                size - total, &written, &error);
            g_io_channel_set_flags(out, G_IO_FLAG_NONBLOCK, NULL);
        }

        if (status == G_IO_STATUS_NORMAL) {
            GVERBOSE("Written %u bytes", (guint) written);
            app->written += written;
            total += written;
            written = 0;
        } else {
            if (error) {
                GDEBUG("Write failed: %s", GERRMSG(error));
                g_error_free(error);
            }
            return FALSE;
        }
    }
    return TRUE;
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
        return nfcdep_write(app, buf, bytes_read, out);
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

    if (nfcdep_read(app, "Peer", app->llc_io, app->stdout_io)) {
        return G_SOURCE_CONTINUE;
    } else {
        app->llc_read_id = 0;
        g_main_loop_quit(app->loop);
        return G_SOURCE_REMOVE;
    }
}

static
gboolean
nfcdep_local_read_cb(
    GIOChannel* source,
    GIOCondition condition,
    gpointer user_data)
{
    AppData* app = user_data;

    if (nfcdep_read(app, app->input_name, app->input_io, app->llc_io)) {
        return G_SOURCE_CONTINUE;
    } else {
        app->local_read_id = 0;

        /*
         * If we terminate the loop without waiting for hangup, the 
         * data buffered by nfcd won't be sent and the connection
         * would terminate early because nfcd will most likely drop
         * the connection once this process exits (if it was the only
         * registered service).
         *
         * If we are copying standard input, it doesn't matter.
         */
        if (!app->reading_file) {
            g_main_loop_quit(app->loop);
        }
        return G_SOURCE_REMOVE;
    }
}

static
int
nfcdep_connected(
    AppData* app,
    int fd)
{
    app->llc_io = nfcdep_channel_new(fd);
    if (app->llc_io) {
        app->input_io = nfcdep_channel_new(app->input_fd);
        if (app->input_io) {
            app->stdout_io = nfcdep_channel_new(STDOUT_FILENO);
            if (app->stdout_io) {
                guint sigterm = g_unix_signal_add(SIGTERM, nfcdep_signal, app);
                guint sigint = g_unix_signal_add(SIGINT, nfcdep_signal, app);
                gint64 start_time = app->reading_file ? g_get_real_time() : 0;

                app->llc_read_id = g_io_add_watch(app->llc_io,
                    G_IO_IN | G_IO_ERR | G_IO_HUP, nfcdep_llc_read_cb, app);
                app->local_read_id = g_io_add_watch(app->input_io,
                    G_IO_IN | G_IO_ERR | G_IO_HUP, nfcdep_local_read_cb, app);
                g_main_loop_run(app->loop);
                g_source_remove(sigterm);
                g_source_remove(sigint);
                if (app->llc_read_id) {
                    g_source_remove(app->llc_read_id);
                    app->llc_read_id = 0;
                }
                if (app->local_read_id) {
                    g_source_remove(app->local_read_id);
                    app->local_read_id = 0;
                }
                GDEBUG("%lu bytes written", app->written);
                if (start_time) {
                    gint64 end_time = g_get_real_time();

                    if (end_time > start_time) {
                        GDEBUG("%ld bytes/sec", (long) (app->written *
                            G_TIME_SPAN_SECOND / (end_time - start_time)));
                    }
                }
                g_io_channel_flush(app->llc_io, NULL);
                g_io_channel_unref(app->stdout_io);
                app->stdout_io = NULL;
            }
            g_io_channel_unref(app->input_io);
            app->input_io = NULL;
        }
        g_io_channel_unref(app->llc_io);
        app->llc_io = NULL;
    }
    return RET_OK;
}

static
int
nfcdep_connect(
    AppData* app,
    OrgSailfishosNfcPeer* peer)
{
    int ret = RET_ERR;
    GError* error = NULL;
    GVariant* res = NULL;
    GUnixFDList* fdl = NULL;

    if (app->sn ?
        org_sailfishos_nfc_peer_call_connect_service_name_sync(peer,
            app->sn, NULL, &res, &fdl, NULL, &error) :
        org_sailfishos_nfc_peer_call_connect_access_point_sync(peer,
            app->sap, NULL, &res, &fdl, NULL, &error)) {
        int fd = g_unix_fd_list_peek_fds(fdl, NULL)[0];

        if (fd >= 0) {
            GDEBUG("Connected!");
            ret = nfcdep_connected(app, fd);
            shutdown(fd, SHUT_RDWR);
        }
        g_variant_unref(res);
        g_object_unref(fdl);
    } else {
        GERR("%s: %s", g_dbus_proxy_get_object_path(G_DBUS_PROXY(peer)),
            GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
gboolean
nfcdep_peers_changed(
    OrgSailfishosNfcAdapter* adapter,
    const gchar* const* peers,
    AppData* app)
{
    if (peers[0]) {
        if (!app->peers) {
            app->peers = g_strdupv((char**)peers);
        }
        GDEBUG("Peer detected");
        g_main_loop_quit(app->loop);
    }
    return TRUE;
}

static
int
nfcdep_adapter(
    AppData* app,
    OrgSailfishosNfcAdapter* adapter)
{
    int ret = RET_ERR;
    GError* error = NULL;
    gulong signal_id = g_signal_connect(adapter, "peers-changed",
        G_CALLBACK(nfcdep_peers_changed), app);

    if (org_sailfishos_nfc_adapter_call_get_peers_sync(adapter, &app->peers,
        NULL, &error)) {
        if (!app->peers || !app->peers[0]) {
            guint sigterm = g_unix_signal_add(SIGTERM, nfcdep_signal, app);
            guint sigint = g_unix_signal_add(SIGINT, nfcdep_signal, app);

            g_strfreev(app->peers);
            app->peers = NULL;
            GINFO("Waiting for peer...");
            g_main_loop_run(app->loop);
            g_source_remove(sigterm);
            g_source_remove(sigint);
        }

        /* Don't need the signal anymore */
        g_signal_handler_disconnect(adapter, signal_id);
        signal_id = 0;

        if (app->peers && app->peers[0]) {
            const char* path = app->peers[0];
            OrgSailfishosNfcPeer* peer =
                org_sailfishos_nfc_peer_proxy_new_for_bus_sync(NFC_BUS,
                    G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
                    path, NULL, &error);

            GDEBUG("Peer %s", path);
            if (peer) {
                ret = nfcdep_connect(app, peer);
                g_object_unref(peer);
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
        g_signal_handler_disconnect(adapter, signal_id);
    }
    return ret;
}

static
int
nfcdep_adapter_path(
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
        app->loop = g_main_loop_new(NULL, FALSE);
        ret = nfcdep_adapter(app, adapter);
        g_object_unref(adapter);
        g_main_loop_unref(app->loop);
        app->loop = NULL;
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

static
int
nfcdep_run(
    AppData* app)
{
    int ret = RET_ERR;
    GError* error = NULL;
    OrgSailfishosNfcDaemon* daemon =
        org_sailfishos_nfc_daemon_proxy_new_for_bus_sync(NFC_BUS,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
            NFC_DAEMON_PATH, NULL, &error);

    if (daemon) {
        guint mode_id = 0;

        /* Enable P2P modes */
        if (org_sailfishos_nfc_daemon_call_request_mode_sync(daemon,
            NFC_MODES_P2P, NFC_MODE_READER_WRITER, &mode_id, NULL, &error)) {
            char** adapters = NULL;

            if (org_sailfishos_nfc_daemon_call_get_adapters_sync(daemon,
                &adapters, NULL, &error)) {
                if (adapters && adapters[0]) {
                    ret = nfcdep_adapter_path(app, adapters[0]);
                } else {
                    GERR("No NFC adapters found.");
                }
                g_strfreev(adapters);
            }
        }
        g_object_unref(daemon);
    }
    if (error) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
    return ret;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean verbose = FALSE;
    char* in_file = NULL;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "input", 'i', 0, G_OPTION_ARG_FILENAME, &in_file,
          "Read input from FILE", "FILE" },
        { NULL }
    };
    GOptionContext* opts = g_option_context_new("<SN|SAP>");
    GError* error = NULL;

    g_option_context_add_main_entries(opts, entries, NULL);
    g_option_context_set_summary(opts, "Connects to NFC peer.");
    if (g_option_context_parse(opts, &argc, &argv, &error) && argc == 2) {
        const char* arg = argv[1];
        AppData app;
        int sap;

        gutil_log_timestamp = FALSE;
        gutil_log_default.level = verbose ?
            GLOG_LEVEL_VERBOSE :
            GLOG_LEVEL_INFO;

        memset(&app, 0, sizeof(app));
        if (gutil_parse_int(arg, 0, &sap) && sap > 0) {
            app.sap = sap;
        } else {
            app.sn = arg;
        }
        if (in_file) {
            app.input_name = in_file;
            app.input_fd = open(in_file, O_RDONLY);
            app.reading_file = TRUE;
        } else {
            app.input_name = "Standard input";
            app.input_fd = dup(STDIN_FILENO);
        }
        if (app.input_fd >= 0) {
            ret = nfcdep_run(&app);
            g_strfreev(app.peers);
            close(app.input_fd);
        } else {
            GERR("Failed to open %s: %s", app.input_name, strerror(errno));
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
    g_free(in_file);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
