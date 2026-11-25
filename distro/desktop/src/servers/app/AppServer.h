/*
 * Copyright 2001-2015, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef	APP_SERVER_H
#define	APP_SERVER_H


#include <Application.h>
#include <List.h>
#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>
#include <String.h>
#include <Window.h>

#include "MessageLooper.h"
#include "ServerConfig.h"


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	include <Server.h>
#	define SERVER_BASE BServer
#else
#	include "TestServerLoopAdapter.h"
#	define SERVER_BASE TestServerLoopAdapter
#endif


class ServerApp;
class BitmapManager;
class Desktop;


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
struct r5_admin_sender;
// SPDX-SnippetEnd


class AppServer : public SERVER_BASE {
public:
								AppServer(status_t* status);
	virtual						~AppServer();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			Desktop*			_CreateDesktop(uid_t userID,
									const char* targetScreen,
									bool startHidden = false);
			status_t			_ActivateDesktop(Desktop* target);
			status_t			_DestroyDesktop(uid_t userID);
			void				_FillDesktopInfo(BMessage* reply);
	static	void				_AdminHandler(BMessage* request,
									const r5_admin_sender& sender,
									BMessage* reply, void* cookie);
			void				_HandleAdminRequest(BMessage* request,
									const r5_admin_sender& sender,
									BMessage* reply);
// SPDX-SnippetEnd
	virtual	Desktop*			_FindDesktop(uid_t userID,
									const char* targetScreen);

			void				_LaunchInputServer();

private:
			BObjectList<Desktop> fDesktops;
			BLocker				fDesktopLock;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			port_id				fAdminPort;
// SPDX-SnippetEnd
};


extern BitmapManager *gBitmapManager;
extern port_id gAppServerPort;


#endif	/* APP_SERVER_H */
