/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2021 Jolla Ltd.
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

#ifndef DBUS_SERVICE_H
#define DBUS_SERVICE_H

/* Internal header file for dbus_service plugin implementation */

#define GLOG_MODULE_NAME dbus_service_log
#include <gutil_log.h>

#include <nfc_peer_service.h>
#include <nfc_host_service.h>
#include <nfc_host_app.h>

#include <nfcdef.h>

#include <gio/gio.h>

typedef struct dbus_service_adapter DBusServiceAdapter;
typedef struct dbus_service_ndef DBusServiceNdef;
typedef struct dbus_service_plugin DBusServicePlugin;
typedef struct dbus_service_tag DBusServiceTag;
typedef struct dbus_service_tag_t2 DBusServiceTagType2;
typedef struct dbus_service_isodep DBusServiceIsoDep;
typedef struct dbus_service_peer DBusServicePeer;
typedef struct dbus_service_host DBusServiceHost;

#define DBUS_SERVICE_ERROR (dbus_service_error_quark())
GQuark dbus_service_error_quark(void);

typedef enum dbus_service_error {
    DBUS_SERVICE_ERROR_FAILED,          /* Failed */
    DBUS_SERVICE_ERROR_ACCESS_DENIED,   /* AccessDenied */
    DBUS_SERVICE_ERROR_INVALID_ARGS,    /* InvalidArgs */
    DBUS_SERVICE_ERROR_NOT_FOUND,       /* NotFound */
    DBUS_SERVICE_ERROR_NOT_SUPPORTED,   /* NotSupported */
    DBUS_SERVICE_ERROR_ABORTED,         /* Aborted */
    DBUS_SERVICE_ERROR_NACK,            /* NACK */
    DBUS_SERVICE_ERROR_CANCELLED,       /* Cancelled */
    DBUS_SERVICE_ERROR_NO_SERVICE,      /* NoService */
    DBUS_SERVICE_ERROR_REJECTED,        /* Rejected */
    DBUS_SERVICE_ERROR_ALREADY_EXISTS,  /* AlreadyExists */
    DBUS_SERVICE_NUM_ERRORS
} DBusServiceError;

#define NFC_DBUS_TAG_T2_INTERFACE "org.sailfishos.nfc.TagType2"
#define NFC_DBUS_ISODEP_INTERFACE "org.sailfishos.nfc.IsoDep"

DBusServicePeer*
dbus_service_plugin_find_peer(
    DBusServicePlugin* plugin,
    NfcPeer* peer);

DBusServiceHost*
dbus_service_plugin_find_host(
    DBusServicePlugin* plugin,
    NfcHost* host);

/* org.sailfishos.nfc.LocalService */

typedef struct dbus_service_local {
    NfcPeerService service;
    DBusServicePlugin* plugin;
    const char* dbus_name;
    const char* obj_path;
} DBusServiceLocal;

DBusServiceLocal*
dbus_service_local_new(
    GDBusConnection* connection,
    const char* obj_path,
    const char* llc_name,
    const char* dbus_name);

/* org.sailfishos.nfc.LocalHostService */

typedef struct dbus_service_local_host {
    NfcHostService service;
    DBusServicePlugin* plugin;
    const char* dbus_name;
    const char* obj_path;
} DBusServiceLocalHost;

DBusServiceLocalHost*
dbus_service_local_host_new(
    GDBusConnection* connection,
    const char* obj_path,
    const char* name,
    const char* dbus_name);

/* org.sailfishos.nfc.LocalHostApp */

typedef struct dbus_service_local_app {
    NfcHostApp app;
    DBusServicePlugin* plugin;
    const char* dbus_name;
    const char* obj_path;
} DBusServiceLocalApp;

DBusServiceLocalApp*
dbus_service_local_app_new(
    GDBusConnection* connection,
    const char* obj_path,
    const char* name,
    const GUtilData* aid,
    NFC_HOST_APP_FLAGS flags,
    const char* dbus_name);

/* org.sailfishos.nfc.Adapter */

DBusServiceAdapter*
dbus_service_adapter_new(
    NfcAdapter* adapter,
    GDBusConnection* connection);

const char*
dbus_service_adapter_path(
    DBusServiceAdapter* adapter);

DBusServicePeer*
dbus_service_adapter_find_peer(
    DBusServiceAdapter* self,
    NfcPeer* peer);

DBusServiceHost*
dbus_service_adapter_find_host(
    DBusServiceAdapter* self,
    NfcHost* host);

void
dbus_service_adapter_free(
    DBusServiceAdapter* adapter);

/* org.sailfishos.nfc.Tag */

struct dbus_service_tag {
    GDBusConnection* connection;
    const char* path;
    NfcTag* tag;
};

DBusServiceTag*
dbus_service_tag_new(
    NfcTag* tag,
    const char* parent_path,
    GDBusConnection* connection);

NfcTargetSequence*
dbus_service_tag_sequence(
    DBusServiceTag* tag,
    GDBusMethodInvocation* call);

void
dbus_service_tag_free(
    DBusServiceTag* tag);

/* org.sailfishos.nfc.NDEF */

DBusServiceNdef*
dbus_service_ndef_new(
    NdefRec* rec,
    const char* path,
    GDBusConnection* connection);

const char*
dbus_service_ndef_path(
    DBusServiceNdef* ndef);

void
dbus_service_ndef_free(
    DBusServiceNdef* ndef);

/* org.sailfishos.nfc.TagType2 */

DBusServiceTagType2*
dbus_service_tag_t2_new(
    NfcTagType2* tag,
    DBusServiceTag* owner);

void
dbus_service_tag_t2_free(
    DBusServiceTagType2* t2);

/* org.sailfishos.nfc.IsoDep */

DBusServiceIsoDep*
dbus_service_isodep_new(
    NfcTagType4* tag,
    DBusServiceTag* owner);

void
dbus_service_isodep_free(
    DBusServiceIsoDep* isodep);

/* org.sailfishos.nfc.Peer */

struct dbus_service_peer {
    GDBusConnection* connection;
    const char* path;
    NfcPeer* peer;
};

DBusServicePeer*
dbus_service_peer_new(
    NfcPeer* peer,
    const char* parent_path,
    GDBusConnection* connection);

void
dbus_service_peer_free(
    DBusServicePeer* peer);

/* org.sailfishos.nfc.Host */

struct dbus_service_host {
    GDBusConnection* connection;
    const char* path;
    NfcHost* host;
};

DBusServiceHost*
dbus_service_host_new(
    NfcHost* host,
    const char* parent_path,
    GDBusConnection* connection);

void
dbus_service_host_free(
    DBusServiceHost* host);

#endif /* DBUS_SERVICE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
