/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * gamecontroller_test: libr5game.so without a real controller
 * (docs/GAME-CONTROLLERS.md).
 *
 *  1. the mapping parser, the axis order and the hat directions; and what
 *     the HID descriptor decides first: the line for the same bus, the
 *     buttons by usage, the d-pad on the hat or on X and Y, the fixed
 *     layouts and a saved setup's text;
 *  2. the installed SDL_GameControllerDB: the DualSense and the 8BitDo M30
 *     are found, and every Windows vendor and product entry parses;
 *  3. as uid 0, with the bluetooth_hid driver (regular image): a virtual
 *     controller with the DualSense's IDs and report layout, fed the way
 *     bluetooth_server feeds a real one, read through BGameController
 *     (R5_JOYSTICK_GET_INFO, the mapping, the standard layout), and the
 *     B_GAME_CONTROLLER_ADDED and _REMOVED messages; two synthetic pads
 *     with the 8BitDo M30's IDs (2dc8:0651), one with a hat and one with
 *     the d-pad on X and Y; a saved setup and a chosen layout;
 *  4. with app_server: the four PlayStation glyphs and the five shapes of
 *     BGameControllerView draw something.
 *
 * "gamecontroller_test --hold <seconds>" only keeps that virtual
 * controller, with Cross down, L2 at 60 % and the left stick up and left,
 * so the Settings page can be looked at in QEMU.
 *
 * Prints "gamecontroller_test: ok" or the first failure; exit code 0 or 1.
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Application.h>
#include <Bitmap.h>
#include <FindDirectory.h>
#include <Looper.h>
#include <OS.h>
#include <Path.h>
#include <String.h>
#include <View.h>

#include <GameController.h>
#include <GameControllerView.h>
#include <R5BluetoothHID.h>
#include <R5Icons.h>

#include "GameControllerMapping.h"


using namespace R5GameController;


static int
fail(const char* what)
{
	printf("gamecontroller_test: FAIL %s\n", what);
	return 1;
}


#define CHECK(condition, what) \
	do { if (!(condition)) return fail(what); } while (false)


// #pragma mark - 1. the parser


static const char* kDualSenseLine
	= "030000004c050000e60c000000000000,PS5 Controller,a:b1,b:b2,back:b8,"
	"dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,"
	"leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,"
	"misc1:b14,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,"
	"righty:a5,start:b9,touchpad:b13,x:b0,y:b3,platform:Windows,\n";


static int
test_parser()
{
	uint16 vendor = 0;
	uint16 product = 0;
	CHECK(parse_guid("030000004c050000e60c000000000000", &vendor, &product),
		"guid of the DualSense");
	CHECK(vendor == 0x054c && product == 0x0ce6, "vendor and product");
	CHECK(!parse_guid("78696e70757401000000000000000000", NULL, NULL),
		"xinput guid taken as vendor and product");
	CHECK(!parse_guid("030000004c05", NULL, NULL), "short guid accepted");

	controller_mapping m;
	CHECK(parse_line(kDualSenseLine, 0x054c, 0x0ce6, m), "DualSense line");
	CHECK(strcmp(m.name, "PS5 Controller") == 0, "name");
	CHECK(m.buttons[B_CONTROLLER_BUTTON_A].type == SOURCE_BUTTON
		&& m.buttons[B_CONTROLLER_BUTTON_A].index == 1, "a:b1");
	CHECK(m.buttons[B_CONTROLLER_BUTTON_DPAD_UP].type == SOURCE_HAT
		&& m.buttons[B_CONTROLLER_BUTTON_DPAD_UP].hatMask == 1, "dpup:h0.1");
	CHECK(m.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER].whole.type == SOURCE_AXIS
		&& m.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER].whole.index == 3,
		"lefttrigger:a3");
	CHECK(!mapping_has_button(m, B_CONTROLLER_BUTTON_PADDLE1),
		"paddle1 not in the line");
	CHECK(mapping_has_axis(m, B_CONTROLLER_AXIS_RIGHT_Y), "righty");

	CHECK(!parse_line(kDualSenseLine, 0x054c, 0x05c4, m), "other product");
	CHECK(!parse_line("# comment", 0x054c, 0x0ce6, m), "comment");
	CHECK(!parse_line("030000004c050000e60c000011010000,PS5 Controller,"
		"a:b1,platform:Linux,", 0x054c, 0x0ce6, m), "Linux line taken");

	// Half axes, inverted axes and half targets.
	CHECK(parse_line("03000000c82d00005106000000000000,8BitDo M30,a:b0,"
		"dpdown:+a2,dpup:-a2,-leftx:b5,+leftx:b6,lefty:a1~,"
		"platform:Windows,", 0x2dc8, 0x0651, m), "half axes line");
	CHECK(m.buttons[B_CONTROLLER_BUTTON_DPAD_DOWN].half == 1
		&& m.buttons[B_CONTROLLER_BUTTON_DPAD_UP].half == -1, "+a2 / -a2");
	CHECK(m.axes[B_CONTROLLER_AXIS_LEFT_X].negative.type == SOURCE_BUTTON
		&& m.axes[B_CONTROLLER_AXIS_LEFT_X].positive.index == 6,
		"-leftx / +leftx");
	CHECK(m.axes[B_CONTROLLER_AXIS_LEFT_Y].whole.invert, "a1~");

	raw_state raw;
	memset(&raw, 0, sizeof(raw));
	raw.axisCount = 3;
	raw.axes[1] = 1000;
	raw.axes[2] = 30000;
	raw.buttonCount = 8;
	raw.buttons[5] = true;
	mapped_state out;
	map_state(m, raw, out);
	CHECK((out.buttons & (1 << B_CONTROLLER_BUTTON_DPAD_DOWN)) != 0,
		"+a2 pressed");
	CHECK((out.buttons & (1 << B_CONTROLLER_BUTTON_DPAD_UP)) == 0,
		"-a2 not pressed");
	CHECK(out.axes[B_CONTROLLER_AXIS_LEFT_X] == -32767, "-leftx from b5");
	CHECK(out.axes[B_CONTROLLER_AXIS_LEFT_Y] == -1001, "inverted a1");

	// The DualSense's report order: X Y Z Rz, then Rx Ry. DirectInput
	// counts X Y Z Rx Ry Rz.
	uint16 usages[6] = { 0x30, 0x31, 0x32, 0x35, 0x33, 0x34 };
	int32 order[6];
	axis_order(usages, 6, order);
	CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 4
		&& order[4] == 5 && order[5] == 3, "axis order");

	CHECK(hat_mask(0) == 0 && hat_mask(1) == 1 && hat_mask(4) == 6
		&& hat_mask(5) == 4 && hat_mask(8) == 9 && hat_mask(9) == 0,
		"hat directions");

	// Triggers run from 0 to 32767 over the whole axis.
	CHECK(parse_line(kDualSenseLine, 0x054c, 0x0ce6, m), "DualSense again");
	memset(&raw, 0, sizeof(raw));
	raw.axisCount = 6;
	raw.axes[3] = -32768;
	raw.axes[4] = 32767;
	raw.hatCount = 1;
	raw.hats[0] = 5;
	raw.buttonCount = 14;
	raw.buttons[1] = true;
	map_state(m, raw, out);
	CHECK(out.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER] == 0, "L2 at rest");
	CHECK(out.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER] == 32767, "R2 down");
	CHECK(out.buttons == ((1 << B_CONTROLLER_BUTTON_A)
		| (1 << B_CONTROLLER_BUTTON_DPAD_DOWN)), "cross and down only");

	CHECK(family_for(0x054c, 0x0ce6, "PS5 Controller", true)
		== B_GAME_CONTROLLER_FAMILY_PLAYSTATION, "PlayStation family");
	CHECK(family_for(0x2dc8, 0x5101, "8BitDo M30", true)
		== B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS, "M30 family");
	CHECK(family_for(0x057e, 0x2009, "Switch Pro Controller", true)
		== B_GAME_CONTROLLER_FAMILY_NINTENDO, "Nintendo family");
	CHECK(family_for(0x045e, 0x028e, "Xbox 360 Controller", true)
		== B_GAME_CONTROLLER_FAMILY_XBOX, "Xbox family");
	CHECK(family_for(0x054c, 0x0ce6, "PS5 Controller", false)
		== B_GAME_CONTROLLER_FAMILY_UNKNOWN, "no mapping");
	printf("gamecontroller_test: parser ok\n");
	return 0;
}


// #pragma mark - 1b. what the HID descriptor says first


static bool
same_mapping(const controller_mapping& a, const controller_mapping& b)
{
	return memcmp(a.buttons, b.buttons, sizeof(a.buttons)) == 0
		&& memcmp(a.axes, b.axes, sizeof(a.axes)) == 0;
}


static bool
is_half(const mapping_source& source, uint8 index, int8 half)
{
	return source.type == SOURCE_AXIS && source.index == index
		&& source.half == half;
}


// The 8BitDo M30's Windows line for 2dc8:0651 (a USB entry: no Bluetooth
// line exists), the one the 17 Nov boot on the 5625U used.
static const char* kM30Line
	= "03000000c82d00005106000000000000,8BitDo M30,a:b0,b:b1,back:b10,"
	"dpdown:+a2,dpleft:-a0,dpright:+a0,dpup:-a2,guide:b2,leftshoulder:b8,"
	"lefttrigger:b9,rightshoulder:b6,righttrigger:b7,start:b11,x:b3,y:b4,"
	"platform:Windows,\n";


static int
test_descriptor_first()
{
	CHECK(guid_bus("030000004c050000e60c000000000000") == 0x03, "USB bus");
	CHECK(guid_bus("050000004c050000e60c000000000000") == 0x05,
		"Bluetooth bus");

	// A line for the same bus comes first.
	BPath path;
	CHECK(find_directory(B_SYSTEM_TEMP_DIRECTORY, &path) == B_OK, "temp");
	path.Append("gamecontroller_test.txt");
	FILE* file = fopen(path.Path(), "w");
	CHECK(file != NULL, "temp file");
	fputs("03000000341200007856000000000000,Cable,a:b0,platform:Windows,\n"
		"05000000341200007856000000000000,Radio,a:b5,platform:Windows,\n",
		file);
	fclose(file);
	controller_mapping m;
	CHECK(find_mapping(path.Path(), 0x1234, 0x5678, 0x05, m) == B_OK
		&& m.buttons[B_CONTROLLER_BUTTON_A].index == 5, "Bluetooth line");
	CHECK(find_mapping(path.Path(), 0x1234, 0x5678, 0x03, m) == B_OK
		&& m.buttons[B_CONTROLLER_BUTTON_A].index == 0, "USB line");
	CHECK(find_mapping(path.Path(), 0x1234, 0x5678, 0x18, m) == B_OK
		&& strcmp(m.name, "Cable") == 0, "any bus: the first line");
	unlink(path.Path());

	// Button usages 1 to 4 and 9 to 12: DirectInput's b4 is usage 9, the
	// raw button 8.
	device_shape shape;
	memset(&shape, 0, sizeof(shape));
	shape.buttonCount = 12;
	shape.knowsButtons = true;
	shape.buttonUsages[0] = 0x00f | 0xf00;
	CHECK(parse_pairs("a:b0,b:b4,x:b7,y:b8,", m), "pairs");
	buttons_by_usage(m, shape);
	CHECK(m.buttons[B_CONTROLLER_BUTTON_A].index == 0
		&& m.buttons[B_CONTROLLER_BUTTON_B].index == 8
		&& m.buttons[B_CONTROLLER_BUTTON_X].index == 11
		&& m.buttons[B_CONTROLLER_BUTTON_Y].type == SOURCE_NONE,
		"buttons by usage");

	// The M30 line on a controller with X, Y and a hat: the d-pad is the
	// hat, not Z (which it does not have).
	memset(&shape, 0, sizeof(shape));
	shape.axisCount = 2;
	shape.axisUsage[0] = 0x30;
	shape.axisUsage[1] = 0x31;
	shape.hatCount = 1;
	shape.buttonCount = 16;
	CHECK(parse_line(kM30Line, 0x2dc8, 0x0651, m), "M30 line");
	fit_to_device(m, shape);
	CHECK(m.buttons[B_CONTROLLER_BUTTON_DPAD_UP].type == SOURCE_HAT
		&& m.buttons[B_CONTROLLER_BUTTON_DPAD_UP].hatMask == 1
		&& m.buttons[B_CONTROLLER_BUTTON_DPAD_LEFT].hatMask == 8,
		"d-pad on the hat");
	CHECK(m.buttons[B_CONTROLLER_BUTTON_A].index == 0
		&& m.buttons[B_CONTROLLER_BUTTON_START].index == 11, "M30 buttons");

	// The same without a hat: the d-pad is X and Y.
	shape.hatCount = 0;
	CHECK(parse_line(kM30Line, 0x2dc8, 0x0651, m), "M30 line again");
	fit_to_device(m, shape);
	CHECK(is_half(m.buttons[B_CONTROLLER_BUTTON_DPAD_LEFT], 0, -1)
		&& is_half(m.buttons[B_CONTROLLER_BUTTON_DPAD_RIGHT], 0, 1)
		&& is_half(m.buttons[B_CONTROLLER_BUTTON_DPAD_UP], 1, -1)
		&& is_half(m.buttons[B_CONTROLLER_BUTTON_DPAD_DOWN], 1, 1),
		"d-pad on X and Y");
	raw_state raw;
	memset(&raw, 0, sizeof(raw));
	raw.axisCount = 2;
	raw.axes[0] = -32768;
	raw.axes[1] = 32767;
	raw.buttonCount = 16;
	mapped_state out;
	map_state(m, raw, out);
	CHECK(out.buttons == ((1 << B_CONTROLLER_BUTTON_DPAD_LEFT)
		| (1 << B_CONTROLLER_BUTTON_DPAD_DOWN)), "left and down");

	// X and Y read by a stick are not taken for the d-pad.
	CHECK(parse_pairs("leftx:a0,lefty:a1,dpup:h0.1,dpdown:h0.4,", m),
		"stick pairs");
	fit_to_device(m, shape);
	CHECK(m.buttons[B_CONTROLLER_BUTTON_DPAD_UP].type == SOURCE_NONE
		&& m.axes[B_CONTROLLER_AXIS_LEFT_X].whole.type == SOURCE_AXIS,
		"stick axes kept, no d-pad");

	// A d-pad on buttons stays.
	CHECK(parse_pairs("dpup:b11,dpdown:b12,", m), "button d-pad");
	shape.hatCount = 1;
	fit_to_device(m, shape);
	CHECK(m.buttons[B_CONTROLLER_BUTTON_DPAD_UP].type == SOURCE_BUTTON,
		"button d-pad kept");

	// Fixed layouts. Sega Genesis on the M30's shape without a hat.
	shape.hatCount = 0;
	CHECK(fixed_layout(B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS, shape, m),
		"Sega layout");
	CHECK(m.buttons[B_CONTROLLER_BUTTON_A].index == 0
		&& m.buttons[B_CONTROLLER_BUTTON_X].index == 3
		&& m.buttons[B_CONTROLLER_BUTTON_LEFT_SHOULDER].index == 8
		&& m.buttons[B_CONTROLLER_BUTTON_RIGHT_SHOULDER].index == 6
		&& is_half(m.buttons[B_CONTROLLER_BUTTON_DPAD_UP], 1, -1)
		&& !mapping_has_axis(m, B_CONTROLLER_AXIS_LEFT_X), "Sega buttons");
	CHECK(!fixed_layout(B_GAME_CONTROLLER_FAMILY_UNKNOWN, shape, m),
		"no fixed layout for Unknown");

	// PlayStation on the DualSense's axes (DirectInput order X Y Z Rx Ry
	// Rz) and hat: the right stick on Z and Rz, the triggers on Rx and Ry.
	static const uint16 kDualSense[6] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35 };
	memset(&shape, 0, sizeof(shape));
	shape.axisCount = 6;
	memcpy(shape.axisUsage, kDualSense, sizeof(kDualSense));
	shape.hatCount = 1;
	shape.buttonCount = 14;
	CHECK(fixed_layout(B_GAME_CONTROLLER_FAMILY_PLAYSTATION, shape, m),
		"PlayStation layout");
	CHECK(m.buttons[B_CONTROLLER_BUTTON_A].index == 1
		&& m.buttons[B_CONTROLLER_BUTTON_X].index == 0
		&& m.axes[B_CONTROLLER_AXIS_RIGHT_X].whole.index == 2
		&& m.axes[B_CONTROLLER_AXIS_RIGHT_Y].whole.index == 5
		&& m.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER].whole.index == 3
		&& m.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER].whole.type == SOURCE_AXIS
		&& m.buttons[B_CONTROLLER_BUTTON_DPAD_DOWN].type == SOURCE_HAT
		&& m.buttons[B_CONTROLLER_BUTTON_MISC1].type == SOURCE_NONE,
		"PlayStation sources");

	// A saved setup: written, read back, and its axes (the driver's
	// order) put in DirectInput order.
	char text[512];
	CHECK(format_pairs(m, text, sizeof(text)), "format");
	controller_mapping back;
	CHECK(parse_pairs(text, back) && same_mapping(m, back), "round trip");
	CHECK(!format_pairs(m, text, 20), "too small accepted");
	CHECK(parse_pairs("leftx:a3,+righttrigger:a4~,", back), "saved axes");
	int32 order[6] = { 0, 1, 2, 4, 5, 3 };
	axes_to_order(back, order, 6);
	CHECK(back.axes[B_CONTROLLER_AXIS_LEFT_X].whole.index == 5
		&& back.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER].positive.index == 3
		&& back.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER].positive.invert,
		"driver order to DirectInput order");
	CHECK(!parse_pairs("nonsense", back), "nonsense parses");

	printf("gamecontroller_test: descriptor first ok\n");
	return 0;
}


// #pragma mark - 2. the database


static int
test_database()
{
	BPath path;
	CHECK(find_directory(B_SYSTEM_NONPACKAGED_DATA_DIRECTORY, &path) == B_OK,
		"data directory");
	path.Append("game_controllers/gamecontrollerdb.txt");
	FILE* file = fopen(path.Path(), "r");
	CHECK(file != NULL, "gamecontrollerdb.txt not installed");
	char line[2048];
	int32 windows = 0;
	int32 parsed = 0;
	while (fgets(line, sizeof(line), file) != NULL) {
		uint16 vendor;
		uint16 product;
		if (strstr(line, "platform:Windows") == NULL
			|| !parse_guid(line, &vendor, &product))
			continue;
		windows++;
		controller_mapping m;
		if (parse_line(line, vendor, product, m))
			parsed++;
	}
	fclose(file);
	printf("gamecontroller_test: %" B_PRId32 " of %" B_PRId32 " Windows "
		"vendor and product entries parse\n", parsed, windows);
	CHECK(windows > 300 && parsed == windows, "database entries");

	controller_mapping m;
	CHECK(find_system_mapping(0x054c, 0x0ce6, m) == B_OK, "DualSense");
	CHECK(strcmp(m.name, "PS5 Controller") == 0, "DualSense name");
	CHECK(find_system_mapping(0x2dc8, 0x5101, m) == B_OK, "8BitDo M30");
	CHECK(family_for(0x2dc8, 0x5101, m.name, true)
		== B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS, "M30 shape");
	CHECK(find_system_mapping(0x1234, 0x5678, m) == B_ENTRY_NOT_FOUND,
		"made-up controller found");
	printf("gamecontroller_test: database ok\n");
	return 0;
}


// #pragma mark - 3. a virtual controller


// 4 axes (X Y Z Rz), a hat, 14 buttons, 6 bits of padding, 2 axes (Rx Ry):
// the order of the DualSense's simple report. No report ID.
static const uint8 kPad[] = {
	0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
	0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
	0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
	0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01,
	0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x0E, 0x81, 0x02,
	0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
	0x05, 0x01, 0x09, 0x33, 0x09, 0x34,
	0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
	0xC0
};

// Synthetic pads in the 8BitDo M30's place (2dc8:0651; its own 86 bytes are
// not in the boot log): X and Y, then either a hat (null state) and 16
// buttons, or no hat and 12 buttons, as Bluetooth pads without sticks
// report a d-pad one way or the other.
static const uint8 kHatPad[] = {
	0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
	0x09, 0x30, 0x09, 0x31,
	0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
	0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
	0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
	0xC0
};

static const uint8 kAxisPad[] = {
	0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
	0x09, 0x30, 0x09, 0x31,
	0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x0C, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x0C, 0x81, 0x02,
	0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
	0xC0
};


struct test_pad {
	int32	id;
	uint8	report[16];
	uint32	length;
};

static int sControl = -1;
static volatile bool sFeeding = true;
static test_pad sPads[3] = { { -1 }, { -1 }, { -1 } };
// Not the name of --hold, which may run at the same time.
static const char* const kTestName = "DualSense test controller";


static void
set_report(test_pad& pad, uint8 x, uint8 y, uint8 z, uint8 rz, uint8 hat,
	uint16 buttons, uint8 rx, uint8 ry)
{
	pad.report[0] = x;
	pad.report[1] = y;
	pad.report[2] = z;
	pad.report[3] = rz;
	pad.report[4] = (hat & 0x0f) | (uint8)((buttons & 0x0f) << 4);
	pad.report[5] = (uint8)(buttons >> 4);
	pad.report[6] = (uint8)((buttons >> 12) & 0x03);
	pad.report[7] = rx;
	pad.report[8] = ry;
	pad.length = 9;
}


// hat: 0 up ... 7 up and left, 8 centered (the null state).
static void
set_hat_report(test_pad& pad, uint8 x, uint8 y, uint8 hat, uint16 buttons)
{
	pad.report[0] = x;
	pad.report[1] = y;
	pad.report[2] = hat & 0x0f;
	pad.report[3] = (uint8)buttons;
	pad.report[4] = (uint8)(buttons >> 8);
	pad.length = 5;
}


static void
set_axis_report(test_pad& pad, uint8 x, uint8 y, uint16 buttons)
{
	pad.report[0] = x;
	pad.report[1] = y;
	pad.report[2] = (uint8)buttons;
	pad.report[3] = (uint8)((buttons >> 8) & 0x0f);
	pad.length = 4;
}


static status_t
feed_thread(void*)
{
	while (sFeeding) {
		for (int i = 0; i < 3; i++) {
			if (sPads[i].id < 0)
				continue;
			r5_bthid_input input = { sPads[i].id, sPads[i].length,
				sPads[i].report };
			ioctl(sControl, R5_BTHID_INPUT, &input, sizeof(input));
		}
		snooze(20000);
	}
	return B_OK;
}


static int32
create_pad(const char* name, const uint8* descriptor, uint32 length,
	uint16 vendor, uint16 product)
{
	r5_bthid_create request;
	memset(&request, 0, sizeof(request));
	request.descriptor = descriptor;
	request.descriptor_length = length;
	if (ioctl(sControl, R5_BTHID_CREATE, &request, sizeof(request)) != 0)
		return -1;

	r5_bthid_info info;
	memset(&info, 0, sizeof(info));
	info.id = request.id;
	info.vendor_id = vendor;
	info.product_id = product;
	strlcpy(info.name, name, sizeof(info.name));
	if (ioctl(sControl, R5_BTHID_SET_INFO, &info, sizeof(info)) != 0) {
		ioctl(sControl, R5_BTHID_DESTROY, &request.id, sizeof(request.id));
		return -1;
	}
	return request.id;
}


static int32
create_pad(const char* name)
{
	return create_pad(name, kPad, sizeof(kPad), 0x054c, 0x0ce6);
}


static void
destroy_pad(test_pad& pad)
{
	if (pad.id < 0)
		return;
	int32 id = pad.id;
	pad.id = -1;
	ioctl(sControl, R5_BTHID_DESTROY, &id, sizeof(id));
}


static bool
find_pad(const char* wanted, BString& device)
{
	for (int i = 0; i < 40; i++) {
		BGameController controller;
		for (int32 j = 0; j < controller.CountDevices(); j++) {
			char name[B_OS_NAME_LENGTH];
			if (controller.GetDeviceName(j, name, sizeof(name)) != B_OK
				|| strncmp(name, "bluetooth/", 10) != 0
				|| controller.Open(name) != B_OK)
				continue;
			if (strcmp(controller.Name(), wanted) == 0) {
				device = name;
				return true;
			}
		}
		snooze(50000);
	}
	return false;
}


static bool
find_pad(BString& device)
{
	return find_pad(kTestName, device);
}


// Opens the pad called name and waits until condition() holds, at most
// two seconds.
static bool
wait_for(const char* name, BGameController& controller,
	bool (*condition)(BGameController&))
{
	BString device;
	if (!find_pad(name, device) || controller.Open(device.String()) != B_OK)
		return false;
	for (int i = 0; i < 50; i++) {
		snooze(40000);
		if (controller.Update() == B_OK && condition(controller))
			return true;
	}
	return false;
}


static bool
up_and_a(BGameController& c)
{
	return c.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_UP)
		&& c.IsButtonDown(B_CONTROLLER_BUTTON_A);
}


static bool
left_and_b(BGameController& c)
{
	return c.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_LEFT)
		&& c.IsButtonDown(B_CONTROLLER_BUTTON_B);
}


static bool
saved_a_and_left(BGameController& c)
{
	return c.IsButtonDown(B_CONTROLLER_BUTTON_A)
		&& c.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_LEFT);
}


/*!	The two M30-like pads read through the database line for 2dc8:0651
	(a d-pad on the hat, and one on X and Y), then a saved setup and a
	chosen layout on a made-up model, removed again afterwards.
*/
static int
test_m30_pads()
{
	// The hat at "up" (0), button usage 1 down: d-pad up and A.
	set_hat_report(sPads[1], 0x80, 0x80, 0, 1 << 0);
	sPads[1].id = create_pad("M30 hat test", kHatPad, sizeof(kHatPad),
		0x2dc8, 0x0651);
	// X at the left end, button usage 2 down: d-pad left and B.
	set_axis_report(sPads[2], 0x00, 0x80, 1 << 1);
	sPads[2].id = create_pad("M30 axes test", kAxisPad, sizeof(kAxisPad),
		0x2dc8, 0x0651);
	CHECK(sPads[1].id >= 0 && sPads[2].id >= 0, "create the M30 pads");

	BGameController hat;
	bool hatOk = wait_for("M30 hat test", hat, up_and_a);
	printf("gamecontroller_test: M30 hat pad: %s mapping, family %d, "
		"%" B_PRId32 " hats, up %d, A %d\n", hat.HasMapping() ? "a" : "no",
		hat.Family(), hat.CountRawHats(),
		hat.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_UP),
		hat.IsButtonDown(B_CONTROLLER_BUTTON_A));
	CHECK(hatOk, "M30 hat pad: the hat is not d-pad up, or b0 not A");
	CHECK(hat.Family() == B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS,
		"M30 hat pad: Sega shape");
	hat.Close();

	BGameController axes;
	bool axesOk = wait_for("M30 axes test", axes, left_and_b);
	printf("gamecontroller_test: M30 axes pad: %s mapping, %" B_PRId32
		" hats, left %d, up %d, B %d\n", axes.HasMapping() ? "a" : "no",
		axes.CountRawHats(), axes.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_LEFT),
		axes.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_UP),
		axes.IsButtonDown(B_CONTROLLER_BUTTON_B));
	CHECK(axesOk, "M30 axes pad: X is not d-pad left, or b1 not B");
	CHECK(!axes.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_UP)
		&& !axes.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_DOWN),
		"M30 axes pad: Y at rest pressed");
	axes.Close();
	destroy_pad(sPads[1]);
	destroy_pad(sPads[2]);

	// A made-up model with no database line: no mapping, then a saved
	// setup, then a chosen layout under it, then the layout alone.
	set_axis_report(sPads[2], 0x00, 0x80, 1 << 3);
	sPads[2].id = create_pad("Setup test", kAxisPad, sizeof(kAxisPad),
		0x1234, 0x5678);
	CHECK(sPads[2].id >= 0, "create the setup pad");
	BGameController::SetSavedMapping(0x1234, 0x5678, NULL);
	BGameController::SetLayout(0x1234, 0x5678,
		B_GAME_CONTROLLER_FAMILY_UNKNOWN);
	BString device;
	BGameController pad;
	CHECK(find_pad("Setup test", device) && pad.Open(device.String()) == B_OK,
		"open the setup pad");
	CHECK(!pad.HasMapping() && !pad.HasSavedMapping(), "made-up mapping");
	pad.Close();

	CHECK(BGameController::SetSavedMapping(0x1234, 0x5678, "nonsense")
		== B_BAD_VALUE, "nonsense saved");
	CHECK(BGameController::SetSavedMapping(0x1234, 0x5678,
		"a:b3,dpleft:-a0,dpright:+a0,") == B_OK, "save a setup");
	char text[256];
	CHECK(BGameController::GetSavedMapping(0x1234, 0x5678, text,
		sizeof(text)) == B_OK && strcmp(text, "a:b3,dpleft:-a0,dpright:+a0,")
		== 0, "read the setup back");
	CHECK(BGameController::SetLayout(0x1234, 0x5678,
		B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS) == B_OK, "choose a layout");
	CHECK(wait_for("Setup test", pad, saved_a_and_left),
		"saved setup: b3 is not A, or X not d-pad left");
	CHECK(pad.HasSavedMapping()
		&& pad.Family() == B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS,
		"saved setup with the Sega shape");
	pad.Close();

	// Without the setup, the Sega layout: b3 is X.
	BGameController::SetSavedMapping(0x1234, 0x5678, "");
	CHECK(BGameController::GetSavedMapping(0x1234, 0x5678, text,
		sizeof(text)) == B_ENTRY_NOT_FOUND, "setup removed");
	CHECK(find_pad("Setup test", device) && pad.Open(device.String()) == B_OK,
		"open the setup pad again");
	bool x = false;
	for (int i = 0; i < 50 && !x; i++) {
		snooze(40000);
		x = pad.Update() == B_OK && pad.IsButtonDown(B_CONTROLLER_BUTTON_X)
			&& pad.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_LEFT);
	}
	CHECK(x && pad.HasMapping() && !pad.HasSavedMapping(),
		"Sega layout: b3 is not X, or X not d-pad left");
	pad.Close();
	BGameController::SetLayout(0x1234, 0x5678,
		B_GAME_CONTROLLER_FAMILY_UNKNOWN);
	CHECK(BGameController::LayoutFor(0x1234, 0x5678)
		== B_GAME_CONTROLLER_FAMILY_UNKNOWN, "layout removed");
	destroy_pad(sPads[2]);
	printf("gamecontroller_test: M30 pads and the setup ok\n");
	return 0;
}


class Receiver : public BLooper {
public:
	Receiver()
		:
		BLooper("receiver"),
		fAdded(create_sem(0, "added")),
		fRemoved(create_sem(0, "removed"))
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_GAME_CONTROLLER_ADDED)
			release_sem(fAdded);
		else if (message->what == B_GAME_CONTROLLER_REMOVED)
			release_sem(fRemoved);
		else
			BLooper::MessageReceived(message);
	}

	sem_id	fAdded;
	sem_id	fRemoved;
};


static int
test_virtual_controller()
{
	if (geteuid() != 0) {
		printf("gamecontroller_test: virtual controller skipped (not uid "
			"0)\n");
		return 0;
	}
	sControl = open("/dev/" R5_BTHID_CONTROL_NAME, O_RDWR);
	if (sControl < 0) {
		printf("gamecontroller_test: virtual controller skipped (no "
			"bluetooth_hid)\n");
		return 0;
	}

	// The info call on a device that is not ours is refused.
	r5_bthid_info stranger;
	memset(&stranger, 0, sizeof(stranger));
	stranger.id = R5_BTHID_MAX_DEVICES;
	CHECK(ioctl(sControl, R5_BTHID_SET_INFO, &stranger, sizeof(stranger))
		!= 0, "info for a bad id accepted");

	// Cross (button 2) down, the hat at "down" (HID 4), R2 (Ry) full, the
	// right stick (Z) to the right; L2 (Rx) at rest.
	set_report(sPads[0], 0x80, 0x80, 0xff, 0x80, 4, 1 << 1, 0x00, 0xff);
	sPads[0].id = create_pad(kTestName);
	CHECK(sPads[0].id >= 0, "create the virtual controller");
	thread_id feeder = spawn_thread(feed_thread, "feed", B_NORMAL_PRIORITY,
		NULL);
	resume_thread(feeder);

	BString device;
	bool found = find_pad(device);
	BGameController controller;
	int result = 0;
	if (!found)
		result = fail("virtual controller not listed with its IDs");
	else if (controller.Open(device.String()) != B_OK)
		result = fail("open");
	if (result == 0) {
		bool pressed = false;
		for (int i = 0; i < 50 && !pressed; i++) {
			snooze(40000);
			if (controller.Update() == B_OK)
				pressed = controller.IsButtonDown(B_CONTROLLER_BUTTON_A);
		}
		printf("gamecontroller_test: %s is \"%s\", %04x:%04x, %s mapping, "
			"%" B_PRId32 " axes, %" B_PRId32 " buttons, %" B_PRId32 " hats\n",
			device.String(), controller.Name(), controller.VendorID(),
			controller.ProductID(), controller.HasMapping() ? "a" : "no",
			controller.CountRawAxes(), controller.CountRawButtons(),
			controller.CountRawHats());
		if (strcmp(controller.Name(), kTestName) != 0)
			result = fail("name from R5_BTHID_SET_INFO");
		else if (controller.ProductID() != 0x0ce6)
			result = fail("product ID");
		else if (controller.Connection()
				!= B_GAME_CONTROLLER_CONNECTION_BLUETOOTH)
			result = fail("connection");
		else if (!controller.HasMapping())
			result = fail("no mapping for 054c:0ce6");
		else if (controller.Family() != B_GAME_CONTROLLER_FAMILY_PLAYSTATION)
			result = fail("family");
		else if (!pressed)
			result = fail("Cross (b1) is not A");
		else if (!controller.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_DOWN)
			|| controller.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_UP))
			result = fail("hat is not d-pad down");
		else if (controller.AxisValue(B_CONTROLLER_AXIS_RIGHT_TRIGGER) < 32000
			|| controller.AxisValue(B_CONTROLLER_AXIS_LEFT_TRIGGER) > 500)
			result = fail("triggers (Rx and Ry by usage)");
		else if (controller.AxisValue(B_CONTROLLER_AXIS_RIGHT_X) < 30000)
			result = fail("right stick (Z)");
		else if (controller.BatteryLevel() != -1)
			result = fail("battery without Battery Strength");
		printf("gamecontroller_test: A %d, down %d, L2 %d, R2 %d, right x %d\n",
			controller.IsButtonDown(B_CONTROLLER_BUTTON_A),
			controller.IsButtonDown(B_CONTROLLER_BUTTON_DPAD_DOWN),
			controller.AxisValue(B_CONTROLLER_AXIS_LEFT_TRIGGER),
			controller.AxisValue(B_CONTROLLER_AXIS_RIGHT_TRIGGER),
			controller.AxisValue(B_CONTROLLER_AXIS_RIGHT_X));
	}
	controller.Close();

	// The label choice, stored per model.
	if (result == 0) {
		game_controller_labels before
			= BGameController::LabelStyleFor(0x054c, 0x0ce6);
		if (BGameController::SetLabelStyle(0x1234, 0x5678,
				B_GAME_CONTROLLER_LABELS_PLAYSTATION) != B_OK
			|| BGameController::LabelStyleFor(0x1234, 0x5678)
				!= B_GAME_CONTROLLER_LABELS_PLAYSTATION
			|| BGameController::SetLabelStyle(0x1234, 0x5678,
				B_GAME_CONTROLLER_LABELS_AUTOMATIC) != B_OK
			|| BGameController::LabelStyleFor(0x1234, 0x5678)
				!= B_GAME_CONTROLLER_LABELS_AUTOMATIC
			|| BGameController::LabelStyleFor(0x054c, 0x0ce6) != before)
			result = fail("label choice");
	}

	// Plug and unplug messages.
	if (result == 0) {
		Receiver* receiver = new Receiver;
		receiver->Run();
		if (BGameController::StartWatching(BMessenger(receiver)) != B_OK)
			result = fail("StartWatching");
		int32 second = result == 0 ? create_pad("Second controller") : -1;
		if (result == 0 && second < 0)
			result = fail("create the second controller");
		if (result == 0 && acquire_sem_etc(receiver->fAdded, 1,
				B_RELATIVE_TIMEOUT, 3000000) != B_OK)
			result = fail("no B_GAME_CONTROLLER_ADDED");
		if (second >= 0)
			ioctl(sControl, R5_BTHID_DESTROY, &second, sizeof(second));
		if (result == 0 && acquire_sem_etc(receiver->fRemoved, 1,
				B_RELATIVE_TIMEOUT, 3000000) != B_OK)
			result = fail("no B_GAME_CONTROLLER_REMOVED");
		BGameController::StopWatching(BMessenger(receiver));
		receiver->Lock();
		receiver->Quit();
	}

	// The M30-like pads and a saved setup, while the feeder runs.
	if (result == 0)
		result = test_m30_pads();

	sFeeding = false;
	status_t status;
	wait_for_thread(feeder, &status);
	for (int i = 0; i < 3; i++)
		destroy_pad(sPads[i]);
	close(sControl);
	sControl = -1;
	if (result == 0)
		printf("gamecontroller_test: virtual controller ok\n");
	return result;
}


static int
hold(int seconds)
{
	sControl = open("/dev/" R5_BTHID_CONTROL_NAME, O_RDWR);
	if (sControl < 0)
		return fail("open the bluetooth_hid control node (uid 0?)");
	// As the mockup: Cross down, L2 at 60 %, the left stick up and left.
	set_report(sPads[0], 0x40, 0x40, 0x80, 0x80, 8, 1 << 1, 153, 0x00);
	sPads[0].id = create_pad("DualSense Wireless Controller");
	if (sPads[0].id < 0)
		return fail("create the virtual controller");
	printf("gamecontroller_test: holding a virtual controller for %d s\n",
		seconds);
	fflush(stdout);
	thread_id feeder = spawn_thread(feed_thread, "feed", B_NORMAL_PRIORITY,
		NULL);
	resume_thread(feeder);
	snooze((bigtime_t)seconds * 1000000);
	sFeeding = false;
	status_t status;
	wait_for_thread(feeder, &status);
	destroy_pad(sPads[0]);
	close(sControl);
	return 0;
}


// #pragma mark - 4. drawing


static int32
painted_pixels(BBitmap* bitmap)
{
	int32 count = 0;
	const uint8* bits = (const uint8*)bitmap->Bits();
	int32 length = bitmap->BitsLength();
	for (int32 i = 0; i + 3 < length; i += 4) {
		if (bits[i + 3] != 0)
			count++;
	}
	return count;
}


static int
test_drawing()
{
	if (be_app == NULL)
		return 0;

	static const r5_glyph kGlyphs[] = {
		R5_GLYPH_CROSS, R5_GLYPH_CIRCLE, R5_GLYPH_SQUARE, R5_GLYPH_TRIANGLE
	};
	for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); i++) {
		BBitmap* glyph = r5_glyph_bitmap(kGlyphs[i], 24,
			make_color(0, 0, 0));
		CHECK(glyph != NULL, "glyph bitmap");
		int32 pixels = painted_pixels(glyph);
		delete glyph;
		CHECK(pixels > 20, "glyph draws nothing");
	}

	static const game_controller_family kFamilies[] = {
		B_GAME_CONTROLLER_FAMILY_PLAYSTATION, B_GAME_CONTROLLER_FAMILY_XBOX,
		B_GAME_CONTROLLER_FAMILY_NINTENDO,
		B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS,
		B_GAME_CONTROLLER_FAMILY_UNKNOWN
	};
	BRect bounds(0, 0, 207, 123);
	for (size_t i = 0; i < sizeof(kFamilies) / sizeof(kFamilies[0]); i++) {
		BBitmap bitmap(bounds, B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
		CHECK(bitmap.InitCheck() == B_OK, "bitmap");
		BGameControllerView* view = new BGameControllerView("shape");
		view->SetFamily(kFamilies[i]);
		view->ResizeTo(bounds.Width(), bounds.Height());
		view->SetViewColor(255, 255, 255);
		bitmap.AddChild(view);
		CHECK(bitmap.Lock(), "lock bitmap");
		view->Draw(bounds);
		view->Sync();
		bitmap.Unlock();
		// Count what is not the white background.
		int32 lines = 0;
		const uint8* bits = (const uint8*)bitmap.Bits();
		int32 length = bitmap.BitsLength();
		for (int32 j = 0; j + 3 < length; j += 4) {
			if (bits[j] < 200 || bits[j + 1] < 200 || bits[j + 2] < 200)
				lines++;
		}
		printf("gamecontroller_test: shape %d, %" B_PRId32 " line pixels\n",
			(int)kFamilies[i], lines);
		CHECK(lines > 300, "shape draws too little");
	}
	printf("gamecontroller_test: drawing ok\n");
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc == 3 && strcmp(argv[1], "--hold") == 0)
		return hold(atoi(argv[2]));

	if (test_parser() != 0 || test_descriptor_first() != 0
		|| test_database() != 0
		|| test_virtual_controller() != 0)
		return 1;

	BApplication app("application/x-vnd.R5OS-gamecontroller-test");
	if (test_drawing() != 0)
		return 1;

	printf("gamecontroller_test: ok\n");
	return 0;
}
