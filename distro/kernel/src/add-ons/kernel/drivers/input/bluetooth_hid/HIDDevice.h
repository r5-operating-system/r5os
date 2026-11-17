/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * One Bluetooth HID device. The transport is bluetooth_server: input
 * reports arrive through Feed(), output reports leave through the output
 * queue in Driver.cpp. The interface is the one hid_shared uses of the
 * usb_hid and i2c_hid HIDDevice.
 */
#ifndef BLUETOOTH_HID_DEVICE_H
#define BLUETOOTH_HID_DEVICE_H


#include <joystick_driver.h>
#include <lock.h>

#include "bluetooth/R5BluetoothHID.h"

#include "HIDParser.h"
#include "R5HIDReports.h"


class HIDReportItem;
class ProtocolHandler;


class HIDDevice {
public:
								HIDDevice(int32 id, void* owner,
									const uint8* descriptor,
									size_t descriptorLength);
								~HIDDevice();

			status_t			InitCheck() const { return fStatus; }

			int32				ID() const { return fID; }
			void*				Owner() const { return fOwner; }

			bool				IsOpen() const { return fOpenCount > 0; }
			status_t			Open(ProtocolHandler* handler, uint32 flags);
			status_t			Close(ProtocolHandler* handler);
			int32				OpenCount() const { return fOpenCount; }

			// Open file handles of its handlers, counted from open to free
			// under the driver lock: the device is deleted only at zero.
			void				AcquireCookie() { fCookieCount++; }
			bool				ReleaseCookie() { return --fCookieCount == 0; }
			int32				CookieCount() const { return fCookieCount; }

			void				Removed();
			bool				IsRemoved() const { return fRemoved; }

			// Called by hid_shared.
			status_t			MaybeScheduleTransfer(HIDReport* report);
			status_t			SendReport(HIDReport* report);

			// From the control node: one input report, already in kernel
			// memory, at most R5_BTHID_MAX_REPORT bytes.
			void				Feed(const uint8* data, size_t length);

			HIDParser&			Parser() { return fParser; }
			ProtocolHandler*	ProtocolHandlerAt(uint32 index) const;
			uint32				CountProtocolHandlers() const
									{ return fProtocolHandlerCount; }

			// R5_BTHID_SET_INFO from bluetooth_server, and
			// R5_JOYSTICK_GET_INFO from hid_shared (docs/GAME-CONTROLLERS.md).
			void				SetInfo(const r5_bthid_info& info);
			void				GetJoystickInfo(r5_joystick_info& info) const;

			// R5_BTHID_GET_BATTERY: the Battery Strength (Generic Device
			// Controls page 0x06, usage 0x20) of the last input report that
			// had it, 0 to 100; -1 when the descriptor has none or no such
			// report came yet.
			int32				Battery() const;

			// R5_JOYSTICK_SEND_REPORT, R5_JOYSTICK_GET_REPORT and
			// R5_JOYSTICK_GET_DESCRIPTOR (docs/GAME-CONTROLLERS.md, option C
			// phase 1), called by hid_shared with kernel buffers it checked
			// against the descriptor. Output reports go to bluetooth_server;
			// feature reports are not carried over Bluetooth yet.
			status_t			R5SendReport(uint8 type, const uint8* data,
									size_t length);
			status_t			R5GetReport(uint8 type, uint8 id, uint8* data,
									size_t capacity, size_t* length,
									bigtime_t* when);
			status_t			R5GetDescriptor(uint8* data, size_t capacity,
									size_t* length) const;

private:
			void				_Deliver(const uint8* data, size_t length);
			void				_FindBattery(const uint8* descriptor,
									size_t descriptorLength);
			void				_UpdateBattery(const uint8* data,
									size_t length);

	// Reports that came while no reader waited; the oldest is dropped
	// when it is full.
	static	const int32			kQueueLength = 16;

	struct queued_report {
		size_t	length;
		uint8	data[R5_BTHID_MAX_REPORT];
	};

			status_t			fStatus;
			int32				fID;
			void*				fOwner;

			mutex				fLock;
			bool				fTransferScheduled;
			uint8*				fTransferBuffer;
			queued_report*		fQueue;
			int32				fQueueHead;
			int32				fQueueCount;

			int32				fOpenCount;
			int32				fCookieCount;
			bool				fRemoved;

			HIDParser			fParser;

			uint32				fProtocolHandlerCount;
			ProtocolHandler*	fProtocolHandlerList;

			r5_bthid_info		fInfo;

			// A second parser of the same descriptor, only for the battery:
			// it is fed every report, so it never touches the reports and
			// items the protocol handlers read (fParser).
			HIDParser*			fBatteryParser;
			HIDReportItem*		fBatteryItem;
			uint8*				fBatteryBuffer;
			int32				fBattery;

			// A copy of the report descriptor, the last input report per
			// ID as it came (fLock), and room for the short report built
			// from a PlayStation controller's full one (R5HIDReports.h).
			uint8*				fDescriptor;
			size_t				fDescriptorLength;
			r5_hid_report_cache	fReportCache;
			size_t				fShortReportSize;

			// Phase 2: a Switch controller gets its first subcommands from
			// here (R5HIDReports.h, r5_hid_switch_init), and the Pro
			// Controller its simple report 0x3F built from each full one when
			// its descriptor has that report with the expected size.
			r5_hid_switch_init	fSwitchInit;
			bool				fSwitchStarted;
			bool				fSwitchSimple;
};


#endif // BLUETOOTH_HID_DEVICE_H
