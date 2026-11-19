/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * A2DP source (A2DP 1.3.2, AVDTP 1.3) and AVRCP (1.6, absolute volume and
 * PASS THROUGH play and pause) for bluetooth_audio. See A2DPSource.h.
 */


#include "A2DPSource.h"

#include <errno.h>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>

#include <Autolock.h>
#include <Messenger.h>
#include <Message.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetoothserver_p.h>
#include <R5Admin.h>

#include "a2dp_proto.h"
#include "AudioSettings.h"


using namespace a2dp;


// R5's one stream endpoint: an SBC audio source.
static const uint8 kLocalSEID = 1;

// How long R5 waits for an AVDTP answer (AVDTP 1.3, 8.4.4 suggests a
// signalling timeout of a few seconds: TGAVDTP100 is 0.5 to 3 s).
static const bigtime_t kSignalTimeout = 3000000;
// After Set Configuration from the headset, R5 sends Open itself when the
// headset has not within this time.
static const bigtime_t kOpenDelay = 2000000;
// The headset usually opens AVRCP itself after the stream; R5 does it
// after this long otherwise.
static const bigtime_t kControlDelay = 2500000;
// Between two attempts to reach the chosen headset while audio plays.
static const bigtime_t kRetryInterval = 30000000;
// The manager thread wakes this often to look at the session.
static const bigtime_t kPollInterval = 200000;
// After this long of only silence the stream is suspended, so the headset
// may sleep; the next sound starts it again.
static const bigtime_t kIdleSuspend = 5000000;
// A reported headset delay above this is not believed (AVDTP allows up to
// 6.5 s; real headsets report 0.1 to 0.3 s).
static const bigtime_t kMaxRemoteDelay = 500000;
// A link to the chosen headset may take this long to come up and be
// encrypted (page timeout 5 s, then authentication and encryption).
static const bigtime_t kLinkWait = 15000000;
// A headset that connects opens AVDTP right after the link, often before
// encryption is on: R5 waits this long for it.
static const bigtime_t kEncryptionWait = 5000000;
static const bigtime_t kLinkPoll = 250000;

// Audio kept between the mixer and the encoder. Packets leave this much
// before their time, so a late wake-up of the sending thread never starves
// the headset.
static const bigtime_t kRingLatency = 40000;
static const bigtime_t kSendLead = 20000;
// More than this behind the clock (the thread did not run), the stream
// starts its timing again instead of sending a burst.
static const bigtime_t kResyncAfter = 200000;
static const size_t kRingFrames = 65536;
// Audio kept from before the stream (re)starts.
static const bigtime_t kStartKeep = 150000;


struct listen_args {
	A2DPSource*	source;
	int			index;
};


static void
set_receive_timeout(int fd, bigtime_t timeout)
{
	struct timeval value;
	value.tv_sec = timeout / 1000000;
	value.tv_usec = timeout % 1000000;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
}


static bool
is_timeout(int error)
{
	return error == B_TIMED_OUT || error == EAGAIN || error == EWOULDBLOCK
		|| error == B_INTERRUPTED;
}


A2DPSource::A2DPSource(Listener* listener, int rate)
	:
	fListener(listener),
	fRate(rate),
	fLock("bluetooth audio"),
	fQuitting(false),
	fPlaying(false),
	fPaused(false),
	fManagerThread(-1),
	fStreamThread(-1),
	fControlThread(-1),
	fControlStop(false),
	fControlDone(false),
	fWake(-1),
	fHasRemote(false),
	fSignal(-1),
	fTransport(-1),
	fControl(-1),
	fPendingSignal(-1),
	fPendingTransport(-1),
	fPendingControl(-1),
	fState(kIdle),
	fInitiator(false),
	fRemoteSEID(0),
	fCandidateCount(0),
	fCandidateIndex(0),
	fLabel(0),
	fPendingLabel(0),
	fPendingCommand(0),
	fPendingSince(0),
	fSessionStart(0),
	fConfiguredAt(0),
	fLastAttempt(-kRetryInterval),
	fTriedControl(false),
	fTriedAllCapabilities(false),
	fRetriedBitpool(false),
	fDelayReporting(false),
	fSinkCapabilitiesLength(0),
	fStreaming(false),
	fLoggedStart(false),
	fReportedReady(false),
	fControlLock("bluetooth audio control"),
	fControlLabel(0),
	fAbsoluteVolume(false),
	fVolume(127),
	fSoftwareGain(1.0f),
	fRemoteDelay(0),
	fLastSound(0),
	fRing(NULL),
	fRingFrames(kRingFrames),
	fRingRead(0),
	fRingWrite(0)
{
	memset(&fRemote, 0, sizeof(fRemote));
	memset(&fPendingAddress, 0, sizeof(fPendingAddress));
	memset(&fConfig, 0, sizeof(fConfig));
	for (int i = 0; i < 2; i++) {
		fListenSockets[i] = -1;
		fListenThreads[i] = -1;
	}
}


A2DPSource::~A2DPSource()
{
	Stop();
	delete[] fRing;
}


status_t
A2DPSource::Start()
{
	fRing = new(std::nothrow) float[fRingFrames * 2];
	if (fRing == NULL)
		return B_NO_MEMORY;
	memset(fRing, 0, sizeof(float) * fRingFrames * 2);

	fWake = create_sem(0, "bluetooth audio wake");
	if (fWake < 0)
		return fWake;

	static const uint16 psms[2] = { kPSMAVDTP, kPSMAVCTP };
	for (int i = 0; i < 2; i++) {
		int fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
		if (fd < 0) {
			status_t error = errno;
			syslog(LOG_WARNING, "bluetooth_audio: no L2CAP (%s)",
				strerror(error));
			Stop();
			return error;
		}
		struct sockaddr_l2cap address;
		memset(&address, 0, sizeof(address));
		address.l2cap_len = sizeof(address);
		address.l2cap_family = AF_BLUETOOTH;
		address.l2cap_bdaddr = BDADDR_ANY;
		address.l2cap_psm = B_HOST_TO_LENDIAN_INT16(psms[i]);
		if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0
			|| listen(fd, 4) < 0) {
			status_t error = errno;
			syslog(LOG_WARNING, "bluetooth_audio: cannot listen on PSM %#x: "
				"%s", psms[i], strerror(error));
			close(fd);
			// Without AVDTP there is no audio; without AVCTP only the
			// headset's buttons and volume are missing.
			if (psms[i] == kPSMAVDTP) {
				Stop();
				return error;
			}
			continue;
		}
		fListenSockets[i] = fd;

		listen_args* args = new(std::nothrow) listen_args;
		if (args == NULL)
			continue;
		args->source = this;
		args->index = i;
		fListenThreads[i] = spawn_thread(_ListenEntry,
			i == 0 ? "bt audio listen avdtp" : "bt audio listen avctp",
			B_NORMAL_PRIORITY, args);
		if (fListenThreads[i] < 0)
			delete args;
		else
			resume_thread(fListenThreads[i]);
	}

	fManagerThread = spawn_thread(_ManagerEntry, "bt audio signalling",
		B_URGENT_DISPLAY_PRIORITY, this);
	if (fManagerThread < 0) {
		status_t error = fManagerThread;
		Stop();
		return error;
	}
	resume_thread(fManagerThread);
	// One line, so a log tells "never started" from "started and idle".
	bdaddr_t chosen;
	syslog(LOG_INFO, "bluetooth_audio: listening, %s",
		_ChosenDevice(&chosen) ? "a headset is chosen"
			: "no headset chosen (a paired one is used)");
	return B_OK;
}


void
A2DPSource::Stop()
{
	if (fQuitting.exchange(true))
		return;

	// The manager closes the stream (AVDTP Close) before it ends.
	if (fWake >= 0)
		release_sem(fWake);
	status_t result;
	if (fManagerThread >= 0) {
		wait_for_thread(fManagerThread, &result);
		fManagerThread = -1;
	}

	for (int i = 0; i < 2; i++) {
		if (fListenSockets[i] >= 0) {
			shutdown(fListenSockets[i], SHUT_RDWR);
			close(fListenSockets[i]);
			fListenSockets[i] = -1;
		}
	}
	for (int i = 0; i < 2; i++) {
		if (fListenThreads[i] >= 0) {
			wait_for_thread(fListenThreads[i], &result);
			fListenThreads[i] = -1;
		}
	}

	BAutolock locker(fLock);
	int pending[3] = { fPendingSignal, fPendingTransport, fPendingControl };
	for (int i = 0; i < 3; i++) {
		if (pending[i] >= 0)
			close(pending[i]);
	}
	fPendingSignal = fPendingTransport = fPendingControl = -1;
	if (fWake >= 0) {
		delete_sem(fWake);
		fWake = -1;
	}
}


void
A2DPSource::SetPlaying(bool playing)
{
	fPlaying = playing;
	if (playing)
		fPaused = false;
	if (fWake >= 0)
		release_sem(fWake);
}


void
A2DPSource::SetVolume(float gain)
{
	uint8 volume = VolumeFromGain(gain);
	fVolume = volume;
	if (fAbsoluteVolume) {
		fSoftwareGain = 1.0f;
		_SendAbsoluteVolume(volume);
	} else
		fSoftwareGain = gain < 0.0f ? 0.0f : gain > 1.0f ? 1.0f : gain;
}


bigtime_t
A2DPSource::BufferLatency() const
{
	// With the headset's own delay, so players keep picture and sound
	// together (AVDTP 1.3 delay reporting).
	return kRingLatency + kSendLead + fRemoteDelay.load();
}


// #pragma mark - Ring buffer


void
A2DPSource::Write(const float* samples, size_t frames)
{
	if (fRing == NULL || samples == NULL)
		return;
	size_t read = fRingRead.load(std::memory_order_acquire);
	size_t write = fRingWrite.load(std::memory_order_relaxed);
	size_t free = fRingFrames - (write - read);
	// Full (the headset is gone or slower): the newest audio is dropped,
	// the reader skips ahead on its own.
	if (frames > free)
		frames = free;
	// The mixer sends silence when nothing plays; only sound keeps the
	// stream (and the headset) awake.
	for (size_t i = 0; i < frames * 2; i++) {
		if (samples[i] != 0.0f) {
			fLastSound = system_time();
			break;
		}
	}
	for (size_t i = 0; i < frames; i++) {
		size_t slot = (write + i) % fRingFrames;
		fRing[slot * 2] = samples[i * 2];
		fRing[slot * 2 + 1] = samples[i * 2 + 1];
	}
	fRingWrite.store(write + frames, std::memory_order_release);
}


size_t
A2DPSource::_RingFill() const
{
	return fRingWrite.load(std::memory_order_acquire)
		- fRingRead.load(std::memory_order_relaxed);
}


size_t
A2DPSource::_ReadRing(float* out, size_t frames)
{
	size_t write = fRingWrite.load(std::memory_order_acquire);
	size_t read = fRingRead.load(std::memory_order_relaxed);
	size_t available = write - read;
	size_t count = frames < available ? frames : available;
	for (size_t i = 0; i < count; i++) {
		size_t slot = (read + i) % fRingFrames;
		out[i * 2] = fRing[slot * 2];
		out[i * 2 + 1] = fRing[slot * 2 + 1];
	}
	// An underrun becomes silence, so the headset's clock keeps running.
	memset(out + count * 2, 0, sizeof(float) * 2 * (frames - count));
	fRingRead.store(read + count, std::memory_order_release);
	return count;
}


// #pragma mark - Connections


int32
A2DPSource::_ListenEntry(void* data)
{
	listen_args* args = (listen_args*)data;
	A2DPSource* source = args->source;
	int index = args->index;
	delete args;
	source->_Listen(index);
	return 0;
}


void
A2DPSource::_Listen(int index)
{
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

		bool allowed = _Allowed(peer.l2cap_bdaddr);
		BAutolock locker(fLock);
		if (!allowed || fQuitting) {
			if (!allowed) {
				syslog(LOG_NOTICE, "bluetooth_audio: connection refused, not "
					"the chosen headset and not paired");
			}
			close(fd);
			continue;
		}

		bool sameRemote = fHasRemote
			&& bdaddrUtils::Compare(fRemote, peer.l2cap_bdaddr);
		if (index == 1) {
			if (fControl < 0 && fPendingControl < 0 && sameRemote)
				fPendingControl = fd;
			else
				close(fd);
		} else if (fSignal < 0 && fPendingSignal < 0) {
			// The headset starts a session (it connected to R5).
			fPendingSignal = fd;
			fPendingAddress = peer.l2cap_bdaddr;
		} else if (sameRemote && fTransport < 0 && fPendingTransport < 0
			&& (fState == kOpen || fState == kConfigured
				|| fState == kOpening)) {
			// AVDTP 1.3, 6.11: the second channel to the same PSM is the
			// media transport, opened by the side that sent Open.
			fPendingTransport = fd;
		} else
			close(fd);
		release_sem(fWake);
	}
}


bool
A2DPSource::_ChosenDevice(bdaddr_t* device)
{
	BPath path;
	if (bluetooth_audio_session_settings_path(path) != B_OK)
		return false;
	return bluetooth_audio_read_device(path, device) == B_OK;
}


// Without a bt_audio choice: the first paired Classic device whose class of
// device is Audio/Video (major class 0x04), a connected one first. Nothing
// else opened AVDTP after a headset paired, so it dropped the link and
// stayed in pairing mode (stick 5).
bool
A2DPSource::_PairedHeadset(bdaddr_t* device)
{
	BMessenger server(BLUETOOTH_SIGNATURE);
	BMessage request(BT_MSG_R5_GET_PAIRED);
	BMessage reply;
	if (server.SendMessage(&request, &reply, 2000000, 2000000) != B_OK)
		return false;
	bool found = false;
	const void* data;
	ssize_t size;
	for (int32 i = 0; reply.FindData("bdaddr", B_ANY_TYPE, i, &data, &size)
			== B_OK; i++) {
		if (size != (ssize_t)sizeof(bdaddr_t) || reply.GetBool("le", i, true))
			continue;
		const uint32 deviceClass = reply.GetUInt32("class", i, 0);
		// Class 0 until the radio reports it: SDP's Audio Sink (0x110b)
		// is enough. The WH-1000XM3 was that, and A2DP never opened.
		if (((deviceClass >> 8) & 0x1f) != 0x04
			&& !reply.GetBool("a2dp", i, false))
			continue;
		const bool connected = reply.GetBool("connected", i, false);
		if (!found || connected) {
			memcpy(device, data, sizeof(bdaddr_t));
			found = true;
			if (connected)
				break;
		}
	}
	return found;
}


bool
A2DPSource::_LinkState(const bdaddr_t& address, bool* paired,
	bool* connected, bool* encrypted)
{
	*paired = *connected = *encrypted = false;
	BMessenger server(BLUETOOTH_SIGNATURE);
	BMessage request(BT_MSG_R5_AUDIO_PAIRED);
	request.AddData("bdaddr", B_ANY_TYPE, &address, sizeof(bdaddr_t));
	BMessage reply;
	// The admin port: the server checks that the sender is uid 0 as the
	// kernel names it (libr5admin.so).
	if (r5_admin_send(server, &request, &reply, 2000000) != B_OK)
		return false;
	reply.FindBool("paired", paired);
	reply.FindBool("connected", connected);
	reply.FindBool("encrypted", encrypted);
	return true;
}


bool
A2DPSource::_Allowed(const bdaddr_t& address)
{
	// The chosen headset, or without a choice any paired device; in both
	// cases paired here and on an encrypted link, as HIDHost waits for
	// Encryption Change. An address alone proves nothing.
	bdaddr_t chosen;
	if (_ChosenDevice(&chosen) && !bdaddrUtils::Compare(chosen, address))
		return false;

	bigtime_t until = system_time() + kEncryptionWait;
	while (!fQuitting) {
		bool paired;
		bool connected;
		bool encrypted;
		if (!_LinkState(address, &paired, &connected, &encrypted) || !paired)
			return false;
		if (encrypted)
			return true;
		if (system_time() >= until)
			return false;
		snooze(kLinkPoll);
	}
	return false;
}


int
A2DPSource::_Connect(const bdaddr_t& address, uint16 psm)
{
	// The L2CAP socket only uses a link that is up (btCoreData has no
	// Create Connection of its own): bluetooth_server brings it up for a
	// paired headset, and R5 waits until it is encrypted.
	bool paired;
	bool connected;
	bool encrypted;
	if (!_LinkState(address, &paired, &connected, &encrypted))
		return -1;
	if (!paired) {
		syslog(LOG_NOTICE, "bluetooth_audio: the chosen headset is not "
			"paired");
		return -1;
	}
	if (!encrypted) {
		if (!connected) {
			BMessenger server(BLUETOOTH_SIGNATURE);
			BMessage request(BT_MSG_R5_AUDIO_CONNECT);
			request.AddData("bdaddr", B_ANY_TYPE, &address,
				sizeof(bdaddr_t));
			BMessage reply;
			int32 status = B_ERROR;
			if (r5_admin_send(server, &request, &reply, 2000000) != B_OK
				|| reply.FindInt32("status", &status) != B_OK
				|| status != B_OK)
				return -1;
		}
		bigtime_t until = system_time() + kLinkWait;
		while (!encrypted) {
			if (fQuitting || system_time() >= until) {
				syslog(LOG_NOTICE, "bluetooth_audio: no encrypted link to "
					"the headset (it is off or out of range)");
				return -1;
			}
			snooze(kLinkPoll);
			if (!_LinkState(address, &paired, &connected, &encrypted)
				|| !paired)
				return -1;
		}
	}

	int fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (fd < 0)
		return -1;
	struct sockaddr_l2cap remote;
	memset(&remote, 0, sizeof(remote));
	remote.l2cap_len = sizeof(remote);
	remote.l2cap_family = AF_BLUETOOTH;
	remote.l2cap_bdaddr = address;
	remote.l2cap_psm = B_HOST_TO_LENDIAN_INT16(psm);
	if (connect(fd, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
		syslog(LOG_NOTICE, "bluetooth_audio: cannot open PSM %#x: %s", psm,
			strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}


void
A2DPSource::_TellServer(bool streaming)
{
	// The audio thread must not wait for the server: a thread of its own
	// sends it on the admin port.
	thread_id thread = spawn_thread(_TellServerEntry, "bt audio tell",
		B_NORMAL_PRIORITY, (void*)(addr_t)(streaming ? 1 : 0));
	if (thread >= 0)
		resume_thread(thread);
}


int32
A2DPSource::_TellServerEntry(void* data)
{
	BMessenger server(BLUETOOTH_SIGNATURE);
	BMessage message(BT_MSG_R5_AUDIO_STREAMING);
	message.AddBool("streaming", data != NULL);
	BMessage reply;
	r5_admin_send(server, &message, &reply, 2000000);
	return B_OK;
}


// #pragma mark - Signalling (manager thread)


int32
A2DPSource::_ManagerEntry(void* data)
{
	((A2DPSource*)data)->_Manage();
	return 0;
}


void
A2DPSource::_Manage()
{
	uint8 buffer[kMediaMTU];

	while (true) {
		fLock.Lock();

		if (fQuitting) {
			if (fSignal >= 0 && fState >= kConfigured && fState != kClosing) {
				// Close the stream, then wait a moment for the accept.
				_StopStreaming();
				uint8 seid = (uint8)(fRemoteSEID << 2);
				if (_SendCommand(kClose, &seid, 1) == B_OK) {
					fState = kClosing;
					set_receive_timeout(fSignal, 1000000);
					ssize_t received = recv(fSignal, buffer, sizeof(buffer),
						0);
					if (received > 0)
						_HandleSignal(buffer, received);
				}
			}
			if (fSignal >= 0) {
				bool clean = fState == kIdle;
				_EndSession(clean, clean ? NULL : "stopped");
			}
			fLock.Unlock();
			break;
		}

		// An AVRCP thread that ended because the headset closed AVCTP.
		if (fControlDone) {
			fControlLock.Lock();
			thread_id done = fControlThread;
			fControlThread = -1;
			fControlDone = false;
			fControlLock.Unlock();
			status_t result;
			if (done >= 0)
				wait_for_thread(done, &result);
		}

		// Channels the listen threads accepted.
		if (fPendingSignal >= 0 && fSignal < 0) {
			fSignal = fPendingSignal;
			fPendingSignal = -1;
			fRemote = fPendingAddress;
			fHasRemote = true;
			fInitiator = false;
			fState = kIdle;
			fSessionStart = system_time();
			fTriedControl = false;
			set_receive_timeout(fSignal, kPollInterval);
		}
		if (fPendingTransport >= 0) {
			if (fTransport < 0)
				fTransport = fPendingTransport;
			else
				close(fPendingTransport);
			fPendingTransport = -1;
			if (fState == kOpening)
				fState = kOpen;
		}
		if (fPendingControl >= 0) {
			fControlLock.Lock();
			if (fControl < 0 && fControlThread < 0) {
				fControl = fPendingControl;
				fControlStop = false;
				fControlDone = false;
				fControlThread = spawn_thread(_ControlEntry,
					"bt audio avrcp", B_NORMAL_PRIORITY, this);
				if (fControlThread >= 0)
					resume_thread(fControlThread);
			} else
				close(fPendingControl);
			fPendingControl = -1;
			fControlLock.Unlock();
		}

		if (fSignal < 0) {
			// No session. Open AVDTP as soon as a paired headset's link is
			// up, not only when sound plays. The WH-1000XM3 ended the ACL
			// (reason 0x13, the headset) about 2 s after SDP when no profile
			// had connected; A2DP used to wait for playback.
			bdaddr_t device;
			bigtime_t now = system_time();
			bool playing = fPlaying
				&& now - fLastSound.load() < kIdleSuspend;
			bool have = now - fLastAttempt >= kRetryInterval
				&& (_ChosenDevice(&device) || _PairedHeadset(&device));
			fLock.Unlock();
			bool linkUp = false;
			if (have && !playing) {
				bool paired = false;
				bool connected = false;
				bool encrypted = false;
				linkUp = _LinkState(device, &paired, &connected, &encrypted)
					&& connected;
			}
			bool connect = have && (playing || linkUp);
			if (!connect) {
				acquire_sem_etc(fWake, 1, B_RELATIVE_TIMEOUT, kPollInterval);
				continue;
			}
			fLastAttempt = system_time();
			syslog(LOG_INFO, "bluetooth_audio: opening AVDTP");
			int fd = _Connect(device, kPSMAVDTP);
			if (fd < 0)
				continue;
			BAutolock locker(fLock);
			if (fSignal >= 0 || fQuitting) {
				close(fd);
				continue;
			}
			fSignal = fd;
			fRemote = device;
			fHasRemote = true;
			fInitiator = true;
			fState = kIdle;
			fSessionStart = system_time();
			fTriedControl = false;
			fTriedAllCapabilities = false;
			fRetriedBitpool = false;
			set_receive_timeout(fSignal, kPollInterval);
			// Without a sent Discover no answer and no timeout would come.
			if (_SendCommand(kDiscover, NULL, 0) != B_OK) {
				syslog(LOG_NOTICE, "bluetooth_audio: AVDTP discover was "
					"not sent");
				_EndSession(false, "the headset did not take the request");
				continue;
			}
			syslog(LOG_INFO, "bluetooth_audio: AVDTP discover");
			fState = kDiscovering;
			continue;
		}

		bigtime_t now = system_time();

		// The media channel failed under the stream thread.
		if (fState == kStreaming && !fStreaming) {
			_EndSession(false, "the media channel was lost");
			fLock.Unlock();
			continue;
		}

		// A command that was never answered ends the session.
		if (fPendingCommand != 0 && now - fPendingSince > kSignalTimeout) {
			_EndSession(false, "the headset did not answer");
			fLock.Unlock();
			continue;
		}

		// The headset configured R5 and did not open the stream.
		if (!fInitiator && fState == kConfigured && fPendingCommand == 0
			&& now - fConfiguredAt > kOpenDelay) {
			uint8 seid = (uint8)(fRemoteSEID << 2);
			if (_SendCommand(kOpen, &seid, 1) == B_OK) {
				syslog(LOG_INFO, "bluetooth_audio: AVDTP open");
				fState = kOpening;
			} else {
				syslog(LOG_NOTICE, "bluetooth_audio: AVDTP open was not "
					"sent");
			}
		}

		// Notify media_server when A2DP transport becomes ready or drops.
		bool ready = fState >= kOpen && fState != kClosing && fTransport >= 0;
		_UpdateReady(ready);

		// Start or suspend with the media node and the headset's buttons.
		bool wanted = fPlaying && !fPaused
			&& now - fLastSound.load() < kIdleSuspend;
		if (fState == kOpen && fTransport >= 0 && wanted
			&& fPendingCommand == 0) {
			uint8 seid = (uint8)(fRemoteSEID << 2);
			if (_SendCommand(kStart, &seid, 1) == B_OK) {
				syslog(LOG_INFO, "bluetooth_audio: AVDTP start");
				fState = kStarting;
			} else {
				syslog(LOG_NOTICE, "bluetooth_audio: AVDTP start was not "
					"sent");
			}
		} else if (fState == kStreaming && !wanted && fPendingCommand == 0) {
			uint8 seid = (uint8)(fRemoteSEID << 2);
			if (_SendCommand(kSuspend, &seid, 1) == B_OK)
				fState = kSuspending;
		}

		// AVRCP, when the headset has not opened it itself.
		bool openControl = false;
		bdaddr_t remote = fRemote;
		if (!fTriedControl && fState >= kOpen
			&& now - fSessionStart > kControlDelay) {
			fTriedControl = true;
			fControlLock.Lock();
			openControl = fControl < 0 && fControlThread < 0;
			fControlLock.Unlock();
		}

		int signal = fSignal;
		fLock.Unlock();

		if (openControl) {
			int fd = _Connect(remote, kPSMAVCTP);
			if (fd >= 0) {
				BAutolock locker(fLock);
				if (fPendingControl < 0)
					fPendingControl = fd;
				else
					close(fd);
				continue;
			}
		}

		ssize_t received = recv(signal, buffer, sizeof(buffer), 0);
		int error = errno;
		BAutolock locker(fLock);
		if (fSignal != signal)
			continue;
		if (received > 0)
			_HandleSignal(buffer, received);
		else if (received == 0 || !is_timeout(error))
			_EndSession(false, "the headset went away");
	}
}


void
A2DPSource::_HandleSignal(const uint8* data, size_t size)
{
	SignalHeader header;
	if (!ParseSignalHeader(data, size, &header)) {
		syslog(LOG_INFO, "bluetooth_audio: AVDTP signal dropped, %zu bytes,"
			" header %#x", size, size >= 1 ? data[0] : 0);
		// Fragmented or broken: a command gets a General Reject (8.18).
		if (size >= 1 && (data[0] & 3) == kMessageCommand) {
			uint8 reply[2];
			BuildSignalHeader(reply, sizeof(reply), data[0] >> 4,
				kMessageGeneralReject, size >= 2 ? data[1] : 0);
			send(fSignal, reply, sizeof(reply), 0);
		}
		return;
	}

	const uint8* payload = data + header.length;
	size_t payloadSize = size - header.length;
	syslog(LOG_INFO, "bluetooth_audio: AVDTP signal %u label %u type %u"
		" received (%zu bytes)", header.signal, header.label,
		header.messageType, size);
	if (header.messageType == kMessageCommand) {
		_HandleCommand(header.label, header.signal, payload, payloadSize);
		return;
	}
	if (fPendingCommand == 0 || header.label != fPendingLabel) {
		syslog(LOG_INFO, "bluetooth_audio: AVDTP signal %u label %u dropped"
			" (pending %u label %u)", header.signal, header.label,
			fPendingCommand, fPendingLabel);
		return;
	}
	// A General Reject may not name the signal.
	if (header.messageType != kMessageGeneralReject
		&& header.signal != fPendingCommand)
		return;
	uint8 signal = fPendingCommand;
	fPendingCommand = 0;
	_HandleResponse(header.messageType, signal, payload, payloadSize);
}


status_t
A2DPSource::_SendCommand(uint8 signal, const uint8* payload, size_t size)
{
	uint8 packet[2 + 64];
	if (size > sizeof(packet) - 2)
		return B_BAD_VALUE;
	fLabel = (uint8)((fLabel + 1) & 0x0f);
	BuildSignalHeader(packet, sizeof(packet), fLabel, kMessageCommand, signal);
	if (size > 0)
		memcpy(packet + 2, payload, size);
	if (send(fSignal, packet, 2 + size, 0) < 0)
		return errno;
	syslog(LOG_INFO, "bluetooth_audio: AVDTP signal %u label %u sent"
		" (%zu bytes)", signal, fLabel, 2 + size);
	fPendingLabel = fLabel;
	fPendingCommand = signal;
	fPendingSince = system_time();
	return B_OK;
}


void
A2DPSource::_SendReply(uint8 label, uint8 messageType, uint8 signal,
	const uint8* payload, size_t size)
{
	uint8 packet[2 + 64];
	if (size > sizeof(packet) - 2)
		return;
	BuildSignalHeader(packet, sizeof(packet), label, messageType, signal);
	if (size > 0)
		memcpy(packet + 2, payload, size);
	send(fSignal, packet, 2 + size, 0);
}


void
A2DPSource::_HandleCommand(uint8 label, uint8 signal, const uint8* payload,
	size_t size)
{
	uint8 reply[64];
	bool seidOK = size >= 1 && ((payload[0] >> 2) & 0x3f) == kLocalSEID;

	switch (signal) {
		case kDiscover:
		{
			reply[0] = (uint8)((kLocalSEID << 2)
				| (fState >= kConfigured ? 2 : 0));
			reply[1] = (uint8)(kMediaTypeAudio << 4);	// TSEP 0: source
			_SendReply(label, kMessageAccept, signal, reply, 2);
			return;
		}

		case kGetCapabilities:
		case kGetAllCapabilities:
		{
			if (!seidOK) {
				reply[0] = kErrorBadACPSEID;
				_SendReply(label, kMessageReject, signal, reply, 1);
				return;
			}
			// Delay reporting is an AVDTP 1.3 capability: only in Get All
			// Capabilities (8.8).
			size_t length = BuildSourceCapabilities(reply, sizeof(reply),
				signal == kGetAllCapabilities);
			_SendReply(label, kMessageAccept, signal, reply, length);
			return;
		}

		case kSetConfiguration:
		{
			if (size < 2 || !seidOK) {
				reply[0] = 0;
				reply[1] = size < 2 ? kErrorBadLength : kErrorBadACPSEID;
				_SendReply(label, kMessageReject, signal, reply, 2);
				return;
			}
			if (fState >= kConfigured) {
				reply[0] = 0;
				reply[1] = kErrorSEPInUse;
				_SendReply(label, kMessageReject, signal, reply, 2);
				return;
			}
			SBCCapabilities caps;
			uint8 badCategory;
			uint8 error = ParseCapabilities(payload + 2, size - 2, &caps,
				&badCategory);
			sbc::Config config;
			if (error == 0 && (!caps.mediaTransport || !caps.sbc)) {
				badCategory = caps.sbc ? kCategoryMediaTransport
					: kCategoryMediaCodec;
				error = caps.sbc ? kErrorInvalidCapabilities
					: kErrorNotSupportedCodecType;
			}
			if (error == 0 && !ConfigFromCapabilities(caps, &config)) {
				badCategory = kCategoryMediaCodec;
				error = kErrorUnsupportedConfiguration;
			}
			// The mixer delivers fRate; R5 does not resample, and only
			// offers that rate (BuildSourceCapabilities()).
			if (error == 0 && sbc::SampleRate(config) != fRate) {
				badCategory = kCategoryMediaCodec;
				error = kErrorNotSupportedSamplingFrequency;
			}
			if (error != 0) {
				reply[0] = badCategory;
				reply[1] = error;
				_SendReply(label, kMessageReject, signal, reply, 2);
				return;
			}
			fRemoteSEID = (payload[1] >> 2) & 0x3f;
			fConfig = config;
			fDelayReporting = caps.delayReporting;
			fState = kConfigured;
			fConfiguredAt = system_time();
			fPendingCommand = 0;
			_SendReply(label, kMessageAccept, signal, NULL, 0);
			return;
		}

		case kGetConfiguration:
		{
			if (!seidOK || fState < kConfigured) {
				reply[0] = seidOK ? kErrorBadState : kErrorBadACPSEID;
				_SendReply(label, kMessageReject, signal, reply, 1);
				return;
			}
			size_t length = BuildConfiguration(reply, sizeof(reply), fConfig,
				fDelayReporting);
			_SendReply(label, kMessageAccept, signal, reply, length);
			return;
		}

		case kReconfigure:
			// R5 configures once per stream; a new setting needs Close.
			reply[0] = kCategoryMediaCodec;
			reply[1] = kErrorNotSupportedCommand;
			_SendReply(label, kMessageReject, signal, reply, 2);
			return;

		case kOpen:
			if (!seidOK || fState != kConfigured) {
				reply[0] = seidOK ? kErrorBadState : kErrorBadACPSEID;
				_SendReply(label, kMessageReject, signal, reply, 1);
				return;
			}
			// The headset opens the transport channel next.
			fState = kOpening;
			if (fPendingCommand == kOpen)
				fPendingCommand = 0;
			_SendReply(label, kMessageAccept, signal, NULL, 0);
			return;

		case kStart:
			if (!seidOK || (fState != kOpen && fState != kStarting)
				|| fTransport < 0) {
				reply[0] = size >= 1 ? payload[0] : 0;
				reply[1] = seidOK ? kErrorBadState : kErrorBadACPSEID;
				_SendReply(label, kMessageReject, signal, reply, 2);
				return;
			}
			_SendReply(label, kMessageAccept, signal, NULL, 0);
			if (fPendingCommand == kStart)
				fPendingCommand = 0;
			fPaused = false;
			_StartStreaming();
			return;

		case kSuspend:
			if (!seidOK || fState < kOpen) {
				reply[0] = size >= 1 ? payload[0] : 0;
				reply[1] = seidOK ? kErrorBadState : kErrorBadACPSEID;
				_SendReply(label, kMessageReject, signal, reply, 2);
				return;
			}
			_SendReply(label, kMessageAccept, signal, NULL, 0);
			_StopStreaming();
			fState = kOpen;
			// The headset paused: stay paused until play or a new start.
			fPaused = true;
			return;

		case kClose:
			if (!seidOK || fState < kConfigured) {
				reply[0] = seidOK ? kErrorBadState : kErrorBadACPSEID;
				_SendReply(label, kMessageReject, signal, reply, 1);
				return;
			}
			_SendReply(label, kMessageAccept, signal, NULL, 0);
			_StopStreaming();
			_CloseTransport();
			fState = kIdle;
			syslog(LOG_INFO, "bluetooth_audio: stream closed cleanly");
			return;

		case kAbort:
			_SendReply(label, kMessageAccept, signal, NULL, 0);
			_StopStreaming();
			_CloseTransport();
			fState = kIdle;
			fPendingCommand = 0;
			syslog(LOG_NOTICE, "bluetooth_audio: stream aborted by the "
				"headset");
			return;

		case kDelayReport:
		{
			if (size < 3 || !seidOK) {
				reply[0] = size >= 1 ? payload[0] : 0;
				reply[1] = size < 3 ? kErrorBadLength : kErrorBadACPSEID;
				_SendReply(label, kMessageReject, signal, reply, 2);
				return;
			}
			// AVDTP 1.3, 8.19: in 1/10 milliseconds.
			uint16 delay = (uint16)((payload[1] << 8) | payload[2]);
			_SendReply(label, kMessageAccept, signal, NULL, 0);
			bigtime_t value = (bigtime_t)delay * 100;
			if (value > kMaxRemoteDelay)
				value = kMaxRemoteDelay;
			fRemoteDelay = value;
			if (fListener != NULL)
				fListener->RemoteDelayChanged(value);
			return;
		}

		case kSecurityControl:
			reply[0] = kErrorNotSupportedCommand;
			_SendReply(label, kMessageReject, signal, reply, 1);
			return;

		default:
			_SendReply(label, kMessageGeneralReject, signal, NULL, 0);
			return;
	}
}


void
A2DPSource::_AskCapabilities()
{
	fRemoteSEID = fCandidates[fCandidateIndex];
	uint8 seid = (uint8)(fRemoteSEID << 2);
	fTriedAllCapabilities = true;
	fRetriedBitpool = false;
	if (_SendCommand(kGetAllCapabilities, &seid, 1) == B_OK) {
		syslog(LOG_INFO, "bluetooth_audio: AVDTP get capabilities, "
			"endpoint %u", fRemoteSEID);
		fState = kGettingCapabilities;
	} else {
		syslog(LOG_NOTICE, "bluetooth_audio: AVDTP get capabilities was "
			"not sent");
	}
}


// False when this endpoint has no SBC setting R5 can use (the caller tries
// the next one); every other failure ends the session here.
bool
A2DPSource::_SendSetConfiguration()
{
	SBCCapabilities caps;
	uint8 badCategory;
	sbc::Config config;
	if (ParseCapabilities(fSinkCapabilities, fSinkCapabilitiesLength, &caps,
			&badCategory) != 0
		|| !ChooseConfiguration(caps, fRate, &config))
		return false;
	if (fRetriedBitpool && config.bitpool > 53) {
		// A headset that refused its own maximum: the A2DP high quality
		// setting (table 4.7) instead.
		config.bitpool = caps.minBitpool > 53 ? caps.minBitpool : 53;
	}

	uint8 payload[2 + 16];
	payload[0] = (uint8)(fRemoteSEID << 2);
	payload[1] = (uint8)(kLocalSEID << 2);
	bool delay = caps.delayReporting;
	size_t length = BuildConfiguration(payload + 2, sizeof(payload) - 2,
		config, delay);
	if (length == 0) {
		_EndSession(false, "internal");
		return true;
	}
	fConfig = config;
	fDelayReporting = delay;
	if (_SendCommand(kSetConfiguration, payload, 2 + length) == B_OK) {
		syslog(LOG_INFO, "bluetooth_audio: AVDTP set configuration, "
			"endpoint %u", fRemoteSEID);
		fState = kConfiguring;
	} else {
		syslog(LOG_NOTICE, "bluetooth_audio: AVDTP set configuration was "
			"not sent");
	}
	return true;
}


void
A2DPSource::_HandleResponse(uint8 messageType, uint8 signal,
	const uint8* payload, size_t size)
{
	bool accepted = messageType == kMessageAccept;

	switch (signal) {
		case kDiscover:
		{
			EndpointInfo endpoints[16];
			int count = accepted
				? ParseDiscover(payload, size, endpoints, 16) : -1;
			// A headset often lists AAC, aptX or LDAC first and SBC on a
			// later endpoint: each free audio sink is asked in turn.
			fCandidateCount = 0;
			fCandidateIndex = 0;
			for (int i = 0; i < count && fCandidateCount < 16; i++) {
				if (endpoints[i].sink && !endpoints[i].inUse
					&& endpoints[i].mediaType == kMediaTypeAudio)
					fCandidates[fCandidateCount++] = endpoints[i].seid;
			}
			syslog(LOG_INFO, "bluetooth_audio: AVDTP discover answer, %d "
				"audio sinks", fCandidateCount);
			if (fCandidateCount == 0) {
				_EndSession(false, "no free audio sink on the headset");
				return;
			}
			_AskCapabilities();
			return;
		}

		case kGetAllCapabilities:
		case kGetCapabilities:
			if (!accepted) {
				if (signal == kGetAllCapabilities) {
					// An AVDTP 1.2 headset (8.8).
					uint8 seid = (uint8)(fRemoteSEID << 2);
					_SendCommand(kGetCapabilities, &seid, 1);
					return;
				}
				if (++fCandidateIndex < fCandidateCount) {
					_AskCapabilities();
					return;
				}
				_EndSession(false, "the headset refused its capabilities");
				return;
			}
			if (size > sizeof(fSinkCapabilities))
				size = sizeof(fSinkCapabilities);
			memcpy(fSinkCapabilities, payload, size);
			fSinkCapabilitiesLength = size;
			if (!_SendSetConfiguration()) {
				if (++fCandidateIndex < fCandidateCount) {
					_AskCapabilities();
					return;
				}
				_EndSession(false,
					"the headset offers no SBC setting R5 can use");
			}
			return;

		case kSetConfiguration:
			if (!accepted) {
				if (!fRetriedBitpool && fConfig.bitpool > 53) {
					fRetriedBitpool = true;
					if (!_SendSetConfiguration())
						_EndSession(false, "the headset refused the SBC "
							"setting");
					return;
				}
				_EndSession(false, "the headset refused the SBC setting");
				return;
			}
			{
				fState = kConfigured;
				uint8 seid = (uint8)(fRemoteSEID << 2);
				if (_SendCommand(kOpen, &seid, 1) == B_OK)
					fState = kOpening;
			}
			return;

		case kOpen:
		{
			if (!accepted) {
				_EndSession(false, "the headset refused to open the stream");
				return;
			}
			// R5 sent Open, so R5 opens the media channel (AVDTP 6.11).
			bdaddr_t remote = fRemote;
			int signalChannel = fSignal;
			fLock.Unlock();
			int fd = _Connect(remote, kPSMAVDTP);
			fLock.Lock();
			if (fSignal != signalChannel) {
				if (fd >= 0)
					close(fd);
				return;
			}
			if (fd < 0) {
				_EndSession(false, "no media channel");
				return;
			}
			if (fTransport >= 0)
				close(fd);
			else
				fTransport = fd;
			syslog(LOG_INFO, "bluetooth_audio: AVDTP stream open");
			fState = kOpen;
			return;
		}

		case kStart:
			if (!accepted) {
				fState = kOpen;
				// Try again with the next poll only when asked to play
				// again: pause until then.
				fPaused = true;
				return;
			}
			_StartStreaming();
			return;

		case kSuspend:
			_StopStreaming();
			fState = kOpen;
			return;

		case kClose:
			_StopStreaming();
			_CloseTransport();
			fState = kIdle;
			if (accepted)
				syslog(LOG_INFO, "bluetooth_audio: stream closed cleanly");
			return;
	}
}


void
A2DPSource::_StartStreaming()
{
	if (fStreaming || fTransport < 0)
		return;
	sbc::Encoder probe;
	if (!probe.Init(fConfig)) {
		_EndSession(false, "bad SBC setting");
		return;
	}
	fState = kStreaming;
	fStreaming = true;
	// Begin with recent audio, not all that queued while the stream was
	// down, but keep the last moment: the sound that woke the stream came
	// in while Start went to the headset and back.
	size_t write = fRingWrite.load(std::memory_order_acquire);
	size_t fill = write - fRingRead.load(std::memory_order_relaxed);
	size_t keep = (size_t)((int64)fRate * kStartKeep / 1000000);
	fRingRead.store(write - (fill < keep ? fill : keep),
		std::memory_order_release);
	fStreamThread = spawn_thread(_StreamEntry, "bt audio stream",
		B_REAL_TIME_PRIORITY, this);
	if (fStreamThread < 0) {
		fStreaming = false;
		fState = kOpen;
		return;
	}
	resume_thread(fStreamThread);
	_TellServer(true);

	static const char* kModes[4] = { "mono", "dual channel", "stereo",
		"joint stereo" };
	syslog(LOG_INFO, "bluetooth_audio: streaming sbc %dHz",
		sbc::SampleRate(fConfig));
	syslog(LOG_INFO, "bluetooth_audio: sbc %s, %d blocks, %d subbands, "
		"bitpool %d, %u kb/s", kModes[fConfig.mode & 3], fConfig.blocks,
		fConfig.subbands, fConfig.bitpool, sbc::BitRate(fConfig) / 1000);
}


void
A2DPSource::_StopStreaming()
{
	if (!fStreaming && fStreamThread < 0)
		return;
	fStreaming = false;
	if (fStreamThread >= 0) {
		status_t result;
		thread_id thread = fStreamThread;
		fStreamThread = -1;
		// The stream thread never takes fLock.
		wait_for_thread(thread, &result);
	}
	_TellServer(false);
}


void
A2DPSource::_CloseTransport()
{
	if (fTransport >= 0) {
		shutdown(fTransport, SHUT_RDWR);
		close(fTransport);
		fTransport = -1;
	}
}


void
A2DPSource::_UpdateReady(bool ready)
{
	if (fReportedReady == ready)
		return;
	fReportedReady = ready;
	if (fListener != NULL)
		fListener->ConnectionStateChanged(ready);
}


void
A2DPSource::_EndSession(bool clean, const char* reason)
{
	_StopStreaming();
	_CloseTransport();
	if (fSignal >= 0) {
		shutdown(fSignal, SHUT_RDWR);
		close(fSignal);
		fSignal = -1;
	}

	// The session is over before fLock is let go below, so a listen thread
	// that takes it meanwhile sees no session.
	if (!clean && reason != NULL)
		syslog(LOG_NOTICE, "bluetooth_audio: stream ended: %s", reason);
	fState = kIdle;
	fHasRemote = false;
	fPendingCommand = 0;
	fAbsoluteVolume = false;
	_UpdateReady(false);

	// The AVRCP thread reads with a timeout and sees fControlStop within
	// kPollInterval: shutdown() alone does not wake an L2CAP recv().
	fControlLock.Lock();
	int control = fControl;
	thread_id controlThread = fControlThread;
	fControlStop = true;
	if (control >= 0)
		shutdown(control, SHUT_RDWR);
	fControlLock.Unlock();
	if (controlThread >= 0) {
		status_t result;
		fLock.Unlock();
		wait_for_thread(controlThread, &result);
		fLock.Lock();
		fControlLock.Lock();
		if (fControlThread == controlThread)
			fControlThread = -1;
		fControlDone = false;
		fControlLock.Unlock();
	}
}


// #pragma mark - Media (real-time thread)


int32
A2DPSource::_StreamEntry(void* data)
{
	((A2DPSource*)data)->_Stream();
	return 0;
}


void
A2DPSource::_Stream()
{
	sbc::Encoder encoder;
	if (!encoder.Init(fConfig))
		return;
	int transport = fTransport;

	const int channels = sbc::Channels(fConfig);
	const int rate = sbc::SampleRate(fConfig);
	const size_t frameSamples = encoder.FrameSamples();
	const size_t frameLength = encoder.FrameLength();
	int framesPerPacket = (int)((kMediaMTU - kMediaHeaderLength)
		/ frameLength);
	if (framesPerPacket > 15)
		framesPerPacket = 15;
	if (framesPerPacket < 1)
		return;

	uint8 packet[kMediaMTU];
	float stereo[16 * 8 * 2];
	float frame[16 * 8 * 2];
	uint16 sequence = (uint16)(system_time() & 0xffff);
	uint32 timestamp = 0;
	const uint32 ssrc = 1;

	bigtime_t start = system_time();
	int64 sent = 0;
	int32 underruns = 0;

	while (fStreaming) {
		bigtime_t due = start + sent * 1000000 / rate - kSendLead;
		bigtime_t now = system_time();
		if (now < due)
			snooze_until(due, B_SYSTEM_TIMEBASE);
		else if (now - due > kResyncAfter) {
			start = now;
			sent = 0;
		}
		if (!fStreaming)
			break;

		// The node's clock and the headset's run apart a little: keep the
		// buffered audio near its target instead of letting it grow.
		// The mixer sends its buffers BufferLatency() early, the headset
		// delay included, so that is what normally waits here.
		size_t ringTarget = (size_t)((int64)rate
			* (kRingLatency + fRemoteDelay.load()) / 1000000);
		// A little too much (clock drift, about 50 ppm): one sample less
		// per packet, which is not heard. Far too much (the thread did not
		// run): back to the target at once.
		size_t fill = _RingFill();
		if (fill > ringTarget * 6)
			fRingRead.fetch_add(fill - ringTarget, std::memory_order_acq_rel);
		else if (fill > ringTarget * 2)
			fRingRead.fetch_add(1, std::memory_order_acq_rel);

		float gain = fSoftwareGain;
		size_t length = kMediaHeaderLength;
		int framesWritten = 0;
		for (int f = 0; f < framesPerPacket; f++) {
			if (_ReadRing(stereo, frameSamples) < frameSamples)
				underruns++;
			for (size_t i = 0; i < frameSamples; i++) {
				float left = stereo[i * 2] * gain;
				float right = stereo[i * 2 + 1] * gain;
				if (channels == 1)
					frame[i] = (left + right) * 0.5f;
				else {
					frame[i * 2] = left;
					frame[i * 2 + 1] = right;
				}
			}
			size_t written = encoder.Encode(frame, packet + length,
				sizeof(packet) - length);
			if (written == 0)
				break;
			length += written;
			framesWritten++;
		}
		// The header counts the frames in the packet, not the frames asked
		// for: an encoder that stopped early wrote fewer.
		if (framesWritten == 0) {
			// The encoder wrote nothing: the stream cannot go on.
			fStreaming = false;
			if (fWake >= 0)
				release_sem(fWake);
			break;
		}
		BuildMediaHeader(packet, sizeof(packet), sequence, timestamp, ssrc,
			framesWritten);

		if (send(transport, packet, length, 0) < 0) {
			int error = errno;
			// The headset asked for a smaller L2CAP MTU than 672 (the
			// socket does not say): this packet is lost, the next ones
			// carry fewer frames.
			if (error == EMSGSIZE && framesPerPacket > 1) {
				// The lost audio still counts, so timing and RTP stay right.
				sequence++;
				timestamp += (uint32)(framesWritten * frameSamples);
				sent += framesWritten * frameSamples;
				framesPerPacket--;
				syslog(LOG_INFO, "bluetooth_audio: %d frames per packet "
					"(the headset's MTU is smaller)", framesPerPacket);
				continue;
			}
			// A full queue drops this packet; anything else ends the
			// stream, and the signalling thread notices the loss.
			if (error != ENOBUFS && error != EAGAIN && error != B_NO_MEMORY) {
				fStreaming = false;
				if (fWake >= 0)
					release_sem(fWake);
				break;
			}
		}
		sequence++;
		timestamp += (uint32)(framesWritten * frameSamples);
		sent += framesWritten * frameSamples;
	}

	if (underruns > 0) {
		syslog(LOG_INFO, "bluetooth_audio: %" B_PRId32 " frames without "
			"audio from the mixer (sent as silence)", underruns);
	}
}


// #pragma mark - AVRCP (control thread)


int32
A2DPSource::_ControlEntry(void* data)
{
	((A2DPSource*)data)->_Control();
	return 0;
}


void
A2DPSource::_Control()
{
	fControlLock.Lock();
	int fd = fControl;
	fControlLock.Unlock();

	// Ask which notifications the headset has (AVRCP 1.6, 6.4.1); absolute
	// volume needs EVENT_VOLUME_CHANGED.
	uint8 parameter = kCapabilityEvents;
	uint8 message[32];
	fControlLock.Lock();
	fControlLabel = (uint8)((fControlLabel + 1) & 0x0f);
	size_t length = BuildVendor(message, sizeof(message), fControlLabel,
		false, kCTypeStatus, kPDUGetCapabilities, &parameter, 1);
	fControlLock.Unlock();
	_SendControl(message, length);

	set_receive_timeout(fd, kPollInterval);
	uint8 buffer[512];
	while (!fQuitting && !fControlStop) {
		ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
		if (received > 0) {
			_HandleControl(buffer, received);
			continue;
		}
		if (received == 0 || !is_timeout(errno))
			break;
	}

	// Who started this thread waits for it: _EndSession(), or the manager
	// when fControlDone says it ended by itself.
	fControlLock.Lock();
	close(fd);
	fControl = -1;
	fAbsoluteVolume = false;
	fControlDone = true;
	fControlLock.Unlock();
	if (fWake >= 0)
		release_sem(fWake);
}


void
A2DPSource::_SendControl(const uint8* data, size_t size)
{
	if (size == 0)
		return;
	BAutolock locker(fControlLock);
	if (fControl >= 0)
		send(fControl, data, size, MSG_DONTWAIT);
}


void
A2DPSource::_SendAbsoluteVolume(uint8 volume)
{
	uint8 message[32];
	uint8 parameter = (uint8)(volume & 0x7f);
	size_t length;
	{
		BAutolock locker(fControlLock);
		fControlLabel = (uint8)((fControlLabel + 1) & 0x0f);
		length = BuildVendor(message, sizeof(message), fControlLabel, false,
			kCTypeControl, kPDUSetAbsoluteVolume, &parameter, 1);
	}
	_SendControl(message, length);
}


void
A2DPSource::_HandleControl(const uint8* data, size_t size)
{
	AVCTPHeader header;
	if (!ParseAVCTP(data, size, &header))
		return;

	uint8 reply[64];
	if (header.pid != kAVRemoteControlPID) {
		// AVCTP 1.4, 6.1.1: answer with IPID set and no payload.
		if (!header.response) {
			BuildAVCTP(reply, sizeof(reply), header.label, true);
			reply[0] |= 1;
			reply[1] = data[1];
			reply[2] = data[2];
			_SendControl(reply, 3);
		}
		return;
	}

	AVCFrame frame;
	if (!ParseAVC(data + 3, size - 3, &frame))
		return;

	if (header.response) {
		// Answers to R5's own commands.
		VendorPDU pdu;
		if (!ParseVendor(frame, &pdu))
			return;
		if (pdu.pdu == kPDUGetCapabilities
			&& frame.ctype == kResponseStable && pdu.length >= 2
			&& pdu.parameters[0] == kCapabilityEvents) {
			uint8 count = pdu.parameters[1];
			bool volume = false;
			for (uint8 i = 0; i < count && 2u + i < pdu.length; i++) {
				if (pdu.parameters[2 + i] == kEventVolumeChanged)
					volume = true;
			}
			if (volume) {
				uint8 parameters[5] = { kEventVolumeChanged, 0, 0, 0, 0 };
				uint8 message[32];
				size_t length;
				{
					BAutolock locker(fControlLock);
					fControlLabel = (uint8)((fControlLabel + 1) & 0x0f);
					length = BuildVendor(message, sizeof(message),
						fControlLabel, false, kCTypeNotify,
						kPDURegisterNotification, parameters, 5);
				}
				_SendControl(message, length);
			}
			return;
		}
		if (pdu.pdu == kPDURegisterNotification && pdu.length >= 2
			&& pdu.parameters[0] == kEventVolumeChanged
			&& (frame.ctype == kResponseInterim
				|| frame.ctype == kResponseChanged)) {
			// The headset now does the volume, and R5 takes over the level
			// it has (never a jump to R5's last level, which could be loud).
			fAbsoluteVolume = true;
			fSoftwareGain = 1.0f;
			uint8 volume = pdu.parameters[1] & 0x7f;
			fVolume = volume;
			if (fListener != NULL)
				fListener->RemoteVolumeChanged(GainFromVolume(volume));
			if (frame.ctype == kResponseChanged) {
				// A notification fires once (6.7): register again.
				uint8 parameters[5] = { kEventVolumeChanged, 0, 0, 0, 0 };
				uint8 message[32];
				size_t length;
				{
					BAutolock locker(fControlLock);
					fControlLabel = (uint8)((fControlLabel + 1) & 0x0f);
					length = BuildVendor(message, sizeof(message),
						fControlLabel, false, kCTypeNotify,
						kPDURegisterNotification, parameters, 5);
				}
				_SendControl(message, length);
			}
			return;
		}
		if (pdu.pdu == kPDUSetAbsoluteVolume
			&& frame.ctype == kResponseAccepted && pdu.length >= 1) {
			fVolume = pdu.parameters[0] & 0x7f;
		}
		return;
	}

	// Commands from the headset (it is the controller for its buttons).
	BuildAVCTP(reply, sizeof(reply), header.label, true);
	size_t length = 3;
	switch (frame.opcode) {
		case kOpcodeUnitInfo:
		case kOpcodeSubunitInfo:
		{
			// AV/C: a panel unit, no company ID of its own.
			reply[3] = kResponseStable;
			reply[4] = kSubunitUnit;
			reply[5] = frame.opcode;
			reply[6] = 0x07;
			reply[7] = kSubunitPanel;
			reply[8] = 0xff;
			reply[9] = 0xff;
			reply[10] = 0xff;
			length = 11;
			break;
		}

		case kOpcodePassThrough:
		{
			PassThrough pass;
			bool known = ParsePassThrough(frame, &pass)
				&& (pass.operation == kOpPlay || pass.operation == kOpPause
					|| pass.operation == kOpStop);
			size_t copy = 3 + frame.operandLength;
			if (copy > sizeof(reply) - 3)
				copy = sizeof(reply) - 3;
			memcpy(reply + 3, data + 3, copy);
			reply[3] = known ? kResponseAccepted : kResponseNotImplemented;
			length = 3 + copy;
			if (known && !pass.released) {
				// Play starts the stream again; pause and stop suspend it.
				fPaused = pass.operation != kOpPlay;
				if (fWake >= 0)
					release_sem(fWake);
			}
			break;
		}

		case kOpcodeVendorDependent:
		{
			VendorPDU pdu;
			bool parsed = ParseVendor(frame, &pdu);
			if (parsed && pdu.pdu == kPDUGetCapabilities && pdu.length >= 1) {
				// AVRCP 1.6, 6.4.1: Company ID and Events are mandatory;
				// R5 sends no notifications, so the event list is empty.
				// Another capability ID is an invalid parameter.
				if (pdu.parameters[0] == kCapabilityCompanyID) {
					uint8 parameters[5] = { kCapabilityCompanyID, 1,
						(uint8)(kBluetoothSIGCompanyID >> 16),
						(uint8)(kBluetoothSIGCompanyID >> 8),
						(uint8)kBluetoothSIGCompanyID };
					length = BuildVendor(reply, sizeof(reply), header.label,
						true, kResponseStable, kPDUGetCapabilities,
						parameters, 5);
				} else if (pdu.parameters[0] == kCapabilityEvents) {
					uint8 parameters[2] = { kCapabilityEvents, 0 };
					length = BuildVendor(reply, sizeof(reply), header.label,
						true, kResponseStable, kPDUGetCapabilities,
						parameters, 2);
				} else {
					uint8 error = kAVRCPErrorInvalidParameter;
					length = BuildVendor(reply, sizeof(reply), header.label,
						true, kResponseRejected, kPDUGetCapabilities, &error,
						1);
				}
				break;
			}
			// R5 is no media player: no metadata, no notifications. A
			// rejected command carries its PDU and an error (6.15.1).
			uint8 error = kAVRCPErrorInvalidCommand;
			length = BuildVendor(reply, sizeof(reply), header.label, true,
				kResponseRejected, frame.operandLength >= 4
					? frame.operands[3] : 0, &error, 1);
			break;
		}

		default:
		{
			size_t copy = 3 + frame.operandLength;
			if (copy > sizeof(reply) - 3)
				copy = sizeof(reply) - 3;
			memcpy(reply + 3, data + 3, copy);
			reply[3] = kResponseNotImplemented;
			length = 3 + copy;
			break;
		}
	}
	_SendControl(reply, length);
}
