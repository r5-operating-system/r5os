/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "WebContentView.h"

#include "ServoContentView.h"

#include <string.h>


BWebSession::BWebSession(bool privateSession)
	:
	fPrivate(privateSession)
{
	_reserved[0] = _reserved[1] = _reserved[2] = _reserved[3] = 0;
}


BWebContentView::BWebContentView(const char* name, BWebSession* session)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE
		| B_FULL_UPDATE_ON_RESIZE),
	fSession(session)
{
	// The page is opaque on every engine. Glass stays in the chrome.
	SetViewColor(255, 255, 255);
}


BWebContentView::~BWebContentView()
{
}


void
BWebContentView::SetListener(const BMessenger& listener)
{
	fListener = listener;
}


status_t
BWebContentView::Report(BMessage* message)
{
	if (message == NULL)
		return B_BAD_VALUE;
	if (!fListener.IsValid())
		return B_NO_INIT;
	return fListener.SendMessage(message);
}




status_t
BWebContentView::SetColorScheme(bool dark)
{
	(void)dark;
	return B_NOT_SUPPORTED;
}


status_t
BWebContentView::SetScale(float scale)
{
	(void)scale;
	return B_NOT_SUPPORTED;
}


status_t
aomi_engine_start(const char* engine)
{
	if (engine == NULL || strcmp(engine, "Servo") == 0)
		return aomi_servo_start();
	return B_NOT_SUPPORTED;
}


BWebContentView*
create_aomi_content_view(const char* engine, const char* name,
	BWebSession* session)
{
	if (engine == NULL || strcmp(engine, "Servo") == 0)
		return new BServoContentView(name, session);
	return NULL;
}
