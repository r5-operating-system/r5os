/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef AOMI_SUPPORT_H
#define AOMI_SUPPORT_H


#include <String.h>

#include <vector>


class BMenu;


struct AomiPrefs {
	int32		start;
		// 0 new tab, 1 home, 2 last session
	BString		home;
	BString		search;
	bool		showHome;
	bool		showBookmarks;
	bool		zoomText;
	BString		downloadPath;
	bool		askDownload;
	bool		showMenuBar;
	// Since 27 Nov 2026.
	int32		pageColors;
		// 0 follow R5's light or dark mode, 1 always light, 2 always dark
	int32		zoom;
		// the zoom of a new tab, in percent
};


struct AomiSearchEngine {
	const char*	name;
	const char*	address;
		// "%s" is what was typed
};

// The search engines the settings offer. The last entry is { NULL, NULL }.
extern const AomiSearchEngine kAomiSearchEngines[];
// The index in kAomiSearchEngines of an address, or -1 (Other).
int32							aomi_search_engine_index(const char* address);


AomiPrefs						aomi_prefs();
void							aomi_prefs_save(const AomiPrefs& prefs);
void							aomi_prefs_defaults(AomiPrefs* prefs);

status_t						aomi_bookmark_add(const char* url,
									const char* title);
bool							aomi_bookmark_has(const char* url);
void							aomi_fill_bookmarks(BMenu* menu, uint32 what);
void							aomi_open_bookmarks();

void							aomi_history_add(const char* url);
status_t						aomi_history_clear();
void							aomi_fill_history(BMenu* menu, uint32 what);
int32							aomi_history_match(const char* text,
									BString* urls, int32 max);

// Empty when the text should not be loaded (another app took the scheme,
// or the field was empty).
BString							aomi_smart_url(const BString& text,
									const BString& search);

// The bookmarks in the "Bookmark bar" folder (WebPositive's), by name.
void							aomi_bookmark_bar(std::vector<BString>* titles,
									std::vector<BString>* urls);

// Hands an address that is not a web page (mailto:, tel:) to the program
// that takes its scheme. B_NAME_NOT_FOUND when none does, B_NOT_ALLOWED for
// a scheme other than mailto: and tel: (a web page may not start programs).
status_t						aomi_open_external(const char* url);

// The open pages of the last window that closed ("Last session").
void							aomi_session_save(
									const std::vector<BString>& urls);
void							aomi_session_load(std::vector<BString>* urls);


#endif
