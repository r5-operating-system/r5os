/*
 * Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "ConnectionInterface.h"


#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/bluetooth_error.h>
#include <bluetooth/HCI/btHCI_transport.h>
#include <bluetooth/HCI/btHCI_event.h>

#define BT_DEBUG_THIS_MODULE
#include <btDebug.h>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <string.h>

#include <R5BluetoothLE.h>
// SPDX-SnippetEnd


int32 api_version = B_CUR_DRIVER_API_VERSION;


mutex sConnectionListLock = MUTEX_INITIALIZER("bt connection list");
DoublyLinkedList<HciConnection> sConnectionList;
net_buffer_module_info* gBufferModule = NULL;


inline bool
ExistConnectionByDestination(const bdaddr_t& destination, hci_id hid = -1)
{
	return ConnectionByDestination(destination, hid) != NULL;
}


inline bool
ExistConnectionByHandle(uint16 handle, hci_id hid)
{
	return ConnectionByHandle(handle, hid);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Bluetooth Low Energy links (docs/BLUETOOTH.md, step 6). The LE ACL
// buffer length of each radio, from the answer to LE Read Buffer Size,
// which bluetooth_server asks after every reset: 0 means the LE links share
// the BR/EDR buffers.
#define R5_LE_MAX_RADIOS		8
static uint16 sLEBufferLength[R5_LE_MAX_RADIOS];


static void
r5_le_command_complete(bluetooth_device* ndev, const uint8* data, size_t size)
{
	// Command Complete: commands (1), opcode (2), status (1), then the
	// LE ACL length (2) of LE Read Buffer Size, v1 0x2002 or v2 0x2060
	// (Core 6.0, Vol 4, Part E, 7.8.2).
	if (size < 6)
		return;
	const uint16 opcode = data[1] | (data[2] << 8);
	if (opcode != 0x2002 && opcode != 0x2060)
		return;
	if (data[3] != BT_OK || ndev->index < 0
		|| ndev->index >= R5_LE_MAX_RADIOS)
		return;
	sLEBufferLength[ndev->index] = data[4] | (data[5] << 8);
}


static void
r5_le_meta_event(bluetooth_device* ndev, const uint8* data, size_t size)
{
	// LE Connection Complete (0x01, 19 bytes with the subevent), LE
	// Enhanced Connection Complete v1 (0x0A, 31) and v2 (0x29, 34):
	// subevent, status, handle (2), role, peer address type, peer address
	// (6), ... (7.7.65.1 and 7.7.65.10).
	if (size < 1)
		return;
	size_t needed;
	switch (data[0]) {
		case 0x01:	needed = 19; break;
		case 0x0A:	needed = 31; break;
		case 0x29:	needed = 34; break;
		default:	return;
	}
	if (size < needed || data[1] != BT_OK)
		return;

	const uint16 handle = (data[2] | (data[3] << 8)) & 0x0FFF;
	bdaddr_t peer;
	memcpy(&peer, data + 6, sizeof(bdaddr_t));
	// 0x02 and 0x03 are a resolved identity address, public and random.
	const uint8 addressType = (data[5] & 0x01) != 0
		? R5_BDADDR_LE_RANDOM : R5_BDADDR_LE_PUBLIC;

	HciConnection* conn = AddConnection(handle, R5_HCI_LINK_LE, peer,
		ndev->index);
	if (conn == NULL) {
		ERROR("%s: no memory for LE link %#x\n", __func__, handle);
		return;
	}
	conn->ndevice = ndev;

	sockaddr_l2cap_le* destination = (sockaddr_l2cap_le*)&conn->address_dest;
	memset(destination, 0, sizeof(*destination));
	destination->l2cap_len = sizeof(sockaddr_l2cap_le);
	destination->l2cap_family = AF_BLUETOOTH;
	destination->l2cap_bdaddr = peer;
	destination->l2cap_bdaddr_type = addressType;

	// PostACL() splits LE packets by this length (0: the radio's own).
	conn->mtu = 0;
	if (ndev->index >= 0 && ndev->index < R5_LE_MAX_RADIOS)
		conn->mtu = sLEBufferLength[ndev->index];
	TRACE("%s: LE link handle=%#x\n", __func__, handle);
}
// SPDX-SnippetEnd


status_t
PostEvent(bluetooth_device* ndev, void* event, size_t size)
{
	struct hci_event_header* outgoingEvent = (struct hci_event_header*) event;
	status_t err;

	// Take actions on certain type of events.
	switch (outgoingEvent->ecode) {
		case HCI_EVENT_CONN_COMPLETE:
		{
			struct hci_ev_conn_complete* data
				= (struct hci_ev_conn_complete*)(outgoingEvent + 1);

			// TODO: XXX parse handle field
			if (data->status != BT_OK)
				break;

			HciConnection* conn = AddConnection(data->handle, BT_ACL,
				data->bdaddr, ndev->index);

			if (conn == NULL)
				panic("no mem for conn desc");
			conn->ndevice = ndev;
			TRACE("%s: Registered connection handle=%#x\n", __func__,
				data->handle);
			break;
		}

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		case 0x3E:
			// LE Meta: an LE link was made (step 6).
			if (size > sizeof(hci_event_header)) {
				r5_le_meta_event(ndev, (const uint8*)(outgoingEvent + 1),
					min_c(size - sizeof(hci_event_header),
						(size_t)outgoingEvent->elen));
			}
			break;

		case HCI_EVENT_CMD_COMPLETE:
			if (size > sizeof(hci_event_header)) {
				r5_le_command_complete(ndev,
					(const uint8*)(outgoingEvent + 1),
					min_c(size - sizeof(hci_event_header),
						(size_t)outgoingEvent->elen));
			}
			break;
		// SPDX-SnippetEnd

		case HCI_EVENT_DISCONNECTION_COMPLETE:
		{
			struct hci_ev_disconnection_complete_reply* data;

			data = (struct hci_ev_disconnection_complete_reply*)
				(outgoingEvent + 1);

			RemoveConnection(data->handle, ndev->index);
			TRACE("%s: unRegistered connection handle=%#x\n", __func__,
				data->handle);
			break;
		}

	}

	// forward to bluetooth server
	port_id port = find_port(BT_USERLAND_PORT_NAME);
	if (port != B_NAME_NOT_FOUND) {

		err = write_port_etc(port, PACK_PORTCODE(BT_EVENT, ndev->index, -1),
			event, size, B_TIMEOUT, 1 * 1000 * 1000);

		if (err != B_OK)
			ERROR("%s: Error posting userland %s\n", __func__, strerror(err));

	} else {
		ERROR("%s: bluetooth_server not found for posting!\n", __func__);
		err = B_NAME_NOT_FOUND;
	}

	return err;
}


static status_t
bcd_std_ops(int32 op, ...)
{
	status_t status;

	switch (op) {
		case B_MODULE_INIT:
			new (&sConnectionList) DoublyLinkedList<HciConnection>;

			status = get_module(NET_BUFFER_MODULE_NAME,
				(module_info **)&gBufferModule);
			if (status != B_OK)
				return status;

			return B_OK;

		case B_MODULE_UNINIT:
			put_module(NET_BUFFER_MODULE_NAME);
			return B_OK;
	}

	return B_ERROR;
}


bluetooth_core_data_module_info sBCDModule = {
	{
		BT_CORE_DATA_MODULE_NAME,
		0,
		bcd_std_ops
	},
	PostEvent,
	AddConnection,
	// RemoveConnection,
	RemoveConnection,

	RouteConnection,

	ConnectionByHandle,
	ConnectionByDestination,

	allocate_command_ident,
	lookup_command_ident,
	free_command_ident,
};


module_info* modules[] = {
	(module_info*)&sBCDModule,
	NULL
};
