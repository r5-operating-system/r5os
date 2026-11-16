/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 */


#include "HIDDevice.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include <util/AutoLock.h>

#include "Driver.h"
#include "HIDReport.h"
#include "ProtocolHandler.h"


HIDDevice::HIDDevice(int32 id, void* owner, const uint8* descriptor,
	size_t descriptorLength)
	:
	fStatus(B_NO_INIT),
	fID(id),
	fOwner(owner),
	fTransferScheduled(false),
	fTransferBuffer(NULL),
	fQueue(NULL),
	fQueueHead(0),
	fQueueCount(0),
	fOpenCount(0),
	fCookieCount(0),
	fRemoved(false),
	fParser(this),
	fProtocolHandlerCount(0),
	fProtocolHandlerList(NULL)
{
	mutex_init(&fLock, DRIVER_NAME " device");

	// Padded by 3 so that HIDReportItem can read 32 bits at a time, as in
	// usb_hid.
	fTransferBuffer = (uint8*)malloc(R5_BTHID_MAX_REPORT + 3);
	fQueue = new(std::nothrow) queued_report[kQueueLength];
	if (fTransferBuffer == NULL || fQueue == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}
	memset(fTransferBuffer, 0, R5_BTHID_MAX_REPORT + 3);

	status_t result = fParser.ParseReportDescriptor(descriptor,
		descriptorLength);
	if (result != B_OK) {
		TRACE_ALWAYS("parsing the report descriptor failed: %s\n",
			strerror(result));
		fStatus = result;
		return;
	}

	size_t maxSize = fParser.MaxReportSize(HID_REPORT_TYPE_INPUT);
	if (maxSize == 0 || maxSize > R5_BTHID_MAX_REPORT) {
		TRACE_ALWAYS("report size %" B_PRIuSIZE " not supported\n", maxSize);
		fStatus = B_BAD_DATA;
		return;
	}

	ProtocolHandler::AddHandlers(*this, fProtocolHandlerList,
		fProtocolHandlerCount);
	if (fProtocolHandlerCount == 0) {
		TRACE_ALWAYS("no keyboard, mouse or joystick in the descriptor\n");
		fStatus = B_NOT_SUPPORTED;
		return;
	}

	fStatus = B_OK;
}


HIDDevice::~HIDDevice()
{
	ProtocolHandler* handler = fProtocolHandlerList;
	while (handler != NULL) {
		ProtocolHandler* next = handler->NextHandler();
		delete handler;
		handler = next;
	}

	delete[] fQueue;
	free(fTransferBuffer);
	mutex_destroy(&fLock);
}


status_t
HIDDevice::Open(ProtocolHandler* handler, uint32 flags)
{
	if (fRemoved)
		return ENODEV;
	atomic_add(&fOpenCount, 1);
	return B_OK;
}


status_t
HIDDevice::Close(ProtocolHandler* handler)
{
	atomic_add(&fOpenCount, -1);

	// Wake the readers, like usb_hid cancelling its transfer; the handlers
	// decide whether to retry or to close.
	MutexLocker locker(fLock);
	fTransferScheduled = false;
	fParser.SetReport(B_CANCELED, NULL, 0);
	return B_OK;
}


void
HIDDevice::Removed()
{
	MutexLocker locker(fLock);
	fRemoved = true;
	fTransferScheduled = false;
	fQueueCount = 0;
	fParser.SetReport(B_CANCELED, NULL, 0);
}


status_t
HIDDevice::MaybeScheduleTransfer(HIDReport*)
{
	MutexLocker locker(fLock);
	if (fRemoved)
		return ENODEV;

	if (fQueueCount > 0) {
		// A report came while nobody waited: deliver it now. The waiter
		// is already on the condition variable (HIDReport::WaitForReport).
		queued_report& entry = fQueue[fQueueHead];
		fQueueHead = (fQueueHead + 1) % kQueueLength;
		fQueueCount--;
		_Deliver(entry.data, entry.length);
		return B_OK;
	}

	fTransferScheduled = true;
	return B_OK;
}


void
HIDDevice::Feed(const uint8* data, size_t length)
{
	if (length == 0 || length > R5_BTHID_MAX_REPORT)
		return;

	MutexLocker locker(fLock);
	if (fRemoved)
		return;

	if (fTransferScheduled) {
		fTransferScheduled = false;
		_Deliver(data, length);
		return;
	}

	if (fQueueCount == kQueueLength) {
		fQueueHead = (fQueueHead + 1) % kQueueLength;
		fQueueCount--;
	}
	queued_report& entry
		= fQueue[(fQueueHead + fQueueCount) % kQueueLength];
	memcpy(entry.data, data, length);
	entry.length = length;
	fQueueCount++;
}


void
HIDDevice::_Deliver(const uint8* data, size_t length)
{
	// fLock is held. The transfer buffer stays valid until the next
	// delivery, which only a new waiter asks for (as in usb_hid).
	memcpy(fTransferBuffer, data, length);
	memset(fTransferBuffer + length, 0, R5_BTHID_MAX_REPORT + 3 - length);
	fParser.SetReport(B_OK, fTransferBuffer, length);
}


status_t
HIDDevice::SendReport(HIDReport* report)
{
	// Output reports (keyboard lights) go to bluetooth_server, which sends
	// them on the interrupt channel as DATA | Output.
	if (fRemoved)
		return ENODEV;

	size_t size = report->ReportSize();
	bool withID = fParser.UsesReportIDs();
	if (size + (withID ? 1 : 0) > R5_BTHID_MAX_REPORT)
		return B_BAD_VALUE;

	uint8 buffer[R5_BTHID_MAX_REPORT];
	size_t length = 0;
	if (withID)
		buffer[length++] = report->ID();
	memcpy(buffer + length, report->CurrentReport(), size);
	length += size;
	return bthid_queue_output(fID, buffer, length);
}


ProtocolHandler*
HIDDevice::ProtocolHandlerAt(uint32 index) const
{
	ProtocolHandler* handler = fProtocolHandlerList;
	while (handler != NULL) {
		if (index == 0)
			return handler;

		handler = handler->NextHandler();
		index--;
	}

	return NULL;
}
