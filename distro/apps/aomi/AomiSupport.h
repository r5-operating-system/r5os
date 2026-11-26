/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef AOMI_SUPPORT_H
#define AOMI_SUPPORT_H


#include <String.h>


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
};


AomiPrefs						aomi_prefs();
void							aomi_prefs_save(const AomiPrefs& prefs);
void							aomi_prefs_defaults(AomiPrefs* prefs);

status_t						aomi_bookmark_add(const char* url,
									const char* title);
bool							aomi_bookmark_has(const char* url);
void							aomi_fill_bookmarks(BMenu* menu, uint32 what);
void							aomi_open_bookmarks();

void							aomi_history_add(const char* url);
void							aomi_fill_history(BMenu* menu, uint32 what);
int32							aomi_history_match(const char* text,
									BString* urls, int32 max);

// Empty when the text should not be loaded (another app took the scheme,
// or the field was empty).
BString							aomi_smart_url(const BString& text,
									const BString& search);


#endif
