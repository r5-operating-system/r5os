/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * SBC decoder, written for R5 from the A2DP specification (v1.3.2,
 * appendix B, "Technical Specification of SBC") and the mSBC frame of the
 * Hands-Free Profile (HFP 1.8, appendix A). No third-party code. It shares
 * the configuration and the bit allocation with sbc_encoder.h, so encoder
 * and decoder cannot disagree on them.
 *
 * Plain C++ with <stdint.h> only, like the encoder, so the host test and
 * the add-on build the same file. Every field of a frame is checked before
 * it is used: a frame from a radio is never trusted.
 */
#ifndef R5_SBC_DECODER_H
#define R5_SBC_DECODER_H


#include "sbc_encoder.h"


namespace sbc {


// mSBC (HFP 1.8, A.1): sync word 0xAD and two reserved bytes; always
// 16 kHz, mono, 15 blocks, 8 subbands, Loudness, bitpool 26: 57 bytes and
// 120 samples per frame.
static const uint8_t kMSBCSyncWord = 0xad;
static const int kMSBCBlocks = 15;
static const int kMSBCBitpool = 26;
static const size_t kMSBCFrameLength = 57;
static const int kMSBCFrameSamples = 120;

// The configuration of an mSBC frame.
Config		MSBCConfig();


enum DecodeStatus {
	kDecodeOK = 0,
	kDecodeShort,		// fewer bytes than the header or the frame needs
	kDecodeBadSync,
	kDecodeBadHeader,	// a field outside the ranges of 12.5
	kDecodeBadCRC
};


class Decoder {
public:
								Decoder();

			// Forgets the synthesis history (after a gap).
			void				Reset();

			// Decodes one SBC frame (sync 0x9c) or one mSBC frame (sync 0xad)
			// at the start of "data". On kDecodeOK, "samples" holds
			// FrameSamples() interleaved samples per channel at full scale
			// -1.0 .. 1.0 (room for "capacity" floats is checked), and
			// *_length, *_channels and *_frameSamples say what was read.
			DecodeStatus		Decode(const uint8_t* data, size_t size,
									float* samples, size_t capacity,
									size_t* _length, int* _channels,
									int* _frameSamples);

			// The configuration of the last good frame.
			const Config&		LastConfig() const { return fConfig; }

private:
			void				_Synthesize(int channel, const float* subband,
									float* out, int stride);

			Config				fConfig;
			int					fSubbands;
			// V[] of the synthesis filter (12.7, the decoder), 20 *
			// subbands per channel.
			float				fV[kMaxChannels][20 * kMaxSubbands];
			float				fWindow[10 * kMaxSubbands];
			float				fMatrix[2 * kMaxSubbands][kMaxSubbands];
};


}	// namespace sbc


#endif	// R5_SBC_DECODER_H
