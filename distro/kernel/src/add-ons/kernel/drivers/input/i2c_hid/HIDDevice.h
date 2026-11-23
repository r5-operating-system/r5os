/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2008-2011, Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */
#ifndef I2C_HID_DEVICE_H
#define I2C_HID_DEVICE_H


#include <i2c.h>

#include "HIDParser.h"
#include "I2CHIDProtocol.h"

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <joystick_driver.h>
// SPDX-SnippetEnd


class InputReadyLine;
class ProtocolHandler;


class HIDDevice {
public:
								HIDDevice(uint16 descriptorAddress, i2c_device_interface* i2c,
									i2c_device i2cCookie,
									InputReadyLine* inputReady);
								~HIDDevice();

			status_t			InitCheck() const { return fStatus; }

			bool				IsOpen() const { return fOpenCount > 0; }
			status_t			Open(ProtocolHandler *handler, uint32 flags);
			status_t			Close(ProtocolHandler *handler);
			int32				OpenCount() const { return fOpenCount; }

			void				Removed();
			bool				IsRemoved() const { return fRemoved; }

			status_t			MaybeScheduleTransfer(HIDReport *report);

			status_t			SendReport(HIDReport *report);

			HIDParser &			Parser() { return fParser; }
			ProtocolHandler *	ProtocolHandlerAt(uint32 index) const;

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// R5_JOYSTICK_GET_INFO, called by hid_shared: the IDs of the
			// I2C HID descriptor. There is no product name on this bus.
			void				GetJoystickInfo(r5_joystick_info& info) const
								{
									info.bus = R5_JOYSTICK_BUS_I2C;
									info.vendor_id = fDescriptor.wVendorID;
									info.product_id = fDescriptor.wProductID;
									info.version = fDescriptor.wVersionID;
								}

			// R5_JOYSTICK_SEND_REPORT, R5_JOYSTICK_GET_REPORT and
			// R5_JOYSTICK_GET_DESCRIPTOR: not on this bus yet.
			status_t			R5SendReport(uint8 type, const uint8* data,
									size_t length)
								{ return B_NOT_SUPPORTED; }
			status_t			R5GetReport(uint8 type, uint8 id, uint8* data,
									size_t capacity, size_t* length,
									bigtime_t* when)
								{ return B_NOT_SUPPORTED; }
			status_t			R5GetDescriptor(uint8* data, size_t capacity,
									size_t* length) const
								{ return B_NOT_SUPPORTED; }
			// SPDX-SnippetEnd

private:
#if 0
	static	void				_TransferCallback(void *cookie,
									status_t status, void *data,
									size_t actualLength);
	static	void				_UnstallCallback(void *cookie,
									status_t status, void *data,
									size_t actualLength);
#endif
			status_t			_Reset();
			status_t			_SetPower(uint8 power);
			status_t			_WriteReport(uint8 type, uint8 id, void *data,
									size_t reportSize);
			status_t			_FetchBuffer(uint8* cmd, size_t cmdLength,
									void* buffer, size_t bufferLength);
			status_t			_FetchReport(uint8 type, uint8 id,
									size_t reportSize);
			status_t			_ExecCommand(i2c_op op, uint8* cmd,
									size_t cmdLength, void* buffer,
									size_t bufferLength);

private:
			status_t			fStatus;
			

			bigtime_t			fTransferLastschedule;
			int32				fTransferScheduled;
			size_t				fTransferBufferSize;
			uint8 *				fTransferBuffer;
			InputReadyLine*		fInputReady;

			int32				fOpenCount;
			bool				fRemoved;
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			int32				fLoggedInputs;
			// SPDX-SnippetEnd

			HIDParser			fParser;

			uint32				fProtocolHandlerCount;
			ProtocolHandler *	fProtocolHandlerList;

			uint16				fDescriptorAddress;
			i2c_hid_descriptor	fDescriptor;

			uint8*				fReportDescriptor;

			i2c_device_interface*	fI2C;
			i2c_device				fI2CCookie;
	
};


#endif // I2C_HID_DEVICE_H
