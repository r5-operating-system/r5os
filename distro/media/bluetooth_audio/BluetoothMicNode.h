/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * "Bluetooth headset microphone" (docs/BLUETOOTH.md, step 7c): an audio
 * input (buffer producer and physical input) that sends 16 kHz mono float
 * from VoiceSource, at the time source's pace. Without a voice link it
 * sends silence, so a recording program never waits for buffers.
 */
#ifndef R5_BLUETOOTH_MIC_NODE_H
#define R5_BLUETOOTH_MIC_NODE_H


#include <BufferProducer.h>
#include <MediaEventLooper.h>

#include "VoiceSource.h"


class BBufferGroup;


class BluetoothMicNode : public BBufferProducer, public BMediaEventLooper {
public:
								BluetoothMicNode(BMediaAddOn* addOn,
									int32 internalID);
	virtual						~BluetoothMicNode();

	// BMediaNode
	virtual	BMediaAddOn*		AddOn(int32* internalID) const;
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	// BMediaEventLooper
	virtual	void				NodeRegistered();
	virtual	void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness, bool realTimeEvent);

	// BBufferProducer
	virtual	status_t			FormatSuggestionRequested(media_type type,
									int32 quality, media_format* format);
	virtual	status_t			FormatProposal(const media_source& output,
									media_format* format);
	virtual	status_t			FormatChangeRequested(
									const media_source& source,
									const media_destination& destination,
									media_format* format,
									int32* _deprecated);
	virtual	status_t			GetNextOutput(int32* cookie,
									media_output* output);
	virtual	status_t			DisposeOutputCookie(int32 cookie);
	virtual	status_t			SetBufferGroup(const media_source& forSource,
									BBufferGroup* group);
	virtual	status_t			PrepareToConnect(const media_source& what,
									const media_destination& where,
									media_format* format,
									media_source* _source, char* _name);
	virtual	void				Connect(status_t error,
									const media_source& source,
									const media_destination& destination,
									const media_format& format,
									char* ioName);
	virtual	void				Disconnect(const media_source& what,
									const media_destination& where);
	virtual	void				LateNoticeReceived(const media_source& what,
									bigtime_t howMuch,
									bigtime_t performanceTime);
	virtual	void				EnableOutput(const media_source& what,
									bool enabled, int32* _deprecated);
	virtual	void				LatencyChanged(const media_source& source,
									const media_destination& destination,
									bigtime_t newLatency, uint32 flags);

	static	void				PreferredFormat(media_format* format);

private:
			bool				_Compatible(const media_format& format) const;
			void				_SendBuffer(bigtime_t performanceTime);

			BMediaAddOn*		fAddOn;
			int32				fInternalID;
			media_output		fOutput;
			bool				fConnected;
			bool				fEnabled;
			BBufferGroup*		fBufferGroup;
			bigtime_t			fLatency;
			bigtime_t			fStartTime;
			uint64				fFramesSent;
			VoiceSource*		fSource;
};


#endif	// R5_BLUETOOTH_MIC_NODE_H
