/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * A GATT client for BLE HID over GATT (docs/BLUETOOTH.md, step 6;
 * Core 6.0, Vol 3, Part F and G; HID over GATT Profile 1.0): after the
 * link is encrypted it finds the HID service, reads the report map and
 * the report references, turns on notifications for the input reports and
 * creates the device in the bluetooth_hid driver of step 4, so a BLE mouse
 * or keyboard reaches input_server like a USB one. Reports are not logged.
 *
 * It also answers what a device asks of us: our ATT MTU, and "not found"
 * for anything else, since this computer offers no GATT services.
 */


#include "LEManagerPrivate.h"

#include <string.h>

#include <R5BluetoothHID.h>

#include "BluetoothServer.h"
#include "HIDHost.h"
#include "le_parse.h"


// Our ATT MTU: an ATT PDU of 247 bytes fills one LE packet of 251 bytes.
static const uint16 kATTMTU = 247;
static const size_t kMaxReportMap = R5_BTHID_MAX_DESCRIPTOR;

// GATT discovery steps.
enum {
	kGATTIdle = 0,
	kGATTMTU,
	kGATTServices,
	kGATTCharacteristics,
	kGATTDescriptors,
	kGATTReportMap,
	kGATTReportReferences,
	kGATTNotifications,
	kGATTReady,
	kGATTBattery,
	kGATTDeviceInfo
};

// UUIDs (Assigned Numbers, 3.4 to 3.8).
enum {
	kUUIDPrimaryService		= 0x2800,
	kUUIDCharacteristic		= 0x2803,
	kUUIDCCCD				= 0x2902,
	kUUIDReportReference	= 0x2908,
	kUUIDHIDService			= 0x1812,
	kUUIDBatteryService		= 0x180f,
	kUUIDDeviceInformation	= 0x180a,
	kUUIDBatteryLevel		= 0x2a19,
	kUUIDPnPID				= 0x2a50,
	kUUIDReportMap			= 0x2a4b,
	kUUIDReport				= 0x2a4d,
	kUUIDProtocolMode		= 0x2a4e
};

// Characteristic properties (3.3.1.1).
enum {
	kPropertyNotify		= 0x10,
	kPropertyIndicate	= 0x20
};


static uint16
le16(const uint8* data)
{
	return data[0] | (data[1] << 8);
}


void
LEManager::_ATTRequest(Link* link, const uint8* pdu, size_t size)
{
	link->requestPending = true;
	link->attLast = system_time();
	_ATTSend(link, pdu, size);
}


void
LEManager::_ATTReceived(Link* link, const uint8* pdu, size_t size)
{
	if (size == 0 || size > link->mtu + (size_t)64)
		return;

	const uint8 opcode = pdu[0];
	switch (opcode) {
		case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0b:
		case 0x0d: case 0x0f: case 0x11: case 0x13: case 0x17: case 0x19:
		case 0x21:
			// A response: only one request of ours is ever pending.
			if (!link->requestPending)
				return;
			link->requestPending = false;
			_GATTStep(link, pdu, size);
			return;

		case 0x1b:
			// Handle Value Notification.
			if (size >= 4 && link->batteryHandle != 0
				&& le16(pdu + 1) == link->batteryHandle) {
				link->battery = pdu[3] <= 100 ? pdu[3] : -1;
				return;
			}
			if (size >= 3)
				_HIDInput(link, le16(pdu + 1), pdu + 3, size - 3);
			return;

		case 0x1d:
		{
			// Handle Value Indication: confirm, then use it.
			const uint8 confirm = 0x1e;
			_ATTSend(link, &confirm, 1);
			if (size >= 3)
				_HIDInput(link, le16(pdu + 1), pdu + 3, size - 3);
			return;
		}
	}

	// Commands (bit 6) get no answer; requests do.
	if ((opcode & 0x40) == 0)
		_ATTServerRequest(link, pdu, size);
}


void
LEManager::_ATTServerRequest(Link* link, const uint8* pdu, size_t size)
{
	const uint8 opcode = pdu[0];
	if (opcode == 0x02) {
		// Exchange MTU Request.
		if (size < 3)
			return;
		const uint16 client = le16(pdu + 1);
		if (client >= 23)
			link->mtu = client < kATTMTU ? client : kATTMTU;
		uint8 response[3] = { 0x03, 0, 0 };
		le_put16(response + 1, kATTMTU);
		_ATTSend(link, response, sizeof(response));
		return;
	}

	// We have no attributes (3.4.1.1): discovery finds nothing, any
	// handle is invalid, the rest is not supported.
	uint8 error = 0x06;		// Request Not Supported
	uint16 handle = 0;
	switch (opcode) {
		case 0x04: case 0x06: case 0x08: case 0x10:
			error = 0x0a;	// Attribute Not Found
			if (size >= 3)
				handle = le16(pdu + 1);
			break;
		case 0x0a: case 0x0c: case 0x12: case 0x16:
			error = 0x01;	// Invalid Handle
			if (size >= 3)
				handle = le16(pdu + 1);
			break;
	}
	uint8 response[5] = { 0x01, opcode, 0, 0, error };
	le_put16(response + 2, handle);
	_ATTSend(link, response, sizeof(response));
}


void
LEManager::_GATTStart(Link* link)
{
	if (link->gattStep != kGATTIdle)
		return;
	link->characteristics.clear();
	link->reports.clear();
	link->reportMap.clear();
	link->serviceStart = 0;
	link->serviceEnd = 0;
	link->batteryStart = 0;
	link->batteryEnd = 0;
	link->batteryHandle = 0;
	link->infoStart = 0;
	link->infoEnd = 0;
	link->pnpKnown = false;

	link->gattStep = kGATTMTU;
	uint8 request[3] = { 0x02, 0, 0 };
	le_put16(request + 1, kATTMTU);
	_ATTRequest(link, request, sizeof(request));
}


void
LEManager::_GATTStep(Link* link, const uint8* pdu, size_t size)
{
	const bool error = pdu[0] == 0x01;
	le_att_entry entry;
	size_t offset = 0;

	switch (link->gattStep) {
		case kGATTMTU:
			if (!error && pdu[0] == 0x03 && size >= 3) {
				const uint16 server = le16(pdu + 1);
				if (server >= 23)
					link->mtu = server < kATTMTU ? server : kATTMTU;
			}
			link->gattStep = kGATTServices;
			link->next = 0x0001;
			break;

		case kGATTServices:
		{
			if (error || pdu[0] != 0x11) {
				link->next = 0;
				break;
			}
			uint16 last = 0;
			while (le_att_next(pdu, size, &offset, &entry)) {
				if (entry.end < entry.handle)
					continue;
				const uint16 uuid = le_att_uuid16(entry.value,
					entry.value_length);
				if (link->serviceStart == 0 && uuid == kUUIDHIDService) {
					link->serviceStart = entry.handle;
					link->serviceEnd = entry.end;
				}
				if (link->batteryStart == 0 && uuid == kUUIDBatteryService) {
					link->batteryStart = entry.handle;
					link->batteryEnd = entry.end;
				}
				if (link->infoStart == 0 && uuid == kUUIDDeviceInformation) {
					link->infoStart = entry.handle;
					link->infoEnd = entry.end;
				}
				if (entry.end > last)
					last = entry.end;
			}
			// Done at the end of the handles, or when nothing moved on.
			link->next = (last == 0xffff || last < link->next)
				? 0 : (uint16)(last + 1);
			break;
		}

		case kGATTCharacteristics:
		{
			if (error || pdu[0] != 0x09) {
				link->next = 0;
				break;
			}
			uint16 last = 0;
			while (le_att_next(pdu, size, &offset, &entry)) {
				// Properties, value handle, UUID (3.3.1).
				if (entry.value_length < 5)
					continue;
				Link::Characteristic characteristic;
				characteristic.declaration = entry.handle;
				characteristic.properties = entry.value[0];
				characteristic.value = le16(entry.value + 1);
				characteristic.uuid = le_att_uuid16(entry.value + 3,
					entry.value_length - 3);
				characteristic.cccd = 0;
				characteristic.reference = 0;
				if (characteristic.value > entry.handle
					&& link->characteristics.size() < 64)
					link->characteristics.push_back(characteristic);
				if (entry.handle > last)
					last = entry.handle;
			}
			link->next = (last == 0 || last >= link->serviceEnd
				|| last < link->next) ? 0 : (uint16)(last + 1);
			break;
		}

		case kGATTDescriptors:
		{
			if (error || pdu[0] != 0x05) {
				link->next = 0;
				break;
			}
			uint16 last = 0;
			while (le_att_next(pdu, size, &offset, &entry)) {
				const uint16 uuid = le_att_uuid16(entry.value,
					entry.value_length);
				// A descriptor belongs to the characteristic before it.
				Link::Characteristic* owner = NULL;
				for (size_t i = 0; i < link->characteristics.size(); i++) {
					Link::Characteristic& c = link->characteristics[i];
					if (c.value < entry.handle
						&& (owner == NULL || c.value > owner->value))
						owner = &c;
				}
				if (owner != NULL && uuid == kUUIDCCCD)
					owner->cccd = entry.handle;
				else if (owner != NULL && uuid == kUUIDReportReference)
					owner->reference = entry.handle;
				if (entry.handle > last)
					last = entry.handle;
			}
			link->next = (last == 0 || last >= link->serviceEnd
				|| last < link->next) ? 0 : (uint16)(last + 1);
			break;
		}

		case kGATTReportMap:
			if (error || (pdu[0] != 0x0b && pdu[0] != 0x0d)) {
				// Attribute Not Long ends a map that filled one read.
				link->next = 0;
				break;
			}
			if (link->reportMap.size() + size - 1 > kMaxReportMap) {
				link->reportMap.clear();
				link->next = 0;
				break;
			}
			link->reportMap.insert(link->reportMap.end(), pdu + 1, pdu + size);
			// A full read may have more (Read Blob).
			link->next = size - 1 == (size_t)link->mtu - 1 ? 1 : 0;
			break;

		case kGATTReportReferences:
			if (!error && pdu[0] == 0x0b && size >= 3
				&& link->index < link->reports.size()) {
				link->reports[link->index].id = pdu[1];
				link->reports[link->index].type = pdu[2];
			}
			link->index++;
			break;

		case kGATTBattery:
			// Read By Type Response: length, then handle and value.
			if (!error && pdu[0] == 0x09 && size >= 5 && pdu[1] >= 3) {
				link->batteryHandle = le16(pdu + 2);
				link->battery = pdu[4] <= 100 ? pdu[4] : -1;
			}
			link->next = 0;
			break;

		case kGATTDeviceInfo:
			// Read By Type Response: length, handle, then the PnP ID:
			// vendor ID source, vendor, product, version (DIS 1.1, 3.9).
			if (!error && pdu[0] == 0x09 && size >= 11 && pdu[1] >= 9) {
				link->pnpVendor = le16(pdu + 5);
				link->pnpProduct = le16(pdu + 7);
				link->pnpVersion = le16(pdu + 9);
				link->pnpKnown = link->pnpVendor != 0;
			}
			link->next = 0;
			break;

		case kGATTNotifications:
			if (error) {
				syslog(LOG_NOTICE, "bluetooth_server: gatt notifications"
					" refused (%#x)", size >= 5 ? pdu[4] : 0);
			}
			link->index++;
			break;

		default:
			return;
	}

	_GATTNext(link);
}


void
LEManager::_GATTNext(Link* link)
{
	uint8 request[8];

	switch (link->gattStep) {
		case kGATTServices:
			if (link->next != 0) {
				// Read By Group Type: start, end, primary service.
				request[0] = 0x10;
				le_put16(request + 1, link->next);
				le_put16(request + 3, 0xffff);
				le_put16(request + 5, kUUIDPrimaryService);
				_ATTRequest(link, request, 7);
				return;
			}
			// The battery level, once, when the device has the service.
			if (link->batteryStart != 0) {
				link->gattStep = kGATTBattery;
				request[0] = 0x08;	// Read By Type
				le_put16(request + 1, link->batteryStart);
				le_put16(request + 3, link->batteryEnd);
				le_put16(request + 5, kUUIDBatteryLevel);
				_ATTRequest(link, request, 7);
				return;
			}
			// fall through

		case kGATTBattery:
			// The PnP ID, once, when the device has the Device Information
			// service: game controllers are told apart by it.
			if (link->infoStart != 0) {
				link->gattStep = kGATTDeviceInfo;
				request[0] = 0x08;	// Read By Type
				le_put16(request + 1, link->infoStart);
				le_put16(request + 3, link->infoEnd);
				le_put16(request + 5, kUUIDPnPID);
				_ATTRequest(link, request, 7);
				return;
			}
			// fall through

		case kGATTDeviceInfo:
			if (link->serviceStart == 0) {
				syslog(LOG_NOTICE, "bluetooth_server: gatt no hid service");
				link->gattStep = kGATTReady;
				return;
			}
			syslog(LOG_INFO, "bluetooth_server: gatt hid service found");
			link->gattStep = kGATTCharacteristics;
			link->next = link->serviceStart;
			// fall through

		case kGATTCharacteristics:
			if (link->next != 0) {
				// Read By Type: characteristic declarations.
				request[0] = 0x08;
				le_put16(request + 1, link->next);
				le_put16(request + 3, link->serviceEnd);
				le_put16(request + 5, kUUIDCharacteristic);
				_ATTRequest(link, request, 7);
				return;
			}
			link->gattStep = kGATTDescriptors;
			link->next = link->serviceStart;
			// fall through

		case kGATTDescriptors:
			if (link->next != 0) {
				// Find Information over the service.
				request[0] = 0x04;
				le_put16(request + 1, link->next);
				le_put16(request + 3, link->serviceEnd);
				_ATTRequest(link, request, 5);
				return;
			}
			link->gattStep = kGATTReportMap;
			link->next = 1;
			link->reportMap.clear();
			// fall through

		case kGATTReportMap:
		{
			uint16 handle = 0;
			for (size_t i = 0; i < link->characteristics.size(); i++) {
				if (link->characteristics[i].uuid == kUUIDReportMap)
					handle = link->characteristics[i].value;
			}
			if (handle != 0 && link->next != 0) {
				if (link->reportMap.empty()) {
					request[0] = 0x0a;	// Read
					le_put16(request + 1, handle);
					_ATTRequest(link, request, 3);
				} else {
					request[0] = 0x0c;	// Read Blob
					le_put16(request + 1, handle);
					le_put16(request + 3, (uint16)link->reportMap.size());
					_ATTRequest(link, request, 5);
				}
				return;
			}
			if (link->reportMap.empty()) {
				syslog(LOG_NOTICE, "bluetooth_server: gatt no hid report map");
				link->gattStep = kGATTReady;
				return;
			}

			// The reports: input unless a reference says otherwise.
			link->reports.clear();
			for (size_t i = 0; i < link->characteristics.size(); i++) {
				const Link::Characteristic& c = link->characteristics[i];
				if (c.uuid != kUUIDReport)
					continue;
				Report report;
				report.valueHandle = c.value;
				report.cccd = c.cccd;
				report.reference = c.reference;
				report.id = 0;
				report.type = (c.properties & (kPropertyNotify
					| kPropertyIndicate)) != 0 ? 1 : 2;
				link->reports.push_back(report);
			}
			link->gattStep = kGATTReportReferences;
			link->index = 0;
			// fall through
		}

		case kGATTReportReferences:
			while (link->index < link->reports.size()
				&& link->reports[link->index].reference == 0)
				link->index++;
			if (link->index < link->reports.size()) {
				request[0] = 0x0a;
				le_put16(request + 1, link->reports[link->index].reference);
				_ATTRequest(link, request, 3);
				return;
			}
			// Report mode, not boot mode (HOGP 4.4): Write Without
			// Response, which gets no answer.
			for (size_t i = 0; i < link->characteristics.size(); i++) {
				if (link->characteristics[i].uuid != kUUIDProtocolMode)
					continue;
				request[0] = 0x52;
				le_put16(request + 1, link->characteristics[i].value);
				request[3] = 0x01;
				_ATTSend(link, request, 4);
			}
			link->gattStep = kGATTNotifications;
			link->index = 0;
			// fall through

		case kGATTNotifications:
			while (link->index < link->reports.size()
				&& (link->reports[link->index].type != 1
					|| link->reports[link->index].cccd == 0))
				link->index++;
			if (link->index < link->reports.size()) {
				// Write Request: notifications on.
				request[0] = 0x12;
				le_put16(request + 1, link->reports[link->index].cccd);
				le_put16(request + 3, 0x0001);
				_ATTRequest(link, request, 5);
				return;
			}
			link->gattStep = kGATTReady;
			_GATTReady(link);
			return;
	}
}


void
LEManager::_GATTReady(Link* link)
{
	link->usesReportIDs = false;
	for (size_t i = 0; i < link->reports.size(); i++) {
		if (link->reports[i].id != 0)
			link->usesReportIDs = true;
	}

	HIDHost* host = ((BluetoothServer*)be_app)->GetHIDHost();
	uint32 handlers = 0;
	int32 id = host != NULL ? host->CreateLEDevice(&link->reportMap[0],
		link->reportMap.size(), &handlers) : -1;
	if (id < 0) {
		syslog(LOG_WARNING, "bluetooth_hid: ble device not supported");
		return;
	}
	link->kernelID = id;
	if (link->pnpKnown) {
		host->SetLEDeviceInfo(id, link->pnpVendor, link->pnpProduct,
			link->pnpVersion, link->bond.name.String());
	}
	syslog(LOG_INFO, "bluetooth_hid: ble hid ready (%" B_PRIu32
		" input device%s)", handlers, handlers == 1 ? "" : "s");

	LEBond* bond = _FindBond(link->address);
	if (bond == NULL)
		bond = _FindBond(link->bond.address);
	if (bond != NULL && !bond->hid) {
		bond->hid = true;
		_SaveBonds();
	}
}


void
LEManager::_HIDInput(Link* link, uint16 handle, const uint8* value,
	size_t length)
{
	if (link->kernelID < 0 || !link->encrypted)
		return;
	for (size_t i = 0; i < link->reports.size(); i++) {
		const Report& report = link->reports[i];
		if (report.valueHandle != handle || report.type != 1)
			continue;
		// The driver wants the report ID first when the map uses them.
		uint8 buffer[R5_BTHID_MAX_REPORT];
		size_t size = 0;
		if (link->usesReportIDs)
			buffer[size++] = report.id;
		if (length > sizeof(buffer) - size)
			return;
		memcpy(buffer + size, value, length);
		size += length;
		HIDHost* host = ((BluetoothServer*)be_app)->GetHIDHost();
		if (host != NULL)
			host->LEInput(link->kernelID, buffer, size);
		return;
	}
}


void
LEManager::_HIDOutput(int32 kernelID, const uint8* data, size_t length)
{
	for (size_t l = 0; l < fLinks.size(); l++) {
		Link* link = fLinks[l];
		if (link->kernelID != kernelID || !link->encrypted)
			continue;
		uint8 id = 0;
		if (link->usesReportIDs) {
			if (length < 1)
				return;
			id = data[0];
			data++;
			length--;
		}
		for (size_t i = 0; i < link->reports.size(); i++) {
			const Report& report = link->reports[i];
			if (report.type != 2 || report.id != id)
				continue;
			// Write Without Response: keyboard lights need no answer, and
			// it does not wait behind a pending request.
			uint8 command[3 + R5_BTHID_MAX_REPORT];
			if (length > (size_t)link->mtu - 3 || length > R5_BTHID_MAX_REPORT)
				return;
			command[0] = 0x52;
			le_put16(command + 1, report.valueHandle);
			memcpy(command + 3, data, length);
			_ATTSend(link, command, 3 + length);
			return;
		}
		return;
	}
}
