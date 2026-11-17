/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * One RFCOMM data channel to a paired device (docs/BLUETOOTH.md, step 7a),
 * for the HFP layer of step 7b: open a DLC on the device's server
 * channel, send bytes, read lines, close. R5 is the initiator (the HFP
 * Audio Gateway connects to the headset) on an L2CAP socket on PSM 3.
 *
 * A reader thread feeds the socket into r5rfcomm::Mux (RFCOMMMux.h) and
 * runs its timeouts. Every call that waits has a timeout. One thread uses
 * a link at a time; the reader thread is the link's own.
 */
#ifndef R5_RFCOMM_LINK_H
#define R5_RFCOMM_LINK_H


#include <Locker.h>
#include <OS.h>

#include <bluetooth/bluetooth.h>

#include "RFCOMMMux.h"


// How a link moves its frames: one RFCOMM frame per packet. The socket
// transport is the real one; the loopback test passes ports.
class RFCOMMTransport {
public:
	virtual						~RFCOMMTransport() {}
	virtual	status_t			Send(const void* data, size_t length) = 0;
	// The next packet, B_TIMED_OUT after timeout microseconds, or an error
	// when the transport is gone.
	virtual	ssize_t				Receive(void* buffer, size_t size,
									bigtime_t timeout) = 0;
	// Ends a Receive() that waits; after this every call fails.
	virtual	void				Shutdown() = 0;
};


class RFCOMMLink {
public:
	// Connects L2CAP PSM 3 to the device and opens a DLC on that server
	// channel (1 to 30, from the device's SDP record). Waits at most
	// timeout microseconds; the link then belongs to the caller, who
	// deletes it (which closes it).
	// cancel, when not NULL, is a flag of the caller that stays valid as
	// long as the link: once it is true, a wait for the DLC ends within
	// 250 ms with B_CANCELED, and Close() sends DISC without waiting for
	// the answers (for a server that quits). The L2CAP connect itself
	// waits for the kernel's own timeout.
	static	status_t			Connect(const bdaddr_t& address,
									uint8 channel, bigtime_t timeout,
									RFCOMMLink** _link,
									volatile bool* cancel = NULL);

	// The same over another transport, which the link then owns. The
	// responder side (listen) exists for the loopback test.
	static	status_t			Start(RFCOMMTransport* transport,
									bool initiator, uint8 channel,
									bigtime_t timeout, RFCOMMLink** _link,
									volatile bool* cancel = NULL);

								~RFCOMMLink();

	// Sends all bytes, in frames as large as the DLC takes; waits for
	// credit up to timeout microseconds in all.
			status_t			Send(const void* data, size_t length,
									bigtime_t timeout);
	// The next line: the bytes up to a CR or LF, without it, ended with a
	// NUL. Empty lines are skipped. A line longer than size - 1 comes in
	// pieces. B_TIMED_OUT when no whole line came in time, B_ERROR when
	// the link is closed.
			status_t			ReadLine(char* buffer, size_t size,
									bigtime_t timeout);
	// DISC on the DLC and on DLCI 0, each answered or timed out, then the
	// transport ends. The destructor does this too.
			void				Close();

			bool				IsOpen();
			r5rfcomm::Mux::Error Error();
			uint16				MaxFrame();

private:
								RFCOMMLink(RFCOMMTransport* transport,
									bool initiator);

	static	void				_Output(const uint8* frame, size_t length,
									void* cookie);
	static	status_t			_ReaderThread(void* data);
			void				_Read();
			status_t			_WaitOpen(bigtime_t timeout);
			bool				_Ended() const;
			bool				_Cancelled() const
									{ return fCancel != NULL && *fCancel; }
			void				_Changed();

			RFCOMMTransport*	fTransport;
			BLocker				fLock;
			r5rfcomm::Mux		fMux;
			sem_id				fChanged;
			thread_id			fReader;
	volatile bool				fQuitting;
			bool				fGone;		// the transport ended
			bool				fClosed;
	volatile bool*				fCancel;
			uint8				fLine[r5rfcomm::kReceiveBuffer];
			size_t				fLineLength;
};


#endif	// R5_RFCOMM_LINK_H
