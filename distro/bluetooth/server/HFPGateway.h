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


class HFPGateway {
public:
								HFPGateway();
								~HFPGateway();

			void				Stop();

	// The device is connected, encrypted, and its SDP record has a
	// Hands-Free service on this RFCOMM channel. Starts a session unless
	// one runs for the device.
			void				DeviceReady(const bdaddr_t& address,
									uint8 channel);
	// The battery in percent while a session runs and the headset sent
	// one; -1 otherwise.
			int32				Battery(const bdaddr_t& address);

private:
			struct Session;

	static	status_t			_SessionThread(void* data);
			void				_Run(Session* session);

			BLocker				fLock;
			BObjectList<Session, true> fSessions;
	volatile bool				fQuitting;
};


#endif	// R5_HFP_GATEWAY_H
