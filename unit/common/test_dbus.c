/*
 * Copyright (C) 2019-2022 Jolla Ltd.
 * Copyright (C) 2019-2022 Slava Monich <slava.monich@jolla.com>
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

#include <gutil_log.h>

#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include <sys/socket.h>
#include <sys/un.h>

struct test_dbus {
    GDBusConnection* client_connection;
    GDBusConnection* server_connection;
    TestDBusStartFunc start;
    TestDBusStartFunc start2;
    void* user_data;
    guint start_id;
    int fd[2];
};

static
GDBusConnection*
test_dbus_connection(
    int fd)
{
    GInputStream* in = g_unix_input_stream_new(fd, FALSE);
    GOutputStream* out = g_unix_output_stream_new(fd, FALSE);
    GIOStream* stream = g_simple_io_stream_new(in, out);
    GDBusConnection* connection = g_dbus_connection_new_sync(stream, NULL,
        G_DBUS_CONNECTION_FLAGS_NONE, NULL, NULL, NULL);

    g_assert(in);
    g_assert(out);
    g_assert(stream);
    g_assert(connection);
    g_object_unref(in);
    g_object_unref(out);
    g_object_unref(stream);
    return connection;
}

static
gboolean
test_dbus_start2(
    gpointer user_data)
{
    TestDBus* self = user_data;

    g_assert(self->start_id);
    self->start_id = 0;
    if (self->start2) {
        self->start2(self->client_connection, self->server_connection,
            self->user_data);
    }
    return G_SOURCE_REMOVE;
}

static
gboolean
test_dbus_start(
    gpointer user_data)
{
    TestDBus* self = user_data;

    GDEBUG("Starting the test");
    g_assert(self->start_id);
    if (self->start) {
        self->start(self->client_connection, self->server_connection,
            self->user_data);
    }
    if (self->start2) {
        self->start_id = g_idle_add_full(G_PRIORITY_LOW,
            test_dbus_start2, self, NULL);
    } else {
        self->start_id = 0;
    }
    return G_SOURCE_REMOVE;
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

    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, self->fd), == ,0);
    self->client_connection = test_dbus_connection(self->fd[0]);
    self->server_connection = test_dbus_connection(self->fd[1]);
    self->start = start;
    self->start2 = start2;
    self->user_data = user_data;
    self->start_id = g_idle_add_full(G_PRIORITY_LOW,
        test_dbus_start, self, NULL);
    return self;
}

void
test_dbus_free(
    TestDBus* self)
{
    if (self) {
        if (self->start_id) {
            g_source_remove(self->start_id);
        }
        g_object_unref(self->client_connection);
        g_object_unref(self->server_connection);
        g_assert_cmpint(close(self->fd[0]), == ,0);
        g_assert_cmpint(close(self->fd[1]), == ,0);
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
