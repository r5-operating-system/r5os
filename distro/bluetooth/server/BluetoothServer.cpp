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
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <File.h>
#include <FindDirectory.h>

class BMessage;
#include <R5Admin.h>
static bool radio_sender_allowed(const r5_admin_sender& sender);
static void admin_request(BMessage* request, const r5_admin_sender& sender,
	BMessage* reply, void* cookie);
// SPDX-SnippetEnd
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
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <driver_settings.h>
// SPDX-SnippetEnd

#include <bluetoothserver_p.h>
#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetooth/bluetooth.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <bluetooth/LinkKeyUtils.h>
// SPDX-SnippetEnd

#include "BluetoothServer.h"
#include "Debug.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "LEManager.h"
#include "PasskeyWindow.h"
#include "HFPGateway.h"
#include "RFCOMMMux.h"

#include <MessageRunner.h>

// The Hands-Free unit's service class (Bluetooth Assigned Numbers), whose
// SDP record names the RFCOMM channel the Audio Gateway connects to.
static const uint16 kR5ServiceHandsFree = 0x111e;
// SPDX-SnippetEnd
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
		header, size);

	return B_OK;
}


void
BluetoothServer::DispatchToDevice(hci_id hid, struct hci_event_header* header,
	size_t size)
{
	BAutolock locker(fDevicesLock);

	// fetch the LocalDevice who belongs this event
	LocalDeviceImpl* lDeviceImplementation = LocateLocalDeviceImpl(hid);
	if (lDeviceImplementation == NULL) {
		TRACE_BT("BluetoothServer: LocalDevice could not be fetched\n");
		return;
	}

	lDeviceImplementation->HandleEvent(header, size);
}
// SPDX-SnippetEnd


BluetoothServer::BluetoothServer()
	:
	BApplication(BLUETOOTH_SIGNATURE),
	fDevicesLock("bluetooth local devices"),
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	fRadioEnabled(true),
	fAudioTeam(-1),
	fAdminPort(-1)
	// SPDX-SnippetEnd
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Before the first radio is added, so it is set up with the saved choice.
	_LoadRadioSetting();
	// SPDX-SnippetEnd

	fDeviceManager = new DeviceManager();
	fLocalDevicesList.MakeEmpty();
	fWatchersList.MakeEmpty();

	fEventListener2 = new BluetoothPortListener(BT_USERLAND_PORT_NAME,
		(BluetoothPortListener::port_listener_func)&DispatchEvent);
	fSDPServer = new SDPServer();
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	fHIDHost = new HIDHost();
	{
		HFPGateway::Options options = HFPGateway::DefaultOptions();
		options.connected = _R5DeviceConnected;
		// Step 7c: with bt_voice the microphone opens the voice link.
		if (_R5VoiceEnabled()) {
			options.voice = _R5Voice;
			options.codecNegotiation = true;
		}
		fHFPGateway = new HFPGateway(options);
	}
	fLEPulse = NULL;
	// SPDX-SnippetEnd
}


bool BluetoothServer::QuitRequested(void)
{
	LocalDeviceImpl* lDeviceImpl = NULL;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Its thread locks this looper: stopped before the looper goes.
	if (fAdminPort >= 0) {
		port_id adminPort = fAdminPort;
		fAdminPort = -1;
		Unlock();
		r5_admin_port_stop(adminPort);
		Lock();
	}
	// SPDX-SnippetEnd
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// HFP first, while the links are up: its sessions end at once (step 7b).
	fHFPGateway->Stop();
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

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	delete fLEPulse;
	fLEPulse = NULL;
	LEManager::StopChannels();
	fHIDHost->Stop();
	delete fHIDHost;
	fHIDHost = NULL;
	delete fHFPGateway;
	fHFPGateway = NULL;
	// SPDX-SnippetEnd

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
	if (r5_admin_port_start("bluetooth admin", admin_request, this,
			&fAdminPort) != B_OK) {
		fAdminPort = -1;
		syslog(LOG_ERR, "bluetooth_server: no admin port, the radio switch "
			"and audio cannot be used");
	}
	// SPDX-SnippetEnd

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Classic HID: listens on PSM 0x11 and 0x13 (logs when the driver is
	// missing; Bluetooth works without it).
	fHIDHost->Start();
	// BLE: the ATT and SMP sockets, and LE keyboard lights (step 6).
	fHIDHost->SetLEOutputHook(LEManager::HIDOutput, NULL);
	LEManager::StartChannels();
	// The SMP and ATT timeouts are checked every 5 s.
	BMessage pulse(kMsgR5LEPulse);
	r5_admin_mark_local(&pulse);
	fLEPulse = new(std::nothrow) BMessageRunner(be_app_messenger, &pulse,
		5000000);
	// SPDX-SnippetEnd

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

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// Simple Pairing Mode and the Event Mask (step 4), after an HCI
			// Reset: a radio that comes from a warm boot (Windows, or the
			// last R5 boot) keeps its settings and links otherwise.
			lDeviceImpl->ResetController();
			// SPDX-SnippetEnd

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// The radio switch is off: once the radio is up, it stays
			// invisible and does not answer pages either.
			if (!fRadioEnabled)
				lDeviceImpl->WriteScanEnable(0);
			// SPDX-SnippetEnd
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
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			if (!fRadioEnabled) {
				status = B_NOT_ALLOWED;
				break;
			}
			// SPDX-SnippetEnd

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
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// LE pairing windows answer here; only from this process
		// (PasskeyWindow), never from another team. The reply address
		// proves nothing; the team token does (r5_admin_mark_local()).
		case kMsgLEUserAnswer:
			if (r5_admin_is_local(message))
				LEManager::UserAnswer(message);
			break;

		case kMsgR5LEPulse:
			if (r5_admin_is_local(message)) {
				LEManager::Pulse();
				// A Remote Name Request without an answer in time.
				for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++)
					fLocalDevicesList.ItemAt(i)->R5NamePulse();
			}
			break;
		// SPDX-SnippetEnd

		case BT_MSG_R5_GET_RADIO:
			status = reply.AddBool("enabled", fRadioEnabled);
			// The radios by hci_id, so a client acquires exactly the ones it
			// does not know yet (the plain BT_MSG_ACQUIRE_LOCAL_DEVICE hands
			// them out in turn).
			for (int32 i = 0; status == B_OK
					&& i < fLocalDevicesList.CountItems(); i++) {
				status = reply.AddInt32("hci_id",
					fLocalDevicesList.ItemAt(i)->GetID());
			}
			break;

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Forget a paired device: like Pair, for every signed-in person.
		case BT_MSG_R5_FORGET:
		{
			const void* data;
			ssize_t size;
			if (message->FindData("bdaddr", B_ANY_TYPE, &data, &size) != B_OK
				|| size != (ssize_t)sizeof(bdaddr_t)) {
				status = B_BAD_VALUE;
				break;
			}
			bool forgotten = false;
			for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++) {
				if (fLocalDevicesList.ItemAt(i)->R5Forget(
						*(const bdaddr_t*)data))
					forgotten = true;
			}
			reply.AddBool("forgotten", forgotten);
			status = B_OK;
			break;
		}

		// My devices on the Bluetooth page: names and states, no keys.
		case BT_MSG_R5_GET_PAIRED:
		{
			for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++)
				fLocalDevicesList.ItemAt(i)->R5AddPaired(reply);
			status = B_OK;
			break;
		}

		case BT_MSG_R5_DISCONNECT:
		{
			const void* data;
			ssize_t size;
			if (message->FindData("bdaddr", B_ANY_TYPE, &data, &size) != B_OK
				|| size != (ssize_t)sizeof(bdaddr_t)) {
				status = B_BAD_VALUE;
				break;
			}
			bool disconnected = false;
			for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++) {
				if (fLocalDevicesList.ItemAt(i)->R5Disconnect(
						*(const bdaddr_t*)data))
					disconnected = true;
			}
			reply.AddBool("disconnected", disconnected);
			status = B_OK;
			break;
		}
		// SPDX-SnippetEnd

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Decided by the sender: only on the admin port (R5AdminRequest()),
		// where the kernel names it. Here the reply address is whatever the
		// sender chose.
		case BT_MSG_R5_SET_RADIO:
		case BT_MSG_R5_AUDIO_STREAMING:
		case BT_MSG_R5_AUDIO_PAIRED:
		case BT_MSG_R5_AUDIO_CONNECT:
		case BT_MSG_R5_AUDIO_VOICE:
			status = B_NOT_ALLOWED;
			reply.AddString("text", "Send this with r5_admin_send().");
			break;

		case R5_ADMIN_PORT_REQUEST:
			r5_admin_port_reply(message, fAdminPort);
			break;
		// SPDX-SnippetEnd

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


// The radio switch (BT_MSG_R5_SET_RADIO). HCI has no power off; while the
// switch is off, every radio has inquiry scan and page scan off, and the
// commands that scan or page are refused here.
bool
BluetoothServer::_RadioRequestAllowed(BMessage* message) const
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// While bluetooth_audio streams, an inquiry would take the radio's time
	// slots from the audio link and make it stutter: scans wait for the end
	// of the stream (docs/BLUETOOTH.md, step 5).
	if (fRadioEnabled && AudioStreaming()) {
		const void* raw;
		ssize_t rawSize;
		if (message->FindData("raw command", B_ANY_TYPE, &raw, &rawSize)
				== B_OK && raw != NULL
			&& rawSize >= (ssize_t)sizeof(struct hci_command_header)) {
			uint16 opcode = B_LENDIAN_TO_HOST_INT16(
				((const struct hci_command_header*)raw)->opcode);
			if (opcode == PACK_OPCODE(OGF_LINK_CONTROL, OCF_INQUIRY)
				|| opcode == PACK_OPCODE(OGF_LINK_CONTROL, 0x0003)) {
				syslog(LOG_INFO, "bluetooth_server: scan paused while audio "
					"streams");
				return false;
			}
		}
	}
	// SPDX-SnippetEnd

	const void* data;
	ssize_t size;
	if (message->FindData("raw command", B_ANY_TYPE, &data, &size) != B_OK
		|| data == NULL || size < (ssize_t)sizeof(struct hci_command_header))
		return true;

	// Never from a client: the testing commands (OGF 0x06), among them
	// Write Simple Pairing Debug Mode, after which Classic pairing uses the
	// published debug key and a listener can work out the link keys.
	const uint16 rawOpcode = B_LENDIAN_TO_HOST_INT16(
		((const struct hci_command_header*)data)->opcode);
	if ((rawOpcode >> 10) == 0x06)
		return false;

	if (fRadioEnabled)
		return true;

	// While the switch is off, LE does not scan, connect or advertise
	// either (Core 6.0, Vol 4, Part E, 7.8).
	switch (rawOpcode) {
		case 0x200a:	// LE Set Advertising Enable
		case 0x200c:	// LE Set Scan Enable
		case 0x200d:	// LE Create Connection
		case 0x2039:	// LE Set Extended Advertising Enable
		case 0x2042:	// LE Set Extended Scan Enable
		case 0x2043:	// LE Extended Create Connection
			return false;
	}

	const struct hci_command_header* header
		= (const struct hci_command_header*)data;
	switch (B_LENDIAN_TO_HOST_INT16(header->opcode)) {
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_INQUIRY):
		case PACK_OPCODE(OGF_LINK_CONTROL, 0x0003):
			// Periodic Inquiry Mode (Core 6.0, Vol 4, Part E, 7.1.3)
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_CREATE_CONN):
		case PACK_OPCODE(OGF_LINK_CONTROL, OCF_REMOTE_NAME_REQUEST):
			return false;

		case PACK_OPCODE(OGF_CONTROL_BASEBAND, OCF_WRITE_SCAN_ENABLE):
			// Only "no scans" may be written.
			if (size < (ssize_t)(sizeof(struct hci_command_header) + 1))
				return false;
			return ((const uint8*)(header + 1))[0] == 0;
	}

	return true;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Whether bluetooth_audio streams: it said so, and its team still lives (a
// media_addon_server that crashed never says it stopped).
bool
BluetoothServer::AudioStreaming() const
{
	team_info info;
	return fAudioTeam >= 0 && get_team_info(fAudioTeam, &info) == B_OK;
}
// SPDX-SnippetEnd


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// The admin port (libr5admin.so): the radio switch and bluetooth_audio's
// requests, with the sender the kernel named.
static void
admin_request(BMessage* request, const r5_admin_sender& sender,
	BMessage* reply, void* cookie)
{
	BluetoothServer* server = (BluetoothServer*)cookie;
	if (!server->Lock())
		return;
	reply->AddInt32("status", server->R5AdminRequest(request, *reply,
		sender));
	server->Unlock();
}


status_t
BluetoothServer::R5AdminRequest(BMessage* message, BMessage& reply,
	const r5_admin_sender& sender)
{
	BAutolock locker(fDevicesLock);
	status_t status = B_BAD_VALUE;
	switch (message->what) {
		case BT_MSG_R5_SET_RADIO:
		{
			bool enabled;
			if (message->FindBool("enabled", &enabled) != B_OK) {
				status = B_BAD_VALUE;
				break;
			}
			// Any program could send this; the page's lock is not enough.
			if (!radio_sender_allowed(sender)) {
				syslog(LOG_NOTICE, "bluetooth_server: radio switch refused: "
					"the sender is not an administrator");
				status = B_NOT_ALLOWED;
				reply.AddBool("enabled", fRadioEnabled);
				break;
			}
			status = _SetRadioEnabled(enabled);
			reply.AddBool("enabled", fRadioEnabled);
			break;
		}

		case BT_MSG_R5_AUDIO_VOICE:
		{
			// Step 7c: the headset microphone, from bluetooth_audio (uid 0).
			bool on;
			if (sender.uid != 0) {
				status = B_NOT_ALLOWED;
				break;
			}
			if (message->FindBool("on", &on) != B_OK) {
				status = B_BAD_VALUE;
				break;
			}
			if (fHFPGateway != NULL)
				fHFPGateway->SetVoiceWanted(on);
			status = B_OK;
			break;
		}

		case BT_MSG_R5_AUDIO_STREAMING:
		case BT_MSG_R5_AUDIO_PAIRED:
		case BT_MSG_R5_AUDIO_CONNECT:
		{
			// From bluetooth_audio in media_addon_server, a system service.
			team_id team = sender.team;
			if (sender.uid != 0) {
				status = B_NOT_ALLOWED;
				break;
			}
			if (message->what == BT_MSG_R5_AUDIO_STREAMING) {
				bool streaming;
				if (message->FindBool("streaming", &streaming) != B_OK) {
					status = B_BAD_VALUE;
					break;
				}
				fAudioTeam = streaming ? team : -1;
				// An LE scan takes the shared radio's time as an inquiry
				// does: it pauses too and comes back after the stream.
				for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++) {
					LEManager* le = fLocalDevicesList.ItemAt(i)->LE();
					if (le != NULL)
						le->AudioChanged(streaming);
				}
				status = B_OK;
				break;
			}

			const bdaddr_t* address;
			if (!_FindAddress(message, &address)) {
				status = B_BAD_VALUE;
				break;
			}
			bool paired = false;
			bool connected = false;
			bool encrypted = false;
			LocalDeviceImpl* radio = NULL;
			for (int32 i = 0; !paired
					&& i < fLocalDevicesList.CountItems(); i++) {
				ServerRemoteDevice* rd
					= fLocalDevicesList.ItemAt(i)->RemoteDeviceByAddr(
						*address);
				linkkey_t nullKey = LinkKeyUtils::NullKey();
				paired = rd != NULL
					&& !LinkKeyUtils::Compare(&rd->link_key, &nullKey);
				if (paired) {
					radio = fLocalDevicesList.ItemAt(i);
					connected = rd->conn_state == RemoteDevice::CONNECTED;
					encrypted = connected && rd->encryption_enabled != 0;
				}
			}

			if (message->what == BT_MSG_R5_AUDIO_CONNECT) {
				// Only a paired headset, and no pairing: the stored link
				// key authenticates it, and the server turns encryption on
				// as for every connection.
				if (!paired || radio == NULL) {
					status = B_NOT_ALLOWED;
					break;
				}
				if (!connected && fRadioEnabled) {
					BMessage request(BT_REQ_CREATE_CONN);
					request.AddData("bdaddr", B_ANY_TYPE, address,
						sizeof(bdaddr_t));
					request.AddBool("r5:reconnect", true);
					radio->CreateConnection(&request);
				}
				status = fRadioEnabled ? B_OK : B_NOT_ALLOWED;
				break;
			}

			reply.AddBool("connected", connected);
			reply.AddBool("encrypted", encrypted);
			status = reply.AddBool("paired", paired);
			break;
		}

		default:
			break;
	}
	return status;
}
// SPDX-SnippetEnd


// Whether the sender of a radio switch request may change a machine
// setting: uid 0, a member of group "admin", or a team that Login gave a
// grant (the administrator lock opened by a standard account). The same
// rule as sender_may_change() in distro/users/AccountAdmin.cpp; Login keeps
// its grants in /boot/system/settings/r5-machine-unlock, readable by all.
static bool
radio_sender_allowed(const r5_admin_sender& sender)
{
	// The kernel named this sender (admin port): uid, team and the grant
	// are its own, not a reply address it chose.
	team_id team = sender.team;
	if (sender.uid == 0)
		return true;

	struct passwd* account = getpwuid(sender.uid);
	struct group* admin = getgrnam("admin");
	if (account != NULL && account->pw_name != NULL && admin != NULL
		&& admin->gr_mem != NULL) {
		for (int32 i = 0; admin->gr_mem[i] != NULL; i++) {
			if (strcmp(admin->gr_mem[i], account->pw_name) == 0)
				return true;
		}
	}

	FILE* grants = fopen("/boot/system/settings/r5-machine-unlock", "r");
	if (grants == NULL)
		return false;
	bool granted = false;
	long grantTeam;
	long long until;
	time_t now = time(NULL);
	while (!granted
		&& fscanf(grants, " team %ld until %lld", &grantTeam, &until) == 2) {
		granted = grantTeam == (long)team && until > (long long)now;
	}
	fclose(grants);
	return granted;
}


// The server's own settings: the radio switch and the pairing keys. It runs
// as a system service (uid 0, distro/bluetooth/launch), so the directory is
// root's alone, mode 0700 (docs/BLUETOOTH.md, section 4). An earlier build
// had Login make it group-writable; create also puts that back.
status_t
r5_bluetooth_settings_path(BPath& path, bool create)
{
	status_t status = find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path);
	if (status == B_OK)
		status = path.Append("bluetooth");
	if (status != B_OK || !create)
		return status;

	status = create_directory(path.Path(), 0700);
	if (status != B_OK)
		return status;
	if (geteuid() == 0) {
		chown(path.Path(), 0, 0);
		chmod(path.Path(), 0700);
	}
	return B_OK;
}


static status_t
radio_setting_path(BPath& path)
{
	return r5_bluetooth_settings_path(path, false);
}


void
BluetoothServer::_LoadRadioSetting()
{
	BPath path;
	if (radio_setting_path(path) != B_OK || path.Append("r5_radio") != B_OK)
		return;

	BFile file(path.Path(), B_READ_ONLY);
	char buffer[16] = {};
	if (file.InitCheck() != B_OK
		|| file.Read(buffer, sizeof(buffer) - 1) <= 0)
		return;

	fRadioEnabled = strncmp(buffer, "radio off", 9) != 0;
	if (!fRadioEnabled)
		syslog(LOG_INFO, "bluetooth_server: radio switch is off");
}


status_t
BluetoothServer::_SaveRadioSetting() const
{
	BPath directory;
	status_t status = r5_bluetooth_settings_path(directory, true);
	if (status != B_OK)
		return status;

	BPath path(directory);
	BPath temporary(directory);
	if (path.Append("r5_radio") != B_OK
		|| temporary.Append("r5_radio.new") != B_OK)
		return B_NO_MEMORY;

	const char* line = fRadioEnabled ? "radio on\n" : "radio off\n";
	BFile file(temporary.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	// Only the server reads it; programs ask with BT_MSG_R5_GET_RADIO.
	file.SetPermissions(S_IRUSR | S_IWUSR);
	ssize_t written = file.Write(line, strlen(line));
	if (written != (ssize_t)strlen(line))
		return written < 0 ? (status_t)written : B_IO_ERROR;
	file.Sync();
	file.Unset();

	if (rename(temporary.Path(), path.Path()) != 0)
		return errno;
	return B_OK;
}


status_t
BluetoothServer::_SetRadioEnabled(bool enabled)
{
	if (enabled == fRadioEnabled)
		return B_OK;

	fRadioEnabled = enabled;
	status_t status = _SaveRadioSetting();
	if (status != B_OK) {
		fRadioEnabled = !enabled;
		syslog(LOG_ERR, "bluetooth_server: cannot save the radio switch: %s",
			strerror(status));
		return status;
	}

	// Off: nothing can find or page the radio. On: paired devices can page
	// it again (0x02), it is still not visible to a scan (step 4).
	for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++) {
		fLocalDevicesList.ItemAt(i)->WriteScanEnable(enabled ? 0x02 : 0x00);
		// LE: off stops the scan and the links, on connects paired devices.
		if (fLocalDevicesList.ItemAt(i)->LE() != NULL)
			fLocalDevicesList.ItemAt(i)->LE()->RadioChanged(enabled);
	}

	syslog(LOG_INFO, "bluetooth_server: radio switch %s",
		enabled ? "on" : "off");
	return B_OK;
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
	else {
		TRACE_BT("SDP: Error service records could not be loaded\n");
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		syslog(LOG_NOTICE, "bluetooth_server: service records not read");
		// SPDX-SnippetEnd
	}

	// rd->services.PrintToStream();
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
struct r5_sdp_args {
	hci_id		hid;
	bdaddr_t	address;
	BString		name;
	uint8		deviceClass[3];
};


void
BluetoothServer::R5DiscoverServices(hci_id hid, ServerRemoteDevice* rd)
{
	r5_sdp_args* args = new(std::nothrow) r5_sdp_args;
	if (args == NULL)
		return;
	args->hid = hid;
	args->address = rd->bdaddr;
	// The probe record lacked them: the device's name never reached
	// bluetooth_hid (the Game controllers page showed "Game controller"
	// for the 8BitDo M30), and SDP could not tell a headset by its class.
	args->name = rd->friendly_name;
	memcpy(args->deviceClass, rd->classOfDevice, sizeof(args->deviceClass));
	thread_id thread = spawn_thread(_SDPThread, "bluetooth sdp",
		B_NORMAL_PRIORITY, args);
	if (thread < 0) {
		delete args;
		return;
	}
	rd->sdp_running = true;
	resume_thread(thread);
}


status_t
BluetoothServer::_SDPThread(void* data)
{
	r5_sdp_args* args = (r5_sdp_args*)data;

	// SDP into a record of its own: the real one may be gone (Forget, an
	// unpaired device that left) by the time the device answered.
	ServerRemoteDevice probe;
	probe.bdaddr = args->address;
	probe.friendly_name = args->name;
	memcpy(probe.classOfDevice, args->deviceClass,
		sizeof(probe.classOfDevice));
	bool read;
	{
		SDPClient client(&probe);
		read = client.Start() == B_OK
			&& client.RequestServiceRecords() == B_OK;
	}
	if (!read)
		syslog(LOG_NOTICE, "bluetooth_server: service records not read");
	else {
		// Step 7a: where a headset's HFP lives (step 7b connects to it).
		uint8 channel = R5RFCOMMChannel(probe.services, kR5ServiceHandsFree);
		if (channel != 0) {
			syslog(LOG_INFO, "bluetooth_server: hands-free service on "
				"RFCOMM channel %u", channel);
		} else if ((probe.classOfDevice[1] & 0x1f) == 4
			|| (probe.classOfDevice[0] | probe.classOfDevice[1]
				| probe.classOfDevice[2]) == 0) {
			// An audio device (or one of unknown class) without one: HFP
			// does not start for it.
			syslog(LOG_INFO, "bluetooth_server: hfp: no hands-free record");
		}
	}

	BluetoothServer* server = (BluetoothServer*)be_app;
	BAutolock locker(server->fDevicesLock);
	LocalDeviceImpl* radio = server->LocateLocalDeviceImpl(args->hid);
	ServerRemoteDevice* rd = radio != NULL
		? radio->RemoteDeviceByAddr(args->address) : NULL;
	if (rd != NULL) {
		rd->sdp_running = false;
		if (read)
			rd->services = probe.services;
		// Encryption came first: it left the profiles to this thread.
		if (rd->encryption_enabled != 0
			&& rd->conn_state == RemoteDevice::CONNECTED)
			server->NotifyServices(rd);
	}
	delete args;
	return B_OK;
}


uint8
BluetoothServer::R5RFCOMMChannel(const BMessage& services,
	uint16 serviceClass)
{
	BMessage record;
	for (int32 i = 0; services.FindMessage("service", i, &record) == B_OK;
			i++) {
		const void* classList;
		const void* protocolList;
		ssize_t classLength;
		ssize_t protocolLength;
		if (record.FindData("0x0001", B_RAW_TYPE, &classList, &classLength)
				!= B_OK
			|| record.FindData("0x0004", B_RAW_TYPE, &protocolList,
				&protocolLength) != B_OK
			|| classLength <= 0 || protocolLength <= 0)
			continue;
		uint8 channel = r5rfcomm::FindChannel((const uint8*)classList,
			classLength, (const uint8*)protocolList, protocolLength,
			serviceClass);
		if (channel != 0)
			return channel;
	}
	return 0;
}


status_t
BluetoothServer::R5FindRFCOMMChannel(const bdaddr_t& address,
	uint16 serviceClass, uint8* _channel)
{
	if (_channel == NULL)
		return B_BAD_VALUE;
	BAutolock locker(fDevicesLock);
	for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++) {
		ServerRemoteDevice* rd
			= fLocalDevicesList.ItemAt(i)->RemoteDeviceByAddr(address);
		if (rd == NULL)
			continue;
		*_channel = R5RFCOMMChannel(rd->services, serviceClass);
		return *_channel != 0 ? B_OK : B_NAME_NOT_FOUND;
	}
	return B_NAME_NOT_FOUND;
}


bool
BluetoothServer::_R5DeviceConnected(const bdaddr_t& address, void*)
{
	BluetoothServer* server = (BluetoothServer*)be_app;
	BAutolock locker(server->fDevicesLock);
	for (int32 i = 0; i < server->fLocalDevicesList.CountItems(); i++) {
		ServerRemoteDevice* rd
			= server->fLocalDevicesList.ItemAt(i)->RemoteDeviceByAddr(address);
		if (rd != NULL && rd->conn_state == RemoteDevice::CONNECTED
			&& rd->encryption_enabled != 0)
			return true;
	}
	return false;
}


bool
BluetoothServer::_R5VoiceEnabled()
{
	// The same file and line the h2generic driver reads (step 7c). By
	// path: a bare name would follow this service's home folder, while
	// the kernel reads the boot volume's settings.
	void* settings = load_driver_settings(
		"/boot/home/config/settings/kernel/drivers/h2generic");
	if (settings == NULL) {
		settings = load_driver_settings(
			"/boot/system/settings/kernel/drivers/h2generic");
	}
	bool enabled = get_driver_boolean_parameter(settings, "bt_voice", false,
		true);
	unload_driver_settings(settings);
	return enabled;
}


status_t
BluetoothServer::_R5Voice(const bdaddr_t& address, int codec, bool open,
	void*)
{
	BluetoothServer* server = (BluetoothServer*)be_app;
	BAutolock locker(server->fDevicesLock);
	for (int32 i = 0; i < server->fLocalDevicesList.CountItems(); i++) {
		LocalDeviceImpl* radio = server->fLocalDevicesList.ItemAt(i);
		ServerRemoteDevice* rd = radio->RemoteDeviceByAddr(address);
		if (rd == NULL || rd->conn_state != RemoteDevice::CONNECTED)
			continue;
		if (!open) {
			radio->R5VoiceClose(address);
			return B_OK;
		}
		return radio->R5VoiceOpen(address, codec);
	}
	return open ? B_NOT_ALLOWED : B_OK;
}
// SPDX-SnippetEnd


static bool
r5_service_uuid(const BMessage& services, uint16 uuid)
{
	uint8 needle[3];
	needle[0] = 0x19;
	needle[1] = (uint8)(uuid >> 8);
	needle[2] = (uint8)uuid;
	BMessage record;
	for (int32 i = 0; services.FindMessage("service", i, &record) == B_OK;
			i++) {
		const void* data;
		ssize_t size;
		if (record.FindData("0x0001", B_RAW_TYPE, &data, &size) != B_OK
			|| size < 3)
			continue;
		const uint8* bytes = (const uint8*)data;
		for (ssize_t n = 0; n + 3 <= size; n++) {
			if (memcmp(bytes + n, needle, 3) == 0)
				return true;
		}
	}
	return false;
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

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Step 7b: a headset with a Hands-Free service gets an HFP session
	// (R5 is the Audio Gateway), whatever profile the call above chose.
	uint8 channel = R5RFCOMMChannel(rd->services, kR5ServiceHandsFree);
	bool audioSink = r5_service_uuid(rd->services, 0x110b);
	if (audioSink) {
		syslog(LOG_INFO, "bluetooth_server: audio sink; A2DP opens before "
			"hands-free");
	}
	if (channel != 0 && fHFPGateway != NULL) {
		// The WH-1000XM3 refused RFCOMM (PSM 3) and then ended the link
		// when that was the first profile. Give AVDTP the first second.
		fHFPGateway->DeviceReady(rd->bdaddr, channel,
			audioSink ? 1000000 : 0);
	}
	// SPDX-SnippetEnd
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
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// FindData() hands out a pointer; it was written into a 6-byte
	// bdaddr_t on the stack.
	const void* bdaddr;
	// SPDX-SnippetEnd
	LocalDeviceImpl* lDeviceImpl = NULL;
	static int32 lastIndex = 0;

	if (message->FindInt32("hci_id", &hid) == B_OK)	{
		TRACE_BT("BluetoothServer: GetLocalDevice requested with id\n");
		lDeviceImpl = LocateDelegateFromMessage(message);

	} else if (message->FindData("bdaddr", B_ANY_TYPE, &bdaddr, &size)
			== B_OK) {

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

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (!_RadioRequestAllowed(message))
		return B_NOT_ALLOWED;
	// SPDX-SnippetEnd

	// we are gonna need issue the command ...
	if (lDeviceImpl->ProcessSimpleRequest(DetachCurrentMessage()) == B_OK)
		return B_WOULD_BLOCK;
	else {
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// A command the driver refused (a bad size from any program)
		// removed the radio for everyone. A radio that is really gone is
		// removed by the device manager.
		return B_ERROR;
		// SPDX-SnippetEnd
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
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// This added the server's own binary to Deskbar, so Deskbar loaded the
	// whole server as an add-on (docs/BLUETOOTH.md forbids that). The item
	// is now bluetooth_deskbar, which only sends messages. An item that an
	// older build left in Deskbar is removed.
	BDeskbar deskbar;
	if (deskbar.IsRunning() && deskbar.HasItem(kDeskbarItemName))
		_RemoveDeskbarIcon();
	// SPDX-SnippetEnd
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

