/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host unit test of the report code behind rumble, trigger effects, motion
 * and HID PID force feedback (docs/GAME-CONTROLLERS.md, option C phase 1):
 * libr5game's GameControllerReports.cpp and hid_shared's R5HIDReports.h.
 * Exact bytes of every output report, the CRC-32 of the Bluetooth reports
 * against a second, table-driven CRC, the motion parser, the short report
 * the kernel builds from a full one, and the HID PID reports for a
 * synthetic descriptor. No controller needed; scripts/test-game-feel.sh
 * builds and runs it with AddressSanitizer and UBSan.
 * Prints "feel_host_test: ok" or every failure.
 */


#include <math.h>
#include <stdio.h>
#include <string.h>

#include "GameControllerReports.h"
#include "R5HIDReports.h"


using namespace R5GameController;


static int sFailures = 0;


#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			printf("feel_host_test: FAIL line %d: %s\n", __LINE__, \
				#condition); \
			sFailures++; \
		} \
	} while (0)


static void
check_bytes(int line, const char* what, const uint8* got, size_t gotLength,
	const uint8* expected, size_t expectedLength)
{
	if (gotLength == expectedLength
		&& memcmp(got, expected, expectedLength) == 0)
		return;
	printf("feel_host_test: FAIL line %d: %s\n  got     ", line, what);
	for (size_t i = 0; i < gotLength; i++)
		printf("%02x ", got[i]);
	printf("\n  expected ");
	for (size_t i = 0; i < expectedLength; i++)
		printf("%02x ", expected[i]);
	printf("\n");
	sFailures++;
}


#define CHECK_BYTES(what, got, length, ...) \
	do { \
		const uint8 expected_[] = { __VA_ARGS__ }; \
		check_bytes(__LINE__, what, got, length, expected_, \
			sizeof(expected_)); \
	} while (0)


// A second CRC-32, table driven, to check the bitwise one.
static uint32
table_crc32(const uint8* data, size_t length)
{
	static uint32 table[256];
	static bool ready = false;
	if (!ready) {
		for (uint32 n = 0; n < 256; n++) {
			uint32 c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		ready = true;
	}
	uint32 crc = 0xffffffffu;
	for (size_t i = 0; i < length; i++)
		crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
	return crc ^ 0xffffffffu;
}


static void
check_bluetooth_crc(int line, const uint8* report, size_t size)
{
	uint8 buffer[1 + 78];
	buffer[0] = 0xa2;
	memcpy(buffer + 1, report, size - 4);
	uint32 expected = table_crc32(buffer, size - 3);
	uint32 stored = report[size - 4] | (report[size - 3] << 8)
		| (report[size - 2] << 16) | ((uint32)report[size - 1] << 24);
	if (stored != expected) {
		printf("feel_host_test: FAIL line %d: CRC %08x, expected %08x\n",
			line, stored, expected);
		sFailures++;
	}
}


static bool
all_zero(const uint8* data, size_t from, size_t to)
{
	for (size_t i = from; i < to; i++) {
		if (data[i] != 0)
			return false;
	}
	return true;
}


static void
test_crc()
{
	const char* text = "123456789";
	CHECK(crc32(0, (const uint8*)text, 9) == 0xCBF43926u);
	CHECK(table_crc32((const uint8*)text, 9) == 0xCBF43926u);
	// Continued in two parts gives the same.
	uint32 part = crc32(0, (const uint8*)text, 4);
	CHECK(crc32(part, (const uint8*)text + 4, 5) == 0xCBF43926u);
}


static void
test_models()
{
	CHECK(playstation_model_for(0x054c, 0x05c4) == PLAYSTATION_DUALSHOCK4);
	CHECK(playstation_model_for(0x054c, 0x09cc) == PLAYSTATION_DUALSHOCK4);
	CHECK(playstation_model_for(0x054c, 0x0ce6) == PLAYSTATION_DUALSENSE);
	CHECK(playstation_model_for(0x054c, 0x0df2)
		== PLAYSTATION_DUALSENSE_EDGE);
	CHECK(playstation_model_for(0x2dc8, 0x0651) == PLAYSTATION_NONE);
	CHECK(playstation_model_for(0x054c, 0x0268) == PLAYSTATION_NONE);
}


static void
test_trigger_effects()
{
	uint8 effect[kTriggerEffectSize];
	dualsense_trigger_effect(TRIGGER_EFFECT_OFF, 0.5f, 0.5f, effect);
	CHECK_BYTES("trigger off", effect, sizeof(effect),
		0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	dualsense_trigger_effect(TRIGGER_EFFECT_RESISTANCE, 0.4f, 0.7f, effect);
	CHECK_BYTES("trigger resistance", effect, sizeof(effect),
		0x01, 102, 179, 0, 0, 0, 0, 0, 0, 0, 0);
	dualsense_trigger_effect(TRIGGER_EFFECT_CLICK, 0, 1, effect);
	CHECK_BYTES("trigger click at the start", effect, sizeof(effect),
		0x02, 0x10, 0x50, 255, 0, 0, 0, 0, 0, 0, 0);
	dualsense_trigger_effect(TRIGGER_EFFECT_CLICK, 1, 0.5f, effect);
	CHECK_BYTES("trigger click at the end", effect, sizeof(effect),
		0x02, 0xa0, 0xe0, 128, 0, 0, 0, 0, 0, 0, 0);
	dualsense_trigger_effect(TRIGGER_EFFECT_VIBRATION, 0.5f, 1, effect);
	CHECK_BYTES("trigger vibration", effect, sizeof(effect),
		0x06, 30, 255, 128, 0, 0, 0, 0, 0, 0, 0);
	// Out of range and NaN are clamped, an unknown effect is off.
	dualsense_trigger_effect(TRIGGER_EFFECT_RESISTANCE, -3, NAN, effect);
	CHECK_BYTES("trigger clamped", effect, sizeof(effect),
		0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	dualsense_trigger_effect(99, 1, 1, effect);
	CHECK(effect[0] == 0x05);
}


static void
test_dualsense()
{
	playstation_effects effects;
	playstation_effects_init(effects);
	effects.strong = 255;
	effects.weak = 128;
	dualsense_trigger_effect(TRIGGER_EFFECT_RESISTANCE, 0.4f, 0.7f,
		effects.rightTrigger);

	uint8 report[80];
	CHECK(build_dualsense_output(effects, false, report, 47) == 0);

	// USB: report 0x02, 48 bytes, the effects state from byte 1.
	memset(report, 0xee, sizeof(report));
	size_t length = build_dualsense_output(effects, false, report,
		sizeof(report));
	CHECK(length == 48);
	CHECK_BYTES("DualSense USB head", report, 5,
		0x02, 0x0e, 0x00, 128, 255);
	CHECK(all_zero(report, 5, 11));
	CHECK_BYTES("DualSense USB right trigger", report + 11, 11,
		0x01, 102, 179, 0, 0, 0, 0, 0, 0, 0, 0);
	CHECK_BYTES("DualSense USB left trigger", report + 22, 11,
		0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	CHECK(all_zero(report, 33, 39));
	CHECK(report[39] == 0x04);
	CHECK(all_zero(report, 40, 48));

	// Older firmware: rumble emulation instead of the improved rumble.
	effects.improvedRumble = false;
	length = build_dualsense_output(effects, false, report, sizeof(report));
	CHECK(length == 48 && report[1] == 0x0f && report[39] == 0x00);
	effects.improvedRumble = true;

	// Bluetooth: report 0x31, 78 bytes, tag 0x00 and 0x10, the state from
	// byte 3, CRC-32 of 0xA2 and bytes 0 to 73 in bytes 74 to 77.
	memset(report, 0xee, sizeof(report));
	length = build_dualsense_output(effects, true, report, sizeof(report));
	CHECK(length == 78);
	CHECK_BYTES("DualSense Bluetooth head", report, 7,
		0x31, 0x00, 0x10, 0x0e, 0x00, 128, 255);
	CHECK(all_zero(report, 7, 13));
	CHECK(report[13] == 0x01 && report[14] == 102 && report[15] == 179);
	CHECK(report[24] == 0x05);
	CHECK(report[41] == 0x04);
	CHECK(all_zero(report, 42, 74));
	check_bluetooth_crc(__LINE__, report, 78);
	CHECK(report[78] == 0xee);
}


static void
test_dualshock4()
{
	playstation_effects effects;
	playstation_effects_init(effects);
	effects.strong = 200;
	effects.weak = 50;

	// USB: report 0x05, 32 bytes, flags 0x01 (rumble only), the right
	// (weak) motor in byte 4, the left (strong) one in byte 5.
	uint8 report[80];
	memset(report, 0xee, sizeof(report));
	size_t length = build_dualshock4_output(effects, false, report,
		sizeof(report));
	CHECK(length == 32);
	CHECK_BYTES("DualShock 4 USB", report, 32,
		0x05, 0x01, 0x00, 0x00, 50, 200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	// Bluetooth: report 0x11, 78 bytes, 0xC4, flags in byte 3, motors in
	// bytes 6 and 7, CRC.
	length = build_dualshock4_output(effects, true, report, sizeof(report));
	CHECK(length == 78);
	CHECK_BYTES("DualShock 4 Bluetooth head", report, 8,
		0x11, 0xc4, 0x00, 0x01, 0x00, 0x00, 50, 200);
	CHECK(all_zero(report, 8, 74));
	check_bluetooth_crc(__LINE__, report, 78);
	CHECK(build_dualshock4_output(effects, true, report, 77) == 0);
}


static void
put16(uint8* at, int16 value)
{
	at[0] = (uint8)value;
	at[1] = (uint8)((uint16)value >> 8);
}


static void
fill_sensors(uint8* state, size_t gyro, size_t accel)
{
	put16(state + gyro, 16);
	put16(state + gyro + 2, -32);
	put16(state + gyro + 4, 1600);
	put16(state + accel, 8192);
	put16(state + accel + 2, -4096);
	put16(state + accel + 4, 0);
}


static bool
sample_is(const motion_sample& sample)
{
	return sample.rotation[0] == 1 && sample.rotation[1] == -2
		&& sample.rotation[2] == 100 && sample.acceleration[0] == 1
		&& sample.acceleration[1] == -0.5f && sample.acceleration[2] == 0;
}


static void
test_motion()
{
	CHECK(playstation_motion_report(PLAYSTATION_DUALSENSE, false) == 0x01);
	CHECK(playstation_motion_report(PLAYSTATION_DUALSENSE, true) == 0x31);
	CHECK(playstation_motion_report(PLAYSTATION_DUALSHOCK4, true) == 0x11);

	uint8 report[80];
	motion_sample sample;

	// DualSense USB: state from byte 1, gyroscope at 15, accelerometer at
	// 21 of the state.
	memset(report, 0, sizeof(report));
	report[0] = 0x01;
	fill_sensors(report + 1, 15, 21);
	CHECK(parse_playstation_motion(PLAYSTATION_DUALSENSE, false, report, 64,
		sample) && sample_is(sample));
	CHECK(!parse_playstation_motion(PLAYSTATION_DUALSENSE, false, report,
		1 + 21 + 5, sample));
	CHECK(parse_playstation_motion(PLAYSTATION_DUALSENSE_EDGE, false, report,
		1 + 21 + 6, sample));

	// DualSense Bluetooth: report 0x31, state from byte 2.
	memset(report, 0, sizeof(report));
	report[0] = 0x31;
	fill_sensors(report + 2, 15, 21);
	CHECK(parse_playstation_motion(PLAYSTATION_DUALSENSE, true, report, 78,
		sample) && sample_is(sample));
	report[0] = 0x01;
	CHECK(!parse_playstation_motion(PLAYSTATION_DUALSENSE, true, report, 78,
		sample));

	// DualShock 4: gyroscope at 12, accelerometer at 18 of the state; the
	// state from byte 1 on USB, byte 3 in report 0x11.
	memset(report, 0, sizeof(report));
	report[0] = 0x01;
	fill_sensors(report + 1, 12, 18);
	CHECK(parse_playstation_motion(PLAYSTATION_DUALSHOCK4, false, report, 64,
		sample) && sample_is(sample));
	memset(report, 0, sizeof(report));
	report[0] = 0x11;
	fill_sensors(report + 3, 12, 18);
	CHECK(parse_playstation_motion(PLAYSTATION_DUALSHOCK4, true, report, 78,
		sample) && sample_is(sample));
	CHECK(!parse_playstation_motion(PLAYSTATION_DUALSHOCK4, true, report, 10,
		sample));
	CHECK(!parse_playstation_motion(PLAYSTATION_NONE, true, report, 78,
		sample));
	CHECK(!parse_playstation_motion(PLAYSTATION_DUALSHOCK4, true, NULL, 78,
		sample));
}


static void
test_short_report()
{
	uint8 in[78];
	uint8 out[16];
	for (size_t i = 0; i < sizeof(in); i++)
		in[i] = (uint8)(0x40 + i);

	// DualSense full report 0x31: sticks, triggers, counter, buttons from
	// byte 2; the short report has sticks, buttons, triggers.
	in[0] = 0x31;
	CHECK(r5_hid_sony_short_report(0x054c, 0x0ce6, in, 78, out, sizeof(out))
		== 10);
	CHECK_BYTES("DualSense short report", out, 10,
		0x01, 0x42, 0x43, 0x44, 0x45, 0x49, 0x4a, 0x4b, 0x46, 0x47);
	CHECK(r5_hid_sony_short_report(0x054c, 0x0df2, in, 12, out, sizeof(out))
		== 10);
	CHECK(r5_hid_sony_short_report(0x054c, 0x0ce6, in, 11, out, sizeof(out))
		== 0);
	CHECK(r5_hid_sony_short_report(0x054c, 0x0ce6, in, 78, out, 9) == 0);

	// DualShock 4 report 0x11: the same 9 bytes from byte 3.
	in[0] = 0x11;
	CHECK(r5_hid_sony_short_report(0x054c, 0x09cc, in, 78, out, sizeof(out))
		== 10);
	CHECK_BYTES("DualShock 4 short report", out, 10,
		0x01, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b);

	// Not a full report, or not one of these controllers: nothing.
	in[0] = 0x01;
	CHECK(r5_hid_sony_short_report(0x054c, 0x09cc, in, 78, out, sizeof(out))
		== 0);
	in[0] = 0x31;
	CHECK(r5_hid_sony_short_report(0x054c, 0x09cc, in, 78, out, sizeof(out))
		== 0);
	CHECK(r5_hid_sony_short_report(0x2dc8, 0x0ce6, in, 78, out, sizeof(out))
		== 0);
	CHECK(r5_hid_sony_short_report(0x054c, 0x0ce6, NULL, 78, out,
		sizeof(out)) == 0);
}


static void
test_cache()
{
	r5_hid_report_cache cache;
	r5_hid_cache_init(cache);
	uint8 out[200];
	size_t length = 0;
	bigtime_t when = 0;
	CHECK(r5_hid_cache_get(cache, 1, out, sizeof(out), &length, &when)
		== B_ENTRY_NOT_FOUND);

	uint8 report[150];
	for (size_t i = 0; i < sizeof(report); i++)
		report[i] = (uint8)i;
	report[0] = 0x31;
	r5_hid_cache_store(cache, report, sizeof(report), true, 1000);
	CHECK(r5_hid_cache_get(cache, 0x31, out, sizeof(out), &length, &when)
		== B_OK);
	CHECK(length == R5_HID_CACHE_REPORT && when == 1000
		&& memcmp(out, report, R5_HID_CACHE_REPORT) == 0);
	CHECK(r5_hid_cache_get(cache, 0x31, out, 10, &length, &when) == B_OK
		&& length == 10);

	// One slot per ID; a fifth ID replaces the oldest.
	for (uint8 id = 1; id <= 4; id++) {
		report[0] = id;
		r5_hid_cache_store(cache, report, 20, true, 2000 + id);
	}
	CHECK(r5_hid_cache_get(cache, 0x31, out, sizeof(out), &length, &when)
		== B_ENTRY_NOT_FOUND);
	report[0] = 2;
	report[5] = 0x99;
	r5_hid_cache_store(cache, report, 20, true, 3000);
	CHECK(r5_hid_cache_get(cache, 2, out, sizeof(out), &length, &when)
		== B_OK && when == 3000 && out[5] == 0x99 && length == 20);
	CHECK(r5_hid_cache_get(cache, 4, out, sizeof(out), &length, &when)
		== B_OK && when == 2004);

	// Without report IDs everything is ID 0.
	r5_hid_cache_init(cache);
	r5_hid_cache_store(cache, report, 5, false, 1);
	CHECK(r5_hid_cache_get(cache, 0, out, sizeof(out), &length, &when)
		== B_OK && length == 5);
	r5_hid_cache_store(cache, NULL, 5, false, 1);
	r5_hid_cache_store(cache, report, 0, false, 1);
}


// A joystick with an input report (ID 9) and the PID reports for one
// effect: Set Effect (1), Set Periodic (2), Effect Operation (3), Device
// Control (4), Device Gain (5), Create New Effect (6, feature), PID Block
// Load (7, feature). Written from the USB PID 1.0 specification.
static const uint8 kPIDJoystick[] = {
	0x05, 0x01, 0x09, 0x04, 0xa1, 0x01,		// Generic Desktop, Joystick,
											// Application
	0x85, 0x09, 0x09, 0x30, 0x15, 0x00, 0x26, 0xff, 0x00,
	0x75, 0x08, 0x95, 0x01, 0x81, 0x02,		// X, input

	0x05, 0x0f,								// PID page
	// Set Effect Report
	0x09, 0x21, 0xa1, 0x02, 0x85, 0x01,
	0x09, 0x22, 0x15, 0x01, 0x25, 0x28, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
	0x09, 0x25, 0xa1, 0x02,
		0x09, 0x26, 0x09, 0x31, 0x15, 0x01, 0x25, 0x02,
		0x75, 0x08, 0x95, 0x01, 0x91, 0x00,
	0xc0,
	0x09, 0x50, 0x09, 0x54, 0x09, 0x51, 0x15, 0x00, 0x26, 0xff, 0x7f,
	0x75, 0x10, 0x95, 0x03, 0x91, 0x02,
	0x09, 0x52, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01,
	0x91, 0x02,
	0x09, 0x53, 0x15, 0x01, 0x25, 0x08, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
	0x09, 0x56, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x91, 0x02,
	0x75, 0x07, 0x91, 0x03,
	0x09, 0xa7, 0x15, 0x00, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x01,
	0x91, 0x02,
	0xc0,
	// Set Periodic Report
	0x09, 0x6e, 0xa1, 0x02, 0x85, 0x02,
	0x09, 0x22, 0x15, 0x01, 0x25, 0x28, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
	0x09, 0x70, 0x15, 0x00, 0x26, 0x10, 0x27, 0x75, 0x10, 0x95, 0x01,
	0x91, 0x02,
	0x09, 0x6f, 0x16, 0xf0, 0xd8, 0x26, 0x10, 0x27, 0x75, 0x10, 0x95, 0x01,
	0x91, 0x02,
	0x09, 0x71, 0x09, 0x72, 0x15, 0x00, 0x26, 0xff, 0x7f, 0x75, 0x10,
	0x95, 0x02, 0x91, 0x02,
	0xc0,
	// Effect Operation Report
	0x09, 0x77, 0xa1, 0x02, 0x85, 0x03,
	0x09, 0x22, 0x15, 0x01, 0x25, 0x28, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
	0x09, 0x78, 0xa1, 0x02,
		0x09, 0x79, 0x09, 0x7a, 0x09, 0x7b, 0x15, 0x01, 0x25, 0x03,
		0x75, 0x08, 0x95, 0x01, 0x91, 0x00,
	0xc0,
	0x09, 0x7c, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01,
	0x91, 0x02,
	0xc0,
	// PID Device Control Report
	0x09, 0x95, 0xa1, 0x02, 0x85, 0x04,
	0x09, 0x96, 0xa1, 0x02,
		0x09, 0x97, 0x09, 0x98, 0x09, 0x99, 0x09, 0x9a, 0x09, 0x9b, 0x09,
		0x9c, 0x15, 0x01, 0x25, 0x06, 0x75, 0x08, 0x95, 0x01, 0x91, 0x00,
	0xc0,
	0xc0,
	// Device Gain Report
	0x09, 0x7d, 0xa1, 0x02, 0x85, 0x05,
	0x09, 0x7e, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01,
	0x91, 0x02,
	0xc0,
	// Create New Effect Report (feature)
	0x09, 0xab, 0xa1, 0x02, 0x85, 0x06,
	0x09, 0x25, 0xa1, 0x02,
		0x09, 0x26, 0x09, 0x31, 0x15, 0x01, 0x25, 0x02,
		0x75, 0x08, 0x95, 0x01, 0xb1, 0x00,
	0xc0,
	0x05, 0x01, 0x09, 0x3b, 0x15, 0x00, 0x26, 0xff, 0x01,
	0x75, 0x0a, 0x95, 0x01, 0xb1, 0x02, 0x75, 0x06, 0xb1, 0x03,
	0x05, 0x0f,
	0xc0,
	// PID Block Load Report (feature)
	0x09, 0x89, 0xa1, 0x02, 0x85, 0x07,
	0x09, 0x22, 0x15, 0x01, 0x25, 0x28, 0x75, 0x08, 0x95, 0x01, 0xb1, 0x02,
	0x09, 0x8b, 0xa1, 0x02,
		0x09, 0x8c, 0x09, 0x8d, 0x09, 0x8e, 0x15, 0x01, 0x25, 0x03,
		0x75, 0x08, 0x95, 0x01, 0xb1, 0x00,
	0xc0,
	0x09, 0xac, 0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00,
	0x75, 0x10, 0x95, 0x01, 0xb1, 0x02,
	0xc0,
	0xc0
};

// A plain gamepad (8 buttons, X and Y): no PID page.
static const uint8 kGamepad[] = {
	0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xff, 0x00,
	0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
	0xc0
};


static void
test_pid()
{
	static hid_layout layout;
	pid_device device;

	CHECK(parse_hid_descriptor(kGamepad, sizeof(kGamepad), layout));
	CHECK(!layout.hasPIDPage && !layout.usesReportIDs);
	CHECK(hid_report_length(layout, HID_INPUT, 0) == 3);
	CHECK(!pid_find(layout, device) && !device.usable);

	// Malformed: an End Collection too many, a cut item, a Pop without a
	// Push, an unclosed collection.
	const uint8 extraEnd[] = { 0xa1, 0x01, 0xc0, 0xc0 };
	CHECK(!parse_hid_descriptor(extraEnd, sizeof(extraEnd), layout));
	const uint8 cut[] = { 0x05, 0x01, 0x26, 0xff };
	CHECK(!parse_hid_descriptor(cut, sizeof(cut), layout));
	const uint8 pop[] = { 0xb4 };
	CHECK(!parse_hid_descriptor(pop, sizeof(pop), layout));
	const uint8 open[] = { 0xa1, 0x01 };
	CHECK(!parse_hid_descriptor(open, sizeof(open), layout));
	CHECK(!parse_hid_descriptor(NULL, 4, layout));

	CHECK(parse_hid_descriptor(kPIDJoystick, sizeof(kPIDJoystick), layout));
	CHECK(layout.hasPIDPage && layout.usesReportIDs);
	CHECK(hid_report_length(layout, HID_INPUT, 9) == 2);
	CHECK(hid_report_length(layout, HID_OUTPUT, 1) == 14);
	CHECK(hid_report_length(layout, HID_OUTPUT, 2) == 10);
	CHECK(hid_report_length(layout, HID_OUTPUT, 3) == 4);
	CHECK(hid_report_length(layout, HID_OUTPUT, 4) == 2);
	CHECK(hid_report_length(layout, HID_OUTPUT, 5) == 2);
	CHECK(hid_report_length(layout, HID_FEATURE, 6) == 4);
	CHECK(hid_report_length(layout, HID_FEATURE, 7) == 5);
	CHECK(hid_report_length(layout, HID_OUTPUT, 7) == 0);

	CHECK(pid_find(layout, device) && device.usable);
	CHECK(device.periodic && device.effectType == 0x31);
	CHECK(device.setEffect.id == 1 && device.setPeriodic.id == 2
		&& device.effectOperation.id == 3 && device.deviceControl.id == 4
		&& device.deviceGain.id == 5 && device.createNewEffect.id == 6
		&& device.blockLoad.id == 7 && !device.setConstant.present);
	CHECK(device.createNewEffect.type == HID_FEATURE
		&& device.setEffect.type == HID_OUTPUT);

	hid_report reports[3];
	CHECK(pid_init_reports(layout, device, reports, 3) == 3);
	CHECK_BYTES("PID Device Reset", reports[0].data, reports[0].length,
		0x04, 0x04);
	CHECK_BYTES("PID Enable Actuators", reports[1].data, reports[1].length,
		0x04, 0x01);
	CHECK_BYTES("PID Device Gain", reports[2].data, reports[2].length,
		0x05, 0xff);
	CHECK(reports[2].type == HID_OUTPUT && reports[2].id == 5);

	hid_report create;
	CHECK(pid_create_report(layout, device, create));
	CHECK(create.type == HID_FEATURE && create.id == 6);
	CHECK_BYTES("PID Create New Effect", create.data, create.length,
		0x06, 0x02, 0x00, 0x00);

	uint8 block = 0;
	const uint8 loaded[] = { 0x07, 0x05, 0x01, 0x00, 0x10 };
	CHECK(pid_parse_block_load(layout, device, loaded, sizeof(loaded), block)
		&& block == 5);
	const uint8 full[] = { 0x07, 0x05, 0x02, 0x00, 0x00 };
	CHECK(!pid_parse_block_load(layout, device, full, sizeof(full), block));
	const uint8 wrongID[] = { 0x06, 0x05, 0x01, 0x00, 0x10 };
	CHECK(!pid_parse_block_load(layout, device, wrongID, sizeof(wrongID),
		block));
	CHECK(!pid_parse_block_load(layout, device, loaded, 4, block));
	const uint8 noIndex[] = { 0x07, 0x00, 0x01, 0x00, 0x10 };
	CHECK(!pid_parse_block_load(layout, device, noIndex, sizeof(noIndex),
		block));

	CHECK(pid_start_reports(layout, device, 1, 0.5f, 500, 60, reports, 2)
		== 0);
	CHECK(pid_start_reports(layout, device, 1, 0.5f, 500, 60, reports, 3)
		== 3);
	CHECK_BYTES("PID Set Effect", reports[0].data, reports[0].length,
		0x01, 0x01, 0x02, 0xf4, 0x01, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
		0x01, 0x00, 0x00);
	CHECK_BYTES("PID Set Periodic", reports[1].data, reports[1].length,
		0x02, 0x01, 0x88, 0x13, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00);
	CHECK_BYTES("PID Start Solo", reports[2].data, reports[2].length,
		0x03, 0x01, 0x02, 0x01);

	// No duration: the longest the field allows; a magnitude above 1 is 1.
	CHECK(pid_start_reports(layout, device, 5, 3.0f, 0, 20, reports, 3)
		== 3);
	CHECK_BYTES("PID Set Effect, no duration", reports[0].data,
		reports[0].length,
		0x01, 0x05, 0x02, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
		0x01, 0x00, 0x00);
	CHECK_BYTES("PID Set Periodic, full", reports[1].data,
		reports[1].length,
		0x02, 0x05, 0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00);

	hid_report stop;
	CHECK(pid_stop_report(layout, device, 5, stop));
	CHECK_BYTES("PID Stop", stop.data, stop.length, 0x03, 0x05, 0x03, 0x01);
}


#include "feel_phase2_test.h"


int
main()
{
	test_crc();
	test_models();
	test_trigger_effects();
	test_dualsense();
	test_dualshock4();
	test_motion();
	test_short_report();
	test_cache();
	test_pid();
	test_switch_kernel();
	test_switch_library();
	test_xbox();
	test_gip();

	if (sFailures != 0) {
		printf("feel_host_test: %d failure%s\n", sFailures,
			sFailures == 1 ? "" : "s");
		return 1;
	}
	printf("feel_host_test: ok\n");
	return 0;
}
