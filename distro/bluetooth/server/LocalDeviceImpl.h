/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _LOCALDEVICE_IMPL_H_
#define _LOCALDEVICE_IMPL_H_

#include <String.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetoothserver_p.h>
#include <bluetooth/RemoteDevice.h>
#include <ObjectList.h>

#include "LocalDeviceHandler.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <Messenger.h>
#include "R5Pairing.h"
// SPDX-SnippetEnd

#include "HCIDelegate.h"
#include "HCIControllerAccessor.h"
#include "HCITransportAccessor.h"


struct ServerRemoteDevice
{
	bdaddr_t			bdaddr;
	BString     		friendly_name;
	uint16				clock_offset;
	uint8				pscan_rep_mode;
	uint8				classOfDevice[3];
	linkkey_t			link_key;
	uint8				link_type;
	uint8				encryption_enabled;

	uint16				handle;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Every new remote device starts disconnected, not with stack garbage.
	RemoteDevice::ConnectionState			conn_state
		= RemoteDevice::DISCONNECTED;
	// Pairing was asked for here (Create Connection from the Bluetooth
	// page): only then is a Numeric Comparison or Just Works accepted.
	bool				user_pairing = false;
	// SPDX-SnippetEnd
	BMessage			services;
};

typedef BObjectList<ServerRemoteDevice> RemoteDevicesList;

class LocalDeviceImpl : public LocalDeviceHandler {

private:
	LocalDeviceImpl(HCIDelegate* hd);

public:

	// Factory methods
	static LocalDeviceImpl* CreateControllerAccessor(BPath* path);
	static LocalDeviceImpl* CreateTransportAccessor(BPath* path);
	~LocalDeviceImpl();
	void Unregister();

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// size: what the port delivered, header included.
	void HandleEvent(struct hci_event_header* event, size_t size);
	static bool _EventFits(const struct hci_event_header* event, size_t size);
	// SPDX-SnippetEnd

	// Request handling
	status_t 	ProcessSimpleRequest(BMessage* request);

	// Connection
	void CreateConnection(BMessage* message);
	void CancelConnection(BMessage* message);
	void Disconnect(BMessage* message);
	void SetConnEncryption(uint16 handle, bool encryption_enabled);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Writes HCI Write Scan Enable without waiting for its answer (the
	// radio switch, BT_MSG_R5_SET_RADIO).
	status_t WriteScanEnable(uint8 mode);

	// Reads the controller features, then writes Simple Pairing Mode and
	// the Event Mask (docs/BLUETOOTH.md, step 4). Called when the radio is
	// added and after every HCI Reset, which clears both.
	void SetupController();
	// SPDX-SnippetEnd

	ServerRemoteDevice*	RemoteDeviceByAddr(bdaddr_t bdaddr);
	ServerRemoteDevice*	RemoteDeviceByHandle(uint16 handle);
	void				AddRemoteDevice(ServerRemoteDevice* rd);
	void				RemoveRemoteDevice(ServerRemoteDevice* rd);
	RemoteDevicesList*	GetRemoteDevicesList();

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The /dev/bluetooth path this radio was opened from.
	const BString&		Path() const { return fPath; }
	// SPDX-SnippetEnd

private:

	RemoteDevicesList	fRemoteDevicesList;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	BString				fPath;

	// Controller setup after a reset (SetupController()).
	uint8				fFeatures[8];
	bool				fHasSSP;
	void				_IssueSetupCommand(const void* command, size_t size,
							uint16 opcode);
	void				_WriteSimplePairingMode();
	void				_WriteEventMask();

	// Passkey Entry (Secure Simple Pairing): one pairing at a time.
	BMessenger			fPasskeyWindow;
	bdaddr_t			fPasskeyAddress;
	bool				fPasskeyUsed;
	BString				_PeerName(const bdaddr_t& bdaddr);
	void				_ClosePasskeyWindow();
	void				UserPasskeyRequest(
							struct r5_hci_ev_user_passkey_request* event,
							BMessage* request);
	void				UserPasskeyNotification(
							struct r5_hci_ev_user_passkey_notification* event,
							BMessage* request);
	void				KeypressNotification(
							struct r5_hci_ev_keypress_notification* event,
							BMessage* request);
	// SPDX-SnippetEnd

	void SaveRemoteDevices();
	void LoadRemoteDevices();

	void HandleUnexpectedEvent(struct hci_event_header* event);
	void HandleExpectedRequest(struct hci_event_header* event,
		BMessage* request);

	// Events handling
	void CommandComplete(struct hci_ev_cmd_complete* event, BMessage* request,
		int32 index);
	void CommandStatus(struct hci_ev_cmd_status* event, BMessage* request,
		int32 index);

	void NumberOfCompletedPackets(struct hci_ev_num_comp_pkts* event);

	// Inquiry
	void InquiryResult(uint8* numberOfResponses, BMessage* request);
	void InquiryResultWithRSSI(uint8* numberOfResponses, BMessage* request);
	void ExtendedInquiryResult(uint8* numberOfResponses, BMessage* request);
	void ParseEIR(const uint8* eir, BMessage& reply);
	void InquiryComplete(uint8* status, BMessage* request);
	void RemoteNameRequestComplete(struct hci_ev_remote_name_request_complete_reply*
		remotename, BMessage* request);

	// Connection
	void Authenticate(uint16 handle);
	void ConnectionComplete(struct hci_ev_conn_complete* event);
	void ConnectionRequest(struct hci_ev_conn_request* event, BMessage* request);
	void DisconnectionComplete(struct hci_ev_disconnection_complete_reply* event);

	// Pairing
	void PinCodeRequest(struct hci_ev_pin_code_req* event, BMessage* request);
	void RoleChange(struct hci_ev_role_change* event, BMessage* request);
	void LinkKeyNotify(struct hci_ev_link_key_notify* event, BMessage* request);
	void ReturnLinkKeys(struct hci_ev_return_link_keys* returnedKeys);

	void LinkKeyRequested(struct hci_ev_link_key_req* keyReqyested,
		BMessage* request);

	void PageScanRepetitionModeChange(struct hci_ev_page_scan_rep_mode_change* event,
		BMessage* request);
	void MaxSlotChange(struct hci_ev_max_slot_change* event, BMessage* request);

	void HardwareError(struct hci_ev_hardware_error* event);

	// Simple Secure Pairing
	void IOCapabilityRequest(struct hci_ev_io_capability_request* event,
		BMessage* request);
	void IOCapabilityResponse(struct hci_ev_io_capability_response* event,
		BMessage* request);
	void UserConfirmationRequest(struct hci_ev_user_confirmation_request* event, BMessage* request);
	void SimplePairingComplete(struct hci_ev_simple_pairing_complete* event,
		BMessage* request);
	void AuthComplete(struct hci_ev_auth_complete* eventData, BMessage* request);

	void EncryptChange(struct hci_ev_encrypt_change* eventData);
};

#endif
