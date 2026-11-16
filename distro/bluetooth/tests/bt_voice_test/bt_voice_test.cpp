/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Test of the voice path (docs/BLUETOOTH.md, step 7c) without a radio,
 * run by distro/bluetooth/tests/run-qemu-smoke.sh:
 *  1. the checks of voice_checks.h (mSBC and SBC through R5's encoder and
 *     decoder, mSBC frames in SCO payloads, the kernel's SCO parsers, the
 *     HCI voice commands), here built for R5;
 *  2. the media add-on lists "Bluetooth headset microphone" as a physical
 *     audio input; the node runs, offers 16 kHz mono float, and is not the
 *     default input unchosen;
 *  3. without bt_voice in the h2generic settings there is no voice node.
 * Prints "bt_voice_test: ok" and exits 0 when all passed.
 */


#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <MediaAddOn.h>
#include <MediaRoster.h>

#include "../../add-ons/media/media-add-ons/bluetooth_audio/msbc.h"
#include "../../add-ons/kernel/drivers/bluetooth/h2/h2generic/h2sco.h"
#include "../../servers/bluetooth/VoiceLink.h"


static int sFailures = 0;


static void
check(bool condition, const char* what)
{
	printf("bt_voice_test: %s %s\n", condition ? "ok" : "FAIL", what);
	if (!condition)
		sFailures++;
}


#include "voice_checks.h"


static void
check_node()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	check(roster != NULL, "media roster");
	if (roster == NULL)
		return;

	dormant_node_info infos[8];
	int32 count = 8;
	status_t status = roster->GetDormantNodes(infos, &count, NULL, NULL,
		"Bluetooth headset microphone", B_BUFFER_PRODUCER | B_PHYSICAL_INPUT,
		0);
	check(status == B_OK && count == 1, "add-on lists \"Bluetooth headset "
		"microphone\" as an audio input");
	if (status != B_OK || count != 1)
		return;

	media_node node;
	bool ownNode = false;
	live_node_info live[8];
	int32 liveCount = 8;
	status = roster->GetLiveNodes(live, &liveCount, NULL, NULL,
		"Bluetooth headset microphone", B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
	if (status == B_OK && liveCount >= 1) {
		node = live[0].node;
		check(liveCount == 1, "media_addon_server runs one microphone node");
	} else {
		status = roster->InstantiateDormantNode(infos[0], &node);
		check(status == B_OK, "microphone node instantiates");
		if (status != B_OK)
			return;
		ownNode = true;
		snooze(500000);
	}

	media_node input;
	if (roster->GetAudioInput(&input) == B_OK)
		check(input.node != node.node, "not the default input unchosen");

	media_output outputs[4];
	int32 outputCount = 0;
	status = roster->GetFreeOutputsFor(node, outputs, 4, &outputCount,
		B_MEDIA_RAW_AUDIO);
	check(status == B_OK && outputCount == 1, "the node has one output");
	if (status == B_OK && outputCount == 1) {
		const media_raw_audio_format& raw = outputs[0].format.u.raw_audio;
		check(raw.frame_rate == 16000.0f && raw.channel_count == 1
			&& raw.format == media_raw_audio_format::B_AUDIO_FLOAT,
			"the output is 16 kHz mono float");
	}

	if (ownNode) {
		status = roster->ReleaseNode(node);
		check(status == B_OK, "microphone node released");
	}
}


static void
check_no_voice_node()
{
	// The image ships no h2generic settings, so bt_voice is off. QEMU has
	// no radio either, so this cannot show that the node comes with it.
	int entries = 0;
	DIR* directory = opendir("/dev/bluetooth/voice");
	if (directory != NULL) {
		struct dirent* entry;
		while ((entry = readdir(directory)) != NULL) {
			if (strcmp(entry->d_name, ".") != 0
				&& strcmp(entry->d_name, "..") != 0)
				entries++;
		}
		closedir(directory);
	}
	check(entries == 0, "no voice node without bt_voice");
}


int
main()
{
	BApplication app("application/x-vnd.R5-bt-voice-test");
	voice_checks::run_all();
	check_node();
	check_no_voice_node();
	if (sFailures != 0) {
		printf("bt_voice_test: %d failures\n", sFailures);
		return 1;
	}
	printf("bt_voice_test: ok\n");
	return 0;
}
