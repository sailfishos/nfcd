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

#include "test_host_app.h"
#include "test_common.h"

#include <gutil_macros.h>
#include <gutil_misc.h>

#define THIS_TYPE TEST_TYPE_HOST_APP
#define THIS(obj) TEST_HOST_APP(obj)
#define PARENT_CLASS test_host_app_parent_class

typedef NfcHostAppClass TestHostAppClass;
G_DEFINE_TYPE(TestHostApp, test_host_app, NFC_TYPE_HOST_APP)

enum test_host_app_signal {
    SIGNAL_START,
    SIGNAL_COUNT
};

#define SIGNAL_START_NAME "test-host-app-start"

static guint test_host_app_signals[SIGNAL_COUNT] = { 0 };

typedef struct test_host_app_bool_data {
    NfcHostApp* app;
    NfcHostAppBoolFunc complete;
    void* user_data;
    GDestroyNotify destroy;
    gboolean ok;
} TestHostAppBoolData;

typedef struct test_host_app_resp_data {
    NfcHostApp* app;
    NfcHostAppResponse resp;
    NfcHostAppResponseFunc complete;
    void* user_data;
    GDestroyNotify destroy;
} TestHostAppRespData;

static
TestHostAppBoolData*
test_host_app_bool_data_new(
    NfcHostApp* app,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy,
    gboolean ok)
{
    TestHostAppBoolData* data = g_slice_new0(TestHostAppBoolData);

    data->app = nfc_host_app_ref(app);
    data->complete = complete;
    data->user_data = user_data;
    data->destroy = destroy;
    data->ok = ok;
    return data;
}

static
gboolean
test_host_app_start_complete(
    void* user_data)
{
    TestHostAppBoolData* data = user_data;
    TestHostApp* self = THIS(data->app);

    /* start is always incremented, regardless of the status */
    self->start++;
    if (data->complete) {
        data->complete(data->app, data->ok, data->user_data);
    }

    g_signal_emit(self, test_host_app_signals[SIGNAL_START], 0, data->ok);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_host_app_bool_data_complete(
    void* user_data)
{
    TestHostAppBoolData* data = user_data;

    if (data->complete) {
        data->complete(data->app, data->ok, data->user_data);
    }
    return G_SOURCE_REMOVE;
}

static
void
test_host_app_bool_data_destroy(
    void* user_data)
{
    TestHostAppBoolData* data = user_data;

    if (data->destroy) {
        data->destroy(data->user_data);
    }
    nfc_host_app_unref(data->app);
    gutil_slice_free(data);
}

static
TestHostAppRespData*
test_host_app_resp_data_new(
    NfcHostApp* app,
    NfcHostAppResponseFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    TestHostAppRespData* data = g_slice_new0(TestHostAppRespData);

    data->app = nfc_host_app_ref(app);
    data->complete = complete;
    data->user_data = user_data;
    data->destroy = destroy;
    return data;
}

static
gboolean
test_host_app_resp_data_complete(
    void* user_data)
{
    TestHostAppRespData* data = user_data;

    if (data->complete) {
        TestHostApp* self = THIS(data->app);

        data->complete(data->app, (self->flags &
            TEST_HOST_APP_FLAG_PROCESS_FAIL) ? NULL : &data->resp,
            data->user_data);
    }
    return G_SOURCE_REMOVE;
}

static
void
test_host_app_resp_data_destroy(
    void* user_data)
{
    TestHostAppRespData* data = user_data;

    if (data->destroy) {
        data->destroy(data->user_data);
    }
    nfc_host_app_unref(data->app);
    gutil_slice_free(data);
}

TestHostApp*
test_host_app_new(
    const GUtilData* aid,
    const char* name,
    NFC_HOST_APP_FLAGS flags)
{
    TestHostApp* test = g_object_new(THIS_TYPE, NULL);

    nfc_host_app_init_base(NFC_HOST_APP(test), aid, name, flags);
    return test;
}

gulong
test_host_app_add_start_handler(
    TestHostApp* app,
    NfcHostAppBoolFunc func,
    void* data)
{
    return g_signal_connect(app, SIGNAL_START_NAME, G_CALLBACK(func), data);
}

static
guint
test_host_app_bool_op(
    NfcHostApp* app,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy,
    gboolean ok)
{
    return test_idle_add_full(test_host_app_bool_data_complete,
        test_host_app_bool_data_new(app, complete, user_data, destroy, ok),
        test_host_app_bool_data_destroy);
}

static
guint
test_host_app_start(
    NfcHostApp* app,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    TestHostApp* self = THIS(app);

    if (self->flags & TEST_HOST_APP_FLAG_START_SYNC_ERR) {
        const gboolean ok = FALSE;

        /* start is always incremented, regardless of the status */
        self->start++;
        g_signal_emit(self, test_host_app_signals[SIGNAL_START], 0, ok);
        if (complete) {
            complete(app, ok, user_data);
        }
        if (destroy) {
            destroy(user_data);
        }
        return NFCD_ID_SYNC;
    } else if (self->flags & TEST_HOST_APP_FLAG_START_SYNC_OK) {
        const gboolean ok = TRUE;

        self->start++;
        g_signal_emit(self, test_host_app_signals[SIGNAL_START], 0, ok);
        return NFC_HOST_APP_CLASS(PARENT_CLASS)->start(app, host,
            complete, user_data, destroy);
    } else if (self->flags & TEST_HOST_APP_FLAG_FAIL_START) {
        /* start is always incremented, regardless of the status */
        g_signal_emit(self, test_host_app_signals[SIGNAL_START], 0, FALSE);
        self->start++;
        return NFCD_ID_FAIL;
    } else {
        return test_idle_add_full(test_host_app_start_complete,
            test_host_app_bool_data_new(app, complete, user_data, destroy,
            !(self->flags & TEST_HOST_APP_FLAG_FAIL_START_ASYNC)),
            test_host_app_bool_data_destroy);
    }
}

static
guint
test_host_app_implicit_select(
    NfcHostApp* app,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    TestHostApp* self = THIS(app);

    self->select++;
    if (self->flags & TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT) {
        /* Default implementation returns failure */
        return NFC_HOST_APP_CLASS(PARENT_CLASS)->implicit_select(app, host,
            complete, user_data, destroy);
    } else {
        return test_host_app_bool_op(app, complete, user_data, destroy,
            !(self->flags & TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT_ASYNC));
    }
}

static
guint
test_host_app_select(
    NfcHostApp* app,
    NfcHost* host,
    NfcHostAppBoolFunc complete,
    void* user_data,
    GDestroyNotify destroy)
{
    TestHostApp* self = THIS(app);

    self->select++;
    if (self->flags & TEST_HOST_APP_FLAG_FAIL_SELECT) {
        /* Default implementation returns failure */
        return NFC_HOST_APP_CLASS(PARENT_CLASS)->select(app, host,
            complete, user_data, destroy);
    } else {
        return test_host_app_bool_op(app, complete, user_data, destroy,
            !(self->flags & TEST_HOST_APP_FLAG_FAIL_SELECT_ASYNC));
    }
}

static
void
test_host_app_deselect(
    NfcHostApp* app,
    NfcHost* host)
{
    THIS(app)->deselect++;
    NFC_HOST_APP_CLASS(PARENT_CLASS)->deselect(app, host);
}

static
guint
test_host_app_process(
    NfcHostApp* app,
    NfcHost* host,
    const NfcApdu* apdu,
    NfcHostAppResponseFunc fn,
    void* user_data,
    GDestroyNotify destroy)
{
    TestHostApp* self = THIS(app);

    /* Count this call */
    self->process++;
    if ((self->tx_done >= self->tx_count) ||
        (self->flags & TEST_HOST_APP_FLAG_PROCESS_ERR)) {
        /* Default implementation returns failure */
        return NFC_HOST_APP_CLASS(PARENT_CLASS)->process(app, host,
            apdu, fn, user_data, destroy);
    } else {
        const TestTx* tx = self->tx_list + (self->tx_done++);
        const GUtilData* out = &tx->out;
        TestHostAppRespData* async = NULL;
        NfcHostAppResponse* resp = NULL;
        NfcHostAppResponse sync;
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

        if (!(self->flags & TEST_HOST_APP_FLAG_PROCESS_SYNC)) {
            async = test_host_app_resp_data_new(app, fn, user_data, destroy);
            resp = &async->resp;
        } else if (!(self->flags & TEST_HOST_APP_FLAG_PROCESS_FAIL)) {
            resp = &sync;
            memset(&sync, 0, sizeof(sync));
        }

        if (resp && !(self->flags & TEST_HOST_APP_FLAG_PROCESS_FAIL)) {
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
                if (self->flags & TEST_HOST_APP_FLAG_PROCESS_SENT_ONCE) {
                    self->sent_cb = NULL;
                    self->sent_data = NULL;
                }
            }
        }

        if (self->flags & TEST_HOST_APP_FLAG_PROCESS_SYNC) {
            if (fn) {
                fn(app, resp, user_data);
            }
            if (destroy) {
                destroy(user_data);
            }
            return NFCD_ID_SYNC;
        } else {
            return test_idle_add_full(test_host_app_resp_data_complete,
                async, test_host_app_resp_data_destroy);
        }
    }
}

static
void
test_host_app_cancel(
    NfcHostApp* app,
    guint id)
{
    if (id != NFCD_ID_SYNC && id != NFCD_ID_FAIL) {
        g_source_remove(id);
    } else {
        /* Default does nothing */
        NFC_HOST_APP_CLASS(PARENT_CLASS)->cancel(app, id);
    }
}

static
void
test_host_app_init(
    TestHostApp* self)
{
}

static
void
test_host_app_class_init(
    TestHostAppClass* klass)
{
    klass->start = test_host_app_start;
    klass->cancel = test_host_app_cancel;
    klass->implicit_select = test_host_app_implicit_select;
    klass->select = test_host_app_select;
    klass->deselect = test_host_app_deselect;
    klass->process = test_host_app_process;
    test_host_app_signals[SIGNAL_START] =
        g_signal_new(SIGNAL_START_NAME, G_OBJECT_CLASS_TYPE(klass),
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
