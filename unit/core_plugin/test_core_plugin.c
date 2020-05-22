/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_plugin_impl.h"
#include "nfc_plugin_p.h"

#include <gutil_log.h>

#include <dlfcn.h>

static TestOpt test_opt;

struct nfc_manager {
    int dummy;
};

/*==========================================================================*
 * Test plugin
 *==========================================================================*/

typedef NfcPluginClass TestPluginClass;
typedef struct test_plugin {
    NfcPlugin plugin;
    NfcManager* manager;
    gboolean fail_start;
} TestPlugin;

G_DEFINE_TYPE(TestPlugin, test_plugin, NFC_TYPE_PLUGIN)
#define TEST_TYPE_PLUGIN (test_plugin_get_type())
#define TEST_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_PLUGIN, TestPlugin))
#define NFC_PLUGIN_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, \
        NFC_TYPE_PLUGIN, NfcPluginClass)

TestPlugin*
test_plugin_new(
    void)
{
    return g_object_new(TEST_TYPE_PLUGIN, NULL);
}

static
gboolean
test_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    TestPlugin* self = TEST_PLUGIN(plugin);

    if (self->fail_start) {
        /* Base class fails the call */
        return NFC_PLUGIN_CLASS(test_plugin_parent_class)->start
            (plugin, manager);
    } else {
        g_assert(!self->manager);
        self->manager = manager;
        return TRUE;
    }
}

static
void
test_plugin_stop(
    NfcPlugin* plugin)
{
    TestPlugin* self = TEST_PLUGIN(plugin);

    g_assert(self->manager);
    self->manager = NULL;
    NFC_PLUGIN_CLASS(test_plugin_parent_class)->stop(plugin);
}

static
void
test_plugin_init(
    TestPlugin* self)
{
}

static
void
test_plugin_class_init(
    NfcPluginClass* klass)
{
    klass->start = test_plugin_start;
    klass->stop = test_plugin_stop;
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
    g_assert(!nfc_plugin_ref(NULL));
    g_assert(!nfc_plugin_start(NULL, NULL));
    nfc_plugin_unref(NULL);
    nfc_plugin_stop(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    TestPlugin* test = test_plugin_new();
    NfcPlugin* plugin = &test->plugin;
    NfcManager manager = { 0 };

    /* Fail one start */
    test->fail_start = TRUE;
    g_assert(!nfc_plugin_start(plugin, &manager));
    g_assert(!test->manager);

    /* Now let the start succeed */
    test->fail_start = FALSE;
    g_assert(nfc_plugin_start(plugin, &manager));
    g_assert(test->manager == &manager);

    /* Second start just returns TRUE */
    g_assert(nfc_plugin_start(plugin, &manager));

    /* Stop */
    nfc_plugin_stop(plugin);
    g_assert(!test->manager);

    g_assert(nfc_plugin_ref(plugin) == plugin);
    nfc_plugin_unref(plugin);
    nfc_plugin_unref(plugin);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/plugin/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
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
