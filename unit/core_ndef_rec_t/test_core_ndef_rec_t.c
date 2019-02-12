/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2018 Bogdan Pankovsky <b.pankovsky@omprussia.ru>
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

#include "nfc_ndef_p.h"

static TestOpt test_opt;
/*==========================================================================*
 * test_data
 *==========================================================================*/
typedef struct test_data {
     const char* language;
     const char* text;
     const guint8* payload;
     const NFC_NDEF_REC_T_ENCODING enc;
 } TestData;

static const TestData test_a = {
  "ru",
  "a"
};

static const TestData test_jollacom = {
  "en",
  "jolla.com"
};

static const TestData test_owerflow = {
  "en",
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
};

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    NfcNdefData ndef;

    memset(&ndef, 0, sizeof(ndef));
    g_assert(!nfc_ndef_rec_t_new(NULL, NULL));
    g_assert(!nfc_ndef_rec_t_new_from_data(NULL));
    g_assert(!nfc_ndef_rec_t_new_from_data(&ndef));
}

/*==========================================================================*
 * test_convert_from_utf16_encoding
 *==========================================================================*/
 static const guint8 test_utf16BE[] = {
 0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
 0x01,           /* Length of the record type */
 0x17,           /* Length of the record payload */
 'T',            /* Record type: 'T' (TEXT) */
 0x82,           /* encoding "UTF-16 LE" language lenght 2 */
 'e', 'n',       /* language "en" */
 0x00, 0x6f,
 0x00, 0x6d,
 0x00, 0x70,
 0x00, 0x72,
 0x00, 0x75,
 0x00, 0x73,
 0x00, 0x73,
 0x00, 0x69,
 0x00, 0x61,     /* "omprussia" */
 };

 static const guint8 test_utf16LE_BOM[] = {
 0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
 0x01,           /* Length of the record type */
 0x19,           /* Length of the record payload */
 'T',            /* Record type: 'T' (TEXT) */
 0x82,           /* encoding "UTF-16 LE" language lenght 2 */
 'e', 'n',       /* language "en" */
 0xff, 0xfe,     /* BOM UTF-16LE*/
 0x6f, 0x00,
 0x6d, 0x00,
 0x70, 0x00,
 0x72, 0x00,
 0x75, 0x00,
 0x73, 0x00,
 0x73, 0x00,
 0x69, 0x00,
 0x61, 0x00,     /* "omprussia" */
 };

 static const guint8 test_utf16BE_BOM[] = {
 0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
 0x01,           /* Length of the record type */
 0x19,           /* Length of the record payload */
 'T',            /* Record type: 'T' (TEXT) */
 0x82,           /* encoding "UTF-16 LE" language lenght 2 */
 'e', 'n',       /* language "en" */
 0xfe, 0xff,     /* BOM UTF-16BE*/
 0x00, 0x6f,
 0x00, 0x6d,
 0x00, 0x70,
 0x00, 0x72,
 0x00, 0x75,
 0x00, 0x73,
 0x00, 0x73,
 0x00, 0x69,
 0x00, 0x61,     /* "omprussia" */
 };

 static const TestData utf16_tests[3] = {
    {
        "en",
        "omprussia",
        test_utf16BE,
        NFC_NDEF_REC_T_ENCODING_UTF16BE,
    },{
        "en",
        "omprussia",
        test_utf16LE_BOM,
        NFC_NDEF_REC_T_ENCODING_UTF16LE,
    },{
        "en",
        "omprussia",
        test_utf16BE_BOM,
        NFC_NDEF_REC_T_ENCODING_UTF16BE,
    }
};

static
void
test_convert_from_utf16_encoding(
    gconstpointer data)
{
    const TestData* _data = data;
    const char* language = _data->language;
    const char* text = _data->text;
    const guint8* payload =  _data->payload;

    NfcNdefData ndef;
    NfcNdefRecT* trec;

    memset(&ndef, 0, sizeof(ndef));
    TEST_BYTES_SET(ndef.rec, payload);
    ndef.payload_length = payload[2];
    ndef.type_offset = 3;
    ndef.type_length = 1;

    trec = nfc_ndef_rec_t_new_from_data(&ndef);
    g_assert(trec);
    g_assert(trec->rec.tnf == NFC_NDEF_TNF_WELL_KNOWN);
    g_assert(trec->rec.rtd == NFC_NDEF_RTD_TEXT);
    g_assert(!g_strcmp0(trec->language,language));
    g_assert(!g_strcmp0(trec->text, text));
    nfc_ndef_rec_unref(&trec->rec);
}

static
void
test_convert_to_utf16_encoding(
    gconstpointer data)
{
    NfcNdefRecT* trec;

    const TestData* _data = data;
    const char* language = _data->language;
    const char* text = _data->text;
    const guint8* payload =  _data->payload + 4;

    guint8 len = 1 + strlen(language) + 2 + strlen(text)*2; /* expected length*/

    trec = nfc_ndef_rec_t_new_enc(text,language,_data->enc);

    g_assert(trec);
    g_assert(trec->rec.tnf == NFC_NDEF_TNF_WELL_KNOWN);
    g_assert(trec->rec.rtd == NFC_NDEF_RTD_TEXT);

    if(_data->enc == NFC_NDEF_REC_T_ENCODING_UTF16BE) {
      len -= 2;
    }

    g_assert(len == trec->rec.payload.size);
    g_assert(!memcmp(trec->rec.payload.bytes, payload, len));
    nfc_ndef_rec_unref(&trec->rec);
}
/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    static const guint8 rec[] = {
        0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
        0x01,           /* Length of the record type */
        0x01,           /* Length of the record payload (1 byte) */
        'T',            /* Record type: 'T' (TEXT) */
        0x00            /* encoding "UTF-8" language lenght 0 */
    };

    NfcNdefData ndef;
    NfcNdefRecT* trec;

    memset(&ndef, 0, sizeof(ndef));
    TEST_BYTES_SET(ndef.rec, rec);
    ndef.payload_length = rec[2];
    ndef.type_offset = 3;
    ndef.type_length = 1;

    trec = nfc_ndef_rec_t_new_from_data(&ndef);
    g_assert(trec);
    g_assert(trec->text == NULL);
    nfc_ndef_rec_unref(&trec->rec);
}


/*==========================================================================*
 * ok
 *==========================================================================*/

typedef struct test_ok_data {
    const char* name;
    const void* data;
    guint size;
    const char* language;
    const char* text;
} TestOkData;

static const guint8 jolla_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x11,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x02,           /* encoding "UTF-8" language lenght 2 */
    'e', 'n',       /* language */
    'j', 'o', 'l', 'l', 'a', '.', 'w', 'e', 'l', 'l', 'c', 'o', 'm', 'e',
};

static const guint8 omp_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x0f,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x05,           /* encoding "UTF-8" language lenght 5*/
    'e', 'n','-','G','B',      /* language */
    'o', 'm', 'p', 'r', 'u', 's', 's', 'i', 'a',
};

static const TestOkData ok_tests[] = {
    {
        "jolla",
        TEST_ARRAY_AND_SIZE(jolla_rec),
        "en",
        "jolla.wellcome"
    },{
        "omp",
        TEST_ARRAY_AND_SIZE(omp_rec),
        "en-GB",
        "omprussia"
    }
};

static
void
test_ok(
    gconstpointer data)
{
    const TestOkData* test = data;
    NfcNdefData ndef;
    NfcNdefRecT* trec;

    memset(&ndef, 0, sizeof(ndef));
    ndef.rec.bytes = test->data;
    ndef.rec.size = test->size;
    ndef.payload_length = ndef.rec.bytes[2];
    ndef.type_offset = 3;
    ndef.type_length = 1;

    trec = nfc_ndef_rec_t_new_from_data(&ndef);
    g_assert(trec);
    g_assert(trec->rec.tnf == NFC_NDEF_TNF_WELL_KNOWN);
    g_assert(trec->rec.rtd == NFC_NDEF_RTD_TEXT);
    g_assert(!g_strcmp0(trec->language, test->language));
    g_assert(!g_strcmp0(trec->text, test->text));
    nfc_ndef_rec_unref(&trec->rec);
}

/*==========================================================================*
 * from_text
 *==========================================================================*/

static
void
test_from_text(
    gconstpointer data)
{
    const TestData* _data = data;
    const char* language = _data->language;
    const char* text = _data->text;

    NfcNdefRecT* trec = nfc_ndef_rec_t_new(text,language );

    NfcNdefRec* rec;
    g_assert(trec);
    rec = nfc_ndef_rec_new(&trec->rec.raw);
    g_assert(rec);
    g_assert(NFC_IS_NFC_NDEF_REC_T(rec));
    g_assert(!g_strcmp0(NFC_NDEF_REC_T(rec)->language, language));
    g_assert(!g_strcmp0(NFC_NDEF_REC_T(rec)->text, text));
    nfc_ndef_rec_unref(&trec->rec);
    nfc_ndef_rec_unref(rec);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/core/ndef_rec_t/"
#define TEST_(name) TEST_PREFIX name
#define TEST_FROM_TEXT_(name) TEST_("from_text/" name)
#define TEST_ENCODING_(name) TEST_("encoding_test/" name)

int main(int argc, char* argv[])
{
    guint i;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("empty"), test_empty);
    for (i = 0; i < G_N_ELEMENTS(ok_tests); i++) {

        const TestOkData* test = ok_tests + i;
        char* path = g_strconcat(TEST_PREFIX, "ok/", test->name, NULL);
        g_test_add_data_func(path, test, test_ok);
        g_free(path);
    }

    const TestData* test1 =  &test_a;
    g_test_add_data_func(TEST_FROM_TEXT_("1"),
        test1, test_from_text);
    const TestData* test2 =  &test_jollacom;
    g_test_add_data_func(TEST_FROM_TEXT_("2"),
        test2, test_from_text);
    const TestData* test3 =  &test_owerflow;
    g_test_add_data_func(TEST_FROM_TEXT_("3"),
        test3, test_from_text);

    const TestData* test_utf16BE = &utf16_tests[0];
    g_test_add_data_func(TEST_ENCODING_("utf16BE"),
        test_utf16BE, test_convert_from_utf16_encoding);
    const TestData* test_utf16LE_BOM = &utf16_tests[1];
    g_test_add_data_func(TEST_ENCODING_("utf16LE_BOM"),
        test_utf16LE_BOM, test_convert_from_utf16_encoding);
    const TestData* test_utf16BE_BOM = &utf16_tests[2];
    g_test_add_data_func(TEST_ENCODING_("utf16BE_BOM"),
        test_utf16BE_BOM, test_convert_from_utf16_encoding);

    g_test_add_data_func(TEST_ENCODING_("to_utf16LE_BOM"),
        test_utf16LE_BOM, test_convert_to_utf16_encoding);
    g_test_add_data_func(TEST_ENCODING_("to_utf16BE"),
        test_utf16BE, test_convert_to_utf16_encoding);

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
