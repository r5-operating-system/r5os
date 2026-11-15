/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * bt_server_smoke_test: starts bluetooth_server if it is not running, and
 * sends it the requests that used to crash it or leave the caller without a
 * reply: a connection state for an unknown radio and address, an address of
 * the wrong size, no address at all, remote devices of an unknown radio, a
 * removal of an unknown device, and a stop-watching for a messenger that
 * never watched. Every request that waits for a reply must get one, and the
 * server must still run at the end.
 *
 * Prints, and writes to the syslog, "bt_server_smoke_test: ok (0 crashes)"
 * or "bt_server_smoke_test: FAIL <reason>". Exit status 0 on ok.
 */


#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <Application.h>
#include <Entry.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>
#include <String.h>

#include <bluetooth/RemoteDevice.h>
#include <bluetooth/bluetooth.h>
#include <bluetoothserver_p.h>


static const bigtime_t kTimeout = 3000000;
static const int32 kUnknownRadio = 0x7fff;


static int sFailures = 0;


static void
fail(const char* what)
{
	sFailures++;
	printf("bt_server_smoke_test: FAIL %s\n", what);
	syslog(LOG_ERR, "bt_server_smoke_test: FAIL %s", what);
}


static bool
ask(BMessenger& server, BMessage& request, BMessage& reply, const char* what)
{
	status_t status = server.SendMessage(&request, &reply, kTimeout, kTimeout);
	if (status != B_OK) {
		BString text;
		text.SetToFormat("%s: no reply (%s)", what, strerror(status));
		fail(text.String());
		return false;
	}
	if (reply.what == B_NO_REPLY) {
		BString text;
		text.SetToFormat("%s: B_NO_REPLY", what);
		fail(text.String());
		return false;
	}
	return true;
}


static void
check_conn_state(BMessenger& server, const void* address, ssize_t size,
	const char* what)
{
	BMessage request(BT_REQ_CONN_STATE);
	request.AddInt32("hci_id", kUnknownRadio);
	if (address != NULL)
		request.AddData("bdaddr", B_ANY_TYPE, address, size);

	BMessage reply;
	if (!ask(server, request, reply, what))
		return;

	uint8 state;
	if (reply.FindUInt8("conn state", &state) != B_OK) {
		BString text;
		text.SetToFormat("%s: no \"conn state\" in the reply", what);
		fail(text.String());
	} else if (state != RemoteDevice::DISCONNECTED) {
		BString text;
		text.SetToFormat("%s: state %u, not DISCONNECTED", what, state);
		fail(text.String());
	}
}


static bool
connect(BMessenger& server)
{
	if (!be_roster->IsRunning(BLUETOOTH_SIGNATURE)) {
		status_t status = be_roster->Launch(BLUETOOTH_SIGNATURE);
		if (status != B_OK && status != B_ALREADY_RUNNING) {
			// The signature may not be in the MIME database yet.
			entry_ref ref;
			if (get_ref_for_path("/system/servers/bluetooth_server", &ref)
					== B_OK)
				status = be_roster->Launch(&ref);
		}
		if (status != B_OK && status != B_ALREADY_RUNNING) {
			BString text;
			text.SetToFormat("cannot start bluetooth_server (%s)",
				strerror(status));
			fail(text.String());
			return false;
		}
	}

	for (int i = 0; i < 50; i++) {
		server = BMessenger(BLUETOOTH_SIGNATURE);
		if (server.IsValid())
			return true;
		snooze(100000);
	}

	fail("bluetooth_server did not come up");
	return false;
}


int
main()
{
	BApplication app("application/x-vnd.R5-bt_server_smoke_test");
	BMessenger server;
	if (!connect(server))
		return 1;

	team_id team = server.Team();

	{
		BMessage request(BT_MSG_COUNT_LOCAL_DEVICES);
		BMessage reply;
		int32 count;
		if (ask(server, request, reply, "count radios")
			&& reply.FindInt32("count", &count) != B_OK)
			fail("count radios: no \"count\" in the reply");
	}

	bdaddr_t unknown = {{ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 }};
	check_conn_state(server, &unknown, sizeof(unknown),
		"conn state, unknown radio");
	check_conn_state(server, &unknown, 2, "conn state, short bdaddr");
	check_conn_state(server, NULL, 0, "conn state, no bdaddr");

	{
		BMessage request(BT_MSG_GET_REMOTE_DEVICES);
		request.AddInt32("hci_id", kUnknownRadio);
		BMessage reply;
		ask(server, request, reply, "remote devices, unknown radio");
	}

	// These two have no reply; the server must only survive them.
	{
		BMessage request(BT_REQ_REMOVE_DEVICE);
		request.AddInt32("hci_id", kUnknownRadio);
		request.AddData("bdaddr", B_ANY_TYPE, &unknown, 2);
		server.SendMessage(&request);
	}
	{
		BMessage request(BT_STOP_WATCHING_CONNECTIONS);
		request.AddMessenger("messenger", BMessenger(be_app));
		server.SendMessage(&request);
	}

	// A last request that waits for a reply, so the two above are handled.
	{
		BMessage request(BT_MSG_COUNT_LOCAL_DEVICES);
		BMessage reply;
		ask(server, request, reply, "count radios after");
	}

	team_info info;
	if (get_team_info(team, &info) != B_OK)
		fail("bluetooth_server is gone");

	if (sFailures != 0)
		return 1;

	printf("bt_server_smoke_test: ok (0 crashes)\n");
	syslog(LOG_INFO, "bt_server_smoke_test: ok (0 crashes)");
	return 0;
}
