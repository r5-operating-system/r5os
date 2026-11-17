/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Phase 2 of feel_host_test (docs/GAME-CONTROLLERS.md): the Switch, Xbox
 * and GIP report code of R5HIDReports.h and GameControllerReports.cpp, with
 * exact bytes. Included by feel_host_test.cpp after its helpers.
 */


static void
test_switch_kernel()
{
	CHECK(r5_hid_is_switch_pro(0x057e, 0x2009));
	CHECK(!r5_hid_is_switch_pro(0x057e, 0x2006));
	CHECK(r5_hid_is_joycon(0x057e, 0x2006) && r5_hid_is_joycon(0x057e, 0x2007));
	CHECK(!r5_hid_is_joycon(0x054c, 0x2006));

	uint8 out[64];
	uint8 argument = 0x30;
	CHECK(r5_hid_switch_subcommand(0x13, 0x03, &argument, 1, out, sizeof(out),
		12) == 12);
	CHECK_BYTES("switch subcommand", out, 12, 0x01, 0x03, 0x00, 0x01, 0x40,
		0x40, 0x00, 0x01, 0x40, 0x40, 0x03, 0x30);
	CHECK(r5_hid_switch_subcommand(0, 0x03, &argument, 1, out, sizeof(out),
		11) == 0);
	CHECK(r5_hid_switch_subcommand(0, 0x03, &argument, 1, out, 11, 49) == 0);
	CHECK(r5_hid_switch_subcommand(0, 0x48, &argument, 1, out, sizeof(out),
		49) == 49 && all_zero(out, 12, 49));
	CHECK(r5_hid_switch_usb_command(0x02, out, sizeof(out), 64) == 64);
	CHECK(out[0] == 0x80 && out[1] == 0x02 && all_zero(out, 2, 64));
	CHECK(r5_hid_switch_usb_command(0x02, out, 1, 64) == 0);

	// A full report: Y, B, ZR (right); minus, Home (shared); up, right, L
	// (left); left stick X 0x123, Y 0xfff; right stick X 0, Y 0x800.
	uint8 full[49];
	memset(full, 0, sizeof(full));
	full[0] = 0x30;
	full[2] = 0x90;		// level 8 (full), charging
	full[3] = 0x01 | 0x04 | 0x80;
	full[4] = 0x01 | 0x10;
	full[5] = 0x02 | 0x04 | 0x40;
	full[6] = 0x23;
	full[7] = 0xf1;
	full[8] = 0xff;
	full[9] = 0x00;
	full[10] = 0x00;
	full[11] = 0x80;
	uint8 simple[12];
	CHECK(r5_hid_switch_simple_report(full, sizeof(full), simple,
		sizeof(simple)) == 12);
	// B 0x01, Y 0x04, L 0x10, ZR 0x80; minus 0x01, Home 0x10; hat up-right
	// 1; X 0x1231, Y 0xffff - 0xffff; X 0, Y 0xffff - 0x8008.
	CHECK_BYTES("switch simple", simple, 12, 0x3f, 0x95, 0x11, 0x01,
		0x31, 0x12, 0x00, 0x00, 0x00, 0x00, 0xf7, 0x7f);
	CHECK(r5_hid_switch_battery(full, sizeof(full)) == 100);
	full[2] = 0x40;
	CHECK(r5_hid_switch_battery(full, sizeof(full)) == 50);
	full[2] = 0x00;
	CHECK(r5_hid_switch_battery(full, sizeof(full)) == 0);
	CHECK(r5_hid_switch_battery(full, 2) == -1);
	full[0] = 0x3f;
	CHECK(r5_hid_switch_battery(full, sizeof(full)) == -1);
	CHECK(r5_hid_switch_simple_report(full, sizeof(full), simple,
		sizeof(simple)) == 0);
	full[0] = 0x21;
	CHECK(r5_hid_switch_simple_report(full, 11, simple, sizeof(simple)) == 0);
	CHECK(r5_hid_switch_simple_report(full, 12, simple, 11) == 0);
	// Opposite directions cancel; nothing is the null hat 8.
	full[5] = 0x01 | 0x02 | 0x04 | 0x08;
	CHECK(r5_hid_switch_simple_report(full, 12, simple, sizeof(simple)) == 12
		&& simple[3] == 8);
	full[5] = 0x01 | 0x08;	// down left
	CHECK(r5_hid_switch_simple_report(full, 12, simple, sizeof(simple)) == 12
		&& simple[3] == 5);

	// The start over Bluetooth: rumble on, full mode, player 1, each after
	// its reply; a missing reply resends, three times, then skips.
	r5_hid_switch_init init;
	uint8 send[64];
	CHECK(r5_hid_switch_init_start(init, true, send, sizeof(send)) == 49);
	CHECK(send[0] == 0x01 && send[1] == 0 && send[10] == 0x48
		&& send[11] == 0x01);
	uint8 reply[49];
	memset(reply, 0, sizeof(reply));
	reply[0] = 0x21;
	reply[13] = 0x80;
	reply[14] = 0x48;
	CHECK(r5_hid_switch_init_input(init, reply, sizeof(reply), send,
		sizeof(send)) == 49);
	CHECK(send[1] == 1 && send[10] == 0x03 && send[11] == 0x30);
	// The same reply again is no answer to 0x03.
	CHECK(r5_hid_switch_init_input(init, reply, sizeof(reply), send,
		sizeof(send)) == 0);
	reply[14] = 0x03;
	CHECK(r5_hid_switch_init_input(init, reply, sizeof(reply), send,
		sizeof(send)) == 49);
	CHECK(send[10] == 0x30 && send[11] == 0x01);
	// No reply: after kR5SwitchInitWait reports it is sent again.
	size_t resent = 0;
	for (uint16 i = 0; i < kR5SwitchInitWait; i++)
		resent += r5_hid_switch_init_input(init, full, sizeof(full), send,
			sizeof(send));
	CHECK(resent == 49 && send[10] == 0x30);
	reply[14] = 0x30;
	CHECK(r5_hid_switch_init_input(init, reply, sizeof(reply), send,
		sizeof(send)) == 0);
	CHECK(init.done);
	CHECK(r5_hid_switch_init_input(init, reply, sizeof(reply), send,
		sizeof(send)) == 0);
	// A Joy-Con: no full mode; each step without any reply is sent three
	// times, then the next one.
	CHECK(r5_hid_switch_init_start(init, false, send, sizeof(send)) == 49);
	size_t sent = 0;
	for (int i = 0; i < 6 * kR5SwitchInitWait; i++)
		sent += r5_hid_switch_init_input(init, NULL, 0, send, sizeof(send));
	CHECK(sent == 5 * 49 && init.done);

	// The R5 descriptors parse, with the sizes the drivers rely on.
	static hid_layout layout;
	CHECK(parse_hid_descriptor(r5_hid_switch_usb_descriptor,
		sizeof(r5_hid_switch_usb_descriptor), layout));
	CHECK(layout.usesReportIDs);
	CHECK(hid_report_length(layout, HID_INPUT, 0x3f) == 12);
	CHECK(hid_report_length(layout, HID_INPUT, 0x30) == 64);
	CHECK(hid_report_length(layout, HID_OUTPUT, 0x01) == 64);
	CHECK(hid_report_length(layout, HID_OUTPUT, 0x10) == 64);
	CHECK(hid_report_length(layout, HID_OUTPUT, 0x80) == 64);
	CHECK(parse_hid_descriptor(r5_hid_gip_descriptor,
		sizeof(r5_hid_gip_descriptor), layout));
	CHECK(hid_report_length(layout, HID_INPUT, 0x20)
		== R5_HID_GIP_INPUT_SIZE);
	CHECK(hid_report_length(layout, HID_OUTPUT, 0x09)
		== R5_HID_GIP_RUMBLE_SIZE);
	CHECK(hid_report_length(layout, HID_INPUT, 0xfe) == 64);
}


static void
test_switch_library()
{
	CHECK(switch_model_for(0x057e, 0x2009) == SWITCH_PRO);
	CHECK(switch_model_for(0x057e, 0x2006) == SWITCH_JOYCON_LEFT);
	CHECK(switch_model_for(0x057e, 0x2007) == SWITCH_JOYCON_RIGHT);
	CHECK(switch_model_for(0x057e, 0x2017) == SWITCH_NONE);
	CHECK(switch_model_for(0x045e, 0x2009) == SWITCH_NONE);

	// The amplitude steps: 1.0 is 100, 0.5 is 68 (SDL's table: 32768 lies
	// in the step that ends at 32861, 0x88 / 2), 0.231 is 32, 12 % and
	// less a straight line.
	CHECK(switch_amplitude_index(0) == 0);
	CHECK(switch_amplitude_index(-1) == 0);
	CHECK(switch_amplitude_index(NAN) == 0);
	CHECK(switch_amplitude_index(1) == 100);
	CHECK(switch_amplitude_index(2) == 100);
	CHECK(switch_amplitude_index(0.5f) == 68);
	CHECK(switch_amplitude_index(0.231f) == 32);
	CHECK(switch_amplitude_index(0.06f) == 8);
	CHECK(switch_amplitude_index(0.001f) == 1);
	uint8 last = 0;
	bool rising = true;
	for (int i = 0; i <= 1000; i++) {
		uint8 index = switch_amplitude_index(i / 1000.0f);
		if (index < last)
			rising = false;
		last = index;
	}
	CHECK(rising);

	uint8 rumble[4];
	switch_rumble_encode(0, 0, rumble);
	CHECK_BYTES("neutral rumble", rumble, 4, 0x00, 0x01, 0x40, 0x40);
	switch_rumble_encode(1, 1, rumble);
	CHECK_BYTES("full rumble", rumble, 4, 0x74, 0xc8, 0x3d, 0x72);
	switch_rumble_encode(0.5f, 0, rumble);
	CHECK_BYTES("strong rumble", rumble, 4, 0x74, 0x00, 0x3d, 0x62);
	switch_rumble_encode(0, 0.001f, rumble);
	CHECK_BYTES("faint rumble", rumble, 4, 0x74, 0x02, 0x3d, 0x40);
	switch_rumble_encode(0.001f, 0, rumble);
	CHECK_BYTES("faint low", rumble, 4, 0x74, 0x00, 0xbd, 0x40);

	uint8 report[64];
	CHECK(build_switch_rumble(0x1f, 1, 1, 49, report, sizeof(report)) == 49);
	CHECK_BYTES("switch rumble", report, 10, 0x10, 0x0f, 0x74, 0xc8, 0x3d,
		0x72, 0x74, 0xc8, 0x3d, 0x72);
	CHECK(all_zero(report, 10, 49));
	CHECK(build_switch_rumble(0, 1, 1, 9, report, sizeof(report)) == 0);
	CHECK(build_switch_rumble(0, 1, 1, 64, report, 63) == 0);
	uint8 on = 0x01;
	CHECK(build_switch_subcommand(2, 0, 0, 0x40, &on, 1, 64, report,
		sizeof(report)) == 64);
	CHECK_BYTES("imu on", report, 12, 0x01, 0x02, 0x00, 0x01, 0x40, 0x40,
		0x00, 0x01, 0x40, 0x40, 0x40, 0x01);
	CHECK(build_switch_subcommand(2, 0, 0, 0x40, NULL, 1, 64, report,
		sizeof(report)) == 0);
	CHECK(build_switch_spi_read(3, 0, 0, 0x6020, 24, 49, report,
		sizeof(report)) == 49);
	CHECK_BYTES("spi read", report, 16, 0x01, 0x03, 0x00, 0x01, 0x40, 0x40,
		0x00, 0x01, 0x40, 0x40, 0x10, 0x20, 0x60, 0x00, 0x00, 0x18);
	CHECK(build_switch_spi_read(3, 0, 0, 0x6020, 30, 49, report,
		sizeof(report)) == 0);

	// The reply: ACK 0x90, subcommand 0x10, address, size, data.
	uint8 reply[49];
	memset(reply, 0, sizeof(reply));
	reply[0] = 0x21;
	reply[13] = 0x90;
	reply[14] = 0x10;
	reply[15] = 0x20;
	reply[16] = 0x60;
	reply[19] = 24;
	// Factory: accelerometer origin 0, sensitivity 16384 (0x4000); gyro
	// origin 10, -20, 30, sensitivity 13371 (0x343b) above the origin.
	static const uint8 kCalibration[24] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x40, 0x00, 0x40, 0x00, 0x40,
		0x0a, 0x00, 0xec, 0xff, 0x1e, 0x00,
		0x45, 0x34, 0x27, 0x34, 0x59, 0x34 };
	memcpy(reply + 20, kCalibration, 24);
	uint8 data[24];
	CHECK(parse_switch_spi_reply(reply, sizeof(reply), 0x6020, 24, data));
	CHECK(memcmp(data, kCalibration, 24) == 0);
	CHECK(!parse_switch_spi_reply(reply, 43, 0x6020, 24, data));
	CHECK(!parse_switch_spi_reply(reply, sizeof(reply), 0x6021, 24, data));
	CHECK(!parse_switch_spi_reply(reply, sizeof(reply), 0x6020, 23, data));
	reply[13] = 0x00;
	CHECK(!parse_switch_spi_reply(reply, sizeof(reply), 0x6020, 24, data));

	switch_imu_calibration calibration;
	CHECK(switch_imu_from_factory(kCalibration, calibration));
	CHECK(calibration.factory);
	CHECK(fabsf(calibration.accelScale[0] - 4.0f / 16384) < 1e-9f);
	CHECK(fabsf(calibration.gyroScale[1] - 936.0f / 13371) < 1e-6f);
	CHECK(calibration.gyroOffset[0] == 10 && calibration.gyroOffset[1] == -20
		&& calibration.gyroOffset[2] == 30);
	uint8 blank[24];
	memset(blank, 0xff, sizeof(blank));
	CHECK(!switch_imu_from_factory(blank, calibration));
	CHECK(!calibration.factory
		&& fabsf(calibration.accelScale[2] - 1.0f / 4096) < 1e-9f);

	// Motion: the newest sample; lying flat the accelerometer reads +Z,
	// which is +Y in the PlayStation axes; a turn around the gyroscope's Z
	// is yaw.
	uint8 full[49];
	memset(full, 0, sizeof(full));
	full[0] = 0x30;
	motion_sample sample;
	switch_imu_nominal(calibration);
	CHECK(!parse_switch_motion(calibration, full, sizeof(full), sample));
	full[13 + 5] = 0x10;	// first sample Z 4096
	uint8* newest = full + 13 + 24;
	newest[0] = 0x00;
	newest[1] = 0x08;		// X 2048: half a g
	newest[4] = 0x00;
	newest[5] = 0x10;		// Z 4096: one g
	newest[10] = 0x8e;
	newest[11] = 0x00;		// gyroscope Z 142: about 10 degrees a second
	CHECK(parse_switch_motion(calibration, full, sizeof(full), sample));
	CHECK(fabsf(sample.acceleration[0]) < 1e-6f);
	CHECK(fabsf(sample.acceleration[1] - 1) < 1e-4f);
	CHECK(fabsf(sample.acceleration[2] + 0.5f) < 1e-4f);
	CHECK(fabsf(sample.rotation[1] - 142 / 14.2842f) < 1e-3f);
	CHECK(fabsf(sample.rotation[0]) < 1e-6f
		&& fabsf(sample.rotation[2]) < 1e-6f);
	CHECK(!parse_switch_motion(calibration, full, 48, sample));
	full[0] = 0x21;
	CHECK(!parse_switch_motion(calibration, full, sizeof(full), sample));
}


static void
test_xbox()
{
	CHECK(r5_hid_is_xbox_bluetooth(0x045e, 0x0b13));
	CHECK(r5_hid_is_xbox_bluetooth(0x045e, 0x02fd));
	CHECK(!r5_hid_is_xbox_bluetooth(0x045e, 0x0b12));
	CHECK(!r5_hid_is_xbox_bluetooth(0x054c, 0x0b13));
	CHECK(xbox_model_for(0x045e, 0x0b13, true, false) == XBOX_BLUETOOTH);
	CHECK(xbox_model_for(0x045e, 0x0b13, false, false) == XBOX_NONE);
	CHECK(xbox_model_for(0x045e, 0x0b12, false, true) == XBOX_GIP);
	CHECK(xbox_model_for(0x0e6f, 0x0246, false, true) == XBOX_GIP);
	CHECK(xbox_has_trigger_rumble(0x045e) && !xbox_has_trigger_rumble(0x0e6f));

	uint8 battery[2] = { 0x04, 0x02 };
	CHECK(r5_hid_xbox_battery(battery, 2) == 70);
	battery[1] = 0x0f;
	CHECK(r5_hid_xbox_battery(battery, 2) == 100);
	battery[1] = 0x00;
	CHECK(r5_hid_xbox_battery(battery, 2) == 10);
	CHECK(r5_hid_xbox_battery(battery, 1) == -1);
	battery[0] = 0x01;
	CHECK(r5_hid_xbox_battery(battery, 2) == -1);

	uint8 report[16];
	CHECK(build_xbox_bluetooth_rumble(0.25f, 1, 0.5f, 2, report, 9) == 9);
	CHECK_BYTES("xbox bluetooth rumble", report, 9, 0x03, 0x0f, 25, 100, 50,
		100, 0xff, 0x00, 0xeb);
	CHECK(build_xbox_bluetooth_rumble(0, 0, 0, 0, report, 8) == 0);
	CHECK(build_xbox_gip_rumble(0x41, 0, 0, 1, 0.3f, report, 13) == 13);
	CHECK_BYTES("xbox gip rumble", report, 13, 0x09, 0x00, 0x41, 0x09, 0x00,
		0x0f, 0, 0, 100, 30, 0xff, 0x00, 0xeb);
	CHECK(build_xbox_gip_rumble(0x41, 0, 0, 1, 0.3f, report, 12) == 0);
}


static void
test_gip()
{
	r5_hid_gip_header header;
	const uint8 small[] = { 0x20, 0x00, 0x05, 0x0e };
	CHECK(r5_hid_gip_decode(small, sizeof(small), header));
	CHECK(header.command == 0x20 && header.sequence == 5
		&& header.length == 14 && header.headerLength == 4);
	// Length 200 in two bytes (0xc8 0x01), a chunk offset 3.
	const uint8 chunked[] = { 0x04, 0xf0, 0x01, 0xc8, 0x01, 0x03 };
	CHECK(r5_hid_gip_decode(chunked, sizeof(chunked), header));
	CHECK(header.length == 200 && header.chunkOffset == 3
		&& header.headerLength == 6);
	CHECK(!r5_hid_gip_decode(chunked, 5, header));
	CHECK(!r5_hid_gip_decode(small, 3, header));
	const uint8 endless[] = { 0x20, 0x00, 0x01, 0x80, 0x80, 0x80, 0x80,
		0x01 };
	CHECK(!r5_hid_gip_decode(endless, sizeof(endless), header));

	r5_hid_gip_state state;
	r5_hid_gip_init(state, 0x045e, 0x0b12);
	r5_hid_gip_sends sends;
	sends.count = 0;
	r5_hid_gip_start(state, sends);
	CHECK(sends.count == 4);
	CHECK_BYTES("gip power on", sends.data[0], sends.length[0], 0x05, 0x20,
		0x01, 0x01, 0x00);
	CHECK_BYTES("gip light", sends.data[1], sends.length[1], 0x0a, 0x20,
		0x02, 0x03, 0x00, 0x01, 0x14);
	CHECK_BYTES("gip security", sends.data[2], sends.length[2], 0x06, 0x20,
		0x03, 0x02, 0x01, 0x00);
	CHECK_BYTES("gip motors", sends.data[3], sends.length[3], 0x09, 0x00,
		0x04, 0x09, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xeb);

	uint8 out[R5_HID_GIP_INPUT_SIZE];
	// The announcement: the start again, no report.
	uint8 announce[32];
	memset(announce, 0, sizeof(announce));
	announce[0] = 0x02;
	announce[1] = 0x20;
	announce[2] = 0x01;
	announce[3] = 0x1c;
	CHECK(r5_hid_gip_input(state, announce, sizeof(announce), out,
		sizeof(out), sends) == 0);
	CHECK(sends.count == 4 && sends.data[0][0] == 0x05);

	// The input: A, Menu; up, LB; LT 1023, RT 100; LX 1000, LY 2000,
	// RX -1, RY -32768; Share on the Series controller.
	uint8 input[4 + 32];
	memset(input, 0, sizeof(input));
	input[0] = 0x20;
	input[1] = 0x00;
	input[2] = 0x07;
	input[3] = 32;
	uint8* p = input + 4;
	p[0] = 0x10 | 0x04;
	p[1] = 0x01 | 0x10;
	p[2] = 0xff;
	p[3] = 0x03;
	p[4] = 100;
	p[5] = 0;
	p[6] = 0xe8;
	p[7] = 0x03;
	p[8] = 0xd0;
	p[9] = 0x07;
	p[10] = 0xff;
	p[11] = 0xff;
	p[12] = 0x00;
	p[13] = 0x80;
	p[14] = 0x01;
	// The first input after the start is dropped.
	CHECK(r5_hid_gip_input(state, input, sizeof(input), out, sizeof(out),
		sends) == 0);
	CHECK(r5_hid_gip_input(state, input, sizeof(input), out, sizeof(out),
		sends) == R5_HID_GIP_INPUT_SIZE);
	CHECK(sends.count == 0);
	// Buttons A 0x0001, LB 0x0010, LT 0x0040, Menu 0x0200, Share 0x2000;
	// hat up 0; LY 2000 becomes ~2000 = -2001 (0xf82f), RY -32768 32767.
	CHECK_BYTES("gip report", out, 16, 0x20, 0x51, 0x22, 0x00,
		0xe8, 0x03, 0x2f, 0xf8, 0xff, 0xff, 0xff, 0x7f,
		0xff, 0x03, 100, 0x00);

	// The guide button, with an acknowledgement asked for.
	const uint8 guide[] = { 0x07, 0x30, 0x09, 0x02, 0x01, 0x5b };
	CHECK(r5_hid_gip_input(state, guide, sizeof(guide), out, sizeof(out),
		sends) == R5_HID_GIP_INPUT_SIZE);
	CHECK((out[2] & 0x10) != 0 && out[1] == 0x51);
	CHECK(sends.count == 1);
	CHECK_BYTES("gip ack", sends.data[0], sends.length[0], 0x01, 0x20, 0x09,
		0x09, 0x00, 0x07, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00);
	// The next input keeps the guide button down.
	CHECK(r5_hid_gip_input(state, input, sizeof(input), out, sizeof(out),
		sends) == R5_HID_GIP_INPUT_SIZE && (out[2] & 0x10) != 0);
	const uint8 released[] = { 0x07, 0x20, 0x0a, 0x02, 0x00, 0x5b };
	CHECK(r5_hid_gip_input(state, released, sizeof(released), out,
		sizeof(out), sends) == R5_HID_GIP_INPUT_SIZE && (out[2] & 0x10) == 0);

	// Too short, from an accessory, or without room: nothing.
	CHECK(r5_hid_gip_input(state, input, 4 + 13, out, sizeof(out), sends)
		== 0);
	input[1] = 0x01;
	CHECK(r5_hid_gip_input(state, input, sizeof(input), out, sizeof(out),
		sends) == 0);
	input[1] = 0x00;
	CHECK(r5_hid_gip_input(state, input, sizeof(input), out, 15, sends) == 0);
	// The Xbox One controller has no Share button.
	r5_hid_gip_init(state, 0x045e, 0x02ea);
	state.started = true;
	CHECK(r5_hid_gip_input(state, input, sizeof(input), out, sizeof(out),
		sends) == R5_HID_GIP_INPUT_SIZE && (out[2] & 0x20) == 0);
}
