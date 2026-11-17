/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Report helpers of hid_shared for R5_JOYSTICK_GET_REPORT, the
 * PlayStation controllers over Bluetooth (docs/GAME-CONTROLLERS.md, option C
 * phase 1), and for phase 2 the Nintendo Switch controllers, the Xbox
 * controllers over Bluetooth and the Xbox controllers over USB (GIP). No
 * locks and no allocation, so scripts/fuzz-bt.sh (target hid) builds them
 * on the host; the drivers hold their own lock around them.
 *
 * Report layouts: facts read from SDL's hidapi drivers (SDL_hidapi_ps4.c,
 * SDL_hidapi_ps5.c, SDL_hidapi_switch.c and SDL_hidapi_xboxone.c, zlib,
 * Copyright (C) 1997-2026 Sam Lantinga; the Switch and Xbox ones read on
 * 17 Nov 2026 at commit 4cc7adcaee482b63b4e1a0de77f52e200aa87b90), not code.
 * The GIP packet header also follows Microsoft's description of the Gaming
 * Input Protocol over USB ([MS-GIPUSB]).
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


// #pragma mark - Nintendo Switch controllers (phase 2)


#define R5_HID_VENDOR_NINTENDO		0x057e
#define R5_HID_SWITCH_SIMPLE_ID		0x3f
#define R5_HID_SWITCH_SIMPLE_SIZE	12
	// the simple report 0x3F with its ID: two button bytes, the hat, and
	// four 16-bit stick axes
#define R5_HID_SWITCH_BT_OUTPUT		49
#define R5_HID_SWITCH_USB_OUTPUT	64
	// output reports with their ID, as SDL pads them per transport


static inline bool
r5_hid_is_switch_pro(uint16 vendor, uint16 product)
{
	return vendor == R5_HID_VENDOR_NINTENDO && product == 0x2009;
}


static inline bool
r5_hid_is_joycon(uint16 vendor, uint16 product)
{
	// The left and the right Joy-Con, each on its own.
	return vendor == R5_HID_VENDOR_NINTENDO
		&& (product == 0x2006 || product == 0x2007);
}


/*!	Output report 0x01: a subcommand with "no rumble" for both sides. The
	layout: the ID, a packet counter (0 to 15), four rumble bytes for the
	left and four for the right side, the subcommand and its arguments;
	zeros up to total bytes. 0 when it does not fit.
*/
static inline size_t
r5_hid_switch_subcommand(uint8 counter, uint8 command, const uint8* args,
	size_t argLength, uint8* out, size_t capacity, size_t total)
{
	if (out == NULL || total < 11 + argLength || capacity < total
		|| (argLength > 0 && args == NULL))
		return 0;
	static const uint8 kNeutral[4] = { 0x00, 0x01, 0x40, 0x40 };
	memset(out, 0, total);
	out[0] = 0x01;
	out[1] = counter & 0x0f;
	memcpy(out + 2, kNeutral, 4);
	memcpy(out + 6, kNeutral, 4);
	out[10] = command;
	if (argLength > 0)
		memcpy(out + 11, args, argLength);
	return total;
}


/*!	Output report 0x80, only over USB: 0x02 handshake, 0x03 high speed,
	0x04 USB only (no Bluetooth timeout).
*/
static inline size_t
r5_hid_switch_usb_command(uint8 command, uint8* out, size_t capacity,
	size_t total)
{
	if (out == NULL || total < 2 || capacity < total)
		return 0;
	memset(out, 0, total);
	out[0] = 0x80;
	out[1] = command;
	return total;
}


static inline uint16
r5_hid_switch_stick16(uint16 value12)
{
	// 12 bits to 16 bits, 0xfff to 0xffff.
	value12 &= 0x0fff;
	return (uint16)((value12 << 4) | (value12 >> 8));
}


/*!	The simple report 0x3F that a Switch Pro Controller sends over
	Bluetooth before it gets the full report mode, built from a full report
	(0x30, 0x31) or a subcommand reply (0x21), so the joystick keeps its
	buttons for every BJoystick program once libr5game or the driver
	switched the controller to full reports. Over USB the driver gives the
	controller an R5 report descriptor with the same 0x3F report
	(r5_hid_switch_usb_descriptor).

	Layouts after the report ID (SDL_hidapi_switch.c): full report: a timer,
	battery and connection, three button bytes (the right side: Y X B A SR
	SL R ZR; the shared one: - + RS LS Home Capture; the left side: down up
	right left SR SL L ZL), then two sticks of two 12-bit values each, Y up.
	Simple report: B A Y X L R ZL ZR, then - + LS RS Home Capture, the hat
	(0 up, clockwise, 8 centred), four 16-bit stick values, Y down.

	Returns R5_HID_SWITCH_SIMPLE_SIZE, or 0 when in is no such report.
*/
static inline size_t
r5_hid_switch_simple_report(const uint8* in, size_t inLength, uint8* out,
	size_t capacity)
{
	if (in == NULL || out == NULL || capacity < R5_HID_SWITCH_SIMPLE_SIZE
		|| inLength < 12)
		return 0;
	if (in[0] != 0x21 && in[0] != 0x30 && in[0] != 0x31)
		return 0;

	uint8 right = in[3];
	uint8 shared = in[4];
	uint8 left = in[5];

	memset(out, 0, R5_HID_SWITCH_SIMPLE_SIZE);
	out[0] = R5_HID_SWITCH_SIMPLE_ID;
	out[1] = ((right & 0x04) ? 0x01 : 0)	// B
		| ((right & 0x08) ? 0x02 : 0)		// A
		| ((right & 0x01) ? 0x04 : 0)		// Y
		| ((right & 0x02) ? 0x08 : 0)		// X
		| ((left & 0x40) ? 0x10 : 0)		// L
		| ((right & 0x40) ? 0x20 : 0)		// R
		| ((left & 0x80) ? 0x40 : 0)		// ZL
		| ((right & 0x80) ? 0x80 : 0);		// ZR
	out[2] = ((shared & 0x01) ? 0x01 : 0)	// minus
		| ((shared & 0x02) ? 0x02 : 0)		// plus
		| ((shared & 0x08) ? 0x04 : 0)		// left stick
		| ((shared & 0x04) ? 0x08 : 0)		// right stick
		| ((shared & 0x10) ? 0x10 : 0)		// Home
		| ((shared & 0x20) ? 0x20 : 0);		// Capture

	// The d-pad as a hat; opposite directions cancel.
	int x = ((left & 0x04) ? 1 : 0) - ((left & 0x08) ? 1 : 0);
	int y = ((left & 0x01) ? 1 : 0) - ((left & 0x02) ? 1 : 0);
	static const uint8 kHat[3][3] = {
		// x = -1, 0, 1
		{ 7, 0, 1 },	// y = -1 (up)
		{ 6, 8, 2 },	// y = 0
		{ 5, 4, 3 }		// y = 1 (down)
	};
	out[3] = kHat[y + 1][x + 1];

	const uint8* stick = in + 6;
	for (int i = 0; i < 2; i++) {
		const uint8* s = stick + 3 * i;
		uint16 sx = (uint16)(s[0] | ((s[1] & 0x0f) << 8));
		uint16 sy = (uint16)((s[1] >> 4) | (s[2] << 4));
		uint16 x16 = r5_hid_switch_stick16(sx);
		uint16 y16 = (uint16)(0xffff - r5_hid_switch_stick16(sy));
		out[4 + 4 * i] = (uint8)x16;
		out[5 + 4 * i] = (uint8)(x16 >> 8);
		out[6 + 4 * i] = (uint8)y16;
		out[7 + 4 * i] = (uint8)(y16 >> 8);
	}
	return R5_HID_SWITCH_SIMPLE_SIZE;
}


/*!	The battery of a full report or a subcommand reply, 0 to 100, or -1:
	the high nibble of the second byte is the level, 0 (empty) to 8 (full)
	in its upper three bits, its lowest bit says charging.
*/
static inline int32
r5_hid_switch_battery(const uint8* in, size_t inLength)
{
	if (in == NULL || inLength < 3)
		return -1;
	if (in[0] != 0x21 && in[0] != 0x30 && in[0] != 0x31)
		return -1;
	int32 level = (in[2] & 0xe0) >> 4;
	return level >= 8 ? 100 : level * 100 / 8;
}


/*!	What the driver sends a Switch controller over Bluetooth when it
	connects, one subcommand at a time, each after the reply (report 0x21
	with the subcommand in byte 14) to the one before: rumble on (0x48),
	the full report mode (0x03 0x30, Pro Controller only: it gives the
	battery and lets libr5game read the motion), and the first player light
	(0x30 0x01), so the lights stop flashing. A step without a reply after
	kSwitchInitWait reports is sent again, three times, then skipped.
*/
struct r5_hid_switch_init {
	uint8	step;
	uint8	steps;
	uint8	tries;
	uint8	counter;
	uint16	waited;
	bool	pro;
	bool	done;
};


static const uint16 kR5SwitchInitWait = 40;


static inline void
r5_hid_switch_init_command(const r5_hid_switch_init& state, uint8* command,
	uint8* argument)
{
	static const uint8 kPro[3][2]
		= { { 0x48, 0x01 }, { 0x03, 0x30 }, { 0x30, 0x01 } };
	static const uint8 kJoyCon[2][2] = { { 0x48, 0x01 }, { 0x30, 0x01 } };
	const uint8* step = state.pro ? kPro[state.step % 3]
		: kJoyCon[state.step % 2];
	*command = step[0];
	*argument = step[1];
}


static inline size_t
r5_hid_switch_init_send(r5_hid_switch_init& state, uint8* out,
	size_t capacity)
{
	uint8 command;
	uint8 argument;
	r5_hid_switch_init_command(state, &command, &argument);
	size_t length = r5_hid_switch_subcommand(state.counter, command,
		&argument, 1, out, capacity, R5_HID_SWITCH_BT_OUTPUT);
	state.counter = (state.counter + 1) & 0x0f;
	state.waited = 0;
	return length;
}


//!	Starts it; returns the first output report (or 0).
static inline size_t
r5_hid_switch_init_start(r5_hid_switch_init& state, bool pro, uint8* out,
	size_t capacity)
{
	memset(&state, 0, sizeof(state));
	state.pro = pro;
	state.steps = pro ? 3 : 2;
	return r5_hid_switch_init_send(state, out, capacity);
}


//!	One input report came; returns the next output report to send, or 0.
static inline size_t
r5_hid_switch_init_input(r5_hid_switch_init& state, const uint8* in,
	size_t inLength, uint8* out, size_t capacity)
{
	if (state.done || state.steps == 0)
		return 0;
	uint8 command;
	uint8 argument;
	r5_hid_switch_init_command(state, &command, &argument);

	bool replied = in != NULL && inLength >= 15 && in[0] == 0x21
		&& in[14] == command;
	if (!replied) {
		if (++state.waited < kR5SwitchInitWait)
			return 0;
		if (++state.tries < 3)
			return r5_hid_switch_init_send(state, out, capacity);
	}
	state.tries = 0;
	if (++state.step >= state.steps) {
		state.done = true;
		return 0;
	}
	return r5_hid_switch_init_send(state, out, capacity);
}


/*!	The report descriptor usb_hid gives a Switch Pro Controller over USB.
	Its own one describes the report 0x30 with items that do not match the
	bytes it sends, so R5 describes the simple report 0x3F the controller
	has over Bluetooth (16 buttons, a hat, four 16-bit axes X Y Rx Ry), which
	the driver builds from each full report, and the controller's vendor
	reports as plain bytes: inputs 0x21, 0x30 and 0x81, outputs 0x01, 0x10
	and 0x80, 63 bytes each, in a collection of their own.
*/
static const uint8 r5_hid_switch_usb_descriptor[] = {
	0x05, 0x01,			// Usage Page (Generic Desktop)
	0x09, 0x05,			// Usage (Gamepad)
	0xa1, 0x01,			// Collection (Application)
	0x85, 0x3f,			//   Report ID (0x3F)
	0x05, 0x09,			//   Usage Page (Button)
	0x19, 0x01,			//   Usage Minimum (1)
	0x29, 0x10,			//   Usage Maximum (16)
	0x15, 0x00,			//   Logical Minimum (0)
	0x25, 0x01,			//   Logical Maximum (1)
	0x75, 0x01,			//   Report Size (1)
	0x95, 0x10,			//   Report Count (16)
	0x81, 0x02,			//   Input (Data, Variable, Absolute)
	0x05, 0x01,			//   Usage Page (Generic Desktop)
	0x09, 0x39,			//   Usage (Hat Switch)
	0x15, 0x00,			//   Logical Minimum (0)
	0x25, 0x07,			//   Logical Maximum (7)
	0x75, 0x04,			//   Report Size (4)
	0x95, 0x01,			//   Report Count (1)
	0x81, 0x42,			//   Input (Data, Variable, Absolute, Null State)
	0x81, 0x01,			//   Input (Constant): 4 bits
	0x09, 0x30,			//   Usage (X)
	0x09, 0x31,			//   Usage (Y)
	0x09, 0x33,			//   Usage (Rx)
	0x09, 0x34,			//   Usage (Ry)
	0x15, 0x00,			//   Logical Minimum (0)
	0x27, 0xff, 0xff, 0x00, 0x00,	// Logical Maximum (65535)
	0x75, 0x10,			//   Report Size (16)
	0x95, 0x04,			//   Report Count (4)
	0x81, 0x02,			//   Input (Data, Variable, Absolute)
	0xc0,				// End Collection
	0x06, 0x00, 0xff,	// Usage Page (Vendor 0xFF00)
	0x09, 0x01,			// Usage (1)
	0xa1, 0x01,			// Collection (Application)
	0x15, 0x00,			//   Logical Minimum (0)
	0x26, 0xff, 0x00,	//   Logical Maximum (255)
	0x75, 0x08,			//   Report Size (8)
	0x95, 0x3f,			//   Report Count (63)
	0x85, 0x21, 0x09, 0x21, 0x81, 0x02,	// Input 0x21
	0x85, 0x30, 0x09, 0x30, 0x81, 0x02,	// Input 0x30
	0x85, 0x81, 0x09, 0x81, 0x81, 0x02,	// Input 0x81
	0x85, 0x01, 0x09, 0x01, 0x91, 0x02,	// Output 0x01
	0x85, 0x10, 0x09, 0x10, 0x91, 0x02,	// Output 0x10
	0x85, 0x80, 0x09, 0x80, 0x91, 0x02,	// Output 0x80
	0xc0				// End Collection
};


// #pragma mark - Xbox controllers over Bluetooth (phase 2)


#define R5_HID_VENDOR_MICROSOFT		0x045e


/*!	The Xbox Wireless Controllers that speak HID over Bluetooth, Classic
	and Low Energy (SDL_IsJoystickBluetoothXboxOne()).
*/
static inline bool
r5_hid_is_xbox_bluetooth(uint16 vendor, uint16 product)
{
	if (vendor != R5_HID_VENDOR_MICROSOFT)
		return false;
	switch (product) {
		case 0x02e0:	// Xbox One S, first firmware
		case 0x02fd:	// Xbox One S
		case 0x0b05:	// Elite Series 2
		case 0x0b0c:	// Adaptive Controller
		case 0x0b13:	// Xbox Series X|S, Low Energy
		case 0x0b20:	// Xbox One S, Low Energy
		case 0x0b21:	// Adaptive Controller, Low Energy
		case 0x0b22:	// Elite Series 2, Low Energy
			return true;
	}
	return false;
}


/*!	The battery of an Xbox controller's report 0x04 over Bluetooth: the
	low two bits of its flags are the level, which Microsoft's GameInput
	maps to 10, 40, 70 and 100 percent. -1 for any other report.
*/
static inline int32
r5_hid_xbox_battery(const uint8* in, size_t inLength)
{
	if (in == NULL || inLength < 2 || in[0] != 0x04)
		return -1;
	static const int32 kLevel[4] = { 10, 40, 70, 100 };
	return kLevel[in[1] & 0x03];
}


// #pragma mark - Xbox controllers over USB: GIP (phase 2)


/*!	An Xbox One or Series controller over USB is no HID device: its first
	interface (class 0xFF, subclass 0x47, protocol 0xD0) speaks the Gaming
	Input Protocol. usb_hid gives it the report descriptor below and turns
	its packets into that report, so it is a joystick like the others.

	A packet: command, options (0x10 wants an acknowledgement, 0x20 a
	system message, 0x40 the first chunk, 0x80 a chunk; the low nibble an
	accessory), sequence, the payload length as a variable-length integer
	(seven bits a byte, the high bit for "more"), padded so the header has
	an even length, then the chunk offset of a chunked packet.
*/
#define R5_HID_GIP_CLASS			0xff
#define R5_HID_GIP_SUBCLASS			0x47
#define R5_HID_GIP_PROTOCOL			0xd0

#define R5_HID_GIP_INPUT_ID			0x20
#define R5_HID_GIP_INPUT_SIZE		16
	// the R5 report with its ID: buttons, hat, four sticks, two triggers
#define R5_HID_GIP_RUMBLE_SIZE		13
#define R5_HID_GIP_MAX_SEND			16
#define R5_HID_GIP_MAX_SENDS		4


struct r5_hid_gip_header {
	uint8	command;
	uint8	options;
	uint8	sequence;
	uint32	length;
	uint32	chunkOffset;
	uint32	headerLength;
};


struct r5_hid_gip_sends {
	uint32	count;
	uint8	length[R5_HID_GIP_MAX_SENDS];
	uint8	data[R5_HID_GIP_MAX_SENDS][R5_HID_GIP_MAX_SEND];
};


struct r5_hid_gip_state {
	uint16	vendor;
	uint16	product;
	uint8	sequence;
	bool	guide;
	bool	started;
		// the first input came (SDL drops it, it may be stale)
	bool	haveLast;
	uint8	last[R5_HID_GIP_INPUT_SIZE];
};


static inline void
r5_hid_gip_init(r5_hid_gip_state& state, uint16 vendor, uint16 product)
{
	memset(&state, 0, sizeof(state));
	state.vendor = vendor;
	state.product = product;
}


static inline uint8
r5_hid_gip_next_sequence(r5_hid_gip_state& state)
{
	if (++state.sequence == 0)
		state.sequence = 1;
	return state.sequence;
}


//!	false when the header does not fit in length bytes.
static inline bool
r5_hid_gip_decode(const uint8* data, size_t length, r5_hid_gip_header& header)
{
	memset(&header, 0, sizeof(header));
	if (data == NULL || length < 4)
		return false;
	header.command = data[0];
	header.options = data[1];
	header.sequence = data[2];
	size_t offset = 3;
	for (int i = 0; i < 4; i++) {
		if (offset >= length)
			return false;
		uint8 byte = data[offset++];
		header.length |= (uint32)(byte & 0x7f) << (7 * i);
		if ((byte & 0x80) == 0)
			break;
		if (i == 3)
			return false;
	}
	if ((header.options & 0x80) != 0) {
		for (int i = 0; i < 4; i++) {
			if (offset >= length)
				return false;
			uint8 byte = data[offset++];
			header.chunkOffset |= (uint32)(byte & 0x7f) << (7 * i);
			if ((byte & 0x80) == 0)
				break;
			if (i == 3)
				return false;
		}
	}
	header.headerLength = (uint32)offset;
	return true;
}


static inline void
r5_hid_gip_add(r5_hid_gip_sends& sends, const uint8* data, size_t length)
{
	if (sends.count >= R5_HID_GIP_MAX_SENDS || length > R5_HID_GIP_MAX_SEND)
		return;
	memcpy(sends.data[sends.count], data, length);
	sends.length[sends.count] = (uint8)length;
	sends.count++;
}


/*!	The start: power on, the guide button light on, "security passed", and
	the motors set up (SDL's xboxone_init_packets, without the PowerA
	extras).
*/
static inline void
r5_hid_gip_start(r5_hid_gip_state& state, r5_hid_gip_sends& sends)
{
	uint8 power[] = { 0x05, 0x20, 0x00, 0x01, 0x00 };
	uint8 light[] = { 0x0a, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14 };
	uint8 security[] = { 0x06, 0x20, 0x00, 0x02, 0x01, 0x00 };
	uint8 motors[] = { 0x09, 0x00, 0x00, 0x09, 0x00, 0x0f, 0x00, 0x00,
		0x00, 0x00, 0xff, 0x00, 0xeb };
	power[2] = r5_hid_gip_next_sequence(state);
	r5_hid_gip_add(sends, power, sizeof(power));
	light[2] = r5_hid_gip_next_sequence(state);
	r5_hid_gip_add(sends, light, sizeof(light));
	security[2] = r5_hid_gip_next_sequence(state);
	r5_hid_gip_add(sends, security, sizeof(security));
	motors[2] = r5_hid_gip_next_sequence(state);
	r5_hid_gip_add(sends, motors, sizeof(motors));
}


/*!	The acknowledgement of a packet whose options ask for one: command
	0x01, a system message, the packet's sequence, nine bytes: 0, the
	command, the system flag, the bytes received so far (16 bits), two
	padding bytes and the bytes still to come (16 bits, 0).
*/
static inline void
r5_hid_gip_ack(const r5_hid_gip_header& header, r5_hid_gip_sends& sends)
{
	uint32 received = header.chunkOffset + header.length;
	uint8 ack[] = { 0x01, 0x20, header.sequence, 0x09,
		0x00, header.command, 0x20, (uint8)received, (uint8)(received >> 8),
		0x00, 0x00, 0x00, 0x00 };
	r5_hid_gip_add(sends, ack, sizeof(ack));
}


static inline bool
r5_hid_gip_has_share(uint16 vendor, uint16 product)
{
	// The Xbox Series X|S controller (SDL: ControllerHasShareButton).
	return vendor == R5_HID_VENDOR_MICROSOFT && product == 0x0b12;
}


static inline void
r5_hid_gip_put16(uint8* out, uint16 value)
{
	out[0] = (uint8)value;
	out[1] = (uint8)(value >> 8);
}


/*!	One packet from the controller. What it asks for (an acknowledgement,
	the start after its announcement) goes into sends; the result is the
	length of the R5 report written to out (R5_HID_GIP_INPUT_SIZE), or 0
	when the packet has nothing for the joystick.

	The input packet 0x20 after its header (SDL_hidapi_xboxone.c): Menu
	0x04, View 0x08, A 0x10, B 0x20, X 0x40, Y 0x80; up 0x01, down 0x02,
	left 0x04, right 0x08, LB 0x10, RB 0x20, LS 0x40, RS 0x80; the
	triggers, 0 to 1023; the sticks, signed 16 bits, Y up. The Series
	controller's Share button is bit 0 of byte 14 (byte 18 in a 44-byte
	input). The guide button comes alone, system message 0x07, bit 0 of its
	first byte.

	The R5 report (r5_hid_gip_descriptor) uses the order of libr5game's
	fixed Xbox layout (the W3C standard gamepad): A B X Y LB RB, the two
	triggers as buttons past a third, View Menu LS RS Guide Share; the hat;
	X Y Z Rz for the sticks, Y down; Rx and Ry for the triggers.
*/
static inline size_t
r5_hid_gip_input(r5_hid_gip_state& state, const uint8* data, size_t length,
	uint8* out, size_t capacity, r5_hid_gip_sends& sends)
{
	sends.count = 0;
	r5_hid_gip_header header;
	if (!r5_hid_gip_decode(data, length, header))
		return 0;
	if ((header.options & 0x10) != 0)
		r5_hid_gip_ack(header, sends);
	if ((header.options & 0x0f) != 0)
		return 0;
		// an accessory plugged into the controller

	const uint8* payload = data + header.headerLength;
	size_t available = length - header.headerLength;
	size_t size = header.length < available ? header.length : available;
	bool system = (header.options & 0x20) != 0;

	if (system && header.command == 0x02) {
		// Announced: it waits to be started.
		r5_hid_gip_start(state, sends);
		state.started = false;
		return 0;
	}
	if (out == NULL || capacity < R5_HID_GIP_INPUT_SIZE)
		return 0;

	if (system && header.command == 0x07) {
		if (size < 1)
			return 0;
		state.guide = (payload[0] & 0x01) != 0;
		if (!state.haveLast)
			return 0;
		memcpy(out, state.last, R5_HID_GIP_INPUT_SIZE);
		if (state.guide)
			out[2] |= 0x10;
		else
			out[2] &= ~0x10;
		memcpy(state.last, out, R5_HID_GIP_INPUT_SIZE);
		return R5_HID_GIP_INPUT_SIZE;
	}

	if (system || header.command != 0x20 || (header.options & 0x80) != 0
		|| size < 14)
		return 0;
	if (!state.started) {
		state.started = true;
		return 0;
	}

	uint8 b0 = payload[0];
	uint8 b1 = payload[1];
	uint16 buttons = ((b0 & 0x10) ? 0x0001 : 0)	// A
		| ((b0 & 0x20) ? 0x0002 : 0)				// B
		| ((b0 & 0x40) ? 0x0004 : 0)				// X
		| ((b0 & 0x80) ? 0x0008 : 0)				// Y
		| ((b1 & 0x10) ? 0x0010 : 0)				// LB
		| ((b1 & 0x20) ? 0x0020 : 0)				// RB
		| ((b0 & 0x08) ? 0x0100 : 0)				// View
		| ((b0 & 0x04) ? 0x0200 : 0)				// Menu
		| ((b1 & 0x40) ? 0x0400 : 0)				// LS
		| ((b1 & 0x80) ? 0x0800 : 0)				// RS
		| (state.guide ? 0x1000 : 0);
	uint16 leftTrigger = (uint16)((payload[2] | (payload[3] << 8)) & 0x3ff);
	uint16 rightTrigger = (uint16)((payload[4] | (payload[5] << 8)) & 0x3ff);
	if (leftTrigger > 128)
		buttons |= 0x0040;
	if (rightTrigger > 128)
		buttons |= 0x0080;
	if (r5_hid_gip_has_share(state.vendor, state.product)) {
		// Firmware 5.0 and 5.1: 32 and 40 bytes; 5.5: 44 bytes.
		if ((size > 14 && size < 44 && (payload[14] & 0x01) != 0)
			|| (size == 44 && (payload[18] & 0x01) != 0))
			buttons |= 0x2000;
	}

	int x = ((b1 & 0x08) ? 1 : 0) - ((b1 & 0x04) ? 1 : 0);
	int y = ((b1 & 0x02) ? 1 : 0) - ((b1 & 0x01) ? 1 : 0);
	static const uint8 kHat[3][3] = {
		{ 7, 0, 1 },
		{ 6, 8, 2 },
		{ 5, 4, 3 }
	};

	memset(out, 0, R5_HID_GIP_INPUT_SIZE);
	out[0] = R5_HID_GIP_INPUT_ID;
	r5_hid_gip_put16(out + 1, buttons);
	out[3] = kHat[y + 1][x + 1];
	// X, Y (down), Z, Rz (down): Y flipped as ~value, as SDL does.
	r5_hid_gip_put16(out + 4, (uint16)(payload[6] | (payload[7] << 8)));
	r5_hid_gip_put16(out + 6, (uint16)~(payload[8] | (payload[9] << 8)));
	r5_hid_gip_put16(out + 8, (uint16)(payload[10] | (payload[11] << 8)));
	r5_hid_gip_put16(out + 10,
		(uint16)~(payload[12] | (payload[13] << 8)));
	r5_hid_gip_put16(out + 12, leftTrigger);
	r5_hid_gip_put16(out + 14, rightTrigger);
	memcpy(state.last, out, R5_HID_GIP_INPUT_SIZE);
	state.haveLast = true;
	return R5_HID_GIP_INPUT_SIZE;
}


/*!	The report descriptor usb_hid gives a GIP controller: the R5 input
	report 0x20 above, the rumble packet as output 0x09 (12 bytes after the
	command: options, sequence, length, then SDL's motor bytes), and an
	input 0xFE of 63 bytes that no packet uses, so the transfers are 64
	bytes, the size of a GIP packet.
*/
static const uint8 r5_hid_gip_descriptor[] = {
	0x05, 0x01,			// Usage Page (Generic Desktop)
	0x09, 0x05,			// Usage (Gamepad)
	0xa1, 0x01,			// Collection (Application)
	0x85, 0x20,			//   Report ID (0x20)
	0x05, 0x09,			//   Usage Page (Button)
	0x19, 0x01,			//   Usage Minimum (1)
	0x29, 0x0e,			//   Usage Maximum (14)
	0x15, 0x00,			//   Logical Minimum (0)
	0x25, 0x01,			//   Logical Maximum (1)
	0x75, 0x01,			//   Report Size (1)
	0x95, 0x0e,			//   Report Count (14)
	0x81, 0x02,			//   Input (Data, Variable, Absolute)
	0x95, 0x02,			//   Report Count (2)
	0x81, 0x01,			//   Input (Constant)
	0x05, 0x01,			//   Usage Page (Generic Desktop)
	0x09, 0x39,			//   Usage (Hat Switch)
	0x25, 0x07,			//   Logical Maximum (7)
	0x75, 0x04,			//   Report Size (4)
	0x95, 0x01,			//   Report Count (1)
	0x81, 0x42,			//   Input (Data, Variable, Absolute, Null State)
	0x81, 0x01,			//   Input (Constant): 4 bits
	0x09, 0x30,			//   Usage (X)
	0x09, 0x31,			//   Usage (Y)
	0x09, 0x32,			//   Usage (Z)
	0x09, 0x35,			//   Usage (Rz)
	0x16, 0x00, 0x80,	//   Logical Minimum (-32768)
	0x26, 0xff, 0x7f,	//   Logical Maximum (32767)
	0x75, 0x10,			//   Report Size (16)
	0x95, 0x04,			//   Report Count (4)
	0x81, 0x02,			//   Input (Data, Variable, Absolute)
	0x09, 0x33,			//   Usage (Rx)
	0x09, 0x34,			//   Usage (Ry)
	0x15, 0x00,			//   Logical Minimum (0)
	0x26, 0xff, 0x03,	//   Logical Maximum (1023)
	0x95, 0x02,			//   Report Count (2)
	0x81, 0x02,			//   Input (Data, Variable, Absolute)
	0xc0,				// End Collection
	0x06, 0x00, 0xff,	// Usage Page (Vendor 0xFF00)
	0x09, 0x01,			// Usage (1)
	0xa1, 0x01,			// Collection (Application)
	0x15, 0x00,			//   Logical Minimum (0)
	0x26, 0xff, 0x00,	//   Logical Maximum (255)
	0x75, 0x08,			//   Report Size (8)
	0x85, 0x09,			//   Report ID (0x09)
	0x09, 0x09,			//   Usage (9)
	0x95, 0x0c,			//   Report Count (12)
	0x91, 0x02,			//   Output (Data, Variable, Absolute)
	0x85, 0xfe,			//   Report ID (0xFE)
	0x09, 0xfe,			//   Usage (0xFE)
	0x95, 0x3f,			//   Report Count (63)
	0x81, 0x02,			//   Input (Data, Variable, Absolute)
	0xc0				// End Collection
};


#endif	// R5_HID_REPORTS_H
