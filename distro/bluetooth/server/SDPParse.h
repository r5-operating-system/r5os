/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The SDP answers of a remote device, read by bluetooth_server
 * (SDPClient.cpp): the pieces of a Service Search Attribute Response, the
 * service records in them and the attributes of a record. Written from
 * Core 5.4, Vol 3, Part B, sections 3 (data elements) and 4.7 (Service
 * Search Attribute transaction). No third-party code read.
 *
 * Every length comes from the remote device and is checked against what
 * arrived. One attribute this code does not know is stepped over by its
 * length; it does not cost the record, and a bad record does not cost the
 * records after it when its own length is still sound.
 *
 * Plain C++ without the Haiku kits, so scripts/bt-sdp-fuzz.cpp tests it
 * on the host.
 */
#ifndef _R5_SDP_PARSE_H
#define _R5_SDP_PARSE_H


#include <stddef.h>
#include <stdint.h>
#include <string.h>


namespace r5sdpparse {


// Data element types (the upper five bits of the header byte, 3.2).
enum {
	kTypeNil		= 0,
	kTypeUnsigned	= 1,
	kTypeSigned		= 2,
	kTypeUUID		= 3,
	kTypeString		= 4,
	kTypeBool		= 5,
	kTypeSequence	= 6,
	kTypeAlternative = 7,
	kTypeURL		= 8
};

static const uint8_t kPduError = 0x01;
static const uint8_t kPduSearchAttributeResponse = 0x07;

// Hands-Free unit (HFP 1.8, 5.3): the service class a headset offers.
static const uint16_t kServiceHandsFree = 0x111e;

// The most service classes one log line names.
static const size_t kMaxLoggedClasses = 16;


struct Element {
	uint8_t			type;
	uint8_t			sizeIndex;
	const uint8_t*	start;		// the header byte
	size_t			total;		// header, length bytes and value
	const uint8_t*	value;
	size_t			length;
};


/*!	Reads the data element at *offset and moves *offset past it. False when
	it does not fit in size or its header is not a valid one (3.3).
*/
inline bool
NextElement(const uint8_t* data, size_t size, size_t* offset,
	Element* element)
{
	if (data == NULL || *offset >= size)
		return false;
	const size_t begin = *offset;
	const uint8_t header = data[begin];
	const uint8_t type = header >> 3;
	const uint8_t index = header & 7;
	size_t position = begin + 1;
	size_t length;

	if (type > kTypeURL)
		return false;
	if (index <= 4) {
		// Nil has size index 0 and no value. Strings, sequences,
		// alternatives and URLs always carry their length.
		if (type == kTypeNil)
			length = 0;
		else if (type == kTypeString || type == kTypeSequence
			|| type == kTypeAlternative || type == kTypeURL)
			return false;
		else {
			static const size_t kSizes[5] = { 1, 2, 4, 8, 16 };
			length = kSizes[index];
		}
		if (type == kTypeNil && index != 0)
			return false;
		if (type == kTypeBool && index != 0)
			return false;
		if (type == kTypeUUID && index != 1 && index != 2 && index != 4)
			return false;
	} else {
		if (type != kTypeString && type != kTypeSequence
			&& type != kTypeAlternative && type != kTypeURL)
			return false;
		const size_t bytes = index == 5 ? 1 : index == 6 ? 2 : 4;
		if (size - position < bytes)
			return false;
		length = 0;
		for (size_t i = 0; i < bytes; i++)
			length = (length << 8) | data[position++];
	}
	if (size - position < length)
		return false;

	element->type = type;
	element->sizeIndex = index;
	element->start = data + begin;
	element->value = data + position;
	element->length = length;
	element->total = position + length - begin;
	*offset = position + length;
	return true;
}


//!	An unsigned integer element of up to 4 bytes, as a number.
inline bool
UnsignedValue(const Element& element, uint32_t* value)
{
	if (element.type != kTypeUnsigned || element.length > 4)
		return false;
	uint32_t result = 0;
	for (size_t i = 0; i < element.length; i++)
		result = (result << 8) | element.value[i];
	*value = result;
	return true;
}


/*!	The 16-bit form of a UUID element: a 16-bit UUID, or a 32-bit or
	128-bit one on the Bluetooth Base UUID (2.5.1) whose value fits.
*/
inline bool
ShortUUID(const Element& element, uint16_t* uuid)
{
	if (element.type != kTypeUUID)
		return false;
	if (element.length == 2) {
		*uuid = (uint16_t)((element.value[0] << 8) | element.value[1]);
		return true;
	}
	if (element.length == 4) {
		if (element.value[0] != 0 || element.value[1] != 0)
			return false;
		*uuid = (uint16_t)((element.value[2] << 8) | element.value[3]);
		return true;
	}
	if (element.length == 16) {
		static const uint8_t kBase[12] = { 0x00, 0x00, 0x10, 0x00, 0x80,
			0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb };
		if (element.value[0] != 0 || element.value[1] != 0
			|| memcmp(element.value + 4, kBase, sizeof(kBase)) != 0)
			return false;
		*uuid = (uint16_t)((element.value[2] << 8) | element.value[3]);
		return true;
	}
	return false;
}


/*!	One piece of a Service Search Attribute Response (4.7.2): the part of
	the attribute lists it carries and its continuation state. False for an
	Error Response, another PDU, or lengths that do not fit.
*/
inline bool
ReadAnswerPiece(const uint8_t* bytes, size_t received,
	const uint8_t** lists, size_t* listsLength,
	const uint8_t** continuation, size_t* continuationLength)
{
	// PDU ID, transaction ID (2), parameter length (2); then the byte
	// count of the lists (2), the lists and the continuation state.
	if (bytes == NULL || received < 5 + 2 + 1
		|| bytes[0] != kPduSearchAttributeResponse)
		return false;
	const size_t parameters = ((size_t)bytes[3] << 8) | bytes[4];
	if (parameters > received - 5)
		return false;
	const size_t end = 5 + parameters;
	size_t position = 5;
	if (end - position < 2 + 1)
		return false;
	const size_t count = ((size_t)bytes[position] << 8) | bytes[position + 1];
	position += 2;
	if (count > end - position - 1)
		return false;
	*lists = bytes + position;
	*listsLength = count;
	position += count;
	const size_t state = bytes[position++];
	if (state > 16 || state > end - position)
		return false;
	*continuation = bytes + position;
	*continuationLength = state;
	return true;
}


/*!	Calls record(value, length) for each service record (an attribute list,
	a sequence) in the attribute lists of a whole answer, and returns how
	many it found. A record whose element is sound but is no sequence is
	stepped over; parsing ends where the elements stop making sense.
*/
template<typename Callback>
inline size_t
ForEachRecord(const uint8_t* lists, size_t size, Callback record)
{
	size_t offset = 0;
	Element outer;
	if (!NextElement(lists, size, &offset, &outer)
		|| outer.type != kTypeSequence)
		return 0;
	size_t count = 0;
	size_t inner = 0;
	Element list;
	while (inner < outer.length
		&& NextElement(outer.value, outer.length, &inner, &list)) {
		if (list.type != kTypeSequence)
			continue;
		record(list.value, list.length);
		count++;
	}
	return count;
}


/*!	Calls attribute(id, value) for each attribute of one record: an
	unsigned 16-bit attribute ID followed by any one data element. A pair
	whose ID is not such an element ends the record; the attributes read
	so far stay.
*/
template<typename Callback>
inline size_t
ForEachAttribute(const uint8_t* record, size_t size, Callback attribute)
{
	size_t offset = 0;
	size_t count = 0;
	while (offset < size) {
		Element id;
		Element value;
		uint32_t number;
		if (!NextElement(record, size, &offset, &id)
			|| id.type != kTypeUnsigned || id.length != 2
			|| !UnsignedValue(id, &number)
			|| !NextElement(record, size, &offset, &value))
			break;
		attribute((uint16_t)number, value);
		count++;
	}
	return count;
}


/*!	The first service class of a ServiceClassIDList (attribute 0x0001, a
	sequence of UUIDs) as a 16-bit UUID; 0 when it has none in that form.
	value and length are those of the sequence's contents.
*/
inline uint16_t
FirstServiceClass(const uint8_t* value, size_t length)
{
	size_t offset = 0;
	Element uuid;
	while (offset < length && NextElement(value, length, &offset, &uuid)) {
		uint16_t shortUUID;
		if (ShortUUID(uuid, &shortUUID))
			return shortUUID;
	}
	return 0;
}


//!	Whether a ServiceClassIDList names this 16-bit service class.
inline bool
HasServiceClass(const uint8_t* value, size_t length, uint16_t serviceClass)
{
	size_t offset = 0;
	Element uuid;
	while (offset < length && NextElement(value, length, &offset, &uuid)) {
		uint16_t shortUUID;
		if (ShortUUID(uuid, &shortUUID) && shortUUID == serviceClass)
			return true;
	}
	return false;
}


/*!	The parameters of a Service Search Attribute Request (4.7.1) for one
	16-bit service class and a list of attribute IDs, without the
	continuation state: the search pattern, the most attribute bytes per
	answer and the attribute ID list. Returns the length, 0 when out is
	too small.
*/
inline size_t
BuildSearchAttributes(uint16_t serviceClass, const uint16_t* attributes,
	size_t attributeCount, uint16_t maxBytes, uint8_t* out, size_t outSize)
{
	const size_t needed = 5 + 2 + 2 + 3 * attributeCount;
	if (out == NULL || attributeCount == 0 || attributeCount > 80
		|| outSize < needed)
		return 0;
	size_t p = 0;
	out[p++] = 0x35;
	out[p++] = 3;
	out[p++] = 0x19;
	out[p++] = (uint8_t)(serviceClass >> 8);
	out[p++] = (uint8_t)serviceClass;
	out[p++] = (uint8_t)(maxBytes >> 8);
	out[p++] = (uint8_t)maxBytes;
	out[p++] = 0x35;
	out[p++] = (uint8_t)(3 * attributeCount);
	for (size_t i = 0; i < attributeCount; i++) {
		out[p++] = 0x09;
		out[p++] = (uint8_t)(attributes[i] >> 8);
		out[p++] = (uint8_t)attributes[i];
	}
	return p;
}


} // namespace r5sdpparse


#endif // _R5_SDP_PARSE_H
