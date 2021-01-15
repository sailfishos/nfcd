/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_peer_services.h"
#include "nfc_peer_service_p.h"

#define GLOG_MODULE_NAME NFC_PEER_LOG_MODULE
#include <gutil_log.h>
#include <gutil_macros.h>

#include <stdlib.h>

#define SAP_BIT(sap) (((guint64)1) << (sap))

typedef struct nfc_peer_services_object {
    NfcPeerServices pub;
    NfcPeerService** list;
    gint64 sap_mask;
    gint refcount;
} NfcPeerServicesObject;

static NfcPeerService* const nfc_peer_services_empty_list[] = { NULL };

static inline
NfcPeerServicesObject* nfc_peer_services_cast(NfcPeerServices* pub)
    { return G_LIKELY(pub) ? G_CAST(pub,NfcPeerServicesObject,pub) : NULL; }

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
nfc_peer_services_free(
    NfcPeerServicesObject* self)
{
    NfcPeerService* const* ptr = self->pub.list;

    while (*ptr) nfc_peer_service_unref(*ptr++);
    g_free(self->list);
    g_slice_free1(sizeof(*self), self);
}

static
gboolean
nfc_peer_services_contains(
    NfcPeerServices* services,
    NfcPeerService* service)
{
    NfcPeerService* const* ptr;

    for (ptr = services->list; *ptr; ptr++) {
        if (*ptr == service) {
            return TRUE;
        }
    }
    return FALSE;
}

static
int
nfc_peer_services_compare(
    const void* p1,
    const void* p2)
{
    NfcPeerService* ps1 = *(NfcPeerService**)p1;
    NfcPeerService* ps2 = *(NfcPeerService**)p2;

    return (int)ps1->sap - (int)ps2->sap;
}

static
void
nfc_peer_services_peer_notify(
    NfcPeerServices* services,
    NfcPeer* peer,
    void (*notify)(NfcPeerService* ps, NfcPeer* peer))
{
    if (G_LIKELY(services)) {
        NfcPeerService* const* ptr;
        guint n = 0;

        /* Count the services and bump references at the same time */
        for (ptr = services->list; *ptr; ptr++) {
            nfc_peer_service_ref(*ptr);
            n++;
        }

        if (n) {
            NfcPeerService** tmp = g_new(NfcPeerService*, n + 1);

            /* In case if callbacks modify the list, make a copy */
            memcpy(tmp, services->list, sizeof(NfcPeerService*) * (n + 1));
            for (ptr = tmp; *ptr; ptr++) {
                NfcPeerService* ps = *ptr;

                /* Paranoid check if the service is still there */
                if (nfc_peer_services_contains(services, ps)) {
                    notify(ps, peer);
                }
            }

            /* Release temporary references */
            for (ptr = tmp; *ptr; ptr++) {
                nfc_peer_service_unref(*ptr);
            }
            g_free(tmp);
        }
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcPeerServices*
nfc_peer_services_new(
    void)
{
    NfcPeerServicesObject* self = g_slice_new0(NfcPeerServicesObject);
    NfcPeerServices* services = &self->pub;

    services->list = nfc_peer_services_empty_list;
    self->sap_mask = 1; /* Reserved for LLC Link Management Service */
    g_atomic_int_set(&self->refcount, 1);
    return services;
}

NfcPeerServices*
nfc_peer_services_ref(
    NfcPeerServices* services)
{
    NfcPeerServicesObject* self = nfc_peer_services_cast(services);

    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return services;
}

void
nfc_peer_services_unref(
    NfcPeerServices* services)
{
    NfcPeerServicesObject* self = nfc_peer_services_cast(services);

    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            nfc_peer_services_free(self);
        }
    }
}

NfcPeerServices*
nfc_peer_services_copy(
    NfcPeerServices* services)
{
    NfcPeerServicesObject* self = nfc_peer_services_cast(services);

    if (G_LIKELY(self)) {
        NfcPeerServices* copy = nfc_peer_services_new();

        if (self->list) {
            NfcPeerServicesObject* priv = nfc_peer_services_cast(copy);
            NfcPeerService* const* ptr;
            guint n = 0;

            /* Count the services and bump references at the same time */
            for (ptr = services->list; *ptr; ptr++) {
                nfc_peer_service_ref(*ptr);
                n++;
            }

            priv->sap_mask = self->sap_mask;
            copy->list = priv->list = g_memdup(self->list,
                sizeof(NfcPeerService*) * (n + 1));
        }
        return copy;
    }
    return NULL;
}

NfcPeerService*
nfc_peer_services_find_sn(
    NfcPeerServices* services,
    const char* name)
{
    NfcPeerServicesObject* self = nfc_peer_services_cast(services);

    if (G_LIKELY(self) && name && name[0]) {
        NfcPeerService* const* ptr = services->list;

        while (*ptr) {
            NfcPeerService* ps = *ptr++;

            if (!g_strcmp0(ps->name, name)) {
                return ps;
            }
        }
    }
    return NULL;
}

NfcPeerService*
nfc_peer_services_find_sap(
    NfcPeerServices* services,
    guint8 sap)
{
    NfcPeerServicesObject* self = nfc_peer_services_cast(services);

    if (G_LIKELY(self) && sap > NFC_LLC_SAP_SDP) {
        NfcPeerService* const* ptr = services->list;

        while (*ptr) {
            NfcPeerService* ps = *ptr++;

            if (ps->sap == sap) {
                return ps;
            } else if (ps->sap > sap) {
                /* The list is sorted */
                break;
            }
        }
    }
    return NULL;
}

gboolean
nfc_peer_services_add(
    NfcPeerServices* services,
    NfcPeerService* ps)
{
    NfcPeerServicesObject* self = nfc_peer_services_cast(services);

    if (G_LIKELY(self) && G_LIKELY(ps)) {
        NfcPeerService* const* ptr;
        guint8 sap_min, sap_max, sap;
        guint n;
        const char* name = (ps->name && ps->name[0]) ? ps->name : NULL;

        /*
         * Count the services and at the same time check if it's already
         * there or if the name has already been taken.
         */
        for (ptr = services->list, n = 0; *ptr; ptr++, n++) {
            if (*ptr == ps || (name && !g_strcmp0(name, (*ptr)->name))) {
                return FALSE;
            }
        }

        /* Pick SAP from the right range */
        if (name) {
            /* Check reserved named */
            if (!g_strcmp0(name, NFC_LLC_NAME_SDP)) {
                /* Can't register this one */
                return FALSE;
            } else if (!g_strcmp0(name, NFC_LLC_NAME_SNEP)) {
                /* This is a well-known service */
                sap_min = sap_max = NFC_LLC_SAP_SNEP;
            } else {
                /* Dynamically pick the number */
                sap_min = NFC_LLC_SAP_NAMED;
                sap_max = NFC_LLC_SAP_UNNAMED - 1;
            }
        } else {
            sap_min = NFC_LLC_SAP_UNNAMED;
            sap_max = NFC_LLC_SAP_MAX;
        }

        for (sap = sap_min;
             sap <= sap_max && (self->sap_mask & SAP_BIT(sap));
             sap++);

        if (sap <= sap_max) {
            ps->sap = sap;
            /* Reallocate the memory and append the reference */
            self->list = g_renew(NfcPeerService*, self->list, n + 2);
            self->list[n] = nfc_peer_service_ref(ps);
            self->list[n + 1] = NULL;
            qsort(self->list, n + 1, sizeof(NfcPeerService*),
                nfc_peer_services_compare);
            services->list = self->list;
            self->sap_mask |= SAP_BIT(sap);
            return TRUE;
        }
    }
    return FALSE;
}

gboolean
nfc_peer_services_remove(
    NfcPeerServices* services,
    NfcPeerService* ps)
{
    NfcPeerServicesObject* self = nfc_peer_services_cast(services);

    if (G_LIKELY(self) && G_LIKELY(ps)) {
        NfcPeerService* const* ptr;
        int pos = -1;
        guint n;

        /*
         * Count the services and at the same time find the position of
         * the service that we are about to remove.
         */
        for (ptr = services->list, n = 0; *ptr; ptr++, n++) {
            if (*ptr == ps) {
                pos = n;
            }
        }

        if (pos >= 0) {
            GASSERT(self->sap_mask & SAP_BIT(ps->sap));
            self->sap_mask &= ~SAP_BIT(ps->sap);
            if (n == 1) {
                /* The last service is gone */
                g_free(self->list);
                self->list = NULL;
                services->list = nfc_peer_services_empty_list;
            } else {
                memmove(self->list + pos, self->list + pos + 1,
                    sizeof(NfcPeerService*) * (n - pos));
                self->list = g_renew(NfcPeerService*, self->list, n);
                services->list = self->list;
            }
            nfc_peer_service_unref(ps);
            return TRUE;
        }
    }
    return FALSE;
}

void
nfc_peer_services_peer_arrived(
    NfcPeerServices* svcs,
    NfcPeer* peer)
{
    nfc_peer_services_peer_notify(svcs, peer, nfc_peer_service_peer_arrived);
}

void
nfc_peer_services_peer_left(
    NfcPeerServices* svcs,
    NfcPeer* peer)
{
    nfc_peer_services_peer_notify(svcs, peer, nfc_peer_service_peer_left);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
