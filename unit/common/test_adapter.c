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

#include "nfc_adapter_impl.h"
#include "test_adapter.h"

/*==========================================================================*
 * Test adapter
 *==========================================================================*/

typedef NfcAdapterClass TestAdapterClass;
typedef NfcAdapter TestAdapter;

G_DEFINE_TYPE(TestAdapter, test_adapter, NFC_TYPE_ADAPTER)
#define TEST_TYPE_ADAPTER (test_adapter_get_type())

NfcAdapter*
test_adapter_new(
    void)
{
    return g_object_new(TEST_TYPE_ADAPTER, NULL);
}

static
gboolean
test_adapter_submit_power_request(
    NfcAdapter* adapter,
    gboolean on)
{
    nfc_adapter_power_notify(adapter, on, TRUE);
    return TRUE;
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
void
test_adapter_init(
    TestAdapter* self)
{
}

static
void
test_adapter_class_init(
    NfcAdapterClass* klass)
{
    klass->submit_power_request = test_adapter_submit_power_request;
    klass->submit_mode_request = test_adapter_submit_mode_request;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
