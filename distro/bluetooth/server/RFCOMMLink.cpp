/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * RFCOMMLink.h: an RFCOMM DLC on an L2CAP socket (docs/BLUETOOTH.md,
 * step 7a). Nothing here logs an address.
 */


#include "RFCOMMLink.h"

#include <errno.h>
#include <new>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>

#include <Autolock.h>
#include <ByteOrder.h>

#include <bluetooth/l2cap.h>
#include <bluetooth/L2CAP/btL2CAP.h>


using r5rfcomm::Mux;


static const uint16 kPSMRFCOMM = 0x0003;
// The reader wakes this often to run the mux's timeouts.
static const bigtime_t kPoll = 250000;
// Close() waits for DISC on the DLC and on DLCI 0, each with its timeout.
static const bigtime_t kCloseWait = 2 * r5rfcomm::kTimeout + 1000000;


class SocketTransport : public RFCOMMTransport {
public:
	SocketTransport(int fd)
		:
		fSocket(fd)
	{
	}

	virtual ~SocketTransport()
	{
		if (fSocket >= 0)
			close(fSocket);
	}

	virtual status_t Send(const void* data, size_t length)
	{
		ssize_t sent = send(fSocket, data, length, 0);
		if (sent < 0)
			return errno;
		return (size_t)sent == length ? B_OK : B_ERROR;
	}

	virtual ssize_t Receive(void* buffer, size_t size, bigtime_t)
	{
		// SO_RCVTIMEO is kPoll (Connect()).
		ssize_t received = recv(fSocket, buffer, size, 0);
		if (received > 0)
			return received;
		if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK
				|| errno == B_TIMED_OUT || errno == EINTR))
			return B_TIMED_OUT;
		return B_ERROR;
	}

	virtual void Shutdown()
	{
		shutdown(fSocket, SHUT_RDWR);
	}

private:
	int		fSocket;
};


static status_t
error_status(Mux::Error error)
{
	switch (error) {
		case Mux::kTimedOut:
			return B_TIMED_OUT;
		case Mux::kRefused:
			return ECONNREFUSED;
		case Mux::kRemoteClosed:
			return ECONNRESET;
		case Mux::kProtocol:
			return B_BAD_DATA;
		default:
			return B_ERROR;
	}
}


RFCOMMLink::RFCOMMLink(RFCOMMTransport* transport, bool initiator)
	:
	fTransport(transport),
	fLock("rfcomm link"),
	fMux(initiator, _Output, this),
	fChanged(create_sem(0, "rfcomm link")),
	fReader(-1),
	fQuitting(false),
	fGone(false),
	fClosed(false),
	fLineLength(0)
{
}


RFCOMMLink::~RFCOMMLink()
{
	Close();
	delete fTransport;
	if (fChanged >= 0)
		delete_sem(fChanged);
}


status_t
RFCOMMLink::Connect(const bdaddr_t& address, uint8 channel,
	bigtime_t timeout, RFCOMMLink** _link)
{
	if (_link == NULL || channel < r5rfcomm::kMinChannel
		|| channel > r5rfcomm::kMaxChannel)
		return B_BAD_VALUE;

	int fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (fd < 0)
		return errno;
	struct sockaddr_l2cap remote;
	memset(&remote, 0, sizeof(remote));
	remote.l2cap_len = sizeof(remote);
	remote.l2cap_family = AF_BLUETOOTH;
	remote.l2cap_bdaddr = address;
	remote.l2cap_psm = B_HOST_TO_LENDIAN_INT16(kPSMRFCOMM);
	// The L2CAP connect has the kernel's own timeout.
	if (connect(fd, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
		status_t status = errno;
		syslog(LOG_NOTICE, "bluetooth_server: RFCOMM: cannot open PSM 3: %s",
			strerror(status));
		close(fd);
		return status;
	}
	timeval wait;
	wait.tv_sec = 0;
	wait.tv_usec = kPoll;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait)) < 0) {
		status_t status = errno;
		close(fd);
		return status;
	}

	SocketTransport* transport = new(std::nothrow) SocketTransport(fd);
	if (transport == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}
	status_t status = Start(transport, true, channel, timeout, _link);
	if (status != B_OK) {
		syslog(LOG_NOTICE, "bluetooth_server: RFCOMM: channel %u not "
			"opened: %s", channel, strerror(status));
	} else {
		syslog(LOG_INFO, "bluetooth_server: RFCOMM: channel %u open, "
			"frames of %u bytes, %s", channel, (*_link)->MaxFrame(),
			(*_link)->fMux.CreditFlow() ? "credit flow" : "no credit flow");
	}
	return status;
}


status_t
RFCOMMLink::Start(RFCOMMTransport* transport, bool initiator, uint8 channel,
	bigtime_t timeout, RFCOMMLink** _link)
{
	if (transport == NULL)
		return B_BAD_VALUE;
	if (_link == NULL) {
		delete transport;
		return B_BAD_VALUE;
	}
	RFCOMMLink* link = new(std::nothrow) RFCOMMLink(transport, initiator);
	if (link == NULL) {
		delete transport;
		return B_NO_MEMORY;
	}
	if (link->fChanged < 0) {
		delete link;
		return B_NO_MORE_SEMS;
	}

	bool started;
	{
		BAutolock locker(link->fLock);
		started = initiator ? link->fMux.Open(channel, system_time())
			: link->fMux.Listen(channel);
	}
	if (!started) {
		delete link;
		return B_BAD_VALUE;
	}

	link->fReader = spawn_thread(_ReaderThread, "rfcomm reader",
		B_NORMAL_PRIORITY, link);
	if (link->fReader < 0) {
		status_t status = link->fReader;
		delete link;
		return status;
	}
	resume_thread(link->fReader);

	status_t status = link->_WaitOpen(timeout);
	if (status != B_OK) {
		delete link;
		return status;
	}
	*_link = link;
	return B_OK;
}


status_t
RFCOMMLink::Send(const void* data, size_t length, bigtime_t timeout)
{
	const uint8* bytes = (const uint8*)data;
	if (bytes == NULL && length > 0)
		return B_BAD_VALUE;
	bigtime_t end = system_time() + timeout;
	size_t offset = 0;
	while (offset < length) {
		{
			BAutolock locker(fLock);
			if (_Ended() || fMux.GetState() != Mux::kOpen)
				return B_ERROR;
			size_t space = fMux.SendSpace();
			if (space > 0) {
				size_t chunk = length - offset < space ? length - offset
					: space;
				if (!fMux.Send(bytes + offset, chunk))
					return B_ERROR;
				offset += chunk;
				continue;
			}
		}
		bigtime_t remaining = end - system_time();
		if (remaining <= 0)
			return B_TIMED_OUT;
		if (acquire_sem_etc(fChanged, 1, B_RELATIVE_TIMEOUT, remaining)
				== B_BAD_SEM_ID)
			return B_ERROR;
	}
	return B_OK;
}


status_t
RFCOMMLink::ReadLine(char* buffer, size_t size, bigtime_t timeout)
{
	if (buffer == NULL || size < 2)
		return B_BAD_VALUE;
	bigtime_t end = system_time() + timeout;
	while (true) {
		{
			BAutolock locker(fLock);
			if (fLineLength < sizeof(fLine)) {
				fLineLength += fMux.Take(fLine + fLineLength,
					sizeof(fLine) - fLineLength);
			}
			size_t skip = 0;
			while (skip < fLineLength
				&& (fLine[skip] == '\r' || fLine[skip] == '\n'))
				skip++;
			if (skip > 0) {
				memmove(fLine, fLine + skip, fLineLength - skip);
				fLineLength -= skip;
			}

			size_t stop = 0;
			while (stop < fLineLength && fLine[stop] != '\r'
				&& fLine[stop] != '\n')
				stop++;
			size_t most = size - 1 < sizeof(fLine) ? size - 1
				: sizeof(fLine);
			bool whole = stop < fLineLength;
			if (whole || fLineLength >= most) {
				size_t count = stop < most ? stop : most;
				memcpy(buffer, fLine, count);
				buffer[count] = '\0';
				size_t used = count + (whole && count == stop ? 1 : 0);
				memmove(fLine, fLine + used, fLineLength - used);
				fLineLength -= used;
				return B_OK;
			}
			if (_Ended())
				return B_ERROR;
		}
		bigtime_t remaining = end - system_time();
		if (remaining <= 0)
			return B_TIMED_OUT;
		if (acquire_sem_etc(fChanged, 1, B_RELATIVE_TIMEOUT, remaining)
				== B_BAD_SEM_ID)
			return B_ERROR;
	}
}


void
RFCOMMLink::Close()
{
	if (fClosed)
		return;
	fClosed = true;

	if (fReader >= 0) {
		{
			BAutolock locker(fLock);
			if (!_Ended())
				fMux.Close(system_time());
		}
		bigtime_t end = system_time() + kCloseWait;
		while (true) {
			{
				BAutolock locker(fLock);
				if (_Ended())
					break;
			}
			bigtime_t remaining = end - system_time();
			if (remaining <= 0
				|| acquire_sem_etc(fChanged, 1, B_RELATIVE_TIMEOUT,
					remaining) == B_BAD_SEM_ID)
				break;
		}
	}

	fQuitting = true;
	fTransport->Shutdown();
	if (fReader >= 0) {
		status_t result;
		wait_for_thread(fReader, &result);
		fReader = -1;
	}
}


bool
RFCOMMLink::IsOpen()
{
	BAutolock locker(fLock);
	return !_Ended() && fMux.GetState() == Mux::kOpen;
}


Mux::Error
RFCOMMLink::Error()
{
	BAutolock locker(fLock);
	if (fMux.GetError() == Mux::kNoError && fGone)
		return Mux::kRemoteClosed;
	return fMux.GetError();
}


uint16
RFCOMMLink::MaxFrame()
{
	BAutolock locker(fLock);
	return fMux.MaxFrame();
}


void
RFCOMMLink::_Output(const uint8* frame, size_t length, void* cookie)
{
	// Called by the mux, under fLock.
	RFCOMMLink* link = (RFCOMMLink*)cookie;
	if (!link->fGone && link->fTransport->Send(frame, length) != B_OK)
		link->fGone = true;
}


status_t
RFCOMMLink::_ReaderThread(void* data)
{
	((RFCOMMLink*)data)->_Read();
	return B_OK;
}


void
RFCOMMLink::_Read()
{
	uint8 buffer[1024];
	while (!fQuitting) {
		ssize_t received = fTransport->Receive(buffer, sizeof(buffer), kPoll);
		// Close() ended the transport: that is not the other side leaving.
		if (fQuitting)
			break;
		BAutolock locker(fLock);
		Mux::State before = fMux.GetState();
		size_t pending = fMux.Pending();
		int credits = fMux.SendCredits();
		if (received > 0)
			fMux.Receive(buffer, received, system_time());
		else if (received != B_TIMED_OUT)
			fGone = true;
		fMux.Tick(system_time());
		if (fGone || fMux.GetState() != before || fMux.Pending() != pending
			|| fMux.SendCredits() != credits)
			_Changed();
		if (fGone)
			break;
	}
}


status_t
RFCOMMLink::_WaitOpen(bigtime_t timeout)
{
	bigtime_t end = system_time() + timeout;
	while (true) {
		{
			BAutolock locker(fLock);
			if (fMux.GetState() == Mux::kOpen && !fGone)
				return B_OK;
			if (_Ended()) {
				Mux::Error error = fMux.GetError();
				return error_status(error == Mux::kNoError
					? Mux::kRemoteClosed : error);
			}
		}
		bigtime_t remaining = end - system_time();
		if (remaining <= 0)
			return B_TIMED_OUT;
		if (acquire_sem_etc(fChanged, 1, B_RELATIVE_TIMEOUT, remaining)
				== B_BAD_SEM_ID)
			return B_ERROR;
	}
}


bool
RFCOMMLink::_Ended() const
{
	Mux::State state = fMux.GetState();
	return fGone || state == Mux::kClosed || state == Mux::kFailed;
}


void
RFCOMMLink::_Changed()
{
	release_sem_etc(fChanged, 1, B_DO_NOT_RESCHEDULE);
}
