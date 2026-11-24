/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: backend/onedrive/onedrive.go and backend/onedrive/api/types.go
 * at c0cef5e872f937727c89bfab71d6be11480f4d40.
 */

#include "OneDrive.h"

#include "ClientConfig.h"
#include "JsonUtil.h"
#include "LocalFile.h"
#include "Url.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char* kGraph = "https://graph.microsoft.com/v1.0";
static const off_t kChunk = 10 * 1024 * 1024;


OAuthConfig
OneDriveOAuthConfig()
{
	OAuthConfig config;
	config.account = "onedrive";
	config.clientId = R5_ONEDRIVE_CLIENT_ID;
	config.authUrl = "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";
	config.tokenUrl = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
	config.redirectUrl.SetToFormat("http://127.0.0.1:%d/", kOAuthLoopbackPort);
	config.scopes = "Files.ReadWrite offline_access";
	return config;
}


OneDriveProvider::OneDriveProvider(Transport& transport, OAuthClient& oauth,
	Sleeper& sleeper, Log& log, const char* root)
	:
	fSession(transport, oauth, sleeper, log, "onedrive"),
	fRoot(root != NULL ? root : "")
{
	while (fRoot.Length() > 0 && fRoot.ByteAt(0) == '/')
		fRoot.Remove(0, 1);
	while (fRoot.Length() > 0 && fRoot.ByteAt(fRoot.Length() - 1) == '/')
		fRoot.Truncate(fRoot.Length() - 1);
}


const char*
OneDriveProvider::Name() const
{
	return "onedrive";
}


BString
OneDriveProvider::_ItemUrl(const BString& path, const char* route) const
{
	BString full = JoinRelative(fRoot, path);
	BString url;
	url << kGraph << "/me/drive/";
	if (full.Length() == 0) {
		url << "root";
		if (route != NULL && route[0] != '\0')
			url << route;
		return url;
	}
	url << "root:/";
	AppendEncodedPath(url, full.String());
	url << ":";
	if (route != NULL)
		url << route;
	return url;
}


static BString
parent_inside_drive(const char* parentPath)
{
	if (parentPath == NULL)
		return BString("");
	const char* mark = strstr(parentPath, "root:");
	if (mark == NULL)
		return BString("");
	mark += strlen("root:");
	if (*mark == '/')
		mark++;
	return BString(mark);
}


static bool
under_root(const BString& root, const BString& full, BString& relative)
{
	if (root.Length() == 0) {
		relative = full;
		return true;
	}
	if (full == root) {
		relative.SetTo("");
		return true;
	}
	if (full.Length() > root.Length() + 1
		&& full.FindFirst(root.String()) == 0
		&& full.ByteAt(root.Length()) == '/') {
		relative = full;
		relative.Remove(0, root.Length() + 1);
		return true;
	}
	return false;
}


status_t
OneDriveProvider::_ReadItem(const BMessage& message, const BString& listFolder,
	bool fromList, CloudItem& item) const
{
	item = CloudItem();
	const char* id = JsonString(message, "id");
	if (id != NULL)
		item.id = id;
	const char* name = JsonString(message, "name");
	BMessage facet;
	item.deleted = message.FindMessage("deleted", &facet) == B_OK;
	item.isFolder = message.FindMessage("folder", &facet) == B_OK;
	const char* ctag = JsonString(message, "cTag");
	const char* etag = JsonString(message, "eTag");
	if (ctag != NULL && ctag[0] != '\0')
		item.revision = ctag;
	else if (etag != NULL)
		item.revision = etag;
	/* If-Match needs the eTag. The cTag stays the change token. */
	if (etag != NULL)
		item.contentHash = etag;
	int64 size = 0;
	if (JsonInt64(message, "size", size) == B_OK)
		item.size = (off_t)size;

	BMessage parent;
	if (message.FindMessage("parentReference", &parent) == B_OK && name != NULL) {
		BString inside = parent_inside_drive(JsonString(parent, "path"));
		BString full = JoinRelative(inside, BString(name));
		if (!under_root(fRoot, full, item.path))
			return B_NAME_NOT_FOUND;
		if (item.path.Length() == 0)
			return B_NAME_NOT_FOUND;
		return B_OK;
	}
	if (!fromList || name == NULL)
		return B_BAD_DATA;
	item.path = JoinRelative(listFolder, BString(name));
	return item.path.Length() > 0 ? B_OK : B_NAME_NOT_FOUND;
}


status_t
OneDriveProvider::List(const BString& folderPath, std::vector<CloudItem>& items)
{
	items.clear();
	BString cursor;
	return _CollectPages(_ItemUrl(folderPath, "/children"), folderPath, true,
		items, cursor, false);
}


status_t
OneDriveProvider::_CollectPages(const BString& firstUrl,
	const BString& listFolder, bool fromList, std::vector<CloudItem>& items,
	BString& finalCursor, bool delta)
{
	BString url = firstUrl;
	finalCursor.SetTo("");
	for (int32 page = 0; page < 1000; page++) {
		HttpExchange exchange;
		exchange.method = "GET";
		exchange.url = url;
		SetHeader(exchange.requestHeaders, "accept", "application/json");
		status_t status = fSession.Call(exchange);
		if (status != B_OK)
			return status;
		if (exchange.status == 404)
			return B_ENTRY_NOT_FOUND;
		if (exchange.status < 200 || exchange.status > 299)
			return B_IO_ERROR;
		BMessage message;
		status = ParseJson(exchange.responseBody, message);
		if (status != B_OK)
			return status;
		BMessage value;
		if (JsonMessage(message, "value", value) == B_OK) {
			int32 count = JsonCount(value);
			for (int32 i = 0; i < count; i++) {
				BMessage child;
				if (JsonAt(value, i, child) != B_OK)
					continue;
				CloudItem item;
				if (_ReadItem(child, listFolder, fromList && !delta, item) != B_OK)
					continue;
				if (item.path.Length() == 0 && !item.deleted)
					continue;
				items.push_back(item);
			}
		}
		const char* next = JsonString(message, "@odata.nextLink");
		const char* deltaLink = JsonString(message, "@odata.deltaLink");
		if (delta && deltaLink != NULL && deltaLink[0] != '\0'
			&& (next == NULL || next[0] == '\0')) {
			finalCursor = deltaLink;
			return B_OK;
		}
		if (next == NULL || next[0] == '\0') {
			if (deltaLink != NULL)
				finalCursor = deltaLink;
			return B_OK;
		}
		url = next;
	}
	return B_TIMED_OUT;
}


status_t
OneDriveProvider::Stat(const BString& path, CloudItem& item)
{
	HttpExchange exchange;
	exchange.method = "GET";
	exchange.url = _ItemUrl(path, "");
	SetHeader(exchange.requestHeaders, "accept", "application/json");
	status_t status = fSession.Call(exchange);
	if (status != B_OK)
		return status;
	if (exchange.status == 404)
		return B_ENTRY_NOT_FOUND;
	if (exchange.status < 200 || exchange.status > 299)
		return B_IO_ERROR;
	BMessage message;
	status = ParseJson(exchange.responseBody, message);
	if (status != B_OK)
		return status;
	status = _ReadItem(message, path, false, item);
	if (status != B_OK) {
		item.path = path;
		return B_OK;
	}
	item.path = path;
	return B_OK;
}


status_t
OneDriveProvider::Download(const BString& path, const char* localFile)
{
	HttpExchange exchange;
	exchange.method = "GET";
	exchange.url = _ItemUrl(path, "/content");
	status_t status = CallIntoFile(fSession, exchange, localFile);
	if (status != B_OK)
		return status;
	if (exchange.status == 404)
		return B_ENTRY_NOT_FOUND;
	if (exchange.status < 200 || exchange.status > 299)
		return B_IO_ERROR;
	return B_OK;
}


status_t
OneDriveProvider::CreateFolder(const BString& path)
{
	BString parent = ParentPath(path);
	BString leaf = LeafName(path);
	HttpExchange exchange;
	exchange.method = "POST";
	exchange.url = _ItemUrl(parent, "/children");
	SetHeader(exchange.requestHeaders, "content-type", "application/json");
	exchange.body << "{\"name\":";
	AppendJsonString(exchange.body, leaf.String());
	exchange.body << ",\"folder\":{},\"@microsoft.graph.conflictBehavior\":\"fail\"}";
	status_t status = fSession.Call(exchange);
	if (status != B_OK)
		return status;
	if (exchange.status == 409)
		return B_FILE_EXISTS;
	if (exchange.status < 200 || exchange.status > 299)
		return B_IO_ERROR;
	return B_OK;
}


status_t
OneDriveProvider::Delete(const BString& path)
{
	HttpExchange exchange;
	exchange.method = "DELETE";
	exchange.url = _ItemUrl(path, "");
	status_t status = fSession.Call(exchange);
	if (status != B_OK)
		return status;
	if (exchange.status == 404)
		return B_OK;
	if (exchange.status < 200 || exchange.status > 299)
		return B_IO_ERROR;
	return B_OK;
}


status_t
OneDriveProvider::Move(const BString& fromPath, const BString& toPath)
{
	HttpExchange exchange;
	exchange.method = "PATCH";
	exchange.url = _ItemUrl(fromPath, "");
	SetHeader(exchange.requestHeaders, "content-type", "application/json");
	BString parent = JoinRelative(fRoot, ParentPath(toPath));
	exchange.body << "{\"name\":";
	AppendJsonString(exchange.body, LeafName(toPath).String());
	exchange.body << ",\"parentReference\":{\"path\":\"/drive/root:";
	if (parent.Length() > 0) {
		exchange.body << "/";
		exchange.body << parent;
	}
	exchange.body << "\"}}";
	status_t status = fSession.Call(exchange);
	if (status != B_OK)
		return status;
	if (exchange.status < 200 || exchange.status > 299)
		return B_IO_ERROR;
	return B_OK;
}


static status_t
expected_start(const BMessage& message, off_t& start)
{
	BMessage ranges;
	if (JsonMessage(message, "nextExpectedRanges", ranges) != B_OK)
		return B_BAD_DATA;
	const char* text = JsonStringAt(ranges, 0);
	if (text == NULL)
		return B_BAD_DATA;
	start = (off_t)strtoll(text, NULL, 10);
	return B_OK;
}


status_t
OneDriveProvider::Upload(const BString& path, const char* localFile,
	const char* expectedRevision)
{
	off_t size = 0;
	status_t status = LocalFileSize(localFile, size);
	if (status != B_OK)
		return status;
	if (size == 0) {
		HttpExchange exchange;
		exchange.method = "PUT";
		exchange.url = _ItemUrl(path, "/content");
		SetHeader(exchange.requestHeaders, "content-type",
			"application/octet-stream");
		if (expectedRevision != NULL && expectedRevision[0] != '\0')
			SetHeader(exchange.requestHeaders, "if-match", expectedRevision);
		status = fSession.Call(exchange);
		if (status != B_OK)
			return status;
		if (exchange.status == 412 || exchange.status == 409)
			return B_BUSY;
		return exchange.status >= 200 && exchange.status <= 299
			? B_OK : B_IO_ERROR;
	}

	HttpExchange created;
	created.method = "POST";
	created.url = _ItemUrl(path, "/createUploadSession");
	SetHeader(created.requestHeaders, "content-type", "application/json");
	const char* behavior = (expectedRevision != NULL
		&& expectedRevision[0] != '\0') ? "replace" : "fail";
	created.body << "{\"item\":{\"@microsoft.graph.conflictBehavior\":\"";
	created.body << behavior;
	created.body << "\",\"name\":";
	AppendJsonString(created.body, LeafName(path).String());
	created.body << "}}";
	if (expectedRevision != NULL && expectedRevision[0] != '\0')
		SetHeader(created.requestHeaders, "if-match", expectedRevision);
	status = fSession.Call(created);
	if (status != B_OK)
		return status;
	if (created.status == 412 || created.status == 409)
		return B_BUSY;
	if (created.status < 200 || created.status > 299)
		return B_IO_ERROR;
	BMessage session;
	status = ParseJson(created.responseBody, session);
	if (status != B_OK)
		return status;
	const char* uploadUrl = JsonString(session, "uploadUrl");
	if (uploadUrl == NULL || uploadUrl[0] == '\0')
		return B_BAD_DATA;

	off_t offset = 0;
	while (offset < size) {
		off_t chunk = size - offset;
		if (chunk > kChunk)
			chunk = kChunk;
		std::vector<char> buffer((size_t)chunk);
		size_t got = 0;
		status = LocalRead(localFile, offset, &buffer[0], (size_t)chunk, got);
		if (status != B_OK)
			return status;
		if ((off_t)got != chunk)
			return B_IO_ERROR;

		bool sent = false;
		for (int32 attempt = 0; attempt < 3 && !sent; attempt++) {
			HttpExchange put;
			put.method = "PUT";
			put.url = uploadUrl;
			SetHeader(put.requestHeaders, "content-type", "application/octet-stream");
			BString range;
			range.SetToFormat("bytes %lld-%lld/%lld",
				(long long)offset, (long long)(offset + chunk - 1),
				(long long)size);
			SetHeader(put.requestHeaders, "content-range", range.String());
			put.body.SetTo(&buffer[0], (int32)got);
			status = fSession.Call(put);
			if (status != B_OK)
				return status;
			if (put.status == 416) {
				HttpExchange probe;
				probe.method = "GET";
				probe.url = uploadUrl;
				status = fSession.Call(probe);
				if (status != B_OK)
					return status;
				BMessage probeMessage;
				if (ParseJson(probe.responseBody, probeMessage) != B_OK)
					return B_BAD_DATA;
				off_t server = 0;
				if (expected_start(probeMessage, server) != B_OK)
					return B_BAD_DATA;
				if (server < offset || server > offset + chunk)
					return B_BAD_DATA;
				if (server == offset + chunk) {
					sent = true;
					break;
				}
				size_t skip = (size_t)(server - offset);
				buffer.erase(buffer.begin(), buffer.begin() + skip);
				offset = server;
				chunk = (off_t)buffer.size();
				continue;
			}
			if (put.status == 404)
				return B_ENTRY_NOT_FOUND;
			if (put.status == 409 || put.status == 412)
				return B_BUSY;
			if (put.status < 200 || put.status > 299)
				return B_IO_ERROR;
			sent = true;
		}
		if (!sent)
			return B_IO_ERROR;
		offset += chunk;
	}
	return B_OK;
}


status_t
OneDriveProvider::Changes(const BString& cursor, BString& newCursor,
	std::vector<CloudItem>& items)
{
	items.clear();
	BString url;
	if (cursor.Length() == 0)
		url = _ItemUrl("", "/delta");
	else if (cursor.FindFirst("https://") == 0)
		url = cursor;
	else {
		url = _ItemUrl("", "/delta");
		url << "?token=" << EncodeForm(cursor.String());
	}
	return _CollectPages(url, "", false, items, newCursor, true);
}


status_t
OneDriveProvider::Tip(BString& cursor)
{
	std::vector<CloudItem> ignored;
	BString url = _ItemUrl("", "/delta");
	url << "?token=latest";
	return _CollectPages(url, "", false, ignored, cursor, true);
}
