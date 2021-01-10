/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_peer_p.h"
#include "nfc_target.h"
#include "nfc_initiator.h"
#include "nfc_peer_services.h"
#include "nfc_peer_service_impl.h"
#include "nfc_peer_socket.h"
#include "nfc_ndef.h"

#include "test_common.h"
#include "test_target.h"
#include "test_initiator.h"

#include <gutil_log.h>

static TestOpt test_opt;

static const guint8 initial_llcp_params [] = {
    0x46, 0x66, 0x6d, 0x01, 0x01, 0x11, 0x02, 0x02,
    0x07, 0xff, 0x03, 0x02, 0x00, 0x13, 0x04, 0x01,
    0xff
};

static const NfcParamNfcDepTarget target_params = {
    { TEST_ARRAY_AND_SIZE(initial_llcp_params) }
};

static const NfcParamNfcDepInitiator initiator_params = {
    { TEST_ARRAY_AND_SIZE(initial_llcp_params) }
};

static const guint8 symm_data[] = { 0x00, 0x00 };

static
void
test_peer_quit_loop_cb(
    NfcPeer* peer,
    void* user_data)
{
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_peer_not_reached_cb(
    NfcPeer* peer,
    void* user_data)
{
    g_assert_not_reached();
}

static
void
test_peer_inc(
    NfcPeer* peer,
    void* user_data)
{
    (*((int*)user_data))++;
}

/*==========================================================================*
 * Test service
 *==========================================================================*/

typedef NfcPeerServiceClass TestServiceClass;
typedef struct test_service {
    NfcPeerService service;
    gboolean fail_connect;
    int peer_in;
    int peer_out;
} TestService;

G_DEFINE_TYPE(TestService, test_service, NFC_TYPE_PEER_SERVICE)
#define TEST_TYPE_SERVICE (test_service_get_type())
#define TEST_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_SERVICE, TestService))

static
void
test_service_peer_arrived(
    NfcPeerService* service,
    NfcPeer* peer)
{
    TEST_SERVICE(service)->peer_in++;
}

static
void
test_service_peer_left(
    NfcPeerService* service,
    NfcPeer* peer)
{
    TEST_SERVICE(service)->peer_out++;
}

static
NfcPeerConnection*
test_service_new_connect(
    NfcPeerService* service,
    guint8 rsap,
    const char* name)
{
    return TEST_SERVICE(service)->fail_connect ? NULL :
        NFC_PEER_CONNECTION(nfc_peer_socket_new_connect(service, rsap, name));
}

static
void
test_service_init(
    TestService* self)
{
}

static
void
test_service_class_init(
    TestServiceClass* klass)
{
    klass->peer_arrived = test_service_peer_arrived;
    klass->peer_left = test_service_peer_left;
    klass->new_connect = test_service_new_connect;
}

static
TestService*
test_service_client_new(
    void)
{
    NfcPeerService* service = g_object_new(TEST_TYPE_SERVICE, NULL);

    nfc_peer_service_init_base(service, NULL);
    return TEST_SERVICE(service);
}

static
TestService*
test_service_client_new_fail(
    void)
{
    TestService* self = g_object_new(TEST_TYPE_SERVICE, NULL);
    NfcPeerService* service = &self->service;

    nfc_peer_service_init_base(service, NULL);
    service->sap = NFC_LLC_SAP_UNNAMED;
    self->fail_connect = TRUE;
    return self;
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
    g_assert(!nfc_peer_new_target(NULL, NFC_TECHNOLOGY_A, NULL, NULL));
    g_assert(!nfc_peer_new_initiator(NULL, NFC_TECHNOLOGY_A, NULL, NULL));
    g_assert(!nfc_peer_ref(NULL));
    g_assert(!nfc_peer_connect(NULL, NULL, 0, NULL, NULL, NULL));
    g_assert(!nfc_peer_connect_sn(NULL, NULL, NULL, NULL, NULL, NULL));
    g_assert(!nfc_peer_add_wks_changed_handler(NULL, NULL, NULL));
    g_assert(!nfc_peer_add_ndef_changed_handler(NULL, NULL, NULL));
    g_assert(!nfc_peer_add_initialized_handler(NULL, NULL, NULL));
    g_assert(!nfc_peer_add_gone_handler(NULL, NULL, NULL));
    g_assert(!nfc_peer_register_service(NULL, NULL));
    nfc_peer_deactivate(NULL);
    nfc_peer_unregister_service(NULL, NULL);
    nfc_peer_remove_handler(NULL, 0);
    nfc_peer_unref(NULL);
}

/*==========================================================================*
 * name
 *==========================================================================*/

static
void
test_name(
    void)
{
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { NULL, 0 }
        }
    };

    NfcTarget* target = test_target_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcPeer* peer = nfc_peer_new_initiator(target, NFC_TECHNOLOGY_A,
        &initiator_params, NULL);
    const char* name = "foo";

    g_assert(peer);
    g_assert(!peer->name);
    nfc_peer_set_name(peer, NULL);
    g_assert(!peer->name);
    nfc_peer_set_name(peer, name);
    g_assert_cmpstr(peer->name, == ,name);

    nfc_peer_unref(peer);
    nfc_target_unref(target);
}

/*==========================================================================*
 * no_magic
 *==========================================================================*/

static
void
test_no_magic(
    gconstpointer data)
{
    NfcTarget* target = test_target_new(TEST_TARGET_FAIL_ALL);
    const NfcParamNfcDepInitiator* param = data;

    g_assert(!nfc_peer_new_initiator(target, NFC_TECHNOLOGY_A, param, NULL));
    nfc_target_unref(target);
}

static const guint8 no_magic_data_1 [] = { 0x46, 0x66  };
static const guint8 no_magic_data_2 [] = { 0x66, 0x66, 0x66, 0x66  };
static const NfcParamNfcDepInitiator no_magic_1 = {
    { TEST_ARRAY_AND_SIZE(no_magic_data_1) }
};
static const NfcParamNfcDepInitiator no_magic_2 = {
    { TEST_ARRAY_AND_SIZE(no_magic_data_2) }
};

/*==========================================================================*
 * no_param
 *==========================================================================*/

static
void
test_no_param_target(
    void)
{
    NfcInitiator* initiator = test_initiator_new();

    g_assert(!nfc_peer_new_target(initiator, NFC_TECHNOLOGY_A, NULL, NULL));
    nfc_initiator_unref(initiator);
}

static
void
test_no_param_initiator(
    void)
{
    NfcTarget* target = test_target_new(TEST_TARGET_FAIL_ALL);

    g_assert(!nfc_peer_new_initiator(target, NFC_TECHNOLOGY_A, NULL, NULL));
    nfc_target_unref(target);
}

/*==========================================================================*
 * ndef
 *==========================================================================*/

static
void
test_ndef(
    void)
{
    static const guint8 atr_res_g [] = {
        0x46, 0x66, 0x6d, 0x01, 0x01, 0x11, 0x02, 0x02,
        0x07, 0xff, 0x03, 0x02, 0x00, 0x13, 0x04, 0x01,
        0xff
    };
    static const NfcParamNfcDepInitiator params = {
        { TEST_ARRAY_AND_SIZE(atr_res_g) }
    };
    static const guint8 connect_snep_data[] = {
        0x05, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
        0x0f, 0x06, 0x0f, 0x75, 0x72, 0x6e, 0x3a, 0x6e,
        0x66, 0x63, 0x3a, 0x73, 0x6e, 0x3a, 0x73, 0x6e,
        0x65, 0x70
    };
    static const guint8 cc_snep_data[] = {
        0x81, 0x84, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
        0x0f
    };
    static const guint8 i_snep_4_32_put_data[] = {
        0x13, 0x20, 0x00,
        0x10, 0x02, 0x00, 0x00, 0x00, 0x1f,
        0xd1, 0x02, 0x1a, 0x53, 0x70, 0x91, 0x01, 0x0a,
        0x55, 0x03, 0x6a, 0x6f, 0x6c, 0x6c, 0x61, 0x2e,
        0x63, 0x6f, 0x6d, 0x51, 0x01, 0x08, 0x54, 0x02,
        0x65, 0x6e, 0x4a, 0x6f, 0x6c, 0x6c, 0x61
    };
    static const guint8 rnr_32_4_data[] = { 0x83, 0x84, 0x01 };
    static const guint8 disc_32_4_data[] = { 0x81, 0x44 };
    static const guint8 dm_4_32_data[] = { 0x11, 0xe0, 0x00 };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(connect_snep_data) }
        },{
            { TEST_ARRAY_AND_SIZE(cc_snep_data) },
            { TEST_ARRAY_AND_SIZE(i_snep_4_32_put_data) }
        },{
            { TEST_ARRAY_AND_SIZE(rnr_32_4_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(disc_32_4_data) },
            { TEST_ARRAY_AND_SIZE(dm_4_32_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        }
    };

    const NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_A;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = test_target_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcPeer* peer = nfc_peer_new_initiator(target, tech, &params, NULL);
    gulong id;

    g_assert(peer);
    g_assert(peer->technology == tech);

    /* Not initialized yet */
    g_assert(!(peer->flags & NFC_PEER_FLAG_INITIALIZED));

    /* Let it initialize */
    id = nfc_peer_add_initialized_handler(peer, test_peer_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    nfc_peer_remove_handler(peer, id);

    /* Now it must be initialized */
    g_assert(peer->present);
    g_assert(peer->flags & NFC_PEER_FLAG_INITIALIZED);
    g_assert(peer->ndef);
    g_assert(NFC_IS_NDEF_REC_SP(peer->ndef));

    nfc_peer_unref(peer);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * no_ndef
 *==========================================================================*/

static
void
test_no_ndef(
    void)
{
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { NULL, 0 }
        }
    };

    const NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_F;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = test_target_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcPeer* peer = nfc_peer_new_initiator(target, tech, &initiator_params,
        NULL);
    gulong id;

    g_assert(peer);
    g_assert(peer->technology == tech);

    /* Not initialized yet */
    g_assert(!(peer->flags & NFC_PEER_FLAG_INITIALIZED));

    /* Let it initialize */
    id = nfc_peer_add_initialized_handler(peer, test_peer_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    nfc_peer_remove_handler(peer, id);

    /* Now it must be initialized */
    g_assert(peer->flags & NFC_PEER_FLAG_INITIALIZED);

    /* But there's no NDEF resord */
    g_assert(!peer->ndef);

    nfc_peer_unref(peer);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * connect
 *==========================================================================*/

typedef struct test_connect_data {
    gboolean destroyed;
    gboolean connected;
} TestConnectData;

static const guint8 cc_32_32_data[] = {
    0x81, 0xa0, 0x02, 0x02, 0x00, 0x00, 0x05, 0x01, 0x0f
};
static const guint8 disc_32_32_data[] = { 0x81, 0x60 };
static const guint8 dm_32_32_0_data[] = { 0x81, 0xe0, 0x00 };
static const guint8 connect_32_32_data[] = {
    0x81, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f
};
static const guint8 connect_32_test_data[] = {
    0x05, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f, 0x06, 0x04, 0x74, 0x65, 0x73, 0x74
};

static
void
test_connect_complete(
    NfcPeer* peer,
    NfcPeerConnection* connection,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data)
{
    TestConnectData* test = user_data;

    GDEBUG("Connection status %d", result);
    g_assert(result == NFC_PEER_CONNECT_OK);
    g_assert(!test->connected);
    test->connected = TRUE;
}

static
void
test_connect_destroy(
    gpointer user_data)
{
    TestConnectData* test = user_data;

    GDEBUG("Connection data finalized");
    g_assert(!test->destroyed);
    test->destroyed = TRUE;
}

static
void
test_connect_sap_common(
    NfcPeer* peer,
    TestService* ts)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcPeerService* service = NFC_PEER_SERVICE(ts);
    NfcPeerConnection* conn;
    TestConnectData test;
    gulong id;

    memset(&test, 0, sizeof(test));
    g_assert(!nfc_peer_register_service(peer, service)); /* Duplicate */

    /* Not initialized yet */
    g_assert(!(peer->flags & NFC_PEER_FLAG_INITIALIZED));

    /* Request the connection */
    conn = nfc_peer_connection_ref(nfc_peer_connect(peer, service,
        NFC_LLC_SAP_UNNAMED, test_connect_complete, test_connect_destroy,
        &test));

    id = nfc_peer_add_initialized_handler(peer, test_peer_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    nfc_peer_remove_handler(peer, id);

    g_assert(conn->state == NFC_LLC_CO_DEAD);
    g_assert(peer->flags & NFC_PEER_FLAG_INITIALIZED);
    g_assert(!peer->ndef);
    g_assert(peer->present);
    g_assert(test.connected);
    g_assert(test.destroyed);

    g_assert_cmpint(ts->peer_in, == ,1);
    g_assert_cmpint(ts->peer_out, == ,0);

    /* Now let it disappear */
    id = nfc_peer_add_gone_handler(peer, test_peer_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    nfc_peer_remove_handler(peer, id);
    g_assert(!peer->present);
    g_assert_cmpint(ts->peer_in, == ,1);
    g_assert_cmpint(ts->peer_out, == ,1);

    nfc_peer_connection_unref(conn);
    g_main_loop_unref(loop);
}

static
void
test_connect_sap_target(
    void)
{
    /* Connection is quickly established and terminated */
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(connect_32_32_data) }
        },{
            { TEST_ARRAY_AND_SIZE(cc_32_32_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(disc_32_32_data) },
            { TEST_ARRAY_AND_SIZE(dm_32_32_0_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{ /* At this point LLCP gets into idle state */
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { NULL, 0  }
        }
    };

    const NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_A;
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    TestService* ts = test_service_client_new();
    NfcPeerService* service = NFC_PEER_SERVICE(ts);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcPeer* peer;

    nfc_peer_services_add(services, service);
    peer = nfc_peer_new_target(init, tech, &target_params, services);
    g_assert(peer);

    test_connect_sap_common(peer, ts);

    nfc_peer_services_unref(services);
    nfc_peer_service_unref(service);
    nfc_peer_unregister_service(peer, service);
    nfc_peer_unref(peer);
    nfc_initiator_unref(init);
}

static
void
test_connect_sap_initiator(
    void)
{
    /* Connection is quickly established and terminated */
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(connect_32_32_data) },
            { TEST_ARRAY_AND_SIZE(cc_32_32_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(disc_32_32_data) }
        },{
            { TEST_ARRAY_AND_SIZE(dm_32_32_0_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) },
        },{ /* At this point LLCP gets into idle state */
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { NULL, 0  }
        }
    };

    const NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_A;
    NfcTarget* target = test_target_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    TestService* ts = test_service_client_new();
    NfcPeerService* service = NFC_PEER_SERVICE(ts);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcPeer* peer;

    nfc_peer_services_add(services, service);
    peer = nfc_peer_new_initiator(target, tech, &initiator_params, services);
    g_assert(peer);

    test_connect_sap_common(peer, ts);

    nfc_peer_services_unref(services);
    nfc_peer_service_unref(service);
    nfc_peer_unregister_service(peer, service);
    nfc_peer_unref(peer);
    nfc_target_unref(target);
}

static
void
test_connect_sn_common(
    NfcPeer* peer,
    NfcPeerService* service)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcPeerConnection* conn;
    TestConnectData test;
    gulong id;

    memset(&test, 0, sizeof(test));
    g_assert(!nfc_peer_register_service(peer, service)); /* Duplicate */

    /* Not initialized yet */
    g_assert(!(peer->flags & NFC_PEER_FLAG_INITIALIZED));

    /* Request the connection */
    conn = nfc_peer_connection_ref(nfc_peer_connect_sn(peer, service,
        "test", test_connect_complete, test_connect_destroy, &test));

    id = nfc_peer_add_initialized_handler(peer, test_peer_quit_loop_cb, loop);
    test_run(&test_opt, loop);

    g_assert(conn->state == NFC_LLC_CO_DEAD);
    g_assert(peer->flags & NFC_PEER_FLAG_INITIALIZED);
    g_assert(!peer->ndef);
    g_assert(peer->present);
    g_assert(test.connected);
    g_assert(test.destroyed);

    nfc_peer_connection_unref(conn);
    nfc_peer_remove_handler(peer, id);
    g_main_loop_unref(loop);
}

static
void
test_connect_sn_target(
    void)
{
    /* Connection is quickly established and terminated */
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(connect_32_test_data) }
        },{
            { TEST_ARRAY_AND_SIZE(cc_32_32_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(disc_32_32_data) },
            { TEST_ARRAY_AND_SIZE(dm_32_32_0_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{ /* At this point LLCP gets into idle state */
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { NULL, 0 }
        }
    };

    const NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_A;
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcPeerService* service = NFC_PEER_SERVICE(test_service_client_new());
    NfcPeerServices* services = nfc_peer_services_new();
    NfcPeer* peer;

    nfc_peer_services_add(services, service);
    peer = nfc_peer_new_target(init, tech, &target_params, services);
    g_assert(peer->technology == tech);

    test_connect_sn_common(peer, service);

    nfc_peer_services_unref(services);
    nfc_peer_service_unref(service);
    nfc_peer_unregister_service(peer, service);
    nfc_peer_unref(peer);
    nfc_initiator_unref(init);
}

static
void
test_connect_sn_initiator(
    void)
{
    /* Connection is quickly established and terminated */
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(connect_32_test_data) },
            { TEST_ARRAY_AND_SIZE(cc_32_32_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(disc_32_32_data) }
        },{
            { TEST_ARRAY_AND_SIZE(dm_32_32_0_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        },{ /* At this point LLCP gets into idle state */
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { NULL, 0 }
        }
    };

    const NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_A;
    NfcTarget* target = test_target_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcPeerService* service = NFC_PEER_SERVICE(test_service_client_new());
    NfcPeerServices* services = nfc_peer_services_new();
    NfcPeer* peer;

    nfc_peer_services_add(services, service);
    peer = nfc_peer_new_initiator(target, tech, &initiator_params, services);
    g_assert(peer->technology == tech);

    test_connect_sn_common(peer, service);

    nfc_peer_services_unref(services);
    nfc_peer_service_unref(service);
    nfc_peer_unregister_service(peer, service);
    nfc_peer_unref(peer);
    nfc_target_unref(target);
}

/*==========================================================================*
 * connect_fail
 *==========================================================================*/

static
void
test_connect_fail(
    void)
{
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { NULL, 0 }
        }
    };

    const NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_A;
    NfcTarget* target = test_target_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcPeerService* service = NFC_PEER_SERVICE(test_service_client_new_fail());
    NfcPeer* peer = nfc_peer_new_initiator(target, tech, &initiator_params,
        NULL);

    g_assert(nfc_peer_register_service(peer, service));
    g_assert(peer->technology == tech);

    /* Service refuses to create connections */
    g_assert(!nfc_peer_connect(peer, service, 0, NULL, NULL, NULL));
    g_assert(!nfc_peer_connect_sn(peer, service, "foo", NULL, NULL, NULL));

    nfc_peer_service_unref(service);
    nfc_peer_unref(peer);
    nfc_target_unref(target);
}

/*==========================================================================*
 * error
 *==========================================================================*/

static
void
test_error(
    void)
{
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { NULL, 0 }
        }
    };

    NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_A;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = test_target_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    TestService* ts = test_service_client_new();
    NfcPeerService* service = NFC_PEER_SERVICE(ts);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcPeer* peer;
    gulong id[2];

    nfc_peer_services_add(services, service);
    peer = nfc_peer_new_initiator(target, tech, &initiator_params, services);
    g_assert(peer);
    g_assert(peer->technology == tech);

    /* Not initialized yet */
    g_assert(!(peer->flags & NFC_PEER_FLAG_INITIALIZED));

    /* Give it a try */
    id[0] = nfc_peer_add_gone_handler(peer, test_peer_quit_loop_cb, loop);
    id[1] = nfc_peer_add_initialized_handler(peer,
        test_peer_not_reached_cb, loop);
    test_run(&test_opt, loop);
    nfc_peer_remove_all_handlers(peer, id);

    /* It must be gone and not initialized */
    g_assert(!(peer->flags & NFC_PEER_FLAG_INITIALIZED));
    g_assert(!peer->ndef);
    g_assert(!peer->present);

    /* Never arriuved and never left */
    g_assert_cmpint(ts->peer_in, == ,0);
    g_assert_cmpint(ts->peer_out, == ,0);

    nfc_peer_unref(peer);
    nfc_target_unref(target);
    nfc_peer_services_unref(services);
    nfc_peer_service_unref(service);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * wks
 *==========================================================================*/

static
void
test_wks(
    void)
{
    static const guint8 atr_res_g [] = {
        0x46, 0x66, 0x6d, 0x01, 0x01, 0x11, 0x02, 0x02,
        0x07, 0xff, 0x03, 0x02, 0x00, 0x01, 0x04, 0x01,
        0xff
    };
    static const guint8 pax_data[] = {
        0x00, 0x40, 0x03, 0x02, 0x00, 0x11
    };
    static const NfcParamNfcDepInitiator params = {
        { TEST_ARRAY_AND_SIZE(atr_res_g) }
    };
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(pax_data) }
        },{
            { TEST_ARRAY_AND_SIZE(symm_data) },
            { TEST_ARRAY_AND_SIZE(symm_data) }
        }
    };

    const NFC_TECHNOLOGY tech = NFC_TECHNOLOGY_A;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = test_target_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcPeer* peer = nfc_peer_new_initiator(target, tech, &params, NULL);
    gulong id[2];
    int count;

    g_assert(peer);
    g_assert_cmpuint(peer->wks, == ,0x01);
    g_assert(peer->technology == tech);

    /* Not initialized yet */
    g_assert(!(peer->flags & NFC_PEER_FLAG_INITIALIZED));

    /* These do nothing */
    g_assert(!nfc_peer_add_wks_changed_handler(peer, NULL, NULL));
    g_assert(!nfc_peer_add_ndef_changed_handler(peer, NULL, NULL));
    g_assert(!nfc_peer_add_initialized_handler(peer, NULL, NULL));
    g_assert(!nfc_peer_add_gone_handler(peer, NULL, NULL));
    nfc_peer_remove_handler(peer, 0);

    /* Wait for it to initialize */
    count = 0;
    id[0] = nfc_peer_add_wks_changed_handler(peer, test_peer_inc, &count);
    id[1] = nfc_peer_add_initialized_handler(peer,
        test_peer_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    nfc_peer_remove_handler(peer, id[0]);
    nfc_peer_remove_handler(peer, id[1]);

    /* Must be initialized and wks must have changed */
    g_assert_cmpuint(count, == ,1);
    g_assert_cmpuint(peer->wks, == ,0x11);
    g_assert(peer->flags & NFC_PEER_FLAG_INITIALIZED);
    g_assert(peer->present);
    g_assert(!peer->ndef);

    nfc_peer_unref(peer);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/peer/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("name"), test_name);
    g_test_add_func(TEST_("error"), test_error);
    g_test_add_func(TEST_("wks"), test_wks);
    g_test_add_data_func(TEST_("no_magic/1"), &no_magic_1, test_no_magic);
    g_test_add_data_func(TEST_("no_magic/2"), &no_magic_2, test_no_magic);
    g_test_add_func(TEST_("no_param/target"), test_no_param_target);
    g_test_add_func(TEST_("no_param/initiator"), test_no_param_initiator);
    g_test_add_func(TEST_("ndef"), test_ndef);
    g_test_add_func(TEST_("no_ndef"), test_no_ndef);
    g_test_add_func(TEST_("connect/sap/target"), test_connect_sap_target);
    g_test_add_func(TEST_("connect/sap/initiator"), test_connect_sap_initiator);
    g_test_add_func(TEST_("connect/sn/target"), test_connect_sn_target);
    g_test_add_func(TEST_("connect/sn/initiator"), test_connect_sn_initiator);
    g_test_add_func(TEST_("connect/fail"), test_connect_fail);
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
