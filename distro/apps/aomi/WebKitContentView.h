/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BWebContentView around Haiku's BWebView (haikuwebkit 1.10.0, Legacy).
 */
#ifndef AOMI_WEBKIT_CONTENT_VIEW_H
#define AOMI_WEBKIT_CONTENT_VIEW_H


#include "WebContentView.h"


class BWebView;


status_t						aomi_webkit_start();


class BWebKitContentView : public BWebContentView {
public:
								BWebKitContentView(const char* name,
									BWebSession* session);
								BWebKitContentView(const char* name,
									BWebSession* session, BWebView* adopt);
	virtual						~BWebKitContentView();

	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				FrameResized(float width, float height);
	virtual	void				MessageReceived(BMessage* message);

	virtual	status_t			LoadURL(const char* url);
	virtual	status_t			LoadHTML(const char* html,
									const char* baseURL);
	virtual	status_t			Reload(bool bypassCache);
	virtual	status_t			StopLoading();

	virtual	status_t			GoBack();
	virtual	status_t			GoForward();
	virtual	status_t			GetBackList(BMessage* items);
	virtual	status_t			GetForwardList(BMessage* items);

	virtual	status_t			URL(BString* url) const;
	virtual	status_t			Title(BString* title) const;
	virtual	status_t			SetZoom(float factor, bool textOnly);
	virtual	status_t			Find(const char* text, bool forward,
									bool matchCase);
	virtual	status_t			SavePage(const char* path);
	virtual	status_t			ViewSource();
	virtual	status_t			Print();
	virtual	status_t			Protocol(BString* protocol) const;

	virtual	status_t			Cancel(int32 id);
	virtual	status_t			Open(int32 id);
	virtual	status_t			ShowInTracker(int32 id);

			void				ShutdownEngine();

private:
			void				_Adopt(BWebView* web);
			void				_Fit();
			void				_Listen();
			void				_Deliver(BMessage* message);
			bool				_Dark() const;

			BWebView*			fWeb;
			float				fZoom;
			bool				fTextOnly;
};


#endif
