/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
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
#include "test_service.h"

#include "nfc_peer_service_p.h"
#include "nfc_llc.h"

#include <gutil_log.h>

static TestOpt test_opt;

#define TEST_(name) "/core/peer_service/" name

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!nfc_peer_service_ref(NULL));
    nfc_peer_service_unref(NULL);
    nfc_peer_service_disconnect_all(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    TestService* test_service = test_service_new("foo");
    NfcPeerService* service = NFC_PEER_SERVICE(test_service);

    g_assert_cmpuint(service->sap, == ,0);

    /* Default implementation doesn't support connections */
    g_assert(!nfc_peer_service_new_connect(service, 0, NULL));
    g_assert(!nfc_peer_service_new_accept(service, 0));

    g_assert(nfc_peer_service_ref(service) == service);
    nfc_peer_service_unref(service);
    nfc_peer_service_unref(service);
}

/*==========================================================================*
 * snep_sap
 *==========================================================================*/

static
void
test_snep_sap(
    void)
{
    TestService* test_service = test_service_new(NFC_LLC_NAME_SNEP);
    NfcPeerService* service = NFC_PEER_SERVICE(test_service);

    /* NFC_LLC_SAP_SNEP is automatically assigned */
    g_assert_cmpuint(service->sap, == ,NFC_LLC_SAP_SNEP);
    nfc_peer_service_unref(service);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("snep_sap"), test_snep_sap);
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
