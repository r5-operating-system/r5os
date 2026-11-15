/*
 * Copyright 2007-2009 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>

#include <Autolock.h>
#include <Entry.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Message.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <Window.h>

#include <TypeConstants.h>
#include <new>
#include <syslog.h>

#include <bluetoothserver_p.h>
#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetooth/bluetooth.h>

#include "BluetoothServer.h"
#include "Debug.h"
#include "DeskbarReplicant.h"
#include "LocalDeviceImpl.h"
#include "SDPServer.h"
#include "SDPClient.h"


status_t
DispatchEvent(struct hci_event_header* header, int32 code, size_t size)
{
	// we only handle events
	if (GET_PORTCODE_TYPE(code)!= BT_EVENT) {
		TRACE_BT("BluetoothServer: Wrong type frame code\n");
		return B_OK;
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// This runs on the port listener thread. The local device list is
	// changed on the application thread, so look up and use the device
	// under the list lock (BluetoothServer::DispatchToDevice).
	((BluetoothServer*)be_app)->DispatchToDevice(GET_PORTCODE_HID(code),
		header);

	return B_OK;
}


void
BluetoothServer::DispatchToDevice(hci_id hid, struct hci_event_header* header)
{
	BAutolock locker(fDevicesLock);

	// fetch the LocalDevice who belongs this event
	LocalDeviceImpl* lDeviceImplementation = LocateLocalDeviceImpl(hid);
	if (lDeviceImplementation == NULL) {
		TRACE_BT("BluetoothServer: LocalDevice could not be fetched\n");
		return;
	}

	lDeviceImplementation->HandleEvent(header);
}
// SPDX-SnippetEnd


BluetoothServer::BluetoothServer()
	:
	BApplication(BLUETOOTH_SIGNATURE),
	fDevicesLock("bluetooth local devices")
{
	fDeviceManager = new DeviceManager();
	fLocalDevicesList.MakeEmpty();
	fWatchersList.MakeEmpty();

	fEventListener2 = new BluetoothPortListener(BT_USERLAND_PORT_NAME,
		(BluetoothPortListener::port_listener_func)&DispatchEvent);
	fSDPServer = new SDPServer();
}


bool BluetoothServer::QuitRequested(void)
{
	LocalDeviceImpl* lDeviceImpl = NULL;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	fDevicesLock.Lock();
	while ((lDeviceImpl = (LocalDeviceImpl*)
		fLocalDevicesList.RemoveItemAt(0)) != NULL)
		delete lDeviceImpl;
	fDevicesLock.Unlock();

	fDeviceManager->Lock();
	fDeviceManager->Quit();
	fDeviceManager = NULL;
	// SPDX-SnippetEnd

	_RemoveDeskbarIcon();

	fSDPServer->Stop();
	delete fSDPServer;

	delete fEventListener2;
	TRACE_BT("Shutting down bluetooth_server.\n");

	return BApplication::QuitRequested();
}


void BluetoothServer::ArgvReceived(int32 argc, char **argv)
{
	if (argc > 1) {
		if (strcmp(argv[1], "--finish") == 0)
			PostMessage(B_QUIT_REQUESTED);
	}
}


void BluetoothServer::ReadyToRun(void)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The device manager is a looper; without Run() it never sees a radio
	// that is plugged in or removed after the server started.
	fDeviceManager->LoadState();
	// SPDX-SnippetEnd

	fDeviceManager->StartMonitoringDevice("bluetooth/h2");
	fDeviceManager->StartMonitoringDevice("bluetooth/h3");
	fDeviceManager->StartMonitoringDevice("bluetooth/h4");
	fDeviceManager->StartMonitoringDevice("bluetooth/h5");

	if (fEventListener2->Launch() != B_OK)
		TRACE_BT("General: Bluetooth event listener failed\n");
	else
		TRACE_BT("General: Bluetooth event listener Ready\n");

	_InstallDeskbarIcon();

	status_t status = fSDPServer->Start();
	if (status != B_OK)
		TRACE_BT("BluetoothServer: Failed launching the SDP server thread\n");

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The radios found at start are queued as BT_MSG_ADD_DEVICE before this
	// message, so the count in the log line includes them.
	PostMessage(kMsgReportStarted);
	// SPDX-SnippetEnd
}


void BluetoothServer::AppActivated(bool act)
{
	printf("Activated %d\n",act);
}


void BluetoothServer::MessageReceived(BMessage* message)
{
	BMessage reply;
	status_t status = B_WOULD_BLOCK; // mark somehow to do not reply anything

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The port listener thread handles HCI events under this same lock
	// (DispatchToDevice), so the local devices, their remote devices and the
	// watchers are never changed by both threads at once.
	BAutolock locker(fDevicesLock);
	// SPDX-SnippetEnd

	switch (message->what) {
		case BT_MSG_ADD_DEVICE:
		{
			BString str;
			message->FindString("name", &str);

			TRACE_BT("BluetoothServer: Requested LocalDevice %s\n", str.String());

			BPath path(str.String());

			LocalDeviceImpl* lDeviceImpl
				= LocalDeviceImpl::CreateTransportAccessor(&path);

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			if (lDeviceImpl == NULL) {
				syslog(LOG_ERR, "bluetooth_server: no memory for %s",
					str.String());
				break;
			}

			if (lDeviceImpl->GetID() < 0) {
				TRACE_BT("BluetoothServer: Adding LocalDevice hci id invalid\n");
				delete lDeviceImpl;
				break;
			}

			fLocalDevicesList.AddItem(lDeviceImpl);
			// SPDX-SnippetEnd

			TRACE_BT("LocalDevice %s id=%" B_PRId32 " added\n", str.String(),
				lDeviceImpl->GetID());

			status = B_WOULD_BLOCK;
			/* TODO: This should be by user request only! */
			lDeviceImpl->Launch();
		}
		break;

		case BT_MSG_REMOVE_DEVICE:
		{
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// By hci_id (the device unregisters itself), or by device path
			// (DeviceManager, when the entry leaves /dev/bluetooth).
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			const char* path;
			if (lDeviceImpl == NULL
				&& message->FindString("name", &path) == B_OK) {
				for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++) {
					LocalDeviceImpl* candidate = fLocalDevicesList.ItemAt(i);
					if (candidate->Path() == path) {
						lDeviceImpl = candidate;
						break;
					}
				}
			}
			if (lDeviceImpl != NULL) {
				fLocalDevicesList.RemoveItem(lDeviceImpl);
				delete lDeviceImpl;
			}
			// SPDX-SnippetEnd
		}
		break;

		case BT_MSG_COUNT_LOCAL_DEVICES:
			status = HandleLocalDevicesCount(message, &reply);
			break;

		case BT_MSG_ACQUIRE_LOCAL_DEVICE:
			status = HandleAcquireLocalDevice(message, &reply);
			break;

		case BT_MSG_HANDLE_SIMPLE_REQUEST:
			status = HandleSimpleRequest(message, &reply);
			break;

		case BT_MSG_GET_PROPERTY:
			status = HandleGetProperty(message, &reply);
			break;

		// Handle if the bluetooth preferences is running?
		case B_SOME_APP_LAUNCHED:
		{
			const char* signature;

			if (message->FindString("be:signature", &signature) == B_OK) {
				printf("input_server : %s\n", signature);
				if (strcmp(signature, "application/x-vnd.Be-TSKB") == 0) {

				}
			}
		}
		return;

		case BT_REQ_CREATE_CONN:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			lDeviceImpl->CreateConnection(message);
			break;
		}

		case BT_REQ_CANCEL_CONN:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			lDeviceImpl->CancelConnection(message);
		}
		break;

		case BT_REQ_DISCONNECT:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			lDeviceImpl->Disconnect(message);
		}
		break;

		case BT_REQ_REMOVE_DEVICE:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			lDeviceImpl->Disconnect(message);

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			const bdaddr_t* bdaddr;
			if (!_FindAddress(message, &bdaddr))
				break;

			ServerRemoteDevice* rd = lDeviceImpl->RemoteDeviceByAddr(*bdaddr);
			if (rd != NULL)
				lDeviceImpl->RemoveRemoteDevice(rd);
			// SPDX-SnippetEnd
		}
		break;

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		case BT_START_WATCHING_CONNECTIONS:
		{
			BMessenger* messenger = new(std::nothrow) BMessenger();
			if (messenger == NULL)
				break;
			if (message->FindMessenger("messenger", messenger) != B_OK) {
				delete messenger;
				break;
			}
			fWatchersList.AddItem(messenger);
		}
		break;

		case BT_STOP_WATCHING_CONNECTIONS:
		{
			// The list holds its own copies, so match by value.
			BMessenger messenger;
			if (message->FindMessenger("messenger", &messenger) != B_OK)
				break;
			for (int32 i = fWatchersList.CountItems() - 1; i >= 0; i--) {
				if (*fWatchersList.ItemAt(i) == messenger)
					delete fWatchersList.RemoveItemAt(i);
			}
		}
		break;
		// SPDX-SnippetEnd

		case BT_MSG_GET_REMOTE_DEVICES:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// The caller waits for a reply; an unknown radio gets an empty
			// list with an error status instead of no answer.
			if (lDeviceImpl == NULL) {
				status = B_BAD_VALUE;
				break;
			}
			// SPDX-SnippetEnd

			RemoteDevicesList* rdList = lDeviceImpl->GetRemoteDevicesList();

			for (int32 i = 0; i < rdList->CountItems(); i++) {
				ServerRemoteDevice* rd = rdList->ItemAt(i);
				BMessage device;
				bdaddr_t bdaddr = rd->bdaddr;
				device.AddData("bdaddr", B_ANY_TYPE, &bdaddr, sizeof(bdaddr_t));
				device.AddString("name", rd->friendly_name);
				device.AddData("class_of_device", B_RAW_TYPE, rd->classOfDevice,
					sizeof(rd->classOfDevice));
				device.AddUInt16("clock_offset", rd->clock_offset);
				device.AddUInt8("pscan_rep_mode", rd->pscan_rep_mode);

				reply.AddMessage("remote", &device);
			}

			message->SendReply(&reply);
		}
		break;

		case BT_REQ_CONN_STATE:
		{
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// RemoteDevice::GetConnectionState() waits for this reply and
			// reads "conn state". An unknown radio or address answers
			// DISCONNECTED with an error status, instead of no reply or a
			// NULL dereference.
			RemoteDevice::ConnectionState state = RemoteDevice::DISCONNECTED;
			status = B_BAD_VALUE;

			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			const bdaddr_t* bdaddr;
			if (lDeviceImpl != NULL && _FindAddress(message, &bdaddr)) {
				ServerRemoteDevice* rd
					= lDeviceImpl->RemoteDeviceByAddr(*bdaddr);
				if (rd != NULL) {
					state = rd->conn_state;
					status = B_OK;
				}
			}

			reply.AddUInt8("conn state", static_cast<uint8>(state));
			// SPDX-SnippetEnd
		}
		break;

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// RemoteDevice::GetConnectionDetails() (docs/BLUETOOTH.md, step 1b).
		case BT_REQ_CONN_DETAILS:
		{
			status = B_BAD_VALUE;
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			const bdaddr_t* bdaddr;
			if (lDeviceImpl != NULL && _FindAddress(message, &bdaddr)) {
				ServerRemoteDevice* rd
					= lDeviceImpl->RemoteDeviceByAddr(*bdaddr);
				if (rd != NULL) {
					reply.AddUInt8("conn state",
						static_cast<uint8>(rd->conn_state));
					reply.AddUInt8("link type", rd->link_type);
					reply.AddBool("encrypted", rd->encryption_enabled != 0);
					if (rd->conn_state == RemoteDevice::CONNECTED)
						reply.AddUInt16("handle", rd->handle);
					status = B_OK;
				}
			}
			if (status != B_OK) {
				reply.AddUInt8("conn state",
					static_cast<uint8>(RemoteDevice::DISCONNECTED));
			}
		}
		break;
		// SPDX-SnippetEnd

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		case kMsgReportStarted:
		{
			syslog(LOG_INFO, "bluetooth_server: started, %" B_PRId32
				" radios", fLocalDevicesList.CountItems());
		}
		break;
		// SPDX-SnippetEnd

		default:
			BApplication::MessageReceived(message);
			break;
	}

	// Can we reply right now?
	// TODO: review this condition
	if (status != B_WOULD_BLOCK) {
		reply.AddInt32("status", status);
		message->SendReply(&reply);
//		printf("Sending reply message for->\n");
//		message->PrintToStream();
	}
}


void
BluetoothServer::NotifyWatchers(BMessage* notice)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	BAutolock locker(fDevicesLock);
	for (int32 i = 0; i < fWatchersList.CountItems(); i++) {
		BMessenger* messenger = fWatchersList.ItemAt(i);
		if (!messenger->IsValid()) {
			delete fWatchersList.RemoveItemAt(i);
			i--;
			continue;
		}
		// Never wait on a watcher while the lock is held.
		messenger->SendMessage(notice, (BHandler*)NULL, 0);
	}
	// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
bool
BluetoothServer::_FindAddress(BMessage* message, const bdaddr_t** _address)
{
	const void* data;
	ssize_t size;
	if (message->FindData("bdaddr", B_ANY_TYPE, &data, &size) != B_OK
		|| data == NULL || size != (ssize_t)sizeof(bdaddr_t))
		return false;

	*_address = (const bdaddr_t*)data;
	return true;
}
// SPDX-SnippetEnd


void
BluetoothServer::DiscoverServices(ServerRemoteDevice* rd)
{
	TRACE_BT("BluetoothServer: Discovering Services for %s\n", rd->friendly_name.String());
	SDPClient sdpClient(rd);
	sdpClient.Start();
	if (sdpClient.RequestServiceRecords() == B_OK)
		TRACE_BT("SDP: Service records loaded successfully\n");
	else
		TRACE_BT("SDP: Error service records could not be loaded\n");

	// rd->services.PrintToStream();
}


void
BluetoothServer::NotifyServices(ServerRemoteDevice* rd)
{
	TRACE_BT("BluetoothServer: Notifying Services for %s\n", rd->friendly_name.String());
	SDPClient sdpClient(rd);

	if (sdpClient.NotifyProfiles() == B_OK)
		TRACE_BT("SDP: Notified the dedicated profile successfully\n");
	else
		TRACE_BT("SDP: Error could not notify the dedicated profile\n");

}


#if 0
#pragma mark -
#endif

LocalDeviceImpl*
BluetoothServer::LocateDelegateFromMessage(BMessage* message)
{
	hci_id hid;

	if (message->FindInt32("hci_id", &hid) != B_OK)
		return NULL;

	return LocateLocalDeviceImpl(hid);
}


LocalDeviceImpl*
BluetoothServer::LocateLocalDeviceImpl(hci_id hid)
{
	// Try to find out when a ID was specified
	int index;

	for (index = 0; index < fLocalDevicesList.CountItems(); index++) {
		LocalDeviceImpl* lDeviceImpl = fLocalDevicesList.ItemAt(index);
		if (lDeviceImpl->GetID() == hid)
			return lDeviceImpl;
	}

	return NULL;
}


#if 0
#pragma - Messages reply
#endif

status_t
BluetoothServer::HandleLocalDevicesCount(BMessage* message, BMessage* reply)
{
	TRACE_BT("BluetoothServer: count requested\n");

	return reply->AddInt32("count", fLocalDevicesList.CountItems());
}


status_t
BluetoothServer::HandleAcquireLocalDevice(BMessage* message, BMessage* reply)
{
	hci_id hid;
	ssize_t size;
	bdaddr_t bdaddr;
	LocalDeviceImpl* lDeviceImpl = NULL;
	static int32 lastIndex = 0;

	if (message->FindInt32("hci_id", &hid) == B_OK)	{
		TRACE_BT("BluetoothServer: GetLocalDevice requested with id\n");
		lDeviceImpl = LocateDelegateFromMessage(message);

	} else if (message->FindData("bdaddr", B_ANY_TYPE,
		(const void**)&bdaddr, &size) == B_OK) {

		// Try to find out when the user specified the address
		TRACE_BT("BluetoothServer: GetLocalDevice requested with bdaddr\n");
		for (lastIndex = 0; lastIndex < fLocalDevicesList.CountItems();
			lastIndex ++) {
			// TODO: Only possible if the property is available
			// bdaddr_t local;
			// lDeviceImpl = fLocalDevicesList.ItemAt(lastIndex);
			// if ((lDeviceImpl->GetAddress(&local, message) == B_OK)
			// 	&& bacmp(&local, &bdaddr)) {
			// 	break;
			// }
		}

	} else {
		// Careless, any device not performing operations will be fine
		TRACE_BT("BluetoothServer: GetLocalDevice plain request\n");
		// from last assigned till end
		for (int index = lastIndex + 1;
			index < fLocalDevicesList.CountItems();	index++) {
			lDeviceImpl= fLocalDevicesList.ItemAt(index);
			if (lDeviceImpl != NULL && lDeviceImpl->Available()) {
				printf("Requested local device %" B_PRId32 "\n",
					lDeviceImpl->GetID());
				TRACE_BT("BluetoothServer: Device available: %" B_PRId32 "\n", lDeviceImpl->GetID());
				lastIndex = index;
				break;
			}
		}

		// from starting till last assigned if not yet found
		if (lDeviceImpl == NULL) {
			for (int index = 0; index <= lastIndex ; index ++) {
				lDeviceImpl = fLocalDevicesList.ItemAt(index);
				if (lDeviceImpl != NULL && lDeviceImpl->Available()) {
					printf("Requested local device %" B_PRId32 "\n",
						lDeviceImpl->GetID());
					TRACE_BT("BluetoothServer: Device available: %" B_PRId32 "\n", lDeviceImpl->GetID());
					lastIndex = index;
					break;
				}
			}
		}
	}

	if (lastIndex <= fLocalDevicesList.CountItems() && lDeviceImpl != NULL
		&& lDeviceImpl->Available()) {

		hid = lDeviceImpl->GetID();
		lDeviceImpl->Acquire();

		TRACE_BT("BluetoothServer: Device acquired %" B_PRId32 "\n", hid);
		return reply->AddInt32("hci_id", hid);
	}

	return B_ERROR;

}


status_t
BluetoothServer::HandleSimpleRequest(BMessage* message, BMessage* reply)
{
	LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
	if (lDeviceImpl == NULL) {
		return B_ERROR;
	}

	const char* propertyRequested;

	// Find out if there is a property being requested,
	if (message->FindString("property", &propertyRequested) == B_OK) {
		// Check if the property has been already retrieved
		if (lDeviceImpl->IsPropertyAvailable(propertyRequested)) {
			// Dump everything
			reply->AddMessage("properties", lDeviceImpl->GetPropertiesMessage());
			return B_OK;
		}
	}

	// we are gonna need issue the command ...
	if (lDeviceImpl->ProcessSimpleRequest(DetachCurrentMessage()) == B_OK)
		return B_WOULD_BLOCK;
	else {
		lDeviceImpl->Unregister();
		return B_ERROR;
	}

}


status_t
BluetoothServer::HandleGetProperty(BMessage* message, BMessage* reply)
{
	// User side will look for the reply in a result field and will
	// not care about status fields, therefore we return OK in all cases

	LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
	if (lDeviceImpl == NULL) {
		return B_ERROR;
	}

	const char* propertyRequested;

	// Find out if there is a property being requested,
	if (message->FindString("property", &propertyRequested) == B_OK) {

		TRACE_BT("BluetoothServer: Searching %s property...\n", propertyRequested);

		// Check if the property has been already retrieved
		if (lDeviceImpl->IsPropertyAvailable(propertyRequested)) {

			// 1 bytes requests
			if (strcmp(propertyRequested, "hci_version") == 0
				|| strcmp(propertyRequested, "lmp_version") == 0
				|| strcmp(propertyRequested, "sco_mtu") == 0) {

				uint8 result = lDeviceImpl->GetPropertiesMessage()->
					FindInt8(propertyRequested);
				reply->AddInt32("result", result);

			// 2 bytes requests
			} else if (strcmp(propertyRequested, "hci_revision") == 0
					|| strcmp(propertyRequested, "lmp_subversion") == 0
					|| strcmp(propertyRequested, "manufacturer") == 0
					|| strcmp(propertyRequested, "acl_mtu") == 0
					|| strcmp(propertyRequested, "acl_max_pkt") == 0
					|| strcmp(propertyRequested, "sco_max_pkt") == 0
					|| strcmp(propertyRequested, "packet_type") == 0 ) {

				uint16 result = lDeviceImpl->GetPropertiesMessage()->
					FindInt16(propertyRequested);
				reply->AddInt32("result", result);

			// 1 bit requests
			} else if (strcmp(propertyRequested, "role_switch_capable") == 0
					|| strcmp(propertyRequested, "encrypt_capable") == 0) {

				bool result = lDeviceImpl->GetPropertiesMessage()->
					FindBool(propertyRequested);

				reply->AddInt32("result", result);



			} else {
				TRACE_BT("BluetoothServer: Property %s could not be satisfied\n", propertyRequested);
			}
		}
	}

	return B_OK;
}


#if 0
#pragma mark -
#endif


void
BluetoothServer::ShowWindow(BWindow* pWindow)
{
	pWindow->Lock();
	if (pWindow->IsHidden())
		pWindow->Show();
	else
		pWindow->Activate();
	pWindow->Unlock();
}


void
BluetoothServer::_InstallDeskbarIcon()
{
	app_info appInfo;
	be_app->GetAppInfo(&appInfo);

	BDeskbar deskbar;

	if (deskbar.HasItem(kDeskbarItemName)) {
		_RemoveDeskbarIcon();
	}

	status_t res = deskbar.AddItem(&appInfo.ref);
	if (res != B_OK)
		TRACE_BT("Failed adding deskbar icon: %" B_PRId32 "\n", res);
}


void
BluetoothServer::_RemoveDeskbarIcon()
{
	BDeskbar deskbar;
	status_t res = deskbar.RemoveItem(kDeskbarItemName);
	if (res != B_OK)
		TRACE_BT("Failed removing Deskbar icon: %" B_PRId32 ": \n", res);
}


#if 0
#pragma mark -
#endif

int
main(int /*argc*/, char** /*argv*/)
{
	BluetoothServer* bluetoothServer = new BluetoothServer;

	bluetoothServer->Run();
	delete bluetoothServer;

	return 0;
}

