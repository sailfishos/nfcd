/*
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2019-2020 Slava Monich <slava.monich@jolla.com>
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

#include "test_dbus.h"

#include <glib/gstdio.h>

#include <gutil_log.h>

struct test_dbus {
    char* tmpdir;
    GDBusConnection* client_connection;
    GDBusConnection* server_connection;
    GDBusServer* server;
    GDBusAuthObserver* observer;
    TestDBusStartFunc start;
    TestDBusStartFunc start2;
    void* user_data;
    gboolean started;
    guint start2_id;
};

static
gboolean
test_dbus_start2(
    gpointer user_data)
{
    TestDBus* self = user_data;

    GDEBUG("Starting test stage 2");
    self->start2_id = 0;
    self->start2(self->client_connection, self->server_connection,
        self->user_data);
    return G_SOURCE_REMOVE;
}

static
void
test_dbus_start(
    TestDBus* self)
{
    if ((self->start || self->start2) && !self->started) {
        self->started = TRUE;
        if (self->start) {
            GDEBUG("Starting the test");
            self->start(self->client_connection, self->server_connection,
                self->user_data);
        }
        if (self->start2) {
            self->start2_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                test_dbus_start2, self, NULL);
        }
    }
}

static
void
test_dbus_client_connection(
    GObject* source,
    GAsyncResult* res,
    gpointer user_data)
{
    TestDBus* self = user_data;

    GDEBUG("Got client connection");
    g_assert(!self->client_connection);
    self->client_connection = g_dbus_connection_new_finish(res, NULL);
    g_assert(self->client_connection);
    if (self->client_connection && self->server_connection && !self->started) {
        test_dbus_start(self);
    }
}

static
gboolean
test_dbus_server_connection(
    GDBusServer* server,
    GDBusConnection* connection,
    gpointer user_data)
{
    TestDBus* self = user_data;

    GDEBUG("Got server connection");
    g_assert(!self->server_connection);
    g_object_ref(self->server_connection = connection);
    if (self->client_connection && self->server_connection && !self->started) {
        test_dbus_start(self);
    }
    return TRUE;
}

static
gboolean
test_dbus_authorize(
    GDBusAuthObserver* observer,
    GIOStream* stream,
    GCredentials* credentials,
    gpointer user_data)
{
    GDEBUG("Authorizing server connection");
    return TRUE;
}

TestDBus*
test_dbus_new(
    TestDBusStartFunc start,
    void* user_data)
{
    return test_dbus_new2(start, NULL, user_data);
}

TestDBus*
test_dbus_new2(
    TestDBusStartFunc start,
    TestDBusStartFunc start2,
    void* user_data)
{
    TestDBus* self = g_new0(TestDBus, 1);
    char* guid = g_dbus_generate_guid();
    char* tmpaddr;
    const char* client_addr;

    self->start = start;
    self->start2 = start2;
    self->user_data = user_data;
    self->tmpdir = g_dir_make_tmp("test_dbus_XXXXXX", NULL);
    tmpaddr = g_strconcat("unix:tmpdir=", self->tmpdir, NULL);

    self->observer = g_dbus_auth_observer_new();
    g_assert(g_signal_connect(self->observer, "authorize-authenticated-peer",
        G_CALLBACK(test_dbus_authorize), self));

    GDEBUG("DBus server address %s", tmpaddr);
    self->server = g_dbus_server_new_sync(tmpaddr, G_DBUS_SERVER_FLAGS_NONE,
        guid, self->observer, NULL, NULL);
    g_assert(self->server);
    g_assert(g_signal_connect(self->server, "new-connection",
        G_CALLBACK(test_dbus_server_connection), self));
    g_dbus_server_start(self->server);

    client_addr = g_dbus_server_get_client_address(self->server);
    GDEBUG("D-Bus client address %s", client_addr);
    g_dbus_connection_new_for_address(client_addr,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, NULL, NULL,
        test_dbus_client_connection, self);

    g_free(tmpaddr);
    g_free(guid);
    return self;
}

void
test_dbus_free(
    TestDBus* self)
{
    if (self) {
        if (self->start2_id) {
            g_source_remove(self->start2_id);
        }
        if (self->client_connection) {
            g_object_unref(self->client_connection);
        }
        if (self->server_connection) {
            g_object_unref(self->server_connection);
        }
        g_object_unref(self->observer);
        g_dbus_server_stop(self->server);
        g_rmdir(self->tmpdir);
        g_free(self->tmpdir);
        g_free(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
