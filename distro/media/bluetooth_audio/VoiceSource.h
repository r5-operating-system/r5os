/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The headset microphone (docs/BLUETOOTH.md, step 7c): reads the SCO
 * packets of a voice link from h2generic's /dev/bluetooth/voice/<n>
 * (<R5BluetoothVoice.h>), decodes mSBC (a transparent link) or takes the
 * 16-bit PCM the radio made from CVSD, and keeps 16 kHz mono float samples
 * for the media node. For each packet that comes in, one packet of the
 * same length goes out (encoded silence or zero PCM), so the headset's
 * speaker side of the link is never starved.
 *
 * Without bt_voice in the h2generic driver settings there is no node, and
 * Read() gives silence.
 */
#ifndef R5_VOICE_SOURCE_H
#define R5_VOICE_SOURCE_H


#include <Locker.h>
#include <OS.h>

#include "msbc.h"
#include "sbc_decoder.h"
#include "sbc_encoder.h"


class VoiceSource {
public:
	static	const int			kRate = 16000;

								VoiceSource();
								~VoiceSource();

			status_t			Start();
			void				Stop();

			// Fills "frames" samples; silence where the headset sent none.
			void				Read(float* samples, size_t frames);

			bool				LinkRunning() const { return fRunning; }

private:
	static	status_t			_ThreadEntry(void* data);
			void				_Thread();
			bool				_Open();
			void				_Close();
			void				_Packet(const uint8* packet, size_t size);
			void				_SendBack(size_t payload);
			void				_Push(const float* samples, size_t count);

			struct FrameSink;

			thread_id			fThread;
	volatile bool				fQuit;
			int					fNode;
	volatile bool				fRunning;
			uint16				fHandle;
			uint8				fAirMode;

			sbc::MSBCDeframer	fDeframer;
			sbc::Decoder		fDecoder;
			sbc::Encoder		fEncoder;
			uint8				fSilence[sbc::kMSBCFrameLength];
			// Room for 255 bytes of payload in whole packets.
			uint8				fOut[5 * sbc::kMSBCPacketLength];
			size_t				fOutHave;
			int					fOutSequence;
			float				fLastPCM;
			bool				fPrimed;

			BLocker				fLock;
			// One second of samples.
			float				fRing[kRate];
			size_t				fRingStart;
			size_t				fRingCount;
};


#endif	// R5_VOICE_SOURCE_H
