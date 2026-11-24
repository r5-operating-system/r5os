/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Enough of BJson::Parse for the host tests. Objects and arrays land in
 * a BMessage the way Haiku's BJsonMessageWriter does: array fields are
 * "0", "1", ... and numbers are doubles.
 */

#ifndef _JSON_H_R5_HOST
#define _JSON_H_R5_HOST

#include <Message.h>

#include <stdlib.h>

namespace BPrivate {

class BJson {
public:
	static status_t Parse(const char* json, BMessage& message)
	{
		if (json == NULL)
			return B_BAD_VALUE;
		const char* cursor = json;
		_Skip(cursor);
		if (*cursor == '{')
			return _Object(cursor, message);
		if (*cursor == '[') {
			message.what = B_JSON_MESSAGE_WHAT_ARRAY;
			return _Array(cursor, message);
		}
		return B_BAD_DATA;
	}
	static status_t Parse(const BString& json, BMessage& message)
	{
		return Parse(json.String(), message);
	}

private:
	static void _Skip(const char*& cursor)
	{
		while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t')
			cursor++;
	}
	static status_t _String(const char*& cursor, BString& out)
	{
		if (*cursor != '"')
			return B_BAD_DATA;
		cursor++;
		out.SetTo("");
		while (*cursor != '\0' && *cursor != '"') {
			if (*cursor == '\\') {
				cursor++;
				if (*cursor == 'n')
					out << '\n';
				else if (*cursor == 'r')
					out << '\r';
				else if (*cursor == 't')
					out << '\t';
				else
					out << *cursor;
				if (*cursor != '\0')
					cursor++;
				continue;
			}
			out << *cursor;
			cursor++;
		}
		if (*cursor != '"')
			return B_BAD_DATA;
		cursor++;
		return B_OK;
	}
	static status_t _Value(const char*& cursor, BMessage& message, const char* name)
	{
		_Skip(cursor);
		if (*cursor == '"') {
			BString text;
			status_t status = _String(cursor, text);
			if (status != B_OK)
				return status;
			return message.AddString(name, text.String());
		}
		if (*cursor == '{') {
			BMessage child(B_JSON_MESSAGE_WHAT_OBJECT);
			status_t status = _Object(cursor, child);
			if (status != B_OK)
				return status;
			return message.AddMessage(name, &child);
		}
		if (*cursor == '[') {
			BMessage child(B_JSON_MESSAGE_WHAT_ARRAY);
			status_t status = _Array(cursor, child);
			if (status != B_OK)
				return status;
			return message.AddMessage(name, &child);
		}
		if (strncmp(cursor, "true", 4) == 0) {
			cursor += 4;
			return message.AddBool(name, true);
		}
		if (strncmp(cursor, "false", 5) == 0) {
			cursor += 5;
			return message.AddBool(name, false);
		}
		if (strncmp(cursor, "null", 4) == 0) {
			cursor += 4;
			return message.AddPointer(name, NULL);
		}
		char* end = NULL;
		double number = strtod(cursor, &end);
		if (end == cursor)
			return B_BAD_DATA;
		cursor = end;
		return message.AddDouble(name, number);
	}
	static status_t _Object(const char*& cursor, BMessage& message)
	{
		message.what = B_JSON_MESSAGE_WHAT_OBJECT;
		if (*cursor != '{')
			return B_BAD_DATA;
		cursor++;
		_Skip(cursor);
		if (*cursor == '}') {
			cursor++;
			return B_OK;
		}
		while (*cursor != '\0') {
			_Skip(cursor);
			BString name;
			status_t status = _String(cursor, name);
			if (status != B_OK)
				return status;
			_Skip(cursor);
			if (*cursor != ':')
				return B_BAD_DATA;
			cursor++;
			status = _Value(cursor, message, name.String());
			if (status != B_OK)
				return status;
			_Skip(cursor);
			if (*cursor == ',') {
				cursor++;
				continue;
			}
			if (*cursor == '}') {
				cursor++;
				return B_OK;
			}
			return B_BAD_DATA;
		}
		return B_BAD_DATA;
	}
	static status_t _Array(const char*& cursor, BMessage& message)
	{
		if (*cursor != '[')
			return B_BAD_DATA;
		cursor++;
		_Skip(cursor);
		int32 index = 0;
		if (*cursor == ']') {
			cursor++;
			return B_OK;
		}
		while (*cursor != '\0') {
			BString name;
			name.SetToFormat("%d", (int)index);
			status_t status = _Value(cursor, message, name.String());
			if (status != B_OK)
				return status;
			index++;
			_Skip(cursor);
			if (*cursor == ',') {
				cursor++;
				_Skip(cursor);
				continue;
			}
			if (*cursor == ']') {
				cursor++;
				return B_OK;
			}
			return B_BAD_DATA;
		}
		return B_BAD_DATA;
	}
};

}

using BPrivate::BJson;

#endif
