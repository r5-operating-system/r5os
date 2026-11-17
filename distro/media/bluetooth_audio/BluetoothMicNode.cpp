/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 */


#include "BluetoothMicNode.h"

#include <new>
#include <string.h>
#include <syslog.h>

#include <Buffer.h>
#include <BufferGroup.h>
#include <MediaAddOn.h>
#include <TimeSource.h>
#include <Messenger.h>
#include <OS.h>

#include <R5Admin.h>
#include <bluetoothserver_p.h>


// 240 frames of 16 kHz mono float: 15 ms, two mSBC frames.
static const size_t kFramesPerBuffer = 240;
static const size_t kBufferSize = kFramesPerBuffer * sizeof(float);


// Tells bluetooth_server that a program records from the microphone (on)
// or stopped (off), so the HFP session opens or closes the voice link
// (BT_MSG_R5_AUDIO_VOICE). On a thread of its own: the node never waits.
static status_t
tell_server_voice(void* data)
{
	BMessenger server(BLUETOOTH_SIGNATURE);
	BMessage message(BT_MSG_R5_AUDIO_VOICE);
	message.AddBool("on", data != NULL);
	BMessage reply;
	r5_admin_send(server, &message, &reply, 2000000);
	return B_OK;
}


static void
voice_wanted(bool on)
{
	thread_id thread = spawn_thread(tell_server_voice, "bt voice wanted",
		B_NORMAL_PRIORITY, on ? (void*)1 : NULL);
	if (thread >= 0)
		resume_thread(thread);
}
static const bigtime_t kBufferDuration
	= (bigtime_t)kFramesPerBuffer * 1000000 / VoiceSource::kRate;
static const char* kName = "Bluetooth headset microphone";


BluetoothMicNode::BluetoothMicNode(BMediaAddOn* addOn, int32 internalID)
	:
	BMediaNode(kName),
	BBufferProducer(B_MEDIA_RAW_AUDIO),
	BMediaEventLooper(),
	fAddOn(addOn),
	fInternalID(internalID),
	fConnected(false),
	fEnabled(true),
	fBufferGroup(NULL),
	fLatency(0),
	fStartTime(0),
	fFramesSent(0),
	fSource(NULL)
{
	// As for the output node: the flavor's kinds are not the live node's.
	AddNodeKind(B_PHYSICAL_INPUT);
}


BluetoothMicNode::~BluetoothMicNode()
{
	BMediaEventLooper::Quit();
	if (fConnected)
		voice_wanted(false);
	if (fSource != NULL) {
		fSource->Stop();
		delete fSource;
	}
	delete fBufferGroup;
}


void
BluetoothMicNode::PreferredFormat(media_format* format)
{
	format->type = B_MEDIA_RAW_AUDIO;
	format->u.raw_audio = media_raw_audio_format::wildcard;
	format->u.raw_audio.frame_rate = VoiceSource::kRate;
	format->u.raw_audio.channel_count = 1;
	format->u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format->u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
	format->u.raw_audio.buffer_size = kBufferSize;
}


BMediaAddOn*
BluetoothMicNode::AddOn(int32* internalID) const
{
	if (internalID != NULL)
		*internalID = fInternalID;
	return fAddOn;
}


status_t
BluetoothMicNode::HandleMessage(int32 message, const void* data, size_t size)
{
	if (BBufferProducer::HandleMessage(message, data, size) == B_OK
		|| BMediaNode::HandleMessage(message, data, size) == B_OK)
		return B_OK;
	HandleBadMessage(message, data, size);
	return B_ERROR;
}


void
BluetoothMicNode::NodeRegistered()
{
	fOutput.node = Node();
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.destination = media_destination::null;
	PreferredFormat(&fOutput.format);
	strlcpy(fOutput.name, kName, sizeof(fOutput.name));

	fSource = new(std::nothrow) VoiceSource();
	if (fSource == NULL || fSource->Start() != B_OK)
		syslog(LOG_WARNING, "bluetooth_audio: the headset microphone is "
			"silent");

	SetPriority(B_URGENT_PRIORITY);
	Run();
}


void
BluetoothMicNode::HandleEvent(const media_timed_event* event,
	bigtime_t lateness, bool realTimeEvent)
{
	switch (event->type) {
		case BTimedEventQueue::B_START:
		{
			if (RunState() != B_STARTED)
				break;
			fStartTime = event->event_time;
			fFramesSent = 0;
			media_timed_event first(fStartTime,
				BTimedEventQueue::B_HANDLE_BUFFER);
			EventQueue()->AddEvent(first);
			break;
		}
		case BTimedEventQueue::B_STOP:
			EventQueue()->FlushEvents(0, BTimedEventQueue::B_ALWAYS, true,
				BTimedEventQueue::B_HANDLE_BUFFER);
			break;
		case BTimedEventQueue::B_HANDLE_BUFFER:
		{
			if (RunState() != B_STARTED)
				break;
			_SendBuffer(event->event_time);
			fFramesSent += kFramesPerBuffer;
			bigtime_t next = fStartTime
				+ (bigtime_t)(fFramesSent * 1000000 / VoiceSource::kRate);
			media_timed_event nextEvent(next,
				BTimedEventQueue::B_HANDLE_BUFFER);
			EventQueue()->AddEvent(nextEvent);
			break;
		}
		default:
			break;
	}
}


void
BluetoothMicNode::_SendBuffer(bigtime_t performanceTime)
{
	if (!fConnected || !fEnabled || fBufferGroup == NULL) {
		// Nobody listens: the samples still go, so the queue stays short.
		float discard[kFramesPerBuffer];
		if (fSource != NULL)
			fSource->Read(discard, kFramesPerBuffer);
		return;
	}

	BBuffer* buffer = fBufferGroup->RequestBuffer(kBufferSize,
		kBufferDuration);
	if (buffer == NULL)
		return;

	float* samples = (float*)buffer->Data();
	if (fSource != NULL)
		fSource->Read(samples, kFramesPerBuffer);
	else
		memset(samples, 0, kBufferSize);

	media_header* header = buffer->Header();
	header->type = B_MEDIA_RAW_AUDIO;
	header->size_used = kBufferSize;
	header->time_source = TimeSource()->ID();
	header->start_time = performanceTime;

	if (SendBuffer(buffer, fOutput.source, fOutput.destination) != B_OK)
		buffer->Recycle();
}


bool
BluetoothMicNode::_Compatible(const media_format& format) const
{
	if (format.type != B_MEDIA_RAW_AUDIO
		&& format.type != B_MEDIA_UNKNOWN_TYPE)
		return false;
	if (format.type == B_MEDIA_UNKNOWN_TYPE)
		return true;
	const media_raw_audio_format& raw = format.u.raw_audio;
	const media_raw_audio_format& any = media_raw_audio_format::wildcard;
	return (raw.frame_rate == any.frame_rate
			|| raw.frame_rate == VoiceSource::kRate)
		&& (raw.channel_count == any.channel_count || raw.channel_count == 1)
		&& (raw.format == any.format
			|| raw.format == media_raw_audio_format::B_AUDIO_FLOAT)
		&& (raw.byte_order == any.byte_order
			|| raw.byte_order == B_MEDIA_HOST_ENDIAN)
		&& (raw.buffer_size == any.buffer_size
			|| raw.buffer_size == kBufferSize);
}


status_t
BluetoothMicNode::FormatSuggestionRequested(media_type type, int32 quality,
	media_format* format)
{
	if (type != B_MEDIA_RAW_AUDIO && type != B_MEDIA_UNKNOWN_TYPE)
		return B_MEDIA_BAD_FORMAT;
	PreferredFormat(format);
	return B_OK;
}


status_t
BluetoothMicNode::FormatProposal(const media_source& output,
	media_format* format)
{
	if (output != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	bool compatible = _Compatible(*format);
	PreferredFormat(format);
	return compatible ? B_OK : B_MEDIA_BAD_FORMAT;
}


status_t
BluetoothMicNode::FormatChangeRequested(const media_source& source,
	const media_destination& destination, media_format* format,
	int32* _deprecated)
{
	return B_ERROR;
}


status_t
BluetoothMicNode::GetNextOutput(int32* cookie, media_output* output)
{
	if (*cookie != 0)
		return B_BAD_INDEX;
	*output = fOutput;
	*cookie = 1;
	return B_OK;
}


status_t
BluetoothMicNode::DisposeOutputCookie(int32 cookie)
{
	return B_OK;
}


status_t
BluetoothMicNode::SetBufferGroup(const media_source& forSource,
	BBufferGroup* group)
{
	if (forSource != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	if (group == fBufferGroup)
		return B_OK;
	delete fBufferGroup;
	fBufferGroup = group;
	if (fBufferGroup == NULL && fConnected) {
		fBufferGroup = new(std::nothrow) BBufferGroup(kBufferSize, 8);
		if (fBufferGroup != NULL && fBufferGroup->InitCheck() != B_OK) {
			delete fBufferGroup;
			fBufferGroup = NULL;
		}
	}
	return fBufferGroup != NULL || !fConnected ? B_OK : B_NO_MEMORY;
}


status_t
BluetoothMicNode::PrepareToConnect(const media_source& what,
	const media_destination& where, media_format* format,
	media_source* _source, char* _name)
{
	if (what != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	if (fOutput.destination != media_destination::null)
		return B_MEDIA_ALREADY_CONNECTED;
	if (!_Compatible(*format))
		return B_MEDIA_BAD_FORMAT;
	PreferredFormat(format);

	fOutput.destination = where;
	fOutput.format = *format;
	*_source = fOutput.source;
	strlcpy(_name, fOutput.name, B_MEDIA_NAME_LENGTH);
	return B_OK;
}


void
BluetoothMicNode::Connect(status_t error, const media_source& source,
	const media_destination& destination, const media_format& format,
	char* ioName)
{
	if (source != fOutput.source)
		return;
	if (error != B_OK) {
		fOutput.destination = media_destination::null;
		PreferredFormat(&fOutput.format);
		return;
	}

	fOutput.destination = destination;
	fOutput.format = format;
	strlcpy(ioName, fOutput.name, B_MEDIA_NAME_LENGTH);

	media_node_id timeSource;
	if (FindLatencyFor(fOutput.destination, &fLatency, &timeSource) != B_OK)
		fLatency = 0;
	SetEventLatency(fLatency + kBufferDuration);

	if (fBufferGroup == NULL) {
		fBufferGroup = new(std::nothrow) BBufferGroup(kBufferSize, 8);
		if (fBufferGroup != NULL && fBufferGroup->InitCheck() != B_OK) {
			delete fBufferGroup;
			fBufferGroup = NULL;
		}
	}
	fConnected = fBufferGroup != NULL;
	if (fConnected)
		voice_wanted(true);
}


void
BluetoothMicNode::Disconnect(const media_source& what,
	const media_destination& where)
{
	if (what != fOutput.source || where != fOutput.destination)
		return;
	fOutput.destination = media_destination::null;
	PreferredFormat(&fOutput.format);
	if (fConnected)
		voice_wanted(false);
	fConnected = false;
	delete fBufferGroup;
	fBufferGroup = NULL;
}


void
BluetoothMicNode::LateNoticeReceived(const media_source& what,
	bigtime_t howMuch, bigtime_t performanceTime)
{
	// The microphone cannot be early; buffers that were late stay late.
}


void
BluetoothMicNode::EnableOutput(const media_source& what, bool enabled,
	int32* _deprecated)
{
	if (what == fOutput.source)
		fEnabled = enabled;
}


void
BluetoothMicNode::LatencyChanged(const media_source& source,
	const media_destination& destination, bigtime_t newLatency,
	uint32 flags)
{
	if (source != fOutput.source || destination != fOutput.destination)
		return;
	fLatency = newLatency;
	SetEventLatency(fLatency + kBufferDuration);
}
