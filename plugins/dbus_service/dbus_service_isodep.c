/*
 * Copyright (C) 2019 Jolla Ltd.
 * Copyright (C) 2019 Slava Monich <slava.monich@jolla.com>
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
#include "dbus_service/org.sailfishos.nfc.IsoDep.h"

#include <nfc_tag_t4.h>

#include <gutil_misc.h>

enum {
    CALL_GET_ALL,
    CALL_GET_INTERFACE_VERSION,
    CALL_TRANSMIT,
    CALL_COUNT
};

struct dbus_service_isodep {
    DBusServiceTag* owner;
    OrgSailfishosNfcIsoDep* iface;
    NfcTagType4* t4;
    gulong call_id[CALL_COUNT];
};

#define NFC_DBUS_ISODEP_INTERFACE_VERSION  (1)

typedef struct dbus_service_isodep_async_call {
    OrgSailfishosNfcIsoDep* iface;
    GDBusMethodInvocation* call;
} DBusServiceIsoDepAsyncCall;

static
GVariant*
dbus_service_isodep_dup_data_as_variant(
    const void* data,
    guint size)
{
    return size ?
        g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, size, 1) :
        g_variant_new_from_data(G_VARIANT_TYPE("ay"), NULL, 0, TRUE,
            NULL, NULL);
}

static
NfcTargetSequence*
dbus_service_isodep_sequence(
    DBusServiceIsoDep* self,
    GDBusMethodInvocation* call)
{
    return dbus_service_tag_sequence(self->owner,
        g_dbus_method_invocation_get_sender(call));
}

/*==========================================================================*
 * Async call context
 *==========================================================================*/

static
DBusServiceIsoDepAsyncCall*
dbus_service_isodep_async_call_new(
    OrgSailfishosNfcIsoDep* iface,
    GDBusMethodInvocation* call)
{
    DBusServiceIsoDepAsyncCall* async = g_slice_new(DBusServiceIsoDepAsyncCall);

    g_object_ref(async->iface = iface);
    g_object_ref(async->call = call);
    return async;
}

static
void
dbus_service_isodep_async_call_free(
    DBusServiceIsoDepAsyncCall* async)
{
    g_object_unref(async->iface);
    g_object_unref(async->call);
    g_slice_free1(sizeof(*async), async);
}

static
void
dbus_service_isodep_async_call_free1(
    void* async)
{
    dbus_service_isodep_async_call_free((DBusServiceIsoDepAsyncCall*)async);
}

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

/* GetAll */

static
gboolean
dbus_service_isodep_handle_get_all(
    OrgSailfishosNfcIsoDep* iface,
    GDBusMethodInvocation* call,
    DBusServiceIsoDep* self)
{
    org_sailfishos_nfc_iso_dep_complete_get_all(iface, call,
        NFC_DBUS_ISODEP_INTERFACE_VERSION);
    return TRUE;
}

/* GetInterfaceVersion */

static
gboolean
dbus_service_isodep_handle_get_interface_version(
    OrgSailfishosNfcIsoDep* iface,
    GDBusMethodInvocation* call,
    DBusServiceIsoDep* self)
{
    org_sailfishos_nfc_iso_dep_complete_get_interface_version(iface, call,
        NFC_DBUS_ISODEP_INTERFACE_VERSION);
    return TRUE;
}

/* Transmit */

static
void
dbus_service_isodep_handle_transmit_done(
    NfcTagType4* tag,
    guint sw,  /* 16 bits (SW1 << 8)|SW2 */
    const void* data,
    guint len,
    void* user_data)
{
    DBusServiceIsoDepAsyncCall* async = user_data;

    if (sw) {
        GDEBUG("%04X", sw);
        org_sailfishos_nfc_iso_dep_complete_transmit(async->iface, async->call,
            dbus_service_isodep_dup_data_as_variant(data, len),
            sw >> 8, sw & 0xff);
    } else {
        GDEBUG("oops");
        g_dbus_method_invocation_return_error_literal(async->call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "APDU command failed");
    }
}

static
gboolean
dbus_service_isodep_handle_transmit(
    OrgSailfishosNfcIsoDep* iface,
    GDBusMethodInvocation* call,
    guchar cla,
    guchar ins,
    guchar p1,
    guchar p2,
    GVariant* data_var,
    guint le,
    DBusServiceIsoDep* self)
{
    GUtilData data;
    DBusServiceIsoDepAsyncCall* async =
        dbus_service_isodep_async_call_new(iface, call);

    data.size = g_variant_get_size(data_var);
    data.bytes = g_variant_get_data(data_var);
    GDEBUG("%02X %02X %02X %02X (%u bytes) %02X", cla, ins, p1, p2, (guint)
        data.size, le);
    if (!nfc_isodep_transmit(self->t4, cla, ins, p1, p2, &data, le,
        dbus_service_isodep_sequence(self, call),
        dbus_service_isodep_handle_transmit_done,
        dbus_service_isodep_async_call_free1, async)) {
        dbus_service_isodep_async_call_free(async);
        g_dbus_method_invocation_return_error_literal(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Failed to submit APDU");
    }
    return TRUE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_isodep_free_unexported(
    DBusServiceIsoDep* self)
{
    nfc_tag_unref(&self->t4->tag);

    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);
    g_object_unref(self->iface);
    g_free(self);
}

DBusServiceIsoDep*
dbus_service_isodep_new(
    NfcTagType4* t4,
    DBusServiceTag* owner)
{
    GDBusConnection* connection = dbus_service_tag_connection(owner);
    const char* path = dbus_service_tag_path(owner);
    DBusServiceIsoDep* self = g_new0(DBusServiceIsoDep, 1);
    GError* error = NULL;

    nfc_tag_ref(&(self->t4 = t4)->tag);
    self->iface = org_sailfishos_nfc_iso_dep_skeleton_new();
    self->owner = owner;

    /* D-Bus calls */
    self->call_id[CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_service_isodep_handle_get_all), self);
    self->call_id[CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_service_isodep_handle_get_interface_version), self);
    self->call_id[CALL_TRANSMIT] =
        g_signal_connect(self->iface, "handle-transmit",
        G_CALLBACK(dbus_service_isodep_handle_transmit), self);

    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, path, &error)) {
        GDEBUG("Created D-Bus object %s (ISO-DEP)", path);
        return self;
    } else {
        GERR("%s: %s", path, GERRMSG(error));
        g_error_free(error);
        dbus_service_isodep_free_unexported(self);
        return NULL;
    }
}

void
dbus_service_isodep_free(
    DBusServiceIsoDep* self)
{
    if (self) {
        GDEBUG("Removing D-Bus object %s (ISO-DEP)",
            dbus_service_tag_path(self->owner));
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON
            (self->iface));
        dbus_service_isodep_free_unexported(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
