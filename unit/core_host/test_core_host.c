/*
 * Copyright (C) 2023-2025 Slava Monich <slava@monich.com>
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

#include "nfc_host_p.h"
#include "nfc_host_app_p.h"
#include "nfc_host_service_p.h"
#include "nfc_initiator_impl.h"
#include "nfc_initiator_p.h"

#include "test_common.h"
#include "test_host_app.h"
#include "test_host_service.h"
#include "test_initiator.h"

#include <gutil_log.h>
#include <gutil_misc.h>

static TestOpt test_opt;

static
void
test_host_inc(
    NfcHost* host,
    void* user_data)
{
    int* counter = user_data;

    (*counter)++;
}

static
void
test_host_not_reached(
    NfcHost* host,
    void* loop)
{
    g_assert_not_reached();
}

static
void
test_host_done_quit(
    NfcHost* host,
    void* loop)
{
    g_assert(!host->initiator->present);
    GDEBUG("%s is gone", host->name);
    g_main_loop_quit((GMainLoop*)loop);
}

static
void
test_app_selected_once(
    NfcHost* host,
    void* user_data)
{
    NfcHostApp** selected = user_data;

    g_assert(host->app);
    GDEBUG("%s selected", host->app->name);
    g_assert(!*selected);
    *selected = nfc_host_app_ref(host->app);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    /* Public interfaces are NULL tolerant */
    g_assert(!nfc_host_ref(NULL));
    g_assert(!nfc_host_add_gone_handler(NULL, NULL, NULL));
    g_assert(!nfc_host_add_app_changed_handler(NULL, NULL, NULL));
    g_assert(!nfc_host_app_ref(NULL));
    g_assert(!nfc_host_service_ref(NULL));

    nfc_host_app_unref(NULL);
    nfc_host_service_unref(NULL);
    nfc_host_remove_handler(NULL, 0);
    nfc_host_remove_handlers(NULL, NULL, 0);
    nfc_host_deactivate(NULL);
    nfc_host_unref(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    static const guchar cmd_select1[] = {
        /* Non-existent app */
        0x00, 0xA4, 0x04, 0x00, 0x07, 0xd2, 0x76, 0x00,
        0x00, 0x85, 0x01, 0x01, 0x00
    };
    static const guchar resp_not_found[] = { 0x6a, 0x82 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select1) },
            { TEST_ARRAY_AND_SIZE(resp_not_found) }
        }
    };
    const char* name = "TestHost";
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcHost* host = nfc_host_new(name, init, NULL, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    g_assert(host);
    g_assert(nfc_host_ref(host) == host);
    g_assert_cmpstr(host->name, == ,name);
    nfc_host_unref(host);

    /* Callback is required */
    g_assert(!nfc_host_add_gone_handler(host, NULL, NULL));
    g_assert(!nfc_host_add_app_changed_handler(host, NULL, NULL));

    test_run(&test_opt, loop);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_handler(host, id);
    nfc_host_remove_handler(host, 0); /* Zero id is ignored */
    nfc_host_unref(host);
}

/*==========================================================================*
 * service
 *==========================================================================*/

static
void
test_service(
    void)
{
    NfcInitiator* initiator = test_initiator_new();
    TestHostService* test = test_host_service_new("TestService");
    NfcHostService* service = NFC_HOST_SERVICE(test);
    NfcHostService* services[2];
    NfcHostApp* apps[1];
    NfcHost* host;

    services[0] = service;
    services[1] = NULL;
    apps[0] = NULL;
    host = nfc_host_new("TestHost", initiator, services, apps);

    nfc_host_start(host);
    nfc_host_deactivate(host);

    /* We don't actually let the service to get started */
    g_assert_cmpint(test->start, == ,0);

    /* These ids get ignored */
    nfc_host_service_cancel(service, NFCD_ID_FAIL);
    nfc_host_service_cancel(service, NFCD_ID_SYNC);

    nfc_initiator_unref(initiator);
    nfc_host_service_unref(service);
    nfc_host_unref(host);
}

/*==========================================================================*
 * service_start
 *==========================================================================*/

static
void
test_service_start_failed(
    NfcHostService* service,
    gboolean result,
    void* user_data)
{
    g_assert(!result);
    GDEBUG("First service failed to start");
    (*((int*)user_data))++;
    g_assert_cmpint(TEST_HOST_SERVICE(service)->start, == ,1);
}

static
void
test_service_start_done(
    NfcHostService* service,
    gboolean result,
    void* user_data)
{
    GDEBUG("Done");
    g_assert_cmpint(TEST_HOST_SERVICE(service)->start, == ,1);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_service_start(
    TEST_HOST_SERVICE_FLAGS service_flags1,
    TEST_HOST_SERVICE_FLAGS service_flags2)
{
    NfcInitiator* initiator = test_initiator_new();
    TestHostService* test1 = test_host_service_new("TestService1");
    TestHostService* test2 = test_host_service_new("TestService2");
    NfcHostService* service1 = NFC_HOST_SERVICE(test1);
    NfcHostService* service2 = NFC_HOST_SERVICE(test2);
    NfcHostService* services[3];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    int failed = 0;
    NfcHost* host;
    gulong id1[2], id2[2];

    id1[0] = test_host_service_add_start_handler(test1,
        test_service_start_failed, &failed);
    id1[1]= test_host_service_add_restart_handler(test1,
        test_service_start_failed, &failed);
    id2[0] = test_host_service_add_start_handler(test2,
        test_service_start_done, loop);
    id2[1] = test_host_service_add_restart_handler(test2,
        test_service_start_done, loop);

    /* The first start fails, second one succeeds */
    test1->flags |= service_flags1;
    test2->flags |= service_flags2;
    services[0] = service1;
    services[1] = service2;
    services[2] = NULL;
    host = nfc_host_new("TestHost", initiator, services, NULL);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(failed, == ,1);
    g_assert_cmpint(test1->start, == ,1);
    g_assert_cmpint(test2->start, == ,1);
    g_assert_cmpint(test1->restart, == ,0);
    g_assert_cmpint(test2->restart, == ,0);

    nfc_initiator_reactivated(initiator);
    test_run(&test_opt, loop);
    g_assert_cmpint(failed, == ,1);
    g_assert_cmpint(test1->start, == ,1);
    g_assert_cmpint(test2->start, == ,1);
    g_assert_cmpint(test1->restart, == ,0); /* Failed to start */
    g_assert_cmpint(test2->restart, == ,1);

    gutil_disconnect_handlers(test1, id1, G_N_ELEMENTS(id1));
    gutil_disconnect_handlers(test2, id2, G_N_ELEMENTS(id2));
    g_main_loop_unref(loop);
    nfc_initiator_unref(initiator);
    nfc_host_service_unref(service1);
    nfc_host_service_unref(service2);
    nfc_host_unref(host);
}

static
void
test_service_start1(
    void)
{
    test_service_start(TEST_HOST_SERVICE_FLAG_FAIL_START,
        TEST_HOST_SERVICE_NO_FLAGS);
}

static
void
test_service_start2(
    void)
{
    test_service_start(TEST_HOST_SERVICE_FLAG_START_SYNC_ERR,
        TEST_HOST_SERVICE_NO_FLAGS);
}

static
void
test_service_start3(
    void)
{
    NfcInitiator* initiator = test_initiator_new();
    TestHostService* service = test_host_service_new("TestService1");
    NfcHostService* services[2];
    NfcHost* host;

    service->flags |= TEST_HOST_SERVICE_FLAG_START_SYNC_OK;
    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;
    host = nfc_host_new("TestHost", initiator, services, NULL);

    /* Service starts synchronously */
    nfc_host_start(host);
    g_assert_cmpint(service->start, == ,1);

    nfc_initiator_unref(initiator);
    nfc_host_service_unref(services[0]);
    nfc_host_unref(host);
}

/*==========================================================================*
 * service_apdu
 *==========================================================================*/

static
void
test_service_apdu(
    const TestTx* tx,
    TEST_HOST_SERVICE_FLAGS service_flags)
{
    const gsize tx_count = 1;
    TestHostService* service = test_host_service_new("TestService");
    NfcHostService* services[2];
    NfcInitiator* init = test_initiator_new_with_tx(tx, tx_count);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id[2];
    NfcHost* host;

    service->flags |= service_flags;
    service->tx_list = tx;
    service->tx_count = tx_count;
    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;
    host = nfc_host_new("TestHost", init, services, NULL);
    id[0] = nfc_host_add_app_changed_handler(host, test_host_not_reached, NULL);
    id[1] = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(service->start, == ,1);
    g_assert_cmpint(service->process, == ,1);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_all_handlers(host, id);
    nfc_host_service_unref(services[0]);
    nfc_host_unref(host);
}

static
void
test_service_apdu_ok(
    TEST_HOST_SERVICE_FLAGS flags)
{
    static const guchar cmd_apdu[] = {
        0x90, 0x5a, 0x00, 0x00, 0x03, 0x14, 0x20, 0xef, 0x00
    };
    static const guchar resp_ok[] = { 0x90, 0x00 };
    static const TestTx tx = {
        { TEST_ARRAY_AND_SIZE(cmd_apdu) },
        { TEST_ARRAY_AND_SIZE(resp_ok) }
    };

    test_service_apdu(&tx, flags);
}

static
void
test_service_apdu_ok1(
    void)
{
    test_service_apdu_ok(TEST_HOST_SERVICE_NO_FLAGS);
}

static
void
test_service_apdu_ok2(
    void)
{
    test_service_apdu_ok(TEST_HOST_SERVICE_FLAG_PROCESS_SYNC);
}

static
void
test_service_apdu_fail(
    TEST_HOST_SERVICE_FLAGS flags)
{
    static const guchar cmd_apdu[] = {
        0x90, 0x5a, 0x00, 0x00, 0x03, 0x14, 0x20, 0xef, 0x00
    };
     /* Expecting 6e00 (Class not supported) */
    static const guchar resp_err[] = { 0x6e, 0x00 };
    static const TestTx tx = {
        { TEST_ARRAY_AND_SIZE(cmd_apdu) },
        { TEST_ARRAY_AND_SIZE(resp_err) }
    };

    test_service_apdu(&tx, flags);
}

static
void
test_service_apdu_fail1(
    void)
{
    test_service_apdu_fail(TEST_HOST_SERVICE_FLAG_PROCESS_ERR);
}

static
void
test_service_apdu_fail2(
    void)
{
    test_service_apdu_fail(TEST_HOST_SERVICE_FLAG_PROCESS_FAIL);
}

static
void
test_service_apdu_fail3(
    void)
{
    test_service_apdu_fail(TEST_HOST_SERVICE_FLAG_PROCESS_FAIL |
        TEST_HOST_SERVICE_FLAG_PROCESS_SYNC);
}

/*==========================================================================*
 * service_apdu_sent
 *==========================================================================*/

static
void
test_service_apdu_sent_cb(
    NfcHostService* service,
    gboolean ok,
    void* user_data)
{
    GDEBUG("Response sent");
    g_assert(ok);
    nfc_initiator_deactivate(NFC_INITIATOR(user_data));
}

static
void
test_service_apdu_sent(
    void)
{
    static const guchar cmd_apdu[] = {
        0x90, 0x5a, 0x00, 0x00, 0x03, 0x14, 0x20, 0xef, 0x00
    };
    static const guchar resp_ok[] = { 0x90, 0x00 };
    static const TestTx tx = {
        { TEST_ARRAY_AND_SIZE(cmd_apdu) },
        { TEST_ARRAY_AND_SIZE(resp_ok) }
    };
    TestHostService* service = test_host_service_new("TestService");
    NfcHostService* services[2];
    NfcInitiator* init = test_initiator_new_with_tx2(&tx, 1, TRUE);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id;
    NfcHost* host;

    service->tx_list = &tx;
    service->tx_count = 1;
    service->flags |= TEST_HOST_SERVICE_FLAG_PROCESS_SENT_ONCE;
    service->sent_cb = test_service_apdu_sent_cb;
    service->sent_data = init;

    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;
    host = nfc_host_new("TestHost", init, services, NULL);
    id = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(service->start, == ,1);
    g_assert_cmpint(service->process, == ,1);
    g_assert(!service->sent_cb); /* Callback was invoked */

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_handler(host, id);
    nfc_host_service_unref(services[0]);
    nfc_host_unref(host);
}

/*==========================================================================*
 * app
 *==========================================================================*/

static
void
test_app(
    void)
{
    static const guchar aid_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const GUtilData aid = { TEST_ARRAY_AND_SIZE(aid_bytes) };
    NfcInitiator* initiator = test_initiator_new();
    TestHostApp* app = test_host_app_new(&aid, NULL, NFC_HOST_APP_FLAGS_NONE);
    NfcHostApp* apps[2];
    NfcHost* host;

    apps[0] = NFC_HOST_APP(app);
    apps[1] = NULL;
    g_assert_cmpstr(apps[0]->name, == ,"01020304");
    host = nfc_host_new("TestHost", initiator, NULL, apps);

    nfc_host_start(host);
    nfc_host_deactivate(host);
    g_assert_cmpint(app->select, == ,0);

    /* These do nothing */
    nfc_host_app_cancel(apps[0], NFCD_ID_SYNC);
    nfc_host_app_cancel(apps[0], NFCD_ID_FAIL);

    /* We don't actually let the app to get started */
    g_assert_cmpint(app->start, == ,0);

    nfc_initiator_unref(initiator);
    nfc_host_app_unref(apps[0]);
    nfc_host_unref(host);
}

/*==========================================================================*
 * app_start
 *==========================================================================*/

static
void
test_app_start_done(
    NfcHostApp* app,
    gboolean result,
    void* user_data)
{
    GDEBUG("Done");
    g_assert_cmpint(TEST_HOST_APP(app)->start, == ,1);
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_app_start(
    TEST_HOST_APP_FLAGS fail_flag)
{
    static const guchar aid1_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const guchar aid2_bytes[] = { 0x05, 0x06, 0x07, 0x08 };
    static const GUtilData aid1 = { TEST_ARRAY_AND_SIZE(aid1_bytes) };
    static const GUtilData aid2 = { TEST_ARRAY_AND_SIZE(aid2_bytes) };
    NfcInitiator* initiator = test_initiator_new();
    TestHostService* service = test_host_service_new("TestService");
    TestHostApp* app1 = test_host_app_new(&aid1, "TestApp1", 0);
    TestHostApp* app2 = test_host_app_new(&aid2, "TestApp2", 0);
    NfcHostService* services[2];
    NfcHostApp* apps[3];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = test_host_app_add_start_handler(app2,
        test_app_start_done, loop);
    NfcHost* host;

    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;

    /* The first app fails to start, second one succeeds */
    app1->flags |= fail_flag;
    apps[0] = NFC_HOST_APP(app1);
    apps[1] = NFC_HOST_APP(app2);
    apps[2] = NULL;
    host = nfc_host_new("TestHost", initiator, services, apps);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert(!host->app);
    g_assert_cmpint(service->start, == ,1);
    g_assert_cmpint(app1->start, == ,1);
    g_assert_cmpint(app2->start, == ,1);
    g_assert_cmpint(app1->select, == ,0);
    g_assert_cmpint(app2->select, == ,0);

    g_main_loop_unref(loop);
    g_signal_handler_disconnect(app2, id);
    nfc_initiator_unref(initiator);
    nfc_host_app_unref(apps[0]);
    nfc_host_app_unref(apps[1]);
    nfc_host_service_unref(services[0]);
    nfc_host_unref(host);
}

static
void
test_app_start1(
    void)
{
    test_app_start(TEST_HOST_APP_FLAG_START_SYNC_ERR);
}

static
void
test_app_start2(
    void)
{
    test_app_start(TEST_HOST_APP_FLAG_FAIL_START);
}

static
void
test_app_start3(
    void)
{
    test_app_start(TEST_HOST_APP_FLAG_FAIL_START_ASYNC);
}

static
void
test_app_start_one(
    TEST_HOST_APP_FLAGS fail_flag)
{
    static const guchar aid_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const GUtilData aid = { TEST_ARRAY_AND_SIZE(aid_bytes) };
    static const guchar cmd_select[] = {
        /* Non-existent app */
        0x00, 0xA4, 0x04, 0x00, 0x07, 0xd2, 0x76, 0x00,
        0x00, 0x85, 0x01, 0x01, 0x00
    };
    static const guchar resp_not_found[] = { 0x6a, 0x82 };
    static const TestTx tx = {
        { TEST_ARRAY_AND_SIZE(cmd_select) },
        { TEST_ARRAY_AND_SIZE(resp_not_found) }
    };
    NfcInitiator* initiator = test_initiator_new_with_tx(&tx, 1);
    TestHostService* service = test_host_service_new("TestService");
    TestHostApp* app = test_host_app_new(&aid, "TestApp", 0);
    NfcHostService* services[2];
    NfcHostApp* apps[2];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcHost* host;
    gulong id;

    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;

    /* One and only app fails to start */
    app->flags |= TEST_HOST_APP_FLAG_FAIL_START_ASYNC;
    apps[0] = NFC_HOST_APP(app);
    apps[1] = NULL;
    host = nfc_host_new("TestHost", initiator, services, apps);
    id = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert(!host->app);
    g_assert_cmpint(service->start, == ,1);
    g_assert_cmpint(app->start, == ,1);

    g_main_loop_unref(loop);
    nfc_initiator_unref(initiator);
    nfc_host_app_unref(apps[0]);
    nfc_host_service_unref(services[0]);
    nfc_host_remove_handler(host, id);
    nfc_host_unref(host);
}

static
void
test_app_start4(
    void)
{
    test_app_start_one(TEST_HOST_APP_FLAG_START_SYNC_ERR);
}

static
void
test_app_start5(
    void)
{
    test_app_start_one(TEST_HOST_APP_FLAG_FAIL_START);
}

static
void
test_app_start6(
    void)
{
    test_app_start_one(TEST_HOST_APP_FLAG_FAIL_START_ASYNC);
}

/*==========================================================================*
 * app_implicit_select
 *==========================================================================*/

static
void
test_app_implicit_select_done(
    NfcHost* host,
    void* user_data)
{
    GDEBUG("Done");
    g_assert(host->app);
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_app_implicit_select(
    NFC_HOST_APP_FLAGS app_flags1,
    NFC_HOST_APP_FLAGS app_flags2,
    TEST_HOST_APP_FLAGS test_flag1)
{
    static const guchar aid1_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const guchar aid2_bytes[] = { 0x05, 0x06, 0x07, 0x08 };
    static const GUtilData aid1 = { TEST_ARRAY_AND_SIZE(aid1_bytes) };
    static const GUtilData aid2 = { TEST_ARRAY_AND_SIZE(aid2_bytes) };
    NfcInitiator* initiator = test_initiator_new();
    TestHostApp* app1 = test_host_app_new(&aid1, "TestApp1", app_flags1);
    TestHostApp* app2 = test_host_app_new(&aid2, "TestApp2", app_flags2);
    NfcHostApp* apps[3];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id;
    NfcHost* host;

    app1->flags |= test_flag1;
    apps[0] = NFC_HOST_APP(app1);
    apps[1] = NFC_HOST_APP(app2);
    apps[2] = NULL;
    host = nfc_host_new("TestHost", initiator, NULL, apps);
    id = nfc_host_add_app_changed_handler(host,
        test_app_implicit_select_done, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app1->start, == ,1);
    g_assert_cmpint(app2->start, == ,1);
    g_assert_cmpint(app2->select, == ,1);
    g_assert(host->app == apps[1]);

    g_main_loop_unref(loop);
    nfc_initiator_unref(initiator);
    nfc_host_remove_handler(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_app_unref(apps[1]);
    nfc_host_unref(host);
}

static
void
test_app_implicit_select1(
    void)
{
    test_app_implicit_select(NFC_HOST_APP_FLAGS_NONE,
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        TEST_HOST_APP_NO_FLAGS);
}

static
void
test_app_implicit_select2(
    void)
{
    test_app_implicit_select(NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT);
}

static
void
test_app_implicit_select3(
    void)
{
    test_app_implicit_select(NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT_ASYNC);
}

/*==========================================================================*
 * app_no_implicit_select
 *==========================================================================*/

static
void
test_app_no_implicit_select(
    NFC_HOST_APP_FLAGS app_flags1,
    NFC_HOST_APP_FLAGS app_flags2,
    TEST_HOST_APP_FLAGS test_flag1,
    TEST_HOST_APP_FLAGS test_flag2)
{
    static const guchar aid1_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const guchar aid2_bytes[] = { 0x05, 0x06, 0x07, 0x08 };
    static const GUtilData aid1 = { TEST_ARRAY_AND_SIZE(aid1_bytes) };
    static const GUtilData aid2 = { TEST_ARRAY_AND_SIZE(aid2_bytes) };
    static const guchar cmd_select1[] = {
        /* Non-existent app */
        0x00, 0xA4, 0x04, 0x00, 0x07, 0xd2, 0x76, 0x00,
        0x00, 0x85, 0x01, 0x01, 0x00
    };
    static const guchar resp_not_found[] = { 0x6a, 0x82 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select1) },
            { TEST_ARRAY_AND_SIZE(resp_not_found) }
        }
    };
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    TestHostApp* app1 = test_host_app_new(&aid1, "TestApp1", app_flags1);
    TestHostApp* app2 = test_host_app_new(&aid2, "TestApp2", app_flags2);
    NfcHostApp* apps[3];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id;
    NfcHost* host;

    app1->flags |= test_flag1;
    app2->flags |= test_flag2;
    apps[0] = NFC_HOST_APP(app1);
    apps[1] = NFC_HOST_APP(app2);
    apps[2] = NULL;
    host = nfc_host_new("TestHost", init, NULL, apps);
    id = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app1->start, == ,1);
    g_assert_cmpint(app2->start, == ,1);
    g_assert(!host->app);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_handler(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_app_unref(apps[1]);
    nfc_host_unref(host);
}

static
void
test_app_no_implicit_select1(
    void)
{
    test_app_no_implicit_select(NFC_HOST_APP_FLAGS_NONE,
        NFC_HOST_APP_FLAGS_NONE,
        TEST_HOST_APP_NO_FLAGS,
        TEST_HOST_APP_NO_FLAGS);
}

static
void
test_app_no_implicit_select2(
    void)
{
    test_app_no_implicit_select(NFC_HOST_APP_FLAGS_NONE,
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        TEST_HOST_APP_NO_FLAGS,
        TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT);
}

static
void
test_app_no_implicit_select3(
    void)
{
    test_app_no_implicit_select(NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION,
        TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT,
        TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT);
}

/*==========================================================================*
 * app_select
 *==========================================================================*/

static
void
test_app_select_done(
    NfcHost* host,
    void* user_data)
{
    if (host->app) {
        GDEBUG("%s selected", host->app->name);
        g_main_loop_quit((GMainLoop*)user_data);
    } else {
        GDEBUG("App deselected");
    }
}

static
void
test_app_select(
    void)
{
    static const guchar cmd_select1[] = {
        /* Non-existent app */
        0x00, 0xA4, 0x04, 0x00, 0x07, 0xd2, 0x76, 0x00,
        0x00, 0x85, 0x01, 0x01, 0x00
    };
    static const guchar cmd_select2[] = {
        /* Existent app */
        0x00, 0xA4, 0x04, 0x00, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x00
    };
    static const guchar resp_not_found[] = { 0x6a, 0x82 };
    static const guchar resp_ok[] = { 0x90, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select1) },
            { TEST_ARRAY_AND_SIZE(resp_not_found) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_select2) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        }
    };
    static const guchar aid1_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const guchar aid2_bytes[] = { 0x05, 0x06, 0x07, 0x08 };
    static const GUtilData aid1 = { TEST_ARRAY_AND_SIZE(aid1_bytes) };
    static const GUtilData aid2 = { TEST_ARRAY_AND_SIZE(aid2_bytes) };
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    TestHostApp* app1 = test_host_app_new(&aid1, "TestApp1",
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION);
    TestHostApp* app2 = test_host_app_new(&aid2, "TestApp2",
        NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION);
    NfcHostApp* apps[3];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id;
    NfcHost* host;

    apps[0] = NFC_HOST_APP(app1);
    apps[1] = NFC_HOST_APP(app2);
    apps[2] = NULL;
    host = nfc_host_new("TestHost", init, NULL, apps);
    id = nfc_host_add_app_changed_handler(host, test_app_select_done, loop);

    /* First app gets selected implicitly */
    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app1->start, == ,1);
    g_assert_cmpint(app2->start, == ,1);
    g_assert(host->app == apps[0]);

    /* And then the second one explicitly */
    test_run(&test_opt, loop);
    g_assert(host->app == apps[1]);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_handler(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_app_unref(apps[1]);
    nfc_host_unref(host);
}

/*==========================================================================*
 * app_select_fail
 *==========================================================================*/

static
void
test_app_select_fail(
    TEST_HOST_APP_FLAGS fail_flag)
{
    static const guchar cmd_select1[] = {
        0x00, 0xA4, 0x04, 0x00, 0x04, 0x01, 0x02, 0x03,
        0x04, 0x00
    };
    static const guchar cmd_select2[] = {
        0x00, 0xA4, 0x04, 0x00, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x00
    };
    static const guchar resp_err[] = { 0x6a, 0x00 };
    static const guchar resp_ok[] = { 0x90, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select1) },
            { TEST_ARRAY_AND_SIZE(resp_err) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_select2) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_select2) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        }
    };
    static const guchar aid1_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const guchar aid2_bytes[] = { 0x05, 0x06, 0x07, 0x08 };
    static const GUtilData aid1 = { TEST_ARRAY_AND_SIZE(aid1_bytes) };
    static const GUtilData aid2 = { TEST_ARRAY_AND_SIZE(aid2_bytes) };
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    TestHostApp* app1 = test_host_app_new(&aid1, "TestApp1", 0);
    TestHostApp* app2 = test_host_app_new(&aid2, "TestApp2", 0);
    NfcHostApp* app_selected = NULL;
    NfcHostApp* apps[3];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id[2];
    NfcHost* host;

    app1->flags |= fail_flag;
    apps[0] = NFC_HOST_APP(app1);
    apps[1] = NFC_HOST_APP(app2);
    apps[2] = NULL;
    host = nfc_host_new("TestHost", init, NULL, apps);
    id[0] = nfc_host_add_app_changed_handler(host, test_app_selected_once,
        &app_selected);
    id[1] = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    /* First app fails to get selected, second select is ok */
    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app1->start, == ,1);
    g_assert_cmpint(app2->start, == ,1);
    g_assert(app_selected);
    g_assert(app_selected == apps[1]);
    g_assert(host->app == app_selected);
    nfc_host_app_unref(app_selected);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_all_handlers(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_app_unref(apps[1]);
    nfc_host_unref(host);
}

static
void
test_app_select_fail1(
    void)
{
    test_app_select_fail(TEST_HOST_APP_FLAG_FAIL_SELECT);
}

static
void
test_app_select_fail2(
    void)
{
    test_app_select_fail(TEST_HOST_APP_FLAG_FAIL_SELECT_ASYNC);
}

/*==========================================================================*
 * app_switch
 *==========================================================================*/

static
void
test_app_switch(
    void)
{
    static const guchar cmd_select1[] = {
        0x00, 0xA4, 0x04, 0x00, 0x04, 0x01, 0x02, 0x03,
        0x04, 0x00
    };
    static const guchar cmd_select2[] = {
        0x00, 0xA4, 0x04, 0x00, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x00
    };
    static const guchar resp_ok[] = { 0x90, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select1) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_select1) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_select2) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        }
    };
    static const guchar aid1_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const guchar aid2_bytes[] = { 0x05, 0x06, 0x07, 0x08 };
    static const GUtilData aid1 = { TEST_ARRAY_AND_SIZE(aid1_bytes) };
    static const GUtilData aid2 = { TEST_ARRAY_AND_SIZE(aid2_bytes) };
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    TestHostApp* app1 = test_host_app_new(&aid1, "TestApp1", 0);
    TestHostApp* app2 = test_host_app_new(&aid2, "TestApp2", 0);
    NfcHostApp* apps[3];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    int app_changed_count = 0;
    gulong id[2];
    NfcHost* host;

    apps[0] = NFC_HOST_APP(app1);
    apps[1] = NFC_HOST_APP(app2);
    apps[2] = NULL;
    host = nfc_host_new("TestHost", init, NULL, apps);
    id[0] = nfc_host_add_app_changed_handler(host, test_host_inc,
        &app_changed_count);
    id[1] = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    /* First one app gets selected, then the other one */
    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app1->start, == ,1);
    g_assert_cmpint(app2->start, == ,1);
    g_assert_cmpint(app1->select, == ,1);
    g_assert_cmpint(app1->deselect, == ,1);
    g_assert_cmpint(app2->select, == ,1);
    g_assert_cmpint(app2->deselect, == ,0);
    g_assert(host->app == apps[1]);
    /* App1 => None => App2 */
    g_assert_cmpint(app_changed_count, == ,3);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_all_handlers(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_app_unref(apps[1]);
    nfc_host_unref(host);
}

/*==========================================================================*
 * app_unhandled_apdu
 *==========================================================================*/

static
void
test_app_unhandled_apdu(
    void)
{
    static const guchar cmd_apdu1[] = {
        0x00, 0xaf, 0x00, 0x00, 0x00
    };
    static const guchar cmd_apdu2[] = {
        0x90, 0xaf, 0x00, 0x00, 0x00
    };
    static const guchar resp_err1[] = { 0x6a, 0x00 };
    static const guchar resp_err2[] = { 0x6e, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_apdu1) },
            { TEST_ARRAY_AND_SIZE(resp_err1) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_apdu2) },
            { TEST_ARRAY_AND_SIZE(resp_err2) }
        }
    };
    static const guchar aid_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static const GUtilData aid = { TEST_ARRAY_AND_SIZE(aid_bytes) };
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    TestHostApp* app = test_host_app_new(&aid, "TestApp", 0);
    NfcHostApp* apps[2];
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id[2];
    NfcHost* host;

    apps[0] = NFC_HOST_APP(app);
    apps[1] = NULL;
    host = nfc_host_new("TestHost", init, NULL, apps);
    id[0] = nfc_host_add_app_changed_handler(host, test_host_not_reached, NULL);
    id[1] = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app->start, == ,1);
    g_assert(!host->app);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_all_handlers(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_unref(host);
}

/*==========================================================================*
 * app_apdu
 *==========================================================================*/

static
void
test_app_apdu(
    TEST_HOST_APP_FLAGS app_flags)
{
    static const guchar aid_bytes[] = {
        0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01
    };
    static const guchar cmd_select_app[] = {
        0x00, 0xA4, 0x04, 0x00, 0x07, 0xd2, 0x76, 0x00,
        0x00, 0x85, 0x01, 0x01, 0x00
    };
    static const guchar cmd_select_cc[] = {
        0x00, 0xa4, 0x00, 0x0c, 0x02, 0xe1, 0x03
    };
    static const guchar cmd_read_cc[] = {
      0x00, 0xb0, 0x00, 0x00, 0x0f
    };
    static const guchar resp_ok[] = { 0x90, 0x00 };
    static const guchar resp_read_cc_ok[] = {
        0x00, 0x0f, 0x20, 0x00, 0x7f, 0x00, 0x7f, 0x04,
        0x06, 0xe1, 0x04, 0x00, 0x7f, 0x00, 0x00, 0x90,
        0x00
    };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select_app) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_select_cc) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_read_cc) },
            { TEST_ARRAY_AND_SIZE(resp_read_cc_ok) }
        }
    };
    static const GUtilData aid = { TEST_ARRAY_AND_SIZE(aid_bytes) };
    TestHostService* service = test_host_service_new("TestService");
    TestHostApp* app = test_host_app_new(&aid, NULL, NFC_HOST_APP_FLAGS_NONE);
    NfcHostService* services[2];
    NfcHostApp* apps[2];
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id;
    NfcHost* host;

    app->flags |= app_flags;
    app->tx_list = tx + 1; /* Skip first SELECT */
    app->tx_count = G_N_ELEMENTS(tx) - 1;
    apps[0] = NFC_HOST_APP(app);
    apps[1] = NULL;

    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;

    host = nfc_host_new("TestHost", init, services, apps);
    id = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app->start, == ,1);
    g_assert_cmpint(app->process, == ,2); /* 2 APDUs are handled */
    g_assert_cmpint(service->start, == ,1);
    g_assert_cmpint(service->process, == ,1);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_handler(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_service_unref(services[0]);
    nfc_host_unref(host);
}

static
void
test_app_apdu1(
    void)
{
    test_app_apdu(TEST_HOST_APP_NO_FLAGS);
}

static
void
test_app_apdu2(
    void)
{
    test_app_apdu(TEST_HOST_APP_FLAG_PROCESS_SYNC);
}

/*==========================================================================*
 * app_apdu_refuse
 *==========================================================================*/

static
void
test_app_apdu_fail(
    TEST_HOST_APP_FLAGS app_flags)
{
    static const guchar aid_bytes[] = {
        0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01
    };
    static const guchar cmd_select_app[] = {
        0x00, 0xA4, 0x04, 0x00, 0x07, 0xd2, 0x76, 0x00,
        0x00, 0x85, 0x01, 0x01, 0x00
    };
    static const guchar cmd_select_cc[] = {
        0x00, 0xa4, 0x00, 0x0c, 0x02, 0xe1, 0x03
    };
    static const guchar resp_ok[] = { 0x90, 0x00 };
    static const guchar resp_err[] = { 0x6a, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select_app) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_select_cc) },
            { TEST_ARRAY_AND_SIZE(resp_err) }
        }
    };
    static const GUtilData aid = { TEST_ARRAY_AND_SIZE(aid_bytes) };
    TestHostService* service = test_host_service_new("TestService");
    TestHostApp* app = test_host_app_new(&aid, NULL, NFC_HOST_APP_FLAGS_NONE);
    NfcHostService* services[2];
    NfcHostApp* apps[2];
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id;
    NfcHost* host;

    app->flags |= app_flags;
    app->tx_list = tx + 1; /* Skip first SELECT */
    app->tx_count = G_N_ELEMENTS(tx) - 1;
    apps[0] = NFC_HOST_APP(app);
    apps[1] = NULL;

    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;

    host = nfc_host_new("TestHost", init, services, apps);
    id = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app->start, == ,1);
    g_assert_cmpint(app->process, == ,1);
    g_assert_cmpint(service->start, == ,1);
    g_assert_cmpint(service->process, == ,2); /* SELECT and the failed APDU */

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_handler(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_service_unref(services[0]);
    nfc_host_unref(host);
}

static
void
test_app_apdu_fail1(
    void)
{
    test_app_apdu_fail(TEST_HOST_APP_FLAG_PROCESS_ERR);
}

static
void
test_app_apdu_fail2(
    void)
{
    test_app_apdu_fail(TEST_HOST_APP_FLAG_PROCESS_FAIL);
}

static
void
test_app_apdu_fail3(
    void)
{
    test_app_apdu_fail(TEST_HOST_APP_FLAG_PROCESS_FAIL |
        TEST_HOST_APP_FLAG_PROCESS_SYNC);
}

/*==========================================================================*
 * app_apdu_sent
 *==========================================================================*/

static
void
test_app_apdu_sent_cb(
    NfcHostApp* app,
    gboolean ok,
    void* user_data)
{
    GDEBUG("Response sent");
    g_assert(ok);
    nfc_initiator_deactivate(NFC_INITIATOR(user_data));
}

static
void
test_app_apdu_sent(
    void)
{
    static const guchar aid_bytes[] = {
        0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01
    };
    static const guchar cmd_select_app[] = {
        0x00, 0xA4, 0x04, 0x00, 0x07, 0xd2, 0x76, 0x00,
        0x00, 0x85, 0x01, 0x01, 0x00
    };
    static const guchar cmd_select_cc[] = {
        0x00, 0xa4, 0x00, 0x0c, 0x02, 0xe1, 0x03
    };
    static const guchar resp_ok[] = { 0x90, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select_app) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_select_cc) },
            { TEST_ARRAY_AND_SIZE(resp_ok) }
        }
    };
    static const GUtilData aid = { TEST_ARRAY_AND_SIZE(aid_bytes) };
    TestHostService* service = test_host_service_new("TestService");
    TestHostApp* app = test_host_app_new(&aid, NULL, NFC_HOST_APP_FLAGS_NONE);
    NfcHostService* services[2];
    NfcHostApp* apps[2];
    NfcInitiator* init = test_initiator_new_with_tx2
        (TEST_ARRAY_AND_COUNT(tx), TRUE);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id;
    NfcHost* host;

    app->tx_list = tx + 1; /* Skip first SELECT */
    app->tx_count = G_N_ELEMENTS(tx) - 1;
    app->sent_cb = test_app_apdu_sent_cb;
    app->sent_data = init;

    apps[0] = NFC_HOST_APP(app);
    apps[1] = NULL;

    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;

    host = nfc_host_new("TestHost", init, services, apps);
    id = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(app->start, == ,1);
    g_assert_cmpint(app->process, == ,1);
    g_assert_cmpint(service->start, == ,1);
    g_assert_cmpint(service->process, == ,1);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_handler(host, id);
    nfc_host_app_unref(apps[0]);
    nfc_host_service_unref(services[0]);
    nfc_host_unref(host);
}

/*==========================================================================*
 * broken_apdu1
 *==========================================================================*/

static
void
test_broken_apdu1(
    void)
{
    static const guchar cmd_broken[] = {
        0x01, 0x02, 0x03
    };
    static const guchar resp_err[] = { 0x6a, 0x00 };
    static const TestTx tx = {
        { TEST_ARRAY_AND_SIZE(cmd_broken) },
        { TEST_ARRAY_AND_SIZE(resp_err) }
    };
    NfcInitiator* initiator = test_initiator_new_with_tx(&tx, 1);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcHost* host = nfc_host_new("TestHost", initiator, NULL, NULL);
    gulong id = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);

    g_main_loop_unref(loop);
    nfc_initiator_unref(initiator);
    nfc_host_remove_handler(host, id);
    nfc_host_unref(host);
}

/*==========================================================================*
 * broken_apdu2
 *==========================================================================*/

static
void
test_broken_apdu2(
    void)
{
    static const guchar cmd_select[] = {
        0x00, 0xA4, 0x04, 0x00, 0x04, 0x01, 0x02, 0x03,
        0x04, 0x00
    };
    static const guchar cmd_broken[] = {
        0x01, 0x02, 0x03
    };
    static const guchar resp_err[] = { 0x6a, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(cmd_select) },
            { TEST_ARRAY_AND_SIZE(resp_err) }
        },{
            { TEST_ARRAY_AND_SIZE(cmd_broken) },
            { TEST_ARRAY_AND_SIZE(resp_err) }
        }
    };
    TestHostService* service = test_host_service_new("TestService");
    NfcHostService* services[2];
    NfcInitiator* init = test_initiator_new_with_tx2
        (TEST_ARRAY_AND_COUNT(tx), FALSE);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id[2];
    NfcHost* host;

    service->tx_list = tx;
    service->tx_count = G_N_ELEMENTS(tx);
    services[0] = NFC_HOST_SERVICE(service);
    services[1] = NULL;
    host = nfc_host_new("TestHost", init, services, NULL);
    id[0] = nfc_host_add_app_changed_handler(host, test_host_not_reached, NULL);
    id[1] = nfc_host_add_gone_handler(host, test_host_done_quit, loop);

    nfc_host_start(host);
    test_run(&test_opt, loop);
    g_assert_cmpint(service->start, == ,1);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_host_remove_all_handlers(host, id);
    nfc_host_service_unref(services[0]);
    nfc_host_unref(host);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/host/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("service"), test_service);
    g_test_add_func(TEST_("service_start/1"), test_service_start1);
    g_test_add_func(TEST_("service_start/2"), test_service_start2);
    g_test_add_func(TEST_("service_start/3"), test_service_start3);
    g_test_add_func(TEST_("service_apdu_ok/1"), test_service_apdu_ok1);
    g_test_add_func(TEST_("service_apdu_ok/2"), test_service_apdu_ok2);
    g_test_add_func(TEST_("service_apdu_fail/1"), test_service_apdu_fail1);
    g_test_add_func(TEST_("service_apdu_fail/2"), test_service_apdu_fail2);
    g_test_add_func(TEST_("service_apdu_fail/3"), test_service_apdu_fail3);
    g_test_add_func(TEST_("service_apdu_sent"), test_service_apdu_sent);
    g_test_add_func(TEST_("app"), test_app);
    g_test_add_func(TEST_("app_start/1"), test_app_start1);
    g_test_add_func(TEST_("app_start/2"), test_app_start2);
    g_test_add_func(TEST_("app_start/3"), test_app_start3);
    g_test_add_func(TEST_("app_start/4"), test_app_start4);
    g_test_add_func(TEST_("app_start/5"), test_app_start5);
    g_test_add_func(TEST_("app_start/6"), test_app_start6);
    g_test_add_func(TEST_("app_implicit_select/1"), test_app_implicit_select1);
    g_test_add_func(TEST_("app_implicit_select/2"), test_app_implicit_select2);
    g_test_add_func(TEST_("app_implicit_select/3"), test_app_implicit_select3);
    g_test_add_func(TEST_("app_no_implicit_select/1"),
        test_app_no_implicit_select1);
    g_test_add_func(TEST_("app_no_implicit_select/2"),
        test_app_no_implicit_select2);
    g_test_add_func(TEST_("app_no_implicit_select/3"),
        test_app_no_implicit_select3);
    g_test_add_func(TEST_("app_select"), test_app_select);
    g_test_add_func(TEST_("app_select_fail/1"), test_app_select_fail1);
    g_test_add_func(TEST_("app_select_fail/2"), test_app_select_fail2);
    g_test_add_func(TEST_("app_switch"), test_app_switch);
    g_test_add_func(TEST_("app_unhandled_apdu"), test_app_unhandled_apdu);
    g_test_add_func(TEST_("app_apdu/1"), test_app_apdu1);
    g_test_add_func(TEST_("app_apdu/2"), test_app_apdu2);
    g_test_add_func(TEST_("app_apdu_fail/1"), test_app_apdu_fail1);
    g_test_add_func(TEST_("app_apdu_fail/2"), test_app_apdu_fail2);
    g_test_add_func(TEST_("app_apdu_fail/3"), test_app_apdu_fail3);
    g_test_add_func(TEST_("app_apdu_sent"), test_app_apdu_sent);
    g_test_add_func(TEST_("broken_apdu/1"), test_broken_apdu1);
    g_test_add_func(TEST_("broken_apdu/2"), test_broken_apdu2);
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
