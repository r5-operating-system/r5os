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
 *		that goes in comes out once, unchanged; and the command
 *		timeout tracker (h2generic/h2command.h).
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
		if (offset + size > buffer->size())
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
				h2_command_sent(&tracker, opcode, now);
				break;
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
	h2_command_reset(&tracker);
	h2_command_sent(&tracker, 0x0c03, 0);
	check(!h2_command_answered(&tracker, 0x0c03), "answer in time passes");
	h2_command_sent(&tracker, 0x0c03, 0);
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
}


static bool
seconds_left(time_t start, int seconds)
{
	return (int)(time(NULL) - start) < seconds;
}


int
main(int argc, char** argv)
{
	if (argc != 3 || strcmp(argv[1], "hci") != 0) {
		fprintf(stderr, "usage: bt-fuzz hci <seconds>\n");
		return 2;
	}
	int seconds = atoi(argv[2]);
	time_t start = time(NULL);
	uint64_t rounds = 0;

	while (seconds_left(start, seconds)) {
		fuzz_reassembly_round();
		fuzz_reassembly_garbage();
		fuzz_tracker_round();
		rounds++;
	}

	printf("%s_fuzz: %ds 0 crashes (%llu rounds)\n", argv[1], seconds,
		(unsigned long long)rounds);
	return 0;
}
