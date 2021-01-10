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

#include "test_common.h"

#include "nfc_peer_services.h"
#include "nfc_peer_service_impl.h"

#include <gutil_log.h>

static TestOpt test_opt;

#define TEST_(name) "/core/peer_services/" name

static
guint
test_services_count(
    NfcPeerServices* services)
{
    guint n = 0;

    if (services) {
        NfcPeerService* const* ptr = services->list;

        while (*ptr++) n++;
    }
    return n;
}

/*==========================================================================*
 * Test service
 *==========================================================================*/

typedef NfcPeerServiceClass TestServiceClass;
typedef struct test_service {
    NfcPeerService service;
    int peer_arrived;
    int peer_left;
} TestService;

G_DEFINE_TYPE(TestService, test_service, NFC_TYPE_PEER_SERVICE)
#define TEST_TYPE_SERVICE (test_service_get_type())
#define TEST_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_SERVICE, TestService))

static
void
test_service_peer_arrived(
    NfcPeerService* service,
    NfcPeer* peer)
{
    TEST_SERVICE(service)->peer_arrived++;
    NFC_PEER_SERVICE_CLASS(test_service_parent_class)->
        peer_arrived(service, peer);
}

static
void
test_service_peer_left(
    NfcPeerService* service,
    NfcPeer* peer)
{
    TEST_SERVICE(service)->peer_left++;
    NFC_PEER_SERVICE_CLASS(test_service_parent_class)->
        peer_left(service, peer);
}

static
void
test_service_init(
    TestService* self)
{
}

static
void
test_service_class_init(
    TestServiceClass* klass)
{
    klass->peer_arrived = test_service_peer_arrived;
    klass->peer_left = test_service_peer_left;
}

static
TestService*
test_service_new(
    const char* name)
{
    TestService* service = g_object_new(TEST_TYPE_SERVICE, NULL);

    nfc_peer_service_init_base(&service->service, name);
    return service;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!nfc_peer_services_ref(NULL));
    nfc_peer_services_unref(NULL);
    g_assert(!nfc_peer_services_copy(NULL));
    g_assert(!nfc_peer_services_find_sn(NULL, NULL));
    g_assert(!nfc_peer_services_find_sap(NULL, 0));
    g_assert(!nfc_peer_services_add(NULL, NULL));
    g_assert(!nfc_peer_services_remove(NULL, NULL));
    nfc_peer_services_peer_arrived(NULL, NULL);
    nfc_peer_services_peer_left(NULL, NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    NfcPeerServices* services = nfc_peer_services_new();
    TestService* ts1 = test_service_new("foo");
    TestService* ts2 = test_service_new("bar");
    TestService* ts3 = test_service_new("");
    TestService* ts4 = test_service_new(NULL);
    TestService* ts5 = test_service_new("foo"); /* Duplicate name */
    NfcPeerService* s1 = NFC_PEER_SERVICE(ts1);
    NfcPeerService* s2 = NFC_PEER_SERVICE(ts2);
    NfcPeerService* s3 = NFC_PEER_SERVICE(ts3);
    NfcPeerService* s4 = NFC_PEER_SERVICE(ts4);
    NfcPeerService* s5 = NFC_PEER_SERVICE(ts5);

    g_assert(services->list);
    g_assert_cmpuint(test_services_count(services), == ,0);
    g_assert(nfc_peer_services_ref(services) == services);
    nfc_peer_services_unref(services);

    /* Make sure that add a) works and b) doesn't add the same thing twice */
    g_assert(!nfc_peer_services_add(services, NULL));
    g_assert(nfc_peer_services_add(services, s1));
    g_assert(!nfc_peer_services_add(services, s5)); /* Duplicate name */
    g_assert_cmpuint(s1->sap, == ,NFC_LLC_SAP_NAMED);
    g_assert(!s5->sap);
    g_assert_cmpuint(test_services_count(services), == ,1);
    g_assert(!nfc_peer_services_add(services, s1));
    g_assert_cmpuint(test_services_count(services), == ,1);
    g_assert(nfc_peer_services_add(services, s2));
    g_assert_cmpuint(test_services_count(services), == ,2);
    g_assert_cmpuint(s2->sap, == ,NFC_LLC_SAP_NAMED + 1);
    g_assert(nfc_peer_services_add(services, s3));
    g_assert_cmpuint(test_services_count(services), == ,3);
    g_assert_cmpuint(s3->sap, == ,NFC_LLC_SAP_UNNAMED);
    g_assert(nfc_peer_services_add(services, s4));
    g_assert_cmpuint(test_services_count(services), == ,4);
    g_assert_cmpuint(s4->sap, == ,NFC_LLC_SAP_UNNAMED + 1);

    /* Search */
    g_assert(nfc_peer_services_find_sn(services, "foo") == s1);
    g_assert(nfc_peer_services_find_sn(services, "bar") == s2);
    g_assert(!nfc_peer_services_find_sn(services, NFC_LLC_NAME_SDP));
    g_assert(!nfc_peer_services_find_sn(services, NULL));
    g_assert(!nfc_peer_services_find_sn(services, ""));
    g_assert(!nfc_peer_services_find_sap(services, 0));
    g_assert(!nfc_peer_services_find_sap(services, NFC_LLC_SAP_SDP));
    g_assert(!nfc_peer_services_find_sap(services, NFC_LLC_SAP_SNEP));
    g_assert(!nfc_peer_services_find_sap(services, s4->sap + 1));
    g_assert(nfc_peer_services_find_sap(services, s1->sap) == s1);
    g_assert(nfc_peer_services_find_sap(services, s2->sap) == s2);
    g_assert(nfc_peer_services_find_sap(services, s3->sap) == s3);
    g_assert(nfc_peer_services_find_sap(services, s4->sap) == s4);

    /* Notifications (those don't check peer pointer, so it can be NULL) */
    nfc_peer_services_peer_arrived(services, NULL);
    g_assert_cmpuint(ts1->peer_arrived, == ,1);
    g_assert_cmpuint(ts2->peer_arrived, == ,1);
    g_assert_cmpuint(ts3->peer_arrived, == ,1);
    g_assert_cmpuint(ts4->peer_arrived, == ,1);

    nfc_peer_services_peer_left(services, NULL);
    g_assert_cmpuint(ts1->peer_left, == ,1);
    g_assert_cmpuint(ts2->peer_left, == ,1);
    g_assert_cmpuint(ts3->peer_left, == ,1);
    g_assert_cmpuint(ts4->peer_left, == ,1);

    /* Test removal */
    g_assert(!nfc_peer_services_remove(services, NULL));
    g_assert(nfc_peer_services_remove(services, s1));
    g_assert(!nfc_peer_services_remove(services, s1));
    g_assert_cmpuint(test_services_count(services), == ,3);
    g_assert(!nfc_peer_services_find_sn(services, "foo"));
    g_assert(nfc_peer_services_remove(services, s2));
    g_assert(!nfc_peer_services_remove(services, s2));
    g_assert_cmpuint(test_services_count(services), == ,2);
    g_assert(!nfc_peer_services_find_sn(services, "bar"));
    g_assert(nfc_peer_services_remove(services, s3));
    g_assert(!nfc_peer_services_remove(services, s3));
    g_assert_cmpuint(test_services_count(services), == ,1);
    g_assert(nfc_peer_services_remove(services, s4));
    g_assert(!nfc_peer_services_remove(services, s4));
    g_assert_cmpuint(test_services_count(services), == ,0);

    /* These do nothing with empty list */
    nfc_peer_services_peer_arrived(services, NULL);
    nfc_peer_services_peer_left(services, NULL);

    /* Add some services back */
    nfc_peer_services_add(services, s1);
    nfc_peer_services_add(services, s2);

    /* And deallocate everything */
    nfc_peer_service_unref(s1);
    nfc_peer_service_unref(s2);
    nfc_peer_service_unref(s3);
    nfc_peer_service_unref(s4);
    nfc_peer_service_unref(s5);
    nfc_peer_services_unref(services);
}

/*==========================================================================*
 * copy
 *==========================================================================*/

static
void
test_copy(
    void)
{
    NfcPeerServices* services = nfc_peer_services_new();
    NfcPeerServices* copy = nfc_peer_services_copy(services);
    TestService* ts1 = test_service_new("foo");
    TestService* ts2 = test_service_new("bar");
    TestService* ts3 = test_service_new(NULL);
    NfcPeerService* s1 = NFC_PEER_SERVICE(ts1);
    NfcPeerService* s2 = NFC_PEER_SERVICE(ts2);
    NfcPeerService* s3 = NFC_PEER_SERVICE(ts3);
    guint n, i;

    g_assert(services->list);
    g_assert(copy->list);
    g_assert_cmpuint(test_services_count(services), == ,0);
    g_assert_cmpuint(test_services_count(copy), == ,0);

    g_assert(nfc_peer_services_add(services, s1));
    g_assert(nfc_peer_services_add(services, s2));
    g_assert(nfc_peer_services_add(services, s3));
    g_assert_cmpuint(test_services_count(services), == ,3);
    nfc_peer_service_unref(s1);
    nfc_peer_service_unref(s2);
    nfc_peer_service_unref(s3);

    nfc_peer_services_unref(copy);
    copy = nfc_peer_services_copy(services);
    n = test_services_count(copy);
    g_assert_cmpuint(n, == ,3);
    for (i = 0; i <= n /* including NULL */; i++) {
        g_assert(services->list[i] == copy->list[i]);
    }

    nfc_peer_services_unref(services);
    nfc_peer_services_unref(copy);
}

/*==========================================================================*
 * reserved
 *==========================================================================*/

static
void
test_reserved(
    void)
{
    NfcPeerService* sdp = NFC_PEER_SERVICE(test_service_new(NFC_LLC_NAME_SDP));
    NfcPeerService* snep = NFC_PEER_SERVICE(test_service_new(NFC_LLC_NAME_SNEP));
    NfcPeerServices* services = nfc_peer_services_new();

    g_assert(!nfc_peer_services_add(services, sdp)); /* Not allowed */
    g_assert(nfc_peer_services_add(services, snep));
    g_assert_cmpuint(test_services_count(services), == ,1);
    g_assert_cmpuint(snep->sap, == ,NFC_LLC_SAP_SNEP);

    nfc_peer_service_unref(sdp);
    nfc_peer_service_unref(snep);
    nfc_peer_services_unref(services);
}

/*==========================================================================*
 * too_many
 *==========================================================================*/

static
void
test_too_many(
    void)
{
    NfcPeerServices* services = nfc_peer_services_new();
    TestService* ts;
    NfcPeerService* ps;
    guint i;

    for (i = 0; i < 32; i++) {
        ts = test_service_new(NULL);
        ps = NFC_PEER_SERVICE(ts);
        g_assert(nfc_peer_services_add(services, ps));
        g_assert_cmpuint(ps->sap, == ,NFC_LLC_SAP_UNNAMED + i);
        nfc_peer_service_unref(ps);
    }

    /* And this one doesn't fit */
    ts = test_service_new(NULL);
    ps = NFC_PEER_SERVICE(ts);
    g_assert(!nfc_peer_services_add(services, ps));
    nfc_peer_service_unref(ps);

    nfc_peer_services_unref(services);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("copy"), test_copy);
    g_test_add_func(TEST_("reserved"), test_reserved);
    g_test_add_func(TEST_("too_many"), test_too_many);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
