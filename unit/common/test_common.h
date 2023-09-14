/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2022 Jolla Ltd.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <gutil_types.h>

/*
 * For whatever reason, g_assert() is a special case and can be disabled
 * with G_DISABLE_ASSERT macro, unlike all other g_assert_* macros. Make
 * sure that it actually works.
 */
#ifdef G_DISABLE_ASSERT
#  error "g_assert is required by unit tests"
#endif

#define TEST_FLAG_DEBUG (0x01)
typedef struct test_opt {
    int flags;
} TestOpt;

typedef struct test_tx {
    GUtilData in;
    GUtilData out;
} TestTx;

/* Should be invoked after g_test_init */
void
test_init(
    TestOpt* opt,
    int argc,
    char* argv[]);

/* Run loop with a timeout */
gboolean
test_timeout_expired(
    gpointer data);

void
test_run(
    const TestOpt* opt,
    GMainLoop* loop);

/* Quits the event loop on the next iteration (or after n iterations) */
void
test_quit_later(
    GMainLoop* loop);

void
test_quit_later_n(
    GMainLoop* loop,
    guint n);

#define TEST_TIMEOUT_SEC (20)
#define TEST_TIMEOUT_MS (TEST_TIMEOUT_SEC * 1000)

/* Utilities */
GUtilData*
test_alloc_data(
    const void* bytes,
    guint len);

GUtilData*
test_clone_data(
    const GUtilData* data);

int
test_rmdir(
    const char* path);

/* Helper macros */

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#  define TEST_INT16_BYTES(v) \
    (guint8)(v), (guint8)((v) >> 8)
#  define TEST_INT32_BYTES(v) \
    (guint8)(v), (guint8)((v) >> 8), \
    (guint8)((v) >> 16), (guint8)((v) >> 24)
#  define TEST_INT64_BYTES(v) \
    (guint8)(v), (guint8)((v) >> 8), \
    (guint8)((v) >> 16), (guint8)((v) >> 24), \
    (guint8)(((guint64)(v)) >> 32), (guint8)(((guint64)(v)) >> 40), \
    (guint8)(((guint64)(v)) >> 48), (guint8)(((guint64)(v)) >> 56)
#elif G_BYTE_ORDER == G_BIG_ENDIAN
#  define TEST_INT16_BYTES(v) \
    (guint8)((v) >> 8), (guint8)(v)
#  define TEST_INT32_BYTES(v) \
    (guint8)((v) >> 24), (guint8)((v) >> 16), \
    (guint8)((v) >> 8), (guint8)(v)
#  define TEST_INT64_BYTES(v) \
    (guint8)(((guint64)(v)) >> 56), (guint8)(((guint64)(v)) >> 48), \
    (guint8)(((guint64)(v)) >> 40), (guint8)(((guint64)(v)) >> 32), \
    (guint8)((v) >> 24), (guint8)((v) >> 16), \
    (guint8)((v) >> 8), (guint8)(v)
#else /* !G_LITTLE_ENDIAN && !G_BIG_ENDIAN */
#error unknown ENDIAN type
#endif /* !G_LITTLE_ENDIAN && !G_BIG_ENDIAN */

#define TEST_ARRAY_AND_COUNT(a) a, G_N_ELEMENTS(a)
#define TEST_ARRAY_AND_SIZE(a) a, sizeof(a)
#define TEST_BYTES_SET(b,d) ((b).bytes = (void*)(d), (b).size = sizeof(d))

#endif /* TEST_COMMON_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
