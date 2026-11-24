/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "MemoryCloud.h"

#include "JsonUtil.h"
#include "Url.h"

#include <stdio.h>
#include <stdlib.h>


static std::string
decode_url(const char* text)
{
	std::string out;
	if (text == NULL)
		return out;
	for (const char* p = text; *p != '\0'; p++) {
		if (*p == '%' && p[1] != '\0' && p[2] != '\0') {
			char hex[3] = { p[1], p[2], 0 };
			out.push_back((char)strtol(hex, NULL, 16));
			p += 2;
		} else
			out.push_back(*p);
	}
	return out;
}


static std::string
bstring(const BString& value)
{
	return std::string(value.String(), (size_t)value.Length());
}


MemoryCloud::MemoryCloud(const char* provider, const char* root)
	:
	fProvider(provider != NULL ? provider : ""),
	fRoot(root != NULL ? root : ""),
	fNextId(1),
	fGeneration(1)
{
}


std::string
MemoryCloud::_Key(const std::string& relative) const
{
	return relative;
}


void
MemoryCloud::_Touch(const std::string& path, const Node& node, bool deleted)
{
	fGeneration++;
	Change change;
	change.generation = fGeneration;
	change.path = path;
	change.deleted = deleted;
	change.node = node;
	fChanges.push_back(change);
	if (deleted)
		fNodes.erase(path);
	else
		fNodes[path] = node;
}


void
MemoryCloud::PutFile(const char* relative, const char* bytes)
{
	Node node;
	char id[32];
	snprintf(id, sizeof(id), "id%d", fNextId++);
	node.id = id;
	node.revision = std::string("rev") + id;
	node.bytes = bytes != NULL ? bytes : "";
	node.folder = false;
	_Touch(relative, node, false);
}


void
MemoryCloud::Remove(const char* relative)
{
	std::map<std::string, Node>::iterator it = fNodes.find(relative);
	if (it == fNodes.end())
		return;
	Node node = it->second;
	_Touch(relative, node, true);
}


void
MemoryCloud::Rename(const char* from, const char* to)
{
	std::map<std::string, Node>::iterator it = fNodes.find(from);
	if (it == fNodes.end())
		return;
	Node node = it->second;
	_Touch(from, node, true);
	char id[32];
	snprintf(id, sizeof(id), "rev%d", fNextId++);
	node.revision = id;
	_Touch(to, node, false);
}


bool
MemoryCloud::Has(const char* relative) const
{
	return fNodes.find(relative) != fNodes.end();
}


std::string
MemoryCloud::Read(const char* relative) const
{
	std::map<std::string, Node>::const_iterator it = fNodes.find(relative);
	if (it == fNodes.end())
		return std::string();
	return it->second.bytes;
}


static std::string
leaf_of(const std::string& path)
{
	size_t slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return path;
	return path.substr(slash + 1);
}


static std::string
parent_of(const std::string& path)
{
	size_t slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return std::string();
	return path.substr(0, slash);
}


std::string
MemoryCloud::_ItemJson(const std::string& path, const Node& node,
	bool deleted) const
{
	std::string parent = fRoot;
	std::string folder = parent_of(path);
	if (folder.size() > 0) {
		if (parent.size() > 0)
			parent += "/";
		parent += folder;
	}
	BString json;
	json << "{\"id\":";
	AppendJsonString(json, node.id.c_str());
	json << ",\"name\":";
	AppendJsonString(json, leaf_of(path).c_str());
	json << ",\"size\":" << (int)node.bytes.size();
	json << ",\"cTag\":";
	AppendJsonString(json, node.revision.c_str());
	json << ",\"eTag\":";
	AppendJsonString(json, node.revision.c_str());
	if (deleted)
		json << ",\"deleted\":{\"state\":\"deleted\"}";
	else if (node.folder)
		json << ",\"folder\":{}";
	else
		json << ",\"file\":{}";
	json << ",\"parentReference\":{\"path\":\"/drive/root:";
	if (parent.size() > 0) {
		json << "/";
		json << parent.c_str();
	}
	json << "\"}}";
	return bstring(json);
}


static std::string
relative_from_full(const std::string& root, const std::string& full)
{
	if (root.size() == 0)
		return full;
	if (full == root)
		return std::string();
	std::string prefix = root + "/";
	if (full.compare(0, prefix.size(), prefix) == 0)
		return full.substr(prefix.size());
	return full;
}


static bool
onedrive_target(const std::string& pathAndQuery, const std::string& root,
	std::string& relative, std::string& route, std::string& token)
{
	std::string path = pathAndQuery;
	std::string query;
	size_t q = path.find('?');
	if (q != std::string::npos) {
		query = path.substr(q + 1);
		path = path.substr(0, q);
	}
	BString queryValue;
	if (QueryValue(query.c_str(), "token", queryValue))
		token = bstring(queryValue);
	const char* rootMark = strstr(path.c_str(), "root:/");
	const char* rootBare = strstr(path.c_str(), "/root");
	std::string full;
	if (rootMark != NULL) {
		full = rootMark + strlen("root:/");
		size_t colon = full.find(':');
		if (colon != std::string::npos) {
			route = full.substr(colon + 1);
			full = full.substr(0, colon);
		}
	} else if (rootBare != NULL) {
		const char* rest = rootBare + strlen("/root");
		route = rest;
		full = root;
	} else
		return false;
	relative = relative_from_full(root, full);
	return true;
}


status_t
MemoryCloud::_OneDrive(HttpExchange& exchange)
{
	std::string url = decode_url(exchange.url.String());
	std::string host = bstring(UrlHost(exchange.url));
	if (host == "upload.memory.test") {
		std::string id = url.substr(url.find_last_of('/') + 1);
		std::string path = fSessionPath[id];
		if (exchange.method == "GET") {
			off_t at = fSessionOffset[id];
			BString body;
			body << "{\"nextExpectedRanges\":[\"" << (int)at << "-\"]}";
			exchange.status = 200;
			exchange.responseBody = body;
			return B_OK;
		}
		const char* range = HeaderValue(exchange.requestHeaders, "content-range");
		long long start = 0;
		long long end = 0;
		long long total = 0;
		if (range == NULL
			|| sscanf(range, "bytes %lld-%lld/%lld", &start, &end, &total) != 3)
			return B_BAD_DATA;
		if (start != (long long)fSessionOffset[id]) {
			exchange.status = 416;
			exchange.responseBody = "{\"error\":\"range\"}";
			return B_OK;
		}
		fSessions[id].append(exchange.body.String(), (size_t)exchange.body.Length());
		fSessionOffset[id] = (off_t)(end + 1);
		if (end + 1 < total) {
			exchange.status = 202;
			BString body;
			body << "{\"nextExpectedRanges\":[\"" << (int)(end + 1) << "-\"]}";
			exchange.responseBody = body;
			return B_OK;
		}
		Node node;
		char ident[32];
		snprintf(ident, sizeof(ident), "id%d", fNextId++);
		node.id = ident;
		node.revision = std::string("rev") + ident;
		node.bytes = fSessions[id];
		node.folder = false;
		_Touch(path, node, false);
		exchange.status = 201;
		exchange.responseBody = _ItemJson(path, node, false).c_str();
		return B_OK;
	}

	std::string path = bstring(UrlPath(exchange.url));
	std::string query = bstring(UrlQuery(exchange.url));
	std::string relative;
	std::string route;
	std::string token;
	if (!onedrive_target(decode_url((path + "?" + query).c_str()), fRoot,
			relative, route, token))
		return B_BAD_VALUE;

	if (exchange.method == "POST" && route.find("createUploadSession") != std::string::npos) {
		char id[32];
		snprintf(id, sizeof(id), "s%d", fNextId++);
		fSessions[id] = "";
		fSessionOffset[id] = 0;
		fSessionPath[id] = relative;
		exchange.status = 200;
		BString body;
		body << "{\"uploadUrl\":\"https://upload.memory.test/" << id << "\"}";
		exchange.responseBody = body;
		return B_OK;
	}
	if (exchange.method == "PUT" && route.find("/content") != std::string::npos) {
		Node node;
		char id[32];
		snprintf(id, sizeof(id), "id%d", fNextId++);
		node.id = id;
		node.revision = std::string("rev") + id;
		node.bytes.assign(exchange.body.String(), (size_t)exchange.body.Length());
		node.folder = false;
		_Touch(relative, node, false);
		exchange.status = 201;
		exchange.responseBody = _ItemJson(relative, node, false).c_str();
		return B_OK;
	}
	if (exchange.method == "GET" && route.find("/content") != std::string::npos) {
		if (fNodes.find(relative) == fNodes.end()) {
			exchange.status = 404;
			return B_OK;
		}
		exchange.status = 200;
		exchange.responseBody.SetTo(fNodes[relative].bytes.data(),
			(int32)fNodes[relative].bytes.size());
		return B_OK;
	}
	if (exchange.method == "DELETE") {
		if (fNodes.find(relative) != fNodes.end())
			Remove(relative.c_str());
		exchange.status = 204;
		return B_OK;
	}
	if (exchange.method == "POST" && route.find("/children") != std::string::npos) {
		BMessage message;
		if (ParseJson(exchange.body, message) != B_OK)
			return B_BAD_DATA;
		const char* name = JsonString(message, "name");
		if (name == NULL)
			return B_BAD_DATA;
		std::string child = relative.size() == 0 ? name : relative + "/" + name;
		if (fNodes.find(child) != fNodes.end()) {
			exchange.status = 409;
			return B_OK;
		}
		Node node;
		char id[32];
		snprintf(id, sizeof(id), "id%d", fNextId++);
		node.id = id;
		node.revision = id;
		node.folder = true;
		_Touch(child, node, false);
		exchange.status = 201;
		exchange.responseBody = _ItemJson(child, node, false).c_str();
		return B_OK;
	}
	if (exchange.method == "PATCH") {
		BMessage message;
		if (ParseJson(exchange.body, message) != B_OK)
			return B_BAD_DATA;
		const char* name = JsonString(message, "name");
		BMessage parent;
		std::string dest = name != NULL ? name : leaf_of(relative);
		if (JsonMessage(message, "parentReference", parent) == B_OK) {
			const char* parentPath = JsonString(parent, "path");
			std::string inside = parentPath != NULL ? parentPath : "";
			size_t mark = inside.find("root:");
			if (mark != std::string::npos) {
				inside = inside.substr(mark + strlen("root:"));
				if (!inside.empty() && inside[0] == '/')
					inside.erase(0, 1);
				inside = relative_from_full(fRoot, inside);
			}
			if (!inside.empty())
				dest = inside + "/" + dest;
		}
		if (fNodes.find(relative) == fNodes.end()) {
			exchange.status = 404;
			return B_OK;
		}
		Node node = fNodes[relative];
		Remove(relative.c_str());
		_Touch(dest, node, false);
		exchange.status = 200;
		exchange.responseBody = _ItemJson(dest, node, false).c_str();
		return B_OK;
	}
	if (exchange.method == "GET" && route.find("/children") != std::string::npos) {
		BString body;
		body << "{\"value\":[";
		bool first = true;
		for (std::map<std::string, Node>::iterator it = fNodes.begin();
				it != fNodes.end(); it++) {
			if (parent_of(it->first) != relative)
				continue;
			if (!first)
				body << ",";
			first = false;
			body << _ItemJson(it->first, it->second, false).c_str();
		}
		body << "]}";
		exchange.status = 200;
		exchange.responseBody = body;
		return B_OK;
	}
	if (exchange.method == "GET" && route.find("/delta") != std::string::npos) {
		int since = 0;
		bool latest = token == "latest";
		bool full = token.empty() && exchange.url.FindFirst("token=") < 0;
		if (!latest && !token.empty())
			since = atoi(token.c_str());
		BString body;
		body << "{\"value\":[";
		bool first = true;
		if (!latest) {
			if (full) {
				for (std::map<std::string, Node>::iterator it = fNodes.begin();
						it != fNodes.end(); it++) {
					if (!first)
						body << ",";
					first = false;
					body << _ItemJson(it->first, it->second, false).c_str();
				}
			} else {
				for (size_t i = 0; i < fChanges.size(); i++) {
					if (fChanges[i].generation <= since)
						continue;
					if (!first)
						body << ",";
					first = false;
					body << _ItemJson(fChanges[i].path, fChanges[i].node,
						fChanges[i].deleted).c_str();
				}
			}
		}
		body << "],\"@odata.deltaLink\":\"https://graph.microsoft.com/v1.0/me/drive/root:";
		if (fRoot.size() > 0) {
			body << "/";
			body << fRoot.c_str();
		}
		body << ":/delta?token=" << fGeneration << "\"}";
		exchange.status = 200;
		exchange.responseBody = body;
		return B_OK;
	}
	if (exchange.method == "GET") {
		if (relative.size() == 0 || fNodes.find(relative) == fNodes.end()) {
			exchange.status = 404;
			return B_OK;
		}
		exchange.status = 200;
		exchange.responseBody = _ItemJson(relative, fNodes[relative], false).c_str();
		return B_OK;
	}
	exchange.status = 400;
	return B_OK;
}


static std::string
dropbox_entry(const std::string& root, const std::string& path,
	const std::string& id, const std::string& revision, const std::string& bytes,
	bool folder, bool deleted)
{
	std::string display = "/";
	if (root.size() > 0) {
		display += root;
		display += "/";
	}
	display += path;
	BString json;
	if (deleted)
		json << "{\".tag\":\"deleted\"";
	else if (folder)
		json << "{\".tag\":\"folder\"";
	else
		json << "{\".tag\":\"file\"";
	json << ",\"name\":";
	AppendJsonString(json, leaf_of(path).c_str());
	json << ",\"path_display\":";
	AppendJsonString(json, display.c_str());
	json << ",\"id\":";
	AppendJsonString(json, id.c_str());
	if (!deleted && !folder) {
		json << ",\"size\":" << (int)bytes.size();
		json << ",\"rev\":";
		AppendJsonString(json, revision.c_str());
		json << ",\"content_hash\":";
		AppendJsonString(json, revision.c_str());
	}
	json << "}";
	return std::string(json.String());
}


status_t
MemoryCloud::_Dropbox(HttpExchange& exchange)
{
	std::string url = bstring(exchange.url);
	BMessage message;
	if (exchange.body.Length() > 0 && exchange.body.ByteAt(0) == '{')
		ParseJson(exchange.body, message);
	const char* arg = HeaderValue(exchange.requestHeaders, "dropbox-api-arg");
	BMessage argMessage;
	if (arg != NULL)
		ParseJson(arg, argMessage);

	if (url.find("/files/download") != std::string::npos) {
		const char* path = JsonString(argMessage, "path");
		std::string relative = relative_from_full(fRoot,
			path != NULL && path[0] == '/' ? path + 1 : "");
		if (fNodes.find(relative) == fNodes.end()) {
			exchange.status = 409;
			exchange.responseBody = "{\"error_summary\":\"not_found\"}";
			return B_OK;
		}
		exchange.status = 200;
		exchange.responseBody.SetTo(fNodes[relative].bytes.data(),
			(int32)fNodes[relative].bytes.size());
		return B_OK;
	}
	if (url.find("/upload_session/start") != std::string::npos) {
		char id[32];
		snprintf(id, sizeof(id), "s%d", fNextId++);
		fSessions[id] = "";
		fSessionOffset[id] = 0;
		exchange.status = 200;
		BString body;
		body << "{\"session_id\":\"" << id << "\"}";
		exchange.responseBody = body;
		return B_OK;
	}
	if (url.find("/upload_session/append_v2") != std::string::npos) {
		BMessage cursor;
		JsonMessage(argMessage, "cursor", cursor);
		const char* session = JsonString(cursor, "session_id");
		int64 offset = 0;
		JsonInt64(cursor, "offset", offset);
		if (session == NULL || fSessions.find(session) == fSessions.end()) {
			exchange.status = 409;
			return B_OK;
		}
		if (offset != (int64)fSessionOffset[session]) {
			exchange.status = 409;
			BString body;
			body << "{\"error\":{\".tag\":\"incorrect_offset\",\"incorrect_offset\":{\"correct_offset\":"
				<< (int)fSessionOffset[session] << "}}}";
			exchange.responseBody = body;
			return B_OK;
		}
		fSessions[session].append(exchange.body.String(),
			(size_t)exchange.body.Length());
		fSessionOffset[session] += (off_t)exchange.body.Length();
		exchange.status = 200;
		return B_OK;
	}
	if (url.find("/upload_session/finish_batch") != std::string::npos
		&& url.find("/check") == std::string::npos) {
		BMessage entries;
		if (JsonMessage(message, "entries", entries) != B_OK)
			return B_BAD_DATA;
		BMessage first;
		JsonAt(entries, 0, first);
		BMessage cursor;
		BMessage commit;
		JsonMessage(first, "cursor", cursor);
		JsonMessage(first, "commit", commit);
		const char* session = JsonString(cursor, "session_id");
		const char* path = JsonString(commit, "path");
		if (session == NULL || path == NULL)
			return B_BAD_DATA;
		std::string relative = relative_from_full(fRoot, path[0] == '/' ? path + 1 : path);
		Node node;
		char id[32];
		snprintf(id, sizeof(id), "id%d", fNextId++);
		node.id = id;
		node.revision = std::string("rev") + id;
		node.bytes = fSessions[session];
		node.folder = false;
		_Touch(relative, node, false);
		exchange.status = 200;
		BString body;
		body << "{\".tag\":\"complete\",\"entries\":[{\".tag\":\"success\",\"success\":"
			<< dropbox_entry(fRoot, relative, node.id, node.revision, node.bytes,
				node.folder, false).c_str() << "}]}";
		exchange.responseBody = body;
		return B_OK;
	}
	if (url.find("/files/list_folder/get_latest_cursor") != std::string::npos) {
		exchange.status = 200;
		BString body;
		body << "{\"cursor\":\"" << fGeneration << "\"}";
		exchange.responseBody = body;
		return B_OK;
	}
	if (url.find("/files/list_folder") != std::string::npos) {
		bool cont = url.find("/continue") != std::string::npos;
		BString body;
		body << "{\"entries\":[";
		bool first = true;
		if (cont) {
			const char* cursor = JsonString(message, "cursor");
			int since = cursor != NULL ? atoi(cursor) : 0;
			for (size_t i = 0; i < fChanges.size(); i++) {
				if (fChanges[i].generation <= since)
					continue;
				if (!first)
					body << ",";
				first = false;
				body << dropbox_entry(fRoot, fChanges[i].path, fChanges[i].node.id,
					fChanges[i].node.revision, fChanges[i].node.bytes,
					fChanges[i].node.folder, fChanges[i].deleted).c_str();
			}
		} else {
			const char* path = JsonString(message, "path");
			std::string folder = path != NULL && path[0] == '/' ? path + 1 : "";
			folder = relative_from_full(fRoot, folder);
			bool recursive = JsonBool(message, "recursive", false);
			for (std::map<std::string, Node>::iterator it = fNodes.begin();
					it != fNodes.end(); it++) {
				bool child = recursive
					? (folder.size() == 0
						|| it->first.compare(0, folder.size(), folder) == 0)
					: parent_of(it->first) == folder;
				if (!child)
					continue;
				if (!first)
					body << ",";
				first = false;
				body << dropbox_entry(fRoot, it->first, it->second.id,
					it->second.revision, it->second.bytes, it->second.folder,
					false).c_str();
			}
		}
		body << "],\"cursor\":\"" << fGeneration << "\",\"has_more\":false}";
		exchange.status = 200;
		exchange.responseBody = body;
		return B_OK;
	}
	if (url.find("/files/get_metadata") != std::string::npos) {
		const char* path = JsonString(message, "path");
		std::string relative = relative_from_full(fRoot,
			path != NULL && path[0] == '/' ? path + 1 : "");
		if (fNodes.find(relative) == fNodes.end()) {
			exchange.status = 409;
			exchange.responseBody = "{\"error_summary\":\"path/not_found\"}";
			return B_OK;
		}
		exchange.status = 200;
		exchange.responseBody = dropbox_entry(fRoot, relative, fNodes[relative].id,
			fNodes[relative].revision, fNodes[relative].bytes,
			fNodes[relative].folder, false).c_str();
		return B_OK;
	}
	if (url.find("/files/delete_v2") != std::string::npos) {
		const char* path = JsonString(message, "path");
		std::string relative = relative_from_full(fRoot,
			path != NULL && path[0] == '/' ? path + 1 : "");
		if (fNodes.find(relative) == fNodes.end()) {
			exchange.status = 409;
			exchange.responseBody = "{\"error_summary\":\"path/not_found\"}";
			return B_OK;
		}
		Remove(relative.c_str());
		exchange.status = 200;
		exchange.responseBody = "{}";
		return B_OK;
	}
	if (url.find("/files/create_folder_v2") != std::string::npos) {
		const char* path = JsonString(message, "path");
		std::string relative = relative_from_full(fRoot,
			path != NULL && path[0] == '/' ? path + 1 : "");
		if (fNodes.find(relative) != fNodes.end()) {
			exchange.status = 409;
			exchange.responseBody = "{\"error_summary\":\"path/conflict\"}";
			return B_OK;
		}
		Node node;
		char id[32];
		snprintf(id, sizeof(id), "id%d", fNextId++);
		node.id = id;
		node.revision = id;
		node.folder = true;
		_Touch(relative, node, false);
		exchange.status = 200;
		exchange.responseBody = "{}";
		return B_OK;
	}
	if (url.find("/files/move_v2") != std::string::npos) {
		const char* from = JsonString(message, "from_path");
		const char* to = JsonString(message, "to_path");
		std::string fromRel = relative_from_full(fRoot,
			from != NULL && from[0] == '/' ? from + 1 : "");
		std::string toRel = relative_from_full(fRoot,
			to != NULL && to[0] == '/' ? to + 1 : "");
		if (fNodes.find(fromRel) == fNodes.end()) {
			exchange.status = 409;
			return B_OK;
		}
		Node node = fNodes[fromRel];
		Remove(fromRel.c_str());
		_Touch(toRel, node, false);
		exchange.status = 200;
		exchange.responseBody = "{}";
		return B_OK;
	}
	exchange.status = 400;
	return B_OK;
}


status_t
MemoryCloud::Exchange(HttpExchange& exchange)
{
	exchange.ClearResponse();
	if (fProvider == "onedrive")
		return _OneDrive(exchange);
	if (fProvider == "dropbox")
		return _Dropbox(exchange);
	return B_BAD_VALUE;
}
