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

#include "test_common.h"

#include "nfc_initiator_p.h"
#include "nfc_initiator_impl.h"

#include <gutil_log.h>

static TestOpt test_opt;

static const GUtilData test_in = { (const void*)"in", 2 };
static const GUtilData test_out = { (const void*)"out", 3 };

static
void
test_initiator_inc(
    NfcInitiator* initiator,
    void* user_data)
{
    (*(int*)user_data)++;
}

/*==========================================================================*
 * Test initiator
 *==========================================================================*/

typedef NfcInitiatorClass TestInitiator1Class;
typedef struct test_initiator1 {
    NfcInitiator initiator;
    GPtrArray* resp;
    guint flags;

#define TEST_INITIATOR_FAIL_RESPONSE (0x01)
#define TEST_INITIATOR_DONT_COMPLETE (0x02)

} TestInitiator1;

#define TEST_TYPE_INITIATOR1 (test_initiator1_get_type())
#define TEST_INITIATOR1(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_INITIATOR1, TestInitiator1))
#define PARENT_CLASS test_initiator1_parent_class
G_DEFINE_TYPE(TestInitiator1, test_initiator1, NFC_TYPE_INITIATOR)

static
gboolean
test_initiator1_respond(
    NfcInitiator* initiator,
    const void* data,
    guint len)
{
    TestInitiator1* self = TEST_INITIATOR1(initiator);

    g_ptr_array_add(self->resp, test_alloc_data(data, len));
    if (self->flags & TEST_INITIATOR_FAIL_RESPONSE) {
        /* Default callback return FALSE */
        return NFC_INITIATOR_CLASS(test_initiator1_parent_class)->respond
            (initiator, data, len);
    } else if (self->flags & TEST_INITIATOR_DONT_COMPLETE) {
        GDEBUG("Queueing response");
        return TRUE;
    } else {
        nfc_initiator_response_sent(initiator, NFC_TRANSMIT_STATUS_OK);
        return TRUE;
    }
}

static
void
test_initiator1_deactivate(
    NfcInitiator* initiator)
{
    /* Base class does nothing */
    g_assert(initiator->present);
    NFC_INITIATOR_CLASS(test_initiator1_parent_class)->deactivate(initiator);
    g_assert(initiator->present);
    nfc_initiator_gone(initiator);
    g_assert(!initiator->present);
}

static
void
test_initiator1_finalize(
    GObject* object)
{
    TestInitiator1* self = TEST_INITIATOR1(object);

    g_ptr_array_free(self->resp, TRUE);
    G_OBJECT_CLASS(test_initiator1_parent_class)->finalize(object);
}

static
void
test_initiator1_init(
    TestInitiator1* self)
{
    self->resp = g_ptr_array_new_with_free_func(g_free);
}

static
void
test_initiator1_class_init(
    TestInitiator1Class* klass)
{
    klass->respond = test_initiator1_respond;
    klass->deactivate = test_initiator1_deactivate;
    G_OBJECT_CLASS(klass)->finalize = test_initiator1_finalize;
}

static
NfcInitiator*
test_initiator1_new(
    guint flags)
{
    TestInitiator1* self = g_object_new(TEST_TYPE_INITIATOR1, NULL);

    self->flags = flags;
    return NFC_INITIATOR(self);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    NfcInitiator* init = test_initiator1_new(0);

    /* Public interfaces are NULL tolerant */
    g_assert(!nfc_initiator_ref(NULL));
    g_assert(!nfc_initiator_add_transmission_handler(NULL, NULL, NULL));
    g_assert(!nfc_initiator_add_transmission_handler(init, NULL, NULL));
    g_assert(!nfc_initiator_add_gone_handler(init, NULL, NULL));
    g_assert(!nfc_initiator_add_gone_handler(NULL, NULL, NULL));
    nfc_initiator_deactivate(NULL);
    nfc_initiator_remove_handler(NULL, 0);
    nfc_initiator_remove_handler(init, 0);
    nfc_initiator_remove_handlers(NULL, NULL, 0);
    nfc_initiator_remove_handlers(init, NULL, 0);
    nfc_initiator_transmit(NULL, NULL, 0);
    nfc_initiator_response_sent(NULL, NFC_TRANSMIT_STATUS_ERROR);
    nfc_initiator_gone(NULL);
    nfc_initiator_unref(NULL);

    g_assert(!nfc_transmission_respond(NULL, NULL, 0, NULL, NULL));
    g_assert(!nfc_transmission_ref(NULL));
    nfc_transmission_unref(NULL);

    nfc_initiator_unref(init);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic_transmission_ok(
    NfcTransmission* t,
    gboolean ok,
    void* user_data)
{
    g_assert(ok);
    (*(int*)user_data)++;
}

static
gboolean
test_basic_transmission_handler(
    NfcInitiator* init,
    NfcTransmission* t,
    const GUtilData* data,
    void* user_data)
{
    NfcTransmission** out = user_data;

    g_assert(!*out);
    g_assert(data);
    g_assert_cmpuint(data->size, == ,test_in.size);
    g_assert(!memcmp(data->bytes, test_in.bytes, data->size));
    *out = nfc_transmission_ref(t);
    return TRUE;
}

static
void
test_basic(
    void)
{
    NfcInitiator* init = test_initiator1_new(0);
    NfcTransmission* trans = NULL;
    int gone = 0, done = 0;
    gulong id[2];

    g_assert(nfc_initiator_ref(init) == init);
    nfc_initiator_unref(init);

    id[0] = nfc_initiator_add_gone_handler(init, test_initiator_inc, &gone);
    id[1] = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);

    g_assert(id[0]);
    g_assert(id[1]);

    /* Simulate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert_cmpint(gone, == ,0);
    g_assert(trans);
    g_assert(nfc_transmission_respond(trans, test_out.bytes, test_out.size,
        test_basic_transmission_ok, &done));
    nfc_transmission_unref(trans);
    g_assert_cmpint(gone, == ,0);
    g_assert_cmpint(done, == ,1);

    /* This call is wrong but it's ignored */
    nfc_initiator_response_sent(init, NFC_TRANSMIT_STATUS_OK);

    /* Simulate deactivation (second time has no effect) */
    nfc_initiator_deactivate(init);
    g_assert_cmpint(gone, == ,1);
    nfc_initiator_deactivate(init);
    g_assert_cmpint(gone, == ,1);

    /* This one does nothing too since the thing is already gone */
    nfc_initiator_gone(init);
    g_assert_cmpint(gone, == ,1);

    nfc_initiator_remove_all_handlers(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * no_response
 *==========================================================================*/

static
void
test_no_response(
    void)
{
    NfcInitiator* init = test_initiator1_new(0);
    int gone = 0;
    gulong id = nfc_initiator_add_gone_handler(init,
        test_initiator_inc, &gone);

    g_assert(id);

    /* Simulate transmission (no handler => deactivation) */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert(!init->present);
    g_assert_cmpint(gone, == ,1);

    /* But the signal is issued only once */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert(!init->present);
    g_assert_cmpint(gone, == ,1);

    nfc_initiator_remove_handler(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * drop_transmission
 *==========================================================================*/

static
void
test_drop_transmission(
    void)
{
    NfcInitiator* init = test_initiator1_new(0);
    NfcTransmission* trans = NULL;
    int gone = 0;
    gulong id[2];

    id[0]= nfc_initiator_add_gone_handler(init,
        test_initiator_inc, &gone);
    /* NOTE: reusing test_basic_transmission_handler */
    id[1] = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);
    g_assert(id[0]);
    g_assert(id[1]);

    /* Simulate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert(trans);
    g_assert(init->present);
    g_assert_cmpint(gone, == ,0);

    /* Drop the transmission without responding */
    nfc_transmission_unref(trans);

    /* That's supposed to deactivate RF interface */
    g_assert(!init->present);
    g_assert_cmpint(gone, == ,1);

    nfc_initiator_remove_all_handlers(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * drop_transmission2
 *==========================================================================*/

static
void
test_drop_transmission2(
    void)
{
    NfcInitiator* init = test_initiator1_new(TEST_INITIATOR_DONT_COMPLETE);
    NfcTransmission* trans = NULL;
    int gone = 0, done = 0;
    gulong id[2];

    /* NOTE: reusing test_basic_transmission_handler */
    id[0] = nfc_initiator_add_gone_handler(init, test_initiator_inc, &gone);
    id[1] = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);

    g_assert(id[0]);
    g_assert(id[1]);

    /* Simulate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert_cmpint(gone, == ,0);
    g_assert(trans);

    /* NOTE: reusing test_basic_transmission_ok */
    g_assert(nfc_transmission_respond(trans, test_out.bytes, test_out.size,
        test_basic_transmission_ok, &done));
    g_assert_cmpint(gone, == ,0);
    g_assert_cmpint(done, == ,0);

    /* Second transmission is queued */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert_cmpint(gone, == ,0);

    /* Complete the first one and ignore the second (deactivating the link) */
    nfc_initiator_remove_handlers(init, id + 1, 1);
    nfc_initiator_response_sent(init, NFC_TRANSMIT_STATUS_OK);
    g_assert_cmpint(done, == ,1);
    g_assert_cmpint(gone, == ,1);
    g_assert(!init->present);

    nfc_transmission_unref(trans);
    nfc_initiator_remove_all_handlers(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * stray_transmission
 *==========================================================================*/

static
void
test_stray_transmission(
    void)
{
    NfcInitiator* init = test_initiator1_new(0);
    NfcTransmission* trans = NULL;
    int gone = 0;
    gulong id[2];

    id[0]= nfc_initiator_add_gone_handler(init,
        test_initiator_inc, &gone);
    /* NOTE: reusing test_basic_transmission_handler */
    id[1] = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);
    g_assert(id[0]);
    g_assert(id[1]);

    /* Legitimate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert(trans);
    g_assert(init->present);
    g_assert_cmpint(gone, == ,0);

    /* Unexpected transmission (before the first one is replied to) */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);

    /* That deactivates RF interface */
    g_assert(!init->present);
    g_assert_cmpint(gone, == ,1);

    nfc_transmission_unref(trans);
    nfc_initiator_remove_all_handlers(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * stray_transmission2
 *==========================================================================*/

static
void
test_stray_transmission2(
    void)
{
    NfcInitiator* init = test_initiator1_new(TEST_INITIATOR_DONT_COMPLETE);
    NfcTransmission* trans = NULL;
    int gone = 0, done = 0;
    gulong id[2];

    id[0]= nfc_initiator_add_gone_handler(init,
        test_initiator_inc, &gone);
    /* NOTE: reusing test_basic_transmission_handler */
    id[1] = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);
    g_assert(id[0]);
    g_assert(id[1]);

    /* Legitimate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert(init->present);
    g_assert_cmpint(gone, == ,0);
    g_assert(trans);

    /* Respond to it (but don't complete it yet) */
    g_assert(nfc_transmission_respond(trans, test_out.bytes, test_out.size,
        test_basic_transmission_ok, &done));
    g_assert_cmpint(gone, == ,0);
    g_assert_cmpint(done, == ,0);

    /* Next transmission (still legitimate) */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert(init->present);
    g_assert_cmpint(gone, == ,0);

    /* But this is too much (RF interface gets deactivated) */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert(!init->present);
    g_assert_cmpint(gone, == ,1);
    g_assert_cmpint(done, == ,0);

    nfc_transmission_unref(trans);
    nfc_initiator_remove_all_handlers(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * queued_transmission
 *==========================================================================*/

static
void
test_queued_transmission(
    void)
{
    NfcInitiator* init = test_initiator1_new(TEST_INITIATOR_DONT_COMPLETE);
    NfcTransmission* trans = NULL;
    NfcTransmission* trans1 = NULL;
    int gone = 0, done = 0;
    gulong id[2];

    /* NOTE: reusing test_basic_transmission_handler */
    id[0] = nfc_initiator_add_gone_handler(init, test_initiator_inc, &gone);
    id[1] = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);

    g_assert(id[0]);
    g_assert(id[1]);

    /* Simulate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert_cmpint(gone, == ,0);
    g_assert(trans);
    trans1 = trans;
    trans = NULL;

    /* NOTE: reusing test_basic_transmission_ok */
    g_assert(nfc_transmission_respond(trans1, test_out.bytes, test_out.size,
        test_basic_transmission_ok, &done));
    g_assert_cmpint(gone, == ,0);
    g_assert_cmpint(done, == ,0);

    /* Second transmission is queued */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert_cmpint(gone, == ,0);
    g_assert(!trans);

    /* Complete the first one and receive the second */
    nfc_initiator_response_sent(init, NFC_TRANSMIT_STATUS_OK);
    g_assert_cmpint(done, == ,1);
    g_assert(trans);

    /* Dropping the current (second) transmission deactivate RF interface */
    nfc_transmission_unref(trans);
    g_assert_cmpint(gone, == ,1);
    g_assert_cmpint(done, == ,1);

    nfc_transmission_unref(trans1);
    nfc_initiator_remove_all_handlers(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * fail_respond
 *==========================================================================*/

static
void
test_fail_respond(
    void)
{
    NfcInitiator* init = test_initiator1_new(TEST_INITIATOR_FAIL_RESPONSE);
    NfcTransmission* trans = NULL;
    int gone = 0, done = 0;
    gulong id[2];

    /* NOTE: reusing test_basic_transmission_handler */
    id[0] = nfc_initiator_add_gone_handler(init, test_initiator_inc, &gone);
    id[1] = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);

    g_assert(id[0]);
    g_assert(id[1]);

    /* Simulate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert_cmpint(gone, == ,0);
    g_assert(trans);
    /* NOTE: reusing test_basic_transmission_ok */
    g_assert(!nfc_transmission_respond(trans, test_out.bytes, test_out.size,
        test_basic_transmission_ok, &done));
    g_assert_cmpint(gone, == ,0);
    g_assert_cmpint(done, == ,0);

    /* Second response fails too, albeit in a different way */
    g_assert(!nfc_transmission_respond(trans, test_out.bytes, test_out.size,
        test_basic_transmission_ok, &done));
    nfc_transmission_unref(trans);
    g_assert_cmpint(gone, == ,0);
    g_assert_cmpint(done, == ,0);

    nfc_initiator_remove_all_handlers(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * queue_response
 *==========================================================================*/

static
void
test_queue_response(
    void)
{
    NfcInitiator* init = test_initiator1_new(TEST_INITIATOR_DONT_COMPLETE);
    NfcTransmission* trans = NULL;
    NfcTransmission* trans1 = NULL;
    int gone = 0, done = 0;
    gulong id[2];

    /* NOTE: reusing test_basic_transmission_handler */
    id[0] = nfc_initiator_add_gone_handler(init, test_initiator_inc, &gone);
    id[1] = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);

    g_assert(id[0]);
    g_assert(id[1]);

    /* Simulate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert_cmpint(gone, == ,0);
    g_assert(trans);
    trans1 = trans;
    trans = NULL;

    /* NOTE: reusing test_basic_transmission_ok */
    g_assert(nfc_transmission_respond(trans1, test_out.bytes, test_out.size,
        test_basic_transmission_ok, &done));
    g_assert_cmpint(gone, == ,0);
    g_assert_cmpint(done, == ,0);

    /* Second transmission is queued */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert_cmpint(gone, == ,0);
    g_assert(!trans);

    /* Dropping the first transmission doesnt't deactivate RF interface */
    nfc_transmission_unref(trans1);
    g_assert_cmpint(gone, == ,0);
    g_assert_cmpint(done, == ,0);

    nfc_initiator_remove_all_handlers(init, id);
    nfc_initiator_unref(init);
}

/*==========================================================================*
 * early_destroy
 *==========================================================================*/

static
void
test_early_destroy(
    void)
{
    NfcInitiator* init = test_initiator1_new(0);
    NfcTransmission* trans = NULL;
    int done = 0;
    gulong id;

    /* NOTE: reusing test_basic_transmission_handler */
    id = nfc_initiator_add_transmission_handler(init,
        test_basic_transmission_handler, &trans);
    g_assert(id);

    /* Simulate transmission */
    nfc_initiator_transmit(init, test_in.bytes, test_in.size);
    g_assert(trans);

    /* Unref the initiator before responding */
    nfc_initiator_remove_handler(init, id);
    nfc_initiator_unref(init);

    /* Obviously, respond must fail now */
    /* NOTE: reusing test_basic_transmission_handler */
    g_assert(!nfc_transmission_respond(trans, test_out.bytes, test_out.size,
        test_basic_transmission_ok, &done));
    g_assert_cmpint(done, == ,0);
    nfc_transmission_unref(trans);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/initiator/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("no_response"), test_no_response);
    g_test_add_func(TEST_("drop_transmission"), test_drop_transmission);
    g_test_add_func(TEST_("drop_transmission2"), test_drop_transmission2);
    g_test_add_func(TEST_("stray_transmission"), test_stray_transmission);
    g_test_add_func(TEST_("stray_transmission2"), test_stray_transmission2);
    g_test_add_func(TEST_("queued_transmission"), test_queued_transmission);
    g_test_add_func(TEST_("fail_respond"), test_fail_respond);
    g_test_add_func(TEST_("queue_response"), test_queue_response);
    g_test_add_func(TEST_("early_destroy"), test_early_destroy);
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
