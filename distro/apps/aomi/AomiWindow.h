/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef AOMI_WINDOW_H
#define AOMI_WINDOW_H


#include <vector>

#include <Bitmap.h>
#include <MenuBar.h>
#include <String.h>
#include <Window.h>

#include "AomiSupport.h"


class BFilePanel;
class BMessageRunner;
class BTextControl;
class BTextView;
class BWebContentView;
class BWebSession;


class AomiWindow : public BWindow {
public:
								AomiWindow(BWebSession* session, bool privat);
	virtual						~AomiWindow();

	virtual	void				Show();
	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MenusBeginning();
	virtual	void				WindowActivated(bool active);

			void				OpenURL(const char* url, bool newTab);
			void				HandleEngine(BMessage* message, int32 tab);
			int32				TabIndexForProxy(const BHandler* proxy) const;
			void				OpenMenu();
			void				OpenDownloads();
			void				OpenSettings(const char* page = NULL);
			int32				CountTabs() const { return (int32)fTabs.size(); }
			int32				Active() const { return fActive; }
			const char*			TabTitle(int32 index) const;
			bool				TabLoading(int32 index) const;
			float				TabProgress(int32 index) const;
			bool				CanBack() const;
			bool				CanForward() const;
			bool				Loading() const;
			const char*			Address() const;
			int32				Security() const;
			bool				Bookmarked() const;
			bool				Private() const { return fPrivate; }
			float				PageZoom() const { return fZoom; }
			const AomiPrefs&	Prefs() const { return fPrefs; }
			void				ReloadPrefs();
			// The addresses of the open web pages (for "Last session").
			void				GetSession(std::vector<BString>* urls) const;

	// The program quits: windows close without asking again.
	static	void				SetQuitting();
	static	bool				Quitting();
	// "Downloads are still running"; true: stop them and close.
	static	bool				AskStopDownloads(int32 running);

private:
			struct Tab {
				BWebContentView*	view;
				BHandler*			proxy;
				BString				title;
				BString				url;
				BBitmap*			icon;
				bool				canBack;
				bool				canForward;
				bool				loading;
				float				progress;
				int32				security;
				BString				securityText;
				BString				protocol;
				BString				hover;
				bool				bookmarked;
				// The engine showed its "could not load" page: loaded
				// again when the network comes back.
				bool				failed;
				bool				zoomApplied;
			};

			void				_BuildMenu();
			void				_AddTab(const char* url, bool select,
									BWebContentView* adopt);
			void				_LoadOn(int32 index, const char* url);
			void				_Select(int32 index);
			void				_Close(int32 index);
			void				_Load(const char* url);
			void				_ApplyChrome();
			void				_Title();
			void				_OpenLock();
			void				_Log(const char* text);
			void				_ApplyLook(int32 index);
			void				_RetryFailed();
			void				_OpenExternal(const char* url);
			void				_Download(const char* url, const char* shown);
			bool				_IsLastBrowserWindow() const;
			static BRect		_Frame();

			BWebSession*		fSession;
			bool				fPrivate;
			AomiPrefs			fPrefs;
			BMenuBar*			fMenu;
			BView*				fChrome;
			BView*				fPages;
			BView*				fNewTab;
			BView*				fFindBar;
			BTextControl*		fFind;
			std::vector<Tab>	fTabs;
			int32				fActive;
			float				fZoom;
			BFilePanel*			fSave;
			BString				fHover;
			BMessageRunner*		fRetry;
			BMessageRunner*		fRetryPoll;
			BFilePanel*			fDownloadPanel;
};


#endif
