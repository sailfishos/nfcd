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

#include "nfc_types_p.h"
#include "nfc_ndef.h"
#include "nfc_snep_server.h"
#include "nfc_target_impl.h"
#include "nfc_peer_services.h"
#include "nfc_llc.h"
#include "nfc_llc_io.h"
#include "nfc_llc_param.h"

#include "test_common.h"

#include <gutil_log.h>

static TestOpt test_opt;

#define TEST_(name) "/core/snep/" name

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
test_snep_event_counter(
    NfcSnepServer* snep,
    void* user_data)
{
    (*((int*)user_data))++;
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
test_target_add_cmd_data(
    TestTarget* self,
    const GUtilData* data)
{
    g_ptr_array_add(self->cmd_resp, test_clone_data(data));
}

static
void
test_target_add_cmd(
    TestTarget* self,
    const void* bytes,
    guint len)
{
    g_ptr_array_add(self->cmd_resp, test_alloc_data(bytes, len));
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    nfc_snep_server_remove_handler(NULL, 0);
    nfc_snep_server_remove_handlers(NULL, NULL, 0);
    g_assert(!nfc_snep_server_add_state_changed_handler(NULL, NULL, NULL));
    g_assert(!nfc_snep_server_add_ndef_changed_handler(NULL, NULL, NULL));
}

/*==========================================================================*
 * idle
 *==========================================================================*/

static
void
test_idle(
    void)
{
    static const guint8 param_tlv_data[] = {
        0x01, 0x01, 0x11, 0x02, 0x02, 0x07, 0xff, 0x03,
        0x02, 0x00, 0x13, 0x04, 0x01, 0xff, 0x07, 0x01,
        0x03
    };
    static const guint8 symm_data[] = { 0x00, 0x00 };
    static const GUtilData param_tlv = { TEST_ARRAY_AND_SIZE(param_tlv_data) };
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    TestTarget* tt = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcLlcParam** params = nfc_llc_param_decode(&param_tlv);
    NfcSnepServer* snep = nfc_snep_server_new();
    NfcPeerService* service = NFC_PEER_SERVICE(snep);
    NfcTarget* target = NFC_TARGET(tt);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    gulong id;

    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));
    test_target_add_cmd(tt, TEST_ARRAY_AND_SIZE(symm_data));

    /* These have no effect */
    g_assert(!nfc_snep_server_add_state_changed_handler(snep, NULL, NULL));
    g_assert(!nfc_snep_server_add_ndef_changed_handler(snep, NULL, NULL));
    nfc_snep_server_remove_handler(snep, 0);

    g_assert(nfc_peer_services_add(services, service));
    g_assert_cmpuint(service->sap, == ,NFC_LLC_SAP_SNEP);
    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* Wait for the conversation to start */
    id = nfc_llc_add_state_changed_handler(llc, test_llc_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    if (llc->state == NFC_LLC_STATE_ACTIVE) {
        /* Now wait until transfer error */
        test_run(&test_opt, loop);
    }

    g_assert(!snep->ndef);
    g_main_loop_unref(loop);
    nfc_llc_remove_handler(llc, id);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
    nfc_target_unref(target);
}

/*==========================================================================*
 * ndef
 *==========================================================================*/

static const guint8 param_tlv_data[] = {
    0x01, 0x01, 0x11, 0x02, 0x02, 0x07, 0xff, 0x03,
    0x02, 0x00, 0x13, 0x04, 0x01, 0xff, 0x07, 0x01,
    0x03
};
static const GUtilData param_tlv = { TEST_ARRAY_AND_SIZE(param_tlv_data) };

static const guint8 symm_data[] = { 0x00, 0x00 };
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

static
void
test_ndef(
    const GUtilData* packets,
    guint count)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    TestTarget* tt = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcLlcParam** params = nfc_llc_param_decode(&param_tlv);
    NfcSnepServer* snep = nfc_snep_server_new();
    NfcPeerService* service = NFC_PEER_SERVICE(snep);
    NfcTarget* target = NFC_TARGET(tt);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    int snep_state_change_count = 0, snep_ndef_change_count = 0;
    gulong id, snep_id[2];
    guint i;

    for (i = 0; i < count; i++) {
        test_target_add_cmd_data(tt, packets + i);
    }

    g_assert(nfc_peer_services_add(services, service));
    g_assert_cmpuint(service->sap, == ,NFC_LLC_SAP_SNEP);
    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* Count NfcSnepServer events */
    snep_id[0] = nfc_snep_server_add_state_changed_handler(snep,
      test_snep_event_counter, &snep_state_change_count);
    snep_id[1] = nfc_snep_server_add_ndef_changed_handler(snep,
      test_snep_event_counter, &snep_ndef_change_count);

    /* Wait for the conversation to start */
    id = nfc_llc_add_state_changed_handler(llc, test_llc_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    if (llc->state == NFC_LLC_STATE_ACTIVE) {
        /* Now wait until transfer error or something else breaks the loop */
        test_run(&test_opt, loop);
    }

    /* Assert that we have received expected number of events */
    g_assert_cmpint(snep_state_change_count, == ,2);
    g_assert_cmpint(snep_ndef_change_count, == ,1);
    nfc_snep_server_remove_handler(snep, snep_id[0]);
    nfc_snep_server_remove_handler(snep, snep_id[1]);

    /* Assert that we have received the NDEF */
    g_assert(snep->ndef);
    g_assert(NFC_IS_NDEF_REC_SP(snep->ndef));
    g_main_loop_unref(loop);
    nfc_llc_remove_handler(llc, id);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
    nfc_target_unref(target);
}

static
void
test_ndef_complete(
    void)
{
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
    static const GUtilData packets[] = {
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(connect_snep_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_4_32_put_data) },
        { TEST_ARRAY_AND_SIZE(rnr_32_4_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(disc_32_4_data) },
        { TEST_ARRAY_AND_SIZE(dm_4_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) }
    };
    test_ndef(TEST_ARRAY_AND_COUNT(packets));
}

static
void
test_ndef_flagmented(
    void)
{
    static const guint8 i_snep_4_32_put_data[] = {
        0x13, 0x20, 0x00,
        0x10, 0x02, 0x00, 0x00, 0x00, 0x1f
    };
    static const guint8 i_snep_32_4_continue_data[] = {
        0x83, 0x04, 0x01,
        0x10, 0x80, 0x00, 0x00, 0x00, 0x00
    };
    static const guint8 i_snep_4_32_ndef_data[] = {
        0x13, 0x20, 0x11,
        0xd1, 0x02, 0x1a, 0x53, 0x70, 0x91, 0x01, 0x0a,
        0x55, 0x03, 0x6a, 0x6f, 0x6c, 0x6c, 0x61, 0x2e,
        0x63, 0x6f, 0x6d, 0x51, 0x01, 0x08, 0x54, 0x02,
        0x65, 0x6e, 0x4a, 0x6f, 0x6c, 0x6c, 0x61
    };
    static const guint8 rnr_32_4_data[] = { 0x83, 0x84, 0x02 };
    static const guint8 disc_32_4_data[] = { 0x81, 0x44 };
    static const guint8 dm_4_32_data[] = { 0x11, 0xe0, 0x00 };
    static const GUtilData packets[] = {
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(connect_snep_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_4_32_put_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_32_4_continue_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_4_32_ndef_data) },
        { TEST_ARRAY_AND_SIZE(rnr_32_4_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(disc_32_4_data) },
        { TEST_ARRAY_AND_SIZE(dm_4_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) }
    };
    test_ndef(TEST_ARRAY_AND_COUNT(packets));
}

/*==========================================================================*
 * fail
 *==========================================================================*/

static
void
test_fail(
    const GUtilData* packets,
    guint count)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    TestTarget* tt = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcLlcParam** params = nfc_llc_param_decode(&param_tlv);
    NfcSnepServer* snep = nfc_snep_server_new();
    NfcPeerService* service = NFC_PEER_SERVICE(snep);
    NfcTarget* target = NFC_TARGET(tt);
    NfcPeerServices* services = nfc_peer_services_new();
    NfcLlcIo* io = nfc_llc_io_initiator_new(target);
    NfcLlc* llc;
    int snep_state_change_count = 0, snep_ndef_change_count = 0;
    gulong id, snep_id[2];
    guint i;

    for (i = 0; i < count; i++) {
        test_target_add_cmd_data(tt, packets + i);
    }

    g_assert(nfc_peer_services_add(services, service));
    g_assert_cmpuint(service->sap, == ,NFC_LLC_SAP_SNEP);
    llc = nfc_llc_new(io, services, nfc_llc_param_constify(params));
    g_assert(llc->state == NFC_LLC_STATE_START);

    /* Count NfcSnepServer events */
    snep_id[0] = nfc_snep_server_add_state_changed_handler(snep,
      test_snep_event_counter, &snep_state_change_count);
    snep_id[1] = nfc_snep_server_add_ndef_changed_handler(snep,
      test_snep_event_counter, &snep_ndef_change_count);

    /* Wait for the conversation to start */
    id = nfc_llc_add_state_changed_handler(llc, test_llc_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    if (llc->state == NFC_LLC_STATE_ACTIVE) {
        /* Now wait until transfer error or something else breaks the loop */
        test_run(&test_opt, loop);
    }

    /* Assert that we have received expected number of events */
    g_assert_cmpint(snep_state_change_count, == ,2);
    g_assert_cmpint(snep_ndef_change_count, == ,0);
    nfc_snep_server_remove_handler(snep, snep_id[0]);
    nfc_snep_server_remove_handler(snep, snep_id[1]);

    /* Assert that we have received the NDEF */
    g_assert(!snep->ndef);
    g_main_loop_unref(loop);
    nfc_llc_remove_handler(llc, id);
    nfc_llc_param_free(params);
    nfc_peer_service_unref(service);
    nfc_peer_services_unref(services);
    nfc_llc_io_unref(io);
    nfc_llc_free(llc);
    nfc_target_unref(target);
}

static
void
test_fail_short(
    void)
{
    static const guint8 i_snep_4_32_short_data[] = {
        0x13, 0x20, 0x00,
        0x20, 0x02
    };
    static const guint8 rnr_32_4_data[] = { 0x83, 0x84, 0x01 };
    static const guint8 disc_32_4_data[] = { 0x81, 0x44 };
    static const guint8 dm_4_32_data[] = { 0x11, 0xe0, 0x00 };
    static const GUtilData packets[] = {
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(connect_snep_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_4_32_short_data) },
        { TEST_ARRAY_AND_SIZE(rnr_32_4_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(disc_32_4_data) },
        { TEST_ARRAY_AND_SIZE(dm_4_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) }
    };
    test_fail(TEST_ARRAY_AND_COUNT(packets));
}

static
void
test_fail_version(
    void)
{
    static const guint8 i_snep_4_32_put_data[] = {
        0x13, 0x20, 0x00,
        0x20, 0x02, 0x00, 0x00, 0x00, 0x00
    };
    static const guint8 i_snep_32_4_resp_data[] = {
        0x83, 0x04, 0x01,
        0x10, 0xe1, 0x00, 0x00, 0x00, 0x00
    };
    static const guint8 rnr_4_32_data[] = { 0x13, 0xa0, 0x01 };
    static const guint8 disc_32_4_data[] = { 0x81, 0x44 };
    static const guint8 dm_4_32_data[] = { 0x11, 0xe0, 0x00 };
    static const GUtilData packets[] = {
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(connect_snep_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_4_32_put_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_32_4_resp_data) },
        { TEST_ARRAY_AND_SIZE(rnr_4_32_data) },
        { TEST_ARRAY_AND_SIZE(disc_32_4_data) },
        { TEST_ARRAY_AND_SIZE(dm_4_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) }
    };
    test_fail(TEST_ARRAY_AND_COUNT(packets));
}

static
void
test_fail_get(
    void)
{
    static const guint8 i_snep_4_32_get_data[] = {
        0x13, 0x20, 0x00,
        0x10, 0x01, 0x00, 0x00, 0x00, 0x00
    };
    static const guint8 i_snep_32_4_resp_data[] = {
        0x83, 0x04, 0x01,
        0x10, 0xe0, 0x00, 0x00, 0x00, 0x00
    };
    static const guint8 rnr_4_32_data[] = { 0x13, 0xa0, 0x01 };
    static const guint8 disc_32_4_data[] = { 0x81, 0x44 };
    static const guint8 dm_4_32_data[] = { 0x11, 0xe0, 0x00 };
    static const GUtilData packets[] = {
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(connect_snep_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_4_32_get_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_32_4_resp_data) },
        { TEST_ARRAY_AND_SIZE(rnr_4_32_data) },
        { TEST_ARRAY_AND_SIZE(disc_32_4_data) },
        { TEST_ARRAY_AND_SIZE(dm_4_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) }
    };
    test_fail(TEST_ARRAY_AND_COUNT(packets));
}

static
void
test_fail_bad_request(
    void)
{
    static const guint8 i_snep_4_32_get_data[] = {
        0x13, 0x20, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    static const guint8 i_snep_32_4_resp_data[] = {
        0x83, 0x04, 0x01,
        0x10, 0xc2, 0x00, 0x00, 0x00, 0x00
    };
    static const guint8 rnr_4_32_data[] = { 0x13, 0xa0, 0x01 };
    static const guint8 disc_32_4_data[] = { 0x81, 0x44 };
    static const guint8 dm_4_32_data[] = { 0x11, 0xe0, 0x00 };
    static const GUtilData packets[] = {
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(connect_snep_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_4_32_get_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_32_4_resp_data) },
        { TEST_ARRAY_AND_SIZE(rnr_4_32_data) },
        { TEST_ARRAY_AND_SIZE(disc_32_4_data) },
        { TEST_ARRAY_AND_SIZE(dm_4_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) }
    };
    test_fail(TEST_ARRAY_AND_COUNT(packets));
}

static
void
test_fail_extra_data(
    void)
{
    static const guint8 i_snep_4_32_broken_data[] = {
        0x13, 0x20, 0x00,
        0x10, 0x02, 0x00, 0x00, 0x00, 0x1f,
        0xd1, 0x02, 0x1a, 0x53, 0x70, 0x91, 0x01, 0x0a,
        0x55, 0x03, 0x6a, 0x6f, 0x6c, 0x6c, 0x61, 0x2e,
        0x63, 0x6f, 0x6d, 0x51, 0x01, 0x08, 0x54, 0x02,
        0x65, 0x6e, 0x4a, 0x6f, 0x6c, 0x6c, 0x61,
        0x00 /* Extra byte */
    };
    static const guint8 rnr_32_4_data[] = { 0x83, 0x84, 0x01 };
    static const guint8 disc_32_4_data[] = { 0x81, 0x44 };
    static const guint8 dm_4_32_data[] = { 0x11, 0xe0, 0x00 };
    static const GUtilData packets[] = {
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(connect_snep_data) },
        { TEST_ARRAY_AND_SIZE(cc_snep_data) },
        { TEST_ARRAY_AND_SIZE(i_snep_4_32_broken_data) },
        { TEST_ARRAY_AND_SIZE(rnr_32_4_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(disc_32_4_data) },
        { TEST_ARRAY_AND_SIZE(dm_4_32_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) },
        { TEST_ARRAY_AND_SIZE(symm_data) }
    };
    test_fail(TEST_ARRAY_AND_COUNT(packets));
}

/*==========================================================================*
 * Common
 *==========================================================================*/

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("idle"), test_idle);
    g_test_add_func(TEST_("ndef/complete"), test_ndef_complete);
    g_test_add_func(TEST_("ndef/flagmented"), test_ndef_flagmented);
    g_test_add_func(TEST_("fail/short"), test_fail_short);
    g_test_add_func(TEST_("fail/version"), test_fail_version);
    g_test_add_func(TEST_("fail/get"), test_fail_get);
    g_test_add_func(TEST_("fail/bad_request"), test_fail_bad_request);
    g_test_add_func(TEST_("fail/extra_data"), test_fail_extra_data);
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
