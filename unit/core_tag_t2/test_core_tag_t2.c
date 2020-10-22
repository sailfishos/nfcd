/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2020 Open Mobile Platform LLC.
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

#include "nfc_ndef.h"
#include "nfc_tag_p.h"
#include "nfc_tag_t2.h"
#include "nfc_target_impl.h"

#include <gutil_log.h>
#include <gutil_misc.h>

static TestOpt test_opt;

#define SUPER_LONG_TIMEOUT (24*60*60) /* seconds */

static
void
test_unexpected_destroy(
    gpointer user_data)
{
    g_assert(FALSE);
}

static
void
test_unexpected_read_completion(
    NfcTagType2* t2,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    g_assert(FALSE);
}

static
void
test_unexpected_write_completion(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(FALSE);
}

static
void
test_unexpected_write_data_completion(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(FALSE);
}

static
void
test_destroy_quit_loop(
    gpointer user_data)
{
    g_main_loop_quit((GMainLoop*)user_data);
}

/*==========================================================================*
 * Test target
 *==========================================================================*/

static const guint8 test_data_empty[] = {
    0x04, 0xd4, 0xfb, 0xa3, 0x4a, 0xeb, 0x2b, 0x80,
    0x0a, 0x48, 0x00, 0x00, 0xe1, 0x10, 0x12, 0x00,
    0x01, 0x03, 0xa0, 0x10, 0x44, 0x03, 0x00, 0xfe,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const guint8 test_data_unsup1[] = { /* Zero spec version in CC */
    0x04, 0xd4, 0xfb, 0xa3, 0x4a, 0xeb, 0x2b, 0x80,
    0x0a, 0x48, 0x00, 0x00, 0xe1, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const GUtilData test_unsup1_bytes = {
    TEST_ARRAY_AND_SIZE(test_data_unsup1)
};

static const guint8 test_data_unsup2[] = { /* No FC Forum magic in CC */
    0x04, 0xd4, 0xfb, 0xa3, 0x4a, 0xeb, 0x2b, 0x80,
    0x0a, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const GUtilData test_unsup2_bytes = {
    TEST_ARRAY_AND_SIZE(test_data_unsup2)
};

static const guint8 test_data_google[] = { /* "http://google.com" */
    0x04, 0x9b, 0xfb, 0xec, 0x4a, 0xeb, 0x2b, 0x80,
    0x0a, 0x48, 0x00, 0x00, 0xe1, 0x10, 0x12, 0x00,
    0x03, 0x0f, 0xd1, 0x01, 0x0b,  'U', 0x03,  'g',
     'o',  'o',  'g',  'l',  'e',  '.',  'c',  'o',
    'm',  0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#define NDEF_GOOGLE_COM_SIZE_EXACT (0x22)
#define NDEF_GOOGLE_COM_SIZE_ALIGNED (0x24)

static const guint8 jolla_rec[] = { /* "https://www.jolla.com" */
    0x03, 0x0e, 0xd1, 0x01, 0x0a,  'U', 0x02,  'j',
     'o',  'l',  'l',  'a',  '.',  'c',  'o',  'm',
    0xfe, 0x00, 0x00, 0x00
};

#define NDEF_JOLLA_COM_SIZE_EXACT (sizeof(jolla_rec) - 3)

static const guint8 test_data_jolla[] = { /* "https://www.jolla.com" */
    0x04, 0x9b, 0xfb, 0xec, 0x4a, 0xeb, 0x2b, 0x80,
    0x0a, 0x48, 0x00, 0x00, 0xe1, 0x10, 0x12, 0x00,
    0x03, 0x0e, 0xd1, 0x01, 0x0a,  'U', 0x02,  'j',
     'o',  'l',  'l',  'a',  '.',  'c',  'o',  'm',
    0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * UID: 04 ea 3d 9a 85 5c 80
 * Data size: 872 bytes
 */
static const guint8 test_data_ntag216[] = { /* "https://www.merproject.org" */
    0x04, 0xea, 0x3d, 0x5b, 0x9a, 0x85, 0x5c, 0x80,
    0xc3, 0x48, 0x00, 0x00, 0xe1, 0x10, 0x6d, 0x00,
    0x03, 0x13, 0xd1, 0x01, 0x0f, 0x55, 0x02, 0x6d,
    0x65, 0x72, 0x70, 0x72, 0x6f, 0x6a, 0x65, 0x63,
    0x74, 0x2e, 0x6f, 0x72, 0x67, 0xfe, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef struct test_target_error TestTargetError;
typedef NfcTargetClass TestTargetClass;
typedef struct test_target {
    NfcTarget target;
    guint transmit_id;
    guint8* storage;
    GUtilData data;
    const TestTargetError* read_error;
    const TestTargetError* write_error;
} TestTarget;

#define TEST_TARGET_READ_SIZE (16)
#define TEST_TARGET_BLOCK_SIZE (4)
#define TEST_FIRST_DATA_BLOCK (4)
#define TEST_DATA_OFFSET (TEST_FIRST_DATA_BLOCK * TEST_TARGET_BLOCK_SIZE)
G_DEFINE_TYPE(TestTarget, test_target, NFC_TYPE_TARGET)
#define TEST_TYPE_TARGET (test_target_get_type())
#define TEST_TARGET(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj, \
        TEST_TYPE_TARGET, TestTarget))

typedef enum test_target_error_type {
     TEST_TARGET_ERROR_TRANSMIT,
     TEST_TARGET_ERROR_CRC,
     TEST_TARGET_ERROR_NACK,
     TEST_TARGET_ERROR_SHORT_RESP,
     TEST_TARGET_ERROR_TIMEOUT
} TEST_TARGET_ERROR_TYPE;

struct test_target_error {
    TEST_TARGET_ERROR_TYPE type;
    guint block;
};

typedef struct test_target_read {
    TestTarget* test;
    guint block;
} TestTargetRead;

typedef struct test_target_write {
    TestTarget* test;
    guint block;
    guint8* data;
    guint size;
} TestTargetWrite;

static
NfcTagType2*
test_tag_new(
    TestTarget* test,
    guint8 sel_res)
{
    static const guint8 nfcid1[] = {0x04, 0x9b, 0xfb, 0x4a, 0xeb, 0x2b, 0x80};
    NfcParamPollA param;
    NfcTagType2* tag;

    memset(&param, 0, sizeof(param));
    TEST_BYTES_SET(param.nfcid1, nfcid1);
    param.sel_res = sel_res;
    tag = nfc_tag_t2_new(&test->target, &param);
    g_assert(tag);
    return tag;
}

static
TestTarget*
test_target_new(
     const guint8* bytes,
     guint size)
{
     TestTarget* self = g_object_new(TEST_TYPE_TARGET, NULL);

     self->target.technology = NFC_TECHNOLOGY_A;
     self->data.bytes = self->storage = g_memdup(bytes, size);
     self->data.size = size;
     return self;
}

static
gboolean
test_target_read_done(
    gpointer user_data)
{
    TestTargetRead* read = user_data;
    TestTarget* test = read->test;
    NfcTarget* target = &test->target;
    const GUtilData* data = &test->data;
    NFC_TRANSMIT_STATUS status = NFC_TRANSMIT_STATUS_OK;
    guint offset = (read->block * TEST_TARGET_BLOCK_SIZE) % data->size;
    guint8 buf[TEST_TARGET_READ_SIZE];
    guint len = sizeof(buf);

    g_assert(test->transmit_id);
    test->transmit_id = 0;

    if ((offset + TEST_TARGET_READ_SIZE) <= data->size) {
        memcpy(buf, data->bytes + offset, TEST_TARGET_READ_SIZE);
    } else {
        const guint remain = (offset + TEST_TARGET_READ_SIZE) - data->size;

        memcpy(buf, data->bytes + offset, TEST_TARGET_READ_SIZE - remain);
        memcpy(buf + (TEST_TARGET_READ_SIZE - remain), data->bytes, remain);
    }

    if (test->read_error && test->read_error->block == read->block) {
        switch (test->read_error->type) {
        case TEST_TARGET_ERROR_TRANSMIT:
            status = NFC_TRANSMIT_STATUS_ERROR;
            len = 0;
            break;
        case TEST_TARGET_ERROR_CRC:
            status = NFC_TRANSMIT_STATUS_CORRUPTED;
            len = 0;
            break;
        case TEST_TARGET_ERROR_NACK:
            status = NFC_TRANSMIT_STATUS_NACK;
            buf[0] = 0;
            len = 1;
            break;
        case TEST_TARGET_ERROR_SHORT_RESP:
            buf[0] = 0x08; /* Neither ACK nor NACK */
            len = 1;
            break;
        case TEST_TARGET_ERROR_TIMEOUT:
            test->read_error = NULL;
            /* To avoid glib assert on cancel */
            test->transmit_id = g_timeout_add_seconds(SUPER_LONG_TIMEOUT,
                test_timeout_expired, NULL);
            /* Don't call nfc_target_transmit_done() */
            return G_SOURCE_REMOVE;
        }
        test->read_error = NULL;
    }

    nfc_target_transmit_done(target, status, buf, len);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_target_write_done(
    gpointer user_data)
{
    TestTargetWrite* write = user_data;
    TestTarget* test = write->test;
    NfcTarget* target = &test->target;
    guint8 ack = 0xaa;
    guint len = 1;
    NFC_TRANSMIT_STATUS status = NFC_TRANSMIT_STATUS_OK;

    g_assert(test->transmit_id);
    test->transmit_id = 0;

    if (test->write_error && test->write_error->block == write->block) {
        switch (test->write_error->type) {
        case TEST_TARGET_ERROR_TRANSMIT:
            status = NFC_TRANSMIT_STATUS_ERROR;
            len = 0;
            break;
        case TEST_TARGET_ERROR_CRC:
            g_assert(FALSE);
            break;
        case TEST_TARGET_ERROR_NACK:
            ack = 0;
            break;
        case TEST_TARGET_ERROR_SHORT_RESP:
            g_assert(FALSE);
            break;
        case TEST_TARGET_ERROR_TIMEOUT:
            test->read_error = NULL;
            /* To avoid glib assert on cancel */
            test->transmit_id = g_timeout_add_seconds(SUPER_LONG_TIMEOUT,
                test_timeout_expired, NULL);
            /* Don't call nfc_target_transmit_done() */
            return G_SOURCE_REMOVE;
        }
        test->write_error = NULL;
    } else {
        const guint storage_size = test->data.size;
        guint offset = (write->block * TEST_TARGET_BLOCK_SIZE) % storage_size;
        guint size = write->size;
        const guint8* src = write->data;

        while (size > 0) {
            if ((offset + size) <= storage_size) {
                memcpy(test->storage + offset, src, size);
                break;
            } else {
                const guint to_copy = storage_size - offset;

                memcpy(test->storage + offset, src, to_copy);
                size -= to_copy;
                src += to_copy;
                offset = 0;
            }
        }
    }

    nfc_target_transmit_done(target, status, &ack, len);
    return G_SOURCE_REMOVE;
}

static
void
test_target_write_free(
    gpointer user_data)
{
    TestTargetWrite* write = user_data;

    g_free(write->data);
    g_free(write);
}

static
gboolean
test_target_transmit(
    NfcTarget* target,
    const void* data,
    guint len)
{
    TestTarget* test = TEST_TARGET(target);

    g_assert(!test->transmit_id);
    if (len > 0) {
        const guint8* cmd = data;

        switch (cmd[0]) {
        case 0x30: /* READ */
            if (len == 2) {
                TestTargetRead* read = g_new(TestTargetRead, 1);

                read->test = test;
                read->block = cmd[1];
                GDEBUG("Read block #%u", read->block);
                test->transmit_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                    test_target_read_done, read, g_free);
                return TRUE;
            }
            break;
        case 0xa2: /* WRITE */
            if (len >= 2) {
                TestTargetWrite* write = g_new(TestTargetWrite, 1);

                write->test = test;
                write->block = cmd[1];
                write->size = len - 2;
                write->data = g_memdup(cmd + 2, write->size);
                GDEBUG("Write block #%u, %u bytes", write->block, write->size);
                test->transmit_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                    test_target_write_done, write, test_target_write_free);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static
void
test_target_cancel_transmit(
    NfcTarget* target)
{
    TestTarget* test = TEST_TARGET(target);

    g_assert(test->transmit_id);
    g_source_remove(test->transmit_id);
    test->transmit_id = 0;
}

static
void
test_target_init(
    TestTarget* self)
{
}

static
void
test_target_finalize(
    GObject* object)
{
    TestTarget* test = TEST_TARGET(object);

    if (test->transmit_id) {
        g_source_remove(test->transmit_id);
    }
    g_free(test->storage);
    G_OBJECT_CLASS(test_target_parent_class)->finalize(object);
}

static
void
test_target_class_init(
    NfcTargetClass* klass)
{
    klass->transmit = test_target_transmit;
    klass->cancel_transmit = test_target_cancel_transmit;
    G_OBJECT_CLASS(klass)->finalize = test_target_finalize;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    NfcTarget* target = g_object_new(TEST_TYPE_TARGET, NULL);

    /* Public interfaces are NULL tolerant */
    g_assert(!nfc_tag_t2_new(NULL, NULL));
    g_assert(!nfc_tag_t2_new(target, NULL));
    g_assert(!nfc_tag_t2_read(NULL, 0, 0, NULL, NULL, NULL));
    g_assert(!nfc_tag_t2_read_data(NULL, 0, 0, NULL, NULL, NULL));
    g_assert(nfc_tag_t2_read_data_sync(NULL, 0, 0, NULL) ==
        NFC_TAG_T2_IO_STATUS_FAILURE);
    g_assert(!nfc_tag_t2_write(NULL, 0, 0, NULL, NULL, NULL, NULL));
    g_assert(!nfc_tag_t2_write_data(NULL, 0, NULL, NULL, NULL, NULL));
    nfc_target_unref(target);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic_exit(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);

    g_assert(gutil_data_equal(&t2->serial, &t2->nfcid1));
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_basic_check_sel_res(
    TestTarget* test,
    guint8 sel_res,
    NFC_TAG_TYPE type)
{
    NfcTagType2* t2 = test_tag_new(test, sel_res);
    NfcTag* tag = &t2->tag;

    g_assert(tag->type == type);
    nfc_tag_unref(tag);
}

static
void
test_basic(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_empty));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_tag_add_initialized_handler(tag, test_basic_exit, loop);
    guint i;
    static const guint8 mifare_classic_sel_res[] = {
        0x01, 0x08, 0x09, 0x10, 0x11, 0x18, 0x28, 0x38, 0x88, 0x98, 0xB8
    };

    for (i = 0; i < G_N_ELEMENTS(mifare_classic_sel_res); i++) {
        test_basic_check_sel_res(test, mifare_classic_sel_res[i],
            NFC_TAG_TYPE_MIFARE_CLASSIC);
    }

    test_basic_check_sel_res(test, 0x02, NFC_TAG_TYPE_UNKNOWN);
    g_assert(tag->type == NFC_TAG_TYPE_MIFARE_ULTRALIGHT);
    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * unsup
 *==========================================================================*/

static
void
test_unsup_done(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);

    g_assert(tag->flags & NFC_TAG_FLAG_INITIALIZED);

    g_assert(gutil_data_equal(&t2->serial, &t2->nfcid1));

    /* But no NDEF and no size */
    g_assert(!tag->ndef);
    g_assert(!t2->data_size);
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_unsup(
    gconstpointer test_data)
{
    const GUtilData* data = test_data;
    TestTarget* test = test_target_new(data->bytes, data->size);
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_tag_add_initialized_handler(tag, test_unsup_done, loop);

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_init_err1
 *==========================================================================*/

static
void
test_init_err1_done(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);

    g_assert(tag->flags & NFC_TAG_FLAG_INITIALIZED);
    g_assert(gutil_data_equal(&t2->serial, &t2->nfcid1));
    g_assert(!t2->data_size);
    g_assert(!tag->ndef);
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_init_err1(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_empty));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_tag_add_initialized_handler(tag, test_init_err1_done, loop);

    /* Damage CRC for the very first block */
    memset(&error, 0, sizeof(error));
    error.type = TEST_TARGET_ERROR_CRC;
    test->read_error = &error;

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_init_err2
 *==========================================================================*/

static
void
test_init_err2_done(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);

    g_assert(tag->flags & NFC_TAG_FLAG_INITIALIZED);
    g_assert(t2->data_size);

    g_assert(gutil_data_equal(&t2->serial, &t2->nfcid1));

    /* But no NDEF */
    g_assert(!tag->ndef);
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_init_err2(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_empty));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_tag_add_initialized_handler(tag, test_init_err2_done, loop);

    /* Generate transmission error for a block containing NDEF */
    memset(&error, 0, sizeof(error));
    error.block = TEST_FIRST_DATA_BLOCK;
    error.type = TEST_TARGET_ERROR_TRANSMIT;
    test->read_error = &error;

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_data
 *==========================================================================*/

static
void
test_read_data_done(
    NfcTagType2* t2,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    TestTarget* test = TEST_TARGET(t2->tag.target);

    g_assert(len == t2->data_size);
    g_assert(!memcmp(data, test->data.bytes + TEST_DATA_OFFSET,
        t2->data_size));
}

static
void
test_read_data_start(
    NfcTag* tag,
    void* user_data)
{
    TestTarget* test = TEST_TARGET(tag->target);
    NfcTagType2* t2 = NFC_TAG_T2(tag);
    NfcNdefRec* rec = tag->ndef;
    guint8* buf;

    g_assert(gutil_data_equal(&t2->serial, &t2->nfcid1));
    g_assert(t2->data_size == test->data.size - TEST_DATA_OFFSET);

    g_assert(rec);
    g_assert(!rec->next);
    g_assert(NFC_IS_NDEF_REC_U(rec));
    g_assert(!g_strcmp0(NFC_NDEF_REC_U(rec)->uri, "http://google.com"));

    /* First two data blocks must have been read */
    buf = g_malloc(t2->data_size);
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, 32, buf) ==
        NFC_TAG_T2_IO_STATUS_OK);
    g_assert(!memcmp(buf, test->data.bytes + TEST_DATA_OFFSET, 32));

    /* But not the rest */
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, t2->data_size, buf) ==
        NFC_TAG_T2_IO_STATUS_NOT_CACHED);
    /* Try to read one more byte than there is available - that's ok */
    g_assert(nfc_tag_t2_read_data(t2, 0, t2->data_size + 1,
        test_read_data_done, test_destroy_quit_loop, user_data /* loop */));
    g_free(buf);
}

static
void
test_read_data(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong init_id = nfc_tag_add_initialized_handler(tag,
        test_read_data_start, loop);
    guint8* buf;

    test_run(&test_opt, loop);

    /* Read beyond the end of data */
    g_assert(nfc_tag_t2_read_data_sync(t2, t2->data_size, 1, NULL) ==
        NFC_TAG_T2_IO_STATUS_BAD_BLOCK);
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, t2->data_size + 1, NULL) ==
        NFC_TAG_T2_IO_STATUS_BAD_SIZE);

    /* Now the whole thing must be cached */
    g_assert(t2->data_size == test->data.size - TEST_DATA_OFFSET);
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, t2->data_size, NULL) ==
        NFC_TAG_T2_IO_STATUS_OK);
    buf = g_malloc(t2->data_size);
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, t2->data_size, buf) ==
        NFC_TAG_T2_IO_STATUS_OK);
    g_assert(!memcmp(buf, test->data.bytes + TEST_DATA_OFFSET, t2->data_size));
    g_free(buf);

    /* This one will still complete asynchronously */
    g_assert(nfc_tag_t2_read_data(t2, 0, t2->data_size, test_read_data_done,
        test_destroy_quit_loop, loop));

    test_run(&test_opt, loop);

    /* And this one will be canceled when we delete the tag */
    g_assert(nfc_tag_t2_read_data(t2, 0, t2->data_size, test_read_data_done,
        test_destroy_quit_loop, loop));

    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_data_872
 *==========================================================================*/

static
void
test_read_data_872_start(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);
    NfcNdefRec* rec = tag->ndef;

    g_assert(t2->data_size == 872);

    g_assert(rec);
    g_assert(!rec->next);
    g_assert(NFC_IS_NDEF_REC_U(rec));
    g_assert(!g_strcmp0(NFC_NDEF_REC_U(rec)->uri,
        "https://www.merproject.org"));

    /* Note: reusing test_read_data_done callback */
    g_assert(nfc_tag_t2_read_data(t2, 0, t2->data_size,
        test_read_data_done, test_destroy_quit_loop, user_data /* loop */));
}

static
void
test_read_data_872(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_ntag216));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong init_id = nfc_tag_add_initialized_handler(tag,
        test_read_data_872_start, loop);

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_data_cached
 *==========================================================================*/

#define TEST_READ_DATA_CACHED_OFFSET (1)
#define TEST_READ_DATA_CACHED_SIZE (2)

static
void
test_read_data_cached_done(
    NfcTagType2* t2,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_OK);
    g_assert(len == TEST_READ_DATA_CACHED_SIZE);
    g_assert(!memcmp(data, test_data_empty + TEST_DATA_OFFSET +
        TEST_READ_DATA_CACHED_OFFSET, len));
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_read_data_cached_start(
    NfcTag* tag,
    void* loop)
{
    g_assert(nfc_tag_t2_read_data(NFC_TAG_T2(tag),
        TEST_READ_DATA_CACHED_OFFSET, TEST_READ_DATA_CACHED_SIZE,
        test_read_data_cached_done, test_destroy_quit_loop, loop));
}

static
void
test_read_data_cached(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_empty));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong init_id = nfc_tag_add_initialized_handler(tag,
        test_read_data_cached_start, loop);

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_data_abort
 *==========================================================================*/

static
void
test_read_data_abort_start(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);

    /* Submit read request */
    g_assert(nfc_tag_t2_read_data(t2, 0, t2->data_size,
        test_unexpected_read_completion, NULL, user_data));

    /* And immediately terminate the loop */
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_read_data_abort(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong init_id = nfc_tag_add_initialized_handler(tag,
        test_read_data_abort_start, loop);

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_data_err
 *==========================================================================*/

#define TEST_READ_DATA_ERR_BLOCK (4)

static
void
test_read_data_err_done(
    NfcTagType2* t2,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_IO_ERROR);
    g_assert(len == (TEST_READ_DATA_ERR_BLOCK * TEST_FIRST_DATA_BLOCK));
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_read_data_err_start(
    NfcTag* tag,
    void* loop)
{
    g_assert(nfc_tag_t2_read_data(NFC_TAG_T2(tag), 0,
        TEST_READ_DATA_ERR_BLOCK * TEST_TARGET_BLOCK_SIZE + 1,
        test_read_data_err_done, test_destroy_quit_loop, loop));
}

static
void
test_read_data_err(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_empty));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong init_id = nfc_tag_add_initialized_handler(tag,
        test_read_data_err_start, loop);

    /* Damage CRC for data block #4 (not fetched during initialization) */
    memset(&error, 0, sizeof(error));
    error.block = TEST_FIRST_DATA_BLOCK + TEST_READ_DATA_ERR_BLOCK;
    error.type = TEST_TARGET_ERROR_CRC;
    test->read_error = &error;

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_crc_err
 *==========================================================================*/

static
void
test_read_crc_err_done(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    g_assert(status == NFC_TRANSMIT_STATUS_CORRUPTED);
    g_assert(!len);
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_read_crc_err_start(
    NfcTag* tag,
    void* user_data)
{
    g_assert(nfc_tag_t2_read(NFC_TAG_T2(tag), 0, 16, test_read_crc_err_done,
        test_destroy_quit_loop, user_data /* loop */));
}

static
void
test_read_crc_err(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_empty));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong init_id = nfc_tag_add_initialized_handler(tag,
        test_read_crc_err_start, loop);

    /* Damage CRC for block #16 (not fetched during initialization) */
    memset(&error, 0, sizeof(error));
    error.block = 16;
    error.type = TEST_TARGET_ERROR_CRC;
    test->read_error = &error;

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_nack
 *==========================================================================*/

static
void
test_read_nack_done(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    const guint8* payload = data;

    g_assert(status == NFC_TRANSMIT_STATUS_NACK);
    g_assert(len == 1);
    g_assert(!(payload[0] & 0x0a));
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_read_nack_start(
    NfcTag* tag,
    void* user_data)
{
    g_assert(nfc_tag_t2_read(NFC_TAG_T2(tag), 0, 16, test_read_nack_done,
        test_destroy_quit_loop, user_data /* loop */));
}

static
void
test_read_nack(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_empty));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong init_id = nfc_tag_add_initialized_handler(tag,
        test_read_nack_start, loop);

    /* Generate NACK for block #16 (not fetched during initialization) */
    memset(&error, 0, sizeof(error));
    error.block = 16;
    error.type = TEST_TARGET_ERROR_NACK;
    test->read_error = &error;

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * read_timeout
 *==========================================================================*/

#define TEST_READ_TIMEOUT_BLOCK (4)

static
void
test_read_timeout_done(
    NfcTagType2* t2,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_IO_ERROR);
    g_assert(len == TEST_DATA_OFFSET); /* This much was cached */
    g_main_loop_quit((GMainLoop*)user_data);
}

static
void
test_read_timeout_start(
    NfcTag* tag,
    void* loop)
{
    g_assert(nfc_tag_t2_read_data(NFC_TAG_T2(tag), 0,
        TEST_READ_TIMEOUT_BLOCK * TEST_TARGET_BLOCK_SIZE + 1,
        test_read_timeout_done, test_destroy_quit_loop, loop));
}

static
void
test_read_timeout(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_empty));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong init_id = nfc_tag_add_initialized_handler(tag,
        test_read_timeout_start, loop);

    /* Never complete read of block #4 (not fetched during initialization) */
    memset(&error, 0, sizeof(error));
    error.block = TEST_FIRST_DATA_BLOCK + TEST_READ_TIMEOUT_BLOCK;
    error.type = TEST_TARGET_ERROR_TIMEOUT;
    test->read_error = &error;

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, init_id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * write
 *==========================================================================*/

static
void
test_write_done(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TRANSMIT_STATUS_OK);
    g_assert(written == sizeof(jolla_rec));
}

static
void
test_write_check(
    NfcTagType2* t2,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    TestTarget* test = TEST_TARGET(t2->tag.target);

    g_assert(len == t2->data_size);
    g_assert(!memcmp(data, test->data.bytes + TEST_DATA_OFFSET, t2->data_size));
}

static
void
test_write_start(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);
    GBytes* rec = g_bytes_new_static(TEST_ARRAY_AND_SIZE(jolla_rec));

    g_assert(nfc_tag_t2_write(t2, 0, 4, rec, test_write_done,
        test_destroy_quit_loop, user_data /* loop */));
    g_bytes_unref(rec);
}

static
void
test_write(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    GBytes* rec = g_bytes_new_static(TEST_ARRAY_AND_SIZE(jolla_rec));
    GBytes* short_buf = g_bytes_new_static(jolla_rec, 3);
    gulong id = nfc_tag_add_initialized_handler(tag, test_write_start, loop);

    /* We can't just start writing right away */
    g_assert(!nfc_tag_t2_write(t2, 0, 4, rec,
        test_unexpected_write_completion,
        test_unexpected_destroy, NULL));

    /* And we write have at least one block */
    g_assert(!nfc_tag_t2_write(t2, 0, 4, short_buf, NULL, NULL, NULL));

    test_run(&test_opt, loop);

    /* Check the contents */
    g_assert(!memcmp(test->storage, test_data_jolla, sizeof(test_data_jolla)));

    /* The whole thing is still not cached */
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, t2->data_size, NULL) ==
        NFC_TAG_T2_IO_STATUS_NOT_CACHED);

    /* This one will still complete asynchronously */
    g_assert(nfc_tag_t2_read_data(t2, 0, t2->data_size, test_write_check,
        test_destroy_quit_loop, loop));

    test_run(&test_opt, loop);

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_bytes_unref(rec);
    g_bytes_unref(short_buf);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * write_data1
 *==========================================================================*/

static
void
test_write_data1_done(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_OK);
    g_assert(written == sizeof(jolla_rec));
}

static
void
test_write_data1_start(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);
    GBytes* rec = g_bytes_new_static(TEST_ARRAY_AND_SIZE(jolla_rec));

    g_assert(nfc_tag_t2_write_data(t2, 0, rec, test_write_data1_done,
        test_destroy_quit_loop, user_data /* loop */));
    g_bytes_unref(rec);
}

static
void
test_write_data1(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    GBytes* rec = g_bytes_new_static(TEST_ARRAY_AND_SIZE(jolla_rec));
    gulong id = nfc_tag_add_initialized_handler(tag,
        test_write_data1_start, loop);

    /* Data is required */
    g_assert(!nfc_tag_t2_write_data(t2, 0, NULL,
        test_unexpected_write_data_completion,
        test_unexpected_destroy, NULL));

    /* We can't just start writing right away */
    g_assert(!nfc_tag_t2_write_data(t2, 0, rec,
        test_unexpected_write_data_completion,
        test_unexpected_destroy, NULL));

    test_run(&test_opt, loop);

    /* Check the contents */
    g_assert(!memcmp(test->data.bytes + TEST_DATA_OFFSET, jolla_rec,
        sizeof(jolla_rec)));

    /* It's not considered cached anymore */
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, sizeof(jolla_rec), NULL) ==
        NFC_TAG_T2_IO_STATUS_NOT_CACHED);

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_bytes_unref(rec);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * write_data2
 *==========================================================================*/

typedef struct test_write_data2_really_start {
    NfcTagType2* t2;
    GMainLoop* loop;
} TestWriteData2Start;

static
void
test_write_data2_done(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_OK);
    g_assert(written == (NDEF_JOLLA_COM_SIZE_EXACT - 1));
}

static
gboolean
test_write_data2_really_start(
    gpointer user_data)
{
    TestWriteData2Start* start = user_data;

    /* Skip the first byte, to make it completely unaligned, both the first
     * and the last block. The first is the same for both NDEF records
     * anyway. */
    GBytes* rec = g_bytes_new_static(jolla_rec + 1,
        NDEF_JOLLA_COM_SIZE_EXACT - 1);

    /* And start writing at offset 1 */
    g_assert(nfc_tag_t2_write_data(start->t2, 1, rec, test_write_data2_done,
        test_destroy_quit_loop, start->loop));
    g_bytes_unref(rec);
    return G_SOURCE_REMOVE;
}

static
void
test_write_data2_start(
    NfcTag* tag,
    void* user_data)
{
    TestWriteData2Start* start = g_new0(TestWriteData2Start, 1);

    /* For this test we want to call nfc_tag_t2_write_data when target is
     * completely idle (no sequence in progress) */
    g_assert(tag->target->sequence);
    start->t2 = NFC_TAG_T2(tag);
    start->loop = user_data;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, test_write_data2_really_start,
        start, g_free);
}

static
void
test_write_data2(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_tag_add_initialized_handler(tag,
        test_write_data2_start, loop);

    test_run(&test_opt, loop);

    /* Check the contents */
    g_assert(!memcmp(test->data.bytes + TEST_DATA_OFFSET, jolla_rec,
        NDEF_JOLLA_COM_SIZE_EXACT));

    /* It's not considered cached anymore */
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, sizeof(jolla_rec), NULL) ==
        NFC_TAG_T2_IO_STATUS_NOT_CACHED);

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * write_data3
 *==========================================================================*/

#define TEST_WRITE_DATA3_CHUNK (1)

static
void
test_write_data3_done_wipe(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_OK);
    g_assert(written == NDEF_GOOGLE_COM_SIZE_EXACT);
}

static
void
test_write_data3_done_chunk1(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_OK);
    g_assert(written == TEST_WRITE_DATA3_CHUNK);
}

static
void
test_write_data3_done_chunk2(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_OK);
    g_assert(written == (NDEF_JOLLA_COM_SIZE_EXACT - TEST_WRITE_DATA3_CHUNK));
}

static
void
test_write_data3_start(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);
    GBytes* rec = g_bytes_new_take(g_new0(guint8, NDEF_GOOGLE_COM_SIZE_EXACT),
        NDEF_GOOGLE_COM_SIZE_EXACT);

    /* Zero the NDEF */
    g_assert(nfc_tag_t2_write_data(t2, 0, rec, test_write_data3_done_wipe,
        test_destroy_quit_loop, user_data /* loop */));
    g_bytes_unref(rec);
}

static
void
test_write_data3(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    GBytes* chunk1 = g_bytes_new_static(jolla_rec, 1);
    GBytes* chunk2 = g_bytes_new_static(jolla_rec + 1,
        NDEF_JOLLA_COM_SIZE_EXACT - 1);
    guint i;
    gulong id = nfc_tag_add_initialized_handler(tag,
        test_write_data3_start, loop);

    test_run(&test_opt, loop);

    /* Contents should be wiped (test a few more bytes then we have written) */
    for (i = 0; i < NDEF_GOOGLE_COM_SIZE_ALIGNED; i++) {
        g_assert(!test->data.bytes[TEST_DATA_OFFSET + i]);
    }

    /* It's not considered cached anymore */
    g_assert(nfc_tag_t2_read_data_sync(t2, 0, NDEF_GOOGLE_COM_SIZE_EXACT,
        NULL) == NFC_TAG_T2_IO_STATUS_NOT_CACHED);

    /* Write data as two chunks */
    g_assert(nfc_tag_t2_write_data(t2, 0, chunk1, test_write_data3_done_chunk1,
        NULL, NULL));
    g_assert(nfc_tag_t2_write_data(t2, TEST_WRITE_DATA3_CHUNK, chunk2,
        test_write_data3_done_chunk2, test_destroy_quit_loop, loop));
    test_run(&test_opt, loop);

    /* Check the contents */
    g_assert(!memcmp(test->data.bytes + TEST_DATA_OFFSET, jolla_rec,
        sizeof(jolla_rec)));

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_bytes_unref(chunk1);
    g_bytes_unref(chunk2);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * write_err1
 *==========================================================================*/

static
void
test_write_err1_complete(
    NfcTagType2* tag,
    NFC_TRANSMIT_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TRANSMIT_STATUS_ERROR);
    g_assert(!written);
}

static
void
test_write_err1_start(
    NfcTag* tag,
    void* loop)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);
    GBytes* rec = g_bytes_new_take(g_new0(guint8, NDEF_GOOGLE_COM_SIZE_EXACT),
        NDEF_GOOGLE_COM_SIZE_ALIGNED);

    /* Try to zero the NDEF (and fail) */
    g_assert(nfc_tag_t2_write(t2, 0, TEST_FIRST_DATA_BLOCK, rec,
        test_write_err1_complete, test_destroy_quit_loop, loop));
    g_bytes_unref(rec);
}

static
void
test_write_err1(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_tag_add_initialized_handler(tag,
        test_write_err1_start, loop);

    /* Generate write error for the very first block we try to erase */
    memset(&error, 0, sizeof(error));
    error.block = TEST_FIRST_DATA_BLOCK;
    error.type = TEST_TARGET_ERROR_TRANSMIT;
    test->write_error = &error;

    test_run(&test_opt, loop);

    /* Make sure nothing has been written */
    g_assert(!memcmp(test->data.bytes, TEST_ARRAY_AND_SIZE(test_data_google)));

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * write_data_err1
 *==========================================================================*/

static
void
test_write_data_err1_complete(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_IO_ERROR);
    g_assert(!written);
}

static
void
test_write_data_err1_start(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);
    GBytes* rec = g_bytes_new_take(g_new0(guint8, NDEF_GOOGLE_COM_SIZE_EXACT),
        NDEF_GOOGLE_COM_SIZE_EXACT);

    /* Try to zero the NDEF (and fail) */
    g_assert(nfc_tag_t2_write_data(t2, 0, rec, test_write_data_err1_complete,
        test_destroy_quit_loop, user_data /* loop */));
    g_bytes_unref(rec);
}

static
void
test_write_data_err1(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_tag_add_initialized_handler(tag,
        test_write_data_err1_start, loop);

    /* Generate write error for the very first block we try to erase */
    memset(&error, 0, sizeof(error));
    error.block = TEST_FIRST_DATA_BLOCK;
    error.type = TEST_TARGET_ERROR_TRANSMIT;
    test->write_error = &error;

    test_run(&test_opt, loop);

    /* Make sure nothing has been written */
    g_assert(!memcmp(test->data.bytes, TEST_ARRAY_AND_SIZE(test_data_google)));

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * write_data_err2
 *==========================================================================*/

#define TEST_DATA_ERR2_BLOCK (15)

static
void
test_write_data_err2_complete(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data)
{
    g_assert(status == NFC_TAG_T2_IO_STATUS_IO_ERROR);
    g_assert(!written);
}

static
void
test_write_data_err2_start(
    NfcTag* tag,
    void* user_data)
{
    NfcTagType2* t2 = NFC_TAG_T2(tag);
    guint8 ff = 0xff;
    GBytes* rec = g_bytes_new(&ff, 1);

    /* Try to a byte (and fail to fetch the current contents) */
    g_assert(nfc_tag_t2_write_data(t2, 1 + TEST_DATA_ERR2_BLOCK *
        TEST_TARGET_BLOCK_SIZE, rec, test_write_data_err2_complete,
        test_destroy_quit_loop, user_data /* loop */));
    g_bytes_unref(rec);
}

static
void
test_write_data_err2(
    void)
{
    TestTarget* test = test_target_new(TEST_ARRAY_AND_SIZE(test_data_google));
    NfcTagType2* t2 = test_tag_new(test, 0);
    NfcTag* tag = &t2->tag;
    TestTargetError error;
    GMainLoop* loop = g_main_loop_new(NULL, TRUE);
    gulong id = nfc_tag_add_initialized_handler(tag,
        test_write_data_err2_start, loop);

    /* Generate read error for the very first block we try to erase */
    memset(&error, 0, sizeof(error));
    error.block = TEST_DATA_ERR2_BLOCK + TEST_FIRST_DATA_BLOCK;
    error.type = TEST_TARGET_ERROR_TRANSMIT;
    test->read_error = &error;

    test_run(&test_opt, loop);

    /* Make sure nothing has been written */
    g_assert(!memcmp(test->data.bytes, TEST_ARRAY_AND_SIZE(test_data_google)));

    nfc_tag_remove_handler(tag, id);
    nfc_tag_unref(tag);
    nfc_target_unref(&test->target);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/tag_t2/" name

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_data_func(TEST_("unsup1"), &test_unsup1_bytes, test_unsup);
    g_test_add_data_func(TEST_("unsup2"), &test_unsup2_bytes, test_unsup);
    g_test_add_func(TEST_("init_err1"), test_init_err1);
    g_test_add_func(TEST_("init_err2"), test_init_err2);
    g_test_add_func(TEST_("read_data"), test_read_data);
    g_test_add_func(TEST_("read_data_872"), test_read_data_872);
    g_test_add_func(TEST_("read_data_cached"), test_read_data_cached);
    g_test_add_func(TEST_("read_data_abort"), test_read_data_abort);
    g_test_add_func(TEST_("read_data_err"), test_read_data_err);
    g_test_add_func(TEST_("read_crc_err"), test_read_crc_err);
    g_test_add_func(TEST_("read_nack"), test_read_nack);
    g_test_add_func(TEST_("read_timeout"), test_read_timeout);
    g_test_add_func(TEST_("write"), test_write);
    g_test_add_func(TEST_("write_data1"), test_write_data1);
    g_test_add_func(TEST_("write_data2"), test_write_data2);
    g_test_add_func(TEST_("write_data3"), test_write_data3);
    g_test_add_func(TEST_("write_err1"), test_write_err1);
    g_test_add_func(TEST_("write_data_err1"), test_write_data_err1);
    g_test_add_func(TEST_("write_data_err2"), test_write_data_err2);
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
