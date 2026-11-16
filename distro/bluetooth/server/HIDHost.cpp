/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth Classic HID host, HID profile 1.1.1 over BR/EDR L2CAP. Only a
 * device that is paired and encrypted gets a report descriptor
 * (DeviceReady() comes from SDPClient after Encryption Change), and only
 * then are its reports passed on. Reports are not logged.
 */


#include "HIDHost.h"

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <Autolock.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <R5BluetoothHID.h>

#include "Debug.h"


// HID profile 1.1.1, 5.2 and 7.4: the PSMs and the transaction headers.
static const uint16 kPSMControl = 0x0011;
static const uint16 kPSMInterrupt = 0x0013;
static const uint8 kDataInput = 0xA1;		// DATA | Input
static const uint8 kDataOutput = 0xA2;		// DATA | Output
static const uint8 kVirtualCableUnplug = 0x15;	// HID_CONTROL | unplug

// The other side often opens the channels itself right after the link
// comes up; wait this long before opening them from here.
static const bigtime_t kConnectDelay = 1500000;


struct HIDHost::Session {
	bdaddr_t	address;
	int			channel[2];		// control, interrupt
	bool		reading[2];
	thread_id	readers[2];
	uint8*		descriptor;
	size_t		descriptorLength;
	int32		kernelID;
	bool		connecting;

	Session(const bdaddr_t& _address)
		:
		address(_address),
		descriptor(NULL),
		descriptorLength(0),
		kernelID(-1),
		connecting(false)
	{
		for (int i = 0; i < 2; i++) {
			channel[i] = -1;
			reading[i] = false;
			readers[i] = -1;
		}
	}

	~Session()
	{
		free(descriptor);
	}
};


struct reader_args {
	HIDHost*			host;
	void*				session;
	bool				interrupt;
};


struct listen_args {
	HIDHost*	host;
	uint16		psm;
};


HIDHost::HIDHost()
	:
	fLock("bluetooth hid host"),
	fControlNode(-1),
	fOutputThread(-1),
	fQuitting(false)
{
	for (int i = 0; i < 2; i++) {
		fListenSockets[i] = -1;
		fListenThreads[i] = -1;
	}
}


HIDHost::~HIDHost()
{
	Stop();
}


status_t
HIDHost::Start()
{
	fControlNode = open("/dev/" R5_BTHID_CONTROL_NAME, O_RDWR | O_CLOEXEC);
	if (fControlNode < 0) {
		syslog(LOG_WARNING, "bluetooth_hid: no driver (%s), HID off",
			strerror(errno));
		return errno;
	}

	static const uint16 psms[2] = { kPSMControl, kPSMInterrupt };
	for (int i = 0; i < 2; i++) {
		int fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
		if (fd < 0)
			continue;
		struct sockaddr_l2cap address;
		memset(&address, 0, sizeof(address));
		address.l2cap_len = sizeof(address);
		address.l2cap_family = AF_BLUETOOTH;
		address.l2cap_bdaddr = BDADDR_ANY;
		address.l2cap_psm = B_HOST_TO_LENDIAN_INT16(psms[i]);
		if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0
			|| listen(fd, 4) < 0) {
			syslog(LOG_WARNING, "bluetooth_hid: cannot listen on PSM %#x: %s",
				psms[i], strerror(errno));
			close(fd);
			continue;
		}
		fListenSockets[i] = fd;

		listen_args* args = new(std::nothrow) listen_args;
		if (args == NULL)
			continue;
		args->host = this;
		args->psm = psms[i];
		fListenThreads[i] = spawn_thread(_ListenThread, "bt hid listen",
			B_NORMAL_PRIORITY, args);
		if (fListenThreads[i] < 0)
			delete args;
		else
			resume_thread(fListenThreads[i]);
	}

	fOutputThread = spawn_thread(_OutputThread, "bt hid output",
		B_NORMAL_PRIORITY, this);
	if (fOutputThread >= 0)
		resume_thread(fOutputThread);

	TRACE_BT("HIDHost: started\n");
	return B_OK;
}


void
HIDHost::Stop()
{
	if (fControlNode < 0)
		return;
	fQuitting = true;

	for (int i = 0; i < 2; i++) {
		if (fListenSockets[i] >= 0) {
			shutdown(fListenSockets[i], SHUT_RDWR);
			close(fListenSockets[i]);
			fListenSockets[i] = -1;
		}
	}

	{
		BAutolock locker(fLock);
		for (int32 i = 0; i < fSessions.CountItems(); i++) {
			Session* session = fSessions.ItemAt(i);
			_CloseLocked(session);
			// The server quits: no new descriptor can take these numbers,
			// so closing here is safe, and it ends a recv() that shutdown()
			// did not wake.
			for (int j = 0; j < 2; j++) {
				if (session->reading[j] && session->channel[j] >= 0)
					close(session->channel[j]);
			}
		}
	}

	status_t result;
	for (int i = 0; i < 2; i++) {
		if (fListenThreads[i] >= 0)
			wait_for_thread(fListenThreads[i], &result);
	}
	if (fOutputThread >= 0)
		wait_for_thread(fOutputThread, &result);

	// The readers end once their channels are shut down.
	for (int32 i = 0; i < fSessions.CountItems(); i++) {
		Session* session = fSessions.ItemAt(i);
		for (int j = 0; j < 2; j++) {
			if (session->readers[j] >= 0)
				wait_for_thread(session->readers[j], &result);
		}
	}

	close(fControlNode);
	fControlNode = -1;
}


void
HIDHost::DeviceReady(const bdaddr_t& address, const uint8* descriptor,
	size_t length)
{
	if (fControlNode < 0 || length == 0 || length > R5_BTHID_MAX_DESCRIPTOR)
		return;

	BAutolock locker(fLock);
	Session* session = _FindOrAddLocked(address);
	if (session == NULL)
		return;

	uint8* copy = (uint8*)malloc(length);
	if (copy == NULL)
		return;
	memcpy(copy, descriptor, length);
	free(session->descriptor);
	session->descriptor = copy;
	session->descriptorLength = length;

	if (session->channel[0] >= 0 && session->channel[1] >= 0) {
		_TryStartLocked(session);
		return;
	}

	if (session->connecting)
		return;
	session->connecting = true;
	reader_args* args = new(std::nothrow) reader_args;
	if (args == NULL) {
		session->connecting = false;
		return;
	}
	args->host = this;
	args->session = session;
	args->interrupt = false;
	thread_id thread = spawn_thread(_ConnectThread, "bt hid connect",
		B_NORMAL_PRIORITY, args);
	if (thread < 0) {
		delete args;
		session->connecting = false;
		return;
	}
	resume_thread(thread);
}


/*static*/ bool
HIDHost::FindReportDescriptor(const uint8* list, size_t listLength,
	const uint8** _start, size_t* _length)
{
	// Data elements (Core 6.0, Vol 3, Part B, 3): a header byte with the
	// type in bits 7-3 and the size index in bits 2-0, then for size index
	// 5, 6 and 7 a length of 1, 2 or 4 bytes. Wanted: the first sequence
	// (type 6), in it an unsigned 8-bit 0x22 (Report), then a text string
	// or a byte sequence (type 4) with the descriptor.
	struct Reader {
		const uint8*	data;
		size_t			length;
		size_t			position;

		bool Next(uint8* type, const uint8** body, size_t* bodyLength)
		{
			if (position >= length)
				return false;
			uint8 header = data[position++];
			*type = header >> 3;
			uint8 sizeIndex = header & 0x7;
			size_t size;
			if (*type == 0)
				size = 0;
			else if (sizeIndex <= 4)
				size = (size_t)1 << sizeIndex;
			else {
				size_t lengthBytes = (size_t)1 << (sizeIndex - 5);
				if (length - position < lengthBytes)
					return false;
				size = 0;
				for (size_t i = 0; i < lengthBytes; i++)
					size = (size << 8) | data[position++];
			}
			if (length - position < size)
				return false;
			*body = data + position;
			*bodyLength = size;
			position += size;
			return true;
		}
	};

	if (list == NULL)
		return false;

	Reader outer = { list, listLength, 0 };
	uint8 type;
	const uint8* body;
	size_t bodyLength;
	while (outer.Next(&type, &body, &bodyLength)) {
		if (type != 6)
			continue;

		Reader inner = { body, bodyLength, 0 };
		if (!inner.Next(&type, &body, &bodyLength) || type != 1
			|| bodyLength != 1 || body[0] != 0x22)
			continue;
		if (!inner.Next(&type, &body, &bodyLength) || type != 4
			|| bodyLength == 0)
			continue;

		*_start = body;
		*_length = bodyLength;
		return true;
	}
	return false;
}


// #pragma mark - threads


/*static*/ int32
HIDHost::_ListenThread(void* data)
{
	listen_args* args = (listen_args*)data;
	HIDHost* host = args->host;
	uint16 psm = args->psm;
	delete args;
	host->_Listen(psm);
	return 0;
}


/*static*/ int32
HIDHost::_ConnectThread(void* data)
{
	reader_args* args = (reader_args*)data;
	HIDHost* host = args->host;
	Session* session = (Session*)args->session;
	delete args;
	host->_Connect(session);
	return 0;
}


/*static*/ int32
HIDHost::_ReadThread(void* data)
{
	reader_args* args = (reader_args*)data;
	HIDHost* host = args->host;
	Session* session = (Session*)args->session;
	bool interrupt = args->interrupt;
	delete args;
	host->_Read(session, interrupt);
	return 0;
}


/*static*/ int32
HIDHost::_OutputThread(void* data)
{
	((HIDHost*)data)->_Output();
	return 0;
}


void
HIDHost::_Listen(uint16 psm)
{
	int index = psm == kPSMControl ? 0 : 1;
	while (!fQuitting) {
		struct sockaddr_l2cap peer;
		memset(&peer, 0, sizeof(peer));
		socklen_t length = sizeof(peer);
		int fd = accept(fListenSockets[index], (struct sockaddr*)&peer,
			&length);
		if (fd < 0) {
			if (fQuitting)
				break;
			snooze(100000);
			continue;
		}

		BAutolock locker(fLock);
		Session* session = _FindOrAddLocked(peer.l2cap_bdaddr);
		if (session == NULL || session->channel[index] >= 0 || fQuitting) {
			close(fd);
			continue;
		}
		session->channel[index] = fd;
		TRACE_BT("HIDHost: %s opened PSM %#x\n",
			bdaddrUtils::ToString(peer.l2cap_bdaddr).String(), psm);
		_TryStartLocked(session);
	}
}


void
HIDHost::_Connect(Session* session)
{
	snooze(kConnectDelay);

	static const uint16 psms[2] = { kPSMControl, kPSMInterrupt };
	for (int i = 0; i < 2 && !fQuitting; i++) {
		bdaddr_t address;
		{
			BAutolock locker(fLock);
			if (session->channel[i] >= 0)
				continue;
			address = session->address;
		}

		// The control channel first (HID profile 1.1.1, 5.2.2).
		int fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
		if (fd < 0)
			break;
		struct sockaddr_l2cap remote;
		memset(&remote, 0, sizeof(remote));
		remote.l2cap_len = sizeof(remote);
		remote.l2cap_family = AF_BLUETOOTH;
		remote.l2cap_bdaddr = address;
		remote.l2cap_psm = B_HOST_TO_LENDIAN_INT16(psms[i]);
		if (connect(fd, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
			syslog(LOG_NOTICE, "bluetooth_hid: cannot open PSM %#x: %s",
				psms[i], strerror(errno));
			close(fd);
			break;
		}

		BAutolock locker(fLock);
		if (session->channel[i] >= 0) {
			// The device opened it meanwhile.
			close(fd);
			continue;
		}
		session->channel[i] = fd;
	}

	BAutolock locker(fLock);
	session->connecting = false;
	_TryStartLocked(session);
}


void
HIDHost::_Read(Session* session, bool interrupt)
{
	int index = interrupt ? 1 : 0;
	int fd;
	{
		BAutolock locker(fLock);
		fd = session->channel[index];
	}

	uint8 buffer[R5_BTHID_MAX_REPORT + 1];
	while (true) {
		ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
		if (received <= 0)
			break;

		if (!interrupt) {
			// The device leaves for good (HID profile 1.1.1, 7.4.2).
			if (buffer[0] == kVirtualCableUnplug)
				break;
			continue;
		}

		if (received < 2 || buffer[0] != kDataInput)
			continue;

		int32 id;
		{
			BAutolock locker(fLock);
			id = session->kernelID;
		}
		if (id < 0)
			break;

		r5_bthid_input input;
		input.id = id;
		input.length = received - 1;
		input.data = buffer + 1;
		ioctl(fControlNode, R5_BTHID_INPUT, &input, sizeof(input));
	}

	// This reader owns its descriptor: only it closes it, so the number
	// cannot be reused while it still reads.
	BAutolock locker(fLock);
	close(fd);
	session->channel[index] = -1;
	session->reading[index] = false;
	_CloseLocked(session);
}


void
HIDHost::_Output()
{
	uint8 buffer[R5_BTHID_MAX_REPORT + 1];
	while (!fQuitting) {
		r5_bthid_output output;
		memset(&output, 0, sizeof(output));
		output.timeout = 500000;
		output.data = buffer + 1;
		output.capacity = R5_BTHID_MAX_REPORT;
		if (ioctl(fControlNode, R5_BTHID_GET_OUTPUT, &output,
				sizeof(output)) != 0)
			continue;
		if (output.length == 0 || output.length > R5_BTHID_MAX_REPORT)
			continue;

		BAutolock locker(fLock);
		Session* session = _FindByKernelIDLocked(output.id);
		if (session == NULL || session->channel[1] < 0)
			continue;
		buffer[0] = kDataOutput;
		send(session->channel[1], buffer, output.length + 1, 0);
	}
}


// #pragma mark - sessions (fLock held)


HIDHost::Session*
HIDHost::_FindLocked(const bdaddr_t& address)
{
	for (int32 i = 0; i < fSessions.CountItems(); i++) {
		Session* session = fSessions.ItemAt(i);
		if (bdaddrUtils::Compare(session->address, address))
			return session;
	}
	return NULL;
}


HIDHost::Session*
HIDHost::_FindByKernelIDLocked(int32 id)
{
	for (int32 i = 0; i < fSessions.CountItems(); i++) {
		Session* session = fSessions.ItemAt(i);
		if (session->kernelID == id)
			return session;
	}
	return NULL;
}


HIDHost::Session*
HIDHost::_FindOrAddLocked(const bdaddr_t& address)
{
	// Sessions stay until the server quits (one per device that ever
	// connected), so threads never hold a deleted one.
	Session* session = _FindLocked(address);
	if (session != NULL)
		return session;

	if (fSessions.CountItems() >= 64)
		return NULL;
	session = new(std::nothrow) Session(address);
	if (session == NULL || !fSessions.AddItem(session)) {
		delete session;
		return NULL;
	}
	return session;
}


void
HIDHost::_TryStartLocked(Session* session)
{
	if (fQuitting || session->kernelID >= 0 || session->descriptor == NULL
		|| session->channel[0] < 0 || session->channel[1] < 0
		|| session->reading[0] || session->reading[1])
		return;

	r5_bthid_create create;
	memset(&create, 0, sizeof(create));
	create.descriptor = session->descriptor;
	create.descriptor_length = session->descriptorLength;
	if (ioctl(fControlNode, R5_BTHID_CREATE, &create, sizeof(create)) != 0) {
		syslog(LOG_WARNING, "bluetooth_hid: device not supported (%s)",
			strerror(errno));
		_CloseLocked(session);
		return;
	}
	session->kernelID = create.id;

	for (int i = 0; i < 2; i++) {
		reader_args* args = new(std::nothrow) reader_args;
		if (args == NULL)
			break;
		args->host = this;
		args->session = session;
		args->interrupt = i == 1;
		thread_id thread = spawn_thread(_ReadThread,
			i == 0 ? "bt hid control" : "bt hid interrupt",
			B_URGENT_DISPLAY_PRIORITY, args);
		if (thread < 0) {
			delete args;
			break;
		}
		session->reading[i] = true;
		session->readers[i] = thread;
		resume_thread(thread);
	}
	if (!session->reading[0] || !session->reading[1]) {
		_CloseLocked(session);
		return;
	}

	syslog(LOG_INFO, "bluetooth_hid: br-edr hid ready (%" B_PRIu32
		" input device%s)", create.handlers, create.handlers == 1 ? "" : "s");
}


void
HIDHost::_CloseLocked(Session* session)
{
	if (session->kernelID >= 0) {
		int32 id = session->kernelID;
		ioctl(fControlNode, R5_BTHID_DESTROY, &id, sizeof(id));
		session->kernelID = -1;
		syslog(LOG_INFO, "bluetooth_hid: device gone");
	}

	for (int i = 0; i < 2; i++) {
		if (session->channel[i] < 0)
			continue;
		if (session->reading[i]) {
			// Wakes the reader, which closes the descriptor itself.
			shutdown(session->channel[i], SHUT_RDWR);
		} else {
			close(session->channel[i]);
			session->channel[i] = -1;
		}
	}
}
