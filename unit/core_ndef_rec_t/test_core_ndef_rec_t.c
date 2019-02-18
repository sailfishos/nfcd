/*
 * Copyright (C) 2018-2019 Jolla Ltd.
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

#include "nfc_ndef.h"
#include "nfc_ndef_p.h"

#include <locale.h>

static TestOpt test_opt;

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
    g_assert(!nfc_ndef_rec_t_new_from_data(NULL));
    g_assert(!nfc_ndef_rec_t_new_from_data(&ndef));
}

/*==========================================================================*
 * invalid_enc
 *==========================================================================*/

static
void
test_invalid_enc(
    void)
{
    g_assert(!nfc_ndef_rec_t_new_enc("", "", NFC_NDEF_REC_T_ENC_UTF16LE + 1));
}

/*==========================================================================*
 * invalid_text
 *==========================================================================*/

static
void
test_invalid_text(
    void)
{
    g_assert(!nfc_ndef_rec_t_new_enc("\xff", "", NFC_NDEF_REC_T_ENC_UTF16LE));
}

/*==========================================================================*
 * default_lang
 *==========================================================================*/

static
void
test_default_lang(
    void)
{
    NfcNdefRecT* trec;

    setlocale(LC_ALL, "C");
    trec = nfc_ndef_rec_t_new(NULL, NULL);
    g_assert(trec);
    g_assert(!g_strcmp0(trec->lang, "en"));
    g_assert(!g_strcmp0(trec->text, ""));
    nfc_ndef_rec_unref(&trec->rec);
}

/*==========================================================================*
 * locale
 *==========================================================================*/

static
void
test_locale(
    void)
{
    NfcNdefRecT* trec;

    /* Locale behavior is very platform specific, it's hard to write
     * portable tests for it. */
    setlocale(LC_ALL, "");
    trec = nfc_ndef_rec_t_new(NULL, NULL);
    g_assert(trec);
    g_assert(trec->lang && trec->lang[0]);
    g_assert(!g_strcmp0(trec->text, ""));
    nfc_ndef_rec_unref(&trec->rec);
}

/*==========================================================================*
 * utf16
 *==========================================================================*/

typedef struct test_utf16 {
    const char* lang;
    const char* text;
    GUtilData rec;
    NFC_NDEF_REC_T_ENC enc;
} TestUtf16;

static const guint8 test_utf16BE[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x15,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x82,           /* encoding "UTF-16 BE" language length 2 */
    'e', 'n',       /* language "en" */
    0x00, 'o',      /* "omprussia" */
    0x00, 'm',
    0x00, 'p',
    0x00, 'r',
    0x00, 'u',
    0x00, 's',
    0x00, 's',
    0x00, 'i',
    0x00, 'a'
};

static const guint8 test_utf16LE_BOM[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x17,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x82,           /* encoding "UTF-16 LE" language length 2 */
    'e', 'n',       /* language "en" */
    0xff, 0xfe,     /* BOM UTF-16LE */
    'o', 0x00,
    'm', 0x00,
    'p', 0x00,
    'r', 0x00,
    'u', 0x00,
    's', 0x00,
    's', 0x00,
    'i', 0x00,
    'a', 0x00       /* "omprussia" */
};

static const guint8 test_utf16BE_BOM[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x17,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x82,           /* encoding "UTF-16 BE" language length 2 */
    'e', 'n',       /* language "en" */
    0xfe, 0xff,     /* BOM UTF-16BE */
    0x00, 'o',
    0x00, 'm',
    0x00, 'p',
    0x00, 'r',
    0x00, 'u',
    0x00, 's',
    0x00, 's',
    0x00, 'i',
    0x00, 'a'       /* "omprussia" */
};

static const TestUtf16 utf16_tests[3] = {
    {
        "en",
        "omprussia",
        { TEST_ARRAY_AND_SIZE(test_utf16BE) },
        NFC_NDEF_REC_T_ENC_UTF16BE
    },{
        "en",
        "omprussia",
        { TEST_ARRAY_AND_SIZE(test_utf16LE_BOM) },
        NFC_NDEF_REC_T_ENC_UTF16LE
    },{
        "en",
        "omprussia",
        { TEST_ARRAY_AND_SIZE(test_utf16BE_BOM) },
        NFC_NDEF_REC_T_ENC_UTF16BE
    }
};

static
void
test_utf16_decode(
    gconstpointer data)
{
    const TestUtf16* test = data;
    const char* language = test->lang;
    const char* text = test->text;
    NfcNdefRec* rec;
    NfcNdefRecT* trec;

    rec = nfc_ndef_rec_new(&test->rec);
    g_assert(rec);
    g_assert(rec->tnf == NFC_NDEF_TNF_WELL_KNOWN);
    g_assert(rec->rtd == NFC_NDEF_RTD_TEXT);

    trec = NFC_NDEF_REC_T(rec);
    g_assert(trec);
    g_assert(!g_strcmp0(trec->lang, language));
    g_assert(!g_strcmp0(trec->text, text));
    nfc_ndef_rec_unref(rec);
}

static
void
test_utf16_encode(
    gconstpointer data)
{
    const TestUtf16* test = data;
    const GUtilData* rec = &test->rec;
    const guint payload_offset = 4;
    NfcNdefRecT* trec = nfc_ndef_rec_t_new_enc(test->text, test->lang,
        test->enc);

    g_assert(trec);
    g_assert(trec->rec.tnf == NFC_NDEF_TNF_WELL_KNOWN);
    g_assert(trec->rec.rtd == NFC_NDEF_RTD_TEXT);

    g_assert(test->rec.size == trec->rec.payload.size + payload_offset);
    g_assert(!memcmp(trec->rec.payload.bytes, rec->bytes + payload_offset,
        rec->size - payload_offset));
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
        0x00            /* encoding "UTF-8" language length 0 */
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
    g_assert(!g_strcmp0(trec->lang, ""));
    g_assert(!g_strcmp0(trec->text, ""));
    nfc_ndef_rec_unref(&trec->rec);
}

/*==========================================================================*
 * invalid
 *==========================================================================*/

typedef struct test_invalid {
    const char* name;
    GUtilData rec;
} TestInvalid;

static const guint8 invalid_lang_len_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x01,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x01            /* Invalid language length 1 */
};

static const guint8 invalid_lang_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x02,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x01,           /* Language length 1 */
    0xff            /* Invalid language */
};

static const guint8 invalid_utf8_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x02,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x00,           /* No language */
    0xff            /* Invalid UTF-8 */
};

static const guint8 invalid_utf16_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x04,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x82,           /* UTF-16, language length 2 */
    'e', 'n',       /* Language */
    0xff            /* Too short UTF16 */
};
static const TestInvalid tests_invalid[] = {
    {
        "lang_len",
        { TEST_ARRAY_AND_SIZE(invalid_lang_len_rec) },
    },{
        "lang",
        { TEST_ARRAY_AND_SIZE(invalid_lang_rec) },
    },{
        "utf8",
        { TEST_ARRAY_AND_SIZE(invalid_utf8_rec) },
    },{
        "utf16",
        { TEST_ARRAY_AND_SIZE(invalid_utf16_rec) },
    }
};

static
void
test_invalid(
    gconstpointer data)
{
    const TestInvalid* test = data;
    const guint payload_offset = 4;
    NfcNdefData ndef;
    NfcNdefRec* rec;

    memset(&ndef, 0, sizeof(ndef));
    ndef.rec = test->rec;
    ndef.payload_length = ndef.rec.bytes[2];
    ndef.type_length = 1;
    ndef.type_offset = payload_offset - ndef.type_length;

    g_assert(!nfc_ndef_rec_t_new_from_data(&ndef));

    /* It still gets interpreted as a generic record by nfc_ndef_rec_new() */
    rec = nfc_ndef_rec_new(&test->rec);
    g_assert(rec);
    g_assert(!NFC_IS_NDEF_REC_T(rec));
    nfc_ndef_rec_unref(rec);
}

/*==========================================================================*
 * utf8
 *==========================================================================*/

typedef struct test_utf8 {
    const char* name;
    GUtilData rec;
    const char* lang;
    const char* text;
} TestUtf8;

static const guint8 jolla_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x10,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x02,           /* encoding UTF-8 language length 2 */
    'e', 'n',       /* language */
    'j', 'o', 'l', 'l', 'a', '.', 'w', 'e', 'l', 'c', 'o', 'm', 'e'
};

static const guint8 omp_rec[] = {
    0xd1,           /* NDEF record header (MB=1, ME=1, SR=1, TNF=0x01) */
    0x01,           /* Length of the record type */
    0x0f,           /* Length of the record payload */
    'T',            /* Record type: 'T' (TEXT) */
    0x05,           /* encoding UTF-8 language length 5 */
    'r', 'u','_','R','U',  /* language */
    'o', 'm', 'p', 'r', 'u', 's', 's', 'i', 'a'
};

static const TestUtf8 tests_utf8[] = {
    {
        "jolla",
        { TEST_ARRAY_AND_SIZE(jolla_rec) },
        "en",
        "jolla.welcome"
    },{
        "omp",
        { TEST_ARRAY_AND_SIZE(omp_rec) },
        "ru_RU",
        "omprussia"
    }
};

static
void
test_utf8(
    gconstpointer data)
{
    const TestUtf8* test = data;
    const GUtilData* rec = &test->rec;
    NfcNdefData ndef;
    NfcNdefRecT* trec;
    const guint payload_offset = 4;

    memset(&ndef, 0, sizeof(ndef));
    ndef.rec = *rec;
    ndef.payload_length = rec->bytes[2];
    ndef.type_length = 1;
    ndef.type_offset = payload_offset - ndef.type_length;

    trec = nfc_ndef_rec_t_new_from_data(&ndef);
    g_assert(trec);
    g_assert(trec->rec.tnf == NFC_NDEF_TNF_WELL_KNOWN);
    g_assert(trec->rec.rtd == NFC_NDEF_RTD_TEXT);
    g_assert(!g_strcmp0(trec->lang, test->lang));
    g_assert(!g_strcmp0(trec->text, test->text));
    nfc_ndef_rec_unref(&trec->rec);

    trec = nfc_ndef_rec_t_new(test->text, test->lang);
    g_assert(test->rec.size == trec->rec.payload.size + payload_offset);
    g_assert(!memcmp(trec->rec.payload.bytes, rec->bytes + payload_offset,
        rec->size - payload_offset));
    nfc_ndef_rec_unref(&trec->rec);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/core/ndef_rec_t/"
#define TEST_(name) TEST_PREFIX name
#define TEST_RECODE_(name) TEST_("recode/" name)
#define TEST_ENCODE_(name) TEST_("encode/" name)
#define TEST_DECODE_(name) TEST_("decode/" name)

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("empty"), test_empty);
    g_test_add_func(TEST_("invalid_enc"), test_invalid_enc);
    g_test_add_func(TEST_("invalid_text"), test_invalid_text);
    g_test_add_func(TEST_("default_lang"), test_default_lang);
    g_test_add_func(TEST_("locale"), test_locale);

    for (i = 0; i < G_N_ELEMENTS(tests_invalid); i++) {
        const TestInvalid* test = tests_invalid + i;
        char* path = g_strconcat(TEST_PREFIX "invalid/", test->name, NULL);

        g_test_add_data_func(path, test, test_invalid);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(tests_utf8); i++) {
        const TestUtf8* test = tests_utf8 + i;
        char* path = g_strconcat(TEST_PREFIX "utf8/", test->name, NULL);

        g_test_add_data_func(path, test, test_utf8);
        g_free(path);
    }

    g_test_add_data_func(TEST_DECODE_("utf16BE"),
        utf16_tests + 0, test_utf16_decode);
    g_test_add_data_func(TEST_DECODE_("utf16LE_BOM"),
        utf16_tests + 1, test_utf16_decode);
    g_test_add_data_func(TEST_DECODE_("utf16BE_BOM"),
        utf16_tests + 2, test_utf16_decode);

    g_test_add_data_func(TEST_ENCODE_("utf16BE"),
        utf16_tests + 0, test_utf16_encode);
    g_test_add_data_func(TEST_ENCODE_("utf16LE_BOM"),
        utf16_tests + 1, test_utf16_encode);

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
