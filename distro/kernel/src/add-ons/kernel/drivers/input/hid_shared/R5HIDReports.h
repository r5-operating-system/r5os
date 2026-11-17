/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Report helpers of hid_shared for R5_JOYSTICK_GET_REPORT and the
 * PlayStation controllers over Bluetooth (docs/GAME-CONTROLLERS.md, option C
 * phase 1). No locks and no allocation, so scripts/fuzz-bt.sh (target hid)
 * builds them on the host; the drivers hold their own lock around them.
 *
 * Report layouts: facts read from SDL's hidapi drivers (SDL_hidapi_ps4.c and
 * SDL_hidapi_ps5.c, zlib, Copyright (C) 1997-2026 Sam Lantinga), not code.
 */
#ifndef R5_HID_REPORTS_H
#define R5_HID_REPORTS_H


#include <stddef.h>
#include <string.h>

#include <SupportDefs.h>


// #pragma mark - the last input report per report ID


#define R5_HID_CACHE_SLOTS		4
#define R5_HID_CACHE_REPORT		128
	// R5_JOYSTICK_CACHED_REPORT


struct r5_hid_cached_report {
	uint32		length;		// 0: empty
	uint8		id;
	bigtime_t	when;
	uint8		data[R5_HID_CACHE_REPORT];
};


struct r5_hid_report_cache {
	r5_hid_cached_report	slot[R5_HID_CACHE_SLOTS];
	uint32					next;
};


static inline void
r5_hid_cache_init(r5_hid_report_cache& cache)
{
	memset(&cache, 0, sizeof(cache));
}


/*!	Keeps the first R5_HID_CACHE_REPORT bytes of an input report, one slot
	per report ID; the oldest ID gives way when all slots are taken.
*/
static inline void
r5_hid_cache_store(r5_hid_report_cache& cache, const uint8* data,
	size_t length, bool usesReportIDs, bigtime_t when)
{
	if (data == NULL || length == 0)
		return;
	uint8 id = usesReportIDs ? data[0] : 0;

	r5_hid_cached_report* target = NULL;
	for (uint32 i = 0; i < R5_HID_CACHE_SLOTS; i++) {
		if (cache.slot[i].length != 0 && cache.slot[i].id == id) {
			target = &cache.slot[i];
			break;
		}
	}
	if (target == NULL) {
		target = &cache.slot[cache.next % R5_HID_CACHE_SLOTS];
		cache.next = (cache.next + 1) % R5_HID_CACHE_SLOTS;
	}

	size_t keep = length < R5_HID_CACHE_REPORT ? length : R5_HID_CACHE_REPORT;
	memcpy(target->data, data, keep);
	target->length = (uint32)keep;
	target->id = id;
	target->when = when;
}


/*!	Copies the last report with that ID (with the ID first when the device
	uses report IDs) into data. B_ENTRY_NOT_FOUND when none came yet.
*/
static inline status_t
r5_hid_cache_get(const r5_hid_report_cache& cache, uint8 id, uint8* data,
	size_t capacity, size_t* length, bigtime_t* when)
{
	for (uint32 i = 0; i < R5_HID_CACHE_SLOTS; i++) {
		const r5_hid_cached_report& entry = cache.slot[i];
		if (entry.length == 0 || entry.id != id)
			continue;
		size_t copy = entry.length < capacity ? entry.length : capacity;
		memcpy(data, entry.data, copy);
		*length = copy;
		*when = entry.when;
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}


// #pragma mark - PlayStation controllers over Bluetooth


#define R5_HID_VENDOR_SONY			0x054c


static inline bool
r5_hid_is_dualshock4(uint16 vendor, uint16 product)
{
	return vendor == R5_HID_VENDOR_SONY
		&& (product == 0x05c4 || product == 0x09cc || product == 0x0ba0);
}


static inline bool
r5_hid_is_dualsense(uint16 vendor, uint16 product)
{
	// The DualSense and the DualSense Edge.
	return vendor == R5_HID_VENDOR_SONY
		&& (product == 0x0ce6 || product == 0x0df2);
}


/*!	A DualShock 4 or DualSense over Bluetooth starts with its short report
	0x01, the one its report descriptor describes with buttons and axes.
	After its first output report (rumble, a trigger effect, or libr5game
	switching motion on) it sends only its full report (0x11 to 0x19 on the
	DualShock 4, 0x31 on the DualSense), which the descriptor describes as
	vendor bytes. So the joystick keeps working for every BJoystick program,
	this builds the short report from a full one.

	in is one report as it came (report ID first), out has room for
	capacity bytes; the result is the length of the short report (the ID
	and 9 bytes), or 0 when in is no full report of that controller.
	Layouts, in bytes after the report ID: the short report is the left
	stick X and Y, the right stick X and Y, three bytes of hat, buttons and
	counter, and the left and right trigger. The DualShock 4 full report has
	the same 9 bytes from its third byte; the DualSense full report from its
	second byte has the sticks, the triggers, a counter and then the button
	bytes.
*/
static inline size_t
r5_hid_sony_short_report(uint16 vendor, uint16 product, const uint8* in,
	size_t inLength, uint8* out, size_t capacity)
{
	if (in == NULL || out == NULL || capacity < 10 || inLength == 0)
		return 0;

	if (r5_hid_is_dualshock4(vendor, product)) {
		if (in[0] < 0x11 || in[0] > 0x19 || inLength < 3 + 9)
			return 0;
		out[0] = 0x01;
		memcpy(out + 1, in + 3, 9);
		return 10;
	}

	if (r5_hid_is_dualsense(vendor, product)) {
		if (in[0] != 0x31 || inLength < 2 + 10)
			return 0;
		const uint8* state = in + 2;
		out[0] = 0x01;
		out[1] = state[0];		// left stick X
		out[2] = state[1];		// left stick Y
		out[3] = state[2];		// right stick X
		out[4] = state[3];		// right stick Y
		out[5] = state[7];		// hat and face buttons
		out[6] = state[8];		// shoulders, triggers, Create, Options,
								// stick buttons
		out[7] = state[9];		// PS, touchpad, mute
		out[8] = state[4];		// left trigger
		out[9] = state[5];		// right trigger
		return 10;
	}

	return 0;
}


#endif	// R5_HID_REPORTS_H
