/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "JsonUtil.h"

#include <Json.h>

#include <stdio.h>
#include <stdlib.h>


status_t
ParseJson(const char* json, BMessage& message)
{
	if (json == NULL)
		return B_BAD_VALUE;
	message.MakeEmpty();
	return BJson::Parse(json, message);
}


status_t
ParseJson(const BString& json, BMessage& message)
{
	return ParseJson(json.String(), message);
}


const char*
JsonString(const BMessage& message, const char* name)
{
	return message.FindString(name);
}


bool
JsonBool(const BMessage& message, const char* name, bool fallback)
{
	bool value = fallback;
	if (message.FindBool(name, &value) != B_OK)
		return fallback;
	return value;
}


status_t
JsonInt64(const BMessage& message, const char* name, int64& value)
{
	double number = 0.0;
	status_t status = message.FindDouble(name, &number);
	if (status != B_OK)
		return status;
	value = (int64)number;
	return B_OK;
}


status_t
JsonMessage(const BMessage& message, const char* name, BMessage& child)
{
	return message.FindMessage(name, &child);
}


int32
JsonCount(const BMessage& array)
{
	int32 count = 0;
	for (;;) {
		BString name;
		name.SetToFormat("%" B_PRId32, count);
		type_code type;
		int32 fieldCount = 0;
		if (array.GetInfo(name.String(), &type, &fieldCount) != B_OK)
			break;
		count++;
	}
	return count;
}


status_t
JsonAt(const BMessage& array, int32 index, BMessage& child)
{
	BString name;
	name.SetToFormat("%" B_PRId32, index);
	return array.FindMessage(name.String(), &child);
}


const char*
JsonStringAt(const BMessage& array, int32 index)
{
	BString name;
	name.SetToFormat("%" B_PRId32, index);
	return array.FindString(name.String());
}


bool
JsonIsNull(const BMessage& message, const char* name)
{
	void* pointer = (void*)1;
	if (message.FindPointer(name, &pointer) != B_OK)
		return false;
	return pointer == NULL;
}


void
AppendJsonString(BString& out, const char* value)
{
	out << "\"";
	if (value == NULL)
		value = "";
	for (const char* p = value; *p != '\0'; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '"' || c == '\\') {
			out << "\\";
			out << (char)c;
		} else if (c == '\n') {
			out << "\\n";
		} else if (c == '\r') {
			out << "\\r";
		} else if (c == '\t') {
			out << "\\t";
		} else if (c < 0x20) {
			char hex[8];
			snprintf(hex, sizeof(hex), "\\u%04x", c);
			out << hex;
		} else
			out << (char)c;
	}
	out << "\"";
}
