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

			void				OpenURL(const char* url, bool newTab);
			void				HandleEngine(BMessage* message, int32 tab);
			void				OpenMenu();
			void				OpenDownloads();
			void				OpenSettings();
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
			const AomiPrefs&	Prefs() const { return fPrefs; }
			void				ReloadPrefs();

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
				BString				hover;
				bool				bookmarked;
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
};


class AomiSettingsWindow : public BWindow {
public:
								AomiSettingsWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Load();
			void				_Save();
			AomiPrefs			fPrefs;
			AomiPrefs			fOpened;
			class BTextControl*	fHome;
			class BTextControl*	fSearch;
			class BMenuField*	fStart;
			class BMenuField*	fEngine;
			class BCheckBox*	fShowHome;
			class BCheckBox*	fShowBar;
			class BCheckBox*	fZoomText;
			class BTextControl*	fDownloads;
			class BCheckBox*	fAsk;
};


#endif
