/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2019 Jolla Ltd.
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

/* This test intentionally keeps using legacy NfcNdef API */
#include <glib.h>
#undef G_DEPRECATED_FOR
#define G_DEPRECATED_FOR(x)

#include "test_common.h"

#include "nfc_types_p.h"
#include "nfc_ndef.h"

static TestOpt test_opt;

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!nfc_ndef_rec_u_new(NULL));
}

/*==========================================================================*
 * invalid_prefix
 *==========================================================================*/

static
void
test_invalid_prefix(
    void)
{
    static const guint8 data[] = {
        0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
        0x01,           /* Length of the record type */
        0x02,           /* Length of the record payload (2 bytes) */
        'U',            /* Record type: 'U' (URI) */
        0x24,           /* The last valid prefix is 0x23 */
        0x00
    };

    /* This records is interpreted as generic by nfc_ndef_rec_new() */
    static const GUtilData ndef = { TEST_ARRAY_AND_SIZE(data) };
    NfcNdefRec* rec = nfc_ndef_rec_new(&ndef);

    g_assert(rec);
    g_assert(!NFC_IS_NDEF_REC_U(rec));
    nfc_ndef_rec_unref(rec);
}

/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    static const guint8 data[] = {
        0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
        0x01,           /* Length of the record type */
        0x01,           /* Length of the record payload (1 byte) */
        'U',            /* Record type: 'U' (URI) */
        0x00
    };

    static const GUtilData ndef = { TEST_ARRAY_AND_SIZE(data) };
    NfcNdefRec* rec = nfc_ndef_rec_new(&ndef);
    NfcNdefRecU* urec = NFC_NDEF_REC_U(rec);

    g_assert(urec);
    g_assert(urec->uri);
    g_assert(!urec->uri[0]);
    g_assert_cmpint(rec->tnf, == ,NFC_NDEF_TNF_WELL_KNOWN);
    g_assert_cmpint(rec->rtd, == ,NFC_NDEF_RTD_URI);
    nfc_ndef_rec_unref(rec);
}

/*==========================================================================*
 * ok
 *==========================================================================*/

typedef struct test_ok_data {
    const char* name;
    const GUtilData data;
    const char* uri;
} TestOkData;

static const guint8 jolla_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x0a,           /* Length of the record payload */
    'U',            /* Record type: 'U' (URI) */
    0x02,           /* "https://www." */
    'j', 'o', 'l', 'l', 'a', '.', 'c', 'o', 'm',
};

static const guint8 omp_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x0e,           /* Length of the record payload */
    'U',            /* Record type: 'U' (URI) */
    0x03,           /* "http://" */
    'o', 'm', 'p', 'r', 'u', 's', 's', 'i', 'a', '.', 'r', 'u', '/'
};

static const TestOkData ok_tests[] = {
    {
        "jolla",
        { TEST_ARRAY_AND_SIZE(jolla_rec) },
        "https://www.jolla.com"
    },{
        "omp",
        { TEST_ARRAY_AND_SIZE(omp_rec) },
        "http://omprussia.ru/"
    }
};

static
void
test_ok(
    gconstpointer data)
{
    const TestOkData* test = data;
    NfcNdefRec* rec = nfc_ndef_rec_new(&test->data);
    NfcNdefRecU* urec = NFC_NDEF_REC_U(rec);

    g_assert(urec);
    g_assert_cmpint(rec->tnf, == ,NFC_NDEF_TNF_WELL_KNOWN);
    g_assert_cmpint(rec->rtd, == ,NFC_NDEF_RTD_URI);
    g_assert_cmpstr(urec->uri, == ,test->uri);
    nfc_ndef_rec_unref(rec);
}

/*==========================================================================*
 * from_uri
 *==========================================================================*/

static
void
test_from_uri(
    gconstpointer data)
{
    const char* uri = data;
    NfcNdefRecU* urec = nfc_ndef_rec_u_new(uri);
    NfcNdefRec* rec;

    g_assert(urec);
    rec = nfc_ndef_rec_new(&urec->rec.raw);
    g_assert(rec);
    g_assert(NFC_IS_NDEF_REC_U(rec));
    g_assert(!g_strcmp0(NFC_NDEF_REC_U(rec)->uri, uri));
    nfc_ndef_rec_unref(&urec->rec);
    nfc_ndef_rec_unref(rec);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/core/ndef_rec_u/"
#define TEST_(name) TEST_PREFIX name
#define TEST_FROM_URI_(name) TEST_("from_uri/" name)

int main(int argc, char* argv[])
{
    guint i;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("invalid_prefix"), test_invalid_prefix);
    g_test_add_func(TEST_("empty"), test_empty);
    for (i = 0; i < G_N_ELEMENTS(ok_tests); i++) {
        const TestOkData* test = ok_tests + i;
        char* path = g_strconcat(TEST_PREFIX, "ok/", test->name, NULL);

        g_test_add_data_func(path, test, test_ok);
        g_free(path);
    }
    g_test_add_data_func(TEST_FROM_URI_("1"),
        "a", test_from_uri);
    g_test_add_data_func(TEST_FROM_URI_("2"),
        "http://jolla.com", test_from_uri);
    g_test_add_data_func(TEST_FROM_URI_("3"),
        "verystrangeschema://foo.bar", test_from_uri);
    g_test_add_data_func(TEST_FROM_URI_("4"),
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", test_from_uri);
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
