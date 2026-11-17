/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 */


#include "VoiceSource.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <Autolock.h>

#include <R5BluetoothVoice.h>


// Samples kept before the node takes any (40 ms), and the most it keeps
// before it drops the oldest to stay near real time (200 ms down to 60 ms).
static const size_t kPrime = 640;
static const size_t kMaxQueued = 3200;
static const size_t kTrimTo = 960;
static const bigtime_t kLinkPoll = 250000;


struct VoiceSource::FrameSink {
	VoiceSource*	source;

	void operator()(const uint8_t* frame, int lost)
	{
		float samples[sbc::kMSBCFrameSamples];
		memset(samples, 0, sizeof(samples));
		// What the sequence numbers say is missing comes back as silence,
		// at most three frames (the sequence counts to four).
		for (int i = 0; i < lost; i++)
			source->_Push(samples, sbc::kMSBCFrameSamples);

		size_t length;
		int channels;
		int count;
		if (source->fDecoder.Decode(frame, sbc::kMSBCFrameLength, samples,
				sbc::kMSBCFrameSamples, &length, &channels, &count)
				!= sbc::kDecodeOK
			|| channels != 1 || count != sbc::kMSBCFrameSamples)
			memset(samples, 0, sizeof(samples));
		source->_Push(samples, sbc::kMSBCFrameSamples);
	}
};


VoiceSource::VoiceSource()
	:
	fThread(-1),
	fQuit(false),
	fNode(-1),
	fRunning(false),
	fHandle(0),
	fAirMode(0),
	fOutHave(0),
	fOutSequence(0),
	fLastPCM(0.0f),
	fPrimed(false),
	fLock("bluetooth voice"),
	fRingStart(0),
	fRingCount(0)
{
	memset(fSilence, 0, sizeof(fSilence));
	memset(fOut, 0, sizeof(fOut));
	if (fEncoder.InitMSBC()) {
		// The analysis starts from silence, so a few frames in every frame
		// of silence is the same; that one is sent back.
		for (int i = 0; i < 4; i++)
			fEncoder.EncodeSilence(fSilence, sizeof(fSilence));
	}
}


VoiceSource::~VoiceSource()
{
	Stop();
}


status_t
VoiceSource::Start()
{
	if (fThread >= 0)
		return B_OK;
	fQuit = false;
	fThread = spawn_thread(_ThreadEntry, "bluetooth headset microphone",
		B_REAL_TIME_PRIORITY, this);
	if (fThread < 0)
		return fThread;
	return resume_thread(fThread);
}


void
VoiceSource::Stop()
{
	if (fThread < 0)
		return;
	fQuit = true;
	status_t result;
	wait_for_thread(fThread, &result);
	fThread = -1;
	_Close();
}


void
VoiceSource::Read(float* samples, size_t frames)
{
	BAutolock _(fLock);
	if (fRingCount > kMaxQueued) {
		size_t drop = fRingCount - kTrimTo;
		fRingStart = (fRingStart + drop) % kRate;
		fRingCount -= drop;
	}

	size_t take = 0;
	if (fRingCount >= kPrime)
		fPrimed = true;
	if (fPrimed)
		take = fRingCount < frames ? fRingCount : frames;
	for (size_t i = 0; i < take; i++) {
		samples[i] = fRing[fRingStart];
		fRingStart = (fRingStart + 1) % kRate;
	}
	fRingCount -= take;
	if (take < frames) {
		memset(samples + take, 0, (frames - take) * sizeof(float));
		fPrimed = false;
	}
}


status_t
VoiceSource::_ThreadEntry(void* data)
{
	((VoiceSource*)data)->_Thread();
	return B_OK;
}


void
VoiceSource::_Thread()
{
	bigtime_t nextPoll = 0;
	uint8 packet[R5_BTVOICE_MAX_PACKET];

	while (!fQuit) {
		if (fNode < 0 && !_Open()) {
			// No radio, or bt_voice is off: look again now and then.
			for (int i = 0; i < 10 && !fQuit; i++)
				snooze(100000);
			continue;
		}

		if (system_time() >= nextPoll) {
			nextPoll = system_time() + kLinkPoll;
			r5_btvoice_link link;
			if (ioctl(fNode, R5_BTVOICE_GET_LINK, &link, sizeof(link)) != 0) {
				_Close();
				continue;
			}
			bool running = link.running != 0;
			if (running && (!fRunning || link.handle != fHandle
					|| link.air_mode != fAirMode)) {
				fHandle = link.handle;
				fAirMode = link.air_mode;
				fDeframer.Reset();
				fDecoder.Reset();
				fOutHave = 0;
				fLastPCM = 0.0f;
				syslog(LOG_INFO, "bluetooth_audio: headset microphone on "
					"(%s)", fAirMode == R5_BTVOICE_AIR_TRANSPARENT
						? "mSBC 16000Hz" : "CVSD 8000Hz");
			} else if (!running && fRunning) {
				syslog(LOG_INFO, "bluetooth_audio: headset microphone off "
					"(%u packets lost)", (unsigned)link.dropped);
			}
			fRunning = running;
		}

		ssize_t size = read(fNode, packet, sizeof(packet));
		if (size < 0) {
			if (errno == B_INTERRUPTED)
				continue;
			_Close();
			continue;
		}
		if (size > 0 && fRunning)
			_Packet(packet, (size_t)size);
	}
}


bool
VoiceSource::_Open()
{
	for (int i = 0; i < 16; i++) {
		char path[64];
		snprintf(path, sizeof(path), "/dev/" R5_BTVOICE_DIRECTORY "/%d", i);
		int node = open(path, O_RDWR | O_CLOEXEC);
		if (node >= 0) {
			fNode = node;
			return true;
		}
	}
	return false;
}


void
VoiceSource::_Close()
{
	if (fNode >= 0)
		close(fNode);
	fNode = -1;
	if (fRunning)
		syslog(LOG_INFO, "bluetooth_audio: headset microphone off");
	fRunning = false;
}


void
VoiceSource::_Packet(const uint8* packet, size_t size)
{
	if (size < 3)
		return;
	size_t payload = packet[2];
	if (payload > size - 3)
		payload = size - 3;
	const uint8* data = packet + 3;

	if (fAirMode == R5_BTVOICE_AIR_TRANSPARENT) {
		FrameSink sink = { this };
		fDeframer.Feed(data, payload, sink);
	} else {
		// Voice_Setting 0x0060: 16-bit little-endian PCM at 8 kHz. Each
		// sample becomes two at 16 kHz, the first halfway from the last.
		float samples[256];
		size_t count = 0;
		for (size_t i = 0; i + 1 < payload; i += 2) {
			float value = (int16)(data[i] | (data[i + 1] << 8)) / 32768.0f;
			samples[count++] = (fLastPCM + value) * 0.5f;
			samples[count++] = value;
			fLastPCM = value;
		}
		_Push(samples, count);
	}

	_SendBack(payload);
}


void
VoiceSource::_SendBack(size_t payload)
{
	if (payload == 0 || payload > 255)
		return;

	uint8 packet[3 + 255];
	packet[0] = (uint8)fHandle;
	packet[1] = (uint8)((fHandle >> 8) & 0x0f);
	packet[2] = (uint8)payload;

	if (fAirMode == R5_BTVOICE_AIR_TRANSPARENT) {
		while (fOutHave < payload
			&& fOutHave + sbc::kMSBCPacketLength <= sizeof(fOut)) {
			sbc::MSBCPacket(fSilence, fOutSequence++, fOut + fOutHave);
			fOutHave += sbc::kMSBCPacketLength;
		}
		size_t take = payload < fOutHave ? payload : fOutHave;
		memcpy(packet + 3, fOut, take);
		memset(packet + 3 + take, 0, payload - take);
		memmove(fOut, fOut + take, fOutHave - take);
		fOutHave -= take;
	} else
		memset(packet + 3, 0, payload);

	// A full transmit queue (B_WOULD_BLOCK) loses this one packet.
	write(fNode, packet, 3 + payload);
}


void
VoiceSource::_Push(const float* samples, size_t count)
{
	BAutolock _(fLock);
	for (size_t i = 0; i < count; i++) {
		if (fRingCount == (size_t)kRate) {
			fRingStart = (fRingStart + 1) % kRate;
			fRingCount--;
		}
		fRing[(fRingStart + fRingCount) % kRate] = samples[i];
		fRingCount++;
	}
}
