/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The voice path of h2generic without kernel dependencies, so
 * scripts/fuzz-bt.sh (target sco) and the host test run the same code
 * (docs/BLUETOOTH.md, step 7c). Written from Core 5.4: Vol 4, Part E,
 * 5.4.3 (HCI Synchronous Data packets), 7.7.5 (Disconnection Complete),
 * 7.7.35 (Synchronous Connection Complete), and Vol 4, Part B, 2.1 (the
 * USB alternate settings of the isochronous interface).
 *
 *	h2_voice_events		follows the event stream and reports a voice link
 *						that came up or went down;
 *	h2_voice_choose_alt	picks the alternate setting for a link;
 *	h2_sco_assembler	turns isochronous packets into HCI SCO packets;
 *	h2_sco_ring			keeps whole SCO packets for the reader;
 *	h2_sco_tx_check		checks one SCO packet from the reader before it is
 *						sent.
 * Every length from the radio is checked here; nothing here allocates.
 */
#ifndef _H2SCO_H_
#define _H2SCO_H_


#include <string.h>

#include <SupportDefs.h>


#define H2_EVENT_DISCONNECTION_COMPLETE		0x05
#define H2_EVENT_SYNC_CONNECTION_COMPLETE	0x2c
// Parameter lengths of the two events (7.7.5, 7.7.35).
#define H2_DISCONNECTION_COMPLETE_SIZE		4
#define H2_SYNC_CONNECTION_COMPLETE_SIZE	17

// Air_Mode of Synchronous Connection Complete (7.7.35).
#define H2_AIR_MODE_ULAW			0x00
#define H2_AIR_MODE_ALAW			0x01
#define H2_AIR_MODE_CVSD			0x02
#define H2_AIR_MODE_TRANSPARENT		0x03

// Link_Type (7.7.35).
#define H2_LINK_SCO					0x00
#define H2_LINK_ESCO				0x02

// An HCI SCO packet: handle and flags, length, at most 255 bytes of data.
#define H2_SCO_HEADER_SIZE			3
#define H2_SCO_MAX_PACKET			(H2_SCO_HEADER_SIZE + 255)
#define H2_HANDLE_MASK				0x0fff

// Bytes per USB frame each kind of link needs (Vol 4, Part B, 2.1.1):
// one channel of 8-bit data (transparent, as mSBC uses it: 64 kb/s) takes
// alternate setting 1 with 9 bytes, one channel of 16-bit linear PCM
// (CVSD with Voice_Setting 0x0060: 128 kb/s) setting 2 with 17 bytes.
#define H2_VOICE_BYTES_8BIT			9
#define H2_VOICE_BYTES_16BIT		17
// Bluetooth's settings stop at 63 bytes (setting 6); a larger endpoint is
// not a voice endpoint of this specification and is not used.
#define H2_VOICE_MAX_ISO_PACKET		64
#define H2_VOICE_MAX_ALTS			8


enum {
	H2_VOICE_LINK_UP = 1,
	H2_VOICE_LINK_DOWN = 2
};

struct h2_voice_event {
	uint8		kind;			// H2_VOICE_LINK_UP or H2_VOICE_LINK_DOWN
	uint8		status;
	uint16		handle;
	uint8		linkType;
	uint8		airMode;
	uint16		rxLength;
	uint16		txLength;
};


// #pragma mark - events


// Follows whole events through a byte stream cut anywhere (the event
// endpoint does not keep event boundaries, see h2command.h). Only the two
// events of a voice link are kept, at most 2 + 17 bytes.
struct h2_voice_events {
	uint8		event[2 + H2_SYNC_CONNECTION_COMPLETE_SIZE];
	uint8		have;
	uint8		left;
		// Bytes of an event that is not kept, still to come
	bool		keep;
};


static inline void
h2_voice_events_reset(h2_voice_events* stream)
{
	stream->have = 0;
	stream->left = 0;
	stream->keep = false;
}


static inline uint16
h2_le16(const uint8* data)
{
	return (uint16)(data[0] | (data[1] << 8));
}


// One whole event that was kept. Returns true and fills "out" when it is a
// voice link that came up (status 0, SCO or eSCO) or any disconnection.
static inline bool
h2_voice_parse_event(const uint8* event, size_t size, h2_voice_event* out)
{
	if (size < 2 || (size_t)event[1] + 2 != size)
		return false;

	if (event[0] == H2_EVENT_SYNC_CONNECTION_COMPLETE
		&& event[1] == H2_SYNC_CONNECTION_COMPLETE_SIZE) {
		// Status, handle, BD_ADDR (6, not kept), link type, transmission
		// interval, retransmission window, RX and TX packet length, air mode.
		const uint8* p = event + 2;
		out->kind = H2_VOICE_LINK_UP;
		out->status = p[0];
		out->handle = h2_le16(p + 1) & H2_HANDLE_MASK;
		out->linkType = p[9];
		out->rxLength = h2_le16(p + 12);
		out->txLength = h2_le16(p + 14);
		out->airMode = p[16];
		return out->status == 0
			&& (out->linkType == H2_LINK_SCO || out->linkType == H2_LINK_ESCO);
	}

	if (event[0] == H2_EVENT_DISCONNECTION_COMPLETE
		&& event[1] == H2_DISCONNECTION_COMPLETE_SIZE) {
		const uint8* p = event + 2;
		out->kind = H2_VOICE_LINK_DOWN;
		out->status = p[0];
		out->handle = h2_le16(p + 1) & H2_HANDLE_MASK;
		out->linkType = 0;
		out->airMode = 0;
		out->rxLength = 0;
		out->txLength = 0;
		return out->status == 0;
	}

	return false;
}


// Feeds bytes of the event stream; sink(const h2_voice_event&) is called
// for each voice link event. Returns the number of events reported.
template<typename Sink>
static inline int
h2_voice_events_feed(h2_voice_events* stream, const uint8* data, size_t size,
	Sink& sink)
{
	int reported = 0;
	size_t i = 0;
	while (i < size) {
		if (stream->left > 0) {
			size_t count = size - i;
			if (count > stream->left)
				count = stream->left;
			i += count;
			stream->left = (uint8)(stream->left - count);
			continue;
		}

		if (stream->have < 2) {
			stream->event[stream->have++] = data[i++];
			if (stream->have < 2)
				continue;
			uint8 code = stream->event[0];
			uint8 length = stream->event[1];
			stream->keep = (code == H2_EVENT_SYNC_CONNECTION_COMPLETE
					&& length == H2_SYNC_CONNECTION_COMPLETE_SIZE)
				|| (code == H2_EVENT_DISCONNECTION_COMPLETE
					&& length == H2_DISCONNECTION_COMPLETE_SIZE);
			if (!stream->keep) {
				stream->left = length;
				stream->have = 0;
				continue;
			}
		} else
			stream->event[stream->have++] = data[i++];

		// keep is true here, and the length fits the buffer.
		size_t whole = 2 + (size_t)stream->event[1];
		if (stream->have < whole)
			continue;

		h2_voice_event event;
		if (h2_voice_parse_event(stream->event, whole, &event)) {
			sink(event);
			reported++;
		}
		stream->have = 0;
		stream->keep = false;
	}
	return reported;
}


// #pragma mark - alternate setting


static inline uint16
h2_voice_bytes_per_frame(uint8 airMode)
{
	return airMode == H2_AIR_MODE_TRANSPARENT
		? H2_VOICE_BYTES_8BIT : H2_VOICE_BYTES_16BIT;
}


// inSize[a] and outSize[a] are the isochronous IN and OUT packet sizes of
// alternate setting a (0 when it has no such endpoint). Returns the setting
// with the smallest packets that still hold "need" bytes both ways, never
// setting 0 (no bandwidth) and never one above H2_VOICE_MAX_ISO_PACKET, or
// -1 when none fits.
static inline int
h2_voice_choose_alt(const uint16* inSize, const uint16* outSize, int count,
	uint16 need)
{
	int best = -1;
	if (count > H2_VOICE_MAX_ALTS)
		count = H2_VOICE_MAX_ALTS;
	for (int alt = 1; alt < count; alt++) {
		uint16 in = inSize[alt];
		uint16 out = outSize[alt];
		if (in < need || out < need || in > H2_VOICE_MAX_ISO_PACKET
			|| out > H2_VOICE_MAX_ISO_PACKET)
			continue;
		if (best < 0 || in < inSize[best])
			best = alt;
	}
	return best;
}


// #pragma mark - reassembly


// The isochronous IN endpoint cuts SCO packets at its packet size; a SCO
// packet starts at the start of an isochronous packet (Vol 4, Part B,
// 2.1.1). A packet that was lost, or a header with another handle, drops
// what was collected, and the next isochronous packet starts over.
struct h2_sco_assembler {
	uint8		packet[H2_SCO_MAX_PACKET];
	uint16		have;
	uint32		broken;
		// Packets dropped because of a gap or a wrong header
};


static inline void
h2_sco_assembler_reset(h2_sco_assembler* assembler)
{
	assembler->have = 0;
	assembler->broken = 0;
}


// One isochronous packet. ok is false when the bus reported an error for
// it. sink(const uint8* packet, size_t size) gets each whole SCO packet.
template<typename Sink>
static inline void
h2_sco_assemble(h2_sco_assembler* assembler, uint16 handle, const uint8* data,
	size_t size, bool ok, Sink& sink)
{
	if (!ok) {
		if (assembler->have > 0)
			assembler->broken++;
		assembler->have = 0;
		return;
	}

	size_t i = 0;
	while (i < size) {
		if (assembler->have < H2_SCO_HEADER_SIZE) {
			assembler->packet[assembler->have++] = data[i++];
			if (assembler->have < H2_SCO_HEADER_SIZE)
				continue;
			if ((h2_le16(assembler->packet) & H2_HANDLE_MASK) != handle) {
				// Not the start of one of our packets: drop the rest of
				// this isochronous packet.
				assembler->broken++;
				assembler->have = 0;
				return;
			}
		} else {
			size_t whole = H2_SCO_HEADER_SIZE + assembler->packet[2];
			size_t count = whole - assembler->have;
			if (count > size - i)
				count = size - i;
			memcpy(assembler->packet + assembler->have, data + i, count);
			assembler->have = (uint16)(assembler->have + count);
			i += count;
		}

		size_t whole = H2_SCO_HEADER_SIZE + assembler->packet[2];
		if (assembler->have == whole) {
			sink((const uint8*)assembler->packet, whole);
			assembler->have = 0;
		}
	}
}


// #pragma mark - ring


#define H2_SCO_RING_SLOTS			32

// Whole SCO packets for the reader. When it is full the oldest packet goes:
// late voice is worth less than the voice that is coming.
struct h2_sco_ring {
	uint16		length[H2_SCO_RING_SLOTS];
	uint8		data[H2_SCO_RING_SLOTS][H2_SCO_MAX_PACKET];
	uint32		head;
	uint32		count;
	uint32		dropped;
};


static inline void
h2_sco_ring_reset(h2_sco_ring* ring)
{
	ring->head = 0;
	ring->count = 0;
	ring->dropped = 0;
}


static inline bool
h2_sco_ring_push(h2_sco_ring* ring, const uint8* packet, size_t size)
{
	if (size < H2_SCO_HEADER_SIZE || size > H2_SCO_MAX_PACKET)
		return false;
	if (ring->count == H2_SCO_RING_SLOTS) {
		ring->head = (ring->head + 1) % H2_SCO_RING_SLOTS;
		ring->count--;
		ring->dropped++;
	}
	uint32 slot = (ring->head + ring->count) % H2_SCO_RING_SLOTS;
	memcpy(ring->data[slot], packet, size);
	ring->length[slot] = (uint16)size;
	ring->count++;
	return true;
}


// Takes the oldest packet into "out". Returns its size, 0 when the ring is
// empty, or -1 when "capacity" is too small (the packet stays).
static inline int
h2_sco_ring_pop(h2_sco_ring* ring, uint8* out, size_t capacity)
{
	if (ring->count == 0)
		return 0;
	uint32 slot = ring->head;
	size_t size = ring->length[slot];
	if (size > capacity)
		return -1;
	memcpy(out, ring->data[slot], size);
	ring->head = (ring->head + 1) % H2_SCO_RING_SLOTS;
	ring->count--;
	return (int)size;
}


// #pragma mark - transmit


// One SCO packet from the reader: a whole packet on the voice link's
// handle, with Packet_Status 0 (only the controller sets it).
static inline bool
h2_sco_tx_check(const uint8* packet, size_t size, uint16 handle)
{
	if (size < H2_SCO_HEADER_SIZE || size > H2_SCO_MAX_PACKET)
		return false;
	if ((size_t)packet[2] + H2_SCO_HEADER_SIZE != size)
		return false;
	uint16 head = h2_le16(packet);
	return (head & H2_HANDLE_MASK) == handle && (head >> 12) == 0;
}


#endif	// _H2SCO_H_
