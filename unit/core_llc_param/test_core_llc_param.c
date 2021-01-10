/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "test_common.h"

#include "nfc_llc_param.h"

static TestOpt test_opt;

#define TEST_(name) "/core/llc_param/" name
#define TESTE_(name) TEST_("encode/" name)
#define TESTD_(name) TEST_("decode/" name)

typedef struct test_single_param_data {
    const char* name;
    GUtilData tlv;
    NfcLlcParam param;
} TestSingleParamData;

static const guint8 tlv_version_1_0[] = {
    NFC_LLC_PARAM_VERSION, 0x01, NFC_LLCP_VERSION_1_0
};
static const guint8 tlv_wks[] = {
    NFC_LLC_PARAM_WKS, 0x02, 0x01, 0x03
};
static const guint8 tlv_lto[] = {
    NFC_LLC_PARAM_LTO, 0x01, 0x01
};
static const guint8 tlv_sn[] = {
    NFC_LLC_PARAM_SN, 0x0f,
    'u', 'r', 'n', ':', 'n', 'f', 'c', ':',
    's', 'n', ':', 's', 'n', 'e', 'p'
};
static const guint8 tlv_empty_sn[] = {
    NFC_LLC_PARAM_SN, 0x00
};
static const guint8 tlv_opt[] = {
    NFC_LLC_PARAM_OPT, 0x01, NFC_LLC_OPT_CL | NFC_LLC_OPT_CO
}; 
static const guint8 tlv_empty_sdreq[] = {
    NFC_LLC_PARAM_SDREQ, 0x01, 0x0a
};
static const guint8 tlv_sdreq[] = {
    NFC_LLC_PARAM_SDREQ, 0x10, 0x0a, 'u', 'r', 'n', ':',
    'n', 'f', 'c', ':', 's', 'n', ':', 's', 'n', 'e', 'p'
};

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!nfc_llc_param_encode(NULL, NULL, 0));
    g_assert(!nfc_llc_param_decode(NULL));
    nfc_llc_param_free(NULL);
}

/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    GUtilData tlv;
    NfcLlcParam** params;
    static const guint8 data[] = { 0x00, 0x00 };
    GByteArray* bytes;

    memset(&tlv, 0, sizeof(tlv));
    params = nfc_llc_param_decode(&tlv);
    g_assert(params);
    g_assert(!params[0]);
    nfc_llc_param_free(params);

    TEST_BYTES_SET(tlv, data);
    params = nfc_llc_param_decode(&tlv);
    g_assert(params);
    g_assert(!params[0]);

    /* Encoding empty (but non-NULL) list produces empty (but non-NULL)
     * byte array */
    bytes = nfc_llc_param_encode(nfc_llc_param_constify(params), NULL, 0);
    g_assert(bytes);
    g_assert(!bytes->len);
    g_byte_array_free(bytes, TRUE);

    nfc_llc_param_free(params);
}

/*==========================================================================*
 * find
 *==========================================================================*/

static
void
test_find(
    void)
{
    GUtilData tlv;
    NfcLlcParam** params;
    const NfcLlcParam* param;

    g_assert(!nfc_llc_param_find(NULL, NFC_LLC_PARAM_VERSION));

    TEST_BYTES_SET(tlv, tlv_version_1_0);
    params = nfc_llc_param_decode(&tlv);
    g_assert(params);
    param = params[0];
    g_assert(param);
    g_assert(param->type == NFC_LLC_PARAM_VERSION);
    g_assert(!params[1]);

    g_assert(nfc_llc_param_find(nfc_llc_param_constify(params), 
        NFC_LLC_PARAM_VERSION) == param);
    g_assert(!nfc_llc_param_find(nfc_llc_param_constify(params), 
        NFC_LLC_PARAM_SN));

    nfc_llc_param_free(params);
}

/*==========================================================================*
 * count
 *==========================================================================*/

static
void
test_count(
    void)
{
    const NfcLlcParam* params[2];
    static NfcLlcParam param = { NFC_LLC_PARAM_VERSION, .value.version =
        NFC_LLCP_VERSION_1_0 };

    params[0] = NULL;
    g_assert(!nfc_llc_param_count(NULL));
    g_assert(!nfc_llc_param_count(params));

    params[0] = &param;
    params[1] = NULL;
    g_assert_cmpuint(nfc_llc_param_count(params), == ,1);
}

/*==========================================================================*
 * truncate
 *==========================================================================*/

static
void
test_truncate(
    void)
{
    static NfcLlcParam param1 = { NFC_LLC_PARAM_VERSION, .value.version =
        NFC_LLCP_VERSION_1_0 };
    static NfcLlcParam param2 = { NFC_LLC_PARAM_VERSION, .value.version =
        NFC_LLCP_VERSION_1_1 };
    static const guint8 tlv[] = {
        NFC_LLC_PARAM_VERSION, 0x01, NFC_LLCP_VERSION_1_0,
        NFC_LLC_PARAM_VERSION, 0x01, NFC_LLCP_VERSION_1_1
    };
    const NfcLlcParam* params[3];
    GByteArray* bytes = NULL;

    /* Nothing fits at all */
    params[0] = &param1;
    params[1] = NULL;
    bytes = nfc_llc_param_encode(params, bytes, 1);
    g_assert(bytes);
    g_assert_cmpuint(bytes->len, == ,0);

    /* Still nothing fits */
    params[1] = &param2;
    params[2] = NULL;
    bytes = nfc_llc_param_encode(params, bytes, 1);
    g_assert(bytes);
    g_assert_cmpuint(bytes->len, == ,0);

    /* One thing fits */
    bytes = nfc_llc_param_encode(params, bytes, 3);
    g_assert(bytes);
    g_assert_cmpuint(bytes->len, == ,3);
    g_assert(!memcmp(tlv, bytes->data, bytes->len));

    /* Both things fit (limited by exact size) */
    g_byte_array_set_size(bytes, 0);
    bytes = nfc_llc_param_encode(params, bytes, 6);
    g_assert(bytes);
    g_assert_cmpuint(bytes->len, == ,sizeof(tlv));
    g_assert(!memcmp(tlv, bytes->data, bytes->len));

    /* Both things fit again (plenty of space available) */
    g_byte_array_set_size(bytes, 0);
    bytes = nfc_llc_param_encode(params, bytes, 2 * sizeof(tlv));
    g_assert(bytes);
    g_assert_cmpuint(bytes->len, == ,sizeof(tlv));
    g_assert(!memcmp(tlv, bytes->data, bytes->len));

    g_byte_array_free(bytes, TRUE);
}

/*==========================================================================*
 * encode_list
 *==========================================================================*/

static
void
test_encode_list(
    void)
{
    static NfcLlcParam invalid = { 0 };
    static NfcLlcParam version = { NFC_LLC_PARAM_VERSION, .value.version =
        NFC_LLCP_VERSION_1_0 };
    const NfcLlcParam* params[3] = { &invalid, &version, NULL };
    GByteArray* bytes = nfc_llc_param_encode(params, NULL, 0);

    g_assert(bytes);
    g_assert_cmpuint(bytes->len, == ,sizeof(tlv_version_1_0));
    g_assert(!memcmp(bytes->data, tlv_version_1_0, bytes->len));
    g_byte_array_free(bytes, TRUE);
}

/*==========================================================================*
 * decode_list
 *==========================================================================*/

static
void
test_decode_list(
    void)
{
    GUtilData tlv;
    NfcLlcParam** params;
    const NfcLlcParam* param;
    static const guint8 data[] = {
        0x00, 0x00,
        NFC_LLC_PARAM_VERSION, 0x01, NFC_LLCP_VERSION_1_0,
        NFC_LLC_PARAM_WKS, 0x02, 0x00, 0x03
    };

    TEST_BYTES_SET(tlv, data);
    params = nfc_llc_param_decode(&tlv);
    g_assert(params);

    param = params[0];
    g_assert(param);
    g_assert_cmpint(param->type, == ,NFC_LLC_PARAM_VERSION);
    g_assert_cmpint(param->value.version, == ,NFC_LLCP_VERSION_1_0);

    param = params[1];
    g_assert(param);
    g_assert_cmpint(param->type, == ,NFC_LLC_PARAM_WKS);
    g_assert_cmpint(param->value.wks, == ,0x03);

    g_assert(!params[2]);
    nfc_llc_param_free(params);
}

/*==========================================================================*
 * decode_bytes
 *==========================================================================*/

static
void
test_decode_bytes(
    void)
{
    NfcLlcParam** params;
    const NfcLlcParam* param;
    static const guint8 data[] = {
        NFC_LLC_PARAM_VERSION, 0x01, NFC_LLCP_VERSION_1_0
    };

    g_assert(!nfc_llc_param_decode_bytes(NULL, 0));

    params = nfc_llc_param_decode_bytes(data, 0);
    g_assert(params);
    g_assert(!params[0]);
    nfc_llc_param_free(params);

    params = nfc_llc_param_decode_bytes(data, sizeof(data));
    g_assert(params);

    param = params[0];
    g_assert(param);
    g_assert_cmpint(param->type, == ,NFC_LLC_PARAM_VERSION);
    g_assert_cmpint(param->value.version, == ,NFC_LLCP_VERSION_1_0);
    g_assert(!params[1]);

    nfc_llc_param_free(params);
}

/*==========================================================================*
 * encode_single_param
 *==========================================================================*/

static const guint8 tlv_encode_miux[] = {
    NFC_LLC_PARAM_MIUX, 0x02, 0x00, 0x02
};
static const guint8 tlv_encode_miux_default[] = {
    NFC_LLC_PARAM_MIUX, 0x02, 0x00, 0x00
};
static const guint8 tlv_encode_lto_max[] = {
    NFC_LLC_PARAM_LTO, 0x01, 0xff
};
static const guint8 tlv_encode_rw[] = {
    NFC_LLC_PARAM_RW, 0x01, 0x07
};
static const guint8 tlv_encode_rw_max[] = {
    NFC_LLC_PARAM_RW, 0x01, 0x0f
};
static const guint8 tlv_encode_truncate_sn[] = {
    NFC_LLC_PARAM_SN, 0xff,
    'u', 'r', 'n', ':', 'n', 'f', 'c', ':',
    'x', 's', 'n', ':', 't', 'e', 's', 't',
    ':', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',

    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',

    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',

    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x'
};
static const guint8 tlv_encode_truncate_sdreq[] = {
    NFC_LLC_PARAM_SDREQ, 0xff, 0x0a,
    'u', 'r', 'n', ':', 'n', 'f', 'c', ':',
    'x', 's', 'n', ':', 't', 'e', 's', 't',
    ':', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',

    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',

    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',

    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x'
};
static const guint8 tlv_encode_sdres[] = {
    NFC_LLC_PARAM_SDRES, 0x02, 0x01, 0x04
};

static const TestSingleParamData encode_single_param_tests[] = {
    {
        TESTE_("version"),
        { TEST_ARRAY_AND_SIZE(tlv_version_1_0) },
        { NFC_LLC_PARAM_VERSION, .value.version = NFC_LLCP_VERSION_1_0 }
    },{
        TESTE_("miux"),
        { TEST_ARRAY_AND_SIZE(tlv_encode_miux) },
        { NFC_LLC_PARAM_MIUX, .value.miu = NFC_LLC_MIU_DEFAULT + 2 }
    },{
        TESTE_("miux_default"),
        { TEST_ARRAY_AND_SIZE(tlv_encode_miux_default) },
        { NFC_LLC_PARAM_MIUX, .value.miu = NFC_LLC_MIU_DEFAULT }
    },{
        TESTE_("wks"),
        { TEST_ARRAY_AND_SIZE(tlv_wks) },
        { NFC_LLC_PARAM_WKS, .value.wks = 0x0103 }
    },{
        TESTE_("lto"),
        { TEST_ARRAY_AND_SIZE(tlv_lto) },
        { NFC_LLC_PARAM_LTO, .value.lto = 10 }
    },{
        TESTE_("lto_max"),
        { TEST_ARRAY_AND_SIZE(tlv_encode_lto_max) },
        { NFC_LLC_PARAM_LTO, .value.lto = 3000 /* anything above 2550 */ }
    },{
        TESTE_("rw"),
        { TEST_ARRAY_AND_SIZE(tlv_encode_rw) },
        { NFC_LLC_PARAM_RW, .value.rw = 0x07 }
    },{
        TESTE_("rw_max"),
        { TEST_ARRAY_AND_SIZE(tlv_encode_rw_max) },
        { NFC_LLC_PARAM_RW, .value.rw = 0x10 /* anything above 0x0f */}
    },{
        TESTE_("sn"),
        { TEST_ARRAY_AND_SIZE(tlv_sn) },
        { NFC_LLC_PARAM_SN, .value.sn = "urn:nfc:sn:snep" }
    },{
        TESTE_("empty_sn"),
        { TEST_ARRAY_AND_SIZE(tlv_empty_sn) },
        { NFC_LLC_PARAM_SN, .value.sn = "" }
    },{
        TESTE_("null_sn"),
        { TEST_ARRAY_AND_SIZE(tlv_empty_sn) },
        { NFC_LLC_PARAM_SN }
    },{
        TESTE_("truncate_sn"),
        { TEST_ARRAY_AND_SIZE(tlv_encode_truncate_sn) },
        { NFC_LLC_PARAM_SN, .value.sn = 
          "urn:nfc:xsn:test:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" },
    },{
        TESTE_("opt"),
        { TEST_ARRAY_AND_SIZE(tlv_opt) },
        { NFC_LLC_PARAM_OPT, .value.opt = NFC_LLC_OPT_CL | NFC_LLC_OPT_CO }
    },{
        TESTE_("sdreq"),
        { TEST_ARRAY_AND_SIZE(tlv_sdreq) },
        { NFC_LLC_PARAM_SDREQ, .value.sdreq = { .tid = 0x0a, .uri =
          "urn:nfc:sn:snep"} }
    },{
        TESTE_("null_sdreq"),
        { TEST_ARRAY_AND_SIZE(tlv_empty_sdreq) },
        { NFC_LLC_PARAM_SDREQ, .value.sdreq = { .tid = 0x0a } }
    },{
        TESTE_("truncate_sdreq"),
        { TEST_ARRAY_AND_SIZE(tlv_encode_truncate_sdreq) },
        { NFC_LLC_PARAM_SDREQ, .value.sdreq = { .tid = 0x0a, .uri =
          "urn:nfc:xsn:test:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"}},
    },{
        TESTE_("sdres"),
        { TEST_ARRAY_AND_SIZE(tlv_encode_sdres) },
        { NFC_LLC_PARAM_SDRES, .value.sdres = { .tid = 0x01, .sap = 0x04 } }
    }
};

static
void
test_encode_single_param(
    gconstpointer test_data)
{
    const TestSingleParamData* test = test_data;
    const NfcLlcParam* params[2];
    GByteArray* bytes;

    params[0] = &test->param;
    params[1] = NULL;
    bytes = nfc_llc_param_encode(params, g_byte_array_new(), 0);

    g_assert(bytes);
    g_assert_cmpint(bytes->len, == ,test->tlv.size);
    g_assert(!memcmp(bytes->data, test->tlv.bytes, test->tlv.size));
    g_byte_array_free(bytes, TRUE);
}

/*==========================================================================*
 * decode_single_param
 *==========================================================================*/

static const guint8 tlv_decode_miux[] = {
    NFC_LLC_PARAM_MIUX, 0x02, 0x80 /* bit 0x80 is ignored */, 0x02
};
static const guint8 tlv_lto_default[] = {
    NFC_LLC_PARAM_LTO, 0x01, 0x00
};
static const guint8 tlv_decode_rw[] = {
    NFC_LLC_PARAM_RW, 0x01, 0x1f /* bit 0x10 is ignored */
};
static const guint8 tlv_decode_sdres[] = {
    NFC_LLC_PARAM_SDRES, 0x02, 0x01, 0xff /* bits 0xc0 are ignored */
};

static const TestSingleParamData decode_single_param_tests[] = {
    {
        TESTD_("version"),
        { TEST_ARRAY_AND_SIZE(tlv_version_1_0) },
        { NFC_LLC_PARAM_VERSION, .value.version = NFC_LLCP_VERSION_1_0 }
    },{
        TESTD_("miux"),
        { TEST_ARRAY_AND_SIZE(tlv_decode_miux) },
        { NFC_LLC_PARAM_MIUX, .value.miu = NFC_LLC_MIU_DEFAULT + 2 }
    },{
        TESTD_("wks"),
        { TEST_ARRAY_AND_SIZE(tlv_wks) },
        { NFC_LLC_PARAM_WKS, .value.wks = 0x0103 }
    },{
        TESTD_("lto"),
        { TEST_ARRAY_AND_SIZE(tlv_lto) },
        { NFC_LLC_PARAM_LTO, .value.lto = 10 }
    },{
        TESTD_("lto_default"),
        { TEST_ARRAY_AND_SIZE(tlv_lto_default) },
        { NFC_LLC_PARAM_LTO, .value.lto = NFC_LLC_LTO_DEFAULT }
    },{
        TESTD_("rw"),
        { TEST_ARRAY_AND_SIZE(tlv_decode_rw) },
        { NFC_LLC_PARAM_RW, .value.rw = 0x0f }
    },{
        TESTD_("opt"),
        { TEST_ARRAY_AND_SIZE(tlv_opt) },
        { NFC_LLC_PARAM_OPT, .value.opt = NFC_LLC_OPT_CL | NFC_LLC_OPT_CO }
    },{
        TESTD_("sdres"),
        { TEST_ARRAY_AND_SIZE(tlv_decode_sdres) },
        { NFC_LLC_PARAM_SDRES, .value.sdres = { .tid = 0x01, .sap = 0x3f } }
    }
};

static
void
test_decode_single_param(
    gconstpointer test_data)
{
    const TestSingleParamData* test = test_data;
    NfcLlcParam** params;
    const NfcLlcParam* param;

    params = nfc_llc_param_decode(&test->tlv);
    g_assert(params);
    param = params[0];
    g_assert(param);
    g_assert_cmpint(param->type, == ,test->param.type);
    g_assert(!memcmp(&param->value, &test->param.value, sizeof(param->value)));

    g_assert(!params[1]);
    nfc_llc_param_free(params);
}

/*==========================================================================*
 * sn
 *==========================================================================*/

static
void
test_sn(
    void)
{
    GUtilData tlv;
    NfcLlcParam** params;
    const NfcLlcParam* param;

    TEST_BYTES_SET(tlv, tlv_sn);
    params = nfc_llc_param_decode(&tlv);
    g_assert(params);
    param = params[0];
    g_assert(param);
    g_assert_cmpint(param->type, == ,NFC_LLC_PARAM_SN);
    g_assert_cmpstr(param->value.sn, == ,"urn:nfc:sn:snep");

    g_assert(!params[1]);
    nfc_llc_param_free(params);
}

/*==========================================================================*
 * sdreq
 *==========================================================================*/

static
void
test_sdreq(
    void)
{
    GUtilData tlv;
    NfcLlcParam** params;
    const NfcLlcParam* param;

    TEST_BYTES_SET(tlv, tlv_sdreq);
    params = nfc_llc_param_decode(&tlv);
    g_assert(params);
    param = params[0];
    g_assert(param);
    g_assert_cmpint(param->type, == ,NFC_LLC_PARAM_SDREQ);
    g_assert_cmpint(param->value.sdreq.tid, == ,0x0a);
    g_assert_cmpstr(param->value.sdreq.uri, == ,"urn:nfc:sn:snep");

    g_assert(!params[1]);
    nfc_llc_param_free(params);
}

/*==========================================================================*
 * decode_invalid_param
 *==========================================================================*/

typedef struct test_invalid_param_data {
    const char* name;
    GUtilData tlv;
} TestInvalidParamData;

static const guint8 tlv_oob[] = { 0x00, 0x01 /* out of bounds */ };
static const guint8 tlv_invalid_version[] = { NFC_LLC_PARAM_VERSION, 0x00 };
static const guint8 tlv_invalid_miux[] = { NFC_LLC_PARAM_MIUX, 0x01, 0x00 };
static const guint8 tlv_invalid_wks[] = { NFC_LLC_PARAM_WKS, 0x01, 0x00 };
static const guint8 tlv_invalid_lto[] = { NFC_LLC_PARAM_LTO, 0x00 };
static const guint8 tlv_invalid_rw[] = { NFC_LLC_PARAM_RW, 0x00 };
static const guint8 tlv_invalid_opt[] = { NFC_LLC_PARAM_OPT, 0x00 };
static const guint8 tlv_invalid_sdreq[] = { NFC_LLC_PARAM_SDREQ, 0x00 };
static const guint8 tlv_invalid_sdres[] = { NFC_LLC_PARAM_SDRES, 0x01, 0x00 };

static const TestInvalidParamData decode_invalid_param_tests[] = {
    { TESTD_("oob"), {TEST_ARRAY_AND_SIZE(tlv_oob)} },
    { TESTD_("invalid_version"), {TEST_ARRAY_AND_SIZE(tlv_invalid_version)} },
    { TESTD_("invalid_miux"), {TEST_ARRAY_AND_SIZE(tlv_invalid_miux)} },
    { TESTD_("invalid_wks"), {TEST_ARRAY_AND_SIZE(tlv_invalid_wks)} },
    { TESTD_("invalid_lto"), {TEST_ARRAY_AND_SIZE(tlv_invalid_lto)} },
    { TESTD_("invalid_rw"), {TEST_ARRAY_AND_SIZE(tlv_invalid_rw)} },
    { TESTD_("invalid_opt"), {TEST_ARRAY_AND_SIZE(tlv_invalid_opt)} },
    { TESTD_("invalid_sdreq"), {TEST_ARRAY_AND_SIZE(tlv_invalid_sdreq)} },
    { TESTD_("invalid_sdres"), {TEST_ARRAY_AND_SIZE(tlv_invalid_sdres)} }
};

static
void
test_decode_invalid_param(
    gconstpointer test_data)
{
    const TestInvalidParamData* test = test_data;
    NfcLlcParam** params = nfc_llc_param_decode(&test->tlv);

    g_assert(params);
    g_assert(!params[0]);
    nfc_llc_param_free(params);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("empty"), test_empty);
    g_test_add_func(TEST_("find"), test_find);
    g_test_add_func(TEST_("count"), test_count);
    g_test_add_func(TESTE_("truncate"), test_truncate);
    g_test_add_func(TESTE_("list"), test_encode_list);
    g_test_add_func(TESTD_("list"), test_decode_list);
    g_test_add_func(TESTD_("bytes"), test_decode_bytes);
    g_test_add_func(TESTD_("sn"), test_sn);
    g_test_add_func(TESTD_("sdreq"), test_sdreq);
    for (i = 0; i < G_N_ELEMENTS(encode_single_param_tests); i++) {
        const TestSingleParamData* test = encode_single_param_tests + i;

        g_test_add_data_func(test->name, test, test_encode_single_param);
    }
    for (i = 0; i < G_N_ELEMENTS(decode_single_param_tests); i++) {
        const TestSingleParamData* test = decode_single_param_tests + i;

        g_test_add_data_func(test->name, test, test_decode_single_param);
    }
    for (i = 0; i < G_N_ELEMENTS(decode_invalid_param_tests); i++) {
        const TestInvalidParamData* test = decode_invalid_param_tests + i;

        g_test_add_data_func(test->name, test, test_decode_invalid_param);
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
