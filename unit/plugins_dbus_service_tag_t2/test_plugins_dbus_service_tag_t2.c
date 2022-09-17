/*
 * Copyright (C) 2022 Jolla Ltd.
 * Copyright (C) 2022 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_tag_p.h"
#include "nfc_adapter.h"
#include "nfc_tag_t2.h"

#include "internal/nfc_manager_i.h"

#include "dbus_service/dbus_service.h"
#include "dbus_service/dbus_service_util.h"

#include "test_common.h"
#include "test_adapter.h"
#include "test_target_t2.h"
#include "test_dbus.h"
#include "test_name_watch.h"

#include <gutil_idlepool.h>

#define NFC_TAG_T2_INTERFACE "org.sailfishos.nfc.TagType2"
#define DBUS_SERVICE_ERROR_(error) "org.sailfishos.nfc.Error." error
#define MIN_INTERFACE_VERSION (1)
#define TEST_DATA_SIZE (sizeof(test_tag_data) - TEST_TARGET_T2_DATA_OFFSET)
#define TEST_DUMP_VARIANT_DATA(v) \
    GDEBUG_DUMP(g_variant_get_data(v), g_variant_get_size(v))

static TestOpt test_opt;
static const char test_sender[] = ":1.1";
static const guint8 test_nfcid1[] = {0x04, 0x9b, 0xfb, 0x4a, 0xeb, 0x2b, 0x80};
static const guint8 test_write_data[4] = { 0x01, 0x02, 0x03, 0x04 };
static const guint8 test_tag_data[] = {
    0x04, 0xd4, 0xfb, 0xa3, 0x4a, 0xeb, 0x2b, 0x80,
    0x0a, 0x48, 0x00, 0x00, 0xe1, 0x10, 0x12, 0x00,
    0x01, 0x03, 0xa0, 0x10, 0x44, 0x03, 0x00, 0xfe,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    TestTargetT2* target;
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
    NfcTagParamT2 param;

    g_assert(!test_name_watch_count());
    memset(test, 0, sizeof(*test));
    memset(&pi, 0, sizeof(pi));
    g_assert((test->manager = nfc_manager_new(&pi)) != NULL);
    g_assert((test->adapter = test_adapter_new()) != NULL);
    g_assert((test->target = test_target_t2_new
        (TEST_ARRAY_AND_SIZE(test_tag_data))) != NULL);

    memset(&param, 0, sizeof(param));
    TEST_BYTES_SET(param.nfcid1, test_nfcid1);
    g_assert(nfc_adapter_add_tag_t2(test->adapter, NFC_TARGET(test->target),
        &param));

    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    test->loop = g_main_loop_new(NULL, TRUE);
    test->pool = gutil_idle_pool_new();
}

static
void
test_data_cleanup(
    TestData* test)
{
    nfc_target_unref(NFC_TARGET(test->target));
    nfc_manager_stop(test->manager, 0);
    if (test->connection) {
        g_object_unref(test->connection);
    }
    nfc_adapter_unref(test->adapter);
    nfc_manager_unref(test->manager);
    dbus_service_adapter_free(test->service);
    g_main_loop_unref(test->loop);
    gutil_idle_pool_destroy(test->pool);
    g_assert(!test_name_watch_count());
}

static
const char*
test_tag_path(
    TestData* test)
{
    NfcTag* tag = test->adapter->tags[0];
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
test_complete_ok_data(
    GObject* conn,
    GAsyncResult* result,
    const void* expect_data,
    gsize expect_size)
{
    GVariant* data = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(@ay)", &data);

    g_assert(data);
    GDEBUG("%u bytes", (guint) g_variant_get_size(data));
    TEST_DUMP_VARIANT_DATA(data);
    g_assert_cmpuint(g_variant_get_size(data), == ,expect_size);
    g_assert(!memcmp(g_variant_get_data(data), expect_data, expect_size));

    g_variant_unref(data);
    g_variant_unref(var);
}

static
void
test_complete_error_failed(
    GObject* object,
    GAsyncResult* result)
{
    GError* error = NULL;
    char* error_name;

    g_assert(!g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error));
    g_assert(error);
    GDEBUG("%s", GERRMSG(error));
    error_name = g_dbus_error_get_remote_error(error);
    g_assert_cmpstr(error_name, == ,DBUS_SERVICE_ERROR_("Failed"));
    g_free(error_name);
    g_error_free(error);
}

static
void
test_expect_error_failed(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_error_failed(object, result);
    test_quit_later(test->loop);
}

static
void
test_call_no_args(
    TestData* test,
    const char* method,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL, test_tag_path(test),
        NFC_TAG_T2_INTERFACE, method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
        NULL, callback, test);
}

static
void
test_call_read(
    TestData* test,
    guint sector,
    guint block,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL, test_tag_path(test),
        NFC_TAG_T2_INTERFACE, "Read", g_variant_new("(uu)", sector, block),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, test);
}

static
void
test_call_read_data(
    TestData* test,
    guint offset,
    guint size,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL, test_tag_path(test),
        NFC_TAG_T2_INTERFACE, "ReadData", g_variant_new("(uu)", offset, size),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, test);
}

static
void
test_call_write(
    TestData* test,
    guint sector,
    guint block,
    const void* data,
    guint size,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL, test_tag_path(test),
        NFC_TAG_T2_INTERFACE, "Write", g_variant_new("(uu@ay)", sector, block,
        dbus_service_dup_byte_array_as_variant(data, size)), NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, test);
}

static
void
test_call_write_data(
    TestData* test,
    guint offset,
    const void* data,
    guint size,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL, test_tag_path(test),
        NFC_TAG_T2_INTERFACE, "WriteData", g_variant_new("(u@ay)", offset,
        dbus_service_dup_byte_array_as_variant(data, size)), NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, test);
}

static
void
test_start_and_call(
    TestData* test,
    GDBusConnection* client,
    GDBusConnection* server,
    const char* method,
    GAsyncReadyCallback callback)
{
    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_no_args(test, method, callback);
}

/*==========================================================================*
 * Stubs
 *==========================================================================*/

const char*
g_dbus_method_invocation_get_sender(
    GDBusMethodInvocation* call)
{
    return test_sender;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    dbus_service_tag_t2_free(NULL);
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
    guint block_size, data_size;
    GVariant* serial;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(iuu@ay)", &version, &block_size, &data_size, &serial);
    g_assert(serial);
    GDEBUG("version=%d, block_size=%u, data_size=%u, serial %u bytes",
        version, block_size, data_size,  (guint) g_variant_get_size(serial));
    TEST_DUMP_VARIANT_DATA(serial);
    g_assert_cmpuint(g_variant_get_size(serial), == ,sizeof(test_nfcid1));
    g_assert(!memcmp(g_variant_get_data(serial), test_nfcid1,
        sizeof(test_nfcid1)));
    g_assert_cmpint(version, >= ,MIN_INTERFACE_VERSION);
    g_assert_cmpuint(block_size, == ,TEST_TARGET_T2_BLOCK_SIZE);
    g_assert_cmpuint(data_size, == ,TEST_DATA_SIZE);
    g_variant_unref(serial);
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
    test_start_and_call(test, client, server, "GetAll", test_get_all_done);
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
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(i)", &version);
    GDEBUG("version=%d", version);
    g_assert_cmpint(version, >= ,MIN_INTERFACE_VERSION);
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
    test_start_and_call((TestData*)user_data, client, server,
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
 * get_block_size
 *==========================================================================*/

static
void
test_get_block_size_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint block_size = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &block_size);
    GDEBUG("block_size=%u", block_size);
    g_assert_cmpuint(block_size, == ,TEST_TARGET_T2_BLOCK_SIZE);
    g_variant_unref(var);

    test_quit_later(test->loop);
}

static
void
test_get_block_size_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "GetBlockSize", test_get_block_size_done);
}

static
void
test_get_block_size(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_block_size_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_data_size
 *==========================================================================*/

static
void
test_get_data_size_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint data_size = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &data_size);
    GDEBUG("data_size=%u", data_size);
    g_assert_cmpuint(data_size, == ,TEST_DATA_SIZE);
    g_variant_unref(var);

    test_quit_later(test->loop);
}

static
void
test_get_data_size_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "GetDataSize", test_get_data_size_done);
}

static
void
test_get_data_size(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_data_size_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_serial
 *==========================================================================*/

static
void
test_get_serial_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok_data(conn, result, TEST_ARRAY_AND_SIZE(test_nfcid1));
    test_quit_later(test->loop);
}

static
void
test_get_serial_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "GetSerial", test_get_serial_done);
}

static
void
test_get_serial(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_serial_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read/ok
 *==========================================================================*/

static
void
test_read_ok_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok_data(conn, result, test_tag_data,
        TEST_TARGET_T2_READ_SIZE);
    test_quit_later(test->loop);
}

static
void
test_read_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_read(test, 0, 0, test_read_ok_done);
}

static
void
test_read_ok(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_read_ok_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read/nack
 *==========================================================================*/

static
void
test_read_nack_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_read(test, 0, 16, test_expect_error_failed);
}

static
void
test_read_nack(
    void)
{
    TestData test;
    TestDBus* dbus;
    TestTargetT2Error error;

    test_data_init(&test);

    /* Generate NACK for block #16 (not fetched during initialization) */
    memset(&error, 0, sizeof(error));
    error.block = 16;
    error.type = TEST_TARGET_T2_ERROR_NACK;
    test.target->read_error = &error;

    dbus = test_dbus_new(test_read_nack_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read/txfail
 *==========================================================================*/

static
void
test_read_txfail_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test->target->transmit_error = 1; /* Simulate 1 transmission failure */
    test_call_read(test, 0, 0, test_expect_error_failed);
}

static
void
test_read_txfail(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_read_txfail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read_data/bad_block
 *==========================================================================*/

static
void
test_read_data_bad_block_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_read_data(test, 999999, 1, test_expect_error_failed);
}

static
void
test_read_data_bad_block(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_read_data_bad_block_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read_data/ok
 *==========================================================================*/

static
void
test_read_data_ok_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok_data(conn, result, test_tag_data +
        TEST_TARGET_T2_DATA_OFFSET, TEST_TARGET_T2_READ_SIZE);
    test_quit_later(test->loop);
}

static
void
test_read_data_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_read_data(test, 0, TEST_TARGET_T2_READ_SIZE,
        test_read_data_ok_done);
}

static
void
test_read_data_ok(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_read_data_ok_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read_data/nack
 *==========================================================================*/

static
void
test_read_data_nack_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_read_data(test, 8 - TEST_TARGET_T2_FIRST_DATA_BLOCK,
        TEST_TARGET_T2_READ_SIZE, test_expect_error_failed);
}

static
void
test_read_data_nack(
    void)
{
    TestData test;
    TestDBus* dbus;
    TestTargetT2Error error;

    test_data_init(&test);

    /* Generate NACK for block #8 (not fetched during initialization) */
    memset(&error, 0, sizeof(error));
    error.block = 8;
    error.type = TEST_TARGET_T2_ERROR_NACK;
    test.target->read_error = &error;

    dbus = test_dbus_new(test_read_data_nack_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read_data/txfail
 *==========================================================================*/

static
void
test_read_data_txfail_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test->target->transmit_error = 1; /* Simulate 1 transmission failure */
    test_call_read_data(test, 16, TEST_TARGET_T2_READ_SIZE,
        test_expect_error_failed);
}

static
void
test_read_data_txfail(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_read_data_txfail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read_all_data/ok
 *==========================================================================*/

static
void
test_read_all_data_ok_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok_data(conn, result, test_tag_data +
        TEST_TARGET_T2_DATA_OFFSET, TEST_DATA_SIZE);
    test_quit_later(test->loop);
}

static
void
test_read_all_data_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "ReadAllData", test_read_all_data_ok_done);
}

static
void
test_read_all_data_ok(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_read_all_data_ok_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read_all_data/nack
 *==========================================================================*/

static
void
test_read_all_data_nack_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "ReadAllData", test_expect_error_failed);
}

static
void
test_read_all_data_nack(
    void)
{
    TestData test;
    TestDBus* dbus;

    TestTargetT2Error error;

    test_data_init(&test);

    /* Generate NACK for block #8 (not fetched during initialization) */
    memset(&error, 0, sizeof(error));
    error.block = 8;
    error.type = TEST_TARGET_T2_ERROR_NACK;
    test.target->read_error = &error;

    dbus = test_dbus_new(test_read_all_data_nack_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * read_all_data/txfail
 *==========================================================================*/

static
void
test_read_all_data_txfail_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test->target->transmit_error = 1; /* Simulate 1 transmission failure */
    test_call_no_args(test, "ReadAllData", test_expect_error_failed);
}

static
void
test_read_all_data_txfail(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_read_all_data_txfail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * write/ok
 *==========================================================================*/

static
void
test_write_ok_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint written = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &written);
    GDEBUG("written=%u", written);
    g_assert_cmpuint(written, == ,sizeof(test_write_data));
    g_variant_unref(var);

    test_quit_later(test->loop);
}

static
void
test_write_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_write(test, 0, TEST_TARGET_T2_FIRST_DATA_BLOCK,
        TEST_ARRAY_AND_SIZE(test_write_data), test_write_ok_done);
}

static
void
test_write_ok(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_write_ok_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * write/ioerr
 *==========================================================================*/

static
void
test_write_ioerr_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_write(test, 0, 8, TEST_ARRAY_AND_SIZE(test_write_data),
        test_expect_error_failed);
}

static
void
test_write_ioerr(
    void)
{
    TestData test;
    TestDBus* dbus;
    TestTargetT2Error error;

    test_data_init(&test);

    /* Generate I/O error for block #8 */
    memset(&error, 0, sizeof(error));
    error.block = 8;
    error.type = TEST_TARGET_T2_ERROR_TRANSMIT;
    test.target->write_error = &error;

    dbus = test_dbus_new(test_write_ioerr_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * write/txfail
 *==========================================================================*/

static
void
test_write_txfail_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test->target->transmit_error = 1; /* Simulate 1 transmission failure */
    test_call_write(test, 0, 0, TEST_ARRAY_AND_SIZE(test_write_data),
        test_expect_error_failed);
}

static
void
test_write_txfail(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_write_txfail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * write_data/ok
 *==========================================================================*/

static
void
test_write_data_ok_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    guint written = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &written);
    GDEBUG("written=%u", written);
    g_assert_cmpuint(written, == ,sizeof(test_write_data));
    g_variant_unref(var);

    test_quit_later(test->loop);
}

static
void
test_write_data_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_write_data(test, 0, TEST_ARRAY_AND_SIZE(test_write_data),
        test_write_data_ok_done);
}

static
void
test_write_data_ok(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_write_data_ok_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * write_data/ioerr
 *==========================================================================*/

static
void
test_write_data_ioerr_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_write_data(test, 0, TEST_ARRAY_AND_SIZE(test_write_data),
        test_expect_error_failed);
}

static
void
test_write_data_ioerr(
    void)
{
    TestData test;
    TestDBus* dbus;
    TestTargetT2Error error;

    test_data_init(&test);

    /* Generate I/O error for the first data block */
    memset(&error, 0, sizeof(error));
    error.block = TEST_TARGET_T2_FIRST_DATA_BLOCK;
    error.type = TEST_TARGET_T2_ERROR_TRANSMIT;
    test.target->write_error = &error;

    dbus = test_dbus_new(test_write_data_ioerr_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * write_data/txfail
 *==========================================================================*/

static
void
test_write_data_txfail_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test->target->transmit_error = 1; /* Simulate 1 transmission failure */
    test_call_write_data(test, 0, TEST_ARRAY_AND_SIZE(test_write_data),
        test_expect_error_failed);
}

static
void
test_write_data_txfail(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_write_data_txfail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/tag_t2/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_block_size"), test_get_block_size);
    g_test_add_func(TEST_("get_data_size"), test_get_data_size);
    g_test_add_func(TEST_("get_serial"), test_get_serial);
    g_test_add_func(TEST_("read/ok"), test_read_ok);
    g_test_add_func(TEST_("read/nack"), test_read_nack);
    g_test_add_func(TEST_("read/txfail"), test_read_txfail);
    g_test_add_func(TEST_("read_data/ok"), test_read_data_ok);
    g_test_add_func(TEST_("read_data/nack"), test_read_data_nack);
    g_test_add_func(TEST_("read_data/txfail"), test_read_data_txfail);
    g_test_add_func(TEST_("read_data/bad_block"), test_read_data_bad_block);
    g_test_add_func(TEST_("read_all_data/ok"), test_read_all_data_ok);
    g_test_add_func(TEST_("read_all_data/nack"), test_read_all_data_nack);
    g_test_add_func(TEST_("read_all_data/txfail"), test_read_all_data_txfail);
    g_test_add_func(TEST_("write/ok"), test_write_ok);
    g_test_add_func(TEST_("write/ioerr"), test_write_ioerr);
    g_test_add_func(TEST_("write/txfail"), test_write_txfail);
    g_test_add_func(TEST_("write_data/ok"), test_write_data_ok);
    g_test_add_func(TEST_("write_data/ioerr"), test_write_data_ioerr);
    g_test_add_func(TEST_("write_data/txfail"), test_write_data_txfail);
    g_test_init(&argc, &argv, NULL);
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
