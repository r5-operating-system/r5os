/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
trademarks of Be Incorporated in the United States and other countries. Other
brand product names are registered trademarks or trademarks of their respective
holders.
All rights reserved.
*/

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */



#include "BarApp.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT

#include "DeskbarMenu.h"

static const uint32 kConfirmLogout = 'CfLo';
static const uint32 kConfirmForceQuit = 'CfQt';


namespace BPrivate {
	void ForceUnloadCatalog();
}
// SPDX-SnippetEnd

#include <locale.h>
#include <strings.h>

#include <AppFileInfo.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Debug.h>
#include <Directory.h>
#include <Dragger.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <Entry.h>
// SPDX-SnippetEnd
#include <File.h>
#include <FindDirectory.h>
#include <IconUtils.h>
#include <Locale.h>
#include <Message.h>
#include <Messenger.h>
#include <Mime.h>
#include <Path.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <Alert.h>
#include <AdminLock.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <StringFormat.h>
#include <StringView.h>
#include <TextView.h>
#include <Invoker.h>
#include <OS.h>
#include <LaunchRoster.h>
// SPDX-SnippetEnd
#include <Roster.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <String.h>
#include <WindowInfo.h>
#include <R5Admin.h>
#include <R5UI.h>
#include <pwd.h>
#include <unistd.h>
// SPDX-SnippetEnd

#include <DeskbarPrivate.h>
#include <RosterPrivate.h>
#include "tracker_private.h"

#include "BarView.h"
#include "BarWindow.h"
#include "DeskbarUtils.h"
#include "FSUtils.h"
#include "PreferencesWindow.h"
#include "ResourceSet.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <WindowEffects.h>

#include "BarSettings.h"
#include "DeskbarStyle.h"
#include "ExpandoMenuBar.h"
#include "ShowHideMenuItem.h"
#include <algorithm>
#include <strings.h>
// SPDX-SnippetEnd
#include "StatusView.h"
#include "Switcher.h"

#include "icons.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BarApp"
// SPDX-SnippetEnd


BLocker TBarApp::sSubscriberLock;
BList TBarApp::sBarTeamInfoList;
BList TBarApp::sWindowIconCache;
BList TBarApp::sSubscribers;


const uint32 kShowDeskbarMenu		= 'BeMn';
const uint32 kShowTeamMenu			= 'TmMn';


int
main()
{
	setlocale(LC_ALL, "");
	TBarApp app;
	app.Run();

	return B_OK;
}


TBarApp::TBarApp()
	:
	BServer(kDeskbarSignature, true, NULL),
	fSettingsFile(NULL),
	fClockSettingsFile(NULL),
	fPreferencesWindow(NULL)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	debug_printf("R5 deskbar: BarApp from distro\n");
// SPDX-SnippetEnd
	InitSettings();
	InitIconPreloader();

	fBarWindow = new TBarWindow();
	fBarView = fBarWindow->BarView();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// B_SOME_APP_ACTIVATED gives the front application of Default (R5)
	be_roster->StartWatching(this, B_REQUEST_LAUNCHED | B_REQUEST_QUIT
		| B_REQUEST_ACTIVATED);
	app_info activeInfo;
	if (be_roster->GetActiveAppInfo(&activeInfo) == B_OK)
		R5Deskbar::SetFrontTeam(activeInfo.team);
// SPDX-SnippetEnd

	gLocalizedNamePreferred
		= BLocaleRoster::Default()->IsFilesystemTranslationPreferred();

	sBarTeamInfoList.MakeEmpty();

	BList teamList;
	int32 numTeams;
	be_roster->GetAppList(&teamList);
	numTeams = teamList.CountItems();
	for (int32 i = 0; i < numTeams; i++) {
		app_info appInfo;
		team_id tID = (addr_t)teamList.ItemAt(i);
		if (be_roster->GetRunningAppInfo(tID, &appInfo) == B_OK) {
			AddTeam(appInfo.team, appInfo.flags, appInfo.signature,
				&appInfo.ref);
		}
	}

	sWindowIconCache.MakeEmpty();
	for (int32 id = R_WindowShownIcon; id <= R_WindowHiddenSwitchIcon; id++) {
		if (id == R_ResizeIcon)
			continue;

		WindowIconCache* winCache = new WindowIconCache(id);
		_CacheWindowIcon(winCache);
		sWindowIconCache.AddItem(winCache);
	}

	sSubscribers.MakeEmpty();
	fSwitcherMessenger = BMessenger(new TSwitchManager());
	fBarWindow->Show();

	fBarWindow->Lock();
	fBarView->UpdatePlacement();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fBarView->ApplyStyle();
// SPDX-SnippetEnd
	fBarWindow->Unlock();

	// this messenger now targets the barview instead of the
	// statusview so that all additions to the tray
	// follow the same path
	fStatusViewMessenger = BMessenger(fBarWindow->FindView("BarView"));
}


TBarApp::~TBarApp()
{
	be_roster->StopWatching(this);

	int32 teamCount = sBarTeamInfoList.CountItems();
	for (int32 i = 0; i < teamCount; i++) {
		BarTeamInfo* barInfo = (BarTeamInfo*)sBarTeamInfoList.ItemAt(i);
		delete barInfo;
	}

	int32 subsCount = sSubscribers.CountItems();
	for (int32 i = 0; i < subsCount; i++) {
		BMessenger* messenger
			= static_cast<BMessenger*>(sSubscribers.ItemAt(i));
		delete messenger;
	}

	SaveSettings();

	delete fSettingsFile;
	delete fClockSettingsFile;
}


bool
TBarApp::QuitRequested()
{
	// don't allow the user to quit
	if (CurrentMessage() && CurrentMessage()->FindBool("shortcut")) {
		// but close the preferences window
		QuitPreferencesWindow();
		return false;
	}

	// system quitting, call inherited to notify all loopers
	fBarWindow->SaveSettings();
	BApplication::QuitRequested();
	return true;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
status_t
r5_save_deskbar_settings(const char* name, const BMessage& message)
{
	// A power loss between a truncate and the new bytes left a file that
	// Unflatten() rejects, and Deskbar came back on the defaults (#4635).
	// Write a new file beside it, sync it, then rename it over the old one:
	// the rename replaces the entry in one step, so the next start reads
	// either the old settings or the new ones.
	BPath path;
	status_t status = GetDeskbarSettingsDirectory(path, true);
	if (status != B_OK)
		return status;
	if ((status = path.Append(name)) != B_OK)
		return status;

	BString tempPath(path.Path());
	tempPath << ".new";

	BFile file(tempPath.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if ((status = file.InitCheck()) != B_OK)
		return status;
	if ((status = message.Flatten(&file)) == B_OK)
		status = file.Sync();
	file.Unset();

	BEntry entry(tempPath.String());
	if (status == B_OK)
		status = entry.Rename(path.Path(), true);
	if (status != B_OK)
		entry.Remove();

	return status;
}
// SPDX-SnippetEnd


void
TBarApp::SaveSettings()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (fSettingsFile != NULL && fSettingsFile->InitCheck() == B_OK) {
// SPDX-SnippetEnd
		fSettingsFile->Seek(0, SEEK_SET);
		BMessage prefs;
		prefs.AddBool("vertical", fSettings.vertical);
		prefs.AddBool("left", fSettings.left);
		prefs.AddBool("top", fSettings.top);
		prefs.AddInt32("state", fSettings.state);
		prefs.AddFloat("width", fSettings.width);
		prefs.AddPoint("switcherLoc", fSettings.switcherLoc);
		prefs.AddBool("showClock", fSettings.showClock);
		// applications
		prefs.AddBool("trackerAlwaysFirst", fSettings.trackerAlwaysFirst);
		prefs.AddBool("sortRunningApps", fSettings.sortRunningApps);
		prefs.AddBool("superExpando", fSettings.superExpando);
		prefs.AddBool("expandNewTeams", fSettings.expandNewTeams);
		prefs.AddBool("hideLabels", fSettings.hideLabels);
		prefs.AddInt32("iconSize", fSettings.iconSize);
		// recent items
		prefs.AddBool("recentDocsEnabled", fSettings.recentDocsEnabled);
		prefs.AddBool("recentFoldersEnabled", fSettings.recentFoldersEnabled);
		prefs.AddBool("recentAppsEnabled", fSettings.recentAppsEnabled);
		prefs.AddInt32("recentDocsCount", fSettings.recentDocsCount);
		prefs.AddInt32("recentFoldersCount", fSettings.recentFoldersCount);
		prefs.AddInt32("recentAppsCount", fSettings.recentAppsCount);
		// window
		prefs.AddBool("alwaysOnTop", fSettings.alwaysOnTop);
		prefs.AddBool("autoRaise", fSettings.autoRaise);
		prefs.AddBool("autoHide", fSettings.autoHide);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// Deskbar styles: new keys at the end (docs/design/DESKBAR-STYLES.md)
		prefs.AddInt32("style", fSettings.style);
		prefs.AddInt32("layout", fSettings.layout);
		prefs.AddBool("rounded", fSettings.rounded);
		prefs.AddBool("floating", fSettings.floating);
// SPDX-SnippetEnd

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// written beside the old file, synced, then renamed over it (#4635)
		r5_save_deskbar_settings("settings", prefs);
// SPDX-SnippetEnd
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (fClockSettingsFile != NULL
		&& fClockSettingsFile->InitCheck() == B_OK) {
// SPDX-SnippetEnd
		fClockSettingsFile->Seek(0, SEEK_SET);
		BMessage prefs;
		prefs.AddBool("showSeconds", fClockSettings.showSeconds);
		prefs.AddBool("showDayOfWeek", fClockSettings.showDayOfWeek);
		prefs.AddBool("showTimeZone", fClockSettings.showTimeZone);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// written beside the old file, synced, then renamed over it (#4635)
		r5_save_deskbar_settings("clock_settings", prefs);
// SPDX-SnippetEnd
	}
}


void
TBarApp::InitSettings()
{
	// compute metrics
	sIconGap = ceilf(be_control_look->DefaultLabelSpacing() / 3.0f);

	gDragRegionWidth = be_control_look->ComposeSpacing(B_USE_HALF_ITEM_SPACING);
	gDragWidth = ceilf(gDragRegionWidth * 0.6f);

	gMinReplicantHeight = gMinReplicantWidth =
		be_control_look->ComposeIconSize(B_MINI_ICON).IntegerWidth() + 1;

	// 1 pixel for left gutter
	// space for replicant tray (6 items)
	// 6 pixel drag region
	gMinimumTrayWidth = sIconGap + gMinReplicantWidth
		+ (kMinimumReplicantCount * sIconGap)
		+ (kMinimumReplicantCount * gMinReplicantWidth) + kGutter;

	gMinimumWindowWidth = kGutter + gMinimumTrayWidth + gDragRegionWidth;
	gMaximumWindowWidth = gMinimumWindowWidth * 2;

	// defaults
	desk_settings settings;
	settings.vertical = fDefaultSettings.vertical = true;
	settings.left = fDefaultSettings.left = false;
	settings.top = fDefaultSettings.top = true;
	settings.state = fDefaultSettings.state = kExpandoState;
	settings.width = fDefaultSettings.width = gMinimumWindowWidth;
	settings.switcherLoc = fDefaultSettings.switcherLoc = BPoint(5000, 5000);
	settings.showClock = fDefaultSettings.showClock = true;
	// applications
	settings.trackerAlwaysFirst = fDefaultSettings.trackerAlwaysFirst = true;
	settings.sortRunningApps = fDefaultSettings.sortRunningApps = false;
	settings.superExpando = fDefaultSettings.superExpando = false;
	settings.expandNewTeams = fDefaultSettings.expandNewTeams = false;
	settings.hideLabels = fDefaultSettings.hideLabels = false;
	settings.iconSize = fDefaultSettings.iconSize = kMinimumIconSize;
	// recent items
	settings.recentDocsEnabled = fDefaultSettings.recentDocsEnabled = true;
	settings.recentFoldersEnabled
		= fDefaultSettings.recentFoldersEnabled = true;
	settings.recentAppsEnabled = fDefaultSettings.recentAppsEnabled = true;
	settings.recentDocsCount = fDefaultSettings.recentDocsCount = 10;
	settings.recentFoldersCount = fDefaultSettings.recentFoldersCount = 10;
	settings.recentAppsCount = fDefaultSettings.recentAppsCount = 10;
	// window
	settings.alwaysOnTop = fDefaultSettings.alwaysOnTop = false;
	settings.autoRaise = fDefaultSettings.autoRaise = false;
	settings.autoHide = fDefaultSettings.autoHide = false;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Default (R5): Vertical, Square (0 px), Floating, top right.
	settings.style = fDefaultSettings.style = R5Deskbar::kStyleR5;
	settings.layout = fDefaultSettings.layout = R5Deskbar::kLayoutVertical;
	settings.rounded = fDefaultSettings.rounded = false;
	settings.floating = fDefaultSettings.floating = true;
	// Fixed normal (Vertical) width for Default (R5)
	settings.width = fDefaultSettings.width
		= std::max(gMinimumWindowWidth, R5Deskbar::Size(228));
// SPDX-SnippetEnd

	clock_settings clock;
	clock.showSeconds = false;
	clock.showDayOfWeek = false;
	clock.showTimeZone = false;

	BPath dirPath;
	const char* settingsFileName = "settings";
	const char* clockSettingsFileName = "clock_settings";

	find_directory(B_USER_DESKBAR_DIRECTORY, &dirPath, true);
		// just make it

	if (GetDeskbarSettingsDirectory(dirPath, true) == B_OK) {
		BPath filePath = dirPath;
		filePath.Append(settingsFileName);
		fSettingsFile = new BFile(filePath.Path(), O_RDWR);
		if (fSettingsFile->InitCheck() != B_OK) {
			BDirectory theDir(dirPath.Path());
			if (theDir.InitCheck() == B_OK)
				theDir.CreateFile(settingsFileName, fSettingsFile);
		}

		BMessage prefs;
		if (fSettingsFile->InitCheck() == B_OK
			&& prefs.Unflatten(fSettingsFile) == B_OK) {
			settings.vertical = prefs.GetBool("vertical",
				fDefaultSettings.vertical);
			settings.left = prefs.GetBool("left",
				fDefaultSettings.left);
			settings.top = prefs.GetBool("top",
				fDefaultSettings.top);
			settings.state = prefs.GetInt32("state",
				fDefaultSettings.state);
			settings.width = prefs.GetFloat("width",
				fDefaultSettings.width);
			settings.switcherLoc = prefs.GetPoint("switcherLoc",
				fDefaultSettings.switcherLoc);
			settings.showClock = prefs.GetBool("showClock",
				fDefaultSettings.showClock);
			// applications
			settings.trackerAlwaysFirst = prefs.GetBool("trackerAlwaysFirst",
				fDefaultSettings.trackerAlwaysFirst);
			settings.sortRunningApps = prefs.GetBool("sortRunningApps",
				fDefaultSettings.sortRunningApps);
			settings.superExpando = prefs.GetBool("superExpando",
				fDefaultSettings.superExpando);
			settings.expandNewTeams = prefs.GetBool("expandNewTeams",
				fDefaultSettings.expandNewTeams);
			settings.hideLabels = prefs.GetBool("hideLabels",
				fDefaultSettings.hideLabels);
			settings.iconSize = prefs.GetInt32("iconSize",
				fDefaultSettings.iconSize);
			// recent items
			settings.recentDocsEnabled = prefs.GetBool("recentDocsEnabled",
				fDefaultSettings.recentDocsEnabled);
			settings.recentFoldersEnabled
				= prefs.GetBool("recentFoldersEnabled",
					fDefaultSettings.recentFoldersEnabled);
			settings.recentAppsEnabled = prefs.GetBool("recentAppsEnabled",
				fDefaultSettings.recentAppsEnabled);
			settings.recentDocsCount = prefs.GetInt32("recentDocsCount",
				fDefaultSettings.recentDocsCount);
			settings.recentFoldersCount = prefs.GetInt32("recentFoldersCount",
				fDefaultSettings.recentFoldersCount);
			settings.recentAppsCount = prefs.GetInt32("recentAppsCount",
				fDefaultSettings.recentAppsCount);
			// window
			settings.alwaysOnTop = prefs.GetBool("alwaysOnTop",
				fDefaultSettings.alwaysOnTop);
			settings.autoRaise = prefs.GetBool("autoRaise",
				fDefaultSettings.autoRaise);
			settings.autoHide = prefs.GetBool("autoHide",
				fDefaultSettings.autoHide);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			settings.style = prefs.GetInt32("style",
				fDefaultSettings.style);
			if (settings.style != R5Deskbar::kStyleClassic)
				settings.style = R5Deskbar::kStyleR5;
			settings.layout = prefs.GetInt32("layout",
				settings.vertical ? R5Deskbar::kLayoutVertical
					: R5Deskbar::kLayoutHorizontal);
			if (settings.layout < R5Deskbar::kLayoutVertical
				|| settings.layout > R5Deskbar::kLayoutHorizontal) {
				settings.layout = R5Deskbar::kLayoutVertical;
			}
			// Classic (Haiku) keeps Haiku's width when the file has none
			// (review 1, 23 Nov 2026)
			if (!prefs.HasFloat("width")
				&& settings.style == R5Deskbar::kStyleClassic) {
				settings.width = gMinimumWindowWidth;
			}
			settings.rounded = prefs.GetBool("rounded",
				fDefaultSettings.rounded);
			settings.floating = prefs.GetBool("floating",
				fDefaultSettings.floating);
// SPDX-SnippetEnd
		}

		// constrain width setting within limits
		if (settings.width < gMinimumWindowWidth)
			settings.width = gMinimumWindowWidth;
		else if (settings.width > gMaximumWindowWidth)
			settings.width = gMaximumWindowWidth;

		filePath = dirPath;
		filePath.Append(clockSettingsFileName);
		fClockSettingsFile = new BFile(filePath.Path(), O_RDWR);
		if (fClockSettingsFile->InitCheck() != B_OK) {
			BDirectory theDir(dirPath.Path());
			if (theDir.InitCheck() == B_OK)
				theDir.CreateFile(clockSettingsFileName, fClockSettingsFile);
		}

		if (fClockSettingsFile->InitCheck() == B_OK
			&& prefs.Unflatten(fClockSettingsFile) == B_OK) {
			clock.showSeconds = prefs.GetBool("showSeconds", false);
			clock.showDayOfWeek = prefs.GetBool("showDayOfWeek", false);
			clock.showTimeZone = prefs.GetBool("showTimeZone", false);
		}
	}

	fSettings = settings;
	fClockSettings = clock;
}


void
TBarApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case R5Deskbar::kMsgSetStyle:
			_SetStyle(message);
			break;

		case BAppearanceKit::B_WINDOW_EFFECTS_CHANGED:
			// app_server: the effects changed, or the compositor turned off
			// (external review, 23 Nov 2026); ApplyStyle() asks again
			if (fBarWindow != NULL && fBarWindow->Lock()) {
				fBarView->ApplyStyle();
				fBarWindow->Unlock();
			}
			break;

		case B_SOME_APP_ACTIVATED:
		{
			// The front application (Default (R5): the blue line). Deskbar
			// itself never counts.
			team_id team = -1;
			const char* signature = NULL;
			if (message->FindInt32("be:team", &team) != B_OK)
				break;
			if (message->FindString("be:signature", &signature) == B_OK
				&& strcasecmp(signature, kDeskbarSignature) == 0) {
				break;
			}
			R5Deskbar::SetFrontTeam(team);
			if (R5Deskbar::Modern() && fBarWindow->Lock()) {
				if (fBarView->ExpandoMenuBar() != NULL)
					fBarView->ExpandoMenuBar()->Invalidate();
				fBarWindow->Unlock();
			}
			break;
		}

// SPDX-SnippetEnd
		// BDeskbar originating messages we can handle
		case kMsgIsAlwaysOnTop:
		{
			BMessage reply('rply');
			reply.AddBool("always on top", fSettings.alwaysOnTop);
			message->SendReply(&reply);
			break;
		}
		case kMsgIsAutoRaise:
		{
			BMessage reply('rply');
			reply.AddBool("auto raise", fSettings.autoRaise);
			message->SendReply(&reply);
			break;
		}
		case kMsgIsAutoHide:
		{
			BMessage reply('rply');
			reply.AddBool("auto hide", fSettings.autoHide);
			message->SendReply(&reply);
			break;
		}

		// pass rest of BDeskbar originating messages onto the window
		// (except for setters handled below)
		case kMsgLocation:
		case kMsgSetLocation:
		case kMsgIsExpanded:
		case kMsgExpand:
		case kMsgGetItemInfo:
		case kMsgHasItem:
		case kMsgCountItems:
		case kMsgMaxItemSize:
		case kMsgAddView:
		case kMsgRemoveItem:
		case kMsgAddAddOn:
			fBarWindow->PostMessage(message);
			break;

		case kConfigShow:
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// The Desktop and Deskbar page of Settings replaces this window
			// (issue #4615). ShowPreferencesWindow() stays for now.
			r5_settings_open_page("deskbar");
// SPDX-SnippetEnd
			break;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case kOpenSettings:
			r5_settings_open_page(NULL);
			break;
// SPDX-SnippetEnd

		case kConfigQuit:
			QuitPreferencesWindow();
			break;

		case kStateChanged:
			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(message);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
			break;

		case kSaveSettings:
			SaveSettings();
// SPDX-SnippetEnd
			break;

		case kShowDeskbarMenu:
			if (fBarWindow->Lock()) {
				fBarWindow->ShowDeskbarMenu();
				fBarWindow->Unlock();
			}
			break;

		case kShowTeamMenu:
			if (fBarWindow->Lock()) {
				fBarWindow->ShowTeamMenu();
				fBarWindow->Unlock();
			}
			break;

		case kUpdateRecentCounts:
		{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			int32 count = 0;
// SPDX-SnippetEnd
			bool enabled;

			if (message->FindInt32("applications", &count) == B_OK)
				fSettings.recentAppsCount = count;
			if (message->FindBool("applicationsEnabled", &enabled) == B_OK)
				fSettings.recentAppsEnabled = enabled && count > 0;

			if (message->FindInt32("folders", &count) == B_OK)
				fSettings.recentFoldersCount = count;
			if (message->FindBool("foldersEnabled", &enabled) == B_OK)
				fSettings.recentFoldersEnabled = enabled && count > 0;

			if (message->FindInt32("documents", &count) == B_OK)
				fSettings.recentDocsCount = count;
			if (message->FindBool("documentsEnabled", &enabled) == B_OK)
				fSettings.recentDocsEnabled = enabled && count > 0;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd
			break;
		}

		case B_SOME_APP_LAUNCHED:
		{
			team_id team = -1;
			message->FindInt32("be:team", &team);

			uint32 flags = 0;
			message->FindInt32("be:flags", (int32*)&flags);

			const char* signature = NULL;
			message->FindString("be:signature", &signature);

			entry_ref ref;
			message->FindRef("be:ref", &ref);

			AddTeam(team, flags, signature, &ref);
			break;
		}

		case B_SOME_APP_QUIT:
		{
			team_id team = -1;
			message->FindInt32("be:team", &team);
			RemoveTeam(team);
			break;
		}

		case B_ARCHIVED_OBJECT:
			// TODO: what's this???
			message->AddString("special", "Alex Osadzinski");
			fStatusViewMessenger.SendMessage(message);
			break;

		case kToggleDraggers:
			if (BDragger::AreDraggersDrawn())
				BDragger::HideAllDraggers();
			else
				BDragger::ShowAllDraggers();
			break;

		case kMsgAlwaysOnTop: // from BDeskbar
		case kAlwaysTop:
			fSettings.alwaysOnTop = !fSettings.alwaysOnTop;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);

			fBarWindow->SetFeel(fSettings.alwaysOnTop
				? B_FLOATING_ALL_WINDOW_FEEL
				: B_NORMAL_WINDOW_FEEL);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd
			break;

		case kMsgAutoRaise: // from BDeskbar
		case kAutoRaise:
			fSettings.autoRaise = fSettings.alwaysOnTop ? false
				: !fSettings.autoRaise;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd
			break;

		case kMsgAutoHide: // from BDeskbar
		case kAutoHide:
			fSettings.autoHide = !fSettings.autoHide;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);

			fBarWindow->Lock();
			fBarView->HideDeskbar(fSettings.autoHide);
			fBarWindow->Unlock();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd
			break;

		case kTrackerFirst:
			fSettings.trackerAlwaysFirst = !fSettings.trackerAlwaysFirst;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd

			// if mini mode we don't need to update the view
			if (fBarView->MiniState())
				break;

			fBarWindow->Lock();
			fBarView->PlaceApplicationBar();
			fBarWindow->Unlock();
			break;

		case kSortRunningApps:
			fSettings.sortRunningApps = !fSettings.sortRunningApps;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd

			// if mini mode we don't need to update the view
			if (fBarView->MiniState())
				break;

			fBarWindow->Lock();
			fBarView->PlaceApplicationBar();
			fBarWindow->Unlock();
			break;

		case kUnsubscribe:
		{
			BMessenger messenger;
			if (message->FindMessenger("messenger", &messenger) == B_OK)
				Unsubscribe(messenger);
			break;
		}

		case kSuperExpando:
			fSettings.superExpando = !fSettings.superExpando;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd

			// if mini mode we don't need to update the view
			if (fBarView->MiniState())
				break;

			fBarWindow->Lock();
			fBarView->PlaceApplicationBar();
			fBarWindow->Unlock();
			break;

		case kExpandNewTeams:
			fSettings.expandNewTeams = !fSettings.expandNewTeams;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd

			// if mini mode we don't need to update the view
			if (fBarView->MiniState())
				break;

			fBarWindow->Lock();
			fBarView->PlaceApplicationBar();
			fBarWindow->Unlock();
			break;

		case kHideLabels:
			fSettings.hideLabels = !fSettings.hideLabels;

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd

			// if mini mode we don't need to update the view
			if (fBarView->MiniState())
				break;

			fBarWindow->Lock();
			fBarView->PlaceApplicationBar();
			fBarWindow->Unlock();
			break;

		case kResizeTeamIcons:
		{
			int32 oldIconSize = fSettings.iconSize;
			int32 value;
			if (message->FindInt32("be:value", &value) != B_OK)
				break;

			int32 iconSize = value * kIconSizeInterval;
			fSettings.iconSize = iconSize;

			// pin icon size between min and max values
			if (fSettings.iconSize < kMinimumIconSize)
				fSettings.iconSize = kMinimumIconSize;
			else if (fSettings.iconSize > kMaximumIconSize)
				fSettings.iconSize = kMaximumIconSize;

			// don't resize if icon size hasn't changed
			if (fSettings.iconSize == oldIconSize)
				break;

			ResizeTeamIcons();

			if (fPreferencesWindow != NULL)
				fPreferencesWindow->PostMessage(kUpdatePreferences);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SaveSettings();
// SPDX-SnippetEnd

			// if mini mode we don't need to update the view
			if (fBarView->MiniState())
				break;

			fBarWindow->Lock();
			if (!fBarView->Vertical()) {
				// Must also resize the Deskbar menu and replicant tray in
				// horizontal mode
				fBarView->PlaceDeskbarMenu();
				fBarView->PlaceTray(false, false);
			}
			fBarView->PlaceApplicationBar();
			fBarWindow->Unlock();
			break;
		}

		case 'TASK':
			fSwitcherMessenger.SendMessage(message);
			break;

		case kSuspendSystem:
			// TODO: Call BRoster?
			break;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case 'r5sd':
		case 'r5rb':
		{
			if (!r5_admin_is_local(message))
				break;
			bool reboot = (message->what == 'r5rb');
			BMessenger login("application/x-vnd.Haiku-Login");
			if (login.IsValid()) {
				BMessage req(reboot ? 'rebo' : 'halt');
				req.AddBool("confirm", false);
				BMessage reply;
				r5_admin_send(login, &req, &reply, 5000000LL);
			}
			break;
		}
// SPDX-SnippetEnd
		case kRebootSystem:
		case kShutdownSystem:
		{
			bool reboot = (message->what == kRebootSystem);
			bool confirm = false;
			message->FindBool("confirm", &confirm);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// Fast user switching (5.7): ask Login whether other people are
			// signed in. If so, show the shared libr5ui warning; otherwise
			// route through Login passing confirm so the shown person's apps
			// quit normally with "Save changes?" visible.
			BMessenger login("application/x-vnd.Haiku-Login");
			if (login.IsValid()) {
				BMessage query('qses');
				BMessage sessionsReply;
				if (r5_admin_send(login, &query, &sessionsReply, 3000000LL)
						== B_OK) {
					int32 otherCount = 0;
					type_code tc;
					if (sessionsReply.GetInfo("login", &tc, &otherCount) == B_OK
						&& otherCount > 0) {
						r5_show_shutdown_warning(reboot, sessionsReply,
							BMessenger(this), reboot ? 'r5rb' : 'r5sd', false);
						break;
					}
					BMessage req(reboot ? 'rebo' : 'halt');
					req.AddBool("confirm", confirm);
					BMessage reply;
					if (r5_admin_send(login, &req, &reply, 5000000LL) == B_OK
						&& reply.GetInt32("error", B_ERROR) == B_OK) {
						break;
					}
					BAlert* alert = new BAlert(
						reboot ? B_TRANSLATE("Restart")
							: B_TRANSLATE("Shut down"),
						reply.GetString("text",
							B_TRANSLATE("Could not start shut down.")),
						B_TRANSLATE("OK"), NULL, NULL,
						B_WIDTH_AS_USUAL, B_WARNING_ALERT);
					alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
					alert->Go();
					break;
				}
				// Querying Login failed or timed out: do NOT bypass Login and
				// shut down without checking other signed-in sessions.
				BAlert* alert = new BAlert(
					reboot ? B_TRANSLATE("Restart")
						: B_TRANSLATE("Shut down"),
					B_TRANSLATE("Could not check whether other people are "
						"signed in. Try again in a moment."),
					B_TRANSLATE("OK"), NULL, NULL,
					B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
				break;
			}
// SPDX-SnippetEnd

			BRoster roster;
			BRoster::Private rosterPrivate(roster);
			status_t error = rosterPrivate.ShutDown(reboot, confirm, false);
			if (error != B_OK)
				fprintf(stderr, "Shutdown failed: %s\n", strerror(error));

			break;
		}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case kLockSession:
		{
			if (r5_admin_is_system_guest(getuid()))
				break;
			BMessenger login("application/x-vnd.Haiku-Login");
			if (login.IsValid()) {
				BMessage req(kLockSession);
				BMessage reply;
				r5_admin_send(login, &req, &reply);
			}
			break;
		}

		case kSwitchUserSession:
		{
			if (r5_admin_is_system_guest(getuid()))
				break;
			BMessenger login("application/x-vnd.Haiku-Login");
			if (login.IsValid()) {
				BMessage req(kSwitchUserSession);
				BMessage reply;
				r5_admin_send(login, &req, &reply);
			}
			break;
		}

		case kOpenSharedFolder:
		{
			entry_ref ref;
			if (get_ref_for_path("/boot/home/Shared", &ref) == B_OK) {
				BMessage open(B_REFS_RECEIVED);
				open.AddRef("refs", &ref);
				BMessenger(kTrackerSignature).SendMessage(&open);
			}
			break;
		}

		case kLogoutSession:
		{
			bool isGuest = r5_admin_is_system_guest(getuid());
			BAlert* alert;
			if (isGuest) {
				alert = new BAlert("Log out Guest",
					"Log out Guest and delete all files created in this "
					"session?\n\nFiles saved in the Shared folder or on a "
					"USB stick are kept.",
					"Cancel", "Log out and delete files", NULL,
					B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			} else {
				alert = new BAlert("Log out",
					"Log out of this session?",
					"Cancel", "Log out");
			}
			alert->SetShortcut(0, B_ESCAPE);
			alert->Go(new BInvoker(new BMessage(kConfirmLogout), this));
			break;
		}

		case kConfirmLogout:
		{
			int32 which = -1;
			if (message->FindInt32("which", &which) != B_OK || which != 1)
				break;

			BMessenger login("application/x-vnd.Haiku-Login");
			if (login.IsValid()) {
				BMessage req(kLogoutSession);
				BMessage reply;
				r5_admin_send(login, &req, &reply);
			} else {
				BLaunchRoster roster;
				roster.StopTarget("desktop");
				roster.Target("login");
			}
			break;
		}

// SPDX-SnippetEnd
		case kShowSplash:
			run_be_about();
			break;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case kShowDesktop:
		{
			BList teams;
			be_roster->GetAppList(&teams);
			BRect zoom(0, 0, 0, 0);
			for (int32 i = 0; i < teams.CountItems(); i++) {
				team_id team = (addr_t)teams.ItemAt(i);
				app_info info;
				if (be_roster->GetRunningAppInfo(team, &info) != B_OK)
					continue;
				if (strcasecmp(info.signature, kTrackerSignature) == 0
					|| strcasecmp(info.signature, kDeskbarSignature) == 0)
					continue;
				do_minimize_team(zoom, team, false);
			}
			break;
		}

		case kCheckHungApp:
		{
			team_id team;
			if (message->FindInt32("team", &team) != B_OK)
				break;
			team_info info;
			if (get_team_info(team, &info) != B_OK)
				break;
			const char* name;
			if (message->FindString("name", &name) != B_OK)
				name = info.args;
			BString text(B_TRANSLATE("%name is not responding."));
			text.ReplaceFirst("%name", name);
			BAlert* alert = new BAlert("unresponsive", text.String(),
				B_TRANSLATE("Wait"), B_TRANSLATE("Force quit"), NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			BMessage* confirm = new BMessage(kConfirmForceQuit);
			confirm->AddInt32("team", team);
			alert->Go(new BInvoker(confirm, this));
			break;
		}

		case kConfirmForceQuit:
		{
			int32 which = -1;
			team_id team = -1;
			if (message->FindInt32("which", &which) == B_OK && which == 1
				&& message->FindInt32("team", &team) == B_OK) {
				kill_team(team);
			}
			break;
		}

// SPDX-SnippetEnd
		case B_LOCALE_CHANGED:
		{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// The file may still be the previous locale. Apply the message
			// after Refresh() so that copy does not win.
// SPDX-SnippetEnd
			BLocaleRoster::Default()->Refresh();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			BLocaleRoster::Default()->ApplySettings(message);
			BPrivate::ForceUnloadCatalog();
			gLocalizedNamePreferred
				= BLocaleRoster::Default()->IsFilesystemTranslationPreferred();

			if (message->HasString("language") && fBarWindow != NULL
				&& fBarWindow->Lock()) {
				TDeskbarMenu* menu = fBarWindow->DeskbarMenu();
				if (menu != NULL && menu->LockLooper()) {
					menu->ForceRebuild();
					menu->UnlockLooper();
				}
				fBarWindow->Unlock();
			}
// SPDX-SnippetEnd
		}
		// fall-through

		case kRealignReplicants:
		case kShowHideTime:
		case kShowSeconds:
		case kShowDayOfWeek:
		case kShowTimeZone:
		case kGetClockSettings:
			fStatusViewMessenger.SendMessage(message);
				// Notify the replicant tray (through BarView) that the time
				// interval has changed and it should update the time view
				// and reflow the tray icons.
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			break;

		case B_COLORS_UPDATED:
			if (fBarWindow != NULL && fBarWindow->Lock()) {
				// Default (R5) keeps its own surface (TBarView restyles it)
				if (fBarView != NULL && !R5Deskbar::Modern()) {
					fBarView->SetViewUIColor(B_MENU_BACKGROUND_COLOR);
					fBarView->Invalidate();
				}
				fBarWindow->Unlock();
			}
// SPDX-SnippetEnd
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
TBarApp::RefsReceived(BMessage* refs)
{
	entry_ref ref;
	for (int32 i = 0; refs->FindRef("refs", i, &ref) == B_OK; i++) {
		BMessage refsReceived(B_REFS_RECEIVED);
		refsReceived.AddRef("refs", &ref);

		BEntry entry(&ref);
		if (!entry.IsDirectory())
			TrackerLaunch(&refsReceived, false);
	}
}


void
TBarApp::Subscribe(const BMessenger &subscriber, BList* list)
{
	// called when ExpandoMenuBar, TeamMenu or Switcher are built/rebuilt
	list->MakeEmpty();

	BAutolock autolock(sSubscriberLock);
	if (!autolock.IsLocked())
		return;

	int32 numTeams = sBarTeamInfoList.CountItems();
	for (int32 i = 0; i < numTeams; i++) {
		BarTeamInfo* barInfo = (BarTeamInfo*)sBarTeamInfoList.ItemAt(i);
		BarTeamInfo* newBarInfo = new(std::nothrow) BarTeamInfo(*barInfo);
		if (newBarInfo != NULL)
			list->AddItem(newBarInfo);
	}

	int32 subsCount = sSubscribers.CountItems();
	for (int32 i = 0; i < subsCount; i++) {
		BMessenger* messenger = (BMessenger*)sSubscribers.ItemAt(i);
		if (*messenger == subscriber)
			return;
	}

	sSubscribers.AddItem(new BMessenger(subscriber));
}


void
TBarApp::Unsubscribe(const BMessenger &subscriber)
{
	BAutolock autolock(sSubscriberLock);
	if (!autolock.IsLocked())
		return;

	int32 count = sSubscribers.CountItems();
	for (int32 i = 0; i < count; i++) {
		BMessenger* messenger = (BMessenger*)sSubscribers.ItemAt(i);
		if (*messenger == subscriber) {
			sSubscribers.RemoveItem(i);
			delete messenger;
			break;
		}
	}
}


BBitmap*
TBarApp::FetchTeamIcon(team_id team, int32 size)
{
	BAutolock autolock(sSubscriberLock);
	if (!autolock.IsLocked())
		return NULL;

	int32 teamCount = sBarTeamInfoList.CountItems();
	for (int32 i = 0; i < teamCount; i++) {
		BarTeamInfo* barInfo = (BarTeamInfo*)sBarTeamInfoList.ItemAt(i);
		if (barInfo->teams->HasItem((void*)(addr_t)team)) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			BBitmap* previous = barInfo->icon;
// SPDX-SnippetEnd
			_CacheTeamIcon(barInfo, size);
			BBitmap* icon = barInfo->icon;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// Default (R5): its icon of the layout comes back whatever the
			// size asked for (review 2)
			if (R5Deskbar::Modern() && previous != NULL) {
				barInfo->icon = previous;
				return icon;
			}
// SPDX-SnippetEnd

			// restore icon pointer back to setting
			if (size != fSettings.iconSize) {
				int32 index = (fSettings.iconSize - kMinimumIconSize)
					/ kIconSizeInterval;
				barInfo->icon = barInfo->iconCache[index];
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
				// Default (R5)'s icon of the layout is not in that cache
				// (QEMU, 22 Nov 2026: NULL here, and Deskbar crashed; review
				// 1: a cached Haiku-size icon here lost the R5 one)
				if (R5Deskbar::Modern() || barInfo->icon == NULL)
					barInfo->icon = previous;
// SPDX-SnippetEnd
			}

			return icon;
		}
	}

	return NULL;
}


BBitmap*
TBarApp::FetchWindowIcon(bool local, bool minimized)
{
	int32 id = R_WindowShownIcon;
	int32 index = 0;

	if (local) {
		if (!minimized) {
			id = R_WindowShownIcon;
			index = 0;
		}
		else {
			id = R_WindowHiddenIcon;
			index = 1;
		}
	} else {
		if (!minimized) {
			id = R_WindowShownSwitchIcon;
			index = 2;
		}
		else {
			id = R_WindowHiddenSwitchIcon;
			index = 3;
		}
	}

	// create a new cache entry if not found
	WindowIconCache* winCache
		= (WindowIconCache*)sWindowIconCache.ItemAt(index);
	if (winCache == NULL)
		winCache = new WindowIconCache(id);

	_CacheWindowIcon(winCache);

	return winCache->icon;
}


void
TBarApp::AddTeam(team_id team, uint32 flags, const char* sig, entry_ref* ref)
{
	if ((flags & B_BACKGROUND_APP) != 0
		|| strcasecmp(sig, kDeskbarSignature) == 0) {
		// don't add if a background app or Deskbar itself
		return;
	}

	BAutolock autolock(sSubscriberLock);
	if (!autolock.IsLocked())
		return;

	// have we already seen this team, is this another instance of
	// a known app?
	BarTeamInfo* multiLaunchTeam = NULL;
	int32 teamCount = sBarTeamInfoList.CountItems();
	for (int32 i = 0; i < teamCount; i++) {
		BarTeamInfo* barInfo = (BarTeamInfo*)sBarTeamInfoList.ItemAt(i);
		if (barInfo->teams->HasItem((void*)(addr_t)team))
			return;
		if (strcasecmp(barInfo->sig, sig) == 0)
			multiLaunchTeam = barInfo;
	}

	if (multiLaunchTeam != NULL) {
		multiLaunchTeam->teams->AddItem((void*)(addr_t)team);

		int32 subsCount = sSubscribers.CountItems();
		if (subsCount > 0) {
			BMessage message(kAddTeam);
			message.AddInt32("team", team);
			message.AddString("sig", multiLaunchTeam->sig);

			for (int32 i = 0; i < subsCount; i++)
				((BMessenger*)sSubscribers.ItemAt(i))->SendMessage(&message);
		}
		return;
	}

	BString name;
	if (!gLocalizedNamePreferred
		|| BLocaleRoster::Default()->GetLocalizedFileName(name, *ref)
			!= B_OK) {
		name = ref->name;
	}

	BarTeamInfo* barInfo = new BarTeamInfo(new BList(), flags, strdup(sig),
		strdup(name.String()));
	_CacheTeamIcon(barInfo);
	barInfo->teams->AddItem((void*)(addr_t)team);
	sBarTeamInfoList.AddItem(barInfo);

	int32 subsCount = sSubscribers.CountItems();
	if (subsCount > 0) {
		for (int32 i = 0; i < subsCount; i++) {
			BMessenger* messenger = (BMessenger*)sSubscribers.ItemAt(i);
			BMessage message(B_SOME_APP_LAUNCHED);

			BList* tList = new BList(*(barInfo->teams));
			message.AddPointer("teams", tList);

			BBitmap* icon = new BBitmap(barInfo->icon);
			ASSERT(icon);

			message.AddPointer("icon", icon);

			message.AddInt32("flags", static_cast<int32>(barInfo->flags));
			message.AddString("name", barInfo->name);
			message.AddString("sig", barInfo->sig);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			if (messenger->SendMessage(&message) != B_OK) {
				delete tList;
				delete icon;
			}
// SPDX-SnippetEnd
		}
	}
}


void
TBarApp::RemoveTeam(team_id team)
{
	BAutolock autolock(sSubscriberLock);
	if (!autolock.IsLocked())
		return;

	int32 teamCount = sBarTeamInfoList.CountItems();
	for (int32 i = 0; i < teamCount; i++) {
		BarTeamInfo* barInfo = (BarTeamInfo*)sBarTeamInfoList.ItemAt(i);
		if (barInfo->teams->HasItem((void*)(addr_t)team)) {
			int32 subsCount = sSubscribers.CountItems();
			if (subsCount > 0) {
				BMessage message((barInfo->teams->CountItems() == 1)
					? B_SOME_APP_QUIT : kRemoveTeam);

				message.AddInt32("team", team);
				for (int32 i = 0; i < subsCount; i++) {
					BMessenger* messenger = (BMessenger*)sSubscribers.ItemAt(i);
					messenger->SendMessage(&message);
				}
			}

			barInfo->teams->RemoveItem((void*)(addr_t)team);
			if (barInfo->teams->CountItems() < 1) {
				delete (BarTeamInfo*)sBarTeamInfoList.RemoveItem(i);
				return;
			}
		}
	}
}


void
TBarApp::ResizeTeamIcons()
{
	BAutolock autolock(sSubscriberLock);
	if (!autolock.IsLocked())
		return;

	for (int32 i = sBarTeamInfoList.CountItems() - 1; i >= 0; i--) {
		BarTeamInfo* barInfo = (BarTeamInfo*)sBarTeamInfoList.ItemAt(i);
		if ((barInfo->flags & B_BACKGROUND_APP) == 0
			&& strcasecmp(barInfo->sig, kDeskbarSignature) != 0) {
			_CacheTeamIcon(barInfo);
		}
	}
}


int32
TBarApp::TeamIconSize()
{
	static int32 iconSize = 0, composed = 0;
	int32 saved = fSettings.iconSize;
	if (iconSize != saved) {
		composed = be_control_look->ComposeIconSize(saved).IntegerWidth() + 1;
		iconSize = saved;
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Default (R5): the icon size of the layout (board A 20, C 22, B 30),
	// unless the user chose larger icons
	if (R5Deskbar::Modern()) {
		float layoutSize = R5Deskbar::Size(R5Deskbar::Compact() ? 30
			: fSettings.vertical ? 20 : 22);
		return std::max(composed, (int32)layoutSize);
	}
// SPDX-SnippetEnd
	return composed;
}


void
TBarApp::ShowPreferencesWindow()
{
	if (fPreferencesWindow == NULL) {
		fPreferencesWindow = new PreferencesWindow(BRect(100, 100, 320, 240));
		fPreferencesWindow->Show();
	} else if (fPreferencesWindow->Lock()) {
		if (fPreferencesWindow->IsHidden())
			fPreferencesWindow->Show();
		else
			fPreferencesWindow->Activate();

		fPreferencesWindow->Unlock();
	}
}


void
TBarApp::QuitPreferencesWindow()
{
	if (fPreferencesWindow == NULL)
		return;

	if (fPreferencesWindow->Lock()) {
		fPreferencesWindow->Quit();
			// Quit() destroys the window so don't unlock
		fPreferencesWindow = NULL;
	}
}


status_t
TBarApp::_CacheTeamIcon(BarTeamInfo* barInfo)
{
	if (barInfo == NULL)
		return B_BAD_VALUE;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Default (R5): the pixel size of the layout (20, 22 or 30 at 12
	// points), drawn from the vector icon, not a smaller one scaled up
	if (R5Deskbar::Modern()) {
		int32 pixels = TeamIconSize();
		int32 composed = be_control_look->ComposeIconSize(
			fSettings.iconSize).IntegerWidth() + 1;
		if (pixels != composed)
			return _CacheModernIcon(barInfo, pixels);
	}
	// back from Default (R5): its own icon is not in the cache
	bool cached = barInfo->icon == NULL;
	for (int32 i = 0; i < kIconCacheCount; i++) {
		if (barInfo->iconCache[i] == barInfo->icon)
			cached = true;
	}
	if (!cached) {
		delete barInfo->icon;
		barInfo->icon = NULL;
	}
// SPDX-SnippetEnd
	return _CacheTeamIcon(barInfo, fSettings.iconSize);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
status_t
TBarApp::_CacheModernIcon(BarTeamInfo* barInfo, int32 pixels)
{
	if (barInfo->icon != NULL
		&& barInfo->icon->Bounds().IntegerWidth() + 1 == pixels) {
		return B_OK;
	}
	BBitmap* icon = new(std::nothrow) BBitmap(BRect(0, 0, pixels - 1,
		pixels - 1), B_RGBA32);
	if (icon == NULL || icon->InitCheck() != B_OK) {
		delete icon;
		return B_NO_MEMORY;
	}
	status_t status = B_ERROR;
	app_info appInfo;
	if (be_roster->GetAppInfo(barInfo->sig, &appInfo) == B_OK) {
		BFile file(&appInfo.ref, B_READ_ONLY);
		BAppFileInfo appMime(&file);
		status = appMime.GetIcon(icon, (icon_size)pixels);
	}
	if (status != B_OK) {
		BMimeType defaultAppMime(B_APP_MIME_TYPE);
		status = defaultAppMime.GetIcon(icon, (icon_size)pixels);
	}
	if (status != B_OK) {
		// the cached icon of Haiku's size, as before; an uncached one of an
		// earlier layout is ours to free (review 2)
		delete icon;
		bool inCache = barInfo->icon == NULL;
		for (int32 i = 0; i < kIconCacheCount; i++) {
			if (barInfo->iconCache[i] == barInfo->icon)
				inCache = true;
		}
		if (!inCache) {
			delete barInfo->icon;
			barInfo->icon = NULL;
		}
		return _CacheTeamIcon(barInfo, fSettings.iconSize);
	}
	// an icon of an earlier layout that is not in the cache is ours
	bool cached = false;
	for (int32 i = 0; i < kIconCacheCount; i++) {
		if (barInfo->iconCache[i] == barInfo->icon)
			cached = true;
	}
	if (!cached)
		delete barInfo->icon;
	barInfo->icon = icon;
	return B_OK;
}
// SPDX-SnippetEnd


status_t
TBarApp::_CacheTeamIcon(BarTeamInfo* barInfo, int32 size)
{
	if (barInfo == NULL || size < kMinimumIconSize)
		return B_BAD_VALUE;

	// icon index based on icon size
	const int32 index = (size - kMinimumIconSize) / kIconSizeInterval;

	// first look in the icon cache
	barInfo->icon = barInfo->iconCache[index];
	if (barInfo->icon != NULL)
		return B_OK;

	int32 composed = be_control_look->ComposeIconSize(size).IntegerWidth() + 1;
	BRect iconRect = BRect(0, 0, composed - 1, composed - 1);
	BBitmap* icon = new BBitmap(iconRect, B_RGBA32);

	// icon wasn't in cache, get it from be_roster and cache it
	app_info appInfo;
	if (be_roster->GetAppInfo(barInfo->sig, &appInfo) == B_OK) {
		// fetch the app icon
		BFile file(&appInfo.ref, B_READ_ONLY);
		BAppFileInfo appMime(&file);
		if (appMime.GetIcon(icon, (icon_size)size) == B_OK) {
			barInfo->iconCache[index] = barInfo->icon = icon;

			return B_OK;
		}
	}

	// couldn't find the app icon
	// fetch the generic 3 boxes icon and cache it
	BMimeType defaultAppMime;
	defaultAppMime.SetTo(B_APP_MIME_TYPE);
	if (defaultAppMime.GetIcon(icon, (icon_size)size) == B_OK) {
		barInfo->iconCache[index] = barInfo->icon = icon;

		return B_OK;
	}

	// couldn't find generic 3 boxes icon
	// fill with transparent
	uint8* iconBits = (uint8*)icon->Bits();
	if (icon->ColorSpace() == B_RGBA32) {
		size_t i = 0;
		while (i < icon->BitsLength()) {
			iconBits[i++] = B_TRANSPARENT_32_BIT.red;
			iconBits[i++] = B_TRANSPARENT_32_BIT.green;
			iconBits[i++] = B_TRANSPARENT_32_BIT.blue;
			iconBits[i++] = B_TRANSPARENT_32_BIT.alpha;
		}
	} else {
		// Assume B_CMAP8
		for (size_t i = 0; i < icon->BitsLength(); i++)
			iconBits[i] = B_TRANSPARENT_MAGIC_CMAP8;
	}

	barInfo->iconCache[index] = barInfo->icon = icon;

	return B_OK;
}


status_t
TBarApp::_CacheWindowIcon(WindowIconCache* winCache)
{
	if (winCache == NULL)
		return B_BAD_VALUE;

	// clip font size
	int32 fontSize = (int32)floorf(be_plain_font->Size());
	if (fontSize < kMinimumFontSize)
		fontSize = kMinimumFontSize;
	if (fontSize > kMaximumFontSize)
		fontSize = kMaximumFontSize;

	// icon index based on font size
	const int32 index = (fontSize - kMinimumFontSize) / kFontSizeInterval;

	// first look in the icon cache
	winCache->icon = winCache->iconCache[index];
	if (winCache->icon != NULL)
		return B_OK;

	int32 id = winCache->id;
	uint8* data;
	size_t size;

	// icon wasn't in cache, get vector icon from resource and cache it
	data = (uint8*)AppResSet()->FindResource(B_VECTOR_ICON_TYPE, id, &size);
	if (data != NULL && size > 0) {
		// seems valid, scale bitmap according to font size
		BBitmap* icon = new(std::nothrow) BBitmap(
			BRect(B_ORIGIN, be_control_look->ComposeIconSize(B_MINI_ICON)),
			B_RGBA32);
		if (icon != NULL && icon->InitCheck() == B_OK
			&& BIconUtils::GetVectorIcon(const_cast<const uint8*>(data),
				size, icon) == B_OK) {
			winCache->iconCache[index] = winCache->icon = icon;

			return B_OK;
		} else if (icon != NULL) {
			// ran out of memory allocating bitmap, this should never happen
			delete icon;
			delete winCache->iconCache[index];
			winCache->iconCache[index] = winCache->icon = NULL;

			return B_NO_MEMORY;
		}
	}

	return B_ERROR;
}


//	#pragma mark - BarTeamInfo


BarTeamInfo::BarTeamInfo(BList* teams, uint32 flags, char* sig, char* name,
	BBitmap* icon)
	:
	teams(teams),
	flags(flags),
	sig(sig),
	name(name),
	icon(icon)
{
	_Init();
}


BarTeamInfo::BarTeamInfo(const BarTeamInfo &info)
	:
	teams(new BList(*info.teams)),
	flags(info.flags),
	sig(strdup(info.sig)),
	name(strdup(info.name)),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	icon(info.icon != NULL ? new BBitmap(*info.icon) : NULL)
// SPDX-SnippetEnd
{
	_Init();
}


BarTeamInfo::~BarTeamInfo()
{
	delete teams;
	free(sig);
	free(name);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	bool iconInCache = false;
	for (int32 i = 0; i < kIconCacheCount; i++) {
		if (iconCache[i] == icon)
			iconInCache = true;
		delete iconCache[i];
	}
	if (!iconInCache)
		delete icon;
// SPDX-SnippetEnd
}


void
BarTeamInfo::_Init()
{
	for (int32 i = 0; i < kIconCacheCount; i++)
		iconCache[i] = NULL;
}


//	#pragma mark - WindowIconCache


WindowIconCache::WindowIconCache(int32 id, BBitmap* icon)
	:
	id(id),
	icon(icon)
{
	_Init();
}


WindowIconCache::WindowIconCache(const WindowIconCache &cache)
	:
	id(cache.id),
	icon(new BBitmap(*cache.icon))
{
	_Init();
}


WindowIconCache::~WindowIconCache()
{
	for (int32 i = 0; i < kWindowIconCacheCount; i++)
		delete iconCache[i];
}


void
WindowIconCache::_Init()
{
	for (int32 i = 0; i < kWindowIconCacheCount; i++)
		iconCache[i] = NULL;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
TBarApp::_SetStyle(BMessage* message)
{
	// From the Settings page: absolute values, each field optional
	// (DeskbarStyle.h, kMsgSetStyle)
	int32 value;
	bool flag;
	if (message->FindInt32("style", &value) == B_OK) {
		fSettings.style = value == R5Deskbar::kStyleClassic
			? R5Deskbar::kStyleClassic : R5Deskbar::kStyleR5;
	}
	if (message->FindInt32("layout", &value) == B_OK
		&& value >= R5Deskbar::kLayoutVertical
		&& value <= R5Deskbar::kLayoutHorizontal) {
		fSettings.layout = value;
	}
	if (message->FindBool("rounded", &flag) == B_OK)
		fSettings.rounded = flag;
	if (message->FindBool("floating", &flag) == B_OK)
		fSettings.floating = flag;

	// Classic (Haiku) has no Compact layout: its bar instead
	int32 layout = fSettings.layout;
	if (fSettings.style == R5Deskbar::kStyleClassic
		&& layout == R5Deskbar::kLayoutCompact) {
		layout = R5Deskbar::kLayoutHorizontal;
	}
	bool vertical = layout == R5Deskbar::kLayoutVertical;
	int32 state = kExpandoState;
	if (vertical) {
		if (fSettings.vertical)
			state = fSettings.state;
		else if (!fSettings.top)
			state = kMiniState;
	}

	if (fBarWindow->Lock()) {
		fBarView->ChangeState(vertical, fSettings.left, fSettings.top, state);
		fBarWindow->Unlock();
	}
	// the icons have the size of the layout, known now that the state
	// changed (review 2: before it they took the old orientation's size)
	ResizeTeamIcons();
	if (fBarWindow->Lock()) {
		fBarView->ApplyStyle();
		fBarWindow->Unlock();
	}
	SaveSettings();
}
// SPDX-SnippetEnd
