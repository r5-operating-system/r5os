/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 */


#include "BluetoothAudioNode.h"

#include <new>
#include <string.h>
#include <syslog.h>

#include <Buffer.h>
#include <MediaAddOn.h>
#include <ParameterWeb.h>
#include <TimeSource.h>


// The rate asked of the mixer. A2DP 4.3.2: every SBC sink supports 44.1 and
// 48 kHz, and 44.1 kHz is the rate most music is made at, so the mixer
// does not resample it.
static const float kRate = 44100.0f;
// 512 frames of float stereo, about 12 ms.
static const size_t kBufferSize = 512 * 2 * sizeof(float);

enum {
	kVolumeParameter = 1
};


BluetoothAudioNode::BluetoothAudioNode(BMediaAddOn* addOn, int32 internalID)
	:
	BMediaNode("Bluetooth audio"),
	BBufferConsumer(B_MEDIA_RAW_AUDIO),
	BMediaEventLooper(),
	BControllable(),
	fAddOn(addOn),
	fInternalID(internalID),
	fConnected(false),
	fSource(NULL),
	fVolume(100.0f),
	fVolumeChanged(0),
	fLatency(0)
{
	memset(&fInput, 0, sizeof(fInput));
}


BluetoothAudioNode::~BluetoothAudioNode()
{
	BMediaEventLooper::Quit();
	if (fSource != NULL) {
		fSource->Stop();
		delete fSource;
	}
}


void
BluetoothAudioNode::PreferredFormat(media_format* format)
{
	format->type = B_MEDIA_RAW_AUDIO;
	format->u.raw_audio = media_raw_audio_format::wildcard;
	format->u.raw_audio.frame_rate = kRate;
	format->u.raw_audio.channel_count = 2;
	format->u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format->u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
}


BMediaAddOn*
BluetoothAudioNode::AddOn(int32* internalID) const
{
	if (internalID != NULL)
		*internalID = fInternalID;
	return fAddOn;
}


status_t
BluetoothAudioNode::HandleMessage(int32 message, const void* data,
	size_t size)
{
	if (BControllable::HandleMessage(message, data, size) == B_OK
		|| BBufferConsumer::HandleMessage(message, data, size) == B_OK
		|| BMediaNode::HandleMessage(message, data, size) == B_OK)
		return B_OK;
	HandleBadMessage(message, data, size);
	return B_ERROR;
}


void
BluetoothAudioNode::NodeRegistered()
{
	fInput.destination.port = ControlPort();
	fInput.destination.id = 0;
	fInput.node = Node();
	fInput.source = media_source::null;
	PreferredFormat(&fInput.format);
	strlcpy(fInput.name, "Bluetooth audio", sizeof(fInput.name));

	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web != NULL) {
		BParameterGroup* group = web->MakeGroup("Bluetooth audio");
		group->MakeContinuousParameter(kVolumeParameter, B_MEDIA_RAW_AUDIO,
			"Volume", B_GAIN, "%", 0.0f, 100.0f, 1.0f);
		SetParameterWeb(web);
	}

	fSource = new(std::nothrow) A2DPSource(this, (int)kRate);
	if (fSource == NULL || fSource->Start() != B_OK) {
		syslog(LOG_WARNING, "bluetooth_audio: no Bluetooth, output is "
			"silent");
	}
	_UpdateLatency();

	SetPriority(B_URGENT_PRIORITY);
	Run();
}


void
BluetoothAudioNode::HandleEvent(const media_timed_event* event,
	bigtime_t lateness, bool realTimeEvent)
{
	switch (event->type) {
		case BTimedEventQueue::B_START:
			if (fSource != NULL)
				fSource->SetPlaying(true);
			break;
		case BTimedEventQueue::B_STOP:
			EventQueue()->FlushEvents(0, BTimedEventQueue::B_ALWAYS, true,
				BTimedEventQueue::B_HANDLE_BUFFER);
			if (fSource != NULL)
				fSource->SetPlaying(false);
			break;
		default:
			break;
	}
}


// #pragma mark - BBufferConsumer


status_t
BluetoothAudioNode::AcceptFormat(const media_destination& dest,
	media_format* format)
{
	if (dest != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;
	if (format->type == B_MEDIA_UNKNOWN_TYPE)
		format->type = B_MEDIA_RAW_AUDIO;
	if (format->type != B_MEDIA_RAW_AUDIO)
		return B_MEDIA_BAD_FORMAT;

	media_raw_audio_format& raw = format->u.raw_audio;
	const media_raw_audio_format& any = media_raw_audio_format::wildcard;
	if (raw.frame_rate == any.frame_rate)
		raw.frame_rate = kRate;
	if (raw.channel_count == any.channel_count)
		raw.channel_count = 2;
	if (raw.format == any.format)
		raw.format = media_raw_audio_format::B_AUDIO_FLOAT;
	if (raw.byte_order == any.byte_order)
		raw.byte_order = B_MEDIA_HOST_ENDIAN;
	if (raw.buffer_size == any.buffer_size)
		raw.buffer_size = kBufferSize;

	if (raw.frame_rate != kRate || raw.channel_count != 2
		|| raw.format != media_raw_audio_format::B_AUDIO_FLOAT
		|| raw.byte_order != B_MEDIA_HOST_ENDIAN) {
		// Ask for exactly this instead; the mixer converts.
		PreferredFormat(format);
		format->u.raw_audio.buffer_size = kBufferSize;
		return B_MEDIA_BAD_FORMAT;
	}
	return B_OK;
}


status_t
BluetoothAudioNode::GetNextInput(int32* cookie, media_input* input)
{
	if (*cookie != 0)
		return B_BAD_INDEX;
	*input = fInput;
	*cookie = 1;
	return B_OK;
}


void
BluetoothAudioNode::DisposeInputCookie(int32 cookie)
{
}


void
BluetoothAudioNode::BufferReceived(BBuffer* buffer)
{
	// Straight into the ring: A2DPSource sends it at the audio clock's pace,
	// so the node does not wait for the performance time here.
	if (fConnected && fSource != NULL
		&& buffer->Header()->destination == (uint32)fInput.destination.id) {
		size_t frames = buffer->SizeUsed() / (2 * sizeof(float));
		fSource->Write((const float*)buffer->Data(), frames);
	}
	buffer->Recycle();
}


void
BluetoothAudioNode::ProducerDataStatus(const media_destination& forWhom,
	int32 status, bigtime_t atPerformanceTime)
{
}


status_t
BluetoothAudioNode::GetLatencyFor(const media_destination& forWhom,
	bigtime_t* latency, media_node_id* timeSource)
{
	if (forWhom != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;
	*latency = EventLatency() + fLatency;
	*timeSource = TimeSource()->ID();
	return B_OK;
}


status_t
BluetoothAudioNode::Connected(const media_source& producer,
	const media_destination& where, const media_format& format,
	media_input* input)
{
	if (where != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;
	fInput.source = producer;
	fInput.format = format;
	fConnected = true;
	*input = fInput;
	return B_OK;
}


void
BluetoothAudioNode::Disconnected(const media_source& producer,
	const media_destination& where)
{
	if (where != fInput.destination || producer != fInput.source)
		return;
	fInput.source = media_source::null;
	PreferredFormat(&fInput.format);
	fConnected = false;
}


status_t
BluetoothAudioNode::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 changeTag,
	const media_format& format)
{
	// The format is fixed at the connection.
	return B_MEDIA_BAD_FORMAT;
}


void
BluetoothAudioNode::_UpdateLatency()
{
	bigtime_t latency = fSource != NULL ? fSource->BufferLatency() : 0;
	if (latency == fLatency)
		return;
	fLatency = latency;
	if (fConnected) {
		SendLatencyChange(fInput.source, fInput.destination,
			EventLatency() + fLatency);
	}
}


// #pragma mark - BControllable


status_t
BluetoothAudioNode::GetParameterValue(int32 id, bigtime_t* lastChange,
	void* value, size_t* size)
{
	if (id != kVolumeParameter)
		return B_BAD_VALUE;
	if (*size < sizeof(float))
		return B_NO_MEMORY;
	*(float*)value = fVolume;
	*size = sizeof(float);
	*lastChange = fVolumeChanged;
	return B_OK;
}


void
BluetoothAudioNode::SetParameterValue(int32 id, bigtime_t when,
	const void* value, size_t size)
{
	if (id != kVolumeParameter || size < sizeof(float) || value == NULL)
		return;
	float volume = *(const float*)value;
	if (!(volume >= 0.0f))
		volume = 0.0f;
	if (volume > 100.0f)
		volume = 100.0f;
	fVolume = volume;
	fVolumeChanged = when;
	if (fSource != NULL)
		fSource->SetVolume(volume / 100.0f);
	BroadcastNewParameterValue(when, kVolumeParameter, &fVolume,
		sizeof(fVolume));
}


// #pragma mark - A2DPSource::Listener


void
BluetoothAudioNode::RemoteVolumeChanged(float gain)
{
	fVolume = gain * 100.0f;
	fVolumeChanged = TimeSource()->Now();
	BroadcastNewParameterValue(fVolumeChanged, kVolumeParameter, &fVolume,
		sizeof(fVolume));
}


void
BluetoothAudioNode::RemoteDelayChanged(bigtime_t delay)
{
	_UpdateLatency();
}
