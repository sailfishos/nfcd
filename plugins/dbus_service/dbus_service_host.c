/*
 * Copyright (C) 2023 Slava Monich <slava@monich.com>
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
#include "dbus_service/org.sailfishos.nfc.Host.h"

#include <nfc_host.h>
#include <nfc_initiator.h>

#include <gutil_macros.h>
#include <gutil_misc.h>

enum {
    CALL_GET_ALL,
    CALL_GET_INTERFACE_VERSION,
    CALL_GET_PRESENT,
    CALL_GET_TECHNOLOGY,
    CALL_GET_INTERFACES,
    CALL_DEACTIVATE,
    CALL_COUNT
};

typedef struct dbus_service_host_priv {
    DBusServiceHost pub;
    char* path;
    OrgSailfishosNfcHost* iface;
    gulong call_id[CALL_COUNT];
    gulong host_gone_id;
} DBusServiceHostPriv;

#define NFC_DBUS_HOST_INTERFACE "org.sailfishos.nfc.Host"
#define NFC_DBUS_HOST_INTERFACE_VERSION  (1)

static inline DBusServiceHostPriv* dbus_service_host_cast(DBusServiceHost* pub)
    { return G_LIKELY(pub) ? G_CAST(pub, DBusServiceHostPriv, pub) : NULL; }

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

/* GetAll */

static
gboolean
dbus_service_host_handle_get_all(
    OrgSailfishosNfcHost* iface,
    GDBusMethodInvocation* call,
    DBusServiceHostPriv* self)
{
    NfcInitiator* initiator = self->pub.host->initiator;

    org_sailfishos_nfc_host_complete_get_all(self->iface, call,
        NFC_DBUS_HOST_INTERFACE_VERSION, initiator->present,
        initiator->technology);
    return TRUE;
}

/* GetInterfaceVersion */

static
gboolean
dbus_service_host_handle_get_interface_version(
    OrgSailfishosNfcHost* iface,
    GDBusMethodInvocation* call,
    DBusServiceHostPriv* self)
{
    org_sailfishos_nfc_host_complete_get_interface_version(iface, call,
        NFC_DBUS_HOST_INTERFACE_VERSION);
    return TRUE;
}

/* GetPresent */

static
gboolean
dbus_service_host_handle_get_present(
    OrgSailfishosNfcHost* iface,
    GDBusMethodInvocation* call,
    DBusServiceHostPriv* self)
{
    org_sailfishos_nfc_host_complete_get_present(iface, call,
        self->pub.host->initiator->present);
    return TRUE;
}

/* GetTechnology */

static
gboolean
dbus_service_host_handle_get_technology(
    OrgSailfishosNfcHost* iface,
    GDBusMethodInvocation* call,
    DBusServiceHostPriv* self)
{
    org_sailfishos_nfc_host_complete_get_technology(iface, call,
        self->pub.host->initiator->technology);
    return TRUE;
}

/* Deactivate */

static
gboolean
dbus_service_host_handle_deactivate(
    OrgSailfishosNfcHost* iface,
    GDBusMethodInvocation* call,
    DBusServiceHostPriv* self)
{
    nfc_host_deactivate(self->pub.host);
    org_sailfishos_nfc_host_complete_deactivate(iface, call);
    return TRUE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_host_free_unexported(
    DBusServiceHostPriv* self)
{
    DBusServiceHost* pub = &self->pub;

    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);
    nfc_host_remove_handler(pub->host, self->host_gone_id);
    nfc_host_unref(pub->host);
    g_object_unref(pub->connection);
    g_object_unref(self->iface);
    g_free(self->path);
    g_free(self);
}

DBusServiceHost*
dbus_service_host_new(
    NfcHost* host,
    const char* parent_path,
    GDBusConnection* connection)
{
    DBusServiceHostPriv* self = g_new0(DBusServiceHostPriv, 1);
    DBusServiceHost* pub = &self->pub;
    GError* error = NULL;

    g_object_ref(pub->connection = connection);
    pub->path = self->path = g_strconcat(parent_path, "/", host->name, NULL);
    pub->host = nfc_host_ref(host);
    self->iface = org_sailfishos_nfc_host_skeleton_new();

    /* D-Bus calls */
    self->call_id[CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_service_host_handle_get_all), self);
    self->call_id[CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_service_host_handle_get_interface_version), self);
    self->call_id[CALL_GET_PRESENT] =
        g_signal_connect(self->iface, "handle-get-present",
        G_CALLBACK(dbus_service_host_handle_get_present), self);
    self->call_id[CALL_GET_TECHNOLOGY] =
        g_signal_connect(self->iface, "handle-get-technology",
        G_CALLBACK(dbus_service_host_handle_get_technology), self);
    self->call_id[CALL_DEACTIVATE] =
        g_signal_connect(self->iface, "handle-deactivate",
        G_CALLBACK(dbus_service_host_handle_deactivate), self);

    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, self->path, &error)) {
        GDEBUG("Created D-Bus object %s (Host)", self->path);
        return pub;
    } else {
        GERR("%s: %s", self->path, GERRMSG(error));
        g_error_free(error);
        dbus_service_host_free_unexported(self);
        return NULL;
    }
}

void
dbus_service_host_free(
    DBusServiceHost* pub)
{
    if (pub) {
        DBusServiceHostPriv* self = dbus_service_host_cast(pub);

        GDEBUG("Removing D-Bus object %s (Host)", self->path);
        org_sailfishos_nfc_host_emit_removed(self->iface);
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON
            (self->iface));
        dbus_service_host_free_unexported(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
