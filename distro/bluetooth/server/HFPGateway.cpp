/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * HFPGateway.h. Nothing here logs an address.
 */


#include "HFPGateway.h"

#include <new>
#include <string.h>
#include <syslog.h>

#include <Autolock.h>

#include "HFPAgent.h"
#include "RFCOMMLink.h"


// After the link is encrypted: A2DP and HID go first, so HFP never
// delays music.
static const bigtime_t kConnectDelay = 5000000;
static const bigtime_t kOpenTimeout = 30000000;
// The headset starts the SLC (HFP 1.8, 4.2.1); without it in this time
// the session ends.
static const bigtime_t kServiceLevelTimeout = 30000000;
static const bigtime_t kReadWait = 1000000;
static const bigtime_t kSendWait = 5000000;


struct HFPGateway::Session {
	bdaddr_t	address;
	uint8		channel;
	thread_id	thread;
	int32		battery;
	bool		done;
	HFPGateway*	gateway;
};


HFPGateway::HFPGateway()
	:
	fLock("hfp gateway"),
	fSessions(4),
	fQuitting(false)
{
}


HFPGateway::~HFPGateway()
{
	Stop();
}


void
HFPGateway::Stop()
{
	fQuitting = true;
	BObjectList<Session, true> sessions;
	{
		BAutolock locker(fLock);
		while (fSessions.CountItems() > 0)
			sessions.AddItem(fSessions.RemoveItemAt(0));
	}
	for (int32 i = 0; i < sessions.CountItems(); i++) {
		status_t result;
		wait_for_thread(sessions.ItemAt(i)->thread, &result);
	}
}


void
HFPGateway::DeviceReady(const bdaddr_t& address, uint8 channel)
{
	if (fQuitting)
		return;
	BAutolock locker(fLock);
	for (int32 i = fSessions.CountItems() - 1; i >= 0; i--) {
		Session* session = fSessions.ItemAt(i);
		if (session->done) {
			status_t result;
			wait_for_thread(session->thread, &result);
			delete fSessions.RemoveItemAt(i);
			continue;
		}
		if (memcmp(&session->address, &address, sizeof(address)) == 0)
			return;
	}

	Session* session = new(std::nothrow) Session;
	if (session == NULL)
		return;
	session->address = address;
	session->channel = channel;
	session->battery = -1;
	session->done = false;
	session->gateway = this;
	session->thread = spawn_thread(_SessionThread, "bluetooth hfp",
		B_NORMAL_PRIORITY, session);
	if (session->thread < 0 || !fSessions.AddItem(session)) {
		if (session->thread >= 0)
			kill_thread(session->thread);
		delete session;
		return;
	}
	resume_thread(session->thread);
}


int32
HFPGateway::Battery(const bdaddr_t& address)
{
	BAutolock locker(fLock);
	for (int32 i = 0; i < fSessions.CountItems(); i++) {
		Session* session = fSessions.ItemAt(i);
		if (!session->done
			&& memcmp(&session->address, &address, sizeof(address)) == 0)
			return session->battery;
	}
	return -1;
}


status_t
HFPGateway::_SessionThread(void* data)
{
	Session* session = (Session*)data;
	session->gateway->_Run(session);
	return B_OK;
}


void
HFPGateway::_Run(Session* session)
{
	for (bigtime_t waited = 0; waited < kConnectDelay && !fQuitting;
			waited += 250000)
		snooze(250000);

	RFCOMMLink* link = NULL;
	if (fQuitting || RFCOMMLink::Connect(session->address, session->channel,
			kOpenTimeout, &link) != B_OK) {
		BAutolock locker(fLock);
		session->done = true;
		return;
	}

	r5hfp::Agent agent;
	bigtime_t start = system_time();
	char line[256];
	char answer[r5hfp::kMaxAnswer];
	while (!fQuitting) {
		status_t status = link->ReadLine(line, sizeof(line), kReadWait);
		if (status == B_TIMED_OUT) {
			if (!agent.ServiceLevel()
				&& system_time() - start > kServiceLevelTimeout) {
				syslog(LOG_NOTICE, "bluetooth_server: hfp: the headset "
					"started no service level connection");
				break;
			}
			continue;
		}
		if (status != B_OK)
			break;

		size_t length = agent.Handle(line, strlen(line), answer,
			sizeof(answer));
		if (length > 0 && link->Send(answer, length, kSendWait) != B_OK)
			break;

		uint32 events = agent.TakeEvents();
		if ((events & r5hfp::kEventServiceLevel) != 0) {
			syslog(LOG_INFO, "bluetooth_server: hfp: service level "
				"connection up, headset features %#" B_PRIx32,
				agent.HandsFreeFeatures());
		}
		if ((events & r5hfp::kEventBattery) != 0) {
			BAutolock locker(fLock);
			if (session->battery != agent.Battery()) {
				syslog(LOG_INFO, "bluetooth_server: hfp: battery %d %%",
					agent.Battery());
			}
			session->battery = agent.Battery();
		}
		if ((events & r5hfp::kEventButton) != 0) {
			syslog(LOG_INFO, "bluetooth_server: hfp: button %s",
				agent.LastButton());
		}
		if ((events & r5hfp::kEventVolume) != 0) {
			syslog(LOG_INFO, "bluetooth_server: hfp: volume speaker %d "
				"microphone %d", agent.SpeakerVolume(),
				agent.MicrophoneVolume());
		}
	}
	if (agent.Unknown() > 0) {
		syslog(LOG_INFO, "bluetooth_server: hfp: %" B_PRIu32 " commands "
			"not known", agent.Unknown());
	}
	syslog(LOG_INFO, "bluetooth_server: hfp: session ended");
	delete link;

	BAutolock locker(fLock);
	session->battery = -1;
	session->done = true;
}
