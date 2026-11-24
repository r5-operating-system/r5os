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
#include "SyncEngine.h"
#include "TokenStore.h"

#include <Application.h>
#include <OS.h>
#include <Button.h>
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <Roster.h>
#include <StringView.h>
#include <TextControl.h>

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
	kMsgState = 'stat'
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
	int client = -1;
	while (!request->window->_StopRequested()) {
		fd_set ready;
		FD_ZERO(&ready);
		FD_SET(fd, &ready);
		struct timeval wait;
		wait.tv_sec = 0;
		wait.tv_usec = 200000;
		int selected = select(fd + 1, &ready, NULL, NULL, &wait);
		if (selected > 0) {
			client = accept(fd, NULL, NULL);
			break;
		}
	}
	close(fd);
	if (request->window->_StopRequested()) {
		if (client >= 0)
			close(client);
		delete request;
		return B_CANCELED;
	}
	if (client < 0) {
		post_state(request->window, "Error: sign-in was interrupted.", true);
		delete request;
		return B_CANCELED;
	}
	BString code;
	status = ReadLoopbackCode(client, oauth.State(), code,
		&request->window->fStop);
	close(client);
	if (request->window->_StopRequested()) {
		delete request;
		return B_CANCELED;
	}
	if (status != B_OK) {
		post_state(request->window, "Error: the sign-in page was not accepted.", true);
		delete request;
		return status;
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


static status_t
sync_thread(void* data)
{
	SyncRequest* request = (SyncRequest*)data;
	StderrLog log;
	RealSleeper sleep;
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
		status = engine.Sync(request->localFolder.String(),
			request->statePath.String(), result);
	} else {
		OneDriveProvider remote(transport, oauth, sleep, log,
			request->cloudFolder.String());
		SyncEngine engine(remote);
		status = engine.Sync(request->localFolder.String(),
			request->statePath.String(), result);
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


void
OnlineStorageWindow::_Sync()
{
	if (fWorker > 0)
		return;
	if (fCloudFolder->Text()[0] == '\0' || fLocalFolder->Text()[0] == '\0') {
		_SetState("Error: choose both folders.");
		return;
	}
	BPath settings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) != B_OK)
		settings.SetTo("/tmp");
	BString state;
	state << settings.Path() << "/onlinestorage-state";
	SyncRequest* request = new SyncRequest();
	request->window = this;
	request->provider = "onedrive";
	BMessage token;
	if (gTokens.Load("dropbox", token) == B_OK
		&& gTokens.Load("onedrive", token) != B_OK)
		request->provider = "dropbox";
	request->cloudFolder = fCloudFolder->Text();
	request->localFolder = fLocalFolder->Text();
	request->statePath = state;
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
