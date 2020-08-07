/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_tag_p.h"
#include "nfc_target_p.h"
#include "nfc_target_impl.h"

#include <gutil_log.h>

static TestOpt test_opt;

#define TEST_TIMEOUT (10) /* seconds */

static
void
test_quit_loop(
    void* user_data)
{
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_clear_bytes(
    void* user_data)
{
    GUtilData* data = user_data;

    data->bytes = NULL;
    data->size = 0;
}

static
void
test_sequence_started(
    NfcTarget* target,
    void* user_data)
{
    if (target->sequence) {
        (*(int*)user_data)++;
    }
}

static
void
test_sequence_finished(
    NfcTarget* target,
    void* user_data)
{
    if (!target->sequence) {
        (*(int*)user_data)++;
    }
}

static
void
test_target_inc(
    NfcTarget* target,
    void* user_data)
{
    (*(int*)user_data)++;
}

/*==========================================================================*
 * Transmit response
 *==========================================================================*/

typedef struct test_transmit_respose {
    void* data;
    guint len;
    NFC_TRANSMIT_STATUS status;
} TestTransmitResponse;

static
void
test_transmit_response_free(
    TestTransmitResponse* resp)
{
    g_free(resp->data);
    g_free(resp);
}

static
void
test_transmit_response_free1(
    gpointer resp)
{
    test_transmit_response_free(resp);
}

static
TestTransmitResponse*
test_transmit_response_new_ok(
    const guint8* bytes,
    guint len)
{
    TestTransmitResponse* resp = g_new0(TestTransmitResponse, 1);

    resp->data = g_memdup(bytes, len);
    resp->len = len;
    resp->status = NFC_TRANSMIT_STATUS_OK;
    return resp;
}

static
TestTransmitResponse*
test_transmit_response_new_from_bytes(
    const GUtilData* bytes)
{
    return test_transmit_response_new_ok(bytes->bytes, bytes->size);
}

static
TestTransmitResponse*
test_transmit_response_new_fail(
    void)
{
    /* NFC_TARGET_TRANSMIT_FAILED is zero */
    return g_new0(TestTransmitResponse, 1);
}

/*==========================================================================*
 * Test target
 *==========================================================================*/

typedef NfcTargetClass TestTargetClass;
typedef struct test_target {
    NfcTarget target;
    gboolean deactivated;
    gboolean fail_transmit;
    guint transmit_id;
    GSList* transmit_responses;
    guint succeeded;
    guint failed;
} TestTarget;

G_DEFINE_TYPE(TestTarget, test_target, NFC_TYPE_TARGET)
#define TEST_TYPE_TARGET (test_target_get_type())
#define TEST_TARGET(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_TARGET, TestTarget))

static
TestTarget*
test_target_new(
    void)
{
    return g_object_new(TEST_TYPE_TARGET, NULL);
}

static
gboolean
test_target_transmit_cb(
    gpointer user_data)
{
    TestTarget* self = TEST_TARGET(user_data);
    NfcTarget* target = &self->target;

    self->transmit_id = 0;
    if (self->transmit_responses) {
        TestTransmitResponse* resp = self->transmit_responses->data;

        self->transmit_responses = g_slist_delete_link
            (self->transmit_responses, self->transmit_responses);
        nfc_target_transmit_done(target, resp->status, resp->data, resp->len);
        test_transmit_response_free(resp);
    } else {
        nfc_target_transmit_done(target, NFC_TRANSMIT_STATUS_OK, NULL, 0);
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

    if (self->fail_transmit) {
        /* Base class fails the call */
        return NFC_TARGET_CLASS(test_target_parent_class)->transmit
            (target, data, len);
    } else {
        g_assert(!self->transmit_id);
        self->transmit_id = g_idle_add(test_target_transmit_cb, self);
        return TRUE;
    }
}

static
void
test_target_cancel_transmit(
    NfcTarget* target)
{
    NFC_TARGET_CLASS(test_target_parent_class)->cancel_transmit(target);
}

static
void
test_target_deactivate(
    NfcTarget* target)
{
    TestTarget* self = TEST_TARGET(target);

    self->deactivated = TRUE;
    nfc_target_gone(target);
}

static
void
test_target_init(
    TestTarget* self)
{
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
    g_slist_free_full(self->transmit_responses, test_transmit_response_free1);
    G_OBJECT_CLASS(test_target_parent_class)->finalize(object);
}

static
void
test_target_class_init(
    NfcTargetClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = test_target_finalize;
    klass->deactivate = test_target_deactivate;
    klass->transmit = test_target_transmit;
    klass->cancel_transmit = test_target_cancel_transmit;
}

/*==========================================================================*
 * Test target with reactivate
 *==========================================================================*/

typedef enum test_reactivate_mode {
    TEST_REACTIVATE_MODE_OK,
    TEST_REACTIVATE_MODE_FAIL,
    TEST_REACTIVATE_MODE_TIMEOUT
} TEST_REACTIVATE_MODE;

typedef TestTargetClass TestTarget2Class;
typedef struct test_target2 {
    TestTarget parent;
    TEST_REACTIVATE_MODE mode;
    guint reactivate_id;
} TestTarget2;

G_DEFINE_TYPE(TestTarget2, test_target2, TEST_TYPE_TARGET)
#define TEST_TYPE_TARGET2 (test_target2_get_type())
#define TEST_TARGET2(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_TARGET2, TestTarget2))

static
TestTarget2*
test_target2_new(
    void)
{
    return g_object_new(TEST_TYPE_TARGET2, NULL);
}

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
    switch (test->mode) {
    case TEST_REACTIVATE_MODE_OK:
        test->reactivate_id = g_idle_add(test_target2_reactivated, test);
        return TRUE;
    case TEST_REACTIVATE_MODE_FAIL:
        break;
    case TEST_REACTIVATE_MODE_TIMEOUT:
        return TRUE;
    }
    return FALSE;
}

static
void
test_target2_init(
    TestTarget2* self)
{
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
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    /* Public interfaces are NULL tolerant */
    g_assert(!nfc_target_ref(NULL));
    g_assert(!nfc_target_transmit(NULL, NULL, 0, NULL, NULL, NULL, NULL));
    g_assert(!nfc_target_add_gone_handler(NULL, NULL, NULL));
    g_assert(!nfc_target_add_sequence_handler(NULL, NULL, NULL));
    nfc_target_deactivate(NULL);
    g_assert(!nfc_target_can_reactivate(NULL));
    g_assert(!nfc_target_reactivate(NULL, NULL, NULL));
    nfc_target_set_transmit_timeout(NULL, 0);
    nfc_target_set_reactivate_timeout(NULL, 0);
    nfc_target_remove_handler(NULL, 0);
    g_assert(!nfc_target_cancel_transmit(NULL, 0));
    nfc_target_transmit_done(NULL, NFC_TRANSMIT_STATUS_ERROR, NULL, 0);
    nfc_target_reactivated(NULL);
    nfc_target_gone(NULL);
    nfc_target_unref(NULL);
    g_assert(!nfc_target_sequence_new(NULL));
    nfc_target_sequence_free(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    TestTarget* test = test_target_new();
    NfcTarget* target = &test->target;
    int n = 0;
    gulong gone_id = nfc_target_add_gone_handler(target, test_target_inc, &n);

    /* Callback is requred */
    g_assert(!nfc_target_add_gone_handler(target, NULL, NULL));
    g_assert(!nfc_target_add_sequence_handler(target, NULL, NULL));

    /* Fail one transmit */
    test->fail_transmit = TRUE;
    g_assert(!nfc_target_transmit(target, NULL, 0, NULL, NULL, NULL, NULL));

    /* There's nothing to cancel */
    g_assert(!nfc_target_cancel_transmit(target, 0));
    g_assert(!nfc_target_cancel_transmit(target, 1));

    /* Reactivation is not supported by this target */
    g_assert(!nfc_target_can_reactivate(target));
    g_assert(!nfc_target_reactivate(target, NULL, NULL));
    nfc_target_reactivated(target); /* Does nothing */

    /* Deactivate only sets the flag */
    nfc_target_deactivate(target);
    g_assert(test->deactivated);

    /* "Gone" signal is only issued once */
    g_assert(gone_id);
    nfc_target_gone(target);
    nfc_target_gone(target);
    g_assert(n == 1);
    nfc_target_remove_handler(target, 0 /* ignored */);
    nfc_target_remove_handler(target, gone_id);

    /* This deactivate does nothing */
    nfc_target_deactivate(target);

    g_assert(nfc_target_ref(target) == target);
    nfc_target_unref(target);
    nfc_target_unref(target);
}

/*==========================================================================*
 * transmit_ok
 *==========================================================================*/

static
void
test_transmit_ok_resp(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    TestTarget* test = TEST_TARGET(target);
    const GUtilData* expected = user_data;

    GDEBUG("Status %d, %u bytes", status, len);
    g_assert(status == NFC_TRANSMIT_STATUS_OK);
    g_assert(len == expected->size);
    g_assert(!memcmp(data, expected->bytes, len));
    test->succeeded++;
}

static
void
test_transmit_ok(
    void)
{
    static const guint8 data1[] = { 0x01 };
    static const guint8 data2[] = { 0x01, 0x02 };
    GUtilData resp1, resp2;
    TestTarget* test = test_target_new();
    NfcTarget* target = &test->target;
    guint id1, id2, id3;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);

    if (!(test_opt.flags & TEST_FLAG_DEBUG)) {
        nfc_target_set_transmit_timeout(target, TEST_TIMEOUT_SEC * 1000);
    }

    TEST_BYTES_SET(resp1, data1);
    TEST_BYTES_SET(resp2, data2);
    test->transmit_responses = g_slist_append(g_slist_append(
        test->transmit_responses,
        test_transmit_response_new_from_bytes(&resp1)),
        test_transmit_response_new_from_bytes(&resp2));

    id1 = nfc_target_transmit(target, data1, sizeof(data1), NULL,
        test_transmit_ok_resp, test_clear_bytes, &resp1);
    id2 = nfc_target_transmit(target, data2, sizeof(data2), NULL,
        test_transmit_ok_resp, test_clear_bytes, &resp2);
    id3 = nfc_target_transmit(target, NULL, 0, NULL, NULL,
        test_quit_loop, loop);
    g_assert(id1);
    g_assert(id2);
    g_assert(id3);

    test_run(&test_opt, loop);

    g_assert(test->succeeded == 2);
    g_assert(!resp1.bytes);
    g_assert(!resp2.bytes);

    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transmit_fail
 *==========================================================================*/

static
void
test_transmit_fail_resp(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    TestTarget* test = TEST_TARGET(target);

    GDEBUG("Status %d, %u bytes", status, len);
    if (status == NFC_TRANSMIT_STATUS_OK) {
        const GUtilData* expected = user_data;

        g_assert(!test->fail_transmit);
        g_assert(len == expected->size);
        g_assert(!memcmp(data, expected->bytes, len));
        test->succeeded++;
    } else{
        test->failed++;
    }

    /* Next request will fail */
    test->fail_transmit = TRUE;
}

static
void
test_transmit_fail(
    void)
{
    static const guint8 data1[] = { 0x01 };
    static const guint8 data2[] = { 0x01, 0x02 };
    static const guint8 data3[] = { 0x01, 0x02, 0x03 };
    GUtilData resp1, resp2, resp3;
    TestTarget* test = test_target_new();
    NfcTarget* target = &test->target;
    guint id1, id2, id3, id4;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);

    if (!(test_opt.flags & TEST_FLAG_DEBUG)) {
        nfc_target_set_transmit_timeout(target, -1);
    }

    TEST_BYTES_SET(resp1, data1);
    TEST_BYTES_SET(resp2, data2);
    TEST_BYTES_SET(resp3, data3);
    test->transmit_responses = g_slist_append(g_slist_append(g_slist_append(
        test->transmit_responses,
        test_transmit_response_new_from_bytes(&resp1)),
        test_transmit_response_new_from_bytes(&resp2)),
        test_transmit_response_new_fail());

    id1 = nfc_target_transmit(target, data1, sizeof(data1), NULL,
        test_transmit_fail_resp, test_clear_bytes, &resp1);
    id2 = nfc_target_transmit(target, data2, sizeof(data2), NULL,
        test_transmit_fail_resp, test_clear_bytes, &resp2);
    id3 = nfc_target_transmit(target, data3, sizeof(data3), NULL,
        test_transmit_fail_resp, test_clear_bytes, &resp3);
    id4 = nfc_target_transmit(target, NULL, 0, NULL, NULL,
        test_quit_loop, loop);
    g_assert(id1);
    g_assert(id2);
    g_assert(id3);
    g_assert(id4);

    test_run(&test_opt, loop);

    g_assert(test->succeeded == 1);
    g_assert(test->failed == 2);
    g_assert(!resp1.bytes);
    g_assert(!resp2.bytes);
    g_assert(!resp3.bytes);

    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transmit_cancel
 *==========================================================================*/

static
void
test_transmit_cancel(
    void)
{
    static const guint8 d1[] = { 0x01 };
    static const guint8 d2[] = { 0x01, 0x02 };
    static const guint8 d3[] = { 0x01, 0x02, 0x03 };
    static const guint8 d4[] = { 0x01, 0x02, 0x03, 0x04 };
    TestTarget* test = test_target_new();
    NfcTarget* target = &test->target;
    guint id1, id2, id3, id4;

    id1 = nfc_target_transmit(target, d1, sizeof(d1), NULL, NULL, NULL, NULL);
    id2 = nfc_target_transmit(target, d2, sizeof(d2), NULL, NULL, NULL, NULL);
    id3 = nfc_target_transmit(target, d3, sizeof(d3), NULL, NULL, NULL, NULL);
    id4 = nfc_target_transmit(target, d4, sizeof(d4), NULL, NULL, NULL, NULL);
    g_assert(id1);
    g_assert(id2);
    g_assert(id3);
    g_assert(id4);

    g_assert(!nfc_target_cancel_transmit(target, id4 + 1));
    g_assert(nfc_target_cancel_transmit(target, id3));
    g_assert(nfc_target_cancel_transmit(target, id4));
    g_assert(nfc_target_cancel_transmit(target, id1));
    g_assert(nfc_target_cancel_transmit(target, id2));
    g_assert(!nfc_target_cancel_transmit(target, id1));

    /* This is a wrong call but it will be ignored: */
    nfc_target_transmit_done(target, NFC_TRANSMIT_STATUS_OK, NULL, 0);

    nfc_target_unref(target);
}

/*==========================================================================*
 * transmit_destroy
 *==========================================================================*/

static
gboolean
test_transmit_destroy_quit(
    gpointer loop)
{
    GDEBUG("Terminating the loop");
    g_main_loop_quit((GMainLoop*)loop);
    return G_SOURCE_REMOVE;
}

static
void
test_transmit_destroy_resp(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    TestTarget* test = TEST_TARGET(target);

    g_assert(status == NFC_TRANSMIT_STATUS_ERROR);
    g_assert(!len);
    test->failed++;
}

static
void
test_transmit_destroy(
    void)
{
    static const guint8 data1[] = { 0x01 };
    static const guint8 data2[] = { 0x01, 0x02 };
    GUtilData resp1, resp2;
    TestTarget* test = test_target_new();
    NfcTarget* target = &test->target;
    guint id1, id2;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);

    TEST_BYTES_SET(resp1, data1);
    TEST_BYTES_SET(resp2, data2);
    test->transmit_responses = g_slist_append(g_slist_append(
        test->transmit_responses,
        test_transmit_response_new_from_bytes(&resp1)),
        test_transmit_response_new_from_bytes(&resp2));

    id1 = nfc_target_transmit(target, data1, sizeof(data1), NULL,
        test_transmit_destroy_resp, test_clear_bytes, &resp1);
    id2 = nfc_target_transmit(target, data2, sizeof(data2), NULL,
        test_transmit_destroy_resp, test_clear_bytes, &resp2);
    g_assert(id1);
    g_assert(id2);

    g_idle_add_full(G_PRIORITY_HIGH, test_transmit_destroy_quit, loop, NULL);
    test_run(&test_opt, loop);

    g_assert(!test->failed);
    g_assert(resp1.bytes);
    g_assert(resp2.bytes);

    nfc_target_unref(target);
    g_assert(!resp1.bytes);
    g_assert(!resp2.bytes);

    g_main_loop_unref(loop);
}

/*==========================================================================*
 * sequence_basic
 *==========================================================================*/

static
void
test_sequence_basic(
    void)
{
    TestTarget* test = test_target_new();
    NfcTarget* target = &test->target;
    NfcTargetSequence* seq1 = nfc_target_sequence_new(target);
    NfcTargetSequence* seq2 = nfc_target_sequence_new(target);
    NfcTargetSequence* seq3 = nfc_target_sequence_new(target);
    NfcTargetSequence* seq4 = nfc_target_sequence_new(target);
    NfcTargetSequence* seq5 = nfc_target_sequence_new(target);

    g_assert(seq1);
    g_assert(seq2);
    g_assert(seq3);
    g_assert(seq4);
    g_assert(seq5);

    /* Deallocate two sequences before the target and one after */
    nfc_target_sequence_free(seq4);
    nfc_target_sequence_free(seq5);
    nfc_target_sequence_free(seq3);
    nfc_target_sequence_free(seq1);
    nfc_target_unref(target);
    nfc_target_sequence_free(seq2);
}

/*==========================================================================*
 * sequence_ok
 *==========================================================================*/

static
void
test_sequence_ok(
    void)
{
    static const guint8 data1[] = { 0x01 };
    static const guint8 data2[] = { 0x01, 0x02 };
    static const guint8 data3[] = { 0x01, 0x02, 0x03 };
    GUtilData resp1, resp2, resp3;
    TestTarget* test = test_target_new();
    NfcTarget* target = &test->target;
    guint id1, id2, id3, id4;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTargetSequence* seq;
    int sequence_started = 0, sequence_finished = 0;
    gulong id[2];

    nfc_target_set_transmit_timeout(target, 0);
    id[0] = nfc_target_add_sequence_handler(target,
        test_sequence_started, &sequence_started);
    id[1] = nfc_target_add_sequence_handler(target,
        test_sequence_finished, &sequence_finished);

    seq = nfc_target_sequence_new(target);

    TEST_BYTES_SET(resp1, data1);
    TEST_BYTES_SET(resp2, data2);
    TEST_BYTES_SET(resp3, data3);
    test->transmit_responses = g_slist_append(g_slist_append(g_slist_append(
        test->transmit_responses,
        test_transmit_response_new_from_bytes(&resp1)),
        test_transmit_response_new_from_bytes(&resp2)),
        test_transmit_response_new_from_bytes(&resp3));

    g_assert(sequence_started == 1);
    g_assert(sequence_finished == 0);

    /* This one will wait until the next one completes */
    id4 = nfc_target_transmit(target, NULL, 0, NULL, NULL,
        test_quit_loop, loop);

    /* Note: reusing test_sequence_ok_resp() */
    id1 = nfc_target_transmit(target, data1, sizeof(data1), seq,
        test_transmit_ok_resp, test_clear_bytes, &resp1);
    id2 = nfc_target_transmit(target, data2, sizeof(data2), seq,
        test_transmit_ok_resp, test_clear_bytes, &resp2);
    id3 = nfc_target_transmit(target, data3, sizeof(data3), seq,
        test_transmit_ok_resp, test_clear_bytes, &resp3);
    nfc_target_sequence_free(seq);
    g_assert(id1);
    g_assert(id2);
    g_assert(id3);
    g_assert(id4);

    test_run(&test_opt, loop);

    g_assert(sequence_started == 1);
    g_assert(sequence_finished == 1);
    g_assert(test->succeeded == 3);
    g_assert(!resp1.bytes);
    g_assert(!resp2.bytes);

    nfc_target_remove_handlers(target, id, G_N_ELEMENTS(id));
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * sequence2
 *==========================================================================*/

static
void
test_sequence2(
    void)
{
    static const guint8 data1[] = { 0x01 };
    static const guint8 data2[] = { 0x01, 0x02 };
    static const guint8 data3[] = { 0x01, 0x02, 0x03 };
    GUtilData resp1, resp2, resp3;
    TestTarget* test = test_target_new();
    NfcTarget* target = &test->target;
    guint id1, id2, id3, id4;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTargetSequence* seq1;
    NfcTargetSequence* seq2;
    int sequence_started = 0, sequence_finished = 0;
    gulong id[2];

    id[0] = nfc_target_add_sequence_handler(target,
        test_sequence_started, &sequence_started);
    id[1] = nfc_target_add_sequence_handler(target,
        test_sequence_finished, &sequence_finished);

    seq1 = nfc_target_sequence_new(target);
    seq2 = nfc_target_sequence_new(target);

    TEST_BYTES_SET(resp1, data1);
    TEST_BYTES_SET(resp2, data2);
    TEST_BYTES_SET(resp3, data3);
    test->transmit_responses = g_slist_append(g_slist_append(g_slist_append(
        test->transmit_responses,
        test_transmit_response_new_from_bytes(&resp1)),
        test_transmit_response_new_from_bytes(&resp2)),
        test_transmit_response_new_from_bytes(&resp3));

    /* This one will wait until the next one completes */
    id4 = nfc_target_transmit(target, NULL, 0, seq2, NULL,
        test_quit_loop, loop);
    nfc_target_sequence_free(seq2);

    g_assert(sequence_started == 1);
    g_assert(sequence_finished == 0);

    /* Note: reusing test_sequence_ok_resp() */
    id1 = nfc_target_transmit(target, data1, sizeof(data1), seq1,
        test_transmit_ok_resp, test_clear_bytes, &resp1);
    id2 = nfc_target_transmit(target, data2, sizeof(data2), seq1,
        test_transmit_ok_resp, test_clear_bytes, &resp2);
    id3 = nfc_target_transmit(target, data3, sizeof(data3), seq1,
        test_transmit_ok_resp, test_clear_bytes, &resp3);
    nfc_target_sequence_free(seq1);
    g_assert(id1);
    g_assert(id2);
    g_assert(id3);
    g_assert(id4);

    test_run(&test_opt, loop);

    /* Two starts, one finish */
    g_assert(sequence_started == 2);
    g_assert(sequence_finished == 1);
    g_assert(test->succeeded == 3);
    g_assert(!resp1.bytes);
    g_assert(!resp2.bytes);

    nfc_target_remove_handlers(target, id, G_N_ELEMENTS(id));
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * reactivate
 *==========================================================================*/

static
void
test_reactivate(
    void)
{
    TestTarget2* test = test_target2_new();
    NfcTarget* target = NFC_TARGET(test);

    /* Reactivation is supported */
    g_assert(nfc_target_can_reactivate(target));

    /* Immediate reactivation failure */
    test->mode = TEST_REACTIVATE_MODE_FAIL;
    g_assert(nfc_target_can_reactivate(target)); /* At least we can try */
    g_assert(!nfc_target_reactivate(target, NULL, NULL)); /* But fail */

    /* This one succeeds */
    test->mode = TEST_REACTIVATE_MODE_OK;
    g_assert(nfc_target_can_reactivate(target));
    g_assert(nfc_target_reactivate(target, NULL, NULL));

    /* Second one fails because the request has already been submitted */
    g_assert(!nfc_target_can_reactivate(target));
    g_assert(!nfc_target_reactivate(target, NULL, NULL));

    /* And delete it without waiting for reactivation to complete */
    nfc_target_unref(target);
}

/*==========================================================================*
 * reactivate_ok
 *==========================================================================*/

static
void
test_reactivate_ok_done(
    NfcTarget* target,
    void* user_data)
{
    GDEBUG("Reactivation done");
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_reactivate_ok(
    void)
{
    TestTarget2* test = test_target2_new();
    NfcTarget* target = NFC_TARGET(test);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);

    g_assert(nfc_target_can_reactivate(target));
    g_assert(nfc_target_reactivate(target, test_reactivate_ok_done, loop));

    test_run(&test_opt, loop);

    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * reactivate_timeout
 *==========================================================================*/

static
void
test_reactivate_timeout_cb(
    NfcTarget* target,
    void* user_data)
{
    g_assert_not_reached();
}

static
void
test_reactivate_timeout_expired(
    NfcTarget* target,
    void* user_data)
{
    GDEBUG("Reactivation timeout expired");
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_reactivate_timeout(
    void)
{
    TestTarget2* test = test_target2_new();
    NfcTarget* target = NFC_TARGET(test);
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong gone_id = nfc_target_add_gone_handler(target,
        test_reactivate_timeout_expired, loop);

    test->mode = TEST_REACTIVATE_MODE_TIMEOUT;
    nfc_target_set_reactivate_timeout(target, 100); /* Default is quite long */
    g_assert(nfc_target_can_reactivate(target));
    g_assert(nfc_target_reactivate(target, test_reactivate_timeout_cb, NULL));

    test_run(&test_opt, loop);

    nfc_target_remove_handler(target, gone_id);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/target/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("transmit_ok"), test_transmit_ok);
    g_test_add_func(TEST_("transmit_fail"), test_transmit_fail);
    g_test_add_func(TEST_("transmit_cancel"), test_transmit_cancel);
    g_test_add_func(TEST_("transmit_destroy"), test_transmit_destroy);
    g_test_add_func(TEST_("sequence_basic"), test_sequence_basic);
    g_test_add_func(TEST_("sequence_ok"), test_sequence_ok);
    g_test_add_func(TEST_("sequence2"), test_sequence2);
    g_test_add_func(TEST_("reactivate"), test_reactivate);
    g_test_add_func(TEST_("reactivate_ok"), test_reactivate_ok);
    g_test_add_func(TEST_("reactivate_timeout"), test_reactivate_timeout);
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
