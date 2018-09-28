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

#include "nfc_util.h"
#include "nfc_log.h"

static TestOpt test_opt;

static GString* test_log_buf;

static
void
test_log_proc(
    const char* name,
    int level,
    const char* format,
    va_list va)
{
    g_string_append_vprintf(test_log_buf, format, va);
    g_string_append(test_log_buf, "\n");
}

/*==========================================================================*
 * hexdump
 *==========================================================================*/

static
void
test_hexdump(
    void)
{
    static const guint8 data[] = {
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66
    };
    const char data_hexdump[] = "  0000: 30 31 32 33 34 35 36 37  "
        "38 39 61 62 63 64 65 66    01234567 89abcdef\n";
    const GLogProc fn = gutil_log_func;
    const int level = NFC_CORE_LOG_MODULE.level;
    GUtilData buf;

    test_log_buf = g_string_new(NULL);
    gutil_log_func = test_log_proc;

    /* No dump at this level */
    NFC_CORE_LOG_MODULE.level = GLOG_LEVEL_DEBUG;
    nfc_hexdump(data, sizeof(data));
    g_assert(!test_log_buf->len);

    /* Only at VERBOSE */
    NFC_CORE_LOG_MODULE.level = GLOG_LEVEL_VERBOSE;
    nfc_hexdump(data, sizeof(data));
    g_assert(!strcmp(test_log_buf->str, data_hexdump));

    /* Same thing with GUtilData */
    g_string_set_size(test_log_buf, 0);
    TEST_BYTES_SET(buf, data);
    nfc_hexdump_data(NULL); /* This one does nothing */
    nfc_hexdump_data(&buf);
    g_assert(!strcmp(test_log_buf->str, data_hexdump));

    NFC_CORE_LOG_MODULE.level = level;
    g_string_free(test_log_buf, TRUE);
    gutil_log_func = fn;
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/core/util/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("hexdump"), test_hexdump);
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
