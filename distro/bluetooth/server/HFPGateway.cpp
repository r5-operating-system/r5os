/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * HFPGateway.h. Nothing here logs an address.
 */


#include "HFPGateway.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <Autolock.h>

#include "HFPAgent.h"
#include "RFCOMMLink.h"
#include "VoiceLink.h"


static const bigtime_t kOpenTimeout = 30000000;
static const bigtime_t kReadWait = 1000000;
static const bigtime_t kSendWait = 5000000;
static const bigtime_t kSleepStep = 250000;
// The headset answers +BCS with AT+BCS within this time.
static const bigtime_t kCodecWait = 3000000;


struct HFPGateway::Session {
	bdaddr_t	address;
	uint8		channel;
	thread_id	thread;
	int32		battery;
	bool		again;		// DeviceReady() came while this one ran
	// Step 7c, used by the session thread only (voiceOpen read under fLock).
	bool		voiceOpen;
	bool		voiceFailed;	// until the microphone is let go
	int			codecPending;	// +BCS sent, waiting for AT+BCS
	bigtime_t	codecDeadline;
	bool		done;
	bigtime_t	firstWait;	// before the first RFCOMM connect
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
	// No wait. On stick 54 the WH-1000XM3 ended the link (reason 0x13,
	// the headset) about 2 s after SDP when no profile had come up.
	// A2DP now opens as soon as that link is up; HFP starts at once too.
	options.connectDelay = 0;
	options.retryDelay = 10000000;
	options.retries = 2;
	options.serviceLevelTimeout = 30000000;
	options.voice = NULL;
	options.codecNegotiation = false;
	return options;
}


HFPGateway::HFPGateway(const Options& options)
	:
	fOptions(options),
	fLock("hfp gateway"),
	fSessions(4),
	fQuitting(false),
	fVoiceWanted(false),
	fVoiceOwner(NULL)
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
HFPGateway::DeviceReady(const bdaddr_t& address, uint8 channel,
	bigtime_t wait)
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
			if (wait > session->firstWait)
				session->firstWait = wait;
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
	session->voiceOpen = false;
	session->voiceFailed = false;
	session->codecPending = 0;
	session->codecDeadline = 0;
	session->firstWait = wait;
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
	bigtime_t delay;
	{
		BAutolock locker(fLock);
		delay = session->firstWait > 0 ? session->firstWait
			: fOptions.connectDelay;
		session->firstWait = 0;
	}
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
	r5hfp::Agent agent(fOptions.codecNegotiation);
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
			// The microphone may be wanted without a word from the headset.
			_Voice(session, agent, link, 0);
			continue;
		}
		if (status != B_OK)
			break;

		size_t length = agent.Handle(line, strlen(line), answer,
			sizeof(answer));
		if (length > 0 && link->Send(answer, length, kSendWait) != B_OK)
			break;

		uint32 events = agent.TakeEvents();
		_Voice(session, agent, link, events);
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
	// The voice link goes with the session.
	if (session->voiceOpen)
		_VoiceSet(session, 0, false);
	{
		BAutolock locker(fLock);
		if (fVoiceOwner == session)
			fVoiceOwner = NULL;
		session->codecPending = 0;
		session->voiceFailed = false;
	}
	syslog(LOG_INFO, "bluetooth_server: hfp: session ended");
	return agent.ServiceLevel();
}


void
HFPGateway::SetVoiceWanted(bool wanted)
{
	BAutolock locker(fLock);
	if (fVoiceWanted == wanted)
		return;
	fVoiceWanted = wanted;
	syslog(LOG_INFO, "bluetooth_server: hfp: microphone %s",
		wanted ? "wanted" : "no longer wanted");
}


bool
HFPGateway::VoiceOpen()
{
	BAutolock locker(fLock);
	for (int32 i = 0; i < fSessions.CountItems(); i++) {
		if (fSessions.ItemAt(i)->voiceOpen)
			return true;
	}
	return false;
}


// Step 7c: the voice link follows the microphone. With codec negotiation
// on both sides the AG first sends +BCS and waits for AT+BCS (HFP 1.8,
// 4.11.3); otherwise the link is CVSD.
void
HFPGateway::_Voice(Session* session, r5hfp::Agent& agent, RFCOMMLink* link,
	uint32 events)
{
	if (fOptions.voice == NULL || !agent.ServiceLevel())
		return;
	bool wanted;
	{
		BAutolock locker(fLock);
		if (fVoiceWanted && fVoiceOwner == NULL)
			fVoiceOwner = session;
		wanted = fVoiceWanted && fVoiceOwner == session;
		if (!fVoiceWanted)
			session->voiceFailed = false;
	}

	if (!wanted) {
		session->codecPending = 0;
		if (session->voiceOpen)
			_VoiceSet(session, 0, false);
		{
			BAutolock locker(fLock);
			if (fVoiceOwner == session && !fVoiceWanted)
				fVoiceOwner = NULL;
		}
		return;
	}
	if (session->voiceOpen || session->voiceFailed)
		return;

	if (session->codecPending != 0) {
		if ((events & r5hfp::kEventCodec) != 0) {
			int codec = agent.ConfirmedCodec();
			if (codec != voice::kCodecMSBC)
				codec = voice::kCodecCVSD;
			session->codecPending = 0;
			_VoiceSet(session, codec, true);
		} else if (system_time() > session->codecDeadline) {
			syslog(LOG_NOTICE, "bluetooth_server: hfp: the headset did not "
				"confirm the codec");
			session->codecPending = 0;
			session->voiceFailed = true;
		}
		return;
	}

	if (agent.CanNegotiateCodecs()) {
		int codec = voice::ChooseCodec(agent.CodecList(), true);
		char select[32];
		int length = snprintf(select, sizeof(select), "\r\n+BCS: %d\r\n",
			codec);
		if (link->Send(select, length, kSendWait) == B_OK) {
			session->codecPending = codec;
			session->codecDeadline = system_time() + kCodecWait;
		}
		return;
	}
	_VoiceSet(session, voice::kCodecCVSD, true);
}


void
HFPGateway::_VoiceSet(Session* session, int codec, bool open)
{
	bdaddr_t address;
	{
		BAutolock locker(fLock);
		address = session->address;
	}
	// Never under fLock: the server takes its devices lock.
	status_t status = fOptions.voice(address, codec, open, fOptions.cookie);
	if (!open || status == B_OK) {
		BAutolock locker(fLock);
		session->voiceOpen = open;
	}
	if (open && status != B_OK) {
		syslog(LOG_NOTICE, "bluetooth_server: hfp: voice link not asked: "
			"%s", strerror(status));
		session->voiceFailed = true;
	}
}
