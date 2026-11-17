/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Output reports, motion and HID PID for libr5game; the sources of each
 * layout are in GameControllerReports.h.
 */


#include "GameControllerReports.h"

#include <string.h>


namespace R5GameController {


// #pragma mark - CRC-32


uint32
crc32(uint32 crc, const uint8* data, size_t length)
{
	crc = ~crc;
	for (size_t i = 0; i < length; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
	return ~crc;
}


// #pragma mark - PlayStation controllers


static const uint16 kSony = 0x054c;


playstation_model
playstation_model_for(uint16 vendor, uint16 product)
{
	if (vendor != kSony)
		return PLAYSTATION_NONE;
	switch (product) {
		case 0x05c4:	// DualShock 4, first model
		case 0x09cc:	// DualShock 4, second model
		case 0x0ba0:	// DualShock 4 USB wireless adapter
			return PLAYSTATION_DUALSHOCK4;
		case 0x0ce6:
			return PLAYSTATION_DUALSENSE;
		case 0x0df2:
			return PLAYSTATION_DUALSENSE_EDGE;
	}
	return PLAYSTATION_NONE;
}


void
playstation_effects_init(playstation_effects& effects)
{
	memset(&effects, 0, sizeof(effects));
	dualsense_trigger_effect(TRIGGER_EFFECT_OFF, 0, 0, effects.leftTrigger);
	dualsense_trigger_effect(TRIGGER_EFFECT_OFF, 0, 0, effects.rightTrigger);
	effects.improvedRumble = true;
}


static uint8
unit_to_byte(float value)
{
	if (!(value > 0))
		return 0;
	if (value >= 1)
		return 255;
	return (uint8)(value * 255 + 0.5f);
}


void
dualsense_trigger_effect(uint32 effect, float start, float strength,
	uint8 out[kTriggerEffectSize])
{
	memset(out, 0, kTriggerEffectSize);
	uint8 from = unit_to_byte(start);
	uint8 force = unit_to_byte(strength);
	switch (effect) {
		case TRIGGER_EFFECT_RESISTANCE:
			// Resistance from the start position to the end of the pull.
			out[0] = 0x01;
			out[1] = from;
			out[2] = force;
			break;
		case TRIGGER_EFFECT_CLICK:
		{
			// Resistance that gives way, a click, between start and end.
			uint8 begin = from < 0x10 ? 0x10 : (from > 0xa0 ? 0xa0 : from);
			uint32 end = begin + 0x40;
			out[0] = 0x02;
			out[1] = begin;
			out[2] = end > 0xff ? 0xff : (uint8)end;
			out[3] = force;
			break;
		}
		case TRIGGER_EFFECT_VIBRATION:
			// Vibration from the start position: frequency, amplitude,
			// start.
			out[0] = 0x06;
			out[1] = 30;
			out[2] = force;
			out[3] = from;
			break;
		default:
			// Off: the trigger is free again.
			out[0] = 0x05;
			break;
	}
}


static void
put_crc(uint8* report, size_t size)
{
	// The CRC covers the Bluetooth HID header byte (DATA | Output, 0xA2)
	// and the report without its last four bytes; little endian.
	uint8 header = 0xa2;
	uint32 crc = crc32(0, &header, 1);
	crc = crc32(crc, report, size - 4);
	report[size - 4] = (uint8)crc;
	report[size - 3] = (uint8)(crc >> 8);
	report[size - 2] = (uint8)(crc >> 16);
	report[size - 1] = (uint8)(crc >> 24);
}


size_t
build_dualsense_output(const playstation_effects& effects, bool bluetooth,
	uint8* out, size_t capacity)
{
	size_t size = bluetooth ? kPlayStationBluetoothSize : kDualSenseUSBSize;
	if (out == NULL || capacity < size)
		return 0;
	memset(out, 0, size);

	uint8* state;
	if (bluetooth) {
		out[0] = 0x31;
		out[1] = 0x00;	// tag and sequence
		out[2] = 0x10;
		state = out + 3;
	} else {
		out[0] = 0x02;
		state = out + 1;
	}

	// The 47 bytes of the effects state.
	if (effects.improvedRumble) {
		state[0] = 0x02;		// the motors, not the audio haptics
		state[38] = 0x04;		// improved rumble emulation
	} else
		state[0] = 0x01 | 0x02;	// rumble emulation, no audio haptics
	state[0] |= 0x04 | 0x08;	// the right and the left trigger effect
	state[2] = effects.weak;
	state[3] = effects.strong;
	memcpy(state + 10, effects.rightTrigger, kTriggerEffectSize);
	memcpy(state + 21, effects.leftTrigger, kTriggerEffectSize);

	if (bluetooth)
		put_crc(out, size);
	return size;
}


size_t
build_dualshock4_output(const playstation_effects& effects, bool bluetooth,
	uint8* out, size_t capacity)
{
	size_t size = bluetooth ? kPlayStationBluetoothSize : kDualShock4USBSize;
	if (out == NULL || capacity < size)
		return 0;
	memset(out, 0, size);

	// The valid flags: 0x01 rumble only, so the light bar keeps its colour.
	uint8* state;
	if (bluetooth) {
		out[0] = 0x11;
		out[1] = 0xc0 | 0x04;	// HID and CRC, a report every 4 ms
		out[3] = 0x01;
		state = out + 6;
	} else {
		out[0] = 0x05;
		out[1] = 0x01;
		state = out + 4;
	}
	state[0] = effects.weak;
	state[1] = effects.strong;

	if (bluetooth)
		put_crc(out, size);
	return size;
}


uint8
playstation_motion_report(playstation_model model, bool bluetooth)
{
	if (!bluetooth)
		return 0x01;
	return model == PLAYSTATION_DUALSHOCK4 ? 0x11 : 0x31;
}


static int16
read_int16(const uint8* data)
{
	return (int16)(uint16)(data[0] | (data[1] << 8));
}


bool
parse_playstation_motion(playstation_model model, bool bluetooth,
	const uint8* report, size_t length, motion_sample& sample)
{
	if (report == NULL || length == 0)
		return false;

	// Where the state starts after the report ID, and the gyroscope and
	// accelerometer in it (three little-endian int16 each).
	size_t state;
	size_t gyro;
	size_t accel;
	switch (model) {
		case PLAYSTATION_DUALSHOCK4:
			if (report[0] != (bluetooth ? 0x11 : 0x01))
				return false;
			state = bluetooth ? 3 : 1;
			gyro = 12;
			accel = 18;
			break;
		case PLAYSTATION_DUALSENSE:
		case PLAYSTATION_DUALSENSE_EDGE:
			if (report[0] != (bluetooth ? 0x31 : 0x01))
				return false;
			state = bluetooth ? 2 : 1;
			gyro = 15;
			accel = 21;
			break;
		default:
			return false;
	}
	if (length < state + accel + 6)
		return false;

	const uint8* data = report + state;
	for (int i = 0; i < 3; i++) {
		sample.rotation[i] = read_int16(data + gyro + 2 * i) / 16.0f;
		sample.acceleration[i] = read_int16(data + accel + 2 * i) / 8192.0f;
	}
	return true;
}


// #pragma mark - HID report descriptors


static const uint16 kPIDPage = 0x0f;
static const uint32 kMaxReportBits = 4096 * 8;
static const int kMaxCollectionDepth = 16;
static const int kMaxGlobalStack = 4;
static const uint32 kMaxLocalUsages = 64;


struct global_state {
	uint16	usagePage;
	int32	logicalMinimum;
	int32	logicalMaximum;
	bool	maximumSigned;
	uint32	reportSize;
	uint32	reportCount;
	uint8	reportID;
};


struct local_usage {
	uint16	page;
	uint16	usage;
};


static hid_report_size*
find_size(hid_layout& layout, uint8 type, uint8 id, bool create)
{
	for (uint32 i = 0; i < layout.reportCount; i++) {
		if (layout.reports[i].type == type && layout.reports[i].id == id)
			return &layout.reports[i];
	}
	if (!create || layout.reportCount >= kMaxHIDReports)
		return NULL;
	hid_report_size& size = layout.reports[layout.reportCount++];
	size.type = type;
	size.id = id;
	size.bits = 0;
	return &size;
}


static int32
signed_value(uint32 data, int size)
{
	switch (size) {
		case 1:
			return (int8_t)data;
		case 2:
			return (int16)data;
		default:
			return (int32)data;
	}
}


bool
parse_hid_descriptor(const uint8* descriptor, size_t length,
	hid_layout& layout)
{
	memset(&layout, 0, sizeof(layout));
	if (descriptor == NULL)
		return false;

	global_state global;
	memset(&global, 0, sizeof(global));
	global_state stack[kMaxGlobalStack];
	int stackDepth = 0;

	local_usage usages[kMaxLocalUsages];
	uint32 usageCount = 0;
	uint32 usageMinimum = 0;
	bool haveMinimum = false;

	// The usage of each open collection; 0 when it is not on page 0x0F.
	uint16 collections[kMaxCollectionDepth];
	int depth = 0;

	size_t offset = 0;
	while (offset < length) {
		uint8 prefix = descriptor[offset++];
		if (prefix == 0xfe) {
			// A long item: its size, its tag, its data.
			if (offset + 2 > length)
				return false;
			size_t skip = 2 + (size_t)descriptor[offset];
			if (offset + skip > length)
				return false;
			offset += skip;
			continue;
		}

		int size = prefix & 0x03;
		if (size == 3)
			size = 4;
		if (offset + size > length)
			return false;
		uint32 data = 0;
		for (int i = 0; i < size; i++)
			data |= (uint32)descriptor[offset + i] << (8 * i);
		offset += size;

		int type = (prefix >> 2) & 0x03;
		int tag = prefix >> 4;

		if (type == 1) {
			// Global items.
			switch (tag) {
				case 0x0:
					global.usagePage = (uint16)data;
					break;
				case 0x1:
					global.logicalMinimum = signed_value(data, size);
					break;
				case 0x2:
					global.logicalMaximum = signed_value(data, size);
					global.maximumSigned = global.logicalMaximum < 0;
					if (global.maximumSigned && size < 4) {
						// A maximum below a non-negative minimum is meant
						// unsigned (HID 1.11, 6.2.2.7).
						global.logicalMaximum = (int32)data;
					}
					break;
				case 0x7:
					global.reportSize = data;
					break;
				case 0x8:
					if (data == 0 || data > 0xff)
						return false;
					global.reportID = (uint8)data;
					layout.usesReportIDs = true;
					break;
				case 0x9:
					global.reportCount = data;
					break;
				case 0xa:
					if (stackDepth >= kMaxGlobalStack)
						return false;
					stack[stackDepth++] = global;
					break;
				case 0xb:
					if (stackDepth == 0)
						return false;
					global = stack[--stackDepth];
					break;
			}
			continue;
		}

		if (type == 2) {
			// Local items; a four-byte usage carries its page.
			uint16 page = size == 4 ? (uint16)(data >> 16) : global.usagePage;
			switch (tag) {
				case 0x0:
					if (usageCount < kMaxLocalUsages) {
						usages[usageCount].page = page;
						usages[usageCount].usage = (uint16)data;
						usageCount++;
					}
					break;
				case 0x1:
					usageMinimum = data;
					haveMinimum = true;
					break;
				case 0x2:
					if (haveMinimum) {
						for (uint32 u = (uint16)usageMinimum;
								u <= (uint16)data
									&& usageCount < kMaxLocalUsages; u++) {
							usages[usageCount].page = page;
							usages[usageCount].usage = (uint16)u;
							usageCount++;
						}
					}
					haveMinimum = false;
					break;
			}
			continue;
		}

		if (type != 0)
			continue;

		// Main items.
		if (tag == 0xa) {
			// Collection.
			if (depth >= kMaxCollectionDepth)
				return false;
			collections[depth++] = usageCount > 0 && usages[0].page == kPIDPage
				? usages[0].usage : 0;
		} else if (tag == 0xc) {
			// End Collection.
			if (depth == 0)
				return false;
			depth--;
		} else if (tag == 0x8 || tag == 0x9 || tag == 0xb) {
			uint8 reportType = tag == 0x8 ? HID_INPUT
				: tag == 0x9 ? HID_OUTPUT : HID_FEATURE;
			hid_report_size* reportSize = find_size(layout, reportType,
				global.reportID, true);
			if (reportSize == NULL)
				return false;
			uint32 bits = global.reportSize * global.reportCount;
			if (global.reportSize > 32 || global.reportCount > kMaxReportBits
				|| reportSize->bits + bits > kMaxReportBits)
				return false;

			bool constant = (data & 0x01) != 0;
			bool variable = (data & 0x02) != 0;
			uint16 report = 0;
			uint16 inner = 0;
			for (int i = 0; i < depth; i++) {
				if (collections[i] == 0)
					continue;
				if (report == 0)
					report = collections[i];
				inner = collections[i];
			}

			bool pid = usageCount > 0 && usages[0].page == kPIDPage;
			if (pid)
				layout.hasPIDPage = true;
			if (!constant && pid && global.reportSize > 0) {
				uint32 count = variable ? global.reportCount : 1;
				for (uint32 i = 0; i < count
						&& layout.fieldCount < kMaxHIDFields; i++) {
					hid_field& field = layout.fields[layout.fieldCount++];
					memset(&field, 0, sizeof(field));
					field.type = reportType;
					field.id = global.reportID;
					field.collection = inner;
					field.report = report;
					field.isArray = !variable;
					field.logicalMinimum = global.logicalMinimum;
					field.logicalMaximum = global.logicalMaximum;
					field.bitOffset = reportSize->bits
						+ i * global.reportSize;
					field.bitSize = (uint8)global.reportSize;
					if (variable) {
						uint32 index = i < usageCount ? i : usageCount - 1;
						field.usage = usages[index].usage;
					} else {
						for (uint32 u = 0; u < usageCount
								&& u < kMaxArrayUsages; u++)
							field.usages[u] = usages[u].usage;
						field.usageCount = usageCount < kMaxArrayUsages
							? (uint8)usageCount : (uint8)kMaxArrayUsages;
					}
				}
			}
			reportSize->bits += bits;
		}

		// Every main item ends the local items.
		usageCount = 0;
		haveMinimum = false;
	}

	return depth == 0;
}


size_t
hid_report_length(const hid_layout& layout, uint8 type, uint8 id)
{
	for (uint32 i = 0; i < layout.reportCount; i++) {
		const hid_report_size& size = layout.reports[i];
		if (size.type == type && size.id == id)
			return (size.bits + 7) / 8 + (layout.usesReportIDs ? 1 : 0);
	}
	return 0;
}


// #pragma mark - reading and writing fields


static bool
report_begin(const hid_layout& layout, const pid_report_ref& ref,
	hid_report& out)
{
	if (!ref.present)
		return false;
	size_t length = hid_report_length(layout, ref.type, ref.id);
	if (length == 0 || length > kMaxReportBytes)
		return false;
	memset(&out, 0, sizeof(out));
	out.type = ref.type;
	out.id = ref.id;
	out.length = (uint32)length;
	if (layout.usesReportIDs)
		out.data[0] = ref.id;
	return true;
}


static void
write_bits(const hid_layout& layout, hid_report& out, const hid_field& field,
	int32 value)
{
	uint32 offset = field.bitOffset + (layout.usesReportIDs ? 8 : 0);
	for (uint32 bit = 0; bit < field.bitSize; bit++) {
		uint32 at = offset + bit;
		if (at / 8 >= out.length)
			return;
		uint8 mask = (uint8)(1 << (at % 8));
		if ((((uint32)value) >> bit) & 1)
			out.data[at / 8] |= mask;
		else
			out.data[at / 8] &= (uint8)~mask;
	}
}


static int32
read_bits(const hid_layout& layout, const uint8* data, size_t length,
	const hid_field& field)
{
	uint32 offset = field.bitOffset + (layout.usesReportIDs ? 8 : 0);
	uint32 value = 0;
	for (uint32 bit = 0; bit < field.bitSize && bit < 32; bit++) {
		uint32 at = offset + bit;
		if (at / 8 >= length)
			break;
		if ((data[at / 8] >> (at % 8)) & 1)
			value |= 1u << bit;
	}
	// Signed when the logical minimum is negative.
	if (field.logicalMinimum < 0 && field.bitSize > 0 && field.bitSize < 32
		&& (value & (1u << (field.bitSize - 1))) != 0)
		value |= ~0u << field.bitSize;
	return (int32)value;
}


static const hid_field*
find_variable(const hid_layout& layout, uint8 type, uint8 id, uint16 usage)
{
	for (uint32 i = 0; i < layout.fieldCount; i++) {
		const hid_field& field = layout.fields[i];
		if (field.type == type && field.id == id && !field.isArray
			&& field.usage == usage)
			return &field;
	}
	return NULL;
}


static const hid_field*
find_array(const hid_layout& layout, uint8 type, uint8 id,
	uint16 collection)
{
	for (uint32 i = 0; i < layout.fieldCount; i++) {
		const hid_field& field = layout.fields[i];
		if (field.type == type && field.id == id && field.isArray
			&& field.collection == collection)
			return &field;
	}
	return NULL;
}


static bool
array_has(const hid_field& field, uint16 usage)
{
	for (uint32 i = 0; i < field.usageCount; i++) {
		if (field.usages[i] == usage)
			return true;
	}
	return false;
}


// Sets a variable field when the report has it.
static void
set_value(const hid_layout& layout, hid_report& out, uint16 usage,
	int32 value)
{
	const hid_field* field = find_variable(layout, out.type, out.id, usage);
	if (field == NULL)
		return;
	if (value > field->logicalMaximum)
		value = field->logicalMaximum;
	if (value < field->logicalMinimum)
		value = field->logicalMinimum;
	write_bits(layout, out, *field, value);
}


// Sets a fraction 0 to 1 of a variable field's logical maximum.
static void
set_fraction(const hid_layout& layout, hid_report& out, uint16 usage,
	float fraction)
{
	const hid_field* field = find_variable(layout, out.type, out.id, usage);
	if (field == NULL)
		return;
	if (!(fraction > 0))
		fraction = 0;
	if (fraction > 1)
		fraction = 1;
	write_bits(layout, out, *field,
		(int32)(fraction * field->logicalMaximum + 0.5f));
}


// Selects one usage of the array field in that collection.
static bool
set_selector(const hid_layout& layout, hid_report& out, uint16 collection,
	uint16 usage)
{
	const hid_field* field = find_array(layout, out.type, out.id,
		collection);
	if (field == NULL)
		return false;
	for (uint32 i = 0; i < field->usageCount; i++) {
		if (field->usages[i] == usage) {
			write_bits(layout, out, *field,
				field->logicalMinimum + (int32)i);
			return true;
		}
	}
	return false;
}


// #pragma mark - HID PID force feedback


// Usages of the PID page (USB PID 1.0, chapter 5; HID Usage Tables 0x0F).
enum {
	kPIDSetEffectReport = 0x21,
	kPIDEffectBlockIndex = 0x22,
	kPIDEffectType = 0x25,
	kPIDConstantForce = 0x26,
	kPIDSine = 0x31,
	kPIDDuration = 0x50,
	kPIDSamplePeriod = 0x51,
	kPIDGain = 0x52,
	kPIDTriggerButton = 0x53,
	kPIDTriggerRepeatInterval = 0x54,
	kPIDDirectionEnable = 0x56,
	kPIDSetPeriodicReport = 0x6e,
	kPIDOffset = 0x6f,
	kPIDMagnitude = 0x70,
	kPIDPhase = 0x71,
	kPIDPeriod = 0x72,
	kPIDSetConstantForceReport = 0x73,
	kPIDEffectOperationReport = 0x77,
	kPIDEffectOperation = 0x78,
	kPIDOpEffectStartSolo = 0x7a,
	kPIDOpEffectStop = 0x7b,
	kPIDLoopCount = 0x7c,
	kPIDDeviceGainReport = 0x7d,
	kPIDDeviceGain = 0x7e,
	kPIDBlockLoadReport = 0x89,
	kPIDBlockLoadStatus = 0x8b,
	kPIDBlockLoadSuccess = 0x8c,
	kPIDDeviceControlReport = 0x95,
	kPIDEnableActuators = 0x97,
	kPIDDeviceReset = 0x9a,
	kPIDDeviceControl = 0x96,
	kPIDStartDelay = 0xa7,
	kPIDCreateNewEffectReport = 0xab
};


static pid_report_ref
find_report(const hid_layout& layout, uint8 type, uint16 report)
{
	pid_report_ref ref = { false, 0, 0 };
	for (uint32 i = 0; i < layout.fieldCount; i++) {
		const hid_field& field = layout.fields[i];
		if (field.type == type && field.report == report) {
			ref.present = true;
			ref.type = field.type;
			ref.id = field.id;
			return ref;
		}
	}
	return ref;
}


bool
pid_find(const hid_layout& layout, pid_device& device)
{
	memset(&device, 0, sizeof(device));
	if (!layout.hasPIDPage)
		return false;

	device.setEffect = find_report(layout, HID_OUTPUT, kPIDSetEffectReport);
	device.setPeriodic = find_report(layout, HID_OUTPUT,
		kPIDSetPeriodicReport);
	device.setConstant = find_report(layout, HID_OUTPUT,
		kPIDSetConstantForceReport);
	device.effectOperation = find_report(layout, HID_OUTPUT,
		kPIDEffectOperationReport);
	device.deviceControl = find_report(layout, HID_OUTPUT,
		kPIDDeviceControlReport);
	device.deviceGain = find_report(layout, HID_OUTPUT,
		kPIDDeviceGainReport);
	device.createNewEffect = find_report(layout, HID_FEATURE,
		kPIDCreateNewEffectReport);
	device.blockLoad = find_report(layout, HID_FEATURE, kPIDBlockLoadReport);

	if (!device.setEffect.present || !device.effectOperation.present)
		return false;

	// The effect types the Set Effect Report offers.
	const hid_field* types = find_array(layout, HID_OUTPUT,
		device.setEffect.id, kPIDEffectType);
	if (types == NULL)
		return false;
	if (device.setPeriodic.present && array_has(*types, kPIDSine)) {
		device.periodic = true;
		device.effectType = kPIDSine;
	} else if (device.setConstant.present
		&& array_has(*types, kPIDConstantForce)) {
		device.effectType = kPIDConstantForce;
	} else
		return false;

	// Effect Operation must offer Start Solo and Stop.
	const hid_field* operation = find_array(layout, HID_OUTPUT,
		device.effectOperation.id, kPIDEffectOperation);
	if (operation == NULL || !array_has(*operation, kPIDOpEffectStartSolo)
		|| !array_has(*operation, kPIDOpEffectStop))
		return false;

	// Every report must fit.
	const pid_report_ref* refs[] = { &device.setEffect, &device.setPeriodic,
		&device.setConstant, &device.effectOperation, &device.deviceControl,
		&device.deviceGain, &device.createNewEffect, &device.blockLoad };
	for (size_t i = 0; i < sizeof(refs) / sizeof(refs[0]); i++) {
		if (refs[i]->present && hid_report_length(layout, refs[i]->type,
				refs[i]->id) > kMaxReportBytes)
			return false;
	}

	device.usable = true;
	return true;
}


uint32
pid_init_reports(const hid_layout& layout, const pid_device& device,
	hid_report* out, uint32 capacity)
{
	uint32 count = 0;
	if (count < capacity && report_begin(layout, device.deviceControl,
			out[count])
		&& set_selector(layout, out[count], kPIDDeviceControl,
			kPIDDeviceReset))
		count++;
	if (count < capacity && report_begin(layout, device.deviceControl,
			out[count])
		&& set_selector(layout, out[count], kPIDDeviceControl,
			kPIDEnableActuators))
		count++;
	if (count < capacity && report_begin(layout, device.deviceGain,
			out[count])) {
		set_fraction(layout, out[count], kPIDDeviceGain, 1);
		count++;
	}
	return count;
}


bool
pid_create_report(const hid_layout& layout, const pid_device& device,
	hid_report& out)
{
	if (!report_begin(layout, device.createNewEffect, out))
		return false;
	return set_selector(layout, out, kPIDEffectType, device.effectType);
}


bool
pid_parse_block_load(const hid_layout& layout, const pid_device& device,
	const uint8* data, size_t length, uint8& blockIndex)
{
	if (!device.blockLoad.present || data == NULL)
		return false;
	size_t expected = hid_report_length(layout, HID_FEATURE,
		device.blockLoad.id);
	if (length < expected || expected == 0)
		return false;
	if (layout.usesReportIDs && data[0] != device.blockLoad.id)
		return false;

	const hid_field* index = find_variable(layout, HID_FEATURE,
		device.blockLoad.id, kPIDEffectBlockIndex);
	const hid_field* status = find_array(layout, HID_FEATURE,
		device.blockLoad.id, kPIDBlockLoadStatus);
	if (index == NULL || status == NULL)
		return false;

	int32 selected = read_bits(layout, data, length, *status)
		- status->logicalMinimum;
	if (selected < 0 || selected >= status->usageCount
		|| status->usages[selected] != kPIDBlockLoadSuccess)
		return false;
	int32 value = read_bits(layout, data, length, *index);
	if (value < 1 || value > 0xff)
		return false;
	blockIndex = (uint8)value;
	return true;
}


uint32
pid_start_reports(const hid_layout& layout, const pid_device& device,
	uint8 blockIndex, float magnitude, uint32 durationMS, uint32 periodMS,
	hid_report* out, uint32 capacity)
{
	if (!device.usable || capacity < 3)
		return 0;

	// Set Effect: the type, how long, full gain, no trigger button, no
	// delay, along the first axis.
	hid_report& effect = out[0];
	if (!report_begin(layout, device.setEffect, effect)
		|| !set_selector(layout, effect, kPIDEffectType, device.effectType))
		return 0;
	set_value(layout, effect, kPIDEffectBlockIndex, blockIndex);
	const hid_field* duration = find_variable(layout, effect.type, effect.id,
		kPIDDuration);
	if (duration != NULL) {
		int32 value = durationMS == 0
			? duration->logicalMaximum : (int32)durationMS;
		set_value(layout, effect, kPIDDuration, value);
	}
	set_value(layout, effect, kPIDTriggerRepeatInterval, 0);
	set_value(layout, effect, kPIDSamplePeriod, 0);
	set_fraction(layout, effect, kPIDGain, 1);
	const hid_field* trigger = find_variable(layout, effect.type, effect.id,
		kPIDTriggerButton);
	if (trigger != NULL) {
		// A value outside the logical range is the null value: no button.
		int32 none = trigger->logicalMinimum > 0
			? 0 : trigger->logicalMaximum + 1;
		write_bits(layout, effect, *trigger, none);
	}
	set_value(layout, effect, kPIDDirectionEnable, 1);
	set_value(layout, effect, kPIDStartDelay, 0);

	// The force itself.
	hid_report& force = out[1];
	if (device.periodic) {
		if (!report_begin(layout, device.setPeriodic, force))
			return 0;
		set_value(layout, force, kPIDEffectBlockIndex, blockIndex);
		set_fraction(layout, force, kPIDMagnitude, magnitude);
		set_value(layout, force, kPIDOffset, 0);
		set_value(layout, force, kPIDPhase, 0);
		set_value(layout, force, kPIDPeriod, (int32)periodMS);
	} else {
		if (!report_begin(layout, device.setConstant, force))
			return 0;
		set_value(layout, force, kPIDEffectBlockIndex, blockIndex);
		set_fraction(layout, force, kPIDMagnitude, magnitude);
	}

	// Start it alone, once.
	hid_report& operation = out[2];
	if (!report_begin(layout, device.effectOperation, operation)
		|| !set_selector(layout, operation, kPIDEffectOperation,
			kPIDOpEffectStartSolo))
		return 0;
	set_value(layout, operation, kPIDEffectBlockIndex, blockIndex);
	set_value(layout, operation, kPIDLoopCount, 1);
	return 3;
}


bool
pid_stop_report(const hid_layout& layout, const pid_device& device,
	uint8 blockIndex, hid_report& out)
{
	if (!device.usable || !report_begin(layout, device.effectOperation, out)
		|| !set_selector(layout, out, kPIDEffectOperation, kPIDOpEffectStop))
		return false;
	set_value(layout, out, kPIDEffectBlockIndex, blockIndex);
	set_value(layout, out, kPIDLoopCount, 1);
	return true;
}


}	// namespace R5GameController
