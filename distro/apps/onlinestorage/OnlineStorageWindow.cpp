/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "OnlineStorageWindow.h"

#include "ClientConfig.h"
#include "Dropbox.h"
#include "HttpTransport.h"
#include "OAuth.h"
#include "OneDrive.h"
#include "Sha256.h"
#include "SyncEngine.h"
#include "TokenStore.h"

#include <Alert.h>
#include <Application.h>
#include <OS.h>
#include <Button.h>
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Path.h>
#include <Roster.h>
#include <StringView.h>
#include <TextControl.h>

#include <errno.h>
#include <sys/stat.h>

#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>


enum {
	kMsgOneDrive = 'odrv',
	kMsgDropbox = 'drpb',
	kMsgBrowse = 'brow',
	kMsgFolder = 'fold',
	kMsgSync = 'sync',
	kMsgProgress = 'prog',
	kMsgState = 'stat',
	kMsgService = 'srvc',
	kMsgLargeDelete = 'ldel',
	kMsgLargeDeleteAnswer = 'ldan'
};


struct SignInRequest {
	OnlineStorageWindow*	window;
	BString					provider;
};


struct SyncRequest {
	OnlineStorageWindow*	window;
	BString					provider;
	BString					cloudFolder;
	BString					localFolder;
	BString					statePath;
	bool					allowLargeDelete;
};


/* Waits in 200 ms steps, so Quit does not wait out a Retry-After. */
class StoppableSleeper : public Sleeper {
public:
						StoppableSleeper(volatile bool* stop)
							: fStop(stop) {}
	virtual	void		WaitSeconds(int32 seconds)
						{
							for (int32 i = 0; i < seconds * 5; i++) {
								if (fStop != NULL && *fStop)
									return;
								snooze(200000);
							}
						}

private:
	volatile bool*		fStop;
};


static MemoryTokenStore gTokens;


static status_t
open_browser(const BString& url)
{
	const char* arg = url.String();
	const char* mime = "application/x-vnd.Be.URL.https";
	if (url.FindFirst("http://") == 0)
		mime = "application/x-vnd.Be.URL.http";
	return be_roster->Launch(mime, 1, (char**)&arg);
}


static void
post_state(OnlineStorageWindow* window, const char* text, bool done)
{
	BMessage message(done ? kMsgState : kMsgProgress);
	message.AddString("text", text);
	window->PostMessage(&message);
}


status_t
sign_in_thread(void* data)
{
	SignInRequest* request = (SignInRequest*)data;
	OAuthConfig config = request->provider == "dropbox"
		? DropboxOAuthConfig() : OneDriveOAuthConfig();
	OAuthClient oauth(config, gTokens);
	StderrLog log;
	BString url;
	status_t status = oauth.Begin(url);
	if (status != B_OK) {
		post_state(request->window, "Error: could not start sign-in.", true);
		delete request;
		return status;
	}
	int fd = -1;
	uint16 port = 0;
	status = BindLoopback(fd, (uint16)kOAuthLoopbackPort, port);
	if (status != B_OK) {
		post_state(request->window, "Error: the sign-in port is in use.", true);
		delete request;
		return status;
	}
	if (open_browser(url) != B_OK)
		post_state(request->window, "Open the sign-in page in the browser.", false);
	else
		post_state(request->window, "Waiting for the browser.", false);
	/*
	 * Keep listening until one connection brings the code with our state.
	 * A silent connection, a request for another path or a wrong state
	 * (another program on this computer) is answered and dropped.
	 */
	BString code;
	status = B_CANCELED;
	while (!request->window->_StopRequested()) {
		fd_set ready;
		FD_ZERO(&ready);
		FD_SET(fd, &ready);
		struct timeval wait;
		wait.tv_sec = 0;
		wait.tv_usec = 200000;
		int selected = select(fd + 1, &ready, NULL, NULL, &wait);
		if (selected <= 0)
			continue;
		int client = accept(fd, NULL, NULL);
		if (client < 0)
			continue;
		status = ReadLoopbackCode(client, oauth.State(), code,
			&request->window->fStop);
		close(client);
		if (status == B_OK || status == B_CANCELED)
			break;
	}
	close(fd);
	if (request->window->_StopRequested() || status != B_OK) {
		code.SetTo("");
		delete request;
		return B_CANCELED;
	}
	HttpTransport transport;
	status = oauth.ExchangeCode(transport, code, log);
	code.SetTo("");
	if (status != B_OK)
		post_state(request->window, "Error: the service did not grant access.", true);
	else if (request->provider == "dropbox")
		post_state(request->window, "Dropbox is signed in for this session.", true);
	else
		post_state(request->window, "OneDrive is signed in for this session.", true);
	delete request;
	return status;
}


status_t
sync_thread(void* data)
{
	SyncRequest* request = (SyncRequest*)data;
	StderrLog log;
	StoppableSleeper sleep(&request->window->fStop);
	HttpTransport transport;
	OAuthConfig config = request->provider == "dropbox"
		? DropboxOAuthConfig() : OneDriveOAuthConfig();
	OAuthClient oauth(config, gTokens);
	if (oauth.Load() != B_OK) {
		post_state(request->window, "Error: sign in first.", true);
		delete request;
		return B_NOT_ALLOWED;
	}
	SyncResult result;
	status_t status = B_ERROR;
	if (request->provider == "dropbox") {
		DropboxProvider remote(transport, oauth, sleep, log,
			request->cloudFolder.String());
		SyncEngine engine(remote);
		engine.SetStopFlag(&request->window->fStop);
		engine.SetAllowLargeDelete(request->allowLargeDelete);
		status = engine.Sync(request->localFolder.String(),
			request->statePath.String(), result);
	} else {
		OneDriveProvider remote(transport, oauth, sleep, log,
			request->cloudFolder.String());
		SyncEngine engine(remote);
		engine.SetStopFlag(&request->window->fStop);
		engine.SetAllowLargeDelete(request->allowLargeDelete);
		status = engine.Sync(request->localFolder.String(),
			request->statePath.String(), result);
	}
	if (status == B_NOT_ALLOWED && result.pendingDeletes > 0) {
		BMessage ask(kMsgLargeDelete);
		ask.AddInt32("count", result.pendingDeletes);
		ask.AddBool("cloud", result.pendingDeletesInCloud);
		request->window->PostMessage(&ask);
		delete request;
		return status;
	}
	if (status == B_OK)
		post_state(request->window, "Up to date", true);
	else if (result.error.Length() > 0)
		post_state(request->window, BString("Error: ") << result.error, true);
	else
		post_state(request->window, "Error: sync failed.", true);
	delete request;
	return status;
}


OnlineStorageWindow::OnlineStorageWindow()
	:
	BWindow(BRect(80, 80, 560, 360), "Online Storage", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fPanel(NULL),
	fWorker(-1),
	fStop(false)
{
	fOneDriveButton = new BButton("onedrive", "Sign in…",
		new BMessage(kMsgOneDrive));
	fDropboxButton = new BButton("dropbox", "Sign in…",
		new BMessage(kMsgDropbox));
	BPopUpMenu* services = new BPopUpMenu("service");
	BMenuItem* onedrive = new BMenuItem("OneDrive", new BMessage(kMsgService));
	services->AddItem(onedrive);
	services->AddItem(new BMenuItem("Dropbox", new BMessage(kMsgService)));
	onedrive->SetMarked(true);
	fService = new BMenuField("service", "Sync with:", services);
	fCloudFolder = new BTextControl("cloud", "Cloud folder:", "", NULL);
	fLocalFolder = new BTextControl("local", "Local folder:", "", NULL);
	fState = new BStringView("state", "Not synced yet");
	fSync = new BButton("sync", "Sync now", new BMessage(kMsgSync));
	BButton* browse = new BButton("browse", "Browse…", new BMessage(kMsgBrowse));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(new BStringView("od", "OneDrive"))
			.Add(fOneDriveButton)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(new BStringView("db", "Dropbox"))
			.Add(fDropboxButton)
		.End()
		.Add(fService)
		.Add(fCloudFolder)
		.AddGroup(B_HORIZONTAL)
			.Add(fLocalFolder)
			.Add(browse)
		.End()
		.Add(fState)
		.Add(fSync)
	.End();
	fState->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
}


OnlineStorageWindow::~OnlineStorageWindow()
{
	delete fPanel;
}


void
OnlineStorageWindow::_SetState(const char* text)
{
	if (text != NULL)
		fState->SetText(text);
}


void
OnlineStorageWindow::_SignIn(const char* provider)
{
	if (fWorker > 0)
		return;
	_SetState("Waiting for the browser.");
	SignInRequest* request = new SignInRequest();
	request->window = this;
	request->provider = provider;
	fWorker = spawn_thread(sign_in_thread, "sign-in", B_NORMAL_PRIORITY, request);
	if (fWorker < 0) {
		delete request;
		_SetState("Error: could not start sign-in.");
		return;
	}
	resume_thread(fWorker);
}


/*
 * One state file per service, cloud folder and local folder. A snapshot
 * of one pair must never be compared with another pair: every path in it
 * would look deleted on one side.
 */
static status_t
state_path_for(const BString& provider, const BString& cloudFolder,
	const BString& localFolder, BString& out)
{
	BPath settings;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &settings);
	if (status != B_OK)
		return status;
	BString dir;
	dir << settings.Path() << "/OnlineStorage";
	if (mkdir(dir.String(), 0700) != 0 && errno != EEXIST)
		return B_IO_ERROR;
	BString key;
	key << provider << "\n" << cloudFolder << "\n" << localFolder;
	BString hex;
	status = Sha256Bytes(key.String(), (size_t)key.Length(), hex);
	if (status != B_OK)
		return status;
	out.SetTo("");
	out << dir << "/state-" << hex;
	return B_OK;
}


void
OnlineStorageWindow::_Sync(bool allowLargeDelete)
{
	if (fWorker > 0)
		return;
	if (fCloudFolder->Text()[0] == '\0' || fLocalFolder->Text()[0] == '\0') {
		_SetState("Error: choose both folders.");
		return;
	}
	SyncRequest* request = new SyncRequest();
	request->window = this;
	request->provider = fService->Menu()->IndexOf(
		fService->Menu()->FindMarked()) == 1 ? "dropbox" : "onedrive";
	request->cloudFolder = fCloudFolder->Text();
	request->localFolder = fLocalFolder->Text();
	request->allowLargeDelete = allowLargeDelete;
	if (state_path_for(request->provider, request->cloudFolder,
			request->localFolder, request->statePath) != B_OK) {
		delete request;
		_SetState("Error: could not save the sync state.");
		return;
	}
	_SetState("Syncing");
	fWorker = spawn_thread(sync_thread, "sync", B_NORMAL_PRIORITY, request);
	if (fWorker < 0) {
		delete request;
		_SetState("Error: could not start sync.");
		return;
	}
	resume_thread(fWorker);
}


void
OnlineStorageWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOneDrive:
			_SignIn("onedrive");
			break;
		case kMsgDropbox:
			_SignIn("dropbox");
			break;
		case kMsgBrowse:
			if (fPanel == NULL) {
				fPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
					NULL, B_DIRECTORY_NODE, false, new BMessage(kMsgFolder));
			}
			fPanel->Show();
			break;
		case kMsgFolder:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				fLocalFolder->SetText(path.Path());
			}
			break;
		}
		case kMsgSync:
			_Sync();
			break;
		case kMsgProgress:
			_SetState(message->FindString("text"));
			break;
		case kMsgState:
			_SetState(message->FindString("text"));
			if (fWorker > 0) {
				status_t outcome = B_OK;
				wait_for_thread(fWorker, &outcome);
				fWorker = -1;
			}
			break;
		case kMsgLargeDelete:
		{
			if (fWorker > 0) {
				status_t outcome = B_OK;
				wait_for_thread(fWorker, &outcome);
				fWorker = -1;
			}
			int32 count = message->GetInt32("count", 0);
			bool cloud = message->GetBool("cloud", true);
			BString text;
			text << "This sync would delete " << count << " files "
				<< (cloud ? "in the cloud" : "on this computer")
				<< ", more than half of the folder. Nothing has been changed yet."
				"\n\nIs the right folder chosen? Delete them only if you meant to.";
			_SetState("Waiting for your answer.");
			BAlert* alert = new BAlert("Online Storage", text.String(),
				"Cancel", "Delete files", NULL, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			alert->Go(new BInvoker(new BMessage(kMsgLargeDeleteAnswer), this));
			break;
		}
		case kMsgLargeDeleteAnswer:
			if (message->GetInt32("which", 0) == 1)
				_Sync(true);
			else
				_SetState("Not synced: nothing was deleted.");
			break;
		case kMsgService:
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
OnlineStorageWindow::_StopRequested() const
{
	return fStop;
}


bool
OnlineStorageWindow::QuitRequested()
{
	fStop = true;
	if (fWorker > 0) {
		status_t result = B_OK;
		wait_for_thread(fWorker, &result);
		fWorker = -1;
	}
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
