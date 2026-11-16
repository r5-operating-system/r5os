/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * bt_server_smoke_test: starts bluetooth_server if it is not running, and
 * sends it the requests that used to crash it or leave the caller without a
 * reply: a connection state for an unknown radio and address, an address of
 * the wrong size, no address at all, remote devices of an unknown radio, a
 * removal of an unknown device, and a stop-watching for a messenger that
 * never watched; and the radio switch (step 3a): off, a refused
 * connection, on again. Every request that waits for a reply must get one,
 * and the server must still run at the end.
 *
 * Prints, and writes to the syslog, "bt_server_smoke_test: ok (0 crashes)"
 * or "bt_server_smoke_test: FAIL <reason>". Exit status 0 on ok.
 */


#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <Application.h>
#include <Entry.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>
#include <String.h>

#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>
#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/bluetooth.h>
#include <bluetoothserver_p.h>

#include <R5Admin.h>


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

	// With a radio (VirtualBox, a stick), ask each one for its address and
	// name: two real HCI commands (Read BD_ADDR, Read Local Name), each
	// answered or timed out by h2generic within 2 s.
	uint32 radios = LocalDevice::GetLocalDeviceCount();
	printf("bt_server_smoke_test: %" B_PRIu32 " radios\n", radios);
	syslog(LOG_INFO, "bt_server_smoke_test: %" B_PRIu32 " radios", radios);
	for (uint32 i = 0; i < radios; i++) {
		LocalDevice* radio = LocalDevice::GetLocalDevice();
		if (radio == NULL) {
			fail("a radio could not be acquired");
			continue;
		}
		bigtime_t start = system_time();
		bdaddr_t address = radio->GetBluetoothAddress();
		BString name = radio->GetFriendlyName();
		bigtime_t took = (system_time() - start) / 1000;

		bdaddr_t none = {{ 0, 0, 0, 0, 0, 0 }};
		printf("bt_server_smoke_test: radio %" B_PRId32 " address %s name \"%s\""
			" in %" B_PRIdBIGTIME " ms\n", radio->ID(),
			bdaddrUtils::ToString(address).String(), name.String(), took);
		syslog(LOG_INFO, "bt_server_smoke_test: radio %" B_PRId32 " address %s"
			" name \"%s\" in %" B_PRIdBIGTIME " ms", radio->ID(),
			bdaddrUtils::ToString(address).String(), name.String(), took);
		if (memcmp(&address, &none, sizeof(address)) == 0)
			fail("the radio gave no address");
	}

	// The radio switch (step 3a): off, read back, a connection is refused,
	// on again, read back. Leaves the switch on.
	{
		bool before = true;
		BMessage request(BT_MSG_R5_GET_RADIO);
		BMessage reply;
		if (ask(server, request, reply, "radio switch, read")
			&& reply.FindBool("enabled", &before) != B_OK)
			fail("radio switch, read: no \"enabled\" in the reply");

		for (int pass = 0; pass < 2; pass++) {
			bool on = pass == 1;
			BMessage set(BT_MSG_R5_SET_RADIO);
			set.AddBool("enabled", on);
			BMessage setReply;
			// The switch is only on the admin port (libr5admin.so).
			if (r5_admin_send(server, &set, &setReply, kTimeout) != B_OK)
				fail("radio switch, set: no answer on the admin port");
			else if (setReply.GetInt32("status", B_ERROR) != B_OK)
				fail("radio switch, set: not saved");
			if (pass == 0) {
				// The normal messenger refuses it: its reply address
				// proves nothing.
				BMessage plain(BT_MSG_R5_SET_RADIO);
				plain.AddBool("enabled", true);
				BMessage plainReply;
				if (server.SendMessage(&plain, &plainReply, kTimeout,
						kTimeout) == B_OK
					&& plainReply.GetInt32("status", B_OK) != B_NOT_ALLOWED)
					fail("radio switch: taken on the normal messenger");
			}

			BMessage get(BT_MSG_R5_GET_RADIO);
			BMessage getReply;
			if (ask(server, get, getReply, "radio switch, read back")
				&& getReply.GetBool("enabled", !on) != on)
				fail("radio switch, read back: wrong value");

			if (!on) {
				BMessage connect(BT_REQ_CREATE_CONN);
				connect.AddInt32("hci_id", kUnknownRadio);
				connect.AddData("bdaddr", B_ANY_TYPE, &unknown, sizeof(unknown));
				BMessage connectReply;
				if (ask(server, connect, connectReply, "connect while off")
					&& connectReply.GetInt32("status", B_OK) != B_NOT_ALLOWED)
					fail("connect while off: not refused");
			}
		}
		printf("bt_server_smoke_test: radio switch ok (was %s)\n",
			before ? "on" : "off");
		syslog(LOG_INFO, "bt_server_smoke_test: radio switch ok (was %s)",
			before ? "on" : "off");
	}

	// Login's admin port (libr5admin.so): a change on the normal messenger
	// is refused, the admin port answers. An unlock with a wrong password
	// gets "wrong password" or "no such account", not "not allowed".
	// Only when Login runs: on a first boot FirstBootPrompt comes first,
	// and a BMessenger for Login, which launch_daemon pre-registered,
	// waits until it starts.
	bool loginRuns = false;
	int32 cookie = 0;
	team_info loginInfo;
	while (!loginRuns && get_next_team_info(&cookie, &loginInfo) == B_OK) {
		const char* name = strrchr(loginInfo.args, '/');
		name = name != NULL ? name + 1 : loginInfo.args;
		loginRuns = strncmp(name, "Login", 5) == 0
			&& (name[5] == 0 || name[5] == ' ');
	}
	BMessenger login;
	if (loginRuns)
		login = BMessenger("application/x-vnd.Haiku-Login");
	if (!loginRuns) {
		printf("bt_server_smoke_test: admin: Login does not run (first "
			"boot), not tested\n");
		syslog(LOG_INFO, "bt_server_smoke_test: admin: Login does not run, "
			"not tested");
	} else if (login.IsValid()) {
		BMessage change('r5ad');	// kMsgAdminDelete
		change.AddString("name", "nobody-r5-test");
		BMessage changeReply;
		if (login.SendMessage(&change, &changeReply, kTimeout, kTimeout)
				!= B_OK
			|| changeReply.GetInt32("error", B_OK) != B_NOT_ALLOWED)
			fail("admin: Login took a change on its normal messenger");

		syslog(LOG_INFO, "bt_server_smoke_test: admin: normal messenger done");
		BMessage unlock('r5au');	// kMsgAdminUnlock
		unlock.AddString("name", "user");
		unlock.AddString("password", "not-the-password-r5-test");
		BMessage unlockReply;
		status_t status = r5_admin_send(login, &unlock, &unlockReply,
			kTimeout);
		syslog(LOG_INFO, "bt_server_smoke_test: admin: port answered %s",
			strerror(status));
		int32 code = unlockReply.GetInt32("error", B_OK);
		if (status != B_OK)
			fail("admin: no answer on Login's admin port");
		else if (code == B_OK || code == B_NOT_ALLOWED)
			fail("admin: Login's admin port gave a wrong answer");
		else {
			printf("bt_server_smoke_test: admin port ok\n");
			syslog(LOG_INFO, "bt_server_smoke_test: admin port ok");
		}
	} else
		fail("admin: no messenger for Login");

	team_info info;
	if (get_team_info(team, &info) != B_OK)
		fail("bluetooth_server is gone");

	// Last: the same requests as a standard person (uid 1000, not in group
	// admin, no grant). Everything that changes something must be refused,
	// also the old trick of naming Login itself as the reply address.
	if (loginRuns && login.IsValid()) {
		if (setgid(100) != 0 || setuid(1000) != 0 || getuid() != 1000)
			fail("standard: could not become uid 1000");
		else {
			BMessage query('r5aq');	// kMsgAdminQuery
			BMessage queryReply;
			if (r5_admin_send(login, &query, &queryReply, kTimeout) != B_OK
				|| queryReply.GetBool("admin", true)
				|| queryReply.GetBool("unlocked", true))
				fail("standard: Login says uid 1000 is an administrator");

			BMessage add('r5aa');	// kMsgAdminAdd
			add.AddString("name", "r5stdtest");
			add.AddString("real", "R5 test");
			add.AddString("password", "r5-test-password");
			BMessage addReply;
			if (r5_admin_send(login, &add, &addReply, kTimeout) != B_OK
				|| addReply.GetInt32("error", B_OK) == B_OK)
				fail("standard: Login added an account for uid 1000");

			BMessage gmt('r5ag');	// kMsgAdminSetClockGmt
			gmt.AddBool("gmt", true);
			BMessage gmtReply;
			if (r5_admin_send(login, &gmt, &gmtReply, kTimeout) != B_OK
				|| gmtReply.GetInt32("error", B_OK) == B_OK)
				fail("standard: Login changed the clock for uid 1000");

			// The old trick: Login's own messenger as the reply address.
			BMessage spoof('r5aa');
			spoof.AddString("name", "r5spooftest");
			spoof.AddString("real", "R5 test");
			spoof.AddString("password", "r5-test-password");
			login.SendMessage(&spoof, login, kTimeout);
			snooze(2000000);
			if (getpwnam("r5spooftest") != NULL
				|| getpwnam("r5stdtest") != NULL)
				fail("standard: an account was added for uid 1000");

			BMessage radio(BT_MSG_R5_SET_RADIO);
			radio.AddBool("enabled", true);
			BMessage radioReply;
			if (r5_admin_send(server, &radio, &radioReply, kTimeout) != B_OK
				|| radioReply.GetInt32("status", B_OK) != B_NOT_ALLOWED)
				fail("standard: the radio switch took uid 1000");

			if (sFailures == 0) {
				printf("bt_server_smoke_test: standard user refused ok\n");
				syslog(LOG_INFO,
					"bt_server_smoke_test: standard user refused ok");
			}
		}
	}

	if (sFailures != 0)
		return 1;

	printf("bt_server_smoke_test: ok (0 crashes)\n");
	syslog(LOG_INFO, "bt_server_smoke_test: ok (0 crashes)");
	return 0;
}
