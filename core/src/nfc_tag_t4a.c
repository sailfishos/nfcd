/*
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2019-2020 Slava Monich <slava.monich@jolla.com>
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

#include "nfc_tag_t4_p.h"
#include "nfc_target.h"
#include "nfc_log.h"

typedef struct nfc_tag_t4a_class {
    NfcTagType4Class parent;
} NfcTagType4aClass;

struct nfc_tag_t4a {
    NfcTagType4 t4;
};

G_DEFINE_TYPE(NfcTagType4a, nfc_tag_t4a, NFC_TYPE_TAG_T4)

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcTagType4a*
nfc_tag_t4a_new(
    NfcTarget* target,
    const NfcParamPollA* poll_a,
    const NfcParamIsoDepPollA* iso_dep_a)
{
    if (G_LIKELY(iso_dep_a)) {
        NfcTagType4a* self = g_object_new(NFC_TYPE_TAG_T4A, NULL);
        NfcTagType4* t4 = &self->t4;
        NfcParamIsoDep iso_dep;

        GDEBUG("Type 4A tag");
        GASSERT(target->technology == NFC_TECHNOLOGY_A);
        memset(&iso_dep, 0, sizeof(iso_dep));
        iso_dep.a = *iso_dep_a;
        if (poll_a) {
            NfcParamPoll poll;

            memset(&poll, 0, sizeof(poll));
            poll.a = *poll_a;
            nfc_tag_t4_init_base(t4, target, iso_dep_a->fsc, &poll, &iso_dep);
        } else {
            nfc_tag_t4_init_base(t4, target, iso_dep_a->fsc, NULL, &iso_dep);
        }
        return self;
    }
    return NULL;
}


/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_tag_t4a_init(
    NfcTagType4a* self)
{
}

static
void
nfc_tag_t4a_class_init(
    NfcTagType4aClass* klass)
{
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
