/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: lib/rest/rest.go SameHost and isHTTPSDowngrade, at
 * c0cef5e872f937727c89bfab71d6be11480f4d40.
 * The form encoder in this file is R5's.
 */

#include "Url.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static bool
unreserved(char c)
{
	if (c >= 'A' && c <= 'Z')
		return true;
	if (c >= 'a' && c <= 'z')
		return true;
	if (c >= '0' && c <= '9')
		return true;
	return c == '-' || c == '.' || c == '_' || c == '~';
}


static void
append_encoded(BString& out, const char* value, bool formSpace)
{
	if (value == NULL)
		return;
	for (const unsigned char* p = (const unsigned char*)value; *p != '\0'; p++) {
		if (unreserved((char)*p))
			out << (char)*p;
		else if (formSpace && *p == ' ')
			out << "+";
		else {
			char hex[4];
			snprintf(hex, sizeof(hex), "%%%02X", *p);
			out << hex;
		}
	}
}


void
AppendEncodedSegment(BString& out, const char* segment)
{
	append_encoded(out, segment, false);
}


void
AppendEncodedPath(BString& out, const char* path)
{
	if (path == NULL || path[0] == '\0')
		return;
	const char* cursor = path;
	if (*cursor == '/')
		cursor++;
	bool first = true;
	while (*cursor != '\0') {
		const char* slash = strchr(cursor, '/');
		size_t length = slash != NULL ? (size_t)(slash - cursor) : strlen(cursor);
		BString segment;
		segment.SetTo(cursor, (int32)length);
		if (!first)
			out << "/";
		first = false;
		AppendEncodedSegment(out, segment.String());
		if (slash == NULL)
			break;
		cursor = slash + 1;
	}
}


BString
EncodeForm(const char* value)
{
	BString out;
	append_encoded(out, value, true);
	return out;
}


BString
DecodeForm(const char* value)
{
	BString out;
	QueryValue(BString("v=") << (value != NULL ? value : ""), "v", out);
	return out;
}


static void
scheme_host_port(const BString& url, BString& scheme, BString& host,
	int32& port)
{
	scheme.SetTo("");
	host.SetTo("");
	port = 0;
	const char* text = url.String();
	const char* schemeEnd = strstr(text, "://");
	if (schemeEnd == NULL)
		return;
	scheme.SetTo(text, (int32)(schemeEnd - text));
	char lowered[16];
	int32 count = scheme.Length();
	if (count > 15)
		count = 15;
	for (int32 i = 0; i < count; i++) {
		char c = scheme.ByteAt(i);
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		lowered[i] = c;
	}
	lowered[count] = '\0';
	scheme.SetTo(lowered);
	const char* hostStart = schemeEnd + 3;
	const char* path = strchr(hostStart, '/');
	const char* end = path != NULL ? path : hostStart + strlen(hostStart);
	const char* colon = NULL;
	for (const char* p = hostStart; p < end; p++) {
		if (*p == ':')
			colon = p;
	}
	if (colon != NULL) {
		host.SetTo(hostStart, (int32)(colon - hostStart));
		port = atoi(colon + 1);
	} else
		host.SetTo(hostStart, (int32)(end - hostStart));
	if (port == 0) {
		if (scheme == "https")
			port = 443;
		else if (scheme == "http")
			port = 80;
	}
}


BString
UrlHost(const BString& url)
{
	BString scheme;
	BString host;
	int32 port = 0;
	scheme_host_port(url, scheme, host, port);
	return host;
}


BString
UrlPath(const BString& url)
{
	const char* text = url.String();
	const char* schemeEnd = strstr(text, "://");
	const char* start = schemeEnd != NULL ? schemeEnd + 3 : text;
	const char* path = strchr(start, '/');
	if (path == NULL)
		return BString("/");
	const char* query = strchr(path, '?');
	BString out;
	if (query != NULL)
		out.SetTo(path, (int32)(query - path));
	else
		out.SetTo(path);
	return out;
}


BString
UrlQuery(const BString& url)
{
	const char* query = strchr(url.String(), '?');
	if (query == NULL)
		return BString("");
	query++;
	const char* hash = strchr(query, '#');
	BString out;
	if (hash != NULL)
		out.SetTo(query, (int32)(hash - query));
	else
		out.SetTo(query);
	return out;
}


bool
SameOrigin(const BString& a, const BString& b)
{
	BString schemeA;
	BString hostA;
	int32 portA = 0;
	BString schemeB;
	BString hostB;
	int32 portB = 0;
	scheme_host_port(a, schemeA, hostA, portA);
	scheme_host_port(b, schemeB, hostB, portB);
	return schemeA == schemeB && hostA == hostB && portA == portB;
}


bool
IsHttpsDowngrade(const BString& from, const BString& to)
{
	BString fromScheme;
	BString toScheme;
	BString host;
	int32 port = 0;
	scheme_host_port(from, fromScheme, host, port);
	scheme_host_port(to, toScheme, host, port);
	return fromScheme == "https" && toScheme == "http";
}


static int
hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


bool
QueryValue(const BString& query, const char* key, BString& value)
{
	value.SetTo("");
	const char* cursor = query.String();
	size_t keyLength = strlen(key);
	while (*cursor != '\0') {
		const char* amp = strchr(cursor, '&');
		const char* end = amp != NULL ? amp : cursor + strlen(cursor);
		const char* eq = NULL;
		for (const char* p = cursor; p < end; p++) {
			if (*p == '=') {
				eq = p;
				break;
			}
		}
		size_t nameLength = eq != NULL ? (size_t)(eq - cursor) : (size_t)(end - cursor);
		if (nameLength == keyLength && strncmp(cursor, key, keyLength) == 0) {
			const char* raw = eq != NULL ? eq + 1 : end;
			size_t rawLength = (size_t)(end - raw);
			for (size_t i = 0; i < rawLength; i++) {
				if (raw[i] == '+' )
					value << " ";
				else if (raw[i] == '%' && i + 2 < rawLength) {
					int hi = hex_nibble(raw[i + 1]);
					int lo = hex_nibble(raw[i + 2]);
					if (hi >= 0 && lo >= 0) {
						value << (char)((hi << 4) | lo);
						i += 2;
					} else
						value << raw[i];
				} else
					value << raw[i];
			}
			return true;
		}
		if (amp == NULL)
			break;
		cursor = amp + 1;
	}
	return false;
}
