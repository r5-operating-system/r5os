/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LOGINAPP_H_
#define _LOGINAPP_H_


#include <Application.h>
#include <Path.h>


const uint32 kAttemptLogin = 'logi';
const uint32 kHaltAction = 'halt';
const uint32 kRebootAction = 'rebo';
const uint32 kSuspendAction = 'susp';
const uint32 kLogoutAction = 'lout';
const uint32 kLockAction = 'lokc';
const uint32 kUnlockAction = 'unlk';
const uint32 kLoginBad = 'lgba';
const uint32 kLoginOk = 'lgok';
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Issue #4644: Sign in shows how far the first walk of /boot/home is
// (string "text"); the walk thread tells Login when it is done.
const uint32 kLoginProgress = 'lgpr';
const uint32 kHomeWalkDone = 'lghw';

// Issue #4639: a message from Login's own windows. Every message that
// signs in, unlocks, locks, logs out, restarts or shuts down carries
// Login's token (r5_admin_mark_local()); another team cannot make one.
BMessage* login_local_message(uint32 what);
// SPDX-SnippetEnd


class LoginWindow;
class DesktopWindow;
class LockWindow;


class LoginApp : public BApplication {
public:
					LoginApp();
	virtual			~LoginApp();
	void			ReadyToRun();
	void			MessageReceived(BMessage* message);
	void			ArgvReceived(int32 argc, char** argv);

private:
	void			TryLogin(BMessage* message);
	void			Logout();
	void			Lock();
	void			Unlock(BMessage* message);
	void			_CloseLock();
	void			HandleAdmin(BMessage* message);
	status_t		ValidateLogin(const char* login, const char* password);
	void			_EnableDesktopJobs();
	status_t		_LaunchSessionApp(const char* program,
						const char* argument = NULL,
						const char* argument2 = NULL,
						const char* argument3 = NULL,
						const char* argument4 = NULL);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	status_t		_LaunchSessionAppGetTeam(const char* program,
						team_id* _outTeam);
	void			_StartAndUnlockKeyStore(const char* login,
						const char* password);
// SPDX-SnippetEnd
	void			_LaunchSessionHooks();
	void			_StartVirtualBoxGuest();
	status_t		_SessionHome(BPath& path) const;
	status_t		_WaitRunning(const char* signature);
	status_t		_WaitSessionTeam(const char* signature);
	void			_HideLogin();
	void			_ShowLogin();
	void			_OpenLoginWindow();
	void			_QuitUserApps();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	status_t		_StartHomeWalk(BMessage* message, uid_t uid,
						gid_t gid);
	void			_HomeWalkDone(BMessage* message);
// SPDX-SnippetEnd
	status_t		_WaitGone(const char* signature);

	DesktopWindow*	fDesktopWindow;
	LoginWindow*	fLoginWindow;
	LockWindow*		fLockWindow;
	bool			fEditShelfMode;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Requests that change the machine: on this port the kernel names
	// the sender (libr5admin.so).
	static	void	_AdminRequest(BMessage* request,
						const struct r5_admin_sender& sender, BMessage* reply,
						void* cookie);
	port_id			fAdminPort;
	// The sign-in that waits for the first walk of /boot/home.
	thread_id		fHomeWalkThread;
	BMessage*		fPendingLogin;
// SPDX-SnippetEnd
};


#endif	// _LOGINAPP_H_
