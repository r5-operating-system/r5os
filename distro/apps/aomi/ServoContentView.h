/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BWebContentView backed by libr5servo.so loaded via dlopen().
 * If libr5servo.so is missing or fails to start, draws a clear error message.
 */
#ifndef AOMI_SERVO_CONTENT_VIEW_H
#define AOMI_SERVO_CONTENT_VIEW_H


#include "WebContentView.h"


class BBitmap;
class BMessageRunner;
struct r5_servo;
struct r5_servo_event;


status_t						aomi_servo_start();
bool							aomi_servo_available();
const char*						aomi_servo_error();
const char*						aomi_servo_gl_info();
const char*						aomi_servo_last_protocol();
void							aomi_log_add(const char* line);
BString							aomi_log_dump_text();
void							aomi_log_dump_json(BString* out);
status_t						aomi_start_download(const char* url,
									const char* destDir);


class BServoContentView : public BWebContentView {
public:
								BServoContentView(const char* name,
									BWebSession* session);
	virtual						~BServoContentView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Show();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				MakeFocus(bool focused);
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
			void				_ScheduleSpin(bigtime_t delay);
			void				_CopyFrame();
			void				_OnEvent(const r5_servo_event* event);
			status_t			_Report(uint32 what, const char* url,
									const char* text, float progress,
									int32 a, int32 b, int32 c);
			status_t			_FillHistory(BMessage* items,
									bool forward) const;
	static	void				_Callback(void* context,
									const r5_servo_event* event);

			r5_servo*			fEngine;
			BBitmap*			fBitmap;
			BMessageRunner*		fRunner;
			BString				fError;
			BString				fProtocol;
			BString				fCurrentURL;
			BString				fCurrentTitle;
			bool				fLoading;
			int32				fIdleSpins;
};


#endif
