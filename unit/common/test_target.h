/*
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2019-2020 Slava Monich <slava.monich@jolla.com>
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

#ifndef TEST_TARGET_H
#define TEST_TARGET_H

#include "nfc_target_impl.h"

typedef NfcTargetClass TestTargetClass;
typedef struct test_target {
    NfcTarget target;
    guint transmit_id;
    GPtrArray* cmd_resp;
    int fail_transmit;
} TestTarget;

GType test_target_get_type(void);
#define TEST_TYPE_TARGET (test_target_get_type())
#define TEST_TARGET(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_TARGET, TestTarget))

#define TEST_TARGET_FAIL_ALL (-1)
#define TEST_TARGET_FAIL_NONE (FALSE)

NfcTarget*
test_target_new(
    int fail);

NfcTarget*
test_target_new_tech(
    NFC_TECHNOLOGY tech,
    int fail);

NfcTarget*
test_target_new_tech_with_data(
    NFC_TECHNOLOGY tech,
    const void* cmd_bytes,
    guint cmd_len,
    const void* resp_bytes,
    guint resp_len);

#define test_target_new_with_data(cmd,cmd_len,resp,resp_len) \
    test_target_new_tech_with_data(NFC_TECHNOLOGY_A,cmd,cmd_len,resp,resp_len)

void
test_target_add_data(
    NfcTarget* target,
    const void* cmd_bytes,
    guint cmd_len,
    const void* resp_bytes,
    guint resp_len);

NfcTarget*
test_target_new_with_tx(
    const TestTx* tx_list,
    gsize tx_count);

#define test_target_tx_remaining(target) \
    (TEST_TARGET(target)->cmd_resp->len)

#endif /* TEST_TARGET_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
