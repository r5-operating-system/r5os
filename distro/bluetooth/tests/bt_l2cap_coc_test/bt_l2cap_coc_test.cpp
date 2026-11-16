/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * bt_l2cap_coc_test: the checks of docs/BLUETOOTH.md, step 1c, that need no
 * radio, on the LE socket interface of <R5BluetoothLE.h>.
 *
 * - The fixed channels ATT and SMP: bluetooth_server holds them (a second
 *   bind fails with EADDRINUSE; if it does not run, this test binds them),
 *   a non-root team may not bind them (EPERM), and LE signalling or an
 *   unknown CID cannot be bound (EINVAL). A PDU to an address without an
 *   LE link is refused, and a socket bound but never used closes cleanly.
 * - LE credit-based channels: an LE PSM binds and listens, a second bind
 *   on it fails, PSM 0 and above 0xFF fail; connect() without an LE link
 *   fails with ENETUNREACH or EHOSTUNREACH, and with an unknown mode with
 *   EINVAL.
 *
 * Prints, and writes to the syslog, "l2cap_coc_test: ok" or
 * "l2cap_coc_test: FAIL <reason>". Exit status 0 on ok.
 */


#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetooth/bluetooth.h>

#include <R5BluetoothLE.h>


static int sFailures = 0;


static void
fail(const char* what, int value)
{
	sFailures++;
	printf("l2cap_coc_test: FAIL %s (%d: %s)\n", what, value, strerror(value));
	syslog(LOG_ERR, "l2cap_coc_test: FAIL %s (%d: %s)", what, value,
		strerror(value));
}


static int
le_socket()
{
	return socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
}


static sockaddr_l2cap_le
le_address(uint16 psm, uint16 cid, uint8 mode)
{
	sockaddr_l2cap_le address;
	memset(&address, 0, sizeof(address));
	address.l2cap_len = sizeof(address);
	address.l2cap_family = AF_BLUETOOTH;
	address.l2cap_bdaddr = BDADDR_ANY;
	address.l2cap_psm = psm;
	address.l2cap_cid = cid;
	address.l2cap_mode = mode;
	return address;
}


static int
bind_le(int fd, uint16 psm, uint16 cid)
{
	sockaddr_l2cap_le address = le_address(psm, cid, 0);
	if (bind(fd, (sockaddr*)&address, sizeof(address)) != 0)
		return errno;
	return 0;
}


static void
check_fixed(uint16 cid, const char* name)
{
	int fd = le_socket();
	if (fd < 0) {
		fail("socket", errno);
		return;
	}
	int result = bind_le(fd, 0, cid);
	if (result == EADDRINUSE) {
		printf("l2cap_coc_test: %s held by bluetooth_server\n", name);
	} else if (result == 0) {
		printf("l2cap_coc_test: %s bound here (bluetooth_server does not"
			" hold it)\n", name);
		// A PDU to an address without an LE link goes nowhere.
		sockaddr_l2cap_le to = le_address(0, cid, 0);
		to.l2cap_bdaddr.b[0] = 0x5a;
		to.l2cap_bdaddr_type = R5_BDADDR_LE_RANDOM;
		const unsigned char pdu[3] = { 0x02, 0x17, 0x00 };
		if (sendto(fd, pdu, sizeof(pdu), 0, (sockaddr*)&to, sizeof(to)) >= 0)
			fail("sendto without an LE link", 0);
		else if (errno != ENETUNREACH && errno != EHOSTUNREACH)
			fail("sendto without an LE link", errno);
		// A second socket cannot take it.
		int other = le_socket();
		if (other >= 0) {
			int second = bind_le(other, 0, cid);
			if (second != EADDRINUSE)
				fail("second bind of a fixed channel", second);
			close(other);
		}
	} else
		fail(name, result);
	close(fd);
}


static void
check_not_root()
{
	pid_t child = fork();
	if (child < 0) {
		fail("fork", errno);
		return;
	}
	if (child == 0) {
		if (setuid(1000) != 0)
			_exit(2);
		int fd = le_socket();
		int result = fd < 0 ? -1 : bind_le(fd, 0, R5_L2CAP_CID_SMP);
		_exit(result == EPERM ? 0 : 1);
	}
	int status = 0;
	waitpid(child, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) == 1)
		fail("a standard team bound SMP", EPERM);
	else if (WEXITSTATUS(status) == 2)
		printf("l2cap_coc_test: setuid not possible, EPERM not checked\n");
}


int
main()
{
	// Fixed channels.
	check_fixed(R5_L2CAP_CID_ATT, "ATT");
	check_fixed(R5_L2CAP_CID_SMP, "SMP");
	if (geteuid() == 0)
		check_not_root();

	const uint16 badCIDs[] = { R5_L2CAP_CID_LE_SIGNALING, 0x0001, 0x0040 };
	for (size_t i = 0; i < sizeof(badCIDs) / sizeof(badCIDs[0]); i++) {
		int fd = le_socket();
		int result = bind_le(fd, 0, badCIDs[i]);
		if (result != EINVAL) {
			char what[48];
			snprintf(what, sizeof(what), "bind of CID %#x", badCIDs[i]);
			fail(what, result);
		}
		close(fd);
	}

	// LE credit-based channels: listen, refuse a second bind.
	int listener = le_socket();
	int result = bind_le(listener, 0x0081, 0);
	if (result != 0)
		fail("bind LE PSM 0x81", result);
	else if (listen(listener, 2) != 0)
		fail("listen LE PSM 0x81", errno);
	int other = le_socket();
	result = bind_le(other, 0x0081, 0);
	if (result != EADDRINUSE)
		fail("second bind LE PSM 0x81", result);
	close(other);
	close(listener);

	const uint16 badPSMs[] = { 0x0000, 0x0100 };
	for (size_t i = 0; i < 2; i++) {
		int fd = le_socket();
		result = bind_le(fd, badPSMs[i], 0);
		if (result != EINVAL) {
			char what[48];
			snprintf(what, sizeof(what), "bind of LE PSM %#x", badPSMs[i]);
			fail(what, result);
		}
		close(fd);
	}

	// Bound, never listening: close must not touch a link.
	int bound = le_socket();
	if (bind_le(bound, 0x0083, 0) != 0)
		fail("bind LE PSM 0x83", errno);
	close(bound);

	// connect() needs an LE link that bluetooth_server made.
	int fd = le_socket();
	sockaddr_l2cap_le peer = le_address(0x0080, 0, R5_L2CAP_MODE_LE_CREDIT);
	peer.l2cap_bdaddr.b[0] = 0xa5;
	peer.l2cap_bdaddr_type = R5_BDADDR_LE_RANDOM;
	if (connect(fd, (sockaddr*)&peer, sizeof(peer)) == 0)
		fail("connect without an LE link", 0);
	else if (errno != ENETUNREACH && errno != EHOSTUNREACH)
		fail("connect without an LE link", errno);
	close(fd);

	fd = le_socket();
	peer.l2cap_psm = R5_L2CAP_PSM_EATT;
	peer.l2cap_mode = R5_L2CAP_MODE_ENHANCED_CREDIT;
	if (connect(fd, (sockaddr*)&peer, sizeof(peer)) == 0)
		fail("enhanced connect without an LE link", 0);
	else if (errno != ENETUNREACH && errno != EHOSTUNREACH)
		fail("enhanced connect without an LE link", errno);
	close(fd);

	fd = le_socket();
	peer.l2cap_mode = 9;
	if (connect(fd, (sockaddr*)&peer, sizeof(peer)) == 0 || errno != EINVAL)
		fail("connect with an unknown mode", errno);
	close(fd);

	if (sFailures != 0)
		return 1;
	printf("l2cap_coc_test: ok\n");
	syslog(LOG_INFO, "l2cap_coc_test: ok");
	return 0;
}
