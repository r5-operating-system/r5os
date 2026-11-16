#ifndef _BLUETOOTH_SERVER_PRIVATE_H
#define _BLUETOOTH_SERVER_PRIVATE_H


#define BLUETOOTH_SIGNATURE "application/x-vnd.Haiku-bluetooth_server"
#define BLUETOOTH_APP_SIGNATURE "application/x-vnd.Haiku-BluetoothPrefs"

/* Kit Comunication */
// Watching
#define BT_START_WATCHING_CONNECTIONS               'wtST'
#define BT_STOP_WATCHING_CONNECTIONS                'wtSP'

// LocalDevice
#define BT_MSG_COUNT_LOCAL_DEVICES		'btCd'
#define BT_MSG_ACQUIRE_LOCAL_DEVICE     'btAd'
#define BT_MSG_HANDLE_SIMPLE_REQUEST    'btsR'
#define BT_MSG_ADD_DEVICE               'btDD'
#define BT_MSG_REMOVE_DEVICE            'btrD'
#define BT_MSG_GET_PROPERTY             'btgP'
#define BT_MSG_GET_REMOTE_DEVICES       'btgD'
#define BT_MSG_NEW_REMOTE_DEVICE        'btnD'

// Discovery
#define BT_MSG_INQUIRY_STARTED          'IqSt'
#define BT_MSG_INQUIRY_COMPLETED        'IqCM'
#define BT_MSG_INQUIRY_TERMINATED       'IqTR'
#define BT_MSG_INQUIRY_ERROR            'IqER'
#define BT_MSG_INQUIRY_DEVICE           'IqDE'

// Pairing
#define BT_MSG_CONN_FAILED              'CnFL'
#define BT_MSG_CONN_COMPLETED           'CnCM'
#define BT_MSG_DISCONN_COMPLETED        'DcCM'

#define BT_REQ_CREATE_CONN              'rdCN'
#define BT_REQ_CANCEL_CONN              'rdCC'
#define BT_REQ_DISCONNECT               'rdDC'
#define BT_REQ_REMOVE_DEVICE            'rdRD'

#define BT_REQ_CONN_STATE               'rdCS'
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// RemoteDevice::GetConnectionDetails() (docs/BLUETOOTH.md, step 1b).
#define BT_REQ_CONN_DETAILS             'rdCD'
// SPDX-SnippetEnd

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// The radio switch of libr5bt.so (docs/BLUETOOTH.md, step 3a). HCI has no
// power off, so "off" means: inquiry scan and page scan are written off on
// every radio, and the server refuses scans and connections, outgoing and
// incoming, until the switch is on again. The choice is kept in
// bluetooth_server's settings file and survives a restart.
// Reply: bool "enabled", and int32 "hci_id" once per radio.
#define BT_MSG_R5_GET_RADIO             'r5rG'
// bool "enabled". Reply: int32 "status".
#define BT_MSG_R5_SET_RADIO             'r5rS'
// The end of a pairing started with Create Connection, sent to the
// sender of that request: data "bdaddr", bool "paired", string "name".
#define BT_MSG_R5_PAIR_RESULT           'r5pR'
// SPDX-SnippetEnd

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// bluetooth_audio (docs/BLUETOOTH.md, step 5). Only from uid 0, the
// media_addon_server. bool "streaming": while true, and while the sending
// team lives, the server refuses Inquiry and Periodic Inquiry, so a scan
// does not take radio time from the audio. Reply: int32 "status".
#define BT_MSG_R5_AUDIO_STREAMING       'r5aS'
// data "bdaddr" (bdaddr_t). Reply: bool "paired" (a link key is stored),
// bool "connected" and bool "encrypted" (the link is up and encrypted).
#define BT_MSG_R5_AUDIO_PAIRED          'r5aP'
// data "bdaddr": bring up the link to a paired headset (Create Connection,
// no pairing; the stored key authenticates it, encryption follows). Reply:
// int32 "status", B_NOT_ALLOWED for a device that is not paired.
#define BT_MSG_R5_AUDIO_CONNECT         'r5aC'
// SPDX-SnippetEnd


#endif
