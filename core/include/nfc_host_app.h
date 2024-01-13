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

#ifndef NFC_HOST_APP_H
#define NFC_HOST_APP_H

#include "nfc_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

/* Since 1.2.0 */

typedef enum nfc_host_app_flags {
    NFC_HOST_APP_FLAGS_NONE = 0,
    NFC_HOST_APP_FLAG_ALLOW_IMPLICIT_SELECTION = 0x01
} NFC_HOST_APP_FLAGS;

typedef struct nfc_host_app_priv NfcHostAppPriv;

struct nfc_host_app {
    GObject object;
    NfcHostAppPriv* priv;
    GUtilData aid;
    const char* name;
    NFC_HOST_APP_FLAGS flags;
};

GType nfc_host_app_get_type(void) NFCD_EXPORT;
#define NFC_TYPE_HOST_APP (nfc_host_app_get_type())
#define NFC_HOST_APP(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, \
        NFC_TYPE_HOST_APP, NfcHostApp)

NfcHostApp*
nfc_host_app_ref(
    NfcHostApp* app)
    NFCD_EXPORT;

void
nfc_host_app_unref(
    NfcHostApp* app)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_HOST_APP_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
