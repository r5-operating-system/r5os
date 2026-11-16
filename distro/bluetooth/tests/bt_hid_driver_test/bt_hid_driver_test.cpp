/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Step 4 of docs/BLUETOOTH.md without a radio: feeds the bluetooth_hid
 * driver the way bluetooth_server does (as uid 0) and checks what a
 * program sees. A gamepad descriptor must publish joystick/bluetooth/N and
 * BJoystick must read its button and axis; a mouse descriptor must publish
 * input/mouse/bluetooth/N; both nodes must go again on destroy.
 * Prints "bt_hid_driver_test: ok" or the first failure.
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Joystick.h>
#include <OS.h>

#include <R5BluetoothHID.h>


// 8 buttons, then X and Y as 0..255 (HID 1.11 with the Generic Desktop
// and Button usage pages); no report ID.
static const uint8 kGamepad[] = {
	0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00,
	0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
	0xC0
};

// Three buttons and relative X and Y, the boot mouse layout.
static const uint8 kMouse[] = {
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
	0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F,
	0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
	0xC0, 0xC0
};


static int sControl = -1;
static int32 sPadID = -1;
static volatile bool sFeeding = true;


static int
fail(const char* what)
{
	printf("bt_hid_driver_test: FAIL %s (%s)\n", what, strerror(errno));
	return 1;
}


static bool
wait_for_node(const char* path, bool present)
{
	for (int i = 0; i < 40; i++) {
		struct stat st;
		if ((stat(path, &st) == 0) == present)
			return true;
		snooze(50000);
	}
	return false;
}


static status_t
feed_thread(void*)
{
	// Button 1 down, X right, Y centered, again and again: the joystick
	// handler takes the latest report.
	uint8 report[3] = { 0x01, 0xFF, 0x80 };
	while (sFeeding) {
		r5_bthid_input input = { sPadID, sizeof(report), report };
		ioctl(sControl, R5_BTHID_INPUT, &input, sizeof(input));
		snooze(20000);
	}
	return B_OK;
}


static int32
create(const uint8* descriptor, size_t length, uint32* handlers)
{
	r5_bthid_create request;
	memset(&request, 0, sizeof(request));
	request.descriptor = descriptor;
	request.descriptor_length = length;
	if (ioctl(sControl, R5_BTHID_CREATE, &request, sizeof(request)) != 0)
		return -1;
	*handlers = request.handlers;
	return request.id;
}


int
main()
{
	sControl = open("/dev/" R5_BTHID_CONTROL_NAME, O_RDWR);
	if (sControl < 0)
		return fail("open control node");

	// A descriptor that is too large and one that is empty are refused.
	r5_bthid_create bad;
	memset(&bad, 0, sizeof(bad));
	bad.descriptor = kGamepad;
	bad.descriptor_length = R5_BTHID_MAX_DESCRIPTOR + 1;
	if (ioctl(sControl, R5_BTHID_CREATE, &bad, sizeof(bad)) == 0)
		return fail("oversized descriptor accepted");
	bad.descriptor_length = 0;
	if (ioctl(sControl, R5_BTHID_CREATE, &bad, sizeof(bad)) == 0)
		return fail("empty descriptor accepted");
	// A kernel address as the descriptor is refused (SMAP rule).
	bad.descriptor = (const uint8*)(addr_t)0xffffffff80000000ULL;
	bad.descriptor_length = sizeof(kGamepad);
	if (ioctl(sControl, R5_BTHID_CREATE, &bad, sizeof(bad)) == 0)
		return fail("kernel address accepted");

	uint32 handlers = 0;
	sPadID = create(kGamepad, sizeof(kGamepad), &handlers);
	if (sPadID < 0)
		return fail("create gamepad");

	char padPath[64] = "";
	for (int i = 0; i < 4 && padPath[0] == '\0'; i++) {
		char candidate[64];
		snprintf(candidate, sizeof(candidate), "/dev/joystick/bluetooth/%d",
			i);
		if (wait_for_node(candidate, true))
			strcpy(padPath, candidate);
	}
	if (padPath[0] == '\0')
		return fail("joystick/bluetooth node not published");
	printf("bt_hid_driver_test: gamepad at %s (%" B_PRIu32 " handler%s)\n",
		padPath, handlers, handlers == 1 ? "" : "s");

	thread_id feeder = spawn_thread(feed_thread, "feed", B_NORMAL_PRIORITY,
		NULL);
	resume_thread(feeder);

	// Old programs find it the way they find a USB pad: in the list of
	// BJoystick devices, by its name under /dev/joystick.
	BJoystick stick;
	const char* padName = padPath + strlen("/dev/joystick/");
	bool listed = false;
	int32 count = stick.CountDevices();
	for (int32 i = 0; i < count && !listed; i++) {
		char name[B_OS_NAME_LENGTH];
		if (stick.GetDeviceName(i, name, sizeof(name)) == B_OK)
			listed = strcmp(name, padName) == 0;
	}
	if (!listed) {
		sFeeding = false;
		return fail("not in BJoystick::CountDevices()");
	}
	printf("bt_hid_driver_test: listed by BJoystick as %s (%" B_PRId32
		" devices)\n", padName, count);

	if (stick.Open(padName, true) < 0) {
		sFeeding = false;
		return fail("BJoystick open");
	}
	bool pressed = false;
	int16 axes[8];
	for (int i = 0; i < 50 && !pressed; i++) {
		snooze(40000);
		if (stick.Update() != B_OK)
			continue;
		uint32 buttons = stick.ButtonValues();
		if (stick.CountAxes() >= 2 && stick.CountAxes() <= 8)
			stick.GetAxisValues(axes);
		pressed = (buttons & 1) != 0;
	}
	int32 axisCount = stick.CountAxes();
	int32 buttonCount = stick.CountButtons();
	stick.Close();
	sFeeding = false;
	status_t result;
	wait_for_thread(feeder, &result);
	if (!pressed)
		return fail("BJoystick did not see button 1");
	printf("bt_hid_driver_test: BJoystick %" B_PRId32 " axes, %" B_PRId32
		" buttons, button 1 down, X %d\n", axisCount, buttonCount,
		(int)axes[0]);
	if (axes[0] <= 0)
		return fail("X axis not to the right");

	uint32 mouseHandlers = 0;
	int32 mouseID = create(kMouse, sizeof(kMouse), &mouseHandlers);
	if (mouseID < 0)
		return fail("create mouse");
	if (!wait_for_node("/dev/input/mouse/bluetooth/0", true))
		return fail("input/mouse/bluetooth/0 not published");
	printf("bt_hid_driver_test: mouse at /dev/input/mouse/bluetooth/0\n");

	if (ioctl(sControl, R5_BTHID_DESTROY, &sPadID, sizeof(sPadID)) != 0
		|| ioctl(sControl, R5_BTHID_DESTROY, &mouseID, sizeof(mouseID)) != 0)
		return fail("destroy");
	if (!wait_for_node(padPath, false)
		|| !wait_for_node("/dev/input/mouse/bluetooth/0", false))
		return fail("nodes still there after destroy");

	close(sControl);
	printf("bt_hid_driver_test: ok\n");
	return 0;
}
