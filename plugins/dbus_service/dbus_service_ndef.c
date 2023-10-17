/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018 Jolla Ltd.
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

#include "dbus_service.h"
#include "dbus_service/org.sailfishos.nfc.NDEF.h"

#include <gutil_misc.h>

enum {
    CALL_GET_ALL,
    CALL_GET_INTERFACE_VERSION,
    CALL_GET_FLAGS,
    CALL_GET_TNF,
    CALL_GET_INTERFACES,
    CALL_GET_TYPE,
    CALL_GET_ID,
    CALL_GET_PAYLOAD,
    CALL_GET_RAW_DATA,
    CALL_COUNT
};

struct dbus_service_ndef {
    char* path;
    GDBusConnection* connection;
    OrgSailfishosNfcNDEF* iface;
    GVariant* empty_ay;
    NdefRec* rec;
    gulong call_id[CALL_COUNT];
};

#define NFC_DBUS_NDEF_INTERFACE "org.sailfishos.nfc.NDEF"
#define NFC_DBUS_NDEF_INTERFACE_VERSION  (1)

static const char* const dbus_service_ndef_default_interfaces[] = {
    NFC_DBUS_NDEF_INTERFACE, NULL
};

static
GVariant*
dbus_service_ndef_bytes_as_variant(
    DBusServiceNdef* self,
    const GUtilData* data)
{
    /* We need to hold a reference to our NdefRec until newly created
     * variant is freed. */
    return data->size ?
        g_variant_new_from_data(G_VARIANT_TYPE("ay"), data->bytes,
        data->size, TRUE, (GDestroyNotify) ndef_rec_unref,
        ndef_rec_ref(self->rec)) : self->empty_ay;
}

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

static
gboolean
dbus_service_ndef_handle_get_all(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    NdefRec* ndef = self->rec;

    org_sailfishos_nfc_ndef_complete_get_all(iface, call,
        NFC_DBUS_NDEF_INTERFACE_VERSION, ndef->flags, ndef->tnf,
        dbus_service_ndef_default_interfaces,
        dbus_service_ndef_bytes_as_variant(self, &ndef->type),
        dbus_service_ndef_bytes_as_variant(self, &ndef->id),
        dbus_service_ndef_bytes_as_variant(self, &ndef->payload));
    return TRUE;
}

static
gboolean
dbus_service_ndef_handle_get_interface_version(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    org_sailfishos_nfc_ndef_complete_get_interface_version(iface, call,
        NFC_DBUS_NDEF_INTERFACE_VERSION);
    return TRUE;
}

static
gboolean
dbus_service_ndef_handle_get_flags(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    org_sailfishos_nfc_ndef_complete_get_flags(iface, call, self->rec->flags);
    return TRUE;
}

static
gboolean
dbus_service_ndef_handle_get_type_name_format(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    org_sailfishos_nfc_ndef_complete_get_type_name_format(iface, call,
        self->rec->tnf);
    return TRUE;
}

static
gboolean
dbus_service_ndef_handle_get_interfaces(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    org_sailfishos_nfc_ndef_complete_get_interfaces(iface, call,
        dbus_service_ndef_default_interfaces);
    return TRUE;
}

static
gboolean
dbus_service_ndef_handle_get_type(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    NdefRec* ndef = self->rec;

    org_sailfishos_nfc_ndef_complete_get_type(iface, call,
        dbus_service_ndef_bytes_as_variant(self, &ndef->type));
    return TRUE;
}

static
gboolean
dbus_service_ndef_handle_get_id(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    NfcNdefRec* ndef = self->rec;

    org_sailfishos_nfc_ndef_complete_get_id(iface, call,
        dbus_service_ndef_bytes_as_variant(self, &ndef->id));
    return TRUE;
}

static
gboolean
dbus_service_ndef_handle_get_payload(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    NdefRec* ndef = self->rec;

    org_sailfishos_nfc_ndef_complete_get_payload(iface, call,
        dbus_service_ndef_bytes_as_variant(self, &ndef->payload));
    return TRUE;
}

static
gboolean
dbus_service_ndef_handle_get_raw_data(
    OrgSailfishosNfcNDEF* iface,
    GDBusMethodInvocation* call,
    DBusServiceNdef* self)
{
    NdefRec* ndef = self->rec;

    org_sailfishos_nfc_ndef_complete_get_raw_data(iface, call,
        dbus_service_ndef_bytes_as_variant(self, &ndef->raw));
    return TRUE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_ndef_free_unexported(
    DBusServiceNdef* self)
{
    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);
    g_object_unref(self->iface);
    g_object_unref(self->connection);

    ndef_rec_unref(self->rec);
    g_variant_unref(self->empty_ay);
    g_free(self->path);
    g_slice_free(DBusServiceNdef, self);
}

const char*
dbus_service_ndef_path(
    DBusServiceNdef* self)
{
    return self->path;
}

DBusServiceNdef*
dbus_service_ndef_new(
    NdefRec* rec,
    const char* path,
    GDBusConnection* connection)
{
    DBusServiceNdef* self = g_slice_new0(DBusServiceNdef);
    GError* error = NULL;

    g_object_ref(self->connection = connection);
    self->path = g_strdup(path);
    self->rec = ndef_rec_ref(rec);
    self->iface = org_sailfishos_nfc_ndef_skeleton_new();
    self->empty_ay = g_variant_ref_sink(g_variant_new_from_data
        (G_VARIANT_TYPE("ay"), NULL, 0, TRUE, NULL, NULL));

    /* D-Bus calls */
    self->call_id[CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_service_ndef_handle_get_all), self);
    self->call_id[CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_service_ndef_handle_get_interface_version), self);
    self->call_id[CALL_GET_FLAGS] =
        g_signal_connect(self->iface, "handle-get-flags",
        G_CALLBACK(dbus_service_ndef_handle_get_flags), self);
    self->call_id[CALL_GET_TNF] =
        g_signal_connect(self->iface, "handle-get-type-name-format",
        G_CALLBACK(dbus_service_ndef_handle_get_type_name_format), self);
    self->call_id[CALL_GET_INTERFACES] =
        g_signal_connect(self->iface, "handle-get-interfaces",
        G_CALLBACK(dbus_service_ndef_handle_get_interfaces), self);
    self->call_id[CALL_GET_TYPE] =
        g_signal_connect(self->iface, "handle-get-type",
        G_CALLBACK(dbus_service_ndef_handle_get_type), self);
    self->call_id[CALL_GET_ID] =
        g_signal_connect(self->iface, "handle-get-id",
        G_CALLBACK(dbus_service_ndef_handle_get_id), self);
    self->call_id[CALL_GET_PAYLOAD] =
        g_signal_connect(self->iface, "handle-get-payload",
        G_CALLBACK(dbus_service_ndef_handle_get_payload), self);
    self->call_id[CALL_GET_RAW_DATA] =
        g_signal_connect(self->iface, "handle-get-raw-data",
        G_CALLBACK(dbus_service_ndef_handle_get_raw_data), self);

    /* Export the interface */
    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, self->path, &error)) {
        GDEBUG("Created D-Bus object %s", self->path);
        return self;
    } else {
        GERR("%s: %s", self->path, GERRMSG(error));
        g_error_free(error);
        dbus_service_ndef_free_unexported(self);
        return NULL;
    }
}

void
dbus_service_ndef_free(
    DBusServiceNdef* self)
{
    if (self) {
        GDEBUG("Removing D-Bus object %s", self->path);
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON
            (self->iface));
        dbus_service_ndef_free_unexported(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
