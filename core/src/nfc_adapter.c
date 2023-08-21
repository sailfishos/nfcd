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
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfc_adapter_p.h"
#include "nfc_adapter_impl.h"
#include "nfc_host_p.h"
#include "nfc_initiator.h"
#include "nfc_manager_p.h"
#include "nfc_tag_p.h"
#include "nfc_tag_t4_p.h"
#include "nfc_peer_p.h"
#include "nfc_log.h"

#include <gutil_misc.h>
#include <gutil_macros.h>
#include <gutil_objv.h>
#include <gutil_weakref.h>

#include <stdlib.h>

#define NFC_TAG_NAME_FORMAT "tag%u"
#define NFC_PEER_NAME_FORMAT "peer%u"
#define NFC_HOST_NAME_FORMAT "host%u"

typedef struct nfc_adapter_object_entry {
    gpointer obj;
    gulong gone_id;
} NfcAdapterObjectEntry;

struct nfc_adapter_priv {
    char* name;
    GUtilWeakRef* manager_ref;
    GHashTable* tag_table;
    GHashTable* peer_table;
    GHashTable* host_table;
    NfcPeer** peers;
    NfcHost** hosts;
    guint next_tag_index;
    guint next_peer_index;
    guint next_host_index;
    guint32 pending_signals;
    NFC_MODE mode_submitted;
    gboolean mode_pending;
    gboolean power_submitted;
    gboolean power_pending;
};

#define THIS(obj) NFC_ADAPTER(obj)
#define THIS_TYPE NFC_TYPE_ADAPTER
#define PARENT_CLASS nfc_adapter_parent_class
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, THIS_TYPE, \
        NfcAdapterClass)

G_DEFINE_ABSTRACT_TYPE(NfcAdapter, nfc_adapter, G_TYPE_OBJECT)

enum nfc_adapter_signal {
    SIGNAL_TAG_ADDED,
    SIGNAL_TAG_REMOVED,
    SIGNAL_ENABLED_CHANGED,
    SIGNAL_POWERED,
    SIGNAL_POWER_REQUESTED,
    SIGNAL_MODE,
    SIGNAL_MODE_REQUESTED,
    SIGNAL_TARGET_PRESENCE,
    SIGNAL_PEER_ADDED,
    SIGNAL_PEER_REMOVED,
    SIGNAL_HOST_ADDED,
    SIGNAL_HOST_REMOVED,
    SIGNAL_COUNT
};

#define SIGNAL_BIT(name) (1 << SIGNAL_##name)

#define SIGNAL_TAG_ADDED_NAME           "nfc-adapter-tag-added"
#define SIGNAL_TAG_REMOVED_NAME         "nfc-adapter-tag-removed"
#define SIGNAL_ENABLED_CHANGED_NAME     "nfc-adapter-enabled-changed"
#define SIGNAL_POWERED_NAME             "nfc-adapter-powered"
#define SIGNAL_POWER_REQUESTED_NAME     "nfc-adapter-power-requested"
#define SIGNAL_MODE_NAME                "nfc-adapter-mode"
#define SIGNAL_MODE_REQUESTED_NAME      "nfc-adapter-mode-requested"
#define SIGNAL_TARGET_PRESENCE_NAME     "nfc-adapter-target-presence"
#define SIGNAL_PEER_ADDED_NAME          "nfc-adapter-peer-added"
#define SIGNAL_PEER_REMOVED_NAME        "nfc-adapter-peer-removed"
#define SIGNAL_HOST_ADDED_NAME          "nfc-adapter-host-added"
#define SIGNAL_HOST_REMOVED_NAME        "nfc-adapter-host-removed"

static guint nfc_adapter_signals[SIGNAL_COUNT] = { 0 };

#define NEW_SIGNAL(name,type) nfc_adapter_signals[SIGNAL_##name] = \
    g_signal_new(SIGNAL_##name##_NAME, type, G_SIGNAL_RUN_FIRST, \
    0, NULL, NULL, NULL, G_TYPE_NONE, 0)
#define NEW_OBJECT_SIGNAL(name,type) nfc_adapter_signals[SIGNAL_##name] = \
    g_signal_new(SIGNAL_##name##_NAME, type, G_SIGNAL_RUN_FIRST, \
    0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT)

static
void
nfc_adapter_queue_signal(
    NfcAdapter* self,
    int sig)
{
    NfcAdapterPriv* priv = self->priv;

    priv->pending_signals |= (1 << sig);
}

static
void
nfc_adapter_emit_pending_signals(
    NfcAdapter* self)
{
    NfcAdapterPriv* priv = self->priv;

    if (priv->pending_signals) {
        int sig;

        /* Handlers could drops their references to us */
        nfc_adapter_ref(self);
        for (sig = 0; priv->pending_signals && sig < SIGNAL_COUNT; sig++) {
            const guint32 signal_bit = (1 << sig);
            if (priv->pending_signals & signal_bit) {
                priv->pending_signals &= ~signal_bit;
                g_signal_emit(self, nfc_adapter_signals[sig], 0);
            }
        }
        /* And release the temporary reference */
        nfc_adapter_unref(self);
    }
}

static
int
nfc_adapter_compare_tags(
    const void* p1,
    const void* p2)
{
    NfcTag* a1 = *(NfcTag* const*)p1;
    NfcTag* a2 = *(NfcTag* const*)p2;

    return strcmp(a1->name, a2->name);
}

static
void
nfc_adapter_update_tags(
    NfcAdapter* self)
{
    NfcAdapterPriv* priv = self->priv;
    const guint count = g_hash_table_size(priv->tag_table);
    NfcTag** ptr;
    GHashTableIter iter;
    gpointer value;

    g_free(self->tags);
    ptr = self->tags = g_new(NfcTag*, count + 1);
    g_hash_table_iter_init(&iter, priv->tag_table);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        *ptr++ = NFC_TAG(((NfcAdapterObjectEntry*)value)->obj);
    }
    *ptr = NULL;

    /* Sort tags by name */
    qsort(self->tags, count, sizeof(NfcTag*), nfc_adapter_compare_tags);
}

static
int
nfc_adapter_compare_peers(
    const void* p1,
    const void* p2)
{
    NfcPeer* a1 = *(NfcPeer* const*)p1;
    NfcPeer* a2 = *(NfcPeer* const*)p2;

    return strcmp(a1->name, a2->name);
}

static
void
nfc_adapter_update_peers(
    NfcAdapterPriv* priv)
{
    const guint count = g_hash_table_size(priv->peer_table);
    NfcPeer** ptr;
    GHashTableIter iter;
    gpointer value;

    g_free(priv->peers);
    ptr = priv->peers = g_new(NfcPeer*, count + 1);
    g_hash_table_iter_init(&iter, priv->peer_table);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        *ptr++ = NFC_PEER(((NfcAdapterObjectEntry*)value)->obj);
    }
    *ptr = NULL;

    /* Sort peers by name */
    qsort(priv->peers, count, sizeof(NfcPeer*), nfc_adapter_compare_peers);
}

static
int
nfc_adapter_compare_hosts(
    const void* p1,
    const void* p2)
{
    NfcHost* c1 = *(NfcHost* const*)p1;
    NfcHost* c2 = *(NfcHost* const*)p2;

    return strcmp(c1->name, c2->name);
}

static
void
nfc_adapter_update_hosts(
    NfcAdapterPriv* priv)
{
    const guint count = g_hash_table_size(priv->host_table);
    NfcHost** ptr;
    GHashTableIter iter;
    gpointer value;

    g_free(priv->hosts);
    ptr = priv->hosts = g_new(NfcHost*, count + 1);
    g_hash_table_iter_init(&iter, priv->host_table);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        *ptr++ = NFC_HOST(((NfcAdapterObjectEntry*)value)->obj);
    }
    *ptr = NULL;

    /* Sort hosts by name */
    qsort(priv->hosts, count, sizeof(NfcHost*), nfc_adapter_compare_hosts);
}

static
void
nfc_adapter_set_presence(
    NfcAdapter* self,
    gboolean present)
{
    if (self->target_present != present) {
        self->target_present = present;
        GDEBUG("Target %s", present ? "detected" : "disappeared");
        nfc_adapter_queue_signal(self, SIGNAL_TARGET_PRESENCE);
    }
}

static
void
nfc_adapter_update_presence(
    NfcAdapter* self)
{
    NfcAdapterPriv* priv = self->priv;
    gboolean present = FALSE;
    GHashTableIter it;
    gpointer value;

    g_hash_table_iter_init(&it, priv->tag_table);
    while (g_hash_table_iter_next(&it, NULL, &value) && !present) {
        present = NFC_TAG(((NfcAdapterObjectEntry*)value)->obj)->
            present;
    }

    if (!present) {
        g_hash_table_iter_init(&it, priv->peer_table);
        while (g_hash_table_iter_next(&it, NULL, &value) && !present) {
            present = NFC_PEER(((NfcAdapterObjectEntry*)value)->obj)->
                present;
        }

        if (!present) {
            g_hash_table_iter_init(&it, priv->host_table);
            while (g_hash_table_iter_next(&it, NULL, &value) && !present) {
                present = NFC_HOST(((NfcAdapterObjectEntry*)value)->obj)->
                    initiator->present;
            }
        }
    }

    nfc_adapter_set_presence(self, present);
}

static
void
nfc_adapter_update_power(
    NfcAdapter* self)
{
    NfcAdapterClass* c = GET_THIS_CLASS(self);
    NfcAdapterPriv* priv = self->priv;
    const gboolean on = (self->power_requested && self->enabled);

    /* Cancel mode change if we are about to power the whole thing off */
    if (!on && priv->mode_pending) {
        priv->mode_pending = FALSE;
        c->cancel_mode_request(self);
    }

    if (priv->power_pending) {
        if (priv->power_submitted != on) {
            /* Request has been submitted but it hasn't completed yet.
             * Cancel it and start a fresh new one. */
            c->cancel_power_request(self);
            priv->power_submitted = on;
            priv->power_pending = TRUE;
            if (!c->submit_power_request(self, on)) {
                priv->power_pending = FALSE;
            }
        }
    } else if (self->powered != on) {
        /* No request pending, submit one */
        priv->power_submitted = on;
        priv->power_pending = TRUE;
        if (!c->submit_power_request(self, on)) {
            priv->power_pending = FALSE;
        }
    }
}

static
void
nfc_adapter_update_mode(
    NfcAdapter* self)
{
    NfcAdapterPriv* priv = self->priv;
    NfcAdapterClass* c = GET_THIS_CLASS(self);

    if (!self->powered) {
        /* Assume no polling when power is off */
        if (priv->mode_pending) {
            priv->mode_pending = FALSE;
            c->cancel_mode_request(self);
        }
        if (self->mode != NFC_MODE_NONE) {
            self->mode = NFC_MODE_NONE;
            nfc_adapter_queue_signal(self, SIGNAL_MODE);
        }
    } else if (priv->mode_pending) {
        if (priv->mode_submitted != self->mode_requested) {
            /* Request has been submitted but it hasn't completed yet.
             * Cancel it and start a fresh new one. */
            c->cancel_mode_request(self);
            priv->mode_submitted = self->mode_requested;
            if (!c->submit_mode_request(self, self->mode_requested)) {
                priv->mode_pending = FALSE;
            }
        }
    } else if (self->mode != self->mode_requested) {
        /* No request pending, submit one */
        priv->mode_submitted = self->mode_requested;
        priv->mode_pending = TRUE;
        if (!c->submit_mode_request(self, self->mode_requested)) {
            priv->mode_pending = FALSE;
        }
    }
}

static
char*
nfc_adapter_make_name(
    GHashTable* table,
    const char* format,
    guint* next_index)
{
    char* name = g_strdup_printf(format, *next_index);

    (*next_index)++;
    while (g_hash_table_contains(table, name)) {
        /* This is rather unlikely... */
        g_free(name);
        name = g_strdup_printf(format, *next_index);
        (*next_index)++;
    }
    return name;
}

static
void
nfc_adapter_object_entry_free(
    gpointer data)
{
    NfcAdapterObjectEntry* entry = data;

    g_signal_handler_disconnect(entry->obj, entry->gone_id);
    g_object_unref(entry->obj);
    gutil_slice_free(entry);
}

static
NfcManager*
nfc_adapter_get_manager(
    NfcAdapterPriv* priv)
{
    /* The returned ref must be released with nfc_manager_unref() */
    return NFC_MANAGER(gutil_weakref_get(priv->manager_ref));
}

static
void
nfc_adapter_tag_gone(
    NfcTag* tag,
    void* adapter)
{
    nfc_adapter_remove_tag(THIS(adapter), tag->name);
}

static
NfcTag*
nfc_adapter_add_tag(
    NfcAdapter* self,
    NfcTag* tag)
{
    /* This function takes ownership of the tag */
    if (tag->present) {
        NfcAdapterPriv* priv = self->priv;
        NfcAdapterObjectEntry* entry = g_slice_new(NfcAdapterObjectEntry);
        char* name = nfc_adapter_make_name(priv->tag_table,
            NFC_TAG_NAME_FORMAT, &priv->next_tag_index);

        GASSERT(!tag->name);
        nfc_tag_set_name(tag, name);
        entry->obj = tag;
        entry->gone_id = nfc_tag_add_gone_handler(tag, nfc_adapter_tag_gone,
            self);
        g_hash_table_insert(priv->tag_table, name, entry);
        nfc_adapter_update_tags(self);
        nfc_adapter_update_presence(self);
        nfc_adapter_emit_pending_signals(self);
        g_signal_emit(self, nfc_adapter_signals[SIGNAL_TAG_ADDED], 0, tag);
        return tag;
    } else {
        nfc_tag_unref(tag);
        return NULL;
    }
}

static
void
nfc_adapter_peer_gone(
    NfcPeer* peer,
    void* adapter)
{
    nfc_adapter_remove_peer(THIS(adapter), peer->name);
}

static
NfcPeer*
nfc_adapter_add_peer(
    NfcAdapter* self,
    NfcPeer* peer)
{
    /* This function takes ownership of the peer */
    if (peer && peer->present) {
        NfcAdapterPriv* priv = self->priv;
        NfcAdapterObjectEntry* entry = g_slice_new(NfcAdapterObjectEntry);
        char* name = nfc_adapter_make_name(priv->peer_table,
            NFC_PEER_NAME_FORMAT, &priv->next_peer_index);

        GASSERT(!peer->name);
        nfc_peer_set_name(peer, name);
        entry->obj = peer;
        entry->gone_id = nfc_peer_add_gone_handler(peer, nfc_adapter_peer_gone,
            self);
        g_hash_table_insert(priv->peer_table, name, entry);
        nfc_adapter_update_peers(priv);
        nfc_adapter_update_presence(self);
        nfc_adapter_emit_pending_signals(self);
        g_signal_emit(self, nfc_adapter_signals[SIGNAL_PEER_ADDED], 0, peer);
        return peer;
    } else {
        nfc_peer_unref(peer);
        return NULL;
    }
}

static
NfcPeer*
nfc_adapter_add_peer_initiator(
    NfcAdapter* self,
    NfcTarget* target,
    NFC_TECHNOLOGY technology,
    const NfcParamNfcDepInitiator* param)
{
    if (G_LIKELY(self) && G_LIKELY(target) && G_LIKELY(param)) {
        NfcAdapterPriv* priv = self->priv;
        NfcManager* manager = nfc_adapter_get_manager(priv);
        NfcPeer* peer = nfc_peer_new_initiator(target, technology, param,
            nfc_manager_peer_services(manager));

        nfc_manager_unref(manager);
        if (peer) {
            return nfc_adapter_add_peer(self, peer);
        }
    }
    return NULL;
}

static
NfcPeer*
nfc_adapter_add_peer_target(
    NfcAdapter* self,
    NfcInitiator* initiator,
    NFC_TECHNOLOGY technology,
    const NfcParamNfcDepTarget* param)
{
    if (G_LIKELY(self) && G_LIKELY(initiator) && G_LIKELY(param)) {
        NfcAdapterPriv* priv = self->priv;
        NfcManager* manager = nfc_adapter_get_manager(priv);
        NfcPeer* peer = nfc_peer_new_target(initiator, technology, param,
            nfc_manager_peer_services(manager));

        nfc_manager_unref(manager);
        if (peer) {
            return nfc_adapter_add_peer(self, peer);
        }
    }
    return NULL;
}

static
void
nfc_adapter_host_gone(
    NfcHost* host,
    void* adapter)
{
    NfcAdapter* self = THIS(adapter);
    NfcAdapterPriv* priv = self->priv;

    nfc_host_ref(host);
    if (g_hash_table_remove(priv->host_table, host->name)) {
        nfc_adapter_ref(self);
        nfc_adapter_update_hosts(priv);
        nfc_adapter_update_presence(self);
        g_signal_emit(self, nfc_adapter_signals[SIGNAL_HOST_REMOVED], 0, host);
        nfc_adapter_emit_pending_signals(self);
        nfc_adapter_unref(self);
    }
    nfc_host_unref(host);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcAdapter*
nfc_adapter_ref(
    NfcAdapter* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
    }
    return self;
}

void
nfc_adapter_unref(
    NfcAdapter* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

void
nfc_adapter_set_enabled(
    NfcAdapter* self,
    gboolean enabled)
{
    if (G_LIKELY(self) && self->enabled != enabled) {
        self->enabled = enabled;
        nfc_adapter_queue_signal(self, SIGNAL_ENABLED_CHANGED);
        nfc_adapter_update_power(self);
        nfc_adapter_emit_pending_signals(self);
    }
}

void
nfc_adapter_request_power(
    NfcAdapter* self,
    gboolean on)
{
    if (G_LIKELY(self) && self->power_requested != on) {
        self->power_requested = on;
        nfc_adapter_queue_signal(self, SIGNAL_POWER_REQUESTED);
        nfc_adapter_update_power(self);
        nfc_adapter_emit_pending_signals(self);
    }
}

gboolean
nfc_adapter_request_mode(
    NfcAdapter* self,
    NFC_MODE mode)
{
    gboolean ok = FALSE;

    if (G_LIKELY(self)) {
        /* Is anything supported? */
        if (!mode || (mode & self->supported_modes)) {
            mode &= self->supported_modes;
            if (self->mode_requested != mode) {
                self->mode_requested = mode;
                nfc_adapter_queue_signal(self, SIGNAL_MODE_REQUESTED);
                nfc_adapter_update_mode(self);
                nfc_adapter_emit_pending_signals(self);
            }
            ok = TRUE;
        } else {
            GDEBUG("Mode 0x%02x is not supported by %s %s", mode,
                G_OBJECT_TYPE_NAME(self), self->name);
        }
    }
    return ok;
}

NfcPeer**
nfc_adapter_peers(
    NfcAdapter* self) /* Since 1.1.0 */
{
    return G_LIKELY(self) ? self->priv->peers : NULL;
}

NfcHost**
nfc_adapter_hosts(
    NfcAdapter* self) /* Since 1.2.0 */
{
    return G_LIKELY(self) ? self->priv->hosts : NULL;
}

NfcTag*
nfc_adapter_add_tag_t2(
    NfcAdapter* self,
    NfcTarget* target,
    const NfcTagParamT2* params)
{
    if (G_LIKELY(self)) {
        NfcTagType2* t2 = nfc_tag_t2_new(target, params);

        if (t2) {
            return nfc_adapter_add_tag(self, NFC_TAG(t2));
        }
    }
    return NULL;
}

NfcTag*
nfc_adapter_add_tag_t4a(
    NfcAdapter* self,
    NfcTarget* target,
    const NfcParamPollA* tech_param,
    const NfcParamIsoDepPollA* iso_dep_param) /* Since 1.0.20 */
{
    if (G_LIKELY(self) && G_LIKELY(target)) {
        NfcTagType4a* t4a = nfc_tag_t4a_new(target, tech_param, iso_dep_param);

        if (t4a) {
            return nfc_adapter_add_tag(self, NFC_TAG(t4a));
        }
    }
    return NULL;
}

NfcTag*
nfc_adapter_add_tag_t4b(
    NfcAdapter* self,
    NfcTarget* target,
    const NfcParamPollB* tech_param,
    const NfcParamIsoDepPollB* iso_dep_param) /* Since 1.0.20 */
{
    if (G_LIKELY(self) && G_LIKELY(target)) {
        NfcTagType4b* t4b = nfc_tag_t4b_new(target, tech_param, iso_dep_param);

        if (t4b) {
            return nfc_adapter_add_tag(self, NFC_TAG(t4b));
        }
    }
    return NULL;
}

NfcTag*
nfc_adapter_add_other_tag(
    NfcAdapter* self,
    NfcTarget* target)
{
    return nfc_adapter_add_other_tag2(self, target, NULL);
}

NfcTag*
nfc_adapter_add_other_tag2(
    NfcAdapter* self,
    NfcTarget* target,
    const NfcParamPoll* poll) /* Since 1.0.33 */
{
    if (G_LIKELY(self)) {
        NfcTag* tag = nfc_tag_new(target, poll);

        if (tag) {
            return nfc_adapter_add_tag(self, tag);
        }
    }
    return NULL;
}

NfcPeer*
nfc_adapter_add_peer_initiator_a(
    NfcAdapter* self,
    NfcTarget* target,
    const NfcParamPollA* poll_a,
    const NfcParamNfcDepInitiator* param) /* Since 1.1.0 */
{
    return nfc_adapter_add_peer_initiator(self, target,
        NFC_TECHNOLOGY_A, param);
}

NfcPeer*
nfc_adapter_add_peer_initiator_f(
    NfcAdapter* self,
    NfcTarget* target,
    const NfcParamPollF* poll_f,
    const NfcParamNfcDepInitiator* param) /* Since 1.1.0 */
{
    return nfc_adapter_add_peer_initiator(self, target,
        NFC_TECHNOLOGY_F, param);
}

NfcPeer*
nfc_adapter_add_peer_target_a(
    NfcAdapter* self,
    NfcInitiator* initiator,
    const NfcParamListenA* listen_a,
    const NfcParamNfcDepTarget* param) /* Since 1.1.0 */
{
    return nfc_adapter_add_peer_target(self, initiator,
        NFC_TECHNOLOGY_A, param);
}

NfcPeer*
nfc_adapter_add_peer_target_f(
    NfcAdapter* self,
    NfcInitiator* initiator,
    const NfcParamListenF* listen_f,
    const NfcParamNfcDepTarget* param) /* Since 1.1.0 */
{
    return nfc_adapter_add_peer_target(self, initiator,
        NFC_TECHNOLOGY_F, param);
}

void
nfc_adapter_remove_tag(
    NfcAdapter* self,
    const char* name)
{
    if (G_LIKELY(self) && G_LIKELY(name)) {
        NfcAdapterPriv* priv = self->priv;
        NfcAdapterObjectEntry* entry = g_hash_table_lookup(priv->tag_table,
            name);

        if (entry) {
            NfcTag* tag = nfc_tag_ref(entry->obj);

            g_hash_table_remove(priv->tag_table, name);
            nfc_adapter_update_tags(self);
            nfc_adapter_update_presence(self);
            g_signal_emit(self, nfc_adapter_signals
                [SIGNAL_TAG_REMOVED], 0, tag);
            nfc_adapter_emit_pending_signals(self);
            nfc_tag_unref(tag);
        }
    }
}

void
nfc_adapter_remove_peer(
    NfcAdapter* self,
    const char* name) /* Since 1.1.0 */
{
    if (G_LIKELY(self) && G_LIKELY(name)) {
        NfcAdapterPriv* priv = self->priv;
        NfcAdapterObjectEntry* entry = g_hash_table_lookup(priv->peer_table,
            name);

        if (entry) {
            NfcPeer* peer = nfc_peer_ref(entry->obj);

            g_hash_table_remove(priv->peer_table, name);
            nfc_adapter_update_peers(priv);
            nfc_adapter_update_presence(self);
            g_signal_emit(self, nfc_adapter_signals
                [SIGNAL_PEER_REMOVED], 0, peer);
            nfc_adapter_emit_pending_signals(self);
            nfc_peer_unref(peer);
        }
    }
}

NfcHost*
nfc_adapter_add_host(
    NfcAdapter* self,
    NfcInitiator* initiator) /* Since 1.2.0 */
{
    if (G_LIKELY(self) && G_LIKELY(initiator) && initiator->present) {
        NfcAdapterPriv* priv = self->priv;
        GHashTable* table = priv->host_table;
        NfcAdapterObjectEntry* entry = g_slice_new0(NfcAdapterObjectEntry);
        char* name = nfc_adapter_make_name(table, NFC_HOST_NAME_FORMAT,
            &priv->next_host_index);
        NfcManager* manager = nfc_adapter_get_manager(priv);
        NfcHost* host = nfc_host_new(name, initiator,
            nfc_manager_host_services(manager),
            nfc_manager_host_apps(manager));

        entry->obj = host;
        entry->gone_id = nfc_host_add_gone_handler(host, nfc_adapter_host_gone,
            self);
        g_hash_table_insert(table, name, entry);
        nfc_adapter_update_hosts(priv);
        nfc_adapter_update_presence(self);
        nfc_adapter_emit_pending_signals(self);
        g_signal_emit(self, nfc_adapter_signals[SIGNAL_HOST_ADDED], 0, host);
        nfc_manager_unref(manager);
        nfc_host_start(host);
        return host;
    }
    return NULL;
}

gulong
nfc_adapter_add_target_presence_handler(
    NfcAdapter* self,
    NfcAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_TARGET_PRESENCE_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_tag_added_handler(
    NfcAdapter* self,
    NfcAdapterTagFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_TAG_ADDED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_tag_removed_handler(
    NfcAdapter* self,
    NfcAdapterTagFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_TAG_REMOVED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_peer_added_handler(
    NfcAdapter* self,
    NfcAdapterPeerFunc func,
    void* user_data) /* Since 1.1.0 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_PEER_ADDED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_peer_removed_handler(
    NfcAdapter* self,
    NfcAdapterPeerFunc func,
    void* user_data) /* Since 1.1.0 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_PEER_REMOVED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_host_added_handler(
    NfcAdapter* self,
    NfcAdapterHostFunc func,
    void* user_data) /* Since 1.2.0 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_HOST_ADDED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_host_removed_handler(
    NfcAdapter* self,
    NfcAdapterHostFunc func,
    void* user_data) /* Since 1.2.0 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_HOST_REMOVED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_powered_changed_handler(
    NfcAdapter* self,
    NfcAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_POWERED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_power_requested_handler(
    NfcAdapter* self,
    NfcAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_POWER_REQUESTED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_mode_changed_handler(
    NfcAdapter* self,
    NfcAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_MODE_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_mode_requested_handler(
    NfcAdapter* self,
    NfcAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_MODE_REQUESTED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
nfc_adapter_add_enabled_changed_handler(
    NfcAdapter* self,
    NfcAdapterFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ENABLED_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

void
nfc_adapter_remove_handler(
    NfcAdapter* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
nfc_adapter_remove_handlers(
    NfcAdapter* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

void
nfc_adapter_mode_notify(
    NfcAdapter* self,
    NFC_MODE mode,
    gboolean requested)
{
    if (G_LIKELY(self)) {
        NfcAdapterPriv* priv = self->priv;
        const gboolean request_was_pending = priv->mode_pending;

        if (requested) {
            /* Request has completed */
            priv->mode_pending = FALSE;
        }
        if (self->mode != mode) {
            self->mode = mode;
            nfc_adapter_queue_signal(self, SIGNAL_MODE);
        }
        if (request_was_pending && requested) {
            if (self->mode_requested != mode) {
                self->mode_requested = mode;
                nfc_adapter_queue_signal(self, SIGNAL_MODE_REQUESTED);
            }
        }
        nfc_adapter_emit_pending_signals(self);
    }
}

void
nfc_adapter_power_notify(
    NfcAdapter* self,
    gboolean on,
    gboolean requested)
{
    if (G_LIKELY(self)) {
        NfcAdapterPriv* priv = self->priv;
        const gboolean request_was_pending = priv->power_pending;

        if (requested) {
            /* Request has completed */
            priv->power_pending = FALSE;
        }
        if (self->powered != on) {
            self->powered = on;
            nfc_adapter_queue_signal(self, SIGNAL_POWERED);
        }
        nfc_adapter_update_mode(self);
        if (request_was_pending && requested) {
            if (self->power_requested != on) {
                self->power_requested = on;
                nfc_adapter_queue_signal(self, SIGNAL_POWER_REQUESTED);
            }
        }
        nfc_adapter_emit_pending_signals(self);
    }
}

void
nfc_adapter_target_notify(
    NfcAdapter* self,
    gboolean present /* ignored */)
{
    if (G_LIKELY(self)) {
        nfc_adapter_update_presence(self);
        nfc_adapter_emit_pending_signals(self);
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

void
nfc_adapter_set_manager_ref(
    NfcAdapter* self,
    GUtilWeakRef* ref)
{
    if (G_LIKELY(self)) {
        NfcAdapterPriv* priv = self->priv;

        gutil_weakref_unref(priv->manager_ref);
        priv->manager_ref = gutil_weakref_ref(ref);
    }
}

void
nfc_adapter_set_name(
    NfcAdapter* self,
    const char* name)
{
    if (G_LIKELY(self)) {
        NfcAdapterPriv* priv = self->priv;

        g_free(priv->name);
        self->name = priv->name = g_strdup(name);
    }
}

NFC_TECHNOLOGY
nfc_adapter_get_supported_techs(
    NfcAdapter* self)
{
    return G_LIKELY(self) ?
        GET_THIS_CLASS(self)->get_supported_techs(self) :
        NFC_TECHNOLOGY_UNKNOWN;
}

void
nfc_adapter_set_allowed_techs(
    NfcAdapter* self,
    NFC_TECHNOLOGY techs)
{
    GET_THIS_CLASS(self)->set_allowed_techs(self, techs);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
gboolean
nfc_adapter_default_submit_power_request(
    NfcAdapter* self,
    gboolean on)
{
    return FALSE;
}

static
gboolean
nfc_adapter_default_submit_mode_request(
    NfcAdapter* self,
    NFC_MODE mode)
{
    return FALSE;
}

static
void
nfc_adapter_default_cancel_request(
    NfcAdapter* self)
{
}

static
NFC_TECHNOLOGY
nfc_adapter_default_get_supported_techs(
    NfcAdapter* self)
{
    return NFC_TECHNOLOGY_A | NFC_TECHNOLOGY_B;
}

static
void
nfc_adapter_default_set_allowed_techs(
    NfcAdapter* self,
    NFC_TECHNOLOGY techs)
{
}

static
void
nfc_adapter_init(
    NfcAdapter* self)
{
    NfcAdapterPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, THIS_TYPE,
        NfcAdapterPriv);

    self->priv = priv;
    self->tags = g_new0(NfcTag*, 1);
    priv->peers = g_new0(NfcPeer*, 1);
    priv->hosts = g_new0(NfcHost*, 1);
    priv->tag_table = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, nfc_adapter_object_entry_free);
    priv->peer_table = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, nfc_adapter_object_entry_free);
    priv->host_table = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, nfc_adapter_object_entry_free);
}

static
void
nfc_adapter_dispose(
    GObject* object)
{
    NfcAdapter* self = THIS(object);
    NfcAdapterClass* c = GET_THIS_CLASS(object);
    NfcAdapterPriv* priv = self->priv;

    if (priv->mode_pending) {
        priv->mode_pending = FALSE;
        c->cancel_mode_request(self);
    }
    if (priv->power_pending) {
        priv->power_pending = FALSE;
        c->cancel_power_request(self);
    }
    g_hash_table_remove_all(priv->tag_table);
    gutil_weakref_unref(priv->manager_ref);
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
}

static
void
nfc_adapter_finalize(
    GObject* object)
{
    NfcAdapter* self = THIS(object);
    NfcAdapterPriv* priv = self->priv;

    g_hash_table_destroy(priv->tag_table);
    g_hash_table_destroy(priv->peer_table);
    g_hash_table_destroy(priv->host_table);
    g_free(priv->hosts);
    g_free(priv->peers);
    g_free(self->tags);
    g_free(priv->name);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
nfc_adapter_class_init(
    NfcAdapterClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(NfcAdapterPriv));
    object_class->dispose = nfc_adapter_dispose;
    object_class->finalize = nfc_adapter_finalize;
    klass->submit_power_request = nfc_adapter_default_submit_power_request;
    klass->cancel_power_request = nfc_adapter_default_cancel_request;
    klass->submit_mode_request = nfc_adapter_default_submit_mode_request;
    klass->cancel_mode_request = nfc_adapter_default_cancel_request;
    klass->get_supported_techs = nfc_adapter_default_get_supported_techs;
    klass->set_allowed_techs = nfc_adapter_default_set_allowed_techs;

    NEW_SIGNAL(ENABLED_CHANGED, type);
    NEW_SIGNAL(POWERED, type);
    NEW_SIGNAL(POWER_REQUESTED, type);
    NEW_SIGNAL(MODE, type);
    NEW_SIGNAL(MODE_REQUESTED, type);
    NEW_SIGNAL(TARGET_PRESENCE, type);
    NEW_OBJECT_SIGNAL(TAG_ADDED, type);
    NEW_OBJECT_SIGNAL(TAG_REMOVED, type);
    NEW_OBJECT_SIGNAL(PEER_ADDED, type);
    NEW_OBJECT_SIGNAL(PEER_REMOVED, type);
    NEW_OBJECT_SIGNAL(HOST_ADDED, type);
    NEW_OBJECT_SIGNAL(HOST_REMOVED, type);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
