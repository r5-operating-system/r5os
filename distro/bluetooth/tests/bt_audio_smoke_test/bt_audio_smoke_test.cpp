/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Smoke test of bluetooth_audio (docs/BLUETOOTH.md, step 5) without a
 * radio, run by distro/bluetooth/tests/run-qemu-smoke.sh:
 *  1. the media add-on lists "Bluetooth audio" as a physical audio output;
 *  2. media_addon_server runs the node from boot (or the test starts it),
 *     it is not the default output unless chosen, and it holds PSM 0x19
 *     (AVDTP) and 0x17 (AVCTP); a node the test started lets go of them
 *     when it is released;
 *  3. its volume parameter takes a value and gives it back;
 *  4. bluetooth_server answers the two audio messages (paired, streaming).
 * Prints "bt_audio_smoke_test: ok" and exits 0 when all passed.
 */


#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <Application.h>
#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <Messenger.h>
#include <ParameterWeb.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetoothserver_p.h>

#include <R5Admin.h>


static int sFailures = 0;


static void
check(bool condition, const char* what)
{
	printf("bt_audio_smoke_test: %s %s\n", condition ? "ok" : "FAIL", what);
	if (!condition)
		sFailures++;
}


// 0 when the PSM was free (and is free again), else the bind error.
static int
try_bind(uint16 psm)
{
	int fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (fd < 0)
		return errno;
	struct sockaddr_l2cap address;
	memset(&address, 0, sizeof(address));
	address.l2cap_len = sizeof(address);
	address.l2cap_family = AF_BLUETOOTH;
	address.l2cap_bdaddr = BDADDR_ANY;
	address.l2cap_psm = B_HOST_TO_LENDIAN_INT16(psm);
	int result = bind(fd, (struct sockaddr*)&address, sizeof(address)) == 0
		? 0 : errno;
	close(fd);
	return result;
}


static void
check_node()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	check(roster != NULL, "media roster");
	if (roster == NULL)
		return;

	dormant_node_info infos[32];
	int32 count = 32;
	status_t status = roster->GetDormantNodes(infos, &count, NULL, NULL,
		"Bluetooth audio", B_BUFFER_CONSUMER | B_PHYSICAL_OUTPUT, 0);
	check(status == B_OK && count == 1,
		"add-on lists \"Bluetooth audio\" as an audio output");
	if (status != B_OK || count != 1)
		return;

	// media_addon_server starts every physical output itself at boot, so
	// the node is live already; only without it does the test start one.
	media_node node;
	bool ownNode = false;
	live_node_info live[8];
	int32 liveCount = 8;
	status = roster->GetLiveNodes(live, &liveCount, NULL, NULL,
		"Bluetooth audio", B_BUFFER_CONSUMER | B_PHYSICAL_OUTPUT);
	if (status == B_OK && liveCount >= 1) {
		node = live[0].node;
		check(liveCount == 1, "media_addon_server runs one node");
	} else {
		status = roster->InstantiateDormantNode(infos[0], &node);
		check(status == B_OK, "node instantiates");
		if (status != B_OK)
			return;
		ownNode = true;
		snooze(500000);
	}

	// Never the default output by itself (DefaultManager).
	media_node output;
	if (roster->GetAudioOutput(&output) == B_OK)
		check(output.node != node.node, "not the default output unchosen");

	check(try_bind(0x0019) == EADDRINUSE, "node listens on PSM 0x19 (AVDTP)");
	check(try_bind(0x0017) == EADDRINUSE, "node listens on PSM 0x17 (AVCTP)");

	BParameterWeb* web = NULL;
	status = roster->GetParameterWebFor(node, &web);
	BContinuousParameter* volume = NULL;
	for (int32 i = 0; web != NULL && i < web->CountParameters(); i++) {
		BParameter* parameter = web->ParameterAt(i);
		if (parameter->Type() == BParameter::B_CONTINUOUS_PARAMETER
			&& strcmp(parameter->Name(), "Volume") == 0)
			volume = (BContinuousParameter*)parameter;
	}
	check(volume != NULL, "volume parameter");
	if (volume != NULL) {
		float value = 42.0f;
		volume->SetValue(&value, sizeof(value), 0);
		snooze(200000);
		float readBack = 0.0f;
		size_t size = sizeof(readBack);
		bigtime_t when;
		status = volume->GetValue(&readBack, &size, &when);
		check(status == B_OK && readBack == 42.0f, "volume reads back 42 %");
	}
	delete web;

	// GetLiveNodes() took no reference: media_addon_server's node stays.
	if (!ownNode)
		return;
	status = roster->ReleaseNode(node);
	check(status == B_OK, "node released");
	// The listen threads close their sockets when the node is deleted.
	bool free = false;
	for (int i = 0; i < 20 && !free; i++) {
		snooze(250000);
		free = try_bind(0x0019) == 0 && try_bind(0x0017) == 0;
	}
	check(free, "PSM 0x19 and 0x17 free again after release");
}


static void
check_server()
{
	BMessenger server(BLUETOOTH_SIGNATURE);
	check(server.IsValid(), "bluetooth_server runs");
	if (!server.IsValid())
		return;

	BMessage paired(BT_MSG_R5_AUDIO_PAIRED);
	bdaddr_t address = { { 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 } };
	paired.AddData("bdaddr", B_ANY_TYPE, &address, sizeof(address));
	BMessage reply;
	bool isPaired = true;
	status_t status = r5_admin_send(server, &paired, &reply, 2000000);
	check(status == B_OK && reply.FindBool("paired", &isPaired) == B_OK
		&& !isPaired, "server: unknown device is not paired");

	for (int i = 0; i < 2; i++) {
		BMessage streaming(BT_MSG_R5_AUDIO_STREAMING);
		streaming.AddBool("streaming", i == 0);
		reply.MakeEmpty();
		status = r5_admin_send(server, &streaming, &reply, 2000000);
		int32 result = B_ERROR;
		reply.FindInt32("status", &result);
		check(status == B_OK && result == B_OK, i == 0
			? "server: audio streaming on (scans paused)"
			: "server: audio streaming off");
	}
}


int
main()
{
	BApplication app("application/x-vnd.R5-bt-audio-smoke-test");
	check_node();
	check_server();
	if (sFailures != 0) {
		printf("bt_audio_smoke_test: %d failures\n", sFailures);
		return 1;
	}
	printf("bt_audio_smoke_test: ok\n");
	return 0;
}
