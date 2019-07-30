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
#include "dbus_service/org.sailfishos.nfc.Tag.h"

#include <nfc_tag.h>
#include <nfc_tag_t2.h>
#include <nfc_target.h>
#include <nfc_ndef.h>

#include <gutil_idlepool.h>
#include <gutil_misc.h>

enum {
    EVENT_INITIALIZED,
    EVENT_COUNT
};

enum {
    CALL_GET_ALL,
    CALL_GET_INTERFACE_VERSION,
    CALL_GET_PRESENT,
    CALL_GET_TECHNOLOGY,
    CALL_GET_PROTOCOL,
    CALL_GET_TYPE,
    CALL_GET_NDEF_RECORDS,
    CALL_GET_INTERFACES,
    CALL_DEACTIVATE,
    CALL_COUNT
};

typedef
void
(*DBusServiceTagCallFunc)(
    GDBusMethodInvocation* call,
    DBusServiceTag* self);

typedef struct dbus_service_tag_call DBusServiceTagCall;
struct dbus_service_tag_call {
    DBusServiceTagCall* next;
    GDBusMethodInvocation* invocation;
    DBusServiceTagCallFunc func;
};

typedef struct dbus_service_tag_call_queue {
    DBusServiceTagCall* first;
    DBusServiceTagCall* last;
} DBusServiceTagCallQueue;

struct dbus_service_tag {
    char* path;
    GDBusConnection* connection;
    OrgSailfishosNfcTag* iface;
    GUtilIdlePool* pool;
    DBusServiceTagCallQueue queue;
    GSList* ndefs;
    NfcTag* tag;
    gulong event_id[EVENT_COUNT];
    gulong call_id[CALL_COUNT];
    const char** interfaces;
    DBusServiceTagType2* t2;
};

#define NFC_DBUS_TAG_INTERFACE "org.sailfishos.nfc.Tag"
#define NFC_DBUS_TAG_INTERFACE_VERSION  (2)

static const char* const dbus_service_tag_default_interfaces[] = {
    NFC_DBUS_TAG_INTERFACE, NULL
};

static
void
dbus_service_tag_export_all(
    DBusServiceTag* self)
{
    NfcTag* tag = self->tag;
    NfcNdefRec* rec = tag->ndef;
    GPtrArray* interfaces = g_ptr_array_new();

    /* Export NDEF records */
    if (rec) {
        GString* buf = g_string_new(self->path);
        guint base_len, i;

        g_string_append(buf, "/ndef");
        base_len = buf->len;

        for (i = 0; rec; rec = rec->next) {
            DBusServiceNdef* ndef;

            g_string_set_size(buf, base_len);
            g_string_append_printf(buf, "%u", i);
            ndef = dbus_service_ndef_new(rec, buf->str, self->connection);
            if (ndef) {
                self->ndefs = g_slist_append(self->ndefs, ndef);
                i++;
            }
        }
        g_string_free(buf, TRUE);
    }

    /* Export sub-interfaces */
    g_ptr_array_add(interfaces, (gpointer)NFC_DBUS_TAG_INTERFACE);
    if (NFC_IS_TAG_T2(tag)) {
        self->t2 = dbus_service_tag_t2_new(NFC_TAG_T2(tag), self->path,
            self->connection);
        if (self->t2) {
            g_ptr_array_add(interfaces, (gpointer)NFC_DBUS_TAG_T2_INTERFACE);
        }
    }

    GASSERT(!self->interfaces);
    g_ptr_array_add(interfaces, NULL);
    self->interfaces = (const char**)g_ptr_array_free(interfaces, FALSE);
}

static
void
dbus_service_tag_free_ndef_rec(
    void* rec)
{
    dbus_service_ndef_free((DBusServiceNdef*)rec);
}

static
const char**
dbus_service_tag_get_ndef_rec_paths(
    DBusServiceTag* self)
{
    const char** paths = g_new(const char*, g_slist_length(self->ndefs) + 1);
    GSList* l;
    int n = 0;

    for (l = self->ndefs; l; l = l->next) {
        paths[n++] = dbus_service_ndef_path((DBusServiceNdef*)(l->data));
    }
    paths[n] = NULL;
    /* Deallocated by the idle pool (actual strings are owned by records) */
    gutil_idle_pool_add(self->pool, paths, g_free);
    return paths;
}

static
void
dbus_service_tag_free_call(
    DBusServiceTagCall* call)
{
    g_object_unref(call->invocation);
    g_slice_free(DBusServiceTagCall, call);
}

static
void
dbus_service_tag_queue_call(
    DBusServiceTagCallQueue* queue,
    GDBusMethodInvocation* invocation,
    DBusServiceTagCallFunc func)
{
    DBusServiceTagCall* call = g_slice_new0(DBusServiceTagCall);

    g_object_ref(call->invocation = invocation);
    call->func = func;
    if (queue->last) {
        queue->last->next = call;
    } else {
        queue->first = call;
    }
    queue->last = call;
}

static
DBusServiceTagCall*
dbus_service_tag_dequeue_call(
    DBusServiceTagCallQueue* queue)
{
    DBusServiceTagCall* call = queue->first;

    if (call) {
        queue->first = call->next;
        if (!queue->first) {
            queue->last = NULL;
        }
    }
    return call;
}

static
gboolean
dbus_service_tag_handle_call(
    DBusServiceTag* self,
    GDBusMethodInvocation* call,
    DBusServiceTagCallFunc func)
{
    if (self->tag->flags & NFC_TAG_FLAG_INITIALIZED) {
        func(call, self);
    } else {
        dbus_service_tag_queue_call(&self->queue, call, func);
    }
    return TRUE;
}

static
void
dbus_service_tag_complete_pending_calls(
    DBusServiceTag* self)
{
    DBusServiceTagCall* call;

    while ((call = dbus_service_tag_dequeue_call(&self->queue)) != NULL) {
        call->func(call->invocation, self);
        dbus_service_tag_free_call(call);
    }
}

/*==========================================================================*
 * NfcTag events
 *==========================================================================*/

static
void
dbus_service_tag_initialized(
    NfcTag* tag,
    void* user_data)
{
    DBusServiceTag* self = user_data;

    dbus_service_tag_export_all(self);
    dbus_service_tag_complete_pending_calls(self);
}

/*==========================================================================*
 * D-Bus calls
 *==========================================================================*/

/* GetAll */

static
void
dbus_service_tag_complete_get_all(
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    NfcTag* tag = self->tag;
    NfcTarget* target = tag->target;

    org_sailfishos_nfc_tag_complete_get_all(self->iface, call,
        NFC_DBUS_TAG_INTERFACE_VERSION, tag->present, target->technology,
        target->protocol, tag->type, self->interfaces ? self->interfaces :
        dbus_service_tag_default_interfaces,
        dbus_service_tag_get_ndef_rec_paths(self));
}

static
gboolean
dbus_service_tag_handle_get_all(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    /* Queue the call if the tag is not initialized yet */
    return dbus_service_tag_handle_call(self, call,
        dbus_service_tag_complete_get_all);
}

/* GetInterfaceVersion */

static
gboolean
dbus_service_tag_handle_get_interface_version(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    org_sailfishos_nfc_tag_complete_get_interface_version(iface, call,
        NFC_DBUS_TAG_INTERFACE_VERSION);
    return TRUE;
}

/* GetPresent */

static
gboolean
dbus_service_tag_handle_get_present(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    org_sailfishos_nfc_tag_complete_get_present(iface, call,
        self->tag->present);
    return TRUE;
}

/* GetTechnology */

static
gboolean
dbus_service_tag_handle_get_technology(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    org_sailfishos_nfc_tag_complete_get_technology(iface, call,
        self->tag->target->technology);
    return TRUE;
}

/* GetProtocol */

static
gboolean
dbus_service_tag_handle_get_protocol(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    org_sailfishos_nfc_tag_complete_get_protocol(iface, call,
        self->tag->target->protocol);
    return TRUE;
}

/* GetType */

static
gboolean
dbus_service_tag_handle_get_type(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    org_sailfishos_nfc_tag_complete_get_type(iface, call,
        self->tag->type);
    return TRUE;
}

/* GetInterfaces */

static
void
dbus_service_tag_complete_get_interfaces(
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    org_sailfishos_nfc_tag_complete_get_interfaces(self->iface, call,
        self->interfaces ? self->interfaces :
        dbus_service_tag_default_interfaces);
}

static
gboolean
dbus_service_tag_handle_get_interfaces(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    return dbus_service_tag_handle_call(self, call,
        dbus_service_tag_complete_get_interfaces);
}

/* GetNdefRecords */

static
void
dbus_service_tag_complete_get_ndef_records(
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    org_sailfishos_nfc_tag_complete_get_ndef_records(self->iface, call,
        dbus_service_tag_get_ndef_rec_paths(self));
}

static
gboolean
dbus_service_tag_handle_get_ndef_records(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    /* Queue the call if the tag is not initialized yet */
    dbus_service_tag_handle_call(self, call,
        dbus_service_tag_complete_get_ndef_records);
    return TRUE;
}

/* Deactivate */

static
gboolean
dbus_service_tag_handle_deactivate(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTag* self)
{
    nfc_tag_deactivate(self->tag);
    org_sailfishos_nfc_tag_complete_deactivate(iface, call);
    return TRUE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_tag_free_unexported(
    DBusServiceTag* self)
{
    DBusServiceTagCall* call;

    dbus_service_tag_t2_free(self->t2);
    g_slist_free_full(self->ndefs, dbus_service_tag_free_ndef_rec);
    nfc_tag_remove_all_handlers(self->tag, self->event_id);
    nfc_tag_unref(self->tag);

    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);

    /* Cancel pending calls if there are any */
    while ((call = dbus_service_tag_dequeue_call(&self->queue)) != NULL) {
        g_dbus_method_invocation_return_error_literal(call->invocation,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_ABORTED, "Object is gone");
        dbus_service_tag_free_call(call);
    }

    g_object_unref(self->iface);
    g_object_unref(self->connection);

    gutil_idle_pool_drain(self->pool);
    gutil_idle_pool_unref(self->pool);

    g_free(self->interfaces);
    g_free(self->path);
    g_free(self);
}

const char*
dbus_service_tag_path(
    DBusServiceTag* self)
{
    return self->path;
}

DBusServiceTag*
dbus_service_tag_new(
    NfcTag* tag,
    const char* parent_path,
    GDBusConnection* connection)
{
    DBusServiceTag* self = g_new0(DBusServiceTag, 1);
    GError* error = NULL;

    g_object_ref(self->connection = connection);
    self->path = g_strconcat(parent_path, "/", tag->name, NULL);
    self->tag = nfc_tag_ref(tag);
    self->pool = gutil_idle_pool_new();
    self->iface = org_sailfishos_nfc_tag_skeleton_new();

    /* D-Bus calls */
    self->call_id[CALL_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_service_tag_handle_get_all), self);
    self->call_id[CALL_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_service_tag_handle_get_interface_version), self);
    self->call_id[CALL_GET_PRESENT] =
        g_signal_connect(self->iface, "handle-get-present",
        G_CALLBACK(dbus_service_tag_handle_get_present), self);
    self->call_id[CALL_GET_TECHNOLOGY] =
        g_signal_connect(self->iface, "handle-get-technology",
        G_CALLBACK(dbus_service_tag_handle_get_technology), self);
    self->call_id[CALL_GET_PROTOCOL] =
        g_signal_connect(self->iface, "handle-get-protocol",
        G_CALLBACK(dbus_service_tag_handle_get_protocol), self);
    self->call_id[CALL_GET_TYPE] =
        g_signal_connect(self->iface, "handle-get-type",
        G_CALLBACK(dbus_service_tag_handle_get_type), self);
    self->call_id[CALL_GET_INTERFACES] =
        g_signal_connect(self->iface, "handle-get-interfaces",
        G_CALLBACK(dbus_service_tag_handle_get_interfaces), self);
    self->call_id[CALL_GET_NDEF_RECORDS] =
        g_signal_connect(self->iface, "handle-get-ndef-records",
        G_CALLBACK(dbus_service_tag_handle_get_ndef_records), self);
    self->call_id[CALL_DEACTIVATE] =
        g_signal_connect(self->iface, "handle-deactivate",
        G_CALLBACK(dbus_service_tag_handle_deactivate), self);

    if (tag->flags & NFC_TAG_FLAG_INITIALIZED) {
        dbus_service_tag_export_all(self);
    } else {
        /* Otherwise have to wait until the tag is initialized */
        self->event_id[EVENT_INITIALIZED] =
            nfc_tag_add_initialized_handler(tag,
                dbus_service_tag_initialized, self);
    }
    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, self->path, &error)) {
        GDEBUG("Created D-Bus object %s", self->path);
        return self;
    } else {
        GERR("%s: %s", self->path, GERRMSG(error));
        g_error_free(error);
        dbus_service_tag_free_unexported(self);
        return NULL;
    }
}

void
dbus_service_tag_free(
    DBusServiceTag* self)
{
    if (self) {
        GDEBUG("Removing D-Bus object %s", self->path);
        org_sailfishos_nfc_tag_emit_removed(self->iface);
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON
            (self->iface));
        dbus_service_tag_free_unexported(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
