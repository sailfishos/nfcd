/*
 * Copyright (C) 2023 Slava Monich <slava@monich.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfc_util.h"

#include "test_host_service.h"
#include "test_common.h"

#include <gutil_macros.h>
#include <gutil_misc.h>

#define THIS_TYPE TEST_TYPE_HOST_SERVICE
#define THIS(obj) TEST_HOST_SERVICE(obj)
#define PARENT_CLASS test_host_service_parent_class

typedef NfcHostServiceClass TestHostServiceClass;
G_DEFINE_TYPE(TestHostService, test_host_service, NFC_TYPE_HOST_SERVICE)

enum test_host_service_signal {
    SIGNAL_START,
    SIGNAL_RESTART,
    SIGNAL_COUNT
};

#define SIGNAL_START_NAME "test-host-service-start"
#define SIGNAL_RESTART_NAME "test-host-service-restart"

static guint test_host_service_signals[SIGNAL_COUNT] = { 0 };

typedef struct test_host_service_start_data {
    NfcHostService* service;
    NfcHostServiceBoolFunc complete;
    void* user_data;
    GDestroyNotify destroy;
} TestHostServiceStartData;

typedef struct test_host_service_resp_data {
    NfcHostService* service;
    NfcHostServiceResponse resp;
    NfcHostServiceResponseFunc complete;
    void* user_data;
    GDestroyNotify destroy;
} TestHostServiceRespData;

static
TestHostServiceStartData*
test_host_service_start_data_new(
    NfcHostService* service,
    NfcHostServiceBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    TestHostServiceStartData* data = g_slice_new0(TestHostServiceStartData);

    data->service = nfc_host_service_ref(service);
    data->complete = complete;
    data->user_data = user_data;
    data->destroy = destroy;
    return data;
}

static
gboolean
test_host_service_start_data_complete(
    void* user_data)
{
    TestHostServiceStartData* data = user_data;
    TestHostService* self = THIS(data->service);
    gboolean ok = !(self->flags & TEST_HOST_SERVICE_FLAG_FAIL_START);

    /* start is always incremented, regardless of the status */
    self->start++;
    if (data->complete) {
        data->complete(data->service, ok, data->user_data);
    }

    g_signal_emit(data->service, test_host_service_signals
        [SIGNAL_START], 0, ok);
    return G_SOURCE_REMOVE;
}

static
void
test_host_service_start_data_destroy(
    void* user_data)
{
    TestHostServiceStartData* data = user_data;

    if (data->destroy) {
        data->destroy(data->user_data);
    }
    nfc_host_service_unref(data->service);
    gutil_slice_free(data);
}

static
TestHostServiceRespData*
test_host_service_resp_data_new(
    NfcHostService* service,
    NfcHostServiceResponseFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    TestHostServiceRespData* data = g_slice_new0(TestHostServiceRespData);

    data->service = nfc_host_service_ref(service);
    data->complete = complete;
    data->user_data = user_data;
    data->destroy = destroy;
    return data;
}

static
gboolean
test_host_service_resp_data_complete(
    void* user_data)
{
    TestHostServiceRespData* data = user_data;

    if (data->complete) {
        TestHostService* self = THIS(data->service);

        data->complete(data->service, (self->flags &
            TEST_HOST_SERVICE_FLAG_PROCESS_FAIL) ? NULL : &data->resp,
            data->user_data);
    }
    return G_SOURCE_REMOVE;
}

static
void
test_host_service_resp_data_destroy(
    void* user_data)
{
    TestHostServiceRespData* data = user_data;

    if (data->destroy) {
        data->destroy(data->user_data);
    }
    nfc_host_service_unref(data->service);
    gutil_slice_free(data);
}

TestHostService*
test_host_service_new(
    const char* name)
{
    TestHostService* test = g_object_new(THIS_TYPE, NULL);

    nfc_host_service_init_base(NFC_HOST_SERVICE(test), name);
    return test;
}

gulong
test_host_service_add_start_handler(
    TestHostService* service,
    NfcHostServiceBoolFunc func,
    void* user_data)
{
    return g_signal_connect(service, SIGNAL_START_NAME,
        G_CALLBACK(func), user_data);
}

gulong
test_host_service_add_restart_handler(
    TestHostService* service,
    NfcHostServiceBoolFunc func,
    void* user_data)
{
    return g_signal_connect(service, SIGNAL_RESTART_NAME,
        G_CALLBACK(func), user_data);
}

static
guint
test_host_service_start(
    NfcHostService* service,
    NfcHost* host,
    NfcHostServiceBoolFunc complete,
    void* data,
    GDestroyNotify destroy)
{
    TestHostService* self = THIS(service);
    guint id;

    if (self->flags & TEST_HOST_SERVICE_FLAG_START_SYNC_ERR) {
        const gboolean ok = FALSE;

        /* start is always incremented, regardless of the status */
        self->start++;
        g_signal_emit(service, test_host_service_signals
            [SIGNAL_START], 0, ok);
        if (complete) {
            complete(service, ok, data);
        }
        if (destroy) {
            destroy(data);
        }
        id = NFCD_ID_SYNC;
    } else if (self->flags & TEST_HOST_SERVICE_FLAG_START_SYNC_OK) {
        const gboolean ok = TRUE;

        self->start++;
        id = NFC_HOST_SERVICE_CLASS(PARENT_CLASS)->start(service, host,
            complete, data, destroy);
        g_signal_emit(service, test_host_service_signals[SIGNAL_START], 0, ok);
    } else if (self->flags & TEST_HOST_SERVICE_FLAG_FAIL_START) {
        /* start is always incremented, regardless of the status */
        self->start++;
        g_signal_emit(service, test_host_service_signals
            [SIGNAL_START], 0, FALSE);
        id = NFCD_ID_FAIL;
    } else {
        id = test_idle_add_full(test_host_service_start_data_complete,
            test_host_service_start_data_new(service, complete, data, destroy),
            test_host_service_start_data_destroy);
    }
    return id;
}

static
guint
test_host_service_restart(
    NfcHostService* service,
    NfcHost* host,
    NfcHostServiceBoolFunc complete,
    void* data,
    GDestroyNotify destroy)
{
    TestHostService* self = THIS(service);
    const gboolean ok = TRUE;
    guint id;

    self->restart++;
    id = NFC_HOST_SERVICE_CLASS(PARENT_CLASS)->restart(service, host,
        complete, data, destroy);
    g_signal_emit(service, test_host_service_signals[SIGNAL_RESTART], 0, ok);
    return id;
}

static
guint
test_host_service_process(
    NfcHostService* service,
    NfcHost* host,
    const NfcApdu* apdu,
    NfcHostServiceResponseFunc fn,
    void* user_data,
    GDestroyNotify destroy)
{
    TestHostService* self = THIS(service);

    /* Count this call */
    self->process++;
    if ((self->tx_done >= self->tx_count) ||
        (self->flags & TEST_HOST_SERVICE_FLAG_PROCESS_ERR)) {
        /* Default implementation returns failure */
        return NFC_HOST_SERVICE_CLASS(PARENT_CLASS)->process(service, host,
            apdu, fn, user_data, destroy);
    } else {
        const TestTx* tx = self->tx_list + (self->tx_done++);
        const GUtilData* out = &tx->out;
        TestHostServiceRespData* async = NULL;
        NfcHostServiceResponse* resp = NULL;
        NfcHostServiceResponse sync;
        NfcApdu expect;

        /* Validate incoming APDU */
        g_assert(nfc_apdu_decode(&expect, &tx->in));
        g_assert_cmpuint(apdu->cla, == ,expect.cla);
        g_assert_cmpuint(apdu->ins, == ,expect.ins);
        g_assert_cmpuint(apdu->p1, == ,expect.p1);
        g_assert_cmpuint(apdu->p2, == ,expect.p2);
        g_assert_cmpuint(apdu->data.size, == ,expect.data.size);
        g_assert(!memcmp(apdu->data.bytes, expect.data.bytes, apdu->data.size));
        g_assert_cmpuint(apdu->le, == ,expect.le);

        if (!(self->flags & TEST_HOST_SERVICE_FLAG_PROCESS_SYNC)) {
            async = test_host_service_resp_data_new(service, fn, user_data,
                destroy);
            resp = &async->resp;
        } else if (!(self->flags & TEST_HOST_SERVICE_FLAG_PROCESS_FAIL)) {
            resp = &sync;
            memset(&sync, 0, sizeof(sync));
        }

        if (resp && !(self->flags & TEST_HOST_SERVICE_FLAG_PROCESS_FAIL)) {
            /*
             * We assume that tx points to statically allocated memory
             * so we can just store the pointer.
             */
            g_assert_cmpuint(out->size, >= ,2);
            resp->sw = (((guint) (out->bytes[out->size - 2])) << 8) |
                out->bytes[out->size - 1];
            if (out->size > 2) {
                resp->data.bytes = out->bytes;
                resp->data.size = out->size - 2;
            }

            if (self->sent_cb) {
                resp->sent = self->sent_cb;
                resp->user_data = self->sent_data;
                if (self->flags & TEST_HOST_SERVICE_FLAG_PROCESS_SENT_ONCE) {
                    self->sent_cb = NULL;
                    self->sent_data = NULL;
                }
            }
        }

        if (self->flags & TEST_HOST_SERVICE_FLAG_PROCESS_SYNC) {
            if (fn) {
                fn(service, resp, user_data);
            }
            if (destroy) {
                destroy(user_data);
            }
            return NFCD_ID_SYNC;
        } else {
            return test_idle_add_full(test_host_service_resp_data_complete,
                async, test_host_service_resp_data_destroy);
        }
    }
}

static
void
test_host_service_cancel(
    NfcHostService* service,
    guint id)
{
    if (id != NFCD_ID_SYNC && id != NFCD_ID_FAIL) {
        g_source_remove(id);
    } else {
        /* Default does nothing */
        NFC_HOST_SERVICE_CLASS(PARENT_CLASS)->cancel(service, id);
    }
}

static
void
test_host_service_init(
    TestHostService* self)
{
}

static
void
test_host_service_class_init(
    TestHostServiceClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    klass->start = test_host_service_start;
    klass->restart = test_host_service_restart;
    klass->process = test_host_service_process;
    klass->cancel = test_host_service_cancel;
    test_host_service_signals[SIGNAL_START] =
        g_signal_new(SIGNAL_START_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
            G_TYPE_BOOLEAN);
    test_host_service_signals[SIGNAL_RESTART] =
        g_signal_new(SIGNAL_RESTART_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
            G_TYPE_BOOLEAN);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
