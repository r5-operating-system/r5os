/*
 * Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 */

#include "BluetoothServer.h"

#include "CommandManager.h"
#include "Debug.h"
#include "LocalDeviceImpl.h"
#include "bluetooth/HCI/btHCI_command.h"

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/bluetooth_error.h>
#include <bluetooth/LinkKeyUtils.h>
#include <bluetooth/HCI/btHCI_event.h>

#include <ConnectionIncoming.h>
#include <PincodeWindow.h>

#include <FindDirectory.h>
#include <stdio.h>
#include <string.h>
#include <new>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <File.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "PasskeyWindow.h"
// SPDX-SnippetEnd


#if 0
#pragma mark - Class methods -
#endif


// Factory methods
LocalDeviceImpl*
LocalDeviceImpl::CreateControllerAccessor(BPath* path)
{
	HCIDelegate* delegate = new(std::nothrow) HCIControllerAccessor(path);
	if (delegate == NULL)
		return NULL;

	LocalDeviceImpl* device = new(std::nothrow) LocalDeviceImpl(delegate);
	if (device == NULL) {
		delete delegate;
		return NULL;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	device->fPath = path->Path();
	// SPDX-SnippetEnd

	return device;
}


LocalDeviceImpl*
LocalDeviceImpl::CreateTransportAccessor(BPath* path)
{
	HCIDelegate* delegate = new(std::nothrow) HCITransportAccessor(path);
	if (delegate == NULL)
		return NULL;

	LocalDeviceImpl* device = new(std::nothrow) LocalDeviceImpl(delegate);
	if (device == NULL) {
		delete delegate;
		return NULL;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	device->fPath = path->Path();
	// SPDX-SnippetEnd

	return device;
}


LocalDeviceImpl::LocalDeviceImpl(HCIDelegate* hd) : LocalDeviceHandler(hd)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	memset(fFeatures, 0, sizeof(fFeatures));
	fHasSSP = false;
	memset(&fPasskeyAddress, 0, sizeof(fPasskeyAddress));
	fPasskeyUsed = false;
	// SPDX-SnippetEnd
	LoadRemoteDevices();
}


LocalDeviceImpl::~LocalDeviceImpl()
{
	SaveRemoteDevices();
}


void
LocalDeviceImpl::Unregister()
{
	BMessage* msg =	new	BMessage(BT_MSG_REMOVE_DEVICE);

	msg->AddInt32("hci_id", fHCIDelegate->Id());

	TRACE_BT("LocalDeviceImpl: Unregistering %" B_PRId32 "\n",
		fHCIDelegate->Id());

	be_app_messenger.SendMessage(msg);
}


void
LocalDeviceImpl::SaveRemoteDevices()
{
	BMessage devices;

	for (int32 i = 0; i < fRemoteDevicesList.CountItems(); i++) {
		ServerRemoteDevice* rd = fRemoteDevicesList.ItemAt(i);
		BMessage device;
		bdaddr_t bdaddr = rd->bdaddr;
		linkkey_t link_key = rd->link_key;
		device.AddData("bdaddr", B_ANY_TYPE, &bdaddr, sizeof(bdaddr_t));
		device.AddString("name", rd->friendly_name);
		device.AddUInt16("clock_offset", rd->clock_offset);
		device.AddUInt8("pscan_rep_mode", rd->pscan_rep_mode);
		device.AddData("class_of_device", B_RAW_TYPE, rd->classOfDevice,
			sizeof(rd->classOfDevice));
		device.AddData("link key", B_ANY_TYPE, &link_key, sizeof(linkkey_t));
		device.AddUInt8("link type", rd->link_type);

		devices.AddMessage("remote", &device);
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The file holds every link key. It was created with the default mode
	// (0644 under umask 022), readable by every account that can reach the
	// directory (review of #4286). It is now 0600 before anything is
	// written, and replaced through a rename so a crash never leaves half a
	// file. Since the server is a system service (uid 0) the file is in its
	// own 0700 directory, shared by everyone who signs in, not in the
	// settings of whoever happened to start the server.
	BPath path;
	if (r5_bluetooth_settings_path(path, true) != B_OK)
		return;
	BPath temporary(path);
	if (path.Append("paired_devices") != B_OK
		|| temporary.Append("paired_devices.new") != B_OK)
		return;

	BFile file(temporary.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK
		|| file.SetPermissions(S_IRUSR | S_IWUSR) != B_OK
		|| devices.Flatten(&file) != B_OK) {
		file.Unset();
		unlink(temporary.Path());
		return;
	}
	file.Sync();
	file.Unset();
	if (rename(temporary.Path(), path.Path()) != 0) {
		unlink(temporary.Path());
		return;
	}
	// SPDX-SnippetEnd
}


void
LocalDeviceImpl::LoadRemoteDevices()
{
	BPath path;
	BMessage devices;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The same file as SaveRemoteDevices(), in the server's 0700 directory.
	if (r5_bluetooth_settings_path(path, false) == B_OK
		&& path.Append("paired_devices") == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck()==B_OK)
			devices.Unflatten(&file);
	}
	// SPDX-SnippetEnd

	BMessage device;
	for (int32 i = 0; devices.FindMessage("remote", i, &device) == B_OK; i++) {
		ssize_t size;
		bdaddr_t* bdaddr;
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// A damaged settings file skips the entry instead of crashing.
		if (device.FindData("bdaddr", B_ANY_TYPE, (const void**)&bdaddr,
				&size) != B_OK || size != (ssize_t)sizeof(bdaddr_t))
			continue;
		ServerRemoteDevice* rd = new ServerRemoteDevice();
		// SPDX-SnippetEnd
		rd->bdaddr = *bdaddr;
		device.FindString("name", &rd->friendly_name);
		device.FindUInt16("clock_offset", &rd->clock_offset);
		device.FindUInt8("pscan_rep_mode", &rd->pscan_rep_mode);
		device.FindUInt8("class_of_device", 0, &rd->classOfDevice[0]);
		device.FindUInt8("class_of_device", 1, &rd->classOfDevice[1]);
		device.FindUInt8("class_of_device", 2, &rd->classOfDevice[2]);
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// FindData() hands out a pointer; it was written into the key itself,
		// so every saved link key came back as garbage.
		const void* key;
		if (device.FindData("link key", B_ANY_TYPE, &key, &size) == B_OK
			&& size == (ssize_t)sizeof(linkkey_t))
			memcpy(&rd->link_key, key, sizeof(linkkey_t));
		else
			rd->link_key = LinkKeyUtils::NullKey();
		// SPDX-SnippetEnd
		device.FindUInt8("link type", &rd->link_type);
		rd->conn_state = RemoteDevice::DISCONNECTED;

		fRemoteDevicesList.AddItem(rd);
	}
}


ServerRemoteDevice*
LocalDeviceImpl::RemoteDeviceByAddr(bdaddr_t bdaddr)
{
	for (int32 i = 0; i < fRemoteDevicesList.CountItems(); i++) {
		ServerRemoteDevice* rd = fRemoteDevicesList.ItemAt(i);
		if (bdaddrUtils::Compare(rd->bdaddr, bdaddr))
			return rd;
	}
	return NULL;
}


ServerRemoteDevice*
LocalDeviceImpl::RemoteDeviceByHandle(uint16 handle)
{
	for (int32 i = 0; i < fRemoteDevicesList.CountItems(); i++) {
		ServerRemoteDevice* rd = fRemoteDevicesList.ItemAt(i);
		if (rd->handle == handle)
			return rd;
	}
	return NULL;
}


void
LocalDeviceImpl::AddRemoteDevice(ServerRemoteDevice* rd)
{
	fRemoteDevicesList.AddItem(rd);
}


void
LocalDeviceImpl::RemoveRemoteDevice(ServerRemoteDevice* rd)
{
	fRemoteDevicesList.RemoveItem(rd);
}


RemoteDevicesList*
LocalDeviceImpl::GetRemoteDevicesList()
{
	return &fRemoteDevicesList;
}


#if 0
#pragma mark - Event handling methods -
#endif

template<typename T, typename Header>
inline T*
JumpEventHeader(Header* event)
{
	return (T*)(event + 1);
}


void
LocalDeviceImpl::HandleUnexpectedEvent(struct hci_event_header* event)
{
	// Events here might have not been initated by us
	// TODO: ML mark as handled pass a reply by parameter and reply in common
	switch (event->ecode) {
		case HCI_EVENT_HARDWARE_ERROR:
			HardwareError(
				JumpEventHeader<struct hci_ev_hardware_error>(event));
			break;

		case HCI_EVENT_CONN_REQUEST:
			ConnectionRequest(
				JumpEventHeader<struct hci_ev_conn_request>(event), NULL);
			break;

		case HCI_EVENT_NUM_COMP_PKTS:
			NumberOfCompletedPackets(
				JumpEventHeader<struct hci_ev_num_comp_pkts>(event));
			break;

		case HCI_EVENT_DISCONNECTION_COMPLETE:
			DisconnectionComplete(
				JumpEventHeader<struct hci_ev_disconnection_complete_reply>(event));
			break;

		case HCI_EVENT_CONN_COMPLETE:
			ConnectionComplete(JumpEventHeader<struct hci_ev_conn_complete>(event));
			break;

		case HCI_EVENT_ENCRYPT_CHANGE:
			EncryptChange(JumpEventHeader<struct hci_ev_encrypt_change>(event));
			break;

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Secure Simple Pairing that no petition waited for, for example
		// a second pairing on the same link or one the remote started.
		// Dropped before, so that pairing hung until it timed out.
		case HCI_EVENT_IO_CAPABILITY_REQUEST:
			IOCapabilityRequest(
				JumpEventHeader<struct hci_ev_io_capability_request>(event),
				NULL);
			break;

		case HCI_EVENT_USER_CONFIRMATION_REQUEST:
			UserConfirmationRequest(
				JumpEventHeader<struct hci_ev_user_confirmation_request>(event),
				NULL);
			break;

		case HCI_EVENT_USER_PASSKEY_REQUEST:
			UserPasskeyRequest(
				JumpEventHeader<struct r5_hci_ev_user_passkey_request>(event),
				NULL);
			break;

		case R5_HCI_EVENT_USER_PASSKEY_NOTIFICATION:
			UserPasskeyNotification(JumpEventHeader
				<struct r5_hci_ev_user_passkey_notification>(event), NULL);
			break;

		case R5_HCI_EVENT_KEYPRESS_NOTIFICATION:
			KeypressNotification(
				JumpEventHeader<struct r5_hci_ev_keypress_notification>(event),
				NULL);
			break;

		case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
			SimplePairingComplete(
				JumpEventHeader<struct hci_ev_simple_pairing_complete>(event),
				NULL);
			break;
		// SPDX-SnippetEnd

		default:
			TRACE_BT("Couldn't handle the unexpected event with code: %x", event->ecode);
			break;
	}
}


void
LocalDeviceImpl::HandleExpectedRequest(struct hci_event_header* event,
	BMessage* request)
{
	// we are waiting for a reply
	switch (event->ecode) {
		case HCI_EVENT_INQUIRY_COMPLETE:
			InquiryComplete(JumpEventHeader<uint8>(event), request);
			break;

		case HCI_EVENT_INQUIRY_RESULT:
			InquiryResult(JumpEventHeader<uint8>(event), request);
			break;

		case HCI_EVENT_AUTH_COMPLETE:
			AuthComplete(JumpEventHeader<struct hci_ev_auth_complete>(event), request);
			break;

		case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
			RemoteNameRequestComplete(
				JumpEventHeader
					<struct hci_ev_remote_name_request_complete_reply>
				(event), request);
			break;

		case HCI_EVENT_CHANGE_CONN_LINK_KEY_COMPLETE:
			break;

		case HCI_EVENT_MASTER_LINK_KEY_COMPL:
			break;

		case HCI_EVENT_RMT_FEATURES:
			break;

		case HCI_EVENT_RMT_VERSION:
			break;

		case HCI_EVENT_QOS_SETUP_COMPLETE:
			break;

		case HCI_EVENT_FLUSH_OCCUR:
			break;

		case HCI_EVENT_ROLE_CHANGE:
			RoleChange(
				JumpEventHeader<struct hci_ev_role_change>(event), request);
			break;

		case HCI_EVENT_MODE_CHANGE:
			break;

		case HCI_EVENT_RETURN_LINK_KEYS:
			ReturnLinkKeys(
				JumpEventHeader<struct hci_ev_return_link_keys>(event));
			break;

		case HCI_EVENT_LINK_KEY_REQ:
			LinkKeyRequested(
				JumpEventHeader<struct hci_ev_link_key_req>(event), request);
			break;

		case HCI_EVENT_LINK_KEY_NOTIFY:
			LinkKeyNotify(
				JumpEventHeader<struct hci_ev_link_key_notify>(event), request);
			break;

		case HCI_EVENT_LOOPBACK_COMMAND:
			break;

		case HCI_EVENT_DATA_BUFFER_OVERFLOW:
			break;

		case HCI_EVENT_MAX_SLOT_CHANGE:
			MaxSlotChange(JumpEventHeader<struct hci_ev_max_slot_change>(event),
			request);
			break;

		case HCI_EVENT_READ_CLOCK_OFFSET_COMPL:
			break;

		case HCI_EVENT_CON_PKT_TYPE_CHANGED:
			break;

		case HCI_EVENT_QOS_VIOLATION:
			break;

		case HCI_EVENT_PAGE_SCAN_REP_MODE_CHANGE:
			PageScanRepetitionModeChange(
				JumpEventHeader<struct hci_ev_page_scan_rep_mode_change>(event),
				request);
			break;

		case HCI_EVENT_FLOW_SPECIFICATION:
			break;

		case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
			InquiryResultWithRSSI(JumpEventHeader<uint8>(event), request);
			break;
		case HCI_EVENT_EXTENDED_INQUIRY_RESULT:
			ExtendedInquiryResult(JumpEventHeader<uint8>(event), request);
			break;

		case HCI_EVENT_REMOTE_EXTENDED_FEATURES:
			break;

		case HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETED:
			break;

		case HCI_EVENT_SYNCHRONOUS_CONNECTION_CHANGED:
			break;

		case HCI_EVENT_IO_CAPABILITY_REQUEST:
			IOCapabilityRequest(JumpEventHeader<struct hci_ev_io_capability_request>(event),
				request);
			break;

		case HCI_EVENT_IO_CAPABILITY_RESPONSE:
			IOCapabilityResponse(JumpEventHeader<struct hci_ev_io_capability_response>(event),
				request);
			break;

		case HCI_EVENT_USER_CONFIRMATION_REQUEST:
			UserConfirmationRequest(JumpEventHeader<struct hci_ev_user_confirmation_request>(event),
				request);
			break;

		case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
			SimplePairingComplete(JumpEventHeader<struct hci_ev_simple_pairing_complete>(event),
				request);
			break;

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		case HCI_EVENT_USER_PASSKEY_REQUEST:
			UserPasskeyRequest(
				JumpEventHeader<struct r5_hci_ev_user_passkey_request>(event),
				request);
			break;

		case R5_HCI_EVENT_USER_PASSKEY_NOTIFICATION:
			UserPasskeyNotification(JumpEventHeader
				<struct r5_hci_ev_user_passkey_notification>(event), request);
			break;

		case R5_HCI_EVENT_KEYPRESS_NOTIFICATION:
			KeypressNotification(
				JumpEventHeader<struct r5_hci_ev_keypress_notification>(event),
				request);
			break;
		// SPDX-SnippetEnd

		case HCI_EVENT_PIN_CODE_REQ:
			PinCodeRequest(JumpEventHeader<struct hci_ev_pin_code_req>(event), NULL);
			break;
	}
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*static*/ bool
LocalDeviceImpl::_EventFits(const struct hci_event_header* event, size_t size)
{
	if (event == NULL || size < sizeof(*event)
		|| size - sizeof(*event) < event->elen)
		return false;

	// Fixed-size events that the pairing code reads (7.7.40 to 7.7.49).
	size_t minimum = 0;
	switch (event->ecode) {
		case HCI_EVENT_IO_CAPABILITY_REQUEST:
			minimum = sizeof(struct hci_ev_io_capability_request);
			break;
		case HCI_EVENT_IO_CAPABILITY_RESPONSE:
			minimum = sizeof(struct hci_ev_io_capability_response);
			break;
		case HCI_EVENT_USER_CONFIRMATION_REQUEST:
			minimum = sizeof(struct hci_ev_user_confirmation_request);
			break;
		case HCI_EVENT_USER_PASSKEY_REQUEST:
			minimum = sizeof(struct r5_hci_ev_user_passkey_request);
			break;
		case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
			minimum = sizeof(struct hci_ev_simple_pairing_complete);
			break;
		case R5_HCI_EVENT_USER_PASSKEY_NOTIFICATION:
			minimum = sizeof(struct r5_hci_ev_user_passkey_notification);
			break;
		case R5_HCI_EVENT_KEYPRESS_NOTIFICATION:
			minimum = sizeof(struct r5_hci_ev_keypress_notification);
			break;
	}
	if (event->elen < minimum)
		return false;

	// Events that carry a count of records (Core 6.0, Vol 4, Part E, 7.7):
	// the count and every record must lie within the event.
	const uint8* data = (const uint8*)(event + 1);
	size_t recordSize;
	switch (event->ecode) {
		case HCI_EVENT_INQUIRY_RESULT:
			// BD_ADDR, Page_Scan_Repetition_Mode, two reserved bytes,
			// Class_Of_Device, Clock_Offset (7.7.2)
			recordSize = 6 + 1 + 2 + 3 + 2;
			break;
		case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
			// BD_ADDR, Page_Scan_Repetition_Mode, reserved, Class_Of_Device,
			// Clock_Offset, RSSI (7.7.33)
			recordSize = 6 + 1 + 1 + 3 + 2 + 1;
			break;
		case HCI_EVENT_EXTENDED_INQUIRY_RESULT:
			recordSize = sizeof(hci_ev_extended_inquiry_info);
			break;
		case HCI_EVENT_RETURN_LINK_KEYS:
			// BD_ADDR, Link_Key (7.7.21)
			recordSize = 6 + 16;
			break;
		case HCI_EVENT_NUM_COMP_PKTS:
			// Connection_Handle, Num_Completed_Packets (7.7.19)
			recordSize = 2 + 2;
			break;
		default:
			return true;
	}

	if (event->elen < 1)
		return false;
	return 1 + (size_t)data[0] * recordSize <= event->elen;
}
// SPDX-SnippetEnd


void
LocalDeviceImpl::HandleEvent(struct hci_event_header* event, size_t size)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The radio's own lengths and counts were trusted, so an Inquiry Result
	// with 255 responses read 14 * 255 bytes from a 260-byte buffer (review
	// of #4286). Skip an event that does not fit what was received.
	if (!_EventFits(event, size)) {
		syslog(LOG_WARNING, "bluetooth_server: dropped event %#x: %"
			B_PRIuSIZE " bytes do not hold what it declares",
			size >= sizeof(*event) ? event->ecode : 0, size);
		return;
	}
	// SPDX-SnippetEnd

/*
	printf("### Incoming event: len = %d\n", event->elen);
	for (int16 index = 0; index < event->elen + 2; index++) {
		printf("%x:", ((uint8*)event)[index]);
	}
	printf("### \n");
*/
	BMessage* request = NULL;
	int32 eventIndexLocation;

	// Check if it is a requested one
	switch (event->ecode) {
		case HCI_EVENT_CMD_COMPLETE:
		{
			struct hci_ev_cmd_complete* commandComplete
				= JumpEventHeader<struct hci_ev_cmd_complete>(event);

			TRACE_BT("LocalDeviceImpl: Incoming CommandComplete(%d) for %s\n", commandComplete->ncmd,
				BluetoothCommandOpcode(commandComplete->opcode));

			request = FindPetition(event->ecode, commandComplete->opcode,
				&eventIndexLocation);

			if (request != NULL)
				CommandComplete(commandComplete, request, eventIndexLocation);

			break;
		}
		case HCI_EVENT_CMD_STATUS:
		{
			struct hci_ev_cmd_status* commandStatus
				= JumpEventHeader<struct hci_ev_cmd_status>(event);

			TRACE_BT("LocalDeviceImpl: Incoming CommandStatus(%d)(%s) for %s\n", commandStatus->ncmd,
				BluetoothError(commandStatus->status),
				BluetoothCommandOpcode(commandStatus->opcode));

			request = FindPetition(event->ecode, commandStatus->opcode,
				&eventIndexLocation);
			if (request != NULL)
				CommandStatus(commandStatus, request, eventIndexLocation);
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			else if (commandStatus->status != BT_OK) {
				// A command that waits for Command Complete was refused with
				// Command Status instead: unknown to this radio (Core 6.0,
				// Vol 4, Part E, 4.5), or the 2 s timeout of h2generic. The
				// one who asked waited forever (the kit has no timeout), and
				// on the 5625U the Settings page hung on "Starting
				// Bluetooth". Answer with the error.
				request = FindPetition(HCI_EVENT_CMD_COMPLETE,
					commandStatus->opcode, &eventIndexLocation);
				if (request != NULL)
					_RefuseRequest(request, commandStatus->opcode,
						commandStatus->status);
			}
			// SPDX-SnippetEnd

			break;
		}
		default:
			TRACE_BT("LocalDeviceImpl: Incoming %s event\n", BluetoothEvent(event->ecode));

			request = FindPetition(event->ecode);
			if (request != NULL)
				HandleExpectedRequest(event, request);

			break;
	}

	if (request == NULL) {
		TRACE_BT("LocalDeviceImpl: Event %s could not be understood or delivered\n",
			BluetoothEvent(event->ecode));
		HandleUnexpectedEvent(event);
	}
}


#if 0
#pragma mark -
#endif


void
LocalDeviceImpl::CommandComplete(struct hci_ev_cmd_complete* event,
	BMessage* request, int32 index)
{
	int16 opcodeExpected;
	BMessage reply;
	status_t status;

	// Handle command complete information
	request->FindInt16("opcodeExpected", index, &opcodeExpected);

	if (request->IsSourceWaiting() == false) {
		TRACE_BT("LocalDeviceImpl: Nobody waiting for the event\n");
	}

	switch ((uint16)opcodeExpected) {

		case PACK_OPCODE(OGF_INFORMATIONAL_PARAM, OCF_READ_LOCAL_VERSION):
		{
			struct hci_rp_read_loc_version* version
				= JumpEventHeader<struct hci_rp_read_loc_version,
				struct hci_ev_cmd_complete>(event);


			if (version->status == BT_OK) {

				if (!IsPropertyAvailable("hci_version"))
					fProperties->AddInt8("hci_version", version->hci_version);

				if (!IsPropertyAvailable("hci_revision")) {
					fProperties->AddInt16("hci_revision",
						version->hci_revision);
				}

				if (!IsPropertyAvailable("lmp_version"))
					fProperties->AddInt8("lmp_version", version->lmp_version);

				if (!IsPropertyAvailable("lmp_subversion")) {
					fProperties->AddInt16("lmp_subversion",
						version->lmp_subversion);
				}

				if (!IsPropertyAvailable("manufacturer")) {
					fProperties->AddInt16("manufacturer",
						version->manufacturer);
				}
			}

			TRACE_BT("LocalDeviceImpl: Reply for Local Version %x\n", version->status);

			reply.AddUInt8("status", version->status);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		case  PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_READ_PG_TIMEOUT):
		{
			struct hci_rp_read_page_timeout* pageTimeout
				= JumpEventHeader<struct hci_rp_read_page_timeout,
				struct hci_ev_cmd_complete>(event);

			if (pageTimeout->status == BT_OK) {
				fProperties->AddInt16("page_timeout",
					pageTimeout->page_timeout);

				TRACE_BT("LocalDeviceImpl: Page Timeout=%x\n", pageTimeout->page_timeout);
			}

			reply.AddUInt8("status", pageTimeout->status);
			reply.AddInt32("result", pageTimeout->page_timeout);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_INFORMATIONAL_PARAM, OCF_READ_LOCAL_FEATURES):
		{
			struct hci_rp_read_loc_features* features
				= JumpEventHeader<struct hci_rp_read_loc_features,
				struct hci_ev_cmd_complete>(event);

			if (features->status == BT_OK) {

				if (!IsPropertyAvailable("features")) {
					fProperties->AddData("features", B_ANY_TYPE,
						&features->features, 8);

					uint16 packetType = HCI_DM1 | HCI_DH1 | HCI_HV1;

					bool roleSwitch
						= (features->features[0] & LMP_RSWITCH) != 0;
					bool encryptCapable
						= (features->features[0] & LMP_ENCRYPT) != 0;

					if (features->features[0] & LMP_3SLOT)
						packetType |= (HCI_DM3 | HCI_DH3);

					if (features->features[0] & LMP_5SLOT)
						packetType |= (HCI_DM5 | HCI_DH5);

					if (features->features[1] & LMP_HV2)
						packetType |= (HCI_HV2);

					if (features->features[1] & LMP_HV3)
						packetType |= (HCI_HV3);

					fProperties->AddInt16("packet_type", packetType);
					fProperties->AddBool("role_switch_capable", roleSwitch);
					fProperties->AddBool("encrypt_capable", encryptCapable);

					TRACE_BT("LocalDeviceImpl: Packet type %x role switch %d encrypt %d\n",
						packetType, roleSwitch, encryptCapable);
				}

			}

			TRACE_BT("LocalDeviceImpl: Reply for Local Features %x\n", features->status);

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// SetupController(): the next step writes Simple Pairing Mode
			// or the Event Mask. Nobody waits for a reply.
			if (request->HasBool("r5_setup")) {
				ClearWantedEvent(request);
				delete request;
				if (features->status != BT_OK) {
					syslog(LOG_WARNING, "bluetooth_server: hci%" B_PRId32
						" read features failed (%#x), event mask unchanged",
						GetID(), features->status);
					break;
				}
				memcpy(fFeatures, features->features, sizeof(fFeatures));
				fHasSSP = r5_has_ssp(fFeatures);
				if (fHasSSP)
					_WriteSimplePairingMode();
				else
					_WriteEventMask();
				break;
			}
			// SPDX-SnippetEnd

			reply.AddUInt8("status", features->status);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_INFORMATIONAL_PARAM, OCF_READ_BUFFER_SIZE):
		{
			struct hci_rp_read_buffer_size* buffer
				= JumpEventHeader<struct hci_rp_read_buffer_size,
				struct hci_ev_cmd_complete>(event);

			if (buffer->status == BT_OK) {

				if (!IsPropertyAvailable("acl_mtu"))
					fProperties->AddInt16("acl_mtu", buffer->acl_mtu);

				if (!IsPropertyAvailable("sco_mtu"))
					fProperties->AddInt8("sco_mtu", buffer->sco_mtu);

				if (!IsPropertyAvailable("acl_max_pkt"))
					fProperties->AddInt16("acl_max_pkt", buffer->acl_max_pkt);

				if (!IsPropertyAvailable("sco_max_pkt"))
					fProperties->AddInt16("sco_max_pkt", buffer->sco_max_pkt);

			}

			TRACE_BT("LocalDeviceImpl: Reply for Read Buffer Size %x\n", buffer->status);


			reply.AddUInt8("status", buffer->status);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_INFORMATIONAL_PARAM, OCF_READ_LOCAL_SUPPORTED_COMMANDS):
		{
			struct hci_rp_read_loc_supported_cmd* supported_commands
				= JumpEventHeader<struct hci_rp_read_loc_supported_cmd,
				struct hci_ev_cmd_complete>(event);

			if (supported_commands->status == BT_OK) {

				if (!IsPropertyAvailable("supported_commands"))
					fProperties->AddData("supported_commands", B_ANY_TYPE,
						&supported_commands->supported_commands, 64);

			}

			TRACE_BT("LocalDeviceImpl: Reply for Supported Commands %x\n",
				supported_commands->status);

			reply.AddUInt8("status", supported_commands->status);
			status = request->SendReply(&reply);
			// printf("Sending reply... %d\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_INFORMATIONAL_PARAM, OCF_READ_BD_ADDR):
		{
			struct hci_rp_read_bd_addr* readbdaddr
				= JumpEventHeader<struct hci_rp_read_bd_addr,
				struct hci_ev_cmd_complete>(event);

			if (readbdaddr->status == BT_OK) {
				reply.AddData("bdaddr", B_ANY_TYPE, &readbdaddr->bdaddr,
					sizeof(bdaddr_t));
			}

			TRACE_BT("LocalDeviceImpl: Read bdaddr status = %x\n", readbdaddr->status);

			reply.AddUInt8("status", readbdaddr->status);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_READ_CLASS_OF_DEV):
		{
			struct hci_read_dev_class_reply* classDev
				= JumpEventHeader<struct hci_read_dev_class_reply,
				struct hci_ev_cmd_complete>(event);

			if (classDev->status == BT_OK) {
				reply.AddData("devclass", B_ANY_TYPE, &classDev->dev_class,
					sizeof(classDev->dev_class));
			}

			TRACE_BT("LocalDeviceImpl: Read DeviceClass status = %x DeviceClass = [%x][%x][%x]\n",
				classDev->status, classDev->dev_class[0],
				classDev->dev_class[1], classDev->dev_class[2]);


			reply.AddUInt8("status", classDev->status);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_READ_LOCAL_NAME):
		{
			struct hci_rp_read_local_name* readLocalName
				= JumpEventHeader<struct hci_rp_read_local_name,
				struct hci_ev_cmd_complete>(event);


			if (readLocalName->status == BT_OK) {
				reply.AddString("friendlyname",
					(const char*)readLocalName->local_name);
			}

			TRACE_BT("LocalDeviceImpl: Friendly name status %x\n", readLocalName->status);

			reply.AddUInt8("status", readLocalName->status);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_PIN_CODE_REPLY):
		{
			uint8* statusReply = (uint8*)(event + 1);

			// TODO: This reply has to match the BDADDR of the outgoing message
			TRACE_BT("LocalDeviceImpl: pincode accept status %x\n", *statusReply);

			reply.AddUInt8("status", *statusReply);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			//ClearWantedEvent(request, HCI_EVENT_CMD_COMPLETE, opcodeExpected);
			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_PIN_CODE_NEG_REPLY):
		{
			uint8* statusReply = (uint8*)(event + 1);

			// TODO: This reply might match the BDADDR of the outgoing message
			// => FindPetition should be expanded....
			TRACE_BT("LocalDeviceImpl: pincode reject status %x\n", *statusReply);

			reply.AddUInt8("status", *statusReply);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request, HCI_EVENT_CMD_COMPLETE, opcodeExpected);
			break;
		}

		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_READ_STORED_LINK_KEY):
		{
			struct hci_read_stored_link_key_reply* linkKeyRetrieval
				= JumpEventHeader<struct hci_read_stored_link_key_reply,
				struct hci_ev_cmd_complete>(event);

			TRACE_BT("LocalDeviceImpl: Status %s MaxKeys=%d, KeysRead=%d\n",
				BluetoothError(linkKeyRetrieval->status),
				linkKeyRetrieval->max_num_keys,
				linkKeyRetrieval->num_keys_read);

			reply.AddUInt8("status", linkKeyRetrieval->status);
			status = request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			ClearWantedEvent(request);
			break;
		}

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_LINK_KEY_NEG_REPLY):
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_LINK_KEY_REPLY):
		{
			struct hci_cp_link_key_reply_reply* linkKeyReply
				= JumpEventHeader<struct hci_cp_link_key_reply_reply,
				struct hci_ev_cmd_complete>(event);

			TRACE_BT("LocalDeviceImpl: Status %s addresss=%s\n", BluetoothError(linkKeyReply->status),
				bdaddrUtils::ToString(linkKeyReply->bdaddr).String());

			ClearWantedEvent(request, HCI_EVENT_CMD_COMPLETE, opcodeExpected);
			break;
		}

		case  PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_READ_SCAN_ENABLE):
		{
			struct hci_read_scan_enable* scanEnable
				= JumpEventHeader<struct hci_read_scan_enable,
				struct hci_ev_cmd_complete>(event);

			if (scanEnable->status == BT_OK) {
				fProperties->AddInt8("scan_enable", scanEnable->enable);

				TRACE_BT("LocalDeviceImpl: enable = %x\n", scanEnable->enable);
			}

			reply.AddUInt8("status", scanEnable->status);
			reply.AddInt8("scan_enable", scanEnable->enable);
			status = request->SendReply(&reply);
			printf("Sending reply. scan_enable = %d\n", scanEnable->enable);
			// debug reply.PrintToStream();

			// This request is not gonna be used anymore
			ClearWantedEvent(request);
			break;
		}

		// without clearing all events, no reply
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_IO_CAPABILITY_REQUEST_REPLY):
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_USER_CONFIRM_REPLY):
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_USER_CONFIRM_NEG_REPLY):
		{
			TRACE_BT("LocalDeviceImpl: %s for %s status %x\n", __FUNCTION__,
				BluetoothCommandOpcode(opcodeExpected), *(uint8*)(event + 1));
			ClearWantedEvent(request, HCI_EVENT_CMD_COMPLETE, opcodeExpected);
			break;
		}

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_INQUIRY_CANCEL):
		{
			reply.AddUInt8("status", *(uint8*)(event + 1));

			TRACE_BT("LocalDeviceImpl: %s for %s status %x\n", __FUNCTION__,
				BluetoothCommandOpcode(opcodeExpected), *(uint8*)(event + 1));

			status = request->SendReply(&reply);
			printf("%s: Sending reply write...\n", __func__);
			if (status < B_OK)
				printf("%s: Error sending reply write!\n", __func__);

			BMessage* inquiry_request = FindPetition(HCI_EVENT_INQUIRY_COMPLETE);
			if (inquiry_request != NULL)
				ClearWantedEvent(inquiry_request);

			ClearWantedEvent(request);
			break;
		}

		// place here all CC that just replies a uint8 status
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_SET_CONN_ENCRYPT):
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_CREATE_CONN_CANCEL):
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_RESET):
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_WRITE_SCAN_ENABLE):
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_WRITE_CLASS_OF_DEV):
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_WRITE_PG_TIMEOUT):
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_WRITE_CA_TIMEOUT):
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_WRITE_AUTH_ENABLE):
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_WRITE_LOCAL_NAME):
		case PACK_OPCODE(OGF_VENDOR_CMD, OCF_WRITE_BCM2035_BDADDR):
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		case PACK_OPCODE(OGF_LINK_CONTROL, R5_OCF_USER_PASSKEY_REPLY):
		case PACK_OPCODE(OGF_LINK_CONTROL, R5_OCF_USER_PASSKEY_NEG_REPLY):
		// SPDX-SnippetEnd
		{
			reply.AddUInt8("status", *(uint8*)(event + 1));

			TRACE_BT("LocalDeviceImpl: %s for %s status %x\n", __FUNCTION__,
				BluetoothCommandOpcode(opcodeExpected), *(uint8*)(event + 1));

			status = request->SendReply(&reply);
			printf("%s: Sending reply write...\n", __func__);
			if (status < B_OK)
				printf("%s: Error sending reply write!\n", __func__);

			ClearWantedEvent(request);
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// A reset clears the Event Mask and Simple Pairing Mode.
			if ((uint16)opcodeExpected
					== PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_RESET)
				&& *(uint8*)(event + 1) == BT_OK) {
				SetupController();
			}
			// The server's own setup step (Write Scan Enable) has no sender.
			if (request->HasBool("r5_setup"))
				delete request;
			// SPDX-SnippetEnd
			break;
		}

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, R5_OCF_WRITE_SIMPLE_PAIRING_MODE):
		case PACK_OPCODE(OGF_CONTROL_BASEBAND, R5_OCF_SET_EVENT_MASK):
		{
			uint8 commandStatus = *(uint8*)(event + 1);
			bool simplePairing = (uint16)opcodeExpected
				== PACK_OPCODE(OGF_CONTROL_BASEBAND,
					R5_OCF_WRITE_SIMPLE_PAIRING_MODE);
			ClearWantedEvent(request);
			delete request;

			if (commandStatus != BT_OK) {
				syslog(LOG_WARNING, "bluetooth_server: hci%" B_PRId32
					" %s failed (%#x)", GetID(), simplePairing
						? "simple pairing mode" : "event mask", commandStatus);
				if (!simplePairing)
					break;
				// Without SSP on the host the pairing events do not come.
				fHasSSP = false;
			}
			if (simplePairing) {
				_WriteEventMask();
				break;
			}
			syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " event mask %#"
				B_PRIx64 " (secure simple pairing %s)", GetID(),
				r5_event_mask(fHasSSP ? fFeatures : NULL),
				fHasSSP ? "on" : "not supported");

			// A reset leaves the radio unreachable (Scan Enable 0). A paired
			// controller or keyboard pages the computer to come back after
			// sleep, so the radio answers pages (0x02), without being
			// visible to a scan. Off by the radio switch stays 0x00.
			if (((BluetoothServer*)be_app)->RadioEnabled()) {
				size_t size;
				void* command = buildWriteScan(0x02, &size);
				if (command != NULL) {
					_IssueSetupCommand(command, size, PACK_OPCODE(
						OGF_CONTROL_BASEBAND, OCF_WRITE_SCAN_ENABLE));
					free(command);
				}
			}
			break;
		}
		// SPDX-SnippetEnd

		default:
			TRACE_BT("LocalDeviceImpl: Command Complete not handled\n");
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// Still answered, so a sender never waits forever.
			_RefuseRequest(request, (uint16)opcodeExpected,
				*(uint8*)(event + 1));
			// SPDX-SnippetEnd
			break;
	}
}


void
LocalDeviceImpl::CommandStatus(struct hci_ev_cmd_status* event,
	BMessage* request, int32 index)
{
	int16 opcodeExpected;
	BMessage reply;

	// Handle command complete information
	request->FindInt16("opcodeExpected", index, &opcodeExpected);

	if (request->IsSourceWaiting() == false) {
		TRACE_BT("LocalDeviceImpl: Nobody waiting for the event\n");
	}

	switch (opcodeExpected) {

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_INQUIRY):
		{
			reply.what = BT_MSG_INQUIRY_STARTED;

			TRACE_BT("LocalDeviceImpl: Inquiry status %x\n", event->status);

			reply.AddUInt8("status", event->status);
			request->SendReply(&reply);
			//printf("Sending reply... %ld\n", status);
			// debug reply.PrintToStream();

			ClearWantedEvent(request, HCI_EVENT_CMD_STATUS,
				PACK_OPCODE(OGF_LINK_CONTROL, OCF_INQUIRY));
		}
		break;

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_REMOTE_NAME_REQUEST):
		{
			TRACE_BT("LocalDeviceImpl: Command Status for remote friendly name %x\n",
				event->status);

			if (event->status != BT_OK) {
				reply.AddInt8("status", event->status);
				request->SendReply(&reply);
			}
			// printf("Sending reply... %ld\n", status);
			//  debug reply.PrintToStream();

			ClearWantedEvent(request, HCI_EVENT_CMD_STATUS, opcodeExpected);

		}
		break;
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_CREATE_CONN):
		{
			TRACE_BT("LocalDeviceImpl: Command Status for create connection %x\n", event->status);

			switch (event->status) {
				case BT_OK:
					ClearWantedEvent(request, HCI_EVENT_CMD_STATUS, opcodeExpected);
					break;

				case BT_ACL_CONNECTION_EXISTS:
					reply.what = BT_MSG_CONN_COMPLETED;
					reply.AddUInt8("status", BT_OK);
					request->SendReply(&reply);

					ClearWantedEvent(request);
					break;

				default:
					reply.what = BT_MSG_CONN_FAILED;
					reply.AddUInt8("status", event->status);
					request->SendReply(&reply);

					ClearWantedEvent(request);
					break;
			}
		}
		break;

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_DISCONNECT):
		{
			TRACE_BT("LocalDeviceImpl: Command Status for disconnect %x\n", event->status);

			ClearWantedEvent(request, HCI_EVENT_CMD_STATUS, opcodeExpected);
			if (event->status == BT_NO_CONNECTION) {
				reply.what = BT_MSG_DISCONN_COMPLETED;

				reply.AddUInt8("status", event->status);
				((BluetoothServer*)be_app)->NotifyWatchers(&reply);

				ClearWantedEvent(request);
			}
		}
		break;


		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_ACCEPT_CONN_REQ):
		{
			TRACE_BT("LocalDeviceImpl: Command Status for accept connection request %x\n",
				event->status);
			ClearWantedEvent(request, HCI_EVENT_CMD_STATUS, opcodeExpected);
		}
		break;

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_REJECT_CONN_REQ):
		{
			TRACE_BT("LocalDeviceImpl: Command Status for reject connection request %x\n",
				event->status);
			ClearWantedEvent(request, HCI_EVENT_CMD_STATUS, opcodeExpected);
		} break;

		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_AUTH_REQUESTED):
		{
			TRACE_BT("LocalDeviceImpl: Command Status for auth requested %x\n", event->status);
			ClearWantedEvent(request, HCI_EVENT_CMD_STATUS, opcodeExpected);
		} break;

		default:
			TRACE_BT("LocalDeviceImpl: Command Status not handled\n");
		break;
	}
}


void
LocalDeviceImpl::InquiryResult(uint8* numberOfResponses, BMessage* request)
{
	uint8 count = *numberOfResponses;
	TRACE_BT("LocalDeviceImpl: %s #responses=%d\n", __FUNCTION__, count);
	if (count == 0 || request == NULL)
		return;

	uint8* base_ptr = numberOfResponses + 1;

	// get pointers ready for the arrays
	bdaddr_t* bdaddr_array = (bdaddr_t*)base_ptr;
	uint8* page_repetition_mode_array = (uint8*)(bdaddr_array + count);
	uint8* scan_period_mode_array = page_repetition_mode_array + count;
	uint8* scan_mode_array = scan_period_mode_array + count;
	uint8* dev_class_array = scan_mode_array + count;
	uint16* clock_offset_array = (uint16*)(dev_class_array + (count * 3));

	BMessage reply(BT_MSG_INQUIRY_DEVICE);
	reply.AddUInt8("count", count);

	for (uint8 i = 0; i < count; i++) {
		TRACE_BT("LocalDeviceImpl: page_rep=%d scan_period=%d, scan=%d clock=%d\n",
			page_repetition_mode_array[i], scan_period_mode_array[i], scan_mode_array[i],
			clock_offset_array[i]);

		reply.AddData("bdaddr", B_ANY_TYPE, &bdaddr_array[i], sizeof(bdaddr_t));
		reply.AddData("dev_class", B_ANY_TYPE, &dev_class_array[i * 3], 3);
		reply.AddUInt8("page_repetition_mode", page_repetition_mode_array[i]);
		reply.AddUInt8("scan_period_mode", scan_period_mode_array[i]);
		reply.AddUInt8("scan_mode", scan_mode_array[i]);
		reply.AddUInt16("clock_offset", clock_offset_array[i]);
	}

	printf("%s: Sending reply...\n", __func__);
	status_t status = request->SendReply(&reply);
	if (status < B_OK)
		printf("%s: Error sending reply!\n", __func__);
}


void
LocalDeviceImpl::InquiryResultWithRSSI(uint8* numberOfResponses, BMessage* request)
{
	uint8 count = *numberOfResponses;
	TRACE_BT("LocalDeviceImpl: %s #responses=%d\n", __FUNCTION__, count);
	if (count == 0 || request == NULL)
		return;

	uint8* base_ptr = numberOfResponses + 1;

	// get pointers ready for the parallel arrays
	bdaddr_t* bdaddr_array = (bdaddr_t*)base_ptr;
	uint8* page_repetition_mode_array = (uint8*)(bdaddr_array + count);
	uint8* scan_period_mode_array = page_repetition_mode_array + count;
	uint8* dev_class_array = scan_period_mode_array + count;
	uint16* clock_offset_array = (uint16*)(dev_class_array + (count * 3));
	int8* rssi_array = (int8*)(clock_offset_array + count);

	BMessage reply(BT_MSG_INQUIRY_DEVICE);
	reply.AddUInt8("count", count);

	for (uint8 i = 0; i < count; i++) {
		TRACE_BT("LocalDeviceImpl: page_rep=%d scan_period=%d clock=%d rssi=%d\n",
			page_repetition_mode_array[i], scan_period_mode_array[i], clock_offset_array[i],
			rssi_array[i]);

		reply.AddData("bdaddr", B_ANY_TYPE, &bdaddr_array[i], sizeof(bdaddr_t));
		reply.AddData("dev_class", B_ANY_TYPE, &dev_class_array[i * 3], 3);
		reply.AddUInt8("page_repetition_mode", page_repetition_mode_array[i]);
		reply.AddUInt8("scan_period_mode", scan_period_mode_array[i]);
		reply.AddUInt16("clock_offset", clock_offset_array[i]);
		reply.AddInt8("rssi", rssi_array[i]);
	}

	printf("%s: Sending reply...\n", __func__);
	status_t status = request->SendReply(&reply);
	if (status < B_OK)
		printf("%s: Error sending reply!\n", __func__);
}


void
LocalDeviceImpl::ExtendedInquiryResult(uint8* numberOfResponses, BMessage* request)
{
	uint8 count = *numberOfResponses;
	TRACE_BT("LocalDeviceImpl: %s #responses=%d\n", __FUNCTION__, count);

	// the spec says count always equals 1
	if (count != 1 || request == NULL)
		return;

	hci_ev_extended_inquiry_info* info = (hci_ev_extended_inquiry_info*)(numberOfResponses + 1);

	BMessage reply(BT_MSG_INQUIRY_DEVICE);
	reply.AddUInt8("count", count);

	TRACE_BT("LocalDeviceImpl: page_rep=%d scan_period=%d clock=%d rssi=%d\n",
		info->page_repetition_mode, info->scan_period_mode, info->clock_offset, info->rssi);

	reply.AddData("bdaddr", B_ANY_TYPE, &info->bdaddr, sizeof(bdaddr_t));
	reply.AddData("dev_class", B_ANY_TYPE, info->dev_class, 3);
	reply.AddUInt8("page_repetition_mode", info->page_repetition_mode);
	reply.AddUInt8("scan_period_mode", info->scan_period_mode);
	reply.AddUInt16("clock_offset", info->clock_offset);
	reply.AddInt8("rssi", info->rssi);

	ParseEIR(info->eir, reply);

	printf("%s: Sending reply...\n", __func__);
	status_t status = request->SendReply(&reply);
	if (status < B_OK)
		printf("%s: Error sending reply!\n", __func__);
}


void
LocalDeviceImpl::ParseEIR(const uint8* eir, BMessage& reply)
{
	if (eir == NULL)
		return;

	int offset = 0;
	BString completeName;
	BString shortName;

	while (offset < HCI_MAX_EIR_LENGTH) {
		uint8 length = eir[offset];
		// break either when finished reading buffer or when next data value is zero
		if (length == 0 || offset + length >= HCI_MAX_EIR_LENGTH)
			break;
		uint8 type = eir[offset + 1];
		const uint8* data = &eir[offset + 2];
		uint8 dataLen = length - 1;
		switch (type) {
			case EIR_FLAGS:
				if (dataLen >= 1) {
					reply.AddUInt8("eir_flags", data[0]);
					TRACE_BT("LocalDeviceImpl: Parsed EIR Flags: 0x%02X\n", data[0]);
				}
				break;
			case EIR_UUID16_INCOMPLETE:
				reply.AddBool("uuid16_complete", false);
			case EIR_UUID16_COMPLETE:
				for (uint8 i = 0; i + 1 < dataLen; i += 2) {
					uint16 uuid = B_LENDIAN_TO_HOST_INT16(*(const uint16*)(data + i));
					reply.AddUInt16("uuid16", uuid);
					TRACE_BT("LocalDeviceImpl: Parsed EIR UUID16: 0x%04X\n", uuid);
				}
				break;
			case EIR_UUID32_INCOMPLETE:
				reply.AddBool("uuid32_complete", false);
			case EIR_UUID32_COMPLETE:
				for (uint8 i = 0; i + 3 < dataLen; i += 4) {
					uint32 uuid = B_LENDIAN_TO_HOST_INT32(*(const uint32*)(data + i));
					reply.AddUInt32("uuid32", uuid);
					TRACE_BT("LocalDeviceImpl: Parsed EIR of UUID32: 0x%08" B_PRIx32 "\n", uuid);
				}
				break;
			case EIR_UUID128_INCOMPLETE:
				reply.AddBool("uuid128_complete", false);
			case EIR_UUID128_COMPLETE:
				for (uint8 i = 0; i + 15 < dataLen; i += 16) {
					// UUID128 is stored as 16 bytes in little-endian format
					reply.AddData("uuid128", B_ANY_TYPE, data + i, 16);
					TRACE_BT("LocalDeviceImpl: Parsed EIR UUID128 \n");
				}
				break;
			case EIR_NAME_SHORT:
				if (shortName.Length() == 0) {
					shortName.SetTo((const char*)data, dataLen);
					TRACE_BT("LocalDeviceImpl: Parsed EIR Short Name: '%s'\n", shortName.String());
				}
				break;
			case EIR_NAME_COMPLETE:
				if (completeName.Length() == 0) {
					completeName.SetTo((const char*)data, dataLen);
					TRACE_BT("LocalDeviceImpl: Parsed EIR Complete Name: '%s'\n",
						completeName.String());
				}
				break;
			case EIR_TX_POWER:
				if (dataLen >= 1) {
					reply.AddInt8("tx_power", (int8)data[0]);
					TRACE_BT("LocalDeviceImpl: Parsed EIR TX Power: %d dBm\n", (int8)data[0]);
				}
				break;
			case EIR_CLASS_OF_DEVICE:
				if (dataLen >= 3) {
					reply.AddData("dev_class", B_ANY_TYPE, data, 3);
					TRACE_BT("LocalDeviceImpl: Parsed EIR Class of Device: "
							 "0x%02X 0x%02X 0x%02X\n",
						data[0], data[1], data[2]);
				}
				break;
			case EIR_MANUFACTURER_DATA:
				if (dataLen >= 2) {
					reply.AddData("manufacturer_data", B_ANY_TYPE, data, dataLen);
					uint16 company = data[0] | (data[1] << 8);
					TRACE_BT("LocalDeviceImpl: Parsed EIR Manufacturer Data: "
							 "company=0x%04X len=%d\n",
						company, dataLen);
				}
				break;
			default:
				TRACE_BT("LocalDeviceImpl: Ignored EIR Type: 0x%02X (Length: %d)\n", type, dataLen);
				break;
		}

		offset += length + 1;
	}

	if (completeName.Length() > 0) {
		reply.AddString("friendly_name", completeName.String());
		reply.AddBool("friendly_name_is_complete", true);
	} else if (shortName.Length() > 0) {
		reply.AddString("friendly_name", shortName.String());
		reply.AddBool("friendly_name_is_complete", false);
	}
}


void
LocalDeviceImpl::InquiryComplete(uint8* status, BMessage* request)
{
	BMessage reply(BT_MSG_INQUIRY_COMPLETED);

	reply.AddUInt8("status", *status);

	printf("%s: Sending reply...\n", __func__);
	status_t stat = request->SendReply(&reply);
	if (stat < B_OK)
		printf("%s: Error sending reply!\n", __func__);

	ClearWantedEvent(request);
}


void
LocalDeviceImpl::RemoteNameRequestComplete(
	struct hci_ev_remote_name_request_complete_reply* remotename,
	BMessage* request)
{
	BMessage reply;

	if (remotename->status == BT_OK) {
		reply.AddString("friendlyname", (const char*)remotename->remote_name );
	}

	reply.AddUInt8("status", remotename->status);

	TRACE_BT("LocalDeviceImpl: %s for %s with status %s\n",
		BluetoothEvent(HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE),
		bdaddrUtils::ToString(remotename->bdaddr).String(),
		BluetoothError(remotename->status));

	status_t status = request->SendReply(&reply);
	if (status < B_OK)
		printf("%s: Error sending reply to BMessage request: %s!\n",
			__func__, strerror(status));

	// This request is not gonna be used anymore
	ClearWantedEvent(request);
}


void
LocalDeviceImpl::ConnectionRequest(struct hci_ev_conn_request* event,
	BMessage* request)
{
	size_t size;
	void* command;

	TRACE_BT("LocalDeviceImpl: Connection Incoming type %x from %s...\n",
		event->link_type, bdaddrUtils::ToString(event->bdaddr).String());

	// TODO: add a possible request in the queue
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The radio switch is off (BT_MSG_R5_SET_RADIO): turn the request down.
	if (!((BluetoothServer*)be_app)->RadioEnabled()) {
		command = buildRejectConnectionRequest(event->bdaddr, &size);
		if (command == NULL)
			return;
		// buildRejectConnectionRequest() leaves the reason unset.
		struct hci_cp_reject_conn_req* param = (struct hci_cp_reject_conn_req*)
			((uint8*)command + sizeof(struct hci_command_header));
		param->reason = BT_REJECTED_LIMITED_RESOURCES;
		if (fHCIDelegate->IssueCommand(command, size) == B_ERROR)
			TRACE_BT("LocalDeviceImpl: Command issued error for rejecting\n");
		free(command);
		return;
	}
	// SPDX-SnippetEnd

	if (true) { // Check Preferences if we are to accept this connection

		ServerRemoteDevice* serverRd;
		serverRd = RemoteDeviceByAddr(event->bdaddr);

		if (serverRd == NULL) {
			serverRd = new ServerRemoteDevice();
			serverRd->bdaddr = event->bdaddr;
			serverRd->link_key = LinkKeyUtils::NullKey();
		}

		serverRd->link_type = event->link_type;
		serverRd->conn_state = RemoteDevice::CONNECTING;
		memcpy(serverRd->classOfDevice, event->dev_class, 3 * sizeof(uint8));

		BMessage notice(BT_MSG_NEW_REMOTE_DEVICE);
		notice.AddData("bdaddr", B_ANY_TYPE, &serverRd->bdaddr, sizeof(bdaddr_t));
		notice.AddData("class_of_device", B_RAW_TYPE, serverRd->classOfDevice,
			sizeof(serverRd->classOfDevice));

		((BluetoothServer*)be_app)->NotifyWatchers(&notice);
		AddRemoteDevice(serverRd);

		// Keep ourselves as slave
		command = buildAcceptConnectionRequest(event->bdaddr, 0x01 , &size);

		BMessage* newrequest = new BMessage;
		newrequest->AddInt16("eventExpected", HCI_EVENT_CMD_STATUS);
		newrequest->AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL,
			OCF_ACCEPT_CONN_REQ));

		newrequest->AddInt16("eventExpected", HCI_EVENT_PAGE_SCAN_REP_MODE_CHANGE);

		newrequest->AddInt16("eventExpected", HCI_EVENT_LINK_KEY_REQ);
		newrequest->AddInt16("eventExpected", HCI_EVENT_ROLE_CHANGE);

#if 0
		newrequest->AddInt16("eventExpected", HCI_EVENT_MAX_SLOT_CHANGE);
		newrequest->AddInt16("eventExpected", HCI_EVENT_DISCONNECTION_COMPLETE);
#endif

		AddWantedEvent(newrequest);

		if ((fHCIDelegate)->IssueCommand(command, size) == B_ERROR) {
			TRACE_BT("LocalDeviceImpl: Command issued error for Accepting connection\n");
				// remove the request?
		} else {
			TRACE_BT("LocalDeviceImpl: Command issued for Accepting connection\n");
		}
	}
}


void
LocalDeviceImpl::CreateConnection(BMessage* message)
{
	const bdaddr_t* bdaddr;
	ssize_t addr_size;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (message->FindData("bdaddr", B_ANY_TYPE, (const void**)&bdaddr,
			&addr_size) != B_OK || addr_size != (ssize_t)sizeof(bdaddr_t)) {
		TRACE_BT("LocalDeviceImpl: Create Connection without an address\n");
		return;
	}
	bool added = false;
	// SPDX-SnippetEnd

	TRACE_BT("LocalDeviceImpl: Create Connection to %s...\n",
		bdaddrUtils::ToString(*bdaddr).String());

	ServerRemoteDevice* rdConn;
	rdConn = RemoteDeviceByAddr(*bdaddr);

	if (rdConn == NULL) {
		rdConn = new ServerRemoteDevice();
		rdConn->bdaddr = *bdaddr;
		rdConn->link_key = LinkKeyUtils::NullKey();
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		added = true;
		// SPDX-SnippetEnd
	}

	message->FindString("name", &rdConn->friendly_name);
	message->FindUInt8("pscan_rep_mode", &rdConn->pscan_rep_mode);
	message->FindUInt16("clock_offset", &rdConn->clock_offset);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	uint32 record = 0;
	// SPDX-SnippetEnd
	message->FindUInt32("record", &record);
	rdConn->classOfDevice[0] = (record & 0x000000FF);
	rdConn->classOfDevice[1] = (record & 0x0000FF00) >> 8;
	rdConn->classOfDevice[2] = (record & 0x00FF0000) >> 16;

	rdConn->conn_state = RemoteDevice::CONNECTING;
	rdConn->link_type = HCI_ACL_CONN;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	rdConn->user_pairing = true;
	// SPDX-SnippetEnd

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// A device that was already known was added a second time.
	if (added)
		AddRemoteDevice(rdConn);
	// SPDX-SnippetEnd

	BluetoothCommand<typed_command(hci_cp_create_conn)>
		command(OGF_LINK_CONTROL, OCF_CREATE_CONN);

	command->bdaddr = *bdaddr;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// pkt_type is a packed member: read into an aligned value first.
	uint16 packetType = 0;
	message->FindUInt16("packet type", &packetType);
	command->pkt_type = packetType & ACL_PTYPE_MASK;
	// SPDX-SnippetEnd
	command->pscan_rep_mode = rdConn->pscan_rep_mode;
	message->FindUInt8("pscan_mode", &command->pscan_mode); // Reserved in spec 2.1
	command->clock_offset = rdConn->clock_offset;
	command->clock_offset |= 0x8000; // substract!
	message->FindUInt8("role_switch", &command->role_switch);


	if (fHCIDelegate->IssueCommand(command.Data(), command.Size()) == B_ERROR) {
		TRACE_BT("LocalDeviceImpl: Command issued error for %s\n", __FUNCTION__);
		return;
	} else {
		TRACE_BT("LocalDeviceImpl: Command issued for %s\n", __FUNCTION__);
	}

	BMessage* newRequest = new BMessage;

	newRequest->AddInt32("hci_id", fHCIDelegate->Id());

	newRequest->AddInt16("eventExpected",  HCI_EVENT_CMD_STATUS);
	newRequest->AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL, OCF_CREATE_CONN));

	newRequest->AddInt16("eventExpected", HCI_EVENT_LINK_KEY_REQ);
	newRequest->AddInt16("eventExpected", HCI_EVENT_ROLE_CHANGE);

	AddWantedEvent(newRequest);
}


void
LocalDeviceImpl::CancelConnection(BMessage* message)
{
	const bdaddr_t* bdaddr;
	ssize_t addr_size;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// An unchecked FindData() left bdaddr unset (review of #4286).
	if (message->FindData("bdaddr", B_ANY_TYPE, (const void**)&bdaddr,
			&addr_size) != B_OK || bdaddr == NULL
		|| addr_size != (ssize_t)sizeof(bdaddr_t))
		return;
	// SPDX-SnippetEnd

	TRACE_BT("LocalDeviceImpl: Cancel Connection to %s...\n",
		bdaddrUtils::ToString(*bdaddr).String());

	BluetoothCommand<typed_command(hci_cp_create_conn_cancel)> command(OGF_LINK_CONTROL,
		OCF_CREATE_CONN_CANCEL);

	command->bdaddr = *bdaddr;

	if (fHCIDelegate->IssueCommand(command.Data(), command.Size()) == B_ERROR) {
		TRACE_BT("LocalDeviceImpl: Command issued error for %s\n", __FUNCTION__);
		return;
	} else {
		TRACE_BT("LocalDeviceImpl: Command issued for %s\n", __FUNCTION__);
	}

	BMessage* newRequest = new BMessage(BT_MSG_HANDLE_SIMPLE_REQUEST);

	newRequest->AddInt32("hci_id", fHCIDelegate->Id());
	newRequest->AddInt16("eventExpected", HCI_EVENT_CMD_COMPLETE);
	newRequest->AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL, OCF_CREATE_CONN_CANCEL));

	AddWantedEvent(newRequest);
}


void
LocalDeviceImpl::Disconnect(BMessage* message)
{
	TRACE_BT("LocalDeviceImpl: %s...\n", __FUNCTION__);

	BluetoothCommand<typed_command(hci_disconnect)> command(OGF_LINK_CONTROL,
		OCF_DISCONNECT);

	const bdaddr_t* bdaddr;
	ssize_t addr_size;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// A missing or short address, or one this radio does not know, crashed
	// the server (review of #4286).
	if (message->FindData("bdaddr", B_ANY_TYPE, (const void**)&bdaddr,
			&addr_size) != B_OK || bdaddr == NULL
		|| addr_size != (ssize_t)sizeof(bdaddr_t))
		return;

	ServerRemoteDevice* rd = RemoteDeviceByAddr(*bdaddr);
	if (rd == NULL)
		return;
	// SPDX-SnippetEnd
	command->handle = rd->handle;
	message->FindUInt8("reason", &command->reason);

	if (fHCIDelegate->IssueCommand(command.Data(), command.Size()) == B_ERROR) {
		TRACE_BT("LocalDeviceImpl: Command issued error for %s\n", __FUNCTION__);
		return;
	} else {
		TRACE_BT("LocalDeviceImpl: Command issued for %s\n", __FUNCTION__);
	}
	BMessage* newRequest = new BMessage(BT_MSG_HANDLE_SIMPLE_REQUEST);

	newRequest->AddInt32("hci_id", fHCIDelegate->Id());

	newRequest->AddInt16("eventExpected", HCI_EVENT_CMD_STATUS);
	newRequest->AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL, OCF_DISCONNECT));

	AddWantedEvent(newRequest);
}


void
LocalDeviceImpl::SetConnEncryption(uint16 handle, bool encryption_enabled)
{
	TRACE_BT("LocalDeviceImpl: %s...\n", __FUNCTION__);

	BluetoothCommand<typed_command(hci_cp_set_conn_encrypt)> command(OGF_LINK_CONTROL,
		OCF_SET_CONN_ENCRYPT);

	command->handle = handle;
	command->encrypt = encryption_enabled;

	if (fHCIDelegate->IssueCommand(command.Data(), command.Size()) == B_ERROR) {
		TRACE_BT("LocalDeviceImpl: Command issued error for %s\n", __FUNCTION__);
		return;
	}
	TRACE_BT("LocalDeviceImpl: Command issued for %s\n", __FUNCTION__);

	BMessage* newRequest = new BMessage(BT_MSG_HANDLE_SIMPLE_REQUEST);

	newRequest->AddInt32("hci_id", fHCIDelegate->Id());

	newRequest->AddInt16("eventExpected", HCI_EVENT_CMD_STATUS);
	newRequest->AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL, OCF_SET_CONN_ENCRYPT));

	AddWantedEvent(newRequest);
}


void
LocalDeviceImpl::Authenticate(uint16 handle)
{
	TRACE_BT("LocalDeviceImpl: %s...\n", __FUNCTION__);

	BluetoothCommand<typed_command(hci_cp_auth_requested)> command(OGF_LINK_CONTROL,
		OCF_AUTH_REQUESTED);

	command->handle = handle;


	if (fHCIDelegate->IssueCommand(command.Data(), command.Size()) == B_ERROR) {
		TRACE_BT("LocalDeviceImpl: Command issued error for %s\n", __FUNCTION__);
		return;
	} else {
		TRACE_BT("LocalDeviceImpl: Command issued for %s\n", __FUNCTION__);
	}
	BMessage* newRequest = new BMessage();

	newRequest->AddInt32("hci_id", fHCIDelegate->Id());

	newRequest->AddInt16("eventExpected", HCI_EVENT_CMD_STATUS);
	newRequest->AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL, OCF_AUTH_REQUESTED));

	newRequest->AddInt16("eventExpected", HCI_EVENT_AUTH_COMPLETE);

	AddWantedEvent(newRequest);
}


void
LocalDeviceImpl::ConnectionComplete(struct hci_ev_conn_complete* event)
{
	BMessage reply;

	ServerRemoteDevice* rd = RemoteDeviceByAddr(event->bdaddr);
	if (rd != NULL)
		reply.AddData("bdaddr", B_ANY_TYPE, &event->bdaddr, sizeof(bdaddr_t));

	reply.AddUInt8("status", event->status);
	if (event->status == BT_OK) {
		if (rd != NULL) {
			rd->handle = event->handle;
			rd->link_type = event->link_type;
			rd->conn_state = RemoteDevice::CONNECTED;
			rd->encryption_enabled = event->encrypt_mode;
		}

		// TODO: Review, this rDevice is leaked
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// rd is NULL for a connection this radio did not know (review of
		// #4286); the window then shows the address without a class.
		uint8 noClass[3] = { 0, 0, 0 };
		ConnectionIncoming* iConnection = new ConnectionIncoming(
			new RemoteDevice(event->bdaddr,
				rd != NULL ? rd->classOfDevice : noClass));
		// SPDX-SnippetEnd
		iConnection->Show();

		TRACE_BT("LocalDeviceImpl: %s: Address %s handle=%#x type=%d encrypt=%d\n", __FUNCTION__,
				bdaddrUtils::ToString(event->bdaddr).String(), event->handle,
				event->link_type, event->encrypt_mode);

		BMessage* newrequest = new BMessage;
		newrequest->AddInt16("eventExpected", HCI_EVENT_IO_CAPABILITY_REQUEST);
		newrequest->AddInt16("eventExpected", HCI_EVENT_IO_CAPABILITY_RESPONSE);

		// this implies PAIRING complete
		newrequest->AddInt16("eventExpected", HCI_EVENT_LINK_KEY_NOTIFY);
		AddWantedEvent(newrequest);

		Authenticate(event->handle);

		reply.what = BT_MSG_CONN_COMPLETED;
	} else {
		linkkey_t nullLinkKey = LinkKeyUtils::NullKey();
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// rd is NULL for an address this radio did not know (review of
		// #4286).
		if (rd != NULL) {
			if (LinkKeyUtils::Compare(&rd->link_key, &nullLinkKey))
				RemoveRemoteDevice(rd);
			else
				rd->conn_state = RemoteDevice::DISCONNECTED;
		}
		// SPDX-SnippetEnd

		TRACE_BT("LocalDeviceImpl: %s: failed with error %s\n", __FUNCTION__,
			BluetoothError(event->status));

		reply.what = BT_MSG_CONN_FAILED;
	}

	((BluetoothServer*)be_app)->NotifyWatchers(&reply);
}


void
LocalDeviceImpl::DisconnectionComplete(hci_ev_disconnection_complete_reply* event)
{
	TRACE_BT("LocalDeviceImpl: %s: Handle=%#x, reason=%s status=%x\n", __FUNCTION__, event->handle,
		BluetoothError(event->reason), event->status);

	ServerRemoteDevice* rd;
	rd = RemoteDeviceByHandle(event->handle);
	BMessage reply(BT_MSG_DISCONN_COMPLETED);
	reply.AddUInt8("status", event->status);

	if (rd != NULL)
		reply.AddData("bdaddr", B_ANY_TYPE, &rd->bdaddr, sizeof(bdaddr_t));

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// rd is NULL for an unknown handle (review of #4286).
	if (rd != NULL
		&& (event->status == BT_OK || event->status == BT_NO_CONNECTION))
		rd->conn_state = RemoteDevice::DISCONNECTED;
	// SPDX-SnippetEnd


	((BluetoothServer*)be_app)->NotifyWatchers(&reply);
}


void
LocalDeviceImpl::PinCodeRequest(struct hci_ev_pin_code_req* event,
	BMessage* request)
{
	TRACE_BT("LocalDeviceImpl: Opening PincodeWindow...");
	PincodeWindow* iPincode = new PincodeWindow(event->bdaddr, GetID());
	iPincode->Show();
}


void
LocalDeviceImpl::RoleChange(hci_ev_role_change* event, BMessage* request)
{
	// TODO: we shall update the RemoteDevice
	TRACE_BT("LocalDeviceImpl: %s: Address %s role=%d status=%d\n", __FUNCTION__,
		bdaddrUtils::ToString(event->bdaddr).String(), event->role, event->status);
}


void
LocalDeviceImpl::PageScanRepetitionModeChange(
	struct hci_ev_page_scan_rep_mode_change* event, BMessage* request)
{
	// TODO: we shall update the RemoteDevice
	TRACE_BT("LocalDeviceImpl: %s: Address %s type=%d\n",	__FUNCTION__,
		bdaddrUtils::ToString(event->bdaddr).String(), event->page_scan_rep_mode);
}


void
LocalDeviceImpl::LinkKeyNotify(hci_ev_link_key_notify* event,
	BMessage* request)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The link key went to the log of the shipped server (review of #4286).
	TRACE_BT("LocalDeviceImpl: %s: Address %s, type=%d\n", __FUNCTION__,
		bdaddrUtils::ToString(event->bdaddr).String(), event->key_type);
	// SPDX-SnippetEnd

	if (request != NULL)
		ClearWantedEvent(request);

	ServerRemoteDevice* rd = RemoteDeviceByAddr(event->bdaddr);
	if (rd == NULL)
		return;

	rd->link_key = event->link_key;
	rd->link_type = event->key_type;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Saved only when the radio went away, so an unclean shutdown lost
	// every new pairing (review of #4291).
	SaveRemoteDevices();
	// SPDX-SnippetEnd
}


void
LocalDeviceImpl::LinkKeyRequested(struct hci_ev_link_key_req* keyRequested,
	BMessage* request)
{
	TRACE_BT("LocalDeviceImpl: %s: Address %s\n", __FUNCTION__,
		bdaddrUtils::ToString(keyRequested->bdaddr).String());

	ServerRemoteDevice* rd = RemoteDeviceByAddr(keyRequested->bdaddr);
	linkkey_t nullLinkKey = LinkKeyUtils::NullKey();
	if (rd == NULL || LinkKeyUtils::Compare(&rd->link_key, &nullLinkKey)) {
		BluetoothCommand<typed_command(hci_cp_link_key_neg_reply)>
			linkKeyNegativeReply(OGF_LINK_CONTROL, OCF_LINK_KEY_NEG_REPLY);

		BMessage* newrequest = new BMessage;
		newrequest->AddInt16("eventExpected", HCI_EVENT_CMD_COMPLETE);
		newrequest->AddInt16("opcodeExpected",
			PACK_OPCODE(OGF_LINK_CONTROL, OCF_LINK_KEY_NEG_REPLY));
		// on neg reply we expect this
		newrequest->AddInt16("eventExpected", HCI_EVENT_PIN_CODE_REQ);
		AddWantedEvent(newrequest);
		bdaddrUtils::Copy(linkKeyNegativeReply->bdaddr, keyRequested->bdaddr);

		if ((fHCIDelegate)->IssueCommand(linkKeyNegativeReply.Data(),
			linkKeyNegativeReply.Size()) == B_ERROR) {
			TRACE_BT("LocalDeviceImpl: Command issued error for neg reply %s\n", __FUNCTION__);
		} else {
			TRACE_BT("LocalDeviceImpl: Command issued in neg reply of  %s\n", __FUNCTION__);
		}
	} else {

		BluetoothCommand<typed_command(hci_cp_link_key_reply)>
			linkKeyReply(OGF_LINK_CONTROL, OCF_LINK_KEY_REPLY);

		BMessage* newrequest = new BMessage;
		newrequest->AddInt16("eventExpected", HCI_EVENT_CMD_COMPLETE);
		newrequest->AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL, OCF_LINK_KEY_REPLY));
		// on neg reply we expect this
		newrequest->AddInt16("eventExpected", HCI_EVENT_PIN_CODE_REQ);
		AddWantedEvent(newrequest);

		bdaddrUtils::Copy(linkKeyReply->bdaddr, keyRequested->bdaddr);
		linkKeyReply->link_key = rd->link_key;

		if ((fHCIDelegate)->IssueCommand(linkKeyReply.Data(), linkKeyReply.Size()) == B_ERROR) {
			TRACE_BT("LocalDeviceImpl: Command issued error for reply %s\n", __FUNCTION__);
		} else {
			TRACE_BT("LocalDeviceImpl: Command issued in reply of  %s\n", __FUNCTION__);
		}
	}
	if (request != NULL)
		ClearWantedEvent(request, HCI_EVENT_LINK_KEY_REQ);

}


void
LocalDeviceImpl::ReturnLinkKeys(struct hci_ev_return_link_keys* returnedKeys)
{
	TRACE_BT("LocalDeviceImpl: %s: #keys=%d\n",
		__FUNCTION__, returnedKeys->num_keys);

	uint8 numKeys = returnedKeys->num_keys;

	struct link_key_info* linkKeys = &returnedKeys->link_keys;

	while (numKeys > 0) {

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		TRACE_BT("LocalDeviceImpl: Address=%s (key not logged)\n",
			bdaddrUtils::ToString(linkKeys->bdaddr).String());
		// SPDX-SnippetEnd

		linkKeys++;
		numKeys--;
	}
}


void
LocalDeviceImpl::MaxSlotChange(struct hci_ev_max_slot_change* event,
	BMessage* request)
{
	TRACE_BT("LocalDeviceImpl: %s: Handle=%#x, max slots=%d\n", __FUNCTION__,
		event->handle, event->lmp_max_slots);
}


void
LocalDeviceImpl::HardwareError(struct hci_ev_hardware_error* event)
{
	TRACE_BT("LocalDeviceImpl: %s: hardware code=%#x\n",	__FUNCTION__, event->hardware_code);
}


void
LocalDeviceImpl::NumberOfCompletedPackets(struct hci_ev_num_comp_pkts* event)
{
	TRACE_BT("LocalDeviceImpl: %s: #Handles=%d\n", __FUNCTION__, event->num_hndl);

	struct handle_and_number* numberPackets
		= JumpEventHeader<handle_and_number, hci_ev_num_comp_pkts>(event);

	for (uint8 i = 0; i < event->num_hndl; i++) {

		TRACE_BT("LocalDeviceImpl: %s: Handle=%d #packets=%d\n", __FUNCTION__, numberPackets->handle,
			numberPackets->num_completed);

			numberPackets++;
	}
}


void
LocalDeviceImpl::IOCapabilityRequest(struct hci_ev_io_capability_request* event, BMessage* request)
{
	size_t size;
	void* command;

	TRACE_BT("LocalDeviceImpl: IO Capability Request from %s...\n",
		bdaddrUtils::ToString(event->bdaddr).String());

	// TODO: this should be temporary, need to change this to HCI_IO_CAP_DISPLAY_YES_NO
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// KeyboardDisplay, as BlueZ: a keyboard (KeyboardOnly) gets Passkey
	// Entry with the passkey shown here, a device with only a display gets
	// Passkey Entry typed here, a phone gets Numeric Comparison
	// (Core 6.0, Vol 3, Part C, 5.2.2.6).
	command = buildIOCapabilityRequestReply(event->bdaddr,
		R5_HCI_IO_CAP_KEYBOARD_DISPLAY, HCI_OOB_DATA_NOT_PRESENT,
		HCI_AUTH_REQ_MITM_GENERAL_BOND, &size);
	if (command == NULL)
		return;
	// SPDX-SnippetEnd

	BMessage* newrequest = new BMessage;

	newrequest->AddInt16("eventExpected", HCI_EVENT_CMD_COMPLETE);
	newrequest->AddInt16("opcodeExpected",
		PACK_OPCODE(OGF_LINK_CONTROL, OCF_IO_CAPABILITY_REQUEST_REPLY));

	// TODO:Check if there are more events we need to look for
	newrequest->AddInt16("eventExpected", HCI_EVENT_USER_CONFIRMATION_REQUEST);
	newrequest->AddInt16("eventExpected", HCI_EVENT_USER_PASSKEY_REQUEST);
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	newrequest->AddInt16("eventExpected",
		R5_HCI_EVENT_USER_PASSKEY_NOTIFICATION);
	newrequest->AddInt16("eventExpected", R5_HCI_EVENT_KEYPRESS_NOTIFICATION);
	// SPDX-SnippetEnd
	newrequest->AddInt16("eventExpected", HCI_EVENT_SIMPLE_PAIRING_COMPLETE);

	AddWantedEvent(newrequest);

	if ((fHCIDelegate)->IssueCommand(command, size) == B_ERROR)
		TRACE_BT("LocalDeviceImpl: Command issued error for reply %s\n", __FUNCTION__);
	else
		TRACE_BT("LocalDeviceImpl: Command issued in reply of  %s\n", __FUNCTION__);
}


void
LocalDeviceImpl::IOCapabilityResponse(struct hci_ev_io_capability_response* event,
	BMessage* request)
{
	TRACE_BT("LocalDeviceImpl: %s for %s - Capability: 0x%02x, OOB: 0x%02x, Auth: 0x%02x\n",
		__FUNCTION__, bdaddrUtils::ToString(event->bdaddr).String(), event->capability,
		event->oob_data, event->authentication);
}


void
LocalDeviceImpl::UserConfirmationRequest(struct hci_ev_user_confirmation_request* event,
	BMessage* request)
{
	size_t size;
	void* command;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The passkey is not logged.
	TRACE_BT("LocalDeviceImpl: User Confirmation Request for %s\n",
		bdaddrUtils::ToString(event->bdaddr).String());

	// The confirmation is answered without asking the person (Just Works
	// or Numeric Comparison without showing the number). That was also
	// done for a pairing a stranger started, who could then pair silently
	// and, from step 4 on, type as a keyboard. Refused unless the pairing
	// was asked for here.
	ServerRemoteDevice* rd = RemoteDeviceByAddr(event->bdaddr);
	if (rd == NULL || !rd->user_pairing) {
		syslog(LOG_NOTICE, "bluetooth_server: pairing refused, not started "
			"on this computer");
		BluetoothCommand<typed_command(hci_cp_user_confirm_neg_reply)>
			refuse(OGF_LINK_CONTROL, OCF_USER_CONFIRM_NEG_REPLY);
		refuse->bdaddr = event->bdaddr;
		fHCIDelegate->IssueCommand(refuse.Data(), refuse.Size());
		return;
	}
	// SPDX-SnippetEnd

	command = buildUserConfirmReply(event->bdaddr, &size);


	BMessage* newrequest = new BMessage;
	newrequest->AddInt16("eventExpected", HCI_EVENT_CMD_COMPLETE);
	newrequest->AddInt16("opcodeExpected", PACK_OPCODE(OGF_LINK_CONTROL, OCF_USER_CONFIRM_REPLY));

	AddWantedEvent(newrequest);

	if ((fHCIDelegate)->IssueCommand(command, size) == B_ERROR)
		TRACE_BT("LocalDeviceImpl: Command issued error for reply %s\n", __FUNCTION__);
	else
		TRACE_BT("LocalDeviceImpl: Command issued in reply of  %s\n", __FUNCTION__);
}


void
LocalDeviceImpl::SimplePairingComplete(struct hci_ev_simple_pairing_complete* event,
	BMessage* request)
{
	if (event->status == BT_OK) {
		TRACE_BT("LocalDeviceImpl: %s successfull for %s!\n", __FUNCTION__,
			bdaddrUtils::ToString(event->bdaddr).String());

	} else {
		TRACE_BT("LocalDeviceImpl: %s failed for %s with error %s\n", __FUNCTION__,
			bdaddrUtils::ToString(event->bdaddr).String(), BluetoothError(event->status));
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// One pairing asked for here allows one confirmation: a later pairing
	// that the other side starts is refused again.
	ServerRemoteDevice* paired = RemoteDeviceByAddr(event->bdaddr);
	if (paired != NULL)
		paired->user_pairing = false;

	// The pass line of step 4 (docs/BLUETOOTH.md); never the passkey.
	if (fPasskeyUsed && bdaddrUtils::Compare(fPasskeyAddress, event->bdaddr)) {
		if (event->status == BT_OK)
			syslog(LOG_INFO, "bluetooth_server: ssp passkey ok");
		else {
			syslog(LOG_NOTICE, "bluetooth_server: ssp passkey failed (%#x)",
				event->status);
		}
		fPasskeyUsed = false;
		_ClosePasskeyWindow();
	}
	// SPDX-SnippetEnd

	if (request != NULL)
		ClearWantedEvent(request);
}


void
LocalDeviceImpl::AuthComplete(struct hci_ev_auth_complete* eventData, BMessage* request)
{
	uint16 handle = B_LENDIAN_TO_HOST_INT16(eventData->handle);
	uint8 status = eventData->status;

	if (status == BT_OK) {
		TRACE_BT("LocalDeviceImpl: Authentication Successful for handle %d\n", handle);
		ServerRemoteDevice* rd = RemoteDeviceByHandle(eventData->handle);

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// rd is NULL for an unknown handle (review of #4286).
		if (rd != NULL) {
			SetConnEncryption(rd->handle, true);
			((BluetoothServer*)be_app)->DiscoverServices(rd);
		}
		// SPDX-SnippetEnd
	} else {
		TRACE_BT("LocalDeviceImpl: Authentication Failed for handle %d with status 0x%02x\n",
			handle, status);

		ServerRemoteDevice* rd = RemoteDeviceByHandle(eventData->handle);
		if (rd != NULL) {
			rd->link_key = LinkKeyUtils::NullKey();
			BMessage disconnReq;
			bdaddr_t bdaddr = rd->bdaddr;
			disconnReq.AddData("bdaddr", B_ANY_TYPE, &bdaddr, sizeof(bdaddr_t));
			disconnReq.AddUInt8("reason", BT_AUTHENTICATION_FAILURE);
			Disconnect(&disconnReq);
		}
	}

	if (request != NULL) {
		BMessage reply;
		reply.AddUInt8("status", status);
		reply.AddUInt16("handle", handle);

		request->SendReply(&reply);

		ClearWantedEvent(request);
	} else {
		TRACE_BT("LocalDeviceImpl: Auth Complete received but no local request was waiting.\n");
	}
}


void
LocalDeviceImpl::EncryptChange(struct hci_ev_encrypt_change* event)
{
	TRACE_BT("LocalDeviceImpl: %s: Handle=%#x, encrypt=%x, status=%x\n",
		__FUNCTION__, event->handle, event->encrypt, event->status);

	ServerRemoteDevice* rd = RemoteDeviceByHandle(event->handle);
	if (rd == NULL)
		return;

	rd->encryption_enabled = event->encrypt;
	if (rd->encryption_enabled != 0)
		((BluetoothServer*)be_app)->NotifyServices(rd);
}


#if 0
#pragma mark - Request Methods -
#endif

status_t
LocalDeviceImpl::ProcessSimpleRequest(BMessage* request)
{
	ssize_t size;
	void* command = NULL;

	if (request->FindData("raw command", B_ANY_TYPE, 0,
		(const void **)&command, &size) == B_OK) {

		// Give the chance of just issuing the command
		int16 eventFound;
		if (request->FindInt16("eventExpected", &eventFound) == B_OK)
			AddWantedEvent(request);
		// LEAK: is command buffer freed within the Message?
		if (((HCITransportAccessor*)fHCIDelegate)->IssueCommand(command, size)
			== B_ERROR) {
			// TODO:
			// Reply the request with error!
			// Remove the just added request
			TRACE_BT("LocalDeviceImpl: ## ERROR Command issue, REMOVING!\n");
			ClearWantedEvent(request);

		} else {
			return B_OK;
		}
	} else {
		TRACE_BT("LocalDeviceImpl: No command specified for simple request\n");
	}

	return B_ERROR;
}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
status_t
LocalDeviceImpl::WriteScanEnable(uint8 mode)
{
	size_t size;
	void* command = buildWriteScan(mode, &size);
	if (command == NULL)
		return B_NO_MEMORY;

	status_t status = fHCIDelegate->IssueCommand(command, size);
	free(command);
	return status == B_ERROR ? B_ERROR : B_OK;
}
// SPDX-SnippetEnd


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#if 0
#pragma mark - R5 controller setup and Passkey Entry -
#endif


void
LocalDeviceImpl::SetupController()
{
	BluetoothCommand<> command(OGF_INFORMATIONAL_PARAM,
		OCF_READ_LOCAL_FEATURES);
	_IssueSetupCommand(command.Data(), command.Size(),
		PACK_OPCODE(OGF_INFORMATIONAL_PARAM, OCF_READ_LOCAL_FEATURES));
}


void
LocalDeviceImpl::_IssueSetupCommand(const void* command, size_t size,
	uint16 opcode)
{
	// The petition is in place before the command goes out, so the answer
	// cannot arrive first. "r5_setup" marks it as the server's own: the
	// Command Complete handler continues the setup instead of replying.
	BMessage* request = new(std::nothrow) BMessage;
	if (request == NULL)
		return;
	request->AddInt32("hci_id", fHCIDelegate->Id());
	request->AddInt16("eventExpected", HCI_EVENT_CMD_COMPLETE);
	request->AddInt16("opcodeExpected", opcode);
	request->AddBool("r5_setup", true);
	AddWantedEvent(request);

	if (fHCIDelegate->IssueCommand((void*)command, size) == B_ERROR) {
		syslog(LOG_WARNING, "bluetooth_server: hci%" B_PRId32
			" setup command %#x not sent", GetID(), opcode);
		ClearWantedEvent(request);
		delete request;
	}
}


void
LocalDeviceImpl::_WriteSimplePairingMode()
{
	BluetoothCommand<typed_command(r5_hci_cp_write_simple_pairing_mode)>
		command(OGF_CONTROL_BASEBAND, R5_OCF_WRITE_SIMPLE_PAIRING_MODE);
	command->mode = 1;
	_IssueSetupCommand(command.Data(), command.Size(),
		PACK_OPCODE(OGF_CONTROL_BASEBAND, R5_OCF_WRITE_SIMPLE_PAIRING_MODE));
}


void
LocalDeviceImpl::_WriteEventMask()
{
	// Only the bits this controller supports: the SSP events need its SSP
	// feature, and Simple Pairing Mode must have been accepted.
	uint64 mask = r5_event_mask(fHasSSP ? fFeatures : NULL);
	BluetoothCommand<typed_command(r5_hci_cp_set_event_mask)>
		command(OGF_CONTROL_BASEBAND, R5_OCF_SET_EVENT_MASK);
	for (int i = 0; i < 8; i++)
		command->mask[i] = (uint8)(mask >> (8 * i));
	_IssueSetupCommand(command.Data(), command.Size(),
		PACK_OPCODE(OGF_CONTROL_BASEBAND, R5_OCF_SET_EVENT_MASK));
}


void
LocalDeviceImpl::_RefuseRequest(BMessage* request, uint16 opcode,
	uint8 status)
{
	syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32 " command %#06x "
		"answered with status %#x", GetID(), opcode, status);

	bool own = request->HasBool("r5_setup");
	if (!own) {
		BMessage reply;
		reply.AddUInt8("status", status == BT_OK ? BT_ERROR : status);
		request->SendReply(&reply);
	}
	ClearWantedEvent(request);
	if (own)
		delete request;
}


BString
LocalDeviceImpl::_PeerName(const bdaddr_t& bdaddr)
{
	ServerRemoteDevice* rd = RemoteDeviceByAddr(bdaddr);
	if (rd != NULL && rd->friendly_name.Length() > 0)
		return rd->friendly_name;
	return bdaddrUtils::ToString(bdaddr);
}


void
LocalDeviceImpl::_ClosePasskeyWindow()
{
	if (fPasskeyWindow.IsValid())
		fPasskeyWindow.SendMessage(B_QUIT_REQUESTED);
	fPasskeyWindow = BMessenger();
}


void
LocalDeviceImpl::UserPasskeyRequest(
	struct r5_hci_ev_user_passkey_request* event, BMessage* request)
{
	// The remote shows a passkey; the person types it here (Passkey Entry,
	// Core 6.0, Vol 4, Part E, 7.7.43). Never logged.
	TRACE_BT("LocalDeviceImpl: User Passkey Request for %s\n",
		bdaddrUtils::ToString(event->bdaddr).String());

	_ClosePasskeyWindow();
	fPasskeyAddress = event->bdaddr;
	fPasskeyUsed = true;

	PasskeyWindow* window = new(std::nothrow) PasskeyWindow(event->bdaddr,
		GetID(), _PeerName(event->bdaddr));
	if (window == NULL) {
		// Without a window nobody can answer: refuse at once.
		BluetoothCommand<typed_command(r5_hci_cp_user_passkey_neg_reply)>
			reply(OGF_LINK_CONTROL, R5_OCF_USER_PASSKEY_NEG_REPLY);
		reply->bdaddr = event->bdaddr;
		fHCIDelegate->IssueCommand(reply.Data(), reply.Size());
		return;
	}
	fPasskeyWindow = BMessenger(window);
	window->Show();
}


void
LocalDeviceImpl::UserPasskeyNotification(
	struct r5_hci_ev_user_passkey_notification* event, BMessage* request)
{
	// The remote (a keyboard) has no display: the passkey is shown here and
	// typed there (7.7.48). Never logged.
	TRACE_BT("LocalDeviceImpl: User Passkey Notification for %s\n",
		bdaddrUtils::ToString(event->bdaddr).String());

	uint32 passkey = B_LENDIAN_TO_HOST_INT32(event->passkey);
	if (passkey > R5_PASSKEY_MAX) {
		syslog(LOG_WARNING, "bluetooth_server: passkey out of range, ignored");
		return;
	}

	_ClosePasskeyWindow();
	fPasskeyAddress = event->bdaddr;
	fPasskeyUsed = true;

	PasskeyWindow* window = new(std::nothrow) PasskeyWindow(event->bdaddr,
		GetID(), _PeerName(event->bdaddr), passkey);
	if (window == NULL)
		return;
	fPasskeyWindow = BMessenger(window);
	window->Show();
}


void
LocalDeviceImpl::KeypressNotification(
	struct r5_hci_ev_keypress_notification* event, BMessage* request)
{
	// The keyboard reports each digit typed (7.7.49); the window counts
	// them. The digits themselves are not sent.
	if (!fPasskeyUsed || !bdaddrUtils::Compare(fPasskeyAddress, event->bdaddr)
		|| !fPasskeyWindow.IsValid())
		return;

	BMessage keypress(kMsgPasskeyKeypress);
	keypress.AddUInt8("type", event->type);
	fPasskeyWindow.SendMessage(&keypress);
}
// SPDX-SnippetEnd
