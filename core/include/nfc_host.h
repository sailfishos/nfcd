/*
 * Copyright (C) 2023 Slava Monich <slava@monich.com>
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

#ifndef NFC_HOST_H
#define NFC_HOST_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

/* Since 1.2.0 */

typedef struct nfc_host_priv NfcHostPriv;

struct nfc_host {
    GObject object;
    NfcHostPriv* priv;
    NfcInitiator* initiator;
    const char* name;
    NfcHostApp* app; /* Selected app */
};

GType nfc_host_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_HOST (nfc_host_get_type())
#define NFC_IS_HOST(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, NFC_TYPE_HOST)
#define NFC_HOST(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, NFC_TYPE_HOST, NfcHost)

typedef
void
(*NfcHostFunc)(
    NfcHost* host,
    void* user_data);

typedef
void
(*NfcHostResultFunc)(
    NfcHost* host,
    gboolean ok,
    void* user_data);

NfcHost*
nfc_host_ref(
    NfcHost* host)
    NFCD_EXPORT;

void
nfc_host_unref(
    NfcHost* host)
    NFCD_EXPORT;

void
nfc_host_select_app(
    NfcHost* host,
    const char* aid)
    NFCD_EXPORT;

void
nfc_host_deselect_app(
    NfcHost* host)
    NFCD_EXPORT;

void
nfc_host_deactivate(
    NfcHost* host)
    NFCD_EXPORT;

gulong
nfc_host_add_app_changed_handler(
    NfcHost* host,
    NfcHostFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_host_add_gone_handler(
    NfcHost* host,
    NfcHostFunc func,
    void* user_data)
    NFCD_EXPORT;

void
nfc_host_remove_handler(
    NfcHost* host,
    gulong id)
    NFCD_EXPORT;

void
nfc_host_remove_handlers(
    NfcHost* host,
    gulong* ids,
    guint count)
    NFCD_EXPORT;

#define nfc_host_remove_all_handlers(host,ids) \
    nfc_host_remove_handlers(host, ids, G_N_ELEMENTS(ids))

G_END_DECLS

#endif /* NFC_HOST_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
