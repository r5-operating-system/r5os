/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_URL_H
#define ONLINE_STORAGE_URL_H

#include <String.h>


void			AppendEncodedSegment(BString& out, const char* segment);
void			AppendEncodedPath(BString& out, const char* path);
BString			EncodeForm(const char* value);
BString			DecodeForm(const char* value);
BString			UrlHost(const BString& url);
BString			UrlPath(const BString& url);
BString			UrlQuery(const BString& url);
bool			SameOrigin(const BString& a, const BString& b);
bool			IsHttpsDowngrade(const BString& from, const BString& to);

/* Query item from "code=a&state=b", percent-decoded. */
bool			QueryValue(const BString& query, const char* key, BString& value);

#endif
