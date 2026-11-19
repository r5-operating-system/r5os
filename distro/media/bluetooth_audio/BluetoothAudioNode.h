/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The media node of bluetooth_audio: an audio output (buffer consumer and
 * physical output) that takes float stereo from the system mixer and hands
 * it to A2DPSource, with a volume control that is the headset's own
 * volume when it supports AVRCP absolute volume.
 */
#ifndef R5_BLUETOOTH_AUDIO_NODE_H
#define R5_BLUETOOTH_AUDIO_NODE_H


#include <atomic>

#include <BufferConsumer.h>
#include <Controllable.h>
#include <MediaEventLooper.h>

#include "A2DPSource.h"


class BluetoothAudioNode : public BBufferConsumer, public BMediaEventLooper,
	public BControllable, public A2DPSource::Listener {
public:
								BluetoothAudioNode(BMediaAddOn* addOn,
									int32 internalID);
	virtual						~BluetoothAudioNode();

	// BMediaNode
	virtual	BMediaAddOn*		AddOn(int32* internalID) const;
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	// BMediaEventLooper
	virtual	void				NodeRegistered();
	virtual	void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness, bool realTimeEvent);

	// BBufferConsumer
	virtual	status_t			AcceptFormat(const media_destination& dest,
									media_format* format);
	virtual	status_t			GetNextInput(int32* cookie,
									media_input* input);
	virtual	void				DisposeInputCookie(int32 cookie);
	virtual	void				BufferReceived(BBuffer* buffer);
	virtual	void				ProducerDataStatus(
									const media_destination& forWhom,
									int32 status, bigtime_t atPerformanceTime);
	virtual	status_t			GetLatencyFor(const media_destination& forWhom,
									bigtime_t* latency,
									media_node_id* timeSource);
	virtual	status_t			Connected(const media_source& producer,
									const media_destination& where,
									const media_format& format,
									media_input* input);
	virtual	void				Disconnected(const media_source& producer,
									const media_destination& where);
	virtual	status_t			FormatChanged(const media_source& producer,
									const media_destination& consumer,
									int32 changeTag,
									const media_format& format);

	// BControllable
	virtual	status_t			GetParameterValue(int32 id,
									bigtime_t* lastChange, void* value,
									size_t* size);
	virtual	void				SetParameterValue(int32 id, bigtime_t when,
									const void* value, size_t size);

	// A2DPSource::Listener
	virtual	void				RemoteVolumeChanged(float gain);
	virtual	void				RemoteDelayChanged(bigtime_t delay);
	virtual	void				ConnectionStateChanged(bool connected);

	static	void				PreferredFormat(media_format* format);

private:
			void				_UpdateLatency();

			BMediaAddOn*		fAddOn;
			int32				fInternalID;
			media_input			fInput;
			bool				fConnected;
			A2DPSource*			fSource;
			// Also written by A2DPSource's threads (RemoteVolumeChanged(),
			// RemoteDelayChanged()).
			std::atomic<float>	fVolume;		// percent
			std::atomic<bigtime_t> fVolumeChanged;
			std::atomic<bigtime_t> fLatency;
};


#endif	// R5_BLUETOOTH_AUDIO_NODE_H
