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

#include "dbus_neard.h"

static
const DBusNeardProtocolName*
dbus_neard_find_protocol(
    NFC_PROTOCOL protocol,
    const DBusNeardProtocolName* names,
    guint count)
{
    guint i;

    for (i = 0; i < count; i++) {
        if (names[i].protocols & protocol) {
            return names + i;
        }
    }
    return NULL;
}

const DBusNeardProtocolName*
dbus_neard_tag_type_name(
    NFC_PROTOCOL protocol)
{
    static const DBusNeardProtocolName dbus_neard_tag_type_names[] = {
        { NFC_PROTOCOL_T1_TAG, "Type 1" },
        { NFC_PROTOCOL_T2_TAG, "Type 2" },
        { NFC_PROTOCOL_T3_TAG, "Type 3" },
        { NFC_PROTOCOL_T4A_TAG, "Type 4A" },
        { NFC_PROTOCOL_T4B_TAG, "Type 4B" },
        { NFC_PROTOCOL_NFC_DEP, "NFC-DEP" }
    };

    return dbus_neard_find_protocol(protocol, dbus_neard_tag_type_names,
        G_N_ELEMENTS(dbus_neard_tag_type_names));
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
