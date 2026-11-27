/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "AomiSettings.h"

#include "AomiDownloads.h"
#include "AomiMessages.h"
#include "AomiWindow.h"

#include <AppFileInfo.h>
#include <Application.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GridLayoutBuilder.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <Locker.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextControl.h>

#include <PageHeader.h>
#include <R5Icons.h>
#include <Sidebar.h>
#include <SidebarItem.h>
#include <WindowEffects.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Aomi settings"


namespace {


const uint32 kMsgPage = 'sPag';
const uint32 kMsgChanged = 'sChg';
const uint32 kMsgEngine = 'sEng';
const uint32 kMsgRevert = 'sRev';
const uint32 kMsgDefaults = 'sDef';
const uint32 kMsgUseCurrent = 'sCur';
const uint32 kMsgChoose = 'sCho';
const uint32 kMsgChosen = 'sCsn';
const uint32 kMsgClearHistory = 'sClH';
const uint32 kMsgClearSiteData = 'sClS';
const uint32 kMsgShowDownloads = 'sDls';

const int32 kZooms[] = { 50, 67, 80, 90, 100, 110, 125, 150, 175, 200 };
const int32 kZoomCount = 10;

BLocker sWindowLock("aomi settings window");
AomiSettingsWindow* sWindow = NULL;


struct PageInfo {
	const char*	id;
	const char*	label;
	const char*	subtitle;
};


// The labels are translated where they are shown (B_TRANSLATE_MARK).
const PageInfo kPages[] = {
	{ "general", B_TRANSLATE_MARK("General"),
		B_TRANSLATE_MARK("What Aomi opens with, and what the toolbar shows") },
	{ "search", B_TRANSLATE_MARK("Search"),
		B_TRANSLATE_MARK("Where the address field looks up what you type") },
	{ "appearance", B_TRANSLATE_MARK("Appearance"),
		B_TRANSLATE_MARK("Light or dark pages, and how large they are") },
	{ "downloads", B_TRANSLATE_MARK("Downloads"),
		B_TRANSLATE_MARK("Where the files you save from web pages go") },
	{ "privacy", B_TRANSLATE_MARK("Privacy"),
		B_TRANSLATE_MARK("History, private windows and what sites store") },
};
const int32 kPageCount = 5;


float
unit()
{
	return be_plain_font->Size() / 13.0f;
}


// The program's own vector icon, at `side`.
BBitmap*
app_icon(float side)
{
	app_info info;
	if (be_app == NULL || be_app->GetAppInfo(&info) != B_OK)
		return NULL;
	BFile file(&info.ref, B_READ_ONLY);
	BAppFileInfo appInfo(&file);
	uint8* data = NULL;
	size_t size = 0;
	if (appInfo.GetIcon(&data, &size) != B_OK)
		return NULL;
	BBitmap* icon = r5_vector_icon_bitmap(data, size, side);
	delete[] data;
	return icon;
}


BBitmap*
page_icon(const char* id, float side)
{
	BBitmap* icon = NULL;
	if (strcmp(id, "search") == 0)
		icon = r5_mime_icon_bitmap("application/x-vnd.Be-query", side);
	else if (strcmp(id, "appearance") == 0)
		icon = r5_mime_icon_bitmap("application/x-vnd.Haiku-Appearance", side);
	else if (strcmp(id, "downloads") == 0)
		icon = r5_mime_icon_bitmap("application/x-vnd.Be-directory", side);
	else if (strcmp(id, "privacy") == 0)
		icon = r5_icon_bitmap(R5_ICON_LOCKED, side);
	if (icon == NULL)
		icon = app_icon(side);
	return icon;
}


// A label of a form row: right-aligned in the label column, on the row's
// vertical centre (docs/STYLE.md).
BLayoutItem*
right(BLayoutItem* item)
{
	item->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_VERTICAL_CENTER));
	return item;
}


BStringView*
note(const char* text)
{
	BStringView* view = new BStringView("note", text);
	view->SetHighColor(mix_color(ui_color(B_PANEL_TEXT_COLOR),
		ui_color(B_PANEL_BACKGROUND_COLOR), 160));
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	return view;
}


// A box with a bold title in ordinary text, as on R5's Settings pages.
BBox*
box(const char* title, BView* content)
{
	BBox* frame = new BBox(title);
	BStringView* heading = new BStringView("title", title);
	heading->SetFont(be_bold_font);
	frame->SetLabel(heading);
	frame->AddChild(content);
	return frame;
}


BView*
page(BView* first, BView* second = NULL, BView* third = NULL)
{
	BGroupView* view = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(view)
		.Add(first);
	if (second != NULL)
		view->GroupLayout()->AddView(second);
	if (third != NULL)
		view->GroupLayout()->AddView(third);
	view->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
	return view;
}


BPopUpMenu*
radio_menu(const char* name)
{
	BPopUpMenu* menu = new BPopUpMenu(name);
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	return menu;
}


void
mark(BMenuField* field, int32 index)
{
	BMenuItem* item = field->Menu()->ItemAt(index);
	if (item != NULL)
		item->SetMarked(true);
}


int32
marked(BMenuField* field)
{
	BMenuItem* item = field->Menu()->FindMarked();
	return item != NULL ? field->Menu()->IndexOf(item) : -1;
}


BString
site_data_flag()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return BString();
	path.Append("WebPositive/AomiClearSiteData");
	return BString(path.Path());
}


void
remove_all(const char* path)
{
	DIR* dir = opendir(path);
	if (dir == NULL) {
		unlink(path);
		return;
	}
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		BString child(path);
		child << "/" << entry->d_name;
		struct stat st;
		if (lstat(child.String(), &st) == 0 && S_ISDIR(st.st_mode))
			remove_all(child.String());
		else
			unlink(child.String());
	}
	closedir(dir);
	rmdir(path);
}


}	// namespace


bool
aomi_site_data_clear_pending()
{
	BString flag = site_data_flag();
	struct stat st;
	return flag.Length() > 0 && stat(flag.String(), &st) == 0;
}


void
aomi_site_data_clear_request()
{
	BString flag = site_data_flag();
	if (flag.Length() == 0)
		return;
	BFile file(flag.String(), B_CREATE_FILE | B_WRITE_ONLY);
}


void
aomi_site_data_clear_if_pending(const char* profileDir)
{
	if (!aomi_site_data_clear_pending() || profileDir == NULL
		|| strstr(profileDir, "/Aomi/Servo") == NULL)
		return;
	remove_all(profileDir);
	unlink(site_data_flag().String());
}


AomiSettingsWindow::AomiSettingsWindow()
	:
	BWindow(BRect(0, 0, 799, 519), B_TRANSLATE("Aomi settings"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fFilling(false),
	fFolderPanel(NULL)
{
	// No glass: a form is many controls, and each one paints the panel
	// colour, which showed as a box behind every label over the blur. R5's
	// Settings window is opaque too.
	fOpened = fPrefs = aomi_prefs();
	float u = unit();

	fList = new BAppearanceKit::BSidebar("pages");
	float side = floorf(u * 24);
	for (int32 i = 0; i < kPageCount; i++) {
		BBitmap* icon = page_icon(kPages[i].id, side);
		fList->AddItem(new BAppearanceKit::BSidebarItem(
			B_TRANSLATE_NOCOLLECT(kPages[i].label), kPages[i].id, icon));
		delete icon;
	}
	fList->SetSelectionMessage(new BMessage(kMsgPage));
	BScrollView* scroll = new BScrollView("pages-scroll", fList, 0, false,
		false, B_PLAIN_BORDER);
	scroll->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	scroll->SetExplicitMinSize(BSize(200, 160));
	scroll->SetExplicitMaxSize(BSize(200, B_SIZE_UNLIMITED));

	fHeader = new BAppearanceKit::BPageHeader("heading");
	fHeader->SetEyebrow("Aomi");

	BView* deck = new BView("deck", 0);
	fCards = new BCardLayout();
	deck->SetLayout(fCards);
	fCards->AddView(_GeneralPage());
	fCards->AddView(_SearchPage());
	fCards->AddView(_AppearancePage());
	fCards->AddView(_DownloadsPage());
	fCards->AddView(_PrivacyPage());
	deck->SetExplicitMinSize(BSize(u * 520, u * 300));

	fRevert = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));
	fDefaults = new BButton("defaults", B_TRANSLATE("Defaults"),
		new BMessage(kMsgDefaults));

	BLayoutBuilder::Group<>(this, B_VERTICAL, u * 12)
		.SetInsets(u * 12)
		.AddGroup(B_HORIZONTAL, u * 14, 1)
			.Add(scroll)
			.AddGroup(B_VERTICAL, u * 12, 1)
				.Add(fHeader)
				.Add(deck, 1)
				.AddGroup(B_HORIZONTAL, u * 6, 0)
					.AddGlue()
					.Add(fRevert)
					.Add(fDefaults)
					.End()
				.End()
			.End();

	_Fill();
	_SelectPage("general");
	ResizeTo(floorf(u * 900), floorf(u * 580));
	CenterOnScreen();
}


AomiSettingsWindow::~AomiSettingsWindow()
{
	delete fFolderPanel;
	BAutolock _(sWindowLock);
	if (sWindow == this)
		sWindow = NULL;
}


bool
AomiSettingsWindow::QuitRequested()
{
	BAutolock _(sWindowLock);
	if (sWindow == this)
		sWindow = NULL;
	return true;
}


/*static*/ void
AomiSettingsWindow::ShowWindow(const char* page)
{
	BAutolock _(sWindowLock);
	if (sWindow == NULL) {
		sWindow = new AomiSettingsWindow();
		if (page != NULL)
			sWindow->_SelectPage(page);
		sWindow->Show();
		return;
	}
	if (sWindow->Lock()) {
		if (page != NULL)
			sWindow->_SelectPage(page);
		if (sWindow->IsHidden())
			sWindow->Show();
		sWindow->Activate(true);
		sWindow->Unlock();
	}
}


BView*
AomiSettingsWindow::_GeneralPage()
{
	BPopUpMenu* starts = radio_menu("start");
	starts->AddItem(new BMenuItem(B_TRANSLATE("The new tab page"),
		new BMessage(kMsgChanged)));
	starts->AddItem(new BMenuItem(B_TRANSLATE("The home page"),
		new BMessage(kMsgChanged)));
	starts->AddItem(new BMenuItem(B_TRANSLATE("The pages from last time"),
		new BMessage(kMsgChanged)));
	fStart = new BMenuField("start", B_TRANSLATE("Open:"), starts);
	fHome = new BTextControl("home", B_TRANSLATE("Home page:"), "",
		new BMessage(kMsgChanged));
	fHome->SetModificationMessage(new BMessage(kMsgChanged));
	BButton* current = new BButton("current", B_TRANSLATE("Use current page"),
		new BMessage(kMsgUseCurrent));

	BGroupView* start = new BGroupView(B_VERTICAL);
	BLayoutBuilder::Group<>(start)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(right(fStart->CreateLabelLayoutItem()), 0, 0)
			.Add(fStart->CreateMenuBarLayoutItem(), 1, 0)
			.Add(right(fHome->CreateLabelLayoutItem()), 0, 1)
			.Add(fHome->CreateTextViewLayoutItem(), 1, 1)
			.Add(current, 2, 1)
			.SetColumnWeight(1, 1)
			.End();

	fShowHome = new BCheckBox("home button",
		B_TRANSLATE("Show the home button"), new BMessage(kMsgChanged));
	fShowBar = new BCheckBox("bookmarks bar",
		B_TRANSLATE("Show the bookmarks bar"), new BMessage(kMsgChanged));
	fShowMenuBar = new BCheckBox("menu bar",
		B_TRANSLATE("Show the menu bar"), new BMessage(kMsgChanged));
	BGroupView* toolbar = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(toolbar)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fShowHome)
		.Add(fShowBar)
		.Add(fShowMenuBar)
		.Add(note(B_TRANSLATE("The bookmarks bar shows the folder \"Bookmark "
			"bar\" in your bookmarks.")));

	return page(box(B_TRANSLATE("When Aomi starts"), start),
		box(B_TRANSLATE("Toolbar"), toolbar));
}


BView*
AomiSettingsWindow::_SearchPage()
{
	BPopUpMenu* engines = radio_menu("engine");
	for (int32 i = 0; kAomiSearchEngines[i].name != NULL; i++) {
		engines->AddItem(new BMenuItem(kAomiSearchEngines[i].name,
			new BMessage(kMsgEngine)));
	}
	engines->AddSeparatorItem();
	engines->AddItem(new BMenuItem(B_TRANSLATE("Other"),
		new BMessage(kMsgEngine)));
	fEngine = new BMenuField("engine", B_TRANSLATE("Search with:"),
		engines);
	fSearch = new BTextControl("search", B_TRANSLATE("Search address:"), "",
		new BMessage(kMsgChanged));
	fSearch->SetModificationMessage(new BMessage(kMsgChanged));

	BGroupView* search = new BGroupView(B_VERTICAL);
	BLayoutBuilder::Group<>(search)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(right(fEngine->CreateLabelLayoutItem()), 0, 0)
			.Add(fEngine->CreateMenuBarLayoutItem(), 1, 0)
			.Add(right(fSearch->CreateLabelLayoutItem()), 0, 1)
			.Add(fSearch->CreateTextViewLayoutItem(), 1, 1)
			.Add(note(B_TRANSLATE("%s stands for the words you type.")), 1, 2)
			.SetColumnWeight(1, 1)
			.End()
		.Add(note(B_TRANSLATE("Text that looks like an address opens that "
			"site; anything else is searched.")));
	return page(box(B_TRANSLATE("Search"), search));
}


BView*
AomiSettingsWindow::_AppearancePage()
{
	BPopUpMenu* colors = radio_menu("colors");
	colors->AddItem(new BMenuItem(B_TRANSLATE("Like R5 (light or dark)"),
		new BMessage(kMsgChanged)));
	colors->AddItem(new BMenuItem(B_TRANSLATE("Always light"),
		new BMessage(kMsgChanged)));
	colors->AddItem(new BMenuItem(B_TRANSLATE("Always dark"),
		new BMessage(kMsgChanged)));
	fColors = new BMenuField("colors", B_TRANSLATE("Page colours:"),
		colors);
	BPopUpMenu* zooms = radio_menu("zoom");
	for (int32 i = 0; i < kZoomCount; i++) {
		BString text;
		text.SetToFormat("%d %%", (int)kZooms[i]);
		zooms->AddItem(new BMenuItem(text.String(), new BMessage(kMsgChanged)));
	}
	fZoom = new BMenuField("zoom", B_TRANSLATE("Zoom of a new tab:"),
		zooms);

	BGroupView* pages = new BGroupView(B_VERTICAL);
	BLayoutBuilder::Group<>(pages)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(right(fColors->CreateLabelLayoutItem()), 0, 0)
			.Add(fColors->CreateMenuBarLayoutItem(), 1, 0)
			.Add(right(fZoom->CreateLabelLayoutItem()), 0, 1)
			.Add(fZoom->CreateMenuBarLayoutItem(), 1, 1)
			.SetColumnWeight(1, 1)
			.End()
		.Add(note(B_TRANSLATE("Sites that offer a dark look use it in dark "
			"mode. Pages also grow with the font size in Appearance.")));
	return page(box(B_TRANSLATE("Pages"), pages));
}


BView*
AomiSettingsWindow::_DownloadsPage()
{
	fFolder = new BTextControl("folder", B_TRANSLATE("Save files to:"), "",
		new BMessage(kMsgChanged));
	fFolder->SetModificationMessage(new BMessage(kMsgChanged));
	BButton* choose = new BButton("choose", B_TRANSLATE("Choose…"),
		new BMessage(kMsgChoose));
	fAsk = new BCheckBox("ask", B_TRANSLATE("Ask where to save each file"),
		new BMessage(kMsgChanged));
	BButton* show = new BButton("show", B_TRANSLATE("Show downloads"),
		new BMessage(kMsgShowDownloads));

	BGroupView* downloads = new BGroupView(B_VERTICAL);
	BLayoutBuilder::Group<>(downloads)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(right(fFolder->CreateLabelLayoutItem()), 0, 0)
			.Add(fFolder->CreateTextViewLayoutItem(), 1, 0)
			.Add(choose, 2, 0)
			.Add(fAsk, 1, 1, 2)
			.SetColumnWeight(1, 1)
			.End()
		.AddGroup(B_HORIZONTAL)
			.Add(show)
			.AddGlue()
			.End();
	return page(box(B_TRANSLATE("Downloads"), downloads));
}


BView*
AomiSettingsWindow::_PrivacyPage()
{
	BGroupView* privat = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(privat)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(new BStringView("private1", B_TRANSLATE("A private window "
			"forgets its history, cookies and site data when you close it.")))
		.Add(note(B_TRANSLATE("Downloads and new bookmarks stay. Open one "
			"with Shift-Command-N.")));

	fClearHistory = new BButton("clear history",
		B_TRANSLATE("Clear history"), new BMessage(kMsgClearHistory));
	fHistoryNote = note(B_TRANSLATE("The list of pages you visited. "
		"Bookmarks stay."));
	BGroupView* history = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(history)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(fClearHistory)
			.AddGlue()
			.End()
		.Add(fHistoryNote);

	fClearSiteData = new BButton("clear site data",
		B_TRANSLATE("Clear cookies and site data"),
		new BMessage(kMsgClearSiteData));
	fSiteDataNote = note("");
	BGroupView* site = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(site)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(fClearSiteData)
			.AddGlue()
			.End()
		.Add(fSiteDataNote);

	return page(box(B_TRANSLATE("Private windows"), privat),
		box(B_TRANSLATE("History"), history),
		box(B_TRANSLATE("Cookies and site data"), site));
}


void
AomiSettingsWindow::_SelectPage(const char* id)
{
	if (id == NULL)
		return;
	for (int32 i = 0; i < kPageCount; i++) {
		if (strcmp(kPages[i].id, id) != 0)
			continue;
		fCards->SetVisibleItem(i);
		fHeader->SetTitle(B_TRANSLATE_NOCOLLECT(kPages[i].label));
		fHeader->SetSubtitle(B_TRANSLATE_NOCOLLECT(kPages[i].subtitle));
		// The heading's icon square is 48 units (docs/LIBR5.md).
		BBitmap* icon = page_icon(id, floorf(be_plain_font->Size() / 13.0f
			* 48));
		fHeader->SetIcon(icon);
		delete icon;
		if (fList->CurrentSelection() != i) {
			BMessage* message = fList->SelectionMessage() != NULL
				? new BMessage(*fList->SelectionMessage()) : NULL;
			fList->SetSelectionMessage(NULL);
			fList->Select(i);
			fList->SetSelectionMessage(message);
		}
		return;
	}
}


void
AomiSettingsWindow::_Fill()
{
	fFilling = true;
	mark(fStart, fPrefs.start);
	fHome->SetText(fPrefs.home.String());
	fShowHome->SetValue(fPrefs.showHome);
	fShowBar->SetValue(fPrefs.showBookmarks);
	fShowMenuBar->SetValue(fPrefs.showMenuBar);
	int32 engine = aomi_search_engine_index(fPrefs.search.String());
	int32 count = fEngine->Menu()->CountItems();
	mark(fEngine, engine >= 0 ? engine : count - 1);
	fSearch->SetText(fPrefs.search.String());
	fSearch->SetEnabled(engine < 0);
	mark(fColors, fPrefs.pageColors);
	int32 zoom = 4;
	for (int32 i = 0; i < kZoomCount; i++) {
		if (kZooms[i] == fPrefs.zoom)
			zoom = i;
	}
	mark(fZoom, zoom);
	fFolder->SetText(fPrefs.downloadPath.String());
	fAsk->SetValue(fPrefs.askDownload);
	fFilling = false;
	_UpdateButtons();
}


void
AomiSettingsWindow::_Save()
{
	if (fFilling)
		return;
	fPrefs.start = marked(fStart) >= 0 ? marked(fStart) : 0;
	fPrefs.home = fHome->Text();
	fPrefs.showHome = fShowHome->Value() != 0;
	fPrefs.showBookmarks = fShowBar->Value() != 0;
	fPrefs.showMenuBar = fShowMenuBar->Value() != 0;
	fPrefs.search = fSearch->Text();
	fPrefs.pageColors = marked(fColors) >= 0 ? marked(fColors) : 0;
	int32 zoom = marked(fZoom);
	fPrefs.zoom = zoom >= 0 && zoom < kZoomCount ? kZooms[zoom] : 100;
	fPrefs.downloadPath = fFolder->Text();
	fPrefs.askDownload = fAsk->Value() != 0;
	aomi_prefs_save(fPrefs);
	for (int32 i = 0; i < be_app->CountWindows(); i++) {
		BWindow* window = be_app->WindowAt(i);
		if (dynamic_cast<AomiWindow*>(window) != NULL)
			window->PostMessage(MSG_SETTINGS_CHANGED);
	}
	_UpdateButtons();
}


void
AomiSettingsWindow::_UpdateButtons()
{
	AomiPrefs defaults;
	aomi_prefs_defaults(&defaults);
	auto same = [](const AomiPrefs& a, const AomiPrefs& b) {
		return a.start == b.start && a.home == b.home && a.search == b.search
			&& a.showHome == b.showHome && a.showBookmarks == b.showBookmarks
			&& a.downloadPath == b.downloadPath
			&& a.askDownload == b.askDownload
			&& a.showMenuBar == b.showMenuBar
			&& a.pageColors == b.pageColors && a.zoom == b.zoom;
	};
	fRevert->SetEnabled(!same(fPrefs, fOpened));
	fDefaults->SetEnabled(!same(fPrefs, defaults));
	bool pending = aomi_site_data_clear_pending();
	fClearSiteData->SetEnabled(!pending);
	fSiteDataNote->SetText(pending
		? B_TRANSLATE("They are cleared when Aomi starts again.")
		: B_TRANSLATE("Sign-ins and site preferences are lost. Aomi clears "
			"them when it starts again."));
}


void
AomiSettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPage:
		{
			int32 index = fList->CurrentSelection();
			if (index >= 0 && index < kPageCount)
				_SelectPage(kPages[index].id);
			break;
		}
		case kMsgChanged:
			_Save();
			break;
		case kMsgEngine:
		{
			// The engines, a separator, then Other.
			int32 engines = 0;
			while (kAomiSearchEngines[engines].name != NULL)
				engines++;
			int32 index = marked(fEngine);
			bool other = index < 0 || index >= engines;
			if (!other) {
				fFilling = true;
				fSearch->SetText(kAomiSearchEngines[index].address);
				fFilling = false;
			}
			fSearch->SetEnabled(other);
			if (other)
				fSearch->MakeFocus(true);
			_Save();
			break;
		}
		case kMsgRevert:
			fPrefs = fOpened;
			_Fill();
			_Save();
			break;
		case kMsgDefaults:
			aomi_prefs_defaults(&fPrefs);
			_Fill();
			_Save();
			break;
		case kMsgUseCurrent:
		{
			for (int32 i = 0; i < be_app->CountWindows(); i++) {
				AomiWindow* window
					= dynamic_cast<AomiWindow*>(be_app->WindowAt(i));
				if (window == NULL || !window->Lock())
					continue;
				BString address(window->Address());
				window->Unlock();
				if (address.StartsWith("http://")
					|| address.StartsWith("https://")) {
					fHome->SetText(address.String());
					_Save();
					break;
				}
			}
			break;
		}
		case kMsgChoose:
			if (fFolderPanel == NULL) {
				fFolderPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
					NULL, B_DIRECTORY_NODE, false, new BMessage(kMsgChosen));
				fFolderPanel->Window()->SetTitle(
					B_TRANSLATE("Save downloads to"));
			}
			{
				entry_ref dir;
				if (get_ref_for_path(fFolder->Text(), &dir) == B_OK)
					fFolderPanel->SetPanelDirectory(&dir);
			}
			fFolderPanel->Show();
			break;
		case kMsgChosen:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				fFolder->SetText(path.Path());
				_Save();
			}
			break;
		}
		case kMsgShowDownloads:
			AomiDownloadsWindow::ShowWindow(fPrefs.downloadPath.String());
			break;
		case kMsgClearHistory:
			if (aomi_history_clear() == B_OK) {
				fHistoryNote->SetText(B_TRANSLATE("History cleared."));
				fClearHistory->SetEnabled(false);
			}
			break;
		case kMsgClearSiteData:
			aomi_site_data_clear_request();
			_UpdateButtons();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
