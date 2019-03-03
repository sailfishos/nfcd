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

#include "nfc_tag_t2.h"
#include "nfc_tag_p.h"
#include "nfc_target_p.h"
#include "nfc_ndef.h"
#include "nfc_util.h"
#include "nfc_tlv.h"
#include "nfc_log.h"

#include <gutil_misc.h>

/* Block size got Type 2 Tags is always 4 bytes according to
 * NFCForum-TS-Type-2-Tag_1.1 spec */
#define NFC_TAG_T2_BLOCK_SIZE   (4)
#define NFC_TAG_T2_MAX_BLOCK_SIZE NFC_TAG_T2_BLOCK_SIZE

#define NFC_TAG_T2_CC_NFC_FORUM_MAGIC (0xe1)
#define NFC_TAG_T2_CC_MIN_VERSION (0x10)

#define NXP_MANUFACTURER_ID (0x04)

/*
 * Command set.
 *
 * NFCForum-TS-DigitalProtocol-1.0
 * Section 9 "Type 2 Tag Platform"
 */
#define NFC_TAG_T2_CMD_READ (0x30)
#define NFC_TAG_T2_CMD_WRITE (0xa2)

typedef struct nfc_tag_t2_cmd_data {
    NfcTagType2* t2;
    NfcTagType2ReadFunc resp;
    GDestroyNotify destroy;
    void* user_data;
} NfcTagType2Cmd;

typedef struct nfc_tag_t2_read_data {
    NfcTagType2* t2;
    guint8* buffer;
    guint size;
    guint read;
    guint offset;
    guint complete_id;
    guint cmd_id;
    guint seq_id;
    NfcTargetSequence* seq;
    NfcTagType2ReadDataFunc complete;
    GDestroyNotify destroy;
    void* user_data;
} NfcTagType2ReadData;

typedef struct nfc_tag_t2_write_data {
    NfcTagType2* t2;
    GBytes* bytes;
    guint sector_number;
    guint offset;
    guint written;
    guint cmd_id;
    guint seq_id;
    gulong start_id;
    NfcTargetSequence* seq;
    union nfc_tag_t2_write_data_complete {
        GCallback cb;
        NfcTagType2WriteFunc write_cb;
        NfcTagType2WriteDataFunc write_data_cb;
    } complete;
    GDestroyNotify destroy;
    void* user_data;
} NfcTagType2WriteData;

typedef struct nfc_tag_t2_sector {
    guint size;             /* Number of bytes in the sector */
    guint8* bytes;          /* Sector's contents (not necessarily valid) */
    guint8* valid;          /* One bit per block, 1 = cached, 0 = dirty */
    GUtilData data;         /* Data portion of the sector */
} NfcTagType2Sector;

struct nfc_tag_t2_priv {
    NfcTargetSequence* init_seq;
    GHashTable* reads;
    GHashTable* writes;
    GByteArray* cached_blocks;
    guint8 serial[4];
    void* nfcid1;
    guint sector_count;
    NfcTagType2Sector* sectors;
    guint init_id;
};

typedef struct nfc_tag_t2_class {
    NfcTagClass parent;
} NfcTagType2Class;

G_DEFINE_TYPE(NfcTagType2, nfc_tag_t2, NFC_TYPE_TAG)

static
NfcTagType2Sector*
nfc_tag_t2_data_block_to_sector(
    NfcTagType2* self,
    guint block,
    guint* bno, /* block number relative to the start of sector */
    gboolean* cached)
{
    guint i;
    guint sector_start = 0;
    const guint block_size = self->block_size;
    NfcTagType2Priv* priv = self->priv;

    for (i = 0; i < priv->sector_count; i++) {
        NfcTagType2Sector* sector = priv->sectors + i;
        const GUtilData* data = &sector->data;
        const guint data_blocks = data->size / block_size;

        if (block < (sector_start + data_blocks)) {
            const guint header = data->bytes - sector->bytes;
            const guint rel_block = block - sector_start + header / block_size;

            if (bno) {
                *bno = rel_block;
            }
            if (cached) {
                const guint8 bit = (1 << (rel_block % 8));

                *cached = ((sector->valid[rel_block / 8] & bit) != 0);
            }
            return sector;
        }
        sector_start += data_blocks;
    }
    return NULL;
}

static
void
nfc_tag_t2_sector_init(
    NfcTagType2Sector* sector,
    guint block_size,
    guint header_blocks,
    guint data_blocks,
    guint trailer_blocks)
{
    const guint total_blocks = header_blocks + data_blocks + trailer_blocks;

    sector->size = total_blocks * block_size;
    sector->bytes = g_malloc0(sector->size);
    sector->valid = g_malloc0((total_blocks + 7) / 8);
    sector->data.bytes = sector->bytes + (header_blocks * block_size);
    sector->data.size = data_blocks * block_size;
}

static
void
nfc_tag_t2_sector_deinit(
    NfcTagType2Sector* sector)
{
    g_free(sector->bytes);
    g_free(sector->valid);
}

static
void
nfc_tag_t2_sector_set_data(
    NfcTagType2Sector* sector,
    guint block_size,
    const guint8* bytes,
    guint block,
    guint num_blocks)
{
    const guint total_blocks = sector->size / block_size;

    if (block < total_blocks) {
        guint i;

        if ((block + num_blocks) > total_blocks) {
            num_blocks = total_blocks - block;
        }

        memcpy(sector->bytes + block * block_size, bytes,
            num_blocks * block_size);

        /* Mark blocks as valid */
        for (i = 0; i < num_blocks; i++) {
            sector->valid[(block + i) / 8] |= (1 << ((block + i) % 8));
        }
    }
}

static
void
nfc_tag_t2_sector_invalidate(
    NfcTagType2Sector* sector,
    guint block_size,
    guint block,
    guint num_blocks)
{
#pragma message("TODO: Invalidate and re-read NDEF")
    const guint total_blocks = sector->size / block_size;

    if (block < total_blocks) {
        guint i;

        if ((block + num_blocks) > total_blocks) {
            num_blocks = total_blocks - block;
        }

        memset(sector->bytes + block, 0, num_blocks * block_size);

        /* Mark blocks as invalid */
        for (i = 0; i < num_blocks; i++) {
            sector->valid[(block + i) / 8] &= ~(1 << ((block + i) % 8));
        }
    }
}

static
guint
nfc_tag_t2_generate_id(
    NfcTagType2* self)
{
    guint id;
    NfcTagType2Priv* priv = self->priv;
    gpointer key;

    do {
        /* It's highly unlikely that we have to repeat this more than once */
        id = nfc_target_generate_id(self->tag.target);
        key = GUINT_TO_POINTER(id);
    } while ((priv->writes && g_hash_table_contains(priv->writes, key)) ||
             (priv->reads && g_hash_table_contains(priv->reads, key)));
    return id;
}

/*==========================================================================*
 * Commands
 *==========================================================================*/

static
void
nfc_tag_t2_cmd_destroy(
    void* data)
{
    g_slice_free(NfcTagType2Cmd, data);
}

static
void
nfc_tag_t2_cmd_resp(
    NfcTarget* target,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType2Cmd* cmd = user_data;

    cmd->resp(cmd->t2, status, data, len, cmd->user_data);
}

static
guint
nfc_tag_t2_cmd(
    NfcTagType2* self,
    const void* cmd,
    guint size,
    NfcTargetSequence* seq,
    NfcTagType2ReadFunc resp,
    GDestroyNotify destroy,
    void* user_data)
{
    NfcTag* tag = &self->tag;
    NfcTagType2Cmd* data = g_slice_new(NfcTagType2Cmd);
    guint id;

    data->t2 = self;
    data->resp = resp;
    data->destroy = destroy;
    data->user_data = user_data;

    id = nfc_target_transmit(tag->target, cmd, size, seq,
        resp ? nfc_tag_t2_cmd_resp : NULL, nfc_tag_t2_cmd_destroy, data);
    if (id) {
        return id;
    } else {
        g_slice_free(NfcTagType2Cmd, data);
        return 0;
    }
}

static
guint
nfc_tag_t2_cmd_read(
    NfcTagType2* self,
    guint block,
    NfcTargetSequence* seq,
    NfcTagType2ReadFunc resp,
    GDestroyNotify done,
    void* user_data)
{
    if (block <= 0xff) {
        guint8 cmd[2];
        /*
         * NFCForum-TS-DigitalProtocol-1.0
         * Section 9 "Type 2 Tag Platform"
         * 9.6 READ
         */
        cmd[0] = NFC_TAG_T2_CMD_READ;
        cmd[1] = block;
        return nfc_tag_t2_cmd(self, cmd, 2, seq, resp, done, user_data);
    }
    return 0;
}

static
guint
nfc_tag_t2_cmd_write(
    NfcTagType2* self,
    guint block,
    const guint8* data,
    NfcTargetSequence* seq,
    NfcTagType2ReadFunc resp,
    GDestroyNotify done,
    void* user_data)
{
    if (block <= 0xff) {
        guint8 cmd[2 + NFC_TAG_T2_MAX_BLOCK_SIZE];

        /*
         * NFCForum-TS-DigitalProtocol-1.0
         * Section 9 "Type 2 Tag Platform"
         * 9.7 WRITE
         */
        cmd[0] = NFC_TAG_T2_CMD_WRITE;
        cmd[1] = block;
        memcpy(cmd + 2, data, self->block_size);
        return nfc_tag_t2_cmd(self, cmd, self->block_size + 2, seq,
            resp, done, user_data);
    }
    return 0;
}

/*==========================================================================*
 * Read
 *==========================================================================*/

static
void
nfc_tag_t2_read_data_free(
    gpointer user_data)
{
    NfcTagType2ReadData* read = user_data;

    nfc_target_sequence_free(read->seq);
    nfc_target_cancel_transmit(read->t2->tag.target, read->cmd_id);
    if (read->destroy) {
        read->destroy(read->user_data);
    }
    if (read->complete_id) {
        g_source_remove(read->complete_id);
    }
    g_free(read->buffer);
    g_slice_free(NfcTagType2ReadData, read);
}

static
gboolean
nfc_tag_t2_read_complete(
    gpointer user_data)
{
    NfcTagType2ReadData* read = user_data;
    NfcTagType2* t2 = read->t2;
    NfcTagType2Priv* priv = t2->priv;
    NfcTag* tag = &t2->tag;
    const guint seq_id = read->seq_id;

    read->complete_id = 0;
    nfc_tag_ref(tag);
    if (read->complete) {
        read->complete(t2, NFC_TAG_T2_IO_STATUS_OK, read->buffer, read->read,
            read->user_data);
    }

    g_hash_table_remove(priv->reads, GUINT_TO_POINTER(seq_id));
    nfc_tag_unref(tag);
    return G_SOURCE_REMOVE;
}

static
void
nfc_tag_t2_read_resp(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType2ReadData* read = user_data;
    NfcTagType2Priv* priv = t2->priv;
    NfcTag* tag = &t2->tag;
    const guint seq_id = read->seq_id;

    read->cmd_id = 0;
    nfc_tag_ref(tag);
    if (status == NFC_TRANSMIT_STATUS_OK && len > 0) {
        const guint block_size = t2->block_size;
        const guint8* bytes = data;
        guint rel_block;
        NfcTagType2Sector* sector = nfc_tag_t2_data_block_to_sector(t2,
            (read->offset + read->read) / block_size, &rel_block, NULL);

        if (sector) {
            const guint nb = len / block_size;
            const guint offset = (read->offset + read->read) % block_size;

            nfc_tag_t2_sector_set_data(sector, block_size, bytes,
                rel_block, nb);
            if (len > (read->size - read->read)) {
                len = read->size - read->read;
            }
            memcpy(read->buffer + read->read, bytes + offset, len);
            read->read += len;
            if (read->read < read->size) {
                /* Submit the next read */
                read->cmd_id = nfc_tag_t2_cmd_read(t2, rel_block + nb,
                    read->seq, nfc_tag_t2_read_resp, NULL, read);
            }
        }
    } else {
        GDEBUG("Oops, read failed!");
    }

    if (!read->cmd_id) {
        if (read->complete) {
            read->complete(t2, (status == NFC_TRANSMIT_STATUS_OK) ?
                NFC_TAG_T2_IO_STATUS_OK : NFC_TAG_T2_IO_STATUS_IO_ERROR,
                read->buffer, read->read, read->user_data);
        }
        g_hash_table_remove(priv->reads, GUINT_TO_POINTER(seq_id));
    }
    nfc_tag_unref(tag);
}

/*==========================================================================*
 * Write
 *==========================================================================*/

static
void
nfc_tag_t2_write_data_resp(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    const void* resp,
    guint len,
    void* user_data);

static
void
nfc_tag_t2_write_data_free(
    gpointer data)
{
    NfcTagType2WriteData* write = data;
    NfcTarget* target = write->t2->tag.target;

    nfc_target_remove_handler(target, write->start_id);
    nfc_target_sequence_free(write->seq);
    nfc_target_cancel_transmit(target, write->cmd_id);
    if (write->destroy) {
        write->destroy(write->user_data);
    }
    g_bytes_unref(write->bytes);
    g_slice_free(NfcTagType2WriteData, write);
}

static
NfcTagType2WriteData*
nfc_tag_t2_write_data_new(
    NfcTagType2* self,
    guint sector_number,
    guint offset,
    GBytes* bytes,
    GCallback complete,
    GDestroyNotify destroy,
    void* user_data)
{
    NfcTagType2Priv* priv = self->priv;
    NfcTagType2WriteData* write = g_slice_new0(NfcTagType2WriteData);

    write->t2 = self;
    write->bytes = g_bytes_ref(bytes);
    write->offset = offset;
    write->sector_number = sector_number;
    write->seq = nfc_target_sequence_new(self->tag.target);
    write->seq_id = nfc_tag_t2_generate_id(self);
    write->complete.cb = complete;
    write->destroy = destroy;
    write->user_data = user_data;

    if (!priv->writes) {
        priv->writes = g_hash_table_new_full(g_direct_hash, g_direct_equal,
            NULL, nfc_tag_t2_write_data_free);
    }
    g_hash_table_insert(priv->writes, GUINT_TO_POINTER(write->seq_id), write);
    return write;
}

static
void
nfc_tag_t2_write_data_error(
    NfcTagType2WriteData* write)
{
    NfcTagType2* t2 = write->t2;
    NfcTagType2Priv* priv = t2->priv;
    NfcTagType2WriteDataFunc complete = write->complete.write_data_cb;

    GDEBUG("Wrote %u bytes out of %u", write->written, (guint)
        g_bytes_get_size(write->bytes));
    if (complete) {
        write->complete.write_cb = NULL;
        complete(write->t2, NFC_TAG_T2_IO_STATUS_IO_ERROR, write->written,
            write->user_data);
    }
    g_hash_table_remove(priv->writes, GUINT_TO_POINTER(write->seq_id));
}

static
void
nfc_tag_t2_write_resp(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    const void* resp,
    guint len,
    void* user_data)
{
    NfcTag* tag = &t2->tag;
    NfcTagType2WriteData* write = user_data;
    NfcTagType2Priv* priv = t2->priv;
    gsize total_size = 0;
    const guint block_size = t2->block_size;
    const guint8* data = g_bytes_get_data(write->bytes, &total_size);

    write->cmd_id = 0;

    /* Round total size down to the nearest block boundary.
     * Offset is rounded too (see nfc_tag_t2_write) */
    total_size -= total_size % block_size;

    nfc_tag_ref(tag);
    if (status != NFC_TRANSMIT_STATUS_OK) {
        NfcTagType2WriteFunc complete = write->complete.write_cb;

        GDEBUG("Oops, write failed!");
        if (complete) {
            write->complete.write_cb = NULL;
            complete(write->t2, status, write->written, write->user_data);
        }
        g_hash_table_remove(priv->writes, GUINT_TO_POINTER(write->seq_id));
    } else if ((write->written + block_size) >= total_size) {
        /* Last write succeeded */
        guint written = write->written;
        NfcTagType2WriteFunc complete = write->complete.write_cb;

        written += block_size;
        GDEBUG("Wrote %u byte(s)", written);
        GASSERT(written == total_size);
        if (complete) {
            write->complete.write_cb = NULL;
            complete(write->t2, status, written, write->user_data);
        }
        g_hash_table_remove(priv->writes, GUINT_TO_POINTER(write->seq_id));
    } else {
        /* Write the next block */
        NfcTagType2Sector* sector = priv->sectors + write->sector_number;
        guint next_block;

        write->written += block_size;
        next_block = (write->offset + write->written)/block_size;
        nfc_tag_t2_sector_invalidate(sector, block_size, next_block, 1);
        write->cmd_id = nfc_tag_t2_cmd_write(t2, next_block,
            data + write->written, write->seq, nfc_tag_t2_write_resp,
            NULL, write);
    }
    nfc_tag_unref(tag);
}

static
void
nfc_tag_t2_write_data_fetch_resp(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType2WriteData* write = user_data;
    NfcTag* tag = &t2->tag;

    write->cmd_id = 0;
    nfc_tag_ref(tag);

    if (status == NFC_TRANSMIT_STATUS_OK && len > 0) {
        const guint block_size = t2->block_size;
        const guint8* bytes = data;
        const guint nb = len / block_size;
        guint8 b[NFC_TAG_T2_MAX_BLOCK_SIZE];
        guint next_block;
        gsize write_size;
        const guint8* write_data = g_bytes_get_data(write->bytes, &write_size);
        NfcTagType2Sector* sector = nfc_tag_t2_data_block_to_sector(t2,
            (write->offset + write->written) / block_size, &next_block, NULL);
        /* Sector must be there, the initiator of fetch has checked that. */

        nfc_tag_t2_sector_set_data(sector, block_size, bytes, next_block, nb);

        /* Mix the contents */
        if (!write->written) {
            const guint block_offset = write->offset % block_size;

            /* First block (and possibly the last one) */
            GASSERT(block_offset);
            GASSERT(!write->written);
            memcpy(b, sector->data.bytes + (write->offset - block_offset),
                block_size);
            memcpy(b + block_offset, write_data, MIN(block_size - block_offset,
                write_size));
            write->cmd_id = nfc_tag_t2_cmd_write(t2, next_block, b,
                write->seq, nfc_tag_t2_write_data_resp, NULL, write);
        } else {
            const guint remaining = write_size - write->written;

            /* Last block */
            GASSERT(remaining < block_size);
            memcpy(b, write_data + write->written, remaining);
            memcpy(b + remaining, sector->bytes + (next_block * block_size +
                remaining), block_size - remaining);
            write->cmd_id = nfc_tag_t2_cmd_write(t2, next_block, b,
                write->seq, nfc_tag_t2_write_data_resp, NULL, write);
        }
    } else {
        GDEBUG("Oops, fetch failed!");
        nfc_tag_t2_write_data_error(write);
    }
    nfc_tag_unref(tag);
}

static
void
nfc_tag_t2_write_data_resp(
    NfcTagType2* t2,
    NFC_TRANSMIT_STATUS status,
    const void* resp,
    guint len,
    void* user_data)
{
    NfcTagType2WriteData* write = user_data;
    NfcTagType2Priv* priv = t2->priv;
    NfcTag* tag = &t2->tag;
    gsize total_size = 0;
    const guint8* data = g_bytes_get_data(write->bytes, &total_size);
    const guint block_size = t2->block_size;
    const guint written = block_size - /* Adjust for first unaligned block */
        (write->written + write->offset) % block_size;

    write->cmd_id = 0;
    nfc_tag_ref(tag);
    if (status != NFC_TRANSMIT_STATUS_OK) {
        GDEBUG("Oops, write failed!");
        nfc_tag_t2_write_data_error(write);
    } else if ((write->written + written) >= total_size) {
        /* Last write succeeded */
        guint total = write->written + written;
        NfcTagType2WriteDataFunc complete = write->complete.write_data_cb;

        if (total > total_size) {
            /* Don't report more bytes written than requested (even
             * though we could have actually written more due to
             * unaligned offset and/or size) */
            total = total_size;
        }

        GDEBUG("Wrote %u byte(s)", total);
        if (complete) {
            write->complete.write_cb = NULL;
            complete(write->t2, NFC_TAG_T2_IO_STATUS_OK, total,
                write->user_data);
        }
        g_hash_table_remove(priv->writes, GUINT_TO_POINTER(write->seq_id));
    } else {
        /* Write the next block */
        guint remaining;
        guint next_block;
        gboolean next_block_cached;
        NfcTagType2Sector* sector;

        write->written += written;
        remaining = total_size - write->written;
        sector = nfc_tag_t2_data_block_to_sector(t2, (write->offset +
            write->written) / block_size, &next_block, &next_block_cached);

        nfc_tag_t2_sector_invalidate(sector, block_size, next_block, 1);
        if (remaining < block_size) {
            if (next_block_cached) {
                guint8 b[NFC_TAG_T2_MAX_BLOCK_SIZE];

                /* Mix the contents */
                memcpy(b, data + write->written, remaining);
                memcpy(b + remaining, sector->bytes +
                    (next_block * block_size + remaining),
                    block_size - remaining);
                write->cmd_id = nfc_tag_t2_cmd_write(t2, next_block, b,
                    write->seq, nfc_tag_t2_write_data_resp, NULL, write);
            } else {
                /* Have to fetch it first */
                write->cmd_id = nfc_tag_t2_cmd_read(t2, next_block,
                   write->seq, nfc_tag_t2_write_data_fetch_resp, NULL, write);
            }
        } else{
            write->cmd_id = nfc_tag_t2_cmd_write(t2, next_block,
                data + write->written, write->seq, nfc_tag_t2_write_data_resp,
                NULL, write);
        }
    }
    nfc_tag_unref(tag);
}

static
void
nfc_tag_t2_write_data_unaligned_start(
    NfcTagType2WriteData* write)
{
    NfcTagType2* t2 = write->t2;
    const guint block_size = t2->block_size;
    const guint block_offset = write->offset % block_size;
    gsize write_size;
    const guint8* write_data = g_bytes_get_data(write->bytes, &write_size);
    guint start_block;
    gboolean start_block_cached;
    NfcTagType2Sector* sector = nfc_tag_t2_data_block_to_sector(t2,
        write->offset / block_size, &start_block, &start_block_cached);

    GASSERT(block_offset);
    GASSERT(!write->cmd_id);

    /* For unaligned writes we have to first read the block that
     * we are going to (partially) overwrite. */
    nfc_tag_t2_sector_invalidate(sector, block_size, start_block, 1);
    if (start_block_cached) {
        guint8 b[NFC_TAG_T2_MAX_BLOCK_SIZE];

        /* Mix the contents */
        memcpy(b, sector->data.bytes + (write->offset - block_offset),
            block_size);
        memcpy(b + block_offset, write_data, MIN(block_size - block_offset,
            write_size));
        write->cmd_id = nfc_tag_t2_cmd_write(t2, start_block, b, write->seq,
            nfc_tag_t2_write_data_resp, NULL, write);
    } else {
        /* Have to fetch it first */
        write->cmd_id = nfc_tag_t2_cmd_read(t2, start_block, write->seq,
            nfc_tag_t2_write_data_fetch_resp, NULL, write);
    }
}

static
void
nfc_tag_t2_write_data_unaligned_wait(
    NfcTarget* target,
    void* user_data)
{
    NfcTagType2WriteData* write = user_data;

    if (target->sequence == write->seq) {
        GDEBUG("Starting write #%u", write->seq_id);
        nfc_target_remove_handler(target, write->start_id);
        write->start_id = 0;
        nfc_tag_t2_write_data_unaligned_start(write);
    }
}

/*==========================================================================*
 * Initialization
 *==========================================================================*/

static
void
nfc_tag_t2_initialized(
    NfcTagType2* self)
{
    NfcTagType2Priv* priv = self->priv;
    NfcTag* tag = &self->tag;

    if (priv->init_seq) {
        nfc_target_sequence_free(priv->init_seq);
        priv->init_seq = NULL;
    }
    nfc_tag_set_initialized(tag);
}

static
void
nfc_tag_t2_init_read_resp(
    NfcTagType2* self,
    NFC_TRANSMIT_STATUS status,
    const void* bytes,
    guint len,
    void* user_data)
{
    NfcTagType2Priv* priv = self->priv;
    guint block = GPOINTER_TO_UINT(user_data);

    priv->init_id = 0;
    if (status == NFC_TRANSMIT_STATUS_OK) {
        NfcTagType2Sector* sector = priv->sectors; /* sector 0 */
        const guint block_size = self->block_size;
        const guint nb = len / block_size;
        GUtilData data;

        GASSERT(!(len % block_size));
        nfc_tag_t2_sector_set_data(sector, block_size, bytes, block, nb);
        block += nb;
        data.bytes = sector->data.bytes;
        data.size = (block - NFC_TAG_T2_DATA_BLOCK0) * block_size;

        /* Stop reading when we have fetched the entire TLV sequence.
         * That should be enough to parse the NDEF (if there's any)
         * which all we really need in most cases. */
        if ((block * block_size) < sector->size &&
            len >= block_size && !nfc_tlv_check(&data)) {
            /* Continue reading the data */
            priv->init_id = nfc_tag_t2_cmd_read(self, block, priv->init_seq,
                nfc_tag_t2_init_read_resp, NULL, GUINT_TO_POINTER(block));
        } else {
            NfcTag* tag = &self->tag;

            GDEBUG("Tag data:");
            nfc_hexdump_data(&data);
            if ((block * block_size) < sector->size) {
                /* Inficate that data wasn't fully read */
                gutil_log(&_nfc_dump_log, GLOG_LEVEL_DEBUG, "  %04X: ...",
                    block * block_size);
            }

            /* Find NDEF */
            tag->ndef = nfc_ndef_rec_new_tlv(&sector->data);
            nfc_tag_t2_initialized(self);
        }
    } else {
        GDEBUG("Failed to read data block %u, giving up", block);
        nfc_tag_t2_initialized(self);
    }
}

static
void
nfc_tag_t2_control_area_read_resp(
    NfcTagType2* self,
    NFC_TRANSMIT_STATUS status,
    const void* data,
    guint len,
    void* user_data)
{
    NfcTagType2Priv* priv = self->priv;

    priv->init_id = 0;
    if (status == NFC_TRANSMIT_STATUS_OK && len == 16) {
        const guint8* bytes = data;
        const guint8* serial = bytes + 4;
        const guint8* cc = bytes + 12;

        /*
         * Layout of the first 4 blocks accorting to NFCForum-TS-Type-2-Tag:
         *
         * Bytes 0..3   - UID / Internal
         * Bytes 4..7   - Serial Number
         * Bytes 8..11  - Internal / Lock
         * Bytes 12..15 - Capability Container (CC)
         */
        memcpy(priv->serial, serial, 4);
        self->serial.bytes = priv->serial;
        self->serial.size = 4;
        GDEBUG("Serial: %02x %02x %02x %02x", priv->serial[0],
            priv->serial[1], priv->serial[2], priv->serial[3]);

        if (cc[0] == NFC_TAG_T2_CC_NFC_FORUM_MAGIC &&
            cc[1] >= NFC_TAG_T2_CC_MIN_VERSION) {
            NfcTagType2Sector* sector0;

            self->data_size = cc[2] * 8;
            GDEBUG("Data size: %u bytes", self->data_size);
            /* Allocate sector descriptors (only one sector for now) */
            priv->sector_count = 1;
            priv->sectors = g_new0(NfcTagType2Sector, priv->sector_count);

            sector0 = priv->sectors;
            nfc_tag_t2_sector_init(sector0, self->block_size,
                NFC_TAG_T2_DATA_BLOCK0, self->data_size / self->block_size, 0);
            nfc_tag_t2_sector_set_data(sector0, self->block_size, data, 0,
                len / self->block_size);

            /* We can already mark it as NFC Forum compatible */
            self->t2flags |= NFC_TAG_T2_FLAG_NFC_FORUM_COMPATIBLE;
            /* Start reading the data */
            priv->init_id = nfc_tag_t2_cmd_read(self, NFC_TAG_T2_DATA_BLOCK0,
                priv->init_seq, nfc_tag_t2_init_read_resp, NULL,
                GUINT_TO_POINTER(NFC_TAG_T2_DATA_BLOCK0));
        } else {
            GDEBUG("Tag is not NFC Forum compatible");
            nfc_tag_t2_initialized(self);
        }
    } else {
        GDEBUG("Failed to read first 16 bytes of the first sector, giving up");
        nfc_tag_t2_initialized(self);
    }
}

static
void
nfc_tag_t2_init2(
    NfcTagType2* self,
    NfcTarget* target,
    const NfcParamPollA* param)
{
    NfcTagType2Priv* priv = self->priv;

    nfc_tag_set_target(&self->tag, target);
    priv->init_seq = nfc_target_sequence_new(target);
    if (param) {
        priv->nfcid1 = g_memdup(param->nfcid1.bytes, param->nfcid1.size);
        self->sel_res = param->sel_res;
        self->nfcid1.size = param->nfcid1.size;
        self->nfcid1.bytes = priv->nfcid1;
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcTagType2*
nfc_tag_t2_new(
    NfcTarget* target,
    const NfcParamPollA* param)
{
    if (G_LIKELY(target) && G_LIKELY(param)) {
        NfcTagType2* self = g_object_new(NFC_TYPE_TAG_T2, NULL);
        NfcTagType2Priv* priv = self->priv;
        NfcTag* tag = &self->tag;
        const char* desc = "";

        if (param->nfcid1.size == 7 &&
            param->nfcid1.bytes[0] == NXP_MANUFACTURER_ID &&
            param->sel_res == 0) {
            tag->type = NFC_TAG_TYPE_MIFARE_ULTRALIGHT;
            desc = " (MIFARE Ultralight)";
        } else {
            switch (param->sel_res) {
            case 0x01:
            case 0x08:
            case 0x88:
                tag->type = NFC_TAG_TYPE_MIFARE_CLASSIC;
                desc = " (MIFARE Classic 1k)";
                break;
            case 0x09:
                tag->type = NFC_TAG_TYPE_MIFARE_CLASSIC;
                desc = " (MIFARE Classic Mini)";
                break;
            case 0x10:
                tag->type = NFC_TAG_TYPE_MIFARE_CLASSIC;
                desc = " (MIFARE Classic 2k)";
                break;
            case 0x11:
                tag->type = NFC_TAG_TYPE_MIFARE_CLASSIC;
                desc = " (MIFARE Plus 4k)";
                break;
            case 0x18:
                tag->type = NFC_TAG_TYPE_MIFARE_CLASSIC;
                desc = " (MIFARE Classic 4k)";
                break;
            case 0x28:
                tag->type = NFC_TAG_TYPE_MIFARE_CLASSIC;
                desc = " (MIFARE Classic 1k, emulated)";
                break;
            case 0x38:
                tag->type = NFC_TAG_TYPE_MIFARE_CLASSIC;
                desc = " (MIFARE Classic 4k, emulated)";
                break;
            case 0x98:
            case 0xB8:
                tag->type = NFC_TAG_TYPE_MIFARE_CLASSIC;
                desc = " (MIFARE Pro 4k)";
            break;
            default:
                break;
            }
        }

        GDEBUG("Type 2 tag%s", desc);
        nfc_tag_t2_init2(self, target, param);

        /* Start initialization by reading first blocks of sector 0 */
        priv->init_id = nfc_tag_t2_cmd_read(self, 0, priv->init_seq,
            nfc_tag_t2_control_area_read_resp, NULL, NULL);
        return self;
    }
    return NULL;
}

guint
nfc_tag_t2_read(
    NfcTagType2* self,
    guint sector,
    guint block,
    NfcTagType2ReadFunc resp,
    GDestroyNotify done,
    void* user_data)
{
#pragma message("TODO: Support more than one sector")
    if (G_LIKELY(self) && sector == 0) {
        return nfc_tag_t2_cmd_read(self, block, NULL, resp, done, user_data);
    }
    return 0;
}

guint
nfc_tag_t2_read_data(
    NfcTagType2* self,
    guint offset,
    guint maxbytes,
    NfcTagType2ReadDataFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
#pragma message("TODO: Support more than one sector and cross-sector reads")
    if (G_LIKELY(self) && (self->tag.flags & NFC_TAG_FLAG_INITIALIZED)) {
        NfcTagType2Priv* priv = self->priv;
        NfcTagType2Sector* sector = priv->sectors;
        const GUtilData* data = &sector->data;

        if (offset < data->size) {
            NfcTagType2ReadData* read = g_slice_new0(NfcTagType2ReadData);
            const guint block_size = self->block_size;
            const guint header_bytes = data->bytes - sector->bytes;
            const guint header_blocks = header_bytes / block_size;
            guint end_block, start_block = offset / block_size;
            gboolean valid;

            if (maxbytes > (data->size - offset)) {
                maxbytes = (data->size - offset);
            }

            read->t2 = self;
            read->buffer = g_malloc0(maxbytes);
            read->offset = offset;
            read->size = maxbytes;
            read->complete = complete;
            read->destroy = destroy;
            read->user_data = user_data;
            read->seq_id = nfc_tag_t2_generate_id(self);

            if (!priv->reads) {
                priv->reads = g_hash_table_new_full(g_direct_hash,
                    g_direct_equal, NULL, nfc_tag_t2_read_data_free);
            }
            g_hash_table_insert(priv->reads, GUINT_TO_POINTER(read->seq_id),
                read);

            end_block = (offset + read->size + block_size - 1) / block_size;
            nfc_tag_t2_data_block_to_sector(self, start_block, NULL, &valid);
            if (valid) {
                guint i, cached_bytes = block_size - (offset % block_size);

                for (i = start_block + 1; i < end_block; i++) {
                    nfc_tag_t2_data_block_to_sector(self, i, NULL, &valid);
                    if (valid) {
                        cached_bytes += block_size;
                    } else {
                        break;
                    }
                }

                /* Copy cached data */
                if (cached_bytes > maxbytes) {
                    cached_bytes = maxbytes;
                }
                memcpy(read->buffer, data->bytes + offset, cached_bytes);
                read->read = cached_bytes;
                start_block = i;
            }

            if (start_block == end_block) {
                /* Everything was cached - call completion on a fresh stack */
                read->complete_id = g_idle_add(nfc_tag_t2_read_complete, read);
            } else {
                /* We actually need to read something */
                read->seq = nfc_target_sequence_new(self->tag.target);
                read->cmd_id = nfc_tag_t2_cmd_read(self, header_blocks +
                    start_block, read->seq, nfc_tag_t2_read_resp, NULL, read);
            }
            return read->seq_id;
        }
    }
    return 0;
}

NFC_TAG_T2_IO_STATUS
nfc_tag_t2_read_data_sync(
    NfcTagType2* self,
    guint offset,
    guint size,
    void* buffer)
{
    if (G_LIKELY(self)) {
        const guint block_size = self->block_size;
        const guint start_block = offset / block_size;
        const guint end_block = (offset + size + block_size - 1) / block_size;
        gboolean valid;
        NfcTagType2Sector* sector = nfc_tag_t2_data_block_to_sector(self,
            start_block, NULL, &valid);

        if (!sector) {
            return NFC_TAG_T2_IO_STATUS_BAD_BLOCK;
        } else if (!nfc_tag_t2_data_block_to_sector(self, end_block - 1,
            NULL, NULL)) {
            return NFC_TAG_T2_IO_STATUS_BAD_SIZE;
        } else if (!valid) {
            /* The very first block is invalid, we are done */
            return NFC_TAG_T2_IO_STATUS_NOT_CACHED;
        } else {
            guint i;
            guint sect_offset = offset;
            guint sect_bytes = block_size - (offset % block_size);
            const GUtilData* data = &sector->data;

            /* Check the remaining blocks */
            for (i = start_block + 1; i < end_block; i++) {
                /* Since we have checked the range, sector must be found */
                NfcTagType2Sector* next = nfc_tag_t2_data_block_to_sector
                    (self, i, NULL, &valid);
                if (!valid) {
                    return NFC_TAG_T2_IO_STATUS_NOT_CACHED;
                } else if (sector == next) {
                    /* Count bytes in this sector */
                    sect_bytes += block_size;
                } else {
                    /* Copy bytes from this sector and switch to the next */
                    if (buffer) {
                        memcpy(buffer, data->bytes + sect_offset, sect_bytes);
                        buffer += sect_bytes;
                    }
                    sect_offset = 0;
                    sect_bytes = 0;
                    sector = next;
                    data = &sector->data;
                }
            }

            if (buffer) {
                memcpy(buffer, data->bytes + sect_offset, sect_bytes);
            }
            return NFC_TAG_T2_IO_STATUS_OK;
        }
    }
    return NFC_TAG_T2_IO_STATUS_FAILURE;
}

/* Primitive write, absolute block number withint a sector, only writes
 * entire blocks, can be used to write special areas, i.e. lock bytes. */
guint
nfc_tag_t2_write(
    NfcTagType2* self,
    guint sector_number,
    guint block,
    GBytes* bytes,
    NfcTagType2WriteFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
#pragma message("TODO: Support more than one sector and cross-sector writes")
    if (G_LIKELY(self) && bytes && sector_number == 0 &&
        (self->tag.flags & NFC_TAG_FLAG_INITIALIZED)) {
        gsize size;
        const guint block_size = self->block_size;
        gsize offset = block * block_size;
        const guint8* data = g_bytes_get_data(bytes, &size);
        NfcTagType2Priv* priv = self->priv;
        NfcTagType2Sector* sector = priv->sectors + sector_number;

        /* Round total size down to the nearest block boundary */
        size -= size % block_size;
        if (sector && size > 0 && (offset + size) <= sector->size) {
            NfcTagType2WriteData* write = nfc_tag_t2_write_data_new(self,
                sector_number, offset, bytes, G_CALLBACK(complete), destroy,
                user_data);

            GDEBUG("Writing %u blocks starting at %u", (guint)
                (size / block_size), block);
            nfc_tag_t2_sector_invalidate(sector, block_size, block, 1);
            write->cmd_id = nfc_tag_t2_cmd_write(self, block, data,
                write->seq, nfc_tag_t2_write_resp, NULL, write);
            return write->seq_id;
        }
    }
    return 0;
}

/* This one only touches the data area, allows unaligned access */
guint
nfc_tag_t2_write_data(
    NfcTagType2* self,
    guint offset,
    GBytes* bytes,
    NfcTagType2WriteDataFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(self) && bytes &&
       (self->tag.flags & NFC_TAG_FLAG_INITIALIZED)) {
        gsize size;
        const guint block_size = self->block_size;
        const guint8* data = g_bytes_get_data(bytes, &size);
        NfcTagType2Priv* priv = self->priv;
        guint start_block;
        gboolean start_block_cached;
        NfcTagType2Sector* sector = nfc_tag_t2_data_block_to_sector(self,
            offset / block_size, &start_block, &start_block_cached);

#pragma message("TODO: Support more than one sector and cross-sector writes")
        if (sector && size > 0 && (offset + size) <= sector->size) {
            const guint block_offset = offset % block_size;
            NfcTagType2WriteData* write = nfc_tag_t2_write_data_new(self,
                sector - priv->sectors, offset, bytes, G_CALLBACK(complete),
                destroy, user_data);

            GDEBUG("Writing %u data byte(s) starting at offset %u",
                (guint)size, offset);

            if (block_offset) {
                NfcTarget* target = self->tag.target;

                if (target->sequence == write->seq) {
                    /* Our sequence has started right away */
                    nfc_tag_t2_write_data_unaligned_start(write);
                } else {
                    /* Even if the block is cached, we can't really do
                     * anything until our sequence starts, because the
                     * block can be overwritten, invalidated or whatever
                     * between now and then. */
                    GDEBUG("Write #%u is pending", write->seq_id);
                    write->start_id = nfc_target_add_sequence_handler(target,
                        nfc_tag_t2_write_data_unaligned_wait, write);
                }
            } else {
                nfc_tag_t2_sector_invalidate(sector, block_size,
                    start_block, 1);
                write->cmd_id = nfc_tag_t2_cmd_write(self, start_block,
                    data, write->seq, nfc_tag_t2_write_data_resp, NULL, write);
            }

            return write->seq_id;
        }
    }
    return 0;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_tag_t2_init(
    NfcTagType2* self)
{
    self->block_size = NFC_TAG_T2_BLOCK_SIZE;
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NFC_TYPE_TAG_T2,
        NfcTagType2Priv);
}

static
void
nfc_tag_t2_finalize(
    GObject* object)
{
    NfcTagType2* self = NFC_TAG_T2(object);
    NfcTagType2Priv* priv = self->priv;

    if (priv->reads) {
        g_hash_table_destroy(priv->reads);
    }
    if (priv->writes) {
        g_hash_table_destroy(priv->writes);
    }
    if (priv->sectors) {
        guint i;

        for (i = 0; i < priv->sector_count; i++) {
            nfc_tag_t2_sector_deinit(priv->sectors + i);
        }
        g_free(priv->sectors);
    }
    nfc_target_cancel_transmit(self->tag.target, priv->init_id);
    nfc_target_sequence_free(priv->init_seq);
    g_free(priv->nfcid1);
    G_OBJECT_CLASS(nfc_tag_t2_parent_class)->finalize(object);
}

static
void
nfc_tag_t2_class_init(
    NfcTagType2Class* klass)
{
    g_type_class_add_private(klass, sizeof(NfcTagType2Priv));
    G_OBJECT_CLASS(klass)->finalize = nfc_tag_t2_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
