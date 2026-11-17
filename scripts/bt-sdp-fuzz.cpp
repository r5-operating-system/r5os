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
 *  - a search for the Audio Source finds exactly its record;
 *  - the client side (distro/bluetooth/server/SDPParse.h, which SDPClient
 *    uses for a headset's answer): a made-up answer with five records,
 *    among them a Hands-Free unit, with a Nil, a URL, a 128-bit integer,
 *    128-bit UUIDs and an alternative, in pieces of any size, gives all
 *    five records, their service classes, every attribute of the record
 *    with the odd elements, and the Hands-Free RFCOMM channel through
 *    r5rfcomm::FindChannel(); damaged copies of it never read out of
 *    bounds.
 * Prints "sdp_fuzz: <seconds>s 0 crashes" when it ends.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "RFCOMMMux.h"
#include "SDPParse.h"
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


// #pragma mark - the client side (SDPParse.h)


typedef std::vector<uint8_t> Bytes;


static Bytes
cat(const Bytes& a, const Bytes& b)
{
	Bytes result(a);
	result.insert(result.end(), b.begin(), b.end());
	return result;
}


static Bytes
with_length(uint8_t type8, const Bytes& content)
{
	Bytes element;
	if (content.size() < 256) {
		element.push_back(type8);
		element.push_back((uint8_t)content.size());
	} else {
		element.push_back((uint8_t)(type8 + 1));
		push16(element, (uint16_t)content.size());
	}
	return cat(element, content);
}


static Bytes seq(const Bytes& content) { return with_length(0x35, content); }
static Bytes alt(const Bytes& content) { return with_length(0x3d, content); }
static Bytes str(const char* text)
{
	return with_length(0x25, Bytes(text, text + strlen(text)));
}
static Bytes url(const char* text)
{
	return with_length(0x45, Bytes(text, text + strlen(text)));
}
static Bytes uint8e(uint8_t value) { return Bytes{ 0x08, value }; }
static Bytes uint16e(uint16_t value)
{
	return Bytes{ 0x09, (uint8_t)(value >> 8), (uint8_t)value };
}
static Bytes uuid16(uint16_t value)
{
	return Bytes{ 0x19, (uint8_t)(value >> 8), (uint8_t)value };
}
// A 16-bit UUID written as 128 bits on the Bluetooth Base UUID.
static Bytes uuid128(uint16_t value)
{
	return Bytes{ 0x1c, 0x00, 0x00, (uint8_t)(value >> 8), (uint8_t)value,
		0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34,
		0xfb };
}
static Bytes attribute(uint16_t id, const Bytes& value)
{
	return cat(uint16e(id), value);
}


static const uint8_t kHandsFreeChannel = 3;
static const size_t kRecordCount = 5;
static const uint16_t kClasses[kRecordCount] = { 0x1000, 0x110b, 0x110e,
	0x111e, 0 };


// Five records as a headset might send them, the Hands-Free unit fourth.
static Bytes
headset_answer()
{
	// The SDP server record, with the elements SDPDataIO lost its step on:
	// Nil, a URL and a 128-bit integer. 0x0100 after them must stay.
	Bytes nil{ 0x00 };
	Bytes uint128{ 0x0c };
	for (int i = 0; i < 16; i++)
		uint128.push_back((uint8_t)i);
	Bytes server = seq(cat(cat(cat(cat(
		attribute(0x0001, seq(uuid16(0x1000))),
		attribute(0x0200, nil)),
		attribute(0x000a, url("http://example.invalid/doc"))),
		attribute(0x0300, uint128)),
		attribute(0x0100, str("SDP server"))));

	// A2DP sink with its class as a 128-bit UUID.
	Bytes sink = seq(cat(cat(
		attribute(0x0001, seq(uuid128(0x110b))),
		attribute(0x0004, seq(cat(seq(cat(uuid16(0x0100), uint16e(0x0019))),
			seq(cat(uuid16(0x0019), uint16e(0x0103))))))),
		attribute(0x0311, uint16e(0x0001))));

	// AVRCP target with an alternative of protocol stacks.
	Bytes remote = seq(cat(
		attribute(0x0001, seq(cat(uuid16(0x110e), uuid16(0x110f)))),
		attribute(0x0004, alt(seq(seq(cat(uuid16(0x0100),
			uint16e(0x0017))))))));

	// Hands-Free unit: classes HF and Generic Audio, L2CAP then RFCOMM
	// channel 3, profile descriptor HFP 1.7, supported features.
	Bytes handsFree = seq(cat(cat(cat(cat(
		attribute(0x0000, Bytes{ 0x0a, 0x00, 0x01, 0x00, 0x05 }),
		attribute(0x0001, seq(cat(uuid16(0x111e), uuid16(0x1203))))),
		attribute(0x0004, seq(cat(seq(uuid16(0x0100)),
			seq(cat(uuid16(0x0003), uint8e(kHandsFreeChannel))))))),
		attribute(0x0009, seq(seq(cat(uuid16(0x111e), uint16e(0x0107)))))),
		attribute(0x0311, uint16e(0x003f))));

	// A vendor service with its own 128-bit UUID (not on the base).
	Bytes vendorUUID{ 0x1c };
	for (int i = 0; i < 16; i++)
		vendorUUID.push_back((uint8_t)(0x90 + i));
	Bytes vendor = seq(cat(attribute(0x0001, seq(vendorUUID)),
		attribute(0x0100, str("Vendor service"))));

	return seq(cat(cat(cat(cat(server, sink), remote), handsFree), vendor));
}


// The answer as a device sends it in pieces of at most maxBytes, each a
// Service Search Attribute Response with a continuation state, put back
// together through ReadAnswerPiece() as SDPClient does.
static Bytes
reassemble(const Bytes& answer, size_t maxBytes)
{
	Bytes whole;
	size_t sent = 0;
	for (int piece = 0; piece < 10000; piece++) {
		size_t count = answer.size() - sent;
		if (count > maxBytes)
			count = maxBytes;
		const bool last = sent + count == answer.size();
		Bytes pdu{ r5sdpparse::kPduSearchAttributeResponse, 0, 1 };
		Bytes parameters;
		push16(parameters, (uint16_t)count);
		parameters.insert(parameters.end(), answer.begin() + sent,
			answer.begin() + sent + count);
		if (last)
			parameters.push_back(0);
		else {
			parameters.push_back(2);
			push16(parameters, (uint16_t)piece);
		}
		push16(pdu, (uint16_t)parameters.size());
		pdu = cat(pdu, parameters);

		const uint8_t* lists;
		size_t listsLength;
		const uint8_t* state;
		size_t stateLength;
		check(r5sdpparse::ReadAnswerPiece(&pdu[0], pdu.size(), &lists,
			&listsLength, &state, &stateLength), "answer piece read");
		check(listsLength == count, "piece byte count");
		whole.insert(whole.end(), lists, lists + listsLength);
		sent += count;
		if (stateLength == 0) {
			check(last, "continuation ends with the last piece");
			return whole;
		}
	}
	check(false, "pieces end");
	return whole;
}


struct ParsedRecord {
	uint16_t	serviceClass;
	size_t		attributes;
	bool		hasName;
	Bytes		classList;
	Bytes		protocolList;
};


static std::vector<ParsedRecord>
parse_answer(const uint8_t* data, size_t size)
{
	std::vector<ParsedRecord> records;
	r5sdpparse::ForEachRecord(data, size,
		[&](const uint8_t* value, size_t length) {
			ParsedRecord record;
			record.serviceClass = 0;
			record.hasName = false;
			record.attributes = r5sdpparse::ForEachAttribute(value, length,
				[&](uint16_t id, const r5sdpparse::Element& element) {
					if (id == 0x0001
						&& element.type == r5sdpparse::kTypeSequence) {
						record.serviceClass = r5sdpparse::FirstServiceClass(
							element.value, element.length);
						record.classList.assign(element.value,
							element.value + element.length);
					}
					if (id == 0x0004
						&& element.type == r5sdpparse::kTypeSequence) {
						record.protocolList.assign(element.value,
							element.value + element.length);
					}
					if (id == 0x0100)
						record.hasName = element.type == r5sdpparse::kTypeString;
				});
			records.push_back(record);
		});
	return records;
}


static uint8_t
hands_free_channel(const std::vector<ParsedRecord>& records)
{
	for (size_t i = 0; i < records.size(); i++) {
		const ParsedRecord& record = records[i];
		if (record.classList.empty() || record.protocolList.empty())
			continue;
		uint8_t channel = r5rfcomm::FindChannel(&record.classList[0],
			record.classList.size(), &record.protocolList[0],
			record.protocolList.size(), r5sdpparse::kServiceHandsFree);
		if (channel != 0)
			return channel;
	}
	return 0;
}


static void
check_client_parse()
{
	const Bytes answer = headset_answer();
	static const size_t kPieces[] = { 1, 7, 32, 255, 1000 };
	for (size_t p = 0; p < sizeof(kPieces) / sizeof(kPieces[0]); p++) {
		Bytes whole = reassemble(answer, kPieces[p]);
		check(whole == answer, "pieces give the answer");
		std::vector<ParsedRecord> records = parse_answer(&whole[0],
			whole.size());
		check(records.size() == kRecordCount, "all five records");
		for (size_t i = 0; i < kRecordCount; i++)
			check(records[i].serviceClass == kClasses[i], "service classes");
		check(records[0].attributes == 5 && records[0].hasName,
			"no attribute lost after Nil, URL and a 128-bit integer");
		check(hands_free_channel(records) == kHandsFreeChannel,
			"hands-free channel");
	}

	// The search for the Hands-Free class alone (SDPClient's second
	// request) asks for these attributes.
	static const uint16_t kAttributes[] = { 0x0001, 0x0004, 0x0009,
		0x0311 };
	uint8_t request[32];
	size_t length = r5sdpparse::BuildSearchAttributes(0x111e, kAttributes,
		4, 255, request, sizeof(request));
	static const uint8_t kExpected[] = { 0x35, 0x03, 0x19, 0x11, 0x1e, 0x00,
		0xff, 0x35, 0x0c, 0x09, 0x00, 0x01, 0x09, 0x00, 0x04, 0x09, 0x00,
		0x09, 0x09, 0x03, 0x11 };
	check(length == sizeof(kExpected)
		&& memcmp(request, kExpected, length) == 0, "hands-free request");
	check(r5sdpparse::BuildSearchAttributes(0x111e, kAttributes, 4, 255,
		request, 10) == 0, "request too small for its buffer");

	// An Error Response is no answer.
	const uint8_t error[] = { 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03 };
	const uint8_t* lists;
	size_t listsLength;
	const uint8_t* state;
	size_t stateLength;
	check(!r5sdpparse::ReadAnswerPiece(error, sizeof(error), &lists,
		&listsLength, &state, &stateLength), "error response refused");
}


// A damaged copy of the answer, and a damaged answer piece, in heap
// buffers of exactly their size: AddressSanitizer catches a read past them.
static void
fuzz_client_round()
{
	static const Bytes answer = headset_answer();
	Bytes damaged(answer);
	const int changes = 1 + next_word() % 8;
	for (int i = 0; i < changes; i++) {
		switch (next_word() % 4) {
			case 0:
				damaged[next_word() % damaged.size()] = (uint8_t)next_word();
				break;
			case 1:
				damaged.resize(next_word() % (damaged.size() + 1));
				break;
			case 2:
				damaged.insert(damaged.begin() + next_word()
					% (damaged.size() + 1), (uint8_t)next_word());
				break;
			default:
				if (!damaged.empty())
					damaged.erase(damaged.begin() + next_word()
						% damaged.size());
				break;
		}
		if (damaged.empty())
			break;
	}
	uint8_t* copy = (uint8_t*)malloc(damaged.size() > 0 ? damaged.size() : 1);
	if (!damaged.empty())
		memcpy(copy, &damaged[0], damaged.size());
	std::vector<ParsedRecord> records = parse_answer(copy, damaged.size());
	hands_free_channel(records);
	free(copy);

	uint8_t raw[64];
	size_t size = next_word() % sizeof(raw);
	for (size_t i = 0; i < size; i++)
		raw[i] = (uint8_t)next_word();
	if (size > 0 && (next_word() & 1))
		raw[0] = r5sdpparse::kPduSearchAttributeResponse;
	uint8_t* piece = (uint8_t*)malloc(size > 0 ? size : 1);
	memcpy(piece, raw, size);
	const uint8_t* lists;
	size_t listsLength;
	const uint8_t* state;
	size_t stateLength;
	if (r5sdpparse::ReadAnswerPiece(piece, size, &lists, &listsLength,
			&state, &stateLength)) {
		check(lists >= piece && lists + listsLength <= piece + size
			&& state + stateLength <= piece + size && stateLength <= 16,
			"piece within its buffer");
	}
	free(piece);
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
	check_client_parse();
	printf("sdp_fuzz: client parse: 5 records, hands-free channel %u\n",
		kHandsFreeChannel);
	time_t start = time(NULL);
	uint64_t rounds = 0;
	while (time(NULL) - start < seconds) {
		fuzz_round();
		fuzz_client_round();
		rounds++;
	}
	printf("sdp_fuzz: %ds 0 crashes (%llu rounds)\n", seconds,
		(unsigned long long)rounds);
	return 0;
}
