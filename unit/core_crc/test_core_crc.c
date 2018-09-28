/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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

#include "nfc_crc.h"

static TestOpt test_opt;

typedef struct test_crc16 {
    const char* name;
    const void* data;
    gulong len;
    guint8 crc_a[2];
    guint8 crc_b[2];
} TestCrc16;

/*==========================================================================*
 * crc_a
 *==========================================================================*/

static
void
test_crc_a(
    gconstpointer test_data)
{
    const TestCrc16* test = test_data;
    guint8 buf[8];

    g_assert((test->len + 2) <= sizeof(buf));
    memset(buf, 0, sizeof(buf));
    memcpy(buf, test->data, test->len);
    nfc_crc_a_append(buf, test->len);
    g_assert(nfc_crc_a_check_tail(buf, test->len));
    g_assert(!memcmp(buf + test->len, test->crc_a, 2));
}

/*==========================================================================*
 * crc_b
 *==========================================================================*/

static
void
test_crc_b(
    gconstpointer test_data)
{
    const TestCrc16* test = test_data;
    guint8 buf[8];

    g_assert((test->len + 2) <= sizeof(buf));
    memset(buf, 0, sizeof(buf));
    memcpy(buf, test->data, test->len);
    nfc_crc_b_append(buf, test->len);
    g_assert(nfc_crc_b_check_tail(buf, test->len));
    g_assert(!memcmp(buf + test->len, test->crc_b, 2));
}

/*==========================================================================*
 * Common
 *==========================================================================*/

static const TestCrc16 tests[] = {
    { "empty", NULL,   0, {0x63, 0x63}, {0xff, 0xff} },
    { "a",      "a",      1, {0x71, 0x23}, {0x08, 0x7d} },
    { "ab",     "ab",     2, {0x39, 0x22}, {0x21, 0xcc} },
    { "abc",    "abc",    3, {0xfd, 0xfd}, {0xda, 0x61} },
    { "abcd",   "abcd",   4, {0xb5, 0x09}, {0x94, 0x5c} },
    { "abcde",  "abcde",  5, {0x84, 0xd6}, {0x5a, 0xe6} },
    { "abcdef", "abcdef", 6, {0xca, 0xc4}, {0x09, 0xfb} }
};

#define TEST_(test) "/core/crc/" test

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    for (i = 0; i < G_N_ELEMENTS(tests); i++) {
        const TestCrc16* test = tests + i;
        char* path = g_strconcat(TEST_("crc_a/"), test->name, NULL);

        g_test_add_data_func(path, test, test_crc_a);
        g_free(path);
    }
    for (i = 0; i < G_N_ELEMENTS(tests); i++) {
        const TestCrc16* test = tests + i;
        char* path = g_strconcat(TEST_("crc_b/"), test->name, NULL);

        g_test_add_data_func(path, test, test_crc_b);
        g_free(path);
    }
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
