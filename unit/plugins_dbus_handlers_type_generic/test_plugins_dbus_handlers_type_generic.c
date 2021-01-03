/*
 * Copyright (C) 2019-2021 Jolla Ltd.
 * Copyright (C) 2019-2021 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_types_p.h"

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
    NfcNdefRec* rec = NFC_NDEF_REC(nfc_ndef_rec_u_new("http://jolla.com"));
    DBusHandlersConfig* handlers;
    const DBusHandlerType* type;
    const char* contents1 =
        "[Handler]\n"
        "Path = /h1\n"
        "Service = h1.s\n"
        "Method = h1.i.m\n"
        "\n"
        "[Listener]\n"
        "Path = /l1\n"
        "Service = l1.s\n"
        "Method = l1.i.m\n";

    GDEBUG("created %s", dir);
    g_assert(g_file_set_contents(fname1, contents1, -1, NULL));

    g_assert(rec);
    handlers = dbus_handlers_config_load(dir, rec);
    g_unlink(fname1);
    g_rmdir(dir);

    g_assert(handlers);
    g_assert(handlers->handlers);
    g_assert(handlers->listeners);
    g_assert(!handlers->handlers->next);
    g_assert(!handlers->listeners->next);

    g_assert(!g_strcmp0(handlers->handlers->dbus.service, "h1.s"));
    g_assert(!g_strcmp0(handlers->handlers->dbus.path, "/h1"));
    g_assert(!g_strcmp0(handlers->listeners->dbus.service, "l1.s"));
    g_assert(!g_strcmp0(handlers->listeners->dbus.path, "/l1"));

    type = handlers->handlers->type;
    args = type->handler_args(rec);
    g_assert(args);
    g_assert(!g_strcmp0(g_variant_get_type_string(args), "(ay)"));
    g_variant_unref(g_variant_ref_sink(args));

    g_assert(type == handlers->listeners->type);
    args = type->listener_args(TRUE, rec);
    g_assert(args);
    g_assert(!g_strcmp0(g_variant_get_type_string(args), "(bay)"));
    g_variant_unref(g_variant_ref_sink(args));

    dbus_handlers_config_free(handlers);
    nfc_ndef_rec_unref(rec);
    g_free(fname1);
    g_free(dir);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_handlers/type_generic/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
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
