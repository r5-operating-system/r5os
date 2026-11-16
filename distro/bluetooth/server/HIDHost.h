/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth Classic HID host (docs/BLUETOOTH.md, step 4): the L2CAP
 * control (PSM 0x11) and interrupt (PSM 0x13) channels of a paired HID
 * device, fed into the bluetooth_hid driver (<R5BluetoothHID.h>).
 */
#ifndef R5_HID_HOST_H
#define R5_HID_HOST_H


#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>

#include <bluetooth/bluetooth.h>


class HIDHost {
public:
								HIDHost();
								~HIDHost();

			status_t			Start();
			void				Stop();

	// From SDPClient, after encryption is on: the device is paired and
	// its SDP record has this report descriptor (attribute 0x0206).
			void				DeviceReady(const bdaddr_t& address,
									const uint8* descriptor, size_t length);

	// The SDP HIDDescriptorList (attribute 0x0206, without its outer
	// sequence header, as SDPDataIO keeps it): the first report
	// descriptor. Bounds-checked; returns false on anything malformed.
	static	bool				FindReportDescriptor(const uint8* list,
									size_t listLength, const uint8** _start,
									size_t* _length);

private:
			struct Session;

	static	int32				_ListenThread(void* data);
	static	int32				_ConnectThread(void* data);
	static	int32				_ReadThread(void* data);
	static	int32				_OutputThread(void* data);

			void				_Listen(uint16 psm);
			void				_Connect(Session* session);
			void				_Read(Session* session, bool interrupt);
			void				_Output();

			Session*			_FindLocked(const bdaddr_t& address);
			Session*			_FindByKernelIDLocked(int32 id);
			Session*			_FindOrAddLocked(const bdaddr_t& address);
			void				_TryStartLocked(Session* session);
			void				_CloseLocked(Session* session);

			BLocker				fLock;
			BObjectList<Session, true> fSessions;
			int					fControlNode;
			int					fListenSockets[2];
			thread_id			fListenThreads[2];
			thread_id			fOutputThread;
	volatile bool				fQuitting;
};


#endif // R5_HID_HOST_H
