/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * "Aomi settings": Aomi's own window in the frame of R5's Settings (a list
 * of pages on the left, the page heading, the page, Revert and Defaults).
 * Not a page of the Settings program, whose list is fixed (AGENTS.md).
 * A change applies at once.
 */
#ifndef AOMI_SETTINGS_H
#define AOMI_SETTINGS_H


#include <Window.h>

#include "AomiSupport.h"


class BButton;
class BCardLayout;
class BCheckBox;
class BFilePanel;
class BMenuField;
class BStringView;
class BTextControl;

namespace BAppearanceKit {
	class BPageHeader;
	class BSidebar;
}


class AomiSettingsWindow : public BWindow {
public:
								AomiSettingsWindow();
	virtual						~AomiSettingsWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

	// The one settings window, made on first use, on `page` ("general",
	// "search", "appearance", "downloads", "privacy"; NULL keeps the page).
	static	void				ShowWindow(const char* page);

private:
			BView*				_GeneralPage();
			BView*				_SearchPage();
			BView*				_AppearancePage();
			BView*				_DownloadsPage();
			BView*				_PrivacyPage();
			void				_SelectPage(const char* id);
			void				_Fill();
			void				_Save();
			void				_UpdateButtons();

			AomiPrefs			fPrefs;
			AomiPrefs			fOpened;
			bool				fFilling;

			BAppearanceKit::BSidebar* fList;
			BAppearanceKit::BPageHeader* fHeader;
			BCardLayout*		fCards;
			BButton*			fRevert;
			BButton*			fDefaults;

			BMenuField*			fStart;
			BTextControl*		fHome;
			BCheckBox*			fShowHome;
			BCheckBox*			fShowBar;
			BCheckBox*			fShowMenuBar;
			BMenuField*			fEngine;
			BTextControl*		fSearch;
			BMenuField*			fColors;
			BMenuField*			fZoom;
			BTextControl*		fFolder;
			BCheckBox*			fAsk;
			BButton*			fClearHistory;
			BStringView*		fHistoryNote;
			BButton*			fClearSiteData;
			BStringView*		fSiteDataNote;
			BFilePanel*			fFolderPanel;
};


// Site data (cookies, storage, the cache) is cleared on the next start:
// Servo keeps the profile open while it runs.
bool							aomi_site_data_clear_pending();
void							aomi_site_data_clear_request();
// Called once at start, before the engine: clears the profile if asked.
void							aomi_site_data_clear_if_pending(
									const char* profileDir);


#endif
