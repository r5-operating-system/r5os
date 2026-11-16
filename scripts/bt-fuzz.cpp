/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the Bluetooth kernel parsers. Build with
 * AddressSanitizer and UBSan (scripts/fuzz-bt.sh).
 * Usage: bt-fuzz <target> <seconds>
 * Targets:
 *	hci	the HCI reassembly (hci/hci_assemble.h) on random pieces of
 *		events, ACL and SCO packets, checking that every whole packet
 *		that goes in comes out once, unchanged; the command timeout
 *		tracker and the event stream filter (h2generic/h2command.h).
 *	l2cap	the L2CAP signalling checks (l2cap/l2cap_parse.h): random and
 *		well-formed signalling packets walked the way the dispatcher
 *		walks them, configuration options, PSM and fixed CID rules.
 * Prints "<target>_fuzz: <seconds>s 0 crashes" when it ends; ASan or UBSan
 * stop it with a report on a fault.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "hci_assemble.h"
#include "h2command.h"
#include "l2cap_parse.h"


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
		fprintf(stderr, "bt-fuzz: check failed: %s\n", what);
		abort();
	}
}


// --- HCI reassembly -----------------------------------------------------


struct HostOps {
	typedef std::vector<uint8>* Buffer;

	Buffer current[4] = { NULL, NULL, NULL, NULL };
	std::vector<std::vector<uint8> > events;
	std::vector<std::vector<uint8> > acl;
	bool failCreate = false;
	bool failAppend = false;
	bool failRead = false;

	Buffer Current(uint8 type) { return current[type]; }
	void SetCurrent(uint8 type, Buffer buffer) { current[type] = buffer; }

	Buffer Create()
	{
		if (failCreate)
			return NULL;
		return new std::vector<uint8>();
	}

	bool Append(Buffer buffer, const uint8* data, size_t size)
	{
		if (failAppend)
			return false;
		buffer->insert(buffer->end(), data, data + size);
		return true;
	}

	size_t Size(Buffer buffer) { return buffer->size(); }

	bool Read(Buffer buffer, size_t offset, uint8* data, size_t size)
	{
		if (failRead || offset + size > buffer->size())
			return false;
		memcpy(data, buffer->data() + offset, size);
		return true;
	}

	void Destroy(Buffer buffer) { delete buffer; }

	void PostEvent(const uint8* event, size_t size)
	{
		check(size >= 2 && size == 2u + event[1], "event size");
		events.push_back(std::vector<uint8>(event, event + size));
	}

	void PostAcl(Buffer buffer)
	{
		check(buffer->size() >= 4, "acl header");
		check(buffer->size() == 4u + ((*buffer)[2] | ((*buffer)[3] << 8)),
			"acl size");
		acl.push_back(*buffer);
		delete buffer;
	}

	~HostOps()
	{
		for (int i = 0; i < 4; i++)
			delete current[i];
	}
};


static std::vector<uint8>
random_packet(uint8 type)
{
	std::vector<uint8> packet;
	size_t length;
	if (type == HCI_ASSEMBLE_EVENT) {
		length = next_word() % 256;
		packet.push_back((uint8)next_word());
		packet.push_back((uint8)length);
	} else if (type == HCI_ASSEMBLE_ACL) {
		// Mostly short, sometimes up to the 16-bit limit.
		length = (next_word() % 8) == 0 ? next_word() % 65536
			: next_word() % 1100;
		packet.push_back((uint8)next_word());
		packet.push_back((uint8)next_word());
		packet.push_back((uint8)(length & 0xff));
		packet.push_back((uint8)(length >> 8));
	} else {
		length = next_word() % 256;
		packet.push_back((uint8)next_word());
		packet.push_back((uint8)next_word());
		packet.push_back((uint8)length);
	}
	for (size_t i = 0; i < length; i++)
		packet.push_back((uint8)next_word());
	return packet;
}


// Well-formed packets cut at random places: each must come out once.
static void
fuzz_reassembly_round()
{
	static const uint8 kTypes[] = { HCI_ASSEMBLE_EVENT, HCI_ASSEMBLE_ACL,
		HCI_ASSEMBLE_SCO };
	uint8 type = kTypes[next_word() % 3];

	std::vector<std::vector<uint8> > packets;
	std::vector<uint8> stream;
	int count = 1 + next_word() % 6;
	for (int i = 0; i < count; i++) {
		packets.push_back(random_packet(type));
		stream.insert(stream.end(), packets.back().begin(),
			packets.back().end());
	}

	HostOps ops;
	size_t offset = 0;
	while (offset < stream.size()) {
		size_t piece = 1 + next_word() % ((next_word() % 4) == 0 ? 3 : 300);
		if (piece > stream.size() - offset)
			piece = stream.size() - offset;
		// A copy of exactly this piece, so ASan sees a read past it.
		std::vector<uint8> chunk(stream.begin() + offset,
			stream.begin() + offset + piece);
		check(hci_assemble(ops, type, chunk.data(), chunk.size()) == B_OK,
			"status");
		offset += piece;
	}

	check(ops.current[type] == NULL, "nothing left over");
	if (type == HCI_ASSEMBLE_EVENT) {
		check(ops.events == packets, "events out as in");
	} else if (type == HCI_ASSEMBLE_ACL) {
		check(ops.acl == packets, "acl out as in");
	}
}


// Random bytes, random types and failing storage: no fault, no hang.
static void
fuzz_reassembly_garbage()
{
	HostOps ops;
	for (int round = 0; round < 8; round++) {
		size_t size = next_word() % 600;
		std::vector<uint8> chunk(size);
		for (size_t i = 0; i < size; i++)
			chunk[i] = (uint8)next_word();
		ops.failCreate = (next_word() % 16) == 0;
		ops.failAppend = (next_word() % 16) == 0;
		ops.failRead = (next_word() % 16) == 0;
		uint8 type = (uint8)(next_word() % 6);
		status_t status = hci_assemble(ops, type, chunk.data(), chunk.size());
		if (type < HCI_ASSEMBLE_EVENT || type > HCI_ASSEMBLE_SCO)
			check(status == B_BAD_VALUE, "unknown type");
	}
}


// --- Command tracker ----------------------------------------------------


static void
fuzz_tracker_round()
{
	h2_command_tracker tracker;
	h2_command_reset(&tracker);
	bigtime_t now = 1000;

	// A reference: which opcodes the host was told timed out and which
	// answers it saw for them.
	for (int step = 0; step < 64; step++) {
		uint16 opcode = (uint16)(1 + next_word() % 6);
		switch (next_word() % 4) {
			case 0:
			{
				uint16 dropped;
				int slot = h2_command_sent(&tracker, opcode, now, &dropped);
				check(slot >= 0 && slot < H2_TRACKED_COMMANDS, "slot");
				if (next_word() % 8 == 0) {
					// Not sent after all: only that slot is freed.
					h2_command_unsent(&tracker, slot, opcode);
					check(tracker.slots[slot].state == H2_COMMAND_FREE,
						"unsent frees its slot");
				}
				break;
			}
			case 1:
			{
				// An answer event, well formed or not.
				uint8 event[8];
				size_t size = next_word() % sizeof(event) + 1;
				for (size_t i = 0; i < sizeof(event); i++)
					event[i] = (uint8)next_word();
				if (next_word() % 2) {
					event[0] = (next_word() % 2) ? H2_EVENT_COMMAND_COMPLETE
						: H2_EVENT_COMMAND_STATUS;
					size = event[0] == H2_EVENT_COMMAND_COMPLETE ? 5 : 6;
					event[1] = (uint8)(size - 2);
					event[size - 2] = (uint8)(opcode & 0xff);
					event[size - 1] = (uint8)(opcode >> 8);
				}
				uint16 answered;
				if (h2_command_answer(event, size, &answered))
					h2_command_answered(&tracker, answered);
				break;
			}
			case 2:
				now += next_word() % (2 * kH2CommandTimeout);
				break;
			case 3:
			{
				uint16 expired;
				int count = 0;
				while (h2_command_expired(&tracker, now, &expired)) {
					check(expired != 0, "expired opcode");
					check(++count <= H2_TRACKED_COMMANDS, "expired count");
				}
				bigtime_t next = h2_command_next_deadline(&tracker);
				check(next < 0 || next > now, "next deadline");
				break;
			}
		}
	}

	// Fixed cases: an answer in time passes; one after the timeout drops.
	uint16 dropped;
	h2_command_reset(&tracker);
	h2_command_sent(&tracker, 0x0c03, 0, &dropped);
	check(dropped == 0, "nothing dropped");
	check(!h2_command_answered(&tracker, 0x0c03), "answer in time passes");
	h2_command_sent(&tracker, 0x0c03, 0, &dropped);
	uint16 expired;
	check(h2_command_expired(&tracker, kH2CommandTimeout, &expired)
		&& expired == 0x0c03, "times out at the deadline");
	check(h2_command_answered(&tracker, 0x0c03), "late answer drops");
	check(!h2_command_answered(&tracker, 0x0c03), "dropped once");

	uint8 event[H2_TIMEOUT_EVENT_SIZE];
	h2_command_timeout_event(0x0c03, event);
	uint16 opcode;
	check(h2_command_answer(event, sizeof(event), &opcode) && opcode == 0x0c03,
		"timeout event parses");
	check(event[2] == kH2TimeoutStatus, "timeout status");

	// A full tracker reports the command it gives up on; a failed send
	// frees its own slot, not an older one of the same opcode.
	h2_command_reset(&tracker);
	for (int i = 0; i < H2_TRACKED_COMMANDS; i++) {
		h2_command_sent(&tracker, (uint16)(0x1000 + i), i, &dropped);
		check(dropped == 0, "room left");
	}
	h2_command_sent(&tracker, 0x2000, 100, &dropped);
	check(dropped == 0x1000, "the oldest waiting command is reported");
	h2_command_reset(&tracker);
	int first = h2_command_sent(&tracker, 0x0c03, 0, &dropped);
	int second = h2_command_sent(&tracker, 0x0c03, 1, &dropped);
	h2_command_unsent(&tracker, second, 0x0c03);
	check(tracker.slots[first].state == H2_COMMAND_WAITING
		&& tracker.slots[second].state == H2_COMMAND_FREE,
		"unsent keeps the earlier command");
}


// --- Event stream (h2command.h) -----------------------------------------


// Events, some Command Complete or Command Status, cut at random places
// and run together: the filter must pass every kept event unchanged and
// judge each event once, from its first bytes.
static void
fuzz_event_stream_round()
{
	std::vector<std::vector<uint8> > events;
	std::vector<uint8> stream;
	std::vector<uint8> expected;
	int count = 1 + next_word() % 8;
	for (int i = 0; i < count; i++) {
		std::vector<uint8> event;
		uint8 kind = (uint8)(next_word() % 3);
		size_t length = next_word() % 256;
		if (kind == 0) {
			event.push_back(H2_EVENT_COMMAND_COMPLETE);
			if (length < 3)
				length = 3;
		} else if (kind == 1) {
			event.push_back(H2_EVENT_COMMAND_STATUS);
			if (length < 4)
				length = 4;
		} else
			event.push_back((uint8)(0x10 + next_word() % 0x30));
		event.push_back((uint8)length);
		for (size_t j = 0; j < length; j++)
			event.push_back((uint8)next_word());
		events.push_back(event);
		stream.insert(stream.end(), event.begin(), event.end());

		uint16 opcode;
		bool drop = h2_command_answer(event.data(), event.size(), &opcode)
			&& (opcode & 1) != 0;
		if (!drop)
			expected.insert(expected.end(), event.begin(), event.end());
	}

	int judged = 0;
	auto drop = [&judged](const uint8* head, size_t size) {
		judged++;
		check(size >= 2 && size <= H2_EVENT_HEAD_SIZE, "head size");
		check(size == H2_EVENT_HEAD_SIZE || size == 2u + head[1],
			"head is min(6, event)");
		uint16 opcode;
		return h2_command_answer_head(head, size, &opcode)
			&& (opcode & 1) != 0;
	};

	h2_event_stream state;
	h2_event_stream_reset(&state);
	std::vector<uint8> out;
	size_t offset = 0;
	while (offset < stream.size()) {
		size_t piece = 1 + next_word() % ((next_word() % 4) == 0 ? 7 : 300);
		if (piece > stream.size() - offset)
			piece = stream.size() - offset;
		std::vector<uint8> chunk(stream.begin() + offset,
			stream.begin() + offset + piece);
		std::vector<uint8> buffer(piece + H2_EVENT_HEAD_SIZE);
		size_t size = h2_event_stream_filter(&state, chunk.data(), chunk.size(),
			buffer.data(), drop);
		check(size <= buffer.size(), "out room");
		out.insert(out.end(), buffer.begin(), buffer.begin() + size);
		offset += piece;
	}

	check(judged == count, "each event judged once");
	check(state.headSize == 0 && state.left == 0, "ends on a boundary");
	check(out == expected, "kept events out as in");
}


// --- L2CAP signalling ---------------------------------------------------


struct WalkedCommand {
	uint8	code;
	uint16	length;
	int		check;
	std::vector<std::vector<uint8> > options;
	bool	malformed;
};


// Walks a signalling packet the way l2cap_handle_signaling_command() does.
static std::vector<WalkedCommand>
walk_signalling(const std::vector<uint8>& packet)
{
	std::vector<WalkedCommand> walked;
	size_t position = 0;
	int steps = 0;
	while (position < packet.size()) {
		check(++steps <= 70000, "walk ends");
		if (packet.size() - position < L2CAP_PARSE_COMMAND_HEADER)
			break;
		const uint8* header = packet.data() + position;
		WalkedCommand command;
		command.code = header[0];
		command.length = (uint16)(header[2] | (header[3] << 8));
		command.malformed = false;
		size_t remaining = packet.size() - position - L2CAP_PARSE_COMMAND_HEADER;
		command.check = l2cap_parse_check(command.code, command.length,
			remaining);
		if (command.check == L2CAP_PARSE_TRUNCATED) {
			check(command.length > remaining, "truncated means too long");
			walked.push_back(command);
			break;
		}
		const uint8* data = header + L2CAP_PARSE_COMMAND_HEADER;
		if (command.check == L2CAP_PARSE_HANDLE) {
			check(command.length >= l2cap_parse_min_length(command.code),
				"handled commands are long enough");
			// Configuration request and response carry options after 4
			// and 6 bytes.
			size_t optionsAt = command.code == 0x04 ? 4
				: command.code == 0x05 ? 6 : 0;
			if (optionsAt != 0 && command.length > optionsAt) {
				// The kernel copies exactly this range out first.
				std::vector<uint8> copy(data + optionsAt, data + command.length);
				size_t offset = 0;
				l2cap_parse_option option;
				bool malformed;
				while (l2cap_parse_next_option(copy.data(), copy.size(),
						&offset, &option, &malformed)) {
					check(option.value >= copy.data() && option.value
						+ option.length <= copy.data() + copy.size(),
						"option value inside");
					check(offset <= copy.size(), "option offset");
					command.options.push_back(std::vector<uint8>(option.value,
						option.value + option.length));
				}
				command.malformed = malformed;
			}
		}
		walked.push_back(command);
		position += L2CAP_PARSE_COMMAND_HEADER + command.length;
	}
	return walked;
}


static void
fuzz_l2cap_round()
{
	// Random bytes, kept as their own allocation so ASan sees overreads.
	{
		std::vector<uint8> packet(next_word() % 300);
		for (size_t i = 0; i < packet.size(); i++)
			packet[i] = (uint8)next_word();
		// Mostly small codes and lengths, so the handled paths are reached.
		for (size_t i = 0; i + 3 < packet.size(); i += 4 + next_word() % 9) {
			packet[i] = (uint8)(next_word() % 13);
			packet[i + 3] = 0;
			packet[i + 2] = (uint8)(next_word() % 40);
		}
		walk_signalling(packet);
	}

	// Well-formed commands with options come back as they went in.
	std::vector<uint8> packet;
	std::vector<WalkedCommand> expected;
	int count = 1 + next_word() % 4;
	for (int i = 0; i < count; i++) {
		WalkedCommand command;
		command.code = (uint8)(1 + next_word() % 11);
		command.malformed = false;
		command.check = L2CAP_PARSE_HANDLE;
		std::vector<uint8> data(l2cap_parse_min_length(command.code));
		for (size_t j = 0; j < data.size(); j++)
			data[j] = (uint8)next_word();
		if (command.code == 0x04 || command.code == 0x05) {
			int options = next_word() % 4;
			for (int j = 0; j < options; j++) {
				std::vector<uint8> value(next_word() % 24);
				for (size_t k = 0; k < value.size(); k++)
					value[k] = (uint8)next_word();
				data.push_back((uint8)next_word());
				data.push_back((uint8)value.size());
				data.insert(data.end(), value.begin(), value.end());
				command.options.push_back(value);
			}
		}
		command.length = (uint16)data.size();
		packet.push_back(command.code);
		packet.push_back((uint8)next_word());
		packet.push_back((uint8)(command.length & 0xff));
		packet.push_back((uint8)(command.length >> 8));
		packet.insert(packet.end(), data.begin(), data.end());
		expected.push_back(command);
	}
	std::vector<WalkedCommand> walked = walk_signalling(packet);
	check(walked.size() == expected.size(), "command count");
	for (size_t i = 0; i < walked.size(); i++) {
		check(walked[i].check == L2CAP_PARSE_HANDLE, "well-formed is handled");
		check(walked[i].code == expected[i].code, "code");
		check(walked[i].length == expected[i].length, "length");
		check(!walked[i].malformed, "options well formed");
		check(walked[i].options == expected[i].options, "options");
	}

	// The PSM and fixed channel rules.
	uint16 psm = (uint16)next_word();
	check(l2cap_parse_valid_psm(psm)
		== (((psm & 1) != 0) && ((psm & 0x100) == 0)), "psm rule");
	check(l2cap_parse_valid_psm(0x0011) && l2cap_parse_valid_psm(0x0013)
		&& l2cap_parse_valid_psm(0x0019) && l2cap_parse_valid_psm(0x1001),
		"hid and avdtp psm");
	check(!l2cap_parse_valid_psm(0x0010) && !l2cap_parse_valid_psm(0x0101),
		"bad psm");
	check(l2cap_parse_is_fixed_channel(0x0004)
		&& l2cap_parse_is_fixed_channel(0x0006)
		&& !l2cap_parse_is_fixed_channel(0x0040)
		&& !l2cap_parse_is_fixed_channel(0), "fixed channels");
}


static bool
seconds_left(time_t start, int seconds)
{
	return (int)(time(NULL) - start) < seconds;
}


int
main(int argc, char** argv)
{
	bool hci = argc == 3 && strcmp(argv[1], "hci") == 0;
	bool l2cap = argc == 3 && strcmp(argv[1], "l2cap") == 0;
	if (!hci && !l2cap) {
		fprintf(stderr, "usage: bt-fuzz hci|l2cap <seconds>\n");
		return 2;
	}
	int seconds = atoi(argv[2]);
	time_t start = time(NULL);
	uint64_t rounds = 0;

	while (seconds_left(start, seconds)) {
		if (hci) {
			fuzz_reassembly_round();
			fuzz_reassembly_garbage();
			fuzz_tracker_round();
			fuzz_event_stream_round();
		} else
			fuzz_l2cap_round();
		rounds++;
	}

	printf("%s_fuzz: %ds 0 crashes (%llu rounds)\n", argv[1], seconds,
		(unsigned long long)rounds);
	return 0;
}
