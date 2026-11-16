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
	}
	return 0;
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
