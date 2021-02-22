/*
 * Copyright (C) 2021 Jolla Ltd.
 * Copyright (C) 2021 Slava Monich <slava.monich@jolla.com>
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

#include "test_service.h"

#include "nfc_peer_service_impl.h"

typedef NfcPeerServiceClass TestServiceClass;

G_DEFINE_TYPE(TestService, test_service, NFC_TYPE_PEER_SERVICE)

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

TestService*
test_service_new(
    const char* name)
{
    TestService* test = g_object_new(TEST_TYPE_SERVICE, NULL);

    nfc_peer_service_init_base(&test->service, name);
    return test;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
