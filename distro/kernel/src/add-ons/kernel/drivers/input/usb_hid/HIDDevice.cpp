/*
 * Copyright 2008-2011, Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */


//!	Driver for USB Human Interface Devices.


#include "Driver.h"
#include "HIDDevice.h"
#include "HIDReport.h"
#include "HIDWriter.h"
#include "ProtocolHandler.h"
#include "QuirkyDevices.h"

#include <usb/USB_hid.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <new>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <util/AutoLock.h>
// SPDX-SnippetEnd


HIDDevice::HIDDevice(usb_device device, const usb_configuration_info *config,
	size_t interfaceIndex, int32 quirkyIndex)
	:	fStatus(B_NO_INIT),
		fDevice(device),
		fInterfaceIndex(interfaceIndex),
		fTransferScheduled(0),
		fTransferBufferSize(0),
		fTransferBuffer(NULL),
		fParentCookie(-1),
		fOpenCount(0),
		fRemoved(false),
		fParser(this),
		fProtocolHandlerCount(0),
		fProtocolHandlerList(NULL),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		fVendorID(0),
		fProductID(0),
		fVersion(0),
		fR5Descriptor(NULL),
		fR5DescriptorLength(0),
		fR5OutPipe(0),
		fR5Kind(R5_USB_KIND_PLAIN)
// SPDX-SnippetEnd
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	mutex_init(&fR5OutLock, "usb_hid report out");
	B_INITIALIZE_SPINLOCK(&fR5CacheLock);
	r5_hid_cache_init(fR5Cache);
	memset(&fR5Gip, 0, sizeof(fR5Gip));
	// SPDX-SnippetEnd

	uint8 *reportDescriptor = NULL;
	size_t descriptorLength = 0;

	const usb_device_descriptor *deviceDescriptor
		= gUSBModule->get_device_descriptor(device);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	fProductName[0] = '\0';
	if (deviceDescriptor != NULL) {
		fVendorID = deviceDescriptor->vendor_id;
		fProductID = deviceDescriptor->product_id;
		fVersion = deviceDescriptor->device_version;
		_ReadProductName(deviceDescriptor->product);
	}
	// Phase 2: which devices get an R5 report descriptor (QuirkyDevices.cpp).
	const usb_interface_descriptor* r5Interface
		= config->interface[interfaceIndex].active->descr;
	if (r5_hid_is_switch_pro(fVendorID, fProductID))
		fR5Kind = R5_USB_KIND_SWITCH;
	else if (r5Interface != NULL
		&& r5Interface->interface_class == R5_HID_GIP_CLASS
		&& r5Interface->interface_subclass == R5_HID_GIP_SUBCLASS
		&& r5Interface->interface_protocol == R5_HID_GIP_PROTOCOL)
		fR5Kind = R5_USB_KIND_GIP;
	// SPDX-SnippetEnd

	HIDWriter descriptorWriter;
	bool hasFixedDescriptor = false;
	if (quirkyIndex >= 0) {
		quirky_build_descriptor quirkyBuildDescriptor
			= gQuirkyDevices[quirkyIndex].build_descriptor;

		if (quirkyBuildDescriptor != NULL
			&& quirkyBuildDescriptor(descriptorWriter) == B_OK) {

			reportDescriptor = (uint8 *)descriptorWriter.Buffer();
			descriptorLength = descriptorWriter.BufferLength();
			hasFixedDescriptor = true;
		}
	}

	if (!hasFixedDescriptor) {
		// Conforming device, find the HID descriptor and get the report
		// descriptor from the device.
		usb_hid_descriptor *hidDescriptor = NULL;

		const usb_interface_info *interfaceInfo
			= config->interface[interfaceIndex].active;
		for (size_t i = 0; i < interfaceInfo->generic_count; i++) {
			const usb_generic_descriptor &generic
				= interfaceInfo->generic[i]->generic;
			if (generic.descriptor_type == B_USB_HID_DESCRIPTOR_HID) {
				hidDescriptor = (usb_hid_descriptor *)&generic;
				descriptorLength
					= hidDescriptor->descriptor_info[0].descriptor_length;
				break;
			}
		}

		if (hidDescriptor == NULL) {
			TRACE_ALWAYS("didn't find a HID descriptor in the configuration, "
				"trying to retrieve manually\n");

			descriptorLength = sizeof(usb_hid_descriptor);
			hidDescriptor = (usb_hid_descriptor *)malloc(descriptorLength);
			if (hidDescriptor == NULL) {
				TRACE_ALWAYS("failed to allocate buffer for hid descriptor\n");
				fStatus = B_NO_MEMORY;
				return;
			}

			status_t result = gUSBModule->send_request(device,
				USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
				USB_REQUEST_GET_DESCRIPTOR,
				B_USB_HID_DESCRIPTOR_HID << 8, interfaceIndex, descriptorLength,
				hidDescriptor, &descriptorLength);

			TRACE("get hid descriptor: result: 0x%08" B_PRIx32 "; length: %lu"
				"\n", result, descriptorLength);
			if (result == B_OK) {
				descriptorLength
					= hidDescriptor->descriptor_info[0].descriptor_length;
			} else {
				descriptorLength = 256; /* XXX */
				TRACE_ALWAYS("failed to get HID descriptor, trying with a "
					"fallback report descriptor length of %lu\n",
					descriptorLength);
			}

			free(hidDescriptor);
		}

		reportDescriptor = (uint8 *)malloc(descriptorLength);
		if (reportDescriptor == NULL) {
			TRACE_ALWAYS("failed to allocate buffer for report descriptor\n");
			fStatus = B_NO_MEMORY;
			return;
		}

		status_t result = gUSBModule->send_request(device,
			USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
			USB_REQUEST_GET_DESCRIPTOR,
			B_USB_HID_DESCRIPTOR_REPORT << 8, interfaceIndex, descriptorLength,
			reportDescriptor, &descriptorLength);

		TRACE("get report descriptor: result: 0x%08" B_PRIx32 "; length: %"
			B_PRIuSIZE "\n", result, descriptorLength);
		if (result != B_OK) {
			TRACE_ALWAYS("failed tot get report descriptor\n");
			free(reportDescriptor);
			return;
		}
	} else {
		TRACE_ALWAYS("found quirky device, using patched descriptor\n");
	}

#if 1
	// save report descriptor for troubleshooting
	char outputFile[128];
	sprintf(outputFile, "/tmp/usb_hid_report_descriptor_%04x_%04x_%lu.bin",
		deviceDescriptor->vendor_id, deviceDescriptor->product_id,
		interfaceIndex);
	int fd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		write(fd, reportDescriptor, descriptorLength);
		close(fd);
	}
#endif

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Kept for R5_JOYSTICK_GET_DESCRIPTOR (HID PID force feedback in
	// libr5game); without memory for it the device works without it.
	if (descriptorLength > 0
		&& descriptorLength <= R5_JOYSTICK_MAX_DESCRIPTOR) {
		fR5Descriptor = (uint8 *)malloc(descriptorLength);
		if (fR5Descriptor != NULL) {
			memcpy(fR5Descriptor, reportDescriptor, descriptorLength);
			fR5DescriptorLength = descriptorLength;
		}
	}
	// SPDX-SnippetEnd

	status_t result = fParser.ParseReportDescriptor(reportDescriptor,
		descriptorLength);
	if (!hasFixedDescriptor)
		free(reportDescriptor);

	if (result != B_OK) {
		TRACE_ALWAYS("parsing the report descriptor failed\n");
		fStatus = result;
		return;
	}

#if 0
	for (uint32 i = 0; i < fParser.CountReports(HID_REPORT_TYPE_ANY); i++)
		fParser.ReportAt(HID_REPORT_TYPE_ANY, i)->PrintToStream();
#endif

	// find the interrupt in pipe
	usb_interface_info *interface = config->interface[interfaceIndex].active;
	for (size_t i = 0; i < interface->endpoint_count; i++) {
		usb_endpoint_descriptor *descriptor = interface->endpoint[i].descr;
		if ((descriptor->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
			&& (descriptor->attributes & USB_ENDPOINT_ATTR_MASK)
				== USB_ENDPOINT_ATTR_INTERRUPT) {
			fEndpointAddress = descriptor->endpoint_address;
			fInterruptPipe = interface->endpoint[i].handle;
			break;
		}
	}

	if (fInterruptPipe == 0) {
		TRACE_ALWAYS("didn't find a suitable interrupt pipe\n");
		return;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The interrupt OUT pipe, where a HID device takes its output reports
	// (HID 1.11, 4.4); without one they go as SET_REPORT.
	for (size_t i = 0; i < interface->endpoint_count; i++) {
		usb_endpoint_descriptor *descriptor = interface->endpoint[i].descr;
		if ((descriptor->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN) == 0
			&& (descriptor->attributes & USB_ENDPOINT_ATTR_MASK)
				== USB_ENDPOINT_ATTR_INTERRUPT) {
			fR5OutPipe = interface->endpoint[i].handle;
			break;
		}
	}
	// SPDX-SnippetEnd

	fTransferBufferSize = fParser.MaxReportSize(HID_REPORT_TYPE_INPUT);
	if (fTransferBufferSize == 0) {
		TRACE_ALWAYS("report claims a report size of 0\n");
		return;
	}

	// We pad the allocation size so that we can always read 32 bits at a time
	// (as done in HIDReportItem) without the need for an additional boundary
	// check. We don't increase the transfer buffer size though as to not expose
	// this implementation detail onto the device when scheduling transfers.
	fTransferBuffer = (uint8 *)malloc(fTransferBufferSize + 3);
	if (fTransferBuffer == NULL) {
		TRACE_ALWAYS("failed to allocate transfer buffer\n");
		fStatus = B_NO_MEMORY;
		return;
	}

	if (quirkyIndex >= 0) {
		quirky_init_function quirkyInit
			= gQuirkyDevices[quirkyIndex].init_function;

		if (quirkyInit != NULL) {
			fStatus = quirkyInit(device, config, interfaceIndex);
			if (fStatus != B_OK)
				return;
		}
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	_R5Start();
	// SPDX-SnippetEnd

	ProtocolHandler::AddHandlers(*this, fProtocolHandlerList,
		fProtocolHandlerCount);
	fStatus = B_OK;
}


HIDDevice::~HIDDevice()
{
	ProtocolHandler *handler = fProtocolHandlerList;
	while (handler != NULL) {
		ProtocolHandler *next = handler->NextHandler();
		delete handler;
		handler = next;
	}

	free(fTransferBuffer);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	free(fR5Descriptor);
	mutex_destroy(&fR5OutLock);
	// SPDX-SnippetEnd
}


void
HIDDevice::SetParentCookie(int32 cookie)
{
	fParentCookie = cookie;
}


status_t
HIDDevice::Open(ProtocolHandler *handler, uint32 flags)
{
	atomic_add(&fOpenCount, 1);
	return B_OK;
}


status_t
HIDDevice::Close(ProtocolHandler *handler)
{
	atomic_add(&fOpenCount, -1);
	gUSBModule->cancel_queued_transfers(fInterruptPipe);
		// This will wake up any listeners. Whether they should close or retry
		// is handeled internally by the handlers.
	return B_OK;
}


void
HIDDevice::Removed()
{
	fRemoved = true;
	gUSBModule->cancel_queued_transfers(fInterruptPipe);
}


status_t
HIDDevice::MaybeScheduleTransfer(HIDReport*)
{
	if (fRemoved)
		return ENODEV;

	if (atomic_get_and_set(&fTransferScheduled, 1) != 0) {
		// someone else already caused a transfer to be scheduled
		return B_OK;
	}

	TRACE("scheduling interrupt transfer of %lu bytes\n", fTransferBufferSize);
	status_t result = gUSBModule->queue_interrupt(fInterruptPipe,
		fTransferBuffer, fTransferBufferSize, _TransferCallback, this);
	if (result != B_OK) {
		TRACE_ALWAYS("failed to schedule interrupt transfer 0x%08" B_PRIx32
			"\n", result);
		return result;
	}

	return B_OK;
}


status_t
HIDDevice::SendReport(HIDReport *report)
{
	size_t actualLength;
	return gUSBModule->send_request(fDevice,
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_SET_REPORT, 0x200 | report->ID(), fInterfaceIndex,
		report->ReportSize(), report->CurrentReport(), &actualLength);
}


ProtocolHandler *
HIDDevice::ProtocolHandlerAt(uint32 index) const
{
	ProtocolHandler *handler = fProtocolHandlerList;
	while (handler != NULL) {
		if (index == 0)
			return handler;

		handler = handler->NextHandler();
		index--;
	}

	return NULL;
}


void
HIDDevice::_UnstallCallback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	HIDDevice *device = (HIDDevice *)cookie;
	if (status != B_OK) {
		TRACE_ALWAYS("Unable to unstall device: %s\n", strerror(status));
	}

	// Now report the original failure, since we're ready to retry
	_TransferCallback(cookie, B_ERROR, device->fTransferBuffer, 0);
}


void
HIDDevice::_TransferCallback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	HIDDevice *device = (HIDDevice *)cookie;
	if (status == B_DEV_STALLED && !device->fRemoved) {
		// try clearing stalls right away, the report listeners will resubmit
		gUSBModule->queue_request(device->fDevice,
			USB_REQTYPE_STANDARD | USB_REQTYPE_ENDPOINT_OUT,
			USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT,
			device->fEndpointAddress, 0, NULL, _UnstallCallback, device);
		return;
	}

	atomic_set(&device->fTransferScheduled, 0);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The last input report per ID, for R5_JOYSTICK_GET_REPORT (motion).
	if (status == B_OK && actualLength > 0
		&& actualLength <= device->fTransferBufferSize) {
		InterruptsSpinLocker locker(device->fR5CacheLock);
		r5_hid_cache_store(device->fR5Cache, device->fTransferBuffer,
			actualLength, device->fParser.UsesReportIDs(), system_time());
	}
	// A Switch Pro Controller or GIP packet becomes the R5 report.
	if (status == B_OK && actualLength > 0
		&& actualLength <= device->fTransferBufferSize
		&& device->fR5Kind != R5_USB_KIND_PLAIN)
		actualLength = device->_R5Translate(actualLength);
	// SPDX-SnippetEnd

	device->fParser.SetReport(status, device->fTransferBuffer, actualLength);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
HIDDevice::GetJoystickInfo(r5_joystick_info& info) const
{
	info.bus = R5_JOYSTICK_BUS_USB;
	info.vendor_id = fVendorID;
	info.product_id = fProductID;
	info.version = fVersion;
	strlcpy(info.name, fProductName, sizeof(info.name));
	// A GIP controller: its report is in libr5game's fixed Xbox order,
	// and it rumbles with output 0x09 (R5HIDReports.h).
	if (fR5Kind == R5_USB_KIND_GIP)
		info.flags = R5_JOYSTICK_FLAG_STANDARD_LAYOUT | R5_JOYSTICK_FLAG_GIP;
}


/*!	The product string (USB 2.0, 9.6.7), in the first language the device
	lists, as UTF-8. A device without one keeps an empty name.
*/
void
HIDDevice::_ReadProductName(uint8 index)
{
	if (index == 0)
		return;

	uint8 buffer[256];
	size_t actual = 0;
	uint16 language = 0x0409;
	if (gUSBModule->get_descriptor(fDevice, USB_DESCRIPTOR_STRING, 0, 0,
			buffer, sizeof(buffer), &actual) == B_OK && actual >= 4)
		language = buffer[2] | (buffer[3] << 8);

	actual = 0;
	if (gUSBModule->get_descriptor(fDevice, USB_DESCRIPTOR_STRING, index,
			language, buffer, sizeof(buffer), &actual) != B_OK || actual < 2)
		return;
	size_t length = buffer[0];
	if (length > actual)
		length = actual;

	size_t out = 0;
	for (size_t i = 2; i + 1 < length; i += 2) {
		uint16 c = buffer[i] | (buffer[i + 1] << 8);
		// UTF-16 to UTF-8; a surrogate (outside the Basic Multilingual
		// Plane) becomes "?".
		if (c >= 0xd800 && c <= 0xdfff)
			c = '?';
		if (c < 0x80) {
			if (out + 1 >= sizeof(fProductName))
				break;
			fProductName[out++] = (char)c;
		} else if (c < 0x800) {
			if (out + 2 >= sizeof(fProductName))
				break;
			fProductName[out++] = (char)(0xc0 | (c >> 6));
			fProductName[out++] = (char)(0x80 | (c & 0x3f));
		} else {
			if (out + 3 >= sizeof(fProductName))
				break;
			fProductName[out++] = (char)(0xe0 | (c >> 12));
			fProductName[out++] = (char)(0x80 | ((c >> 6) & 0x3f));
			fProductName[out++] = (char)(0x80 | (c & 0x3f));
		}
	}
	// No trailing spaces, which some devices pad their name with.
	while (out > 0 && fProductName[out - 1] == ' ')
		out--;
	fProductName[out] = '\0';
}


// One interrupt OUT transfer: freed by whoever comes last, the waiter or
// the callback, so a transfer that outlives its waiter has its memory.
struct r5_out_transfer {
	sem_id		done;
	int32		references;
	status_t	status;
	size_t		actual;
	uint8		data[0];
};


static void
r5_release_transfer(r5_out_transfer* transfer)
{
	if (atomic_add(&transfer->references, -1) == 1) {
		delete_sem(transfer->done);
		free(transfer);
	}
}


void
HIDDevice::_R5OutCallback(void* cookie, status_t status, void* data,
	size_t actualLength)
{
	r5_out_transfer* transfer = (r5_out_transfer*)cookie;
	transfer->status = status;
	transfer->actual = actualLength;
	release_sem_etc(transfer->done, 1, B_DO_NOT_RESCHEDULE);
	r5_release_transfer(transfer);
}


status_t
HIDDevice::_R5InterruptOut(const uint8* data, size_t length)
{
	// fR5OutLock is held.
	r5_out_transfer* transfer = (r5_out_transfer*)malloc(
		sizeof(r5_out_transfer) + length);
	if (transfer == NULL)
		return B_NO_MEMORY;
	transfer->done = create_sem(0, "usb_hid report out");
	if (transfer->done < 0) {
		status_t status = transfer->done;
		free(transfer);
		return status;
	}
	transfer->references = 2;
	transfer->status = B_ERROR;
	transfer->actual = 0;
	memcpy(transfer->data, data, length);

	status_t status = gUSBModule->queue_interrupt(fR5OutPipe, transfer->data,
		length, _R5OutCallback, transfer);
	if (status != B_OK) {
		transfer->references = 1;
		r5_release_transfer(transfer);
		return status;
	}

	status = acquire_sem_etc(transfer->done, 1, B_RELATIVE_TIMEOUT, 1000000);
	if (status == B_OK)
		status = transfer->status;
	else {
		// The controller did not take it: cancel it. The callback still
		// runs (with B_CANCELED) and frees the transfer if it comes last.
		gUSBModule->cancel_queued_transfers(fR5OutPipe);
	}
	r5_release_transfer(transfer);
	return status;
}


status_t
HIDDevice::R5SendReport(uint8 type, const uint8* data, size_t length)
{
	if (fRemoved)
		return ENODEV;
	if (data == NULL || length == 0)
		return B_BAD_VALUE;

	MutexLocker locker(fR5OutLock);
	if (type == HID_REPORT_TYPE_OUTPUT && fR5OutPipe != 0)
		return _R5InterruptOut(data, length);

	// SET_REPORT on the control pipe (HID 1.11, 7.2.2): the report type in
	// the high byte, the ID in the low byte; with IDs the data starts with
	// the ID.
	uint16 reportType = type == HID_REPORT_TYPE_FEATURE
		? B_USB_REQUEST_HID_FEATURE_REPORT : B_USB_REQUEST_HID_OUTPUT_REPORT;
	uint8 id = fParser.UsesReportIDs() ? data[0] : 0;
	size_t actualLength = 0;
	return gUSBModule->send_request(fDevice,
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_SET_REPORT, (reportType << 8) | id, fInterfaceIndex,
		length, (void*)data, &actualLength);
}


status_t
HIDDevice::R5GetReport(uint8 type, uint8 id, uint8* data, size_t capacity,
	size_t* length, bigtime_t* when)
{
	if (fRemoved)
		return ENODEV;
	*length = 0;
	*when = 0;

	if (type == HID_REPORT_TYPE_INPUT) {
		InterruptsSpinLocker locker(fR5CacheLock);
		return r5_hid_cache_get(fR5Cache, id, data, capacity, length, when);
	}
	if (type != HID_REPORT_TYPE_FEATURE)
		return B_BAD_VALUE;

	// GET_REPORT (HID 1.11, 7.2.1) for a feature report.
	MutexLocker locker(fR5OutLock);
	size_t actualLength = 0;
	status_t status = gUSBModule->send_request(fDevice,
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_GET_REPORT,
		(B_USB_REQUEST_HID_FEATURE_REPORT << 8) | id, fInterfaceIndex,
		capacity, data, &actualLength);
	if (status != B_OK)
		return status;
	*length = actualLength < capacity ? actualLength : capacity;
	*when = system_time();
	return B_OK;
}


status_t
HIDDevice::R5GetDescriptor(uint8* data, size_t capacity, size_t* length)
	const
{
	if (fR5Descriptor == NULL)
		return B_NOT_SUPPORTED;
	*length = fR5DescriptorLength;
	memcpy(data, fR5Descriptor,
		fR5DescriptorLength < capacity ? fR5DescriptorLength : capacity);
	return B_OK;
}


// #pragma mark - phase 2: Switch Pro Controller and GIP


/*!	One interrupt IN transfer of at most capacity bytes, waited for up to
	timeout. Only while the device is being added, before any protocol
	handler reads it.
*/
status_t
HIDDevice::_R5InterruptIn(uint8* data, size_t capacity, size_t* length,
	bigtime_t timeout)
{
	*length = 0;
	r5_out_transfer* transfer = (r5_out_transfer*)malloc(
		sizeof(r5_out_transfer) + capacity);
	if (transfer == NULL)
		return B_NO_MEMORY;
	transfer->done = create_sem(0, "usb_hid report in");
	if (transfer->done < 0) {
		status_t status = transfer->done;
		free(transfer);
		return status;
	}
	transfer->references = 2;
	transfer->status = B_ERROR;
	transfer->actual = 0;

	status_t status = gUSBModule->queue_interrupt(fInterruptPipe,
		transfer->data, capacity, _R5OutCallback, transfer);
	if (status != B_OK) {
		transfer->references = 1;
		r5_release_transfer(transfer);
		return status;
	}

	status = acquire_sem_etc(transfer->done, 1, B_RELATIVE_TIMEOUT, timeout);
	if (status == B_OK) {
		status = transfer->status;
		if (status == B_OK) {
			size_t actual = transfer->actual < capacity
				? transfer->actual : capacity;
			memcpy(data, transfer->data, actual);
			*length = actual;
		}
	} else
		gUSBModule->cancel_queued_transfers(fInterruptPipe);
	r5_release_transfer(transfer);
	return status;
}


/*!	Reads reports until one with that ID has value at offset; up to eight
	reports, and gives up at the first 100 ms without one.
*/
bool
HIDDevice::_R5WaitFor(uint8 id, uint8 offset, uint8 value)
{
	uint8 report[R5_HID_SWITCH_USB_OUTPUT];
	for (int i = 0; i < 8; i++) {
		size_t length = 0;
		if (_R5InterruptIn(report, sizeof(report), &length, 100000) != B_OK)
			return false;
		if (length > offset && report[0] == id && report[offset] == value)
			return true;
	}
	return false;
}


/*!	The Switch Pro Controller over USB (SDL_hidapi_switch.c): the
	handshake, high speed and the handshake again (each answered with 0x81
	and the command), "USB only", then the subcommands rumble on, the full
	report mode and the first player light (each answered with 0x21 and the
	subcommand in byte 14). A step without an answer is not repeated: the
	next one is tried anyway.
*/
void
HIDDevice::_R5StartSwitch()
{
	uint8 packet[R5_HID_SWITCH_USB_OUTPUT];
	static const uint8 kCommands[] = { 0x02, 0x03, 0x02, 0x04 };
	for (size_t i = 0; i < sizeof(kCommands); i++) {
		if (r5_hid_switch_usb_command(kCommands[i], packet, sizeof(packet),
				sizeof(packet)) == 0
			|| _R5InterruptOut(packet, sizeof(packet)) != B_OK)
			return;
		if (kCommands[i] != 0x04)
			_R5WaitFor(0x81, 1, kCommands[i]);
	}

	static const uint8 kSubcommands[][2]
		= { { 0x48, 0x01 }, { 0x03, 0x30 }, { 0x30, 0x01 } };
	for (uint8 i = 0; i < 3; i++) {
		if (r5_hid_switch_subcommand(i, kSubcommands[i][0],
				&kSubcommands[i][1], 1, packet, sizeof(packet),
				sizeof(packet)) == 0
			|| _R5InterruptOut(packet, sizeof(packet)) != B_OK)
			return;
		_R5WaitFor(0x21, 14, kSubcommands[i][0]);
	}
}


void
HIDDevice::_R5Start()
{
	if (fR5Kind == R5_USB_KIND_PLAIN || fR5OutPipe == 0)
		return;

	MutexLocker locker(fR5OutLock);
	if (fR5Kind == R5_USB_KIND_SWITCH) {
		_R5StartSwitch();
		return;
	}

	// GIP: power on and the rest; again when it announces itself later.
	r5_hid_gip_init(fR5Gip, fVendorID, fProductID);
	r5_hid_gip_sends sends;
	sends.count = 0;
	r5_hid_gip_start(fR5Gip, sends);
	for (uint32 i = 0; i < sends.count; i++) {
		if (_R5InterruptOut(sends.data[i], sends.length[i]) != B_OK)
			break;
	}
}


static void
r5_queue_out_callback(void* cookie, status_t status, void* data,
	size_t actualLength)
{
	free(cookie);
}


/*!	Sends a packet without waiting, from _TransferCallback(): GIP
	acknowledgements and the start after an announcement. The memory is
	freed when the transfer is done.
*/
void
HIDDevice::_R5QueueOut(const uint8* data, size_t length)
{
	if (fR5OutPipe == 0 || fRemoved || length == 0)
		return;
	uint8* copy = (uint8*)malloc(length);
	if (copy == NULL)
		return;
	memcpy(copy, data, length);
	if (gUSBModule->queue_interrupt(fR5OutPipe, copy, length,
			r5_queue_out_callback, copy) != B_OK)
		free(copy);
}


/*!	In _TransferCallback(): turns the packet in fTransferBuffer into the
	report of the R5 descriptor and returns its length. A packet without
	anything for the joystick gets the report ID 0xFF, which the descriptor
	does not have, so the waiting handlers read again.
*/
size_t
HIDDevice::_R5Translate(size_t length)
{
	uint8* buffer = fTransferBuffer;
	if (fR5Kind == R5_USB_KIND_SWITCH) {
		uint8 simple[R5_HID_SWITCH_SIMPLE_SIZE];
		if (r5_hid_switch_simple_report(buffer, length, simple,
				sizeof(simple)) == 0)
			return length;
		memcpy(buffer, simple, sizeof(simple));
		return sizeof(simple);
	}

	uint8 report[R5_HID_GIP_INPUT_SIZE];
	r5_hid_gip_sends sends;
	size_t built = r5_hid_gip_input(fR5Gip, buffer, length, report,
		sizeof(report), sends);
	for (uint32 i = 0; i < sends.count; i++)
		_R5QueueOut(sends.data[i], sends.length[i]);
	if (built == 0) {
		buffer[0] = 0xff;
		return length;
	}
	if (built > fTransferBufferSize)
		return length;
	memcpy(buffer, report, built);
	return built;
}
// SPDX-SnippetEnd
