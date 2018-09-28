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

#include "nfc_tlv.h"

static TestOpt test_opt;

#define TLV_TEST (0x04)

/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    GUtilData buf, value;

    memset(&buf, 0, sizeof(buf));
    TEST_BYTES_SET(value, &value);
    g_assert(!nfc_tlv_check(&buf));
    g_assert(!nfc_tlv_next(&buf, &value));
    g_assert(!value.bytes);
    g_assert(!value.size);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    static const guint8 null_tlv[] = { TLV_NULL };
    GUtilData buf, value;

    TEST_BYTES_SET(buf, null_tlv);
    TEST_BYTES_SET(value, &value);
    g_assert(!nfc_tlv_check(&buf));
    g_assert(!nfc_tlv_next(&buf, &value));
    g_assert(!value.bytes);
    g_assert(!value.size);
}

/*==========================================================================*
 * null_term
 *==========================================================================*/

static
void
test_null_term(
    void)
{
    static const guint8 null_term_tlv[] = { TLV_NULL, TLV_TERMINATOR };
    GUtilData buf, value;

    TEST_BYTES_SET(buf, null_term_tlv);
    TEST_BYTES_SET(value, &value);
    g_assert(nfc_tlv_check(&buf));
    g_assert(!nfc_tlv_next(&buf, &value));
    g_assert(!value.bytes);
    g_assert(!value.size);
}

/*==========================================================================*
 * missing_len
 *==========================================================================*/

static
void
test_missing_len(
    void)
{
    static const guint8 short_tlv[] = { TLV_TEST };
    GUtilData buf, value;

    TEST_BYTES_SET(buf, short_tlv);
    TEST_BYTES_SET(value, &value);
    g_assert(!nfc_tlv_check(&buf));
    g_assert(!nfc_tlv_next(&buf, &value));
    g_assert(!value.bytes);
    g_assert(!value.size);
}

/*==========================================================================*
 * missing_len2
 *==========================================================================*/

static
void
test_missing_len2(
    void)
{
    static const guint8 short_tlv[] = { TLV_TEST, 0xff, 0x01 };
    GUtilData buf, value;

    TEST_BYTES_SET(buf, short_tlv);
    TEST_BYTES_SET(value, &value);
    g_assert(!nfc_tlv_check(&buf));
    g_assert(!nfc_tlv_next(&buf, &value));
    g_assert(!value.bytes);
    g_assert(!value.size);
}

/*==========================================================================*
 * missing_value
 *==========================================================================*/

static
void
test_missing_value(
    void)
{
    static const guint8 short_tlv[] = { TLV_TEST, 1 };
    GUtilData buf, value;

    TEST_BYTES_SET(buf, short_tlv);
    TEST_BYTES_SET(value, &value);
    g_assert(!nfc_tlv_check(&buf));
    g_assert(!nfc_tlv_next(&buf, &value));
    g_assert(!value.bytes);
    g_assert(!value.size);
}

/*==========================================================================*
 * short_len
 *==========================================================================*/

static
void
test_short_len(
    void)
{
    static const guint8 test_tlv[] = {
        TLV_TEST, 0x01, 0x02,
        TLV_TERMINATOR
    };
    GUtilData buf, value;

    TEST_BYTES_SET(buf, test_tlv);
    memset(&value, 0, sizeof(value));
    g_assert(nfc_tlv_check(&buf));

    /* Read test TLV */
    g_assert(nfc_tlv_next(&buf, &value));
    g_assert(value.bytes == test_tlv + 2);
    g_assert(value.size == 1);

    /* And bump into TLV_TERMINATOR */
    g_assert(!nfc_tlv_next(&buf, &value));
    g_assert(!value.bytes);
    g_assert(!value.size);
}

/*==========================================================================*
 * long_len
 *==========================================================================*/

static
void
test_long_len(
    void)
{
    static const guint8 test_tlv[] = {
        TLV_TEST, 0xff, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        TLV_TERMINATOR, 0x00, 0x00
    };
    GUtilData buf, value;

    TEST_BYTES_SET(buf, test_tlv);
    memset(&value, 0, sizeof(value));
    g_assert(nfc_tlv_check(&buf));

    /* Read test TLV */
    g_assert(nfc_tlv_next(&buf, &value));
    g_assert(value.bytes == test_tlv + 4);
    g_assert(value.size == 256);

    /* And bump into TLV_TERMINATOR */
    g_assert(!nfc_tlv_next(&buf, &value));
    g_assert(!value.bytes);
    g_assert(!value.size);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/tlv/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("empty"), test_empty);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("null_term"), test_null_term);
    g_test_add_func(TEST_("missing_len"), test_missing_len);
    g_test_add_func(TEST_("missing_len2"), test_missing_len2);
    g_test_add_func(TEST_("missing_value"), test_missing_value);
    g_test_add_func(TEST_("short_len"), test_short_len);
    g_test_add_func(TEST_("long"), test_long_len);
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
