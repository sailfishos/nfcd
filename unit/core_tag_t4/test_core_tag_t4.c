/*
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2019-2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2020 Open Mobile Platform LLC.
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
#include "test_target.h"

#include "nfc_ndef.h"
#include "nfc_tag_p.h"
#include "nfc_tag_t4_p.h"
#include "nfc_target_impl.h"

#include <gutil_log.h>

static TestOpt test_opt;

static const guint8 test_resp_ok[] = { 0x90, 0x00 };
static const guint8 test_resp_not_found[] = { 0x6a, 0x82 };
static const guint8 test_resp_err[] = { 0x6a, 0x00 };
static const guint8 test_cmd_select_ndef_app[] = {
    0x00, 0xa4, 0x04, 0x00, 0x07,             /* CLA|INS|P1|P2|Lc  */
    0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, /* Data */
    0x00                                      /* Le */
};
static const guint8 test_cmd_select_ndef_cc[] = {
    0x00, 0xa4, 0x00, 0x0c, 0x02,             /* CLA|INS|P1|P2|Lc  */
    0xe1, 0x03                                /* Data */
};
static const guint8 test_cmd_read_ndef_cc[] = {
    0x00, 0xb0, 0x00, 0x00, 0x0f              /* CLA|INS|P1|P2|Le  */
};
static const guint8 test_resp_read_ndef_cc[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x04, 0x06, 0xe1, 0x04, 0x0f, 0xff, 0x00,
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_v3[] = {
    0x00, 0x0f, 0x30, 0x00, 0x3b, 0x00, 0x34, /* Data */
    /*            ^ version 3                */
    0x04, 0x06, 0xe1, 0x04, 0x0f, 0xff, 0x00,
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_short_mle[] = {
    0x00, 0x0f, 0x20, 0x00, 0x00, 0x00, 0x34, /* Data */
    /*        short MLe ^^    ^^             */
    0x04, 0x06, 0xe1, 0x04, 0x0f, 0xff, 0x00,
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_no_access[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x04, 0x06, 0xe1, 0x04, 0x0f, 0xff, 0xff,
    /*                     no read access ^^ */
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_invalid_t[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x03, 0x06, 0xe1, 0x04, 0x0f, 0xff, 0x00,
    /* ^ invalid T                           */
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_invalid_l[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x04, 0x05, 0xe1, 0x04, 0x0f, 0xff, 0x00,
    /*       ^ invalid L                     */
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_invalid_fid_1[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x04, 0x06, 0x00, 0x00, 0x0f, 0xff, 0x00,
    /*            ^^    ^^                   */
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_invalid_fid_2[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x04, 0x06, 0xe1, 0x02, 0x0f, 0xff, 0x00,
    /*            ^^    ^^                   */
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_invalid_fid_3[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x04, 0x06, 0xe1, 0x03, 0x0f, 0xff, 0x00,
    /*            ^^    ^^                   */
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_invalid_fid_4[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x04, 0x06, 0x3f, 0x00, 0x0f, 0xff, 0x00,
    /*            ^^    ^^                   */
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_cc_invalid_fid_5[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, /* Data */
    0x04, 0x06, 0x3f, 0xff, 0x0f, 0xff, 0x00,
    /*            ^^    ^^                   */
    0xff,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_cmd_select_ndef_ef[] = {
    0x00, 0xa4, 0x00, 0x0c, 0x02,             /* CLA|INS|P1|P2|Lc  */
    0xe1, 0x04                                /* Data */
};
static const guint8 test_cmd_read_ndef_len[] = {
    0x00, 0xb0, 0x00, 0x00, 0x02              /* CLA|INS|P1|P2|Le  */
};
static const guint8 test_resp_read_ndef_len[] = {
    0x00, 0x42,                               /* Data */
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_len_zero[] = {
    0x00, 0x00,                               /* Data */
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_resp_read_ndef_len_wrong[] = {
    0x00,                                     /* Data */
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_cmd_read_ndef_1[] = {
    0x00, 0xb0, 0x00, 0x02, 0x3b              /* CLA|INS|P1|P2|Le  */
};
static const guint8 test_resp_read_ndef_1[] = {
    0xd1, 0x01, 0x3e, 0x54, 0x02, 0x65, 0x6e, /* Data */
    0x54, 0x65, 0x73, 0x74, 0x20, 0x74, 0x65,
    0x73, 0x74, 0x20, 0x74, 0x65, 0x73, 0x74,
    0x20, 0x74, 0x65, 0x73, 0x74, 0x20, 0x74,
    0x65, 0x73, 0x74, 0x20, 0x74, 0x65, 0x73,
    0x74, 0x20, 0x74, 0x65, 0x73, 0x74, 0x20,
    0x74, 0x65, 0x73, 0x74, 0x20, 0x74, 0x65,
    0x73, 0x74, 0x20, 0x74, 0x65, 0x73, 0x74,
    0x20, 0x74, 0x65,
    0x90, 0x00                                /* SW1|SW2 */
};
static const guint8 test_cmd_read_ndef_2[] = {
    0x00, 0xb0, 0x00, 0x3d, 0x07              /* CLA|INS|P1|P2|Le  */
};
static const guint8 test_resp_read_ndef_2[] = {
    0x73, 0x74, 0x20, 0x74, 0x65, 0x73, 0x74, /* Data */
    0x90, 0x00                                /* SW1|SW2 */
};

static
void
test_tag_quit_loop_cb(
    NfcTag* tag,
    void* user_data)
{
    g_main_loop_quit((GMainLoop*)user_data);
}

/*==========================================================================*
 * Test target with reactivate
 *==========================================================================*/

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
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcParamIsoDepPollA iso_dep_poll_a;
    NfcTagType4* t4a;
    NfcTag* tag;

    memset(&iso_dep_poll_a, 0, sizeof(iso_dep_poll_a));
    iso_dep_poll_a.fsc = 256;
    t4a = NFC_TAG_T4(nfc_tag_t4a_new(target, NULL, &iso_dep_poll_a));
    g_assert(NFC_IS_TAG_T4A(t4a));
    tag = &t4a->tag;

    /* If the target doesn't support reactivation, tag gets initialized
     * right away (and obviously there won't be any NDEF) */
    g_assert(tag->flags & NFC_TAG_FLAG_INITIALIZED);
    g_assert(!tag->ndef);

    nfc_tag_unref(tag);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

static
void
test_basic_a(
    void)
{
    static const guint8 t1[] = {0x01, 0x02, 0x03, 0x04};
    static const GUtilData t1_data = { TEST_ARRAY_AND_SIZE(t1) };

    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcParamPollA poll_a;
    NfcParamIsoDepPollA iso_dep_poll_a;
    NfcTagType4* t4a;
    NfcTag* tag;

    memset(&poll_a, 0, sizeof(poll_a));
    memset(&iso_dep_poll_a, 0, sizeof(iso_dep_poll_a));
    iso_dep_poll_a.fsc = 256;
    target->technology = NFC_TECHNOLOGY_A;

    t4a = NFC_TAG_T4(nfc_tag_t4a_new(target, NULL, &iso_dep_poll_a));
    g_assert(NFC_IS_TAG_T4A(t4a));
    tag = &t4a->tag;
    nfc_tag_unref(tag);

    /* Handle case with Historical Bytes present */
    iso_dep_poll_a.t1 = t1_data;
    t4a = NFC_TAG_T4(nfc_tag_t4a_new(target, NULL, &iso_dep_poll_a));
    g_assert(NFC_IS_TAG_T4A(t4a));
    tag = &t4a->tag;
    nfc_tag_unref(tag);

    /* Handle case with Poll parameter present */
    t4a = NFC_TAG_T4(nfc_tag_t4a_new(target, &poll_a, &iso_dep_poll_a));
    g_assert(NFC_IS_TAG_T4A(t4a));
    tag = &t4a->tag;
    nfc_tag_unref(tag);

    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

static
void
test_basic_b(
    void)
{
    static const guint8 hlr[] = {0x01, 0x02, 0x03, 0x04};
    static const GUtilData hlr_data = { TEST_ARRAY_AND_SIZE(hlr) };

    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);
    NfcParamPollB poll_b;
    NfcParamIsoDepPollB iso_dep_poll_b;
    NfcTagType4* t4b;
    NfcTag* tag;

    memset(&poll_b, 0, sizeof(poll_b));
    poll_b.fsc = 256;
    memset(&iso_dep_poll_b, 0, sizeof(iso_dep_poll_b));
    target->technology = NFC_TECHNOLOGY_B;

    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, &iso_dep_poll_b));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = &t4b->tag;
    nfc_tag_unref(tag);

    /* Handle case with HILR present */
    iso_dep_poll_b.hlr = hlr_data;
    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, &iso_dep_poll_b));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = &t4b->tag;
    nfc_tag_unref(tag);

    /* Handle case with no ISO-DEP param */
    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, NULL));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = &t4b->tag;
    nfc_tag_unref(tag);

    nfc_target_unref(target);
    g_main_loop_unref(loop);
}


/*==========================================================================*
 * init_seq
 *==========================================================================*/

typedef struct test_init_data {
    const char* name;
    const GUtilData* cmd_resp;
    gsize count;
    int fail_transmit;
    guint flags;

#define TEST_INIT_NDEF (0x01)
#define TEST_INIT_FAIL_REACT (0x02)

} TestInitData;

static const GUtilData test_init_data_app_not_found[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_not_found) }
};

static const GUtilData test_init_data_app_select_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_err) }
};

static const guint8 test_resp_too_long[0x10001] = { 0 };
static const GUtilData test_init_data_app_select_resp_too_long[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_too_long) }
};

static const GUtilData test_init_data_app_select_resp_empty[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { test_resp_ok, 0 }
};

static const GUtilData test_init_data_cc_not_found[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_not_found) }
};

static const GUtilData test_init_data_cc_select_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_err) }
};

static const GUtilData test_init_data_cc_select_io_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) }
    /* Missing response becomes an I/O error */
};

static const GUtilData test_init_data_cc_short_read[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) }
};

static const GUtilData test_init_data_cc_read_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_err) }
};

static const GUtilData test_init_data_cc_read_io_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) }
    /* Missing response becomes an I/O error */
};

static const GUtilData test_init_data_cc_v3[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_v3) }
};

static const GUtilData test_init_data_cc_short_mle[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_short_mle) }
};

static const GUtilData test_init_data_cc_no_access[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_no_access) }
};

static const GUtilData test_init_data_cc_invalid_t[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_invalid_t) }
};

static const GUtilData test_init_data_cc_invalid_l[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_invalid_l) }
};

static const GUtilData test_init_data_cc_invalid_fid_1[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_invalid_fid_1) }
};

static const GUtilData test_init_data_cc_invalid_fid_2[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_invalid_fid_2) }
};

static const GUtilData test_init_data_cc_invalid_fid_3[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_invalid_fid_3) }
};

static const GUtilData test_init_data_cc_invalid_fid_4[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_invalid_fid_4) }
};

static const GUtilData test_init_data_cc_invalid_fid_5[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc_invalid_fid_5) }
};

static const GUtilData test_init_data_ndef_not_found[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_not_found) }
};

static const GUtilData test_init_data_ndef_select_io_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) }
    /* Missing response becomes an I/O error */
};

static const GUtilData test_init_data_ndef_read_len_zero[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_len_zero) }
};

static const GUtilData test_init_data_ndef_read_len_wrong[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_len_wrong) }
};

static const GUtilData test_init_data_ndef_read_len_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_resp_err) }
};

static const GUtilData test_init_data_ndef_read_len_io_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_len) }
    /* Missing response becomes an I/O error */
};

static const GUtilData test_init_data_ndef_read_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_1) },
    { TEST_ARRAY_AND_SIZE(test_resp_err) }
};

static const GUtilData test_init_data_ndef_read_io_err[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_1) },
    /* Missing response becomes an I/O error */
};

static const GUtilData test_init_data_ndef_short[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_1) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) }
};

#define test_init_data_app_select_submit_failure test_init_data_success
#define test_init_data_cc_select_submit_error test_init_data_success
#define test_init_data_cc_read_submit_error test_init_data_success
#define test_init_data_ndef_select_submit_error test_init_data_success
#define test_init_data_ndef_read_submit_error1 test_init_data_success
#define test_init_data_ndef_read_submit_error2 test_init_data_success
#define test_init_data_ndef_read_submit_error3 test_init_data_success
#define test_init_data_success_no_react test_init_data_success
static const GUtilData test_init_data_success[] = {
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_cc) },
    { TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_ef) },
    { TEST_ARRAY_AND_SIZE(test_resp_ok) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_len) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_1) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_1) },
    { TEST_ARRAY_AND_SIZE(test_cmd_read_ndef_2) },
    { TEST_ARRAY_AND_SIZE(test_resp_read_ndef_2) }
};

static const TestInitData init_tests[] = {
#define TEST_INIT(x,y,z) {#x, TEST_ARRAY_AND_COUNT(test_init_data_##x), y, z}
    TEST_INIT(app_not_found, 0, 0),
    TEST_INIT(app_select_err, 0, 0),
    TEST_INIT(app_select_resp_too_long, 0, 0),
    TEST_INIT(app_select_resp_empty, 0, 0),
    TEST_INIT(cc_not_found, 0, 0),
    TEST_INIT(cc_select_err, 0, 0),
    TEST_INIT(cc_select_io_err, 0, 0),
    TEST_INIT(cc_short_read, 0, 0),
    TEST_INIT(cc_read_err, 0, 0),
    TEST_INIT(cc_read_io_err, 0, 0),
    TEST_INIT(cc_v3, 0, 0),
    TEST_INIT(cc_short_mle, 0, 0),
    TEST_INIT(cc_no_access, 0, 0),
    TEST_INIT(cc_invalid_t, 0, 0),
    TEST_INIT(cc_invalid_l, 0, 0),
    TEST_INIT(cc_invalid_fid_1, 0, 0),
    TEST_INIT(cc_invalid_fid_2, 0, 0),
    TEST_INIT(cc_invalid_fid_3, 0, 0),
    TEST_INIT(cc_invalid_fid_4, 0, 0),
    TEST_INIT(cc_invalid_fid_5, 0, 0),
    TEST_INIT(ndef_not_found, 0, 0),
    TEST_INIT(ndef_select_io_err, 0, 0),
    TEST_INIT(ndef_read_len_zero, 0, 0),
    TEST_INIT(ndef_read_len_wrong, 0, 0),
    TEST_INIT(ndef_read_len_err, 0, 0),
    TEST_INIT(ndef_read_len_io_err, 0, 0),
    TEST_INIT(ndef_read_err, 0, 0),
    TEST_INIT(ndef_read_io_err, 0, 0),
    TEST_INIT(ndef_short, 0, 0),
    TEST_INIT(app_select_submit_failure, 1, 0),
    TEST_INIT(cc_select_submit_error, 2, 0),
    TEST_INIT(cc_read_submit_error, 3, 0),
    TEST_INIT(ndef_select_submit_error, 4, 0),
    TEST_INIT(ndef_read_submit_error1, 5, 0),
    TEST_INIT(ndef_read_submit_error2, 6, 0),
    TEST_INIT(ndef_read_submit_error3, 7, 0),
    TEST_INIT(success, 0, TEST_INIT_NDEF),
    TEST_INIT(success_no_react, 0, TEST_INIT_NDEF | TEST_INIT_FAIL_REACT)
};

static
void
test_init_seq(
     gconstpointer test_data)
{
    const TestInitData* test = test_data;
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET2, NULL);
    TestTarget2* test_target2 = TEST_TARGET2(target);
    TestTarget* test_target = TEST_TARGET(target);
    NfcParamPollB poll_b;
    NfcTagType4* t4b;
    NfcTag* tag;
    guint i;

    for (i = 0; i < test->count; i++) {
        g_ptr_array_add(test_target->cmd_resp,
            test_clone_data(test->cmd_resp + i));
    }

    test_target->fail_transmit = test->fail_transmit;
    test_target2->fail_reactivate = (test->flags & TEST_INIT_FAIL_REACT) != 0;

    memset(&poll_b, 0, sizeof(poll_b));
    poll_b.fsc = 0x0b; /* i.e. 256 */
    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, NULL));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = &t4b->tag;

    /* Run the initialization sequence if not initialized yet */
    if (!(tag->flags & NFC_TAG_FLAG_INITIALIZED)) {
        GMainLoop* loop = g_main_loop_new(NULL, TRUE);
        const gulong id = nfc_tag_add_initialized_handler(tag,
            test_tag_quit_loop_cb, loop);

        test_run(&test_opt, loop);
        nfc_tag_remove_handler(tag, id);
        g_main_loop_unref(loop);

        /* Now it must be initialized  */
        g_assert(tag->flags & NFC_TAG_FLAG_INITIALIZED);
    }

    /* Check if are supposed to have NDEF */
    g_assert(!tag->ndef == !(test->flags & TEST_INIT_NDEF));

    nfc_tag_unref(tag);
    nfc_target_unref(target);
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

static const guint8 mf_path[] = {
    0x3f, 0x00
};

static const guint8 select_mf_expected[] = {
    0x00, 0xa4, 0x00, 0x00
};

static const guint8 select_mf_full_expected[] = {
    0x00, 0xa4, 0x00, 0x00, 0x02, 0x3f, 0x00
};

static const guint8 read_256_expected[] = {
    0x00, 0xb0, 0x00, 0x00, 0x00
};

static const guint8 read_257_expected[] = {
    0x00, 0xb0, 0x00, 0x00, 0x00, 0x01, 0x01
};

static const guint8 read_65536_expected[] = {
    0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const TestApduData apdu_tests[] = {
    { "select_mf", 0x00, 0xa4, 0x00, 0x00, { NULL, 0 }, 0,
      { TEST_ARRAY_AND_SIZE(select_mf_expected) } },
    { "select_mf_full", 0x00, 0xa4, 0x00, 0x00,
      { TEST_ARRAY_AND_SIZE(mf_path) }, 0,
      { TEST_ARRAY_AND_SIZE(select_mf_full_expected) } },
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
test_apdu_ok(
    gconstpointer test_data)
{
    const TestApduData* data = test_data;
    const GUtilData* expected = &data->expected;
    NfcTarget* target = test_target_new_tech_with_data(NFC_TECHNOLOGY_B,
        expected->bytes, expected->size, TEST_ARRAY_AND_SIZE(test_resp_ok));
    NfcParamPollB poll_b;
    NfcTagType4* t4b;
    NfcTag* tag;
    TestApdu test;

    memset(&test, 0, sizeof(test));
    test.data = data;
    test.loop = g_main_loop_new(NULL, TRUE);

    memset(&poll_b, 0, sizeof(poll_b));
    poll_b.fsc = 0x0b; /* i.e. 256 */
    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, NULL));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = &t4b->tag;

    /* Target doesn't support reactivation, tag gets initialized right away */
    g_assert(tag->flags & NFC_TAG_FLAG_INITIALIZED);

    /* Submit and validate APDU */
    g_assert(nfc_isodep_transmit(t4b, data->cla, data->ins,
        data->p1, data->p2, data->data.bytes ? &data->data : NULL, data->le,
        NULL, test_apdu_ok_done, test_apdu_ok_destroy, &test));

    test_run(&test_opt, test.loop);
    g_assert(test.destroyed);

    nfc_tag_unref(tag);
    nfc_target_unref(target);
    g_main_loop_unref(test.loop);
}

/*==========================================================================*
 * apdu_fail
 *==========================================================================*/

static
void
test_apdu_fail_done(
    NfcTagType4* tag,
    guint sw,  /* 16 bits (SW1 << 8)|SW2 */
    const void* data,
    guint len,
    void* user_data)
{
    g_assert(sw == ISO_SW_IO_ERR);
    g_main_loop_quit(user_data);
}

static
void
test_apdu_fail(
    void)
{
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET2, NULL);
    NfcParamPollB poll_b;
    NfcTagType4* t4b;
    NfcTag* tag;
    guint8 zero = 0;
    gulong id;

    /* Command-response pair for missing NDEF application */
    test_target_add_data(target,
        TEST_ARRAY_AND_SIZE(test_cmd_select_ndef_app),
        TEST_ARRAY_AND_SIZE(test_resp_not_found));

    memset(&poll_b, 0, sizeof(poll_b));
    poll_b.fsc = 0x0b; /* i.e. 256 */
    t4b = NFC_TAG_T4(nfc_tag_t4b_new(target, &poll_b, NULL));
    g_assert(NFC_IS_TAG_T4B(t4b));
    tag = NFC_TAG(t4b);

    /* Not initialized yet  */
    g_assert(!(tag->flags & NFC_TAG_FLAG_INITIALIZED));

    /* Run the initialization sequence */
    id = nfc_tag_add_initialized_handler(tag, test_tag_quit_loop_cb, loop);
    test_run(&test_opt, loop);
    nfc_tag_remove_handler(tag, id);

    /* Now it must be initialized  */
    g_assert(tag->flags & NFC_TAG_FLAG_INITIALIZED);

    /* Invalid Le */
    g_assert(!nfc_isodep_transmit(t4b, 0x00, 0xb0, 0x00, 0x00, NULL, 0x10001,
        NULL, NULL, NULL, NULL));

    /* Lower level failure (Le is OK this time) */
    TEST_TARGET(target)->fail_transmit++;
    g_assert(!nfc_isodep_transmit(t4b, 0x00, 0xb0, 0x00, 0x00, NULL, 0x100,
        NULL, NULL, NULL, NULL));

    /* Transmission failure */
    g_assert(nfc_isodep_transmit(t4b, 0x00, 0xb0, 0x00, 0x00, NULL, 0x100,
        NULL, test_apdu_fail_done, NULL, loop));
    test_run(&test_opt, loop);

    /* Short response */
    test_target_add_data(tag->target,
        TEST_ARRAY_AND_SIZE(select_mf_expected), &zero, 1);
    g_assert(nfc_isodep_transmit(t4b, 0x00, 0xa4, 0x00, 0x00, NULL, 0,
        NULL, test_apdu_fail_done, NULL, loop));
    test_run(&test_opt, loop);

    nfc_tag_unref(tag);
    nfc_target_unref(target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Encode
 *==========================================================================*/

static
void
test_encode_fail(
    void)
{
    GByteArray* a = g_byte_array_new();

    /* Invalid Lc (pointer is ignored) */
    g_assert(!nfc_tag_t4_build_apdu(a, 0, 0, 0, 0, 65536, NULL, 0));

    /* Invalid Le */
    g_assert(!nfc_tag_t4_build_apdu(a, 0, 0, 0, 0, 0, NULL, 65537));

    g_byte_array_free(a, TRUE);
}

/* Case 1:  |CLA|INS|P1|P2|
 * Lc = 0, Le = 0, n = 4 */
static
void
test_encode_case1(
    void)
{                                  /* CLA   INS    P1    P2 */
    static const guint8 expect[] = { 0x01, 0x02, 0x03, 0x04 };
    GByteArray* a = g_byte_array_new();

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 0, NULL, 0));
    g_assert_cmpuint(a->len, == ,sizeof(expect));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(expect)));
    g_byte_array_free(a, TRUE);
}

/* Case 2s: |CLA|INS|P1|P2|LE|
 * Lc = 0, Le = 1..256, n = 5) */
static
void
test_encode_case2s(
    void)
{                                  /* CLA   INS    P1    P2    LE */
    static const guint8 le_1[]   = { 0x01, 0x02, 0x03, 0x04, 0x01 };
    static const guint8 le_255[] = { 0x01, 0x02, 0x03, 0x04, 0xff };
    static const guint8 le_256[] = { 0x01, 0x02, 0x03, 0x04, 0x00 };
    GByteArray* a = g_byte_array_new();

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 0, NULL, 1));
    g_assert_cmpuint(a->len, == ,sizeof(le_1));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(le_1)));

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 0, NULL, 255));
    g_assert_cmpuint(a->len, == ,sizeof(le_255));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(le_255)));

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 0, NULL, 256));
    g_assert_cmpuint(a->len, == ,sizeof(le_256));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(le_256)));

    g_byte_array_free(a, TRUE);
}

/* Case 2e: |CLA|INS|P1|P2|00|LE1|LE2|
 * Lc = 0, Le = 257..65536, n = 7 */
static
void
test_encode_case2e(
    void)
{
    static const guint8 le_257[] =
        /* CLA   INS    P1    P2    00   LE1   LE2 */
        { 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0x01 };
    static const guint8 le_65535[] =
        { 0x01, 0x02, 0x03, 0x04, 0x00, 0xff, 0xff };
    static const guint8 le_65536[] =
        { 0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00 };
    GByteArray* a = g_byte_array_new();

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 0, NULL, 257));
    g_assert_cmpuint(a->len, == ,sizeof(le_257));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(le_257)));

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 0, NULL, 65535));
    g_assert_cmpuint(a->len, == ,sizeof(le_65535));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(le_65535)));

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 0, NULL, 65536));
    g_assert_cmpuint(a->len, == ,sizeof(le_65536));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(le_65536)));

    g_byte_array_free(a, TRUE);
}

/* Case 3s: |CLA|INS|P1|P2|LC|...BODY...|
 * Lc = 1..255, n = 6..260 */
static
void
test_encode_case3s(
    void)
{
    static const guint8 lc_2[] =
        /* CLA   INS    P1    P2    LC  BODY... */
        { 0x01, 0x02, 0x03, 0x04, 0x02, 0x05, 0x06 };
    static const guint8 lc_255[] =
        { 0x01, 0x02, 0x03, 0x04, 0xff,
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
          0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe };
    GByteArray* a = g_byte_array_new();

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 2, lc_2+5, 0));
    g_assert_cmpuint(a->len, == ,sizeof(lc_2));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(lc_2)));

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 255,lc_255+5,0));
    g_assert_cmpuint(a->len, == ,sizeof(lc_255));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(lc_255)));

    g_byte_array_free(a, TRUE);
}

/* Case 3e: |CLA|INS|P1|P2|00|LC1|LC2|...BODY...|
 * Lc = 1..65535, n = 8..65542 */
static
void
test_encode_case3e(
    void)
{
    static const guint8 lc_256[] =
        /* CLA   INS    P1    P2    00  LC1    LC2  BODY... */
        { 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0x00,
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
          0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff };
    GByteArray* a = g_byte_array_new();

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04, 256,lc_256+7,0));
    g_assert_cmpuint(a->len, == ,sizeof(lc_256));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(lc_256)));

    g_byte_array_free(a, TRUE);
}

/* Case 4s: |CLA|INS|P1|P2|LC|...BODY...|LE |
 * Lc = 1..255, Le = 1..256, n = 7..261 */
static
void
test_encode_case4s(
    void)
{
    static const guint8 lc_1_le_1[] =
        /* CLA   INS    P1    P2    LC   BODY    LE */
        { 0x01, 0x02, 0x03, 0x04, 0x01,  0x05, 0x01 };
    static const guint8 lc_255_le_2[] =
        { 0x01, 0x02, 0x03, 0x04, 0xff,
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
          0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe,
          0x02 /* Le */ };
    GByteArray* a = g_byte_array_new();

    g_assert(nfc_tag_t4_build_apdu(a,0x01,0x02,0x03,0x04,1,lc_1_le_1+5,1));
    g_assert_cmpuint(a->len, == ,sizeof(lc_1_le_1));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(lc_1_le_1)));

    g_assert(nfc_tag_t4_build_apdu(a,0x01,0x02,0x03,0x04,255,lc_255_le_2+5,2));
    g_assert_cmpuint(a->len, == ,sizeof(lc_255_le_2));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(lc_255_le_2)));

    g_byte_array_free(a, TRUE);
}

/* Case 4e: |CLA|INS|P1|P2|00|LC1|LC2|...BODY...|LE1|LE2|
 * Lc = 1..65535, Le = 1..65536, n = 10..65544 */
static
void
test_encode_case4e(
    void)
{
    static const guint8 lc_256_le_1[] =
        /* CLA   INS    P1    P2    00   LC1   LC2  BODY...  LE1 LE2 */
        { 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0x00,
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
          0x00, 0x01 /* Le */ };
    static const guint8 lc_256_le_256[] =
        /* CLA   INS    P1    P2    00   LC1   LC2  BODY...  LE1 LE2 */
        { 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0x00,
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
          0x01, 0x00 /* Le */ };
    GByteArray* a = g_byte_array_new();

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04,
        256, lc_256_le_1 + 7, 1));
    g_assert_cmpuint(a->len, == ,sizeof(lc_256_le_1));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(lc_256_le_1)));

    g_assert(nfc_tag_t4_build_apdu(a, 0x01, 0x02, 0x03, 0x04,
        256, lc_256_le_256 + 7, 256));
    g_assert_cmpuint(a->len, == ,sizeof(lc_256_le_256));
    g_assert(!memcmp(a->data, TEST_ARRAY_AND_SIZE(lc_256_le_256)));

    g_byte_array_free(a, TRUE);
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
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("basic_a"), test_basic_a);
    g_test_add_func(TEST_("basic_b"), test_basic_b);
    for (i = 0; i < G_N_ELEMENTS(init_tests); i++) {
        const TestInitData* test = init_tests + i;
        char* path = g_strconcat(TEST_("init_seq/"), test->name, NULL);

        g_test_add_data_func(path, test, test_init_seq);
        g_free(path);
    }
    for (i = 0; i < G_N_ELEMENTS(apdu_tests); i++) {
        const TestApduData* test = apdu_tests + i;
        char* path = g_strconcat(TEST_("apdu_ok/"), test->name, NULL);

        g_test_add_data_func(path, test, test_apdu_ok);
        g_free(path);
    }
    g_test_add_func(TEST_("apdu_fail"), test_apdu_fail);
    g_test_add_func(TEST_("encode/fail"), test_encode_fail);
    g_test_add_func(TEST_("encode/case1"), test_encode_case1);
    g_test_add_func(TEST_("encode/case2s"), test_encode_case2s);
    g_test_add_func(TEST_("encode/case2e"), test_encode_case2e);
    g_test_add_func(TEST_("encode/case3s"), test_encode_case3s);
    g_test_add_func(TEST_("encode/case3e"), test_encode_case3e);
    g_test_add_func(TEST_("encode/case4s"), test_encode_case4s);
    g_test_add_func(TEST_("encode/case4e"), test_encode_case4e);
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
