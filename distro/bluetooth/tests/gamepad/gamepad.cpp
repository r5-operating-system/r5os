/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * gamepad: shows what a game controller sends, through BJoystick, the way
 * a game reads it. Haiku's Joysticks preferences look for a description
 * file per gameport model and do not show a HID controller (USB or
 * Bluetooth), so they are no test for one.
 *
 * Usage: gamepad [seconds]   (default: until Ctrl+C, like a MIDI monitor)
 * Lists the controllers, opens the first one, and prints a line each time
 * a button or an axis changes.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Joystick.h>
#include <OS.h>


int
main(int argc, char** argv)
{
	const int seconds = argc > 1 ? atoi(argv[1]) : 0;

	BJoystick stick;
	const int32 count = stick.CountDevices();
	if (count <= 0) {
		printf("gamepad: no controller found (is it paired and on?)\n");
		return 1;
	}
	char name[B_OS_NAME_LENGTH];
	for (int32 i = 0; i < count; i++) {
		if (stick.GetDeviceName(i, name, sizeof(name)) == B_OK)
			printf("gamepad: controller %" B_PRId32 ": %s\n", i, name);
	}
	if (stick.GetDeviceName(0, name, sizeof(name)) != B_OK
		|| stick.Open(name, true) < 0) {
		printf("gamepad: cannot open %s\n", name);
		return 1;
	}

	const int32 axes = stick.CountAxes();
	const int32 buttons = stick.CountButtons();
	printf("gamepad: %s, %" B_PRId32 " axes, %" B_PRId32 " buttons, %" B_PRId32
		" hats. Press buttons and move the sticks%s.\n", name, axes,
		buttons, stick.CountHats(), seconds > 0 ? "" : " (Ctrl+C ends)");

	int16* axis = new int16[axes > 0 ? axes : 1];
	int16* lastAxis = new int16[axes > 0 ? axes : 1];
	memset(lastAxis, 0, sizeof(int16) * (axes > 0 ? axes : 1));
	uint32 lastButtons = 0;
	int changes = 0;

	const bigtime_t end = seconds > 0
		? system_time() + (bigtime_t)seconds * 1000000 : B_INFINITE_TIMEOUT;
	while (system_time() < end) {
		if (stick.Update() != B_OK) {
			snooze(10000);
			continue;
		}
		const uint32 pressed = stick.ButtonValues();
		if (pressed != lastButtons) {
			printf("gamepad: buttons %#06" B_PRIx32 "\n", pressed);
			fflush(stdout);
			lastButtons = pressed;
			changes++;
		}
		if (axes > 0 && stick.GetAxisValues(axis) == B_OK) {
			for (int32 i = 0; i < axes; i++) {
				// Only a real move, not the noise around the middle.
				if (abs(axis[i] - lastAxis[i]) > 4000) {
					printf("gamepad: axis %" B_PRId32 " %d\n", i, axis[i]);
					fflush(stdout);
					lastAxis[i] = axis[i];
					changes++;
				}
			}
		}
		snooze(10000);
	}

	printf("gamepad: %d change%s seen\n", changes, changes == 1 ? "" : "s");
	delete[] axis;
	delete[] lastAxis;
	stick.Close();
	return changes > 0 ? 0 : 2;
}
