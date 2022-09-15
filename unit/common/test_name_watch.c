/*
 * Copyright (C) 2019-2022 Jolla Ltd.
 * Copyright (C) 2019-2022 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2020 Open Mobile Platform LLC.
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

#include "test_name_watch.h"

#include <gio/gio.h>

/*==========================================================================*
 * Peer-to-peer D-Bus connection doesn't fully simulate the real bus
 * connection. Some tricks are necessary.
 *==========================================================================*/

typedef struct test_name_watch {
    guint id;
    char* name;
    GDBusConnection* connection;
    GBusNameVanishedCallback name_vanished;
    GDestroyNotify destroy;
    gpointer user_data;
    guint name_vanished_id;
} TestNameWatch;

static GSList* test_name_watches = NULL;
static guint test_name_watches_last_id = 0;

/*==========================================================================*
 * Stubs
 *==========================================================================*/

static
void
test_name_watch_free(
    TestNameWatch* watch)
{
    if (watch->destroy) {
        watch->destroy(watch->user_data);
    }
    if (watch->name_vanished_id) {
        g_source_remove(watch->name_vanished_id);
    }
    g_object_unref(watch->connection);
    g_free(watch->name);
    g_free(watch);
}

static
gboolean
test_name_watch_vanished(
    void* data)
{
    TestNameWatch* watch = data;

    watch->name_vanished_id = 0;
    watch->name_vanished(watch->connection, watch->name, watch->user_data);
    return G_SOURCE_REMOVE;
}

guint
g_bus_watch_name_on_connection(
    GDBusConnection* connection,
    const gchar* name,
    GBusNameWatcherFlags flags,
    GBusNameAppearedCallback name_appeared,
    GBusNameVanishedCallback name_vanished,
    gpointer user_data,
    GDestroyNotify destroy)
{
    TestNameWatch* watch = g_new0(TestNameWatch, 1);

    watch->id = ++test_name_watches_last_id;
    watch->name = g_strdup(name);
    watch->name_vanished = name_vanished;
    watch->destroy = destroy;
    watch->user_data = user_data;
    g_object_ref(watch->connection = connection);
    test_name_watches = g_slist_append(test_name_watches, watch);
    return watch->id;
}

void
g_bus_unwatch_name(
    guint id)
{
    GSList* l;

    for (l = test_name_watches; l; l = l->next) {
        TestNameWatch* watch = l->data;

        if (watch->id == id) {
            test_name_watches = g_slist_delete_link(test_name_watches, l);
            test_name_watch_free(watch);
            return;
        }
    }
    g_assert_not_reached();
}

/*==========================================================================*
 * Test API
 *==========================================================================*/

int
test_name_watch_count(
    void)
{
    GSList* l;
    int n;

    for (l = test_name_watches, n = 0; l; l = l->next, n++);
    return n;
}

void
test_name_watch_vanish(
    const char* name)
{
    GSList* l;

    for (l = test_name_watches; l; l = l->next) {
        TestNameWatch* watch = l->data;

        if (!strcmp(watch->name, name)) {
            if (watch->name_vanished && !watch->name_vanished_id) {
                watch->name_vanished_id = g_idle_add(test_name_watch_vanished,
                    watch);
            }
            return;
        }
    }
    g_assert_not_reached();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
