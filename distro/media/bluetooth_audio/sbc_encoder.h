/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * SBC encoder, written for R5 from the A2DP specification (v1.3.2,
 * appendix B, "Technical Specification of SBC"). No third-party code.
 * SBC is the mandatory A2DP codec; the Bluetooth SIG lists it as
 * royalty-free for A2DP.
 *
 * Plain C++ with <stdint.h> only, so the host test (tests/sbc_test.cpp)
 * and the add-on build the same file. The analysis works in floating point
 * on float input, so audio from the mixer is never rounded to 16 bits
 * before SBC quantizes it.
 */
#ifndef R5_SBC_ENCODER_H
#define R5_SBC_ENCODER_H


#include <stddef.h>
#include <stdint.h>


namespace sbc {


// Header field values (appendix B, 12.5).
enum {
	kFrequency16000 = 0,
	kFrequency32000 = 1,
	kFrequency44100 = 2,
	kFrequency48000 = 3
};

enum {
	kModeMono = 0,
	kModeDualChannel = 1,
	kModeStereo = 2,
	kModeJointStereo = 3
};

enum {
	kAllocationLoudness = 0,
	kAllocationSNR = 1
};

static const uint8_t kSyncWord = 0x9c;
static const int kMaxSubbands = 8;
static const int kMaxBlocks = 16;
static const int kMaxChannels = 2;


struct Config {
	int		frequency;		// kFrequency*
	int		blocks;			// 4, 8, 12 or 16
	int		mode;			// kMode*
	int		allocation;		// kAllocation*
	int		subbands;		// 4 or 8
	int		bitpool;		// 2 .. MaxBitpool()
};


// Checks the fields against the ranges of 12.5 and 4.3.2 of A2DP.
bool		ValidConfig(const Config& config);
int			Channels(const Config& config);
int			SampleRate(const Config& config);
// Largest bitpool for this mode and subband count (12.5.5: 16 * subbands
// for mono and dual channel, 32 * subbands for the stereo modes, never more
// than 250).
int			MaxBitpool(const Config& config);
// Frame length in bytes (12.9).
size_t		FrameLength(const Config& config);
// Bit rate in bits per second (12.9).
uint32_t	BitRate(const Config& config);
// CRC-8 of 12.6.4: generator x^8 + x^4 + x^3 + x^2 + 1, start 0x0f, over
// the first "bits" bits of data, most significant bit first.
uint8_t		CRC8(const uint8_t* data, size_t bits);
// The bit allocation of 12.6.3 for one frame (exposed for the test).
void		AllocateBits(const Config& config,
				const uint8_t scaleFactors[kMaxChannels][kMaxSubbands],
				uint8_t bits[kMaxChannels][kMaxSubbands]);


class Encoder {
public:
								Encoder();

			bool				Init(const Config& config);
			const Config&		GetConfig() const { return fConfig; }

			// Samples per channel in one frame (blocks * subbands).
			int					FrameSamples() const;
			size_t				FrameLength() const;

			// Encodes one frame from FrameSamples() interleaved samples per
			// channel, full scale at -1.0 .. 1.0. Returns the bytes written,
			// or 0 if "size" is smaller than FrameLength().
			size_t				Encode(const float* samples, uint8_t* out,
									size_t size);

			// Encodes one frame of silence (the analysis state goes on, so
			// the next real frame starts without a click).
			size_t				EncodeSilence(uint8_t* out, size_t size);

private:
			Config				fConfig;
			int					fChannels;
			// Analysis history X[] of 12.6.1, 10 * subbands per channel.
			float				fHistory[kMaxChannels][10 * kMaxSubbands];
			// Window C[] (prototype with the folding sign) and cosine
			// matrix M[][] of the analysis.
			float				fWindow[10 * kMaxSubbands];
			float				fMatrix[kMaxSubbands][2 * kMaxSubbands];
};


}	// namespace sbc


#endif	// R5_SBC_ENCODER_H
