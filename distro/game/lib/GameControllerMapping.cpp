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


static void
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
		return;

	if (targetHalf == 0) {
		for (int32 i = 0; i < B_CONTROLLER_BUTTON_COUNT; i++) {
			if (target_is(key, keyLength, kButtonNames[i])) {
				mapping.buttons[i] = source;
				return;
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
		return;
	}
	// Other keys (crc, hint, sdk, misc2 and later) are not used.
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

	const char* item = comma + 1;
	while (*item != '\0' && *item != '\n' && *item != '\r') {
		const char* end = item;
		while (*end != ',' && *end != '\0' && *end != '\n' && *end != '\r')
			end++;
		const char* colon = (const char*)memchr(item, ':', end - item);
		if (colon != NULL) {
			apply_pair(item, colon - item, colon + 1, end - colon - 1,
				mapping);
		}
		if (*end != ',')
			break;
		item = end + 1;
	}
	return true;
}


status_t
find_mapping(const char* path, uint16 vendor, uint16 product,
	controller_mapping& mapping)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;

	// The longest lines of the database are a few hundred bytes.
	char line[2048];
	bool skipping = false;
	status_t status = B_ENTRY_NOT_FOUND;
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
		if (parse_line(line, vendor, product, mapping)) {
			status = B_OK;
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
			if (find_mapping(path.Path(), vendor, product, mapping) == B_OK)
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


}	// namespace R5GameController
