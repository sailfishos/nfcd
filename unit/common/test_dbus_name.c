/*
 * Copyright (C) 2019-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2019-2022 Jolla Ltd.
 * Copyright (C) 2020 Open Mobile Platform LLC.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "test_dbus_name.h"

#include <gutil_log.h>
#include <gutil_misc.h>

/*==========================================================================*
 * Peer-to-peer D-Bus connection doesn't fully simulate the real bus
 * connection. Some tricks are necessary.
 *==========================================================================*/

typedef struct test_bus_name_watch {
    guint id;
    char* name;
    GDBusConnection* connection;
    GBusNameVanishedCallback name_vanished;
    GDestroyNotify destroy;
    gpointer user_data;
    guint name_vanished_id;
} TestBusNameWatch;

typedef struct test_bus_name_own {
    guint id;
    char* name;
    GBusAcquiredCallback bus_acquired;
    GBusNameAcquiredCallback name_acquired;
    GBusNameLostCallback name_lost;
    GDestroyNotify destroy;
    gpointer user_data;
    guint bus_acquired_id;
    guint name_acquired_id;
    guint name_lost_id;
} TestBusNameOwn;

static GDBusConnection* test_name_own_connection;
static GSList* test_name_watch_list = NULL;
static GSList* test_name_own_list = NULL;
static guint test_dbus_name_last_id = 0;

/*==========================================================================*
 * Name watching
 *==========================================================================*/

static
void
test_name_watch_free(
    TestBusNameWatch* watch)
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
    TestBusNameWatch* watch = data;

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
    TestBusNameWatch* watch = g_new0(TestBusNameWatch, 1);

    GDEBUG("Watching '%s'", name);
    watch->id = ++test_dbus_name_last_id;
    watch->name = g_strdup(name);
    watch->name_vanished = name_vanished;
    watch->destroy = destroy;
    watch->user_data = user_data;
    g_object_ref(watch->connection = connection);
    test_name_watch_list = g_slist_append(test_name_watch_list, watch);
    return watch->id;
}

void
g_bus_unwatch_name(
    guint id)
{
    GSList* l;

    for (l = test_name_watch_list; l; l = l->next) {
        TestBusNameWatch* watch = l->data;

        if (watch->id == id) {
            GDEBUG("Unwatching '%s'", watch->name);
            test_name_watch_list = g_slist_delete_link(test_name_watch_list, l);
            test_name_watch_free(watch);
            return;
        }
    }
    g_assert_not_reached();
}

/*==========================================================================*
 * Name owning
 *==========================================================================*/

static
gboolean
test_name_own_name_lost(
    void* data)
{
    TestBusNameOwn* own = data;

    own->name_lost_id = 0;
    own->name_lost(test_name_own_connection, own->name, own->user_data);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_name_own_name_acquired(
    void* data)
{
    TestBusNameOwn* own = data;

    GDEBUG("Name '%s' is acquired", own->name);
    own->name_acquired_id = 0;
    own->name_acquired(test_name_own_connection, own->name, own->user_data);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_name_own_bus_acquired(
    void* data)
{
    TestBusNameOwn* own = data;

    GDEBUG("But for '%s' is acquired", own->name);
    own->bus_acquired_id = 0;
    if (own->name_acquired) {
        own->name_acquired_id = g_idle_add(test_name_own_name_acquired, own);
    }
    own->bus_acquired(test_name_own_connection, own->name, own->user_data);
    return G_SOURCE_REMOVE;
}

static
void
test_name_own_free(
    TestBusNameOwn* own)
{
    if (own->destroy) {
        own->destroy(own->user_data);
    }
    if (own->bus_acquired_id) {
        g_source_remove(own->bus_acquired_id);
    }
    if (own->name_acquired_id) {
        g_source_remove(own->name_acquired_id);
    }
    if (own->name_lost_id) {
        g_source_remove(own->name_lost_id);
    }
    g_free(own->name);
    g_free(own);
}

guint
g_bus_own_name(
    GBusType bus_type,
    const gchar* name,
    GBusNameOwnerFlags flags,
    GBusAcquiredCallback bus_acquired,
    GBusNameAcquiredCallback name_acquired,
    GBusNameLostCallback name_lost,
    gpointer user_data,
    GDestroyNotify destroy)
{
    TestBusNameOwn* own = g_new0(TestBusNameOwn, 1);

    own->id = ++test_dbus_name_last_id;
    own->name = g_strdup(name);
    own->bus_acquired = bus_acquired;
    own->name_acquired = name_acquired;
    own->name_lost = name_lost;
    own->destroy = destroy;
    own->user_data = user_data;

    GDEBUG("Owning '%s'", name);
    test_name_own_list = g_slist_append(test_name_own_list, own);
    if (test_name_own_connection) {
        if (bus_acquired) {
            own->bus_acquired_id =
                g_idle_add(test_name_own_bus_acquired, own);
        } else if (name_acquired) {
            own->name_acquired_id =
                g_idle_add(test_name_own_name_acquired, own);
        }
    }
    return own->id;
}

void
g_bus_unown_name(
    guint id)
{
    GSList* l;

    for (l = test_name_own_list; l; l = l->next) {
        TestBusNameOwn* own = l->data;

        if (own->id == id) {
            GDEBUG("Dropping '%s'", own->name);
            test_name_own_list = g_slist_delete_link(test_name_own_list, l);
            test_name_own_free(own);
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
    return g_slist_length(test_name_watch_list);
}

void
test_name_watch_vanish(
    const char* name)
{
    GSList* l;

    for (l = test_name_watch_list; l; l = l->next) {
        TestBusNameWatch* watch = l->data;

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

void
test_name_own_set_connection(
    GDBusConnection* connection)
{
    if (test_name_own_connection != connection) {
        GSList* l;

        gutil_object_unref(test_name_own_connection);
        test_name_own_connection = gutil_object_ref(connection);
        for (l = test_name_own_list; l; l = l->next) {
            TestBusNameOwn* own = l->data;

            if (own->bus_acquired_id) {
                g_source_remove(own->bus_acquired_id);
                own->bus_acquired_id = 0;
            }
            if (own->name_acquired_id) {
                g_source_remove(own->name_acquired_id);
                own->name_acquired_id = 0;
            }
            if (own->name_lost_id) {
                g_source_remove(own->name_lost_id);
                own->name_lost_id = 0;
            }
            if (connection) {
                if (own->bus_acquired) {
                    own->bus_acquired_id =
                        g_idle_add(test_name_own_bus_acquired, own);
                } else if (own->name_acquired) {
                    own->name_acquired_id =
                        g_idle_add(test_name_own_name_acquired, own);
                }
            } else if (own->name_lost) {
                own->name_lost_id = g_idle_add(test_name_own_name_lost, own);
            }
        }
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
