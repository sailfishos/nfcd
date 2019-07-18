/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_util.h"
#include "nfc_system.h"
#include "nfc_log.h"

#include <gutil_misc.h>
#include <gutil_macros.h>

/* sub-module, to turn prefix off */
GLogModule _nfc_dump_log = {
    .name = "nfc.dump",
    .parent = &GLOG_MODULE_NAME,
    .max_level = GLOG_LEVEL_MAX,
    .level = GLOG_LEVEL_INHERIT,
    .flags = GLOG_FLAG_HIDE_NAME
};

void
nfc_hexdump(
    const void* data,
    int len)
{
    const int level = GLOG_LEVEL_VERBOSE;
    GLogModule* log = &_nfc_dump_log;

    if (gutil_log_enabled(log, level)) {
        const guint8* ptr = data;
        guint off = 0;

        while (len > 0) {
            char buf[GUTIL_HEXDUMP_BUFSIZE];
            const guint consumed = gutil_hexdump(buf, ptr + off, len);

            gutil_log(log, level, "  %04X: %s", off, buf);
            len -= consumed;
            off += consumed;
        }
    }
}

void
nfc_hexdump_data(
    const GUtilData* data)
{
    if (G_LIKELY(data)) {
        nfc_hexdump(data->bytes, data->size);
    }
}

NfcLanguage*
nfc_system_language(
    void)
{
    const char* locale = nfc_system_locale();

    /* Ignore special "C" and "POSIX" values */
    if (locale && strcmp(locale, "C") && strcmp(locale, "POSIX")) {
        /* language[_territory][.codeset][@modifier] */
        NfcLanguage* result;
        const char* codeset = strchr(locale, '.');
        const char* modifier = strchr(locale, '@');
        const char* lang = locale;
        const char* terr;
        const char* sep;
        char* ptr;
        gsize len, lang_len, terr_len, total;

        /* Cut off codeset and/or modifier */
        if (!codeset && !modifier) {
            len = strlen(locale);
        } else if (!codeset) {
            len = modifier - locale;
        } else if (!modifier) {
            len = codeset - locale;
        } else {
            len = MIN(codeset, modifier) - locale;
        }

        /* Split language from territory and calculate total size */
        total = sizeof(NfcLanguage);
        sep = memchr(locale, '_', len);
        if (sep) {
            lang_len = sep - locale;
            terr_len = len - lang_len - 1;
            terr = sep + 1;
            total += G_ALIGN8(lang_len + 1) + terr_len + 1;
        } else {
            lang_len = len;
            terr_len = 0;
            terr = NULL;
            total += len + 1;
        }

        /*
         * Copy parsed data to single memory block so that the whole thing
         * can be deallocated with a single g_free() call.
         */
        result = g_malloc0(total);
        ptr = (char*)(result + 1);
        memcpy(ptr, lang, lang_len);
        result->language = ptr;
        if (terr) {
            ptr += G_ALIGN8(lang_len + 1);
            result->territory = ptr;
            memcpy(ptr, terr, terr_len);
        }
        return result;
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
