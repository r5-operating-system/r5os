/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "AomiSupport.h"

#include <Directory.h>
#include <DateTime.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>


static const char* kFolder = "WebPositive";


static status_t
settings_path(BPath* path, const char* leaf)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, path);
	if (status != B_OK)
		return status;
	status = path->Append(kFolder);
	if (status != B_OK)
		return status;
	create_directory(path->Path(), 0777);
	if (leaf != NULL)
		status = path->Append(leaf);
	return status;
}


static BString
trim(const BString& input)
{
	const char* s = input.String();
	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	BString out(s);
	while (out.Length() > 0
		&& isspace((unsigned char)out.String()[out.Length() - 1])) {
		out.Truncate(out.Length() - 1);
	}
	return out;
}


void
aomi_prefs_defaults(AomiPrefs* prefs)
{
	prefs->start = 0;
	prefs->home = "https://r5-operating-system.github.io/";
	prefs->search = "https://duckduckgo.com/?q=%s";
	prefs->showHome = false;
	prefs->showBookmarks = false;
	prefs->zoomText = false;
	BPath desktop;
	if (find_directory(B_DESKTOP_DIRECTORY, &desktop) == B_OK)
		prefs->downloadPath = desktop.Path();
	else {
		BPath home;
		if (find_directory(B_USER_DIRECTORY, &home) == B_OK) {
			home.Append("Desktop");
			prefs->downloadPath = home.Path();
		} else
			prefs->downloadPath = "";
	}
	prefs->askDownload = false;
	prefs->showMenuBar = false;
}


AomiPrefs
aomi_prefs()
{
	AomiPrefs prefs;
	aomi_prefs_defaults(&prefs);
	BPath path;
	if (settings_path(&path, "Aomi") != B_OK)
		return prefs;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage message;
	if (file.InitCheck() != B_OK || message.Unflatten(&file) != B_OK)
		return prefs;
	message.FindInt32("start", &prefs.start);
	message.FindString("home page", &prefs.home);
	message.FindString("search url", &prefs.search);
	message.FindBool("show home", &prefs.showHome);
	message.FindBool("show bookmarks", &prefs.showBookmarks);
	message.FindBool("zoom text only", &prefs.zoomText);
	message.FindString("download path", &prefs.downloadPath);
	message.FindBool("ask download", &prefs.askDownload);
	message.FindBool("show menu bar", &prefs.showMenuBar);
	return prefs;
}


void
aomi_prefs_save(const AomiPrefs& prefs)
{
	BPath path;
	if (settings_path(&path, "Aomi") != B_OK)
		return;
	BMessage message;
	message.AddInt32("start", prefs.start);
	message.AddString("home page", prefs.home);
	message.AddString("search url", prefs.search);
	message.AddBool("show home", prefs.showHome);
	message.AddBool("show bookmarks", prefs.showBookmarks);
	message.AddBool("zoom text only", prefs.zoomText);
	message.AddString("download path", prefs.downloadPath);
	message.AddBool("ask download", prefs.askDownload);
	message.AddBool("show menu bar", prefs.showMenuBar);
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	if (file.InitCheck() == B_OK)
		message.Flatten(&file);
}


static status_t
bookmarks_dir(BPath* path, const char* sub)
{
	status_t status = settings_path(path, "Bookmarks");
	if (status != B_OK)
		return status;
	create_directory(path->Path(), 0777);
	if (sub != NULL) {
		status = path->Append(sub);
		if (status == B_OK)
			create_directory(path->Path(), 0777);
	}
	return status;
}


status_t
aomi_bookmark_add(const char* url, const char* title)
{
	if (url == NULL || url[0] == '\0')
		return B_BAD_VALUE;
	BPath dir;
	status_t status = bookmarks_dir(&dir, NULL);
	if (status != B_OK)
		return status;
	BString name(title != NULL && title[0] != '\0' ? title : url);
	name.ReplaceAll("/", "-");
	name.ReplaceAll(":", "-");
	if (name.Length() > 80)
		name.Truncate(80);
	BPath file(dir);
	file.Append(name.String());
	BFile node(file.Path(), B_CREATE_FILE | B_WRITE_ONLY);
	status = node.InitCheck();
	if (status != B_OK)
		return status;
	BString urlString(url);
	urlString << '\0';
	BString titleString(title != NULL ? title : url);
	titleString << '\0';
	node.WriteAttr("META:url", B_STRING_TYPE, 0, urlString.String(),
		urlString.Length());
	node.WriteAttr("META:title", B_STRING_TYPE, 0, titleString.String(),
		titleString.Length());
	BNodeInfo info(&node);
	info.SetType("application/x-vnd.Be-bookmark");
	return B_OK;
}


bool
aomi_bookmark_has(const char* url)
{
	if (url == NULL)
		return false;
	BPath dir;
	if (bookmarks_dir(&dir, NULL) != B_OK)
		return false;
	BDirectory directory(dir.Path());
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;
		BNode node(&entry);
		char stored[2048];
		ssize_t n = node.ReadAttr("META:url", B_STRING_TYPE, 0, stored,
			sizeof(stored) - 1);
		if (n < 0)
			continue;
		stored[n] = '\0';
		if (strcmp(stored, url) == 0)
			return true;
	}
	return false;
}


static void
fill_dir(BMenu* menu, const char* path, uint32 what)
{
	BDirectory directory(path);
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;
		BNode node(&entry);
		char url[2048];
		char title[512];
		ssize_t n = node.ReadAttr("META:url", B_STRING_TYPE, 0, url,
			sizeof(url) - 1);
		if (n <= 0)
			continue;
		url[n] = '\0';
		ssize_t t = node.ReadAttr("META:title", B_STRING_TYPE, 0, title,
			sizeof(title) - 1);
		if (t > 0)
			title[t] = '\0';
		else {
			entry.GetName(title);
		}
		BMessage* message = new BMessage(what);
		message->AddString("url", url);
		menu->AddItem(new BMenuItem(title, message));
	}
}


void
aomi_fill_bookmarks(BMenu* menu, uint32 what)
{
	BPath dir;
	if (bookmarks_dir(&dir, NULL) != B_OK)
		return;
	fill_dir(menu, dir.Path(), what);
}


void
aomi_open_bookmarks()
{
	BPath dir;
	if (bookmarks_dir(&dir, NULL) != B_OK)
		return;
	entry_ref ref;
	if (get_ref_for_path(dir.Path(), &ref) == B_OK)
		be_roster->Launch(&ref);
}


void
aomi_history_add(const char* url)
{
	if (url == NULL || url[0] == '\0' || strncmp(url, "aomi:", 5) == 0)
		return;
	if (strncmp(url, "data:", 5) == 0)
		return;
	BPath path;
	if (settings_path(&path, "BrowsingHistory") != B_OK)
		return;
	BMessage archive;
	{
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			archive.Unflatten(&file);
	}
	if (!archive.HasInt32("max history item age"))
		archive.AddInt32("max history item age", 7);
	BMessage existing;
	for (int32 i = 0; archive.FindMessage("history item", i, &existing) == B_OK;
			i++) {
		BString have;
		if (existing.FindString("url", &have) == B_OK && have == url)
			return;
		existing.MakeEmpty();
	}
	BMessage item;
	BMessage when;
	BDateTime now = BDateTime::CurrentDateTime(B_LOCAL_TIME);
	if (now.Archive(&when) == B_OK)
		item.AddMessage("date time", &when);
	item.AddString("url", url);
	item.AddUInt32("invokations", 1);
	archive.AddMessage("history item", &item);
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	if (file.InitCheck() == B_OK)
		archive.Flatten(&file);
}


void
aomi_fill_history(BMenu* menu, uint32 what)
{
	BPath path;
	if (settings_path(&path, "BrowsingHistory") != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage archive;
	if (file.InitCheck() != B_OK || archive.Unflatten(&file) != B_OK)
		return;
	BMessage item;
	int32 added = 0;
	for (int32 i = 0; archive.FindMessage("history item", i, &item) == B_OK
		&& added < 12; i++) {
		BString url;
		if (item.FindString("url", &url) != B_OK)
			continue;
		BMessage* message = new BMessage(what);
		message->AddString("url", url);
		menu->AddItem(new BMenuItem(url.String(), message));
		added++;
		item.MakeEmpty();
	}
}


int32
aomi_history_match(const char* text, BString* urls, int32 max)
{
	if (text == NULL || text[0] == '\0' || urls == NULL || max < 1)
		return 0;
	BPath path;
	if (settings_path(&path, "BrowsingHistory") != B_OK)
		return 0;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage archive;
	if (file.InitCheck() != B_OK || archive.Unflatten(&file) != B_OK)
		return 0;
	BMessage item;
	int32 count = 0;
	BString needle(text);
	needle.ToLower();
	for (int32 i = 0; archive.FindMessage("history item", i, &item) == B_OK
		&& count < max; i++) {
		BString url;
		if (item.FindString("url", &url) == B_OK) {
			BString lower(url);
			lower.ToLower();
			if (lower.FindFirst(needle) >= 0)
				urls[count++] = url;
		}
		item.MakeEmpty();
	}
	return count;
}


static void
encode_query(const BString& text, BString* out)
{
	static const char* hex = "0123456789ABCDEF";
	for (int32 i = 0; i < text.Length(); i++) {
		unsigned char c = (unsigned char)text.String()[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
			|| c == '~') {
			char one[2] = { (char)c, 0 };
			*out << one;
		} else if (c == ' ') {
			*out << "+";
		} else {
			char code[4] = { '%', hex[c >> 4], hex[c & 0xf], 0 };
			*out << code;
		}
	}
}


BString
aomi_smart_url(const BString& raw, const BString& search)
{
	BString text = trim(raw);
	if (text.Length() == 0)
		return BString();
	if (text.StartsWith("http://") || text.StartsWith("https://")
		|| text.StartsWith("file:") || text.StartsWith("about:")
		|| text.StartsWith("data:") || text.StartsWith("aomi:")
		|| text.StartsWith("ftp:")) {
		return text;
	}
	int32 colon = text.FindFirst(':');
	int32 space = text.FindFirst(' ');
	if (colon > 0 && (space < 0 || space > colon)) {
		BString scheme;
		scheme.Append(text, colon);
		BString signature("application/x-vnd.Be.URL.");
		signature << scheme;
		entry_ref ref;
		if (be_roster->FindApp(signature.String(), &ref) == B_OK) {
			const char* args[] = { text.String() };
			be_roster->Launch(signature.String(), 1, (char**)args);
			return BString();
		}
	}
	if (text == "localhost" || text.StartsWith("localhost/")
		|| text.StartsWith("localhost:")) {
		BString url("http://");
		url << text;
		return url;
	}
	int32 slash = text.FindFirst('/');
	BString host(text);
	if (slash >= 0)
		host.Truncate(slash);
	if (host.FindFirst('.') >= 0 && host.FindFirst(' ') < 0) {
		BString url("http://");
		url << text;
		return url;
	}
	BString query;
	encode_query(text, &query);
	BString url(search.Length() > 0 ? search : "https://duckduckgo.com/?q=%s");
	if (url.FindFirst("%s") >= 0)
		url.ReplaceFirst("%s", query);
	else {
		url << query;
	}
	return url;
}
