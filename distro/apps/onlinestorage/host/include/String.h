/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef _STRING_H_R5_HOST
#define _STRING_H_R5_HOST

#include <SupportDefs.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

class BString {
public:
	BString() : fText(NULL), fLength(0) { _Set("", 0); }
	BString(const char* string) : fText(NULL), fLength(0) { _Set(string, string ? (int32)strlen(string) : 0); }
	BString(const BString& other) : fText(NULL), fLength(0) { _Set(other.fText, other.fLength); }
	~BString() { delete[] fText; }

	BString& operator=(const BString& other) { if (this != &other) _Set(other.fText, other.fLength); return *this; }
	BString& operator=(const char* string) { _Set(string, string ? (int32)strlen(string) : 0); return *this; }

	BString& SetTo(const char* string) { return (*this) = string; }
	BString& SetTo(const char* string, int32 maxLength)
	{
		if (string == NULL || maxLength <= 0)
			return _Set("", 0);
		return _Set(string, maxLength);
	}
	BString& SetToFormat(const char* format, ...)
	{
		char buffer[1024];
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		return (*this) = buffer;
	}

	const char* String() const { return fText != NULL ? fText : ""; }
	int32 Length() const { return fLength; }
	bool IsEmpty() const { return fLength == 0; }
	char ByteAt(int32 index) const { return (index >= 0 && index < fLength) ? fText[index] : 0; }

	BString& Append(const char* string, int32 length)
	{
		if (string == NULL || length <= 0)
			return *this;
		int32 next = fLength + length;
		char* text = new char[next + 1];
		memcpy(text, fText, (size_t)fLength);
		memcpy(text + fLength, string, (size_t)length);
		text[next] = '\0';
		delete[] fText;
		fText = text;
		fLength = next;
		return *this;
	}
	BString& Append(const char* string)
	{
		return Append(string, string ? (int32)strlen(string) : 0);
	}
	BString& Append(const BString& string) { return Append(string.fText, string.fLength); }

	BString& operator<<(const char* string) { return Append(string); }
	BString& operator<<(const BString& string) { return Append(string); }
	BString& operator<<(char c) { return Append(&c, 1); }
	BString& operator<<(int value)
	{
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "%d", value);
		return Append(buffer);
	}
	BString& operator<<(unsigned int value)
	{
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "%u", value);
		return Append(buffer);
	}

	BString& Truncate(int32 newLength)
	{
		if (newLength < 0)
			newLength = 0;
		if (newLength < fLength) {
			fText[newLength] = '\0';
			fLength = newLength;
		}
		return *this;
	}
	BString& Remove(int32 from, int32 length)
	{
		if (from < 0 || length <= 0 || from >= fLength)
			return *this;
		if (from + length > fLength)
			length = fLength - from;
		memmove(fText + from, fText + from + length, (size_t)(fLength - from - length + 1));
		fLength -= length;
		return *this;
	}

	int32 FindFirst(const char* string) const
	{
		if (string == NULL)
			return B_ERROR;
		const char* found = strstr(fText, string);
		if (found == NULL)
			return B_ERROR;
		return (int32)(found - fText);
	}
	int32 FindLast(const char* string) const
	{
		if (string == NULL || string[0] == '\0')
			return B_ERROR;
		int32 at = B_ERROR;
		for (int32 i = 0; i < fLength; i++) {
			if (strncmp(fText + i, string, strlen(string)) == 0)
				at = i;
		}
		return at;
	}

	bool operator==(const BString& other) const
	{
		return fLength == other.fLength && memcmp(fText, other.fText, (size_t)fLength) == 0;
	}
	bool operator==(const char* string) const { return strcmp(String(), string != NULL ? string : "") == 0; }
	bool operator!=(const BString& other) const { return !(*this == other); }
	bool operator!=(const char* string) const { return !(*this == string); }
	bool operator<(const BString& other) const { return strcmp(String(), other.String()) < 0; }
	operator const char*() const { return String(); }

private:
	BString& _Set(const char* string, int32 length)
	{
		char* text = new char[length + 1];
		if (length > 0 && string != NULL)
			memcpy(text, string, (size_t)length);
		text[length] = '\0';
		delete[] fText;
		fText = text;
		fLength = length;
		return *this;
	}

	char* fText;
	int32 fLength;
};

#endif
