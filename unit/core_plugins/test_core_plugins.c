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

#include "test_common.h"

#include "nfc_plugins.h"
#include "nfc_plugin_impl.h"

#include <dlfcn.h>

static TestOpt test_opt;
static char* test_dir;

struct nfc_manager {
    int dummy;
};

#define TMP_DIR_TEMPLATE "test_XXXXXX"

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
#define PARENT_CLASS() G_TYPE_CHECK_CLASS_CAST(test_plugin_parent_class, \
        NFC_TYPE_PLUGIN, NfcPluginClass)

static
NfcPlugin*
test_plugin_create(
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
        return PARENT_CLASS()->start(plugin, manager);
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
    PARENT_CLASS()->stop(plugin);
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
    /* Public interfaces are NULL tolerant (except for nfc_plugins_new)*/
    g_assert(!nfc_plugins_start(NULL, NULL));
    g_assert(!nfc_plugins_list(NULL));
    nfc_plugins_free(NULL);
    nfc_plugins_stop(NULL);
}

/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;

    memset(&pi, 0, sizeof(pi));
    memset(&manager, 0, sizeof(manager));
    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);
    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(!list[0]);
    g_assert(nfc_plugins_start(plugins, &manager));
    nfc_plugins_stop(plugins);
    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * builtin
 *==========================================================================*/

static
void
test_builtin(
    void)
{
    static NFC_PLUGIN_DEFINE(test_plugin, "Test", test_plugin_create)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin),
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;
    char* empty_dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;
    pi.plugin_dir = empty_dir;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(list[0]);
    g_assert(!list[1]);

    /* Stop before start does nothing */
    nfc_plugins_stop(plugins);
    g_assert(nfc_plugins_start(plugins, &manager));
    nfc_plugins_stop(plugins);
    nfc_plugins_free(plugins);

    remove(empty_dir);
    g_free(empty_dir);
}

/*==========================================================================*
 * external
 *==========================================================================*/

static
void
test_external(
    void)
{
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.plugin_dir = test_dir;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    /* We have 2 test plugins */
    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(list[0]);
    g_assert(list[1]);
    g_assert(!list[2]);
    g_assert(!g_strcmp0(list[0]->desc->name, "test_plugin1"));
    g_assert(!g_strcmp0(list[1]->desc->name, "test_plugin2"));

    g_assert(nfc_plugins_start(plugins, &manager));
    nfc_plugins_stop(plugins);
    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * replace
 *==========================================================================*/

static
void
test_replace(
    void)
{
    static NFC_PLUGIN_DEFINE(test_plugin1, "Test1", test_plugin_create)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin1),
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.plugin_dir = test_dir;
    pi.builtins = builtins;
    pi.flags = NFC_PLUGINS_DONT_UNLOAD;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    /* We have 2 test plugins */
    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(list[0]);
    g_assert(list[1]);
    g_assert(!list[2]);
    g_assert(!g_strcmp0(list[0]->desc->name, "test_plugin1"));
    g_assert(!g_strcmp0(list[1]->desc->name, "test_plugin2"));

    /* Builtin plugin has been replaced by external: */
    g_assert(list[0]->desc != &NFC_PLUGIN_DESC(test_plugin1));

    g_assert(nfc_plugins_start(plugins, &manager));
    nfc_plugins_stop(plugins);
    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * nodir
 *==========================================================================*/

static
void
test_nodir(
    void)
{
    static NFC_PLUGIN_DEFINE2(test_plugin, "Test", test_plugin_create, NULL, 0)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin),
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;
    char* dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);

    /* Make sure it doesn't exist */
    g_assert(dir);
    remove(dir);

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;
    pi.plugin_dir = dir;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(list[0]);
    g_assert(!list[1]);

    g_assert(nfc_plugins_start(plugins, &manager));
    nfc_plugins_stop(plugins);
    nfc_plugins_free(plugins);
    g_free(dir);
}

/*==========================================================================*
 * autostop
 *==========================================================================*/

static
void
test_autostop(
    void)
{
    static NFC_PLUGIN_DEFINE2(test_plugin, "Test", test_plugin_create, NULL, 0)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin),
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;
    TestPlugin* test;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(list[0]);
    g_assert(!list[1]);

    test = TEST_PLUGIN(list[0]);
    nfc_plugin_ref(&test->plugin);

    g_assert(nfc_plugins_start(plugins, &manager));
    g_assert(test->manager);

    /* nfc_plugins_free stops the plugin */
    nfc_plugins_free(plugins);
    g_assert(!test->manager);
    nfc_plugin_unref(&test->plugin);
}

/*==========================================================================*
 * enable
 *==========================================================================*/

static
void
test_enable(
    void)
{
    static NFC_PLUGIN_DEFINE2(test_plugin1, "Test1", test_plugin_create, NULL,
        NFC_PLUGIN_FLAG_DISABLED)
    static NFC_PLUGIN_DEFINE2(test_plugin2, "Test2", test_plugin_create, NULL,
        0)
    static NFC_PLUGIN_DEFINE2(test_plugin3, "Test3", test_plugin_create, NULL,
        NFC_PLUGIN_FLAG_DISABLED) /* This one stays disabled */
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin1),
        &NFC_PLUGIN_DESC(test_plugin2),
        &NFC_PLUGIN_DESC(test_plugin3),
        NULL
    };
    static const char* const enable[] = {
        "test_plugin1",
        "test_plugin2",
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;
    pi.enable = enable;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    /* There should be two plugins in the list */
    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(list[0] && list[0]->desc == &NFC_PLUGIN_DESC(test_plugin1));
    g_assert(list[1] && list[1]->desc == &NFC_PLUGIN_DESC(test_plugin2));
    g_assert(!list[2]);

    nfc_plugins_stop(plugins);
    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * disable
 *==========================================================================*/

static
void
test_disable(
    void)
{
    static NFC_PLUGIN_DEFINE2(test_plugin1, "Test1", test_plugin_create, NULL,
        NFC_PLUGIN_FLAG_DISABLED)
    static NFC_PLUGIN_DEFINE2(test_plugin2, "Test2", test_plugin_create, NULL,
        NFC_PLUGIN_FLAG_DISABLED)
    static NFC_PLUGIN_DEFINE2(test_plugin3, "Test3", test_plugin_create, NULL,
        0)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin1),
        &NFC_PLUGIN_DESC(test_plugin2),
        &NFC_PLUGIN_DESC(test_plugin3),
        NULL
    };
    static const char* const disable[] = {
        "test_plugin2",
        "test_plugin3",
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;
    pi.disable = disable;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    /* All plugins are disabled => list is empty */
    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(!list[0]);

    nfc_plugins_stop(plugins);
    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * invalid
 *==========================================================================*/

static
void
test_invalid(
    void)
{
    static NFC_PLUGIN_DEFINE2(test_plugin, "Test", NULL, NULL, 0)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin),
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    /* The only plugin has no start function => list is empty */
    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(!list[0]);

    g_assert(nfc_plugins_start(plugins, &manager));
    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * failcreate
 *==========================================================================*/

static
NfcPlugin*
test_failcreate_proc(
    void)
{
    return NULL;
}

static
void
test_failcreate(
    void)
{
    static NFC_PLUGIN_DEFINE2(fail, "Test", test_failcreate_proc, NULL, 0)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(fail),
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    /* The only plugin fails to instantiate => list is empty */
    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(!list[0]);

    g_assert(nfc_plugins_start(plugins, &manager));
    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * failstart
 *==========================================================================*/

static
void
test_failstart(
    void)
{
    static NFC_PLUGIN_DEFINE2(test_plugin, "Test", test_plugin_create, NULL, 0)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin),
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;
    TestPlugin* test;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(list[0]);
    g_assert(!list[1]);

    test = TEST_PLUGIN(list[0]);
    nfc_plugin_ref(&test->plugin);
    test->fail_start = TRUE;

    g_assert(nfc_plugins_start(plugins, &manager));
    g_assert(!test->manager);
    nfc_plugin_unref(&test->plugin);

    /* Start removes the plugin that failed to start */
    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(!list[0]);

    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * muststart
 *==========================================================================*/

static
void
test_muststart(
    void)
{
    static NFC_PLUGIN_DEFINE2(test_plugin, "Test", test_plugin_create, NULL, 
        NFC_PLUGIN_FLAG_MUST_START)
    static const NfcPluginDesc* const builtins[] = {
        &NFC_PLUGIN_DESC(test_plugin),
        NULL
    };
    NfcPluginsInfo pi;
    NfcPlugins* plugins;
    NfcPlugin* const* list;
    NfcManager manager;
    TestPlugin* test;

    memset(&manager, 0, sizeof(manager));
    memset(&pi, 0, sizeof(pi));
    pi.builtins = builtins;

    plugins = nfc_plugins_new(&pi);
    g_assert(plugins);

    list = nfc_plugins_list(plugins);
    g_assert(list);
    g_assert(list[0]);
    g_assert(!list[1]);

    test = TEST_PLUGIN(list[0]);
    nfc_plugin_ref(&test->plugin);
    test->fail_start = TRUE;

    /* Must-start plugin fails => the whole thing fails */
    g_assert(!nfc_plugins_start(plugins, &manager));
    g_assert(!test->manager);
    nfc_plugin_unref(&test->plugin);

    nfc_plugins_free(plugins);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/plugins/" name

int main(int argc, char* argv[])
{
    int ret;
    char* path;
    void* test_plugin1;
    void* test_plugin2;

    test_dir = g_path_get_dirname(argv[0]);

    /* Make sure libraries don't get unloaded between tests - glib
     * has a problem with re-registering the same types twice */
    path = g_strconcat(test_dir, "/test_plugin1.so", NULL);
    test_plugin1 = dlopen(path, RTLD_NOW);
    g_assert(test_plugin1);
    g_free(path);

    path = g_strconcat(test_dir, "/test_plugin2.so", NULL);
    test_plugin2 = dlopen(path, RTLD_NOW);
    g_assert(test_plugin2);
    g_free(path);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("empty"), test_empty);
    g_test_add_func(TEST_("builtin"), test_builtin);
    g_test_add_func(TEST_("external"), test_external);
    g_test_add_func(TEST_("replace"), test_replace);
    g_test_add_func(TEST_("nodir"), test_nodir);
    g_test_add_func(TEST_("autostop"), test_autostop);
    g_test_add_func(TEST_("enable"), test_enable);
    g_test_add_func(TEST_("disable"), test_disable);
    g_test_add_func(TEST_("invalid"), test_invalid);
    g_test_add_func(TEST_("failcreate"), test_failcreate);
    g_test_add_func(TEST_("failstart"), test_failstart);
    g_test_add_func(TEST_("muststart"), test_muststart);
    test_init(&test_opt, argc, argv);
    ret = g_test_run();

    dlclose(test_plugin1);
    dlclose(test_plugin2);
    g_free(test_dir);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
