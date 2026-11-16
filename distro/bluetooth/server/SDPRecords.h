/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The SDP answers of bluetooth_server (Core 6.0, Vol 3, Part B): fixed
 * records for what R5 offers, so a headset that looks before it connects
 * finds them. Plain C++ with <stdint.h> only, so scripts/fuzz-bt.sh builds
 * the same code on the host. Every parser takes a length and never reads
 * past it; nothing here allocates.
 *
 * Records:
 *	0x00010001	A2DP Audio Source (0x110A), AVDTP 1.3 on PSM 0x19
 *	0x00010002	AVRCP Controller (0x110E, 0x110F), AVCTP 1.4 on PSM 0x17,
 *			category 2 (the volume of the headset)
 *	0x00010003	AVRCP Target (0x110C), category 1 (play and pause)
 */
#ifndef R5_SDP_RECORDS_H
#define R5_SDP_RECORDS_H


#include <stddef.h>
#include <stdint.h>
#include <string.h>


namespace r5sdp {


enum {
	kErrorResponse = 0x01,
	kServiceSearchRequest = 0x02,
	kServiceSearchResponse = 0x03,
	kServiceAttributeRequest = 0x04,
	kServiceAttributeResponse = 0x05,
	kServiceSearchAttributeRequest = 0x06,
	kServiceSearchAttributeResponse = 0x07
};

enum {
	kErrorInvalidRecordHandle = 0x0002,
	kErrorInvalidSyntax = 0x0003,
	kErrorInvalidPDUSize = 0x0004,
	kErrorInvalidContinuation = 0x0005
};

// At most this much attribute data per answer, below the default L2CAP
// MTU of 672 with the PDU header around it.
static const size_t kMaxChunk = 600;
static const int kMaxPatternUUIDs = 12;		// Part B, 4.5.1
static const int kMaxRanges = 16;


struct Record {
	uint32_t	handle;
	uint16_t	classes[2];
	int			classCount;
	uint16_t	psm;
	uint16_t	protocol;			// AVDTP 0x0019 or AVCTP 0x0017
	uint16_t	protocolVersion;
	uint16_t	profile;
	uint16_t	profileVersion;
	uint16_t	features;
};

static const Record kRecords[] = {
	{ 0x00010001, { 0x110a, 0 }, 1, 0x0019, 0x0019, 0x0103, 0x110d, 0x0103,
		0x0001 },
	{ 0x00010002, { 0x110e, 0x110f }, 2, 0x0017, 0x0017, 0x0104, 0x110e,
		0x0106, 0x0002 },
	{ 0x00010003, { 0x110c, 0 }, 1, 0x0017, 0x0017, 0x0104, 0x110e, 0x0106,
		0x0001 },
};
static const int kRecordCount = sizeof(kRecords) / sizeof(kRecords[0]);

static const uint16_t kUUIDL2CAP = 0x0100;
static const uint16_t kUUIDPublicBrowse = 0x1002;


// #pragma mark - Writing data elements


struct Writer {
	uint8_t*	data;
	size_t		size;
	size_t		length;
	bool		ok;

	Writer(uint8_t* _data, size_t _size)
		:
		data(_data), size(_size), length(0), ok(true)
	{
	}

	void Byte(uint8_t value)
	{
		if (length < size)
			data[length] = value;
		else
			ok = false;
		length++;
	}

	void U16(uint16_t value)
	{
		Byte((uint8_t)(value >> 8));
		Byte((uint8_t)value);
	}

	void U32(uint32_t value)
	{
		U16((uint16_t)(value >> 16));
		U16((uint16_t)value);
	}

	void UInt16Element(uint16_t value)
	{
		Byte(0x09);
		U16(value);
	}

	void UInt32Element(uint32_t value)
	{
		Byte(0x0a);
		U32(value);
	}

	void UUID16Element(uint16_t value)
	{
		Byte(0x19);
		U16(value);
	}

	// A sequence with a 16-bit length, filled in by End().
	size_t Begin()
	{
		Byte(0x36);
		size_t at = length;
		U16(0);
		return at;
	}

	void End(size_t at)
	{
		size_t content = length - at - 2;
		if (content > 0xffff) {
			ok = false;
			return;
		}
		if (at + 1 < size) {
			data[at] = (uint8_t)(content >> 8);
			data[at + 1] = (uint8_t)content;
		}
	}
};


struct Range {
	uint16_t	low;
	uint16_t	high;
};


static inline bool
in_ranges(uint16_t id, const Range* ranges, int count)
{
	for (int i = 0; i < count; i++) {
		if (id >= ranges[i].low && id <= ranges[i].high)
			return true;
	}
	return false;
}


// One record's attribute list (a sequence of ID, value pairs in rising ID
// order), only the attributes in the ranges.
static inline void
write_record(Writer& w, const Record& record, const Range* ranges,
	int rangeCount)
{
	size_t list = w.Begin();
	if (in_ranges(0x0000, ranges, rangeCount)) {
		w.UInt16Element(0x0000);	// ServiceRecordHandle
		w.UInt32Element(record.handle);
	}
	if (in_ranges(0x0001, ranges, rangeCount)) {
		w.UInt16Element(0x0001);	// ServiceClassIDList
		size_t classes = w.Begin();
		for (int i = 0; i < record.classCount; i++)
			w.UUID16Element(record.classes[i]);
		w.End(classes);
	}
	if (in_ranges(0x0004, ranges, rangeCount)) {
		w.UInt16Element(0x0004);	// ProtocolDescriptorList
		size_t protocols = w.Begin();
		size_t l2cap = w.Begin();
		w.UUID16Element(kUUIDL2CAP);
		w.UInt16Element(record.psm);
		w.End(l2cap);
		size_t upper = w.Begin();
		w.UUID16Element(record.protocol);
		w.UInt16Element(record.protocolVersion);
		w.End(upper);
		w.End(protocols);
	}
	if (in_ranges(0x0005, ranges, rangeCount)) {
		w.UInt16Element(0x0005);	// BrowseGroupList
		size_t browse = w.Begin();
		w.UUID16Element(kUUIDPublicBrowse);
		w.End(browse);
	}
	if (in_ranges(0x0009, ranges, rangeCount)) {
		w.UInt16Element(0x0009);	// BluetoothProfileDescriptorList
		size_t profiles = w.Begin();
		size_t profile = w.Begin();
		w.UUID16Element(record.profile);
		w.UInt16Element(record.profileVersion);
		w.End(profile);
		w.End(profiles);
	}
	if (in_ranges(0x0311, ranges, rangeCount)) {
		w.UInt16Element(0x0311);	// SupportedFeatures
		w.UInt16Element(record.features);
	}
	w.End(list);
}


// Whether every UUID of the pattern is somewhere in the record (4.5.1).
static inline bool
record_matches(const Record& record, const uint32_t* uuids, int count)
{
	for (int i = 0; i < count; i++) {
		uint32_t uuid = uuids[i];
		bool found = uuid == kUUIDL2CAP || uuid == kUUIDPublicBrowse
			|| uuid == record.protocol || uuid == record.profile;
		for (int c = 0; c < record.classCount && !found; c++)
			found = uuid == record.classes[c];
		if (!found)
			return false;
	}
	return true;
}


// #pragma mark - Reading a request


struct Reader {
	const uint8_t*	data;
	size_t			size;
	size_t			offset;

	bool Left(size_t count) const
	{
		return offset <= size && size - offset >= count;
	}

	bool Byte(uint8_t* value)
	{
		if (!Left(1))
			return false;
		*value = data[offset++];
		return true;
	}

	bool U16(uint16_t* value)
	{
		if (!Left(2))
			return false;
		*value = (uint16_t)((data[offset] << 8) | data[offset + 1]);
		offset += 2;
		return true;
	}

	bool U32(uint32_t* value)
	{
		uint16_t high;
		uint16_t low;
		if (!U16(&high) || !U16(&low))
			return false;
		*value = ((uint32_t)high << 16) | low;
		return true;
	}

	// A sequence header; *_length is the content length.
	bool Sequence(size_t* _length)
	{
		uint8_t header;
		if (!Byte(&header))
			return false;
		if (header == 0x35) {
			uint8_t length;
			if (!Byte(&length))
				return false;
			*_length = length;
		} else if (header == 0x36) {
			uint16_t length;
			if (!U16(&length))
				return false;
			*_length = length;
		} else if (header == 0x37) {
			uint32_t length;
			if (!U32(&length))
				return false;
			*_length = length;
		} else
			return false;
		return Left(*_length);
	}
};


// The Bluetooth Base UUID's last 12 bytes (Part B, 2.5.1).
static const uint8_t kBaseUUIDTail[12] = { 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb };


static inline bool
read_pattern(Reader& r, uint32_t* uuids, int* _count)
{
	size_t length;
	if (!r.Sequence(&length))
		return false;
	size_t end = r.offset + length;
	int count = 0;
	while (r.offset < end) {
		uint8_t type;
		if (!r.Byte(&type))
			return false;
		uint32_t uuid;
		if (type == 0x19) {
			uint16_t value;
			if (r.offset + 2 > end || !r.U16(&value))
				return false;
			uuid = value;
		} else if (type == 0x1a) {
			if (r.offset + 4 > end || !r.U32(&uuid))
				return false;
		} else if (type == 0x1c) {
			if (r.offset + 16 > end || !r.U32(&uuid))
				return false;
			// A UUID outside the Bluetooth base matches nothing here.
			if (memcmp(r.data + r.offset, kBaseUUIDTail, 12) != 0)
				uuid = 0xffffffff;
			r.offset += 12;
		} else
			return false;
		if (count >= kMaxPatternUUIDs)
			return false;
		uuids[count++] = uuid;
	}
	*_count = count;
	return count > 0;
}


static inline bool
read_ranges(Reader& r, Range* ranges, int* _count)
{
	size_t length;
	if (!r.Sequence(&length))
		return false;
	size_t end = r.offset + length;
	int count = 0;
	while (r.offset < end) {
		uint8_t type;
		if (!r.Byte(&type))
			return false;
		Range range;
		if (type == 0x09) {
			if (r.offset + 2 > end || !r.U16(&range.low))
				return false;
			range.high = range.low;
		} else if (type == 0x0a) {
			if (r.offset + 4 > end || !r.U16(&range.low)
				|| !r.U16(&range.high))
				return false;
		} else
			return false;
		if (count >= kMaxRanges)
			return false;
		ranges[count++] = range;
	}
	*_count = count;
	return count > 0;
}


// Continuation state: R5 uses two bytes, the offset into the answer.
static inline bool
read_continuation(Reader& r, uint16_t* _offset)
{
	uint8_t length;
	if (!r.Byte(&length))
		return false;
	if (length == 0) {
		*_offset = 0;
		return r.offset == r.size;
	}
	if (length != 2 || !r.U16(_offset))
		return false;
	return r.offset == r.size;
}


// #pragma mark - Answering


static inline size_t
error_response(uint8_t* out, size_t size, uint16_t transaction,
	uint16_t error)
{
	if (size < 7)
		return 0;
	out[0] = kErrorResponse;
	out[1] = (uint8_t)(transaction >> 8);
	out[2] = (uint8_t)transaction;
	out[3] = 0;
	out[4] = 2;
	out[5] = (uint8_t)(error >> 8);
	out[6] = (uint8_t)error;
	return 7;
}


// One part of an attribute answer: byte count, the bytes from "offset",
// continuation state. full/fullLength is the whole answer.
static inline size_t
chunk_response(uint8_t* out, size_t size, uint8_t pdu, uint16_t transaction,
	const uint8_t* full, size_t fullLength, uint16_t offset,
	uint16_t maxBytes)
{
	if (offset > fullLength)
		return error_response(out, size, transaction,
			kErrorInvalidContinuation);
	size_t limit = maxBytes < kMaxChunk ? maxBytes : kMaxChunk;
	size_t count = fullLength - offset;
	if (count > limit)
		count = limit;
	bool more = offset + count < fullLength;
	size_t parameters = 2 + count + (more ? 3 : 1);
	if (size < 5 + parameters)
		return 0;
	out[0] = pdu;
	out[1] = (uint8_t)(transaction >> 8);
	out[2] = (uint8_t)transaction;
	out[3] = (uint8_t)(parameters >> 8);
	out[4] = (uint8_t)parameters;
	out[5] = (uint8_t)(count >> 8);
	out[6] = (uint8_t)count;
	memcpy(out + 7, full + offset, count);
	size_t i = 7 + count;
	if (more) {
		uint16_t next = (uint16_t)(offset + count);
		out[i++] = 2;
		out[i++] = (uint8_t)(next >> 8);
		out[i++] = (uint8_t)next;
	} else
		out[i++] = 0;
	return i;
}


// Answers one SDP request PDU. Returns the length of the answer in out, 0
// when out is too small for any answer.
static inline size_t
HandleRequest(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize)
{
	if (in == NULL || inSize < 5)
		return 0;
	uint8_t pdu = in[0];
	uint16_t transaction = (uint16_t)((in[1] << 8) | in[2]);
	uint16_t parameterLength = (uint16_t)((in[3] << 8) | in[4]);
	if ((size_t)parameterLength != inSize - 5)
		return error_response(out, outSize, transaction,
			kErrorInvalidPDUSize);

	Reader r = { in + 5, inSize - 5, 0 };
	uint32_t uuids[kMaxPatternUUIDs];
	int uuidCount = 0;
	Range ranges[kMaxRanges];
	int rangeCount = 0;
	uint16_t maxCount = 0;
	uint16_t continuation = 0;
	uint8_t full[1024];

	switch (pdu) {
		case kServiceSearchRequest:
		{
			if (!read_pattern(r, uuids, &uuidCount) || !r.U16(&maxCount)
				|| !read_continuation(r, &continuation) || continuation != 0)
				return error_response(out, outSize, transaction,
					kErrorInvalidSyntax);
			uint32_t handles[kRecordCount];
			uint16_t found = 0;
			for (int i = 0; i < kRecordCount && found < maxCount; i++) {
				if (record_matches(kRecords[i], uuids, uuidCount))
					handles[found++] = kRecords[i].handle;
			}
			size_t parameters = 4 + 4 * (size_t)found + 1;
			if (outSize < 5 + parameters)
				return 0;
			Writer w(out, outSize);
			w.Byte(kServiceSearchResponse);
			w.U16(transaction);
			w.U16((uint16_t)parameters);
			w.U16(found);
			w.U16(found);
			for (uint16_t i = 0; i < found; i++)
				w.U32(handles[i]);
			w.Byte(0);
			return w.ok ? w.length : 0;
		}

		case kServiceAttributeRequest:
		{
			uint32_t handle;
			if (!r.U32(&handle) || !r.U16(&maxCount) || maxCount < 7
				|| !read_ranges(r, ranges, &rangeCount)
				|| !read_continuation(r, &continuation))
				return error_response(out, outSize, transaction,
					kErrorInvalidSyntax);
			const Record* record = NULL;
			for (int i = 0; i < kRecordCount; i++) {
				if (kRecords[i].handle == handle)
					record = &kRecords[i];
			}
			if (record == NULL)
				return error_response(out, outSize, transaction,
					kErrorInvalidRecordHandle);
			Writer w(full, sizeof(full));
			write_record(w, *record, ranges, rangeCount);
			if (!w.ok)
				return 0;
			return chunk_response(out, outSize, kServiceAttributeResponse,
				transaction, full, w.length, continuation, maxCount);
		}

		case kServiceSearchAttributeRequest:
		{
			if (!read_pattern(r, uuids, &uuidCount) || !r.U16(&maxCount)
				|| maxCount < 7 || !read_ranges(r, ranges, &rangeCount)
				|| !read_continuation(r, &continuation))
				return error_response(out, outSize, transaction,
					kErrorInvalidSyntax);
			Writer w(full, sizeof(full));
			size_t lists = w.Begin();
			for (int i = 0; i < kRecordCount; i++) {
				if (record_matches(kRecords[i], uuids, uuidCount))
					write_record(w, kRecords[i], ranges, rangeCount);
			}
			w.End(lists);
			if (!w.ok)
				return 0;
			return chunk_response(out, outSize,
				kServiceSearchAttributeResponse, transaction, full, w.length,
				continuation, maxCount);
		}

		default:
			return error_response(out, outSize, transaction,
				kErrorInvalidSyntax);
	}
}


}	// namespace r5sdp


#endif	// R5_SDP_RECORDS_H
