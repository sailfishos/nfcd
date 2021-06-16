/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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

#include "dbus_service.h"
#include "dbus_service_util.h"
#include "dbus_service/org.sailfishos.nfc.Tag.h"

#include <nfc_tag.h>
#include <nfc_tag_t2.h>
#include <nfc_tag_t4.h>
#include <nfc_target.h>
#include <nfc_ndef.h>

#include <gutil_idlepool.h>
#include <gutil_macros.h>
#include <gutil_misc.h>

enum {
    TARGET_SEQUENCE,
    TARGET_EVENT_COUNT
};

enum {
    TAG_INITIALIZED,
    TAG_EVENT_COUNT
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
    CALL_ACQUIRE,
    CALL_RELEASE,
    CALL_GET_ALL3,
    CALL_GET_POLL_PARAMETERS,
    CALL_TRANSCEIVE,
    CALL_ACQUIRE2,
    CALL_RELEASE2,
    CALL_COUNT
};

typedef struct dbus_service_tag_priv DBusServiceTagPriv;
typedef struct dbus_service_tag_call DBusServiceTagCall;

typedef
void
(*DBusServiceTagCallFunc)(
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self);

typedef
void
(*DBusServiceTagCallCompleteFunc)(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call);

struct dbus_service_tag_call {
    DBusServiceTagCall* next;
    GDBusMethodInvocation* invocation;
    DBusServiceTagCallFunc func;
};

typedef struct dbus_service_tag_call_queue {
    DBusServiceTagCall* first;
    DBusServiceTagCall* last;
} DBusServiceTagCallQueue;

typedef struct dbus_service_tag_lock {
    char* name;
    guint watch_id;
    guint count;
    NfcTargetSequence* seq;
    DBusServiceTagPriv* tag;
} DBusServiceTagLock;

typedef struct dbus_service_tag_lock_waiter {
    DBusServiceTagLock* lock;
    GSList* pending_calls;  /* GDBusMethodInvocation references */
    DBusServiceTagCallCompleteFunc complete;
} DBusServiceTagLockWaiter;

typedef struct dbus_service_tag_async_call {
    OrgSailfishosNfcTag* iface;
    GDBusMethodInvocation* call;
} DBusServiceTagAsyncCall;

struct dbus_service_tag_priv {
    DBusServiceTag pub;
    char* path;
    OrgSailfishosNfcTag* iface;
    GUtilIdlePool* pool;
    GSList* lock_waiters;
    DBusServiceTagLock* lock;
    DBusServiceTagCallQueue queue;
    GSList* ndefs;
    gulong target_event_id[TARGET_EVENT_COUNT];
    gulong tag_event_id[TAG_EVENT_COUNT];
    gulong call_id[CALL_COUNT];
    const char** interfaces;
    DBusServiceTagType2* t2;
    DBusServiceIsoDep* isodep;
};

#define NFC_DBUS_TAG_INTERFACE "org.sailfishos.nfc.Tag"
#define NFC_DBUS_TAG_INTERFACE_VERSION  (5)

static const char* const dbus_service_tag_default_interfaces[] = {
    NFC_DBUS_TAG_INTERFACE, NULL
};

static inline DBusServiceTagPriv* dbus_service_tag_cast(DBusServiceTag* pub)
    { return G_LIKELY(pub) ? G_CAST(pub, DBusServiceTagPriv, pub) : NULL; }

static
gboolean
dbus_service_tag_lock_matches(
    DBusServiceTagLock* lock,
    const char* name,
    NFC_SEQUENCE_FLAGS flags)
{
    return lock && !g_strcmp0(lock->name, name) &&
        nfc_target_sequence_flags(lock->seq) == flags;
}

static
DBusServiceTagLockWaiter*
dbus_service_tag_find_waiter(
    DBusServiceTagPriv* self,
    const char* name,
    NFC_SEQUENCE_FLAGS flags)
{
    GSList* l;

    for (l = self->lock_waiters; l; l = l->next) {
        DBusServiceTagLockWaiter* waiter = l->data;

        if (dbus_service_tag_lock_matches(waiter->lock, name, flags)) {
            return waiter;
        }
    }
    return NULL;
}

NfcTargetSequence*
dbus_service_tag_sequence(
    DBusServiceTag* pub,
    GDBusMethodInvocation* call)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);

    if (G_LIKELY(pub) && G_LIKELY(sender)) {
        DBusServiceTagPriv* self = dbus_service_tag_cast(pub);

        if (self->lock && !g_strcmp0(self->lock->name, sender)) {
            return self->lock->seq;
        }
    }
    return NULL;
}

static
GVariant*
dbus_service_tag_get_poll_parameters(
    NfcTag* tag,
    const NfcParamPoll* poll)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

    if (G_LIKELY(tag) && G_UNLIKELY(poll)) {
        const NfcTarget* target = tag->target;
        const NfcParamPollA* poll_a = NULL;
        const NfcParamPollB* poll_b = NULL;

        if (G_LIKELY(target)) {
            switch(tag->target->technology) {
            case NFC_TECHNOLOGY_B:
                poll_b = &poll->b;
                dbus_service_dict_add_byte_array(&builder, "APPDATA",
                    poll_b->app_data, sizeof(poll_b->app_data));
                if (poll_b->prot_info.bytes) {
                    dbus_service_dict_add_byte_array_data(&builder, "PROTINFO",
                        &poll_b->prot_info);
                }
                if (poll_b->nfcid0.bytes) {
                    dbus_service_dict_add_byte_array_data(&builder, "NFCID0",
                        &poll_b->nfcid0);
                }
                break;
            case NFC_TECHNOLOGY_A:
                poll_a = &poll->a;
                dbus_service_dict_add_byte(&builder, "SEL_RES",
                    poll_a->sel_res);
                if (poll_a->nfcid1.bytes) {
                    dbus_service_dict_add_byte_array_data(&builder, "NFCID1",
                        &poll_a->nfcid1);
                }
                break;
            case NFC_TECHNOLOGY_F:
            case NFC_TECHNOLOGY_UNKNOWN:
                break;
            }
        }
    }
    return g_variant_builder_end(&builder);
}

static
void
dbus_service_tag_lock_cancel_acquire(
    gpointer data)
{
    GDBusMethodInvocation* acquire = data;

    g_dbus_method_invocation_return_error_literal(acquire,
        DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_ABORTED, "Not locked");
    g_object_unref(acquire);
}

static
void
dbus_service_tag_lock_free(
    DBusServiceTagLock* lock)
{
    if (G_LIKELY(lock)) {
        nfc_target_sequence_free(lock->seq);
        g_bus_unwatch_name(lock->watch_id);
        g_free(lock->name);
        gutil_slice_free(lock);
    }
}

static
void
dbus_service_tag_lock_waiter_free(
    DBusServiceTagLockWaiter* waiter)
{
    g_slist_free_full(waiter->pending_calls,
        dbus_service_tag_lock_cancel_acquire);
    gutil_slice_free(waiter);
    /* DBusServiceTagLock is freed separately */
}

static
void
dbus_service_tag_lock_waiter_free1(
    gpointer data)
{
    DBusServiceTagLockWaiter* waiter = data;

    dbus_service_tag_lock_free(waiter->lock);
    dbus_service_tag_lock_waiter_free(waiter);
}

static
void
dbus_service_tag_target_sequence_changed(
    NfcTarget* target,
    void* user_data)
{
    DBusServiceTagPriv* self = user_data;

    /*
     * Once the lock has been acquired, it remains acquired until we
     * explicitly delete self->lock (and its associated NfcTargetSequence).
     * Therefore, self->lock can't be NULL here.
     */
    GASSERT(!self->lock);
    GVERBOSE_("%p", target->sequence);
    if (target->sequence) {
        GSList* l;

        for (l = self->lock_waiters; l; l = l->next) {
            DBusServiceTagLockWaiter* waiter = l->data;
            DBusServiceTagLock* lock = waiter->lock;

            if (lock->seq == target->sequence) {
                self->lock_waiters = g_slist_delete_link(self->lock_waiters, l);
                GDEBUG("%s owns %s", lock->name, self->path);

                /*
                 * Number of waiters (must be positive) becomes the lock's
                 * reference count.
                 */
                self->lock = lock;
                lock->count = g_slist_length(waiter->pending_calls);
                GASSERT(lock->count);

                /* Complete all pending Acquire(2) calls */
                for (l = waiter->pending_calls; l; l = l->next) {
                    GDBusMethodInvocation* acquire = l->data;

                    waiter->complete(self->iface, acquire);
                    g_object_unref(acquire);
                }
                g_slist_free(waiter->pending_calls);
                waiter->pending_calls = NULL;
                dbus_service_tag_lock_waiter_free(waiter);
                break;
            }
        }
    }
}

static
void
dbus_service_tag_lock_peer_vanished(
    GDBusConnection* bus,
    const char* name,
    gpointer data)
{
    DBusServiceTagLock* lock = data;
    DBusServiceTagPriv* self = lock->tag;

    if (self->lock == lock) {
        /* This owner of the current lock is gone */
        self->lock = NULL;
        GDEBUG("Name '%s' has disappeared, releasing the lock", name);
    } else {
        GSList* l;

        /* Dispose of the dead waiter */
        for (l = self->lock_waiters; l; l = l->next) {
            DBusServiceTagLockWaiter* waiter = l->data;

            if (waiter->lock == lock) {
                GDEBUG("Name '%s' has disappeared, dropping the waiter", name);
                self->lock_waiters = g_slist_delete_link(self->lock_waiters, l);
                dbus_service_tag_lock_waiter_free(waiter);
                break;
            }
        }
    }

    /*
     * Delete the orphaned lock and its associated NfcTargetSequence.
     * That may cause another sequence to become the current one,
     * which results in dbus_service_tag_target_sequence_changed()
     * being called and tag->lock becoming non-NULL when this
     * function returns.
     */
    dbus_service_tag_lock_free(lock);
}

static
void
dbus_service_tag_export_all(
    DBusServiceTagPriv* self)
{
    DBusServiceTag* pub = &self->pub;
    NfcTag* tag = pub->tag;
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
            ndef = dbus_service_ndef_new(rec, buf->str, pub->connection);
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
        self->t2 = dbus_service_tag_t2_new(NFC_TAG_T2(tag), pub);
        if (self->t2) {
            const char* iface = NFC_DBUS_TAG_T2_INTERFACE;
            GDEBUG("Adding %s", iface);
            g_ptr_array_add(interfaces, (gpointer)iface);
        }
    }

    if (NFC_IS_TAG_T4(tag)) {
        self->isodep = dbus_service_isodep_new(NFC_TAG_T4(tag), pub);
        if (self->isodep) {
            const char* iface = NFC_DBUS_ISODEP_INTERFACE;
            GDEBUG("Adding %s", iface);
            g_ptr_array_add(interfaces, (gpointer)iface);
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
    DBusServiceTagPriv* self)
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
    DBusServiceTagPriv* self,
    GDBusMethodInvocation* call,
    DBusServiceTagCallFunc func)
{
    if (self->pub.tag->flags & NFC_TAG_FLAG_INITIALIZED) {
        func(call, self);
    } else {
        dbus_service_tag_queue_call(&self->queue, call, func);
    }
    return TRUE;
}

static
void
dbus_service_tag_complete_pending_calls(
    DBusServiceTagPriv* self)
{
    DBusServiceTagCall* call;

    while ((call = dbus_service_tag_dequeue_call(&self->queue)) != NULL) {
        call->func(call->invocation, self);
        dbus_service_tag_free_call(call);
    }
}

static
void
dbus_service_tag_acquire(
    DBusServiceTagPriv* self,
    GDBusMethodInvocation* call,
    gboolean wait,
    NFC_SEQUENCE_FLAGS flags,
    DBusServiceTagCallCompleteFunc complete)
{
    DBusServiceTag* pub = &self->pub;
    NfcTarget* target = pub->tag->target;
    DBusServiceTagLock* current_lock = self->lock;
    const char* name = g_dbus_method_invocation_get_sender(call);

    if (dbus_service_tag_lock_matches(current_lock, name, flags)) {
        /* This client already has the lock */
        current_lock->count++;
        GDEBUG("Lock request from %s flags 0x%02x (%u)", name, flags,
            current_lock->count);
        complete(self->iface, call);
    } else if (current_lock && !wait) {
        /* Another client already has the lock but we can't wait */
        GDEBUG("Lock request from %s (non-waitable, failed)", name);
        g_dbus_method_invocation_return_error_literal(call, DBUS_SERVICE_ERROR,
            DBUS_SERVICE_ERROR_FAILED, "Already locked");
    } else {
        DBusServiceTagLockWaiter* waiter =
            dbus_service_tag_find_waiter(self, name, flags);

        GDEBUG("Lock request from %s flags 0x%02x (waiting)", name, flags);
        if (waiter) {
            /* Another waiter for the same lock */
            GASSERT(waiter->complete == complete);
            waiter->pending_calls = g_slist_append(waiter->pending_calls,
                g_object_ref(call));
        } else {
            DBusServiceTagLock* lock = g_slice_new0(DBusServiceTagLock);

            lock->name = g_strdup(name);
            lock->seq = nfc_target_sequence_new2(target, flags);
            lock->tag = self;
            lock->watch_id = g_bus_watch_name_on_connection(pub->connection,
                name, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
                dbus_service_tag_lock_peer_vanished, lock, NULL);

            GVERBOSE_("Created sequence %p flags 0x%02x for %s",
                target->sequence, flags, name);
            if (target->sequence == lock->seq) {
                /* nfc_target_sequence_new() has acquired the lock */
                GASSERT(!self->lock);
                lock->count = 1;
                self->lock = lock;
                GDEBUG("%s owns %s", name, self->path);
                complete(self->iface, call);
            } else {
                /* We actually have to wait */
                GASSERT(wait);
                waiter = g_slice_new0(DBusServiceTagLockWaiter);
                waiter->lock = lock;
                waiter->complete = complete;
                waiter->pending_calls = g_slist_append(waiter->pending_calls,
                    g_object_ref(call));
                self->lock_waiters = g_slist_append(self->lock_waiters, waiter);
            }
        }
    }
}

static
void
dbus_service_tag_release(
    DBusServiceTagPriv* self,
    GDBusMethodInvocation* call,
    NFC_SEQUENCE_FLAGS flags,
    DBusServiceTagCallCompleteFunc complete)
{
    DBusServiceTagLock* current_lock = self->lock;
    const char* name = g_dbus_method_invocation_get_sender(call);

    if (dbus_service_tag_lock_matches(current_lock, name, flags)) {
        /* Client has the lock */
        GDEBUG("%s released the lock", name);
        complete(self->iface, call);
        current_lock->count--;
        if (!current_lock->count) {
            self->lock = NULL;
            dbus_service_tag_lock_free(current_lock);
        }
    } else {
        DBusServiceTagLockWaiter* waiter =
            dbus_service_tag_find_waiter(self, name, flags);

        if (waiter) {
            /* Cancel one pending Acquire call */
            GDBusMethodInvocation* acquire = waiter->pending_calls->data;

            GDEBUG("%s drops the lock 0x%02x", name, flags);
            dbus_service_tag_lock_cancel_acquire(acquire);
            waiter->pending_calls = g_slist_remove(waiter->pending_calls,
                acquire);

            /* Complete this call */
            complete(self->iface, call);

            /* If no more requests is pending, delete the waiter */
            if (!waiter->pending_calls) {
                self->lock_waiters = g_slist_remove(self->lock_waiters, waiter);
                dbus_service_tag_lock_free(waiter->lock);
                dbus_service_tag_lock_waiter_free(waiter);
            }
        } else {
            GDEBUG("%s doesn't have lock 0x%02x", name, flags);
            g_dbus_method_invocation_return_error_literal(call,
                DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_NOT_FOUND,
                "Not locked");
        }
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
    DBusServiceTagPriv* self = user_data;

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
    DBusServiceTagPriv* self)
{
    NfcTag* tag = self->pub.tag;
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
    DBusServiceTagPriv* self)
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
    DBusServiceTagPriv* self)
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
    DBusServiceTagPriv* self)
{
    org_sailfishos_nfc_tag_complete_get_present(iface, call,
        self->pub.tag->present);
    return TRUE;
}

/* GetTechnology */

static
gboolean
dbus_service_tag_handle_get_technology(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    org_sailfishos_nfc_tag_complete_get_technology(iface, call,
        self->pub.tag->target->technology);
    return TRUE;
}

/* GetProtocol */

static
gboolean
dbus_service_tag_handle_get_protocol(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    org_sailfishos_nfc_tag_complete_get_protocol(iface, call,
        self->pub.tag->target->protocol);
    return TRUE;
}

/* GetType */

static
gboolean
dbus_service_tag_handle_get_type(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    org_sailfishos_nfc_tag_complete_get_type(iface, call,
        self->pub.tag->type);
    return TRUE;
}

/* GetInterfaces */

static
void
dbus_service_tag_complete_get_interfaces(
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
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
    DBusServiceTagPriv* self)
{
    return dbus_service_tag_handle_call(self, call,
        dbus_service_tag_complete_get_interfaces);
}

/* GetNdefRecords */

static
void
dbus_service_tag_complete_get_ndef_records(
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    org_sailfishos_nfc_tag_complete_get_ndef_records(self->iface, call,
        dbus_service_tag_get_ndef_rec_paths(self));
}

static
gboolean
dbus_service_tag_handle_get_ndef_records(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
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
    DBusServiceTagPriv* self)
{
    nfc_tag_deactivate(self->pub.tag);
    org_sailfishos_nfc_tag_complete_deactivate(iface, call);
    return TRUE;
}

/* Acquire */

static
gboolean
dbus_service_tag_handle_acquire(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    gboolean wait,
    DBusServiceTagPriv* self)
{
    dbus_service_tag_acquire(self, call, wait, NFC_SEQUENCE_FLAGS_NONE,
        org_sailfishos_nfc_tag_complete_acquire);
    return TRUE;
}

/* Release */

static
gboolean
dbus_service_tag_handle_release(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    dbus_service_tag_release(self, call, NFC_SEQUENCE_FLAGS_NONE,
        org_sailfishos_nfc_tag_complete_release);
    return TRUE;
}

/* Interface Version 3 */

/* GetAll3 */

static
void
dbus_service_tag_complete_get_all3(
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    NfcTag* tag = self->pub.tag;
    NfcTarget* target = tag->target;

    org_sailfishos_nfc_tag_complete_get_all3(self->iface, call,
        NFC_DBUS_TAG_INTERFACE_VERSION, tag->present, target->technology,
        target->protocol, tag->type, self->interfaces ? self->interfaces :
        dbus_service_tag_default_interfaces,
        dbus_service_tag_get_ndef_rec_paths(self),
        dbus_service_tag_get_poll_parameters(tag, nfc_tag_param(tag)));
}

static
gboolean
dbus_service_tag_handle_get_all3(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    /* Queue the call if the tag is not initialized yet */
    return dbus_service_tag_handle_call(self, call,
        dbus_service_tag_complete_get_all3);
}

/* GetPollParameters */

static
gboolean
dbus_service_tag_handle_get_poll_parameters(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    NfcTag* tag = self->pub.tag;

    org_sailfishos_nfc_tag_complete_get_poll_parameters(iface, call,
        dbus_service_tag_get_poll_parameters(tag, nfc_tag_param(tag)));
    return TRUE;
}

/* Interface Version 4 */

static
void
dbus_service_tag_async_free(
    DBusServiceTagAsyncCall* async)
{
    g_object_unref(async->iface);
    g_object_unref(async->call);
    gutil_slice_free(async);
}

static
void
dbus_service_tag_async_destroy(
    void* async)
{
    dbus_service_tag_async_free((DBusServiceTagAsyncCall*)async);
}

static
void
dbus_service_tag_handle_transmit_complete(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    DBusServiceTagAsyncCall* async = user_data;

    switch (status) {
    case NFC_TRANSMIT_STATUS_OK:
        org_sailfishos_nfc_tag_complete_transceive(async->iface, async->call,
            dbus_service_dup_byte_array_as_variant(data, len));
        break;
    case NFC_TRANSMIT_STATUS_NACK:
    case NFC_TRANSMIT_STATUS_ERROR:
    case NFC_TRANSMIT_STATUS_CORRUPTED:
    case NFC_TRANSMIT_STATUS_TIMEOUT:
        g_dbus_method_invocation_return_error_literal(async->call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Transmission failed");
        break;
    }
}

static
gboolean
dbus_service_tag_handle_transceive(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    GVariant* data,
    DBusServiceTag* self)
{
    NfcTag* tag = self->tag;
    DBusServiceTagAsyncCall* async = g_slice_new(DBusServiceTagAsyncCall);

    g_object_ref(async->iface = iface);
    g_object_ref(async->call = call);
    if (!nfc_target_transmit(tag->target,
        g_variant_get_data(data), g_variant_get_size(data),
        dbus_service_tag_sequence(self, call),
        dbus_service_tag_handle_transmit_complete,
        dbus_service_tag_async_destroy, async)) {
        dbus_service_tag_async_free(async);
        g_dbus_method_invocation_return_error_literal(call,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_FAILED,
            "Failed to send data to the target");
    }
    return TRUE;
}

/* Interface Version 5 */

/* Acquire */

static
gboolean
dbus_service_tag_handle_acquire2(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    gboolean wait,
    DBusServiceTagPriv* self)
{
    dbus_service_tag_acquire(self, call, wait,
        NFC_SEQUENCE_FLAG_ALLOW_PRESENCE_CHECK,
        org_sailfishos_nfc_tag_complete_acquire2);
    return TRUE;
}

/* Release */

static
gboolean
dbus_service_tag_handle_release2(
    OrgSailfishosNfcTag* iface,
    GDBusMethodInvocation* call,
    DBusServiceTagPriv* self)
{
    dbus_service_tag_release(self, call,
        NFC_SEQUENCE_FLAG_ALLOW_PRESENCE_CHECK,
        org_sailfishos_nfc_tag_complete_release2);
    return TRUE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

static
void
dbus_service_tag_free_unexported(
    DBusServiceTagPriv* self)
{
    DBusServiceTag* pub = &self->pub;
    NfcTag* tag = pub->tag;
    DBusServiceTagCall* call;

    nfc_target_remove_all_handlers(tag->target, self->target_event_id);
    nfc_tag_remove_all_handlers(tag, self->tag_event_id);

    g_slist_free_full(self->ndefs, dbus_service_tag_free_ndef_rec);
    g_slist_free_full(self->lock_waiters, dbus_service_tag_lock_waiter_free1);
    dbus_service_isodep_free(self->isodep);
    dbus_service_tag_t2_free(self->t2);
    dbus_service_tag_lock_free(self->lock);

    nfc_tag_unref(tag);

    gutil_disconnect_handlers(self->iface, self->call_id, CALL_COUNT);

    /* Cancel pending calls if there are any */
    while ((call = dbus_service_tag_dequeue_call(&self->queue)) != NULL) {
        g_dbus_method_invocation_return_error_literal(call->invocation,
            DBUS_SERVICE_ERROR, DBUS_SERVICE_ERROR_ABORTED, "Object is gone");
        dbus_service_tag_free_call(call);
    }

    g_object_unref(self->iface);
    g_object_unref(pub->connection);

    gutil_idle_pool_destroy(self->pool);

    g_free(self->interfaces);
    g_free(self->path);
    g_free(self);
}

DBusServiceTag*
dbus_service_tag_new(
    NfcTag* tag,
    const char* parent_path,
    GDBusConnection* connection)
{
    DBusServiceTagPriv* self = g_new0(DBusServiceTagPriv, 1);
    DBusServiceTag* pub = &self->pub;
    GError* error = NULL;

    g_object_ref(pub->connection = connection);
    pub->path = self->path = g_strconcat(parent_path, "/", tag->name, NULL);
    pub->tag = nfc_tag_ref(tag);
    self->pool = gutil_idle_pool_new();
    self->iface = org_sailfishos_nfc_tag_skeleton_new();

    /* NfcTarget events */
    self->target_event_id[TARGET_SEQUENCE] =
        nfc_target_add_sequence_handler(tag->target,
            dbus_service_tag_target_sequence_changed, self);

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
    self->call_id[CALL_ACQUIRE] =
        g_signal_connect(self->iface, "handle-acquire",
        G_CALLBACK(dbus_service_tag_handle_acquire), self);
    self->call_id[CALL_RELEASE] =
        g_signal_connect(self->iface, "handle-release",
        G_CALLBACK(dbus_service_tag_handle_release), self);
    self->call_id[CALL_GET_ALL3] =
        g_signal_connect(self->iface, "handle-get-all3",
        G_CALLBACK(dbus_service_tag_handle_get_all3), self);
    self->call_id[CALL_GET_POLL_PARAMETERS] =
        g_signal_connect(self->iface, "handle-get-poll-parameters",
        G_CALLBACK(dbus_service_tag_handle_get_poll_parameters), self);
    self->call_id[CALL_TRANSCEIVE] =
        g_signal_connect(self->iface, "handle-transceive",
        G_CALLBACK(dbus_service_tag_handle_transceive), self);
    self->call_id[CALL_ACQUIRE2] =
        g_signal_connect(self->iface, "handle-acquire2",
        G_CALLBACK(dbus_service_tag_handle_acquire2), self);
    self->call_id[CALL_RELEASE2] =
        g_signal_connect(self->iface, "handle-release2",
        G_CALLBACK(dbus_service_tag_handle_release2), self);

    if (tag->flags & NFC_TAG_FLAG_INITIALIZED) {
        dbus_service_tag_export_all(self);
    } else {
        /* Otherwise have to wait until the tag is initialized */
        self->tag_event_id[TAG_INITIALIZED] =
            nfc_tag_add_initialized_handler(tag,
                dbus_service_tag_initialized, self);
    }
    if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON
        (self->iface), connection, self->path, &error)) {
        GDEBUG("Created D-Bus object %s", self->path);
        return pub;
    } else {
        GERR("%s: %s", self->path, GERRMSG(error));
        g_error_free(error);
        dbus_service_tag_free_unexported(self);
        return NULL;
    }
}

void
dbus_service_tag_free(
    DBusServiceTag* pub)
{
    if (pub) {
        DBusServiceTagPriv* self = dbus_service_tag_cast(pub);

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
