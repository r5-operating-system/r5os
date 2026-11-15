/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Reassembly of the HCI events, ACL and SCO packets that a transport hands
 * over in pieces of any size. It has no kernel dependencies, so
 * scripts/fuzz-bt.sh runs the same code on the host.
 *
 * Every piece of data is consumed: a header split over two pieces is
 * collected first, a packet split over several pieces is appended until it
 * is complete, and a piece that holds several packets is split. Nothing
 * here panics on what a radio sends; a packet that cannot be stored is
 * dropped and the next byte starts a new packet.
 *
 * Ops supplies the storage (the kernel uses net_buffers):
 *	Buffer	the buffer type, a pointer
 *	Buffer	Current(type) / void SetCurrent(type, Buffer)
 *	Buffer	Create()			may return NULL
 *	bool	Append(Buffer, const uint8*, size_t)
 *	size_t	Size(Buffer)
 *	bool	Read(Buffer, size_t offset, uint8*, size_t)
 *	void	Destroy(Buffer)
 *	void	PostEvent(const uint8* event, size_t size)	flat copy
 *	void	PostAcl(Buffer)		takes the buffer
 */
#ifndef _HCI_ASSEMBLE_H
#define _HCI_ASSEMBLE_H


#include <SupportDefs.h>


// The same values as bt_packet_t in <bluetooth/HCI/btHCI.h>.
enum {
	HCI_ASSEMBLE_EVENT = 1,
	HCI_ASSEMBLE_ACL = 2,
	HCI_ASSEMBLE_SCO = 3
};

// Header sizes: event code and length; handle and 16-bit length; handle
// and 8-bit length (Core 6.0, Vol 4, Part E, 5.4).
static const size_t kHciEventHeader = 2;
static const size_t kHciAclHeader = 4;
static const size_t kHciScoHeader = 3;
static const size_t kHciMaxEvent = 2 + 255;


static inline size_t
hci_assemble_header_size(uint8 type)
{
	switch (type) {
		case HCI_ASSEMBLE_EVENT:
			return kHciEventHeader;
		case HCI_ASSEMBLE_ACL:
			return kHciAclHeader;
		case HCI_ASSEMBLE_SCO:
			return kHciScoHeader;
	}
	return 0;
}


// The whole packet size given its complete header.
static inline size_t
hci_assemble_packet_size(uint8 type, const uint8* header)
{
	switch (type) {
		case HCI_ASSEMBLE_EVENT:
			return kHciEventHeader + header[1];
		case HCI_ASSEMBLE_ACL:
			return kHciAclHeader + (header[2] | (header[3] << 8));
		case HCI_ASSEMBLE_SCO:
			return kHciScoHeader + header[2];
	}
	return 0;
}


template<typename Ops>
static void
hci_assemble_complete(Ops& ops, uint8 type, typename Ops::Buffer buffer)
{
	ops.SetCurrent(type, NULL);

	if (type == HCI_ASSEMBLE_EVENT) {
		uint8 event[kHciMaxEvent];
		size_t size = ops.Size(buffer);
		if (size <= sizeof(event) && ops.Read(buffer, 0, event, size))
			ops.PostEvent(event, size);
		ops.Destroy(buffer);
	} else if (type == HCI_ASSEMBLE_ACL) {
		ops.PostAcl(buffer);
	} else {
		// Nothing consumes SCO from here yet.
		ops.Destroy(buffer);
	}
}


// Returns B_OK, B_BAD_VALUE for a packet type it does not know (the data is
// ignored), or B_NO_MEMORY when a packet had to be dropped.
template<typename Ops>
static status_t
hci_assemble(Ops& ops, uint8 type, const uint8* data, size_t count)
{
	const size_t headerSize = hci_assemble_header_size(type);
	if (headerSize == 0)
		return B_BAD_VALUE;

	status_t result = B_OK;

	while (count > 0) {
		typename Ops::Buffer buffer = ops.Current(type);

		if (buffer == NULL) {
			// A whole event in one piece goes up without a copy.
			if (type == HCI_ASSEMBLE_EVENT && count >= headerSize) {
				size_t size = hci_assemble_packet_size(type, data);
				if (count >= size) {
					ops.PostEvent(data, size);
					data += size;
					count -= size;
					continue;
				}
			}

			buffer = ops.Create();
			if (buffer == NULL) {
				// Nothing to keep the start of this packet in: drop this
				// piece. A later piece then starts a new packet.
				return B_NO_MEMORY;
			}
			ops.SetCurrent(type, buffer);
		}

		size_t have = ops.Size(buffer);
		size_t target = headerSize;
		if (have >= headerSize) {
			uint8 header[kHciAclHeader];
			if (!ops.Read(buffer, 0, header, headerSize)) {
				ops.SetCurrent(type, NULL);
				ops.Destroy(buffer);
				result = B_NO_MEMORY;
				continue;
			}
			target = hci_assemble_packet_size(type, header);
		}

		// have < target here: a buffer is completed as soon as it is full.
		size_t take = target - have;
		if (take > count)
			take = count;

		if (!ops.Append(buffer, data, take)) {
			ops.SetCurrent(type, NULL);
			ops.Destroy(buffer);
			data += take;
			count -= take;
			result = B_NO_MEMORY;
			continue;
		}
		data += take;
		count -= take;
		have += take;

		if (have == headerSize) {
			uint8 header[kHciAclHeader];
			if (ops.Read(buffer, 0, header, headerSize))
				target = hci_assemble_packet_size(type, header);
		}

		if (have >= headerSize && have == target)
			hci_assemble_complete(ops, type, buffer);
	}

	return result;
}


#endif	// _HCI_ASSEMBLE_H
