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

#ifndef NFC_TAG_T2_H
#define NFC_TAG_T2_H

#include "nfc_tag.h"

/* Type 2 tag */

G_BEGIN_DECLS

typedef struct nfc_tag_t2_priv NfcTagType2Priv;

typedef enum nfc_tag_t2_flags {
    NFC_TAG_T2_FLAGS_NONE = 0x00,
    NFC_TAG_T2_FLAG_NFC_FORUM_COMPATIBLE = 0x01
} NFC_TAG_T2_FLAGS;

struct nfc_tag_t2 {
    NfcTag tag;
    NfcTagType2Priv* priv;
    guint8 sel_res;  /* (SAK)*/
    GUtilData nfcid1;
    NFC_TAG_T2_FLAGS t2flags;
    guint block_size;   /* Valid only when initialized */
    guint data_size;    /* Valid only when initialized */
    GUtilData serial;   /* Valid only when initialized */
};

GType nfc_tag_t2_get_type();
#define NFC_TYPE_TAG_T2 (nfc_tag_t2_get_type())
#define NFC_TAG_T2(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_TAG_T2, NfcTagType2))
#define NFC_IS_TAG_T2(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, \
        NFC_TYPE_TAG_T2)

#define NFC_TAG_T2_DATA_BLOCK0  (4) /* Index of the first data block */

typedef
void
(*NfcTagType2ReadFunc)(
    NfcTagType2* tag,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data);

typedef
void
(*NfcTagType2WriteFunc)(
    NfcTagType2* tag,
    NFC_TRANSMIT_STATUS status,
    guint written,
    void* user_data);

guint
nfc_tag_t2_read(
    NfcTagType2* tag,
    guint sector,
    guint block,
    NfcTagType2ReadFunc resp,
    GDestroyNotify destroy,
    void* user_data);

guint
nfc_tag_t2_write(
    NfcTagType2* tag,
    guint sector,
    guint block,
    GBytes* bytes,
    NfcTagType2WriteFunc complete,
    GDestroyNotify destroy,
    void* user_data);

guint
nfc_tag_t2_write_seq(
    NfcTagType2* tag,
    guint sector,
    guint block,
    GBytes* bytes,
    NfcTargetSequence* seq,
    NfcTagType2WriteFunc complete,
    GDestroyNotify destroy,
    void* user_data); /* Since 1.0.17 */

/*
 * The methods belows read only the data part of the chip's memory,
 * excluding sector headers, trailers or other reserved areas. Blocks
 * are numbered sequentially, starting with the first data block of
 * the first sector, crossing sector boundary if necessary.
 */

typedef enum nfc_tag_t2_io_status {
    NFC_TAG_T2_IO_STATUS_OK,          /* Data received */
    NFC_TAG_T2_IO_STATUS_FAILURE,     /* Unspecified failure */
    NFC_TAG_T2_IO_STATUS_IO_ERROR,    /* Transmission error of CRC mismatch */
    NFC_TAG_T2_IO_STATUS_BAD_BLOCK,   /* Invalid start block */
    NFC_TAG_T2_IO_STATUS_BAD_SIZE,    /* Too much data requested */
    NFC_TAG_T2_IO_STATUS_NOT_CACHED   /* Requested region is not cached */
} NFC_TAG_T2_IO_STATUS;

typedef
void
(*NfcTagType2ReadDataFunc)(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    const void* data,
    guint len,
    void* user_data);

typedef
void
(*NfcTagType2WriteDataFunc)(
    NfcTagType2* tag,
    NFC_TAG_T2_IO_STATUS status,
    guint written,
    void* user_data);

guint
nfc_tag_t2_read_data(
    NfcTagType2* tag,
    guint offset,
    guint maxbytes,
    NfcTagType2ReadDataFunc resp,
    GDestroyNotify destroy,
    void* user_data);

guint
nfc_tag_t2_read_data_seq(
    NfcTagType2* tag,
    guint offset,
    guint maxbytes,
    NfcTargetSequence* seq,
    NfcTagType2ReadDataFunc resp,
    GDestroyNotify destroy,
    void* user_data); /* Since 1.0.17 */

NFC_TAG_T2_IO_STATUS
nfc_tag_t2_read_data_sync(
    NfcTagType2* tag,
    guint offset,
    guint nbytes,
    void* buffer);

guint
nfc_tag_t2_write_data(
    NfcTagType2* tag,
    guint offset,
    GBytes* bytes,
    NfcTagType2WriteDataFunc complete,
    GDestroyNotify destroy,
    void* user_data);

guint
nfc_tag_t2_write_data_seq(
    NfcTagType2* tag,
    guint offset,
    GBytes* bytes,
    NfcTargetSequence* seq,
    NfcTagType2WriteDataFunc complete,
    GDestroyNotify destroy,
    void* user_data); /* Since 1.0.17 */

G_END_DECLS

#endif /* NFC_TAG_T2_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
