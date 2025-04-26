/*
 * Copyright (C) 2019-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2020 Open Mobile Platform LLC.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfc_tag_t4_p.h"
#include "nfc_target.h"
#include "nfc_log.h"

typedef struct nfc_tag_t4b_class {
    NfcTagType4Class parent;
} NfcTagType4bClass;

struct nfc_tag_t4b {
    NfcTagType4 t4;
};

G_DEFINE_TYPE(NfcTagType4b, nfc_tag_t4b, NFC_TYPE_TAG_T4)

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcTagType4b*
nfc_tag_t4b_new(
    NfcTarget* target,
    gboolean read_ndef,
    const NfcParamPollB* poll_b,
    const NfcParamIsoDepPollB* iso_dep_b)
{
    if (G_LIKELY(poll_b)) {
        NfcTagType4b* self = g_object_new(NFC_TYPE_TAG_T4B, NULL);
        NfcTagType4* t4 = &self->t4;
        NfcParamPoll poll;
        NfcParamIsoDep iso_dep;
        NfcParamIsoDep* p = NULL;

        GDEBUG("Type 4B tag");
        GASSERT(target->technology == NFC_TECHNOLOGY_B);
        memset(&poll, 0, sizeof(poll));
        poll.b = *poll_b;
        if (iso_dep_b) {
            memset(&iso_dep, 0, sizeof(iso_dep));
            iso_dep.b = *iso_dep_b;
            p = &iso_dep;
        }
        nfc_tag_t4_init_base(t4, target, read_ndef, poll_b->fsc, &poll, p);
        return self;
    }
    return NULL;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_tag_t4b_init(
    NfcTagType4b* self)
{
}

static
void
nfc_tag_t4b_class_init(
    NfcTagType4bClass* klass)
{
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
