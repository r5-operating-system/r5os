/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The page view every engine fills. The window talks to this class and
 * to nothing else. WebKit is BWebKitContentView. A later engine
 * (BServoView) is another subclass. New calls go at the end. A call
 * that shipped does not change. docs/design/AOMI-UI.md, "The engine view".
 */
#ifndef AOMI_WEB_CONTENT_VIEW_H
#define AOMI_WEB_CONTENT_VIEW_H


#include <Messenger.h>
#include <String.h>
#include <View.h>


class BWebSession {
public:
								BWebSession(bool privateSession);
			bool				IsPrivate() const { return fPrivate; }

private:
			bool				fPrivate;
			uint32				_reserved[4];
};


// Reports. The fields below are part of the contract.
enum {
	// "url" string
	AOMI_LOAD_STARTED			= 'aLdS',
	// "url" string. The address that committed.
	AOMI_LOAD_COMMITTED			= 'aLdC',
	// "url" string
	AOMI_LOAD_FINISHED			= 'aLdF',
	// "url" string, "text" string, "error" int32
	AOMI_LOAD_FAILED			= 'aLdX',
	// "canBack" bool, "canForward" bool, "loading" bool
	AOMI_NAVIGATION				= 'aNav',
	// "title" string
	AOMI_TITLE_CHANGED			= 'aTit',
	// "url" string
	AOMI_URL_CHANGED			= 'aUrl',
	// "icon" archived BBitmap (a BMessage)
	AOMI_FAVICON				= 'aFav',
	// "url" string. Empty when the pointer left the link.
	AOMI_HOVER_LINK				= 'aHov',
	// "state" int32 (AOMI_SECURITY_*), "text" string
	AOMI_SECURITY				= 'aSec',
	// "protocol" string: "h2", "http/1.1" or "h3"
	AOMI_PROTOCOL				= 'aPrt',
	// "progress" float, 0 to 1
	AOMI_PROGRESS				= 'aPrg',
	AOMI_FOCUS_WANTED			= 'aFoc',
	// "cursor" int32, a cursor_id
	AOMI_CURSOR					= 'aCur',
	// "on" bool
	AOMI_FULLSCREEN				= 'aFsc',
	// "url" string, "primary" bool. "view" pointer when the engine
	// already built the BWebContentView the window should adopt.
	// Otherwise the window creates one through create_aomi_content_view()
	// and loads "url". primary false: do not select the new tab.
	AOMI_NEW_WINDOW				= 'aNww',
	AOMI_CLOSE_WANTED			= 'aClw',
	// "id" int32, "name" string, "size" int64 (expected; -1 unknown)
	AOMI_DOWNLOAD_ADDED			= 'aDla',
	// "id" int32, "current" int64, "expected" int64
	AOMI_DOWNLOAD_PROGRESS		= 'aDlp',
	// "id" int32, "path" string
	AOMI_DOWNLOAD_FINISHED		= 'aDlf',
	// "id" int32, "text" string
	AOMI_DOWNLOAD_FAILED		= 'aDlx',
	// "kind" int32 (AOMI_DIALOG_*), "text" string, "where" BPoint in view
	// coordinates. For the listener's information: the view itself asks the
	// user (alert, confirm, prompt, sign-in, file upload, context menu,
	// select list) and answers the page.
	AOMI_DIALOG					= 'aDlg',
	// "url" string: an address for another program (mailto:, tel:). The
	// page stays where it is; the window hands the address on.
	AOMI_EXTERNAL_URL			= 'aExt',
	// "url" string, the file to fetch; "text" string, the address to show
	// and log (no sign-in in it). A link to a file the engine cannot show.
	AOMI_DOWNLOAD_WANTED		= 'aDlw'
};


enum {
	AOMI_SECURITY_UNKNOWN		= 0,
	AOMI_SECURITY_SECURE		= 1,
	AOMI_SECURITY_NOT_SECURE	= 2,
	AOMI_SECURITY_CERTIFICATE	= 3
};


enum {
	AOMI_DIALOG_ALERT			= 1,
	AOMI_DIALOG_CONFIRM			= 2,
	AOMI_DIALOG_PROMPT			= 3,
	AOMI_DIALOG_LEAVE			= 4,
	AOMI_DIALOG_AUTH			= 5,
	AOMI_DIALOG_FILE			= 6,
	AOMI_DIALOG_PERMISSION		= 7,
	AOMI_DIALOG_CONTEXT			= 8,
	// "text": one line per entry, "o<TAB>id<TAB>flags<TAB>label" for an
	// option (flags: s selected, d disabled), "g<TAB><TAB><TAB>label" for a
	// group heading. Answer with the chosen option's id as value.
	AOMI_DIALOG_SELECT			= 9
};


class BWebContentView : public BView {
public:
								BWebContentView(const char* name,
									BWebSession* session);
	virtual						~BWebContentView();

			void				SetListener(const BMessenger& listener);
			BWebSession*		Session() const { return fSession; }

	// Load
	virtual	status_t			LoadURL(const char* url) = 0;
	virtual	status_t			LoadHTML(const char* html,
									const char* baseURL) = 0;
	virtual	status_t			Reload(bool bypassCache) = 0;
	virtual	status_t			StopLoading() = 0;

	// Navigation. The lists are repeated "url" and "title" strings.
	virtual	status_t			GoBack() = 0;
	virtual	status_t			GoForward() = 0;
	virtual	status_t			GetBackList(BMessage* items) = 0;
	virtual	status_t			GetForwardList(BMessage* items) = 0;

	// Page
	virtual	status_t			URL(BString* url) const = 0;
	virtual	status_t			Title(BString* title) const = 0;
	virtual	status_t			SetZoom(float factor, bool textOnly) = 0;
	virtual	status_t			Find(const char* text, bool forward,
									bool matchCase) = 0;
	virtual	status_t			SavePage(const char* path) = 0;
	virtual	status_t			ViewSource() = 0;
	virtual	status_t			Print() = 0;
			// "h2", "http/1.1" or "h3". B_NOT_SUPPORTED when this engine
			// cannot see the protocol of the main document.
	virtual	status_t			Protocol(BString* protocol) const = 0;

	// Downloads. The engine writes the file. The window does not fetch.
	virtual	status_t			Cancel(int32 id) = 0;
	virtual	status_t			Open(int32 id) = 0;
	virtual	status_t			ShowInTracker(int32 id) = 0;

	// Added 27 Nov 2026. The page's light or dark preference
	// (prefers-color-scheme) and how many device pixels a CSS pixel is
	// (1 at a 12 point system font). B_NOT_SUPPORTED when the engine
	// cannot.
	virtual	status_t			SetColorScheme(bool dark);
	virtual	status_t			SetScale(float scale);



protected:
			status_t			Report(BMessage* message);

			BMessenger			fListener;
			BWebSession*		fSession;
};


// Starts the named engine once. "WebKit" today. Unknown name:
// B_NOT_SUPPORTED.
status_t						aomi_engine_start(const char* engine);

// The one place a window creates a page view. "WebKit" today.
// A later engine is another branch in this function only.
BWebContentView*				create_aomi_content_view(const char* engine,
									const char* name, BWebSession* session);


#endif
