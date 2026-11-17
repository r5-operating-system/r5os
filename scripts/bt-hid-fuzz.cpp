/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the game controller report code
 * (docs/GAME-CONTROLLERS.md, option C phase 1), built with AddressSanitizer
 * and UBSan by scripts/fuzz-bt.sh (target "hid"):
 * - the kernel side, hid_shared's R5HIDReports.h: the short report built
 *   from a PlayStation controller's full one, and the input report cache
 *   that R5_JOYSTICK_GET_REPORT reads, with reports of every length; and
 *   (phase 2) the Switch simple report, battery and start, the Xbox
 *   battery and the GIP packets;
 * - libr5game's GameControllerReports.cpp, which reads what a controller
 *   gives: its HID report descriptor (random bytes, and a PID descriptor
 *   with random bytes changed), the PID reports built from it, the PID
 *   Block Load answer, the motion of an input report, and (phase 2) the
 *   Switch SPI reply, IMU calibration and motion.
 * Every report lives in a heap buffer of exactly its length. Checked
 * besides "no fault": a built report fits the declared length and buffer.
 * Prints "hid_fuzz: <seconds>s 0 crashes" when it ends.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "GameControllerReports.h"
#include "R5HIDReports.h"


using namespace R5GameController;


static uint64_t gState = 0x9e3779b97f4a7c15ULL;


static uint32_t
next_word()
{
	gState ^= gState << 13;
	gState ^= gState >> 7;
	gState ^= gState << 17;
	return (uint32_t)gState;
}


static void
fail(const char* what)
{
	printf("hid_fuzz: FAIL %s\n", what);
	exit(1);
}


// The PID descriptor of distro/game/tests/feel_host_test, shortened to
// the reports rumble needs.
static const uint8 kSeed[] = {
	0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x05, 0x0f,
	0x09, 0x21, 0xa1, 0x02, 0x85, 0x01,
	0x09, 0x22, 0x15, 0x01, 0x25, 0x28, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
	0x09, 0x25, 0xa1, 0x02, 0x09, 0x26, 0x09, 0x31, 0x15, 0x01, 0x25, 0x02,
	0x75, 0x08, 0x95, 0x01, 0x91, 0x00, 0xc0,
	0x09, 0x50, 0x09, 0x53, 0x15, 0x00, 0x26, 0xff, 0x7f, 0x75, 0x10,
	0x95, 0x02, 0x91, 0x02, 0xc0,
	0x09, 0x6e, 0xa1, 0x02, 0x85, 0x02,
	0x09, 0x22, 0x15, 0x01, 0x25, 0x28, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
	0x09, 0x70, 0x09, 0x72, 0x15, 0x00, 0x26, 0x10, 0x27, 0x75, 0x10,
	0x95, 0x02, 0x91, 0x02, 0xc0,
	0x09, 0x77, 0xa1, 0x02, 0x85, 0x03,
	0x09, 0x22, 0x15, 0x01, 0x25, 0x28, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
	0x09, 0x78, 0xa1, 0x02, 0x09, 0x79, 0x09, 0x7a, 0x09, 0x7b, 0x15, 0x01,
	0x25, 0x03, 0x75, 0x08, 0x95, 0x01, 0x91, 0x00, 0xc0, 0xc0,
	0x09, 0x89, 0xa1, 0x02, 0x85, 0x07,
	0x09, 0x22, 0x15, 0x01, 0x25, 0x28, 0x75, 0x08, 0x95, 0x01, 0xb1, 0x02,
	0x09, 0x8b, 0xa1, 0x02, 0x09, 0x8c, 0x09, 0x8d, 0x15, 0x01, 0x25, 0x02,
	0x75, 0x08, 0x95, 0x01, 0xb1, 0x00, 0xc0, 0xc0,
	0xc0
};


static void
check_report(const hid_layout& layout, const hid_report& report)
{
	if (report.length == 0 || report.length > kMaxReportBytes)
		fail("built report length");
	if (report.length != hid_report_length(layout, report.type, report.id))
		fail("built report not the declared length");
}


static void
fuzz_descriptor(const uint8* data, size_t length)
{
	static hid_layout layout;
	uint8* copy = (uint8*)malloc(length > 0 ? length : 1);
	memcpy(copy, data, length);
	bool parsed = parse_hid_descriptor(copy, length, layout);
	free(copy);
	if (!parsed)
		return;
	if (layout.fieldCount > kMaxHIDFields
		|| layout.reportCount > kMaxHIDReports)
		fail("layout counts");

	pid_device device;
	if (!pid_find(layout, device))
		return;

	hid_report reports[3];
	uint32 count = pid_init_reports(layout, device, reports, 3);
	for (uint32 i = 0; i < count; i++)
		check_report(layout, reports[i]);
	hid_report one;
	if (pid_create_report(layout, device, one))
		check_report(layout, one);
	float magnitude = (next_word() % 300) / 100.0f - 1;
	count = pid_start_reports(layout, device, (uint8)next_word(), magnitude,
		next_word() % 100000, next_word() % 1000, reports, 3);
	for (uint32 i = 0; i < count; i++)
		check_report(layout, reports[i]);
	if (pid_stop_report(layout, device, (uint8)next_word(), one))
		check_report(layout, one);

	// A Block Load answer of any length.
	size_t answerLength = next_word() % 16;
	uint8* answer = (uint8*)malloc(answerLength > 0 ? answerLength : 1);
	for (size_t i = 0; i < answerLength; i++)
		answer[i] = (uint8)next_word();
	if (answerLength > 0 && (next_word() & 1))
		answer[0] = device.blockLoad.id;
	uint8 block = 0;
	if (pid_parse_block_load(layout, device, answer, answerLength, block)
		&& block == 0)
		fail("block index 0");
	free(answer);
}


static void
fuzz_report(size_t length)
{
	uint8* report = (uint8*)malloc(length > 0 ? length : 1);
	for (size_t i = 0; i < length; i++)
		report[i] = (uint8)next_word();
	if (length > 0) {
		static const uint8 kIDs[] = { 0x01, 0x11, 0x19, 0x31 };
		if (next_word() & 1)
			report[0] = kIDs[next_word() % 4];
	}

	// The kernel side.
	static const uint16 kProducts[] = { 0x05c4, 0x09cc, 0x0ba0, 0x0ce6,
		0x0df2, 0x0268 };
	uint16 product = kProducts[next_word() % 6];
	size_t capacity = next_word() % 24;
	uint8* out = (uint8*)malloc(capacity > 0 ? capacity : 1);
	size_t built = r5_hid_sony_short_report(0x054c, product, report, length,
		out, capacity);
	if (built > capacity || (built != 0 && built != 10))
		fail("short report length");
	free(out);

	static r5_hid_report_cache cache;
	static bool cacheReady = false;
	if (!cacheReady) {
		r5_hid_cache_init(cache);
		cacheReady = true;
	}
	bool withIDs = (next_word() & 1) != 0;
	r5_hid_cache_store(cache, report, length, withIDs, next_word());
	size_t getCapacity = next_word() % 200;
	uint8* got = (uint8*)malloc(getCapacity > 0 ? getCapacity : 1);
	size_t gotLength = 0;
	bigtime_t when = 0;
	uint8 id = length > 0 && withIDs ? report[0] : 0;
	if (r5_hid_cache_get(cache, id, got, getCapacity, &gotLength, &when)
			== B_OK
		&& (gotLength > getCapacity || gotLength > R5_HID_CACHE_REPORT))
		fail("cache length");
	free(got);

	// The motion parser.
	motion_sample sample;
	parse_playstation_motion((playstation_model)(next_word() % 5),
		(next_word() & 1) != 0, report, length, sample);

	// Phase 2, the kernel side: the Switch simple report, battery and
	// start, the Xbox battery, and GIP packets.
	if (length > 0 && (next_word() & 1)) {
		static const uint8 kSwitchIDs[] = { 0x21, 0x30, 0x31, 0x3f, 0x81,
			0x04, 0x20, 0x07, 0x02 };
		report[0] = kSwitchIDs[next_word() % sizeof(kSwitchIDs)];
	}
	size_t simpleCapacity = next_word() % 16;
	uint8* simple = (uint8*)malloc(simpleCapacity > 0 ? simpleCapacity : 1);
	built = r5_hid_switch_simple_report(report, length, simple,
		simpleCapacity);
	if (built != 0 && (built != R5_HID_SWITCH_SIMPLE_SIZE
			|| built > simpleCapacity))
		fail("switch simple report length");
	free(simple);
	int32 battery = r5_hid_switch_battery(report, length);
	if (battery < -1 || battery > 100)
		fail("switch battery");
	battery = r5_hid_xbox_battery(report, length);
	if (battery < -1 || battery > 100)
		fail("xbox battery");

	static r5_hid_switch_init init;
	if (next_word() % 64 == 0 || init.steps == 0) {
		uint8 first[R5_HID_SWITCH_BT_OUTPUT];
		r5_hid_switch_init_start(init, (next_word() & 1) != 0, first,
			sizeof(first));
	}
	size_t sendCapacity = next_word() % 64;
	uint8* send = (uint8*)malloc(sendCapacity > 0 ? sendCapacity : 1);
	size_t sent = r5_hid_switch_init_input(init, report, length, send,
		sendCapacity);
	if (sent != 0 && (sent != R5_HID_SWITCH_BT_OUTPUT || sent > sendCapacity))
		fail("switch start length");
	free(send);

	static r5_hid_gip_state gip;
	static bool gipReady = false;
	if (!gipReady || next_word() % 64 == 0) {
		r5_hid_gip_init(gip, 0x045e, (next_word() & 1) ? 0x0b12 : 0x02ea);
		gipReady = true;
	}
	r5_hid_gip_sends sends;
	size_t gipCapacity = next_word() % 20;
	uint8* gipOut = (uint8*)malloc(gipCapacity > 0 ? gipCapacity : 1);
	built = r5_hid_gip_input(gip, report, length, gipOut, gipCapacity, sends);
	if (built != 0 && (built != R5_HID_GIP_INPUT_SIZE || built > gipCapacity))
		fail("gip report length");
	if (sends.count > R5_HID_GIP_MAX_SENDS)
		fail("gip sends");
	for (uint32 i = 0; i < sends.count; i++) {
		if (sends.length[i] == 0 || sends.length[i] > R5_HID_GIP_MAX_SEND)
			fail("gip send length");
	}
	free(gipOut);

	// Phase 2, libr5game: the Switch SPI reply, calibration and motion.
	uint8 data[29];
	parse_switch_spi_reply(report, length, next_word() & 0xffff,
		(uint8)(next_word() % 30), data);
	switch_imu_calibration calibration;
	if (length >= 24)
		switch_imu_from_factory(report, calibration);
	else
		switch_imu_nominal(calibration);
	parse_switch_motion(calibration, report, length, sample);
	free(report);
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60;
	if (seconds <= 0)
		seconds = 60;
	gState ^= (uint64_t)time(NULL);
	time_t end = time(NULL) + seconds;
	unsigned long rounds = 0;

	uint8 buffer[512];
	while (time(NULL) < end) {
		for (int i = 0; i < 200; i++) {
			// Random bytes, or the seed with a few bytes changed or cut.
			size_t length;
			if (next_word() & 1) {
				length = next_word() % sizeof(buffer);
				for (size_t j = 0; j < length; j++)
					buffer[j] = (uint8)next_word();
			} else {
				length = sizeof(kSeed);
				memcpy(buffer, kSeed, length);
				int changes = 1 + next_word() % 4;
				for (int j = 0; j < changes; j++)
					buffer[next_word() % length] = (uint8)next_word();
				if (next_word() % 4 == 0)
					length = next_word() % length;
			}
			fuzz_descriptor(buffer, length);
			fuzz_report(next_word() % 140);
			rounds++;
		}
	}

	printf("hid_fuzz: %lu rounds\n", rounds);
	printf("hid_fuzz: %ds 0 crashes\n", seconds);
	return 0;
}
