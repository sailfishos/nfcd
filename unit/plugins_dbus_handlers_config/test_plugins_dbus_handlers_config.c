/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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

#include "dbus_handlers/dbus_handlers.h"

#include "test_common.h"

#include <glib/gstdio.h>

static TestOpt test_opt;

static
NfcNdefRec*
test_ndef_record_media_new(
    const char* mediatype,
    const GUtilData* payload)
{
    GUtilData data;
    NfcNdefRec* rec;
    const gsize type_length = strlen(mediatype);
    const gsize payload_length = (payload ? payload->size : 0);
    guint8* bytes;

    g_assert(type_length < 0x100);
    g_assert(payload_length < 0x100);
    data.size = 3 + type_length + (payload ? payload->size : 0);
    data.bytes = bytes = g_malloc(data.size);
    bytes[0] = 0xd2;                   /* (MB,ME,SR,TNF=0x02) */
    bytes[1] = (guint8)type_length;    /* Length of the record type */
    bytes[2] = (guint8)payload_length; /* Length of the record payload */
    memcpy(bytes + 3, mediatype, type_length);
    if (payload_length) {
        memcpy(bytes + 3 + type_length, payload->bytes, payload_length);
    }

    rec = nfc_ndef_rec_new(&data);
    g_assert(rec);
    g_free(bytes);
    return rec;
}

static
NfcNdefRec*
test_ndef_record_new_media_text(
    const char* mediatype,
    const char* text)
{
    GUtilData payload;

    if (text) {
        payload.bytes = (void*)text;
        payload.size = strlen(text);
    } else {
        memset(&payload, 0, sizeof(payload));
    }
    return test_ndef_record_media_new(mediatype, &payload);
}

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

    /* No config at all */
    g_assert(!dbus_handlers_new_handler_config(k, group));

    /* Invalid D-Bus name */
    g_key_file_set_string(k, group, "Service", "foo,bar");
    g_assert(!dbus_handlers_new_handler_config(k, group));

    /* Missing interface name */
    g_key_file_set_string(k, group, "Service", "foo.service");
    g_assert(!dbus_handlers_new_handler_config(k, group));

    g_key_file_set_string(k, group, "Method", "Bar");
    g_assert(!dbus_handlers_new_handler_config(k, group));

    /* Invalid interface name */
    g_key_file_set_string(k, group, "Method", "foo.Bar");
    g_assert(!dbus_handlers_new_handler_config(k, group));

    /* Invalid method name */
    g_key_file_set_string(k, group, "Method", "foo.interface.1");
    g_assert(!dbus_handlers_new_handler_config(k, group));

    g_key_file_set_string(k, group, "Method", "foo.interface.Bar");
    config = dbus_handlers_new_handler_config(k, group);
    g_assert(config);

    g_assert(!g_strcmp0(config->dbus.service, "foo.service"));
    g_assert(!g_strcmp0(config->dbus.iface, "foo.interface"));
    g_assert(!g_strcmp0(config->dbus.method, "Bar"));
    g_assert(!g_strcmp0(config->dbus.path, "/"));
    dbus_handlers_free_handler_config(config);

    /* Invalid path */
    g_key_file_set_string(k, group, "Path", "//");
    g_assert(!dbus_handlers_new_handler_config(k, group));

    g_key_file_set_string(k, group, "Path", "/foo");
    config = dbus_handlers_new_handler_config(k, group);
    g_assert(config);
    g_assert(!g_strcmp0(config->dbus.service, "foo.service"));
    g_assert(!g_strcmp0(config->dbus.iface, "foo.interface"));
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

    /* No config at all */
    g_assert(!dbus_handlers_new_listener_config(k, group));

    /* Invalid D-Bus name */
    g_key_file_set_string(k, group, "Service", "foo,bar");
    g_assert(!dbus_handlers_new_listener_config(k, group));

    /* Missing interface name */
    g_key_file_set_string(k, group, "Service", "foo.service");
    g_assert(!dbus_handlers_new_listener_config(k, group));

    g_key_file_set_string(k, group, "Method", "Bar");
    g_assert(!dbus_handlers_new_listener_config(k, group));

    /* Invalid interface name */
    g_key_file_set_string(k, group, "Method", "foo.Bar");
    g_assert(!dbus_handlers_new_listener_config(k, group));

    /* Invalid method name */
    g_key_file_set_string(k, group, "Method", "foo.interface.1");
    g_assert(!dbus_handlers_new_listener_config(k, group));

    g_key_file_set_string(k, group, "Method", "foo.interface.Bar");
    config = dbus_handlers_new_listener_config(k, group);
    g_assert(config);

    g_assert(!g_strcmp0(config->dbus.service, "foo.service"));
    g_assert(!g_strcmp0(config->dbus.iface, "foo.interface"));
    g_assert(!g_strcmp0(config->dbus.method, "Bar"));
    g_assert(!g_strcmp0(config->dbus.path, "/"));
    dbus_handlers_free_listener_config(config);

    /* Invalid path */
    g_key_file_set_string(k, group, "Path", "//");
    g_assert(!dbus_handlers_new_listener_config(k, group));

    g_key_file_set_string(k, group, "Path", "/foo");
    config = dbus_handlers_new_listener_config(k, group);
    g_assert(config);
    g_assert(!g_strcmp0(config->dbus.service, "foo.service"));
    g_assert(!g_strcmp0(config->dbus.iface, "foo.interface"));
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
        "Method = foo.bar1.Handle1\n";
    const char* contents2 =
        "[Handler]\n"
        "Path = /foo\n"
        "Service = foo.bar2\n"
        "Method = foo.bar2.Handle2\n";
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
        "Method = foo.bar1.Handle1\n";
    const char* contents2 =
        "[Listener]\n"
        "Path = /foo\n"
        "Service = foo.bar2\n"
        "Method = foo.bar2.Handle2\n";
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
 * multiple_ndefs
 *==========================================================================*/

static
void
test_multiple_ndefs(
    void)
{
    static const char* contents[] = {
        /* test0.conf */
        "[URI-Handler]\n"
        "Path = /h1\n"
        "Service = h1.s\n"
        "Method = h1.i.m\n"
        "\n"
        "[MediaType-Handler]\n"
        "MediaType = text/*\n"
        "Path = /h2\n"
        "Service = h2.s\n"
        "Method = h2.i.m\n",

        /* test1.conf */
        "[MediaType-Handler]\n"
        "MediaType = text/plain\n"
        "Path = /h3\n"
        "Service = h3.s\n"
        "Method = h4.i.m\n"
        "\n"
        "[Handler]\n"
        "Path = /h4\n"
        "Service = h4.s\n"
        "Method = h4.i.m\n"
    };
    guint i;
    NfcNdefRec* rec;
    DBusHandlersConfig* handlers;
    char* fname[G_N_ELEMENTS(contents)];
    char* dir = g_dir_make_tmp("test_XXXXXX", NULL);

    GDEBUG("created %s", dir);
    for (i = 0; i < G_N_ELEMENTS(contents); i++) {
        char name[16];

        sprintf(name, "test%u.conf", i);
        fname[i] = g_build_filename(dir, name, NULL);
        g_assert(g_file_set_contents(fname[i], contents[i], -1, NULL));
    }

    (((rec = test_ndef_record_new())->next =
    test_ndef_record_new_media_text("text/plain", "test1"))->next =
    NFC_NDEF_REC(nfc_ndef_rec_u_new("http://jolla.com")))->next =
    test_ndef_record_new_media_text("text/plain", "test2");

    handlers = dbus_handlers_config_load(dir, rec);
    g_assert(handlers);
    g_assert(!handlers->listeners);
    g_assert(handlers->handlers);
    g_assert(handlers->handlers->next);
    g_assert(handlers->handlers->next->next);
    g_assert(handlers->handlers->next->next->next);
    g_assert(!handlers->handlers->next->next->next->next);

    /* Mediatype record goes before URI record */
    g_assert(!g_strcmp0(handlers->handlers->dbus.service, "h3.s"));
    g_assert(!g_strcmp0(handlers->handlers->dbus.path, "/h3"));
    g_assert(!g_strcmp0(handlers->handlers->next->dbus.service, "h2.s"));
    g_assert(!g_strcmp0(handlers->handlers->next->dbus.path, "/h2"));
    g_assert(!g_strcmp0(handlers->handlers->next->next->dbus.service, "h1.s"));
    g_assert(!g_strcmp0(handlers->handlers->next->next->dbus.path, "/h1"));

    dbus_handlers_config_free(handlers);
    nfc_ndef_rec_unref(rec);
    for (i = 0; i < G_N_ELEMENTS(fname); i++) {
        g_unlink(fname[i]);
        g_free(fname[i]);
    }
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
    g_test_add_func(TEST_("multiple_ndefs"), test_multiple_ndefs);
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
