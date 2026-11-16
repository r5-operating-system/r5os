/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the RFCOMM code of bluetooth_server
 * (distro/bluetooth/server/RFCOMMMux.cpp, docs/BLUETOOTH.md step 7a),
 * built with AddressSanitizer and UBSan by scripts/fuzz-bt.sh (target
 * "rfcomm"). Every input lives in a heap buffer of exactly its length.
 *  - random bytes into the frame, MCC, PN and SDP parsers;
 *  - frames built from random fields parse back to the same fields;
 *  - a mux, as initiator or responder and often brought to an open DLC
 *    first, gets random frames with a right FCS (DLCI 0 with random MCC
 *    messages, the data DLC with data and credits, other DLCIs), some
 *    with one byte changed, between random Send(), Take(), Tick() and
 *    Close() calls. Checked besides "no fault": every frame it sends
 *    parses and has a right FCS; buffered data plus the credits it gave
 *    never exceed its buffer; credits are never negative.
 * Prints "rfcomm_fuzz: <seconds>s 0 crashes" when it ends.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "RFCOMMMux.h"


using namespace r5rfcomm;


static uint64_t gState = 0x2545f4914f6cdd1dULL;
static unsigned long gOpened = 0;


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
		fprintf(stderr, "rfcomm_fuzz: check failed: %s\n", what);
		abort();
	}
}


static uint8_t*
heap_copy(const uint8_t* data, size_t size)
{
	uint8_t* copy = (uint8_t*)malloc(size > 0 ? size : 1);
	if (size > 0)
		memcpy(copy, data, size);
	return copy;
}


// Every frame a mux sends: parsed again as the other side would.
struct Sink {
	std::vector<std::vector<uint8_t> > frames;
};


static void
sink_output(const uint8_t* frame, size_t length, void* cookie)
{
	uint8_t* copy = heap_copy(frame, length);
	Frame parsed;
	bool credits = length >= 2 && (copy[0] >> 2) != 0
		&& (copy[1] & kPF) != 0;
	check(ParseFrame(copy, length, credits, &parsed), "sent frame parses");
	free(copy);
	Sink* sink = (Sink*)cookie;
	if (sink != NULL && sink->frames.size() < 64)
		sink->frames.push_back(std::vector<uint8_t>(frame, frame + length));
}


static void
check_mux(const Mux& mux)
{
	check(mux.Pending() <= kReceiveBuffer, "pending fits");
	check(mux.SendCredits() >= 0, "send credits");
	check(mux.GivenCredits() >= 0, "given credits");
	if (mux.CreditFlow()) {
		check(mux.Pending() + (size_t)mux.GivenCredits() * mux.MaxFrame()
			<= kReceiveBuffer, "credits never more than room");
	}
	check(mux.MaxFrame() > 0 && mux.MaxFrame() <= kDefaultN1, "N1");
}


static void
fuzz_parsers()
{
	uint8_t raw[300];
	size_t size = next_word() % sizeof(raw);
	for (size_t i = 0; i < size; i++)
		raw[i] = (uint8_t)next_word();
	// Often a frame with a right FCS, so the parser goes past it.
	if (size >= 4 && (next_word() & 1)) {
		raw[0] |= 0x01;
		raw[1] = (uint8_t)(kUIH | (next_word() & 1 ? kPF : 0));
		raw[size - 1] = Fcs(raw, 2);
	}
	uint8_t* copy = heap_copy(raw, size);
	Frame frame;
	for (int credits = 0; credits < 2; credits++) {
		if (ParseFrame(copy, size, credits != 0, &frame)) {
			check(frame.data >= copy && frame.data + frame.length
				<= copy + size, "frame data inside the packet");
		}
	}
	Mcc mcc;
	size_t used;
	if (ParseMcc(copy, size, &mcc, &used)) {
		check(used <= size && mcc.value + mcc.length <= copy + size,
			"MCC inside");
		Negotiation negotiation;
		ParseNegotiation(mcc.value, mcc.length, &negotiation);
	}
	size_t split = size > 0 ? next_word() % (size + 1) : 0;
	uint8_t* classes = heap_copy(raw, split);
	uint8_t* protocols = heap_copy(raw + split, size - split);
	uint8_t channel = FindChannel(classes, split, protocols, size - split,
		(uint16_t)(next_word() & 1 ? 0x111e : next_word()));
	check(channel == 0 || (channel >= kMinChannel && channel <= kMaxChannel),
		"channel range");
	free(classes);
	free(protocols);
	free(copy);
}


static void
fuzz_round_trip()
{
	uint8_t data[400];
	size_t length = next_word() % 300;
	for (size_t i = 0; i < length; i++)
		data[i] = (uint8_t)next_word();
	static const uint8_t kTypes[5] = { kSABM, kUA, kDM, kDISC, kUIH };
	uint8_t type = kTypes[next_word() % 5];
	if (type != kUIH)
		length = 0;
	uint8_t dlci = (uint8_t)(next_word() % 64);
	bool pollFinal = next_word() & 1;
	int credits = type == kUIH && pollFinal && dlci != 0 && (next_word() & 1)
		? (int)(next_word() % 256) : -1;
	bool commandResponse = next_word() & 1;
	uint8_t out[400 + kMaxOverhead];
	size_t size = BuildFrame(out, sizeof(out), dlci, commandResponse, type,
		pollFinal, credits, data, length);
	check(size > 0, "frame built");
	uint8_t* copy = heap_copy(out, size);
	Frame frame;
	check(ParseFrame(copy, size, credits >= 0, &frame), "built frame parses");
	check(frame.dlci == dlci && frame.type == type
		&& frame.pollFinal == pollFinal
		&& frame.commandResponse == commandResponse
		&& frame.hasCredits == (credits >= 0)
		&& (credits < 0 || frame.credits == credits)
		&& frame.length == length
		&& memcmp(frame.data, data, length) == 0, "same fields");
	free(copy);
}


// A frame with a right FCS for the mux: what an odd headset may send.
static std::vector<uint8_t>
random_frame(const Mux& mux)
{
	uint8_t data[200];
	size_t length = 0;
	uint8_t dlci;
	uint32_t pick = next_word() % 8;
	if (pick < 3)
		dlci = 0;
	else if (pick < 6)
		dlci = mux.DataDLCI();
	else
		dlci = (uint8_t)(next_word() % 64);

	static const uint8_t kTypes[5] = { kSABM, kUA, kDM, kDISC, kUIH };
	uint8_t type = kTypes[next_word() % 5];
	if (next_word() % 3 != 0)
		type = kUIH;
	bool pollFinal = next_word() % 4 == 0;
	int credits = -1;

	if (type == kUIH && dlci == 0) {
		// One to three MCC messages, of known and unknown types.
		static const uint8_t kMcc[9] = { kMccPN, kMccMSC, kMccTest, kMccFCon,
			kMccFCoff, kMccRPN, kMccRLS, kMccNSC, 0x45 };
		int count = 1 + next_word() % 3;
		for (int i = 0; i < count; i++) {
			uint8_t value[16];
			size_t valueLength = next_word() % 12;
			for (size_t j = 0; j < valueLength; j++)
				value[j] = (uint8_t)next_word();
			uint8_t mccType = kMcc[next_word() % 9];
			if (mccType == kMccPN && (next_word() & 1)) {
				Negotiation negotiation;
				negotiation.dlci = next_word() & 1 ? mux.DataDLCI()
					: (uint8_t)(next_word() % 64);
				static const uint8_t kCL[3] = { 0x0f, 0x0e, 0 };
				negotiation.convergence = kCL[next_word() % 3];
				negotiation.priority = 0;
				negotiation.maxFrame = (uint16_t)(next_word() % 4 == 0
					? next_word() : next_word() % 300);
				negotiation.credits = (uint8_t)(next_word() % 8);
				BuildNegotiation(value, negotiation);
				valueLength = kNegotiationSize;
			} else if (mccType == kMccMSC && (next_word() & 1)) {
				value[0] = (uint8_t)((mux.DataDLCI() << 2) | 0x03);
				value[1] = (uint8_t)(next_word() | 0x01);
				valueLength = 2;
			}
			size_t built = BuildMcc(data + length, sizeof(data) - length,
				mccType, next_word() & 1, value, valueLength);
			length += built;
		}
	} else if (type == kUIH) {
		length = next_word() % 4 == 0 ? next_word() % 200
			: next_word() % 20;
		for (size_t i = 0; i < length; i++)
			data[i] = (uint8_t)next_word();
		if (pollFinal && dlci != 0 && mux.CreditFlow())
			credits = (int)(next_word() % 256);
	}

	uint8_t out[200 + kMaxOverhead];
	size_t size = BuildFrame(out, sizeof(out), dlci, next_word() & 1, type,
		pollFinal, credits, data, length);
	std::vector<uint8_t> frame(out, out + size);
	if (size > 0 && next_word() % 16 == 0)
		frame[next_word() % size] ^= (uint8_t)(1 << (next_word() % 8));
	if (size > 0 && next_word() % 32 == 0)
		frame.resize(next_word() % size);
	return frame;
}


static void
deliver(Mux& mux, const std::vector<uint8_t>& frame, uint64_t now)
{
	uint8_t* copy = heap_copy(frame.empty() ? NULL : &frame[0], frame.size());
	mux.Receive(copy, frame.size(), now);
	free(copy);
	check_mux(mux);
}


static void
fuzz_mux()
{
	Sink fromInitiator, fromResponder;
	Mux initiator(true, sink_output, &fromInitiator);
	Mux responder(false, sink_output, &fromResponder);
	uint64_t now = next_word();
	uint8_t channel = (uint8_t)(1 + next_word() % 30);
	responder.Listen(channel);
	initiator.Open(channel, now);

	// Often the handshake first, as far as it goes.
	int steps = next_word() % 3 == 0 ? (int)(next_word() % 12) : 40;
	for (int i = 0; i < steps; i++) {
		std::vector<std::vector<uint8_t> > toResponder, toInitiator;
		toResponder.swap(fromInitiator.frames);
		toInitiator.swap(fromResponder.frames);
		if (toResponder.empty() && toInitiator.empty())
			break;
		for (size_t j = 0; j < toResponder.size(); j++)
			deliver(responder, toResponder[j], now);
		for (size_t j = 0; j < toInitiator.size(); j++)
			deliver(initiator, toInitiator[j], now);
	}

	if (initiator.GetState() == Mux::kOpen && responder.GetState() == Mux::kOpen)
		gOpened++;
	Mux& mux = next_word() & 1 ? initiator : responder;
	Sink& sink = &mux == &initiator ? fromInitiator : fromResponder;
	int rounds = 1 + next_word() % 40;
	for (int i = 0; i < rounds; i++) {
		sink.frames.clear();
		switch (next_word() % 10) {
			case 0:
			{
				uint8_t data[200];
				size_t length = 1 + next_word() % 150;
				memset(data, 'a', sizeof(data));
				bool sent = mux.Send(data, length);
				check(!sent || length <= mux.MaxFrame(), "send size");
				break;
			}
			case 1:
			{
				uint8_t out[kReceiveBuffer];
				size_t size = next_word() % sizeof(out);
				size_t before = mux.Pending();
				size_t got = mux.Take(out, size);
				check(got <= size && got <= before, "take");
				break;
			}
			case 2:
				now += next_word() % 3 == 0 ? kTimeout : next_word() % 1000;
				mux.Tick(now);
				break;
			case 3:
				if (next_word() % 4 == 0)
					mux.Close(now);
				break;
			default:
				deliver(mux, random_frame(mux), now);
				break;
		}
		check_mux(mux);
		check(mux.SendSpace() <= mux.MaxFrame(), "send space");
	}
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60;
	if (seconds <= 0)
		seconds = 60;
	gState ^= (uint64_t)time(NULL);
	time_t end = time(NULL) + seconds;
	unsigned long rounds = 0;
	while (time(NULL) < end) {
		for (int i = 0; i < 200; i++) {
			fuzz_parsers();
			fuzz_round_trip();
			fuzz_mux();
			rounds++;
		}
	}
	printf("rfcomm_fuzz: %lu rounds, %lu with an open DLC\n", rounds,
		gOpened);
	printf("rfcomm_fuzz: %ds 0 crashes\n", seconds);
	return 0;
}
