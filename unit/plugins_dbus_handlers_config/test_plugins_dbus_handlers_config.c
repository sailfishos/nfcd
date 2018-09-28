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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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

#include "dbus_handlers/dbus_handlers.h"

#include "test_common.h"

#include <nfc_ndef.h>

#include <glib/gstdio.h>

static TestOpt test_opt;

static
NfcNdefRec*
test_ndef_record_new(
    void)
{
    GUtilData bytes;
    NfcNdefRec* rec;
    static const guint8 ndef_data[] = {
        0xd1,       /* NDEF record header (MB,ME,SR,TNF=0x01) */
        0x01,       /* Length of the record type */
        0x00,       /* Length of the record payload */
        'x'         /* Record type: 'x' */
    };

    TEST_BYTES_SET(bytes, ndef_data);
    rec = nfc_ndef_rec_new(&bytes);
    g_assert(rec);
    return rec;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!dbus_handlers_config_load(NULL, NULL));
    g_assert(!dbus_handlers_config_load(".", NULL));
    dbus_handlers_config_free(NULL);
}

/*==========================================================================*
 * parse_handler
 *==========================================================================*/

static
void
test_parse_handler(
    void)
{
    GKeyFile* k = g_key_file_new();
    const char* group = "test";
    DBusHandlerConfig* config;

    g_assert(!dbus_handlers_new_handler_config(k, group));
    g_key_file_set_string(k, group, "Service", "Foo");
    g_assert(!dbus_handlers_new_handler_config(k, group));
    g_key_file_set_string(k, group, "Method", "Bar");
    g_assert(!dbus_handlers_new_handler_config(k, group));
    g_key_file_set_string(k, group, "Method", "foo.Bar");

    config = dbus_handlers_new_handler_config(k, group);
    g_assert(config);
    g_assert(!g_strcmp0(config->dbus.service, "Foo"));
    g_assert(!g_strcmp0(config->dbus.iface, "foo"));
    g_assert(!g_strcmp0(config->dbus.method, "Bar"));
    g_assert(!g_strcmp0(config->dbus.path, "/"));
    dbus_handlers_free_handler_config(config);

    g_key_file_set_string(k, group, "Path", "/foo");
    config = dbus_handlers_new_handler_config(k, group);
    g_assert(config);
    g_assert(!g_strcmp0(config->dbus.service, "Foo"));
    g_assert(!g_strcmp0(config->dbus.iface, "foo"));
    g_assert(!g_strcmp0(config->dbus.method, "Bar"));
    g_assert(!g_strcmp0(config->dbus.path, "/foo"));
    dbus_handlers_free_handler_config(config);

    g_key_file_unref(k);
}

/*==========================================================================*
 * parse_listener
 *==========================================================================*/

static
void
test_parse_listener(
    void)
{
    GKeyFile* k = g_key_file_new();
    const char* group = "test";
    DBusListenerConfig* config;

    g_assert(!dbus_handlers_new_listener_config(k, group));
    g_key_file_set_string(k, group, "Service", "Foo");
    g_assert(!dbus_handlers_new_listener_config(k, group));
    g_key_file_set_string(k, group, "Method", "Bar");
    g_assert(!dbus_handlers_new_listener_config(k, group));
    g_key_file_set_string(k, group, "Method", "foo.Bar");

    config = dbus_handlers_new_listener_config(k, group);
    g_assert(config);
    g_assert(!g_strcmp0(config->dbus.service, "Foo"));
    g_assert(!g_strcmp0(config->dbus.iface, "foo"));
    g_assert(!g_strcmp0(config->dbus.method, "Bar"));
    g_assert(!g_strcmp0(config->dbus.path, "/"));
    dbus_handlers_free_listener_config(config);

    g_key_file_set_string(k, group, "Path", "/foo");
    config = dbus_handlers_new_listener_config(k, group);
    g_assert(config);
    g_assert(!g_strcmp0(config->dbus.service, "Foo"));
    g_assert(!g_strcmp0(config->dbus.iface, "foo"));
    g_assert(!g_strcmp0(config->dbus.method, "Bar"));
    g_assert(!g_strcmp0(config->dbus.path, "/foo"));
    dbus_handlers_free_listener_config(config);

    g_key_file_unref(k);
}

/*==========================================================================*
 * load_empty
 *==========================================================================*/

static
void
test_load_empty(
    void)
{
    char* dir = g_dir_make_tmp("test_XXXXXX", NULL);
    char* fname1 = g_build_filename(dir, "test1.conf", NULL);
    char* fname2 = g_build_filename(dir, "test2.conf", NULL);
    const char* contents = "# Nothing here\n";
    NfcNdefRec* rec = test_ndef_record_new();

    GDEBUG("created %s", dir);
    g_assert(!dbus_handlers_config_load("...", rec));
    g_assert(!dbus_handlers_config_load(dir, rec)); /* No files yet */

    g_assert(g_file_set_contents(fname1, contents, -1, NULL));
    g_assert(g_file_set_contents(fname2, contents, -1, NULL));
    g_assert(!dbus_handlers_config_load(dir, rec)); /* No configuration */

    g_unlink(fname1);
    g_unlink(fname2);
    g_rmdir(dir);

    nfc_ndef_rec_unref(rec);
    g_free(fname1);
    g_free(fname2);
    g_free(dir);
}

/*==========================================================================*
 * load_handlers
 *==========================================================================*/

static
void
test_load_handlers(
    void)
{
    char* dir = g_dir_make_tmp("test_XXXXXX", NULL);
    char* fname1 = g_build_filename(dir, "test1.conf", NULL);
    char* fname2 = g_build_filename(dir, "test2.conf", NULL);
    char* fskip = g_build_filename(dir, "foo.bar", NULL);
    NfcNdefRec* rec = test_ndef_record_new();
    DBusHandlersConfig* handlers;
    const char* contents1 =
        "[Handler]\n"
        "Service = foo.bar1\n"
        "Method = foo.Handle1\n";
    const char* contents2 =
        "[Handler]\n"
        "Path = /foo\n"
        "Service = foo.bar2\n"
        "Method = foo.Handle2\n";
    const char* contents_unused =
        "[Handler]\n"
        "Service = foooooo.barrrrrr\n"
        "Method = bar.DontHandle\n";

    GDEBUG("created %s", dir);
    g_assert(g_file_set_contents(fname1, contents1, -1, NULL));
    g_assert(g_file_set_contents(fname2, contents2, -1, NULL));
    g_assert(g_file_set_contents(fskip, contents_unused, -1, NULL));

    handlers = dbus_handlers_config_load(dir, rec);
    g_assert(handlers);
    g_assert(!handlers->listeners);
    g_assert(handlers->handlers);
    g_assert(handlers->handlers->next);
    g_assert(!handlers->handlers->next->next);

    g_assert(!g_strcmp0(handlers->handlers->dbus.service, "foo.bar1"));
    g_assert(!g_strcmp0(handlers->handlers->dbus.path, "/"));
    g_assert(!g_strcmp0(handlers->handlers->next->dbus.service, "foo.bar2"));
    g_assert(!g_strcmp0(handlers->handlers->next->dbus.path, "/foo"));

    g_unlink(fname1);
    g_unlink(fname2);
    g_unlink(fskip);
    g_rmdir(dir);

    dbus_handlers_config_free(handlers);
    nfc_ndef_rec_unref(rec);
    g_free(fname1);
    g_free(fname2);
    g_free(fskip);
    g_free(dir);
}

/*==========================================================================*
 * load_listeners
 *==========================================================================*/

static
void
test_load_listeners(
    void)
{
    char* dir = g_dir_make_tmp("test_XXXXXX", NULL);
    char* fname1 = g_build_filename(dir, "test1.conf", NULL);
    char* fname2 = g_build_filename(dir, "test2.conf", NULL);
    char* fskip = g_build_filename(dir, "skip.conf", NULL);
    NfcNdefRec* rec = test_ndef_record_new();
    DBusHandlersConfig* handlers;
    const char* contents1 =
        "[Listener]\n"
        "Service = foo.bar1\n"
        "Method = foo.Handle1\n";
    const char* contents2 =
        "[Listener]\n"
        "Path = /foo\n"
        "Service = foo.bar2\n"
        "Method = foo.Handle2\n";
    const char* contents_unused =
        "[Listenerrrrr]\n"
        "Service = foooooo.barrrrrr\n"
        "Method = bar.DontHandle\n";

    GDEBUG("created %s", dir);
    g_assert(g_file_set_contents(fname1, contents1, -1, NULL));
    g_assert(g_file_set_contents(fname2, contents2, -1, NULL));
    g_assert(g_file_set_contents(fskip, contents_unused, -1, NULL));

    handlers = dbus_handlers_config_load(dir, rec);
    g_assert(handlers);
    g_assert(!handlers->handlers);
    g_assert(handlers->listeners);
    g_assert(handlers->listeners->next);
    g_assert(!handlers->listeners->next->next);

    g_assert(!g_strcmp0(handlers->listeners->dbus.service, "foo.bar1"));
    g_assert(!g_strcmp0(handlers->listeners->dbus.path, "/"));
    g_assert(!g_strcmp0(handlers->listeners->next->dbus.service, "foo.bar2"));
    g_assert(!g_strcmp0(handlers->listeners->next->dbus.path, "/foo"));

    g_unlink(fname1);
    g_unlink(fname2);
    g_unlink(fskip);
    g_rmdir(dir);

    dbus_handlers_config_free(handlers);
    nfc_ndef_rec_unref(rec);
    g_free(fname1);
    g_free(fname2);
    g_free(fskip);
    g_free(dir);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_handlers/config/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("parse_handler"), test_parse_handler);
    g_test_add_func(TEST_("parse_listener"), test_parse_listener);
    g_test_add_func(TEST_("load_empty"), test_load_empty);
    g_test_add_func(TEST_("load_handlers"), test_load_handlers);
    g_test_add_func(TEST_("load_listeners"), test_load_listeners);
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
