/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "Transport.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>


HttpExchange::HttpExchange()
	:
	status(0),
	responseInFile(false)
{
}


void
HttpExchange::ClearResponse()
{
	status = 0;
	responseHeaders.MakeEmpty();
	responseBody.SetTo("");
	responseInFile = false;
}


Transport::~Transport()
{
}


Sleeper::~Sleeper()
{
}


void
RealSleeper::WaitSeconds(int32 seconds)
{
	/*
	 * rclone waits the full Retry-After. A value above five minutes is
	 * shortened so a bad header cannot stall Quit for hours. 120 seconds,
	 * which servers do send, is waited in full.
	 */
	if (seconds > 300)
		seconds = 300;
	if (seconds > 0)
		sleep((unsigned)seconds);
}


Log::~Log()
{
}


void
StderrLog::Line(const char* text)
{
	if (text == NULL)
		return;
	fprintf(stderr, "Online Storage: %s\n", text);
}


static void
lower_copy(const char* name, char* out, size_t outSize)
{
	size_t i = 0;
	if (name == NULL)
		name = "";
	for (; name[i] != '\0' && i + 1 < outSize; i++) {
		char c = name[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		out[i] = c;
	}
	out[i] = '\0';
}


const char*
HeaderValue(const BMessage& headers, const char* name)
{
	char key[128];
	lower_copy(name, key, sizeof(key));
	return headers.FindString(key);
}


status_t
SetHeader(BMessage& headers, const char* name, const char* value)
{
	char key[128];
	lower_copy(name, key, sizeof(key));
	headers.RemoveName(key);
	return headers.AddString(key, value != NULL ? value : "");
}
