/*
 * Copyright (C) 2022 Jolla Ltd.
 * Copyright (C) 2022 Slava Monich <slava.monich@jolla.com>
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

#ifndef TEST_TARGET_T2_H
#define TEST_TARGET_T2_H

#include "nfc_target.h"

#define TEST_TARGET_T2_READ_SIZE (16)
#define TEST_TARGET_T2_BLOCK_SIZE (4)
#define TEST_TARGET_T2_FIRST_DATA_BLOCK (4)
#define TEST_TARGET_T2_DATA_OFFSET \
    (TEST_TARGET_T2_FIRST_DATA_BLOCK * TEST_TARGET_T2_BLOCK_SIZE)

typedef struct test_target_t2_error TestTargetT2Error;
typedef struct test_target_t2 {
    NfcTarget target;
    guint transmit_id;
    guint8* storage;
    GUtilData data;
    const TestTargetT2Error* read_error;
    const TestTargetT2Error* write_error;
    gboolean transmit_error;
} TestTargetT2;

typedef enum test_target_t2_error_type {
     TEST_TARGET_T2_ERROR_TRANSMIT,
     TEST_TARGET_T2_ERROR_CRC,
     TEST_TARGET_T2_ERROR_NACK,
     TEST_TARGET_T2_ERROR_SHORT_RESP,
     TEST_TARGET_T2_ERROR_TIMEOUT
} TEST_TARGET_T2_ERROR_TYPE;

struct test_target_t2_error {
    TEST_TARGET_T2_ERROR_TYPE type;
    guint block;
};

typedef struct test_target_t2_read {
    TestTargetT2* target;
    guint block;
} TestTargetT2Read;

typedef struct test_target_t2_write {
    TestTargetT2* target;
    guint block;
    guint8* data;
    guint size;
} TestTargetT2Write;

GType test_target_t2_get_type();
#define TEST_TYPE_TARGET_T2 (test_target_t2_get_type())
#define TEST_TARGET_T2(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_TARGET_T2, TestTargetT2)

TestTargetT2*
test_target_t2_new(
     const guint8* bytes,
     guint size);

#endif /* TEST_TARGET_T2_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
