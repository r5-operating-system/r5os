/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host test of the R5 SBC encoder (scripts/test-sbc.sh), against the A2DP
 * specification v1.3.2:
 *  - frame length and bit rate per bitpool, the values of table 4.7
 *    (the recommended middle and high quality settings) and the formula of
 *    appendix B 12.9 for every bitpool;
 *  - the bitstream layout of known frames: sync word, header byte and
 *    bitpool, and the CRC-8 checked with a second, table-driven CRC;
 *  - a decoder written here from appendix B 12.6 to 12.8 (parse, bit
 *    allocation recomputed, dequantization, synthesis filter bank) that
 *    reads every frame back: the signal must come back with a high SNR,
 *    which only works when the analysis window, matrix, scale factors,
 *    joint stereo and bit packing all match the specification;
 *  - a tone in the middle of a subband lands in that subband.
 * Prints "sbc_test: ok" and exits 0 when everything passes.
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "sbc_encoder.h"


static int sFailures = 0;


static void
check(bool condition, const char* what)
{
	if (!condition) {
		printf("sbc_test: FAIL %s\n", what);
		sFailures++;
	}
}


// #pragma mark - Table driven CRC, independent of sbc::CRC8()


// Whole bytes through a 256-entry table (crc = table[crc ^ byte]), the
// trailing bits of the last byte one by one.
static uint8_t
crc_independent(const uint8_t* data, size_t bits)
{
	static uint8_t table[256];
	static bool ready = false;
	if (!ready) {
		for (int i = 0; i < 256; i++) {
			uint8_t value = (uint8_t)i;
			for (int b = 0; b < 8; b++)
				value = (uint8_t)((value & 0x80) ? (value << 1) ^ 0x1d
					: value << 1);
			table[i] = value;
		}
		ready = true;
	}

	uint8_t crc = 0x0f;
	size_t i = 0;
	for (; i + 8 <= bits; i += 8)
		crc = table[crc ^ data[i / 8]];
	for (; i < bits; i++) {
		uint8_t in = (uint8_t)(((data[i / 8] >> (7 - (i % 8))) & 1) << 7);
		crc = (uint8_t)(((crc ^ in) & 0x80) ? (crc << 1) ^ 0x1d : crc << 1);
	}
	return crc;
}


// #pragma mark - Reference decoder (appendix B 12.6 to 12.8)


struct BitReader {
	const uint8_t*	data;
	size_t			bit;
	size_t			end;

	uint32_t Get(int count)
	{
		uint32_t value = 0;
		for (int i = 0; i < count; i++) {
			if (bit >= end)
				return value;
			value = (value << 1) | ((data[bit >> 3] >> (7 - (bit & 7))) & 1);
			bit++;
		}
		return value;
	}
};


struct Decoder {
	double	V[2][200];
	double	D[80];
	double	N[16][8];
	int		M;

	Decoder()
		:
		M(0)
	{
		memset(V, 0, sizeof(V));
	}

	void Setup(int subbands)
	{
		if (M == subbands)
			return;
		M = subbands;
		memset(V, 0, sizeof(V));
		// The window of the synthesis is the analysis window times -M
		// (12.8, the decoder uses the same prototype); built here from the
		// encoder-independent definition: h[n] symmetric, sign of each odd
		// group of 2M taps flipped.
		static const double p4[21] = {
			0.00000000E+00, 5.36548976E-04, 1.49188357E-03, 2.73370904E-03,
			3.83720193E-03, 3.89205149E-03, 1.86581691E-03, -3.06012286E-03,
			-1.09137620E-02, -2.04385087E-02, -2.88757392E-02,
			-3.21939290E-02, -2.58767811E-02, -6.13245186E-03,
			2.88217274E-02, 7.76463494E-02, 1.35593274E-01, 1.94987841E-01,
			2.46636662E-01, 2.81828203E-01, 2.94315332E-01 };
		static const double p8[41] = {
			0.00000000E+00, 1.56575398E-04, 3.43256425E-04, 5.54620202E-04,
			8.23919506E-04, 1.13992507E-03, 1.47640169E-03, 1.78371725E-03,
			2.01182542E-03, 2.10371989E-03, 1.99454554E-03, 1.61656283E-03,
			9.02154502E-04, -1.78805361E-04, -1.64973098E-03,
			-3.49717454E-03, -5.65949473E-03, -8.02941163E-03,
			-1.04584443E-02, -1.27472335E-02, -1.46525263E-02,
			-1.59045603E-02, -1.62208471E-02, -1.53184106E-02,
			-1.29371806E-02, -8.85757540E-03, -2.92408442E-03,
			4.91578024E-03, 1.46404076E-02, 2.61098752E-02, 3.90751381E-02,
			5.31873032E-02, 6.79989431E-02, 8.29847578E-02, 9.75753918E-02,
			1.11196689E-01, 1.23264548E-01, 1.33264415E-01, 1.40753505E-01,
			1.45389847E-01, 1.46955068E-01 };
		const double* p = M == 4 ? p4 : p8;
		for (int n = 0; n < 10 * M; n++) {
			double h = n <= 5 * M ? p[n] : p[10 * M - n];
			if ((n / (2 * M)) & 1)
				h = -h;
			D[n] = h * M * sDecoderSign;
		}
		for (int k = 0; k < 2 * M; k++) {
			for (int i = 0; i < M; i++)
				N[k][i] = cos((i + 0.5) * (k + M / 2.0) * M_PI / M);
		}
	}

	static double sDecoderSign;

	// Returns the frame length, 0 on a bad frame. Appends the samples.
	size_t Decode(const uint8_t* frame, size_t size,
		std::vector<double> out[2], sbc::Config* _config)
	{
		if (size < 4 || frame[0] != sbc::kSyncWord)
			return 0;
		sbc::Config config;
		config.frequency = frame[1] >> 6;
		config.blocks = ((frame[1] >> 4) & 3) * 4 + 4;
		config.mode = (frame[1] >> 2) & 3;
		config.allocation = (frame[1] >> 1) & 1;
		config.subbands = (frame[1] & 1) ? 8 : 4;
		config.bitpool = frame[2];
		if (!sbc::ValidConfig(config))
			return 0;
		size_t length = sbc::FrameLength(config);
		if (size < length)
			return 0;
		*_config = config;

		const int channels = sbc::Channels(config);
		const int B = config.blocks;
		Setup(config.subbands);

		BitReader reader = { frame, 32, length * 8 };
		int join[8] = { 0 };
		if (config.mode == sbc::kModeJointStereo) {
			for (int sb = 0; sb < M; sb++)
				join[sb] = (int)reader.Get(1);
		}
		uint8_t scf[2][8];
		for (int ch = 0; ch < channels; ch++) {
			for (int sb = 0; sb < M; sb++)
				scf[ch][sb] = (uint8_t)reader.Get(4);
		}

		size_t protectedBits = reader.bit - 32;
		std::vector<uint8_t> crcData(2 + (protectedBits + 7) / 8);
		crcData[0] = frame[1];
		crcData[1] = frame[2];
		memcpy(&crcData[2], frame + 4, (protectedBits + 7) / 8);
		if (crc_independent(&crcData[0], 16 + protectedBits) != frame[3])
			return 0;

		uint8_t bits[2][8];
		sbc::AllocateBits(config, scf, bits);

		// The allocation must spend the bitpool and no more (12.6.3).
		int spent[2] = { 0, 0 };
		for (int ch = 0; ch < channels; ch++) {
			for (int sb = 0; sb < M; sb++)
				spent[ch] += bits[ch][sb];
		}
		if (config.mode == sbc::kModeStereo
			|| config.mode == sbc::kModeJointStereo) {
			if (spent[0] + spent[1] > config.bitpool)
				return 0;
		} else {
			for (int ch = 0; ch < channels; ch++) {
				if (spent[ch] > config.bitpool)
					return 0;
			}
		}

		double sb_sample[16][2][8];
		for (int blk = 0; blk < B; blk++) {
			for (int ch = 0; ch < channels; ch++) {
				for (int sb = 0; sb < M; sb++) {
					if (bits[ch][sb] == 0) {
						sb_sample[blk][ch][sb] = 0;
						continue;
					}
					uint32_t q = reader.Get(bits[ch][sb]);
					double levels = (double)((1u << bits[ch][sb]) - 1);
					double scale = (double)(2u << scf[ch][sb]);
					sb_sample[blk][ch][sb]
						= scale * ((q * 2.0 + 1.0) / levels - 1.0);
				}
			}
		}
		if (reader.bit > length * 8)
			return 0;

		if (config.mode == sbc::kModeJointStereo) {
			for (int blk = 0; blk < B; blk++) {
				for (int sb = 0; sb < M; sb++) {
					if (!join[sb])
						continue;
					double mid = sb_sample[blk][0][sb];
					double side = sb_sample[blk][1][sb];
					sb_sample[blk][0][sb] = mid + side;
					sb_sample[blk][1][sb] = mid - side;
				}
			}
		}

		// Synthesis (12.8).
		for (int blk = 0; blk < B; blk++) {
			for (int ch = 0; ch < channels; ch++) {
				double* v = V[ch];
				memmove(v + 2 * M, v, sizeof(double) * 18 * M);
				for (int k = 0; k < 2 * M; k++) {
					double sum = 0;
					for (int i = 0; i < M; i++)
						sum += N[k][i] * sb_sample[blk][ch][i];
					v[k] = sum;
				}
				double U[80];
				for (int i = 0; i < 5; i++) {
					for (int j = 0; j < M; j++) {
						U[i * 2 * M + j] = v[i * 4 * M + j];
						U[i * 2 * M + M + j] = v[i * 4 * M + 3 * M + j];
					}
				}
				for (int j = 0; j < M; j++) {
					double sum = 0;
					for (int i = 0; i < 10; i++)
						sum += U[j + M * i] * D[j + M * i];
					out[ch].push_back(sum / 32768.0);
				}
			}
		}
		return length;
	}
};

double Decoder::sDecoderSign = -1.0;


// #pragma mark - Tests


static void
test_table_4_7()
{
	// A2DP v1.3.2, table 4.7: 16 blocks, 8 subbands, Loudness.
	struct Row {
		int frequency;
		int mode;
		int bitpool;
		size_t length;
		uint32_t kbps;
	};
	static const Row rows[] = {
		{ sbc::kFrequency48000, sbc::kModeMono, 18, 44, 132 },
		{ sbc::kFrequency44100, sbc::kModeMono, 19, 46, 127 },
		{ sbc::kFrequency48000, sbc::kModeMono, 29, 66, 198 },
		{ sbc::kFrequency44100, sbc::kModeMono, 31, 70, 193 },
		{ sbc::kFrequency48000, sbc::kModeJointStereo, 33, 79, 237 },
		{ sbc::kFrequency44100, sbc::kModeJointStereo, 35, 83, 229 },
		{ sbc::kFrequency48000, sbc::kModeJointStereo, 51, 115, 345 },
		{ sbc::kFrequency44100, sbc::kModeJointStereo, 53, 119, 328 },
	};
	for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		sbc::Config config = { rows[i].frequency, 16, rows[i].mode,
			sbc::kAllocationLoudness, 8, rows[i].bitpool };
		char what[96];
		snprintf(what, sizeof(what), "table 4.7 row %zu length %zu", i,
			sbc::FrameLength(config));
		check(sbc::FrameLength(config) == rows[i].length, what);
		uint32_t kbps = (sbc::BitRate(config) + 500) / 1000;
		snprintf(what, sizeof(what), "table 4.7 row %zu %u kb/s", i, kbps);
		check(kbps == rows[i].kbps, what);
	}

	// 12.9 for every bitpool, mode and subband count.
	for (int mode = 0; mode < 4; mode++) {
		for (int subbands = 4; subbands <= 8; subbands += 4) {
			for (int blocks = 4; blocks <= 16; blocks += 4) {
				sbc::Config config = { sbc::kFrequency44100, blocks, mode,
					sbc::kAllocationLoudness, subbands, 2 };
				int channels = mode == sbc::kModeMono ? 1 : 2;
				for (int bp = 2; bp <= sbc::MaxBitpool(config); bp++) {
					config.bitpool = bp;
					size_t expected = 4 + (4 * subbands * channels) / 8;
					if (mode == sbc::kModeMono || mode == sbc::kModeDualChannel)
						expected += (blocks * channels * bp + 7) / 8;
					else if (mode == sbc::kModeStereo)
						expected += (blocks * bp + 7) / 8;
					else
						expected += (subbands + blocks * bp + 7) / 8;
					if (sbc::FrameLength(config) != expected) {
						check(false, "12.9 frame length");
						return;
					}
				}
			}
		}
	}
	check(sbc::MaxBitpool((sbc::Config){ 2, 16, sbc::kModeJointStereo, 0, 8,
		2 }) == 250, "max bitpool joint 8 subbands is 250");
	check(sbc::MaxBitpool((sbc::Config){ 2, 16, sbc::kModeMono, 0, 8,
		2 }) == 128, "max bitpool mono 8 subbands is 128");
	check(sbc::MaxBitpool((sbc::Config){ 2, 16, sbc::kModeDualChannel, 0, 4,
		2 }) == 64, "max bitpool dual 4 subbands is 64");
}


static void
test_crc()
{
	// The two CRCs agree on random data of every bit length.
	uint32_t state = 12345;
	for (int round = 0; round < 2000; round++) {
		uint8_t data[24];
		for (size_t i = 0; i < sizeof(data); i++) {
			state = state * 1103515245 + 12345;
			data[i] = (uint8_t)(state >> 16);
		}
		size_t bits = 1 + round % (sizeof(data) * 8);
		if (sbc::CRC8(data, bits) != crc_independent(data, bits)) {
			check(false, "CRC8 agrees with the table CRC");
			return;
		}
	}
	// Known value: no data leaves the start value.
	check(sbc::CRC8(NULL, 0) == 0x0f, "CRC8 start value 0x0f");
}


static void
test_layout()
{
	sbc::Config config = { sbc::kFrequency44100, 16, sbc::kModeJointStereo,
		sbc::kAllocationLoudness, 8, 53 };
	sbc::Encoder encoder;
	check(encoder.Init(config), "init 44.1 kHz joint stereo bitpool 53");
	uint8_t frame[512];
	size_t length = encoder.EncodeSilence(frame, sizeof(frame));
	check(length == 119, "silence frame is 119 bytes");
	check(frame[0] == 0x9c, "sync word 0x9c");
	// 10 (44.1 kHz) 11 (16 blocks) 11 (joint) 0 (loudness) 1 (8 subbands)
	check(frame[1] == 0xbd, "header byte 0xbd");
	check(frame[2] == 53, "bitpool byte");
	// Silence: no joint subbands, every scale factor 0.
	for (int i = 4; i < 4 + 1 + 8; i++) {
		if (frame[i] != 0) {
			check(false, "silence has zero join bits and scale factors");
			break;
		}
	}
	// The CRC of that header: byte 1, bitpool, 8 join bits and 64 scale
	// factor bits, all zero after the first two bytes.
	uint8_t crcData[2 + 9] = { 0xbd, 53 };
	check(frame[3] == crc_independent(crcData, 16 + 8 + 64),
		"CRC of the silence frame");

	check(encoder.Encode(NULL, frame, 118) == 0, "short buffer refused");

	sbc::Config bad = config;
	bad.bitpool = 251;
	check(!encoder.Init(bad), "bitpool 251 refused");
	bad = config;
	bad.blocks = 10;
	check(!encoder.Init(bad), "10 blocks refused");
}


static double
round_trip(const sbc::Config& config, const char* name, bool print,
	double frequencyLeft = 1000.0, double frequencyRight = 1500.0)
{
	sbc::Encoder encoder;
	if (!encoder.Init(config)) {
		check(false, "round trip init");
		return 0;
	}
	const int channels = sbc::Channels(config);
	const int samplesPerFrame = encoder.FrameSamples();
	const int frames = 200;
	const double rate = sbc::SampleRate(config);

	std::vector<float> input((size_t)samplesPerFrame * frames * channels);
	for (int n = 0; n < samplesPerFrame * frames; n++) {
		double t = n / rate;
		input[(size_t)n * channels]
			= (float)(0.5 * sin(2 * M_PI * frequencyLeft * t)
				+ 0.2 * sin(2 * M_PI * 3 * frequencyLeft * t + 0.3));
		if (channels == 2) {
			input[(size_t)n * 2 + 1]
				= (float)(0.4 * sin(2 * M_PI * frequencyRight * t + 1.0)
					+ 0.15 * sin(2 * M_PI * 5130.0 * t));
		}
	}

	Decoder decoder;
	std::vector<double> output[2];
	uint8_t frame[1024];
	for (int f = 0; f < frames; f++) {
		size_t length = encoder.Encode(&input[(size_t)f * samplesPerFrame
			* channels], frame, sizeof(frame));
		sbc::Config parsed;
		size_t used = decoder.Decode(frame, length, output, &parsed);
		if (used != length || used == 0) {
			char what[96];
			snprintf(what, sizeof(what), "%s: frame %d decodes", name, f);
			check(false, what);
			return 0;
		}
		if (parsed.bitpool != config.bitpool || parsed.mode != config.mode) {
			check(false, "decoded header matches");
			return 0;
		}
	}

	// The filter bank delays the signal; find the delay, then the SNR.
	const int skip = 4 * samplesPerFrame;
	const int total = samplesPerFrame * frames;
	double best = -1e9;
	int bestDelay = 0;
	for (int delay = 0; delay < 120; delay++) {
		double signal = 0;
		double noise = 0;
		for (int ch = 0; ch < channels; ch++) {
			for (int n = skip; n < total - delay; n++) {
				double in = input[(size_t)n * channels + ch];
				double out = output[ch][n + delay];
				signal += in * in;
				noise += (in - out) * (in - out);
			}
		}
		double snr = 10 * log10(signal / (noise + 1e-30));
		if (snr > best) {
			best = snr;
			bestDelay = delay;
		}
	}
	if (print) {
		printf("sbc_test: %-36s %6.1f dB SNR (delay %d, %u kb/s)\n", name,
			best, bestDelay, sbc::BitRate(config) / 1000);
	}
	return best;
}


static void
test_round_trip()
{
	// The sign of the synthesis window: try both and keep the one that
	// reconstructs (the specification's decoder window is the analysis
	// prototype times -M; this confirms it).
	sbc::Config probe = { sbc::kFrequency44100, 16, sbc::kModeStereo,
		sbc::kAllocationLoudness, 8, 53 };
	Decoder::sDecoderSign = -1.0;
	double minus = round_trip(probe, "probe -M", false);
	Decoder::sDecoderSign = 1.0;
	double plus = round_trip(probe, "probe +M", false);
	printf("sbc_test: synthesis window -M: %.1f dB, +M: %.1f dB\n", minus,
		plus);
	Decoder::sDecoderSign = minus >= plus ? -1.0 : 1.0;

	struct Case {
		sbc::Config config;
		const char* name;
		double minimum;
	};
	const Case cases[] = {
		{ { sbc::kFrequency44100, 16, sbc::kModeJointStereo,
			sbc::kAllocationLoudness, 8, 53 },
			"44.1k joint 16/8 loudness bp53", 30.0 },
		{ { sbc::kFrequency44100, 16, sbc::kModeJointStereo,
			sbc::kAllocationLoudness, 8, 35 },
			"44.1k joint 16/8 loudness bp35", 20.0 },
		{ { sbc::kFrequency48000, 16, sbc::kModeJointStereo,
			sbc::kAllocationLoudness, 8, 51 },
			"48k joint 16/8 loudness bp51", 30.0 },
		{ { sbc::kFrequency44100, 16, sbc::kModeJointStereo,
			sbc::kAllocationLoudness, 8, 86 },
			"44.1k joint 16/8 loudness bp86", 40.0 },
		{ { sbc::kFrequency44100, 16, sbc::kModeStereo,
			sbc::kAllocationSNR, 8, 53 },
			"44.1k stereo 16/8 snr bp53", 30.0 },
		{ { sbc::kFrequency44100, 16, sbc::kModeDualChannel,
			sbc::kAllocationLoudness, 8, 38 },
			"44.1k dual 16/8 loudness bp38", 30.0 },
		{ { sbc::kFrequency44100, 16, sbc::kModeMono,
			sbc::kAllocationLoudness, 8, 31 },
			"44.1k mono 16/8 loudness bp31", 30.0 },
		{ { sbc::kFrequency32000, 8, sbc::kModeJointStereo,
			sbc::kAllocationLoudness, 4, 32 },
			"32k joint 8/4 loudness bp32", 25.0 },
		{ { sbc::kFrequency16000, 4, sbc::kModeMono,
			sbc::kAllocationSNR, 4, 20 },
			"16k mono 4/4 snr bp20", 20.0 },
		{ { sbc::kFrequency44100, 12, sbc::kModeStereo,
			sbc::kAllocationLoudness, 4, 60 },
			"44.1k stereo 12/4 loudness bp60", 30.0 },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		double snr = round_trip(cases[i].config, cases[i].name, true);
		char what[128];
		snprintf(what, sizeof(what), "%s SNR %.1f >= %.1f", cases[i].name, snr,
			cases[i].minimum);
		check(snr >= cases[i].minimum, what);
	}

	// A higher bitpool must never sound worse.
	double previous = -1e9;
	for (int bp = 20; bp <= 86; bp += 11) {
		sbc::Config config = { sbc::kFrequency44100, 16,
			sbc::kModeJointStereo, sbc::kAllocationLoudness, 8, bp };
		double snr = round_trip(config, "monotonic", false);
		if (snr + 0.5 < previous) {
			check(false, "SNR rises with the bitpool");
			break;
		}
		previous = snr;
	}

	// Full scale and clipped input stay decodable (scale factor 15).
	{
		sbc::Config config = { sbc::kFrequency44100, 16,
			sbc::kModeJointStereo, sbc::kAllocationLoudness, 8, 53 };
		sbc::Encoder encoder;
		encoder.Init(config);
		std::vector<float> loud(128 * 2);
		for (size_t i = 0; i < loud.size(); i++)
			loud[i] = (i / 2) % 2 ? 1.5f : -1.5f;
		Decoder decoder;
		std::vector<double> output[2];
		uint8_t frame[512];
		sbc::Config parsed;
		bool ok = true;
		for (int f = 0; f < 10 && ok; f++) {
			size_t length = encoder.Encode(&loud[0], frame, sizeof(frame));
			ok = decoder.Decode(frame, length, output, &parsed) == length;
		}
		check(ok, "clipped input decodes");
	}
}


static void
test_subband_placement()
{
	// A tone in the middle of subband k (f = (k + 0.5) * fs / 2M) gives the
	// largest scale factor in subband k.
	const int M = 8;
	sbc::Config config = { sbc::kFrequency48000, 16, sbc::kModeMono,
		sbc::kAllocationSNR, M, 60 };
	for (int k = 0; k < M; k++) {
		sbc::Encoder encoder;
		encoder.Init(config);
		double frequency = (k + 0.5) * 48000.0 / (2 * M);
		std::vector<float> tone(128);
		uint8_t frame[256];
		size_t length = 0;
		for (int f = 0; f < 8; f++) {
			for (int n = 0; n < 128; n++) {
				tone[n] = (float)(0.5 * sin(2 * M_PI * frequency
					* (f * 128 + n) / 48000.0));
			}
			length = encoder.Encode(&tone[0], frame, sizeof(frame));
		}
		check(length > 0, "tone frame");
		int best = -1;
		int bestValue = -1;
		for (int sb = 0; sb < M; sb++) {
			int value = (frame[4 + sb / 2] >> (sb % 2 ? 0 : 4)) & 0x0f;
			if (value > bestValue) {
				bestValue = value;
				best = sb;
			}
		}
		char what[64];
		snprintf(what, sizeof(what), "tone of subband %d lands in %d", k, best);
		check(best == k, what);
	}
}


int
main()
{
	test_table_4_7();
	test_crc();
	test_layout();
	test_subband_placement();
	test_round_trip();
	if (sFailures != 0) {
		printf("sbc_test: %d failures\n", sFailures);
		return 1;
	}
	printf("sbc_test: ok\n");
	return 0;
}
