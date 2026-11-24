/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_JSON_UTIL_H
#define ONLINE_STORAGE_JSON_UTIL_H

#include <Message.h>
#include <String.h>


/*
 * BJson stores an object as a BMessage (what B_JSON_MESSAGE_WHAT_OBJECT)
 * and an array as a BMessage (what B_JSON_MESSAGE_WHAT_ARRAY) whose fields
 * are "0", "1", ... Numbers are doubles. Null is a NULL pointer.
 */

status_t		ParseJson(const char* json, BMessage& message);
status_t		ParseJson(const BString& json, BMessage& message);

const char*		JsonString(const BMessage& message, const char* name);
bool			JsonBool(const BMessage& message, const char* name,
					bool fallback);
status_t		JsonInt64(const BMessage& message, const char* name,
					int64& value);
status_t		JsonMessage(const BMessage& message, const char* name,
					BMessage& child);
int32			JsonCount(const BMessage& array);
status_t		JsonAt(const BMessage& array, int32 index, BMessage& child);
const char*		JsonStringAt(const BMessage& array, int32 index);
bool			JsonIsNull(const BMessage& message, const char* name);

void			AppendJsonString(BString& out, const char* value);

#endif
