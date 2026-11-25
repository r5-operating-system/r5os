/*
 * Copyright 2001-2016, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 * 		Christian Packmann
 */


#include "AppServer.h"

#include <string.h>
#include <syslog.h>

#include <OS.h>

#include <Autolock.h>

#include <AutoDeleter.h>
#include <LaunchRoster.h>
#include <PortLink.h>
#include <RosterPrivate.h>

#include "BitmapManager.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "Compositor.h"
#include <R5Admin.h>
// SPDX-SnippetEnd
#include "Desktop.h"
#include "GlobalFontManager.h"
#include "InputManager.h"
#include "ScreenManager.h"
#include "ServerProtocol.h"


//#define DEBUG_SERVER
#ifdef DEBUG_SERVER
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


// Globals
port_id gAppServerPort;
BTokenSpace gTokenSpace;
uint32 gAppServerSIMDFlags = 0;


/*!	\brief Constructor

	This loads the default fonts, allocates all the major global variables,
	spawns the main housekeeping threads, loads user preferences for the UI
	and decorator, and allocates various locks.
*/
AppServer::AppServer(status_t* status)
	:
	SERVER_BASE("application/x-vnd.Haiku-app_server", "picasso", -1, false,
		status),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fDesktopLock("AppServerDesktopLock"),
	fAdminPort(-1)
// SPDX-SnippetEnd
{
	openlog("app_server", 0, LOG_DAEMON);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	r5_admin_port_start("app_server admin", &_AdminHandler, this, &fAdminPort);
// SPDX-SnippetEnd

	gInputManager = new InputManager();

	// Create the font server and scan the proper directories.
	gFontManager = new GlobalFontManager;
	if (gFontManager->InitCheck() != B_OK)
		debugger("font manager could not be initialized!");

	gFontManager->Run();

	gScreenManager = new ScreenManager();
	gScreenManager->Run();

	// Create the bitmap allocator. Object declared in BitmapManager.cpp
	gBitmapManager = new BitmapManager();

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#if 0
	// This is not presently needed, as app_server is launched from the login session.
	// TODO: check the attached displays, and launch login session for them
	BMessage data;
	data.AddString("name", "app_server");
	data.AddInt32("session", 0);
	BLaunchRoster().Target("login", data);
#endif

	// Inform the registrar we've (re)started.
	BMessage request(kMsgAppServerStarted);
	BRoster::Private().SendTo(&request, NULL, false);
#endif
}


/*!	\brief Destructor
	Reached only when the server is asked to shut down in Test mode.
*/
AppServer::~AppServer()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (fAdminPort >= 0) {
		r5_admin_port_stop(fAdminPort);
		fAdminPort = -1;
	}
// SPDX-SnippetEnd
	delete gBitmapManager;

	gScreenManager->Lock();
	gScreenManager->Quit();

	gFontManager->Lock();
	gFontManager->Quit();

	closelog();
}


void
AppServer::MessageReceived(BMessage* message)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (r5_admin_port_reply(message, fAdminPort))
		return;
// SPDX-SnippetEnd
	switch (message->what) {
		case AS_GET_DESKTOP:
		{
			Desktop* desktop = NULL;

			int32 userID = message->GetInt32("user", 0);
			int32 version = message->GetInt32("version", 0);
			const char* targetScreen = message->GetString("target");

			if (version != AS_PROTOCOL_VERSION) {
				// SPDX-SnippetBegin
				// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
				// SPDX-License-Identifier: MIT
				// Name the sender, so a refused program can be found in the log.
				team_id team = message->ReturnAddress().Team();
				team_info info;
				if (get_team_info(team, &info) != B_OK)
					strlcpy(info.args, "?", sizeof(info.args));
				syslog(LOG_ERR, "Application for user %" B_PRId32 " does not "
					"support the current server protocol (%" B_PRId32 "), "
					"team %" B_PRId32 " (%s).\n", userID, version, team,
					info.args);
				// SPDX-SnippetEnd
			} else {
				desktop = _FindDesktop(userID, targetScreen);
				if (desktop == NULL) {
					// One graphical session until StartSession. Tracker
					// after Sign in is a person uid; keep it on the
					// screen already owned by Login (uid 0).
					{
						BAutolock locker(fDesktopLock);
						for (int32 i = 0; i < fDesktops.CountItems();
							i++) {
							Desktop* existing = fDesktops.ItemAt(i);
							if ((existing->TargetScreen() == NULL
									&& targetScreen == NULL)
								|| (existing->TargetScreen() != NULL
									&& targetScreen != NULL
									&& strcmp(existing->TargetScreen(),
										targetScreen) == 0)) {
								desktop = existing;
								break;
							}
						}
						if (desktop == NULL && fDesktops.CountItems() > 0)
							desktop = fDesktops.ItemAt(0);
					}
				}
				if (desktop == NULL) {
					// we need to create a new desktop object for this user
					// TODO: test if the user exists on the system
					// TODO: maybe have a separate AS_START_DESKTOP_SESSION for
					// authorizing the user
					desktop = _CreateDesktop(userID, targetScreen);
				}
			}

			BMessage reply;
			if (desktop != NULL)
				reply.AddInt32("port", desktop->MessagePort());
			else
				reply.what = (uint32)B_ERROR;

			message->SendReply(&reply);
			break;
		}

		default:
			// We don't allow application scripting
			STRACE(("AppServer received unexpected code %" B_PRId32 "\n",
				message->what));
			break;
	}
}


bool
AppServer::QuitRequested()
{
#if TEST_MODE
	while (fDesktops.CountItems() > 0) {
		Desktop *desktop = fDesktops.RemoveItemAt(0);

		thread_id thread = desktop->Thread();
		desktop->PostMessage(B_QUIT_REQUESTED);

		// we just wait for the desktop to kill itself
		status_t status;
		wait_for_thread(thread, &status);
	}

	delete this;
	exit(0);

	return SERVER_BASE::QuitRequested();
#else
	return false;
#endif

}


/*!	\brief Creates a desktop object for an authorized user
*/
Desktop*
AppServer::_CreateDesktop(uid_t userID, const char* targetScreen,
	bool startHidden)
{
	BAutolock locker(fDesktopLock);
	ObjectDeleter<Desktop> desktop;
	try {
		desktop.SetTo(new Desktop(userID, targetScreen));

		status_t status = desktop->Init(startHidden);
		if (status == B_OK)
			status = desktop->Run();
		if (status == B_OK && !fDesktops.AddItem(desktop.Get()))
			status = B_NO_MEMORY;

		if (status != B_OK) {
			syslog(LOG_ERR, "Cannot initialize Desktop object: %s\n",
				strerror(status));
			return NULL;
		}
	} catch (...) {
		// there is obviously no memory left
		return NULL;
	}

	return desktop.Detach();
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
status_t
AppServer::_ActivateDesktop(Desktop* target)
{
	if (target == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fDesktopLock);
	if (!fDesktops.HasItem(target))
		return B_NAME_NOT_FOUND;
	if (target->IsShown())
		return B_OK;

	Desktop* current = NULL;
	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);
		if (desktop->TargetScreen() == NULL && desktop->IsShown()) {
			current = desktop;
			break;
		}
	}

	if (current != NULL && current != target) {
		status_t status = current->Deactivate();
		if (status != B_OK)
			return status;
	}

	status_t status = target->Activate();
	if (status != B_OK && current != NULL && current != target) {
		// Roll back to the previously shown desktop if activation failed.
		current->Activate();
	}
	return status;
}


status_t
AppServer::_DestroyDesktop(uid_t userID)
{
	if (userID == 0)
		return B_PERMISSION_DENIED;

	thread_id thread = -1;
	{
		BAutolock locker(fDesktopLock);
		for (int32 i = 0; i < fDesktops.CountItems(); i++) {
			Desktop* item = fDesktops.ItemAt(i);
			if (item->UserID() == userID && item->TargetScreen() == NULL) {
				if (item->IsShown())
					return B_BUSY;
				fDesktops.RemoveItemAt(i);
				thread = item->Thread();
				// Desktop deletes itself on its own looper thread via
				// B_QUIT_REQUESTED -> kMsgQuitLooper -> MessageLooper::Quit()
				// -> delete this. Do not dereference or delete item after
				// posting B_QUIT_REQUESTED.
				item->PostMessage(B_QUIT_REQUESTED);
				break;
			}
		}
	}

	if (thread < 0)
		return B_NAME_NOT_FOUND;

	// Bounded wait policy: wait up to 5 s for the desktop thread to exit so
	// normal teardown finishes before returning B_OK, without blocking the
	// admin port forever if a client app refuses to quit. On timeout, return
	// B_TIMED_OUT and let the desktop finish quitting in the background.
	const bigtime_t kDestroyTimeout = 5000000;
	bigtime_t deadline = system_time() + kDestroyTimeout;
	thread_info info;
	while (get_thread_info(thread, &info) == B_OK) {
		if (system_time() >= deadline)
			return B_TIMED_OUT;
		snooze(50000);
	}
	return B_OK;
}


void
AppServer::_FillDesktopInfo(BMessage* reply)
{
	BAutolock locker(fDesktopLock);
	int32 activeUser = -1;
	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);
		if (desktop->TargetScreen() != NULL)
			continue;
		if (desktop->IsShown())
			activeUser = (int32)desktop->UserID();
		int32 workspace = 0;
		int32 count = 0;
		int32 columns = 0;
		int32 rows = 0;
		int64 windowBytes = 0;
		if (desktop->LockSingleWindow()) {
			DesktopSettings settings(desktop);
			workspace = desktop->CurrentWorkspace();
			count = settings.WorkspacesCount();
			columns = settings.WorkspacesColumns();
			rows = settings.WorkspacesRows();
			Compositor* compositor = desktop->GetCompositor();
			windowBytes = compositor != NULL
				? (int64)compositor->WindowBytes() : 0;
			desktop->UnlockSingleWindow();
		}
		reply->AddInt32("user", (int32)desktop->UserID());
		reply->AddBool("shown", desktop->IsShown());
		reply->AddInt32("workspace", workspace);
		reply->AddInt32("workspaces_count", count);
		reply->AddInt32("workspaces_columns", columns);
		reply->AddInt32("workspaces_rows", rows);
		reply->AddInt64("window_bytes", windowBytes);
	}
	reply->AddInt32("active_user", activeUser);
	reply->AddInt64("total_window_bytes",
		(int64)Compositor::TotalWindowBytes());
}


/*static*/ void
AppServer::_AdminHandler(BMessage* request, const r5_admin_sender& sender,
	BMessage* reply, void* cookie)
{
	((AppServer*)cookie)->_HandleAdminRequest(request, sender, reply);
}


void
AppServer::_HandleAdminRequest(BMessage* request,
	const r5_admin_sender& sender, BMessage* reply)
{
	// Only uid 0 (Login / privileged tools) may manage desktops.
	if (sender.uid != 0) {
		reply->what = (uint32)B_PERMISSION_DENIED;
		return;
	}

	switch (request->what) {
		case AS_R5_CREATE_DESKTOP:
		{
			int32 userID = -1;
			if (request->FindInt32("user", &userID) != B_OK || userID < 0) {
				reply->what = (uint32)B_BAD_VALUE;
				return;
			}
			bool activate = request->GetBool("activate", false);
			Desktop* desktop = _FindDesktop((uid_t)userID, NULL);
			if (desktop == NULL) {
				bool startHidden = true;
				{
					BAutolock locker(fDesktopLock);
					if (fDesktops.CountItems() == 0)
						startHidden = false;
				}
				desktop = _CreateDesktop((uid_t)userID, NULL, startHidden);
			}
			if (desktop == NULL) {
				reply->what = (uint32)B_ERROR;
				return;
			}
			status_t status = B_OK;
			if (activate && !desktop->IsShown())
				status = _ActivateDesktop(desktop);
			reply->what = (uint32)status;
			if (status == B_OK)
				reply->AddInt32("port", desktop->MessagePort());
			break;
		}

		case AS_R5_ACTIVATE_DESKTOP:
		{
			int32 userID = -1;
			if (request->FindInt32("user", &userID) != B_OK || userID < 0) {
				reply->what = (uint32)B_BAD_VALUE;
				return;
			}
			Desktop* desktop = _FindDesktop((uid_t)userID, NULL);
			if (desktop == NULL) {
				reply->what = (uint32)B_NAME_NOT_FOUND;
				return;
			}
			reply->what = (uint32)_ActivateDesktop(desktop);
			break;
		}

		case AS_R5_DESTROY_DESKTOP:
		{
			int32 userID = -1;
			if (request->FindInt32("user", &userID) != B_OK || userID < 0) {
				reply->what = (uint32)B_BAD_VALUE;
				return;
			}
			reply->what = (uint32)_DestroyDesktop((uid_t)userID);
			break;
		}

		case AS_R5_DESKTOP_INFO:
		{
			_FillDesktopInfo(reply);
			reply->what = (uint32)B_OK;
			break;
		}

		default:
			reply->what = (uint32)B_BAD_VALUE;
			break;
	}
}
// SPDX-SnippetEnd


/*!	\brief Finds the desktop object that belongs to a certain user
*/
Desktop*
AppServer::_FindDesktop(uid_t userID, const char* targetScreen)
{
	BAutolock locker(fDesktopLock);

	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);

		if (desktop->UserID() == userID
			&& ((desktop->TargetScreen() == NULL && targetScreen == NULL)
				|| (desktop->TargetScreen() != NULL && targetScreen != NULL
					&& strcmp(desktop->TargetScreen(), targetScreen) == 0))) {
			return desktop;
		}
	}

	return NULL;
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	srand(real_time_clock_usecs());

	status_t status;
	AppServer* server = new AppServer(&status);
	if (status == B_OK)
		server->Run();

	return status == B_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
