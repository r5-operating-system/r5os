/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _REMOTE_DEVICE_H
#define _REMOTE_DEVICE_H

#include <bluetooth/bluetooth.h>
#include <bluetooth/bluetooth_error.h>
#include <bluetooth/BluetoothDevice.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <bluetooth/HCI/btHCI.h>
// SPDX-SnippetEnd

#include <bluetoothserver_p.h>
#include <support/String.h>
#include <ObjectList.h>

#define B_BT_WAIT 0x00
#define B_BT_SUCCEEDED 0x01


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// What RemoteDevice::GetConnectionDetails() fills in. Outside the
// namespace, like bdaddr_t, so it needs no qualifier. The caller sets
// "size" to sizeof(bt_connection_details); a later, larger version of this
// struct only adds fields at the end.
struct bt_connection_details {
	uint32			size;
	uint8			state;		// RemoteDevice::ConnectionState
	uint8			transport;	// BT_TRANSPORT_BR_EDR (LE: step 6)
	uint8			link_type;	// HCI link type: 0 SCO, 1 ACL, 2 eSCO
	bool			encrypted;
	uint16			handle;		// HCI connection handle while connected
	hci_id			hci;		// the local radio
};

#define BT_TRANSPORT_BR_EDR		0
#define BT_TRANSPORT_LE			1
// SPDX-SnippetEnd

namespace Bluetooth {

class Connection;
class LocalDevice;


class RemoteDevice : public BluetoothDevice {

public:
	static const int WAIT = B_BT_WAIT;
	static const int SUCCEEDED = B_BT_SUCCEEDED;

	enum ConnectionState {
		CONNECTED,
		CONNECTING,
		DISCONNECTED
	};

	static BObjectList<RemoteDevice> GetRemoteDevices(LocalDevice* localDevice);
	static RemoteDevice* ParseRemoteDevice(LocalDevice* localDevice, BMessage* message);

	virtual 		~RemoteDevice();

	bool 			IsTrustedDevice();
	BString 		GetFriendlyName(bool alwaysAsk); /* Throwing */
	BString 		GetFriendlyName(void); /* Throwing */
	BString 		GetCachedFriendlyName();
	bdaddr_t 		GetBluetoothAddress();
	uint8 			GetPageRepetitionMode();
	uint16 			GetClockOffset();
	DeviceClass 	GetDeviceClass();

	bool 			Equals(RemoteDevice* obj);

	status_t		Connect();
	status_t		CancelConnection();
	status_t		Disconnect(bool removeDevice);

	/*static RemoteDevice* GetRemoteDevice(Connection conn);   Throwing */
	/* bool Authorize(Connection conn);  Throwing */
	/*bool Encrypt(Connection conn, bool on);  Throwing */
	/*bool IsAuthorized(Connection conn);  Throwing */
	RemoteDevice::ConnectionState 	GetConnectionState();
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Additive: B_OK with "details" filled in, or an error (and state
	// DISCONNECTED) for a device the server does not know.
	status_t		GetConnectionDetails(bt_connection_details* details);
	// Additive (step 3a): the signal strength in dBm from the inquiry that
	// found the device, or HCI_RSSI_INVALID (127) when it was not reported.
	int8			GetRSSI();
	// SPDX-SnippetEnd
	bool 			IsEncrypted(); /* Throwing */

	BString 		GetProperty(const char* property); /* Throwing */
	status_t 		GetProperty(const char* property, uint32* value); /* Throwing */

	LocalDevice* 	GetLocalDeviceOwner();

					RemoteDevice(const BString& address);
					RemoteDevice(const bdaddr_t address, uint8 record[3]);


protected:
	/* called by Discovery[Listener|Agent] */
	void 			SetLocalDeviceOwner(LocalDevice* ld);
	friend class DiscoveryListener;

private:

	LocalDevice* 	fDiscovererLocalDevice;
	BMessenger*	 	fMessenger;

	uint8			fPageRepetitionMode;
	uint8			fScanPeriodMode;
	uint8			fScanMode;
	uint16			fClockOffset;
	int8        	fRSSI;
	BString     	fFriendlyName;
	bool        	fFriendlyNameIsComplete;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::RemoteDevice;
#endif

#endif // _REMOTE_DEVICE_H
