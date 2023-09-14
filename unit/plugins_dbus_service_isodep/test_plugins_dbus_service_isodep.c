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
#include "nfc_tag_t4.h"

#include "internal/nfc_manager_i.h"

#include "dbus_service/dbus_service.h"

#include "test_common.h"
#include "test_adapter.h"
#include "test_target.h"
#include "test_dbus.h"
#include "test_dbus_name.h"

#include <gutil_idlepool.h>

#define NFC_ISODEP_INTERFACE "org.sailfishos.nfc.IsoDep"
#define MIN_INTERFACE_VERSION (3)

static TestOpt test_opt;
static const char test_sender[] = ":1.1";

#define TEST_DBUS_TIMEOUT \
    ((test_opt.flags & TEST_FLAG_DEBUG) ? -1 : TEST_TIMEOUT_MS)

typedef struct test_data {
    GMainLoop* loop;
    NfcManager* manager;
    NfcAdapter* adapter;
    DBusServiceAdapter* service;
    GDBusConnection* connection;
    GUtilIdlePool* pool;
} TestData;

typedef TestTargetClass TestTarget2Class;
typedef struct test_target2 {
    TestTarget parent;
    gboolean fail_reactivate;
    guint reactivate_id;
} TestTarget2;

G_DEFINE_TYPE(TestTarget2, test_target2, TEST_TYPE_TARGET)
#define TEST_TYPE_TARGET2 (test_target2_get_type())
#define TEST_TARGET2(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_TARGET2, TestTarget2))

#define TEST_CAN_REACTIVATE (0x01)
#define TEST_FAIL_REACTIVATE (0x02)
#define TEST_FAIL_TRANSMIT (0x04)

static
NfcTarget*
test_target_create(
    int flags)
{
    NfcTarget* target;

    if (flags & TEST_CAN_REACTIVATE) {
        TestTarget2* target2 = g_object_new(TEST_TYPE_TARGET2, NULL);

        target2->fail_reactivate = ((flags & TEST_FAIL_REACTIVATE) != 0);
        target = NFC_TARGET(target2);
    } else {
        target = g_object_new(TEST_TYPE_TARGET, NULL);
    }

    if (!(flags & TEST_FAIL_TRANSMIT)) {
        TEST_TARGET(target)->fail_transmit = 0;
    }

    return target;
}

static
void
test_data_init_with_target_a(
    TestData* test,
    NfcTarget* target,
    guint8 t0)
{
    NfcPluginsInfo pi;
    NfcParamPollA poll_a;
    NfcParamIsoDepPollA iso_dep_poll_a;

    memset(test, 0, sizeof(*test));
    memset(&pi, 0, sizeof(pi));

    g_assert(!test_name_watch_count());
    g_assert((test->manager = nfc_manager_new(&pi)) != NULL);
    g_assert((test->adapter = test_adapter_new()) != NULL);

    memset(&poll_a, 0, sizeof(poll_a));
    memset(&iso_dep_poll_a, 0, sizeof(iso_dep_poll_a));
    iso_dep_poll_a.fsc = 256;
    iso_dep_poll_a.t0 = t0;
    target->technology = NFC_TECHNOLOGY_A;

    g_assert(nfc_adapter_add_tag_t4a(test->adapter, target, &poll_a,
        &iso_dep_poll_a));

    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    test->loop = g_main_loop_new(NULL, TRUE);
    test->pool = gutil_idle_pool_new();
}

static
void
test_data_init_with_target_b(
    TestData* test,
    NfcTarget* target)
{
    static const guint8 hlr[] = {0x01, 0x02, 0x03, 0x04};
    static const GUtilData hlr_data = { TEST_ARRAY_AND_SIZE(hlr) };

    NfcPluginsInfo pi;
    NfcParamPollB poll_b;
    NfcParamIsoDepPollB iso_dep_poll_b;

    memset(test, 0, sizeof(*test));
    memset(&pi, 0, sizeof(pi));

    g_assert(!test_name_watch_count());
    g_assert((test->manager = nfc_manager_new(&pi)) != NULL);
    g_assert((test->adapter = test_adapter_new()) != NULL);

    memset(&poll_b, 0, sizeof(poll_b));
    memset(&iso_dep_poll_b, 0, sizeof(iso_dep_poll_b));
    poll_b.fsc = 256;
    iso_dep_poll_b.hlr = hlr_data;
    target->technology = NFC_TECHNOLOGY_B;

    g_assert(nfc_adapter_add_tag_t4b(test->adapter, target, &poll_b,
        &iso_dep_poll_b));

    g_assert(nfc_manager_add_adapter(test->manager, test->adapter));
    test->loop = g_main_loop_new(NULL, TRUE);
    test->pool = gutil_idle_pool_new();
}

static
void
test_data_init(
    TestData* test,
    int flags)
{
    NfcTarget* target = test_target_create(flags);

    test_data_init_with_target_a(test, target, 0);
    nfc_target_unref(target);
}

static
void
test_data_cleanup(
    TestData* test)
{
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
test_complete_ok(
    GObject* object,
    GAsyncResult* result)
{
    GError* error = NULL;
    GVariant* out = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, &error);

    g_assert(out);
    g_variant_unref(out);
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
    g_assert_cmpstr(error_name, == , "org.sailfishos.nfc.Error.Failed");
    g_free(error_name);
    g_error_free(error);
}

static
void
test_call_transmit(
    TestData* test,
    guint8 cla,
    guint8 ins,
    guint8 p1,
    guint8 p2,
    const GUtilData* data,
    guint le,
    GAsyncReadyCallback callback)
{
    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL,
        test_tag_path(test, test->adapter->tags[0]), NFC_ISODEP_INTERFACE,
        "Transmit", g_variant_new("(yyyy@ayu)", cla, ins, p1, p2,
        g_variant_new_from_data(G_VARIANT_TYPE_BYTESTRING, data->bytes,
        data->size, TRUE, NULL, NULL), le), NULL, G_DBUS_CALL_FLAGS_NONE,
        TEST_DBUS_TIMEOUT, NULL, callback, test);
}

static
void
test_call_no_args(
    TestData* test,
    const char* method,
    GAsyncReadyCallback callback)
{
    NfcTag* tag = test->adapter->tags[0];

    g_assert(test->connection);
    g_dbus_connection_call(test->connection, NULL, test_tag_path(test, tag),
        NFC_ISODEP_INTERFACE, method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
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

static
gboolean
test_dict_contains(
    GVariant* dict,
    const char* key,
    const GVariantType* type)
{
    GVariant* value = g_variant_lookup_value(dict, key, type);

    if (value) {
        g_variant_unref(value);
        return TRUE;
    } else {
        return FALSE;
    }
}

/*==========================================================================*
 * Test target with reactivate
 *==========================================================================*/

static
gboolean
test_target2_reactivated(
    gpointer user_data)
{
    TestTarget2* test = TEST_TARGET2(user_data);

    test->reactivate_id = 0;
    nfc_target_reactivated(NFC_TARGET(test));
    return G_SOURCE_REMOVE;
}

static
gboolean
test_target2_reactivate(
    NfcTarget* target)
{
    TestTarget2* test = TEST_TARGET2(target);

    g_assert(!test->reactivate_id);
    if (test->fail_reactivate) {
        GDEBUG("Failing reactivation");
        return FALSE;
    } else {
        test->reactivate_id = g_idle_add(test_target2_reactivated, test);
        return TRUE;
    }
}

static
void
test_target2_init(
    TestTarget2* self)
{
    /* Tests assume NFC-B and no failures */
    self->parent.target.technology = NFC_TECHNOLOGY_B;
    self->parent.fail_transmit = 0;
}

static
void
test_target2_finalize(
    GObject* object)
{
    TestTarget2* test = TEST_TARGET2(object);

    if (test->reactivate_id) {
        g_source_remove(test->reactivate_id);
    }
    G_OBJECT_CLASS(test_target2_parent_class)->finalize(object);
}

static
void
test_target2_class_init(
    NfcTargetClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = test_target2_finalize;
    klass->reactivate = test_target2_reactivate;
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
    dbus_service_isodep_free(NULL);
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

    test_data_init(&test, 0);
    dbus = test_dbus_new(test_get_all_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_all2
 *==========================================================================*/

static
void
test_get_all2_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    GVariant* params = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(i@a{sv})", &version, &params);
    g_assert(params);
    GDEBUG("version=%d, %d params", version, (int)g_variant_n_children(params));
    g_assert(version >= MIN_INTERFACE_VERSION);
    g_assert(test_dict_contains(params, "T0", G_VARIANT_TYPE_BYTE));
    g_assert(test_dict_contains(params, "HB", G_VARIANT_TYPE_BYTESTRING));

    g_variant_unref(var);
    g_variant_unref(params);
    test_quit_later(test->loop);
}

static
void
test_get_all2_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_call(test, client, server, "GetAll2", test_get_all2_done);
}

static
void
test_get_all2(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test, 0);
    dbus = test_dbus_new(test_get_all2_start, &test);
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
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    gint version = 0;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(connection),
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

    test_data_init(&test, 0);
    dbus = test_dbus_new(test_get_interface_version_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_parameters1
 *==========================================================================*/

static
void
test_get_parameters1_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GVariant* params = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(@a{sv})", &params);
    g_assert(params);
    GDEBUG("%d params", (int)g_variant_n_children(params));
    g_assert(test_dict_contains(params, "T0", G_VARIANT_TYPE_BYTE));
    g_assert(test_dict_contains(params, "HB", G_VARIANT_TYPE_BYTESTRING));

    g_assert(!test_dict_contains(params, "TA", G_VARIANT_TYPE_BYTE));
    g_assert(!test_dict_contains(params, "TB", G_VARIANT_TYPE_BYTE));
    g_assert(!test_dict_contains(params, "TC", G_VARIANT_TYPE_BYTE));

    g_variant_unref(var);
    g_variant_unref(params);
    test_quit_later(test->loop);
}

static
void
test_get_parameters1_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_call(test, client, server, "GetActivationParameters",
        test_get_parameters1_done);
}

static
void
test_get_parameters1(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test, 0);
    dbus = test_dbus_new(test_get_parameters1_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_parameters2
 *==========================================================================*/

static
void
test_get_parameters2_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GVariant* params = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(@a{sv})", &params);
    g_assert(params);
    GDEBUG("%d params", (int)g_variant_n_children(params));
    g_assert(test_dict_contains(params, "T0", G_VARIANT_TYPE_BYTE));
    g_assert(test_dict_contains(params, "TA", G_VARIANT_TYPE_BYTE));
    g_assert(test_dict_contains(params, "TB", G_VARIANT_TYPE_BYTE));
    g_assert(test_dict_contains(params, "TC", G_VARIANT_TYPE_BYTE));
    g_assert(test_dict_contains(params, "HB", G_VARIANT_TYPE_BYTESTRING));

    g_variant_unref(var);
    g_variant_unref(params);
    test_quit_later(test->loop);
}

static
void
test_get_parameters2_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_call(test, client, server, "GetActivationParameters",
        test_get_parameters2_done);
}

static
void
test_get_parameters2(
    void)
{
    TestData test;
    TestDBus* dbus;
    NfcTarget* target = test_target_create(0);

    test_data_init_with_target_a(&test, target, NFC_PARAM_ISODEP_T0_A |
        NFC_PARAM_ISODEP_T0_B| NFC_PARAM_ISODEP_T0_C);
    nfc_target_unref(target);
    dbus = test_dbus_new(test_get_parameters2_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * get_parameters3
 *==========================================================================*/

static
void
test_get_parameters3_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GVariant* params = NULL;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(@a{sv})", &params);
    g_assert(params);
    GDEBUG("%d params", (int)g_variant_n_children(params));
    g_assert(test_dict_contains(params, "MBLI", G_VARIANT_TYPE_BYTE));
    g_assert(test_dict_contains(params, "DID", G_VARIANT_TYPE_BYTE));
    g_assert(test_dict_contains(params, "HLR", G_VARIANT_TYPE_BYTESTRING));
    /* And no NFC-A params */
    g_assert(!test_dict_contains(params, "T0", G_VARIANT_TYPE_BYTE));
    g_assert(!test_dict_contains(params, "HB", G_VARIANT_TYPE_BYTESTRING));
    g_assert(!test_dict_contains(params, "TA", G_VARIANT_TYPE_BYTE));
    g_assert(!test_dict_contains(params, "TB", G_VARIANT_TYPE_BYTE));
    g_assert(!test_dict_contains(params, "TC", G_VARIANT_TYPE_BYTE));

    g_variant_unref(var);
    g_variant_unref(params);
    test_quit_later(test->loop);
}

static
void
test_get_parameters3_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_call(test, client, server, "GetActivationParameters",
        test_get_parameters3_done);
}

static
void
test_get_parameters3(
    void)
{
    TestData test;
    TestDBus* dbus;
    NfcTarget* target = test_target_create(0);

    test_data_init_with_target_b(&test, target);
    nfc_target_unref(target);
    dbus = test_dbus_new(test_get_parameters3_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * transmit/ok
 *==========================================================================*/

static const guint8 test_transmit_cmd_select_mf[] = {
    0x00, 0xa4, 0x00, 0x00, 0x02,  /* CLA|INS|P1|P2|Lc  */
    0x3f, 0x00                     /* Data */
    /* no Le */
};
static const guint8 test_transmit_resp_ok[] = { 0x90, 0x00 };

static
void
test_transmit_ok_done(
    GObject* object,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;
    GVariant* data = NULL;
    guint8 sw1, sw2;
    GVariant* var = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
        result, NULL);

    g_assert(var);
    g_variant_get(var, "(@ayyy)", &data, &sw1, &sw2);
    g_assert(data);
    g_assert_cmpuint(g_variant_get_size(data), == ,0);
    GDEBUG("%02X %02X", sw1, sw2);

    g_variant_unref(data);
    g_variant_unref(var);
    test_quit_later(test->loop);
}

static
void
test_transmit_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    const guint8* cmd = test_transmit_cmd_select_mf;
    GUtilData cmd_data;

    cmd_data.bytes = cmd + 5;
    cmd_data.size = cmd[4];

    nfc_tag_set_initialized(test->adapter->tags[0]);
    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_transmit(test, cmd[0], cmd[1], cmd[2], cmd[3],
        &cmd_data, 0, test_transmit_ok_done);
}

static
void
test_transmit_ok(
    void)
{
    TestData test;
    TestDBus* dbus;
    NfcTarget* target = test_target_create(0);

    test_data_init_with_target_a(&test, target, 0);
    test_target_add_data(target,
        TEST_ARRAY_AND_SIZE(test_transmit_cmd_select_mf),
        TEST_ARRAY_AND_SIZE(test_transmit_resp_ok));
    nfc_target_unref(target);

    dbus = test_dbus_new(test_transmit_ok_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * transmit/fail
 * transmit/fail_early
 *==========================================================================*/

static
void
test_transmit_fail_done(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_error_failed(connection, result);
    test_quit_later(test->loop);
}

static
void
test_transmit_fail_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    const guint8* cmd = test_transmit_cmd_select_mf;
    GUtilData cmd_data;

    cmd_data.bytes = cmd + 5;
    cmd_data.size = cmd[4];

    nfc_tag_set_initialized(test->adapter->tags[0]);
    g_object_ref(test->connection = client);
    test->service = dbus_service_adapter_new(test->adapter, server);
    g_assert(test->service);
    test_call_transmit(test, cmd[0], cmd[1], cmd[2], cmd[3],
        &cmd_data, 0, test_transmit_fail_done);
}

static
void
test_transmit_fail(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test, 0);
    dbus = test_dbus_new(test_transmit_fail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

static
void
test_transmit_fail_early(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test, TEST_FAIL_TRANSMIT);
    dbus = test_dbus_new(test_transmit_fail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * reset/ok
 *==========================================================================*/

static
void
test_reset_ok_done(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_ok(connection, result);
    test_quit_later(test->loop);
}

static
void
test_reset_ok_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_call(test, client, server, "Reset", test_reset_ok_done);
}

static
void
test_reset_ok(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test, TEST_CAN_REACTIVATE);
    dbus = test_dbus_new(test_reset_ok_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * reset/fail
 * reset/unsupported
 *==========================================================================*/

static
void
test_reset_fail_done(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    TestData* test = user_data;

    test_complete_error_failed(connection, result);
    test_quit_later(test->loop);
}

static
void
test_reset_fail_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    nfc_tag_set_initialized(test->adapter->tags[0]);
    test_start_and_call(test, client, server, "Reset", test_reset_fail_done);
}

static
void
test_reset_fail(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test, 0);
    dbus = test_dbus_new(test_reset_fail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

static
void
test_reset_unsupported(
    void)
{
    TestData test;
    TestDBus* dbus;

    test_data_init(&test, TEST_CAN_REACTIVATE | TEST_FAIL_REACTIVATE);
    dbus = test_dbus_new(test_reset_fail_start, &test);
    test_run(&test_opt, test.loop);
    test_data_cleanup(&test);
    test_dbus_free(dbus);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/isodep/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("get_all"), test_get_all);
    g_test_add_func(TEST_("get_all2"), test_get_all2);
    g_test_add_func(TEST_("get_interface_version"), test_get_interface_version);
    g_test_add_func(TEST_("get_parameters1"), test_get_parameters1);
    g_test_add_func(TEST_("get_parameters2"), test_get_parameters2);
    g_test_add_func(TEST_("get_parameters3"), test_get_parameters3);
    g_test_add_func(TEST_("transmit/ok"), test_transmit_ok);
    g_test_add_func(TEST_("transmit/fail"), test_transmit_fail);
    g_test_add_func(TEST_("transmit/fail_early"), test_transmit_fail_early);
    g_test_add_func(TEST_("reset/ok"), test_reset_ok);
    g_test_add_func(TEST_("reset/fail"), test_reset_fail);
    g_test_add_func(TEST_("reset/unsupported"), test_reset_unsupported);
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
