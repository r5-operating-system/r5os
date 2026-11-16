/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the SCO packet path of step 7c (docs/BLUETOOTH.md),
 * built with AddressSanitizer and UBSan by scripts/fuzz-bt.sh (target sco):
 *  - h2generic/h2sco.h: the voice event watcher on random and well-formed
 *    event streams cut anywhere, the alternate setting choice, the SCO
 *    reassembly from random isochronous packets (sizes, errors, handles),
 *    the packet ring and the transmit check;
 *  - bluetooth_audio: the mSBC deframer on random SCO payloads with real
 *    headers mixed in, and the SBC decoder on random and on damaged real
 *    frames.
 * Usage: bt-sco-fuzz <seconds>. Prints "sco_fuzz: <seconds>s 0 crashes".
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "h2sco.h"
#include "msbc.h"


static uint64_t gState = 0x9e3779b97f4a7c15ULL;


static uint32_t
next_word()
{
	gState ^= gState << 13;
	gState ^= gState >> 7;
	gState ^= gState << 17;
	return (uint32_t)gState;
}


static void
check(bool condition, const char* what)
{
	if (!condition) {
		fprintf(stderr, "bt-sco-fuzz: check failed: %s\n", what);
		abort();
	}
}


struct EventSink {
	int		up;
	int		down;

	void operator()(const h2_voice_event& event)
	{
		check(event.kind == H2_VOICE_LINK_UP
			|| event.kind == H2_VOICE_LINK_DOWN, "event kind");
		check(event.handle <= H2_HANDLE_MASK, "handle mask");
		if (event.kind == H2_VOICE_LINK_UP)
			up++;
		else
			down++;
	}
};


struct PacketSink {
	uint16			handle;
	h2_sco_ring*	ring;
	int				count;

	void operator()(const uint8* packet, size_t size)
	{
		check(size >= H2_SCO_HEADER_SIZE && size <= H2_SCO_MAX_PACKET,
			"packet size");
		check((size_t)packet[2] + H2_SCO_HEADER_SIZE == size,
			"packet length field");
		check((h2_le16(packet) & H2_HANDLE_MASK) == handle, "packet handle");
		h2_sco_ring_push(ring, packet, size);
		count++;
	}
};


struct FrameSink {
	sbc::Decoder*	decoder;
	int				frames;

	void operator()(const uint8_t* frame, int lost)
	{
		check(lost >= 0 && lost <= 3, "lost count");
		float samples[sbc::kMSBCFrameSamples];
		size_t length;
		int channels;
		int count;
		decoder->Decode(frame, sbc::kMSBCFrameLength, samples,
			sbc::kMSBCFrameSamples, &length, &channels, &count);
		frames++;
	}
};


static void
fuzz_events()
{
	// Well-formed voice events with random fields between random events.
	std::vector<uint8> stream;
	int expectedUp = 0;
	int expectedDown = 0;
	int events = 1 + next_word() % 12;
	for (int e = 0; e < events; e++) {
		uint32 kind = next_word() % 3;
		if (kind == 0) {
			uint8 event[19];
			event[0] = H2_EVENT_SYNC_CONNECTION_COMPLETE;
			event[1] = 17;
			for (int i = 2; i < 19; i++)
				event[i] = (uint8)next_word();
			if (next_word() & 1) {
				event[2] = 0;
				event[11] = (next_word() & 1) != 0 ? H2_LINK_ESCO : H2_LINK_SCO;
			}
			if (event[2] == 0 && (event[11] == 0 || event[11] == 2))
				expectedUp++;
			stream.insert(stream.end(), event, event + 19);
		} else if (kind == 1) {
			uint8 event[6] = { H2_EVENT_DISCONNECTION_COMPLETE, 4,
				(uint8)((next_word() & 1) != 0 ? 0 : next_word()),
				(uint8)next_word(), (uint8)next_word(), (uint8)next_word() };
			if (event[2] == 0)
				expectedDown++;
			stream.insert(stream.end(), event, event + 6);
		} else {
			uint8 length = (uint8)next_word();
			uint8 code = (uint8)next_word();
			// Not a voice event of the right length.
			if ((code == H2_EVENT_SYNC_CONNECTION_COMPLETE && length == 17)
				|| (code == H2_EVENT_DISCONNECTION_COMPLETE && length == 4))
				length++;
			stream.push_back(code);
			stream.push_back(length);
			for (int i = 0; i < length; i++)
				stream.push_back((uint8)next_word());
		}
	}

	h2_voice_events watcher;
	h2_voice_events_reset(&watcher);
	EventSink sink = { 0, 0 };
	size_t at = 0;
	while (at < stream.size()) {
		size_t piece = 1 + next_word() % 70;
		if (piece > stream.size() - at)
			piece = stream.size() - at;
		h2_voice_events_feed(&watcher, &stream[at], piece, sink);
		at += piece;
	}
	check(sink.up == expectedUp && sink.down == expectedDown,
		"every voice event is seen once, however it is cut");

	// Random bytes.
	uint8 noise[512];
	size_t size = next_word() % sizeof(noise);
	for (size_t i = 0; i < size; i++)
		noise[i] = (uint8)next_word();
	h2_voice_events_feed(&watcher, noise, size, sink);

	uint16 in[H2_VOICE_MAX_ALTS + 4];
	uint16 out[H2_VOICE_MAX_ALTS + 4];
	int count = next_word() % (H2_VOICE_MAX_ALTS + 4);
	for (int i = 0; i < count; i++) {
		in[i] = (uint16)(next_word() % 1100);
		out[i] = (uint16)(next_word() % 1100);
	}
	uint16 need = (next_word() & 1) != 0 ? 9 : 17;
	int alt = h2_voice_choose_alt(in, out, count, need);
	check(alt == -1 || (alt >= 1 && alt < count && alt < H2_VOICE_MAX_ALTS
		&& in[alt] >= need && out[alt] >= need
		&& in[alt] <= H2_VOICE_MAX_ISO_PACKET), "alternate setting choice");
}


static void
fuzz_sco(h2_sco_ring* ring)
{
	h2_sco_assembler assembler;
	h2_sco_assembler_reset(&assembler);
	h2_sco_ring_reset(ring);
	uint16 handle = (uint16)(next_word() & H2_HANDLE_MASK);
	PacketSink sink = { handle, ring, 0 };

	int packets = next_word() % 64;
	for (int p = 0; p < packets; p++) {
		uint8 data[H2_VOICE_MAX_ISO_PACKET * 2];
		size_t size = next_word() % sizeof(data);
		uint32 mode = next_word() % 4;
		for (size_t i = 0; i < size; i++)
			data[i] = (uint8)next_word();
		if (mode < 2 && size >= 3) {
			// A header with the right handle and a random length.
			data[0] = (uint8)handle;
			data[1] = (uint8)((handle >> 8) | (next_word() & 0x30));
		}
		h2_sco_assemble(&assembler, handle, data, size, mode != 3, sink);
	}

	uint8 out[H2_SCO_MAX_PACKET];
	while (true) {
		size_t capacity = (next_word() & 7) == 0 ? next_word() % 300
			: sizeof(out);
		int size = h2_sco_ring_pop(ring, out, capacity);
		if (size == 0)
			break;
		if (size < 0) {
			check(capacity < H2_SCO_MAX_PACKET, "pop refuses only when small");
			continue;
		}
		check((size_t)size <= capacity, "pop fits");
		check(h2_sco_tx_check(out, size, handle)
			|| (out[1] >> 4) != 0, "a reassembled packet passes the tx check");
	}

	uint8 tx[300];
	size_t size = next_word() % sizeof(tx);
	for (size_t i = 0; i < size; i++)
		tx[i] = (uint8)next_word();
	if (h2_sco_tx_check(tx, size, handle)) {
		check(size == (size_t)tx[2] + 3 && size <= H2_SCO_MAX_PACKET,
			"tx check bounds");
	}
}


static void
fuzz_msbc(const std::vector<std::vector<uint8_t> >& frames)
{
	sbc::Decoder decoder;
	sbc::MSBCDeframer deframer;
	FrameSink sink = { &decoder, 0 };

	std::vector<uint8_t> stream;
	int pieces = next_word() % 20;
	for (int p = 0; p < pieces; p++) {
		uint32 mode = next_word() % 4;
		if (mode == 0) {
			size_t size = next_word() % 80;
			for (size_t i = 0; i < size; i++)
				stream.push_back((uint8_t)next_word());
		} else {
			uint8_t packet[sbc::kMSBCPacketLength];
			std::vector<uint8_t> frame = frames[next_word() % frames.size()];
			if (mode == 3)
				frame[next_word() % frame.size()] ^= (uint8_t)next_word();
			sbc::MSBCPacket(&frame[0], next_word() % 4, packet);
			size_t size = sizeof(packet);
			if ((next_word() & 7) == 0)
				size = next_word() % sizeof(packet);
			stream.insert(stream.end(), packet, packet + size);
		}
	}
	size_t at = 0;
	while (at < stream.size()) {
		size_t piece = 1 + next_word() % 60;
		if (piece > stream.size() - at)
			piece = stream.size() - at;
		deframer.Feed(&stream[at], piece, sink);
		at += piece;
	}

	// The decoder alone, on random data behind both sync words.
	uint8_t data[600];
	size_t size = next_word() % sizeof(data);
	for (size_t i = 0; i < size; i++)
		data[i] = (uint8_t)next_word();
	if (size > 0)
		data[0] = (next_word() & 1) != 0 ? 0x9c : 0xad;
	float samples[2 * 16 * 8];
	size_t capacity = next_word() % (sizeof(samples) / sizeof(float) + 1);
	size_t length = 0;
	int channels = 0;
	int count = 0;
	if (decoder.Decode(data, size, samples, capacity, &length, &channels,
			&count) == sbc::kDecodeOK) {
		check(length <= size, "decoded length fits");
		check((size_t)(count * channels) <= capacity, "decoded samples fit");
	}
}


int
main(int argc, char** argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: bt-sco-fuzz <seconds>\n");
		return 2;
	}
	int seconds = atoi(argv[1]);
	gState ^= (uint64_t)time(NULL) << 1;

	// Real mSBC frames to damage.
	std::vector<std::vector<uint8_t> > frames;
	sbc::Encoder encoder;
	check(encoder.InitMSBC(), "mSBC encoder");
	for (int i = 0; i < 16; i++) {
		float samples[sbc::kMSBCFrameSamples];
		for (int j = 0; j < sbc::kMSBCFrameSamples; j++)
			samples[j] = ((int)(next_word() % 2001) - 1000) / 1200.0f;
		uint8_t frame[sbc::kMSBCFrameLength];
		check(encoder.Encode(samples, frame, sizeof(frame))
			== sbc::kMSBCFrameLength, "mSBC frame");
		frames.push_back(std::vector<uint8_t>(frame, frame + sizeof(frame)));
	}

	h2_sco_ring* ring = new h2_sco_ring;
	time_t end = time(NULL) + seconds;
	uint64_t rounds = 0;
	while (time(NULL) < end) {
		for (int i = 0; i < 200; i++) {
			fuzz_events();
			fuzz_sco(ring);
			fuzz_msbc(frames);
			rounds++;
		}
	}
	delete ring;
	printf("sco_fuzz: %llu rounds\n", (unsigned long long)rounds);
	printf("sco_fuzz: %ds 0 crashes\n", seconds);
	return 0;
}
