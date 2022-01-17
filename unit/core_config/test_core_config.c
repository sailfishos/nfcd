/*
 * Copyright (C) 2022 Jolla Ltd.
 * Copyright (C) 2022 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_plugin_p.h"
#include "nfc_plugin_impl.h"
#include "nfc_config.h"

#include "test_common.h"

#include <gutil_strv.h>
#include <gutil_log.h>

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
    gboolean value;
} TestPlugin;


static void test_plugin_configurable_init(NfcConfigurableInterface* iface);
G_DEFINE_TYPE_WITH_CODE(TestPlugin, test_plugin, NFC_TYPE_PLUGIN,
G_IMPLEMENT_INTERFACE(NFC_TYPE_CONFIGURABLE, test_plugin_configurable_init))
#define TEST_TYPE_PLUGIN (test_plugin_get_type())
#define TEST_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_PLUGIN, TestPlugin))
#define NFC_PLUGIN_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, \
        NFC_TYPE_PLUGIN, NfcPluginClass)
enum test_plugin_signal {
     TEST_SIGNAL_VALUE_CHANGED,
     TEST_SIGNAL_COUNT
};
#define TEST_SIGNAL_VALUE_CHANGED_NAME "test-value-changed"
static const char test_plugin_key[] = "key";
static const char* test_plugin_keys[] = { test_plugin_key, NULL };
static guint test_plugin_signals[TEST_SIGNAL_COUNT] = { 0 };

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

    g_assert(!self->manager);
    self->manager = manager;
    return TRUE;
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
const char* const*
test_plugin_configurable_get_keys(
    NfcConfigurable* conf)
{
    return test_plugin_keys;
}

static
GVariant*
test_plugin_configurable_get_value(
    NfcConfigurable* conf,
    const char* key)
{
    if (!g_strcmp0(test_plugin_key, key)) {
        /* OK to return a floating reference */
        return g_variant_new_boolean(TEST_PLUGIN(conf)->value);
    } else {
        return NULL;
    }
}

static
gboolean
test_plugin_configurable_set_value(
    NfcConfigurable* conf,
    const char* key,
    GVariant* value)
{
    if (!g_strcmp0(test_plugin_key, key)) {
        TestPlugin* self = TEST_PLUGIN(conf);
        const gboolean default_value = FALSE;
        gboolean changed = FALSE;

        if (value) {
            gboolean newval = g_variant_get_boolean(value);

            g_assert(g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN));
            if (self->value != newval) {
                self->value = newval;
                changed = TRUE;
            }
        } else {
            if (self->value != default_value) {
                self->value = default_value;
                changed = TRUE;
            }
        }
        if (changed) {
            g_signal_emit(self, test_plugin_signals[TEST_SIGNAL_VALUE_CHANGED],
                g_quark_from_string(key), key, value);
        }
        return TRUE;
    }
    return FALSE;
}

static
gulong
test_plugin_configurable_add_change_handler(
    NfcConfigurable* conf,
    const char* key,
    NfcConfigChangeFunc func,
    void* user_data)
{
    return g_signal_connect_closure_by_id(TEST_PLUGIN(conf),
        test_plugin_signals[TEST_SIGNAL_VALUE_CHANGED],
        key ? g_quark_from_string(key) : 0,
        g_cclosure_new(G_CALLBACK(func), user_data, NULL), FALSE);
}

static
void
test_plugin_configurable_init(
    NfcConfigurableInterface* iface)
{
    const char* const* keys;

    /* Poke default implementations */
    g_assert(iface->get_keys);
    g_assert(iface->get_value);
    g_assert(iface->set_value);
    g_assert(iface->add_change_handler);
    g_assert(iface->remove_handler);

    /* Default implementation ignore the object pointer, pass NULL */
    keys = iface->get_keys(NULL);
    g_assert(keys);
    g_assert(!keys[0]);
    g_assert(!iface->get_value(NULL, NULL));
    g_assert(!iface->set_value(NULL, NULL, NULL));
    g_assert(!iface->add_change_handler(NULL, NULL, NULL, NULL));
    /* Except for remove_handler() and there's no point of overwriting it */

    iface->get_keys = test_plugin_configurable_get_keys;
    iface->get_value = test_plugin_configurable_get_value;
    iface->set_value = test_plugin_configurable_set_value;
    iface->add_change_handler = test_plugin_configurable_add_change_handler;
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
    test_plugin_signals[TEST_SIGNAL_VALUE_CHANGED] =
        g_signal_new(TEST_SIGNAL_VALUE_CHANGED_NAME, TEST_TYPE_PLUGIN,
            G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VARIANT);
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
    g_assert(!nfc_config_get_keys(NULL));
    g_assert(!nfc_config_get_value(NULL, NULL));
    g_assert(!nfc_config_set_value(NULL, NULL, NULL));
    g_assert(!nfc_config_add_change_handler(NULL, NULL, NULL, NULL));
    nfc_config_remove_handler(NULL, 0);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic_cb(
    NfcConfigurable* conf,
    const char* key,
    GVariant* value,
    void* user_data)
{
    int* count = user_data;

    GDEBUG("%s changed", key);
    g_assert_cmpstr(key, == ,test_plugin_key);
    (*count)++;
}

static
void
test_basic(
    void)
{
    TestPlugin* test = test_plugin_new();
    NfcPlugin* plugin = &test->plugin;
    NfcManager manager = { 0 };
    NfcConfigurable* conf;
    GVariant* value;
    int n = 0;
    gulong id;

    /* Start */
    g_assert(nfc_plugin_start(plugin, &manager));
    g_assert(test->manager == &manager);

    /* Test NfcConfigurable interface */
    conf = NFC_CONFIGURABLE(plugin);
    g_assert(conf);
    g_assert(!nfc_config_get_value(conf, NULL));
    g_assert(!nfc_config_set_value(conf, NULL, NULL));
    g_assert(!nfc_config_get_value(conf, "foo"));
    g_assert(!nfc_config_set_value(conf, "foo", NULL));

    g_assert(gutil_strv_equal((GStrV*)nfc_config_get_keys(conf),
        (GStrV*)test_plugin_keys));

    g_assert(!nfc_config_add_change_handler(conf, NULL, NULL, NULL));
    id = nfc_config_add_change_handler(conf, NULL, test_basic_cb, &n);
    g_assert(id);

    value = nfc_config_get_value(conf, test_plugin_key);
    g_assert(value);
    g_assert(g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN));
    g_assert(!g_variant_get_boolean(value));
    g_variant_unref(value);

    value = g_variant_take_ref(g_variant_new_boolean(TRUE));
    g_assert(nfc_config_set_value(conf, test_plugin_key, value));
    g_assert_cmpint(n, == ,1);
    g_assert(nfc_config_set_value(conf, test_plugin_key, value));
    g_assert_cmpint(n, == ,1); /* No change => no notification */
    g_assert(test->value);
    g_variant_unref(value);

    /* Reset to default */
    g_assert(nfc_config_set_value(conf, test_plugin_key, NULL));
    g_assert(!test->value);
    g_assert_cmpint(n, == ,2);

    nfc_config_remove_handler(conf, 0); /* No effect */
    nfc_config_remove_handler(conf, id);

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
