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

#include "dbus_service.h"
#include "dbus_service/org.sailfishos.nfc.TagType2.h"

#include <nfc_tag_t2.h>

#include <gutil_misc.h>

enum {
    CALL_GET_ALL,
    CALL_GET_INTERFACE_VERSION,
    CALL_GET_BLOCK_SIZE,
    CALL_GET_DATA_SIZE,
    CALL_GET_SERIAL,
    CALL_READ,
    CALL_WRITE,
    CALL_READ_DATA,
    CALL_READ_ALL_DATA,
    CALL_WRITE_DATA,
    CALL_COUNT
};

typedef
void
(*DBusServiceTagType2CompleteWriteFunc)(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    guint written);

struct dbus_service_tag_t2 {
    DBusServiceTag* owner;
    OrgSailfishosNfcTagType2* iface;
    NfcTagType2* t2;
    gulong call_id[CALL_COUNT];
    GVariant* serial;
};

#define NFC_DBUS_TAG_T2_INTERFACE_VERSION  (1)

typedef struct dbus_service_tag_t2_async_call {
    OrgSailfishosNfcTagType2* iface;
    GDBusMethodInvocation* call;
} DBusServiceTagType2AsyncCall;

static
GVariant*
dbus_service_tag_t2_dup_data_as_variant(
    const void* data,
    guint size)
{
    return size ?
        g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, size, 1) :
        g_variant_new_from_data(G_VARIANT_TYPE("ay"), NULL, 0, TRUE,
            NULL, NULL);
}

static
GVariant*
dbus_service_tag_t2_bytes_as_variant(
    NfcTagType2* t2,
    const GUtilData* data)
{
    /* We need to hold a reference to NfcTagType2 until newly created
     * variant is freed. Returns floating reference. */
    return data->bytes ?
        g_variant_new_from_data(G_VARIANT_TYPE("ay"), data->bytes, data->size,
            TRUE, g_object_unref, nfc_tag_ref(&t2->tag)) :
        g_variant_new_from_data(G_VARIANT_TYPE("ay"), NULL, 0,
            TRUE, NULL, NULL);
}

static
GVariant*
dbus_service_tag_t2_get_serial(
    DBusServiceTagType2* self)
{
    NfcTagType2* t2 = self->t2;
    
    if (!self->serial) {
        self->serial = g_variant_ref_sink(dbus_service_tag_t2_bytes_as_variant
            (t2, &t2->serial));
    }
    return  self->serial;
}

static
NfcTargetSequence*
dbus_service_tag_t2_sequence(
    DBusServiceTagType2* self,
    GDBusMethodInvocation* call)
{
    return dbus_service_tag_sequence(self->owner,
        g_dbus_method_invocation_get_sender(call));
}

/*==========================================================================*
 * Async call context
 *==========================================================================*/

static
DBusServiceTagType2AsyncCall*
dbus_service_tag_t2_async_call_new(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call)
{
    DBusServiceTagType2AsyncCall* async =
        g_slice_new(DBusServiceTagType2AsyncCall);

    g_object_ref(async->iface = iface);
    g_object_ref(async->call = call);
    return async;
}

static
void
dbus_service_tag_t2_async_call_free1(
    DBusServiceTagType2AsyncCall* async)
{
    g_object_unref(async->iface);
    g_object_unref(async->call);
    g_slice_free(DBusServiceTagType2AsyncCall, async);
}

static
void
dbus_service_tag_t2_async_call_free(
    void* user_data)
{
    dbus_service_tag_t2_async_call_free1
        ((DBusServiceTagType2AsyncCall*)user_data);
}

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

/* GetAll */

static
gboolean
dbus_service_tag_t2_handle_get_all(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagType2* self)
{
    NfcTagType2* t2 = self->t2;

    org_sailfishos_nfc_tag_type2_complete_get_all(iface, call,
        NFC_DBUS_TAG_T2_INTERFACE_VERSION, t2->block_size, t2->data_size,
        dbus_service_tag_t2_get_serial(self));
    return TRUE;
}

/* GetInterfaceVersion */

static
gboolean
dbus_service_tag_t2_handle_get_interface_version(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagType2* self)
{
    org_sailfishos_nfc_tag_type2_complete_get_interface_version(iface, call,
        NFC_DBUS_TAG_T2_INTERFACE_VERSION);
    return TRUE;
}

/* GetBlockSize */

static
gboolean
dbus_service_tag_t2_handle_get_block_size(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagType2* self)
{
    org_sailfishos_nfc_tag_type2_complete_get_block_size(iface, call,
        self->t2->block_size);
    return TRUE;
}

/* GetDataSize */

static
gboolean
dbus_service_tag_t2_handle_get_data_size(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagType2* self)
{
    org_sailfishos_nfc_tag_type2_complete_get_data_size(iface, call,
        self->t2->data_size);
    return TRUE;
}

/* GetSerial */

static
gboolean
dbus_service_tag_t2_handle_get_serial(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagType2* self)
{
    org_sailfishos_nfc_tag_type2_complete_get_serial(iface, call,
        dbus_service_tag_t2_get_serial(self));
    return TRUE;
}

/* Read */

static
void
dbus_service_tag_t2_handle_read_done(
    NfcTagType2* tag,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    DBusServiceTagType2AsyncCall* read = user_data;

    if (status == NFC_TRANSMIT_STATUS_OK) {
        org_sailfishos_nfc_tag_type2_complete_read(read->iface,
            read->call, dbus_service_tag_t2_dup_data_as_variant(data, len));
    } else {
        g_dbus_method_invocation_return_error_literal(read->call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Read failed");
    }
}

static
gboolean
dbus_service_tag_t2_handle_read(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    guint sector,
    guint block,
    DBusServiceTagType2* self)
{
    if (sector != 0) {
        g_dbus_method_invocation_return_error_literal(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_SUPPORTED,
            "Only sector 0 is supported");
    } else {
        DBusServiceTagType2AsyncCall* read =
            dbus_service_tag_t2_async_call_new(iface, call);

        if (!nfc_tag_t2_read(self->t2, sector, block,
            dbus_service_tag_t2_handle_read_done,
            dbus_service_tag_t2_async_call_free, read)) {
            dbus_service_tag_t2_async_call_free1(read);
            g_dbus_method_invocation_return_error_literal(call,
                DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
                "Read failed");
        }
    }
    return TRUE;
}

/* Write */

static
void
dbus_service_tag_t2_handle_write_done(
    NfcTagType2* tag,
    NFC_TRANSMIT_STATUS status,
    guint written,
    void* user_data)
{
    DBusServiceTagType2AsyncCall* write = user_data;

    if (written > 0 || status == NFC_TRANSMIT_STATUS_OK) {
        org_sailfishos_nfc_tag_type2_complete_write(write->iface,
            write->call, written);
    } else {
        g_dbus_method_invocation_return_error_literal(write->call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Write failed");
    }
}

static
gboolean
dbus_service_tag_t2_handle_write(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    guint sector,
    guint block,
    GVariant* data,
    DBusServiceTagType2* self)
{
    if (sector != 0) {
        g_dbus_method_invocation_return_error_literal(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_SUPPORTED,
            "Only sector 0 is supported");
    } else {
        GBytes* bytes = g_variant_get_data_as_bytes(data);
        DBusServiceTagType2AsyncCall* write =
            dbus_service_tag_t2_async_call_new(iface, call);

        if (!nfc_tag_t2_write_seq(self->t2, sector, block, bytes,
            dbus_service_tag_t2_sequence(self, call),
            dbus_service_tag_t2_handle_write_done,
            dbus_service_tag_t2_async_call_free, write)) {
            dbus_service_tag_t2_async_call_free1(write);
            g_dbus_method_invocation_return_error_literal(call,
                DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
                "Write failed");
        }
        g_bytes_unref(bytes);
    }
    return TRUE;
}

/* ReadData */

static
void
dbus_service_tag_t2_handle_read_data_done(
    NfcTagType2* t2,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    DBusServiceTagType2AsyncCall* read = user_data;

    if (status == NFC_TAG_T2_IO_STATUS_OK) {
        org_sailfishos_nfc_tag_type2_complete_read_data(read->iface,
            read->call, dbus_service_tag_t2_dup_data_as_variant(data, len));
    } else {
        switch (status) {
        case NFC_TAG_T2_IO_STATUS_BAD_BLOCK:
        case NFC_TAG_T2_IO_STATUS_BAD_SIZE:
            g_dbus_method_invocation_return_error_literal(read->call,
                DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_INVALID_ARGS,
                "Invalid read block or size");
            break;
        default:
            g_dbus_method_invocation_return_error_literal(read->call,
                DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
                "Failed to read tag data");
            break;
        }
    }
}

static
gboolean
dbus_service_tag_t2_handle_read_data(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    guint offset,
    guint maxbytes,
    DBusServiceTagType2* self)
{
    NfcTagType2* t2 = self->t2;
    DBusServiceTagType2AsyncCall* read =
        dbus_service_tag_t2_async_call_new(iface, call);

    if (!nfc_tag_t2_read_data_seq(t2, offset, maxbytes,
        dbus_service_tag_t2_sequence(self, call),
        dbus_service_tag_t2_handle_read_data_done,
        dbus_service_tag_t2_async_call_free, read)) {
        dbus_service_tag_t2_async_call_free1(read);
        g_dbus_method_invocation_return_error_literal(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Failed to read tag data");
    }
    return TRUE;
}

/* ReadAllData */

static
void
dbus_service_tag_t2_handle_read_all_data_done(
    NfcTagType2* t2,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    DBusServiceTagType2AsyncCall* read = user_data;

    if (status == NFC_TAG_T2_IO_STATUS_OK) {
        org_sailfishos_nfc_tag_type2_complete_read_all_data(read->iface,
            read->call, dbus_service_tag_t2_dup_data_as_variant(data, len));
    } else {
        g_dbus_method_invocation_return_error_literal(read->call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Failed to read tag data");
    }
}

static
gboolean
dbus_service_tag_t2_handle_read_all_data(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagType2* self)
{
    NfcTagType2* t2 = self->t2;
    DBusServiceTagType2AsyncCall* read =
        dbus_service_tag_t2_async_call_new(iface, call);

    if (!nfc_tag_t2_read_data_seq(t2, 0, t2->data_size,
        dbus_service_tag_t2_sequence(self, call),
        dbus_service_tag_t2_handle_read_all_data_done,
        dbus_service_tag_t2_async_call_free, read)) {
        dbus_service_tag_t2_async_call_free1(read);
        g_dbus_method_invocation_return_error_literal(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Failed to read tag data");
    }
    return TRUE;
}

/* WriteData */

static
void
dbus_service_tag_t2_handle_write_data_done(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    DBusServiceTagType2AsyncCall* write = user_data;

    if (written > 0 || status == NFC_TAG_T2_IO_STATUS_OK) {
        org_sailfishos_nfc_tag_type2_complete_write_data(write->iface,
            write->call, written);
    } else {
        g_dbus_method_invocation_return_error_literal(write->call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Write failed");
    }
}

static
gboolean
dbus_service_tag_t2_handle_write_data(
    OrgSailfishosNfcTagType2* iface,
    GDBusMethodInvocation* call,
    guint offset,
    GVariant* data,
    DBusServiceTagType2* self)
{
    GBytes* bytes = g_variant_get_data_as_bytes(data);
    DBusServiceTagType2AsyncCall* write =
        dbus_service_tag_t2_async_call_new(iface, call);

    if (!nfc_tag_t2_write_data_seq(self->t2, offset, bytes,
        dbus_service_tag_t2_sequence(self, call),
        dbus_service_tag_t2_handle_write_data_done,
        dbus_service_tag_t2_async_call_free, write)) {
        dbus_service_tag_t2_async_call_free1(write);
        g_dbus_method_invocation_return_error_literal(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Write failed");
    }
    g_bytes_unref(bytes);
    return TRUE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_tag_t2_free_unexported(
    DBusServiceTagType2* self)
{
    nfc_tag_unref(&self->t2->tag);

    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);
    g_object_unref(self->iface);

    if (self->serial) {
        g_variant_unref(self->serial);
    }
    g_free(self);
}

DBusServiceTagType2*
dbus_service_tag_t2_new(
    NfcTagType2* t2,
    DBusServiceTag* owner)
{
    GDBusConnection* connection = dbus_service_tag_connection(owner);
    const char* path = dbus_service_tag_path(owner);
    DBusServiceTagType2* self = g_new0(DBusServiceTagType2, 1);
    GError* error = NULL;

    nfc_tag_ref(&(self->t2 = t2)->tag);
    self->iface = org_sailfishos_nfc_tag_type2_skeleton_new();
    self->owner = owner;

    /* D-Bus calls */
    self->call_id[CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_service_tag_t2_handle_get_all), self);
    self->call_id[CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_service_tag_t2_handle_get_interface_version), self);
    self->call_id[CALL_GET_BLOCK_SIZE] =
        g_signal_connect(self->iface, "handle-get-block-size",
        G_CALLBACK(dbus_service_tag_t2_handle_get_block_size), self);
    self->call_id[CALL_GET_DATA_SIZE] =
        g_signal_connect(self->iface, "handle-get-data-size",
        G_CALLBACK(dbus_service_tag_t2_handle_get_data_size), self);
    self->call_id[CALL_GET_SERIAL] =
        g_signal_connect(self->iface, "handle-get-serial",
        G_CALLBACK(dbus_service_tag_t2_handle_get_serial), self);
    self->call_id[CALL_READ] =
        g_signal_connect(self->iface, "handle-read",
        G_CALLBACK(dbus_service_tag_t2_handle_read), self);
    self->call_id[CALL_WRITE] =
        g_signal_connect(self->iface, "handle-write",
        G_CALLBACK(dbus_service_tag_t2_handle_write), self);
    self->call_id[CALL_READ_DATA] =
        g_signal_connect(self->iface, "handle-read-data",
        G_CALLBACK(dbus_service_tag_t2_handle_read_data), self);
    self->call_id[CALL_READ_ALL_DATA] =
        g_signal_connect(self->iface, "handle-read-all-data",
        G_CALLBACK(dbus_service_tag_t2_handle_read_all_data), self);
    self->call_id[CALL_WRITE_DATA] =
        g_signal_connect(self->iface, "handle-write-data",
        G_CALLBACK(dbus_service_tag_t2_handle_write_data), self);

    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, path, &error)) {
        GDEBUG("Created D-Bus object %s (Type2)", path);
        return self;
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
        dbus_service_tag_t2_free_unexported(self);
        return NULL;
    }
}

void
dbus_service_tag_t2_free(
    DBusServiceTagType2* self)
{
    if (self) {
        GDEBUG("Removing D-Bus object %s (Type2)",
            dbus_service_tag_path(self->owner));
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON
            (self->iface));
        dbus_service_tag_t2_free_unexported(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
