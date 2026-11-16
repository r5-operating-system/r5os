/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * R5 as the HFP Audio Gateway (docs/BLUETOOTH.md, step 7b): for a paired
 * headset with a Hands-Free record, a thread opens the RFCOMM channel
 * (RFCOMMLink) and answers its AT commands (r5hfp::Agent). What it keeps
 * is the battery level, for BT_MSG_R5_GET_PAIRED; buttons and volume go
 * to the syslog. No audio (that is step 7c).
 */
#ifndef R5_HFP_GATEWAY_H
#define R5_HFP_GATEWAY_H


#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>

#include <bluetooth/bluetooth.h>


class RFCOMMLink;


class HFPGateway {
public:
	// How a session opens its channel; RFCOMMLink::Connect() by default.
	typedef	status_t			(*Connector)(const bdaddr_t& address,
									uint8 channel, bigtime_t timeout,
									volatile bool* cancel,
									RFCOMMLink** _link, void* cookie);
	// Whether the device still has its link; NULL means always. A
	// session only connects, and tries again, while it has.
	typedef	bool				(*ConnectedCheck)(const bdaddr_t& address,
									void* cookie);

	struct Options {
		Connector		connect;
		ConnectedCheck	connected;
		void*			cookie;
		// After DeviceReady(): A2DP and HID go first.
		bigtime_t		connectDelay;
		// After a session that never reached the SLC, up to retries
		// times while the device is connected.
		bigtime_t		retryDelay;
		int32			retries;
		// The headset starts the SLC (HFP 1.8, 4.2.1) within this time.
		bigtime_t		serviceLevelTimeout;
	};

	static	Options				DefaultOptions();

								HFPGateway(
									const Options& options
										= DefaultOptions());
								~HFPGateway();

	// Ends every session: waits in progress end within about a second
	// (the L2CAP connect itself has the kernel's timeout). DeviceReady()
	// does nothing afterwards.
			void				Stop();

	// The device is connected, encrypted, and its SDP record has a
	// Hands-Free service on this RFCOMM channel. Starts a session, or
	// asks the running one to try again when it ends.
			void				DeviceReady(const bdaddr_t& address,
									uint8 channel);
	// The battery in percent while a session runs and the headset sent
	// one; -1 otherwise.
			int32				Battery(const bdaddr_t& address);
	// Sessions that run (for the test).
			int32				CountSessions();

private:
			struct Session;

	static	status_t			_SessionThread(void* data);
			void				_Run(Session* session);
			bool				_Serve(Session* session, RFCOMMLink* link);
			void				_Sleep(bigtime_t duration);

			Options				fOptions;
			BLocker				fLock;
			BObjectList<Session, true> fSessions;
	volatile bool				fQuitting;
};


#endif	// R5_HFP_GATEWAY_H
