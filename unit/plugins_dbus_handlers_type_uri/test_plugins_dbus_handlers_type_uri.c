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

#include "dbus_handlers/dbus_handlers.h"

#include "test_common.h"

#include <nfc_ndef.h>

#include <glib/gstdio.h>

static TestOpt test_opt;

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    GVariant* args;
    char* dir = g_dir_make_tmp("test_XXXXXX", NULL);
    char* fname1 = g_build_filename(dir, "test1.conf", NULL);
    char* fname2 = g_build_filename(dir, "test2.conf", NULL);
    char* fname3 = g_build_filename(dir, "test3.conf", NULL);
    NfcNdefRec* http = NFC_NDEF_REC(nfc_ndef_rec_u_new("http://jolla.com"));
    NfcNdefRec* https = NFC_NDEF_REC(nfc_ndef_rec_u_new("https://jolla.com"));
    DBusHandlersConfig* handlers_http;
    DBusHandlersConfig* handlers_https;
    const char* contents1 =
        "[URI-Handler]\n"
        "URI = http://*\n"
        "Path = /h1\n"
        "Service = h1.s\n"
        "Method = h1.m\n"
        "\n"
        "[URI-Listener]\n"
        "URI = http://*\n"
        "Path = /l1\n"
        "Service = l1.s\n"
        "Method = l1.m\n";
    const char* contents2 =
        "[URI-Handler]\n"
        "URI = https://*\n"
        "Path = /h2\n"
        "Service = h2.s\n"
        "Method = h2.m\n"
        "\n"
        "[URI-Listener]\n"
        "URI = https://*\n"
        "Path = /l2\n"
        "Service = l2.s\n"
        "Method = l2.m\n";
    const char* contents3 =
        "[URI-Handler]\n"
        "Path = /h3\n"
        "Service = h3.s\n"
        "Method = h3.m\n"
        "\n"
        "[URI-Listener]\n"
        "Path = /l3\n"
        "Service = l3.s\n"
        "Method = l3.m\n";

    GDEBUG("created %s", dir);
    g_assert(g_file_set_contents(fname1, contents1, -1, NULL));
    g_assert(g_file_set_contents(fname2, contents2, -1, NULL));
    g_assert(g_file_set_contents(fname3, contents3, -1, NULL));

    g_assert(http);
    g_assert(https);
    handlers_http = dbus_handlers_config_load(dir, http);
    handlers_https = dbus_handlers_config_load(dir, https);
    g_unlink(fname1);
    g_unlink(fname2);
    g_unlink(fname3);
    g_rmdir(dir);

    g_assert(handlers_http);
    g_assert(handlers_http->handlers);
    g_assert(handlers_http->handlers->next);
    g_assert(!handlers_http->handlers->next->next);
    g_assert(handlers_http);
    g_assert(handlers_http->listeners);
    g_assert(handlers_http->listeners->next);
    g_assert(!handlers_http->listeners->next->next);

    g_assert(!g_strcmp0(handlers_http->handlers->dbus.service, "h1.s"));
    g_assert(!g_strcmp0(handlers_http->handlers->dbus.path, "/h1"));
    g_assert(!g_strcmp0(handlers_http->handlers->next->dbus.service, "h3.s"));
    g_assert(!g_strcmp0(handlers_http->handlers->next->dbus.path, "/h3"));
    g_assert(!g_strcmp0(handlers_http->listeners->dbus.service, "l1.s"));
    g_assert(!g_strcmp0(handlers_http->listeners->dbus.path, "/l1"));
    g_assert(!g_strcmp0(handlers_http->listeners->next->dbus.service, "l3.s"));
    g_assert(!g_strcmp0(handlers_http->listeners->next->dbus.path, "/l3"));

    g_assert(handlers_https);
    g_assert(handlers_https->handlers);
    g_assert(handlers_https->handlers->next);
    g_assert(!handlers_https->handlers->next->next);
    g_assert(handlers_https);
    g_assert(handlers_https->listeners);
    g_assert(handlers_https->listeners->next);
    g_assert(!handlers_https->listeners->next->next);

    g_assert(!g_strcmp0(handlers_https->handlers->dbus.service, "h2.s"));
    g_assert(!g_strcmp0(handlers_https->handlers->dbus.path, "/h2"));
    g_assert(!g_strcmp0(handlers_https->handlers->next->dbus.service, "h3.s"));
    g_assert(!g_strcmp0(handlers_https->handlers->next->dbus.path, "/h3"));
    g_assert(!g_strcmp0(handlers_https->listeners->dbus.service, "l2.s"));
    g_assert(!g_strcmp0(handlers_https->listeners->dbus.path, "/l2"));
    g_assert(!g_strcmp0(handlers_https->listeners->next->dbus.service, "l3.s"));
    g_assert(!g_strcmp0(handlers_https->listeners->next->dbus.path, "/l3"));

    args = handlers_http->handlers->type->handler_args(http);
    g_assert(args);
    g_assert(!g_strcmp0(g_variant_get_type_string(args), "(s)"));
    g_variant_unref(g_variant_ref_sink(args));

    args = handlers_http->handlers->type->listener_args(TRUE, http);
    g_assert(args);
    g_assert(!g_strcmp0(g_variant_get_type_string(args), "(bs)"));
    g_variant_unref(g_variant_ref_sink(args));

    dbus_handlers_config_free(handlers_http);
    dbus_handlers_config_free(handlers_https);
    nfc_ndef_rec_unref(http);
    nfc_ndef_rec_unref(https);
    g_free(fname1);
    g_free(fname2);
    g_free(fname3);
    g_free(dir);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_handlers/type_uri/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
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
