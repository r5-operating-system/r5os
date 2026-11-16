/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the SDP answers of bluetooth_server
 * (distro/bluetooth/server/SDPRecords.h), built with AddressSanitizer and
 * UBSan by scripts/fuzz-bt.sh (target "sdp"). Each request lives in a heap
 * buffer of exactly its length. Checked besides "no fault":
 *  - every answer fits its buffer, and its parameter length is right;
 *  - an attribute answer pieced together over its continuation states,
 *    with any small byte count, is the same as in one piece, and is a
 *    well-formed data element sequence;
 *  - a search for the Audio Source finds exactly its record.
 * Prints "sdp_fuzz: <seconds>s 0 crashes" when it ends.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "SDPRecords.h"


static uint64_t gState = 0x853c49e6748fea9bULL;


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
		fprintf(stderr, "sdp_fuzz: check failed: %s\n", what);
		abort();
	}
}


static size_t
ask(const std::vector<uint8_t>& request, uint8_t* out, size_t outSize)
{
	uint8_t* copy = (uint8_t*)malloc(request.size() > 0 ? request.size() : 1);
	if (!request.empty())
		memcpy(copy, &request[0], request.size());
	size_t length = r5sdp::HandleRequest(copy, request.size(), out, outSize);
	free(copy);
	check(length <= outSize, "answer fits");
	if (length >= 5) {
		size_t parameters = ((size_t)out[3] << 8) | out[4];
		check(parameters == length - 5, "parameter length");
	}
	return length;
}


// Skips one data element; false when it is malformed.
static bool
walk_element(const uint8_t* data, size_t size, size_t* offset, int depth)
{
	if (*offset >= size || depth > 8)
		return false;
	uint8_t header = data[(*offset)++];
	uint8_t type = header >> 3;
	uint8_t index = header & 7;
	size_t length;
	if (index <= 4 && type != 6) {
		static const size_t kSizes[5] = { 1, 2, 4, 8, 16 };
		length = kSizes[index];
	} else if (index >= 5 && index <= 7) {
		size_t bytes = index == 5 ? 1 : index == 6 ? 2 : 4;
		if (size - *offset < bytes)
			return false;
		length = 0;
		for (size_t i = 0; i < bytes; i++)
			length = (length << 8) | data[(*offset)++];
	} else
		return false;
	if (size - *offset < length)
		return false;
	if (type == 6) {
		size_t end = *offset + length;
		while (*offset < end) {
			if (!walk_element(data, end, offset, depth + 1))
				return false;
		}
		return *offset == end;
	}
	*offset += length;
	return true;
}


static void
push16(std::vector<uint8_t>& v, uint16_t value)
{
	v.push_back((uint8_t)(value >> 8));
	v.push_back((uint8_t)value);
}


static std::vector<uint8_t>
search_attribute_request(uint16_t transaction, uint16_t uuid,
	uint16_t maxBytes, const uint8_t* continuation, size_t continuationSize)
{
	std::vector<uint8_t> p;
	p.push_back(0x35);
	p.push_back(3);
	p.push_back(0x19);
	push16(p, uuid);
	push16(p, maxBytes);
	// All attributes: 0x0000 to 0xffff.
	p.push_back(0x35);
	p.push_back(5);
	p.push_back(0x0a);
	push16(p, 0x0000);
	push16(p, 0xffff);
	for (size_t i = 0; i < continuationSize; i++)
		p.push_back(continuation[i]);

	std::vector<uint8_t> request;
	request.push_back(r5sdp::kServiceSearchAttributeRequest);
	push16(request, transaction);
	push16(request, (uint16_t)p.size());
	request.insert(request.end(), p.begin(), p.end());
	return request;
}


// The whole attribute answer for a UUID, asked in pieces of maxBytes.
static std::vector<uint8_t>
whole_answer(uint16_t uuid, uint16_t maxBytes)
{
	std::vector<uint8_t> whole;
	uint8_t continuation[3] = { 0 };
	size_t continuationSize = 1;
	uint8_t out[700];
	for (int round = 0; round < 1000; round++) {
		std::vector<uint8_t> request = search_attribute_request(
			(uint16_t)round, uuid, maxBytes, continuation, continuationSize);
		size_t length = ask(request, out, sizeof(out));
		check(length >= 8 && out[0] == r5sdp::kServiceSearchAttributeResponse,
			"attribute answer");
		size_t count = ((size_t)out[5] << 8) | out[6];
		check(count <= maxBytes, "no more than the byte count");
		check(7 + count < length, "continuation state present");
		whole.insert(whole.end(), out + 7, out + 7 + count);
		uint8_t stateLength = out[7 + count];
		if (stateLength == 0)
			return whole;
		check(stateLength == 2 && 7 + count + 3 == length,
			"two byte continuation");
		continuation[0] = 2;
		continuation[1] = out[8 + count];
		continuation[2] = out[9 + count];
		continuationSize = 3;
	}
	check(false, "continuation ends");
	return whole;
}


static void
fuzz_round()
{
	// Random requests, often with a valid header and PDU id.
	uint8_t raw[96];
	size_t size = next_word() % sizeof(raw);
	for (size_t i = 0; i < size; i++)
		raw[i] = (uint8_t)next_word();
	if (size >= 5 && (next_word() & 1)) {
		raw[0] = (uint8_t)(2 + 2 * (next_word() % 3));
		raw[3] = (uint8_t)((size - 5) >> 8);
		raw[4] = (uint8_t)(size - 5);
	}
	std::vector<uint8_t> request(raw, raw + size);
	uint8_t out[700];
	size_t outSize = next_word() % 4 == 0 ? next_word() % 64 : sizeof(out);
	size_t length = ask(request, out, outSize);
	if (length > 0) {
		check(out[0] == r5sdp::kErrorResponse
			|| out[0] == r5sdp::kServiceSearchResponse
			|| out[0] == r5sdp::kServiceAttributeResponse
			|| out[0] == r5sdp::kServiceSearchAttributeResponse,
			"answer PDU id");
	}

	// The whole answer in pieces equals the answer in one piece.
	static const uint16_t kUUIDs[5] = { 0x110a, 0x110e, 0x0100, 0x1002,
		0x1108 };
	uint16_t uuid = kUUIDs[next_word() % 5];
	uint16_t maxBytes = (uint16_t)(7 + next_word() % 120);
	std::vector<uint8_t> pieces = whole_answer(uuid, maxBytes);
	std::vector<uint8_t> once = whole_answer(uuid, 600);
	check(pieces == once, "pieces equal one answer");
	size_t offset = 0;
	check(walk_element(&once[0], once.size(), &offset, 0)
		&& offset == once.size(), "well-formed attribute lists");
	if (uuid == 0x1108)
		check(once.size() == 3, "unknown service: empty list");
}


static void
check_audio_source()
{
	std::vector<uint8_t> request;
	request.push_back(r5sdp::kServiceSearchRequest);
	push16(request, 7);
	push16(request, 8);
	request.push_back(0x35);
	request.push_back(3);
	request.push_back(0x19);
	push16(request, 0x110a);
	push16(request, 10);
	request.push_back(0);
	uint8_t out[64];
	size_t length = ask(request, out, sizeof(out));
	check(length == 14 && out[0] == r5sdp::kServiceSearchResponse
		&& out[6] == 1 && out[8] == 1 && out[9] == 0x00 && out[10] == 0x01
		&& out[11] == 0x00 && out[12] == 0x01, "Audio Source record found");
}


int
main(int argc, char** argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: bt-sdp-fuzz <seconds>\n");
		return 2;
	}
	int seconds = atoi(argv[1]);
	check_audio_source();
	time_t start = time(NULL);
	uint64_t rounds = 0;
	while (time(NULL) - start < seconds) {
		fuzz_round();
		rounds++;
	}
	printf("sdp_fuzz: %ds 0 crashes (%llu rounds)\n", seconds,
		(unsigned long long)rounds);
	return 0;
}
