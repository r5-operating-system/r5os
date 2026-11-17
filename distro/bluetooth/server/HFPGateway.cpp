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


static const bigtime_t kOpenTimeout = 30000000;
static const bigtime_t kReadWait = 1000000;
static const bigtime_t kSendWait = 5000000;
static const bigtime_t kSleepStep = 250000;


struct HFPGateway::Session {
	bdaddr_t	address;
	uint8		channel;
	thread_id	thread;
	int32		battery;
	bool		again;		// DeviceReady() came while this one ran
	bool		done;
	HFPGateway*	gateway;
};


static status_t
default_connect(const bdaddr_t& address, uint8 channel, bigtime_t timeout,
	volatile bool* cancel, RFCOMMLink** _link, void*)
{
	return RFCOMMLink::Connect(address, channel, timeout, _link, cancel);
}


HFPGateway::Options
HFPGateway::DefaultOptions()
{
	Options options;
	options.connect = default_connect;
	options.connected = NULL;
	options.cookie = NULL;
	options.connectDelay = 5000000;
	options.retryDelay = 10000000;
	options.retries = 2;
	options.serviceLevelTimeout = 30000000;
	return options;
}


HFPGateway::HFPGateway(const Options& options)
	:
	fOptions(options),
	fLock("hfp gateway"),
	fSessions(4),
	fQuitting(false)
{
	if (fOptions.connect == NULL)
		fOptions.connect = default_connect;
}


HFPGateway::~HFPGateway()
{
	Stop();
}


void
HFPGateway::Stop()
{
	BObjectList<Session, true> sessions;
	{
		// Under the lock: a DeviceReady() after this adds nothing.
		BAutolock locker(fLock);
		fQuitting = true;
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
	BAutolock locker(fLock);
	if (fQuitting)
		return;
	for (int32 i = fSessions.CountItems() - 1; i >= 0; i--) {
		Session* session = fSessions.ItemAt(i);
		if (session->done) {
			// Done is the thread's last step under the lock.
			status_t result;
			wait_for_thread(session->thread, &result);
			delete fSessions.RemoveItemAt(i);
			continue;
		}
		if (memcmp(&session->address, &address, sizeof(address)) == 0) {
			session->channel = channel;
			session->again = true;
			return;
		}
	}

	Session* session = new(std::nothrow) Session;
	if (session == NULL)
		return;
	session->address = address;
	session->channel = channel;
	session->battery = -1;
	session->again = false;
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


int32
HFPGateway::CountSessions()
{
	BAutolock locker(fLock);
	int32 count = 0;
	for (int32 i = 0; i < fSessions.CountItems(); i++) {
		if (!fSessions.ItemAt(i)->done)
			count++;
	}
	return count;
}


status_t
HFPGateway::_SessionThread(void* data)
{
	Session* session = (Session*)data;
	session->gateway->_Run(session);
	return B_OK;
}


void
HFPGateway::_Sleep(bigtime_t duration)
{
	for (bigtime_t waited = 0; waited < duration && !fQuitting;
			waited += kSleepStep)
		snooze(duration - waited < kSleepStep ? duration - waited : kSleepStep);
}


void
HFPGateway::_Run(Session* session)
{
	int32 failures = 0;
	bigtime_t delay = fOptions.connectDelay;
	while (true) {
		_Sleep(delay);
		if (fQuitting)
			break;
		bdaddr_t address;
		uint8 channel;
		{
			BAutolock locker(fLock);
			session->again = false;
			address = session->address;
			channel = session->channel;
		}

		// Never under fLock: the check takes the server's devices lock,
		// which is always taken before fLock.
		bool connected = fOptions.connected == NULL
			|| fOptions.connected(address, fOptions.cookie);
		bool serviceLevel = false;
		if (connected) {
			RFCOMMLink* link = NULL;
			if (fOptions.connect(address, channel, kOpenTimeout, &fQuitting,
					&link, fOptions.cookie) == B_OK && link != NULL) {
				serviceLevel = _Serve(session, link);
			}
			delete link;
		}

		bool again;
		{
			BAutolock locker(fLock);
			session->battery = -1;
			again = session->again;
		}
		if (fQuitting)
			break;
		if (again) {
			// The device came back (or encrypted again) meanwhile.
			failures = 0;
			delay = fOptions.connectDelay;
			continue;
		}
		// A session that ran ends with the headset; one that never got
		// its SLC is tried again while the link is there.
		if (serviceLevel || !connected || ++failures > fOptions.retries)
			break;
		syslog(LOG_INFO, "bluetooth_server: hfp: trying again (%" B_PRId32
			" of %" B_PRId32 ")", failures, fOptions.retries);
		delay = fOptions.retryDelay;
	}

	BAutolock locker(fLock);
	session->battery = -1;
	session->done = true;
}


bool
HFPGateway::_Serve(Session* session, RFCOMMLink* link)
{
	r5hfp::Agent agent;
	bigtime_t start = system_time();
	char line[256];
	char answer[r5hfp::kMaxAnswer];
	while (!fQuitting) {
		status_t status = link->ReadLine(line, sizeof(line), kReadWait);
		if (status == B_TIMED_OUT) {
			if (!agent.ServiceLevel()
				&& system_time() - start > fOptions.serviceLevelTimeout) {
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
			{
				// A DeviceReady() for this same link (encryption and SDP
				// both report it) asks for nothing more.
				BAutolock locker(fLock);
				session->again = false;
			}
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
	return agent.ServiceLevel();
}
