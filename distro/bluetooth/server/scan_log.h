/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * One syslog line per device a scan hears (Classic inquiry answers and LE
 * advertisements), at most one when it is first heard and one when it first
 * goes to the scan list, per scan. Flat bytes and no Haiku dependencies, so
 * scripts/fuzz-bt.sh (target le) runs the same code on the host.
 *
 * The address is masked: only its last two bytes are printed (the same
 * device can be followed through one log, not looked up). No keys.
 */
#ifndef R5_BT_SCAN_LOG_H
#define R5_BT_SCAN_LOG_H


#include <stdio.h>
#include <string.h>

#include <SupportDefs.h>


// Devices one scan logs; the rest are only counted.
#define SCAN_LOG_MAX		48
// Bytes of a device name that go into a line.
#define SCAN_LOG_NAME_MAX	48


struct scan_log_entry {
	uint8	address[6];
	bool	listed;			// a line that says "to the list" was written
};

struct scan_log {
	scan_log_entry	entries[SCAN_LOG_MAX];
	uint32			count;		// entries in use
	uint32			heard;		// devices heard, also past SCAN_LOG_MAX
	uint32			listed;		// of those, sent to the list
	uint32			unlogged;	// devices past SCAN_LOG_MAX
};


static inline void
scan_log_reset(scan_log* log)
{
	memset(log, 0, sizeof(*log));
}


/*!	Notes one answer of a device. Returns true when it gets a line: the first
	answer of its address in this scan, or the first one that went to the
	list after answers that did not.
*/
static inline bool
scan_log_note(scan_log* log, const uint8 address[6], bool listed)
{
	for (uint32 i = 0; i < log->count; i++) {
		scan_log_entry& entry = log->entries[i];
		if (memcmp(entry.address, address, 6) != 0)
			continue;
		if (!listed || entry.listed)
			return false;
		entry.listed = true;
		log->listed++;
		return true;
	}
	if (log->count >= SCAN_LOG_MAX) {
		// Not remembered: counted once per answer, an upper bound.
		log->unlogged++;
		return false;
	}
	scan_log_entry& entry = log->entries[log->count++];
	memcpy(entry.address, address, 6);
	entry.listed = listed;
	log->heard++;
	if (listed)
		log->listed++;
	return true;
}


/*!	Writes the masked address ("xx:xx:xx:xx:B1:B0", B0 the lowest byte, as
	bdaddrUtils prints it last) and the name, cut at SCAN_LOG_NAME_MAX bytes on
	a UTF-8 boundary, with control characters and quotes replaced by '?'.
	Both buffers are always NUL-terminated.
*/
static inline void
scan_log_format(const uint8 address[6], const char* name,
	char addressText[18], char nameText[SCAN_LOG_NAME_MAX + 1])
{
	snprintf(addressText, 18, "xx:xx:xx:xx:%02x:%02x", address[1],
		address[0]);

	size_t length = 0;
	if (name != NULL) {
		while (name[length] != '\0' && length < SCAN_LOG_NAME_MAX)
			length++;
		// A cut name does not end inside a UTF-8 sequence.
		if (name[length] != '\0') {
			while (length > 0
				&& ((uint8)name[length] & 0xc0) == 0x80)
				length--;
		}
	}
	for (size_t i = 0; i < length; i++) {
		const uint8 c = (uint8)name[i];
		nameText[i] = c < 0x20 || c == 0x7f || c == '"' ? '?' : (char)c;
	}
	nameText[length] = '\0';
}


#endif	// R5_BT_SCAN_LOG_H
