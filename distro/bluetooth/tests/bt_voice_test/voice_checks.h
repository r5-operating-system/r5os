/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The checks of the voice path that need no radio (docs/BLUETOOTH.md,
 * step 7c), shared by bt_voice_test (in QEMU) and the host test
 * (scripts/test-voice.sh). The file that includes this one includes
 * msbc.h, h2sco.h and VoiceLink.h first, and defines
 *	void check(bool condition, const char* what);
 */
#ifndef R5_VOICE_CHECKS_H
#define R5_VOICE_CHECKS_H


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>


namespace voice_checks {


static uint32_t sState = 0x2545f491;


static inline uint32_t
random_word()
{
	sState ^= sState << 13;
	sState ^= sState >> 17;
	sState ^= sState << 5;
	return sState;
}


// The best alignment of "out" against "in" (the filter bank delays the
// signal), and the signal to noise ratio and gain there.
static inline void
compare(const std::vector<float>& in, const std::vector<float>& out,
	int channels, double* _snr, double* _gain, int* _delay)
{
	double bestSnr = -1000.0;
	double bestGain = 0.0;
	int bestDelay = 0;
	size_t frames = in.size() / channels;
	for (int delay = 0; delay < 400; delay++) {
		double signal = 0.0;
		double noise = 0.0;
		double cross = 0.0;
		double power = 0.0;
		// Skip the first 1000 frames: the filters start from silence.
		for (size_t i = 1000; i + delay < out.size() / channels
				&& i < frames; i++) {
			for (int ch = 0; ch < channels; ch++) {
				double a = in[i * channels + ch];
				double b = out[(i + delay) * channels + ch];
				signal += a * a;
				noise += (a - b) * (a - b);
				cross += a * b;
				power += a * a;
			}
		}
		if (signal <= 0.0)
			continue;
		double snr = 10.0 * log10(signal / (noise > 1e-20 ? noise : 1e-20));
		if (snr > bestSnr) {
			bestSnr = snr;
			bestGain = cross / power;
			bestDelay = delay;
		}
	}
	*_snr = bestSnr;
	*_gain = bestGain;
	*_delay = bestDelay;
}


static inline std::vector<float>
tone(int rate, int channels, size_t frames)
{
	std::vector<float> samples(frames * channels);
	for (size_t i = 0; i < frames; i++) {
		for (int ch = 0; ch < channels; ch++) {
			double frequency = ch == 0 ? 1000.0 : 440.0;
			samples[i * channels + ch]
				= (float)(0.4 * sin(2.0 * M_PI * frequency * i / rate)
					+ 0.1 * sin(2.0 * M_PI * 3100.0 * i / rate));
		}
	}
	return samples;
}


struct FrameSink {
	std::vector<std::vector<uint8_t> >	frames;
	int									lost;

	void operator()(const uint8_t* frame, int lostBefore)
	{
		frames.push_back(std::vector<uint8_t>(frame,
			frame + sbc::kMSBCFrameLength));
		lost += lostBefore;
	}
};


// mSBC: encode a tone, put it into H2 packets, cut those into the 24-byte
// SCO payloads of a USB radio, find the frames again and decode them.
static inline void
check_msbc_round_trip()
{
	sbc::Encoder encoder;
	check(encoder.InitMSBC(), "mSBC encoder starts");
	check(encoder.FrameLength() == sbc::kMSBCFrameLength,
		"an mSBC frame is 57 bytes");
	check(encoder.FrameSamples() == sbc::kMSBCFrameSamples,
		"an mSBC frame has 120 samples");

	const int frames = 200;
	std::vector<float> input = tone(16000, 1, frames * 120);
	std::vector<uint8_t> stream;
	bool headerOK = true;
	for (int i = 0; i < frames; i++) {
		uint8_t frame[sbc::kMSBCFrameLength];
		size_t length = encoder.Encode(&input[i * 120], frame, sizeof(frame));
		if (length != sbc::kMSBCFrameLength || frame[0] != 0xad
			|| frame[1] != 0 || frame[2] != 0)
			headerOK = false;
		uint8_t packet[sbc::kMSBCPacketLength];
		sbc::MSBCPacket(frame, i, packet);
		stream.insert(stream.end(), packet, packet + sizeof(packet));
	}
	check(headerOK, "mSBC frames carry 0xad and two zero bytes");

	sbc::MSBCDeframer deframer;
	FrameSink sink;
	sink.lost = 0;
	for (size_t i = 0; i < stream.size(); i += 24) {
		size_t count = stream.size() - i < 24 ? stream.size() - i : 24;
		deframer.Feed(&stream[i], count, sink);
	}
	check(sink.frames.size() == (size_t)frames,
		"every mSBC frame is found in 24-byte SCO payloads");
	check(sink.lost == 0 && deframer.Skipped() == 0,
		"no frame is lost and no byte skipped");

	sbc::Decoder decoder;
	std::vector<float> output;
	bool decoded = true;
	for (size_t i = 0; i < sink.frames.size(); i++) {
		float samples[120];
		size_t length = 0;
		int channels = 0;
		int count = 0;
		if (decoder.Decode(&sink.frames[i][0], sink.frames[i].size(), samples,
				120, &length, &channels, &count) != sbc::kDecodeOK
			|| length != 57 || channels != 1 || count != 120) {
			decoded = false;
			break;
		}
		output.insert(output.end(), samples, samples + 120);
	}
	check(decoded, "every mSBC frame decodes");

	double snr;
	double gain;
	int delay;
	compare(input, output, 1, &snr, &gain, &delay);
	printf("voice: mSBC round trip %.1f dB, gain %.3f, delay %d samples\n",
		snr, gain, delay);
	check(snr > 20.0, "mSBC round trip is above 20 dB");
	check(gain > 0.9 && gain < 1.1, "mSBC round trip keeps the level");

	// A lost packet shows up in the sequence numbers.
	std::vector<uint8_t> gap(stream.begin(), stream.begin() + 3 * 60);
	gap.insert(gap.end(), stream.begin() + 4 * 60, stream.begin() + 8 * 60);
	sbc::MSBCDeframer deframer2;
	FrameSink sink2;
	sink2.lost = 0;
	deframer2.Feed(&gap[0], gap.size(), sink2);
	check(sink2.frames.size() == 7 && sink2.lost == 1,
		"a lost mSBC packet is counted");

	// A bit error in the scale factors is refused by the CRC.
	std::vector<uint8_t> broken = sink.frames[10];
	broken[5] ^= 0x10;
	float samples[120];
	size_t length;
	int channels;
	int count;
	check(decoder.Decode(&broken[0], broken.size(), samples, 120, &length,
		&channels, &count) == sbc::kDecodeBadCRC,
		"a damaged mSBC frame is refused (CRC)");
	check(decoder.Decode(&broken[0], 30, samples, 120, &length, &channels,
		&count) == sbc::kDecodeShort, "a short mSBC frame is refused");
	check(decoder.Decode(&sink.frames[10][0], 57, samples, 100, &length,
		&channels, &count) == sbc::kDecodeShort,
		"a frame that does not fit the output is refused");
}


// SBC as A2DP uses it, through the same decoder.
static inline void
check_sbc_round_trip(int frequency, int mode, int blocks, int subbands,
	int bitpool, double minimum)
{
	sbc::Config config;
	config.frequency = frequency;
	config.blocks = blocks;
	config.mode = mode;
	config.allocation = sbc::kAllocationLoudness;
	config.subbands = subbands;
	config.bitpool = bitpool;
	sbc::Encoder encoder;
	check(encoder.Init(config), "SBC encoder starts");
	int channels = sbc::Channels(config);
	int rate = sbc::SampleRate(config);
	int frameSamples = encoder.FrameSamples();
	const int frames = 300;
	std::vector<float> input = tone(rate, channels, frames * frameSamples);

	sbc::Decoder decoder;
	std::vector<float> output;
	bool decoded = true;
	for (int i = 0; i < frames; i++) {
		uint8_t frame[1024];
		size_t length = encoder.Encode(&input[i * frameSamples * channels],
			frame, sizeof(frame));
		std::vector<float> samples(frameSamples * channels);
		size_t read = 0;
		int gotChannels = 0;
		int count = 0;
		if (decoder.Decode(frame, length, &samples[0], samples.size(), &read,
				&gotChannels, &count) != sbc::kDecodeOK || read != length
			|| gotChannels != channels || count != frameSamples) {
			decoded = false;
			break;
		}
		output.insert(output.end(), samples.begin(), samples.end());
	}
	char what[160];
	snprintf(what, sizeof(what), "SBC %d Hz, mode %d, %d blocks, %d "
		"subbands, bitpool %d decodes", rate, mode, blocks, subbands,
		bitpool);
	check(decoded, what);

	double snr;
	double gain;
	int delay;
	compare(input, output, channels, &snr, &gain, &delay);
	printf("voice: SBC %d Hz mode %d bitpool %d round trip %.1f dB, gain "
		"%.3f, delay %d\n", rate, mode, bitpool, snr, gain, delay);
	snprintf(what, sizeof(what), "SBC %d Hz, mode %d round trip is above "
		"%.0f dB", rate, mode, minimum);
	check(snr > minimum && gain > 0.9 && gain < 1.1, what);
}


// Random bytes never crash the decoder or the deframer.
static inline void
check_garbage()
{
	sbc::Decoder decoder;
	sbc::MSBCDeframer deframer;
	FrameSink sink;
	sink.lost = 0;
	int good = 0;
	for (int round = 0; round < 20000; round++) {
		uint8_t data[300];
		size_t size = random_word() % sizeof(data);
		for (size_t i = 0; i < size; i++)
			data[i] = (uint8_t)random_word();
		if (size > 0)
			data[0] = (random_word() & 1) != 0 ? 0x9c : 0xad;
		float samples[2 * 128];
		size_t length;
		int channels;
		int count;
		if (decoder.Decode(data, size, samples, 256, &length, &channels,
				&count) == sbc::kDecodeOK)
			good++;
		deframer.Feed(data, size, sink);
	}
	check(true, "random frames do not crash the decoder or the deframer");
	printf("voice: %d of 20000 random frames passed the CRC\n", good);
}


struct PacketSink {
	std::vector<std::vector<uint8_t> >	packets;

	void operator()(const uint8* packet, size_t size)
	{
		packets.push_back(std::vector<uint8_t>(packet, packet + size));
	}
};


struct EventSink {
	std::vector<h2_voice_event>	events;

	void operator()(const h2_voice_event& event)
	{
		events.push_back(event);
	}
};


// The kernel side of the voice path (h2sco.h).
static inline void
check_kernel_parsers()
{
	// Synchronous Connection Complete: eSCO, handle 0x0123, transparent,
	// 60-byte packets; then an unrelated event; then its disconnection.
	const uint8 stream[] = {
		0x0e, 0x04, 0x01, 0x28, 0x04, 0x00,
		0x2c, 0x11, 0x00, 0x23, 0x01, 1, 2, 3, 4, 5, 6, 0x02, 0x0c, 0x06,
			0x3c, 0x00, 0x3c, 0x00, 0x03,
		0x13, 0x05, 0x01, 0x23, 0x01, 0x01, 0x00,
		0x05, 0x04, 0x00, 0x23, 0x01, 0x13
	};
	bool same = true;
	for (size_t cut = 1; cut < sizeof(stream); cut++) {
		h2_voice_events events;
		h2_voice_events_reset(&events);
		EventSink sink;
		h2_voice_events_feed(&events, stream, cut, sink);
		h2_voice_events_feed(&events, stream + cut, sizeof(stream) - cut,
			sink);
		if (sink.events.size() != 2
			|| sink.events[0].kind != H2_VOICE_LINK_UP
			|| sink.events[0].handle != 0x0123
			|| sink.events[0].linkType != H2_LINK_ESCO
			|| sink.events[0].airMode != H2_AIR_MODE_TRANSPARENT
			|| sink.events[0].rxLength != 60
			|| sink.events[1].kind != H2_VOICE_LINK_DOWN
			|| sink.events[1].handle != 0x0123)
			same = false;
	}
	check(same, "a voice link is seen however the events are cut");

	h2_voice_event event;
	const uint8 failed[] = { 0x2c, 0x11, 0x0d, 0x23, 0x01, 1, 2, 3, 4, 5, 6,
		0x02, 0x0c, 0x06, 0x3c, 0x00, 0x3c, 0x00, 0x03 };
	check(!h2_voice_parse_event(failed, sizeof(failed), &event),
		"a failed voice link starts nothing");
	const uint8 acl[] = { 0x2c, 0x11, 0x00, 0x23, 0x01, 1, 2, 3, 4, 5, 6,
		0x01, 0x0c, 0x06, 0x3c, 0x00, 0x3c, 0x00, 0x03 };
	check(!h2_voice_parse_event(acl, sizeof(acl), &event),
		"a link type that is not SCO or eSCO starts nothing");

	// Alternate settings of a usual radio: 0, 9, 17, 25, 33, 49, 63.
	const uint16 sizes[] = { 0, 9, 17, 25, 33, 49, 63 };
	check(h2_voice_choose_alt(sizes, sizes, 7, H2_VOICE_BYTES_8BIT) == 1,
		"mSBC takes alternate setting 1");
	check(h2_voice_choose_alt(sizes, sizes, 7, H2_VOICE_BYTES_16BIT) == 2,
		"CVSD takes alternate setting 2");
	const uint16 none[] = { 0, 0 };
	check(h2_voice_choose_alt(none, none, 2, 9) == -1,
		"no voice endpoint, no alternate setting");
	const uint16 huge[] = { 0, 1023 };
	check(h2_voice_choose_alt(huge, huge, 2, 9) == -1,
		"an endpoint above 64 bytes is not used");

	// SCO packets of 3 + 24 bytes in 9-byte isochronous packets.
	h2_sco_assembler assembler;
	h2_sco_assembler_reset(&assembler);
	PacketSink packets;
	for (int p = 0; p < 4; p++) {
		uint8 sco[27];
		sco[0] = 0x23;
		sco[1] = 0x01;
		sco[2] = 24;
		for (int i = 0; i < 24; i++)
			sco[3 + i] = (uint8)(p * 24 + i);
		for (int i = 0; i < 3; i++) {
			// The second packet loses its middle piece.
			bool ok = !(p == 1 && i == 1);
			h2_sco_assemble(&assembler, 0x0123, sco + 9 * i, 9, ok, packets);
		}
	}
	check(packets.packets.size() == 3 && packets.packets[1][3] == 48,
		"SCO packets are put together, a broken one is dropped");
	PacketSink other;
	const uint8 foreign[9] = { 0x24, 0x01, 6, 1, 2, 3, 4, 5, 6 };
	h2_sco_assemble(&assembler, 0x0123, foreign, sizeof(foreign), true,
		other);
	check(other.packets.empty(), "a packet of another handle is dropped");

	h2_sco_ring* ring = new h2_sco_ring;
	h2_sco_ring_reset(ring);
	uint8 packet[H2_SCO_MAX_PACKET];
	memset(packet, 0, sizeof(packet));
	for (int i = 0; i < H2_SCO_RING_SLOTS + 5; i++) {
		packet[3] = (uint8)i;
		h2_sco_ring_push(ring, packet, 27);
	}
	uint8 out[H2_SCO_MAX_PACKET];
	check(h2_sco_ring_pop(ring, out, 10) == -1,
		"a small read buffer is refused");
	check(h2_sco_ring_pop(ring, out, sizeof(out)) == 27 && out[3] == 5
		&& ring->dropped == 5, "a full ring drops the oldest packets");
	check(!h2_sco_ring_push(ring, packet, H2_SCO_MAX_PACKET + 1),
		"an oversized packet is refused");
	delete ring;

	uint8 tx[27] = { 0x23, 0x01, 24 };
	check(h2_sco_tx_check(tx, sizeof(tx), 0x0123), "a whole SCO packet goes");
	check(!h2_sco_tx_check(tx, 26, 0x0123), "a cut SCO packet is refused");
	check(!h2_sco_tx_check(tx, sizeof(tx), 0x0124),
		"a packet for another handle is refused");
	tx[1] = 0x11;
	check(!h2_sco_tx_check(tx, sizeof(tx), 0x0123),
		"a packet with status bits set is refused");
}


// The HCI commands of the Audio Gateway (VoiceLink.h).
static inline void
check_voice_link()
{
	check(voice::ChooseCodec("1,2", true) == voice::kCodecMSBC,
		"AT+BAC=1,2 chooses mSBC");
	check(voice::ChooseCodec("1", true) == voice::kCodecCVSD,
		"AT+BAC=1 chooses CVSD");
	check(voice::ChooseCodec("1,2", false) == voice::kCodecCVSD,
		"without mSBC on this side, CVSD");
	check(voice::ChooseCodec("12,x", true) == voice::kCodecCVSD,
		"a broken AT+BAC list falls back to CVSD");

	voice::Parameters parameters;
	check(voice::GetParameters(voice::kSettingT2, &parameters),
		"setting T2 exists");
	uint8 command[voice::kSetupSize];
	size_t size = voice::BuildSetup(0x0041, parameters, command,
		sizeof(command));
	const uint8 expected[] = { 0x28, 0x04, 17, 0x41, 0x00, 0x40, 0x1f, 0, 0,
		0x40, 0x1f, 0, 0, 0x0d, 0x00, 0x63, 0x00, 0x02, 0x88, 0x03 };
	check(size == sizeof(expected)
		&& memcmp(command, expected, sizeof(expected)) == 0,
		"Setup Synchronous Connection with T2 has the bytes of HFP 1.8");

	voice::GetParameters(voice::kSettingD1, &parameters);
	const uint8 address[6] = { 1, 2, 3, 4, 5, 6 };
	uint8 accept[voice::kAcceptSize];
	size = voice::BuildAccept(address, parameters, accept, sizeof(accept));
	check(size == voice::kAcceptSize && accept[0] == 0x29 && accept[2] == 21
		&& accept[19] == 0x60 && accept[21] == 0xff && accept[22] == 0xc4
		&& accept[23] == 0x03,
		"Accept Synchronous Connection Request with D1");
	check(voice::BuildSetup(0x41, parameters, command, 5) == 0,
		"a small command buffer is refused");

	voice::Setting settings[4];
	check(voice::Fallbacks(voice::kCodecMSBC, true, settings, 4) == 2
		&& settings[0] == voice::kSettingT2,
		"mSBC tries T2, then T1");
	check(voice::Fallbacks(voice::kCodecCVSD, false, settings, 4) == 1
		&& settings[0] == voice::kSettingD1,
		"CVSD without eSCO uses D1");
}


static inline void
run_all()
{
	check_msbc_round_trip();
	check_sbc_round_trip(sbc::kFrequency16000, sbc::kModeMono, 16, 8, 26,
		20.0);
	check_sbc_round_trip(sbc::kFrequency44100, sbc::kModeJointStereo, 16, 8,
		53, 25.0);
	check_sbc_round_trip(sbc::kFrequency48000, sbc::kModeStereo, 8, 4, 40,
		15.0);
	check_garbage();
	check_kernel_parsers();
	check_voice_link();
}


}	// namespace voice_checks


#endif	// R5_VOICE_CHECKS_H
