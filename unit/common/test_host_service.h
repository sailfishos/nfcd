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

#ifndef TEST_HOST_SERVICE_H
#define TEST_HOST_SERVICE_H

#include "test_types.h"

#include "nfc_host_service_impl.h"

typedef enum test_host_service_flags {
    TEST_HOST_SERVICE_NO_FLAGS = 0,
    TEST_HOST_SERVICE_FLAG_START_SYNC_OK = 0x01,
    TEST_HOST_SERVICE_FLAG_START_SYNC_ERR = 0x02,
    TEST_HOST_SERVICE_FLAG_FAIL_START = 0x04,
    TEST_HOST_SERVICE_FLAG_FAIL_START_ASYNC = 0x08,
    TEST_HOST_SERVICE_FLAG_PROCESS_ERR = 0x10,
    TEST_HOST_SERVICE_FLAG_PROCESS_SYNC = 0x20,
    TEST_HOST_SERVICE_FLAG_PROCESS_FAIL = 0x40,
    TEST_HOST_SERVICE_FLAG_PROCESS_SENT_ONCE = 0x80
} TEST_HOST_SERVICE_FLAGS;

typedef struct test_host_service {
    NfcHostService service;
    TEST_HOST_SERVICE_FLAGS flags;
    const TestTx* tx_list; /* Assumed to point to static data */
    gsize tx_count;
    gsize tx_done;
    int start;   /* Counter of start() calls */
    int restart; /* Counter of restart() calls */
    int process; /* Counter or process() calls */
    NfcHostServiceBoolFunc sent_cb;
    void* sent_data;
} TestHostService;

GType test_host_service_get_type(void);
#define TEST_TYPE_HOST_SERVICE test_host_service_get_type()
#define TEST_HOST_SERVICE(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_HOST_SERVICE, TestHostService)

gulong
test_host_service_add_start_handler(
    TestHostService* service,
    NfcHostServiceBoolFunc func,
    void* user_data);

gulong
test_host_service_add_restart_handler(
    TestHostService* service,
    NfcHostServiceBoolFunc func,
    void* user_data);

TestHostService*
test_host_service_new(
    const char* name);

#endif /* TEST_HOST_SERVICE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
