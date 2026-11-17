/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The SDL_GameControllerDB line format, read the way SDL documents it
 * (README of SDL_GameControllerDB and SDL's gamecontroller mapping string:
 * "GUID,name,target:source,...,platform:Windows,"). No SDL code is used.
 */


#include "GameControllerMapping.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <FindDirectory.h>
#include <Path.h>


namespace R5GameController {


static const char* const kButtonNames[B_CONTROLLER_BUTTON_COUNT] = {
	"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
	"leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright",
	"misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad"
};

static const char* const kAxisNames[B_CONTROLLER_AXIS_COUNT] = {
	"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"
};


static int
hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


bool
parse_guid(const char* guid, uint16* vendor, uint16* product)
{
	if (guid == NULL)
		return false;
	uint8 bytes[16];
	for (int i = 0; i < 16; i++) {
		int high = hex_value(guid[i * 2]);
		int low = high < 0 ? -1 : hex_value(guid[i * 2 + 1]);
		if (high < 0 || low < 0)
			return false;
		bytes[i] = (uint8)(high << 4 | low);
	}
	if (hex_value(guid[32]) >= 0)
		return false;

	// bus (2), CRC (2), vendor (2), 0 (2), product (2), 0 (2), version (2),
	// driver data (2); all little endian. The zeros make it the vendor and
	// product form, not a name hash or "xinput".
	if (bytes[6] != 0 || bytes[7] != 0 || bytes[10] != 0 || bytes[11] != 0)
		return false;
	uint16 v = (uint16)(bytes[4] | bytes[5] << 8);
	uint16 p = (uint16)(bytes[8] | bytes[9] << 8);
	if (v == 0)
		return false;
	if (vendor != NULL)
		*vendor = v;
	if (product != NULL)
		*product = p;
	return true;
}


// "b3", "a2", "-a2", "+a5~", "h0.4"
static bool
parse_source(const char* text, size_t length, mapping_source& source)
{
	memset(&source, 0, sizeof(source));
	size_t i = 0;
	if (i < length && (text[i] == '+' || text[i] == '-')) {
		source.half = text[i] == '+' ? 1 : -1;
		i++;
	}
	if (length > 0 && text[length - 1] == '~') {
		source.invert = true;
		length--;
	}
	if (i >= length)
		return false;

	char kind = text[i++];
	if (i >= length || !isdigit((unsigned char)text[i]))
		return false;
	int number = 0;
	while (i < length && isdigit((unsigned char)text[i])) {
		number = number * 10 + (text[i] - '0');
		if (number > 255)
			return false;
		i++;
	}
	source.index = (uint8)number;

	switch (kind) {
		case 'b':
			source.type = SOURCE_BUTTON;
			return i == length && source.half == 0;
		case 'a':
			source.type = SOURCE_AXIS;
			return i == length;
		case 'h':
		{
			if (i >= length || text[i] != '.' || source.half != 0)
				return false;
			i++;
			int mask = 0;
			while (i < length && isdigit((unsigned char)text[i])) {
				mask = mask * 10 + (text[i] - '0');
				if (mask > 15)
					return false;
				i++;
			}
			if (i != length || mask == 0)
				return false;
			source.type = SOURCE_HAT;
			source.hatMask = (uint8)mask;
			return true;
		}
	}
	return false;
}


static bool
target_is(const char* text, size_t length, const char* name)
{
	return strlen(name) == length && strncmp(text, name, length) == 0;
}


static bool
apply_pair(const char* key, size_t keyLength, const char* value,
	size_t valueLength, controller_mapping& mapping)
{
	int8 targetHalf = 0;
	if (keyLength > 0 && (key[0] == '+' || key[0] == '-')) {
		targetHalf = key[0] == '+' ? 1 : -1;
		key++;
		keyLength--;
	}

	mapping_source source;
	if (!parse_source(value, valueLength, source))
		return false;

	if (targetHalf == 0) {
		for (int32 i = 0; i < B_CONTROLLER_BUTTON_COUNT; i++) {
			if (target_is(key, keyLength, kButtonNames[i])) {
				mapping.buttons[i] = source;
				return true;
			}
		}
	}
	for (int32 i = 0; i < B_CONTROLLER_AXIS_COUNT; i++) {
		if (!target_is(key, keyLength, kAxisNames[i]))
			continue;
		if (targetHalf > 0)
			mapping.axes[i].positive = source;
		else if (targetHalf < 0)
			mapping.axes[i].negative = source;
		else
			mapping.axes[i].whole = source;
		return true;
	}
	// Other keys (crc, hint, sdk, misc2 and later) are not used.
	return false;
}


// "target:source,target:source,..." up to the end of the line; how many
// pairs were taken.
static int32
apply_pairs(const char* item, controller_mapping& mapping)
{
	int32 count = 0;
	while (*item != '\0' && *item != '\n' && *item != '\r') {
		const char* end = item;
		while (*end != ',' && *end != '\0' && *end != '\n' && *end != '\r')
			end++;
		const char* colon = (const char*)memchr(item, ':', end - item);
		if (colon != NULL && apply_pair(item, colon - item, colon + 1,
				end - colon - 1, mapping))
			count++;
		if (*end != ',')
			break;
		item = end + 1;
	}
	return count;
}


bool
parse_line(const char* line, uint16 vendor, uint16 product,
	controller_mapping& mapping)
{
	if (line == NULL || line[0] == '#' || line[0] == '\0')
		return false;
	// Cheap checks first: most lines are for another controller.
	if (strstr(line, "platform:Windows") == NULL)
		return false;
	uint16 v;
	uint16 p;
	if (!parse_guid(line, &v, &p) || v != vendor || p != product)
		return false;
	if (line[32] != ',')
		return false;

	memset(&mapping, 0, sizeof(mapping));
	const char* name = line + 33;
	const char* comma = strchr(name, ',');
	if (comma == NULL)
		return false;
	size_t nameLength = comma - name;
	if (nameLength >= sizeof(mapping.name))
		nameLength = sizeof(mapping.name) - 1;
	memcpy(mapping.name, name, nameLength);
	mapping.name[nameLength] = '\0';

	apply_pairs(comma + 1, mapping);
	return true;
}


status_t
find_mapping(const char* path, uint16 vendor, uint16 product,
	controller_mapping& mapping)
{
	return find_mapping(path, vendor, product, 0, mapping);
}


status_t
find_mapping(const char* path, uint16 vendor, uint16 product, uint16 bus,
	controller_mapping& mapping)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;

	// The longest lines of the database are a few hundred bytes.
	char line[2048];
	bool skipping = false;
	status_t status = B_ENTRY_NOT_FOUND;
	controller_mapping found;
	while (fgets(line, sizeof(line), file) != NULL) {
		bool whole = strchr(line, '\n') != NULL || feof(file);
		if (skipping) {
			// The rest of a line that did not fit.
			skipping = !whole;
			continue;
		}
		if (!whole) {
			skipping = true;
			continue;
		}
		if (!parse_line(line, vendor, product, found))
			continue;
		// The first line of any bus, unless one for this bus follows.
		if (status != B_OK) {
			mapping = found;
			status = B_OK;
		}
		if (bus == 0)
			break;
		if (guid_bus(line) == bus) {
			mapping = found;
			break;
		}
	}
	fclose(file);
	return status;
}


status_t
find_system_mapping(uint16 vendor, uint16 product,
	controller_mapping& mapping)
{
	return find_system_mapping(vendor, product, 0, mapping);
}


status_t
find_system_mapping(uint16 vendor, uint16 product, uint16 bus,
	controller_mapping& mapping)
{
	// The image puts the files in system/non-packaged/data; a package
	// would put them in system/data.
	static const directory_which kDirectories[] = {
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY
	};
	static const char* const kFiles[] = {
		"r5_gamecontrollerdb.txt",
		"gamecontrollerdb.txt"
	};
	for (size_t f = 0; f < sizeof(kFiles) / sizeof(kFiles[0]); f++) {
		for (size_t d = 0;
				d < sizeof(kDirectories) / sizeof(kDirectories[0]); d++) {
			BPath path;
			if (find_directory(kDirectories[d], &path) != B_OK)
				continue;
			path.Append("game_controllers");
			path.Append(kFiles[f]);
			if (find_mapping(path.Path(), vendor, product, bus, mapping)
					== B_OK)
				return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


void
axis_order(const uint16* usages, int32 count, int32* order)
{
	for (int32 i = 0; i < count; i++)
		order[i] = i;
	if (usages == NULL)
		return;
	// Insertion sort: stable, and there are only a few axes.
	for (int32 i = 1; i < count; i++) {
		int32 value = order[i];
		uint32 key = usages[value] != 0 ? usages[value] : 0x10000;
		int32 j = i - 1;
		while (j >= 0) {
			uint32 other = usages[order[j]] != 0 ? usages[order[j]] : 0x10000;
			if (other <= key)
				break;
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = value;
	}
}


uint8
hat_mask(uint8 hat)
{
	static const uint8 kMasks[9] = {
		0,		// centered
		1,		// N
		1 | 2,	// NE
		2,		// E
		2 | 4,	// SE
		4,		// S
		4 | 8,	// SW
		8,		// W
		8 | 1	// NW
	};
	return hat <= 8 ? kMasks[hat] : 0;
}


static int16
clamp16(int32 value)
{
	if (value < -32768)
		return -32768;
	if (value > 32767)
		return 32767;
	return (int16)value;
}


static int16
raw_axis(const raw_state& raw, const mapping_source& source)
{
	if (source.index >= raw.axisCount)
		return 0;
	int32 value = raw.axes[source.index];
	if (source.invert)
		value = -value - 1;
	return clamp16(value);
}


static bool
source_pressed(const raw_state& raw, const mapping_source& source)
{
	switch (source.type) {
		case SOURCE_BUTTON:
			return source.index < raw.buttonCount && raw.buttons[source.index];
		case SOURCE_HAT:
			return source.index < raw.hatCount
				&& (hat_mask(raw.hats[source.index]) & source.hatMask) != 0;
		case SOURCE_AXIS:
		{
			int16 value = raw_axis(raw, source);
			if (source.half > 0)
				return value > 16384;
			if (source.half < 0)
				return value < -16384;
			return value > 0;
		}
	}
	return false;
}


// 0 to 32767: how far a source is pushed, for a trigger or a half axis.
static int32
source_amount(const raw_state& raw, const mapping_source& source)
{
	switch (source.type) {
		case SOURCE_BUTTON:
		case SOURCE_HAT:
			return source_pressed(raw, source) ? 32767 : 0;
		case SOURCE_AXIS:
		{
			int32 value = raw_axis(raw, source);
			if (source.half > 0)
				return value > 0 ? value : 0;
			if (source.half < 0)
				return value < 0 ? -value - 1 : 0;
			return (value + 32768) / 2;
		}
	}
	return 0;
}


static int16
axis_value(const raw_state& raw, const axis_mapping& axis, bool trigger)
{
	const mapping_source& whole = axis.whole;
	if (whole.type != SOURCE_NONE) {
		if (trigger)
			return clamp16(source_amount(raw, whole));
		if (whole.type == SOURCE_AXIS && whole.half == 0)
			return raw_axis(raw, whole);
		// A half axis, a button or a hat over the whole stick range.
		return clamp16(source_amount(raw, whole) * 2 - 32767);
	}
	int32 value = 0;
	if (axis.positive.type != SOURCE_NONE)
		value += source_amount(raw, axis.positive);
	if (axis.negative.type != SOURCE_NONE && !trigger)
		value -= source_amount(raw, axis.negative);
	if (trigger && value < 0)
		value = 0;
	return clamp16(value);
}


void
map_state(const controller_mapping& mapping, const raw_state& raw,
	mapped_state& out)
{
	memset(&out, 0, sizeof(out));
	for (int32 i = 0; i < B_CONTROLLER_BUTTON_COUNT; i++) {
		if (mapping.buttons[i].type != SOURCE_NONE
			&& source_pressed(raw, mapping.buttons[i]))
			out.buttons |= (uint32)1 << i;
	}
	for (int32 i = 0; i < B_CONTROLLER_AXIS_COUNT; i++) {
		bool trigger = i == B_CONTROLLER_AXIS_LEFT_TRIGGER
			|| i == B_CONTROLLER_AXIS_RIGHT_TRIGGER;
		out.axes[i] = axis_value(raw, mapping.axes[i], trigger);
	}
}


bool
mapping_has_button(const controller_mapping& mapping,
	game_controller_button button)
{
	return button >= 0 && button < B_CONTROLLER_BUTTON_COUNT
		&& mapping.buttons[button].type != SOURCE_NONE;
}


bool
mapping_has_axis(const controller_mapping& mapping, game_controller_axis axis)
{
	if (axis < 0 || axis >= B_CONTROLLER_AXIS_COUNT)
		return false;
	const axis_mapping& a = mapping.axes[axis];
	return a.whole.type != SOURCE_NONE || a.positive.type != SOURCE_NONE
		|| a.negative.type != SOURCE_NONE;
}


static bool
name_has(const char* name, const char* word)
{
	size_t length = strlen(word);
	for (const char* p = name; *p != '\0'; p++) {
		if (strncasecmp(p, word, length) == 0)
			return true;
	}
	return false;
}


game_controller_family
family_for(uint16 vendor, uint16 product, const char* name, bool hasMapping)
{
	(void)product;
	if (!hasMapping)
		return B_GAME_CONTROLLER_FAMILY_UNKNOWN;
	if (name == NULL)
		name = "";
	// Six face buttons in two rows: the 8BitDo M30 and Sega-style pads.
	if (name_has(name, "M30") || name_has(name, "Genesis")
		|| name_has(name, "Mega Drive") || name_has(name, "Saturn"))
		return B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS;
	if (vendor == 0x054c)
		return B_GAME_CONTROLLER_FAMILY_PLAYSTATION;
	if (vendor == 0x057e)
		return B_GAME_CONTROLLER_FAMILY_NINTENDO;
	return B_GAME_CONTROLLER_FAMILY_XBOX;
}


// #pragma mark - what the HID descriptor says first


uint16
guid_bus(const char* guid)
{
	if (guid == NULL)
		return 0;
	int digits[4];
	for (int i = 0; i < 4; i++) {
		digits[i] = hex_value(guid[i]);
		if (digits[i] < 0)
			return 0;
	}
	// Little endian: "0300" is 0x0003.
	return (uint16)((digits[0] << 4 | digits[1])
		| (digits[2] << 4 | digits[3]) << 8);
}


static bool
has_button_usage(const device_shape& shape, uint32 usage)
{
	if (!shape.knowsButtons)
		return usage >= 1 && (int32)usage <= shape.buttonCount;
	if (usage < 1 || usage > 128)
		return false;
	return (shape.buttonUsages[(usage - 1) / 32]
		& ((uint32)1 << ((usage - 1) % 32))) != 0;
}


static int32
axis_with_usage(const device_shape& shape, uint16 usage)
{
	for (int32 i = 0; i < shape.axisCount; i++) {
		if (shape.axisUsage[i] == usage)
			return i;
	}
	return -1;
}


static void
renumber_button(mapping_source& source, const uint8* byUsage, int32 count)
{
	if (source.type != SOURCE_BUTTON)
		return;
	if (source.index >= count)
		memset(&source, 0, sizeof(source));
	else
		source.index = byUsage[source.index];
}


void
buttons_by_usage(controller_mapping& mapping, const device_shape& shape)
{
	if (!shape.knowsButtons)
		return;
	// DirectInput's button n is the n-th usage the report has.
	uint8 byUsage[128];
	int32 count = 0;
	for (uint32 usage = 1; usage <= 128; usage++) {
		if (has_button_usage(shape, usage))
			byUsage[count++] = (uint8)(usage - 1);
	}
	for (int32 i = 0; i < B_CONTROLLER_BUTTON_COUNT; i++)
		renumber_button(mapping.buttons[i], byUsage, count);
	for (int32 i = 0; i < B_CONTROLLER_AXIS_COUNT; i++) {
		renumber_button(mapping.axes[i].whole, byUsage, count);
		renumber_button(mapping.axes[i].positive, byUsage, count);
		renumber_button(mapping.axes[i].negative, byUsage, count);
	}
}


static void
fit_source(mapping_source& source, const device_shape& shape)
{
	bool keep = true;
	switch (source.type) {
		case SOURCE_BUTTON:
			keep = has_button_usage(shape, source.index + 1);
			break;
		case SOURCE_AXIS:
			keep = source.index < shape.axisCount;
			break;
		case SOURCE_HAT:
			keep = source.index < shape.hatCount;
			break;
	}
	if (!keep)
		memset(&source, 0, sizeof(source));
}


static bool
uses_axis(const mapping_source& source, int32 axis)
{
	return source.type == SOURCE_AXIS && source.index == axis;
}


// Whether anything but the d-pad reads that axis.
static bool
axis_in_use(const controller_mapping& mapping, int32 axis)
{
	for (int32 i = 0; i < B_CONTROLLER_BUTTON_COUNT; i++) {
		if (i >= B_CONTROLLER_BUTTON_DPAD_UP
			&& i <= B_CONTROLLER_BUTTON_DPAD_RIGHT)
			continue;
		if (uses_axis(mapping.buttons[i], axis))
			return true;
	}
	for (int32 i = 0; i < B_CONTROLLER_AXIS_COUNT; i++) {
		const axis_mapping& a = mapping.axes[i];
		if (uses_axis(a.whole, axis) || uses_axis(a.positive, axis)
			|| uses_axis(a.negative, axis))
			return true;
	}
	return false;
}


static void
set_hat(mapping_source& source, uint8 mask)
{
	memset(&source, 0, sizeof(source));
	source.type = SOURCE_HAT;
	source.hatMask = mask;
}


static void
set_half_axis(mapping_source& source, int32 axis, int8 half)
{
	memset(&source, 0, sizeof(source));
	source.type = SOURCE_AXIS;
	source.index = (uint8)axis;
	source.half = half;
}


static void
set_button(mapping_source& source, int32 index)
{
	memset(&source, 0, sizeof(source));
	if (index < 0)
		return;
	source.type = SOURCE_BUTTON;
	source.index = (uint8)index;
}


static void
set_axis(mapping_source& source, int32 axis)
{
	memset(&source, 0, sizeof(source));
	if (axis < 0)
		return;
	source.type = SOURCE_AXIS;
	source.index = (uint8)axis;
}


void
fit_to_device(controller_mapping& mapping, const device_shape& shape)
{
	for (int32 i = 0; i < B_CONTROLLER_BUTTON_COUNT; i++)
		fit_source(mapping.buttons[i], shape);
	for (int32 i = 0; i < B_CONTROLLER_AXIS_COUNT; i++) {
		fit_source(mapping.axes[i].whole, shape);
		fit_source(mapping.axes[i].positive, shape);
		fit_source(mapping.axes[i].negative, shape);
	}

	// A d-pad made of buttons is left as it is: a hat or a pair of axes
	// can be read from the descriptor, which buttons are the d-pad cannot.
	mapping_source* dpad = &mapping.buttons[B_CONTROLLER_BUTTON_DPAD_UP];
	bool buttons = false;
	for (int32 i = 0; i < 4; i++) {
		if (dpad[i].type == SOURCE_BUTTON)
			buttons = true;
	}
	if (buttons)
		return;

	if (shape.hatCount > 0) {
		set_hat(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_UP], 1);
		set_hat(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_RIGHT], 2);
		set_hat(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_DOWN], 4);
		set_hat(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_LEFT], 8);
		return;
	}

	// No hat: a d-pad on X and Y, as many Bluetooth pads without sticks
	// report it, when no stick reads them.
	int32 x = axis_with_usage(shape, 0x30);
	int32 y = axis_with_usage(shape, 0x31);
	if (x < 0 || y < 0 || axis_in_use(mapping, x) || axis_in_use(mapping, y))
		return;
	set_half_axis(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_LEFT], x, -1);
	set_half_axis(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_RIGHT], x, 1);
	set_half_axis(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_UP], y, -1);
	set_half_axis(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_DOWN], y, 1);
}


bool
fixed_layout(game_controller_family family, const device_shape& shape,
	controller_mapping& mapping)
{
	// Button numbers are HID usage - 1, in the order of the button enum
	// (A B X Y back guide start leftstick rightstick leftshoulder
	// rightshoulder), then the triggers as buttons when the controller has
	// no free axes for them, then misc1 and the touchpad. -1: none.
	struct layout {
		int8	buttons[11];
		int8	leftTrigger;
		int8	rightTrigger;
		int8	misc1;
		int8	touchpad;
		bool	sticks;
	};
	// Xbox and Nintendo: the order of the W3C standard gamepad, which most
	// HID pads and the Switch Pro Controller follow (the Nintendo shape
	// has the same places, only the labels differ). PlayStation: the
	// DualShock 4 and DualSense (square, cross, circle, triangle).
	// Sega Genesis: the 8BitDo M30 as the database's Windows lines number
	// it (C and Z are the right and left shoulder, L and R the triggers).
	static const layout kXbox = {
		{ 0, 1, 2, 3, 8, 12, 9, 10, 11, 4, 5 }, 6, 7, -1, -1, true };
	static const layout kPlayStation = {
		{ 1, 2, 0, 3, 8, 12, 9, 10, 11, 4, 5 }, 6, 7, 14, 13, true };
	static const layout kNintendo = {
		{ 0, 1, 2, 3, 8, 12, 9, 10, 11, 4, 5 }, 6, 7, 13, -1, true };
	static const layout kSega = {
		{ 0, 1, 3, 4, 10, 2, 11, -1, -1, 8, 6 }, 9, 7, -1, -1, false };

	const layout* chosen;
	switch (family) {
		case B_GAME_CONTROLLER_FAMILY_XBOX:
			chosen = &kXbox;
			break;
		case B_GAME_CONTROLLER_FAMILY_PLAYSTATION:
			chosen = &kPlayStation;
			break;
		case B_GAME_CONTROLLER_FAMILY_NINTENDO:
			chosen = &kNintendo;
			break;
		case B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS:
			chosen = &kSega;
			break;
		default:
			return false;
	}

	memset(&mapping, 0, sizeof(mapping));
	for (int32 i = 0; i < 11; i++)
		set_button(mapping.buttons[i], chosen->buttons[i]);
	set_button(mapping.buttons[B_CONTROLLER_BUTTON_MISC1], chosen->misc1);
	set_button(mapping.buttons[B_CONTROLLER_BUTTON_TOUCHPAD],
		chosen->touchpad);
	set_hat(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_UP], 1);
	set_hat(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_RIGHT], 2);
	set_hat(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_DOWN], 4);
	set_hat(mapping.buttons[B_CONTROLLER_BUTTON_DPAD_LEFT], 8);

	// The sticks by usage: X and Y on the left; Z and Rz on the right, or
	// Rx and Ry when there is no Z. The triggers on Rx and Ry when the
	// right stick leaves them free, else on buttons.
	int32 rx = axis_with_usage(shape, 0x33);
	int32 ry = axis_with_usage(shape, 0x34);
	bool triggerAxes = false;
	if (chosen->sticks) {
		set_axis(mapping.axes[B_CONTROLLER_AXIS_LEFT_X].whole,
			axis_with_usage(shape, 0x30));
		set_axis(mapping.axes[B_CONTROLLER_AXIS_LEFT_Y].whole,
			axis_with_usage(shape, 0x31));
		int32 z = axis_with_usage(shape, 0x32);
		int32 rz = axis_with_usage(shape, 0x35);
		if (z >= 0 && rz >= 0) {
			set_axis(mapping.axes[B_CONTROLLER_AXIS_RIGHT_X].whole, z);
			set_axis(mapping.axes[B_CONTROLLER_AXIS_RIGHT_Y].whole, rz);
			triggerAxes = rx >= 0 && ry >= 0;
		} else {
			set_axis(mapping.axes[B_CONTROLLER_AXIS_RIGHT_X].whole, rx);
			set_axis(mapping.axes[B_CONTROLLER_AXIS_RIGHT_Y].whole, ry);
		}
	}
	if (triggerAxes) {
		set_axis(mapping.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER].whole, rx);
		set_axis(mapping.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER].whole, ry);
	} else {
		set_button(mapping.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER].whole,
			chosen->leftTrigger);
		set_button(mapping.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER].whole,
			chosen->rightTrigger);
	}

	fit_to_device(mapping, shape);
	return true;
}


bool
parse_pairs(const char* text, controller_mapping& mapping)
{
	memset(&mapping, 0, sizeof(mapping));
	if (text == NULL)
		return false;
	return apply_pairs(text, mapping) > 0;
}


static void
source_to_order(mapping_source& source, const int32* order, int32 count)
{
	if (source.type != SOURCE_AXIS)
		return;
	for (int32 i = 0; i < count; i++) {
		if (order[i] == source.index) {
			source.index = (uint8)i;
			return;
		}
	}
	memset(&source, 0, sizeof(source));
}


void
axes_to_order(controller_mapping& mapping, const int32* order, int32 count)
{
	for (int32 i = 0; i < B_CONTROLLER_BUTTON_COUNT; i++)
		source_to_order(mapping.buttons[i], order, count);
	for (int32 i = 0; i < B_CONTROLLER_AXIS_COUNT; i++) {
		source_to_order(mapping.axes[i].whole, order, count);
		source_to_order(mapping.axes[i].positive, order, count);
		source_to_order(mapping.axes[i].negative, order, count);
	}
}


static bool
append_pair(char* text, size_t size, size_t& length, const char* prefix,
	const char* key, const mapping_source& source)
{
	if (source.type == SOURCE_NONE)
		return true;
	char value[32];
	switch (source.type) {
		case SOURCE_BUTTON:
			snprintf(value, sizeof(value), "b%u", source.index);
			break;
		case SOURCE_HAT:
			snprintf(value, sizeof(value), "h%u.%u", source.index,
				source.hatMask);
			break;
		default:
			snprintf(value, sizeof(value), "%sa%u%s",
				source.half > 0 ? "+" : (source.half < 0 ? "-" : ""),
				source.index, source.invert ? "~" : "");
			break;
	}
	int written = snprintf(text + length, size - length, "%s%s:%s,", prefix,
		key, value);
	if (written < 0 || (size_t)written >= size - length) {
		text[length] = '\0';
		return false;
	}
	length += written;
	return true;
}


bool
format_pairs(const controller_mapping& mapping, char* text, size_t size)
{
	if (text == NULL || size == 0)
		return false;
	text[0] = '\0';
	size_t length = 0;
	for (int32 i = 0; i < B_CONTROLLER_BUTTON_COUNT; i++) {
		if (!append_pair(text, size, length, "", kButtonNames[i],
				mapping.buttons[i]))
			return false;
	}
	for (int32 i = 0; i < B_CONTROLLER_AXIS_COUNT; i++) {
		const axis_mapping& a = mapping.axes[i];
		if (!append_pair(text, size, length, "", kAxisNames[i], a.whole)
			|| !append_pair(text, size, length, "+", kAxisNames[i],
				a.positive)
			|| !append_pair(text, size, length, "-", kAxisNames[i],
				a.negative))
			return false;
	}
	return true;
}


}	// namespace R5GameController
