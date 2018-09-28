/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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

#include "dbus_handlers.h"

#include <nfc_ndef.h>

typedef struct dbus_handler_call DBusHandlerCall;

typedef struct dbus_handlers_run {
    NfcNdefRec* ndef;
    DBusHandlers* handlers;
    DBusHandlersConfig* config;
    DBusHandlerConfig* handler;
    DBusHandlerCall* handler_call;
    DBusHandlerCall* listener_calls;
    GCancellable* cancellable;
    gboolean handled;
} DBusHandlersRun;

struct dbus_handler_call {
    DBusHandlerCall* next;
    DBusHandlersRun* run;
};

struct dbus_handlers {
    char* dir;
    DBusHandlersRun* run;
    GDBusConnection* connection;
};

#define NDEF_NOT_HANDLED (0)
#define NDEF_HANDLED (1)

static
void
dbus_handlers_run_free(
    DBusHandlersRun* run);

static
void
dbus_handlers_run_next(
    DBusHandlersRun* run);

static
DBusHandlerCall*
dbus_handler_call_new(
    DBusHandlersRun* run)
{
    DBusHandlerCall* call = g_slice_new0(DBusHandlerCall);

    call->run = run;
    return call;
}

static
void
dbus_handler_call_free(
    DBusHandlerCall* call)
{
    g_slice_free(DBusHandlerCall, call);
}

static
void
dbus_handlers_run_handler_call_done(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    DBusHandlerCall* call = user_data;
    DBusHandlersRun* run = call->run;
    GError* error = NULL;
    GVariant* out = g_dbus_connection_call_finish
        (G_DBUS_CONNECTION(connection), result, &error);

    if (out) {
        if (run) {
            const char* empty = "()";
            const char* format = "(i)";
            const char* type = g_variant_get_type_string(out);
            DBusHandlerConfig* handler = run->handler;

            if (!g_strcmp0(type, empty)) {
                GDEBUG("No result from %s handler, assuming it's handled",
                    handler->dbus.service);
                run->handled = TRUE;
            } else if (!g_strcmp0(type, format)) {
                gint val = NDEF_NOT_HANDLED;

                g_variant_get(out, format, &val);
                GDEBUG("%s %shandled this NDEF", handler->dbus.service,
                    (val == NDEF_HANDLED) ? "" : "not ");
                if (val == NDEF_HANDLED) {
                    run->handled = TRUE;
                }
            } else {
                GWARN("Unexpected handler result %s", type);
            }
        }
        g_variant_unref(out);
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
    dbus_handler_call_free(call);
    if (run) {
        run->handler_call = NULL;
        run->handler = (run->handled ? NULL : run->handler->next);
        dbus_handlers_run_next(run);
    }
}

static
void
dbus_handlers_run_listener_call_done(
    GObject* connection,
    GAsyncResult* result,
    gpointer user_data)
{
    DBusHandlerCall* call = user_data;
    DBusHandlersRun* run = call->run;
    GError* error = NULL;
    GVariant* out = g_dbus_connection_call_finish
        (G_DBUS_CONNECTION(connection), result, &error);

    if (out) {
        g_variant_unref(out);
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
    if (run) {
        if (run->listener_calls == call) {
            run->listener_calls = call->next;
        } else {
            DBusHandlerCall* prev = run->listener_calls;

            /* This call must be on the list, no need to check for NULL */
            while (prev->next != call) {
                prev = prev->next;
            }
            prev->next = call->next;
        }
        call->next = NULL;
        dbus_handler_call_free(call);
        if (!run->listener_calls) {
            DBusHandlers* handlers = run->handlers;

            GASSERT(handlers->run == run);
            handlers->run = NULL;
            dbus_handlers_run_free(run);
        }
    } else {
        dbus_handler_call_free(call);
    }
}

static
void
dbus_handlers_run_handler(
    DBusHandlersRun* run)
{
    DBusHandlerCall* call = dbus_handler_call_new(run);
    DBusHandlerConfig* handler = run->handler;
    const DBusConfig* dbus = &handler->dbus;
    GVariant* parameters = handler->type->handler_args(run->ndef);

    GASSERT(!run->handler_call);
    run->handler_call = call;
    g_dbus_connection_call(run->handlers->connection, dbus->service,
        dbus->path, dbus->iface, dbus->method, parameters, NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, run->cancellable,
        dbus_handlers_run_handler_call_done, call);
}

static
void
dbus_handlers_run_listeners(
    DBusHandlersRun* run)
{
    DBusListenerConfig* listener = run->config->listeners;
    DBusHandlers* handlers = run->handlers;

    while (listener) {
        const DBusConfig* dbus = &listener->dbus;
        DBusHandlerCall* call = dbus_handler_call_new(run);
        GVariant* arg = listener->type->listener_args(run->handled, run->ndef);

        call->next = run->listener_calls;
        run->listener_calls = call;
        g_dbus_connection_call(handlers->connection, dbus->service,
            dbus->path, dbus->iface, dbus->method, arg, NULL,
            G_DBUS_CALL_FLAGS_NONE, -1, run->cancellable,
            dbus_handlers_run_listener_call_done, call);
        listener = listener->next;
    }

    if (!run->listener_calls) {
        GASSERT(handlers->run == run);
        handlers->run = NULL;
        dbus_handlers_run_free(run);
    }
}

static
void
dbus_handlers_run_next(
    DBusHandlersRun* run)
{
    if (run->handler) {
        dbus_handlers_run_handler(run);
    } else {
        dbus_handlers_run_listeners(run);
    }
}

static
void
dbus_handlers_run_cancelled(
    DBusHandlerCall* calls)
{
    while (calls) {
        DBusHandlerCall* call = calls;

        calls = call->next;
        call->next = NULL;
        call->run = NULL;
    }
}

static
DBusHandlersRun*
dbus_handlers_run_new(
    DBusHandlers* handlers,
    NfcNdefRec* ndef)
{
    /* dbus_handlers_config_load() returns NULL if no configs is found
     * which guarantees that we don't free DBusHandlersRun before we
     * return it to the caller. */
    DBusHandlersConfig* conf = dbus_handlers_config_load(handlers->dir, ndef);

    if (conf) {
        DBusHandlersRun* run = g_slice_new0(DBusHandlersRun);

        run->cancellable = g_cancellable_new();
        run->config = conf;
        run->handlers = handlers;
        run->ndef = nfc_ndef_rec_ref(ndef);
        run->handler = conf->handlers;
        dbus_handlers_run_next(run);
        return run;
    }
    return NULL;
}

static
void
dbus_handlers_run_free(
    DBusHandlersRun* run)
{
    if (run) {
        /* Disassociate pending calls with this DBusHandlersRun */
        g_cancellable_cancel(run->cancellable);
        dbus_handlers_run_cancelled(run->handler_call);
        dbus_handlers_run_cancelled(run->listener_calls);
        dbus_handlers_config_free(run->config);
        nfc_ndef_rec_unref(run->ndef);
        g_slice_free(DBusHandlersRun, run);
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

void
dbus_handlers_run(
    DBusHandlers* self,
    NfcNdefRec* ndef)
{
    if (self) {
        DBusHandlersRun* run = dbus_handlers_run_new(self, ndef);
        if (run) {
            dbus_handlers_run_free(self->run);
            self->run = run;
        } else {
            GDEBUG("No handlers configured");
        }
    }
}

DBusHandlers*
dbus_handlers_new(
    GDBusConnection* connection,
    const char* config_dir)
{
    if (connection && config_dir) {
        DBusHandlers* self = g_new0(DBusHandlers, 1);

        g_object_ref(self->connection = connection);
        self->dir = g_strdup(config_dir);
        GDEBUG("Config dir %s", config_dir);
        return self;
    }
    return NULL;
}

void
dbus_handlers_free(
    DBusHandlers* self)
{
    if (self) {
        dbus_handlers_run_free(self->run);
        g_object_unref(self->connection);
        g_free(self->dir);
        g_free(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
