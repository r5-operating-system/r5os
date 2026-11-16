/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * bt_l2cap_abi_test: the checks of docs/BLUETOOTH.md, step 1b, that need no
 * radio.
 *
 * L2CAP sockets: a socket binds and listens on the HID PSMs 0x11 and 0x13
 * and on AVDTP 0x19; a second bind on a taken PSM fails with EADDRINUSE; an
 * even PSM and one with bit 8 set fail with EINVAL.
 *
 * libbluetooth: RemoteDevice::GetConnectionState() keeps its signature and
 * answers DISCONNECTED for a device nobody knows; the additive
 * GetConnectionDetails() refuses a too small struct with B_BAD_VALUE and a
 * device without an owning radio with B_NO_INIT, and then reports
 * DISCONNECTED.
 *
 * Prints, and writes to the syslog, "bt_l2cap_abi_test: ok" or
 * "bt_l2cap_abi_test: FAIL <reason>". Exit status 0 on ok.
 */


#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <String.h>

#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetooth/RemoteDevice.h>
#include <bluetooth/bluetooth.h>


static int sFailures = 0;


static void
fail(const char* what, int value)
{
	sFailures++;
	printf("bt_l2cap_abi_test: FAIL %s (%d: %s)\n", what, value,
		strerror(value));
	syslog(LOG_ERR, "bt_l2cap_abi_test: FAIL %s (%d: %s)", what, value,
		strerror(value));
}


static int
l2cap_socket()
{
	return socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
}


static int
bind_psm(int fd, uint16 psm)
{
	struct sockaddr_l2cap address;
	memset(&address, 0, sizeof(address));
	address.l2cap_len = sizeof(address);
	address.l2cap_family = AF_BLUETOOTH;
	address.l2cap_psm = B_HOST_TO_LENDIAN_INT16(psm);
	bdaddr_t any = BDADDR_ANY;
	address.l2cap_bdaddr = any;
	if (bind(fd, (struct sockaddr*)&address, sizeof(address)) != 0)
		return errno;
	return 0;
}


static void
check_sockets()
{
	static const uint16 kListen[] = { 0x0011, 0x0013, 0x0019 };
	int listening[3] = { -1, -1, -1 };

	for (int i = 0; i < 3; i++) {
		listening[i] = l2cap_socket();
		if (listening[i] < 0) {
			fail("socket(PF_BLUETOOTH, SOCK_SEQPACKET, L2CAP)", errno);
			return;
		}
		int error = bind_psm(listening[i], kListen[i]);
		if (error != 0) {
			char what[64];
			snprintf(what, sizeof(what), "bind PSM %#06x", kListen[i]);
			fail(what, error);
			continue;
		}
		if (listen(listening[i], 2) != 0) {
			char what[64];
			snprintf(what, sizeof(what), "listen on PSM %#06x", kListen[i]);
			fail(what, errno);
		}
	}

	int second = l2cap_socket();
	if (second >= 0) {
		int error = bind_psm(second, 0x0011);
		if (error != EADDRINUSE)
			fail("a second bind on PSM 0x0011 is not EADDRINUSE", error);
		close(second);
	}

	static const uint16 kInvalid[] = { 0x0010, 0x0101 };
	for (int i = 0; i < 2; i++) {
		int fd = l2cap_socket();
		if (fd < 0)
			continue;
		int error = bind_psm(fd, kInvalid[i]);
		if (error != EINVAL) {
			char what[64];
			snprintf(what, sizeof(what), "bind PSM %#06x is not EINVAL",
				kInvalid[i]);
			fail(what, error);
		}
		close(fd);
	}

	for (int i = 0; i < 3; i++) {
		if (listening[i] >= 0)
			close(listening[i]);
	}
}


static void
check_remote_device()
{
	RemoteDevice device(BString("00:11:22:33:44:55"));

	RemoteDevice::ConnectionState state = device.GetConnectionState();
	if (state != RemoteDevice::DISCONNECTED)
		fail("GetConnectionState() of an unknown device", (int)state);

	bt_connection_details details;
	memset(&details, 0, sizeof(details));
	details.size = sizeof(details) - 1;
	status_t status = device.GetConnectionDetails(&details);
	if (status != B_BAD_VALUE)
		fail("GetConnectionDetails() with a short size", status);

	details.size = sizeof(details);
	status = device.GetConnectionDetails(&details);
	if (status != B_NO_INIT)
		fail("GetConnectionDetails() without an owning radio", status);
	if (details.state != RemoteDevice::DISCONNECTED)
		fail("GetConnectionDetails() state", details.state);

	if (device.GetConnectionDetails(NULL) != B_BAD_VALUE)
		fail("GetConnectionDetails(NULL)", 0);
}


int
main()
{
	check_sockets();
	check_remote_device();

	if (sFailures != 0)
		return 1;

	printf("bt_l2cap_abi_test: ok\n");
	syslog(LOG_INFO, "bt_l2cap_abi_test: ok");
	return 0;
}
