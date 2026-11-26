/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BWebContentView backed by libr5servo.so loaded via dlopen().
 * If libr5servo.so is missing or fails to start, draws a clear error message.
 *
 * Servo runs once per process on its own engine thread (ServoEngine in
 * ServoContentView.cpp); every window of the process shares it. A view
 * talks to it with messages and draws the frames it gets back.
 */
#ifndef AOMI_SERVO_CONTENT_VIEW_H
#define AOMI_SERVO_CONTENT_VIEW_H


#include "WebContentView.h"


struct ServoTab;


status_t						aomi_servo_start();
// After the last window closed: Servo writes its profile and stops.
void							aomi_servo_shutdown();
bool							aomi_servo_available();
const char*						aomi_servo_error();
const char*						aomi_servo_gl_info();
const char*						aomi_servo_last_protocol();
void							aomi_log_add(const char* line);
BString							aomi_log_dump_text();
void							aomi_log_dump_json(BString* out);
status_t						aomi_start_download(const char* url,
									const char* destDir);
status_t						aomi_start_download_to(const char* url,
									const char* path);


class BServoContentView : public BWebContentView {
public:
								// adoptTab: an engine Servo made for a page
								// (window.open()); NULL for a new tab.
								BServoContentView(const char* name,
									BWebSession* session,
									void* adoptTab = NULL);
	virtual						~BServoContentView();

	virtual	void				AttachedToWindow();
	virtual	void				Show();
	virtual	void				Hide();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				MakeFocus(bool focused);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				KeyUp(const char* bytes, int32 numBytes);
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

private:
			status_t			_Start(const char* document, bool isHTML);
			void				_Call(int32 op, const char* text = NULL,
									float value = 0, bool on = false,
									int32 width = 0, int32 height = 0);
			void				_Input(const BMessage* event, BPoint where);
			void				_OnEvent(BMessage* event);
			void				_AskControl(int32 kind, const char* text,
									BPoint where, int64 control);
			status_t			_Report(uint32 what, const char* url,
									const char* text, float progress,
									int32 a, int32 b, int32 c);
			status_t			_FillHistory(BMessage* items,
									bool forward) const;
	static	ServoTab*			_TabIn(BView* view,
									const BServoContentView* except);

			ServoTab*			fTab;
			bool				fAdopted;
			BString				fError;
			BString				fProtocol;
			BString				fCurrentURL;
			BString				fCurrentTitle;
			bool				fLoading;
			BPoint				fLastMouse;
			bool				fHasPending;
			bool				fPendingHTML;
			BString				fPendingDocument;
};


#endif
