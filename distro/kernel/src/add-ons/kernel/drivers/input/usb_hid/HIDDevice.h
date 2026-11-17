/*
 * Copyright 2008-2011, Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */
#ifndef USB_HID_DEVICE_H
#define USB_HID_DEVICE_H


#include "HIDParser.h"

#include <USB3.h>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <KernelExport.h>
#include <joystick_driver.h>
#include <lock.h>

#include "R5HIDReports.h"

// What usb_hid does besides HID (HIDDevice::fR5Kind).
enum {
	R5_USB_KIND_PLAIN = 0,
	R5_USB_KIND_SWITCH,
	R5_USB_KIND_GIP
};
// SPDX-SnippetEnd


class ProtocolHandler;


class HIDDevice {
public:
								HIDDevice(usb_device device,
									const usb_configuration_info *config,
									size_t interfaceIndex, int32 quirkyIndex);
								~HIDDevice();

			void				SetParentCookie(int32 cookie);
			int32				ParentCookie() const { return fParentCookie; }

			status_t			InitCheck() const { return fStatus; }

			bool				IsOpen() const { return fOpenCount > 0; }
			status_t			Open(ProtocolHandler *handler, uint32 flags);
			status_t			Close(ProtocolHandler *handler);
			int32				OpenCount() const { return fOpenCount; }

			void				Removed();
			bool				IsRemoved() const { return fRemoved; }

			status_t			MaybeScheduleTransfer(HIDReport*);

			status_t			SendReport(HIDReport *report);

			HIDParser &			Parser() { return fParser; }
			ProtocolHandler *	ProtocolHandlerAt(uint32 index) const;

			// only to be used for the kernel debugger information
			usb_pipe			InterruptPipe() const { return fInterruptPipe; }

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// R5_JOYSTICK_GET_INFO, called by hid_shared: bus, vendor and
			// product ID, version and the product name.
			void				GetJoystickInfo(r5_joystick_info& info) const;

			// R5_JOYSTICK_SEND_REPORT, R5_JOYSTICK_GET_REPORT and
			// R5_JOYSTICK_GET_DESCRIPTOR, called by hid_shared with kernel
			// buffers it checked against the descriptor. type is
			// HID_REPORT_TYPE_*; with report IDs data starts with the ID.
			status_t			R5SendReport(uint8 type, const uint8* data,
									size_t length);
			status_t			R5GetReport(uint8 type, uint8 id, uint8* data,
									size_t capacity, size_t* length,
									bigtime_t* when);
			status_t			R5GetDescriptor(uint8* data, size_t capacity,
									size_t* length) const;
			// SPDX-SnippetEnd

private:
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			void				_ReadProductName(uint8 index);
			status_t			_R5InterruptOut(const uint8* data,
									size_t length);
	static	void				_R5OutCallback(void* cookie, status_t status,
									void* data, size_t actualLength);
			// Phase 2 (docs/GAME-CONTROLLERS.md): the Switch Pro Controller
			// and Xbox controllers over USB (GIP) get R5 report descriptors
			// (QuirkyDevices.cpp, R5HIDReports.h); their start commands go
			// out once when the device is added, their packets are turned
			// into those reports in _TransferCallback().
			void				_R5Start();
			void				_R5StartSwitch();
			bool				_R5WaitFor(uint8 id, uint8 offset, uint8 value);
			status_t			_R5InterruptIn(uint8* data, size_t capacity,
									size_t* length, bigtime_t timeout);
			void				_R5QueueOut(const uint8* data, size_t length);
			size_t				_R5Translate(size_t length);
			// SPDX-SnippetEnd

	static	void				_TransferCallback(void *cookie,
									status_t status, void *data,
									size_t actualLength);
	static	void				_UnstallCallback(void *cookie,
									status_t status, void *data,
									size_t actualLength);

private:
			status_t			fStatus;
			usb_device			fDevice;
			usb_pipe			fInterruptPipe;
			size_t				fInterfaceIndex;
			uint8				fEndpointAddress;

			int32				fTransferScheduled;
			size_t				fTransferBufferSize;
			uint8 *				fTransferBuffer;

			int32				fParentCookie;
			int32				fOpenCount;
			bool				fRemoved;

			HIDParser			fParser;

			uint32				fProtocolHandlerCount;
			ProtocolHandler *	fProtocolHandlerList;

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			uint16				fVendorID;
			uint16				fProductID;
			uint16				fVersion;
			char				fProductName[R5_JOYSTICK_NAME_LENGTH];

			// A copy of the report descriptor (NULL when it was larger than
			// R5_JOYSTICK_MAX_DESCRIPTOR), the interrupt OUT pipe (0: none,
			// then SET_REPORT), one report out at a time, and the last
			// input report per ID, filled in _TransferCallback().
			uint8*				fR5Descriptor;
			size_t				fR5DescriptorLength;
			usb_pipe			fR5OutPipe;
			mutex				fR5OutLock;
			spinlock			fR5CacheLock;
			r5_hid_report_cache	fR5Cache;

			// R5_USB_KIND_*: a plain HID device, a Switch Pro Controller, a GIP
			// controller. fR5Gip is only used in the constructor and in
			// _TransferCallback(), which the stack calls one at a time.
			uint8				fR5Kind;
			r5_hid_gip_state	fR5Gip;
			// SPDX-SnippetEnd
};


#endif // USB_HID_DEVICE_H
