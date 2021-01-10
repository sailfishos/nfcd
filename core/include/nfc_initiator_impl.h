/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
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

#ifndef NFC_INITIATOR_IMPL_H
#define NFC_INITIATOR_IMPL_H

#include "nfc_initiator.h"

/* Internal API for use by NfcInitiator implemenations */

G_BEGIN_DECLS

/* Since 1.1.0 */

typedef struct nfc_initiator_class {
    GObjectClass parent;

    /* Base class makes sure that there are no overlapping responses.
     * When transmission completes, nfc_initiator_response_sent() is called
     * by the derived class. */
    gboolean (*respond)(NfcInitiator* initiator, const void* data, guint len);

    /* This should deactivate the initiator. When the initiator gets
     * deactivated, subclass calls nfc_initiator_gone() to update the
     * 'present' flag. */
    void (*deactivate)(NfcInitiator* initiator);

    /* These base implementation emits signal, must always be called. */
    void (*gone)(NfcInitiator* initiator);

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
} NfcInitiatorClass;

#define NFC_INITIATOR_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), \
        NFC_TYPE_INITIATOR, NfcInitiatorClass)

/*
 * Normally it goes like this:
 *
 * 1. Data is coming in. Derived class calls nfc_initiator_transmit()
 * 2. Base class issues a signal passing in NfcTransmission object
 * 3. Transmission handler does whatever and calls nfc_transmission_respond()
 *    That translates into respond() call to the derived class.
 * 4. Derived class calls nfc_initiator_response_sent() when response is sent.
 * 5. At this point initiator is ready to receive a new portion of data.
 *
 * Now, if anything goes wrong... Basically, if anything goes wrong,
 * RF interface is deactivated by invoking deactivate() callback of the
 * derived class. Here is what can go wrong:

 * a. No one responds to the signal at step 2.
 * b. nfc_transmission_unref() is called before nfc_transmission_respond()
 *    in other words, transmission is received but dropped with no reply
 *    provided.
 * c. nfc_initiator_response_sent() receives an error status at step 4.
 *
 * It's not quite clear what to do when the next portion of data arrives
 * before we have sent a response to the previous one. Even though it
 * shouldn't happen in real life, lower level APIs (e.g. NCI) often
 * allow it. Currently it's being treated as an error (or was treated
 * at the time of this writing). Let's see how it goes.
 */

void
nfc_initiator_transmit(
    NfcInitiator* initiator,
    const void* data,
    guint len)
    NFCD_EXPORT;

void
nfc_initiator_response_sent(
    NfcInitiator* initiator,
    NFC_TRANSMIT_STATUS status)
    NFCD_EXPORT;

void
nfc_initiator_gone(
    NfcInitiator* initiator)
    NFCD_EXPORT;

G_END_DECLS

#endif /* NFC_INITIATOR_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
