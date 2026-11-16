/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The checks bluetooth_server makes on what a Low Energy device sends
 * (docs/BLUETOOTH.md, step 6), on flat bytes and without Haiku
 * dependencies, so scripts/fuzz-bt.sh runs the same code on the host
 * (target le): advertising reports and their data (Core 6.0, Vol 4,
 * Part E, 7.7.65.2 and 7.7.65.13; Vol 3, Part C, 11), SMP PDU lengths
 * (Vol 3, Part H, 3.5 and 3.6) and the ATT responses GATT discovery reads
 * (Vol 3, Part F, 3.4).
 */
#ifndef R5_LE_PARSE_H
#define R5_LE_PARSE_H


#include <string.h>

#include <SupportDefs.h>


// #pragma mark - advertising


#define LE_AD_NAME_MAX		64

struct le_ad_info {
	char	name[LE_AD_NAME_MAX + 1];	// NUL-terminated, may be empty
	bool	complete_name;
	uint16	appearance;					// 0: none
	bool	has_appearance;
	bool	hid_service;				// 16-bit UUID 0x1812 listed
	uint8	flags;					// the Flags AD (discoverable bits)
	bool	has_flags;
};


// Walks the AD structures (length, type, data) of an advertisement or
// scan response and keeps what the scan list shows. A structure that runs
// past the data ends the walk; what came before stays.
static inline void
le_ad_parse(const uint8* data, size_t size, le_ad_info* info)
{
	size_t offset = 0;
	while (offset < size) {
		const uint8 length = data[offset];
		if (length == 0)
			break;
		if ((size_t)length > size - offset - 1)
			break;
		const uint8 type = data[offset + 1];
		const uint8* value = data + offset + 2;
		const size_t valueLength = length - 1;

		switch (type) {
			case 0x08:	// Shortened Local Name
			case 0x09:	// Complete Local Name
			{
				if (type == 0x08 && info->complete_name)
					break;
				size_t copy = valueLength < LE_AD_NAME_MAX
					? valueLength : LE_AD_NAME_MAX;
				// A name is UTF-8; a NUL inside ends it.
				size_t i = 0;
				for (; i < copy && value[i] != 0; i++)
					info->name[i] = (char)value[i];
				info->name[i] = '\0';
				info->complete_name = type == 0x09;
				break;
			}
			case 0x01:	// Flags
				if (valueLength >= 1) {
					info->flags = value[0];
					info->has_flags = true;
				}
				break;
			case 0x19:	// Appearance
				if (valueLength >= 2) {
					info->appearance = value[0] | (value[1] << 8);
					info->has_appearance = true;
				}
				break;
			case 0x02:	// Incomplete List of 16-bit Service UUIDs
			case 0x03:	// Complete List of 16-bit Service UUIDs
				for (size_t i = 0; i + 1 < valueLength; i += 2) {
					if ((value[i] | (value[i + 1] << 8)) == 0x1812)
						info->hid_service = true;
				}
				break;
		}
		offset += 1 + length;
	}
}


// The class of device the scan list understands, from the GAP appearance
// (Assigned Numbers, 2.6) or the HID service: peripheral (major 0x05)
// with minor keyboard, pointing, gamepad or joystick.
static inline uint32
le_ad_class_of_device(const le_ad_info* info)
{
	if (info->has_appearance) {
		switch (info->appearance) {
			case 0x03c1:	return 0x000540;	// keyboard
			case 0x03c2:	return 0x000580;	// mouse
			case 0x03c3:	return 0x000504;	// joystick
			case 0x03c4:	return 0x000508;	// gamepad
			case 0x03c5:	return 0x000514;	// digitizer tablet
		}
		if ((info->appearance >> 6) == 0x0f)
			return 0x000500;					// another HID
	}
	if (info->hid_service)
		return 0x000500;
	return 0;
}


struct le_adv_report {
	uint8			address_type;	// 0 public, 1 random (+2: resolved)
	const uint8*	address;		// 6 bytes, HCI order
	uint16			event_type;		// as the report has it
	uint8			primary_phy;	// 1 for a legacy report
	bool			connectable;
	bool			scan_response;
	int8			rssi;
	const uint8*	data;
	size_t			data_length;
};


// The next report of an LE Advertising Report (subevent 0x02) or LE
// Extended Advertising Report (0x0D). "event" starts after the subevent
// byte and holds the count; *offset starts at 1 and walks the reports one
// after the other, as BlueZ reads them. Returns false at the end or on a
// report that does not fit.
static inline bool
le_adv_report_next(const uint8* event, size_t size, bool extended,
	size_t* offset, le_adv_report* report)
{
	const size_t header = extended ? 24 : 9;
	if (*offset >= size || size - *offset < header)
		return false;
	const uint8* r = event + *offset;
	size_t dataLength;
	if (extended) {
		// Event type (2), address type, address (6), primary and
		// secondary PHY, SID, TX power, RSSI, periodic interval (2),
		// direct address type and address (6), data length, data.
		const uint16 type = r[0] | (r[1] << 8);
		report->event_type = type;
		report->primary_phy = r[9];
		report->connectable = (type & 0x0001) != 0;
		report->scan_response = (type & 0x0008) != 0;
		report->address_type = r[2];
		report->address = r + 3;
		report->rssi = (int8)r[13];
		dataLength = r[23];
		report->data = r + 24;
	} else {
		// Event type, address type, address (6), data length, data, RSSI.
		const uint8 type = r[0];
		report->event_type = type;
		report->primary_phy = 1;
		report->connectable = type == 0x00 || type == 0x01;
		report->scan_response = type == 0x04;
		report->address_type = r[1];
		report->address = r + 2;
		dataLength = r[8];
		report->data = r + 9;
	}
	if (size - *offset - header < dataLength + (extended ? 0 : 1))
		return false;
	report->data_length = dataLength;
	if (!extended)
		report->rssi = (int8)r[9 + dataLength];
	*offset += header + dataLength + (extended ? 0 : 1);
	return true;
}


// #pragma mark - SMP


// The exact length of an SMP PDU with this code, its code byte included;
// 0 for a code we do not know (Vol 3, Part H, 3.5 and 3.6).
static inline size_t
le_smp_pdu_length(uint8 code)
{
	switch (code) {
		case 0x01:	return 7;	// Pairing Request
		case 0x02:	return 7;	// Pairing Response
		case 0x03:	return 17;	// Pairing Confirm
		case 0x04:	return 17;	// Pairing Random
		case 0x05:	return 2;	// Pairing Failed
		case 0x06:	return 17;	// Encryption Information
		case 0x07:	return 11;	// Central Identification
		case 0x08:	return 17;	// Identity Information
		case 0x09:	return 8;	// Identity Address Information
		case 0x0a:	return 17;	// Signing Information
		case 0x0b:	return 2;	// Security Request
		case 0x0c:	return 65;	// Pairing Public Key
		case 0x0d:	return 17;	// Pairing DHKey Check
		case 0x0e:	return 2;	// Keypress Notification
	}
	return 0;
}


// #pragma mark - ATT


// One entry of a Read By Group Type (0x11), Read By Type (0x09) or Find
// Information (0x05) Response. "uuid" is 2 or 16 bytes; for Read By Type
// it is the attribute value instead.
struct le_att_entry {
	uint16			handle;
	uint16			end;		// group end (Read By Group Type only)
	const uint8*	value;
	size_t			value_length;
};


// Walks a response PDU (its opcode at pdu[0]); *offset starts at 0.
// Returns false at the end or when the response is malformed.
static inline bool
le_att_next(const uint8* pdu, size_t size, size_t* offset,
	le_att_entry* entry)
{
	if (size < 2)
		return false;
	size_t each;
	size_t fixed;
	switch (pdu[0]) {
		case 0x11:	// handle, end, value
			each = pdu[1];
			fixed = 4;
			break;
		case 0x09:	// handle, value
			each = pdu[1];
			fixed = 2;
			break;
		case 0x05:	// format 1: handle and 16-bit UUID; 2: 128-bit
			if (pdu[1] == 1)
				each = 4;
			else if (pdu[1] == 2)
				each = 18;
			else
				return false;
			fixed = 2;
			break;
		default:
			return false;
	}
	if (each <= fixed)
		return false;
	if (*offset == 0)
		*offset = 2;
	if (*offset >= size || size - *offset < each)
		return false;

	const uint8* e = pdu + *offset;
	entry->handle = e[0] | (e[1] << 8);
	entry->end = fixed == 4 ? (uint16)(e[2] | (e[3] << 8)) : 0;
	entry->value = e + fixed;
	entry->value_length = each - fixed;
	*offset += each;
	return true;
}


// A 16-bit UUID, or its 128-bit form on the Bluetooth base UUID
// (0000xxxx-0000-1000-8000-00805F9B34FB, little-endian on the air);
// 0 for any other UUID.
static inline uint16
le_att_uuid16(const uint8* uuid, size_t length)
{
	static const uint8 kBase[12] = { 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00,
		0x00, 0x80, 0x00, 0x10, 0x00, 0x00 };
	if (length == 2)
		return uuid[0] | (uuid[1] << 8);
	if (length == 16 && memcmp(uuid, kBase, sizeof(kBase)) == 0
		&& uuid[14] == 0 && uuid[15] == 0)
		return uuid[12] | (uuid[13] << 8);
	return 0;
}


#endif	// R5_LE_PARSE_H
