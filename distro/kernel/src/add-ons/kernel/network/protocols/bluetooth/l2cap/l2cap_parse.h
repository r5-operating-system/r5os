/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The checks the L2CAP signalling path makes on what a remote device sends,
 * on flat bytes and without kernel dependencies, so scripts/fuzz-bt.sh runs
 * the same code on the host (target l2cap).
 *
 * A signalling packet holds commands of 4 header bytes (code, identifier,
 * 16-bit little-endian length) and their data (Core 6.0, Vol 3, Part A, 4).
 * Configuration options are type, length and value (Part A, 5).
 */
#ifndef _L2CAP_PARSE_H
#define _L2CAP_PARSE_H


#include <SupportDefs.h>


#define L2CAP_PARSE_COMMAND_HEADER	4
#define L2CAP_PARSE_OPTION_HEADER	2

// What the dispatcher does with one command.
enum {
	L2CAP_PARSE_HANDLE = 0,		// long enough: handle it
	L2CAP_PARSE_REJECT,			// shorter than its code needs: reject it, go on
	L2CAP_PARSE_TRUNCATED		// longer than what is left: stop the packet
};


// The fewest data bytes a command of this code carries (Part A, 4.1 to
// 4.11). An unknown code needs none; it is rejected as not understood.
static inline uint16
l2cap_parse_min_length(uint8 code)
{
	switch (code) {
		case 0x01:	return 2;	// Command Reject: reason
		case 0x02:	return 4;	// Connection Request: PSM, SCID
		case 0x03:	return 8;	// Connection Response
		case 0x04:	return 4;	// Configuration Request: DCID, flags
		case 0x05:	return 6;	// Configuration Response: SCID, flags, result
		case 0x06:	return 4;	// Disconnection Request
		case 0x07:	return 4;	// Disconnection Response
		case 0x08:	return 0;	// Echo Request
		case 0x09:	return 0;	// Echo Response
		case 0x0a:	return 2;	// Information Request: type
		case 0x0b:	return 4;	// Information Response: type, result
		// LE signalling and credit-based channels (4.20 to 4.27).
		case 0x12:	return 8;	// Connection Parameter Update Request
		case 0x13:	return 2;	// Connection Parameter Update Response
		case 0x14:	return 10;	// LE Credit Based Connection Request
		case 0x15:	return 10;	// LE Credit Based Connection Response
		case 0x16:	return 4;	// Flow Control Credit Indication
		case 0x17:	return 10;	// Credit Based Connection Request, 1 CID
		case 0x18:	return 8;	// Credit Based Connection Response
		case 0x19:	return 6;	// Credit Based Reconfigure Request, 1 CID
		case 0x1a:	return 2;	// Credit Based Reconfigure Response
	}
	return 0;
}


// Whether a code is an answer to a command of ours, which carries our
// identifier. Not simply odd: 0x18 is an answer, 0x19 is not. The answers
// to commands we never send (0x13, 0x1a) take no identifier, so a forged
// one cannot take that of a command that waits.
static inline bool
l2cap_parse_is_response(uint8 code)
{
	switch (code) {
		case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0b:
		case 0x15: case 0x18:
			return true;
	}
	return false;
}


// Which codes each signalling channel serves here. The BR/EDR channel
// (CID 0x0001) has the classic commands; the LE channel (CID 0x0005) has
// Command Reject, Disconnection and the LE commands (Part A, 4).
static inline bool
l2cap_parse_code_allowed(uint8 code, bool le)
{
	if (code == 0x01)
		return true;
	if (le)
		return code == 0x06 || code == 0x07 || (code >= 0x12 && code <= 0x1a);
	return code >= 0x02 && code <= 0x0b;
}


// The most channels one Credit Based Connection Request opens (4.25).
#define L2CAP_PARSE_ECRED_MAX_CHANNELS	5

// MTU and MPS of an LE credit-based channel: 23 to 65535 and 23 to 65533;
// for the enhanced mode both at least 64 (4.22, 4.25).
static inline bool
l2cap_parse_valid_coc(uint16 mtu, uint16 mps, bool enhanced)
{
	const uint16 least = enhanced ? 64 : 23;
	return mtu >= least && mps >= least && mps <= 65533;
}


// A dynamic channel of an LE link is 0x0040 to 0x007F (2.1).
static inline bool
l2cap_parse_valid_le_cid(uint16 cid)
{
	return cid >= 0x0040 && cid <= 0x007f;
}


// How many channels a Credit Based Connection Request (8 bytes and a
// source CID each) or Reconfigure Request (4 bytes and a CID each) names;
// 0 when the length does not fit 1 to 5 of them.
static inline int
l2cap_parse_cid_count(uint16 length, uint16 fixed)
{
	if (length < fixed + 2 || ((length - fixed) & 1) != 0)
		return 0;
	int count = (length - fixed) / 2;
	return count <= L2CAP_PARSE_ECRED_MAX_CHANNELS ? count : 0;
}


// Receiving K-frames of an LE credit-based channel (Part A, 3.4): the
// first frame of an SDU starts with its 16-bit length, the rest is data.
// No frame is longer than our MPS, no SDU longer than our MTU, and the
// frames of an SDU add up to exactly its length.
struct l2cap_coc_rx {
	uint16	mtu;		// ours
	uint16	mps;		// ours
	uint16	sdu_length;	// 0: no SDU in progress
	uint16	received;
};

enum {
	L2CAP_COC_ERROR		= -1,	// close the channel (4.24)
	L2CAP_COC_START		= 0x1,	// this frame starts an SDU
	L2CAP_COC_COMPLETE	= 0x2	// this frame ends an SDU
};


// "frame" is the frame after the basic header, at least min(size, 2)
// bytes of it; *_dataOffset is where its data starts (2 or 0).
static inline int
l2cap_coc_rx_frame(l2cap_coc_rx* rx, const uint8* frame, size_t size,
	size_t* _dataOffset)
{
	if (size > rx->mps)
		return L2CAP_COC_ERROR;

	int result = 0;
	size_t data = size;
	*_dataOffset = 0;
	if (rx->sdu_length == 0) {
		if (size < 2)
			return L2CAP_COC_ERROR;
		const uint16 length = frame[0] | (frame[1] << 8);
		if (length == 0 || length > rx->mtu)
			return L2CAP_COC_ERROR;
		data = size - 2;
		*_dataOffset = 2;
		rx->sdu_length = length;
		rx->received = 0;
		result |= L2CAP_COC_START;
	}

	if (data > (size_t)(rx->sdu_length - rx->received)) {
		rx->sdu_length = 0;
		return L2CAP_COC_ERROR;
	}
	rx->received += data;
	if (rx->received == rx->sdu_length) {
		rx->sdu_length = 0;
		rx->received = 0;
		result |= L2CAP_COC_COMPLETE;
	}
	return result;
}


// "remaining" is what is left of the packet after this command's header.
static inline int
l2cap_parse_check(uint8 code, uint16 length, size_t remaining)
{
	if (length > remaining)
		return L2CAP_PARSE_TRUNCATED;
	if (length < l2cap_parse_min_length(code))
		return L2CAP_PARSE_REJECT;
	return L2CAP_PARSE_HANDLE;
}


struct l2cap_parse_option {
	uint8			type;		// without the hint bit
	bool			hint;
	uint8			length;
	const uint8*	value;
};


// The next configuration option in data[*offset, size). Returns false at
// the end, or when an option runs past "size" (then *_malformed is true).
// The value pointer stays inside data.
static inline bool
l2cap_parse_next_option(const uint8* data, size_t size, size_t* offset,
	l2cap_parse_option* option, bool* _malformed)
{
	*_malformed = false;
	if (*offset >= size)
		return false;
	if (size - *offset < L2CAP_PARSE_OPTION_HEADER) {
		*_malformed = true;
		return false;
	}

	const uint8* header = data + *offset;
	uint8 length = header[1];
	if (size - *offset - L2CAP_PARSE_OPTION_HEADER < length) {
		*_malformed = true;
		return false;
	}

	option->type = header[0] & 0x7f;
	option->hint = (header[0] & 0x80) != 0;
	option->length = length;
	option->value = header + L2CAP_PARSE_OPTION_HEADER;
	*offset += L2CAP_PARSE_OPTION_HEADER + length;
	return true;
}


// A PSM is odd, and the lowest bit of its upper byte is 0 (Part A, 4.2).
static inline bool
l2cap_parse_valid_psm(uint16 psm)
{
	return (psm & 0x0001) != 0 && (psm & 0x0100) == 0;
}


// Fixed channels below the first dynamic CID (Part A, 2.1): 0x0001 BR/EDR
// signalling, 0x0002 connectionless, 0x0004 ATT, 0x0005 LE signalling,
// 0x0006 SMP, 0x0007 BR/EDR SMP. Dynamic channels start at 0x0040.
static inline bool
l2cap_parse_is_fixed_channel(uint16 cid)
{
	return cid != 0 && cid < 0x0040;
}


#endif	// _L2CAP_PARSE_H
