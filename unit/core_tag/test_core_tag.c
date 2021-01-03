/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_tag_p.h"
#include "nfc_target_p.h"
#include "nfc_target_impl.h"

#include "test_common.h"
#include "test_target.h"

#include <gutil_log.h>

static TestOpt test_opt;

static
void
test_tag_inc(
    NfcTag* tag,
    void* user_data)
{
    (*(int*)user_data)++;
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
    g_assert(!nfc_tag_ref(NULL));
    g_assert(!nfc_tag_param(NULL));
    g_assert(!nfc_tag_add_initialized_handler(NULL, NULL, NULL));
    g_assert(!nfc_tag_add_gone_handler(NULL, NULL, NULL));
    nfc_tag_remove_handler(NULL, 0);
    nfc_tag_remove_handlers(NULL, NULL, 0);
    nfc_tag_unref(NULL);
    nfc_tag_deactivate(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    NfcTag* tag = g_object_new(NFC_TYPE_TAG, NULL);
    NfcTarget* target = test_target_new();
    NfcParamPoll poll;
    const char* name = "test";
    int init_count = 0;
    int gone_count = 0;
    gulong init_id;
    gulong gone_id;

    memset(&poll, 0, sizeof(poll));
    nfc_tag_init_base(tag, target, &poll);
    g_assert(tag->target == target);
    g_assert(tag->present == TRUE);
    g_assert(!nfc_tag_param(tag)); /* No params for NFC_TECHNOLOGY_UNKNOWN */

    g_assert(!tag->name);
    nfc_tag_set_name(tag, name);
    g_assert(!g_strcmp0(tag->name, name));

    g_assert(!nfc_tag_add_initialized_handler(tag, NULL, NULL));
    init_id = nfc_tag_add_initialized_handler(tag, test_tag_inc, &init_count);
    g_assert(init_id);

    g_assert(!nfc_tag_add_gone_handler(tag, NULL, NULL));
    gone_id = nfc_tag_add_gone_handler(tag, test_tag_inc, &gone_count);
    g_assert(gone_id);

    /* "initialized" signal is only issued once */
    nfc_tag_set_initialized(tag);
    g_assert(init_count == 1);
    nfc_tag_set_initialized(tag);
    g_assert(init_count == 1);

    /* Deactivate call is just passed to target */
    nfc_tag_deactivate(tag);
    g_assert(!tag->present);
    g_assert(gone_count == 1);

    /* "gone" is also a one-time signal*/
    nfc_target_gone(target);
    g_assert(gone_count == 1);
    g_assert(!tag->present);

    nfc_tag_remove_handler(tag, 0);
    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_remove_handler(tag, gone_id);

    g_assert(nfc_tag_ref(tag) == tag);
    nfc_tag_unref(tag);
    nfc_tag_unref(tag);
    nfc_target_unref(target);
}

/*==========================================================================*
 * basic_a
 *==========================================================================*/

static
void
test_basic_a(
    void)
{
    static const guint8 nfcid1[] = {0x04, 0xbd, 0xfa, 0x4a, 0xeb, 0x2b, 0x80};
    static const GUtilData nfcid1_data = { TEST_ARRAY_AND_SIZE(nfcid1) };

    NfcTag* tag = g_object_new(NFC_TYPE_TAG, NULL);
    NfcTarget* target = test_target_new_tech(NFC_TECHNOLOGY_A);
    NfcParamPoll poll;
    const NfcParamPollA* poll_a;

    memset(&poll, 0, sizeof(poll));
    poll.a.nfcid1 = nfcid1_data;
    nfc_tag_init_base(tag, target, &poll);
    g_assert(tag->target == target);
    g_assert(tag->present == TRUE);
    poll_a = &nfc_tag_param(tag)->a;
    g_assert(poll_a);
    g_assert(poll_a->nfcid1.size == sizeof(nfcid1));
    g_assert(!memcmp(poll_a->nfcid1.bytes, nfcid1, sizeof(nfcid1)));
    nfc_tag_unref(tag);

    /* Make sure NULL nfcid1 is handled */
    tag = g_object_new(NFC_TYPE_TAG, NULL);
    memset(&poll, 0, sizeof(poll));
    nfc_tag_init_base(tag, target, &poll);
    poll_a = &nfc_tag_param(tag)->a;
    g_assert(poll_a);
    g_assert_cmpuint(poll_a->nfcid1.size, == ,0);
    g_assert(!poll_a->nfcid1.bytes);
    nfc_tag_unref(tag);

    nfc_target_unref(target);
}

/*==========================================================================*
 * basic_b
 *==========================================================================*/

static
void
test_basic_b(
    void)
{
    static const guint8 nfcid0[] = {0x01, 0x01, 0x02, 0x04};
    static const GUtilData nfcid0_data = { TEST_ARRAY_AND_SIZE(nfcid0) };
    static const guint8 app_data[] = {0x05, 0x06, 0x07, 0x08};
    static const guint8 prot_info[] = {0x09, 0x0A, 0x0B, 0x0C, 0x0D};
    static const GUtilData prot_info_data = { TEST_ARRAY_AND_SIZE(prot_info) };
    static const guint8 app_data_empty[] = {0x00, 0x00, 0x00, 0x00};

    NfcTag* tag = g_object_new(NFC_TYPE_TAG, NULL);
    NfcTarget* target = test_target_new_tech(NFC_TECHNOLOGY_B);
    NfcParamPoll poll;
    const NfcParamPollB* poll_b;

    memset(&poll, 0, sizeof(poll));
    poll.b.nfcid0 = nfcid0_data;
    poll.b.prot_info = prot_info_data;
    memcpy(poll.b.app_data, app_data, sizeof(app_data));
    nfc_tag_init_base(tag, target, &poll);
    g_assert(tag->target == target);
    g_assert(tag->present == TRUE);
    poll_b = &nfc_tag_param(tag)->b;
    g_assert(poll_b);
    g_assert(poll_b->nfcid0.bytes != poll.b.nfcid0.bytes);
    g_assert(poll_b->prot_info.bytes != poll.b.prot_info.bytes);
    g_assert_cmpuint(poll_b->nfcid0.size, == ,sizeof(nfcid0));
    g_assert(!memcmp(poll_b->nfcid0.bytes, nfcid0, sizeof(nfcid0)));
    g_assert_cmpuint(poll_b->prot_info.size, == ,sizeof(prot_info));
    g_assert(!memcmp(poll_b->prot_info.bytes, prot_info, sizeof(prot_info)));
    g_assert(!memcmp(poll_b->app_data, app_data, sizeof(app_data)));
    nfc_tag_unref(tag);

    /* Make sure NULL nfcid0 is handled */
    tag = g_object_new(NFC_TYPE_TAG, NULL);
    memset(&poll, 0, sizeof(poll));
    nfc_tag_init_base(tag, target, &poll);
    poll_b = &nfc_tag_param(tag)->b;
    g_assert(poll_b);
    g_assert_cmpuint(poll_b->nfcid0.size, == ,0);
    g_assert(!poll_b->nfcid0.bytes);
    nfc_tag_unref(tag);

    /* Make sure no prot_info and no app_data is handled */
    tag = g_object_new(NFC_TYPE_TAG, NULL);
    memset(&poll, 0, sizeof(poll));
    poll.b.nfcid0 = nfcid0_data;
    nfc_tag_init_base(tag, target, &poll);
    g_assert(tag->target == target);
    g_assert(tag->present == TRUE);
    poll_b = &nfc_tag_param(tag)->b;
    g_assert(poll_b);
    g_assert_cmpuint(poll_b->nfcid0.size, == ,sizeof(nfcid0));
    g_assert(!memcmp(poll_b->nfcid0.bytes, nfcid0, sizeof(nfcid0)));
    g_assert_cmpuint(poll_b->prot_info.size, == ,0);
    g_assert(!poll_b->prot_info.bytes);
    g_assert(!memcmp(poll_b->app_data, app_data_empty, sizeof(app_data_empty)));
    nfc_tag_unref(tag);

    /* Make sure no app_data is handled properly */
    tag = g_object_new(NFC_TYPE_TAG, NULL);
    memset(&poll, 0, sizeof(poll));
    poll.b.nfcid0 = nfcid0_data;
    poll.b.prot_info = prot_info_data;
    nfc_tag_init_base(tag, target, &poll);
    g_assert(tag->target == target);
    g_assert(tag->present == TRUE);
    poll_b = &nfc_tag_param(tag)->b;
    g_assert(poll_b);
    g_assert(poll_b->nfcid0.bytes != poll.b.nfcid0.bytes);
    g_assert(poll_b->prot_info.bytes != poll.b.prot_info.bytes);
    g_assert_cmpuint(poll_b->nfcid0.size, == ,sizeof(nfcid0));
    g_assert(!memcmp(poll_b->nfcid0.bytes, nfcid0, sizeof(nfcid0)));
    g_assert_cmpuint(poll_b->prot_info.size, == ,sizeof(prot_info));
    g_assert(!memcmp(poll_b->prot_info.bytes, prot_info, sizeof(prot_info)));
    g_assert(!memcmp(poll_b->app_data, app_data_empty, sizeof(app_data_empty)));
    nfc_tag_unref(tag);

    /* Make sure NULL prot_info is handled  */
    tag = g_object_new(NFC_TYPE_TAG, NULL);
    memset(&poll, 0, sizeof(poll));
    poll.b.nfcid0 = nfcid0_data;
    memcpy(poll.b.app_data, app_data, sizeof(app_data));
    nfc_tag_init_base(tag, target, &poll);
    g_assert(tag->target == target);
    g_assert(tag->present == TRUE);
    poll_b = &nfc_tag_param(tag)->b;
    g_assert(poll_b);
    g_assert(poll_b->nfcid0.bytes != poll.b.nfcid0.bytes);
    g_assert_cmpuint(poll_b->nfcid0.size, == ,sizeof(nfcid0));
    g_assert(!memcmp(poll_b->nfcid0.bytes, nfcid0, sizeof(nfcid0)));
    g_assert_cmpuint(poll_b->prot_info.size, == ,0);
    g_assert(!poll_b->prot_info.bytes);
    g_assert(!memcmp(poll_b->app_data, app_data, sizeof(app_data)));
    nfc_tag_unref(tag);

    nfc_target_unref(target);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/tag/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("basic_a"), test_basic_a);
    g_test_add_func(TEST_("basic_b"), test_basic_b);
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
