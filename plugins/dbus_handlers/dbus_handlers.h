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

#ifndef DBUS_HANDLERS_H
#define DBUS_HANDLERS_H

/* Internal header file for dbus_handlers plugin implementation */

#define GLOG_MODULE_NAME dbus_handlers_log
#include <gutil_log.h>

#include <nfc_ndef.h>

#include <gio/gio.h>

typedef struct dbus_handlers DBusHandlers;
typedef struct dbus_handlers_adapter DBusHandlersAdapter;
typedef struct dbus_handlers_tag DBusHandlersTag;

typedef struct dbus_handler_type DBusHandlerType;
typedef struct dbus_handler_config DBusHandlerConfig;
typedef struct dbus_listener_config DBusListenerConfig;

typedef struct dbus_config {
    char* service;
    char* path;
    char* iface;
    const char* method;
} DBusConfig;

struct dbus_handler_config {
    const DBusHandlerType* type;
    DBusHandlerConfig* next;
    DBusConfig dbus;
};

struct dbus_listener_config {
    const DBusHandlerType* type;
    DBusListenerConfig* next;
    DBusConfig dbus;
};

typedef enum dbus_handler_priority {
    DBUS_HANDLER_PRIORITY_LOW = -1,
    DBUS_HANDLER_PRIORITY_DEFAULT,
} DBUS_HANDLER_PRIORITY;

struct dbus_handler_type {
    const char* name;
    DBUS_HANDLER_PRIORITY priority;
    const DBusHandlerType* buddy;
    /* Recognizing NDEF records */
    gboolean (*supported_record)(NfcNdefRec* ndef);
    /* Config parsing */
    DBusHandlerConfig* (*new_handler_config)(GKeyFile* f, NfcNdefRec* ndef);
    DBusListenerConfig* (*new_listener_config)(GKeyFile* f, NfcNdefRec* ndef);
    void (*free_handler_config)(DBusHandlerConfig* config);
    void (*free_listener_config)(DBusListenerConfig* config);
    /* DBus message sending (floating ref) */
    GVariant* (*handler_args)(NfcNdefRec* ndef);
    GVariant* (*listener_args)(gboolean handled, NfcNdefRec* ndef);
};

typedef struct dbus_handlers_config {
    DBusHandlerConfig* handlers;
    DBusListenerConfig* listeners;
} DBusHandlersConfig;

extern const DBusHandlerType dbus_handlers_type_uri;
extern const DBusHandlerType dbus_handlers_type_mediatype_wildcard;
extern const DBusHandlerType dbus_handlers_type_mediatype_exact;
extern const DBusHandlerType dbus_handlers_type_generic;

NfcNdefRec*
dbus_handlers_config_find_record(
    NfcNdefRec* ndef,
    gboolean (*check)(NfcNdefRec* ndef));

#define dbus_handlers_config_find_supported_record(ndef, type) \
    dbus_handlers_config_find_record(ndef, (type)->supported_record)

/* DBusHandlersConfig */

DBusHandlersConfig*
dbus_handlers_config_load(
    const char* config_dir,
    NfcNdefRec* ndef);

void
dbus_handlers_config_free(
    DBusHandlersConfig* config);

gboolean
dbus_handlers_config_parse_dbus(
    DBusConfig* config,
    GKeyFile* file,
    const char* group);

char*
dbus_handlers_config_get_string(
    GKeyFile* file,
    const char* group,
    const char* key);

DBusHandlerConfig*
dbus_handlers_new_handler_config(
    GKeyFile* file,
    const char* group);

DBusListenerConfig*
dbus_handlers_new_listener_config(
    GKeyFile* file,
    const char* group);

void
dbus_handlers_free_handler_config(
    DBusHandlerConfig* handler);

void
dbus_handlers_free_listener_config(
    DBusListenerConfig* listener);

/* DBusHandlers */

DBusHandlers*
dbus_handlers_new(
    GDBusConnection* connection,
    const char* config_dir);

void
dbus_handlers_run(
    DBusHandlers* handlers,
    NfcNdefRec* ndef);

void
dbus_handlers_free(
    DBusHandlers* handlers);

/* DBusHandlersAdapter */

DBusHandlersAdapter*
dbus_handlers_adapter_new(
    NfcAdapter* adapter,
    DBusHandlers* handlers);

void
dbus_handlers_adapter_free(
    DBusHandlersAdapter* adapter);

/* DBusHandlersTag */

DBusHandlersTag*
dbus_handlers_tag_new(
    NfcTag* tag,
    DBusHandlers* handlers);

void
dbus_handlers_tag_free(
    DBusHandlersTag* tag);

#endif /* DBUS_HANDLERS_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
