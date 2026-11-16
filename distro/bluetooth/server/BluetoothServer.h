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

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/HCI/btHCI_transport.h>
#include <bluetooth/HCI/btHCI_command.h>

#include "DeviceManager.h"
#include "HCIDelegate.h"
#include "LocalDeviceImpl.h"
#include "SDPServer.h"
#include "SDPClient.h"

#include <PortListener.h>

#define BT "bluetooth_server: "

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Private to the server: posted to itself at the end of ReadyToRun().
static const uint32 kMsgReportStarted = 'r5bs';
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

    LocalDeviceImpl*    LocateLocalDeviceImpl(hci_id hid);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	void		DispatchToDevice(hci_id hid, struct hci_event_header* header,
					size_t size);
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
};

#endif
