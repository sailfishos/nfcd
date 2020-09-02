/*
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2019-2020 Slava Monich <slava.monich@jolla.com>
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

#include <gutil_log.h>

G_DEFINE_TYPE(TestTarget, test_target, NFC_TYPE_TARGET)

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
            nfc_target_transmit_done(target, NFC_TRANSMIT_STATUS_ERROR,
                NULL, 0);
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

    if (self->fail_transmit < 0 ||
       (self->fail_transmit > 0 && --self->fail_transmit == 0)) {
        GDEBUG("Simulating transmit failure");
        g_free(expected);
        return FALSE;
    } else {
        if (expected) {
            g_assert_cmpuint(expected->size, ==, len);
            g_assert(!memcmp(data, expected->bytes,  len));
            g_free(expected);
        }
        self->transmit_id = g_idle_add(test_target_transmit_done, self);
        return TRUE;
    }
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
void
test_target_deactivate(
    NfcTarget* target)
{
    nfc_target_gone(target);
}

static
void
test_target_init(
    TestTarget* self)
{
    self->fail_transmit = -1; /* Always fail everything by default */
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
    klass->deactivate = test_target_deactivate;
    G_OBJECT_CLASS(klass)->finalize = test_target_finalize;
}

NfcTarget*
test_target_new(
    void)
{
     return g_object_new(TEST_TYPE_TARGET, NULL);
}

NfcTarget*
test_target_new_tech(
    NFC_TECHNOLOGY tech)
{
    NfcTarget* target = test_target_new();

    target->technology = tech;
    return target;
}

NfcTarget*
test_target_new_tech_with_data(
    NFC_TECHNOLOGY tech,
    const void* cmd_bytes,
    guint cmd_len,
    const void* resp_bytes,
    guint resp_len)
{
    TestTarget* self = g_object_new(TEST_TYPE_TARGET, NULL);

    self->target.technology = tech;
    self->fail_transmit = 0;
    g_ptr_array_add(self->cmd_resp, test_alloc_data(cmd_bytes, cmd_len));
    g_ptr_array_add(self->cmd_resp, test_alloc_data(resp_bytes, resp_len));
    return &self->target;
}

void
test_target_add_data(
    NfcTarget* target,
    const void* cmd_bytes,
    guint cmd_len,
    const void* resp_bytes,
    guint resp_len)
{
    TestTarget* self = TEST_TARGET(target);

    self->fail_transmit = 0;
    g_ptr_array_add(self->cmd_resp, test_alloc_data(cmd_bytes, cmd_len));
    g_ptr_array_add(self->cmd_resp, test_alloc_data(resp_bytes, resp_len));
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
