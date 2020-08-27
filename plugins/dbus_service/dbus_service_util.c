/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2020 Open Mobile Platform LLC.
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

#include "dbus_service_util.h"

static
void
dbus_service_dict_add_value(
    GVariantBuilder* builder,
    const char* name,
    GVariant* value)
{
    g_variant_builder_add(builder, "{sv}", name, value);
}

GVariant*
dbus_service_dup_byte_array_as_variant(
    const void* data,
    guint size)
{
    return size ?
        g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, size, 1) :
        g_variant_new_from_data(G_VARIANT_TYPE_BYTESTRING, NULL, 0, TRUE,
            NULL, NULL);
}

GVariant*
dbus_service_dup_byte_array_data_as_variant(
    const GUtilData* data)
{
    return data ?
        dbus_service_dup_byte_array_as_variant(data->bytes, data->size) :
        dbus_service_dup_byte_array_as_variant(NULL, 0);
}

void
dbus_service_dict_add_byte(
    GVariantBuilder* builder,
    const char* name,
    guint8 value)
{
    dbus_service_dict_add_value(builder, name, g_variant_new_byte(value));
}

void
dbus_service_dict_add_byte_array(
    GVariantBuilder* builder,
    const char* name,
    const void* data,
    guint size)
{
    dbus_service_dict_add_value(builder, name,
        dbus_service_dup_byte_array_as_variant(data, size));
}

void
dbus_service_dict_add_byte_array_data(
    GVariantBuilder* builder,
    const char* name,
    const GUtilData* data)
{
    dbus_service_dict_add_value(builder, name,
        dbus_service_dup_byte_array_data_as_variant(data));
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
