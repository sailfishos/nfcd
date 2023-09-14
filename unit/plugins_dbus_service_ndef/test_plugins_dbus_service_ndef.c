/*
 * Copyright (C) 2022-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2022 Jolla Ltd.
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

#include "nfc_tag_p.h"
#include "nfc_adapter.h"
#include "nfc_tag_t2.h"
#include "nfc_ndef.h"

#include "internal/nfc_manager_i.h"

#include "dbus_service/dbus_service.h"
#include "dbus_service/dbus_service_util.h"

#include "test_common.h"
#include "test_adapter.h"
#include "test_target_t2.h"
#include "test_dbus.h"
#include "test_dbus_name.h"

#include <gutil_idlepool.h>

#define NFC_TAG_NDEF_INTERFACE "org.sailfishos.nfc.NDEF"
#define MIN_INTERFACE_VERSION (1)
#define TEST_DUMP_VARIANT_DATA(v) \
    GDEBUG_DUMP(g_variant_get_data(v), g_variant_get_size(v))
#define TEST_DBUS_TIMEOUT \
    ((test_opt.flags & TEST_FLAG_DEBUG) ? -1 : TEST_TIMEOUT_MS)

static TestOpt test_opt;
static const char test_sender[] = ":1.1";
static const char test_type[] = { 'U' };
static const guint8 test_nfcid1[] = {0x04, 0x9b, 0xfb, 0x4a, 0xeb, 0x2b, 0x80};
static const guint test_payload_offset = 22;
static const guint test_payload_size = 15;
static const guint test_raw_data_offset = 18;
static const guint test_raw_data_size = 19;
static const guint8 test_tag_data[] = {
    0x04, 0xd4, 0xfb, 0xa3, 0x4a, 0xeb, 0x2b, 0x80,
    0x0a, 0x48, 0x00, 0x00, 0xe1, 0x10, 0x12, 0x00,
    0x03, 0x13, 0xd1, 0x01, 0x0f, 0x55, 0x04, 0x73,
    0x61, 0x69, 0x6c, 0x66, 0x69, 0x73, 0x68, 0x6f,
    0x73, 0x2e, 0x6f, 0x72, 0x67, 0xfe, 0x00, 0x00,
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
        tag->name, "/ndef0", NULL);
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
test_call_no_args(
    TestData* test,
    const char* method,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL, test_tag_path(test),
        NFC_TAG_NDEF_INTERFACE, method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
        TEST_DBUS_TIMEOUT, NULL, callback, test);
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
 * get_interfaces
 *==========================================================================*/

static
void
test_get_interfaces_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gchar** ifaces = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(^as)", &ifaces);
    g_assert(ifaces);
    GDEBUG("%u interface(s)", g_strv_length(ifaces));
    g_assert_cmpint(g_strv_length(ifaces), >= ,1);
    g_assert_cmpstr(ifaces[0], == ,NFC_TAG_NDEF_INTERFACE);

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
    test_start_and_call((TestData*)user_data, client, server,
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
 * get_all
 *==========================================================================*/

static
void
test_get_all_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    guint flags = 0;
    guint tnf = 0;
    gchar** ifaces = NULL;
    GVariant* type = NULL;
    GVariant* id = NULL;
    GVariant* payload = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(iuu^as@ay@ay@ay)", &version, &flags, &tnf, &ifaces,
        &type, &id, &payload);
    g_assert(ifaces);
    g_assert(type);
    g_assert(id);
    g_assert(payload);

    GDEBUG("version=%d", version);
    g_assert_cmpint(version, >= ,MIN_INTERFACE_VERSION);

    GDEBUG("%u interface(s)", g_strv_length(ifaces));
    g_assert_cmpint(g_strv_length(ifaces), >= ,1);
    g_assert_cmpstr(ifaces[0], == ,NFC_TAG_NDEF_INTERFACE);

    GDEBUG("flags=0x%02x", flags);
    g_assert_cmpint(flags, == , NFC_NDEF_REC_FLAG_FIRST |
        NFC_NDEF_REC_FLAG_LAST);

    GDEBUG("tnf=%u", tnf);
    g_assert_cmpint(tnf, == , NFC_NDEF_TNF_WELL_KNOWN);

    g_assert(type);
    GDEBUG("type %u byte(s)", (guint) g_variant_get_size(type));
    TEST_DUMP_VARIANT_DATA(type);
    g_assert_cmpuint(g_variant_get_size(type), == ,sizeof(test_type));
    g_assert(!memcmp(g_variant_get_data(type), test_type, sizeof(test_type)));

    g_assert(id);
    GDEBUG("id %u byte(s)", (guint) g_variant_get_size(id));
    g_assert_cmpuint(g_variant_get_size(id), == ,0);

    g_assert(payload);
    GDEBUG("payload %u byte(s)", (guint) g_variant_get_size(payload));
    TEST_DUMP_VARIANT_DATA(type);
    g_assert_cmpuint(g_variant_get_size(payload), == ,test_payload_size);
    g_assert(!memcmp(g_variant_get_data(payload), test_tag_data +
        test_payload_offset, test_payload_size));

    g_strfreev(ifaces);
    g_variant_unref(type);
    g_variant_unref(id);
    g_variant_unref(payload);
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
    test_start_and_call((TestData*)user_data, client, server,
        "GetAll", test_get_all_done);
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
 * get_flags
 *==========================================================================*/

static
void
test_get_flags_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint flags = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &flags);
    GDEBUG("flags=0x%02x", flags);
    g_assert_cmpint(flags, == , NFC_NDEF_REC_FLAG_FIRST |
        NFC_NDEF_REC_FLAG_LAST);
    g_variant_unref(var);

    test_quit_later(test->loop);
}

static
void
test_get_flags_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "GetFlags", test_get_flags_done);
}

static
void
test_get_flags(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_flags_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_tnf
 *==========================================================================*/

static
void
test_get_tnf_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint tnf = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(u)", &tnf);
    GDEBUG("tnf=%u", tnf);
    g_assert_cmpint(tnf, == , NFC_NDEF_TNF_WELL_KNOWN);
    g_variant_unref(var);

    test_quit_later(test->loop);
}

static
void
test_get_tnf_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "GetTypeNameFormat", test_get_tnf_done);
}

static
void
test_get_tnf(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_tnf_start, &test);
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
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok_data(conn, result, TEST_ARRAY_AND_SIZE(test_type));
    test_quit_later(test->loop);
}

static
void
test_get_type_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
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
 * get_id
 *==========================================================================*/

static
void
test_get_id_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok_data(conn, result, "", 0);
    test_quit_later(test->loop);
}

static
void
test_get_id_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "GetId", test_get_id_done);
}

static
void
test_get_id(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_id_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_payload
 *==========================================================================*/

static
void
test_get_payload_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok_data(conn, result, test_tag_data + test_payload_offset,
        test_payload_size);
    test_quit_later(test->loop);
}

static
void
test_get_payload_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "GetPayload", test_get_payload_done);
}

static
void
test_get_payload(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_payload_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_raw_data
 *==========================================================================*/

static
void
test_get_raw_data_done(
    GObject* conn,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok_data(conn, result, test_tag_data + test_raw_data_offset,
        test_raw_data_size);
    test_quit_later(test->loop);
}

static
void
test_get_raw_data_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    test_start_and_call((TestData*)user_data, client, server,
        "GetRawData", test_get_raw_data_done);
}

static
void
test_get_raw_data(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test);
    dbus = test_dbus_new(test_get_raw_data_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/ndef/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_interfaces"), test_get_interfaces);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_flags"), test_get_flags);
    g_test_add_func(TEST_("get_tnf"), test_get_tnf);
    g_test_add_func(TEST_("get_type"), test_get_type);
    g_test_add_func(TEST_("get_id"), test_get_id);
    g_test_add_func(TEST_("get_payload"), test_get_payload);
    g_test_add_func(TEST_("get_raw_data"), test_get_raw_data);
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
