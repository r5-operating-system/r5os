/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginApp.h"

#include "AccountAdmin.h"
#include "DesktopWindow.h"
#include "DriverAdmin.h"
#include "DriverSettingsAdmin.h"
#include "HomeLock.h"
#include "LockWindow.h"
#include "LoginWindow.h"
#include "SessionLog.h"

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <R5Admin.h>
#include <R5Security.h>
#include "KeyStoreCrypto.h"
#include "KeyStoreProtocol.h"
// SPDX-SnippetEnd

#include <Alert.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <Catalog.h>
// SPDX-SnippetEnd
#include <FindDirectory.h>
#include <LaunchRoster.h>
#include <List.h>
#include <OS.h>
#include <Path.h>
#include <Roster.h>
#include <RosterPrivate.h>
#include <Screen.h>
#include <String.h>
#include <StringList.h>

#include <errno.h>
#include <fcntl.h>
#include <image.h>
#include <new>
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Login: Application"
// SPDX-SnippetEnd

extern char** environ;

const char* kLoginAppSig = "application/x-vnd.Haiku-Login";


static bool
keep_team(const app_info& info, team_id self)
{
	if (info.team == self)
		return true;
	if ((info.flags & B_BACKGROUND_APP) != 0)
		return true;

	const char* signature = info.signature;
	if (signature == NULL || signature[0] == '\0')
		return true;
	if (strcmp(signature, kLoginAppSig) == 0)
		return true;
	if (strcmp(signature, "application/x-vnd.Be-TRAK") == 0)
		return true;
	if (strcmp(signature, "application/x-vnd.Be-TSKB") == 0)
		return true;
	if (strcmp(signature, "application/x-vnd.R5OS-airplay") == 0)
		return true;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// keystore_server runs per-session as the signed-in account and must stop
	// at Log out so its master key is wiped from memory.
	if (strcmp(signature, "application/x-vnd.Haiku-keystore_server") == 0)
		return false;
// SPDX-SnippetEnd
	if (strstr(signature, "server") != NULL
		|| strstr(signature, "daemon") != NULL
		|| strstr(signature, "launch") != NULL
		|| strstr(signature, "registrar") != NULL
		|| strstr(signature, "ROST") != NULL)
		return true;

	BPath path(&info.ref);
	if (path.InitCheck() == B_OK && path.Path() != NULL
		&& strstr(path.Path(), "/servers/") != NULL)
		return true;
	return false;
}


static void
stop_team_named(const char* needle)
{
	int32 cookie = 0;
	team_info info;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		if (strstr(info.args, needle) == NULL)
			continue;
		send_signal(info.team, SIGTERM);
		r5_session_log("signal %s team %d", needle, (int)info.team);
	}
}


static bool
team_named(const char* needle)
{
	int32 cookie = 0;
	team_info info;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		if (strstr(info.args, needle) != NULL)
			return true;
	}
	return false;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static void stop_teams_of_uid(uid_t uid);
// SPDX-SnippetEnd


static const char* kSessionJobs[] = {
	"x-vnd.Be-TRAK",
	"x-vnd.Be-TSKB",
	"x-vnd.R5OS-NetworkStatus",
	"x-vnd.R5OS-airplay-tray",
	"x-vnd.R5OS-vbox-clip",
	NULL
};


static bool
is_session_job(const char* name)
{
	if (name == NULL)
		return false;
	for (int32 i = 0; kSessionJobs[i] != NULL; i++) {
		if (strcmp(name, kSessionJobs[i]) == 0)
			return true;
	}
	return strstr(name, "Be-TRAK") != NULL
		|| strstr(name, "Be-TSKB") != NULL
		|| strstr(name, "NetworkStatus") != NULL
		|| strstr(name, "airplay-tray") != NULL
		|| strstr(name, "vbox-clip") != NULL;
}


LoginApp::LoginApp()
	:
	BApplication(kLoginAppSig),
	fDesktopWindow(NULL),
	fLoginWindow(NULL),
	fLockWindow(NULL),
	fEditShelfMode(false),
	fAdminPort(-1),
	fHomeWalkThread(-1),
	fPendingLogin(NULL)
{
}


LoginApp::~LoginApp()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (fAdminPort >= 0)
		r5_admin_port_stop(fAdminPort);
// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Every request that changes the machine or an account comes here, on the
// admin port's thread, with the sender the kernel named. The normal
// messenger only answers the query (AccountAdmin.cpp): its reply address is
// whatever the sender chose.
void
LoginApp::_AdminRequest(BMessage* request, const r5_admin_sender& sender,
	BMessage* reply, void* cookie)
{
	LoginApp* app = (LoginApp*)cookie;
	// BLooper's lock: LoginApp::Lock() locks the screen.
	if (!app->BLooper::Lock())
		return;
	set_admin_sender(true, sender.uid, sender.team);
	switch (request->what) {
		case kMsgAdminAdd:
		case kMsgAdminDelete:
		case kMsgAdminSetDisabled:
		case kMsgAdminQuery:
		case kMsgAdminUnlock:
		case kMsgAdminRelock:
		case kMsgAdminSetClock:
		case kMsgAdminSetTimezone:
		case kMsgAdminSetClockGmt:
		case kMsgAdminGetProfile:
		case kMsgAdminSetProfile:
		case kMsgAdminListAccounts:
		case kMsgAdminSetAccountType:
		case kMsgAdminGetGuestEnabled:
		case kMsgAdminSetGuestEnabled:
			handle_admin_message(request, *reply);
			break;
		case kMsgAdminSetDriverBlocked:
			handle_driver_admin_message(request, *reply);
			break;
		case kMsgAdminSetDriverSetting:
			handle_driver_settings_admin_message(request, *reply);
			break;
		case kLogoutAction:
		case kLockAction:
		{
			BString sessionUser;
			bool hasSession = signed_in_name(sessionUser) == B_OK;
			bool allowed = (sender.uid == 0);
			if (!allowed && hasSession) {
				struct passwd* pwd = getpwnam(sessionUser.String());
				if (pwd != NULL && pwd->pw_uid == sender.uid)
					allowed = true;
			}
			if (!allowed) {
				reply->AddString("text", B_TRANSLATE("Only the signed-in "
					"session may lock or log out."));
				reply->AddInt32("error", B_PERMISSION_DENIED);
				break;
			}
			if (request->what == kLockAction && hasSession
				&& is_guest_account(sessionUser.String())) {
				reply->AddString("text",
					B_TRANSLATE("Guest sessions cannot be locked."));

				reply->AddInt32("error", B_NOT_ALLOWED);
				break;
			}
			reply->AddInt32("error", B_OK);
			// Issue #4639: Deskbar's Lock screen and Log out arrive here,
			// with the uid the kernel named. MessageReceived() only takes
			// Login's own (marked) messages.
			BMessage* action = login_local_message(request->what);
			app->PostMessage(action);
			delete action;
			break;
		}
		default:
			reply->AddString("text", B_TRANSLATE("Unknown request."));
			reply->AddInt32("error", B_BAD_VALUE);
			break;
	}
	set_admin_sender(false, (uid_t)-1, -1);
	app->BLooper::Unlock();
}
// SPDX-SnippetEnd



void
LoginApp::ReadyToRun()
{
	BScreen screen;

	if (fEditShelfMode) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		BAlert* alert = new BAlert(B_TRANSLATE("Info"),
			B_TRANSLATE("Drop replicants on this desktop to customize the "
				"login background. Quit with Cmd-Q when you are finished."),
			B_TRANSLATE("OK"));
// SPDX-SnippetEnd
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
		fDesktopWindow = new DesktopWindow(screen.Frame(), true);
		fDesktopWindow->Show();
		return;
	}

	lock_private_homes();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	ensure_shared_folder();
	// A restarted Login must not wipe a guest session that still runs; its
	// home is wiped at that session's log out.
	if (!guest_session_active())
		wipe_guest_home();
// SPDX-SnippetEnd
	refresh_machine_settings_access();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (fAdminPort < 0
		&& r5_admin_port_start("r5 login admin", &LoginApp::_AdminRequest,
			this, &fAdminPort) != B_OK) {
		fAdminPort = -1;
		syslog(LOG_ERR, "Login: no admin port, settings cannot be changed");
	}
// SPDX-SnippetEnd

	fDesktopWindow = new DesktopWindow(screen.Frame(), false);
	fDesktopWindow->Show();
	_OpenLoginWindow();
}


void
LoginApp::MessageReceived(BMessage* message)
{
	bool reboot = false;

	switch (message->what) {
		case kAttemptLogin:
		case kLogoutAction:
		case kLockAction:
		case kUnlockAction:
		case kRebootAction:
		case kHaltAction:
		case kSuspendAction:
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// The login window is this team. A BMessenger(Login) from any
			// other team used to halt, lock, or sign in. !IsSourceRemote()
			// was not enough: the sender chooses the reply address, also
			// Login's own (issue #4639). Only Login's own windows and its
			// admin port (lock and log out from the signed-in person) make
			// a message with Login's token.
			if (!r5_admin_is_local(message)) {
				syslog(LOG_WARNING, "Login: refused 0x%08" B_PRIx32
					" from another team", message->what);
				break;
			}
			// SPDX-SnippetEnd
			if (message->what == kAttemptLogin) {
				TryLogin(message);
				break;
			}
			if (message->what == kLogoutAction) {
				Logout();
				break;
			}
			if (message->what == kLockAction) {
				Lock();
				break;
			}
			if (message->what == kUnlockAction) {
				Unlock(message);
				break;
			}
			if (message->what == kSuspendAction) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
				BAlert* alert = new BAlert(B_TRANSLATE("Error"),
					B_TRANSLATE("Sleep is not available yet."),
					B_TRANSLATE("OK"));
// SPDX-SnippetEnd
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
				break;
			}
			reboot = message->what == kRebootAction;
			{
				BRoster roster;
				BRoster::Private rosterPrivate(roster);
				status_t error = rosterPrivate.ShutDown(reboot, false, false);
				if (error < B_OK) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
					BString msg(B_TRANSLATE("Could not shut down: %error%"));
					msg.ReplaceFirst("%error%", strerror(error));
					BAlert* alert = new BAlert(B_TRANSLATE("Error"),
						msg.String(), B_TRANSLATE("OK"));
// SPDX-SnippetEnd
					alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
					alert->Go();
				}
			}
			break;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case R5_ADMIN_PORT_REQUEST:
			r5_admin_port_reply(message, fAdminPort);
			break;
		case kHomeWalkDone:
			if (r5_admin_is_local(message))
				_HomeWalkDone(message);
			break;
// SPDX-SnippetEnd

		case kMsgAdminAdd:
		case kMsgAdminDelete:
		case kMsgAdminSetDisabled:
		case kMsgAdminQuery:
		case kMsgAdminUnlock:
		case kMsgAdminRelock:
		case kMsgAdminSetClock:
		case kMsgAdminSetTimezone:
		case kMsgAdminSetClockGmt:
		case kMsgAdminGetProfile:
		case kMsgAdminSetProfile:
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case kMsgAdminListAccounts:
		case kMsgAdminSetAccountType:
		case kMsgAdminGetGuestEnabled:
		case kMsgAdminSetGuestEnabled:
// SPDX-SnippetEnd
			// New account messages must be listed here. An unhandled
			// message still gets an empty reply, and Users treats a missing
			// "error" field as failure.
			HandleAdmin(message);
			break;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case kMsgAdminSetDriverBlocked:
		{
			// Device Manager turns a driver off or on (DriverAdmin.cpp).
			BMessage reply;
			handle_driver_admin_message(message, reply);
			message->SendReply(&reply);
			break;
		}
		case kMsgAdminSetDriverSetting:
		{
			// Media writes a driver setting (DriverSettingsAdmin.cpp).
			BMessage reply;
			handle_driver_settings_admin_message(message, reply);
			message->SendReply(&reply);
			break;
		}
// SPDX-SnippetEnd
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
LoginApp::ArgvReceived(int32 argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		BString arg(argv[i]);
		if (arg == "--edit")
			fEditShelfMode = true;
		else {
			fprintf(stderr, "Login [--edit]\n");
			exit(arg == "--help" ? 0 : 1);
		}
	}
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Accounts step 5: wipe the password fields of a sign-in message when
// TryLogin() returns, on every path.
struct SignInMessageWiper {
	SignInMessageWiper(BMessage* message)
		:
		fMessage(message),
		fKeep(false)
	{
	}

	// The sign-in goes on later with this message (issue #4644): wipe
	// it then, not now.
	void Keep()
	{
		fKeep = true;
	}

	~SignInMessageWiper()
	{
		if (fKeep)
			return;
		static const char* kFields[] = { "password", "new password", NULL };
		for (int32 i = 0; kFields[i] != NULL; i++) {
			const void* data = NULL;
			ssize_t size = 0;
			if (fMessage->FindData(kFields[i], B_STRING_TYPE, &data, &size)
					== B_OK && data != NULL && size > 0) {
				BSecurityKit::BSecureBuffer::WipeMemory(
					const_cast<void*>(data), static_cast<size_t>(size));
			}
		}
	}

	BMessage* fMessage;
	bool fKeep;
};
// SPDX-SnippetEnd


void
LoginApp::TryLogin(BMessage* message)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	SignInMessageWiper wiper(message);
// SPDX-SnippetEnd
	BMessage reply(kLoginBad);
	status_t status = B_BAD_VALUE;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (fHomeWalkThread >= 0 && message != fPendingLogin) {
		reply.AddInt32("error", B_BUSY);
		reply.AddString("text",
			B_TRANSLATE("Your files are still being prepared."));
		message->SendReply(&reply);
		return;
	}
// SPDX-SnippetEnd

	const char* login = NULL;
	if (message->FindString("login", &login) == B_OK) {
		const char* password = message->GetString("password");
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// The sign-in that waited for the walk (issue #4644): a first
		// password was already set from "new password".
		const char* firstPassword = message->GetString("new password", NULL);
		if (message == fPendingLogin && firstPassword != NULL
			&& firstPassword[0] != '\0')
			password = firstPassword;
// SPDX-SnippetEnd
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// Accounts step 5: on an installed R5 an account without a password
		// sets one (and its name) before its first desktop. Try R5 keeps the
		// account without a password.
		if (needs_first_password(login)) {
			const char* newPassword = message->GetString("new password",
				NULL);
			if (newPassword == NULL || newPassword[0] == '\0') {
				reply.AddInt32("error", B_NOT_ALLOWED);
				reply.AddBool("needs password", true);
				reply.AddString("text", B_TRANSLATE("Choose a password first."));
				message->SendReply(&reply);
				return;
			}
			BString error;
			status_t setStatus = set_first_password(login,
				message->GetString("real name", NULL), newPassword, error);
			if (setStatus != B_OK) {
				reply.AddInt32("error", setStatus);
				reply.AddBool("needs password", true);
				reply.AddString("text", error.String());
				message->SendReply(&reply);
				return;
			}
			r5_session_log("first password set for %s", login);
			password = newPassword;
		}
// SPDX-SnippetEnd
		status = ValidateLogin(login, password);
		if (status == B_OK) {
			status = migrate_admin_off_root();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// Issue #4644: the first walk of /boot/home runs on its own
			// thread; this sign-in goes on when it is done
			// (_HomeWalkDone()), before anything uses the person's files.
			uid_t walkUid = 0;
			gid_t walkGid = 0;
			if (status == B_OK && message != fPendingLogin
				&& first_home_walk_needed(walkUid, walkGid)) {
				status = _StartHomeWalk(message, walkUid, walkGid);
				if (status == B_OK) {
					wiper.Keep();
					return;
				}
			}
			if (status == B_OK && is_guest_account(login))
				status = wipe_guest_home();
// SPDX-SnippetEnd
			bool wroteSession = false;
			if (status == B_OK) {
				status = write_desktop_session(login);
				wroteSession = status == B_OK;
			}
			if (status == B_OK) {
				r5_session_log("sign-in %s", login);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
				_StartAndUnlockKeyStore(login, password);
// SPDX-SnippetEnd
				_EnableDesktopJobs();
				status = _WaitGone("application/x-vnd.Be-TRAK");
				if (status == B_OK)
					status = _WaitGone("application/x-vnd.Be-TSKB");
				if (status == B_OK)
					status = _LaunchSessionApp("/system/Tracker");
				if (status == B_OK)
					status = _LaunchSessionApp("/system/Deskbar");
				if (status == B_OK)
					status = _WaitSessionTeam("application/x-vnd.Be-TRAK");
				if (status == B_OK)
					status = _WaitSessionTeam("application/x-vnd.Be-TSKB");
				// Do not Target("desktop"). That asks launch_daemon user,
				// still uid 0 from autologin, to start Tracker again.
				if (status == B_OK) {
					_LaunchSessionApp("/system/apps/r5-tray");
					_LaunchSessionApp("/system/bin/desklink",
						"--add-volume");
					_LaunchSessionApp("/system/servers/airplay_deskbar");
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
					// The Bluetooth item (regular image only). Its launch job
					// under target desktop never ran: Sign in does not start
					// that target (5625U, 16 Nov 2026).
					if (access("/system/servers/bluetooth_deskbar", X_OK) == 0) {
						_LaunchSessionApp(
							"/system/servers/bluetooth_deskbar");
					}
// SPDX-SnippetEnd
					_LaunchSessionApp("/system/servers/r5_vbox_clip");
					_StartVirtualBoxGuest();
					_LaunchSessionHooks();
				}
				r5_session_log("sign-in done: %s", strerror(status));
			}
			if (status == B_OK)
				_HideLogin();
			else if (wroteSession)
				clear_desktop_session();
		}
	}

	if (status == B_OK) {
		reply.what = kLoginOk;
		message->SendReply(&reply);
	} else {
		reply.AddInt32("error", status);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		if (status == B_PERMISSION_DENIED) {
			const char* entered = message->GetString("password");
			if (entered == NULL || entered[0] == '\0') {
				reply.AddString("text", B_TRANSLATE("Please enter your password."));
			} else
				reply.AddString("text", B_TRANSLATE("That password is not right."));
		} else
		if (status == ENOENT)
			reply.AddString("text", B_TRANSLATE("That password is not right."));
		else if (status == B_NAME_NOT_FOUND || status == B_TIMED_OUT)
			reply.AddString("text", B_TRANSLATE("Could not start the desktop."));
		else {
			BString text(B_TRANSLATE("Could not start the session: %error%"));
			text.ReplaceFirst("%error%", strerror(status));
			reply.AddString("text", text.String());
		}
// SPDX-SnippetEnd

		message->SendReply(&reply);
	}
}


void
LoginApp::HandleAdmin(BMessage* message)
{
	BMessage reply;
	handle_admin_message(message, reply);
	message->SendReply(&reply);
}


void
LoginApp::Lock()
{
	BString name;
	if (signed_in_name(name) != B_OK)
		return;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The guest has no password; never lock a guest session (#72).
	if (is_guest_account(name.String()))
		return;
// SPDX-SnippetEnd

	if (fLockWindow != NULL) {
		if (fLockWindow->Lock()) {
			fLockWindow->Activate(true);
			fLockWindow->Unlock();
		}
		return;
	}

	// Walked away: a preference grant must not stay open behind the lock.
	clear_admin_grants();
	r5_session_log("lock %s", name.String());
	fLockWindow = new LockWindow(name.String());
	fLockWindow->Show();
}


void
LoginApp::Unlock(BMessage* message)
{
	BString name;
	const char* password = message->GetString("password");
	status_t status = signed_in_name(name);
	if (status == B_OK)
		status = ValidateLogin(name.String(), password);

	BMessage reply(status == B_OK ? kLoginOk : kLoginBad);
	if (status != B_OK) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		if (password == NULL || password[0] == '\0')
			reply.AddString("text", B_TRANSLATE("Please enter your password."));
		else
// SPDX-SnippetEnd
			reply.AddString("text", B_TRANSLATE("That password is not right."));
	}
	message->SendReply(&reply);
	if (status != B_OK)
		return;

	// Same session. Do not launch Tracker again and do not clear the
	// session file.
	r5_session_log("unlock %s", name.String());
	_CloseLock();
}


void
LoginApp::_CloseLock()
{
	if (fLockWindow == NULL)
		return;
	BWindow* window = fLockWindow;
	fLockWindow = NULL;
	if (window->Lock())
		window->Quit();
}


void
LoginApp::Logout()
{
	// Do not call the launch daemon. Stop/SetEnabled/GetJobs wait for a
	// reply; launch_daemon user is the team in the crash dialog, and while
	// it sits in the debugger that reply never comes.
	_CloseLock();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	BString loggedOutName;
	bool wasGuest = signed_in_name(loggedOutName) == B_OK
		&& is_guest_account(loggedOutName.String());
	uid_t loggedOutUid = 0;
	if (loggedOutName.Length() > 0) {
		struct passwd* pwd = getpwnam(loggedOutName.String());
		if (pwd != NULL)
			loggedOutUid = pwd->pw_uid;
	}
// SPDX-SnippetEnd
	r5_session_log("logout");
	clear_admin_grants();
	clear_desktop_session();
	_QuitUserApps();
	_WaitGone("application/x-vnd.Haiku-ProcessController");
	_WaitGone("application/x-vnd.Haiku-NetworkStatus");
	_WaitGone("application/x-vnd.R5OS-airplay-tray");
	_WaitGone("application/x-vnd.R5OS-vbox-clip");
	_WaitGone("application/x-vnd.Oracle-VBoxGuest");
	stop_team_named("VBoxService");
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	_WaitGone("application/x-vnd.Haiku-keystore_server");
// SPDX-SnippetEnd
	_WaitGone("application/x-vnd.Be-TRAK");
	_WaitGone("application/x-vnd.Be-TSKB");
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Issue #4642: what the roster does not list (a Terminal job in the
	// background, a program without a signature or with "server" in it)
	// stops too. Only the uid of the person who logs out.
	if (loggedOutUid >= kMinPersonUid)
		stop_teams_of_uid(loggedOutUid);
	if (wasGuest) {
		r5_session_log("wipe guest home");
		wipe_guest_home();
	}
// SPDX-SnippetEnd
	_ShowLogin();
	r5_session_log("logout done");
}


status_t
LoginApp::ValidateLogin(const char* login, const char* password)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (is_guest_account(login)) {
		if (!is_guest_enabled())
			return B_NOT_ALLOWED;
		BString error;
		status_t status = ensure_guest_account(error);
		if (status != B_OK)
			return status;
		return B_OK;
	}
// SPDX-SnippetEnd
	struct passwd* pwd = getpwnam(login);
	if (pwd == NULL || strcmp(pwd->pw_name, login) != 0)
		return ENOENT;

	if (account_password_matches(pwd, password))
		return B_OK;

	return B_PERMISSION_DENIED;
}


void
LoginApp::_EnableDesktopJobs()
{
	BLaunchRoster roster;
	for (int32 i = 0; kSessionJobs[i] != NULL; i++) {
		roster.SetEnabled(kSessionJobs[i], false);
		roster.Stop(kSessionJobs[i], true);
	}

	BStringList jobs;
	status_t jobsStatus = roster.GetJobs("desktop", jobs);
	if (jobsStatus != B_OK) {
		r5_session_log("GetJobs(desktop): %s", strerror(jobsStatus));
		syslog(LOG_WARNING, "Login: GetJobs(desktop) failed; "
			"session jobs stopped by name");
		return;
	}

	for (int32 i = 0; i < jobs.CountStrings(); i++) {
		const char* name = jobs.StringAt(i);
		if (is_session_job(name)) {
			roster.SetEnabled(name, false);
			roster.Stop(name, true);
		} else
			roster.SetEnabled(name, true);
	}
}


status_t
LoginApp::_LaunchSessionApp(const char* program, const char* argument,
	const char* argument2, const char* argument3, const char* argument4)
{
	const char* args[8];
	int32 count = 0;
	args[count++] = "/system/apps/r5-desktop";
	args[count++] = program;
	if (argument != NULL && argument[0] != '\0')
		args[count++] = argument;
	if (argument2 != NULL && argument2[0] != '\0')
		args[count++] = argument2;
	if (argument3 != NULL && argument3[0] != '\0')
		args[count++] = argument3;
	if (argument4 != NULL && argument4[0] != '\0')
		args[count++] = argument4;
	args[count] = NULL;

	r5_session_log("launch %s", program);
	thread_id thread = load_image(count, args, (const char**)environ);
	if (thread < B_OK) {
		r5_session_log("load_image r5-desktop %s: %s", program,
			strerror(thread));
		return thread;
	}
	resume_thread(thread);

	status_t result = B_ERROR;
	wait_for_thread(thread, &result);
	if (result != B_OK) {
		r5_session_log("r5-desktop %s: %s", program, strerror(result));
		syslog(LOG_ERR, "Login: r5-desktop %s: %s", program, strerror(result));
		return result < B_OK ? result : B_ERROR;
	}
	r5_session_log("r5-desktop %s ok", program);
	return B_OK;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
status_t
LoginApp::_LaunchSessionAppGetTeam(const char* program, team_id* _outTeam)
{
	if (_outTeam != NULL)
		*_outTeam = -1;
	if (program == NULL || program[0] == '\0' || _outTeam == NULL)
		return B_BAD_VALUE;

	int fds[2];
	if (pipe(fds) != 0)
		return B_ERROR;
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);

	// Build a private environment array for this single load_image() call so
	// Login's own environ is never mutated and no other child inherits the var.
	char envEntry[36];
	snprintf(envEntry, sizeof(envEntry), "R5_REPORT_TEAM_FD=%d", fds[1]);

	size_t envCount = 0;
	if (environ != NULL) {
		while (environ[envCount] != NULL)
			envCount++;
	}
	const char** childEnv = static_cast<const char**>(
		malloc((envCount + 2) * sizeof(const char*)));
	if (childEnv == NULL) {
		close(fds[0]);
		close(fds[1]);
		return B_NO_MEMORY;
	}
	size_t outIdx = 0;
	for (size_t i = 0; i < envCount; i++) {
		if (strncmp(environ[i], "R5_REPORT_TEAM_FD=", 18) == 0)
			continue;
		childEnv[outIdx++] = environ[i];
	}
	childEnv[outIdx++] = envEntry;
	childEnv[outIdx] = NULL;

	const char* args[] = {
		"/system/apps/r5-desktop",
		program,
		NULL
	};
	r5_session_log("launch %s", program);
	thread_id thread = load_image(2, args, childEnv);
	free(childEnv);
	close(fds[1]);
	if (thread < B_OK) {
		close(fds[0]);
		r5_session_log("load_image r5-desktop %s: %s", program,
			strerror(thread));
		return thread;
	}
	resume_thread(thread);

	status_t result = B_ERROR;
	wait_for_thread(thread, &result);

	team_id launchedTeam = -1;
	ssize_t n = read(fds[0], &launchedTeam, sizeof(launchedTeam));
	close(fds[0]);

	if (result != B_OK) {
		r5_session_log("r5-desktop %s: %s", program, strerror(result));
		return result < B_OK ? result : B_ERROR;
	}
	if (n != static_cast<ssize_t>(sizeof(launchedTeam)) || launchedTeam < 0)
		return B_ERROR;

	*_outTeam = launchedTeam;
	return B_OK;
}


void
LoginApp::_StartAndUnlockKeyStore(const char* login, const char* password)
{
	if (login == NULL)
		return;
	// Accounts step 5: no password. Try R5 (the live medium) gets a key store
	// in memory only, so mail and online storage can keep their sign-ins for
	// this session; they are forgotten at log out or shut down. Elsewhere
	// (the guest, an autologin) the key store stays locked.
	bool memoryOnly = false;
	if (password == NULL || password[0] == '\0') {
		if (!is_live_medium() || is_guest_account(login))
			return;
		memoryOnly = true;
	}

	struct passwd* pwd = getpwnam(login);
	if (pwd == NULL || pwd->pw_uid == 0 || pwd->pw_dir == NULL
		|| pwd->pw_dir[0] == '\0')
		return;

	static const char* kKeyStoreSig
		= "application/x-vnd.Haiku-keystore_server";
	_WaitGone(kKeyStoreSig);

	team_id launchedTeam = -1;
	if (_LaunchSessionAppGetTeam("/system/servers/keystore_server",
			&launchedTeam) != B_OK || launchedTeam < 0) {
		r5_session_log("keystore_server: launch failed");
		return;
	}

	// Wait until the exact team Login started registers with the roster.
	const bigtime_t deadline = system_time() + 5000000LL;
	app_info appInfo;
	bool ready = false;
	while (system_time() < deadline) {
		if (be_roster->GetRunningAppInfo(launchedTeam, &appInfo) == B_OK
			&& strcmp(appInfo.signature, kKeyStoreSig) == 0) {
			ready = true;
			break;
		}
		snooze(50000);
	}
	if (!ready) {
		r5_session_log("keystore_server: team %d did not register",
			(int)launchedTeam);
		return;
	}

	// Verify uid, image path, and exact team started by Login.
	team_info tInfo;
	BPath imgPath(&appInfo.ref);
	if (get_team_info(launchedTeam, &tInfo) != B_OK
		|| tInfo.uid != pwd->pw_uid || tInfo.uid == 0
		|| imgPath.InitCheck() != B_OK
		|| (strcmp(imgPath.Path(), "/boot/system/servers/keystore_server") != 0
			&& strcmp(imgPath.Path(), "/system/servers/keystore_server") != 0)) {
		r5_session_log("keystore_server: team verification failed");
		return;
	}

	if (memoryOnly) {
		BMessage memoryMsg(BPrivate::kMsgR5KeyStoreUnlock);
		memoryMsg.AddBool(BPrivate::kR5KSFieldMemoryOnly, true);
		BMessage memoryReply;
		status_t memoryStatus = r5_admin_send(BMessenger(NULL, launchedTeam),
			&memoryMsg, &memoryReply);
		if (memoryStatus == B_OK) {
			memoryStatus = memoryReply.GetInt32(BPrivate::kR5KSFieldStatus,
				B_ERROR);
		}
		r5_session_log("keystore_server: in memory (Try R5) %s",
			strerror(memoryStatus));
		return;
	}

	BPrivate::R5PasswordProtectorParams protParams;
	bool hasProtector = (BPrivate::R5KeyStoreCrypto::
		ReadAccountPasswordProtectorParams(pwd->pw_dir, pwd->pw_uid,
			protParams) == B_OK);
	if (!hasProtector) {
		protParams.kdf = BSecurityKit::BPasswordHash::DefaultParameters();
		if (BSecurityKit::BRandom::Fill(protParams.salt,
				sizeof(protParams.salt)) != B_OK)
			return;
	}

	BSecurityKit::BSecureBuffer kek;
	status_t status = BSecurityKit::BPasswordHash::DeriveKey(password,
		strlen(password), protParams.salt, sizeof(protParams.salt),
		protParams.kdf, kek, BSecurityKit::BSealedData::kKeySize);
	if (status != B_OK) {
		r5_session_log("keystore_server: key derivation failed");
		return;
	}

	BMessage unlockMsg(BPrivate::kMsgR5KeyStoreUnlock);
	unlockMsg.AddData(BPrivate::kR5KSFieldKek, B_RAW_TYPE, kek.Data(),
		kek.Size());
	if (!hasProtector) {
		unlockMsg.AddData(BPrivate::kR5KSFieldSalt, B_RAW_TYPE,
			protParams.salt, sizeof(protParams.salt));
		unlockMsg.AddUInt32(BPrivate::kR5KSFieldMemKiB,
			protParams.kdf.memoryKiB);
		unlockMsg.AddUInt32(BPrivate::kR5KSFieldPasses, protParams.kdf.passes);
		unlockMsg.AddUInt32(BPrivate::kR5KSFieldLanes, protParams.kdf.lanes);
	}
	kek.Clear();

	BMessage reply;
	status = r5_admin_send(BMessenger(NULL, launchedTeam), &unlockMsg, &reply);
	const void* msgKek = NULL;
	ssize_t msgKekSize = 0;
	if (unlockMsg.FindData(BPrivate::kR5KSFieldKek, B_RAW_TYPE, &msgKek,
			&msgKekSize) == B_OK && msgKek != NULL && msgKekSize > 0) {
		BSecurityKit::BSecureBuffer::WipeMemory(const_cast<void*>(msgKek),
			static_cast<size_t>(msgKekSize));
	}
	unlockMsg.MakeEmpty();
	if (status != B_OK) {
		r5_session_log("keystore_server: unlock handoff failed: %s",
			strerror(status));
		return;
	}
	int32 ksStatus = reply.GetInt32(BPrivate::kR5KSFieldStatus, B_ERROR);

	// Safety net: Login already verified the sign-in password, so if the
	// existing protector refused it (e.g. password changed via passwd in
	// Terminal), move the current store aside as .old* and initialize a fresh
	// store with this password, triggering "Do you still know your old
	// password?" in keystore_server. keystore_server answers
	// B_PERMISSION_DENIED only when the protector itself does not open; a
	// damaged database or a refused sender (B_NOT_ALLOWED) is not a mismatch
	// and must not move the store aside.
	if (hasProtector && ksStatus == B_PERMISSION_DENIED) {
		uint8 freshSalt[16];
		BSecurityKit::BPasswordHash::Parameters freshParams
			= BSecurityKit::BPasswordHash::DefaultParameters();
		BSecurityKit::BSecureBuffer freshKek;
		if (BSecurityKit::BRandom::Fill(freshSalt, sizeof(freshSalt)) == B_OK
			&& BSecurityKit::BPasswordHash::DeriveKey(password,
				strlen(password), freshSalt, sizeof(freshSalt), freshParams,
				freshKek, BSecurityKit::BSealedData::kKeySize) == B_OK) {
			BMessage recoverMsg(BPrivate::kMsgR5KeyStoreUnlock);
			recoverMsg.AddBool(BPrivate::kR5KSFieldRecoverMismatch, true);
			recoverMsg.AddData(BPrivate::kR5KSFieldKek, B_RAW_TYPE,
				freshKek.Data(), freshKek.Size());
			recoverMsg.AddData(BPrivate::kR5KSFieldSalt, B_RAW_TYPE,
				freshSalt, sizeof(freshSalt));
			recoverMsg.AddUInt32(BPrivate::kR5KSFieldMemKiB,
				freshParams.memoryKiB);
			recoverMsg.AddUInt32(BPrivate::kR5KSFieldPasses,
				freshParams.passes);
			recoverMsg.AddUInt32(BPrivate::kR5KSFieldLanes,
				freshParams.lanes);
			freshKek.Clear();

			BMessage recoverReply;
			status = r5_admin_send(BMessenger(NULL, launchedTeam),
				&recoverMsg, &recoverReply);
			const void* recKek = NULL;
			ssize_t recKekSize = 0;
			if (recoverMsg.FindData(BPrivate::kR5KSFieldKek, B_RAW_TYPE,
					&recKek, &recKekSize) == B_OK
				&& recKek != NULL && recKekSize > 0) {
				BSecurityKit::BSecureBuffer::WipeMemory(
					const_cast<void*>(recKek),
					static_cast<size_t>(recKekSize));
			}
			recoverMsg.MakeEmpty();
			if (status == B_OK) {
				ksStatus = recoverReply.GetInt32(BPrivate::kR5KSFieldStatus,
					B_ERROR);
			} else
				ksStatus = status;
			r5_session_log("keystore_server: mismatch recovery %s",
				strerror(ksStatus));
			return;
		}
		freshKek.Clear();
	}

	r5_session_log("keystore_server: unlock %s", strerror(ksStatus));
}
// SPDX-SnippetEnd


void
LoginApp::_StartVirtualBoxGuest()
{
	// The desktop target in launch_daemon is not enabled. Sign in starts
	// these itself, and only when the guest device is there. QEMU and real
	// hardware have no /dev/misc/vboxguest, so this returns without mounting.
	if (access("/dev/misc/vboxguest", F_OK) != 0)
		return;

	if (!team_named("VBoxService")) {
		_LaunchSessionApp("/system/non-packaged/bin/VBoxService",
			"-f", "--disable-memballoon", "--timesync-no-set-start");
	}

	BPath home;
	if (_SessionHome(home) == B_OK) {
		const char* args[] = {
			"/bin/sh",
			"/system/non-packaged/bin/r5-vbox-shares",
			home.Path(),
			NULL
		};
		r5_session_log("launch r5-vbox-shares %s", home.Path());
		thread_id thread = load_image(3, args, (const char**)environ);
		if (thread < B_OK) {
			r5_session_log("r5-vbox-shares: %s", strerror(thread));
		} else {
			resume_thread(thread);
			status_t result = B_ERROR;
			wait_for_thread(thread, &result);
			r5_session_log("r5-vbox-shares: %s", strerror(result));
		}
	}

	if (!be_roster->IsRunning("application/x-vnd.Oracle-VBoxGuest"))
		_LaunchSessionApp("/system/non-packaged/bin/VBoxTray");
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static bool
gpu_selftest_requested()
{
	char path[B_PATH_NAME_LENGTH];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, path,
			sizeof(path)) != B_OK)
		return false;
	if (strlcat(path, "/kernel/drivers/radeon_hd", sizeof(path))
			>= sizeof(path))
		return false;
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;
	bool on = false;
	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, "gpu_selftest true", 17) == 0)
			on = true;
	}
	fclose(file);
	return on;
}
// SPDX-SnippetEnd



void
LoginApp::_LaunchSessionHooks()
{
	status_t status = _LaunchSessionApp("/system/bin/dstcheck");
	if (status != B_OK)
		r5_session_log("dstcheck: %s", strerror(status));

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Once per boot: Login stays the same process across log out and sign
	// in. The self-test's stages are meant to run before any other GPU
	// client; a second run at the next sign-in froze the 5625U (M10 stick).
	static bool sGpuSelftestStarted = false;
	if (gpu_selftest_requested() && !sGpuSelftestStarted) {
		sGpuSelftestStarted = true;
		status = _LaunchSessionApp("/system/bin/r5-gpu-selftest");
		if (status != B_OK)
			r5_session_log("r5-gpu-selftest: %s", strerror(status));
	}
// SPDX-SnippetEnd

	BPath home;
	if (_SessionHome(home) != B_OK)
		return;

	BPath script(home);
	if (script.Append("config/settings/boot/UserBootscript") == B_OK
		&& access(script.Path(), R_OK) == 0) {
		status = _LaunchSessionApp("/bin/sh", script.Path());
		if (status != B_OK)
			r5_session_log("UserBootscript: %s", strerror(status));
	}

	BPath marker(home);
	if (marker.Append("config/settings/first_login") == B_OK
		&& access(marker.Path(), F_OK) == 0) {
		status = _LaunchSessionApp("/bin/sh", "/system/boot/PostInstallScript",
			"first login", marker.Path(), "/boot/system/boot/first-login");
		if (status != B_OK)
			r5_session_log("first-login: %s", strerror(status));
	}
}


status_t
LoginApp::_SessionHome(BPath& path) const
{
	char name[64];
	name[0] = '\0';
	FILE* file = fopen("/boot/system/settings/r5-desktop-session", "r");
	if (file != NULL) {
		if (fgets(name, sizeof(name), file) == NULL)
			name[0] = '\0';
		fclose(file);
	}

	size_t length = strlen(name);
	while (length > 0
		&& (name[length - 1] == '\n' || name[length - 1] == '\r'
			|| name[length - 1] == ' ')) {
		name[--length] = '\0';
	}
	if (length == 0)
		return B_NAME_NOT_FOUND;

	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || pwd->pw_dir == NULL || pwd->pw_dir[0] == '\0')
		return ENOENT;
	return path.SetTo(pwd->pw_dir);
}


status_t
LoginApp::_WaitRunning(const char* signature)
{
	const bigtime_t deadline = system_time() + 8000000LL;
	while (system_time() < deadline) {
		if (be_roster->IsRunning(signature))
			return B_OK;
		snooze(100000);
	}
	r5_session_log("timeout waiting for %s", signature);
	return B_TIMED_OUT;
}


status_t
LoginApp::_WaitSessionTeam(const char* signature)
{
	status_t status = _WaitRunning(signature);
	if (status != B_OK)
		return status;

	team_id team = be_roster->TeamFor(signature);
	team_info info;
	if (team < 0 || get_team_info(team, &info) != B_OK) {
		r5_session_log("%s: no team info", signature);
		return B_BAD_TEAM_ID;
	}
	r5_session_log("%s team %d uid=%d gid=%d", signature, (int)team,
		(int)info.uid, (int)info.gid);
	if (info.uid == 0) {
		BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);
		return B_NOT_ALLOWED;
	}

	char name[64];
	name[0] = '\0';
	FILE* file = fopen("/boot/system/settings/r5-desktop-session", "r");
	if (file != NULL) {
		if (fgets(name, sizeof(name), file) == NULL)
			name[0] = '\0';
		fclose(file);
	}
	size_t length = strlen(name);
	while (length > 0
		&& (name[length - 1] == '\n' || name[length - 1] == '\r'
			|| name[length - 1] == ' ')) {
		name[--length] = '\0';
	}
	struct passwd* pwd = length > 0 ? getpwnam(name) : NULL;
	if (pwd == NULL || pwd->pw_uid != info.uid) {
		r5_session_log("%s uid %d is not session %s", signature,
			(int)info.uid, length > 0 ? name : "(none)");
		BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);
		return B_NOT_ALLOWED;
	}
	return B_OK;
}


void
LoginApp::_HideLogin()
{
	if (fLoginWindow != NULL) {
		BWindow* window = fLoginWindow;
		fLoginWindow = NULL;
		if (window->Lock())
			window->Quit();
	}
	if (fDesktopWindow != NULL) {
		BWindow* window = fDesktopWindow;
		fDesktopWindow = NULL;
		if (window->Lock())
			window->Quit();
	}
}


void
LoginApp::_ShowLogin()
{
	if (fDesktopWindow == NULL) {
		BScreen screen;
		fDesktopWindow = new DesktopWindow(screen.Frame(), false);
		fDesktopWindow->Show();
	} else if (fDesktopWindow->Lock()) {
		if (fDesktopWindow->IsHidden())
			fDesktopWindow->Show();
		fDesktopWindow->Unlock();
	}
	_OpenLoginWindow();
}


void
LoginApp::_OpenLoginWindow()
{
	if (fLoginWindow != NULL) {
		BWindow* old = fLoginWindow;
		fLoginWindow = NULL;
		if (old->Lock())
			old->Quit();
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fLoginWindow = new LoginWindow(BRect(0, 0, 440, 380));
// SPDX-SnippetEnd
	fLoginWindow->Show();
}


void
LoginApp::_QuitUserApps()
{
	BList teams;
	be_roster->GetAppList(&teams);
	team_id self = Team();
	for (int32 i = 0; i < teams.CountItems(); i++) {
		team_id team = (team_id)(addr_t)teams.ItemAt(i);
		app_info info;
		if (be_roster->GetRunningAppInfo(team, &info) != B_OK)
			continue;
		if (keep_team(info, self))
			continue;
		BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);
	}

	const bigtime_t deadline = system_time() + 3000000LL;
	for (;;) {
		BList leftover;
		BList live;
		be_roster->GetAppList(&live);
		for (int32 i = 0; i < live.CountItems(); i++) {
			team_id team = (team_id)(addr_t)live.ItemAt(i);
			app_info info;
			if (be_roster->GetRunningAppInfo(team, &info) != B_OK)
				continue;
			if (keep_team(info, self))
				continue;
			leftover.AddItem((void*)(addr_t)team);
		}
		if (leftover.IsEmpty())
			return;
		if (system_time() < deadline) {
			snooze(100000);
			continue;
		}
		for (int32 i = 0; i < leftover.CountItems(); i++) {
			team_id team = (team_id)(addr_t)leftover.ItemAt(i);
			app_info info;
			const char* name = "app";
			if (be_roster->GetRunningAppInfo(team, &info) == B_OK
				&& info.signature[0] != '\0')
				name = info.signature;
			r5_session_log("killing leftover %s team %d", name, (int)team);
			kill_team(team);
		}
		const bigtime_t killDeadline = system_time() + 1000000LL;
		while (system_time() < killDeadline) {
			BList still;
			be_roster->GetAppList(&still);
			bool remain = false;
			for (int32 i = 0; i < still.CountItems(); i++) {
				team_id team = (team_id)(addr_t)still.ItemAt(i);
				app_info info;
				if (be_roster->GetRunningAppInfo(team, &info) != B_OK)
					continue;
				if (keep_team(info, self))
					continue;
				remain = true;
				break;
			}
			if (!remain)
				return;
			snooze(100000);
		}
		return;
	}
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
BMessage*
login_local_message(uint32 what)
{
	BMessage* message = new BMessage(what);
	if (r5_admin_mark_local(message) != B_OK)
		syslog(LOG_ERR, "Login: no token for its own messages");
	return message;
}


// Issue #4642: system servers that are never stopped at Log out, even if
// one of them runs with the person's uid. A team is kept only when its
// program is this name in /boot/system/servers (or /system/servers), or
// the kernel. Everything else of that uid stops: also a Terminal job in
// the background and a program without a signature.
static const char* kKeptSystemTeams[] = {
	"app_server",
	"debug_server",
	"input_server",
	"launch_daemon",
	"mount_server",
	"net_server",
	"package_daemon",
	"registrar",
	"syslog_daemon",
	NULL
};


static bool
is_kept_system_team(const team_info& info)
{
	if (strcmp(info.args, "kernel_team") == 0)
		return true;
	char program[sizeof(info.args)];
	strlcpy(program, info.args, sizeof(program));
	char* space = strchr(program, ' ');
	if (space != NULL)
		*space = '\0';
	const char* name = NULL;
	if (strncmp(program, "/boot/system/servers/", 21) == 0)
		name = program + 21;
	else if (strncmp(program, "/system/servers/", 16) == 0)
		name = program + 16;
	if (name == NULL)
		return false;
	for (int32 i = 0; kKeptSystemTeams[i] != NULL; i++) {
		if (strcmp(name, kKeptSystemTeams[i]) == 0)
			return true;
	}
	return false;
}


// Stops every team of "uid" by the uid the kernel gives the team, not by
// the roster: SIGTERM, then after a second kill_team(). Only that uid: a
// person uid, never uid 0, never another signed-in person.
static void
stop_teams_of_uid(uid_t uid)
{
	if (uid < kMinPersonUid)
		return;
	team_id self = be_app != NULL ? be_app->Team() : -1;
	int32 cookie = 0;
	team_info info;
	int32 signalled = 0;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		if (info.uid != uid || info.team == self || info.team <= 1
			|| is_kept_system_team(info))
			continue;
		send_signal(info.team, SIGTERM);
		signalled++;
	}
	if (signalled == 0)
		return;
	r5_session_log("logout: SIGTERM to %d teams of uid %d", (int)signalled,
		(int)uid);

	const bigtime_t deadline = system_time() + 1000000LL;
	for (;;) {
		bool left = false;
		cookie = 0;
		while (get_next_team_info(&cookie, &info) == B_OK) {
			if (info.uid == uid && info.team != self && info.team > 1
				&& !is_kept_system_team(info)) {
				left = true;
				break;
			}
		}
		if (!left)
			return;
		if (system_time() >= deadline)
			break;
		snooze(100000);
	}
	cookie = 0;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		if (info.uid != uid || info.team == self || info.team <= 1
			|| is_kept_system_team(info))
			continue;
		r5_session_log("logout: killing team %d of uid %d", (int)info.team,
			(int)uid);
		kill_team(info.team);
	}
}


struct HomeWalkJob {
	uid_t		uid;
	gid_t		gid;
	BMessenger	view;
	BMessenger	app;
};


static void
home_walk_progress_report(int64 count, void* cookie)
{
	HomeWalkJob* job = (HomeWalkJob*)cookie;
	BString text;
	text = B_TRANSLATE("Preparing your files\xe2\x80\xa6 %count%");
	BString number;
	number << count;
	text.ReplaceFirst("%count%", number);
	BMessage* progress = login_local_message(kLoginProgress);
	progress->AddString("text", text.String());
	job->view.SendMessage(progress);
	delete progress;
}


static status_t
home_walk_thread(void* data)
{
	HomeWalkJob* job = (HomeWalkJob*)data;
	status_t status = own_first_home(job->uid, job->gid,
		&home_walk_progress_report, job);
	r5_session_log("home walk for uid %d: %s", (int)job->uid,
		strerror(status));
	BMessage* done = login_local_message(kHomeWalkDone);
	done->AddInt32("status", status);
	job->app.SendMessage(done);
	delete done;
	delete job;
	return B_OK;
}


status_t
LoginApp::_StartHomeWalk(BMessage* message, uid_t uid, gid_t gid)
{
	if (fHomeWalkThread >= 0 || fPendingLogin != NULL)
		return B_BUSY;
	HomeWalkJob* job = new(std::nothrow) HomeWalkJob;
	if (job == NULL)
		return B_NO_MEMORY;
	job->uid = uid;
	job->gid = gid;
	job->view = message->ReturnAddress();
	job->app = BMessenger(this);

	thread_id thread = spawn_thread(&home_walk_thread, "home walk",
		B_LOW_PRIORITY, job);
	if (thread < 0) {
		delete job;
		return thread;
	}
	if (message == CurrentMessage())
		message = DetachCurrentMessage();
	fPendingLogin = message;
	fHomeWalkThread = thread;
	r5_session_log("home walk for uid %d started", (int)uid);
	home_walk_progress_report(0, job);
	resume_thread(thread);
	return B_OK;
}


void
LoginApp::_HomeWalkDone(BMessage* message)
{
	if (fPendingLogin == NULL)
		return;
	if (fHomeWalkThread >= 0) {
		status_t exitValue;
		wait_for_thread(fHomeWalkThread, &exitValue);
		fHomeWalkThread = -1;
	}
	BMessage* pending = fPendingLogin;
	status_t status = message->GetInt32("status", B_ERROR);
	if (status == B_OK) {
		// fPendingLogin stays set while TryLogin() runs: it skips the walk
		// check and takes a first password from "new password".
		TryLogin(pending);
	} else {
		SignInMessageWiper wiper(pending);
		BMessage reply(kLoginBad);
		reply.AddInt32("error", status);
		BString text(B_TRANSLATE("Could not prepare your files: %error%"));
		text.ReplaceFirst("%error%", strerror(status));
		reply.AddString("text", text.String());
		pending->SendReply(&reply);
	}
	fPendingLogin = NULL;
	delete pending;
}
// SPDX-SnippetEnd


status_t
LoginApp::_WaitGone(const char* signature)
{
	if (!be_roster->IsRunning(signature))
		return B_OK;

	team_id team = be_roster->TeamFor(signature);
	if (team >= 0)
		BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);

	const bigtime_t deadline = system_time() + 3000000LL;
	while (be_roster->IsRunning(signature) && system_time() < deadline)
		snooze(100000);
	if (be_roster->IsRunning(signature)) {
		team = be_roster->TeamFor(signature);
		r5_session_log("killing %s team %d", signature, (int)team);
		if (team >= 0)
			kill_team(team);
		const bigtime_t killDeadline = system_time() + 1000000LL;
		while (be_roster->IsRunning(signature) && system_time() < killDeadline)
			snooze(100000);
	}
	if (be_roster->IsRunning(signature)) {
		r5_session_log("still running %s", signature);
		return B_BUSY;
	}
	return B_OK;
}
