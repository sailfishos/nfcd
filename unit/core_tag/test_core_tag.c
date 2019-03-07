/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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
 * Test target
 *==========================================================================*/

typedef NfcTargetClass TestTargetClass;
typedef struct test_target {
    NfcTarget target;
    gboolean deactivated;
} TestTarget;

G_DEFINE_TYPE(TestTarget, test_target, NFC_TYPE_TARGET)
#define TEST_TYPE_TARGET (test_target_get_type())
#define TEST_TARGET(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_TARGET, TestTarget))

TestTarget*
test_target_new(
    void)
{
     return g_object_new(TEST_TYPE_TARGET, NULL);
}

static
void
test_target_deactivate(
    NfcTarget* target)
{
    TEST_TARGET(target)->deactivated = TRUE;
    NFC_TARGET_CLASS(test_target_parent_class)->deactivate(target);
}

static
void
test_target_init(
    TestTarget* self)
{
}

static
void
test_target_class_init(
    NfcTargetClass* klass)
{
    klass->deactivate = test_target_deactivate;
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
    TestTarget* test_target = test_target_new();
    NfcTarget* target = &test_target->target;
    const char* name = "test";
    int init_count = 0;
    int gone_count = 0;
    gulong init_id;
    gulong gone_id;

    nfc_tag_init_base(tag, target);
    g_assert(tag->target == target);
    g_assert(tag->present == TRUE);

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
    g_assert(test_target->deactivated);

    /* "gone" is also a one-time signal*/
    nfc_target_gone(target);
    g_assert(gone_count == 1);
    g_assert(!tag->present);
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
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/tag/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
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
