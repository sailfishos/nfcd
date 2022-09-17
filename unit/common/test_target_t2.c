/*
 * Copyright (C) 2022 Jolla Ltd.
 * Copyright (C) 2022 Slava Monich <slava.monich@jolla.com>
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

#include "test_target_t2.h"
#include "test_common.h"

#include "nfc_target_impl.h"

#include <gutil_log.h>

#define SUPER_LONG_TIMEOUT (24*60*60) /* seconds */

typedef NfcTargetClass TestTargetT2Class;
G_DEFINE_TYPE(TestTargetT2, test_target_t2, NFC_TYPE_TARGET)

static
gboolean
test_target_t2_read_done(
    gpointer user_data)
{
    TestTargetT2Read* read = user_data;
    TestTargetT2* self = read->target;
    NfcTarget* target = &self->target;
    const GUtilData* data = &self->data;
    NFC_TRANSMIT_STATUS status = NFC_TRANSMIT_STATUS_OK;
    guint offset = (read->block * TEST_TARGET_T2_BLOCK_SIZE) % data->size;
    guint8 buf[TEST_TARGET_T2_READ_SIZE];
    guint len = sizeof(buf);

    g_assert(self->transmit_id);
    self->transmit_id = 0;

    if ((offset + TEST_TARGET_T2_READ_SIZE) <= data->size) {
        memcpy(buf, data->bytes + offset, TEST_TARGET_T2_READ_SIZE);
    } else {
        const guint remain = (offset + TEST_TARGET_T2_READ_SIZE) - data->size;

        memcpy(buf, data->bytes + offset, TEST_TARGET_T2_READ_SIZE - remain);
        memcpy(buf + (TEST_TARGET_T2_READ_SIZE - remain), data->bytes, remain);
    }

    if (self->read_error && self->read_error->block == read->block) {
        switch (self->read_error->type) {
        case TEST_TARGET_T2_ERROR_TRANSMIT:
            status = NFC_TRANSMIT_STATUS_ERROR;
            len = 0;
            break;
        case TEST_TARGET_T2_ERROR_CRC:
            status = NFC_TRANSMIT_STATUS_CORRUPTED;
            len = 0;
            break;
        case TEST_TARGET_T2_ERROR_NACK:
            status = NFC_TRANSMIT_STATUS_NACK;
            buf[0] = 0;
            len = 1;
            break;
        case TEST_TARGET_T2_ERROR_SHORT_RESP:
            buf[0] = 0x08; /* Neither ACK nor NACK */
            len = 1;
            break;
        case TEST_TARGET_T2_ERROR_TIMEOUT:
            self->read_error = NULL;
            /* To avoid glib assert on cancel */
            self->transmit_id = g_timeout_add_seconds(SUPER_LONG_TIMEOUT,
                test_timeout_expired, NULL);
            /* Don't call nfc_target_transmit_done() */
            return G_SOURCE_REMOVE;
        }
        self->read_error = NULL;
    }

    nfc_target_transmit_done(target, status, buf, len);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_target_t2_write_done(
    gpointer user_data)
{
    TestTargetT2Write* write = user_data;
    TestTargetT2* self = write->target;
    NfcTarget* target = &self->target;
    guint8 ack = 0xaa;
    guint len = 1;
    NFC_TRANSMIT_STATUS status = NFC_TRANSMIT_STATUS_OK;

    g_assert(self->transmit_id);
    self->transmit_id = 0;

    if (self->write_error && self->write_error->block == write->block) {
        switch (self->write_error->type) {
        case TEST_TARGET_T2_ERROR_TRANSMIT:
            status = NFC_TRANSMIT_STATUS_ERROR;
            len = 0;
            break;
        case TEST_TARGET_T2_ERROR_CRC:
            g_assert(FALSE);
            break;
        case TEST_TARGET_T2_ERROR_NACK:
            ack = 0;
            break;
        case TEST_TARGET_T2_ERROR_SHORT_RESP:
            g_assert(FALSE);
            break;
        case TEST_TARGET_T2_ERROR_TIMEOUT:
            self->write_error = NULL;
            /* To avoid glib assert on cancel */
            self->transmit_id = g_timeout_add_seconds(SUPER_LONG_TIMEOUT,
                test_timeout_expired, NULL);
            /* Don't call nfc_target_transmit_done() */
            return G_SOURCE_REMOVE;
        }
        self->write_error = NULL;
    } else {
        const guint data_size = self->data.size;
        guint offset = (write->block * TEST_TARGET_T2_BLOCK_SIZE) % data_size;
        guint size = write->size;
        const guint8* src = write->data;

        while (size > 0) {
            if ((offset + size) <= data_size) {
                memcpy(self->storage + offset, src, size);
                break;
            } else {
                const guint to_copy = data_size - offset;

                memcpy(self->storage + offset, src, to_copy);
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
test_target_t2_write_free(
    gpointer user_data)
{
    TestTargetT2Write* write = user_data;

    g_free(write->data);
    g_free(write);
}

static
gboolean
test_target_t2_transmit(
    NfcTarget* target,
    const void* data,
    guint len)
{
    TestTargetT2* self = TEST_TARGET_T2(target);

    g_assert(!self->transmit_id);
    if (self->transmit_error > 0) {
        self->transmit_error--;
        GDEBUG("Simulating transmission failure");
    } else if (len > 0) {
        const guint8* cmd = data;

        switch (cmd[0]) {
        case 0x30: /* READ */
            if (len == 2) {
                TestTargetT2Read* read = g_new(TestTargetT2Read, 1);

                read->target = self;
                read->block = cmd[1];
                GDEBUG("Read block #%u", read->block);
                self->transmit_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                    test_target_t2_read_done, read, g_free);
                return TRUE;
            }
            break;
        case 0xa2: /* WRITE */
            if (len >= 2) {
                TestTargetT2Write* write = g_new(TestTargetT2Write, 1);

                write->target = self;
                write->block = cmd[1];
                write->size = len - 2;
                write->data = g_memdup(cmd + 2, write->size);
                GDEBUG("Write block #%u, %u bytes", write->block, write->size);
                self->transmit_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                    test_target_t2_write_done, write,
                    test_target_t2_write_free);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static
void
test_target_t2_cancel_transmit(
    NfcTarget* target)
{
    TestTargetT2* self = TEST_TARGET_T2(target);

    g_assert(self->transmit_id);
    g_source_remove(self->transmit_id);
    self->transmit_id = 0;
}

static
void
test_target_t2_init(
    TestTargetT2* self)
{
}

static
void
test_target_t2_finalize(
    GObject* object)
{
    TestTargetT2* self = TEST_TARGET_T2(object);

    if (self->transmit_id) {
        g_source_remove(self->transmit_id);
    }
    g_free(self->storage);
    G_OBJECT_CLASS(test_target_t2_parent_class)->finalize(object);
}

static
void
test_target_t2_class_init(
    NfcTargetClass* klass)
{
    klass->transmit = test_target_t2_transmit;
    klass->cancel_transmit = test_target_t2_cancel_transmit;
    G_OBJECT_CLASS(klass)->finalize = test_target_t2_finalize;
}

/*==========================================================================*
 * API
 *==========================================================================*/

TestTargetT2*
test_target_t2_new(
     const guint8* bytes,
     guint size)
{
     TestTargetT2* self = g_object_new(TEST_TYPE_TARGET_T2, NULL);

     self->target.technology = NFC_TECHNOLOGY_A;
     self->data.bytes = self->storage = g_memdup(bytes, size);
     self->data.size = size;
     return self;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
