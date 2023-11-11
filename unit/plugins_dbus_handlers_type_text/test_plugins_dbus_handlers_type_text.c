/*
 * Copyright (C) 2019-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2019-2021 Jolla Ltd.
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

#include "dbus_handlers/dbus_handlers.h"

#include "test_common.h"

#include <glib/gstdio.h>

static TestOpt test_opt;
static const NdefLanguage* test_system_language = NULL;

/* Stubs */

static
NdefLanguage*
test_copy_language(
    const NdefLanguage* src)
{
    NdefLanguage* copy = NULL;

    if (src) {
        const gsize lsize = src->language ? (strlen(src->language) + 1) : 0;
        const gsize tsize = src->territory ? (strlen(src->territory) + 1) : 0;
        char* dest;

        copy = g_malloc(sizeof(NdefLanguage) + lsize + tsize);
        memset(copy, 0, sizeof(NdefLanguage));
        dest = (char*) (copy + 1);

        if (lsize) {
            memcpy(dest, src->language, lsize);
            copy->language = dest;
            dest += lsize;
        }

        if (tsize) {
            memcpy(dest, src->territory, tsize);
            copy->territory = dest;
            dest += tsize;
        }
    }
    return copy;
}

NdefLanguage*
ndef_system_language(
    void)
{
    return test_copy_language(test_system_language);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    static const char* contents =
        "[Text-Handler]\n"
        "Path = /h1\n"
        "Service = h1.s\n"
        "Method = h1.i.m\n"
        "\n"
        "[Text-Listener]\n"
        "Path = /l1\n"
        "Service = l1.s\n"
        "Method = l1.i.m\n";
    GVariant* args;
    DBusHandlersConfig* handlers;
    char* dir = g_dir_make_tmp("test_XXXXXX", NULL);
    char* fname = g_build_filename(dir, "test.conf", NULL);
    const char* rec_text = "Test";
    NdefRec* rec = NDEF_REC(ndef_rec_t_new(rec_text, "en"));
    const char* text = NULL;
    gboolean handled = FALSE;

    GDEBUG("created %s", dir);
    g_assert(g_file_set_contents(fname, contents, -1, NULL));

    g_assert(rec);
    test_system_language = NULL;
    handlers = dbus_handlers_config_load(dir, rec);

    g_assert(handlers);
    g_assert(handlers->handlers);
    g_assert(handlers->listeners);
    g_assert(!handlers->handlers->next);
    g_assert(!handlers->listeners->next);

    args = handlers->handlers->type->handler_args(rec);
    g_assert(args);
    g_assert_cmpstr(g_variant_get_type_string(args), == ,"(s)");
    g_variant_get(args, "(&s)", &text);
    g_assert_cmpstr(text, == ,rec_text);
    g_variant_unref(g_variant_ref_sink(args));

    args = handlers->handlers->type->listener_args(TRUE, rec);
    g_assert(args);
    g_assert_cmpstr(g_variant_get_type_string(args), == ,"(bs)");
    g_variant_get(args, "(b&s)", &handled, &text);
    g_assert(handled);
    g_assert_cmpstr(text, == ,rec_text);
    g_variant_unref(g_variant_ref_sink(args));

    dbus_handlers_config_free(handlers);
    ndef_rec_unref(rec);
    g_unlink(fname);
    g_rmdir(dir);
    g_free(fname);
    g_free(dir);
}

/*==========================================================================*
 * language
 *==========================================================================*/

typedef struct test_language_data {
    const char* name;
    const NdefLanguage* lang;
    const char* text;
} TestLanguageData;

static const NdefLanguage test_lang_en = { "en", NULL };
static const NdefLanguage test_lang_en_GB = { "en", "GB" };
static const NdefLanguage test_lang_fi = { "fi", NULL };
static const NdefLanguage test_lang_ru = { "ru", NULL };
static const TestLanguageData tests_language[] = {
    { "none", NULL, "Hi" },
    { "en-US", &test_lang_en, "Hi" },
    { "en-GB", &test_lang_en_GB, "Hello" },
    { "fi", &test_lang_fi, "Moi" },
    { "ru", &test_lang_ru, "Hi" }
};

static
void
test_language(
    gconstpointer test_data)
{
    static const char* contents[] = {
        /* test0.conf */
        "[Text-Handler]\n"
        "Path = /h1\n"
        "Service = h1.s\n"
        "Method = h1.i.m\n"
        "\n"
        "[Text-Listener]\n"
        "Path = /l1\n"
        "Service = l1.s\n"
        "Method = l1.i.m\n",
    };
    guint i;
    const TestLanguageData* test = test_data;
    GVariant* args;
    NdefRec* rec;
    DBusHandlersConfig* handlers;
    char* fname[G_N_ELEMENTS(contents)];
    char* dir = g_dir_make_tmp("test_XXXXXX", NULL);
    const char* text = NULL;
    gboolean handled = FALSE;

    GDEBUG("created %s", dir);
    for (i = 0; i < G_N_ELEMENTS(contents); i++) {
        char name[16];

        sprintf(name, "test%u.conf", i);
        fname[i] = g_build_filename(dir, name, NULL);
        g_assert(g_file_set_contents(fname[i], contents[i], -1, NULL));
    }

    (((rec = NDEF_REC(ndef_rec_t_new("Hi", "en-US")))->next =
    NDEF_REC(ndef_rec_t_new("Hello", "en-GB")))->next =
    NDEF_REC(ndef_rec_t_new("Moi", "fi")))->next =
    NDEF_REC(ndef_rec_u_new("http://jolla.com"));
    g_assert(rec);
    test_system_language = test->lang;
    handlers = dbus_handlers_config_load(dir, rec);

    g_assert(handlers);
    g_assert(handlers->handlers);
    g_assert(handlers->listeners);
    g_assert(!handlers->handlers->next);
    g_assert(!handlers->listeners->next);

    args = handlers->handlers->type->handler_args(rec);
    g_assert(args);
    g_assert_cmpstr(g_variant_get_type_string(args), == ,"(s)");
    g_variant_get(args, "(&s)", &text);
    g_assert_cmpstr(text, == ,test->text);
    g_variant_unref(g_variant_ref_sink(args));

    args = handlers->handlers->type->listener_args(TRUE, rec);
    g_assert(args);
    g_assert_cmpstr(g_variant_get_type_string(args), == ,"(bs)");
    g_variant_get(args, "(b&s)", &handled, &text);
    g_assert(handled);
    g_assert_cmpstr(text, == ,test->text);
    g_variant_unref(g_variant_ref_sink(args));

    dbus_handlers_config_free(handlers);
    ndef_rec_unref(rec);
    for (i = 0; i < G_N_ELEMENTS(fname); i++) {
        g_unlink(fname[i]);
        g_free(fname[i]);
    }
    test_system_language = NULL;
    g_rmdir(dir);
    g_free(dir);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_handlers/type_text/" name

int main(int argc, char* argv[])
{
    guint i;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("basic"), test_basic);
    for (i = 0; i < G_N_ELEMENTS(tests_language); i++) {
        const TestLanguageData* test = tests_language + i;
        char* path = g_strconcat(TEST_(""), test->name, NULL);

        g_test_add_data_func(path, test, test_language);
        g_free(path);
    }
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
