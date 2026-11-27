/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "AomiDownloads.h"
#include "AomiSettings.h"
#include "AomiWindow.h"

#include "AomiMessages.h"
#include "AomiPages.h"
#include "ServoContentView.h"
#include "WebContentView.h"

#include <Application.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>


static const char* kEngine = "Servo";
static const char* kAppSig = "application/x-vnd.R5-Aomi";


static BString
normal_team_file()
{
	BString path;
	path.SetToFormat("/tmp/aomi-normal-%d.team", (int)getuid());
	return path;
}


static team_id
read_normal_team()
{
	FILE* f = fopen(normal_team_file().String(), "r");
	if (f == NULL)
		return -1;
	int team = -1;
	if (fscanf(f, "%d", &team) != 1)
		team = -1;
	fclose(f);
	return (team_id)team;
}


static void
write_normal_team(team_id team)
{
	FILE* f = fopen(normal_team_file().String(), "w");
	if (f == NULL)
		return;
	fprintf(f, "%d\n", (int)team);
	fclose(f);
}


static void
remove_tree(const char* path)
{
	if (path == NULL || path[0] == '\0')
		return;
	DIR* dir = opendir(path);
	if (dir != NULL) {
		struct dirent* ent;
		while ((ent = readdir(dir)) != NULL) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;
			BString child(path);
			child << "/" << ent->d_name;
			struct stat st;
			if (lstat(child.String(), &st) == 0 && S_ISDIR(st.st_mode))
				remove_tree(child.String());
			else
				unlink(child.String());
		}
		closedir(dir);
		rmdir(path);
	} else {
		unlink(path);
	}
}


// Normal windows share this process and its one Servo (on its engine
// thread). A private window is a process of its own: its Servo keeps
// nothing on disk and shares nothing with the normal windows.
static void
launch_window_process(const char* flag, const std::vector<BString>& urls)
{
	app_info info;
	if (be_app == NULL || be_app->GetAppInfo(&info) != B_OK)
		return;
	std::vector<const char*> args;
	args.push_back(flag);
	for (uint32 i = 0; i < urls.size(); i++)
		args.push_back(urls[i].String());
	be_roster->Launch(&info.ref, (int)args.size(),
		const_cast<char**>(args.data()));
}


static void
launch_private_process(const std::vector<BString>& urls)
{
	launch_window_process("--private-process", urls);
}



static bool
forward_to_running_normal(int argc, char** argv)
{
	team_id team = read_normal_team();
	if (team <= 0)
		return false;
	app_info info;
	if (be_roster->GetRunningAppInfo(team, &info) != B_OK
		|| strcmp(info.signature, kAppSig) != 0)
		return false;
	BMessenger target(NULL, team);
	if (!target.IsValid())
		return false;
	BMessage msg(B_ARGV_RECEIVED);
	msg.AddInt32("argc", argc);
	for (int i = 0; i < argc; i++)
		msg.AddString("argv", argv[i]);
	char cwd[B_PATH_NAME_LENGTH];
	if (getcwd(cwd, sizeof(cwd)) != NULL)
		msg.AddString("cwd", cwd);
	return target.SendMessage(&msg) == B_OK;
}


class AomiApp : public BApplication {
public:
								AomiApp(bool privat, const char* privateDir)
									:
									BApplication(kAppSig),
									fPrivate(privat),
									fPrivateDir(privateDir != NULL
										? privateDir : ""),
									fMenu(false),
									fDownloads(false),
									fSettings(false),
									fQuitAsked(false)
								{
									if (!fPrivate)
										write_normal_team(Team());
								}

	virtual						~AomiApp()
								{
									if (fPrivateDir.Length() > 0)
										remove_tree(fPrivateDir.String());
									else if (read_normal_team() == Team())
										unlink(normal_team_file().String());
								}

	virtual	void				ArgvReceived(int32 argc, char** argv)
								{
									bool privat = false;
									bool menu = false;
									bool downloads = false;
									bool settings = false;
									bool closeSettings = false;
									bool newWindow = false;
									bool openLock = false;
									bool doBack = false;
									bool doForward = false;
									bool doReload = false;
									bool doStop = false;
									bool doZoomIn = false;
									bool doZoomOut = false;
									bool doZoomReset = false;
									bool doBookmark = false;
									bool doFind = false;
									BString downloadUrl;
									BString goUrl;
									BString settingsPage;
									int32 closeTab = -1;
									std::vector<BString> urls;
									for (int32 i = 1; i < argc; i++) {
										if (strcmp(argv[i], "--private") == 0
											|| strcmp(argv[i],
												"--private-process") == 0)
											privat = true;
										else if (strcmp(argv[i], "--menu") == 0)
											menu = true;
										else if (strcmp(argv[i],
												"--downloads") == 0)
											downloads = true;
										else if (strcmp(argv[i],
												"--settings") == 0)
											settings = true;
										else if (strncmp(argv[i],
												"--settings-page=", 16) == 0) {
											settings = true;
											settingsPage = argv[i] + 16;
										} else if (strcmp(argv[i],
												"--downloads-window") == 0)
											downloads = true;
										else if (strcmp(argv[i],
												"--close-settings") == 0)
											closeSettings = true;
										else if (strcmp(argv[i], "--lock") == 0)
											openLock = true;
										else if (strcmp(argv[i], "--window") == 0)
											newWindow = true;
										else if (strcmp(argv[i], "--back") == 0)
											doBack = true;
										else if (strcmp(argv[i], "--forward") == 0)
											doForward = true;
										else if (strcmp(argv[i], "--reload") == 0)
											doReload = true;
										else if (strcmp(argv[i], "--stop") == 0)
											doStop = true;
										else if (strcmp(argv[i], "--zoom-in") == 0)
											doZoomIn = true;
										else if (strcmp(argv[i], "--zoom-out") == 0)
											doZoomOut = true;
										else if (strcmp(argv[i], "--zoom-reset") == 0)
											doZoomReset = true;
										else if (strcmp(argv[i], "--bookmark") == 0)
											doBookmark = true;
										else if (strcmp(argv[i], "--find") == 0)
											doFind = true;
										else if (strcmp(argv[i], "--download") == 0
											&& i + 1 < argc) {
											downloadUrl = argv[++i];
										} else if (strcmp(argv[i], "--go") == 0
											&& i + 1 < argc) {
											goUrl = argv[++i];
										} else if (strcmp(argv[i],
												"--close-tab") == 0
											&& i + 1 < argc) {
											closeTab = atoi(argv[++i]);
										}
										else if (argv[i][0] != '-')
											urls.push_back(argv[i]);
									}
									if (IsLaunching()) {
										if (privat)
											fPrivate = true;
										fMenu = menu;
										fDownloads = downloads;
										fSettings = settings;
										fSettingsPage = settingsPage;
										fUrls = urls;
										return;
									}
									if (privat && !fPrivate) {
										launch_private_process(urls);
										return;
									}
									if (newWindow && !fPrivate) {
										// A private window asked for a normal
										// one: it belongs to this process.
										AomiWindow* fresh = new AomiWindow(
											new BWebSession(false), false);
										fresh->Show();
										for (uint32 i = 0; i < urls.size(); i++) {
											BMessage openMsg(MSG_OPEN_URL);
											openMsg.AddString("url", urls[i]);
											openMsg.AddBool("newTab", i > 0);
											fresh->PostMessage(&openMsg);
										}
										return;
									}
									if (closeSettings) {
										for (int32 i = 0; i < CountWindows(); i++) {
											AomiSettingsWindow* sw
												= dynamic_cast<AomiSettingsWindow*>(
													WindowAt(i));
											if (sw != NULL)
												sw->PostMessage(B_QUIT_REQUESTED);
										}
									}
									AomiWindow* window = NULL;
									for (int32 i = 0; i < CountWindows(); i++) {
										window = dynamic_cast<AomiWindow*>(
											WindowAt(i));
										if (window != NULL)
											break;
									}
									if (window == NULL)
										return;
									if (!window->Lock())
										return;
									// Started again from the Deskbar or
									// Tracker: bring the window to the front.
									window->Activate(true);
									if (goUrl.Length() > 0) {
										BMessage openMsg(MSG_OPEN_URL);
										openMsg.AddString("url", goUrl);
										openMsg.AddBool("newTab", false);
										window->PostMessage(&openMsg);
									}
									for (uint32 i = 0; i < urls.size(); i++) {
										BMessage openMsg(MSG_OPEN_URL);
										openMsg.AddString("url", urls[i]);
										openMsg.AddBool("newTab", true);
										window->PostMessage(&openMsg);
									}
									if (menu)
										window->PostMessage(MSG_MENU);
									if (downloads)
										window->PostMessage(MSG_DOWNLOADS);
									if (settings) {
										BMessage open(MSG_SETTINGS);
										if (settingsPage.Length() > 0)
											open.AddString("page", settingsPage);
										window->PostMessage(&open);
									}
									if (openLock)
										window->PostMessage(MSG_LOCK);
									if (doBack)
										window->PostMessage(MSG_BACK);
									if (doForward)
										window->PostMessage(MSG_FORWARD);
									if (doReload)
										window->PostMessage(MSG_RELOAD);
									if (doStop)
										window->PostMessage('esc!');
									if (doZoomIn)
										window->PostMessage(MSG_ZOOM_IN);
									if (doZoomOut)
										window->PostMessage(MSG_ZOOM_OUT);
									if (doZoomReset)
										window->PostMessage(MSG_ZOOM_RESET);
									if (doBookmark)
										window->PostMessage(MSG_STAR);
									if (doFind)
										window->PostMessage(MSG_FIND);
									if (downloadUrl.Length() > 0) {
										aomi_start_download(downloadUrl.String(),
											window->Prefs().downloadPath.String());
									}
									if (closeTab >= 0) {
										BMessage closeMsg(MSG_CLOSE_TAB);
										closeMsg.AddInt32("tab", closeTab);
										window->PostMessage(&closeMsg);
									}
									window->Unlock();
								}

	virtual	void				ReadyToRun()
								{
									aomi_engine_start(kEngine);
									AomiWindow* window = new AomiWindow(
										new BWebSession(fPrivate), fPrivate);
									window->Show();
									bool first = true;
									for (uint32 i = 0; i < fUrls.size(); i++) {
										BMessage openMsg(MSG_OPEN_URL);
										openMsg.AddString("url", fUrls[i]);
										openMsg.AddBool("newTab", !first);
										window->PostMessage(&openMsg);
										first = false;
									}
									if (fMenu)
										window->PostMessage(MSG_MENU);
									if (fDownloads)
										window->PostMessage(MSG_DOWNLOADS);
									if (fSettings) {
										BMessage open(MSG_SETTINGS);
										if (fSettingsPage.Length() > 0)
											open.AddString("page", fSettingsPage);
										window->PostMessage(&open);
									}
								}

	virtual	bool				QuitRequested()
								{
									// Quit (Command-Q) or the last browser
									// window closed. Running downloads ask
									// first; the open pages are kept for "Last
									// session" before any window closes.
									if (!fQuitAsked && !AomiWindow::Quitting()) {
										int32 running
											= aomi_download_count_running();
										if (running > 0
											&& !AomiWindow::AskStopDownloads(
												running))
											return false;
										if (!fPrivate) {
											std::vector<BString> urls;
											for (int32 i = 0; i < CountWindows();
													i++) {
												AomiWindow* window
													= dynamic_cast<AomiWindow*>(
														WindowAt(i));
												if (window != NULL
													&& window->Lock()) {
													window->GetSession(&urls);
													window->Unlock();
												}
											}
											aomi_session_save(urls);
										}
									}
									fQuitAsked = true;
									AomiWindow::SetQuitting();
									aomi_download_stop_all();
									// The windows close first, and with them every tab;
									// then Servo writes its profile and stops.
									if (!BApplication::QuitRequested()) {
										fQuitAsked = false;
										return false;
									}
									aomi_servo_shutdown();
									return true;
								}

	virtual	void				MessageReceived(BMessage* message)
								{
									switch (message->what) {
										case AOMI_DOWNLOAD_ADDED:
										case AOMI_DOWNLOAD_PROGRESS:
										case AOMI_DOWNLOAD_FINISHED:
										case AOMI_DOWNLOAD_FAILED:
											// The browser windows (their
											// download button) and the
											// Downloads window.
											for (int32 i = 0; i < CountWindows(); i++)
												WindowAt(i)->PostMessage(message);
											break;
										case MSG_NEW_WINDOW:
											if (fPrivate) {
												// A normal window lives in the
												// normal process, with its
												// profile; not in this one.
												const char* args[] = { "--window" };
												be_roster->Launch(kAppSig, 1,
													const_cast<char**>(args));
											} else {
												(new AomiWindow(
													new BWebSession(false),
													false))->Show();
											}
											break;

										case MSG_NEW_PRIVATE:
										{
											std::vector<BString> none;
											launch_private_process(none);
											break;
										}
										default:
											BApplication::MessageReceived(message);
											break;
									}
								}

private:
			bool				fPrivate;
			BString				fPrivateDir;
			bool				fMenu;
			bool				fDownloads;
			bool				fSettings;
			bool				fQuitAsked;
			BString				fSettingsPage;
			std::vector<BString> fUrls;
};


int
main(int argc, char** argv)
{
	bool privat = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--technical-json") == 0) {
			aomi_engine_start(kEngine);
			printf("%s", aomi_technical_json().String());
			// The engine thread started with the library; stop it before exit.
			aomi_servo_shutdown();
			return 0;
		}
		if (strcmp(argv[i], "--private") == 0
			|| strcmp(argv[i], "--private-process") == 0) {
			privat = true;
			break;
		}
	}
	if (!privat && forward_to_running_normal(argc, argv))
		return 0;


	BString privDir;
	if (privat) {
		// A private window keeps cookies and storage in memory only (no
		// AOMI_PROFILE_DIR). Anything else that writes a config or cache file
		// goes to a fresh directory of this user, removed when the window
		// closes. mkdtemp() makes it 0700 and refuses an existing name.
		BPath cache;
		if (find_directory(B_USER_CACHE_DIRECTORY, &cache, true) != B_OK
			|| cache.Append("Aomi") != B_OK) {
			fprintf(stderr, "aomi: no cache directory for the private window\n");
			return 1;
		}
		create_directory(cache.Path(), 0700);
		BString pattern(cache.Path());
		pattern << "/private-XXXXXX";
		char* made = mkdtemp(pattern.LockBuffer(pattern.Length()));
		pattern.UnlockBuffer();
		if (made == NULL) {
			fprintf(stderr, "aomi: could not make the private directory\n");
			return 1;
		}
		privDir = pattern;
		setenv("AOMI_PRIVATE_DIR", privDir.String(), 1);
		setenv("XDG_CONFIG_HOME", privDir.String(), 1);
		setenv("XDG_CACHE_HOME", privDir.String(), 1);
		unsetenv("AOMI_PROFILE_DIR");
	} else {
		// Servo keeps cookies, HSTS and saved logins here.
		BPath profile;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &profile, true) == B_OK
			&& profile.Append("Aomi/Servo") == B_OK) {
			// "Clear cookies and site data" in the settings: done here,
			// before Servo opens the profile.
			aomi_site_data_clear_if_pending(profile.Path());
		}
		if (profile.InitCheck() == B_OK
			&& create_directory(profile.Path(), 0700) == B_OK) {
			setenv("AOMI_PROFILE_DIR", profile.Path(), 1);
		}
	}

	AomiApp app(privat, privDir.String());
	app.Run();
	return 0;
}
