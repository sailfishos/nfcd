/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "test_initiator.h"
#include "nfc_initiator_impl.h"

#include <gutil_log.h>

typedef NfcInitiatorClass TestInitiatorClass;
typedef struct test_initiator {
    NfcInitiator initiator;
    guint transmit_id;
    guint response_id;
    GSList* list;
    gboolean stay_alive;
} TestInitiator;

#define THIS_TYPE (test_initiator_get_type())
#define THIS(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, THIS_TYPE, TestInitiator))
#define PARENT_CLASS test_initiator_parent_class

G_DEFINE_TYPE(TestInitiator, test_initiator, NFC_TYPE_INITIATOR)

static
GUtilData*
test_initiator_next_data(
    TestInitiator* self)
{
    if (self->list) {
        GUtilData* expected = self->list->data;

        self->list = g_slist_delete_link(self->list, self->list);
        return expected;
    }
    return NULL;
}

static
gboolean
test_initiator_transmit(
    gpointer user_data)
{
    TestInitiator* self = THIS(user_data);
    NfcInitiator* initiator = &self->initiator;
    GUtilData* data = test_initiator_next_data(self);

    g_assert(self->transmit_id);
    self->transmit_id = 0;
    if (data) {
        nfc_initiator_transmit(initiator, data->bytes, data->size);
        g_free(data);
    } else if (!self->stay_alive) {
        nfc_initiator_gone(initiator);
    }
    return G_SOURCE_REMOVE;
}

static
gboolean
test_initiator_response_done(
    gpointer user_data)
{
    TestInitiator* self = THIS(user_data);

    g_assert(!self->transmit_id);
    g_assert(self->response_id);
    self->response_id = 0;
    self->transmit_id = g_idle_add(test_initiator_transmit, self);
    nfc_initiator_response_sent(&self->initiator, NFC_TRANSMIT_STATUS_OK);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_initiator_respond(
    NfcInitiator* initiator,
    const void* data,
    guint len)
{
    TestInitiator* self = THIS(initiator);
    GUtilData* expected = test_initiator_next_data(self);

    if (expected) {
        g_assert_cmpuint(expected->size, ==, len);
        g_assert(!memcmp(data, expected->bytes,  len));
        g_free(expected);
        self->response_id = g_idle_add(test_initiator_response_done, self);
        return TRUE;
    } else {
        GDEBUG("Simulating response failure");
        return FALSE;
    }
}

static
void
test_initiator_finalize(
    GObject* object)
{
    TestInitiator* self = THIS(object);

    if (self->transmit_id) {
        g_source_remove(self->transmit_id);
    }
    if (self->response_id) {
        g_source_remove(self->response_id);
    }
    g_slist_free_full(self->list, g_free);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
test_initiator_init(
    TestInitiator* self)
{
}

static
void
test_initiator_class_init(
    NfcInitiatorClass* klass)
{
    klass->respond = test_initiator_respond;
    klass->deactivate = nfc_initiator_gone;
    G_OBJECT_CLASS(klass)->finalize = test_initiator_finalize;
}

NfcInitiator*
test_initiator_new(
    void)
{
    return test_initiator_new_with_tx(NULL, 0);
}

NfcInitiator*
test_initiator_new_with_tx(
    const TestTx* tx_list,
    gsize tx_count)
{
    return test_initiator_new_with_tx2(tx_list, tx_count, FALSE);
}

NfcInitiator*
test_initiator_new_with_tx2(
    const TestTx* tx_list,
    gsize tx_count,
    gboolean stay_alive)
{
    gsize i;
    TestInitiator* self = g_object_new(THIS_TYPE, NULL);

    self->stay_alive = stay_alive;
    for (i = 0; i < tx_count; i++) {
        const TestTx* tx = tx_list + i;
        const GUtilData* in = &tx->in;
        const GUtilData* out = &tx->out;

        if (in->bytes) {
            self->list = g_slist_append(self->list, test_clone_data(in));
            if (out->bytes) {
                self->list = g_slist_append(self->list, test_clone_data(out));
            }
        }
    }
    if (tx_count) {
        self->transmit_id = g_idle_add(test_initiator_transmit, self);
    }
    return NFC_INITIATOR(self);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
