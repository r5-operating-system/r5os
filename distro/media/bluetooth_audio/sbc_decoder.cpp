/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * SBC decoder from the A2DP specification, appendix B, and the mSBC frame
 * of HFP 1.8, appendix A. Section numbers in the comments are those of the
 * A2DP appendix.
 */


#include "sbc_decoder.h"

#include <math.h>
#include <string.h>


namespace sbc {


namespace {

struct BitReader {
	const uint8_t*	data;
	size_t			bit;
	size_t			end;		// in bits

	BitReader(const uint8_t* _data, size_t startByte, size_t endByte)
		:
		data(_data),
		bit(startByte * 8),
		end(endByte * 8)
	{
	}

	// The frame length was checked first, so a read past the end means the
	// header and the data disagree; it gives zeros and sets Overrun().
	uint32_t Get(int count)
	{
		uint32_t value = 0;
		for (int i = 0; i < count; i++) {
			value <<= 1;
			if (bit < end) {
				value |= (data[bit >> 3] >> (7 - (bit & 7))) & 1;
			} else
				overrun = true;
			bit++;
		}
		return value;
	}

	bool overrun = false;
};

}	// namespace


Config
MSBCConfig()
{
	Config config;
	config.frequency = kFrequency16000;
	config.blocks = kMSBCBlocks;
	config.mode = kModeMono;
	config.allocation = kAllocationLoudness;
	config.subbands = 8;
	config.bitpool = kMSBCBitpool;
	return config;
}


Decoder::Decoder()
	:
	fSubbands(0)
{
	memset(&fConfig, 0, sizeof(fConfig));
	memset(fV, 0, sizeof(fV));
	memset(fWindow, 0, sizeof(fWindow));
	memset(fMatrix, 0, sizeof(fMatrix));
}


void
Decoder::Reset()
{
	memset(fV, 0, sizeof(fV));
}


DecodeStatus
Decoder::Decode(const uint8_t* data, size_t size, float* samples,
	size_t capacity, size_t* _length, int* _channels, int* _frameSamples)
{
	if (data == NULL || size < 4)
		return kDecodeShort;

	Config config;
	if (data[0] == kMSBCSyncWord) {
		// The two reserved bytes are not checked: the CRC covers them.
		config = MSBCConfig();
	} else if (data[0] == kSyncWord) {
		// 12.5: sampling frequency, blocks, channel mode, allocation method,
		// subbands, bitpool.
		config.frequency = data[1] >> 6;
		config.blocks = 4 * (((data[1] >> 4) & 3) + 1);
		config.mode = (data[1] >> 2) & 3;
		config.allocation = (data[1] >> 1) & 1;
		config.subbands = (data[1] & 1) != 0 ? 8 : 4;
		config.bitpool = data[2];
		if (!ValidConfig(config))
			return kDecodeBadHeader;
	} else
		return kDecodeBadSync;

	const int M = config.subbands;
	const int B = config.blocks;
	const int channels = Channels(config);
	const size_t length = FrameLength(config);
	if (size < length)
		return kDecodeShort;
	if (samples == NULL || capacity < (size_t)(B * M * channels))
		return kDecodeShort;

	BitReader reader(data, 4, length);
	uint8_t join[kMaxSubbands];
	memset(join, 0, sizeof(join));
	if (config.mode == kModeJointStereo) {
		for (int sb = 0; sb < M; sb++)
			join[sb] = (uint8_t)reader.Get(1);
		// The last subband is never joined (12.6.2).
		join[M - 1] = 0;
	}
	uint8_t scaleFactors[kMaxChannels][kMaxSubbands];
	memset(scaleFactors, 0, sizeof(scaleFactors));
	for (int ch = 0; ch < channels; ch++) {
		for (int sb = 0; sb < M; sb++)
			scaleFactors[ch][sb] = (uint8_t)reader.Get(4);
	}

	// 12.6.4: byte 1, byte 2, then the join and scale factor bits.
	size_t protectedBits = reader.bit - 32;
	uint8_t crcData[2 + (kMaxSubbands + 4 * kMaxSubbands * kMaxChannels + 7)
		/ 8];
	crcData[0] = data[1];
	crcData[1] = data[2];
	memcpy(crcData + 2, data + 4, (protectedBits + 7) / 8);
	if (CRC8(crcData, 16 + protectedBits) != data[3])
		return kDecodeBadCRC;

	uint8_t bits[kMaxChannels][kMaxSubbands];
	AllocateBits(config, scaleFactors, bits);

	// 12.6.5 reversed: the samples, block by block, channel by channel,
	// subband by subband.
	float subband[kMaxBlocks][kMaxChannels][kMaxSubbands];
	for (int blk = 0; blk < B; blk++) {
		for (int ch = 0; ch < channels; ch++) {
			for (int sb = 0; sb < M; sb++) {
				int count = bits[ch][sb];
				if (count == 0) {
					subband[blk][ch][sb] = 0.0f;
					continue;
				}
				uint32_t value = reader.Get(count);
				double levels = (double)((1u << count) - 1);
				double scale = (double)(2u << scaleFactors[ch][sb]);
				subband[blk][ch][sb] = (float)(scale
					* ((value * 2.0 + 1.0) / levels - 1.0));
			}
		}
	}
	if (reader.overrun)
		return kDecodeBadHeader;

	if (config.mode == kModeJointStereo) {
		for (int blk = 0; blk < B; blk++) {
			for (int sb = 0; sb < M; sb++) {
				if (!join[sb])
					continue;
				float mid = subband[blk][0][sb];
				float side = subband[blk][1][sb];
				subband[blk][0][sb] = mid + side;
				subband[blk][1][sb] = mid - side;
			}
		}
	}

	if (M != fSubbands || channels != Channels(fConfig)) {
		// A new filter: 12.7, with the window D[] of the prototype filter
		// (the same filter as the analysis, scaled by the subband count) and
		// the cosine matrix N[k][i] = cos((i + 0.5) (k + M / 2) pi / M).
		fSubbands = M;
		memset(fV, 0, sizeof(fV));
		const int taps = 10 * M;
		for (int n = 0; n < taps; n++) {
			double h = PrototypeTap(M, n);
			// The same folding sign as the analysis window (sbc_encoder.cpp,
			// Encoder::_Setup()).
			if ((n / (2 * M)) & 1)
				h = -h;
			fWindow[n] = (float)(-h * M);
		}
		for (int k = 0; k < 2 * M; k++) {
			for (int i = 0; i < M; i++) {
				fMatrix[k][i] = (float)cos((i + 0.5) * (k + M / 2.0) * M_PI
					/ M);
			}
		}
	}
	fConfig = config;

	for (int blk = 0; blk < B; blk++) {
		for (int ch = 0; ch < channels; ch++) {
			_Synthesize(ch, subband[blk][ch],
				samples + (size_t)blk * M * channels + ch, channels);
		}
	}

	*_length = length;
	*_channels = channels;
	*_frameSamples = B * M;
	return kDecodeOK;
}


void
Decoder::_Synthesize(int channel, const float* subband, float* out,
	int stride)
{
	const int M = fSubbands;
	float* V = fV[channel];

	memmove(V + 2 * M, V, sizeof(float) * 18 * M);
	for (int k = 0; k < 2 * M; k++) {
		float sum = 0.0f;
		for (int i = 0; i < M; i++)
			sum += fMatrix[k][i] * subband[i];
		V[k] = sum;
	}

	float U[10 * kMaxSubbands];
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < M; j++) {
			U[i * 2 * M + j] = V[i * 4 * M + j];
			U[i * 2 * M + M + j] = V[i * 4 * M + 3 * M + j];
		}
	}

	for (int j = 0; j < M; j++) {
		float sum = 0.0f;
		for (int i = 0; i < 10; i++)
			sum += U[j + M * i] * fWindow[j + M * i];
		float value = sum / 32768.0f;
		if (value > 1.0f)
			value = 1.0f;
		else if (value < -1.0f)
			value = -1.0f;
		out[j * stride] = value;
	}
}


}	// namespace sbc
