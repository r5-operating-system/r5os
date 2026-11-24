/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "ScriptedTransport.h"

#include <string.h>


void
ScriptedTransport::Add(const char* method, const char* urlPart, int32 status,
	const char* body)
{
	AddRetry(method, urlPart, status, NULL, body);
}


void
ScriptedTransport::AddRetry(const char* method, const char* urlPart,
	int32 status, const char* retryAfter, const char* body)
{
	ScriptedReply reply;
	reply.method = method;
	reply.urlPart = urlPart;
	reply.status = status;
	reply.retryAfter = retryAfter;
	reply.body = body != NULL ? body : "";
	fReplies.push_back(reply);
}


int32
ScriptedTransport::Unused() const
{
	int32 count = 0;
	for (size_t i = 0; i < fReplies.size(); i++) {
		if (!fReplies[i].used)
			count++;
	}
	return count;
}


status_t
ScriptedTransport::Exchange(HttpExchange& exchange)
{
	for (size_t i = 0; i < fReplies.size(); i++) {
		ScriptedReply& reply = fReplies[i];
		if (reply.used)
			continue;
		if (reply.method != NULL && exchange.method != reply.method)
			continue;
		if (reply.urlPart != NULL && exchange.url.FindFirst(reply.urlPart) < 0)
			continue;
		reply.used = true;
		exchange.status = reply.status;
		exchange.responseBody = reply.body;
		if (reply.retryAfter != NULL)
			SetHeader(exchange.responseHeaders, "retry-after", reply.retryAfter);
		return B_OK;
	}
	exchange.status = 0;
	return B_BAD_VALUE;
}


void
CaptureLog::Line(const char* line)
{
	if (line == NULL)
		return;
	text << line << "\n";
}


void
NoSleep::WaitSeconds(int32 seconds)
{
	waited += seconds;
}
