/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * SBC encoder from the A2DP specification, appendix B. Section numbers in
 * the comments are those of that appendix.
 */


#include "sbc_encoder.h"

#include <math.h>
#include <string.h>


namespace sbc {


// 12.6.1, tables of the analysis prototype filter, here as the filter
// itself: the first half and the middle tap of the symmetric impulse
// response (h[n] = h[10 * subbands - n]). The specification prints the
// window with the folding sign of every second group of 2 * subbands taps
// already applied; Encoder::Init() applies that sign (see there), so these
// are the plain filter values.
static const double kProto4[21] = {
	0.00000000E+00, 5.36548976E-04, 1.49188357E-03, 2.73370904E-03,
	3.83720193E-03, 3.89205149E-03, 1.86581691E-03, -3.06012286E-03,
	-1.09137620E-02, -2.04385087E-02, -2.88757392E-02, -3.21939290E-02,
	-2.58767811E-02, -6.13245186E-03, 2.88217274E-02, 7.76463494E-02,
	1.35593274E-01, 1.94987841E-01, 2.46636662E-01, 2.81828203E-01,
	2.94315332E-01
};

static const double kProto8[41] = {
	0.00000000E+00, 1.56575398E-04, 3.43256425E-04, 5.54620202E-04,
	8.23919506E-04, 1.13992507E-03, 1.47640169E-03, 1.78371725E-03,
	2.01182542E-03, 2.10371989E-03, 1.99454554E-03, 1.61656283E-03,
	9.02154502E-04, -1.78805361E-04, -1.64973098E-03, -3.49717454E-03,
	-5.65949473E-03, -8.02941163E-03, -1.04584443E-02, -1.27472335E-02,
	-1.46525263E-02, -1.59045603E-02, -1.62208471E-02, -1.53184106E-02,
	-1.29371806E-02, -8.85757540E-03, -2.92408442E-03, 4.91578024E-03,
	1.46404076E-02, 2.61098752E-02, 3.90751381E-02, 5.31873032E-02,
	6.79989431E-02, 8.29847578E-02, 9.75753918E-02, 1.11196689E-01,
	1.23264548E-01, 1.33264415E-01, 1.40753505E-01, 1.45389847E-01,
	1.46955068E-01
};

// 12.6.3, the offsets of the Loudness allocation, per sampling frequency.
static const int kOffset4[4][4] = {
	{ -1, 0, 0, 0 },
	{ -2, 0, 0, 1 },
	{ -2, 0, 0, 1 },
	{ -2, 0, 0, 1 }
};

static const int kOffset8[4][8] = {
	{ -2, 0, 0, 0, 0, 0, 0, 1 },
	{ -3, 0, 0, 0, 0, 0, 1, 2 },
	{ -4, 0, 0, 0, 0, 0, 1, 2 },
	{ -4, 0, 0, 0, 0, 0, 1, 2 }
};


// #pragma mark - Bit writer


namespace {

struct BitWriter {
	uint8_t*	data;
	size_t		bit;

	BitWriter(uint8_t* _data, size_t startByte)
		:
		data(_data),
		bit(startByte * 8)
	{
	}

	void Put(uint32_t value, int count)
	{
		for (int i = count - 1; i >= 0; i--) {
			uint8_t& byte = data[bit >> 3];
			int shift = 7 - (int)(bit & 7);
			if ((value >> i) & 1)
				byte |= (uint8_t)(1 << shift);
			else
				byte &= (uint8_t)~(1 << shift);
			bit++;
		}
	}
};

}	// namespace


// #pragma mark - Configuration


bool
ValidConfig(const Config& config)
{
	if (config.frequency < kFrequency16000
		|| config.frequency > kFrequency48000)
		return false;
	if (config.blocks != 4 && config.blocks != 8 && config.blocks != 12
		&& config.blocks != 16)
		return false;
	if (config.mode < kModeMono || config.mode > kModeJointStereo)
		return false;
	if (config.allocation != kAllocationLoudness
		&& config.allocation != kAllocationSNR)
		return false;
	if (config.subbands != 4 && config.subbands != 8)
		return false;
	return config.bitpool >= 2 && config.bitpool <= MaxBitpool(config);
}


int
Channels(const Config& config)
{
	return config.mode == kModeMono ? 1 : 2;
}


int
SampleRate(const Config& config)
{
	static const int kRates[4] = { 16000, 32000, 44100, 48000 };
	if (config.frequency < 0 || config.frequency > 3)
		return 0;
	return kRates[config.frequency];
}


int
MaxBitpool(const Config& config)
{
	int max = (config.mode == kModeMono || config.mode == kModeDualChannel)
		? 16 * config.subbands : 32 * config.subbands;
	return max > 250 ? 250 : max;
}


size_t
FrameLength(const Config& config)
{
	size_t subbands = config.subbands;
	size_t blocks = config.blocks;
	size_t bitpool = config.bitpool;
	size_t channels = Channels(config);
	size_t length = 4 + (4 * subbands * channels) / 8;
	switch (config.mode) {
		case kModeMono:
		case kModeDualChannel:
			return length + (blocks * channels * bitpool + 7) / 8;
		case kModeStereo:
			return length + (blocks * bitpool + 7) / 8;
		default:
			return length + (subbands + blocks * bitpool + 7) / 8;
	}
}


uint32_t
BitRate(const Config& config)
{
	uint64_t bits = (uint64_t)FrameLength(config) * 8 * SampleRate(config);
	return (uint32_t)(bits / ((uint64_t)config.subbands * config.blocks));
}


uint8_t
CRC8(const uint8_t* data, size_t bits)
{
	uint8_t crc = 0x0f;
	for (size_t i = 0; i < bits; i++) {
		int in = (data[i >> 3] >> (7 - (i & 7))) & 1;
		int top = (crc >> 7) & 1;
		crc = (uint8_t)(crc << 1);
		if (top ^ in)
			crc ^= 0x1d;
	}
	return crc;
}


// #pragma mark - Bit allocation (12.6.3)


static void
compute_bitneed(const Config& config, const uint8_t* scaleFactors,
	int* bitneed)
{
	for (int sb = 0; sb < config.subbands; sb++) {
		if (config.allocation == kAllocationSNR) {
			bitneed[sb] = scaleFactors[sb];
			continue;
		}
		if (scaleFactors[sb] == 0) {
			bitneed[sb] = -5;
			continue;
		}
		int offset = config.subbands == 4
			? kOffset4[config.frequency][sb] : kOffset8[config.frequency][sb];
		int loudness = scaleFactors[sb] - offset;
		bitneed[sb] = loudness > 0 ? loudness / 2 : loudness;
	}
}


// One allocation over "count" channels sharing one bitpool: count is 1 for
// mono and for each channel of dual channel, 2 for stereo and joint stereo.
static void
allocate(const Config& config, int count,
	int bitneed[kMaxChannels][kMaxSubbands],
	uint8_t bits[kMaxChannels][kMaxSubbands], int first)
{
	const int subbands = config.subbands;
	const int bitpool = config.bitpool;

	int maxBitneed = 0;
	for (int ch = first; ch < first + count; ch++) {
		for (int sb = 0; sb < subbands; sb++) {
			if (bitneed[ch][sb] > maxBitneed)
				maxBitneed = bitneed[ch][sb];
		}
	}

	int bitcount = 0;
	int slicecount = 0;
	int bitslice = maxBitneed + 1;
	do {
		bitslice--;
		bitcount += slicecount;
		slicecount = 0;
		for (int ch = first; ch < first + count; ch++) {
			for (int sb = 0; sb < subbands; sb++) {
				int need = bitneed[ch][sb];
				if (need > bitslice + 1 && need < bitslice + 16)
					slicecount++;
				else if (need == bitslice + 1)
					slicecount += 2;
			}
		}
	} while (bitcount + slicecount < bitpool);

	if (bitcount + slicecount == bitpool) {
		bitcount += slicecount;
		bitslice--;
	}

	for (int ch = first; ch < first + count; ch++) {
		for (int sb = 0; sb < subbands; sb++) {
			int need = bitneed[ch][sb];
			if (need < bitslice + 2)
				bits[ch][sb] = 0;
			else {
				int value = need - bitslice;
				bits[ch][sb] = (uint8_t)(value > 16 ? 16 : value);
			}
		}
	}

	// The rest of the bitpool, subband by subband and, in the stereo modes,
	// left then right within each subband.
	int ch = first;
	int sb = 0;
	while (bitcount < bitpool && sb < subbands) {
		if (bits[ch][sb] >= 2 && bits[ch][sb] < 16) {
			bits[ch][sb]++;
			bitcount++;
		} else if (bitneed[ch][sb] == bitslice + 1
			&& bitpool > bitcount + 1) {
			bits[ch][sb] = 2;
			bitcount += 2;
		}
		if (count == 1 || ch == first + 1) {
			ch = first;
			sb++;
		} else
			ch = first + 1;
	}

	ch = first;
	sb = 0;
	while (bitcount < bitpool && sb < subbands) {
		if (bits[ch][sb] < 16) {
			bits[ch][sb]++;
			bitcount++;
		}
		if (count == 1 || ch == first + 1) {
			ch = first;
			sb++;
		} else
			ch = first + 1;
	}
}


void
AllocateBits(const Config& config,
	const uint8_t scaleFactors[kMaxChannels][kMaxSubbands],
	uint8_t bits[kMaxChannels][kMaxSubbands])
{
	int bitneed[kMaxChannels][kMaxSubbands];
	memset(bitneed, 0, sizeof(bitneed));
	memset(bits, 0, sizeof(uint8_t) * kMaxChannels * kMaxSubbands);

	int channels = Channels(config);
	for (int ch = 0; ch < channels; ch++)
		compute_bitneed(config, scaleFactors[ch], bitneed[ch]);

	if (config.mode == kModeStereo || config.mode == kModeJointStereo)
		allocate(config, 2, bitneed, bits, 0);
	else {
		for (int ch = 0; ch < channels; ch++)
			allocate(config, 1, bitneed, bits, ch);
	}
}


double
PrototypeTap(int subbands, int n)
{
	const int taps = 10 * subbands;
	if ((subbands != 4 && subbands != 8) || n < 0 || n >= taps)
		return 0.0;
	const double* proto = subbands == 4 ? kProto4 : kProto8;
	return n <= taps / 2 ? proto[n] : proto[taps - n];
}


// #pragma mark - Encoder


Encoder::Encoder()
	:
	fChannels(0),
	fMSBC(false)
{
	memset(&fConfig, 0, sizeof(fConfig));
	memset(fHistory, 0, sizeof(fHistory));
	memset(fWindow, 0, sizeof(fWindow));
	memset(fMatrix, 0, sizeof(fMatrix));
}


bool
Encoder::Init(const Config& config)
{
	if (!ValidConfig(config))
		return false;
	fMSBC = false;
	return _Setup(config);
}


bool
Encoder::InitMSBC()
{
	// 15 blocks are not one of the A2DP block counts that ValidConfig()
	// checks; everything else of the fixed configuration is.
	Config config;
	config.frequency = kFrequency16000;
	config.blocks = 15;
	config.mode = kModeMono;
	config.allocation = kAllocationLoudness;
	config.subbands = 8;
	config.bitpool = 26;
	fMSBC = true;
	return _Setup(config);
}


bool
Encoder::_Setup(const Config& config)
{
	fConfig = config;
	fChannels = Channels(config);
	memset(fHistory, 0, sizeof(fHistory));

	const int M = config.subbands;
	const int taps = 10 * M;
	for (int n = 0; n < taps; n++) {
		double h = PrototypeTap(M, n);
		// 12.6.1 sums Z[] in groups of 2 * M taps into Y[] before the
		// cosine matrix. The matrix row for the tap n + 2Mj is the row for
		// n times cos((i + 0.5) * 2j * pi) ... = (-1)^j, so every odd group
		// carries a minus sign; the printed table has it built in.
		if ((n / (2 * M)) & 1)
			h = -h;
		fWindow[n] = (float)h;
	}
	for (int i = 0; i < M; i++) {
		for (int k = 0; k < 2 * M; k++) {
			fMatrix[i][k] = (float)cos((i + 0.5) * (k - M / 2.0) * M_PI / M);
		}
	}
	return true;
}


int
Encoder::FrameSamples() const
{
	return fConfig.blocks * fConfig.subbands;
}


size_t
Encoder::FrameLength() const
{
	return sbc::FrameLength(fConfig);
}


size_t
Encoder::Encode(const float* samples, uint8_t* out, size_t size)
{
	const int M = fConfig.subbands;
	const int B = fConfig.blocks;
	const int channels = fChannels;
	const size_t length = FrameLength();
	if (channels == 0 || out == NULL || size < length)
		return 0;

	// Analysis (12.6.1).
	float subband[kMaxBlocks][kMaxChannels][kMaxSubbands];
	for (int blk = 0; blk < B; blk++) {
		for (int ch = 0; ch < channels; ch++) {
			float* X = fHistory[ch];
			memmove(X + M, X, sizeof(float) * 9 * M);
			for (int i = 0; i < M; i++) {
				float value = samples != NULL
					? samples[(blk * M + i) * channels + ch] * 32768.0f : 0.0f;
				X[M - 1 - i] = value;
			}
			float Y[2 * kMaxSubbands];
			for (int k = 0; k < 2 * M; k++) {
				float sum = 0.0f;
				for (int j = 0; j < 5; j++)
					sum += fWindow[k + j * 2 * M] * X[k + j * 2 * M];
				Y[k] = sum;
			}
			for (int i = 0; i < M; i++) {
				float sum = 0.0f;
				for (int k = 0; k < 2 * M; k++)
					sum += fMatrix[i][k] * Y[k];
				subband[blk][ch][i] = sum;
			}
		}
	}

	// Scale factors (12.6.2): the smallest x with |sample| < 2^(x + 1).
	uint8_t scaleFactors[kMaxChannels][kMaxSubbands];
	memset(scaleFactors, 0, sizeof(scaleFactors));
	for (int ch = 0; ch < channels; ch++) {
		for (int sb = 0; sb < M; sb++) {
			float peak = 0.0f;
			for (int blk = 0; blk < B; blk++) {
				float value = fabsf(subband[blk][ch][sb]);
				if (value > peak)
					peak = value;
			}
			int x = 0;
			while (x < 15 && peak >= (float)(2 << x))
				x++;
			scaleFactors[ch][sb] = (uint8_t)x;
		}
	}

	// Joint stereo (12.6.2): per subband but the last, code (L + R) / 2 and
	// (L - R) / 2 when their scale factors sum lower.
	uint8_t join[kMaxSubbands];
	memset(join, 0, sizeof(join));
	if (fConfig.mode == kModeJointStereo) {
		for (int sb = 0; sb < M - 1; sb++) {
			float peakMid = 0.0f;
			float peakSide = 0.0f;
			for (int blk = 0; blk < B; blk++) {
				float left = subband[blk][0][sb];
				float right = subband[blk][1][sb];
				float mid = fabsf((left + right) * 0.5f);
				float side = fabsf((left - right) * 0.5f);
				if (mid > peakMid)
					peakMid = mid;
				if (side > peakSide)
					peakSide = side;
			}
			int xm = 0;
			while (xm < 15 && peakMid >= (float)(2 << xm))
				xm++;
			int xs = 0;
			while (xs < 15 && peakSide >= (float)(2 << xs))
				xs++;
			if (xm + xs < scaleFactors[0][sb] + scaleFactors[1][sb]) {
				join[sb] = 1;
				scaleFactors[0][sb] = (uint8_t)xm;
				scaleFactors[1][sb] = (uint8_t)xs;
				for (int blk = 0; blk < B; blk++) {
					float left = subband[blk][0][sb];
					float right = subband[blk][1][sb];
					subband[blk][0][sb] = (left + right) * 0.5f;
					subband[blk][1][sb] = (left - right) * 0.5f;
				}
			}
		}
	}

	uint8_t bits[kMaxChannels][kMaxSubbands];
	AllocateBits(fConfig, scaleFactors, bits);

	// Frame header (12.5).
	memset(out, 0, length);
	if (fMSBC) {
		// HFP 1.8, A.1: the sync word, then two reserved bytes.
		out[0] = 0xad;
		out[1] = 0;
		out[2] = 0;
	} else {
		out[0] = kSyncWord;
		int blocksCode = B / 4 - 1;
		out[1] = (uint8_t)((fConfig.frequency << 6) | (blocksCode << 4)
			| (fConfig.mode << 2) | (fConfig.allocation << 1)
			| (M == 8 ? 1 : 0));
		out[2] = (uint8_t)fConfig.bitpool;
	}

	BitWriter writer(out, 4);
	if (fConfig.mode == kModeJointStereo) {
		for (int sb = 0; sb < M; sb++)
			writer.Put(join[sb], 1);
	}
	for (int ch = 0; ch < channels; ch++) {
		for (int sb = 0; sb < M; sb++)
			writer.Put(scaleFactors[ch][sb], 4);
	}

	// The CRC (12.6.4) covers byte 1, the bitpool, and the join and scale
	// factor bits, not the sync word or itself.
	size_t protectedBits = writer.bit - 32;
	uint8_t crcData[2 + (kMaxSubbands + 4 * kMaxSubbands * kMaxChannels + 7)
		/ 8];
	crcData[0] = out[1];
	crcData[1] = out[2];
	memcpy(crcData + 2, out + 4, (protectedBits + 7) / 8);
	out[3] = CRC8(crcData, 16 + protectedBits);

	// Quantization (12.6.5) and the samples, block by block, channel by
	// channel, subband by subband.
	for (int blk = 0; blk < B; blk++) {
		for (int ch = 0; ch < channels; ch++) {
			for (int sb = 0; sb < M; sb++) {
				int count = bits[ch][sb];
				if (count == 0)
					continue;
				uint32_t levels = (1u << count) - 1;
				double scale = (double)(2u << scaleFactors[ch][sb]);
				double value = ((double)subband[blk][ch][sb] / scale + 1.0)
					* levels / 2.0;
				int64_t quantized = (int64_t)floor(value);
				if (quantized < 0)
					quantized = 0;
				if (quantized > (int64_t)levels - 1)
					quantized = levels - 1;
				writer.Put((uint32_t)quantized, count);
			}
		}
	}

	return length;
}


size_t
Encoder::EncodeSilence(uint8_t* out, size_t size)
{
	return Encode(NULL, out, size);
}


}	// namespace sbc
