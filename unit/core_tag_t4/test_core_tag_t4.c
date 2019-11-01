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

#include "test_common.h"

#include "nfc_ndef.h"
#include "nfc_tag_p.h"
#include "nfc_tag_t4_p.h"
#include "nfc_target_p.h"

static TestOpt test_opt;

static const guint8 test_resp_ok[] = { 0x90, 0x00 };
static const guint8 test_resp_not_found[] = { 0x6a, 0x82 };
static const guint8 test_resp_err[] = { 0x6a, 0x00 };

static const guint8 test_cmd_select_ndef[] = {
    0x00, 0xa4, 0x04, 0x00, 0x07,             /* CLA|INS|P1|P2|Lc  */
    0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, /* Data */
    0x00                                      /* Le */
};  

static
GUtilData*
test_alloc_data(
    const void* bytes,
    guint len)
{
    if (bytes) {
        const gsize total = len + sizeof(GUtilData);
        GUtilData* data = g_malloc(total);

        if (len) {
            void* contents = (void*)(data + 1); 

            data->bytes = contents;
            data->size = len;
            memcpy(contents, bytes, len);
        } else {
            memset(data, 0, sizeof(*data));
        }
        return data;
    }
    return NULL;
}

/*==========================================================================*
 * Test target
 *==========================================================================*/

typedef NfcTargetClass TestTargetClass;
typedef struct test_target {
    NfcTarget target;
    guint transmit_id;
    GPtrArray* cmd_resp;
    int fail_transmit;
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

    g_assert(!self->transmit_id);
    if (!expected && self->fail_transmit) {
        self->fail_transmit--;
        return FALSE;
    } else {
        if (expected) {
            g_assert(expected->size == len);
            g_assert(!memcmp(data, expected->bytes,  len));
            g_free(expected);
        }
        self->transmit_id = g_idle_add(test_target_transmit_done, self);
        return TRUE;
    }
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
    const void* cmd_bytes,
    guint cmd_len,
    const void* resp_bytes,
    guint resp_len)
{
    g_ptr_array_add(self->cmd_resp, test_alloc_data(cmd_bytes, cmd_len));
    g_ptr_array_add(self->cmd_resp, test_alloc_data(resp_bytes, resp_len));
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);

    /* Public interfaces are NULL tolerant */
    g_assert(!nfc_tag_t4a_new(NULL, NULL, NULL));
    g_assert(!nfc_tag_t4b_new(NULL, NULL, NULL));
    g_assert(!nfc_tag_t4a_new(target, NULL, NULL));
    g_assert(!nfc_tag_t4b_new(target, NULL, NULL));
    g_assert(!nfc_isodep_transmit(NULL, 0, 0, 0, 0, NULL, 0,
        NULL, NULL, NULL, NULL));
    nfc_target_unref(target);
}

/*==========================================================================*
 * basic_a
 *==========================================================================*/

static
void
test_basic_exit(
    NfcTag* tag,
    void* user_data)
{
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_basic_a(
    void)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcParamIsoDepPollA iso_dep_poll_a;
    NfcTagType4* t4a;
    NfcTag* tag;
    gulong id;

    memset(&iso_dep_poll_a, 0, sizeof(iso_dep_poll_a));
    iso_dep_poll_a.fsc = 256;
    t4a = NFC_TAG_T4(nfc_tag_t4a_new(target, NULL, &iso_dep_poll_a));
    g_assert(NFC_IS_TAG_T4A(t4a));
    tag = &t4a->tag;

    /* Just let transmit fail... */
    id = nfc_tag_add_initialized_handler(tag, test_basic_exit, loop);
    test_run(&test_opt, loop);
    nfc_tag_remove_handler(tag, id);

    nfc_tag_unref(tag);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * basic_b
 *==========================================================================*/

static
void
test_basic_init(
     const guint8* resp,
     guint resp_len)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcParamPollB poll_b;
    NfcTagType4* t4b;
    NfcTag* tag;
    gulong id;

    test_target_add_cmd(TEST_TARGET(target),
        TEST_ARRAY_AND_SIZE(test_cmd_select_ndef),
        resp, resp_len);

    memset(&poll_b, 0, sizeof(poll_b));
    poll_b.fsc = 0x0b; /* i.e. 256 */
    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, NULL));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = &t4b->tag;

    id = nfc_tag_add_initialized_handler(tag, test_basic_exit, loop);
    test_run(&test_opt, loop);
    nfc_tag_remove_handler(tag, id);

    nfc_tag_unref(tag);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

static
void
test_basic_b(
    void)
{
    test_basic_init(TEST_ARRAY_AND_SIZE(test_resp_not_found));
}

/*==========================================================================*
 * init_err
 *==========================================================================*/

static
void
test_init_err(
    void)
{
    test_basic_init(TEST_ARRAY_AND_SIZE(test_resp_err));
}

/*==========================================================================*
 * init_too_long
 *==========================================================================*/

static
void
test_init_too_long(
    void)
{
    const guint resp_len = 0x10001;
    void* resp = g_malloc0(resp_len);

    test_basic_init(resp, resp_len);
    g_free(resp);
}

/*==========================================================================*
 * init_empty_resp
 *==========================================================================*/

static
void
test_init_empty_resp(
    void)
{
    test_basic_init(test_resp_ok, 0);
}

/*==========================================================================*
 * init_ok
 *==========================================================================*/

static
void
test_init_ok(
    void)
{
    test_basic_init(TEST_ARRAY_AND_SIZE(test_resp_ok));
}

/*==========================================================================*
 * apdu_ok
 *==========================================================================*/

typedef struct test_apdu_data {
    const char* name;
    guint8 cla;
    guint8 ins;
    guint8 p1;
    guint8 p2;
    GUtilData data;
    guint le;
    GUtilData expected;
} TestApduData;

static const guint8 select_mf_expected[] = {
    0x00, 0xa4, 0x00, 0x00
};

static const guint8 read_256_expected[] = {
    0x00, 0xb0, 0x00, 0x00, 0x00
};

static const guint8 read_257_expected[] = {
    0x00, 0xb0, 0x00, 0x00, 0x01, 0x01
};

static const guint8 read_65536_expected[] = {
    0x00, 0xb0, 0x00, 0x00, 0x00, 0x00
};

static const TestApduData apdu_tests[] = {
    { "select_mf", 0x00, 0xa4, 0x00, 0x00, { NULL, 0 }, 0,
      { TEST_ARRAY_AND_SIZE(select_mf_expected) } },
    { "read_256", 0x00, 0xb0, 0x00, 0x00, { NULL, 0 }, 256,
      { TEST_ARRAY_AND_SIZE(read_256_expected) } },
    { "read_257", 0x00, 0xb0, 0x00, 0x00, { NULL, 0 }, 257,
      { TEST_ARRAY_AND_SIZE(read_257_expected) } },
    { "read_65536", 0x00, 0xb0, 0x00, 0x00, { NULL, 0 }, 65536,
      { TEST_ARRAY_AND_SIZE(read_65536_expected) } }
};

typedef struct test_apdu {
    const TestApduData* data;
    GMainLoop* loop;
    gboolean destroyed;
} TestApdu;

static
void
test_apdu_ok_destroy(
    void* data)
{
    TestApdu* test = data;

    test->destroyed = TRUE;
}

static
void
test_apdu_ok_done(
    NfcTagType4* tag,
    guint sw,  /* 16 bits (SW1 << 8)|SW2 */
    const void* data,
    guint len,
    void* user_data)
{
    TestApdu* test = user_data;

    g_assert(sw == ISO_SW_OK);
    g_main_loop_quit(test->loop);
}

static
void
test_apdu_ok_initialized(
    NfcTag* tag,
    void* user_data)
{
    TestApdu* test = user_data;
    const TestApduData* data = test->data;
    const GUtilData* expected = &data->expected;

    test_target_add_cmd(TEST_TARGET(tag->target),
        expected->bytes, expected->size,
        TEST_ARRAY_AND_SIZE(test_resp_ok));

    g_assert(nfc_isodep_transmit(NFC_TAG_T4(tag), data->cla, data->ins,
        data->p1, data->p2, data->data.bytes ? &data->data : NULL, data->le,
        NULL, test_apdu_ok_done, test_apdu_ok_destroy, test));
}

static
void
test_apdu_ok(
    gconstpointer test_data)
{
    TestApdu test;
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcParamPollB poll_b;
    NfcTagType4* t4b;
    NfcTag* tag;
    gulong id;

    memset(&test, 0, sizeof(test));
    test.data = test_data;
    test.loop = g_main_loop_new(NULL, TRUE);

    test_target_add_cmd(TEST_TARGET(target),
        TEST_ARRAY_AND_SIZE(test_cmd_select_ndef),
        TEST_ARRAY_AND_SIZE(test_resp_ok));

    memset(&poll_b, 0, sizeof(poll_b));
    poll_b.fsc = 0x0b; /* i.e. 256 */
    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, NULL));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = &t4b->tag;

    id = nfc_tag_add_initialized_handler(tag, test_apdu_ok_initialized, &test);
    test_run(&test_opt, test.loop);
    g_assert(test.destroyed);
    nfc_tag_remove_handler(tag, id);

    nfc_tag_unref(tag);
    nfc_target_unref(target);
    g_main_loop_unref(test.loop);
}

/*==========================================================================*
 * apdu_fail
 *==========================================================================*/

static
void
test_apdu_fail(
    void)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcParamPollB poll_b;
    NfcTagType4* t4b;
    NfcTag* tag;
    gulong id;

    test_target_add_cmd(TEST_TARGET(target),
        TEST_ARRAY_AND_SIZE(test_cmd_select_ndef),
        TEST_ARRAY_AND_SIZE(test_resp_ok));

    memset(&poll_b, 0, sizeof(poll_b));
    poll_b.fsc = 0x0b; /* i.e. 256 */
    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, NULL));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = &t4b->tag;

    id = nfc_tag_add_initialized_handler(tag, test_basic_exit, loop);
    test_run(&test_opt, loop);
    nfc_tag_remove_handler(tag, id);

    /* Invalid Le */
    g_assert(!nfc_isodep_transmit(t4b, 0x00, 0xb0, 0x00, 0x00, NULL, 0x10001,
        NULL, NULL, NULL, NULL));

    /* Lower level failure (Le is OK this time) */
    TEST_TARGET(target)->fail_transmit++;
    g_assert(!nfc_isodep_transmit(t4b, 0x00, 0xb0, 0x00, 0x00, NULL, 0x100,
        NULL, NULL, NULL, NULL));

    nfc_tag_unref(tag);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/tag_t4/" name

int main(int argc, char* argv[])
{
    guint i;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic_a"), test_basic_a);
    g_test_add_func(TEST_("basic_b"), test_basic_b);
    g_test_add_func(TEST_("init_err"), test_init_err);
    g_test_add_func(TEST_("init_too_long"), test_init_too_long);
    g_test_add_func(TEST_("init_empty_resp"), test_init_empty_resp);
    g_test_add_func(TEST_("init_ok"), test_init_ok);
    g_test_add_func(TEST_("apdu_fail"), test_apdu_fail);
    for (i = 0; i < G_N_ELEMENTS(apdu_tests); i++) {
        const TestApduData* test = apdu_tests + i;
        char* path = g_strconcat(TEST_("apdu_ok/"), test->name, NULL);

        g_test_add_data_func(path, test, test_apdu_ok);
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
