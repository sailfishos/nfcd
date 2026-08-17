/*
 * Copyright (C) 2026 Jolla Mobile Ltd
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

#include <gutil_log.h>

#include <glib-unix.h>

#define NFC_BUS G_BUS_TYPE_SYSTEM
#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_DAEMON_PATH "/"
#define NFC_DAEMON_MIN_INTERFACE_VERSION 6

#define RET_OK (0)
#define RET_CMDLINE (1)
#define RET_ERR (2)

static
gboolean
nfc_block_quit_signal(
    gpointer loop)
{
    GDEBUG("Signal caught, shutting down...");
    g_main_loop_quit(loop);
    return G_SOURCE_CONTINUE;
}

static
void
nfc_block_blocked_changed(
    OrgSailfishosNfcDaemon* daemon,
    gboolean blocked,
    gpointer unused)
{
    GINFO("NFC is %sblocked", blocked ? "" : "un");
}

static
int
nfc_block_run_with_daemon(
    OrgSailfishosNfcDaemon* daemon)
{
    guint id = 0;
    GError* error = NULL;

    if (org_sailfishos_nfc_daemon_call_request_block_sync(daemon, &id,
        NULL, &error)) {
        GMainLoop* loop = g_main_loop_new(NULL, FALSE);
        guint sigterm = g_unix_signal_add(SIGTERM, nfc_block_quit_signal, loop);
        guint sigint = g_unix_signal_add(SIGINT, nfc_block_quit_signal, loop);
        gulong signal_id = g_signal_connect(daemon, "blocked-changed",
            G_CALLBACK(nfc_block_blocked_changed), NULL);

        GDEBUG("Request id %u", id);
        g_main_loop_run(loop);
        g_source_remove(sigterm);
        g_source_remove(sigint);
        g_main_loop_unref(loop);

        org_sailfishos_nfc_daemon_call_release_block_sync(daemon, id,
            NULL, &error);
        g_signal_handler_disconnect(daemon, signal_id);
        if (!error) {
            return RET_OK;
        }
    }

    GERR("%s", GERRMSG(error));
    g_error_free(error);
    return RET_ERR;
}

static
int
nfc_block_run()
{
    int ret = RET_ERR;
    GError* error = NULL;
    OrgSailfishosNfcDaemon* daemon =
        org_sailfishos_nfc_daemon_proxy_new_for_bus_sync(NFC_BUS,
            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NFC_SERVICE,
            NFC_DAEMON_PATH, NULL, &error);

    if (daemon) {
        gint version = 0;

        /* Check the nfcd version */
        if (org_sailfishos_nfc_daemon_call_get_interface_version_sync(daemon,
            &version, NULL, &error)) {
            GDEBUG("Daemon interface version %u", version);
            if (version >= NFC_DAEMON_MIN_INTERFACE_VERSION) {
                ret = nfc_block_run_with_daemon(daemon);
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

    return ret;
}

int
main(
    int argc,
    char* argv[])
{
    int ret = RET_ERR;
    gboolean verbose = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { NULL }
    };
    GOptionContext* opts = g_option_context_new(NULL);
    GError* error = NULL;

    g_option_context_add_main_entries(opts, entries, NULL);
    g_option_context_set_summary(opts, "Blocks NFC.");

    if (g_option_context_parse(opts, &argc, &argv, &error) && argc == 1) {
        gutil_log_default.level = verbose ?
            GLOG_LEVEL_VERBOSE :
            GLOG_LEVEL_INFO;
        ret = nfc_block_run();
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
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
