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

#include <algorithm>

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
	prefs->pageColors = 0;
	prefs->zoom = 100;
}


const AomiSearchEngine kAomiSearchEngines[] = {
	{ "DuckDuckGo", "https://duckduckgo.com/?q=%s" },
	{ "Startpage", "https://www.startpage.com/do/search?q=%s" },
	{ "Ecosia", "https://www.ecosia.org/search?q=%s" },
	{ "Bing", "https://www.bing.com/search?q=%s" },
	{ "Google", "https://www.google.com/search?q=%s" },
	{ "Wikipedia", "https://en.wikipedia.org/w/index.php?search=%s" },
	{ NULL, NULL }
};


int32
aomi_search_engine_index(const char* address)
{
	if (address == NULL)
		return -1;
	for (int32 i = 0; kAomiSearchEngines[i].name != NULL; i++) {
		if (strcmp(kAomiSearchEngines[i].address, address) == 0)
			return i;
	}
	return -1;
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
	// Get*() with the default: Haiku's Find*() clears the value when the
	// field is missing (an older settings file), so it cannot keep one.
	prefs.start = message.GetInt32("start", prefs.start);
	prefs.home = message.GetString("home page", prefs.home.String());
	prefs.search = message.GetString("search url", prefs.search.String());
	prefs.showHome = message.GetBool("show home", prefs.showHome);
	prefs.showBookmarks = message.GetBool("show bookmarks", prefs.showBookmarks);
	prefs.zoomText = message.GetBool("zoom text only", prefs.zoomText);
	prefs.downloadPath = message.GetString("download path",
		prefs.downloadPath.String());
	prefs.askDownload = message.GetBool("ask download", prefs.askDownload);
	prefs.showMenuBar = message.GetBool("show menu bar", prefs.showMenuBar);
	prefs.pageColors = message.GetInt32("page colors", prefs.pageColors);
	if (prefs.pageColors < 0 || prefs.pageColors > 2)
		prefs.pageColors = 0;
	prefs.zoom = message.GetInt32("zoom", prefs.zoom);
	if (prefs.zoom < 30 || prefs.zoom > 300)
		prefs.zoom = 100;
	if (prefs.start < 0 || prefs.start > 2)
		prefs.start = 0;
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
	message.AddInt32("page colors", prefs.pageColors);
	message.AddInt32("zoom", prefs.zoom);
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


status_t
aomi_history_clear()
{
	BPath path;
	status_t status = settings_path(&path, "BrowsingHistory");
	if (status != B_OK)
		return status;
	BEntry entry(path.Path());
	if (!entry.Exists())
		return B_OK;
	return entry.Remove();
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
		// A typed name opens the secure address, as in other browsers; a
		// machine on the local network (an IP address, a .local name) the
		// plain one, which is what such devices usually serve.
		BString bare(host);
		int32 port = bare.FindFirst(':');
		if (port >= 0)
			bare.Truncate(port);
		bool local = bare.EndsWith(".local");
		bool numeric = true;
		for (int32 i = 0; i < bare.Length(); i++) {
			char c = bare.String()[i];
			if (!isdigit((unsigned char)c) && c != '.')
				numeric = false;
		}
		BString url(local || numeric ? "http://" : "https://");
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


void
aomi_bookmark_bar(std::vector<BString>* titles, std::vector<BString>* urls)
{
	titles->clear();
	urls->clear();
	BPath dir;
	if (bookmarks_dir(&dir, "Bookmark bar") != B_OK)
		return;
	std::vector<std::pair<BString, BString> > found;
	BDirectory directory(dir.Path());
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;
		BNode node(&entry);
		char url[2048];
		ssize_t n = node.ReadAttr("META:url", B_STRING_TYPE, 0, url,
			sizeof(url) - 1);
		if (n <= 0)
			continue;
		url[n] = '\0';
		char name[B_FILE_NAME_LENGTH];
		entry.GetName(name);
		found.push_back(std::make_pair(BString(name), BString(url)));
	}
	std::sort(found.begin(), found.end());
	for (size_t i = 0; i < found.size(); i++) {
		titles->push_back(found[i].first);
		urls->push_back(found[i].second);
	}
}


status_t
aomi_open_external(const char* url)
{
	if (url == NULL)
		return B_BAD_VALUE;
	BString text(url);
	int32 colon = text.FindFirst(':');
	if (colon <= 0)
		return B_BAD_VALUE;
	BString scheme;
	text.CopyInto(scheme, 0, colon);
	scheme.ToLower();
	BString signature("application/x-vnd.Be.URL.");
	signature << scheme;
	entry_ref ref;
	if (be_roster->FindApp(signature.String(), &ref) != B_OK)
		return B_NAME_NOT_FOUND;
	const char* args[] = { url, NULL };
	status_t status = be_roster->Launch(signature.String(), 1,
		const_cast<char**>(args));
	return status == B_ALREADY_RUNNING ? B_OK : status;
}


void
aomi_session_save(const std::vector<BString>& urls)
{
	BPath path;
	if (settings_path(&path, "AomiSession") != B_OK)
		return;
	BMessage message;
	for (size_t i = 0; i < urls.size(); i++)
		message.AddString("url", urls[i]);
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	if (file.InitCheck() == B_OK)
		message.Flatten(&file);
}


void
aomi_session_load(std::vector<BString>* urls)
{
	urls->clear();
	BPath path;
	if (settings_path(&path, "AomiSession") != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage message;
	if (file.InitCheck() != B_OK || message.Unflatten(&file) != B_OK)
		return;
	const char* url = NULL;
	for (int32 i = 0; message.FindString("url", i, &url) == B_OK; i++)
		urls->push_back(url);
}
