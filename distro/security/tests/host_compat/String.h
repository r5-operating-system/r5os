/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Minimal Haiku BString compatibility header for host unit tests only.
 */
#ifndef _STRING_H_HAIKU_HOST_COMPAT
#define _STRING_H_HAIKU_HOST_COMPAT

#include_next <string.h>
#include <stddef.h>
#include <string>

class BString {
public:
	BString() {}
	explicit BString(const char* str) : fStr(str != NULL ? str : "") {}

	const char* String() const { return fStr.c_str(); }
	size_t Length() const { return fStr.size(); }

	void SetTo(const char* str) { fStr = (str != NULL ? str : ""); }
	void Truncate(size_t newLength)
	{
		if (newLength < fStr.size())
			fStr.resize(newLength);
	}
	void Append(char ch, size_t count) { fStr.append(count, ch); }
	void Append(const char* str) { if (str != NULL) fStr.append(str); }

	BString& operator<<(const char* str)
	{
		if (str != NULL)
			fStr.append(str);
		return *this;
	}
	BString& operator<<(const BString& other)
	{
		fStr.append(other.fStr);
		return *this;
	}

private:
	std::string fStr;
};

#endif
