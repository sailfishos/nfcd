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

#ifndef DBUS_NEARD_H
#define DBUS_NEARD_H

/* Internal header file for dbus_neard plugin implementation */

#define GLOG_MODULE_NAME dbus_neard_log
#include <gutil_log.h>

#include <nfc_types.h>

#include <gio/gio.h>

typedef struct dbus_neard_adapter DBusNeardAdapter;
typedef struct dbus_neard_tag DBusNeardTag;

#define DBUS_NEARD_ERROR (dbus_neard_error_quark())
GQuark dbus_neard_error_quark(void);

typedef enum dbus_neard_error {
    DBUS_NEARD_ERROR_FAILED,            /* org.neard.Error.Failed */
    DBUS_NEARD_ERROR_INVALID_ARGS,      /* org.neard.Error.InvalidArguments */
    DBUS_NEARD_ERROR_NOT_READY,         /* org.neard.Error.NotReady */
    DBUS_NEARD_ERROR_NOT_SUPPORTED,     /* org.neard.Error.NotSupported */
    DBUS_NEARD_ERROR_DOES_NOT_EXIST,    /* org.neard.Error.DoesNotExist */
    DBUS_NEARD_ERROR_ABORTED,           /* org.neard.Error.OperationAborted */
    DBUS_NEARD_NUM_ERRORS
} DBusNeardError;

/* neard D-Bus interface is mixing different things in a weird way... */
#define NEARD_PROTOCOL_FELICA  "Felica"
#define NEARD_PROTOCOL_MIFARE  "MIFARE"
#define NEARD_PROTOCOL_ISO_DEP "ISO-DEP"
#define NEARD_PROTOCOL_NFC_DEP "NFC-DEP"

typedef struct dbus_neard_protocol_name {
    NFC_PROTOCOL protocols;
    const char* name;
} DBusNeardProtocolName;

const DBusNeardProtocolName*
dbus_neard_tag_type_name(
    NFC_PROTOCOL protocol);

DBusNeardAdapter*
dbus_neard_adapter_new(
    NfcAdapter* adapter,
    GDBusObjectManagerServer* object_manager);

void
dbus_neard_adapter_free(
    DBusNeardAdapter* neard_adapter);

DBusNeardTag*
dbus_neard_tag_new(
    NfcTag* tag,
    const char* adapter_path,
    GDBusObjectManagerServer* object_manager);

void
dbus_neard_tag_free(
    DBusNeardTag* neard_tag);

#endif /* DBUS_NEARD_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
