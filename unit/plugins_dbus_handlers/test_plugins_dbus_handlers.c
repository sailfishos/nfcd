/*
 * Copyright (C) 2019 Jolla Ltd.
 * Copyright (C) 2019 Slava Monich <slava.monich@jolla.com>
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

#include "dbus_handlers/dbus_handlers.h"

#include "test_common.h"
#include "test_dbus.h"
#include "test.handler.h"

#include <glib/gstdio.h>

static TestOpt test_opt;

#define TEST_SERVICE "test.service"
#define TEST_INTERFACE "test.handler"
#define TEST_PATH "/test"

static const guint8 test_ndef_data[] = {
    0xd1,       /* NDEF record header (MB,ME,SR,TNF=0x01) */
    0x01,       /* Length of the record type */
    0x00,       /* Length of the record payload */
    'x'         /* Record type: 'x' */
};

typedef struct test_data {
    NfcNdefRec* rec;
    GMainLoop* loop;
    DBusHandlers* handlers;
    char* dir;
    char* fname;
    TestHandler* dbus_handler;
} TestData;

static
NfcNdefRec*
test_ndef_record_new(
    void)
{
    GUtilData bytes;
    NfcNdefRec* rec;

    TEST_BYTES_SET(bytes, test_ndef_data);
    rec = nfc_ndef_rec_new(&bytes);
    g_assert(rec);
    return rec;
}

static
gboolean
test_no_notify(
    TestHandler* object,
    GDBusMethodInvocation* call,
    gboolean handled,
    GVariant* data,
    gpointer user_data)
{
    g_assert(FALSE);
    return FALSE;
}

static
gboolean
test_no_handle(
    TestHandler* object,
    GDBusMethodInvocation* call,
    GVariant* data,
    gpointer user_data)
{
    g_assert(FALSE);
    return FALSE;
}

static
void
test_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;

    /* Typical start callback */
    g_assert(g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (test->dbus_handler), server, TEST_PATH, NULL));
    g_assert(g_bus_own_name_on_connection(server, TEST_SERVICE,
        G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, test, NULL));

    g_assert_nonnull(test->handlers = dbus_handlers_new(client, test->dir));
    dbus_handlers_run(test->handlers, test->rec);
}

static
void
test_data_init(
    TestData* test,
    const char* config)
{
    memset(test, 0, sizeof(*test));

    test->dir = g_dir_make_tmp("test_XXXXXX", NULL);
    test->fname = g_build_filename(test->dir, "test.conf", NULL);

    GDEBUG("Created %s", test->dir);
    g_assert(g_file_set_contents(test->fname, config, -1, NULL));

    test->rec = test_ndef_record_new();
    test->loop = g_main_loop_new(NULL, TRUE);

    test->dbus_handler = test_handler_skeleton_new();
}

static
void
test_data_cleanup(
    TestData* test)
{
    g_unlink(test->fname);
    g_rmdir(test->dir);
    g_main_loop_unref(test->loop);
    g_object_unref(test->dbus_handler);
    dbus_handlers_free(test->handlers);
    nfc_ndef_rec_unref(test->rec);
    g_free(test->fname);
    g_free(test->dir);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!dbus_handlers_new(NULL, NULL));
    dbus_handlers_run(NULL, NULL);
    dbus_handlers_free(NULL);
}

/*==========================================================================*
 * cancel_handler
 *==========================================================================*/

static
void
test_cancel_start(
    GDBusConnection* client,
    GDBusConnection* server,
    void* user_data)
{
    TestData* test = user_data;
    DBusHandlers* handlers;

    g_assert(g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (test->dbus_handler), server, TEST_PATH, NULL));
    g_assert(g_bus_own_name_on_connection(server, TEST_SERVICE,
        G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, test, NULL));

    /* dbus_handlers_new will fail without config dir */
    g_assert(!dbus_handlers_new(client, NULL));
    g_assert_nonnull(handlers = dbus_handlers_new(client, test->dir));
    dbus_handlers_run(handlers, NULL); /* This one has no effect */
    dbus_handlers_run(handlers, test->rec);
    dbus_handlers_free(handlers); /* Immediately cancel the run */
    test_quit_later_n(test->loop, 100); /* Allow everything to complete */
}

static
void
test_cancel_handler(
    void)
{
    TestData test;
    TestDBus* dbus;
    const char* config =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Handle\n"
        "Path = " TEST_PATH "\n"

        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test, config);
    /* "Handle" call may still get delivered (even after getting cancelled) */
    g_assert(g_signal_connect(test.dbus_handler, "handle-notify",
        G_CALLBACK(test_no_notify), &test));

    dbus = test_dbus_new(test_cancel_start, &test);
    test_run(&test_opt, test.loop);
    test_dbus_free(dbus);
    test_data_cleanup(&test);
}

/*==========================================================================*
 * cancel_listener
 *==========================================================================*/

static
void
test_cancel_listener(
    void)
{
    TestData test;
    TestDBus* dbus;
    const char* config =
        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test, config);
    /* "Notify" call may still get delivered (even after getting cancelled) */

    dbus = test_dbus_new(test_cancel_start, &test);
    test_run(&test_opt, test.loop);
    test_dbus_free(dbus);
    test_data_cleanup(&test);
}

/*==========================================================================*
 * handler
 *==========================================================================*/

static
gboolean
test_handler_handle(
    TestHandler* object,
    GDBusMethodInvocation* call,
    GVariant* data,
    gpointer user_data)
{
    gsize size = 0;
    const guint8* ndef = g_variant_get_fixed_array(data, &size, 1);
    TestData* test = user_data;

    GDEBUG("Handler received %u bytes NDEF message", (guint)size);
    g_assert(size == sizeof(test_ndef_data));
    g_assert(!memcmp(ndef, test_ndef_data, size));
    test_handler_complete_handle(object, call, TRUE);
    test_quit_later_n(test->loop, 100); /* Allow everything to complete */
    return TRUE;
}

static
void
test_handler(
    void)
{
    TestData test;
    TestDBus* dbus;
    const char* config =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Handle\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test, config);
    g_assert(g_signal_connect(test.dbus_handler, "handle-handle",
        G_CALLBACK(test_handler_handle), &test));
    g_assert(g_signal_connect(test.dbus_handler, "handle-notify",
        G_CALLBACK(test_no_notify), &test));

    dbus = test_dbus_new(test_start, &test);
    test_run(&test_opt, test.loop);
    test_dbus_free(dbus);
    test_data_cleanup(&test);
}

/*==========================================================================*
 * handler_listener
 *==========================================================================*/

static
gboolean
test_handler_listener_notify(
    TestHandler* object,
    GDBusMethodInvocation* call,
    gboolean handled,
    GVariant* data,
    gpointer user_data)
{
    gsize size = 0;
    const guint8* ndef = g_variant_get_fixed_array(data, &size, 1);
    TestData* test = user_data;

    GDEBUG("Listener received %u bytes NDEF message", (guint)size);
    g_assert(handled);
    g_assert(size == sizeof(test_ndef_data));
    g_assert(!memcmp(ndef, test_ndef_data, size));
    test_handler_complete_notify(object, call);
    test_quit_later_n(test->loop, 100); /* Allow everything to complete */
    return TRUE;
}

static
gboolean
test_handler_listener_handle(
    TestHandler* object,
    GDBusMethodInvocation* call,
    GVariant* data,
    gpointer user_data)
{
    gsize size = 0;
    const guint8* ndef = g_variant_get_fixed_array(data, &size, 1);

    GDEBUG("Handler received %u bytes NDEF message", (guint)size);
    g_assert(size == sizeof(test_ndef_data));
    g_assert(!memcmp(ndef, test_ndef_data, size));
    test_handler_complete_handle(object, call, TRUE);
    /* Now wait for listener to be called */
    return TRUE;
}

static
void
test_handler_listener(
    void)
{
    TestData test;
    TestDBus* dbus;
    const char* config =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Handle\n"
        "Path = " TEST_PATH "\n"

        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test, config);
    g_assert(g_signal_connect(test.dbus_handler, "handle-handle",
        G_CALLBACK(test_handler_listener_handle), &test));
    g_assert(g_signal_connect(test.dbus_handler, "handle-notify",
        G_CALLBACK(test_handler_listener_notify), &test));

    dbus = test_dbus_new(test_start, &test);
    test_run(&test_opt, test.loop);
    test_dbus_free(dbus);
    test_data_cleanup(&test);
}

/*==========================================================================*
 * handlers
 *==========================================================================*/

static
gboolean
test_handlers_handle(
    TestHandler* object,
    GDBusMethodInvocation* call,
    GVariant* data,
    gpointer user_data)
{
    gsize size = 0;
    const guint8* ndef = g_variant_get_fixed_array(data, &size, 1);
    TestData* test = user_data;

    GDEBUG("Handler received %u bytes NDEF message", (guint)size);
    g_assert(size == sizeof(test_ndef_data));
    g_assert(!memcmp(ndef, test_ndef_data, size));
    test_handler_complete_handle(object, call, TRUE);
    test_quit_later_n(test->loop, 100); /* Allow everything to complete */
    return TRUE;
}

static
void
test_handlers(
    void)
{
    TestData test;
    TestDBus* dbus;
    char* fname2;
    const char* config1 =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Handle\n"
        "Path = " TEST_PATH "\n";

    const char* config2 =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Handle2\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test, config1);
    fname2 = g_build_filename(test.dir, "test2.conf", NULL);
    g_assert(g_file_set_contents(fname2, config2, -1, NULL));

    g_assert(g_signal_connect(test.dbus_handler, "handle-handle",
        G_CALLBACK(test_handlers_handle), &test));
    g_assert(g_signal_connect(test.dbus_handler, "handle-handle2",
        G_CALLBACK(test_no_handle), &test));

    dbus = test_dbus_new(test_start, &test);
    test_run(&test_opt, test.loop);
    test_dbus_free(dbus);
    g_unlink(fname2);
    g_free(fname2);
    test_data_cleanup(&test);
}

/*==========================================================================*
 * handlers2
 *==========================================================================*/

static
gboolean
test_handlers2_donthandle(
    TestHandler* object,
    GDBusMethodInvocation* call,
    GVariant* data,
    gpointer user_data)
{
    GDEBUG("Not handling the message");
    test_handler_complete_handle(object, call, FALSE);
    return TRUE;
}

static
gboolean
test_handlers2_handle(
    TestHandler* object,
    GDBusMethodInvocation* call,
    GVariant* data,
    gpointer user_data)
{
    GDEBUG("Handling the message");
    test_handler_complete_handle2(object, call, TRUE);
    return TRUE;
}

static
gboolean
test_handlers2_notify(
    TestHandler* object,
    GDBusMethodInvocation* call,
    gboolean handled,
    GVariant* data,
    gpointer user_data)
{
    TestData* test = user_data;

    GDEBUG("Done");
    g_assert(handled);
    test_handler_complete_notify(object, call);
    test_quit_later_n(test->loop, 100); /* Allow everything to complete */
    return TRUE;
}

static
void
test_handlers2(
    void)
{
    TestData test;
    TestDBus* dbus;
    char* fname2;
    char* fname3;
    const char* config1 =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Handle\n"
        "Path = " TEST_PATH "\n";

    const char* config2 =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Handle2\n"
        "Path = " TEST_PATH "\n";

    const char* config3 =
        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test, config1);
    fname2 = g_build_filename(test.dir, "test2.conf", NULL);
    fname3 = g_build_filename(test.dir, "test3.conf", NULL);
    g_assert(g_file_set_contents(fname2, config2, -1, NULL));
    g_assert(g_file_set_contents(fname3, config3, -1, NULL));

    g_assert(g_signal_connect(test.dbus_handler, "handle-handle",
        G_CALLBACK(test_handlers2_donthandle), &test));
    g_assert(g_signal_connect(test.dbus_handler, "handle-handle2",
        G_CALLBACK(test_handlers2_handle), &test));
    g_assert(g_signal_connect(test.dbus_handler, "handle-notify",
        G_CALLBACK(test_handlers2_notify), &test));

    dbus = test_dbus_new(test_start, &test);
    test_run(&test_opt, test.loop);
    test_dbus_free(dbus);
    g_unlink(fname2);
    g_unlink(fname3);
    g_free(fname2);
    g_free(fname3);
    test_data_cleanup(&test);
}

/*==========================================================================*
 * listeners
 *==========================================================================*/

typedef struct test_listeners_data {
    TestData data;
    int count;
} TestListenersData;

static
gboolean
test_listeners_notify(
    TestHandler* object,
    GDBusMethodInvocation* call,
    gboolean handled,
    GVariant* data,
    gpointer user_data)
{
    TestListenersData* test = user_data;

    test->count++;
    GDEBUG("Notify %d", test->count);
    g_assert(!handled);
    g_assert(test->count <= 3);
    test_handler_complete_notify(object, call);
    if (test->count == 3) {
        /* Allow everything to complete */
        test_quit_later_n(test->data.loop, 100);
    }
    return TRUE;
}

static
void
test_listeners(
    void)
{
    TestListenersData test;
    TestDBus* dbus;
    char* fname2;
    char* fname3;
    const char* config1 =
        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";
    const char* config2 =
        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";
    const char* config3 =
        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test.data, config1);
    test.count = 0;
    fname2 = g_build_filename(test.data.dir, "test2.conf", NULL);
    fname3 = g_build_filename(test.data.dir, "test3.conf", NULL);
    g_assert(g_file_set_contents(fname2, config2, -1, NULL));
    g_assert(g_file_set_contents(fname3, config3, -1, NULL));

    g_assert(g_signal_connect(test.data.dbus_handler, "handle-notify",
        G_CALLBACK(test_listeners_notify), &test));

    dbus = test_dbus_new(test_start, &test);
    test_run(&test_opt, test.data.loop);
    test_dbus_free(dbus);
    g_unlink(fname2);
    g_unlink(fname3);
    g_free(fname2);
    g_free(fname3);
    test_data_cleanup(&test.data);
}

/*==========================================================================*
 * invalid_return
 *==========================================================================*/

static
gboolean
test_invalid_return_handle(
    TestHandler* object,
    GDBusMethodInvocation* call,
    GVariant* data,
    gpointer user_data)
{
    /* We return unexpected value which is interpreted as FALSE */
    GDEBUG("Handling the message (but returning unexpected value)");
    test_handler_complete_invalid_return(object, call, "foo");
    return TRUE;
}

static
gboolean
test_invalid_return_notify(
    TestHandler* object,
    GDBusMethodInvocation* call,
    gboolean handled,
    GVariant* data,
    gpointer user_data)
{
    TestData* test = user_data;

    GDEBUG("Done");
    g_assert(!handled);
    test_handler_complete_notify(object, call);
    test_quit_later_n(test->loop, 100); /* Allow everything to complete */
    return TRUE;
}

static
void
test_invalid_return(
    void)
{
    TestData test;
    TestDBus* dbus;
    const char* config =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".InvalidReturn\n"
        "Path = " TEST_PATH "\n"

        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test, config);
    g_assert(g_signal_connect(test.dbus_handler, "handle-invalid-return",
        G_CALLBACK(test_invalid_return_handle), &test));
    g_assert(g_signal_connect(test.dbus_handler, "handle-notify",
        G_CALLBACK(test_invalid_return_notify), &test));

    dbus = test_dbus_new(test_start, &test);
    test_run(&test_opt, test.loop);
    test_dbus_free(dbus);
    test_data_cleanup(&test);
}

/*==========================================================================*
 * no_return
 *==========================================================================*/

static
gboolean
test_no_return_handle(
    TestHandler* object,
    GDBusMethodInvocation* call,
    GVariant* data,
    gpointer user_data)
{
    /* We don't return any value which is interpreted as TRUE */
    GDEBUG("Handling the message (but not returning the status)");
    test_handler_complete_no_return(object, call);
    return TRUE;
}

static
gboolean
test_no_return_notify(
    TestHandler* object,
    GDBusMethodInvocation* call,
    gboolean handled,
    GVariant* data,
    gpointer user_data)
{
    TestData* test = user_data;

    GDEBUG("Done");
    g_assert(handled);
    test_handler_complete_notify(object, call);
    test_quit_later_n(test->loop, 100); /* Allow everything to complete */
    return TRUE;
}

static
void
test_no_return(
    void)
{
    TestData test;
    TestDBus* dbus;
    const char* config =
        "[Handler]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".NoReturn\n"
        "Path = " TEST_PATH "\n"

        "[Listener]\n"
        "Service = " TEST_SERVICE "\n"
        "Method = " TEST_INTERFACE ".Notify\n"
        "Path = " TEST_PATH "\n";

    test_data_init(&test, config);
    g_assert(g_signal_connect(test.dbus_handler, "handle-no-return",
        G_CALLBACK(test_no_return_handle), &test));
    g_assert(g_signal_connect(test.dbus_handler, "handle-notify",
        G_CALLBACK(test_no_return_notify), &test));

    dbus = test_dbus_new(test_start, &test);
    test_run(&test_opt, test.loop);
    test_dbus_free(dbus);
    test_data_cleanup(&test);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_handlers/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("cancel_handler"), test_cancel_handler);
    g_test_add_func(TEST_("cancel_listener"), test_cancel_listener);
    g_test_add_func(TEST_("handler"), test_handler);
    g_test_add_func(TEST_("handler_listener"), test_handler_listener); 
    g_test_add_func(TEST_("handlers"), test_handlers);
    g_test_add_func(TEST_("handlers2"), test_handlers2);
    g_test_add_func(TEST_("listeners"), test_listeners);
    g_test_add_func(TEST_("invalid_return"), test_invalid_return);
    g_test_add_func(TEST_("no_return"), test_no_return);
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
