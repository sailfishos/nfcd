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

#include "nfc_llc.h"
#include "nfc_llc_io.h"
#include "nfc_llc_param.h"
#include "nfc_peer_services.h"
#include "nfc_peer_service_p.h"
#include "nfc_peer_service_impl.h"
#include "nfc_peer_connection_p.h"
#include "nfc_peer_connection_impl.h"
#include "nfc_initiator.h"
#include "nfc_target_impl.h"

#include "test_common.h"
#include "test_target.h"
#include "test_initiator.h"

#include <gutil_log.h>

static TestOpt test_opt;

#define TEST_PREFIX "/core/llc/"
#define TEST_(name) TEST_PREFIX name

static const guint8 symm_pdu_data[] = { 0x00, 0x00 };
static const guint8 connect_urn_nfc_sn_handover_data[] = {
    0x05, 0x21, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f, 0x06, 0x13, 0x75, 0x72, 0x6e, 0x3a, 0x6e,
    0x66, 0x63, 0x3a, 0x73, 0x6e, 0x3a, 0x68, 0x61,
    0x6e, 0x64, 0x6f, 0x76, 0x65, 0x72
};
static const guint8 connect_2_data[] = {
    0x11, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    /*^ DSAP = 2 */
    0x0f
};
static const guint8 connect_sdp_empty_data[] = {
    0x05, 0x20
};
static const guint8 llc_param_tlv_data[] = {
    0x01, 0x01, 0x11, 0x02, 0x02, 0x07, 0xff, 0x03,
    0x02, 0x00, 0x13, 0x04, 0x01, 0xff, 0x07, 0x01,
    0x03
};
static const GUtilData llc_param_tlv = {
    TEST_ARRAY_AND_SIZE(llc_param_tlv_data)
};

static
void
test_llc_quit_loop_cb(
    NfcLlc* llc,
    void* user_data)
{
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_llc_quit_when_dead_cb(
    NfcPeerConnection* conn,
    void* user_data)
{
    GDEBUG("Connection state %d", conn->state);

    if (conn->state == NFC_LLC_CO_DEAD) {
        g_main_loop_quit((GMainLoop*)user_data);
    }
}

/*==========================================================================*
 * Test connection
 *==========================================================================*/

typedef NfcPeerConnectionClass TestConnectionClass;
typedef struct test_connection TestConnection;
typedef void (*TestConnectionHookFunc)(TestConnection* conn, void* user_data);
typedef struct test_connection_hook {
    TestConnectionHookFunc proc;
    void* user_data;
} TestConnectionHook;

struct test_connection {
    NfcPeerConnection connection;
    TestConnectionHook state_change_hook;
    TestConnectionHook finalize_hook;
    gboolean accept_connection;
    GByteArray* received;
};

G_DEFINE_TYPE(TestConnection, test_connection, NFC_TYPE_PEER_CONNECTION)
#define TEST_TYPE_CONNECTION (test_connection_get_type())
#define TEST_CONNECTION(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_CONNECTION, TestConnection))
#define TEST_CONNECTION_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS((obj), \
        TEST_TYPE_CONNECTION, TestConnectionClass)

static
void
test_connection_accept(
    NfcPeerConnection* conn)
{
    TestConnection* test = TEST_CONNECTION(conn);

    if (test->accept_connection) {
        nfc_peer_connection_accepted(conn);
    } else {
        nfc_peer_connection_rejected(conn);
    }
}

static
void
test_connection_state_changed(
    NfcPeerConnection* conn)
{
    TestConnection* test = TEST_CONNECTION(conn);

    if (test->state_change_hook.proc) {
        test->state_change_hook.proc(test, test->state_change_hook.user_data);
    }
    NFC_PEER_CONNECTION_CLASS(test_connection_parent_class)->
        state_changed(conn);
}

static
void
test_connection_data_received(
    NfcPeerConnection* conn,
    const void* data,
    guint len)
{
    TestConnection* test = TEST_CONNECTION(conn);

    g_byte_array_append(test->received, data, len);
    NFC_PEER_CONNECTION_CLASS(test_connection_parent_class)->
        data_received(conn, data, len);
}

static
void
test_connection_init(
    TestConnection* test)
{
    test->received = g_byte_array_new();
}

static
void
test_connection_finalize(
    GObject* object)
{
    TestConnection* test = TEST_CONNECTION(object);

    if (test->finalize_hook.proc) {
        test->finalize_hook.proc(test, test->finalize_hook.user_data);
    }
    g_byte_array_free(test->received, TRUE);
    G_OBJECT_CLASS(test_connection_parent_class)->finalize(object);
}

static
void
test_connection_class_init(
    TestConnectionClass* klass)
{
    klass->accept = test_connection_accept;
    klass->state_changed = test_connection_state_changed;
    klass->data_received = test_connection_data_received;
    G_OBJECT_CLASS(klass)->finalize = test_connection_finalize;
}

static
TestConnection*
test_connection_new_connect(
    NfcPeerService* svc,
    guint8 rsap,
    const char* name)
{
    TestConnection* test = g_object_new(TEST_TYPE_CONNECTION, NULL);

    nfc_peer_connection_init_connect(&test->connection, svc, rsap, name);
    return test;
}

static
TestConnection*
test_connection_new_accept(
    NfcPeerService* svc,
    guint8 rsap)
{
    TestConnection* test = g_object_new(TEST_TYPE_CONNECTION, NULL);

    nfc_peer_connection_init_accept(&test->connection, svc, rsap);
    return test;
}

/*==========================================================================*
 * Test service
 *==========================================================================*/

typedef NfcPeerServiceClass TestServiceClass;
typedef struct test_service {
    NfcPeerService service;
    TestConnectionHook connection_state_change_hook;
    TestConnectionHook connection_finalize_hook;
    gboolean allow_connections;
    gboolean accept_connections;
    gboolean cancel_connections;
    int accept_count;
} TestService;

G_DEFINE_TYPE(TestService, test_service, NFC_TYPE_PEER_SERVICE)
#define TEST_TYPE_SERVICE (test_service_get_type())
#define TEST_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_SERVICE, TestService))

static
NfcPeerConnection*
test_service_new_connect(
    NfcPeerService* service,
    guint8 rsap,
    const char* name)
{
    TestService* test = TEST_SERVICE(service);

    if (test->allow_connections) {
        TestConnection* conn = test_connection_new_connect(service, rsap, name);

        conn->state_change_hook = test->connection_state_change_hook;
        conn->finalize_hook = test->connection_finalize_hook;
        if (test->cancel_connections) {
            /* Will return dead connection */
            nfc_peer_connection_disconnect(&conn->connection);
        }
        return &conn->connection;
    } else {
        return NFC_PEER_SERVICE_CLASS(test_service_parent_class)->
            new_connect(service, rsap, name);
    }
}

static
NfcPeerConnection*
test_service_new_accept(
    NfcPeerService* service,
    guint8 rsap)
{
    TestService* test = TEST_SERVICE(service);

    if (test->allow_connections) {
        TestConnection* conn = test_connection_new_accept(service, rsap);

        test->accept_count++;
        conn->state_change_hook = test->connection_state_change_hook;
        conn->finalize_hook = test->connection_finalize_hook;
        conn->accept_connection = test->accept_connections;
        if (test->cancel_connections) {
            /* Will return dead connection */
            nfc_peer_connection_disconnect(&conn->connection);
        }
        return &conn->connection;
    } else {
        return NFC_PEER_SERVICE_CLASS(test_service_parent_class)->
            new_accept(service, rsap);
    }
}

static
void
test_service_datagram_received(
    NfcPeerService* service,
    guint8 ssap,
    const void* data,
    guint len)
{
    GDEBUG("%u byte(s) received", len);
    NFC_PEER_SERVICE_CLASS(test_service_parent_class)->
        datagram_received(service, ssap, data, len);
}

static
void
test_service_init(
    TestService* self)
{
    self->allow_connections = TRUE;
    self->accept_connections = TRUE;
}

static
void
test_service_finalize(
    GObject* object)
{
    G_OBJECT_CLASS(test_service_parent_class)->finalize(object);
}

static
void
test_service_class_init(
    TestServiceClass* klass)
{
    klass->new_connect = test_service_new_connect;
    klass->new_accept = test_service_new_accept;
    klass->datagram_received = test_service_datagram_received;
    G_OBJECT_CLASS(klass)->finalize = test_service_finalize;
}

static
TestService*
test_service_new(
    const char* name)
{
    TestService* service = g_object_new(TEST_TYPE_SERVICE, NULL);

    nfc_peer_service_init_base(&service->service, name);
    return service;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    GBytes* pdu = g_bytes_new_static(TEST_ARRAY_AND_SIZE(symm_pdu_data));
    NfcTarget* target = test_target_new(FALSE);
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc = nfc_llc_new(io, NULL, NULL);

    g_assert(!nfc_llc_io_target_new(NULL));
    g_assert(!nfc_llc_io_initiator_new(NULL));
    g_assert(!nfc_llc_new(NULL, NULL, NULL));
    g_assert(!nfc_llc_add_state_changed_handler(NULL, NULL, NULL));
    g_assert(!nfc_llc_add_idle_changed_handler(NULL, NULL, NULL));
    g_assert(!nfc_llc_add_wks_changed_handler(NULL, NULL, NULL));
    g_assert(!nfc_peer_connection_rmiu(NULL));
    g_assert(!nfc_peer_connection_key(NULL));
    g_assert(!nfc_peer_connection_ref(NULL));
    nfc_llc_submit_i_pdu(NULL, NULL, NULL, 0);
    nfc_peer_connection_disconnect(NULL);
    g_assert(!nfc_peer_connection_send(NULL, NULL));
    nfc_llc_connection_dead(NULL, NULL);
    nfc_llc_connection_dead(llc, NULL);
    g_assert(!nfc_peer_connection_cancel(NULL));
    g_assert(!nfc_peer_connection_add_state_changed_handler(NULL, NULL, NULL));
    nfc_peer_connection_remove_handler(NULL, 0);
    nfc_peer_connection_unref(NULL);
    g_assert(!nfc_llc_connect_sn(NULL, NULL, NULL, NULL, NULL, NULL));
    g_assert(!nfc_llc_connect_sn(llc, NULL, NULL, NULL, NULL, NULL));
    g_assert(!nfc_llc_connect(NULL, NULL, 0, NULL, NULL, NULL));
    g_assert(!nfc_llc_connect(llc, NULL, 0, NULL, NULL, NULL));
    g_assert(!nfc_llc_cancel_connect_request(NULL, NULL));
    g_assert(!nfc_llc_cancel_connect_request(llc, NULL));
    g_assert(!nfc_llc_i_pdu_queued(NULL, NULL));
    g_assert(!nfc_llc_i_pdu_queued(llc, NULL));
    nfc_llc_submit_disc_pdu(NULL, 0, 0);
    nfc_llc_submit_dm_pdu(NULL, 0, 0, 0);
    nfc_llc_submit_cc_pdu(NULL, NULL);
    nfc_llc_ack(NULL, NULL, FALSE);
    nfc_llc_ack(llc, NULL, FALSE);
    nfc_llc_remove_handler(NULL, 0);
    nfc_llc_remove_handler(NULL, 1);
    nfc_llc_remove_handlers(NULL, NULL, 0);
    nfc_llc_free(NULL);

    g_assert(!nfc_llc_io_ref(NULL));
    g_assert(!nfc_llc_io_start(NULL));
    g_assert(!nfc_llc_io_send(NULL, NULL));
    g_assert(!nfc_llc_io_add_can_send_handler(NULL, NULL, NULL));
    g_assert(!nfc_llc_io_add_receive_handler(NULL, NULL, NULL));
    g_assert(!nfc_llc_io_add_error_handler(NULL, NULL, NULL));
    nfc_llc_io_unref(NULL);

    g_bytes_unref(pdu);
    nfc_llc_free(llc);
    nfc_llc_io_unref(io);
    nfc_target_unref(target);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    /* The initial SYMM transmit will fail */
    NfcTarget* target = test_target_new(TEST_TARGET_FAIL_ALL);
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc = nfc_llc_new(io, NULL, NULL);

    g_assert(llc);
    g_assert_cmpint(llc->state, == ,NFC_LLC_STATE_PEER_LOST);

    g_assert(!nfc_llc_add_state_changed_handler(llc, NULL, NULL));
    g_assert(!nfc_llc_add_idle_changed_handler(llc, NULL, NULL));
    g_assert(!nfc_llc_add_wks_changed_handler(llc, NULL, NULL));
    nfc_llc_submit_i_pdu(llc, NULL, NULL, 0); /* NULL PDU is ignored */
    nfc_llc_submit_cc_pdu(llc, NULL); /* NULL connection is ignored */
    nfc_llc_remove_handler(llc, 0); /* Zero id is ignored */
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
    nfc_target_unref(target);
}

/*==========================================================================*
 * initiator
 *==========================================================================*/

static
void
test_initiator(
    void)
{
    static const TestTx tx[] = {
        {
            { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
            { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
        }
    };
    NfcInitiator* init = test_initiator_new_with_tx(TEST_ARRAY_AND_COUNT(tx));
    NfcLlcIo* io = nfc_llc_io_target_new(init);
    NfcLlc* llc = nfc_llc_new(io, NULL, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id;

    g_assert(llc->state == NFC_LLC_STATE_START);
    id = nfc_llc_add_state_changed_handler(llc, test_llc_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    g_assert(llc->state == NFC_LLC_STATE_ACTIVE);
    nfc_llc_remove_handler(llc, id);

    g_main_loop_unref(loop);
    nfc_initiator_unref(init);
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
}

/*==========================================================================*
 * advanced
 *==========================================================================*/

typedef struct test_advanced_data {
    const char* name;
    const TestTx* tx;
    guint ntx;
    gboolean allow_connections;
    gboolean accept_connections;
    gboolean cancel_connections;
    TestConnectionHookFunc connection_state_hook;
} TestAdvancedData;

static
void
test_advanced_disconnect_when_active(
    TestConnection* test,
    void* user_data)
{
    NfcPeerConnection* conn = &test->connection;

    switch (conn->state) {
    case NFC_LLC_CO_ACTIVE:
        GDEBUG("Initiating local disconnect");
        nfc_peer_connection_disconnect(conn);
        break;
    case NFC_LLC_CO_DISCONNECTING:
    case NFC_LLC_CO_DEAD:
        nfc_peer_connection_disconnect(conn); /* This has no effect */
        nfc_peer_connection_apply_remote_params(conn, NULL); /* This too */
        break;
    default:
        break;
    }
}

static
void
test_advanced(
    gconstpointer test_data)
{
    const TestAdvancedData* test = test_data;
    NfcPeerService* snep = NFC_PEER_SERVICE
        (test_service_new(NFC_LLC_NAME_SNEP));
    TestService* test_service = test_service_new("foo");
    NfcTarget* target = test_target_new_with_tx(test->tx, test->ntx);
    NfcPeerService* service = NFC_PEER_SERVICE(test_service);
    NfcLlcParam** params = nfc_llc_param_decode(&llc_param_tlv);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    gulong id;

    test_service->allow_connections = test->allow_connections;
    test_service->accept_connections = test->accept_connections;
    test_service->cancel_connections = test->cancel_connections;
    test_service->connection_state_change_hook.user_data = loop;
    test_service->connection_state_change_hook.proc =
        test->connection_state_hook;

    g_assert(nfc_peer_services_add(services, service));
    g_assert(nfc_peer_services_add(services, snep));
    g_assert_cmpuint(service->sap, == ,NFC_LLC_SAP_NAMED);
    g_assert_cmpuint(snep->sap, == ,NFC_LLC_SAP_SNEP);

    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc);
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* Wait for the conversation to start */
    id = nfc_llc_add_state_changed_handler(llc, test_llc_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    if (llc->state == NFC_LLC_STATE_ACTIVE) {
        /* Now wait until transfer error terminates the loop */
        test_run(&test_opt, loop);
        g_assert(llc->state == NFC_LLC_STATE_PEER_LOST);
    }
    nfc_llc_remove_handler(llc, id);
    g_main_loop_unref(loop);

    /* All data must have been sent */
    g_assert_cmpuint(test_target_tx_remaining(target), == ,0);

    nfc_llc_free(llc);
    nfc_llc_io_unref(io);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_service_unref(snep);
    nfc_peer_services_unref(services);
    nfc_target_unref(target);
}

static const guint8 connect_foo_name_data[] = {
    0x05, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f, 0x06, 0x03, 0x66, 0x6f, 0x6f
};
static const guint8 connect_foo_sap_data[] = {
    0x41, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f, 0x06
};
static const guint8 cc_foo_data[] = {
    0x81, 0x90, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f
};
static const guint8 dm_reject_foo_data[] = { 0x81, 0xd0, 0x03 };
static const guint8 remote_disc_foo_data[] = { 0x41, 0x60 };
static const guint8 local_disc_foo_data[] = { 0x81, 0x50 };
static const guint8 remote_dm_disc_data[] = { 0x41, 0xe0, 0x00 };
static const guint8 local_dm_disc_data[] = { 0x81, 0xd0, 0x00 };
static const guint8 frmr_disc_data[] = { 0x82, 0x10, 0x45, 0x00, 0x00, 0x00 };
static const guint8 frmr_dm_data[] = { 0x82, 0x10, 0x47, 0x00, 0x00, 0x00 };
static const guint8 frmr_cc_data[] = { 0x42, 0x00, 0x46, 0x00, 0x00, 0x00 };
static const guint8 snl_empty_data[] = { 0x06, 0x41 };
static const guint8 snl_foo_bar_sdreq_data[] = {
    0x06, 0x41,
    0x08, 0x04, 0x01, 0x66, 0x6f, 0x6f, /* foo */
    0x08, 0x04, 0x02, 0x62, 0x61, 0x72, /* bar */
    0x08, 0x0f, 0x03, 0x75, 0x72, 0x6e, 0x3a, 0x6e,
    0x66, 0x63, 0x3a, 0x73, 0x6e, 0x3a, 0x73, 0x64,
    0x70, /* urn:nfc:sn:sdp */
    /* This one doesn't make sense and will be ignored: */
    0x01, 0x01, 0x11
 };
static const guint8 snl_foo_bar_sdres_data[] = {
    0x06, 0x41, 0x09, 0x02, 0x01, 0x10, 0x09, 0x02,
    0x02, 0x00, 0x09, 0x02, 0x03, 0x01
};
static const guint8 snl_malformed_dsap_data[] = {
    0x82, 0x41, 0x09, 0x02, 0x01, 0x10
};
static const guint8 frmr_snl_malformed_dsap_data[] = {
    0x06, 0x20, 0x49, 0x00, 0x00, 0x00
 };
static const guint8 snl_malformed_ssap_data[] = {
    0x06, 0x60, 0x09, 0x02, 0x01, 0x10
};
static const guint8 frmr_snl_malformed_ssap_data[] = {
    0x82, 0x01, 0x49, 0x00, 0x00, 0x00
};
static const guint8 pax_data[] = {
    0x00, 0x40, 0x01, 0x01, 0x11, 0x02, 0x02, 0x07,
    0xff, 0x03, 0x02, 0x00, 0x13, 0x04, 0x01, 0x00
};
static const guint8 agf_pax_data[] = {
    0x00, 0x80,
    /* Encapsulated PAX PDU */
    0x00, 0x10,
    0x00, 0x40, 0x01, 0x01, 0x11, 0x02, 0x02, 0x07,
    0xff, 0x03, 0x02, 0x00, 0x13, 0x04, 0x01, 0x00,
    /* Empty PDU (ignored) */
    0x00, 0x00
};
static const guint8 pax_malformed_dsap_data[] = { 0x04, 0x40 };
static const guint8 pax_malformed_ssap_data[] = { 0x00, 0x41 };
static const guint8 frmr_pax_malformed_dsap_data[] = {
    0x02, 0x01, 0x41, 0x00, 0x00, 0x00
};
static const guint8 frmr_pax_malformed_ssap_data[] = {
    0x06, 0x00, 0x41, 0x00, 0x00, 0x00
};
static const guint8 ui_valid_data[] = { 0x40, 0xc1, 0x01, 0x02, 0x03 };
static const guint8 ui_invalid_data[] = { 0x80, 0xc1, 0x01, 0x02, 0x03 };
static const guint8 frmr_ui_invalid_data[] = {
    0x06, 0x20, 0x43, 0x00, 0x00, 0x00
};
static const guint8 frmr_rr_32_32_i_data[] = {
    0x82, 0x20, 0x4d, 0x00, 0x00, 0x00
};
static const guint8 frmr_rnr_32_32_i_data[] = {
    0x82, 0x20, 0x4e, 0x00, 0x00, 0x00
};
static const guint8 frmr_i_32_16_i_data[] = {
    0x42, 0x20, 0x4c, 0x00, 0x00, 0x00
};
static const guint8 rr_32_16_0_pdu_data[] = { 0x43, 0x60, 0x00 };
static const guint8 rr_32_32_0_pdu_data[] = { 0x83, 0x60, 0x00 };
static const guint8 rr_16_32_1_pdu_data[] = { 0x83, 0x50, 0x01 };
static const guint8 rnr_32_16_0_pdu_data[] = { 0x43, 0xa0, 0x00 };
static const guint8 rnr_32_32_0_pdu_data[] = { 0x83, 0xa0, 0x00 };
static const guint8 i_0_0_1_pdu_data[] = { 0x43, 0x20, 0x00, 0x01 };
static const guint8 connect_sap_32_17_data[] = {
    0x45, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f
};
static const guint8 connect_sap_32_3_data[] = {
    0x0d, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f
};
static const guint8 dm_noservice_17_32_data[] = { 0x81, 0xd1, 0x02 };
static const guint8 dm_noservice_3_32_data[] = { 0x81, 0xc3, 0x02 };

static const TestTx advanced_pkt_1 [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx advanced_pkt_2 [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx advanced_pkt_3 [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_urn_nfc_sn_handover_data) }
    }
};
static const TestTx advanced_pkt_4 [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_sdp_empty_data) }
    }
};
static const TestTx advanced_pkt_5 [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_2_data) }
    }
};
static const TestTx advanced_empty_snl [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(snl_empty_data) }
    },{
        { TEST_ARRAY_AND_SIZE(snl_empty_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_snl [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(snl_foo_bar_sdreq_data) }
    },{
        { TEST_ARRAY_AND_SIZE(snl_foo_bar_sdres_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_accept_name_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_name_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(rr_32_16_0_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(i_0_0_1_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(rr_16_32_1_pdu_data) },
        { TEST_ARRAY_AND_SIZE(rr_32_32_0_pdu_data) }  /* Invalid RR */
    },{
        { TEST_ARRAY_AND_SIZE(frmr_rr_32_32_i_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_accept_sap_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(rnr_32_16_0_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(i_0_0_1_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(rr_16_32_1_pdu_data) },
        { TEST_ARRAY_AND_SIZE(rnr_32_32_0_pdu_data) } /* Invalid RNR */
    },{
        { TEST_ARRAY_AND_SIZE(frmr_rnr_32_32_i_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_accept_remote_disc_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(remote_disc_foo_data) }
    }
};
static const TestTx advanced_accept_remote_frmr_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(frmr_i_32_16_i_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx advanced_accept_remote_double_disc_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(remote_disc_foo_data) }
    },{
        { TEST_ARRAY_AND_SIZE(local_dm_disc_data) },
        { TEST_ARRAY_AND_SIZE(remote_disc_foo_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_disc_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_accept_remote_disc_invalid_dm_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(remote_disc_foo_data) }
    },{
        { TEST_ARRAY_AND_SIZE(local_dm_disc_data) },
        { TEST_ARRAY_AND_SIZE(remote_dm_disc_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_dm_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_accept_local_disc_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(local_disc_foo_data) },
        { TEST_ARRAY_AND_SIZE(remote_dm_disc_data) }
    }
};
static const TestTx advanced_connect_duplicate_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(dm_reject_foo_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_connect_reject_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_name_data) }
    },{
        { TEST_ARRAY_AND_SIZE(dm_reject_foo_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_connect_reject_sap_17_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_sap_32_17_data) }
    },{
        { TEST_ARRAY_AND_SIZE(dm_noservice_17_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_connect_reject_sap_3_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_sap_32_3_data) }
    },{
        { TEST_ARRAY_AND_SIZE(dm_noservice_3_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_unexpected_cc_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(cc_foo_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_cc_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_snl_malformed_dsap_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(snl_malformed_dsap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_snl_malformed_dsap_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_snl_malformed_ssap_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(snl_malformed_ssap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_snl_malformed_ssap_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_pax_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(pax_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx advanced_pax_malformed_dsap_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(pax_malformed_dsap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_pax_malformed_dsap_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_pax_malformed_ssap_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(pax_malformed_ssap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_pax_malformed_ssap_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx advanced_agf_pax_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(agf_pax_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx advanced_ui_valid_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(ui_valid_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx advanced_ui_invalid_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(ui_invalid_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_ui_invalid_data) },
        { NULL, 0 }
    }
};

static const TestAdvancedData advanced_tests [] = {
    {
        "abort/1",
        TEST_ARRAY_AND_COUNT(advanced_pkt_1)
    },{
        "abort/2",
        TEST_ARRAY_AND_COUNT(advanced_pkt_2)
    },{
        "abort/3",
        TEST_ARRAY_AND_COUNT(advanced_pkt_3)
    },{
        "abort/4",
        TEST_ARRAY_AND_COUNT(advanced_pkt_4)
    },{
        "abort/5",
        TEST_ARRAY_AND_COUNT(advanced_pkt_5)
    },{
        "empty_snl",
        TEST_ARRAY_AND_COUNT(advanced_empty_snl)
    },{
        "snl",
        TEST_ARRAY_AND_COUNT(advanced_snl)
    },{
        "accept_name",
        TEST_ARRAY_AND_COUNT(advanced_accept_name_pkt),
        TRUE, TRUE
    },{
        "accept_sap",
        TEST_ARRAY_AND_COUNT(advanced_accept_sap_pkt),
        TRUE, TRUE
    },{
        "accept_remote_disc",
        TEST_ARRAY_AND_COUNT(advanced_accept_remote_disc_pkt),
        TRUE, TRUE
    },{
        "accept_remote_frmr",
        TEST_ARRAY_AND_COUNT(advanced_accept_remote_frmr_pkt),
        TRUE, TRUE
    },{
        "accept_remote_double_disc",
        TEST_ARRAY_AND_COUNT(advanced_accept_remote_double_disc_pkt),
        TRUE, TRUE
    },{
        "accept_remote_disc_invalid_dm",
        TEST_ARRAY_AND_COUNT(advanced_accept_remote_disc_invalid_dm_pkt),
        TRUE, TRUE
    },{
        "accept_local_disc",
        TEST_ARRAY_AND_COUNT(advanced_accept_local_disc_pkt),
        TRUE, TRUE, FALSE,
        test_advanced_disconnect_when_active
    },{
        "duplicate",
        TEST_ARRAY_AND_COUNT(advanced_connect_duplicate_pkt),
        TRUE, TRUE
    },{
        "cancel",
        TEST_ARRAY_AND_COUNT(advanced_connect_reject_pkt),
        TRUE, TRUE, TRUE
    },{
        "reject1",
        TEST_ARRAY_AND_COUNT(advanced_connect_reject_pkt),
        TRUE, FALSE
    },{
        "reject2",
        TEST_ARRAY_AND_COUNT(advanced_connect_reject_pkt),
    },{
        "reject_sap1",
        TEST_ARRAY_AND_COUNT(advanced_connect_reject_sap_3_pkt),
    },{
        "reject_sap2",
        TEST_ARRAY_AND_COUNT(advanced_connect_reject_sap_17_pkt),
    },{
        "unexpected_cc",
        TEST_ARRAY_AND_COUNT(advanced_unexpected_cc_pkt),
    },{
        "snl_malformed_dsap",
        TEST_ARRAY_AND_COUNT(advanced_snl_malformed_dsap_pkt),
    },{
        "snl_malformed_ssap",
        TEST_ARRAY_AND_COUNT(advanced_snl_malformed_ssap_pkt),
    },{
        "pax",
        TEST_ARRAY_AND_COUNT(advanced_pax_pkt),
    },{
        "pax_malformed_dsap",
        TEST_ARRAY_AND_COUNT(advanced_pax_malformed_dsap_pkt),
    },{
        "pax_malformed_ssap",
        TEST_ARRAY_AND_COUNT(advanced_pax_malformed_ssap_pkt),
    },{
        "agf_pax",
        TEST_ARRAY_AND_COUNT(advanced_agf_pax_pkt),
    },{
        "ui_valid",
        TEST_ARRAY_AND_COUNT(advanced_ui_valid_pkt),
    },{
        "ui_invalid",
        TEST_ARRAY_AND_COUNT(advanced_ui_invalid_pkt),
    }
};

/*==========================================================================*
 * connect
 *==========================================================================*/

typedef struct test_connect_run TestConnectRun;
typedef void (*TestConnectFunc)(TestConnectRun* run);
typedef struct test_connect_data {
    const char* name;
    const TestTx* tx;
    guint ntx;
    TestConnectFunc connect_proc;
    NfcLlcConnectFunc connect_complete;
    NFC_PEER_CONNECT_RESULT connect_result;
    gboolean exit_when_connected;
    NFC_LLC_STATE exit_state;
    GUtilData data_received;
} TestConnectData;

struct test_connect_run {
    const TestConnectData* test;
    NfcLlc* llc;
    NfcPeerService* service;
    GMainLoop* loop;
    gboolean connect_complete;
    gboolean connect_done;
};

static
void
test_connect_done(
    gpointer user_data)
{
    TestConnectRun* run = user_data;

    g_assert(!run->connect_done);
    run->connect_done = TRUE;
}

static
void
test_connect_disconnected(
    TestConnection* conn,
    void* user_data)
{
    TestConnectRun* run = user_data;
    const TestConnectData* test = run->test;

    if (test->data_received.bytes) {
        const GUtilData* expect = &test->data_received;

        g_assert_cmpuint(expect->size, == ,conn->received->len);
        g_assert(!memcmp(expect->bytes, conn->received->data, expect->size));
    }
}

static
void
test_connect_complete(
    NfcPeerConnection* conn,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data)
{
    TestConnectRun* run = user_data;
    const TestConnectData* test = run->test;

    GDEBUG("Connection status %d", result);
    g_assert_cmpint(test->connect_result, == ,result);
    g_assert(!run->connect_complete);
    run->connect_complete = TRUE;
    if (test->exit_when_connected) {
        g_main_loop_quit(run->loop);
    }
}

static
void
test_connect_cancel(
    TestConnectRun* run)
{
    NfcPeerConnection* connection = nfc_llc_connect_sn(run->llc,
        run->service, NFC_LLC_NAME_SNEP, run->test->connect_complete,
        test_connect_done, run);

    g_assert(connection);
    nfc_peer_connection_disconnect(connection);
}

static
void
test_connect_snep_sn(
    TestConnectRun* run)
{
    g_assert(nfc_llc_connect_sn(run->llc, run->service, NFC_LLC_NAME_SNEP,
        run->test->connect_complete, test_connect_done, run));
}

static
void
test_connect_snep_sap(
    TestConnectRun* run)
{
    g_assert(nfc_llc_connect(run->llc, run->service, NFC_LLC_SAP_SNEP,
        run->test->connect_complete, test_connect_done, run));
}

static
void
test_connect(
    gconstpointer test_data)
{
    const TestConnectData* test = test_data;
    TestConnectRun run;
    TestService* test_service = test_service_new(NULL);
    NfcTarget* target = test_target_new_with_tx(test->tx, test->ntx);
    NfcLlcParam** params = nfc_llc_param_decode(&llc_param_tlv);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    gulong id;

    memset(&run, 0, sizeof(run));
    run.test = test;
    run.loop = g_main_loop_new(NULL, TRUE);
    run.service = NFC_PEER_SERVICE(test_service);

    test_service->connection_finalize_hook.proc = test_connect_disconnected;
    test_service->connection_finalize_hook.user_data = &run;
    g_assert(nfc_peer_services_add(services, run.service));
    g_assert_cmpuint(run.service->sap, == ,NFC_LLC_SAP_UNNAMED);

    run.llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(run.llc);
    g_assert_cmpint(run.llc->state, == ,NFC_LLC_STATE_START);

    /* Initiate the connection */
    if (test->connect_proc) {
        test->connect_proc(&run);
    }

    /* Wait for the conversation to start */
    id = nfc_llc_add_state_changed_handler(run.llc, test_llc_quit_loop_cb,
        run.loop);
    test_run(&test_opt, run.loop);
    if (run.llc->state == NFC_LLC_STATE_ACTIVE) {
        /* Now wait until transfer error or something else breaks the loop */
        test_run(&test_opt, run.loop);
    }
    g_assert_cmpint(run.llc->state, == ,test->exit_state);
    g_assert(run.connect_complete == (test->connect_complete != NULL));
    g_assert(run.connect_done);
    nfc_llc_remove_handler(run.llc, id);
    g_main_loop_unref(run.loop);

    /* All data must have been sent */
    g_assert_cmpuint(test_target_tx_remaining(target), == ,0);

    nfc_llc_free(run.llc);
    nfc_llc_io_unref(io);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(run.service);
    nfc_peer_services_unref(services);
    nfc_target_unref(target);
}

static const guint8 connect_snep_name_data[] = {
    0x05, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f, 0x06, 0x0f, 0x75, 0x72, 0x6e, 0x3a, 0x6e,
    0x66, 0x63, 0x3a, 0x73, 0x6e, 0x3a, 0x73, 0x6e,
    0x65, 0x70
};
static const guint8 connect_snep_sap_data[] = {
    0x11, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
    0x0f
};
static const guint8 cc_snep_data[] = {
    0x81, 0x84, 0x02, 0x02, 0x07, 0xff, 0x04, 0x01,
    0xff, 0x05, 0x01, 0x0f 
};
static const guint8 disc_4_32_pdu_data[] = { 0x11, 0x60 };
static const guint8 dm_32_4_pdu_data[] = { 0x81, 0xc4, 0x00 };
static const guint8 connect_snep_name_ok_transfer_expected_data[] = { 0x01 };
static const guint8 i_32_4_1_pdu_data[] = { 0x83, 0x04, 0x00, 0x01 };
static const guint8 i_33_4_1_pdu_data[] = { 0x87, 0x04, 0x00, 0x02 };
static const guint8 rr_4_32_0_pdu_data[] = { 0x13, 0x60, 0x01 };
static const guint8 frmr_connect_data[] = {
    0x82, 0x00, 0x84, 0x00, 0x00, 0x00
};
static const guint8 frmr_invalid_reject_data[] = {
    0x02, 0x10, 0x47, 0x00, 0x00, 0x00
};
static const guint8 frmr_i_4_32_s_data[] = {
    0x12, 0x20, 0x1c, 0x00, 0x01, 0x01
};
static const guint8 frmr_i_4_33_i_data[] = {
    0x12, 0x21, 0x4c, 0x00, 0x00, 0x00
};
static const guint8 dm_snep_noservice_data[] = { 0x81, 0xc0, 0x02 };
static const guint8 dm_snep_reject_data[] = { 0x81, 0xc0, 0x03 };
static const guint8 dm_snep_invalid_reject_data[] = { 0x41, 0xc0, 0x03 };
static const guint8 cc_invalid_sap_data[] = { 0x41, 0x90 };
static const TestTx connect_snep_name_ok_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_name_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx connect_snep_name_ok_cancel_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_name_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(disc_4_32_pdu_data) },
        { TEST_ARRAY_AND_SIZE(dm_32_4_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx connect_snep_name_ok_transfer_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_name_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(i_32_4_1_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(rr_4_32_0_pdu_data) },
        { TEST_ARRAY_AND_SIZE(i_33_4_1_pdu_data) } /* Invalid SAP */
    },{
        { TEST_ARRAY_AND_SIZE(frmr_i_4_33_i_data) },
        { TEST_ARRAY_AND_SIZE(i_32_4_1_pdu_data) } /* Invalid N(S) */
    },{
        { TEST_ARRAY_AND_SIZE(frmr_i_4_32_s_data) },
        { NULL, 0 }
    }
};
static const TestTx connect_snep_sap_ok_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx connect_snep_name_noservice_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_name_data) },
        { TEST_ARRAY_AND_SIZE(dm_snep_noservice_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx connect_snep_name_reject_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_name_data) },
        { TEST_ARRAY_AND_SIZE(dm_snep_invalid_reject_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_invalid_reject_data) },
        { TEST_ARRAY_AND_SIZE(dm_snep_reject_data) }  /* The actual reject */
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx connect_snep_name_reject_frmr_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_name_data) },
        { TEST_ARRAY_AND_SIZE(frmr_connect_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx connect_invalid_cc_ok_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_name_data) },
        { TEST_ARRAY_AND_SIZE(cc_invalid_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(frmr_cc_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};

static const TestConnectData connect_tests [] = {
    {
        "snep_name_ok/1",
        TEST_ARRAY_AND_COUNT(connect_snep_name_ok_pkt),
        test_connect_snep_sn, NULL, NFC_PEER_CONNECT_OK, FALSE,
        NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_ok/2",
        TEST_ARRAY_AND_COUNT(connect_snep_name_ok_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_OK, FALSE, NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_ok/3",
        TEST_ARRAY_AND_COUNT(connect_snep_name_ok_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_OK, TRUE, NFC_LLC_STATE_ACTIVE
    },{
        "snep_name_ok_cancel",
        TEST_ARRAY_AND_COUNT(connect_snep_name_ok_cancel_pkt),
        test_connect_cancel, NULL, NFC_PEER_CONNECT_OK, FALSE,
        NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_ok_transfer",
        TEST_ARRAY_AND_COUNT(connect_snep_name_ok_transfer_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_OK, FALSE, NFC_LLC_STATE_PEER_LOST,
        { TEST_ARRAY_AND_SIZE(connect_snep_name_ok_transfer_expected_data) }
    },{
        "snep_sap_ok/1",
        TEST_ARRAY_AND_COUNT(connect_snep_sap_ok_pkt),
        test_connect_snep_sap, NULL, NFC_PEER_CONNECT_OK, FALSE,
        NFC_LLC_STATE_PEER_LOST
    },{
        "snep_sap_ok/2",
        TEST_ARRAY_AND_COUNT(connect_snep_sap_ok_pkt),
        test_connect_snep_sap, test_connect_complete,
        NFC_PEER_CONNECT_OK, FALSE, NFC_LLC_STATE_PEER_LOST
    },{
        "snep_sap_ok/3",
        TEST_ARRAY_AND_COUNT(connect_snep_sap_ok_pkt),
        test_connect_snep_sap, test_connect_complete,
        NFC_PEER_CONNECT_OK, TRUE, NFC_LLC_STATE_ACTIVE
    },{
        "snep_name_noservice/1",
        TEST_ARRAY_AND_COUNT(connect_snep_name_noservice_pkt),
        test_connect_snep_sn, NULL, NFC_PEER_CONNECT_NO_SERVICE, FALSE,
        NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_noservice/2",
        TEST_ARRAY_AND_COUNT(connect_snep_name_noservice_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_NO_SERVICE, FALSE, NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_noservice/3",
        TEST_ARRAY_AND_COUNT(connect_snep_name_noservice_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_NO_SERVICE, TRUE, NFC_LLC_STATE_ACTIVE
    },{
        "snep_name_reject/1",
        TEST_ARRAY_AND_COUNT(connect_snep_name_reject_pkt),
        test_connect_snep_sn, NULL, NFC_PEER_CONNECT_REJECTED, FALSE,
        NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_reject/2",
        TEST_ARRAY_AND_COUNT(connect_snep_name_reject_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_REJECTED, FALSE, NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_reject/3",
        TEST_ARRAY_AND_COUNT(connect_snep_name_reject_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_REJECTED, TRUE, NFC_LLC_STATE_ACTIVE
    },{
        "snep_name_reject_frmr/1",
        TEST_ARRAY_AND_COUNT(connect_snep_name_reject_frmr_pkt),
        test_connect_snep_sn, NULL, NFC_PEER_CONNECT_REJECTED, FALSE,
        NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_reject_frmr/2",
        TEST_ARRAY_AND_COUNT(connect_snep_name_reject_frmr_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_REJECTED, FALSE, NFC_LLC_STATE_PEER_LOST
    },{
        "snep_name_reject_frmr/3",
        TEST_ARRAY_AND_COUNT(connect_snep_name_reject_frmr_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_REJECTED, TRUE, NFC_LLC_STATE_ACTIVE
    },{
        "invalid_cc_ok/1",
        TEST_ARRAY_AND_COUNT(connect_invalid_cc_ok_pkt),
        test_connect_snep_sn, NULL, NFC_PEER_CONNECT_OK, FALSE,
        NFC_LLC_STATE_PEER_LOST
    },{
        "invalid_cc_ok/2",
        TEST_ARRAY_AND_COUNT(connect_invalid_cc_ok_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_OK, FALSE, NFC_LLC_STATE_PEER_LOST
    },{
        "invalid_cc_ok/3",
        TEST_ARRAY_AND_COUNT(connect_invalid_cc_ok_pkt),
        test_connect_snep_sn, test_connect_complete,
        NFC_PEER_CONNECT_OK, TRUE, NFC_LLC_STATE_ACTIVE
    }
};

/*==========================================================================*
 * send
 *==========================================================================*/

typedef enum test_send_flags {
    TEST_SEND_NO_FLAGS = 0,
    TEST_SEND_LATER = 0x01
} TEST_SEND_FLAGS;

typedef struct test_send_config {
    const char* name;
    const GUtilData* send;
    guint nsend;
    const TestTx* tx;
    guint ntx;
    void (*after_send_fn)(NfcPeerConnection* self);
    TEST_SEND_FLAGS flags;
    gsize bytes_sent;
    NFC_LLC_CO_STATE exit_conn_state;
    NFC_LLC_STATE exit_llc_state;
} TestSendConfig;

typedef struct test_send_data {
    const TestSendConfig* config;
    GMainLoop* loop;
    NfcPeerConnection* conn;
    gulong quit_id;
} TestSendData;

static
void
test_send_connected_abort(
    NfcPeerConnection* conn)
{
    /*
     * nfc_peer_connection_cancel returns FALSE because the connection
     * has already been established but it still srops all unsent data
     * and moves the connection to DISCONNECTING state.
     */
    g_assert(!nfc_peer_connection_cancel(conn));
}

static
void
test_send_now(
    TestSendData* test)
{
    const TestSendConfig* config = test->config;
    NfcPeerConnection* conn = test->conn;
    guint i;

    /* Send the data */
    for (i = 0; i < config->nsend; i++) {
        const GUtilData* data = config->send + i;
        GBytes* bytes = g_bytes_new_static(data->bytes, data->size);

        g_assert(nfc_peer_connection_send(conn, bytes));
        g_bytes_unref(bytes);
    }
    if (config->after_send_fn) {
        config->after_send_fn(conn);
    }
}

static
gboolean
test_send_later(
    void* user_data)
{
    test_send_now((TestSendData*)user_data);
    return G_SOURCE_REMOVE;
}

static
void
test_send_connected(
    NfcPeerConnection* conn,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data)
{
    TestSendData* test = user_data;
    const TestSendConfig* config = test->config;

    GDEBUG("Connection status %d", result);
    g_assert_cmpint(result, == ,NFC_PEER_CONNECT_OK);
    g_assert(!test->conn);
    g_assert(nfc_peer_connection_rmiu(conn) == 128);
    test->conn = nfc_peer_connection_ref(conn);
    test->quit_id = nfc_peer_connection_add_state_changed_handler(conn,
        test_llc_quit_when_dead_cb, test->loop);
    if (config->flags & TEST_SEND_LATER) {
        g_idle_add(test_send_later, test);
    } else {
        test_send_now(test);
    }
}

static
void
test_send(
    gconstpointer test_data)
{
    const TestSendConfig* config = test_data;
    TestService* test_service = test_service_new(NULL);
    TestSendData test;
    NfcTarget* target = test_target_new_with_tx(config->tx, config->ntx);
    NfcLlcParam** params = nfc_llc_param_decode(&llc_param_tlv);
    NfcPeerService* service = NFC_PEER_SERVICE(test_service);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    gulong id;

    memset(&test, 0, sizeof(test));
    test.config = config;
    test.loop = g_main_loop_new(NULL, TRUE);

    g_assert(nfc_peer_services_add(services, service));
    g_assert_cmpuint(service->sap, == ,NFC_LLC_SAP_UNNAMED);

    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc);
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* Initiate the connection */
    g_assert(nfc_llc_connect(llc, service, NFC_LLC_SAP_SNEP,
        test_send_connected, NULL, &test));

    /* Wait for the conversation to start */
    id = nfc_llc_add_state_changed_handler(llc, test_llc_quit_loop_cb,
        test.loop);
    test_run(&test_opt, test.loop);
    if (llc->state == NFC_LLC_STATE_ACTIVE) {
        /* Now wait until transfer error or something else breaks the loop */
        test_run(&test_opt, test.loop);
    }
    g_assert(llc->state == config->exit_llc_state);
    nfc_llc_remove_handler(llc, id);

    /* Must have connection */
    g_assert(test.conn);
    g_assert_cmpuint(test.conn->bytes_queued, ==, 0);
    g_assert_cmpuint(test.conn->bytes_received, == , 0);
    g_assert_cmpuint(test.conn->bytes_sent, == ,config->bytes_sent);
    g_assert(test.conn->state == config->exit_conn_state);
    g_assert(nfc_peer_connection_send(test.conn, NULL) ==
        (test.conn->state <= NFC_LLC_CO_ACTIVE));
    nfc_peer_connection_remove_handler(test.conn, test.quit_id);
    nfc_peer_connection_unref(test.conn);
    g_main_loop_unref(test.loop);

    /* All data must have been sent */
    g_assert_cmpuint(test_target_tx_remaining(target), == ,0);

    nfc_llc_free(llc);
    nfc_llc_io_unref(io);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_target_unref(target);
}

static const guint8 send_cc_snep_data[] = {
    0x81, 0x84, 0x02, 0x02, 0x00, 0x00, 0x04, 0x01,
    0xff, 0x05, 0x01, 0x02, 
};
static const guint8 send_frame_264[] = {
    0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x23, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x33, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x41, 0x42, 0x43, 0x43, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x53, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x63, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x73, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x83, 0x83, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x93, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa3, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb3, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc3, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd3, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe3, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf3, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
    0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07
};
static const GUtilData send_small_frame_send_data [] = {
    { send_frame_264, 1 }
};
static const GUtilData send_small_frames_send_data [] = {
    { send_frame_264, 1 },
    { send_frame_264 + 1, 1 },
    { send_frame_264 + 2, 1 },
    { send_frame_264 + 3, 1 },
    { send_frame_264 + 4, 1 },
    { send_frame_264 + 5, 1 },
    { send_frame_264 + 6, 1 },
    { send_frame_264 + 7, 1 },
};
static const GUtilData send_large_frame_send_data [] = {
    { send_frame_264, 128 }
};
static const GUtilData send_large_frames_send_data [] = {
    { send_frame_264, 200 },
    { send_frame_264 + 200, 64 }
};
static const GUtilData send_extra_large_frame_send_data [] = {
    { send_frame_264, 129 }
};

static const guint8 send_small_frame_i[] = { 0x13, 0x20, 0x00, 0x00 };
static const guint8 send_small_frames_i[] = {
    0x13, 0x20, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
};
static const guint8 send_frame_rr_1[] = { 0x83, 0x44, 0x01 };
static const guint8 send_frame_rr_2[] = { 0x83, 0x44, 0x02 };
static const guint8 send_frame_rr_3[] = { 0x83, 0x44, 0x03 };
static const guint8 send_large_frame_i[] = {
    0x13, 0x20, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x23, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x33, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x41, 0x42, 0x43, 0x43, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x53, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x63, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x73, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f
};
static const guint8 send_large_frames_i_1[] = {
    0x13, 0x20, 0x10,
    0x80, 0x81, 0x82, 0x83, 0x83, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x93, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa3, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb3, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc3, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd3, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe3, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf3, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};
static const guint8 send_large_frames_i_2[] = {
    0x13, 0x20, 0x20,
    0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07
};
static const guint8 send_extra_large_frame_i_1[] = { 0x13, 0x20, 0x10, 0x80 };
static const TestTx send_no_frames_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(disc_4_32_pdu_data) },
        { TEST_ARRAY_AND_SIZE(dm_32_4_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx send_small_frame_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(send_small_frame_i) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_1) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx send_small_frame_abort_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{ /* I-Frame is still sent */
        { TEST_ARRAY_AND_SIZE(send_small_frame_i) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_1) }
    },{
        { TEST_ARRAY_AND_SIZE(disc_4_32_pdu_data) },
        { TEST_ARRAY_AND_SIZE(dm_32_4_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx send_small_frames_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(send_small_frames_i) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_1) }
    }
};
static const TestTx send_large_frame_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frame_i) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_1) }
    }
};
static const TestTx send_large_frames_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frame_i) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_1) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frames_i_1) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_2) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frames_i_2) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_3) }
    }
};
static const TestTx send_large_frames_abort_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frame_i) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_1) }
    },{ /* The remaining data are dropped */
        { TEST_ARRAY_AND_SIZE(disc_4_32_pdu_data) },
        { TEST_ARRAY_AND_SIZE(dm_32_4_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx send_large_frames_disconnect_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frame_i) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_1) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frames_i_1) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_2) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frames_i_2) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_3) }
    },{
        { TEST_ARRAY_AND_SIZE(disc_4_32_pdu_data) },
        { TEST_ARRAY_AND_SIZE(dm_32_4_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    }
};
static const TestTx send_extra_large_frame_pkt [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) }
    },{
        { TEST_ARRAY_AND_SIZE(connect_snep_sap_data) },
        { TEST_ARRAY_AND_SIZE(send_cc_snep_data) }
    },{
        { TEST_ARRAY_AND_SIZE(send_large_frame_i) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_1) }
    },{
        { TEST_ARRAY_AND_SIZE(send_extra_large_frame_i_1) },
        { TEST_ARRAY_AND_SIZE(send_frame_rr_2) }
    }
};
static const TestSendConfig send_tests [] = {
    {
        "no_frames",
        NULL, 0, TEST_ARRAY_AND_COUNT(send_no_frames_pkt),
        nfc_peer_connection_disconnect, TEST_SEND_LATER,
        0, NFC_LLC_CO_DEAD, NFC_LLC_STATE_ACTIVE
    },{
        "small_frame",
        TEST_ARRAY_AND_COUNT(send_small_frame_send_data),
        TEST_ARRAY_AND_COUNT(send_small_frame_pkt),
        NULL, TEST_SEND_LATER,
        1, NFC_LLC_CO_ACTIVE, NFC_LLC_STATE_PEER_LOST
    },{
        "small_frame_abort",
        TEST_ARRAY_AND_COUNT(send_small_frame_send_data),
        TEST_ARRAY_AND_COUNT(send_small_frame_abort_pkt),
        test_send_connected_abort, TEST_SEND_LATER,
        1, NFC_LLC_CO_DEAD, NFC_LLC_STATE_ACTIVE
    },{
        "small_frames",
        TEST_ARRAY_AND_COUNT(send_small_frames_send_data),
        TEST_ARRAY_AND_COUNT(send_small_frames_pkt),
        NULL, TEST_SEND_NO_FLAGS,
        8, NFC_LLC_CO_ACTIVE, NFC_LLC_STATE_PEER_LOST
    },{
        "large_frame",
        TEST_ARRAY_AND_COUNT(send_large_frame_send_data),
        TEST_ARRAY_AND_COUNT(send_large_frame_pkt),
        NULL, TEST_SEND_NO_FLAGS,
        128, NFC_LLC_CO_ACTIVE, NFC_LLC_STATE_PEER_LOST
    },{
        "large_frames",
        TEST_ARRAY_AND_COUNT(send_large_frames_send_data),
        TEST_ARRAY_AND_COUNT(send_large_frames_pkt),
        NULL, TEST_SEND_NO_FLAGS,
        264, NFC_LLC_CO_ACTIVE, NFC_LLC_STATE_PEER_LOST
    },{
        "large_frames_abort",
        TEST_ARRAY_AND_COUNT(send_large_frames_send_data),
        TEST_ARRAY_AND_COUNT(send_large_frames_abort_pkt),
        test_send_connected_abort, TEST_SEND_LATER,
        128, NFC_LLC_CO_DEAD, NFC_LLC_STATE_ACTIVE
    },{
        "large_frames_disconnect",
        TEST_ARRAY_AND_COUNT(send_large_frames_send_data),
        TEST_ARRAY_AND_COUNT(send_large_frames_disconnect_pkt),
        nfc_peer_connection_disconnect, TEST_SEND_LATER,
        264, NFC_LLC_CO_DEAD, NFC_LLC_STATE_ACTIVE
    },{
        "extra_large_frame",
        TEST_ARRAY_AND_COUNT(send_extra_large_frame_send_data),
        TEST_ARRAY_AND_COUNT(send_extra_large_frame_pkt),
        NULL, TEST_SEND_NO_FLAGS,
        129, NFC_LLC_CO_ACTIVE, NFC_LLC_STATE_PEER_LOST
    }
};

/*==========================================================================*
 * protocol_error
 *==========================================================================*/

typedef struct test_protocol_error_data {
    const char* name;
    const TestTx* tx;
    guint ntx;
} TestProtocolErrorData;

static
void
test_protocol_error(
    gconstpointer test_data)
{
    const TestProtocolErrorData* test = test_data;
    TestService* test_service = test_service_new("foo");
    NfcPeerService* service = NFC_PEER_SERVICE(test_service);
    NfcTarget* target = test_target_new_with_tx(test->tx, test->ntx);
    NfcLlcParam** params = nfc_llc_param_decode(&llc_param_tlv);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    gulong id;

    g_assert(nfc_peer_services_add(services, service));
    g_assert_cmpuint(service->sap, == ,NFC_LLC_SAP_NAMED);

    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc);
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* Wait for the conversation to start */
    id = nfc_llc_add_state_changed_handler(llc, test_llc_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    if (llc->state == NFC_LLC_STATE_ACTIVE) {
        /* Protocol error terminates the loop */
        test_run(&test_opt, loop);
    }
    g_assert(llc->state == NFC_LLC_STATE_ERROR);
    nfc_llc_remove_handler(llc, id);
    g_main_loop_unref(loop);

    /* All data must have been sent */
    g_assert_cmpuint(test_target_tx_remaining(target), == ,0);

    nfc_llc_free(llc);
    nfc_llc_io_unref(io);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_target_unref(target);
}

static const guint8 protocol_error_packet_too_short_data[] = { 0xaa };
static const guint8 protocol_error_unhandled_ptype_data[] = { 0x02, 0xc0 };
static const guint8 protocol_error_symm_too_long_data[] = { 0x00, 0x00, 0x00 };
static const guint8 protocol_error_symm_invalid_dsap_data[] = { 0x04, 0x00 };
static const guint8 protocol_error_symm_invalid_ssap_data[] = { 0x00, 0x01 };
static const guint8 protocol_error_disc_too_long_data[] = { 0x41, 0x60, 0x00 };
static const guint8 protocol_error_dm_too_short_data[] = { 0x41, 0xe0 };
static const guint8 protocol_error_frmr_too_short_data[] = { 0x82, 0x00 };
static const guint8 protocol_error_agf_invalid_dsap_data[] = { 0x04, 0x80 };
static const guint8 protocol_error_agf_invalid_ssap_data[] = { 0x00, 0x81 };
static const guint8 protocol_error_agf_broken_1_data[] = {
    0x00, 0x80, 0x00, 0x01 /* Out of bounds */
};
static const guint8 protocol_error_agf_broken_2_data[] = {
    0x00, 0x80, 0x00, 0x01, 0x00 /* Encapsulated packet of size 1 */
};
static const guint8 protocol_error_agf_broken_3_data[] = {
    0x00, 0x80, 0x00, 0x00, 0x00 /* Garbage at the end */
};
static const guint8 protocol_error_i_too_short_data[] = { 0x07, 0x01 };
static const guint8 protocol_error_rr_too_short_data[] = { 0x07, 0x41 };
static const guint8 protocol_error_rnr_too_short_data[] = { 0x07, 0x81 };

static const TestTx protocol_error_packet_too_short [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_packet_too_short_data) }
    }
};
static const TestTx protocol_error_unhandled_ptype [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_unhandled_ptype_data) }
    }
};
static const TestTx protocol_error_symm_too_long [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_symm_too_long_data) }
    }
};
static const TestTx protocol_error_symm_invalid_dsap [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_symm_invalid_dsap_data) }
    }
};
static const TestTx protocol_error_symm_invalid_ssap [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_symm_invalid_ssap_data) }
    }
};
static const TestTx protocol_error_disc_too_long [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_disc_too_long_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx protocol_error_dm_too_short [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(connect_foo_sap_data) }
    },{
        { TEST_ARRAY_AND_SIZE(cc_foo_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_dm_too_short_data) }
    },{
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { NULL, 0 }
    }
};
static const TestTx protocol_error_frmr_too_short [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_frmr_too_short_data) }
    }
};
static const TestTx protocol_error_agf_invalid_dsap [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_agf_invalid_dsap_data) }
    }
};
static const TestTx protocol_error_agf_invalid_ssap [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_agf_invalid_ssap_data) }
    }
};
static const TestTx protocol_error_agf_broken_1 [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_agf_broken_1_data) }
    }
};
static const TestTx protocol_error_agf_broken_2 [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_agf_broken_2_data) }
    }
};
static const TestTx protocol_error_agf_broken_3 [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_agf_broken_3_data) }
    }
};
static const TestTx protocol_error_i_too_short [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_i_too_short_data) }
    }
};
static const TestTx protocol_error_rr_too_short [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_rr_too_short_data) }
    }
};
static const TestTx protocol_error_rnr_too_short [] = {
    {
        { TEST_ARRAY_AND_SIZE(symm_pdu_data) },
        { TEST_ARRAY_AND_SIZE(protocol_error_rnr_too_short_data) }
    }
};

static const TestProtocolErrorData protocol_error_tests [] = {
    {
        "packet_too_short",
        TEST_ARRAY_AND_COUNT(protocol_error_packet_too_short)
    },{
        "unhandled_ptype",
        TEST_ARRAY_AND_COUNT(protocol_error_unhandled_ptype)
    },{
        "symm_too_long",
        TEST_ARRAY_AND_COUNT(protocol_error_symm_too_long)
    },{
        "symm_invalid_dsap",
        TEST_ARRAY_AND_COUNT(protocol_error_symm_invalid_dsap)
    },{
        "symm_invalid_ssap",
        TEST_ARRAY_AND_COUNT(protocol_error_symm_invalid_ssap)
    },{
        "disc_too_long",
        TEST_ARRAY_AND_COUNT(protocol_error_disc_too_long)
    },{
        "dm_too_short",
        TEST_ARRAY_AND_COUNT(protocol_error_dm_too_short)
    },{
        "frmr_too_short",
        TEST_ARRAY_AND_COUNT(protocol_error_frmr_too_short)
    },{
        "agf_invalid_dsap",
        TEST_ARRAY_AND_COUNT(protocol_error_agf_invalid_dsap)
    },{
        "agf_invalid_ssap",
        TEST_ARRAY_AND_COUNT(protocol_error_agf_invalid_ssap)
    },{
        "agf_broken/1",
        TEST_ARRAY_AND_COUNT(protocol_error_agf_broken_1)
    },{
        "agf_broken/2",
        TEST_ARRAY_AND_COUNT(protocol_error_agf_broken_2)
    },{
        "agf_broken/3",
        TEST_ARRAY_AND_COUNT(protocol_error_agf_broken_3)
    },{
        "i_too_short",
        TEST_ARRAY_AND_COUNT(protocol_error_i_too_short)
    },{
        "rr_too_short",
        TEST_ARRAY_AND_COUNT(protocol_error_rr_too_short)
    },{
        "rnr_too_short",
        TEST_ARRAY_AND_COUNT(protocol_error_rnr_too_short)
    }
};

/*==========================================================================*
 * Common
 *==========================================================================*/

int main(int argc, char* argv[])
{
    guint i;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("initiator"), test_initiator);
    for (i = 0; i < G_N_ELEMENTS(advanced_tests); i++) {
        const TestAdvancedData* test = advanced_tests + i;
        char* path = g_strconcat(TEST_("advanced/"), test->name, NULL);

        g_test_add_data_func(path, test, test_advanced);
        g_free(path);
    }
    for (i = 0; i < G_N_ELEMENTS(connect_tests); i++) {
        const TestConnectData* test = connect_tests + i;
        char* path = g_strconcat(TEST_("connect/"), test->name, NULL);

        g_test_add_data_func(path, test, test_connect);
        g_free(path);
    }
    for (i = 0; i < G_N_ELEMENTS(send_tests); i++) {
        const TestSendConfig* test = send_tests + i;
        char* path = g_strconcat(TEST_("send/"), test->name, NULL);

        g_test_add_data_func(path, test, test_send);
        g_free(path);
    }
    for (i = 0; i < G_N_ELEMENTS(protocol_error_tests); i++) {
        const TestProtocolErrorData* test = protocol_error_tests + i;
        char* path = g_strconcat(TEST_("protocol_error/"), test->name, NULL);

        g_test_add_data_func(path, test, test_protocol_error);
        g_free(path);
    }
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
