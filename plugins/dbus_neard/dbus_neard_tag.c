/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2019 Open Mobile Platform LLC.
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

#include "dbus_neard.h"
#include "dbus_neard/org.neard.Tag.h"
#include "dbus_neard/org.neard.Record.h"

#include <nfc_ndef.h>
#include <nfc_tag.h>
#include <nfc_target.h>

#include <gutil_strv.h>
#include <gutil_misc.h>

enum {
    TAG_INITIALIZED,
    TAG_GONE,
    TAG_EVENT_COUNT
};

enum {
    NEARD_CALL_DEACTIVATE,
    NEARD_EVENT_COUNT
};

struct dbus_neard_tag {
    char* path;
    NfcTag* tag;
    GStrV* records;
    gulong tag_event_id[TAG_EVENT_COUNT];
    gulong neard_event_id[NEARD_EVENT_COUNT];
    OrgNeardTag* iface;
    GDBusObjectManagerServer* object_manager;
};

static
gboolean
dbus_neard_tag_export_record(
    DBusNeardTag* self,
    NfcNdefRec* rec,
    const char* path)
{
    OrgNeardRecord* iface = NULL;

    /* Set up D-Bus object */
    if (NFC_IS_NDEF_REC_U(rec)) {
        NfcNdefRecU* uri_rec = NFC_NDEF_REC_U(rec);

        GASSERT(rec->rtd == NFC_NDEF_RTD_URI);
        iface = org_neard_record_skeleton_new();
        org_neard_record_set_type_(iface, "URI");
        org_neard_record_set_uri(iface, uri_rec->uri);
    } else if (NFC_IS_NDEF_REC_T(rec)) {
        NfcNdefRecT* text_rec = NFC_NDEF_REC_T(rec);

        GASSERT(rec->rtd == NFC_NDEF_RTD_TEXT);
        iface = org_neard_record_skeleton_new();
        org_neard_record_set_type_(iface, "Text");
        org_neard_record_set_encoding(iface, "UTF-8");
        org_neard_record_set_representation(iface, text_rec->text);
        if (text_rec->lang && text_rec->lang[0]) {
            org_neard_record_set_language(iface, text_rec->lang);
        }
    } else if (NFC_IS_NDEF_REC_SP(rec)) {
        NfcNdefRecSp* sp_rec = NFC_NDEF_REC_SP(rec);

        GASSERT(rec->rtd == NFC_NDEF_RTD_TEXT);
        iface = org_neard_record_skeleton_new();
        org_neard_record_set_type_(iface, "SmartPoster");
        org_neard_record_set_uri(iface, sp_rec->uri);
        org_neard_record_set_encoding(iface, "UTF-8");
        if (sp_rec->title && sp_rec->title[0]) {
            org_neard_record_set_representation(iface, sp_rec->title);
            if (sp_rec->lang && sp_rec->lang[0]) {
                org_neard_record_set_language(iface, sp_rec->lang);
            }
        }
        if (sp_rec->type && sp_rec->type[0]) {
            org_neard_record_set_mimetype(iface, sp_rec->type);
        }
        if (sp_rec->size) {
            org_neard_record_set_size(iface, sp_rec->size);
        }
        switch (sp_rec->act) {
        case NFC_NDEF_SP_ACT_OPEN:
            org_neard_record_set_action(iface, "Do");
            break;
        case NFC_NDEF_SP_ACT_SAVE:
            org_neard_record_set_action(iface, "Save");
            break;
        case NFC_NDEF_SP_ACT_EDIT:
            org_neard_record_set_action(iface, "Edit");
            break;
        case NFC_NDEF_SP_ACT_DEFAULT:
            break;
        }
    }

    if (iface) {
        GDBusObjectSkeleton* obj = g_dbus_object_skeleton_new(path);

        /* Export it */
        g_dbus_object_skeleton_add_interface(obj,
            G_DBUS_INTERFACE_SKELETON(iface));
        g_dbus_object_manager_server_export(self->object_manager, obj);
        g_object_unref(iface);
        g_object_unref(obj);

        GDEBUG("Created neard D-Bus object for record %s", path);
        self->records = gutil_strv_add(self->records, path);
        return TRUE;
    }

    return FALSE;
}

static
void
dbus_neard_tag_export_records(
    DBusNeardTag* self)
{
    NfcNdefRec* rec = self->tag->ndef;

    if (rec) {
        int i = 0;
        GString* buf = g_string_append(g_string_new(self->path), "/record");
        const gsize baselen = buf->len;

        g_string_append_printf(buf, "%u", i);
        do {
            if (dbus_neard_tag_export_record(self, rec, buf->str)) {
                g_string_set_size(buf, baselen);
                g_string_append_printf(buf, "%u", ++i);
            }
            rec = rec->next;
        } while (rec);

        g_string_free(buf, TRUE);
    }
}

static
void
dbus_neard_tag_unexport(
    DBusNeardTag* self)
{
    if (self->object_manager) {
        if (self->records) {
            const GStrV* ptr = self->records;

            while (*ptr) {
                const char* path = *ptr++;

                GDEBUG("Removing neard D-Bus object for record %s", path);
                g_dbus_object_manager_server_unexport
                    (self->object_manager, path);
            }
            g_strfreev(self->records);
            self->records = NULL;
        }

        GDEBUG("Removing neard D-Bus object for tag %s", self->path);
        g_dbus_object_manager_server_unexport(self->object_manager, self->path);
        g_object_unref(self->object_manager);
        self->object_manager = NULL;
    }
}

static
void
dbus_neard_tag_initialized(
    NfcTag* tag,
    void* user_data)
{
    DBusNeardTag* self = user_data;

    /* This callbacks should only be invoked once, but just in case... */
    nfc_tag_remove_handlers(self->tag, self->tag_event_id + TAG_INITIALIZED, 1);
    dbus_neard_tag_export_records(self);
}

static
void
dbus_neard_tag_gone(
    NfcTag* tag,
    void* user_data)
{
    dbus_neard_tag_unexport((DBusNeardTag*)user_data);
}

static
gboolean
dbus_neard_tag_handle_deactivate(
    OrgNeardTag* iface,
    GDBusMethodInvocation* call,
    gpointer user_data)
{
    DBusNeardTag* self = user_data;

    GDEBUG("Deactivate %s", self->tag->name);
    nfc_tag_deactivate(self->tag);
    org_neard_tag_complete_deactivate(iface, call);
    return TRUE;
}

DBusNeardTag*
dbus_neard_tag_new(
    NfcTag* tag,
    const char* adapter_path,
    GDBusObjectManagerServer* object_manager)
{
    DBusNeardTag* self = g_new0(DBusNeardTag, 1);
    NfcTarget* target = tag->target;
    GDBusObjectSkeleton* object;
    const char* protocol = NULL;
    const DBusNeardProtocolName* type;

    self->path = g_strconcat(adapter_path, "/", tag->name, NULL);
    self->object_manager = g_object_ref(object_manager);
    self->tag = nfc_tag_ref(tag);
    self->iface = org_neard_tag_skeleton_new();

    object = g_dbus_object_skeleton_new(self->path);
    g_dbus_object_skeleton_add_interface(object,
        G_DBUS_INTERFACE_SKELETON(self->iface));

    self->neard_event_id[NEARD_CALL_DEACTIVATE] =
        g_signal_connect(self->iface, "handle-deactivate",
        G_CALLBACK(dbus_neard_tag_handle_deactivate), self);

    switch (tag->type) {
    case NFC_TAG_TYPE_FELICA:
        protocol = NEARD_PROTOCOL_FELICA;
        break;
    case NFC_TAG_TYPE_MIFARE_CLASSIC:
    case NFC_TAG_TYPE_MIFARE_ULTRALIGHT:
        protocol = NEARD_PROTOCOL_MIFARE;
        break;
    default:
        switch (target->protocol) {
        case NFC_PROTOCOL_T4A_TAG:
        case NFC_PROTOCOL_T4B_TAG:
            protocol = NEARD_PROTOCOL_ISO_DEP;
            break;
        case NFC_PROTOCOL_NFC_DEP:
            protocol = NEARD_PROTOCOL_NFC_DEP;
            break;
        default:
            break;
        }
    }

    if (protocol) {
        org_neard_tag_set_protocol(self->iface, protocol);
    }
    type = dbus_neard_tag_type_name(target->protocol);
    if (type) {
        org_neard_tag_set_type_(self->iface, type->name);
    }
    org_neard_tag_set_adapter(self->iface, adapter_path);
    org_neard_tag_set_read_only(self->iface, TRUE);
    g_dbus_object_manager_server_export(object_manager, object);
    g_object_unref(object);

    GDEBUG("Created neard D-Bus object for tag %s", self->path);

    /* Export records now or wait until tag gets initialized */
    if (tag->flags & NFC_TAG_FLAG_INITIALIZED) {
        dbus_neard_tag_export_records(self);
    } else {
        self->tag_event_id[TAG_INITIALIZED] =
            nfc_tag_add_initialized_handler(tag,
                dbus_neard_tag_initialized, self);
    }
    self->tag_event_id[TAG_GONE] = nfc_tag_add_gone_handler(tag,
        dbus_neard_tag_gone, self);

    return self;
}

void
dbus_neard_tag_free(
    DBusNeardTag* self)
{
    dbus_neard_tag_unexport(self);
    gutil_disconnect_handlers(self->iface, self->neard_event_id,
        G_N_ELEMENTS(self->neard_event_id));
    g_object_unref(self->iface);

    nfc_tag_remove_all_handlers(self->tag, self->tag_event_id);
    nfc_tag_unref(self->tag);
    g_free(self->path);
    g_free(self);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
