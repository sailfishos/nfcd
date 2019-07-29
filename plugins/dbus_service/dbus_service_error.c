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

#include "dbus_service.h"

#define DBUS_SERVICE_ERROR_(error) "org.sailfishos.nfc.Error." error

static const GDBusErrorEntry dbus_service_errors[] = {
    { DBUS_SERVICE_ERROR_FAILED,        DBUS_SERVICE_ERROR_("Failed") },
    { DBUS_SERVICE_ERROR_ACCESS_DENIED, DBUS_SERVICE_ERROR_("AccessDenied") },
    { DBUS_SERVICE_ERROR_INVALID_ARGS,  DBUS_SERVICE_ERROR_("InvalidArgs") },
    { DBUS_SERVICE_ERROR_NOT_FOUND,     DBUS_SERVICE_ERROR_("NotFound") },
    { DBUS_SERVICE_ERROR_NOT_SUPPORTED, DBUS_SERVICE_ERROR_("NotSupported") },
    { DBUS_SERVICE_ERROR_ABORTED,       DBUS_SERVICE_ERROR_("Aborted") },
    { DBUS_SERVICE_ERROR_NACK,          DBUS_SERVICE_ERROR_("NACK") }
};

G_STATIC_ASSERT(G_N_ELEMENTS(dbus_service_errors) == DBUS_SERVICE_NUM_ERRORS);

GQuark
dbus_service_error_quark()
{
    static volatile gsize dbus_service_error_quark_value = 0;

    g_dbus_error_register_error_domain("dbus-nfc-error-quark",
        &dbus_service_error_quark_value, dbus_service_errors,
            G_N_ELEMENTS(dbus_service_errors));
    return (GQuark)dbus_service_error_quark_value;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
