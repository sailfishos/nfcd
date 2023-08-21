/*
 * Copyright (C) 2020-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2020 Jolla Ltd.
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

#include "test_common.h"
#include "dbus_service/dbus_service_util.h"

static TestOpt test_opt;

/*==========================================================================*
 * byte_array
 *==========================================================================*/

static
void
test_byte_array(
    void)
{
    static const guint8 value = 42;
    GVariant* var = dbus_service_dup_byte_array_as_variant(NULL, 0);

    /* Empty variant */
    g_assert(var);
    g_assert_cmpstr(g_variant_get_type_string(var), == ,"ay");
    g_assert(!g_variant_get_size(var));
    g_variant_unref(g_variant_take_ref(var));

    /* Variant containing some data */
    var = dbus_service_dup_byte_array_as_variant(&value, sizeof(value));
    g_assert_cmpstr(g_variant_get_type_string(var), == ,"ay");
    g_assert_cmpuint(g_variant_get_size(var), == ,sizeof(value));
    g_assert(!memcmp(g_variant_get_data(var), &value, sizeof(value)));
    g_variant_unref(g_variant_take_ref(var));
}

/*==========================================================================*
 * dict
 *==========================================================================*/

static
void
test_dict_check_data(
    GVariant* var,
    const char* name,
    const GUtilData* data)
{
    guint8 y = 0;
    GVariantIter* it = NULL;
    const guint8* bytes = data->bytes;
    guint i;

    g_assert_cmpuint(g_variant_n_children(var), == ,1);
    g_assert(g_variant_lookup(var, name, "ay", &it));
    for (i = 0; g_variant_iter_loop(it, "y", &y); i++) {
        g_assert_cmpuint(y, == ,bytes[i]);
    }
    g_assert_cmpuint(i, == ,data->size);
    g_variant_iter_free(it);
    g_variant_unref(g_variant_ref_sink(var));
}

static
void
test_dict(
    void)
{
    static const char name[] = "value";
    static const guint8 value = 42;
    static const GUtilData data = { &value, sizeof(value) };
    guint8 y = 0;
    GVariantBuilder builder;
    GVariant* var;

    /* Byte */
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    dbus_service_dict_add_byte(&builder, name, value);
    var = g_variant_builder_end(&builder);
    g_assert_cmpuint(g_variant_n_children(var), == ,1);
    g_assert(g_variant_lookup(var, name, "y", &y));
    g_assert_cmpuint(y, == ,value);
    g_variant_unref(g_variant_take_ref(var));

    /* Byte array */
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    dbus_service_dict_add_byte_array(&builder, name, data.bytes, data.size);
    test_dict_check_data(g_variant_builder_end(&builder), name, &data);

    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    dbus_service_dict_add_byte_array_data(&builder, name, &data);
    test_dict_check_data(g_variant_builder_end(&builder), name, &data);

    /* NULL GUtilData doesn't add anything */
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    dbus_service_dict_add_byte_array_data(&builder, name, NULL);
    var = g_variant_builder_end(&builder);
    g_assert_cmpuint(g_variant_n_children(var), == ,0);
    g_variant_unref(g_variant_take_ref(var));
}

/*==========================================================================*
 * valid_id
 *==========================================================================*/

static
void
test_valid_id(
    void)
{
    g_assert(dbus_service_valid_id(1));
    g_assert(!dbus_service_valid_id(NFCD_ID_FAIL));
    g_assert(!dbus_service_valid_id(NFCD_ID_SYNC));
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_service/util/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("byte_array"), test_byte_array);
    g_test_add_func(TEST_("dict"), test_dict);
    g_test_add_func(TEST_("valid_id"), test_valid_id);
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
