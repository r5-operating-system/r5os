/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * A2DP source and AVRCP for one headset at a time: AVDTP 1.3 signalling
 * and media on L2CAP PSM 0x19 (as initiator and as acceptor), AVRCP play,
 * pause and absolute volume on PSM 0x17, and a real-time thread that
 * encodes SBC and sends it at the pace of the audio clock.
 *
 * Who may connect: the headset chosen in the person's settings
 * (AudioSettings.h), or, without a choice, a device bluetooth_server says is
 * paired. Log lines carry no addresses or keys.
 */
#ifndef R5_A2DP_SOURCE_H
#define R5_A2DP_SOURCE_H


#include <atomic>

#include <Locker.h>
#include <OS.h>

#include <bluetooth/bluetooth.h>

#include "sbc_encoder.h"


class A2DPSource {
public:
	class Listener {
	public:
		virtual				~Listener() {}
		// The headset changed its volume (AVRCP absolute volume).
		virtual	void		RemoteVolumeChanged(float gain) = 0;
		// The headset reported its own delay (AVDTP delay reporting).
		virtual	void		RemoteDelayChanged(bigtime_t delay) = 0;
		// A2DP transport is open (true) or closed/disconnected (false).
		virtual	void		ConnectionStateChanged(bool connected) {}
	};

								A2DPSource(Listener* listener, int rate);
								~A2DPSource();

			status_t			Start();
			// Closes the stream cleanly (AVDTP Close) and stops the threads.
			void				Stop();

			// The media node runs (true) or stopped (false): the stream is
			// started or suspended.
			void				SetPlaying(bool playing);
			// 0.0 .. 1.0. Sent to the headset as absolute volume when it
			// supports that, applied to the samples otherwise.
			void				SetVolume(float gain);

			// Interleaved stereo float samples at the rate given above.
			// Called from the node's thread; never blocks.
			void				Write(const float* samples, size_t frames);

			int					Rate() const { return fRate; }
			// The audio buffered in R5 before it is encoded.
			bigtime_t			BufferLatency() const;

private:
			enum State {
				kIdle,
				kDiscovering,
				kGettingCapabilities,
				kConfiguring,
				kConfigured,
				kOpening,
				kOpen,
				kStarting,
				kStreaming,
				kSuspending,
				kClosing
			};

	static	int32				_ListenEntry(void* data);
	static	int32				_ManagerEntry(void* data);
	static	int32				_StreamEntry(void* data);
	static	int32				_ControlEntry(void* data);
	static	int32				_TellServerEntry(void* data);

			void				_Listen(int index);
			void				_Manage();
			void				_Stream();
			void				_Control();

			bool				_Allowed(const bdaddr_t& address);
			bool				_ChosenDevice(bdaddr_t* device);
			bool				_PairedHeadset(bdaddr_t* device);
			bool				_LinkState(const bdaddr_t& address,
									bool* paired, bool* connected,
									bool* encrypted);
			int					_Connect(const bdaddr_t& address, uint16 psm);

			// Manager thread, fLock held.
			void				_HandleSignal(const uint8* data, size_t size);
			void				_HandleCommand(uint8 label, uint8 signal,
									const uint8* payload, size_t size);
			void				_HandleResponse(uint8 messageType,
									uint8 signal, const uint8* payload,
									size_t size);
			status_t			_SendCommand(uint8 signal,
									const uint8* payload, size_t size);
			void				_SendReply(uint8 label, uint8 messageType,
									uint8 signal, const uint8* payload,
									size_t size);
			bool				_SendSetConfiguration();
			void				_AskCapabilities();
			void				_StartStreaming();
			void				_StopStreaming();
			void				_CloseTransport();
			void				_EndSession(bool clean, const char* reason);
			void				_TellServer(bool streaming);
			void				_UpdateReady(bool ready);

			// Control thread.
			void				_HandleControl(const uint8* data, size_t size);
			void				_SendControl(const uint8* data, size_t size);
			void				_SendAbsoluteVolume(uint8 volume);

			size_t				_ReadRing(float* out, size_t frames);
			size_t				_RingFill() const;

			Listener*			fListener;
			const int			fRate;

			BLocker				fLock;
			std::atomic<bool>	fQuitting;
			std::atomic<bool>	fPlaying;
			std::atomic<bool>	fPaused;	// AVRCP pause from the headset

			int					fListenSockets[2];	// AVDTP, AVCTP
			thread_id			fListenThreads[2];
			thread_id			fManagerThread;
			thread_id			fStreamThread;
			thread_id			fControlThread;
			std::atomic<bool>	fControlStop;	// _EndSession() ends it
			std::atomic<bool>	fControlDone;	// it ended by itself
			sem_id				fWake;

			// The session, under fLock.
			bdaddr_t			fRemote;
			bool				fHasRemote;
			int					fSignal;
			int					fTransport;
			int					fControl;
			int					fPendingSignal;
			int					fPendingTransport;
			int					fPendingControl;
			bdaddr_t			fPendingAddress;
			State				fState;
			bool				fInitiator;
			uint8				fRemoteSEID;
			// The free audio sinks of the headset's Discover answer, in
			// order; R5 asks each for SBC until one has it.
			uint8				fCandidates[16];
			int					fCandidateCount;
			int					fCandidateIndex;
			uint8				fLabel;
			uint8				fPendingLabel;
			uint8				fPendingCommand;
			bigtime_t			fPendingSince;
			bigtime_t			fSessionStart;
			bigtime_t			fConfiguredAt;
			bigtime_t			fLastAttempt;
			bool				fTriedControl;
			bool				fTriedAllCapabilities;
			bool				fRetriedBitpool;
			bool				fDelayReporting;
			uint8				fSinkCapabilities[64];
			size_t				fSinkCapabilitiesLength;
			sbc::Config			fConfig;
			std::atomic<bool>	fStreaming;
			bool				fLoggedStart;
			bool				fReportedReady;

			// AVRCP, under fControlLock.
			BLocker				fControlLock;
			uint8				fControlLabel;
			std::atomic<bool>	fAbsoluteVolume;
			std::atomic<int>	fVolume;	// 0 .. 127, last sent or heard
			std::atomic<float>	fSoftwareGain;
			std::atomic<bigtime_t>	fRemoteDelay;
			std::atomic<bigtime_t>	fLastSound;

			// Single producer (the node), single consumer (_Stream()).
			float*				fRing;
			size_t				fRingFrames;
			std::atomic<size_t>	fRingRead;
			std::atomic<size_t>	fRingWrite;
};


#endif	// R5_A2DP_SOURCE_H
