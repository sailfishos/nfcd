/*
 * Copyright (C) 2020-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2020-2021 Jolla Ltd.
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

#include "nfc_types_p.h"
#include "nfc_target_impl.h"
#include "nfc_peer_connection_impl.h"
#include "nfc_peer_services.h"
#include "nfc_peer_service_p.h"
#include "nfc_peer_service_impl.h"
#include "nfc_peer_socket.h"
#include "nfc_llc_param.h"
#include "nfc_llc_io.h"
#include "nfc_llc.h"
#include "nfc_ndef.h"

#include "test_common.h"

#include <gutil_log.h>
#include <gutil_misc.h>

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

static TestOpt test_opt;

#define TEST_(name) "/core/peer_socket/" name

#define TEST_SERVICE_NAME "test"

static const guint8 param_tlv_data[] = {
    0x01, 0x01, 0x11, 0x02, 0x02, 0x07, 0xff, 0x03,
    0x02, 0x00, 0x13, 0x04, 0x01, 0xff, 0x07, 0x01,
    0x03
};
static const GUtilData param_tlv = { TEST_ARRAY_AND_SIZE(param_tlv_data) };
static const guint8 symm_data[] = { 0x00, 0x00 };

static
void
test_int_inc(
    gpointer data)
{
    (*((int*)data))++;
}

static
void
test_connection_dead_quit_loop_cb(
    NfcPeerConnection* connection,
    void* user_data)
{
    if (connection->state == NFC_LLC_CO_DEAD) {
        GDEBUG("Done");
        g_main_loop_quit((GMainLoop*)user_data);
    }
}

/*==========================================================================*
 * Test service
 *==========================================================================*/

typedef
void
(*TestServiceAcceptFunc)(
    NfcPeerService* service,
    NfcPeerSocket* socket,
    void* user_data);

typedef NfcPeerServiceClass TestServiceClass;
typedef struct test_service {
    NfcPeerService service;
    TestServiceAcceptFunc accept_fn;
    void* accept_data;
} TestService;

G_DEFINE_TYPE(TestService, test_service, NFC_TYPE_PEER_SERVICE)
#define TEST_TYPE_SERVICE (test_service_get_type())
#define TEST_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_SERVICE, TestService))

static
NfcPeerConnection*
test_service_new_connect(
    NfcPeerService* self,
    guint8 rsap,
    const char* name)
{
    NfcPeerSocket* s = nfc_peer_socket_new_connect(self, rsap, name);

    return s ? NFC_PEER_CONNECTION(s) : NULL;
}

static
NfcPeerConnection*
test_service_new_accept(
    NfcPeerService* service,
    guint8 rsap)
{
    TestService* self = TEST_SERVICE(service);
    NfcPeerSocket* s = nfc_peer_socket_new_accept(service, rsap);

    if (s) {
        if (self->accept_fn) {
            self->accept_fn(service, s, self->accept_data);
        }
        return NFC_PEER_CONNECTION(s);
    } else {
        return NULL;
    }
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
    klass->new_connect = test_service_new_connect;
    klass->new_accept = test_service_new_accept;
}

static
NfcPeerService*
test_service_client_new(
    guint8 sap)
{
    TestService* self = g_object_new(TEST_TYPE_SERVICE, NULL);
    NfcPeerService* service = &self->service;

    nfc_peer_service_init_base(service, NULL);
    service->sap = sap;
    return service;
}

static
NfcPeerService*
test_service_server_new(
    const char* name,
    guint8 sap,
    TestServiceAcceptFunc accept_fn,
    void* accept_data)
{
    TestService* self = g_object_new(TEST_TYPE_SERVICE, NULL);
    NfcPeerService* service = &self->service;

    self->accept_fn = accept_fn;
    self->accept_data = accept_data;
    nfc_peer_service_init_base(service, name);
    service->sap = sap;
    return service;
}

/*==========================================================================*
 * Test target
 *==========================================================================*/

typedef NfcTargetClass TestTargetClass;
typedef struct test_target {
    NfcTarget target;
    guint transmit_id;
    GPtrArray* cmd_resp;
} TestTarget;

G_DEFINE_TYPE(TestTarget, test_target, NFC_TYPE_TARGET)
#define TEST_TYPE_TARGET (test_target_get_type())
#define TEST_TARGET(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_TARGET, TestTarget))

static
GUtilData*
test_target_next_data(
    TestTarget* self)
{
    if (self->cmd_resp->len) {
        GUtilData* data = self->cmd_resp->pdata[0];

        self->cmd_resp->pdata[0] = NULL;
        g_ptr_array_remove_index(self->cmd_resp, 0);
        return data;
    }
    return NULL;
}

static
void
test_target_cancel_transmit(
    NfcTarget* target)
{
    TestTarget* self = TEST_TARGET(target);

    g_assert(self->transmit_id);
    g_source_remove(self->transmit_id);
    self->transmit_id = 0;
}

static
gboolean
test_target_transmit_done(
    gpointer user_data)
{
    TestTarget* self = TEST_TARGET(user_data);
    NfcTarget* target = &self->target;

    g_assert(self->transmit_id);
    self->transmit_id = 0;
    if (self->cmd_resp->len) {
        GUtilData* data = test_target_next_data(self);

        if (data) {
            nfc_target_transmit_done(target, NFC_TRANSMIT_STATUS_OK,
                data->bytes, data->size);
            g_free(data);
        } else {
            nfc_target_transmit_done(target, NFC_TRANSMIT_STATUS_OK, NULL, 0);
        }
    } else {
        nfc_target_transmit_done(target, NFC_TRANSMIT_STATUS_ERROR, NULL, 0);
    }
    return G_SOURCE_REMOVE;
}

static
gboolean
test_target_transmit(
    NfcTarget* target,
    const void* data,
    guint len)
{
    TestTarget* self = TEST_TARGET(target);
    GUtilData* expected = test_target_next_data(self);

    if (expected) {
        g_assert_cmpuint(expected->size, ==, len);
        g_assert(!memcmp(data, expected->bytes,  len));
        g_free(expected);
    }
    self->transmit_id = g_idle_add(test_target_transmit_done, self);
    return TRUE;
}

static
void
test_target_init(
    TestTarget* self)
{
    self->cmd_resp = g_ptr_array_new_with_free_func(g_free);
}

static
void
test_target_finalize(
    GObject* object)
{
    TestTarget* self = TEST_TARGET(object);

    if (self->transmit_id) {
        g_source_remove(self->transmit_id);
    }
    g_ptr_array_free(self->cmd_resp, TRUE);
    G_OBJECT_CLASS(test_target_parent_class)->finalize(object);
}

static
void
test_target_class_init(
    NfcTargetClass* klass)
{
    klass->transmit = test_target_transmit;
    klass->cancel_transmit = test_target_cancel_transmit;
    G_OBJECT_CLASS(klass)->finalize = test_target_finalize;
}

static
void
test_target_add_cmd(
    TestTarget* self,
    const void* bytes,
    guint len)
{
    g_ptr_array_add(self->cmd_resp, gutil_data_new(bytes, len));
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    NfcPeerSocket* socket = g_object_new(NFC_TYPE_PEER_SOCKET, NULL);

    g_assert(!nfc_peer_socket_new_connect(NULL, 0, NULL));
    g_assert(!nfc_peer_socket_new_accept(NULL, 0));
    g_assert_cmpint(nfc_peer_socket_fd(NULL), == ,-1);
    g_assert_cmpint(nfc_peer_socket_fd(socket), == ,-1);
    nfc_peer_socket_set_max_send_queue(NULL, 0);
    g_object_unref(socket);
}

/*==========================================================================*
 * connect
 *==========================================================================*/

static
void
test_never_connect(
    NfcPeerConnection* connection,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data)
{
    g_assert_not_reached();
}

static
void
test_connect_cancelled(
    NfcPeerConnection* connection,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data)
{
    g_assert_cmpint(result, == ,NFC_PEER_CONNECT_CANCELLED);
}

static
void
test_connect_success(
    NfcPeerConnection* connection,
    NFC_PEER_CONNECT_RESULT result,
    void* user_data)
{
    int* count = user_data;

    g_assert_cmpint(*count, == ,0);
    (*count)++;
}

static
void
test_connect(
    void)
{
    static const guint8 connect_16_32_data[] = {
        0x41, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
        0x0f
    };
    static const guint8 cc_32_16_data[] = {
        0x81, 0x90, 0x02, 0x02, 0x00, 0x00, 0x05, 0x01,
        0x0f, 0x06
    };
    static const guint8 disc_16_32_data[] = { 0x41, 0x60 };
    static const guint8 dm_32_16_0_data[] = { 0x81, 0xd0, 0x00 };
    static const guint8 connect_32_test_data[] = {
        0x05, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
        0x0f, 0x06, 0x04, 0x74, 0x65, 0x73, 0x74
    };
    static const guint8 cc_32_32_data[] = {
        0x81, 0xa0, 0x02, 0x02, 0x00, 0x00, 0x05, 0x01,
        0x0f
    };
    static const guint8 disc_32_32_data[] = { 0x81, 0x60 };
    static const guint8 dm_32_32_0_data[] = { 0x81, 0xe0, 0x00 };
    static const guint8 i_send_data[] = {
        0x83, 0x20, 0x00,
        0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const guint8 data[] = {
        0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const guint8 i_recv_data[] = {
        0x83, 0x20, 0x01,
        0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const guint8 recv_data[] = {
        0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const guint8 rr_32_32_1_data[] = { 0x83, 0x60, 0x01 };
    TestTarget* tt = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcPeerService* service = test_service_client_new(NFC_LLC_SAP_UNNAMED);
    NfcLlcParam** params = nfc_llc_param_decode(&param_tlv);
    NfcTarget* target = NFC_TARGET(tt);
    NfcPeerConnection* connection;
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    gulong connection_state_id;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    int count1 = 0, count2 = 0, count3 = 0;
    NfcPeerSocket* socket;
    guint8 buf[sizeof(recv_data) + 1];
    int fd;

    /* Connect/diconnect (connection #1) */
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(connect_16_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(cc_32_16_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(disc_16_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(dm_32_16_0_data));

    /* Connect/diconnect (connection #2) */
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(connect_32_test_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(cc_32_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(disc_32_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(dm_32_32_0_data));

    /* Connection #3 */
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(connect_32_test_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(cc_32_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(i_send_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(i_recv_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(rr_32_32_1_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(disc_32_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(dm_32_32_0_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));

    g_assert(nfc_peer_services_add(services, service));
    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* This has no effect since there are no connections yet */
    nfc_peer_service_disconnect_all(service);

    /* Connection #1 (canceled) */
    connection = nfc_llc_connect(llc, service, 16, test_never_connect,
        test_int_inc, &count1);
    g_assert(connection);
    g_assert(nfc_peer_connection_cancel(connection));

    /* Connection #2 (abandoned) */
    connection = nfc_llc_connect_sn(llc, service, TEST_SERVICE_NAME,
        test_connect_cancelled, test_int_inc, &count2);
    g_assert(connection);
    nfc_peer_service_disconnect_all(service);

    /* Connection #3 (succeeds) */
    connection = nfc_llc_connect_sn(llc, service, TEST_SERVICE_NAME,
        test_connect_success, test_int_inc, &count3);
    g_assert(connection);
    nfc_peer_connection_ref(connection);
    socket = NFC_PEER_SOCKET(connection);
    nfc_peer_socket_set_max_send_queue(socket, 0);
    nfc_peer_socket_set_max_send_queue(socket, 0); /* No effect second time */
    fd = nfc_peer_socket_fd(socket);
    g_assert(fd >= 0);
    g_assert(fcntl(fd, F_SETFL, O_NONBLOCK) >= 0);
    g_assert_cmpint(write(fd, data, sizeof(data)), == ,sizeof(data));

    /* Verify NULL resitance for additional parameters */
    g_assert(nfc_peer_connection_send(connection, NULL));
    nfc_peer_connection_remove_handler(connection, 0);
    g_assert(!nfc_peer_connection_add_state_changed_handler(connection,
        NULL, NULL));

    /* Now wait until connection terminates */
    connection_state_id = nfc_peer_connection_add_state_changed_handler
        (connection, test_connection_dead_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    g_assert(connection->state == NFC_LLC_CO_DEAD);
    g_assert_cmpuint(connection->bytes_received, == ,sizeof(data));
    g_assert(llc->state == NFC_LLC_STATE_ACTIVE);

    /* Read the data from the socket */
    memset(buf, 0, sizeof(buf));
    g_assert_cmpint(read(fd, buf, sizeof(buf)), == ,sizeof(recv_data));
    nfc_peer_connection_remove_handler(connection, connection_state_id);

    /* These calls have no effect at this point */
    g_assert(!nfc_peer_connection_cancel(connection));
    nfc_peer_connection_accepted(connection);
    nfc_peer_connection_rejected(connection);

    /* Drop the connection */
    nfc_peer_connection_unref(connection);

    g_assert(count1 == 1);
    g_assert(count2 == 1);
    g_assert(count3 == 2);
    g_main_loop_unref(loop);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
    nfc_target_unref(target);
}

/*==========================================================================*
 * connect_eof
 *==========================================================================*/

static
void
test_connect_eof_idle_cb(
    NfcLlc* llc,
    void* user_data)
{
    if (llc->idle) {
        NfcPeerSocket* socket = NFC_PEER_SOCKET(user_data);

        g_assert(shutdown(nfc_peer_socket_fd(socket), SHUT_RDWR) == 0);
    }
}

static
void
test_connect_eof(
    void)
{
    static const guint8 connect_32_test_data[] = {
        0x05, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
        0x0f, 0x06, 0x04, 0x74, 0x65, 0x73, 0x74
    };
    static const guint8 cc_32_32_data[] = {
        0x81, 0xa0, 0x02, 0x02, 0x00, 0x00, 0x05, 0x01,
        0x0f
    };
    static const guint8 disc_32_32_data[] = { 0x81, 0x60 };
    static const guint8 dm_32_32_0_data[] = { 0x81, 0xe0, 0x00 };
    static const guint8 i_send_data[] = {
        0x83, 0x20, 0x00,
        0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const guint8 data[] = {
        0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const guint8 i_recv_data[] = {
        0x83, 0x20, 0x01,
        0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const guint8 recv_data[] = {
        0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const guint8 rr_32_32_1_data[] = { 0x83, 0x60, 0x01 };
    TestTarget* tt = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcPeerService* service = test_service_client_new(NFC_LLC_SAP_UNNAMED);
    NfcLlcParam** params = nfc_llc_param_decode(&param_tlv);
    NfcTarget* target = NFC_TARGET(tt);
    NfcPeerConnection* connection;
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    gulong llc_idle_id, connection_state_id;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    int count = 0;
    NfcPeerSocket* socket;
    guint8 buf[sizeof(recv_data) + 1];
    int fd;

    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(connect_32_test_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(cc_32_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(i_send_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(i_recv_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(rr_32_32_1_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    /* ==> At this point LLC becomes idle <== */
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(disc_32_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(dm_32_32_0_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));

    g_assert(nfc_peer_services_add(services, service));
    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* Establish the connection */
    connection = nfc_llc_connect_sn(llc, service, TEST_SERVICE_NAME,
        NULL, test_int_inc, &count);
    g_assert(connection);
    nfc_peer_connection_ref(connection);
    socket = NFC_PEER_SOCKET(connection);
    fd = nfc_peer_socket_fd(socket);
    g_assert(fd >= 0);
    g_assert(fcntl(fd, F_SETFL, O_NONBLOCK) >= 0);
    g_assert_cmpint(write(fd, data, sizeof(data)), == ,sizeof(data));

    /* We shutdown the socket when connection becomes idle */
    llc_idle_id = nfc_llc_add_idle_changed_handler(llc,
        test_connect_eof_idle_cb, connection);
    connection_state_id = nfc_peer_connection_add_state_changed_handler
        (connection, test_connection_dead_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    g_assert(llc->state == NFC_LLC_STATE_ACTIVE);
    g_assert(connection->state == NFC_LLC_CO_DEAD);

    /* Read the data from the socket */
    memset(buf, 0, sizeof(buf));
    g_assert_cmpint(read(fd, buf, sizeof(buf)), == ,sizeof(recv_data));
    nfc_peer_connection_remove_handler(connection, connection_state_id);
    g_assert(!nfc_peer_connection_cancel(connection));
    nfc_peer_connection_unref(connection);

    g_assert(count == 1);
    g_main_loop_unref(loop);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_llc_remove_handler(llc, llc_idle_id);
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
    nfc_target_unref(target);
}

/*==========================================================================*
 * connect_error
 *==========================================================================*/

static
void
test_connect_error_idle_cb(
    NfcLlc* llc,
    void* user_data)
{
    if (llc->idle) {
        NfcPeerSocket* socket = NFC_PEER_SOCKET(user_data);

        g_assert(shutdown(nfc_peer_socket_fd(socket), SHUT_RD) == 0);
    }
}

static
void
test_connect_error(
    void)
{
    static const guint8 connect_32_test_data[] = {
        0x05, 0x20, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
        0x0f, 0x06, 0x04, 0x74, 0x65, 0x73, 0x74
    };
    static const guint8 cc_32_32_data[] = {
        0x81, 0xa0, 0x02, 0x02, 0x00, 0x00, 0x05, 0x01,
        0x0f
    };
    static const guint8 disc_32_32_data[] = { 0x81, 0x60 };
    static const guint8 dm_32_32_0_data[] = { 0x81, 0xe0, 0x00 };
    static const guint8 i_recv_data[] = {
        0x83, 0x20, 0x00,
        0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const guint8 rr_32_32_1_data[] = { 0x83, 0x60, 0x01 };
    TestTarget* tt = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcPeerService* service = test_service_client_new(NFC_LLC_SAP_UNNAMED);
    NfcLlcParam** params = nfc_llc_param_decode(&param_tlv);
    NfcTarget* target = NFC_TARGET(tt);
    NfcPeerConnection* connection;
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    gulong llc_idle_id, connection_state_id;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    int count = 0;
    NfcPeerSocket* socket;
    guint8 buf[1];
    int fd;

    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(connect_32_test_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(cc_32_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    /* ==> At this point LLC becomes idle <== */
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(i_recv_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(rr_32_32_1_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(disc_32_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(dm_32_32_0_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));

    g_assert(nfc_peer_services_add(services, service));
    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* Establish the connection */
    connection = nfc_llc_connect_sn(llc, service, TEST_SERVICE_NAME, NULL,
        test_int_inc, &count);
    g_assert(connection);
    nfc_peer_connection_ref(connection);
    socket = NFC_PEER_SOCKET(connection);
    fd = nfc_peer_socket_fd(socket);
    g_assert(fd >= 0);
    g_assert(fcntl(fd, F_SETFL, O_NONBLOCK) >= 0);

    /* We shutdown the socket when connection becomes idle */
    llc_idle_id = nfc_llc_add_idle_changed_handler(llc,
        test_connect_error_idle_cb, connection);
    connection_state_id = nfc_peer_connection_add_state_changed_handler
        (connection, test_connection_dead_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    g_assert(llc->state == NFC_LLC_STATE_ACTIVE);
    g_assert(connection->state == NFC_LLC_CO_DEAD);

    /* Try to read the data from the socket (and get nothing) */
    memset(buf, 0, sizeof(buf));
    g_assert_cmpint(read(fd, buf, sizeof(buf)), == ,0);
    nfc_peer_connection_remove_handler(connection, connection_state_id);
    g_assert(!nfc_peer_connection_cancel(connection));
    nfc_peer_connection_unref(connection);

    g_assert(count == 1);
    g_main_loop_unref(loop);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_llc_remove_handler(llc, llc_idle_id);
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
    nfc_target_unref(target);
}

/*==========================================================================*
 * listen
 *==========================================================================*/

typedef struct test_listen_data {
    GMainLoop* loop;
    NfcPeerSocket* socket;
    gulong connection_state_id;
} TestListenData;

static
void
test_listen_idle_cb(
    NfcLlc* llc,
    void* user_data)
{
    TestListenData* test = user_data;

    g_assert(test->socket);
    g_assert(shutdown(nfc_peer_socket_fd(test->socket), SHUT_RDWR) == 0);
}

static
void
test_listen_accept_cb(
    NfcPeerService* service,
    NfcPeerSocket* socket,
    void* user_data)
{
    TestListenData* test = user_data;
    NfcPeerConnection* pc = &socket->connection;
    int fd;
    static const guint8 data[] = {
        0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };

    g_assert(!test->socket);
    test->socket = socket;
    nfc_peer_connection_ref(pc);
    test->connection_state_id = nfc_peer_connection_add_state_changed_handler
        (pc, test_connection_dead_quit_loop_cb, test->loop);

    fd = nfc_peer_socket_fd(socket);
    g_assert(fd >= 0);
    g_assert(fcntl(fd, F_SETFL, O_NONBLOCK) >= 0);
    g_assert_cmpint(write(fd, data, sizeof(data)), == , sizeof(data));
}

static
void
test_listen(
    void)
{
    static const guint8 connect_test_32_data[] = {
        0x05, 0x20, 0x02, 0x02, 0x00, 0x00, 0x05, 0x01,
        0x0f, 0x06, 0x04, 0x74, 0x65, 0x73, 0x74
    };
    static const guint8 cc_32_16_data[] = {
        0x81, 0x90, 0x02, 0x02, 0x07, 0xff, 0x05, 0x01,
        0x0f
    };
    static const guint8 i_send_data[] = {
        0x83, 0x10, 0x00, /* Matches write_data in test_listen_accept_cb: */
        0x00, 0x01, 0x02, 0x03, 0x03, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const guint8 i_recv_1_data[] = {
        0x43, 0x20, 0x01, /* First chunk */
        0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
    };
    static const guint8 i_recv_2_data[] = {
        0x43, 0x20, 0x11, /* Second chunk */
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const guint8 recv_data[] = {
        0x10, 0x11, 0x12, 0x13, 0x13, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const guint8 rr_32_16_1_data[] = { 0x83, 0x50, 0x01 };
    static const guint8 rr_32_16_2_data[] = { 0x83, 0x50, 0x02 };
    static const guint8 disc_32_16_data[] = { 0x81, 0x50 };
    static const guint8 dm_16_32_0_data[] = { 0x41, 0xe0, 0x00 };
    TestTarget* tt = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcPeerService* service;
    NfcLlcParam** params = nfc_llc_param_decode(&param_tlv);
    NfcTarget* target = NFC_TARGET(tt);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    gulong llc_idle_id;
    guint8 buf[sizeof(recv_data) + 1];
    TestListenData test;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, TRUE);
    service = test_service_server_new(TEST_SERVICE_NAME, NFC_LLC_SAP_NAMED,
        test_listen_accept_cb, &test);

    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(connect_test_32_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(cc_32_16_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(i_send_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(i_recv_1_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(rr_32_16_1_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(i_recv_2_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(rr_32_16_2_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    /* ==> At this point LLC becomes idle <== */
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(disc_32_16_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(dm_16_32_0_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));

    g_assert(nfc_peer_services_add(services, service));
    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* We shutdown the socket when connection becomes idle */
    llc_idle_id = nfc_llc_add_idle_changed_handler(llc,
        test_listen_idle_cb, &test);
    test_run(&test_opt, test.loop);
    g_assert(llc->state == NFC_LLC_STATE_ACTIVE);
    g_assert(test.socket);
    g_assert(test.connection_state_id);

    /* Read the data from the socket */
    memset(buf, 0, sizeof(buf));
    g_assert_cmpint(read(nfc_peer_socket_fd(test.socket), buf, sizeof(buf)),
        == , sizeof(recv_data));
    g_assert(!nfc_peer_connection_cancel(NFC_PEER_CONNECTION(test.socket)));
    nfc_peer_connection_remove_handler(NFC_PEER_CONNECTION(test.socket),
        test.connection_state_id);
    nfc_peer_connection_unref(NFC_PEER_CONNECTION(test.socket));

    g_main_loop_unref(test.loop);
    nfc_llc_remove_handler(llc, llc_idle_id);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
    nfc_target_unref(target);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("connect"), test_connect);
    g_test_add_func(TEST_("connect_eof"), test_connect_eof);
    g_test_add_func(TEST_("connect_error"), test_connect_error);
    g_test_add_func(TEST_("listen"), test_listen);
    signal(SIGPIPE, SIG_IGN);
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
