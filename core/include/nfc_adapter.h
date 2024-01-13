/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2021 Jolla Ltd.
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

#ifndef NFC_ADAPTER_H
#define NFC_ADAPTER_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct nfc_adapter_priv NfcAdapterPriv;

struct nfc_adapter {
    GObject object;
    NfcAdapterPriv* priv;
    const char* name;
    NfcTag** tags;
    gboolean enabled;
    gboolean powered;
    gboolean power_requested;
    NFC_TAG_TYPE supported_tags;
    NFC_PROTOCOL supported_protocols;
    NFC_MODE supported_modes;
    NFC_MODE mode_requested;
    NFC_MODE mode;
    gboolean target_present; /* Presence of anything, actually */
};

GType nfc_adapter_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_ADAPTER (nfc_adapter_get_type())
#define NFC_ADAPTER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_ADAPTER, NfcAdapter))

typedef
void
(*NfcAdapterFunc)(
    NfcAdapter* adapter,
    void* user_data);

typedef
void
(*NfcAdapterTagFunc)(
    NfcAdapter* adapter,
    NfcTag* tag,
    void* user_data);

typedef
void
(*NfcAdapterPeerFunc)(
    NfcAdapter* adapter,
    NfcPeer* peer,
    void* user_data); /* Since 1.1.0 */

typedef
void
(*NfcAdapterHostFunc)(
    NfcAdapter* adapter,
    NfcHost* host,
    void* user_data); /* Since 1.2.0 */

NfcAdapter*
nfc_adapter_ref(
    NfcAdapter* adapter)
    NFCD_EXPORT;

void
nfc_adapter_unref(
    NfcAdapter* adapter)
    NFCD_EXPORT;

void
nfc_adapter_request_power(
    NfcAdapter* adapter,
    gboolean on)
    NFCD_EXPORT;

gboolean
nfc_adapter_request_mode(
    NfcAdapter* adapter,
    NFC_MODE mode)
    NFCD_EXPORT;

NfcPeer**
nfc_adapter_peers(
    NfcAdapter* adapter) /* Since 1.1.0 */
    NFCD_EXPORT;

NfcHost**
nfc_adapter_hosts(
    NfcAdapter* adapter) /* Since 1.2.0 */
    NFCD_EXPORT;

NFC_TECHNOLOGY
nfc_adapter_get_supported_techs(
    NfcAdapter* adapter) /* Since 1.2.0 */
    NFCD_EXPORT;

NfcTag*
nfc_adapter_add_tag_t2(
    NfcAdapter* adapter,
    NfcTarget* target,
    const NfcTagParamT2* params)
    NFCD_EXPORT;

NfcTag*
nfc_adapter_add_tag_t4a(
    NfcAdapter* adapter,
    NfcTarget* target,
    const NfcParamPollA* poll_a,
    const NfcParamIsoDepPollA* iso_dep_param) /* Since 1.0.20 */
    NFCD_EXPORT;

NfcTag*
nfc_adapter_add_tag_t4b(
    NfcAdapter* adapter,
    NfcTarget* target,
    const NfcParamPollB* poll_b,
    const NfcParamIsoDepPollB* iso_dep_param) /* Since 1.0.20 */
    NFCD_EXPORT;

NfcTag*
nfc_adapter_add_other_tag(
    NfcAdapter* adapter,
    NfcTarget* target)
    G_GNUC_DEPRECATED_FOR(nfc_adapter_add_other_tag2)
    NFCD_EXPORT;

NfcTag*
nfc_adapter_add_other_tag2(
    NfcAdapter* adapter,
    NfcTarget* target,
    const NfcParamPoll* poll) /* Since 1.0.33 */
    NFCD_EXPORT;

void
nfc_adapter_remove_tag(
    NfcAdapter* adapter,
    const char* name)
    NFCD_EXPORT;

NfcPeer*
nfc_adapter_add_peer_initiator_a(
    NfcAdapter* adapter,
    NfcTarget* target,
    const NfcParamPollA* tech_param,
    const NfcParamNfcDepInitiator* nfc_dep_param) /* Since 1.1.0 */
    NFCD_EXPORT;

NfcPeer*
nfc_adapter_add_peer_initiator_f(
    NfcAdapter* adapter,
    NfcTarget* target,
    const NfcParamPollF* tech_param,
    const NfcParamNfcDepInitiator* nfc_dep_param) /* Since 1.1.0 */
    NFCD_EXPORT;

NfcPeer*
nfc_adapter_add_peer_target_a(
    NfcAdapter* adapter,
    NfcInitiator* initiator,
    const NfcParamListenA* tech_param,
    const NfcParamNfcDepTarget* nfc_dep_param) /* Since 1.1.0 */
    NFCD_EXPORT;

NfcPeer*
nfc_adapter_add_peer_target_f(
    NfcAdapter* adapter,
    NfcInitiator* initiator,
    const NfcParamListenF* tech_param,
    const NfcParamNfcDepTarget* nfc_dep_param) /* Since 1.1.0 */
    NFCD_EXPORT;

void
nfc_adapter_remove_peer(
    NfcAdapter* adapter,
    const char* name) /* Since 1.1.0 */
    NFCD_EXPORT;

NfcHost*
nfc_adapter_add_host(
    NfcAdapter* adapter,
    NfcInitiator* initiator) /* Since 1.2.0 */
    NFCD_EXPORT;

gulong
nfc_adapter_add_target_presence_handler(
    NfcAdapter* adapter,
    NfcAdapterFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_adapter_add_tag_added_handler(
    NfcAdapter* adapter,
    NfcAdapterTagFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_adapter_add_tag_removed_handler(
    NfcAdapter* adapter,
    NfcAdapterTagFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_adapter_add_peer_added_handler(
    NfcAdapter* adapter,
    NfcAdapterPeerFunc func,
    void* user_data) /* Since 1.1.0 */
    NFCD_EXPORT;

gulong
nfc_adapter_add_peer_removed_handler(
    NfcAdapter* adapter,
    NfcAdapterPeerFunc func,
    void* user_data) /* Since 1.1.0 */
    NFCD_EXPORT;

gulong
nfc_adapter_add_host_added_handler(
    NfcAdapter* adapter,
    NfcAdapterHostFunc func,
    void* user_data) /* Since 1.2.0 */
    NFCD_EXPORT;

gulong
nfc_adapter_add_host_removed_handler(
    NfcAdapter* adapter,
    NfcAdapterHostFunc func,
    void* user_data) /* Since 1.2.0 */
    NFCD_EXPORT;

gulong
nfc_adapter_add_powered_changed_handler(
    NfcAdapter* adapter,
    NfcAdapterFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_adapter_add_power_requested_handler(
    NfcAdapter* adapter,
    NfcAdapterFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_adapter_add_mode_changed_handler(
    NfcAdapter* adapter,
    NfcAdapterFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_adapter_add_mode_requested_handler(
    NfcAdapter* adapter,
    NfcAdapterFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_adapter_add_enabled_changed_handler(
    NfcAdapter* adapter,
    NfcAdapterFunc func,
    void* user_data)
    NFCD_EXPORT;

void
nfc_adapter_remove_handler(
    NfcAdapter* adapter,
    gulong id)
    NFCD_EXPORT;

void
nfc_adapter_remove_handlers(
    NfcAdapter* adapter,
    gulong* ids,
    guint count)
    NFCD_EXPORT;

#define nfc_adapter_remove_all_handlers(adapter,ids) \
    nfc_adapter_remove_handlers(adapter, ids, G_N_ELEMENTS(ids))

G_END_DECLS

#endif /* NFC_ADAPTER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
