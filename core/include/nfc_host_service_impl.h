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

#ifndef NFC_HOST_SERVICE_IMPL_H
#define NFC_HOST_SERVICE_IMPL_H

#include "nfc_host_service.h"

G_BEGIN_DECLS

/* Since 1.2.0 */

/* Internal API for use by NfcHostService implemenations */

typedef
void
(*NfcHostServiceBoolFunc)(
    NfcHostService* service,
    gboolean result,
    void* user_data);

typedef struct nfc_host_service_response {
    guint sw;                    /* 16 bits (SW1 << 8)|SW2 */
    GUtilData data;              /* Optional, may be all zeros */
    NfcHostServiceBoolFunc sent; /* Optional */
    void* user_data;             /* Passed to NfcHostServiceBoolFunc */
} NfcHostServiceResponse;

/*
 * The response argument may be NULL if APDU wasn't handled. If no
 * service or app handles the APDU, then 6F00 (No precise diagnosis)
 * is returned to the card reader.
 */

typedef
void
(*NfcHostServiceResponseFunc)(
    NfcHostService* service,
    const NfcHostServiceResponse* resp,
    void* user_data);

/*
 * Internal API for use by NfcHostService implemenations.
 *
 * NfcHostServices are registered with NfcManager and remain
 * registered until they are unregistered, even when NFC is idle.
 *
 * When NFC reader is detected, first all services and then all apps
 * have their start() method called. From that point on, it's the
 * selected (implicitly or explicitly) instance of NfcHostApp is
 * primarily responsible for communicating with the reader.
 *
 * However, if no app is selected or the selected app refuses to handle
 * the incoming APDU, services will have a chance to process such APDU.
 * Deselected apps are not bothered with any APDUs at all.
 */

typedef struct nfc_host_service_class {
    GObjectClass parent;

    /* Called when the other side starts talking to us */
    guint (*start)(NfcHostService* service, NfcHost* host,
        NfcHostServiceBoolFunc complete, void* user_data,
        GDestroyNotify destroy);

    /*
     * Called when the other side deactivates the interface and quickly
     * reconnects it back. That should be interpreted as a signal to
     * reset the state of the card emulator to default. The host is the
     * same as the one already passed to start() and possibly other methods.
     */
    guint (*restart)(NfcHostService* service, NfcHost* host,
        NfcHostServiceBoolFunc complete, void* user_data,
        GDestroyNotify destroy);

    /*
     * If process() returns zero, neither resp not destroy callback
     * is invoked.
     *
     * If non-zero operation id is returned, the resp and destroy
     * callbacks are eventually invoked when the operation completes.
     * Returning NFCD_ID_SYNC means that the completion and destroy
     * callbacks, if provided, have already been invoked on the same
     * stack. Otherwise callbacks will be invoked on a fresh stask
     * when operation completes, successfully or not.
     *
     * If a non-zero value is returned, the destroy callback is always
     * invoked in any case, regardless of whether the operation gets
     * completed or cancelled.
     */
    guint (*process)(NfcHostService* service, NfcHost* host,
        const NfcApdu* apdu, NfcHostServiceResponseFunc resp,
        void* user_data, GDestroyNotify destroy);

    /*
     * The cancel() method can be used to cancel a pending operation.
     * The implementation must guarantee that the operation completion
     * callback is not invoked for cancelled operations (the destroy
     * callback still is).
     *
     * NFCD_ID_FAIL and NFCD_ID_SYNC ids are ignored (but allowed),
     * even though they don't actually cancel anything.
     */
    void (*cancel)(NfcHostService* service, guint id);

     /* Padding for future expansion */
    void (*_reserved1)(void);
    void (*_reserved2)(void);
    void (*_reserved3)(void);
    void (*_reserved4)(void);
    void (*_reserved5)(void);
    void (*_reserved6)(void);
    void (*_reserved7)(void);
    void (*_reserved8)(void);
    void (*_reserved9)(void);
    void (*_reserved10)(void);
} NfcHostServiceClass;

#define NFC_HOST_SERVICE_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), \
        NFC_TYPE_HOST_SERVICE, NfcHostServiceClass)

void
nfc_host_service_init_base(
    NfcHostService* service,
    const char* name)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_HOST_SERVICE_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
