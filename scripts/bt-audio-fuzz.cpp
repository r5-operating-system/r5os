/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the bluetooth_audio packet code
 * (distro/media/bluetooth_audio/a2dp_proto.h) and the SBC encoder, built
 * with AddressSanitizer and UBSan by scripts/fuzz-bt.sh (target "audio").
 * Every packet lives in a heap buffer of exactly its length, so a read
 * past the end is an ASan report. Checked besides "no fault":
 *  - a configuration chosen from any capabilities is a valid SBC setting
 *    inside the headset's bitpool range, A2DP's bit rate limit and one
 *    media packet, and encodes into exactly FrameLength() bytes;
 *  - Set Configuration built by R5 parses back to the same setting;
 *  - AVCTP, AV/C, PASS THROUGH and vendor dependent parsers stay inside
 *    the packet, and the vendor builder fills exactly what it returns.
 * Prints "audio_fuzz: <seconds>s 0 crashes" when it ends.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "a2dp_proto.h"


using namespace a2dp;


static uint64_t gState = 0x2545f4914f6cdd1dULL;


static uint32_t
next_word()
{
	gState ^= gState << 13;
	gState ^= gState >> 7;
	gState ^= gState << 17;
	return (uint32_t)gState;
}


static uint8_t
next_byte()
{
	return (uint8_t)next_word();
}


static void
check(bool condition, const char* what)
{
	if (!condition) {
		fprintf(stderr, "audio_fuzz: check failed: %s\n", what);
		abort();
	}
}


// A heap copy of exactly "size" bytes (size 0 gives a 1-byte buffer the
// parsers must not read).
struct Packet {
	uint8_t*	data;
	size_t		size;

	Packet(const uint8_t* source, size_t _size)
		:
		data((uint8_t*)malloc(_size > 0 ? _size : 1)),
		size(_size)
	{
		if (_size > 0)
			memcpy(data, source, _size);
	}

	~Packet()
	{
		free(data);
	}
};


static void
random_bytes(uint8_t* out, size_t size)
{
	for (size_t i = 0; i < size; i++)
		out[i] = next_byte();
}


// A capability list that is mostly well-formed: the categories R5 knows,
// with an SBC element whose bits and bitpools are random.
static size_t
make_capabilities(uint8_t* out, size_t size)
{
	size_t i = 0;
	int entries = 1 + next_word() % 5;
	for (int e = 0; e < entries && i + 2 <= size; e++) {
		uint32_t kind = next_word() % 10;
		if (kind < 4 && i + 8 <= size) {
			out[i++] = kCategoryMediaCodec;
			out[i++] = 6;
			out[i++] = (next_word() % 8 == 0) ? next_byte() : 0;
			out[i++] = (next_word() % 8 == 0) ? next_byte() : kCodecSBC;
			out[i++] = next_byte();
			out[i++] = next_byte();
			out[i++] = next_byte();
			out[i++] = next_byte();
		} else if (kind < 6) {
			out[i++] = kCategoryMediaTransport;
			out[i++] = 0;
		} else if (kind < 7) {
			out[i++] = kCategoryDelayReporting;
			out[i++] = 0;
		} else {
			uint8_t length = (uint8_t)(next_word() % 12);
			out[i++] = (uint8_t)(next_word() % 12);
			out[i++] = length;
			for (uint8_t b = 0; b < length && i < size; b++)
				out[i++] = next_byte();
		}
	}
	return i;
}


static void
check_chosen(const SBCCapabilities& caps, int rate)
{
	sbc::Config config;
	if (!ChooseConfiguration(caps, rate, &config))
		return;
	check(sbc::ValidConfig(config), "chosen setting is valid SBC");
	check(config.bitpool >= caps.minBitpool, "bitpool >= headset minimum");
	check(config.bitpool <= caps.maxBitpool, "bitpool <= headset maximum");
	if (config.bitpool > caps.minBitpool) {
		uint32_t limit = config.mode == sbc::kModeMono ? kMaxBitRateMono
			: kMaxBitRateStereo;
		check(sbc::BitRate(config) <= limit, "A2DP bit rate limit");
		check(sbc::FrameLength(config) + kMediaHeaderLength <= kMediaMTU,
			"one frame fits a media packet");
	}

	// Set Configuration from this setting parses back to it.
	uint8_t built[16];
	size_t length = BuildConfiguration(built, sizeof(built), config,
		caps.delayReporting);
	check(length > 0, "configuration builds");
	Packet packet(built, length);
	SBCCapabilities back;
	uint8_t bad;
	check(ParseCapabilities(packet.data, packet.size, &back, &bad) == 0,
		"built configuration parses");
	sbc::Config again;
	check(ConfigFromCapabilities(back, &again), "built configuration is one");
	check(again.frequency == config.frequency && again.mode == config.mode
		&& again.blocks == config.blocks && again.subbands == config.subbands
		&& again.allocation == config.allocation, "same SBC fields");
	check(back.delayReporting == caps.delayReporting, "same delay reporting");

	// One frame of random audio into a buffer of exactly FrameLength().
	sbc::Encoder encoder;
	check(encoder.Init(config), "encoder takes the setting");
	std::vector<float> samples((size_t)encoder.FrameSamples()
		* sbc::Channels(config));
	for (size_t s = 0; s < samples.size(); s++) {
		samples[s] = ((int32_t)next_word() / 2147483648.0f)
			* (next_word() % 4 == 0 ? 4.0f : 1.0f);
	}
	size_t frameLength = encoder.FrameLength();
	uint8_t* frame = (uint8_t*)malloc(frameLength);
	check(encoder.Encode(&samples[0], frame, frameLength) == frameLength,
		"frame is FrameLength() bytes");
	check(frame[0] == sbc::kSyncWord, "sync word");
	free(frame);
}


static void
fuzz_avdtp_round()
{
	uint8_t buffer[96];

	// Signalling header and Discover on random bytes.
	size_t size = next_word() % sizeof(buffer);
	random_bytes(buffer, size);
	{
		Packet packet(buffer, size);
		SignalHeader header;
		if (ParseSignalHeader(packet.data, packet.size, &header))
			check(header.length <= packet.size, "header inside the packet");
		EndpointInfo endpoints[8];
		int count = ParseDiscover(packet.data, packet.size, endpoints, 8);
		check(count <= 8, "discover count bounded");
	}

	// Capabilities: random bytes, then mostly well-formed lists.
	for (int pass = 0; pass < 2; pass++) {
		size = pass == 0 ? next_word() % sizeof(buffer)
			: make_capabilities(buffer, sizeof(buffer));
		if (pass == 0)
			random_bytes(buffer, size);
		Packet packet(buffer, size);
		SBCCapabilities caps;
		uint8_t bad;
		uint8_t error = ParseCapabilities(packet.data, packet.size, &caps,
			&bad);
		if (error != 0)
			continue;
		sbc::Config config;
		if (ConfigFromCapabilities(caps, &config))
			check(sbc::ValidConfig(config), "headset setting is valid SBC");
		static const int kRates[3] = { 44100, 48000, 32000 };
		check_chosen(caps, kRates[next_word() % 3]);
	}

	// Our own capabilities always parse.
	size_t length = BuildSourceCapabilities(buffer, sizeof(buffer),
		next_word() & 1);
	Packet own(buffer, length);
	SBCCapabilities caps;
	uint8_t bad;
	check(ParseCapabilities(own.data, own.size, &caps, &bad) == 0
		&& caps.sbc && caps.mediaTransport, "own capabilities parse");
	check(BuildSourceCapabilities(buffer, next_word() % 10, true) == 0,
		"short buffer refused");

	// The media header.
	uint8_t header[kMediaHeaderLength];
	int frames = (int)(next_word() % 17);
	size_t built = BuildMediaHeader(header, sizeof(header),
		(uint16_t)next_word(), next_word(), next_word(), frames);
	check((frames >= 1 && frames <= 15) == (built == kMediaHeaderLength),
		"media header frame count");
}


static void
fuzz_avrcp_round()
{
	uint8_t buffer[128];
	size_t size = next_word() % sizeof(buffer);
	random_bytes(buffer, size);
	// Often a plausible AVCTP header and AV/C opcode.
	if (size >= 6 && (next_word() & 1)) {
		buffer[0] &= 0xf3;
		buffer[1] = 0x11;
		buffer[2] = 0x0e;
		static const uint8_t kOpcodes[4] = { kOpcodeVendorDependent,
			kOpcodePassThrough, kOpcodeUnitInfo, kOpcodeSubunitInfo };
		buffer[5] = kOpcodes[next_word() % 4];
		if (buffer[5] == kOpcodeVendorDependent && size >= 13) {
			buffer[6] = 0x00;
			buffer[7] = 0x19;
			buffer[8] = 0x58;
			buffer[10] &= 0xfc;
		}
	}

	Packet packet(buffer, size);
	AVCTPHeader header;
	if (!ParseAVCTP(packet.data, packet.size, &header))
		return;
	AVCFrame frame;
	if (!ParseAVC(packet.data + 3, packet.size - 3, &frame))
		return;
	check(frame.operands + frame.operandLength == packet.data + packet.size,
		"AV/C operands end with the packet");
	PassThrough pass;
	if (ParsePassThrough(frame, &pass))
		check(pass.operation < 0x80, "operation id is 7 bits");
	VendorPDU pdu;
	if (ParseVendor(frame, &pdu)) {
		check(pdu.parameters + pdu.length <= packet.data + packet.size,
			"vendor parameters inside the packet");
		// Read every parameter byte, as the event list loop does.
		volatile uint8_t sum = 0;
		for (uint16_t i = 0; i < pdu.length; i++)
			sum += pdu.parameters[i];
		(void)sum;
	}

	// The builder: exact output size, short buffers refused.
	uint8_t parameters[32];
	uint16_t length = (uint16_t)(next_word() % sizeof(parameters));
	random_bytes(parameters, length);
	size_t room = next_word() % 64;
	uint8_t* out = (uint8_t*)malloc(room > 0 ? room : 1);
	size_t built = BuildVendor(out, room, next_byte(), next_word() & 1,
		next_byte() & 0x0f, next_byte(), parameters, length);
	check(built == 0 || built == 13u + length, "vendor length");
	check(built <= room, "vendor fits");
	if (built > 0) {
		Packet back(out, built);
		AVCTPHeader h;
		AVCFrame f;
		VendorPDU p;
		check(ParseAVCTP(back.data, back.size, &h)
			&& ParseAVC(back.data + 3, back.size - 3, &f)
			&& ParseVendor(f, &p) && p.length == length,
			"built vendor message parses");
	}
	free(out);

	check(VolumeFromGain(GainFromVolume(next_byte())) <= 0x7f,
		"volume stays 7 bits");
}


static bool
seconds_left(time_t start, int seconds)
{
	return time(NULL) - start < seconds;
}


int
main(int argc, char** argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: bt-audio-fuzz <seconds>\n");
		return 2;
	}
	int seconds = atoi(argv[1]);
	time_t start = time(NULL);
	uint64_t rounds = 0;
	while (seconds_left(start, seconds)) {
		fuzz_avdtp_round();
		fuzz_avrcp_round();
		rounds++;
	}
	printf("audio_fuzz: %ds 0 crashes (%llu rounds)\n", seconds,
		(unsigned long long)rounds);
	return 0;
}
