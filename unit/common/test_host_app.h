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

#ifndef TEST_CARD_APP_H
#define TEST_CARD_APP_H

#include "test_types.h"

#include "nfc_host_app_impl.h"

typedef enum test_host_app_flags {
    TEST_HOST_APP_NO_FLAGS = 0,
    TEST_HOST_APP_FLAG_START_SYNC_OK = 0x0001,
    TEST_HOST_APP_FLAG_START_SYNC_ERR = 0x0002,
    TEST_HOST_APP_FLAG_FAIL_START = 0x0004,
    TEST_HOST_APP_FLAG_FAIL_START_ASYNC = 0x0008,
    TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT = 0x0010,
    TEST_HOST_APP_FLAG_FAIL_IMPLICIT_SELECT_ASYNC = 0x0020,
    TEST_HOST_APP_FLAG_FAIL_SELECT = 0x0040,
    TEST_HOST_APP_FLAG_FAIL_SELECT_ASYNC = 0x0080,
    TEST_HOST_APP_FLAG_PROCESS_ERR = 0x0100,
    TEST_HOST_APP_FLAG_PROCESS_SYNC = 0x0200,
    TEST_HOST_APP_FLAG_PROCESS_FAIL = 0x0400,
    TEST_HOST_APP_FLAG_PROCESS_SENT_ONCE = 0x0800
} TEST_HOST_APP_FLAGS;

typedef struct test_host_app {
    NfcHostApp app;
    TEST_HOST_APP_FLAGS flags;
    const TestTx* tx_list; /* Assumed to point to static data */
    gsize tx_count;
    gsize tx_done;
    int start;
    int select;
    int deselect;
    int process;
    NfcHostAppBoolFunc sent_cb;
    void* sent_data;
} TestHostApp;

GType test_host_app_get_type(void);
#define TEST_TYPE_HOST_APP test_host_app_get_type()
#define TEST_HOST_APP(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_HOST_APP, TestHostApp)

gulong
test_host_app_add_start_handler(
    TestHostApp* app,
    NfcHostAppBoolFunc func,
    void* user_data);

TestHostApp*
test_host_app_new(
    const GUtilData* aid,
    const char* name,
    NFC_HOST_APP_FLAGS flags);

#endif /* TEST_HOST_APP_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
