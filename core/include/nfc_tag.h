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

#ifndef NFC_TAG_H
#define NFC_TAG_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct nfc_tag_priv NfcTagPriv;

typedef enum nfc_tag_flags {
    NFC_TAG_FLAGS_NONE = 0x00,
    NFC_TAG_FLAG_INITIALIZED = 0x01,
} NFC_TAG_FLAGS;

struct nfc_tag {
    GObject object;
    NfcTagPriv* priv;
    NfcTarget* target;
    const char* name;
    gboolean present;
    NFC_TAG_TYPE type;
    NFC_TAG_FLAGS flags;
    NfcNdefRec* ndef;  /* Valid only when initialized */
};

GType nfc_tag_get_type(void);
#define NFC_TYPE_TAG (nfc_tag_get_type())
#define NFC_TAG(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_TAG, NfcTag))

struct nfc_param_poll_a {
    guint8 sel_res;  /* (SAK)*/
    GUtilData nfcid1;
};

struct nfc_param_poll_b {
    guint fsc;      /* FSC (FSCI converted to bytes) */
    GUtilData nfcid0;
};

typedef
void
(*NfcTagFunc)(
    NfcTag* tag,
    void* user_data);

NfcTag*
nfc_tag_ref(
    NfcTag* tag);

void
nfc_tag_unref(
    NfcTag* tag);

void
nfc_tag_deactivate(
    NfcTag* tag);

gulong
nfc_tag_add_gone_handler(
    NfcTag* tag,
    NfcTagFunc func,
    void* user_data);

gulong
nfc_tag_add_initialized_handler(
    NfcTag* tag,
    NfcTagFunc func,
    void* user_data);

void
nfc_tag_remove_handler(
    NfcTag* tag,
    gulong id);

void
nfc_tag_remove_handlers(
    NfcTag* tag,
    gulong* ids,
    guint count);

#define nfc_tag_remove_all_handlers(tag,ids) \
    nfc_tag_remove_handlers(tag, ids, G_N_ELEMENTS(ids))

G_END_DECLS

#endif /* NFC_TAG_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
