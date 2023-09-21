/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2022 Jolla Ltd.
 * Copyright (C) 2020 Open Mobile Platform LLC.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#ifndef NFC_TYPES_H
#define NFC_TYPES_H

#include <gutil_types.h>

G_BEGIN_DECLS

/* Types */

typedef struct nfc_adapter NfcAdapter;
typedef struct nfc_configurable NfcConfigurable;        /* Since 1.1.10 */
typedef struct nfc_initiator NfcInitiator;              /* Since 1.1.0 */
typedef struct nfc_language NfcLanguage;                /* Since 1.0.15 */
typedef struct nfc_peer_connection NfcPeerConnection;   /* Since 1.1.0 */
typedef struct nfc_peer_service NfcPeerService;         /* Since 1.1.0 */
typedef struct nfc_peer_socket NfcPeerSocket;           /* Since 1.1.0 */
typedef struct nfc_ndef_rec NfcNdefRec;
typedef struct nfc_manager NfcManager;
typedef struct nfc_peer NfcPeer;         /* Since 1.1.0 */
typedef struct nfc_plugin NfcPlugin;
typedef struct nfc_plugin_desc NfcPluginDesc;
typedef struct nfc_tag NfcTag;
typedef struct nfc_tag_t2 NfcTagType2;
typedef struct nfc_tag_t4 NfcTagType4;   /* Since 1.0.20 */
typedef struct nfc_tag_t4a NfcTagType4a; /* Since 1.0.20 */
typedef struct nfc_tag_t4b NfcTagType4b; /* Since 1.0.20 */
typedef struct nfc_target NfcTarget;
typedef struct nfc_target_sequence NfcTargetSequence;

typedef struct nfc_param_listen_a NfcParamListenA;  /* Since 1.1.0 */
typedef struct nfc_param_iso_dep_poll_a NfcParamIsoDepPollA; /* Since 1.0.20 */
typedef struct nfc_param_iso_dep_poll_b NfcParamIsoDepPollB; /* Since 1.0.20 */
typedef struct nfc_param_nfc_dep_initator NfcParamNfcDepInitiator; /* 1.1.0 */
typedef struct nfc_param_nfc_dep_target NfcParamNfcDepTarget; /* 1.1.0 */

/* Constants */

typedef enum nfc_mode {
    NFC_MODE_NONE           = 0x00,
    /* Polling */
    NFC_MODE_P2P_INITIATOR  = 0x01,
    NFC_MODE_READER_WRITER  = 0x02,
    /* Listening */
    NFC_MODE_P2P_TARGET     = 0x04,
    NFC_MODE_CARD_EMULATION = 0x08
} NFC_MODE;

/* This typo was fixed in 1.1.16 */
#define NFC_MODE_CARD_EMILATION NFC_MODE_CARD_EMULATION

/* Combined modes (since 1.1.0) */
#define NFC_MODES_P2P (NFC_MODE_P2P_INITIATOR | NFC_MODE_P2P_TARGET)
#define NFC_MODES_ALL (NFC_MODE_P2P_INITIATOR | NFC_MODE_P2P_TARGET | \
    NFC_MODE_READER_WRITER | NFC_MODE_CARD_EMULATION)

typedef enum nfc_technology {
    NFC_TECHNOLOGY_UNKNOWN = 0x00,
    NFC_TECHNOLOGY_A = 0x01,       /* NFC-A */
    NFC_TECHNOLOGY_B = 0x02,       /* NFC-B */
    NFC_TECHNOLOGY_F = 0x04        /* NFC-F */
} NFC_TECHNOLOGY;

typedef enum nfc_protocol {
    NFC_PROTOCOL_UNKNOWN = 0x00,
    NFC_PROTOCOL_T1_TAG  = 0x01,   /* Type 1 Tag */
    NFC_PROTOCOL_T2_TAG  = 0x02,   /* Type 2 Tag */
    NFC_PROTOCOL_T3_TAG  = 0x04,   /* Type 3 Tag */
    NFC_PROTOCOL_T4A_TAG = 0x08,   /* Type 4A Tag (ISO-DEP, ISO 14443) */
    NFC_PROTOCOL_T4B_TAG = 0x10,   /* Type 4B Tag,(ISO-DEP, ISO 14443) */
    NFC_PROTOCOL_NFC_DEP = 0x20    /* NFC-DEP Protocol (ISO 18092) */
} NFC_PROTOCOL;

typedef enum nfc_tag_type {
    NFC_TAG_TYPE_UNKNOWN           = 0x00,
    NFC_TAG_TYPE_FELICA            = 0x01,
    NFC_TAG_TYPE_MIFARE_CLASSIC    = 0x02,
    NFC_TAG_TYPE_MIFARE_ULTRALIGHT = 0x04
} NFC_TAG_TYPE;

typedef enum nfc_transmit_status {
    NFC_TRANSMIT_STATUS_OK,        /* Successful transmission */
    NFC_TRANSMIT_STATUS_ERROR,     /* Generic error */
    NFC_TRANSMIT_STATUS_NACK,      /* NACK received */
    NFC_TRANSMIT_STATUS_CORRUPTED, /* CRC mismatch etc. */
    NFC_TRANSMIT_STATUS_TIMEOUT    /* No response from NFCC */
} NFC_TRANSMIT_STATUS;

typedef enum nfc_peer_connect_result {
    NFC_PEER_CONNECT_OK,           /* Connection was successful */
    NFC_PEER_CONNECT_DUP,          /* Duplicate connection */
    NFC_PEER_CONNECT_CANCELLED,    /* Connection cancelled */
    NFC_PEER_CONNECT_NO_SERVICE,   /* Service not found */
    NFC_PEER_CONNECT_REJECTED,     /* Connection rejected */
    NFC_PEER_CONNECT_FAILED        /* I/O or protocol error */
} NFC_PEER_CONNECT_RESULT;

typedef enum nfc_llcp_version {
    NFC_LLCP_VERSION_1_0 = 0x10,
    NFC_LLCP_VERSION_1_1 = 0x11,
    NFC_LLCP_VERSION_1_2 = 0x12
} NFC_LLCP_VERSION; /* Since 1.1.1 */

/* RF technology specific parameters */

typedef struct nfc_param_poll_a {
    guint8 sel_res;      /* (SAK)*/
    GUtilData nfcid1;
} NfcParamPollA, /* Since 1.0.8 */
  NfcTagParamT2; /* This one for backward compatibility */

typedef struct nfc_param_poll_b {
    guint fsc;          /* FSC (FSCI converted to bytes) */
    GUtilData nfcid0;
    /* Since 1.0.40 */
    /*
    * NFCForum-TS-DigitalProtocol-1.0
    * Table 25: SENSB_RES Format
    */
    guint8 app_data[4];
    GUtilData prot_info;
} NfcParamPollB; /* Since 1.0.20 */

typedef struct nfc_param_poll_f {
    guint bitrate;      /* In kbps, zero if unknown */
    GUtilData nfcid2;   /* Bytes 2-9 of SENSF_RES */
} NfcParamPollF; /* Since 1.1.0 */

typedef struct nfc_param_listen_f {
    GUtilData nfcid2;   /* NFCID2 generated by the Local NFCC */
} NfcParamListenF; /* Since 1.1.0 */

typedef union nfc_param_poll {
    NfcParamPollA a;
    NfcParamPollB b;
    NfcParamPollF f;
} NfcParamPoll; /* Since 1.0.33 */

/* Mark functions exported to plugins as weak */
#ifndef NFCD_EXPORT
#  define NFCD_EXPORT __attribute__((weak))
#endif

/* Logging */
#define NFC_CORE_LOG_MODULE nfc_core_log
#define NFC_LLC_LOG_MODULE nfc_llc_log
#define NFC_PEER_LOG_MODULE nfc_peer_log
#define NFC_SNEP_LOG_MODULE nfc_snep_log
extern GLogModule NFC_CORE_LOG_MODULE NFCD_EXPORT;
extern GLogModule NFC_LLC_LOG_MODULE NFCD_EXPORT;  /* Since 1.1.0 */
extern GLogModule NFC_PEER_LOG_MODULE NFCD_EXPORT; /* Since 1.1.0 */
extern GLogModule NFC_SNEP_LOG_MODULE NFCD_EXPORT; /* Since 1.1.0 */

G_END_DECLS

#endif /* NFC_TYPES_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
