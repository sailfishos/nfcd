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

#include "nfc_plugin_impl.h"

typedef NfcPluginClass TestExternalPlugin4Class;
typedef NfcPlugin TestExternalPlugin4;

G_DEFINE_TYPE(TestExternalPlugin4, test_external_plugin4, NFC_TYPE_PLUGIN)
#define TEST_TYPE_EXTERNAL_PLUGIN4 (test_external_plugin4_get_type())

static
NfcPlugin*
test_external_plugin4_create(
    void)
{
    return g_object_new(TEST_TYPE_EXTERNAL_PLUGIN4, NULL);
}

static
gboolean
test_external_plugin4_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    return TRUE;
}

static
void
test_external_plugin4_init(
    TestExternalPlugin4* self)
{
}

static
void
test_external_plugin4_class_init(
    NfcPluginClass* klass)
{
    klass->start = test_external_plugin4_start;
}

const NfcPluginDesc NFC_PLUGIN_DESC(test_plugin4) NFC_PLUGIN_DESC_ATTR = {
    NULL, "Test plugin 3 (no name)", NFC_CORE_VERSION,
    test_external_plugin4_create, NULL, 0
};

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
