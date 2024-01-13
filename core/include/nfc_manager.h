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

#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct nfc_manager_priv NfcManagerPriv;

struct nfc_manager {
    GObject object;
    NfcManagerPriv* priv;
    NfcAdapter** adapters;
    gboolean enabled;
    gboolean stopped;
    int error;
    /* Since 1.1.0 */
    NFC_MODE mode;
    /* Since 1.1.1 */
    NFC_LLCP_VERSION llcp_version;
    NfcPeerService* const* services;
    /* Since 1.2.0 */
    NFC_TECHNOLOGY techs;
};

GType nfc_manager_get_type() NFCD_EXPORT;
#define NFC_TYPE_MANAGER (nfc_manager_get_type())
#define NFC_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        NFC_TYPE_MANAGER, NfcManager))

/* Code passed to nfc_manager_stop by plugins to initiate emergency exit */
#define NFC_MANAGER_PLUGIN_ERROR (127)

typedef
void
(*NfcManagerFunc)(
    NfcManager* manager,
    void* user_data);

typedef
void
(*NfcManagerAdapterFunc)(
    NfcManager* manager,
    NfcAdapter* adapter,
    void* user_data);

typedef
void
(*NfcManagerServiceFunc)(
    NfcManager* manager,
    NfcPeerService* service,
    void* user_data);

NfcManager*
nfc_manager_ref(
    NfcManager* manager)
    NFCD_EXPORT;

void
nfc_manager_unref(
    NfcManager* manager)
    NFCD_EXPORT;

NfcPlugin* const*
nfc_manager_plugins(
    NfcManager* manager)
    NFCD_EXPORT;

NfcAdapter*
nfc_manager_get_adapter(
    NfcManager* manager,
    const char* name)
    NFCD_EXPORT;

const char*
nfc_manager_add_adapter(
    NfcManager* manager,
    NfcAdapter* adapter)
    NFCD_EXPORT;

void
nfc_manager_remove_adapter(
    NfcManager* manager,
    const char* name)
    NFCD_EXPORT;

void
nfc_manager_stop(
    NfcManager* manager,
    int error)
    NFCD_EXPORT;

void
nfc_manager_set_enabled(
    NfcManager* manager,
    gboolean enabled)
    NFCD_EXPORT;

void
nfc_manager_request_power(
    NfcManager* manager,
    gboolean on)
    NFCD_EXPORT;

gboolean
nfc_manager_register_service(
    NfcManager* manager,
    NfcPeerService* service) /* Since 1.1.0 */
    NFCD_EXPORT;

void
nfc_manager_unregister_service(
    NfcManager* manager,
    NfcPeerService* service) /* Since 1.1.0 */
    NFCD_EXPORT;

gboolean
nfc_manager_register_host_service(
    NfcManager* manager,
    NfcHostService* service) /* Since 1.2.0 */
    NFCD_EXPORT;

void
nfc_manager_unregister_host_service(
    NfcManager* manager,
    NfcHostService* service) /* Since 1.2.0 */
    NFCD_EXPORT;

gboolean
nfc_manager_register_host_app(
    NfcManager* manager,
    NfcHostApp* app) /* Since 1.2.0 */
    NFCD_EXPORT;

void
nfc_manager_unregister_host_app(
    NfcManager* manager,
    NfcHostApp* app) /* Since 1.2.0 */
    NFCD_EXPORT;

gulong
nfc_manager_add_adapter_added_handler(
    NfcManager* manager,
    NfcManagerAdapterFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_manager_add_adapter_removed_handler(
    NfcManager* manager,
    NfcManagerAdapterFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_manager_add_enabled_changed_handler(
    NfcManager* manager,
    NfcManagerFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_manager_add_stopped_handler(
    NfcManager* manager,
    NfcManagerFunc func,
    void* user_data)
    NFCD_EXPORT;

gulong
nfc_manager_add_mode_changed_handler(
    NfcManager* manager,
    NfcManagerFunc func,
    void* user_data) /* Since 1.1.0 */
    NFCD_EXPORT;

gulong
nfc_manager_add_service_registered_handler(
    NfcManager* manager,
    NfcManagerServiceFunc func,
    void* user_data) /* Since 1.1.1 */
    NFCD_EXPORT;

gulong
nfc_manager_add_service_unregistered_handler(
    NfcManager* manager,
    NfcManagerServiceFunc func,
    void* user_data) /* Since 1.1.1 */
    NFCD_EXPORT;

gulong
nfc_manager_add_techs_changed_handler(
    NfcManager* manager,
    NfcManagerFunc func,
    void* user_data) /* Since 1.2.0 */
    NFCD_EXPORT;

void
nfc_manager_remove_handler(
    NfcManager* manager,
    gulong id)
    NFCD_EXPORT;

void
nfc_manager_remove_handlers(
    NfcManager* manager,
    gulong* ids,
    guint count)
    NFCD_EXPORT;

#define nfc_manager_remove_all_handlers(manager,ids) \
    nfc_manager_remove_handlers(manager, ids, G_N_ELEMENTS(ids))

/*
 * Plugins can ask NfcManager to enable and/or disable certain NFC modes.
 * The last submitted request takes precedence, i.e. if first a request
 * is submitted to enable certain mode and then another another request
 * to disable the same mode, the mode gets disabled.
 *
 * If the same bits are set in both enable and disable masks, the enabling
 * bits take precedence. If both are zero, nfc_manager_mode_request_new()
 * returns NULL which is tolerated by nfc_manager_mode_request_free()
 *
 * Note that each NfcModeRequest carries an implicit reference to NfcManager.
 *
 * NfcTechRequest behaves similarly with respect to allowed NFC technologies.
 */

typedef struct nfc_mode_request NfcModeRequest; /* Since 1.1.0 */
typedef struct nfc_tech_request NfcTechRequest; /* Since 1.2.0 */

NfcModeRequest*
nfc_manager_mode_request_new(
    NfcManager* manager,
    NFC_MODE enable,
    NFC_MODE disable) /* Since 1.1.0 */
    G_GNUC_WARN_UNUSED_RESULT
    NFCD_EXPORT;

void
nfc_manager_mode_request_free(
    NfcModeRequest* req) /* Since 1.1.0 */
    NFCD_EXPORT;

NfcTechRequest*
nfc_manager_tech_request_new(
    NfcManager* manager,
    NFC_TECHNOLOGY enable,
    NFC_TECHNOLOGY disable) /* Since 1.2.0 */
    G_GNUC_WARN_UNUSED_RESULT
    NFCD_EXPORT;

void
nfc_manager_tech_request_free(
    NfcTechRequest* req) /* Since 1.2.0 */
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_MANAGER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
