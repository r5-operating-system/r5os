/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _BLUETOOTH_SERVER_APP_H
#define _BLUETOOTH_SERVER_APP_H

#include <stdlib.h>

#include <Application.h>
#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <R5Admin.h>
// SPDX-SnippetEnd

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/HCI/btHCI_transport.h>
#include <bluetooth/HCI/btHCI_command.h>

#include "DeviceManager.h"
#include "HCIDelegate.h"
#include "LocalDeviceImpl.h"
#include "SDPServer.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "HIDHost.h"
// SPDX-SnippetEnd
#include "SDPClient.h"

#include <PortListener.h>

#define BT "bluetooth_server: "

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Private to the server: posted to itself at the end of ReadyToRun().
static const uint32 kMsgReportStarted = 'r5bs';
static const uint32 kMsgR5LEPulse = 'r5lp';

// B_SYSTEM_SETTINGS_DIRECTORY/bluetooth: the radio switch and the pairing
// keys, root's alone (mode 0700). create makes it and sets that mode.
class BPath;
status_t r5_bluetooth_settings_path(BPath& path, bool create);
// SPDX-SnippetEnd

typedef enum {
	BLACKBOARD_GENERAL = 0,
	BLACKBOARD_DEVICEMANAGER,
	BLACKBOARD_KIT,
	BLACKBOARD_SDP,
	// more blackboards
	BLACKBOARD_END
} BluetoothServerBlackBoardIndex;

#define BLACKBOARD_LD(X) (BLACKBOARD_END+X-HCI_DEVICE_INDEX_OFFSET)

typedef BObjectList<LocalDeviceImpl> LocalDevicesList;
typedef BObjectList<BMessenger> WatchersList;
typedef PortListener<struct hci_event_header, 
	HCI_MAX_EVENT_SIZE, // Event Body can hold max 255 + 2 header
	24					// Some devices have sent chunks of 24 events(inquiry result)
	> BluetoothPortListener;

class BluetoothServer : public BApplication
{
public:

	BluetoothServer();

	virtual bool QuitRequested(void);
	virtual void ArgvReceived(int32 argc, char **argv);
	virtual void ReadyToRun(void);


	virtual void AppActivated(bool act);
	virtual void MessageReceived(BMessage *message);

	static int32 SDPServerThread(void* data);
	
	/* Messages reply */
	status_t	HandleLocalDevicesCount(BMessage* message, BMessage* reply);
	status_t    HandleAcquireLocalDevice(BMessage* message, BMessage* reply);
	
	status_t    HandleGetProperty(BMessage* message, BMessage* reply);
	status_t    HandleSimpleRequest(BMessage* message, BMessage* reply);

	void		NotifyWatchers(BMessage* notice);
	void		DiscoverServices(ServerRemoteDevice* rd);
	void		NotifyServices(ServerRemoteDevice* rd);
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Classic HID (step 4); NULL until ReadyToRun().
	HIDHost*	GetHIDHost() { return fHIDHost; }
	// SPDX-SnippetEnd

    LocalDeviceImpl*    LocateLocalDeviceImpl(hci_id hid);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	void		DispatchToDevice(hci_id hid, struct hci_event_header* header,
					size_t size);
	// SPDX-SnippetEnd

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The radio switch (BT_MSG_R5_SET_RADIO). Read under fDevicesLock, which
	// both MessageReceived() and the port listener thread hold.
	bool		RadioEnabled() const { return fRadioEnabled; }
	// SPDX-SnippetEnd

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// bluetooth_audio streams (BT_MSG_R5_AUDIO_STREAMING) and its team
	// lives: scans wait. LEManager asks this before it scans.
	bool		AudioStreaming() const;
	// The radio switch and bluetooth_audio's requests, from the admin port
	// (libr5admin.so) with the sender the kernel named.
	status_t	R5AdminRequest(BMessage* message, BMessage& reply,
					const r5_admin_sender& sender);
	// SPDX-SnippetEnd

private:

	LocalDeviceImpl*	LocateDelegateFromMessage(BMessage* message);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	static bool			_FindAddress(BMessage* message,
							const bdaddr_t** _address);

	// Held by MessageReceived() and by the port listener thread
	// (DispatchToDevice); guards the local devices and the watchers.
	BLocker				fDevicesLock;
	// SPDX-SnippetEnd

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	bool				_RadioRequestAllowed(BMessage* message) const;
	void				_LoadRadioSetting();
	status_t			_SaveRadioSetting() const;
	status_t			_SetRadioEnabled(bool enabled);

	bool				fRadioEnabled;
	// SPDX-SnippetEnd

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The team of bluetooth_audio while it streams (BT_MSG_R5_AUDIO_STREAMING),
	// -1 otherwise; scans are refused while that team lives.
	team_id				fAudioTeam;
	port_id				fAdminPort;
	// SPDX-SnippetEnd

	void 				ShowWindow(BWindow* pWindow);

	void				_InstallDeskbarIcon();
	void				_RemoveDeskbarIcon();

	LocalDevicesList   	fLocalDevicesList;
	WatchersList		fWatchersList;


	// Notification system
	BluetoothPortListener*	fEventListener2;
	
	DeviceManager*			fDeviceManager;
	
	BPoint 					fCenter;
	
	SDPServer*              fSDPServer;
	SDPClient*				fSDPClient;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	HIDHost*				fHIDHost;
	class BMessageRunner*	fLEPulse;
	// SPDX-SnippetEnd
};

#endif
