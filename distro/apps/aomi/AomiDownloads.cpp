/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "AomiDownloads.h"

#include "AomiMessages.h"
#include "ServoContentView.h"
#include "WebContentView.h"

#include <Application.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <Notification.h>
#include <Path.h>
#include <Roster.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StatusBar.h>
#include <StringForSize.h>
#include <StringView.h>

#include <R5IconView.h>
#include <R5Icons.h>
#include <WindowEffects.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Aomi"


extern char** environ;


namespace {


struct Entry {
	AomiDownload	info;
	BString			url;
	BString			folder;
	// The user chose the path in a save panel: keep that name.
	bool			chosen;
	team_id			team;
	bool			cancel;
};


BLocker sLock("aomi downloads");
std::vector<Entry> sList;
int32 sNextId = 1;


Entry*
find_entry(int32 id)
{
	for (size_t i = 0; i < sList.size(); i++) {
		if (sList[i].info.id == id)
			return &sList[i];
	}
	return NULL;
}


void
post(uint32 what, const AomiDownload& info)
{
	if (be_app == NULL)
		return;
	BMessage message(what);
	message.AddInt32("id", info.id);
	message.AddString("name", info.name);
	message.AddInt64("size", info.expected);
	message.AddInt64("current", info.current);
	message.AddInt64("expected", info.expected);
	if (what == AOMI_DOWNLOAD_FINISHED)
		message.AddString("path", info.path);
	if (what == AOMI_DOWNLOAD_FAILED)
		message.AddString("text", info.error);
	be_app->PostMessage(&message);
}


bool
exists(const BString& path)
{
	struct stat st;
	return lstat(path.String(), &st) == 0;
}


// A file name nobody else uses in folder: "name", "name (2)", ... with the
// ending kept, and no ".part" of that name either.
BString
unique_path(const BString& folder, const BString& name)
{
	BString base(name);
	BString ending;
	int32 dot = name.FindLast('.');
	if (dot > 0) {
		name.CopyInto(base, 0, dot);
		name.CopyInto(ending, dot, name.Length() - dot);
		// .tar.gz and the like keep both parts.
		if (base.EndsWith(".tar")) {
			ending.Prepend(".tar");
			base.Truncate(base.Length() - 4);
		}
	}
	for (int32 n = 1; n < 1000; n++) {
		BString candidate(folder);
		candidate << "/" << base;
		if (n > 1)
			candidate << " (" << n << ")";
		candidate << ending;
		BString part(candidate);
		part << ".part";
		if (!exists(candidate) && !exists(part))
			return candidate;
	}
	BString fallback(folder);
	fallback << "/" << name << "." << (int32)system_time();
	return fallback;
}


int
hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


BString
percent_decode(const BString& text)
{
	BString out;
	const char* s = text.String();
	for (int32 i = 0; s[i] != '\0'; i++) {
		if (s[i] == '%' && hex_value(s[i + 1]) >= 0 && hex_value(s[i + 2]) >= 0) {
			char c = (char)(hex_value(s[i + 1]) * 16 + hex_value(s[i + 2]));
			out.Append(&c, 1);
			i += 2;
		} else
			out.Append(&s[i], 1);
	}
	return out;
}


// A name Tracker can hold: no slash, no control character, not "." or
// "..", at most 200 bytes.
BString
clean_name(const BString& raw)
{
	BString name;
	for (int32 i = 0; i < raw.Length(); i++) {
		unsigned char c = (unsigned char)raw.String()[i];
		if (c < 0x20 || c == '/' || c == 0x7f)
			continue;
		name.Append((const char*)&c, 1);
	}
	name.Trim();
	if (name == "." || name == "..")
		name = "";
	if (name.Length() > 200)
		name.TruncateChars(120);
	return name;
}


BString
name_from_url(const char* url)
{
	BString address(url);
	int32 cut = address.FindFirst('?');
	if (cut >= 0)
		address.Truncate(cut);
	cut = address.FindFirst('#');
	if (cut >= 0)
		address.Truncate(cut);
	int32 slash = address.FindLast('/');
	BString leaf;
	if (slash >= 0 && address.FindFirst("://") + 2 < slash)
		address.CopyInto(leaf, slash + 1, address.Length() - slash - 1);
	BString name = clean_name(percent_decode(leaf));
	if (name.Length() == 0)
		name = "download";
	return name;
}


// The last response's headers in curl's -D file (after redirects): its
// status, Content-Length and the file name of Content-Disposition.
void
read_headers(const BString& path, int32* status, int64* length,
	BString* fileName)
{
	FILE* file = fopen(path.String(), "r");
	if (file == NULL)
		return;
	char line[2048];
	while (fgets(line, sizeof(line), file) != NULL) {
		BString text(line);
		text.RemoveAll("\r").RemoveAll("\n");
		BString lower(text);
		lower.ToLower();
		if (lower.StartsWith("http/")) {
			// A new response (a redirect came first): start over.
			int32 space = text.FindFirst(' ');
			if (status != NULL && space > 0)
				*status = atoi(text.String() + space + 1);
			if (length != NULL)
				*length = -1;
			if (fileName != NULL)
				fileName->Truncate(0);
		} else if (lower.StartsWith("content-length:") && length != NULL) {
			*length = strtoll(text.String() + 15, NULL, 10);
		} else if (lower.StartsWith("content-disposition:") && fileName != NULL) {
			int32 at = lower.FindFirst("filename=");
			if (at >= 0 && lower.FindFirst("filename*=") < 0) {
				BString value(text.String() + at + 9);
				value.Trim();
				if (value.StartsWith("\"")) {
					value.Remove(0, 1);
					int32 end = value.FindFirst('"');
					if (end >= 0)
						value.Truncate(end);
				} else {
					int32 end = value.FindFirst(';');
					if (end >= 0)
						value.Truncate(end);
				}
				*fileName = clean_name(value);
			}
		}
	}
	fclose(file);
}


BString
error_text(int code, int32 status)
{
	// curl's exit codes (curl(1), EXIT CODES).
	BString text;
	switch (code) {
		case 6:
			return B_TRANSLATE("The site could not be found. Check the "
				"connection.");
		case 7:
			return B_TRANSLATE("The site could not be reached.");
		case 22:
			text.SetToFormat(B_TRANSLATE("The site answered with error %d."),
				(int)status);
			return text;
		case 23:
			return B_TRANSLATE("The file could not be written. Is the disk "
				"full?");
		case 28:
			return B_TRANSLATE("The site took too long to answer.");
		case 35:
		case 60:
			return B_TRANSLATE("The secure connection failed.");
		case 127:
			return B_TRANSLATE("The download program could not be started.");
	}
	return B_TRANSLATE("The download stopped.");
}


void
notify(const AomiDownload& info, bool done)
{
	BNotification notification(done
		? B_INFORMATION_NOTIFICATION : B_ERROR_NOTIFICATION);
	notification.SetGroup("Aomi");
	notification.SetTitle(done ? B_TRANSLATE("Download finished")
		: B_TRANSLATE("Download failed"));
	notification.SetContent(info.name);
	entry_ref ref;
	if (done && get_ref_for_path(info.path.String(), &ref) == B_OK)
		notification.SetOnClickFile(&ref);
	notification.Send();
}


int32
download_thread(void* data)
{
	int32 id = (int32)(addr_t)data;
	BString url;
	BString target;
	BString part;
	BString header;
	sLock.Lock();
	Entry* entry = find_entry(id);
	if (entry == NULL) {
		sLock.Unlock();
		return 0;
	}
	url = entry->url;
	target = entry->info.path;
	sLock.Unlock();
	part = target;
	part << ".part";
	header.SetToFormat("/tmp/aomi-download-%d-%d.head", (int)getpid(),
		(int)id);

	// curl writes the file and the response headers; Aomi reads both for
	// the progress. No shell: the address is one argument.
	const char* args[] = { "/bin/curl", "-fsSL", "--proto", "=http,https",
		"--proto-redir", "=http,https", "--connect-timeout", "30",
		"-D", header.String(), "-o", part.String(), url.String(), NULL };
	thread_id team = load_image(13, args, (const char**)environ);
	int code = 127;
	int32 status = 0;
	int64 length = -1;
	if (team >= 0) {
		sLock.Lock();
		entry = find_entry(id);
		if (entry != NULL)
			entry->team = team;
		bool cancel = entry == NULL || entry->cancel;
		sLock.Unlock();
		if (cancel)
			kill(team, SIGTERM);
		resume_thread(team);
		int64 reported = -2;
		while (true) {
			int result = 0;
			pid_t done = waitpid(team, &result, WNOHANG);
			if (done == team) {
				code = WIFEXITED(result) ? WEXITSTATUS(result) : 1;
				break;
			}
			if (done < 0 && errno != EINTR) {
				code = 1;
				break;
			}
			struct stat st;
			int64 current = stat(part.String(), &st) == 0 ? st.st_size : 0;
			if (length < 0)
				read_headers(header, &status, &length, NULL);
			if (current != reported) {
				reported = current;
				sLock.Lock();
				entry = find_entry(id);
				AomiDownload info;
				if (entry != NULL) {
					entry->info.current = current;
					entry->info.expected = length;
					info = entry->info;
				}
				sLock.Unlock();
				if (entry != NULL)
					post(AOMI_DOWNLOAD_PROGRESS, info);
			}
			snooze(250000);
		}
	}

	BString serverName;
	read_headers(header, &status, &length, &serverName);
	unlink(header.String());

	sLock.Lock();
	entry = find_entry(id);
	if (entry == NULL) {
		sLock.Unlock();
		unlink(part.String());
		return 0;
	}
	entry->team = -1;
	AomiDownload info;
	bool notifyUser = false;
	if (entry->cancel) {
		unlink(part.String());
		entry->info.state = AOMI_DOWNLOAD_CANCELLED;
		entry->info.error = B_TRANSLATE("Cancelled");
		info = entry->info;
		sLock.Unlock();
		post(AOMI_DOWNLOAD_FAILED, info);
		aomi_log_add(BString("download cancelled ").Append(info.shown).String());
		return 0;
	}
	if (code == 0) {
		// The server's name for the file, when the address had none of
		// its own (".../download?id=7").
		if (!entry->chosen && serverName.Length() > 0
			&& serverName != entry->info.name) {
			BString better = unique_path(entry->folder, serverName);
			target = better;
		}
		if (rename(part.String(), target.String()) != 0) {
			code = 23;
		} else {
			BPath path(target.String());
			entry->info.path = target;
			entry->info.name = path.Leaf();
			struct stat st;
			if (stat(target.String(), &st) == 0) {
				entry->info.current = st.st_size;
				entry->info.expected = st.st_size;
			}
		}
	}
	if (code == 0) {
		entry->info.state = AOMI_DOWNLOAD_DONE;
		info = entry->info;
		notifyUser = true;
	} else {
		unlink(part.String());
		entry->info.state = AOMI_DOWNLOAD_FAILED_STATE;
		entry->info.error = error_text(code, status);
		info = entry->info;
		notifyUser = true;
	}
	sLock.Unlock();

	post(code == 0 ? AOMI_DOWNLOAD_FINISHED : AOMI_DOWNLOAD_FAILED, info);
	BString line;
	line.SetToFormat("download %s -> %s (%s)", info.shown.String(),
		info.path.String(), code == 0 ? "ok" : "failed");
	aomi_log_add(line.String());
	if (notifyUser)
		notify(info, code == 0);
	return 0;
}


status_t
run(int32 id)
{
	thread_id thread = spawn_thread(download_thread, "aomi download",
		B_LOW_PRIORITY, (void*)(addr_t)id);
	if (thread < 0)
		return thread;
	return resume_thread(thread);
}


status_t
start(const char* url, const char* folder, const char* path,
	const char* shown)
{
	if (url == NULL || (strncmp(url, "http://", 7) != 0
			&& strncmp(url, "https://", 8) != 0))
		return B_BAD_VALUE;
	Entry entry;
	entry.url = url;
	entry.chosen = path != NULL;
	entry.team = -1;
	entry.cancel = false;
	if (path != NULL) {
		BPath target(path);
		if (target.InitCheck() != B_OK)
			return B_BAD_VALUE;
		BPath parent;
		target.GetParent(&parent);
		entry.folder = parent.Path();
		entry.info.path = target.Path();
		entry.info.name = target.Leaf();
	} else {
		BString dir(folder != NULL ? folder : "");
		if (dir.Length() == 0) {
			BPath desktop;
			if (find_directory(B_DESKTOP_DIRECTORY, &desktop) == B_OK)
				dir = desktop.Path();
		}
		create_directory(dir.String(), 0755);
		entry.folder = dir;
		entry.info.name = name_from_url(url);
		entry.info.path = unique_path(dir, entry.info.name);
		entry.info.name = BPath(entry.info.path.String()).Leaf();
	}
	entry.info.shown = shown != NULL ? shown : url;
	entry.info.current = 0;
	entry.info.expected = -1;
	entry.info.state = AOMI_DOWNLOAD_RUNNING;
	sLock.Lock();
	entry.info.id = sNextId++;
	sList.push_back(entry);
	AomiDownload info = entry.info;
	sLock.Unlock();
	post(AOMI_DOWNLOAD_ADDED, info);
	aomi_log_add(BString("download start ").Append(info.shown).String());
	return run(info.id);
}


}	// namespace


status_t
aomi_start_download(const char* url, const char* destDir, const char* shown)
{
	return start(url, destDir, NULL, shown);
}


status_t
aomi_start_download_to(const char* url, const char* path)
{
	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;
	return start(url, NULL, path, NULL);
}


status_t
aomi_download_cancel(int32 id)
{
	BAutolock _(sLock);
	Entry* entry = find_entry(id);
	if (entry == NULL)
		return B_BAD_INDEX;
	if (entry->info.state != AOMI_DOWNLOAD_RUNNING)
		return B_NOT_ALLOWED;
	entry->cancel = true;
	if (entry->team >= 0)
		kill(entry->team, SIGTERM);
	return B_OK;
}


status_t
aomi_download_retry(int32 id)
{
	sLock.Lock();
	Entry* entry = find_entry(id);
	if (entry == NULL || entry->info.state == AOMI_DOWNLOAD_RUNNING
		|| entry->info.state == AOMI_DOWNLOAD_DONE) {
		sLock.Unlock();
		return B_NOT_ALLOWED;
	}
	entry->cancel = false;
	entry->info.state = AOMI_DOWNLOAD_RUNNING;
	entry->info.current = 0;
	entry->info.expected = -1;
	entry->info.error = "";
	if (!entry->chosen) {
		entry->info.path = unique_path(entry->folder, entry->info.name);
		entry->info.name = BPath(entry->info.path.String()).Leaf();
	}
	AomiDownload info = entry->info;
	sLock.Unlock();
	post(AOMI_DOWNLOAD_PROGRESS, info);
	return run(id);
}


void
aomi_download_clear_finished()
{
	sLock.Lock();
	for (size_t i = 0; i < sList.size();) {
		if (sList[i].info.state != AOMI_DOWNLOAD_RUNNING)
			sList.erase(sList.begin() + i);
		else
			i++;
	}
	sLock.Unlock();
	if (be_app != NULL) {
		BMessage message(AOMI_DOWNLOAD_ADDED);
		message.AddInt32("id", 0);
		be_app->PostMessage(&message);
	}
}


void
aomi_download_list(std::vector<AomiDownload>* list)
{
	list->clear();
	BAutolock _(sLock);
	for (size_t i = 0; i < sList.size(); i++)
		list->push_back(sList[i].info);
}


bool
aomi_download_get(int32 id, AomiDownload* download)
{
	BAutolock _(sLock);
	Entry* entry = find_entry(id);
	if (entry == NULL)
		return false;
	*download = entry->info;
	return true;
}


int32
aomi_download_count_running()
{
	BAutolock _(sLock);
	int32 count = 0;
	for (size_t i = 0; i < sList.size(); i++) {
		if (sList[i].info.state == AOMI_DOWNLOAD_RUNNING)
			count++;
	}
	return count;
}


void
aomi_download_stop_all()
{
	BAutolock _(sLock);
	for (size_t i = 0; i < sList.size(); i++) {
		if (sList[i].info.state == AOMI_DOWNLOAD_RUNNING) {
			sList[i].cancel = true;
			if (sList[i].team >= 0)
				kill(sList[i].team, SIGTERM);
		}
	}
}


// #pragma mark - Downloads window


namespace {


// Text on the list background, which is not the panel colour.
void
list_colors(BView* view, bool muted)
{
	view->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	view->SetLowUIColor(B_LIST_BACKGROUND_COLOR);
	if (muted) {
		view->SetHighColor(mix_color(ui_color(B_LIST_ITEM_TEXT_COLOR),
			ui_color(B_LIST_BACKGROUND_COLOR), 110));
	} else
		view->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
}


const uint32 kMsgOpen = 'dOpn';
const uint32 kMsgShow = 'dShw';
const uint32 kMsgCancel = 'dCan';
const uint32 kMsgRetry = 'dRty';
const uint32 kMsgClear = 'dClr';
const uint32 kMsgFolder = 'dFld';

BLocker sWindowLock("aomi downloads window");
AomiDownloadsWindow* sWindow = NULL;


float
unit()
{
	return be_plain_font->Size() / 12.0f;
}


BString
size_text(int64 bytes)
{
	char buffer[64];
	BPrivate::string_for_size((double)bytes, buffer, sizeof(buffer));
	return BString(buffer);
}


BString
status_text(const AomiDownload& info)
{
	BString text;
	switch (info.state) {
		case AOMI_DOWNLOAD_RUNNING:
			if (info.expected > 0) {
				text.SetToFormat(B_TRANSLATE("%s of %s"),
					size_text(info.current).String(),
					size_text(info.expected).String());
			} else if (info.current > 0)
				text = size_text(info.current);
			else
				text = B_TRANSLATE("Starting…");
			break;
		case AOMI_DOWNLOAD_DONE:
			text.SetToFormat(B_TRANSLATE("Done, %s"),
				size_text(info.current).String());
			break;
		default:
			text = info.error;
			break;
	}
	return text;
}


// One row of the list: the file's icon, its name, the progress and what
// can be done with it now.
class DownloadRow : public BGroupView {
public:
	DownloadRow(const AomiDownload& info, BHandler* target)
		:
		BGroupView("download", B_HORIZONTAL),
		fId(info.id),
		fState(-1),
		fTarget(target)
	{
		SetFlags(Flags() | B_WILL_DRAW);
		SetViewUIColor(B_LIST_BACKGROUND_COLOR);
		float u = unit();
		fIcon = new BAppearanceKit::BIconView("icon");
		fName = new BStringView("name", info.name.String());
		fName->SetFont(be_bold_font);
		fName->SetTruncation(B_TRUNCATE_MIDDLE);
		fName->SetExplicitMinSize(BSize(u * 120, B_SIZE_UNSET));
		fName->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		fBar = new BStatusBar("progress");
		fBar->SetBarHeight(u * 6);
		fBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		fStatus = new BStringView("status", "");
		fStatus->SetTruncation(B_TRUNCATE_END);
		fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		list_colors(fName, false);
		list_colors(fStatus, true);
		fBar->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
		fBar->SetLowUIColor(B_LIST_BACKGROUND_COLOR);
		fBar->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
		fMain = new BButton("main", "", NULL);
		fSecond = new BButton("second", "", NULL);
		BLayoutBuilder::Group<>(this, B_HORIZONTAL, u * 10)
			.SetInsets(u * 12, u * 9, u * 12, u * 9)
			.Add(fIcon)
			.AddGroup(B_VERTICAL, u * 3, 1)
				.Add(fName)
				.Add(fBar)
				.Add(fStatus)
				.End()
			.Add(fSecond)
			.Add(fMain);
		Update(info);
	}

	int32 Id() const { return fId; }

	void Update(const AomiDownload& info)
	{
		fName->SetText(info.name.String());
		fStatus->SetText(status_text(info).String());
		bool running = info.state == AOMI_DOWNLOAD_RUNNING;
		if (running && info.expected > 0) {
			fBar->SetMaxValue(100);
			fBar->Update(100.0f * info.current / info.expected
				- fBar->CurrentValue());
		} else if (running)
			fBar->Reset();
		if (running == fBar->IsHidden(fBar)) {
			if (running)
				fBar->Show();
			else
				fBar->Hide();
		}
		if (info.state != fState) {
			fState = info.state;
			_Buttons(info);
		}
	}

	virtual void Draw(BRect updateRect)
	{
		BGroupView::Draw(updateRect);
		// A line between rows, as in a list.
		SetHighColor(tint_color(ui_color(B_LIST_BACKGROUND_COLOR),
			B_DARKEN_1_TINT));
		StrokeLine(Bounds().LeftBottom(), Bounds().RightBottom());
	}

private:
	void _Buttons(const AomiDownload& info)
	{
		float side = be_control_look->ComposeIconSize(32).Width();
		BBitmap* icon = NULL;
		if (info.state == AOMI_DOWNLOAD_DONE)
			icon = r5_file_icon_bitmap(info.path.String(), side);
		if (icon == NULL)
			icon = r5_mime_icon_bitmap("application/octet-stream", side);
		fIcon->SetIcon(icon);
		delete icon;

		uint32 main = kMsgCancel;
		const char* mainLabel = B_TRANSLATE("Cancel");
		bool second = false;
		if (info.state == AOMI_DOWNLOAD_DONE) {
			main = kMsgOpen;
			mainLabel = B_TRANSLATE("Open");
			second = true;
		} else if (info.state != AOMI_DOWNLOAD_RUNNING) {
			main = kMsgRetry;
			mainLabel = B_TRANSLATE("Try again");
		}
		BMessage* message = new BMessage(main);
		message->AddInt32("id", fId);
		fMain->SetLabel(mainLabel);
		fMain->SetMessage(message);
		fMain->SetTarget(fTarget);
		message = new BMessage(kMsgShow);
		message->AddInt32("id", fId);
		fSecond->SetLabel(B_TRANSLATE("Show in folder"));
		fSecond->SetMessage(message);
		fSecond->SetTarget(fTarget);
		if (second == fSecond->IsHidden(fSecond)) {
			if (second)
				fSecond->Show();
			else
				fSecond->Hide();
		}
	}

	int32				fId;
	int32				fState;
	BHandler*			fTarget;
	BAppearanceKit::BIconView* fIcon;
	BStringView*		fName;
	BStatusBar*			fBar;
	BStringView*		fStatus;
	BButton*			fMain;
	BButton*			fSecond;
};


}	// namespace


AomiDownloadsWindow::AomiDownloadsWindow()
	:
	BWindow(BRect(0, 0, 559, 399), B_TRANSLATE("Downloads"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fRows(NULL),
	fEmpty(NULL),
	fClear(NULL)
{
	// No glass: a form is many controls, and each one paints the panel
	// colour, which showed as a box behind every label over the blur. R5's
	// Settings window is opaque too.
	float u = unit();

	fRows = new BGroupView("rows", B_VERTICAL, 0);
	fRows->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	BLayoutBuilder::Group<>(fRows).AddGlue();

	// What the list says while it is empty.
	BGroupView* empty = new BGroupView("empty", B_VERTICAL, u * 6);
	empty->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	rgb_color sub = mix_color(ui_color(B_LIST_ITEM_TEXT_COLOR),
		ui_color(B_LIST_BACKGROUND_COLOR), 110);
	BBitmap* glyph = r5_glyph_bitmap(R5_GLYPH_DOWNLOAD, floorf(u * 40), sub);
	BAppearanceKit::BIconView* picture
		= new BAppearanceKit::BIconView("picture", glyph);
	delete glyph;
	BStringView* title = new BStringView("title",
		B_TRANSLATE("No downloads yet"));
	title->SetFont(be_bold_font);
	title->SetAlignment(B_ALIGN_CENTER);
	BStringView* note = new BStringView("note",
		B_TRANSLATE("Files you save from web pages are listed here."));
	note->SetAlignment(B_ALIGN_CENTER);
	list_colors(title, false);
	list_colors(note, true);
	BLayoutBuilder::Group<>(empty)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(picture)
			.AddGlue()
			.End()
		.Add(title)
		.Add(note)
		.AddGlue();
	fEmpty = empty;

	BGroupView* content = new BGroupView("content", B_VERTICAL, 0);
	content->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	BLayoutBuilder::Group<>(content)
		.Add(fEmpty, 1)
		.Add(fRows, 1);
	BScrollView* scroll = new BScrollView("scroll", content, 0, false, true,
		B_PLAIN_BORDER);
	// Wide enough for a row with both buttons beside the scroll bar.
	scroll->SetExplicitMinSize(BSize(u * 560, u * 220));

	BButton* folder = new BButton("folder",
		B_TRANSLATE("Open downloads folder"), new BMessage(kMsgFolder));
	fClear = new BButton("clear", B_TRANSLATE("Clear finished"),
		new BMessage(kMsgClear));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(scroll, 1)
		.AddGroup(B_HORIZONTAL)
			.Add(folder)
			.AddGlue()
			.Add(fClear)
			.End();

	ResizeTo(floorf(u * 640), floorf(u * 420));
	CenterOnScreen();
	_Rebuild();
}


AomiDownloadsWindow::~AomiDownloadsWindow()
{
	BAutolock _(sWindowLock);
	if (sWindow == this)
		sWindow = NULL;
}


bool
AomiDownloadsWindow::QuitRequested()
{
	BAutolock _(sWindowLock);
	if (sWindow == this)
		sWindow = NULL;
	return true;
}


/*static*/ void
AomiDownloadsWindow::ShowWindow(const char* folder)
{
	BAutolock _(sWindowLock);
	if (sWindow == NULL) {
		sWindow = new AomiDownloadsWindow();
		sWindow->fFolder = folder != NULL ? folder : "";
		sWindow->Show();
		return;
	}
	if (sWindow->Lock()) {
		sWindow->fFolder = folder != NULL ? folder : "";
		if (sWindow->IsHidden())
			sWindow->Show();
		sWindow->Activate(true);
		sWindow->Unlock();
	}
}


void
AomiDownloadsWindow::_Rebuild()
{
	std::vector<AomiDownload> list;
	aomi_download_list(&list);
	// Newest first, as in a browser.
	while (fRows->CountChildren() > 0) {
		BView* child = fRows->ChildAt(0);
		child->RemoveSelf();
		delete child;
	}
	BGroupLayout* layout = fRows->GroupLayout();
	while (layout->CountItems() > 0)
		delete layout->RemoveItem((int32)0);
	bool finished = false;
	for (int32 i = (int32)list.size() - 1; i >= 0; i--) {
		layout->AddView(new DownloadRow(list[i], this));
		if (list[i].state != AOMI_DOWNLOAD_RUNNING)
			finished = true;
	}
	layout->AddItem(BSpaceLayoutItem::CreateGlue());
	bool empty = list.empty();
	if (empty == fEmpty->IsHidden(fEmpty)) {
		if (empty)
			fEmpty->Show();
		else
			fEmpty->Hide();
	}
	if (empty != fRows->IsHidden(fRows)) {
		if (empty)
			fRows->Hide();
		else
			fRows->Show();
	}
	fClear->SetEnabled(finished);
}


void
AomiDownloadsWindow::_Update(int32 id)
{
	AomiDownload info;
	if (!aomi_download_get(id, &info))
		return;
	for (int32 i = 0; i < fRows->CountChildren(); i++) {
		DownloadRow* row = dynamic_cast<DownloadRow*>(fRows->ChildAt(i));
		if (row != NULL && row->Id() == id) {
			row->Update(info);
			std::vector<AomiDownload> list;
			aomi_download_list(&list);
			bool finished = false;
			for (size_t j = 0; j < list.size(); j++)
				finished |= list[j].state != AOMI_DOWNLOAD_RUNNING;
			fClear->SetEnabled(finished);
			return;
		}
	}
	_Rebuild();
}


void
AomiDownloadsWindow::MessageReceived(BMessage* message)
{
	int32 id = message->GetInt32("id", 0);
	switch (message->what) {
		case AOMI_DOWNLOAD_ADDED:
			_Rebuild();
			break;
		case AOMI_DOWNLOAD_PROGRESS:
		case AOMI_DOWNLOAD_FINISHED:
		case AOMI_DOWNLOAD_FAILED:
			_Update(id);
			break;
		case kMsgOpen:
		case kMsgShow:
		{
			AomiDownload info;
			if (!aomi_download_get(id, &info))
				break;
			BPath path(info.path.String());
			if (message->what == kMsgShow)
				path.GetParent(&path);
			entry_ref ref;
			if (get_ref_for_path(path.Path(), &ref) == B_OK)
				be_roster->Launch(&ref);
			break;
		}
		case kMsgCancel:
			aomi_download_cancel(id);
			break;
		case kMsgRetry:
			aomi_download_retry(id);
			break;
		case kMsgClear:
			aomi_download_clear_finished();
			break;
		case kMsgFolder:
		{
			BString folder(fFolder);
			if (folder.Length() == 0) {
				BPath desktop;
				if (find_directory(B_DESKTOP_DIRECTORY, &desktop) == B_OK)
					folder = desktop.Path();
			}
			entry_ref ref;
			if (get_ref_for_path(folder.String(), &ref) == B_OK)
				be_roster->Launch(&ref);
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
