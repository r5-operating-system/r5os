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
#include "HIDReportItem.h"
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
	fProtocolHandlerList(NULL),
	fBatteryParser(NULL),
	fBatteryItem(NULL),
	fBatteryBuffer(NULL),
	fBattery(-1)
{
	mutex_init(&fLock, DRIVER_NAME " device");
	memset(&fInfo, 0, sizeof(fInfo));

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

	// Without memory for it the device works, without a battery.
	_FindBattery(descriptor, descriptorLength);

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

	delete fBatteryParser;
	free(fBatteryBuffer);
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

	_UpdateBattery(data, length);

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


void
HIDDevice::SetInfo(const r5_bthid_info& info)
{
	MutexLocker locker(fLock);
	fInfo = info;
	fInfo.name[sizeof(fInfo.name) - 1] = '\0';
}


void
HIDDevice::GetJoystickInfo(r5_joystick_info& info) const
{
	MutexLocker locker(const_cast<mutex*>(&fLock));
	info.bus = R5_JOYSTICK_BUS_BLUETOOTH;
	info.vendor_id = fInfo.vendor_id;
	info.product_id = fInfo.product_id;
	info.version = fInfo.version;
	strlcpy(info.name, fInfo.name, sizeof(info.name));
	// Also before a program read the joystick (hid_shared takes the
	// handler's own value only when this one is -1).
	info.battery = fBattery;
}


int32
HIDDevice::Battery() const
{
	MutexLocker locker(const_cast<mutex*>(&fLock));
	return fBattery;
}


static HIDReportItem*
find_battery_item(HIDParser& parser)
{
	uint8 count = parser.CountReports(HID_REPORT_TYPE_INPUT);
	for (uint8 i = 0; i < count; i++) {
		HIDReport* report = parser.ReportAt(HID_REPORT_TYPE_INPUT, i);
		if (report == NULL)
			continue;
		for (uint32 j = 0; j < report->CountItems(); j++) {
			HIDReportItem* item = report->ItemAt(j);
			if (item != NULL && item->HasData() && item->UsagePage() == 0x06
				&& item->UsageID() == 0x20)
				return item;
		}
	}
	return NULL;
}


void
HIDDevice::_FindBattery(const uint8* descriptor, size_t descriptorLength)
{
	// Most controllers have no Battery Strength: then nothing is kept.
	if (find_battery_item(fParser) == NULL)
		return;

	fBatteryParser = new(std::nothrow) HIDParser(this);
	fBatteryBuffer = (uint8*)malloc(R5_BTHID_MAX_REPORT + 3);
	if (fBatteryParser == NULL || fBatteryBuffer == NULL
		|| fBatteryParser->ParseReportDescriptor(descriptor,
			descriptorLength) != B_OK
		|| (fBatteryItem = find_battery_item(*fBatteryParser)) == NULL) {
		delete fBatteryParser;
		fBatteryParser = NULL;
		free(fBatteryBuffer);
		fBatteryBuffer = NULL;
		fBatteryItem = NULL;
	}
}


void
HIDDevice::_UpdateBattery(const uint8* data, size_t length)
{
	// fLock is held; length is 1 to R5_BTHID_MAX_REPORT. The buffer is
	// zero padded by 3, as Extract() reads 4 bytes at a time.
	if (fBatteryItem == NULL)
		return;

	memcpy(fBatteryBuffer, data, length);
	memset(fBatteryBuffer + length, 0, R5_BTHID_MAX_REPORT + 3 - length);
	fBatteryParser->SetReport(B_OK, fBatteryBuffer, length);

	// Another report ID: SetReport() gave the battery report no data.
	HIDReport* report = fBatteryItem->Report();
	if (report->CurrentReport() == NULL)
		return;
	// A report shorter than the descriptor says carries no battery.
	size_t payload = length - (fBatteryParser->UsesReportIDs() ? 1 : 0);
	if (payload < report->ReportSize())
		return;

	if (fBatteryItem->Extract() == B_OK && fBatteryItem->Valid())
		fBattery = (int32)fBatteryItem->ScaledRangeData(0, 100);
}
