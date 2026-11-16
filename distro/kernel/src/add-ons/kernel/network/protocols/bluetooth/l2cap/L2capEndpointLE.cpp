/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth Low Energy in L2CAP (docs/BLUETOOTH.md, steps 1c and 6): the
 * fixed channels ATT and SMP as sockets for bluetooth_server, LE credit-
 * based channels (CoC, Core 6.0, Vol 3, Part A, 3.4 and 4.22 to 4.24) and
 * enhanced credit-based channels (4.25 to 4.27), which EATT runs on, and
 * the LE signalling commands. The socket interface is in <R5BluetoothLE.h>.
 *
 * What a remote sends is checked by l2cap_parse.h, which
 * scripts/fuzz-bt.sh l2cap also runs on the host.
 */


#include "L2capEndpoint.h"

#include <string.h>
#include <unistd.h>

#include <NetBufferUtilities.h>

#include <R5BluetoothLE.h>
#include <btDebug.h>

#include "L2capEndpointManager.h"
#include "l2cap_command.h"
#include "l2cap_internal.h"
#include "l2cap_parse.h"
#include "l2cap_signal.h"


// Our side of a credit-based channel: the SDU we take, the frame we take
// (a K-frame of 247 bytes and its basic header fill one LE ACL packet of
// 251 bytes with Data Length Extension), and the credits we give.
static const uint16 kLEMTU = 1024;
static const uint16 kLEMPS = 247;
static const uint16 kLECredits = 10;

// The largest PDU sent on a fixed channel: an ATT MTU of 517 and more.
static const size_t kFixedMaximum = 1024;

// Result codes of the credit-based connection answers (4.23, 4.26).
enum {
	kLEResultSuccess			= 0x0000,
	kLEResultPSMNotSupported	= 0x0002,
	kLEResultNoResources		= 0x0004,
	kLEResultInsufficientAuth	= 0x0005,
	kLEResultKeySize			= 0x0007,
	kLEResultInsufficientEncr	= 0x0008,
	kLEResultInvalidSourceCID	= 0x0009,
	kLEResultSourceCIDInUse		= 0x000a,
	kLEResultUnacceptable		= 0x000b
};


// #pragma mark - endpoint


status_t
L2capEndpoint::_BindLE(const sockaddr* _address)
{
	const sockaddr_l2cap_le* address = (const sockaddr_l2cap_le*)_address;
	if (address->l2cap_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	MutexLocker _(fLock);
	if (fState != CLOSED)
		return EISCONN;

	status_t status;
	if (address->l2cap_cid != 0) {
		// The fixed channels carry the pairing and the GATT of every LE
		// link: only bluetooth_server (uid 0) may hold them.
		if (geteuid() != 0)
			return EPERM;
		status = gL2capEndpointManager.BindFixed(this, _address);
	} else {
		// The LE PSMs the Bluetooth SIG assigns (0x01 to 0x7F, EATT is
		// 0x27) are the system's: another program could take EATT first
		// and receive every device's GATT traffic.
		if (B_LENDIAN_TO_HOST_INT16(address->l2cap_psm) < 0x80
			&& geteuid() != 0)
			return EPERM;
		status = gL2capEndpointManager.BindLE(this, _address);
	}
	if (status != B_OK)
		return status;

	fState = BOUND;
	return B_OK;
}


status_t
L2capEndpoint::_ConnectLE(const sockaddr* _address)
{
	const sockaddr_l2cap_le* address = (const sockaddr_l2cap_le*)_address;
	if (address->l2cap_family != AF_BLUETOOTH || address->l2cap_cid != 0)
		return EINVAL;
	const uint8 mode = address->l2cap_mode;
	if (mode != R5_L2CAP_MODE_LE_CREDIT && mode != R5_L2CAP_MODE_ENHANCED_CREDIT)
		return EINVAL;
	if (address->l2cap_psm == 0 || address->l2cap_psm > 0x00ff)
		return EINVAL;

	MutexLocker _(fLock);

	status_t status;
	bigtime_t timeout;
	if (gStackModule->is_restarted_syscall()) {
		timeout = gStackModule->restore_syscall_restart_timeout();
		while (fState != CLOSED && fState != OPEN) {
			status = _WaitForStateChange(timeout);
			if (status != B_OK)
				return status;
		}
		return fState == OPEN ? B_OK : ECONNREFUSED;
	}
	timeout = gStackModule->set_syscall_restart_timeout(socket->send.timeout);

	if (fState == LISTEN || fFixedChannel != 0)
		return EINVAL;
	if (fState == OPEN)
		return EISCONN;
	if (fState != CLOSED)
		return EALREADY;

	// bluetooth_server makes the LE link; a channel only runs on it.
	hci_id hid = btCoreData->RouteConnection(address->l2cap_bdaddr);
	if (hid <= 0)
		return ENETUNREACH;
	HciConnection* connection = btCoreData->ConnectionByDestination(
		address->l2cap_bdaddr, hid);
	if (connection == NULL || connection->type != R5_HCI_LINK_LE)
		return EHOSTUNREACH;
	fConnection = connection;

	memcpy(&socket->peer, address, sizeof(sockaddr_l2cap_le));

	status = gL2capEndpointManager.BindToLEChannel(this);
	if (status != B_OK) {
		fConnection = NULL;
		return status;
	}

	fLEMode = mode;
	fChannelConfig.incoming_mtu = kLEMTU;
	fLERx.mtu = kLEMTU;
	fLERx.mps = kLEMPS;
	fLERx.sdu_length = 0;
	fLERx.received = 0;
	fLERxCredits = kLECredits;

	uint8 ident = btCoreData->allocate_command_ident(fConnection, this);
	if (ident == L2CAP_NULL_IDENT) {
		_MarkClosed();
		return ENOBUFS;
	}
	gSocketModule->acquire_socket(socket);

	status = send_l2cap_le_credit_connection_req(fConnection, ident,
		mode == R5_L2CAP_MODE_ENHANCED_CREDIT, address->l2cap_psm,
		fChannelID, kLEMTU, kLEMPS, kLECredits);
	if (status != B_OK) {
		if (l2cap_claim_command_ident(fConnection, ident, this))
			gSocketModule->release_socket(socket);
		_MarkClosed();
		return status;
	}

	fState = WAIT_FOR_CONNECTION_RSP;
	while (fState != CLOSED && fState != OPEN) {
		status = _WaitForStateChange(timeout);
		if (status != B_OK) {
			if (fConnection != NULL
				&& l2cap_claim_command_ident(fConnection, ident, this))
				gSocketModule->release_socket(socket);
			_MarkClosed();
			return status;
		}
	}
	if (fState == OPEN)
		return B_OK;
	// A Command Reject closes without giving the channel back.
	if (fChannelID != L2CAP_NULL_CID)
		gL2capEndpointManager.UnbindFromChannel(this);
	return socket->error != B_OK && socket->error != ENOTCONN
		? socket->error : ECONNREFUSED;
}


status_t
L2capEndpoint::_SendFixed(net_buffer* buffer)
{
	if (buffer->size == 0 || buffer->size > kFixedMaximum)
		return EMSGSIZE;

	const sockaddr_l2cap_le* to = (const sockaddr_l2cap_le*)buffer->destination;
	if (to->l2cap_len != sizeof(sockaddr_l2cap_le)
		|| to->l2cap_family != AF_BLUETOOTH)
		return EDESTADDRREQ;

	hci_id hid = btCoreData->RouteConnection(to->l2cap_bdaddr);
	if (hid <= 0)
		return ENETUNREACH;
	HciConnection* connection = btCoreData->ConnectionByDestination(
		to->l2cap_bdaddr, hid);
	if (connection == NULL || connection->type != R5_HCI_LINK_LE)
		return EHOSTUNREACH;

	NetBufferPrepend<l2cap_basic_header> header(buffer);
	if (header.Status() != B_OK)
		return ENOBUFS;
	header->length = htole16(buffer->size - sizeof(l2cap_basic_header));
	header->dcid = htole16(fFixedChannel);
	header.Sync();

	// PostACL() owns the buffer from here, also when it fails; the stack
	// must not free it again, so the PDU counts as sent.
	buffer->type = connection->handle;
	if (l2cap_post_acl(connection->Hid, buffer) != B_OK)
		TRACE("%s: PDU on fixed channel %#x not sent\n", __func__, fFixedChannel);
	return B_OK;
}


void
L2capEndpoint::_SendLECreditQueued()
{
	ASSERT_LOCKED_MUTEX(&fLock);

	while (fState == OPEN && fConnection != NULL && fLETxCredits > 0) {
		if (fLETxSDU == NULL) {
			if (gStackModule->fifo_dequeue_buffer(&fSendQueue, MSG_DONTWAIT, 0,
					&fLETxSDU) < 0) {
				fLETxSDU = NULL;
				break;
			}
			fLETxFirst = true;
			fLETxSDULength = fLETxSDU->size;
		}

		// The first frame of an SDU carries its length (Part A, 3.4.2).
		const uint32 room = fLETxMPS - (fLETxFirst ? 2 : 0);
		net_buffer* frame;
		if (fLETxSDU->size > room) {
			frame = gBufferModule->split(fLETxSDU, room);
			if (frame == NULL)
				break;
		} else {
			frame = fLETxSDU;
			fLETxSDU = NULL;
		}

		if (fLETxFirst) {
			NetBufferPrepend<uint16> length(frame);
			if (length.Status() != B_OK) {
				gBufferModule->free(frame);
				_LEDisconnect();
				return;
			}
			*length = htole16(fLETxSDULength);
			length.Sync();
			fLETxFirst = false;
		}

		NetBufferPrepend<l2cap_basic_header> header(frame);
		if (header.Status() != B_OK) {
			gBufferModule->free(frame);
			_LEDisconnect();
			return;
		}
		header->length = htole16(frame->size - sizeof(l2cap_basic_header));
		header->dcid = htole16(fDestinationChannelID);
		header.Sync();

		fLETxCredits--;
		frame->type = fConnection->handle;
		l2cap_post_acl(fConnection->Hid, frame);
	}
}


status_t
L2capEndpoint::_ReceiveLECredit(net_buffer* buffer)
{
	MutexLocker locker(fLock);

	if (fState != OPEN) {
		gBufferModule->free(buffer);
		return ENOTCONN;
	}

	// One frame for each credit we gave (Part A, 10.1).
	if (fLERxCredits == 0) {
		ERROR("l2cap: LE frame without a credit on channel %#x\n", fChannelID);
		gBufferModule->free(buffer);
		_LEDisconnect();
		return B_BAD_DATA;
	}
	fLERxCredits--;

	uint8 head[2] = {};
	const size_t size = buffer->size;
	if (size > 0 && gBufferModule->read(buffer, 0, head, min_c(size,
			sizeof(head))) != B_OK) {
		gBufferModule->free(buffer);
		return ENOBUFS;
	}

	size_t offset = 0;
	const int result = l2cap_coc_rx_frame(&fLERx, head, size, &offset);
	if (result == L2CAP_COC_ERROR
		|| ((result & L2CAP_COC_START) == 0 && fLERxSDU == NULL)) {
		ERROR("l2cap: bad LE frame of %" B_PRIuSIZE " bytes on channel %#x\n",
			size, fChannelID);
		gBufferModule->free(buffer);
		if (fLERxSDU != NULL) {
			gBufferModule->free(fLERxSDU);
			fLERxSDU = NULL;
		}
		_LEDisconnect();
		return B_BAD_DATA;
	}

	if ((result & L2CAP_COC_START) != 0) {
		if (fLERxSDU != NULL)
			gBufferModule->free(fLERxSDU);
		gBufferModule->remove_header(buffer, offset);
		fLERxSDU = buffer;
	} else if (gBufferModule->merge(fLERxSDU, buffer, true) != B_OK) {
		gBufferModule->free(buffer);
		gBufferModule->free(fLERxSDU);
		fLERxSDU = NULL;
		_LEDisconnect();
		return ENOBUFS;
	}

	ssize_t readable = -1;
	if ((result & L2CAP_COC_COMPLETE) != 0) {
		net_buffer* sdu = fLERxSDU;
		fLERxSDU = NULL;
		if (gStackModule->fifo_enqueue_buffer(&fReceiveQueue, sdu) != B_OK)
			gBufferModule->free(sdu);
		else {
			MutexLocker fifoLocker(fReceiveQueue.lock);
			readable = fReceiveQueue.current_bytes;
		}
	}

	_LEReturnCredits();
	locker.Unlock();

	if (readable >= 0)
		gSocketModule->notify(socket, B_SELECT_READ, readable);
	return B_OK;
}


void
L2capEndpoint::_LEReturnCredits()
{
	ASSERT_LOCKED_MUTEX(&fLock);
	if (fLEMode == 0 || fState != OPEN || fConnection == NULL
		|| fLERxCredits > kLECredits / 2)
		return;

	// Only what the receive queue can still hold; ReadData() gives the
	// rest once the reader took its data.
	const uint16 give = kLECredits - fLERxCredits;
	{
		MutexLocker fifoLocker(fReceiveQueue.lock);
		if (fReceiveQueue.max_bytes - fReceiveQueue.current_bytes
				< (ssize_t)give * kLEMPS)
			return;
	}

	if (send_l2cap_le_flow_control_credit(fConnection, fChannelID, give) == B_OK)
		fLERxCredits += give;
}


void
L2capEndpoint::_LEDisconnect()
{
	ASSERT_LOCKED_MUTEX(&fLock);
	if (fState != OPEN || fConnection == NULL)
		return;

	socket->error = ECONNABORTED;
	uint8 ident = btCoreData->allocate_command_ident(fConnection, this);
	if (ident == L2CAP_NULL_IDENT) {
		_MarkClosed();
		return;
	}
	gSocketModule->acquire_socket(socket);
	if (send_l2cap_disconnection_req(fConnection, ident, fDestinationChannelID,
			fChannelID) != B_OK) {
		if (l2cap_claim_command_ident(fConnection, ident, this))
			gSocketModule->release_socket(socket);
		_MarkClosed();
		return;
	}
	fState = WAIT_FOR_DISCONNECTION_RSP;
	fCommandWait.NotifyAll();
	gSocketModule->notify(socket, B_SELECT_ERROR, ECONNABORTED);
}


uint16
L2capEndpoint::_SpawnLECredit(HciConnection* connection, uint8 mode,
	uint16 scid, uint16 mtu, uint16 mps, uint16 credits, L2capEndpoint** _child)
{
	*_child = NULL;

	MutexLocker locker(fLock);
	if (fState != LISTEN)
		return kLEResultPSMNotSupported;
	locker.Unlock();

	net_socket* newSocket;
	if (gSocketModule->spawn_pending_socket(socket, &newSocket) != B_OK)
		return kLEResultNoResources;

	L2capEndpoint* endpoint = (L2capEndpoint*)newSocket->first_protocol;
	MutexLocker childLocker(endpoint->fLock);

	if (gL2capEndpointManager.BindToLEChannel(endpoint) != B_OK) {
		childLocker.Unlock();
		gSocketModule->set_aborted(newSocket);
		return kLEResultNoResources;
	}

	endpoint->fAcceptSemaphore = fAcceptSemaphore;
	endpoint->fConnection = connection;
	endpoint->fState = CONFIGURATION;
	endpoint->fDestinationChannelID = scid;
	endpoint->fLEMode = mode;
	endpoint->fChannelConfig.outgoing_mtu = mtu;
	endpoint->fChannelConfig.incoming_mtu = kLEMTU;
	endpoint->fLETxMPS = mps;
	endpoint->fLETxCredits = credits;
	endpoint->fLERx.mtu = kLEMTU;
	endpoint->fLERx.mps = kLEMPS;
	endpoint->fLERx.sdu_length = 0;
	endpoint->fLERx.received = 0;
	endpoint->fLERxCredits = kLECredits;
	memcpy(&newSocket->peer, &connection->address_dest,
		sizeof(sockaddr_l2cap_le));

	*_child = endpoint;
	return kLEResultSuccess;
}


void
L2capEndpoint::_FinishLECreditSpawn(bool accepted)
{
	MutexLocker locker(fLock);
	if (fState != CONFIGURATION)
		return;
	if (!accepted) {
		_MarkClosed();
		locker.Unlock();
		gSocketModule->set_aborted(socket);
		return;
	}
	_MarkEstablished();
}


void
L2capEndpoint::_HandleLECreditConnectionRsp(uint8 ident, uint16 dcid,
	uint16 mtu, uint16 mps, uint16 credits, uint16 result)
{
	MutexLocker locker(fLock);
	fCommandWait.NotifyAll();

	if (fState != WAIT_FOR_CONNECTION_RSP || fLEMode == 0) {
		ERROR("l2cap: unexpected LE connection response (ident %d)\n", ident);
		return;
	}

	const bool enhanced = fLEMode == R5_L2CAP_MODE_ENHANCED_CREDIT;
	// A remote CID that another of our channels on this link already uses
	// cannot be told apart from it.
	bool inUse = false;
	if (result == kLEResultSuccess) {
		L2capEndpoint* other = gL2capEndpointManager.GetForDestination(
			fConnection, dcid);
		if (other != NULL) {
			inUse = true;
			gSocketModule->release_socket(other->socket);
		}
	}
	if (result != kLEResultSuccess || !l2cap_parse_valid_le_cid(dcid)
		|| !l2cap_parse_valid_coc(mtu, mps, enhanced) || inUse) {
		// A refusal of a channel the remote did open would leave it open
		// there: close it.
		if (result == kLEResultSuccess && l2cap_parse_valid_le_cid(dcid)) {
			uint8 closeIdent = btCoreData->allocate_command_ident(fConnection,
				NULL);
			// The identifier stays taken until the answer comes, so
			// that answer cannot reach the next command that waits.
			if (closeIdent != L2CAP_NULL_IDENT) {
				send_l2cap_disconnection_req(fConnection, closeIdent, dcid,
					fChannelID);
			}
		}
		_MarkClosed();
		switch (result) {
			case kLEResultInsufficientAuth:
			case kLEResultKeySize:
			case kLEResultInsufficientEncr:
				socket->error = EACCES;
				break;
			default:
				socket->error = ECONNREFUSED;
				break;
		}
		return;
	}

	fDestinationChannelID = dcid;
	fChannelConfig.outgoing_mtu = mtu;
	fLETxMPS = mps;
	fLETxCredits = credits;
	_MarkEstablished();
	_SendLECreditQueued();
}


void
L2capEndpoint::_HandleLECredits(uint16 credits)
{
	MutexLocker locker(fLock);
	if (fLEMode == 0 || fState != OPEN)
		return;

	// More than 65535 credits is an error that closes the channel (4.24).
	if ((uint32)fLETxCredits + credits > 0xffff) {
		ERROR("l2cap: credit overflow on channel %#x\n", fChannelID);
		_LEDisconnect();
		return;
	}
	fLETxCredits += credits;
	_SendLECreditQueued();
}


uint16
L2capEndpoint::_HandleLEReconfigure(uint16 mtu, uint16 mps, bool apply)
{
	MutexLocker locker(fLock);
	if (fLEMode != R5_L2CAP_MODE_ENHANCED_CREDIT || fState != OPEN)
		return 0x0003;	// one or more destination CIDs invalid
	// The MTU may only grow (4.27).
	if (mtu < fChannelConfig.outgoing_mtu)
		return 0x0001;
	if (apply) {
		fChannelConfig.outgoing_mtu = mtu;
		fLETxMPS = mps;
	}
	return 0x0000;
}


// #pragma mark - LE signalling


static status_t
send_le_values(HciConnection* conn, uint8 code, uint8 ident,
	const uint16* values, int count)
{
	// Header room for the L2CAP and ACL headers that go in front, as the
	// Classic commands have (l2cap_command.cpp). With 2 bytes each header
	// went into a separate piece, and h2generic panicked on the
	// non-contiguous buffer ("expected to be contiguous", the first LE
	// Connection Parameter Update Response, 5625U stick 4).
	net_buffer* buffer = gBufferModule->create(128);
	if (buffer == NULL)
		return ENOMEM;
	for (int i = 0; i < count; i++) {
		const uint16 value = htole16(values[i]);
		if (gBufferModule->append(buffer, &value, sizeof(value)) != B_OK) {
			gBufferModule->free(buffer);
			return ENOMEM;
		}
	}
	return send_l2cap_command(conn, code, ident, buffer);
}


status_t
send_l2cap_le_credit_connection_req(HciConnection* conn, uint8 ident,
	bool enhanced, uint16 psm, uint16 scid, uint16 mtu, uint16 mps,
	uint16 credits)
{
	if (enhanced) {
		const uint16 values[] = { psm, mtu, mps, credits, scid };
		return send_le_values(conn, 0x17, ident, values, 5);
	}
	const uint16 values[] = { psm, scid, mtu, mps, credits };
	return send_le_values(conn, 0x14, ident, values, 5);
}


status_t
send_l2cap_le_flow_control_credit(HciConnection* conn, uint16 cid,
	uint16 credits)
{
	uint8 ident = btCoreData->allocate_command_ident(conn, NULL);
	if (ident == L2CAP_NULL_IDENT)
		return ENOBUFS;
	const uint16 values[] = { cid, credits };
	status_t status = send_le_values(conn, 0x16, ident, values, 2);
	// An indication gets no answer.
	btCoreData->free_command_ident(conn, ident);
	return status;
}


// The parameters a peripheral asks for (Vol 3, Part A, 4.20; Vol 6,
// Part B, 4.5.1 and 4.5.2): interval 7.5 ms to 4 s, latency below 500,
// and a supervision timeout longer than the time the link may sleep.
static bool
valid_connection_parameters(uint16 minimum, uint16 maximum, uint16 latency,
	uint16 timeout)
{
	if (minimum < 6 || maximum > 3200 || minimum > maximum)
		return false;
	if (latency > 499 || timeout < 10 || timeout > 3200)
		return false;
	// timeout (10 ms units) > (1 + latency) * maximum (1.25 ms units) * 2
	return (uint32)timeout * 4 > (1 + (uint32)latency) * maximum;
}


static void
update_connection(HciConnection* conn, const uint16* values)
{
	// LE Connection Update (OGF 0x08, OCF 0x0013; Vol 4, Part E, 7.8.18):
	// handle, interval min and max, latency, timeout, CE lengths 0. Its
	// Command Status and LE Connection Update Complete go to
	// bluetooth_server, which only logs them.
	uint8 command[3 + 14];
	command[0] = 0x13;
	command[1] = 0x20;
	command[2] = 14;
	const uint16 fields[7] = { conn->handle, values[0], values[1], values[2],
		values[3], 0, 0 };
	for (int i = 0; i < 7; i++) {
		command[3 + 2 * i] = fields[i] & 0xff;
		command[4 + 2 * i] = fields[i] >> 8;
	}

	net_buffer* buffer = gBufferModule->create(sizeof(command));
	if (buffer == NULL)
		return;
	if (gBufferModule->append(buffer, command, sizeof(command)) == B_OK)
		btDevices->PostCommand(conn->Hid, buffer);
	// The transport copies a command; the buffer stays ours.
	gBufferModule->free(buffer);
}


static uint16
spawn_for(L2capEndpoint* listener, HciConnection* conn, uint8 mode,
	uint16 scid, uint16 mtu, uint16 mps, uint16 credits, L2capEndpoint** child)
{
	*child = NULL;
	if (listener == NULL)
		return kLEResultPSMNotSupported;
	if (!l2cap_parse_valid_le_cid(scid))
		return kLEResultInvalidSourceCID;
	L2capEndpoint* existing = gL2capEndpointManager.GetForDestination(conn, scid);
	if (existing != NULL) {
		gSocketModule->release_socket(existing->socket);
		return kLEResultSourceCIDInUse;
	}
	return listener->_SpawnLECredit(conn, mode, scid, mtu, mps, credits, child);
}


void
l2cap_handle_le_command(HciConnection* conn, uint8 code, uint8 ident,
	net_buffer* buffer, uint16 length, L2capEndpoint* endpoint)
{
	// The longest command read here is a Credit Based Connection Request
	// with 5 channels: 8 + 10 bytes. l2cap_parse_check() made sure the
	// buffer holds "length" bytes and at least the code's minimum.
	uint16 values[9] = {};
	const size_t size = min_c((size_t)length, sizeof(values));
	uint8 raw[sizeof(values)];
	if (gBufferModule->read(buffer, 0, raw, size) != B_OK)
		return;
	for (size_t i = 0; i + 1 < size; i += 2)
		values[i / 2] = raw[i] | (raw[i + 1] << 8);

	switch (code) {
		case 0x12:
		{
			// Connection Parameter Update Request: interval min and max,
			// latency, timeout. We are the central of every LE link.
			const bool valid = valid_connection_parameters(values[0],
				values[1], values[2], values[3]);
			const uint16 result = valid ? 0x0000 : 0x0001;
			send_le_values(conn, 0x13, ident, &result, 1);
			if (valid)
				update_connection(conn, values);
			break;
		}

		case 0x13:
			// We never ask for new parameters.
			break;

		case 0x14:
		{
			// LE Credit Based Connection Request: PSM, SCID, MTU, MPS,
			// credits.
			L2capEndpoint* listener = gL2capEndpointManager.GetForLEPSM(
				values[0]);
			L2capEndpoint* child = NULL;
			uint16 result = kLEResultUnacceptable;
			if (listener == NULL)
				result = kLEResultPSMNotSupported;
			else if (l2cap_parse_valid_coc(values[2], values[3], false)) {
				result = spawn_for(listener, conn, R5_L2CAP_MODE_LE_CREDIT,
					values[1], values[2], values[3], values[4], &child);
			}
			const uint16 dcid = child != NULL ? child->ChannelID() : 0;
			const uint16 answer[] = { dcid,
				kLEMTU, kLEMPS, kLECredits, result };
			status_t status = send_le_values(conn, 0x15, ident, answer, 5);
			if (child != NULL)
				child->_FinishLECreditSpawn(status == B_OK);
			if (listener != NULL)
				gSocketModule->release_socket(listener->socket);
			break;
		}

		case 0x15:
			// LE Credit Based Connection Response: DCID, MTU, MPS,
			// credits, result. Only for a request of that mode.
			if (endpoint != NULL
				&& endpoint->LEMode() != R5_L2CAP_MODE_LE_CREDIT) {
				endpoint->_HandleBadResponse(ident);
			} else if (endpoint != NULL) {
				endpoint->_HandleLECreditConnectionRsp(ident, values[0],
					values[1], values[2], values[3], values[4]);
			}
			break;

		case 0x16:
		{
			// Flow Control Credit Indication: the sender's CID (ours to
			// send to) and the credits.
			L2capEndpoint* target = gL2capEndpointManager.GetForDestination(
				conn, values[0]);
			if (target != NULL) {
				target->_HandleLECredits(values[1]);
				gSocketModule->release_socket(target->socket);
			}
			break;
		}

		case 0x17:
		{
			// Credit Based Connection Request: PSM, MTU, MPS, credits, then
			// 1 to 5 source CIDs.
			const int count = l2cap_parse_cid_count(length, 8);
			if (count == 0) {
				send_l2cap_command_reject(conn, ident,
					l2cap_command_reject::REJECTED_NOT_UNDERSTOOD, 0, 0, 0);
				break;
			}
			L2capEndpoint* listener = gL2capEndpointManager.GetForLEPSM(
				values[0]);
			L2capEndpoint* children[L2CAP_PARSE_ECRED_MAX_CHANNELS] = {};
			uint16 answer[4 + L2CAP_PARSE_ECRED_MAX_CHANNELS]
				= { kLEMTU, kLEMPS, kLECredits, kLEResultSuccess };
			uint16 result = kLEResultSuccess;
			if (listener == NULL)
				result = kLEResultPSMNotSupported;
			else if (!l2cap_parse_valid_coc(values[1], values[2], true))
				result = kLEResultUnacceptable;
			for (int i = 0; i < count; i++) {
				uint16 one = result;
				if (result == kLEResultSuccess) {
					one = spawn_for(listener, conn,
						R5_L2CAP_MODE_ENHANCED_CREDIT, values[4 + i], values[1],
						values[2], values[3], &children[i]);
				}
				answer[4 + i] = (uint16)(children[i] != NULL
					? children[i]->ChannelID() : 0);
				if (one != kLEResultSuccess && answer[3] == kLEResultSuccess)
					answer[3] = one;
			}
			status_t status = send_le_values(conn, 0x18, ident, answer,
				4 + count);
			for (int i = 0; i < count; i++) {
				if (children[i] != NULL)
					children[i]->_FinishLECreditSpawn(status == B_OK);
			}
			if (listener != NULL)
				gSocketModule->release_socket(listener->socket);
			break;
		}

		case 0x18:
			// Credit Based Connection Response: MTU, MPS, credits, result,
			// the DCIDs. We ask for one channel at a time; it is open when
			// its DCID is not 0.
			if (endpoint != NULL
				&& endpoint->LEMode() != R5_L2CAP_MODE_ENHANCED_CREDIT) {
				endpoint->_HandleBadResponse(ident);
			} else if (endpoint != NULL) {
				const uint16 dcid = length >= 10 ? values[4] : 0;
				uint16 result = values[3];
				if (dcid == 0 && result == kLEResultSuccess)
					result = kLEResultNoResources;
				else if (dcid != 0)
					result = kLEResultSuccess;
				endpoint->_HandleLECreditConnectionRsp(ident, dcid, values[0],
					values[1], values[2], result);
			}
			break;

		case 0x19:
		{
			// Credit Based Reconfigure Request: MTU, MPS, then the sender's
			// CIDs. All of them or none (4.27).
			const int count = l2cap_parse_cid_count(length, 4);
			uint16 result = 0x0000;
			if (count == 0 || !l2cap_parse_valid_coc(values[0], values[1], true))
				result = 0x0004;
			for (int pass = 0; pass < 2 && result == 0x0000; pass++) {
				for (int i = 0; i < count && result == 0x0000; i++) {
					L2capEndpoint* target
						= gL2capEndpointManager.GetForDestination(conn,
							values[2 + i]);
					if (target == NULL) {
						result = 0x0003;
						break;
					}
					result = target->_HandleLEReconfigure(values[0], values[1],
						pass == 1);
					gSocketModule->release_socket(target->socket);
				}
			}
			send_le_values(conn, 0x1a, ident, &result, 1);
			break;
		}

		case 0x1a:
			// We never reconfigure.
			break;
	}
}


// #pragma mark - endpoint manager


status_t
L2capEndpointManager::BindLE(L2capEndpoint* endpoint, const sockaddr* _address)
{
	const sockaddr_l2cap_le& address = *(const sockaddr_l2cap_le*)_address;
	const bdaddr_t anyAddr = BDADDR_ANY;
	if (memcmp(&address.l2cap_bdaddr, &anyAddr, sizeof(bdaddr_t)) != 0)
		return EINVAL;
	// LE PSMs: 0x0001 to 0x007F fixed by the SIG, 0x0080 to 0x00FF
	// dynamic (Vol 3, Part A, 4.22).
	if (address.l2cap_psm == 0 || address.l2cap_psm > 0x00ff)
		return EINVAL;

	WriteLocker _(fBoundEndpointsLock);
	if (fLEBoundEndpoints.Find(address.l2cap_psm) != NULL)
		return EADDRINUSE;

	memcpy(*endpoint->LocalAddress(), &address, sizeof(sockaddr_l2cap_le));
	endpoint->fLEBound = true;
	fLEBoundEndpoints.Insert(endpoint);
	gSocketModule->acquire_socket(endpoint->socket);
	return B_OK;
}


L2capEndpoint*
L2capEndpointManager::GetForLEPSM(uint16 psm)
{
	ReadLocker _(fBoundEndpointsLock);
	L2capEndpoint* endpoint = fLEBoundEndpoints.Find(psm);
	if (endpoint != NULL)
		gSocketModule->acquire_socket(endpoint->socket);
	return endpoint;
}


status_t
L2capEndpointManager::BindFixed(L2capEndpoint* endpoint, const sockaddr* _address)
{
	const sockaddr_l2cap_le& address = *(const sockaddr_l2cap_le*)_address;
	const bdaddr_t anyAddr = BDADDR_ANY;
	if (memcmp(&address.l2cap_bdaddr, &anyAddr, sizeof(bdaddr_t)) != 0
		|| address.l2cap_psm != 0)
		return EINVAL;

	WriteLocker _(fBoundEndpointsLock);
	L2capEndpoint** slot;
	switch (address.l2cap_cid) {
		case R5_L2CAP_CID_ATT:	slot = &fFixedATT; break;
		case R5_L2CAP_CID_SMP:	slot = &fFixedSMP; break;
		default:				return EINVAL;
	}
	if (*slot != NULL)
		return EADDRINUSE;

	memcpy(*endpoint->LocalAddress(), &address, sizeof(sockaddr_l2cap_le));
	endpoint->fFixedChannel = address.l2cap_cid;
	*slot = endpoint;
	gSocketModule->acquire_socket(endpoint->socket);
	return B_OK;
}


L2capEndpoint*
L2capEndpointManager::GetForFixed(uint16 cid)
{
	ReadLocker _(fBoundEndpointsLock);
	L2capEndpoint* endpoint = NULL;
	if (cid == R5_L2CAP_CID_ATT)
		endpoint = fFixedATT;
	else if (cid == R5_L2CAP_CID_SMP)
		endpoint = fFixedSMP;
	if (endpoint != NULL)
		gSocketModule->acquire_socket(endpoint->socket);
	return endpoint;
}


status_t
L2capEndpointManager::BindToLEChannel(L2capEndpoint* endpoint)
{
	WriteLocker _(fChannelEndpointsLock);

	const uint16 count = R5_L2CAP_LE_LAST_CID - R5_L2CAP_LE_FIRST_CID + 1;
	for (uint16 i = 0; i < count; i++) {
		const uint16 cid = fNextLEChannelID;
		fNextLEChannelID++;
		if (fNextLEChannelID > R5_L2CAP_LE_LAST_CID)
			fNextLEChannelID = R5_L2CAP_LE_FIRST_CID;
		if (fChannelEndpoints.Find(cid) != NULL)
			continue;

		endpoint->fChannelID = cid;
		fChannelEndpoints.Insert(endpoint);
		gSocketModule->acquire_socket(endpoint->socket);
		return B_OK;
	}

	return EADDRINUSE;
}


L2capEndpoint*
L2capEndpointManager::GetForDestination(HciConnection* connection, uint16 dcid)
{
	ReadLocker _(fChannelEndpointsLock);
	auto iter = fChannelEndpoints.GetIterator();
	while (iter.HasNext()) {
		L2capEndpoint* endpoint = iter.Next();
		if (endpoint->fConnection == connection
			&& endpoint->fDestinationChannelID == dcid
			&& endpoint->fLEMode != 0) {
			gSocketModule->acquire_socket(endpoint->socket);
			return endpoint;
		}
	}
	return NULL;
}
