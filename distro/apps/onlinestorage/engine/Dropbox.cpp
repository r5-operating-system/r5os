/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: backend/dropbox/dropbox.go and backend/dropbox/batcher.go
 * at c0cef5e872f937727c89bfab71d6be11480f4d40.
 */

#include "Dropbox.h"

#include "ClientConfig.h"
#include "JsonUtil.h"
#include "LocalFile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char* kRpc = "https://api.dropboxapi.com/2";
static const char* kContent = "https://content.dropboxapi.com/2";
static const off_t kChunk = 4 * 1024 * 1024;


OAuthConfig
DropboxOAuthConfig()
{
	OAuthConfig config;
	config.account = "dropbox";
	config.clientId = R5_DROPBOX_CLIENT_ID;
	config.authUrl = "https://www.dropbox.com/oauth2/authorize";
	config.tokenUrl = "https://api.dropboxapi.com/oauth2/token";
	config.redirectUrl.SetToFormat("http://127.0.0.1:%d/", kOAuthLoopbackPort);
	config.scopes = "files.metadata.write files.content.write files.content.read";
	config.extraAuth.AddString("token_access_type", "offline");
	return config;
}


DropboxProvider::DropboxProvider(Transport& transport, OAuthClient& oauth,
	Sleeper& sleeper, Log& log, const char* root)
	:
	fSession(transport, oauth, sleeper, log, "dropbox"),
	fRoot(root != NULL ? root : "")
{
	while (fRoot.Length() > 0 && fRoot.ByteAt(0) == '/')
		fRoot.Remove(0, 1);
	while (fRoot.Length() > 0 && fRoot.ByteAt(fRoot.Length() - 1) == '/')
		fRoot.Truncate(fRoot.Length() - 1);
}


const char*
DropboxProvider::Name() const
{
	return "dropbox";
}


BString
DropboxProvider::_ApiPath(const BString& relative) const
{
	BString full = JoinRelative(fRoot, relative);
	if (full.Length() == 0)
		return BString("");
	BString path;
	path << "/" << full;
	return path;
}


status_t
DropboxProvider::_ReadEntry(const BMessage& entry, CloudItem& item) const
{
	item = CloudItem();
	const char* tag = JsonString(entry, ".tag");
	if (tag == NULL)
		return B_BAD_DATA;
	item.deleted = strcmp(tag, "deleted") == 0;
	item.isFolder = strcmp(tag, "folder") == 0;
	const char* id = JsonString(entry, "id");
	if (id != NULL)
		item.id = id;
	const char* rev = JsonString(entry, "rev");
	if (rev != NULL)
		item.revision = rev;
	const char* hash = JsonString(entry, "content_hash");
	if (hash != NULL)
		item.contentHash = hash;
	int64 size = 0;
	if (JsonInt64(entry, "size", size) == B_OK)
		item.size = (off_t)size;

	const char* display = JsonString(entry, "path_display");
	if (display == NULL)
		display = JsonString(entry, "path_lower");
	if (display == NULL)
		return B_BAD_DATA;
	BString full(display);
	if (full.ByteAt(0) == '/')
		full.Remove(0, 1);
	BString relative;
	if (fRoot.Length() == 0)
		relative = full;
	else if (full == fRoot)
		return B_NAME_NOT_FOUND;
	else if (full.Length() > fRoot.Length() + 1
		&& full.FindFirst(fRoot.String()) == 0
		&& full.ByteAt(fRoot.Length()) == '/') {
		relative = full;
		relative.Remove(0, fRoot.Length() + 1);
	} else
		return B_NAME_NOT_FOUND;
	if (relative.Length() == 0)
		return B_NAME_NOT_FOUND;
	item.path = relative;
	return B_OK;
}


status_t
DropboxProvider::_Rpc(const char* route, const BString& body, BMessage& reply)
{
	HttpExchange exchange;
	exchange.method = "POST";
	exchange.url << kRpc << route;
	SetHeader(exchange.requestHeaders, "content-type", "application/json");
	exchange.body = body;
	status_t status = fSession.Call(exchange);
	if (status != B_OK)
		return status;
	if (exchange.status == 409 && exchange.responseBody.FindFirst("not_found") >= 0)
		return B_ENTRY_NOT_FOUND;
	if (exchange.status < 200 || exchange.status > 299)
		return B_IO_ERROR;
	if (exchange.responseBody.Length() == 0)
		return B_OK;
	return ParseJson(exchange.responseBody, reply);
}


status_t
DropboxProvider::_ListPath(const BString& apiPath, bool recursive,
	std::vector<CloudItem>& items, BString& cursor)
{
	items.clear();
	BString body;
	body << "{\"path\":";
	AppendJsonString(body, apiPath.String());
	body << ",\"recursive\":";
	body << (recursive ? "true" : "false");
	body << ",\"limit\":1000}";
	BString route = "/files/list_folder";
	bool first = true;
	for (int32 page = 0; page < 1000; page++) {
		BMessage reply;
		status_t status;
		if (first)
			status = _Rpc(route.String(), body, reply);
		else {
			BString more;
			more << "{\"cursor\":";
			AppendJsonString(more, cursor.String());
			more << "}";
			status = _Rpc("/files/list_folder/continue", more, reply);
		}
		if (status != B_OK)
			return status;
		first = false;
		const char* next = JsonString(reply, "cursor");
		if (next != NULL)
			cursor = next;
		BMessage entries;
		if (JsonMessage(reply, "entries", entries) == B_OK) {
			int32 count = JsonCount(entries);
			for (int32 i = 0; i < count; i++) {
				BMessage entry;
				if (JsonAt(entries, i, entry) != B_OK)
					continue;
				CloudItem item;
				if (_ReadEntry(entry, item) != B_OK)
					continue;
				items.push_back(item);
			}
		}
		if (!JsonBool(reply, "has_more", false))
			return B_OK;
	}
	return B_TIMED_OUT;
}


status_t
DropboxProvider::List(const BString& folderPath, std::vector<CloudItem>& items)
{
	BString cursor;
	return _ListPath(_ApiPath(folderPath), false, items, cursor);
}


status_t
DropboxProvider::Stat(const BString& path, CloudItem& item)
{
	BString body;
	body << "{\"path\":";
	AppendJsonString(body, _ApiPath(path).String());
	body << "}";
	BMessage reply;
	status_t status = _Rpc("/files/get_metadata", body, reply);
	if (status != B_OK)
		return status;
	status = _ReadEntry(reply, item);
	if (status != B_OK)
		return status;
	item.path = path;
	return B_OK;
}


status_t
DropboxProvider::Download(const BString& path, const char* localFile)
{
	HttpExchange exchange;
	exchange.method = "POST";
	exchange.url << kContent << "/files/download";
	BString arg;
	arg << "{\"path\":";
	AppendJsonString(arg, _ApiPath(path).String());
	arg << "}";
	SetHeader(exchange.requestHeaders, "dropbox-api-arg", arg.String());
	status_t status = fSession.Call(exchange);
	if (status != B_OK)
		return status;
	if (exchange.status == 409)
		return B_ENTRY_NOT_FOUND;
	if (exchange.status < 200 || exchange.status > 299)
		return B_IO_ERROR;
	return LocalWriteAtomic(localFile, exchange.responseBody.String(),
		(size_t)exchange.responseBody.Length());
}


status_t
DropboxProvider::CreateFolder(const BString& path)
{
	BString body;
	body << "{\"path\":";
	AppendJsonString(body, _ApiPath(path).String());
	body << ",\"autorename\":false}";
	BMessage reply;
	status_t status = _Rpc("/files/create_folder_v2", body, reply);
	if (status == B_IO_ERROR)
		return B_FILE_EXISTS;
	return status;
}


status_t
DropboxProvider::Delete(const BString& path)
{
	BString body;
	body << "{\"path\":";
	AppendJsonString(body, _ApiPath(path).String());
	body << "}";
	BMessage reply;
	status_t status = _Rpc("/files/delete_v2", body, reply);
	if (status == B_ENTRY_NOT_FOUND)
		return B_OK;
	return status;
}


status_t
DropboxProvider::Move(const BString& fromPath, const BString& toPath)
{
	BString body;
	body << "{\"from_path\":";
	AppendJsonString(body, _ApiPath(fromPath).String());
	body << ",\"to_path\":";
	AppendJsonString(body, _ApiPath(toPath).String());
	body << ",\"autorename\":false}";
	BMessage reply;
	return _Rpc("/files/move_v2", body, reply);
}


status_t
DropboxProvider::Upload(const BString& path, const char* localFile,
	const char* expectedRevision)
{
	off_t size = 0;
	status_t status = LocalFileSize(localFile, size);
	if (status != B_OK)
		return status;

	HttpExchange start;
	start.method = "POST";
	start.url << kContent << "/files/upload_session/start";
	SetHeader(start.requestHeaders, "dropbox-api-arg", "{\"close\":false}");
	SetHeader(start.requestHeaders, "content-type", "application/octet-stream");
	status = fSession.Call(start);
	if (status != B_OK)
		return status;
	if (start.status < 200 || start.status > 299)
		return B_IO_ERROR;
	BMessage started;
	status = ParseJson(start.responseBody, started);
	if (status != B_OK)
		return status;
	const char* session = JsonString(started, "session_id");
	if (session == NULL || session[0] == '\0')
		return B_BAD_DATA;

	off_t offset = 0;
	while (offset < size || size == 0) {
		off_t chunk = size - offset;
		if (chunk > kChunk)
			chunk = kChunk;
		bool last = offset + chunk >= size;
		std::vector<char> buffer((size_t)chunk);
		if (chunk > 0) {
			size_t got = 0;
			status = LocalRead(localFile, offset, &buffer[0], (size_t)chunk, got);
			if (status != B_OK)
				return status;
			if ((off_t)got != chunk)
				return B_IO_ERROR;
		}
		bool sent = false;
		for (int32 attempt = 0; attempt < 3 && !sent; attempt++) {
			BString arg;
			char number[32];
			snprintf(number, sizeof(number), "%lld", (long long)offset);
			arg << "{\"cursor\":{\"session_id\":";
			AppendJsonString(arg, session);
			arg << ",\"offset\":" << number << "},\"close\":";
			arg << (last ? "true" : "false");
			arg << "}";
			HttpExchange append;
			append.method = "POST";
			append.url << kContent << "/files/upload_session/append_v2";
			SetHeader(append.requestHeaders, "dropbox-api-arg", arg.String());
			SetHeader(append.requestHeaders, "content-type",
				"application/octet-stream");
			if (chunk > 0)
				append.body.SetTo(&buffer[0], (int32)chunk);
			status = fSession.Call(append);
			if (status != B_OK)
				return status;
			if (append.status == 409
				&& append.responseBody.FindFirst("incorrect_offset") >= 0) {
				BMessage err;
				if (ParseJson(append.responseBody, err) != B_OK)
					return B_BAD_DATA;
				BMessage error;
				BMessage incorrect;
				if (JsonMessage(err, "error", error) != B_OK)
					return B_BAD_DATA;
				if (JsonMessage(error, "incorrect_offset", incorrect) != B_OK)
					return B_BAD_DATA;
				int64 correct = 0;
				if (JsonInt64(incorrect, "correct_offset", correct) != B_OK)
					return B_BAD_DATA;
				if (correct < (int64)offset || correct > (int64)(offset + chunk))
					return B_BAD_DATA;
				if (correct == (int64)(offset + chunk)) {
					sent = true;
					break;
				}
				size_t skip = (size_t)(correct - (int64)offset);
				buffer.erase(buffer.begin(), buffer.begin() + skip);
				offset = (off_t)correct;
				chunk = (off_t)buffer.size();
				continue;
			}
			if (append.status < 200 || append.status > 299)
				return B_IO_ERROR;
			sent = true;
		}
		if (!sent)
			return B_IO_ERROR;
		offset += chunk;
		if (size == 0 || offset >= size)
			break;
	}

	BString finish;
	finish << "{\"entries\":[{\"cursor\":{\"session_id\":";
	AppendJsonString(finish, session);
	char number[32];
	snprintf(number, sizeof(number), "%lld", (long long)size);
	finish << ",\"offset\":" << number;
	finish << "},\"commit\":{\"path\":";
	AppendJsonString(finish, _ApiPath(path).String());
	if (expectedRevision != NULL && expectedRevision[0] != '\0') {
		finish << ",\"mode\":{\".tag\":\"update\",\"update\":";
		AppendJsonString(finish, expectedRevision);
		finish << "}";
	} else
		finish << ",\"mode\":\"add\"";
	finish << ",\"autorename\":false,\"mute\":true}}]}";
	BMessage reply;
	status = _Rpc("/files/upload_session/finish_batch_v2", finish, reply);
	if (status != B_OK)
		return status;
	const char* tag = JsonString(reply, ".tag");
	if (tag != NULL && strcmp(tag, "async_job_id") == 0) {
		const char* job = JsonString(reply, "async_job_id");
		if (job == NULL)
			return B_BAD_DATA;
		for (int32 i = 0; i < 5; i++) {
			BString check;
			check << "{\"async_job_id\":";
			AppendJsonString(check, job);
			check << "}";
			status = _Rpc("/files/upload_session/finish_batch/check", check, reply);
			if (status != B_OK)
				return status;
			tag = JsonString(reply, ".tag");
			if (tag != NULL && strcmp(tag, "in_progress") != 0)
				break;
		}
	}
	BMessage entries;
	if (JsonMessage(reply, "entries", entries) != B_OK)
		return B_BAD_DATA;
	BMessage first;
	if (JsonAt(entries, 0, first) != B_OK)
		return B_BAD_DATA;
	const char* entryTag = JsonString(first, ".tag");
	if (entryTag != NULL && strcmp(entryTag, "success") != 0) {
		if (strcmp(entryTag, "failure") == 0)
			return B_BUSY;
		return B_IO_ERROR;
	}
	if (entryTag == NULL)
		return B_IO_ERROR;
	return B_OK;
}


status_t
DropboxProvider::Changes(const BString& cursor, BString& newCursor,
	std::vector<CloudItem>& items)
{
	if (cursor.Length() == 0)
		return _ListPath(_ApiPath(""), true, items, newCursor);

	items.clear();
	newCursor = cursor;
	BString route = "/files/list_folder/continue";
	for (int32 page = 0; page < 1000; page++) {
		BString body;
		body << "{\"cursor\":";
		AppendJsonString(body, newCursor.String());
		body << "}";
		BMessage reply;
		status_t status = _Rpc(route, body, reply);
		if (status != B_OK)
			return status;
		const char* next = JsonString(reply, "cursor");
		if (next != NULL)
			newCursor = next;
		BMessage entries;
		if (JsonMessage(reply, "entries", entries) == B_OK) {
			int32 count = JsonCount(entries);
			for (int32 i = 0; i < count; i++) {
				BMessage entry;
				if (JsonAt(entries, i, entry) != B_OK)
					continue;
				CloudItem item;
				if (_ReadEntry(entry, item) != B_OK)
					continue;
				items.push_back(item);
			}
		}
		if (!JsonBool(reply, "has_more", false))
			return B_OK;
	}
	return B_TIMED_OUT;
}


status_t
DropboxProvider::Tip(BString& cursor)
{
	BString body;
	body << "{\"path\":";
	AppendJsonString(body, _ApiPath("").String());
	body << ",\"recursive\":true}";
	BMessage reply;
	status_t status = _Rpc("/files/list_folder/get_latest_cursor", body, reply);
	if (status != B_OK)
		return status;
	const char* value = JsonString(reply, "cursor");
	if (value == NULL || value[0] == '\0')
		return B_BAD_DATA;
	cursor = value;
	return B_OK;
}
