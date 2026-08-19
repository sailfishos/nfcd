/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 * Copyright (C) 2019-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2019 Jolla Ltd.
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

#include "nfc_adapter_impl.h"
#include "test_adapter.h"

#include <gutil_log.h>
#include <gutil_misc.h>

/*==========================================================================*
 * Test adapter
 *==========================================================================*/

typedef NfcAdapterClass TestAdapterClass;
typedef struct test_adapter {
    NfcAdapter adapter;
    TEST_ADAPTER_FLAGS flags;
    NFC_TECHNOLOGY supported_techs;
    guint power_request_id;
} TestAdapter;

#define THIS_TYPE test_adapter_get_type()
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, THIS_TYPE, TestAdapter)
#define PARENT_CLASS test_adapter_parent_class

G_DEFINE_TYPE(TestAdapter, test_adapter, NFC_TYPE_ADAPTER)

NfcAdapter*
test_adapter_new(
    void)
{
    return g_object_new(THIS_TYPE, NULL);
}

NfcAdapter*
test_adapter_new_with_techs(
    NFC_TECHNOLOGY techs)
{
    TestAdapter* self = g_object_new(THIS_TYPE, NULL);

    self->supported_techs = techs;
    self->flags |= TEST_ADAPTER_FLAG_OVERRIDE_TECHS;
    return NFC_ADAPTER(self);
}

NfcAdapter*
test_adapter_new_with_flags(
    TEST_ADAPTER_FLAGS flags)
{
    TestAdapter* self = g_object_new(THIS_TYPE, NULL);

    self->flags = flags;
    return NFC_ADAPTER(self);
}

static
gboolean
test_adapter_complete_power_request(
    TestAdapter* self,
    gboolean on)
{
    GDEBUG("Adapter powered %s", on ? "on" : "off");
    self->power_request_id = 0;
    nfc_adapter_power_notify(NFC_ADAPTER(self), on, TRUE);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_adapter_complete_power_on(
    gpointer user_data)
{
    return test_adapter_complete_power_request(THIS(user_data), TRUE);
}

static
gboolean
test_adapter_complete_power_off(
    gpointer user_data)
{
    return test_adapter_complete_power_request(THIS(user_data), FALSE);
}

static
gboolean
test_adapter_submit_power_request(
    NfcAdapter* adapter,
    gboolean on)
{
    TestAdapter* self = THIS(adapter);

    g_assert_cmpuint(self->power_request_id, == ,0);
    if (on && (self->flags & TEST_ADAPTER_FLAG_ASYNC_POWER_ON)) {
        if (self->flags & TEST_ADAPTER_FLAG_ASYNC_POWER_STUCK) {
            GDEBUG("Adapter power-on request pending forever");
        } else {
            GDEBUG("Adapter power-on request pending");
            self->power_request_id = g_idle_add
                (test_adapter_complete_power_on, self);
        }
    } else if (!on && (self->flags & TEST_ADAPTER_FLAG_ASYNC_POWER_OFF)) {
        if (self->flags & TEST_ADAPTER_FLAG_ASYNC_POWER_STUCK) {
            GDEBUG("Adapter power-off request pending forever");
        } else {
            GDEBUG("Adapter power-off request pending");
            self->power_request_id = g_idle_add
                (test_adapter_complete_power_off, self);
        }
    } else {
        GDEBUG("Adapter powered %s", on ? "on" : "off");
        nfc_adapter_power_notify(adapter, on, TRUE);
    }
    return TRUE;
}

static
void
test_adapter_cancel_power_request(
    NfcAdapter* adapter)
{
    TestAdapter* self = THIS(adapter);

    gutil_source_clear(&self->power_request_id);
}

static
gboolean
test_adapter_submit_mode_request(
    NfcAdapter* adapter,
    NFC_MODE mode)
{
    nfc_adapter_mode_notify(adapter, mode, TRUE);
    return TRUE;
}

static
NFC_TECHNOLOGY
test_adapter_get_supported_techs(
    NfcAdapter* adapter)
{
    TestAdapter* self = THIS(adapter);

    return (self->flags & TEST_ADAPTER_FLAG_OVERRIDE_TECHS) ?
        self->supported_techs : NFC_ADAPTER_CLASS(PARENT_CLASS)->
        get_supported_techs(adapter);
}

static
void
test_adapter_init(
    TestAdapter* self)
{
}

static
void
test_adapter_finalize(
    GObject* object)
{
    TestAdapter* self = THIS(object);

    gutil_source_remove(self->power_request_id);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
test_adapter_class_init(
    NfcAdapterClass* klass)
{
    klass->submit_power_request = test_adapter_submit_power_request;
    klass->cancel_power_request = test_adapter_cancel_power_request;
    klass->submit_mode_request = test_adapter_submit_mode_request;
    klass->get_supported_techs = test_adapter_get_supported_techs;
    G_OBJECT_CLASS(klass)->finalize = test_adapter_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
