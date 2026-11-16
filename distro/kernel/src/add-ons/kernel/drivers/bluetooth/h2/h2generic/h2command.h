/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * A hard timeout for HCI commands. A radio that never answers a command
 * left bluetooth_server, and the program waiting on it, without a reply
 * forever. h2generic now notes every command it sends; when no Command
 * Complete or Command Status for that opcode arrives within
 * kH2CommandTimeout, it hands the host a Command Status with
 * kH2TimeoutStatus for that opcode, and drops the real answer if it still
 * comes later.
 *
 * The tracker below has no kernel dependencies, so scripts/fuzz-bt.sh runs
 * it on the host. The kernel side is in h2generic.cpp.
 */
#ifndef _H2COMMAND_H_
#define _H2COMMAND_H_


#include <SupportDefs.h>


// Linux HCI_CMD_TIMEOUT, msecs_to_jiffies(2000), include/net/bluetooth/hci.h.
static const bigtime_t kH2CommandTimeout = 2000000;

// HCI error 0x1F, Unspecified Error (Core 6.0, Vol 1, Part F, 2.31).
static const uint8 kH2TimeoutStatus = 0x1f;

// Commands that may wait for an answer at the same time, per radio.
#define H2_TRACKED_COMMANDS		8

// Event codes (Core 6.0, Vol 4, Part E, 7.7.14 and 7.7.15).
#define H2_EVENT_COMMAND_COMPLETE	0x0e
#define H2_EVENT_COMMAND_STATUS		0x0f

// The size of the Command Status event that h2_command_timeout_event()
// builds.
#define H2_TIMEOUT_EVENT_SIZE		6


enum {
	H2_COMMAND_FREE = 0,
	H2_COMMAND_WAITING,
	H2_COMMAND_TIMED_OUT
};

struct h2_command_slot {
	uint16		opcode;
	uint8		state;
	bigtime_t	deadline;
};

struct h2_command_tracker {
	h2_command_slot	slots[H2_TRACKED_COMMANDS];
};


static inline void
h2_command_reset(h2_command_tracker* tracker)
{
	for (int i = 0; i < H2_TRACKED_COMMANDS; i++) {
		tracker->slots[i].opcode = 0;
		tracker->slots[i].state = H2_COMMAND_FREE;
		tracker->slots[i].deadline = 0;
	}
}


// The opcode of an HCI command packet (opcode, parameter length,
// parameters), or false when it is too short to have one.
static inline bool
h2_command_opcode(const uint8* command, size_t size, uint16* _opcode)
{
	if (command == NULL || size < 3)
		return false;
	*_opcode = (uint16)(command[0] | (command[1] << 8));
	return true;
}


// Whether this whole event answers a command, and which opcode. Opcode 0
// (No Operation) only says that the radio takes commands, and answers none.
static inline bool
h2_command_answer(const uint8* event, size_t size, uint16* _opcode)
{
	if (event == NULL || size < 2 || size != (size_t)2 + event[1])
		return false;

	uint16 opcode;
	if (event[0] == H2_EVENT_COMMAND_COMPLETE && event[1] >= 3)
		opcode = (uint16)(event[3] | (event[4] << 8));
	else if (event[0] == H2_EVENT_COMMAND_STATUS && event[1] >= 4)
		opcode = (uint16)(event[4] | (event[5] << 8));
	else
		return false;

	if (opcode == 0)
		return false;
	*_opcode = opcode;
	return true;
}


// The same from the first bytes of an event only: the first
// min(6, 2 + length) bytes, enough for both opcodes.
static inline bool
h2_command_answer_head(const uint8* event, size_t size, uint16* _opcode)
{
	if (event == NULL || size < 2)
		return false;

	uint16 opcode;
	if (event[0] == H2_EVENT_COMMAND_COMPLETE && event[1] >= 3 && size >= 5)
		opcode = (uint16)(event[3] | (event[4] << 8));
	else if (event[0] == H2_EVENT_COMMAND_STATUS && event[1] >= 4 && size >= 6)
		opcode = (uint16)(event[4] | (event[5] << 8));
	else
		return false;

	if (opcode == 0)
		return false;
	*_opcode = opcode;
	return true;
}


// Note a command that was sent at "now" and return the slot it uses. A full
// tracker gives up on the slot that waited longest; *_dropped is then that
// command's opcode, so the caller can report its timeout at once (it was
// lost silently). Otherwise *_dropped is 0.
static inline int
h2_command_sent(h2_command_tracker* tracker, uint16 opcode, bigtime_t now,
	uint16* _dropped)
{
	*_dropped = 0;
	int use = -1;
	for (int i = 0; i < H2_TRACKED_COMMANDS && use < 0; i++) {
		if (tracker->slots[i].state == H2_COMMAND_FREE)
			use = i;
	}
	// A command that timed out long ago may never be answered.
	for (int i = 0; i < H2_TRACKED_COMMANDS && use < 0; i++) {
		if (tracker->slots[i].state == H2_COMMAND_TIMED_OUT)
			use = i;
	}
	if (use < 0) {
		use = 0;
		for (int i = 1; i < H2_TRACKED_COMMANDS; i++) {
			if (tracker->slots[i].deadline < tracker->slots[use].deadline)
				use = i;
		}
		*_dropped = tracker->slots[use].opcode;
	}

	tracker->slots[use].opcode = opcode;
	tracker->slots[use].state = H2_COMMAND_WAITING;
	tracker->slots[use].deadline = now + kH2CommandTimeout;
	return use;
}


// The command noted in "slot" was not sent after all. Frees exactly that
// slot (freeing by opcode took the oldest command of that opcode, which
// could be another one still in flight).
static inline void
h2_command_unsent(h2_command_tracker* tracker, int slot, uint16 opcode)
{
	if (slot < 0 || slot >= H2_TRACKED_COMMANDS)
		return;
	h2_command_slot& entry = tracker->slots[slot];
	if (entry.opcode == opcode && entry.state == H2_COMMAND_WAITING)
		entry.state = H2_COMMAND_FREE;
}


// An answer for "opcode" arrived. Returns true when it must be dropped:
// the host already got the timeout for it.
static inline bool
h2_command_answered(h2_command_tracker* tracker, uint16 opcode)
{
	int waiting = -1;
	int timedOut = -1;
	for (int i = 0; i < H2_TRACKED_COMMANDS; i++) {
		h2_command_slot& slot = tracker->slots[i];
		if (slot.opcode != opcode)
			continue;
		if (slot.state == H2_COMMAND_WAITING && (waiting < 0
				|| slot.deadline < tracker->slots[waiting].deadline))
			waiting = i;
		else if (slot.state == H2_COMMAND_TIMED_OUT && (timedOut < 0
				|| slot.deadline < tracker->slots[timedOut].deadline))
			timedOut = i;
	}

	// The oldest command of this opcode is the one being answered.
	int answered = waiting;
	if (timedOut >= 0 && (waiting < 0
			|| tracker->slots[timedOut].deadline
				< tracker->slots[waiting].deadline))
		answered = timedOut;
	if (answered < 0)
		return false;

	bool drop = tracker->slots[answered].state == H2_COMMAND_TIMED_OUT;
	tracker->slots[answered].state = H2_COMMAND_FREE;
	return drop;
}


// Marks one waiting command whose deadline has passed as timed out and
// returns its opcode, or returns false when none has.
static inline bool
h2_command_expired(h2_command_tracker* tracker, bigtime_t now,
	uint16* _opcode)
{
	for (int i = 0; i < H2_TRACKED_COMMANDS; i++) {
		h2_command_slot& slot = tracker->slots[i];
		if (slot.state == H2_COMMAND_WAITING && slot.deadline <= now) {
			slot.state = H2_COMMAND_TIMED_OUT;
			*_opcode = slot.opcode;
			return true;
		}
	}
	return false;
}


// The earliest deadline of a waiting command, or -1 when none waits.
static inline bigtime_t
h2_command_next_deadline(const h2_command_tracker* tracker)
{
	bigtime_t next = -1;
	for (int i = 0; i < H2_TRACKED_COMMANDS; i++) {
		const h2_command_slot& slot = tracker->slots[i];
		if (slot.state == H2_COMMAND_WAITING
			&& (next < 0 || slot.deadline < next))
			next = slot.deadline;
	}
	return next;
}


// A Command Status event for "opcode" with kH2TimeoutStatus. One command
// may be sent again (Num_HCI_Command_Packets 1).
static inline void
h2_command_timeout_event(uint16 opcode, uint8 event[H2_TIMEOUT_EVENT_SIZE])
{
	event[0] = H2_EVENT_COMMAND_STATUS;
	event[1] = 4;
	event[2] = kH2TimeoutStatus;
	event[3] = 1;
	event[4] = (uint8)(opcode & 0xff);
	event[5] = (uint8)(opcode >> 8);
}


// The event endpoint does not cut its transfers at event boundaries. An
// event whose length is a multiple of the packet size has no short packet
// after it, so the next event lands in the same transfer; a transfer that
// fills its buffer ends inside an event. This follows the boundaries across
// transfers, so every event is judged from its first bytes, and an event
// that must go is removed whole, without the events around it.
#define H2_EVENT_HEAD_SIZE		6

struct h2_event_stream {
	uint8		head[H2_EVENT_HEAD_SIZE];
		// The first bytes of an event not judged yet
	uint8		headSize;
	uint16		left;
		// Bytes of the judged event still to come
	bool		dropping;
};


static inline void
h2_event_stream_reset(h2_event_stream* stream)
{
	stream->headSize = 0;
	stream->left = 0;
	stream->dropping = false;
}


// Copies the bytes of "in" that go up to "out" (room for inSize +
// H2_EVENT_HEAD_SIZE bytes) and returns their count. drop(cookie, head,
// headSize) is called once per event with its first min(6, 2 + length)
// bytes and returns true when the whole event must go.
template<typename Drop>
static inline size_t
h2_event_stream_filter(h2_event_stream* stream, const uint8* in,
	size_t inSize, uint8* out, Drop drop)
{
	size_t outSize = 0;
	size_t i = 0;
	while (i < inSize) {
		if (stream->left > 0) {
			size_t count = inSize - i;
			if (count > stream->left)
				count = stream->left;
			if (!stream->dropping) {
				for (size_t j = 0; j < count; j++)
					out[outSize++] = in[i + j];
			}
			i += count;
			stream->left -= count;
			continue;
		}

		stream->head[stream->headSize++] = in[i++];
		if (stream->headSize < 2)
			continue;
		size_t eventSize = 2 + (size_t)stream->head[1];
		size_t need = eventSize < H2_EVENT_HEAD_SIZE
			? eventSize : H2_EVENT_HEAD_SIZE;
		if (stream->headSize < need)
			continue;

		stream->dropping = drop(stream->head, stream->headSize);
		if (!stream->dropping) {
			for (size_t j = 0; j < stream->headSize; j++)
				out[outSize++] = stream->head[j];
		}
		stream->left = (uint16)(eventSize - stream->headSize);
		stream->headSize = 0;
	}
	return outSize;
}


#ifdef _KERNEL_MODE
struct bt_usb_dev;

// h2generic.cpp: called by event_complete() for each event transfer.
// Returns the bytes to pass up (none when the transfer only held a late
// answer, or the start of an event not judged yet) and their count.
const uint8* h2_command_events(struct bt_usb_dev* bdev, const uint8* data,
	size_t size, size_t* _outSize);
#endif


#endif	// _H2COMMAND_H_
