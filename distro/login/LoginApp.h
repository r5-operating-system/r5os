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
// Fast user switching (part 2):
const uint32 kSwitchUserAction = 'swtu';
const uint32 kQuerySessionsAction = 'qses';

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
	void			Logout(uid_t targetUid = 0);
	void			Lock(uid_t targetUid = 0);
	void			SwitchUser();
	void			Unlock(BMessage* message);
	void			_CloseLock();
	void			HandleAdmin(BMessage* message);
	status_t		ValidateLogin(const char* login, const char* password);
	void			_EnableDesktopJobs();
	status_t		_LaunchSessionAppFor(const char* login,
						const char* program,
						const char* argument = NULL,
						const char* argument2 = NULL,
						const char* argument3 = NULL,
						const char* argument4 = NULL);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	status_t		_LaunchSessionAppGetTeamFor(const char* login,
						const char* program, team_id* _outTeam);
	void			_StartAndUnlockKeyStore(const char* login,
						const char* password);
	void			_StopKeyStoreOfUid(uid_t uid);
	status_t		_AppServerCreateDesktop(uid_t uid, bool activate);
	status_t		_AppServerActivateDesktop(uid_t uid);
	status_t		_AppServerDestroyDesktop(uid_t uid);
	void			_SetActiveAudioUid(uid_t uid);
	void			_NotifySessionVisible(uid_t uid, bool visible);
	status_t		_WaitSessionTeamFor(const char* signature, uid_t uid);
	void			_QuitUserAppsFor(uid_t uid);
	int32			_CountUserApps(uid_t uid) const;
	void			_FillOtherSessions(uid_t excludeUid,
						BMessage& reply) const;
	bool			_ConfirmShutdownIfOthers(bool reboot, uid_t requesterUid,
						bool requesterIsAdmin);
	void			_PerformShutdown(bool reboot);
// SPDX-SnippetEnd
	void			_LaunchSessionHooks(const char* login);
	void			_StartVirtualBoxGuest(const char* login);
	status_t		_SessionHomeFor(const char* login, BPath& path) const;
	status_t		_WaitRunning(const char* signature);
	void			_HideLogin();
	void			_ShowLogin();
	void			_OpenLoginWindow();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	status_t		_StartHomeWalk(BMessage* message, uid_t uid,
						gid_t gid);
	void			_HomeWalkDone(BMessage* message);
// SPDX-SnippetEnd
	status_t		_WaitGoneFor(const char* signature, uid_t uid);

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
	// Fast user switching: currently shown desktop uid (0 = Login desktop)
	// and the login name shown on fLockWindow.
	uid_t			fActiveUid;
	BString			fLockedLogin;
// SPDX-SnippetEnd
};


#endif	// _LOGINAPP_H_
