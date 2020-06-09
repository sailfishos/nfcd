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

#include "test_common.h"
#include "test_adapter.h"
#include "test_target.h"
#include "test_dbus.h"

#include "dbus_service/dbus_service.h"

#include "internal/nfc_manager_i.h"
#include "nfc_plugins.h"
#include "nfc_adapter_p.h"
#include "nfc_target_p.h"
#include "nfc_tag_p.h"

#include <gutil_idlepool.h>

#define NFC_SERVICE "org.sailfishos.nfc.daemon"
#define NFC_TAG_INTERFACE "org.sailfishos.nfc.Tag"
#define MIN_INTERFACE_VERSION (2)

static TestOpt test_opt;
static const char test_sender_1[] = ":1.1";
static const char test_sender_2[] = ":1.2";
static const char* test_sender = test_sender_1;
static GSList* test_name_watches = NULL;
static guint test_name_watches_last_id = 0;

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    DBusServiceAdapter* service;
    GDBusConnection* connection;
    GUtilIdlePool* pool;
} TestData;

static
void
test_data_init(
    TestData* test)
{
    NfcPluginsInfo pi;
    NfcTarget* target;
    NfcParamPoll poll;

    g_assert(!test_name_watches);
    memset(test, 0, sizeof(*test));
    memset(&pi, 0, sizeof(pi));
    g_assert((test->manager = nfc_manager_new(&pi)) != NULL);
    g_assert((test->adapter = test_adapter_new()) != NULL);

    target = test_target_new();
    memset(&poll, 0, sizeof(poll));
    g_assert(nfc_adapter_add_other_tag2(test->adapter, target, &poll));
    nfc_target_unref(target);

    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    test->loop = g_main_loop_new(NULL, TRUE);
    test->pool = gutil_idle_pool_new();
}

static
void
test_data_cleanup(
    TestData* test)
{
    if (test->connection) {
        g_object_unref(test->connection);
    }
    nfc_adapter_unref(test->adapter);
    nfc_manager_unref(test->manager);
    dbus_service_adapter_free(test->service);
    g_main_loop_unref(test->loop);
    gutil_idle_pool_destroy(test->pool);
    g_assert(!test_name_watches);
}

static
const char*
test_tag_path(
    TestData* test,
    NfcTag* tag)
{
    char* path;

    g_assert(test->service);
    g_assert(tag);
    path = g_strconcat(dbus_service_adapter_path(test->service), "/",
        tag->name, NULL);
    gutil_idle_pool_add(test->pool, path, g_free);
    return path;
}

static
void
test_call_get(
    TestData* test,
    const char* method,
    GAsyncReadyCallback callback)
{
    NfcTag* tag = test->adapter->tags[0];

    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL, test_tag_path(test, tag),
        NFC_TAG_INTERFACE, method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
        NULL, callback, test);
}

static
void
test_call_acquire(
    TestData* test,
    gboolean wait,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL,
        test_tag_path(test, test->adapter->tags[0]), NFC_TAG_INTERFACE,
        "Acquire", g_variant_new("(b)", wait), NULL, G_DBUS_CALL_FLAGS_NONE,
        -1, NULL, callback, test);
}

static
void
test_call_release(
    TestData* test,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL,
        test_tag_path(test, test->adapter->tags[0]), NFC_TAG_INTERFACE,
        "Release", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
        callback, test);
}

static
void
test_complete_ok(
    GDBusConnection* connection,
    GAsyncResult* result)
{
    GError* error = NULL;
    GVariant* out = g_dbus_connection_call_finish(connection, result, &error);

    g_assert(out);
    g_variant_unref(out);
}

static
void
test_complete_error(
    GDBusConnection* connection,
    GAsyncResult* result,
    DBusServiceError code)
{
    GError* error = NULL;

    /* This call is expected to fail with org.sailfishos.nfc.Error.Aborted */
    g_assert(!g_dbus_connection_call_finish(connection, result, &error));
    g_assert(error->domain == DBUS_SERVICE_ERROR);
    g_assert(error->code == code);
    g_error_free(error);
}

static
void
test_start_and_get(
    TestData* test,
    GDBusConnection* client,
    GDBusConnection* server,
    const char* method,
    GAsyncReadyCallback callback)
{
    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_get(test, method, callback);
}

/*==========================================================================*
 * Stubs
 *
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

static
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

const char*
g_dbus_method_invocation_get_sender(
    GDBusMethodInvocation* call)
{
    return test_sender;
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
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    dbus_service_tag_free(NULL);
    g_assert(!dbus_service_tag_sequence(NULL, NULL));
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    NfcTag* tag = test->adapter->tags[0];
    NfcTargetSequence* seq;

    nfc_tag_set_initialized(tag);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);

    /* Can't register two D-Bus objects for the same path */
    g_assert(!dbus_service_tag_new(tag,
        dbus_service_adapter_path(test->service), server));

    g_assert((seq = nfc_target_sequence_new(tag->target)) != NULL);
    nfc_target_sequence_unref(seq);

    test_quit_later(test->loop);
}

static
void
test_basic(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_basic_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_all
 *==========================================================================*/

static
void
test_get_all_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    gboolean present = FALSE;
    guint tech, protocol, type;
    gchar** ifaces = NULL;
    gchar** records = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(ibuuu^as^ao)", &version, &present, &tech,
        &protocol, &type, &ifaces, &records);
    g_assert(ifaces);
    g_assert(records);
    GDEBUG("version=%d, present=%d, tech=%u, protocol=%u, type=%u, "
        "%u interface(s), %u record(s)", version, present, tech, protocol,
           type, g_strv_length(ifaces), g_strv_length(records));
    g_assert(version >= MIN_INTERFACE_VERSION);
    g_assert(present);
    g_assert(tech == NFC_TECHNOLOGY_UNKNOWN);
    g_assert(protocol == NFC_PROTOCOL_UNKNOWN);
    g_assert(g_strv_length(records) == 0);
    g_strfreev(ifaces);
    g_strfreev(records);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_all_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_get(test, client, server, "GetAll", test_get_all_done);
}

static
void
test_get_all(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_all_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_interface_version
 *==========================================================================*/

static
void
test_get_interface_version_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(i)", &version);
    GDEBUG("version=%d", version);
    g_assert(version >= MIN_INTERFACE_VERSION);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_interface_version_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetInterfaceVersion", test_get_interface_version_done);
}

static
void
test_get_interface_version(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_interface_version_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_present
 *==========================================================================*/

static
void
test_get_present_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gboolean present = FALSE;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(b)", &present);
    GDEBUG("present=%d", present);
    g_assert(present);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_present_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetPresent", test_get_present_done);
}

static
void
test_get_present(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_present_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_technology
 *==========================================================================*/

static
void
test_get_technology_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint tech = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &tech);
    GDEBUG("tech=%u", tech);
    g_assert(tech == NFC_TECHNOLOGY_UNKNOWN);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_technology_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetTechnology", test_get_technology_done);
}

static
void
test_get_technology(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_technology_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_protocol
 *==========================================================================*/

static
void
test_get_protocol_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint protocol = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &protocol);
    GDEBUG("protocol=%u", protocol);
    g_assert(protocol == NFC_PROTOCOL_UNKNOWN);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_protocol_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetProtocol", test_get_protocol_done);
}

static
void
test_get_protocol(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_protocol_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_type
 *==========================================================================*/

static
void
test_get_type_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint type = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &type);
    GDEBUG("type=%u", type);
    g_assert(type == NFC_TAG_TYPE_UNKNOWN);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_type_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_get((TestData*)user_data, client, server,
        "GetType", test_get_type_done);
}

static
void
test_get_type(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_type_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_interfaces
 *==========================================================================*/

static
void
test_get_interfaces_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** ifaces = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(^as)", &ifaces);
    g_assert(ifaces);
    GDEBUG("%u interface(s)", g_strv_length(ifaces));
    g_strfreev(ifaces);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_interfaces_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_get(test, client, server,
        "GetInterfaces", test_get_interfaces_done);
}

static
void
test_get_interfaces(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_interfaces_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_records
 *==========================================================================*/

static
void
test_get_records_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** records = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(^ao)", &records);
    g_assert(records);
    GDEBUG("%u record(s)", g_strv_length(records));
    g_assert(g_strv_length(records) == 0);
    g_strfreev(records);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_get_records_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_get(test, client, server,
        "GetNdefRecords", test_get_records_done);
}

static
void
test_get_records(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_records_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * early_free
 *==========================================================================*/

static
void
test_early_free_done(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_error(G_DBUS_CONNECTION(connection), result,
        DBUS_SERVICE_ERROR_ABORTED);
    test_quit_later(test->loop);
}

static
void
test_early_free_continue(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok(G_DBUS_CONNECTION(connection), result);
    /* This completes pending GetInterfaces with an error */
    nfc_tag_deactivate(test->adapter->tags[0]);
}

static
void
test_early_free_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_start_and_get(test, client, server, "GetInterfaces",
        test_early_free_done);
    /* Wait for GetInterfaceVersion to complete before continuing */
    test_call_get(test, "GetInterfaceVersion", test_early_free_continue);
}

static
void
test_early_free(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_early_free_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * early_free2
 *==========================================================================*/

static
void
test_early_free2_locked(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock acquired (1)");
    /* Change the sender */
    test_sender = test_sender_2;
    /* This one is going to be placed to the queue and then dropped */
    test_call_acquire(test, TRUE, test_early_free_done);
    /* Wait for GetInterfaceVersion to complete before continuing */
    test_call_get(test, "GetInterfaceVersion", test_early_free_continue);
}

static
void
test_early_free2_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_sender = test_sender_1;
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_object_ref(test->connection = client);
    g_assert(test->service);
    test_call_acquire(test, TRUE, test_early_free2_locked);
}

static
void
test_early_free2(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_early_free2_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * block
 *==========================================================================*/

static
void
test_block_get_interfaces_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    gchar** ifaces = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(^as)", &ifaces);
    g_assert(ifaces);
    GDEBUG("%u interface(s)", g_strv_length(ifaces));
    g_strfreev(ifaces);
    g_variant_unref(var);
    /* And wait for test_get_all_done() to finish the test */
}

static
void
test_block_continue(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok(G_DBUS_CONNECTION(connection), result);
    /* This unblocks pending GetInterfaces and GetAll calls */
    nfc_tag_set_initialized(test->adapter->tags[0]);
}

static
void
test_block_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_start_and_get(test, client, server, "GetInterfaces",
        test_block_get_interfaces_done);

    /* test_get_all_done() fill actually finish the test */
    test_call_get(test, "GetAll", test_get_all_done);

    /* Wait for GetInterfaceVersion to complete before continuing */
    test_call_get(test, "GetInterfaceVersion", test_block_continue);
}

static
void
test_block(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_block_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * deactivate
 *==========================================================================*/

static
void
test_deactivate_tag_gone(
    GDBusConnection* connection,
    const char* sender,
    const char* path,
    const char* iface,
    const char* name,
    GVariant* args,
    gpointer user_data)
{
    TestData* test = user_data;

    GDEBUG("Tag %s is gone", path);
    test_quit_later(test->loop);
}

static
void
test_deactivate_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    NfcTag* tag = test->adapter->tags[0];
    const char* tag_path;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    tag_path = test_tag_path(test, tag);

    g_assert(g_dbus_connection_signal_subscribe(client, NULL,
        NFC_TAG_INTERFACE, "Removed", tag_path, NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, test_deactivate_tag_gone,
        test, NULL));
    /* Deactivation call will (eventually) cause Removed signal */
    g_dbus_connection_call(client, NULL, tag_path, NFC_TAG_INTERFACE,
        "Deactivate", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
        NULL, test);
}

static
void
test_deactivate(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_deactivate_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * lock
 *==========================================================================*/

static
void
test_lock_released_3(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error));
    g_error_free(error);
    GDEBUG("Release failed as expected, done!");
    test_quit_later(test->loop);
}

static
void
test_lock_released_2(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GDBusConnection* connection = G_DBUS_CONNECTION(object);

    test_complete_ok(connection, result);
    GDEBUG("Lock released (2)");
    /* This one is going to fail:*/
    test_call_release(test, test_lock_released_3);
}

static
void
test_lock_released_1(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    GDBusConnection* connection = G_DBUS_CONNECTION(object);

    test_complete_ok(connection, result);
    GDEBUG("Lock released (1)");
    test_call_release(test, test_lock_released_2);
}

static
void
test_lock_acquired_2(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    GDBusConnection* connection = G_DBUS_CONNECTION(object);

    test_complete_ok(connection, result);
    GDEBUG("Lock acquired (2)");
    test_call_release(test, test_lock_released_1);
}

static
void
test_lock_acquired_1(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    GDBusConnection* connection = G_DBUS_CONNECTION(object);

    test_complete_ok(connection, result);
    GDEBUG("Lock acquired (1)");
    test_call_acquire(test, TRUE, test_lock_acquired_2);
}

static
void
test_lock_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    g_object_ref(test->connection = client);
    test_call_acquire(test, TRUE, test_lock_acquired_1);
}

static
void
test_lock(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_lock_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * lock_drop_wait
 *==========================================================================*/

static
void
test_lock_drop_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Released lock 1");
    test_quit_later(test->loop);
}

static
void
test_lock_drop_wait_dropped(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

   GDEBUG("Pending lock 2 has been dropped");
    /* This call is expected to fail with org.sailfishos.nfc.Error.Aborted */
    test_complete_error(G_DBUS_CONNECTION(connection), result,
        DBUS_SERVICE_ERROR_ABORTED);

    /* Release the first lock */
    test_sender = test_sender_1;
    test_call_release(test, test_lock_drop_done);
}

static
void
test_lock_drop_wait_continue(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    test_complete_ok(G_DBUS_CONNECTION(connection), result);
    GDEBUG("Dropping pending lock 2");
    test_name_watch_vanish(test_sender_2);
}

static
void
test_lock_drop_wait_locked(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock acquired (1)");
    /* Change the sender */
    test_sender = test_sender_2;
    /* This one is going to be placed to the queue and then dropped */
    test_call_acquire(test, TRUE, test_lock_drop_wait_dropped);
    /* Wait for GetInterfaceVersion to complete before continuing */
    test_call_get(test, "GetInterfaceVersion", test_lock_drop_wait_continue);
}

static
void
test_lock_drop_wait_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_sender = test_sender_1;
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_object_ref(test->connection = client);
    g_assert(test->service);
    test_call_acquire(test, TRUE, test_lock_drop_wait_locked);
}

static
void
test_lock_drop_wait(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_lock_drop_wait_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * lock_release_wait
 *==========================================================================*/

static
void
test_lock_release_wait_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Released lock 1");
    test_quit_later(test->loop);
}

static
void
test_lock_release_wait_released(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    GDBusConnection* connection = G_DBUS_CONNECTION(object);

    test_complete_ok(connection, result);
    GDEBUG("Released pending lock 2");
    test_sender = test_sender_1;
    test_call_release(test, test_lock_release_wait_done);
}

static
void
test_lock_release_wait_dropped(
    GObject* connection,
    GAsyncResult* result,
    gpointer test)
{
    GDEBUG("Pending lock 2 has been dropped");
    /* This call is expected to fail with org.sailfishos.nfc.Error.Aborted */
    test_complete_error(G_DBUS_CONNECTION(connection), result,
        DBUS_SERVICE_ERROR_ABORTED);
}

static
void
test_lock_release_wait_continue(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok(G_DBUS_CONNECTION(connection), result);
    GDEBUG("Releasing pending lock 2");
    test_call_release(test, test_lock_release_wait_released);
}

static
void
test_lock_release_wait_locked(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock acquired (1)");
    /* Change the sender */
    test_sender = test_sender_2;
    /* This one is going to be placed to the queue and then cancelled */
    test_call_acquire(test, TRUE, test_lock_release_wait_dropped);
    /* Wait for GetInterfaceVersion to complete before continuing */
    test_call_get(test, "GetInterfaceVersion", test_lock_release_wait_continue);
}

static
void
test_lock_release_wait_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_sender = test_sender_1;
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_object_ref(test->connection = client);
    g_assert(test->service);
    test_call_acquire(test, TRUE, test_lock_release_wait_locked);
}

static
void
test_lock_release_wait(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_lock_release_wait_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * lock_wait
 *==========================================================================*/

static
void
test_lock_wait_released(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Released lock 2");
    test_quit_later(test->loop);
}

static
void
test_lock_wait_locked_again(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock acquired (2)");
    test_call_release(test, test_lock_wait_released);
}

static
void
test_lock_wait_continue(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    GDEBUG("Dropping lock 1");
    test_complete_ok(G_DBUS_CONNECTION(connection), result);
    test_name_watch_vanish(test_sender_1);
}

static
void
test_lock_wait_locked(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock acquired (1)");
    /* Change the sender */
    test_sender = test_sender_2;
    /* This one is going to be placed to the queue */
    test_call_acquire(test, TRUE, test_lock_wait_locked_again);
    /* Wait for GetInterfaceVersion to complete before continuing */
    test_call_get(test, "GetInterfaceVersion", test_lock_wait_continue);
}

static
void
test_lock_wait_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_sender = test_sender_1;
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    g_object_ref(test->connection = client);
    test_call_acquire(test, TRUE, test_lock_wait_locked);
}

static
void
test_lock_wait(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_lock_wait_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * lock_wait2
 *==========================================================================*/

static
void
test_lock_wait2_locked_again2(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock 2 acquired (2)");
    test_quit_later(test->loop);
}

static
void
test_lock_wait2_locked_again1(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock 2 acquired (1) ");
}

static
void
test_lock_wait2_released(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Released lock 1");
}
static
void
test_lock_wait2_continue(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    GDEBUG("Releasing lock 1");
    test_complete_ok(G_DBUS_CONNECTION(connection), result);
    test_sender = test_sender_1;
    test_call_release(test, test_lock_wait2_released);
}

static
void
test_lock_wait2_locked(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock 1 acquired");
    /* Change the sender */
    test_sender = test_sender_2;
    /* These two are going to be placed to the queue */
    test_call_acquire(test, TRUE, test_lock_wait2_locked_again1);
    test_call_acquire(test, TRUE, test_lock_wait2_locked_again2);
    /* Wait for GetInterfaceVersion to complete before continuing */
    test_call_get(test, "GetInterfaceVersion", test_lock_wait2_continue);
}

static
void
test_lock_wait2_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_sender = test_sender_1;
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    g_object_ref(test->connection = client);
    test_call_acquire(test, TRUE, test_lock_wait2_locked);
}

static
void
test_lock_wait2(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_lock_wait2_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * lock_fail
 *==========================================================================*/

static
void
test_lock_fail_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GError* error = NULL;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error));
    g_error_free(error);
    GDEBUG("Second lock failed, good!");
    test_quit_later(test->loop);
}

static
void
test_lock_fail_locked(
    GObject* object,
    GAsyncResult* result,
    gpointer test)
{
    test_complete_ok(G_DBUS_CONNECTION(object), result);
    GDEBUG("Lock acquired");
    /* Change the sender */
    test_sender = test_sender_2;
    test_call_acquire(test, FALSE, test_lock_fail_done);
}

static
void
test_lock_fail_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    test_sender = test_sender_1;
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    g_object_ref(test->connection = client);
    test_call_acquire(test, TRUE, test_lock_fail_locked);
}

static
void
test_lock_fail(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_lock_fail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/tag/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_present"), test_get_present);
    g_test_add_func(TEST_("get_technology"), test_get_technology);
    g_test_add_func(TEST_("get_protocol"), test_get_protocol);
    g_test_add_func(TEST_("get_type"), test_get_type);
    g_test_add_func(TEST_("get_interfaces"), test_get_interfaces);
    g_test_add_func(TEST_("get_records"), test_get_records);
    g_test_add_func(TEST_("early_free"), test_early_free);
    g_test_add_func(TEST_("early_free2"), test_early_free2);
    g_test_add_func(TEST_("block"), test_block);
    g_test_add_func(TEST_("deactivate"), test_deactivate);
    g_test_add_func(TEST_("lock"), test_lock);
    g_test_add_func(TEST_("lock_wait"), test_lock_wait);
    g_test_add_func(TEST_("lock_wait2"), test_lock_wait2);
    g_test_add_func(TEST_("lock_drop_wait"), test_lock_drop_wait);
    g_test_add_func(TEST_("lock_release_wait"), test_lock_release_wait);
    g_test_add_func(TEST_("lock_fail"), test_lock_fail);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
