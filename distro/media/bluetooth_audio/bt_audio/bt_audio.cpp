/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * bt_audio: the headset bluetooth_audio plays to, for the person who runs
 * it (~/config/settings/Media/bluetooth_audio, found with find_directory).
 *	bt_audio			show the choice
 *	bt_audio use <address>	play to that headset (XX:XX:XX:XX:XX:XX)
 *	bt_audio off		no choice: any paired headset that connects plays
 * Choose "Bluetooth audio" as the audio output in Media too.
 */


#include <stdio.h>
#include <string.h>

#include <bluetooth/bdaddrUtils.h>

#include "AudioSettings.h"


static bool
parse_address(const char* text, bdaddr_t* address)
{
	unsigned int b[6];
	char end;
	if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%c", &b[0], &b[1], &b[2], &b[3],
			&b[4], &b[5], &end) != 6)
		return false;
	// bdaddr_t is stored least significant byte first.
	for (int i = 0; i < 6; i++)
		address->b[5 - i] = (uint8)b[i];
	return true;
}


int
main(int argc, char** argv)
{
	BPath path;
	if (bluetooth_audio_own_settings_path(path) != B_OK) {
		fprintf(stderr, "bt_audio: no settings directory\n");
		return 1;
	}

	if (argc == 1) {
		bdaddr_t device;
		if (bluetooth_audio_read_device(path, &device) == B_OK) {
			printf("Bluetooth audio plays to %s\n",
				bdaddrUtils::ToString(device).String());
		} else
			printf("No headset chosen: any paired headset that connects "
				"plays\n");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "off") == 0) {
		status_t status = bluetooth_audio_write_device(path, NULL);
		if (status != B_OK) {
			fprintf(stderr, "bt_audio: %s\n", strerror(status));
			return 1;
		}
		return 0;
	}

	bdaddr_t device;
	if (argc == 3 && strcmp(argv[1], "use") == 0
		&& parse_address(argv[2], &device)) {
		status_t status = bluetooth_audio_write_device(path, &device);
		if (status != B_OK) {
			fprintf(stderr, "bt_audio: %s\n", strerror(status));
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "usage: bt_audio [use XX:XX:XX:XX:XX:XX | off]\n");
	return 2;
}
