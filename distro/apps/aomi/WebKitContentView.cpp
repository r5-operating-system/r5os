/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "WebKitContentView.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <UrlContext.h>

#include <WebDownload.h>
#include <WebPage.h>
#include <WebSettings.h>
#include <WebView.h>
#include <WebViewConstants.h>

#include <ctype.h>
#include <new>


static const uint32 kIconReady = 'aIcR';

static const int32 kMaxDownloads = 64;

struct DownloadSlot {
	int32			id;
	BWebDownload*	download;
	BString			path;
	bool			finished;
	bool			failed;
};


static DownloadSlot sDownloads[kMaxDownloads];
static int32 sNextDownload = 1;
static BMessenger sListeners[16];
static int32 sListenerCount = 0;


static void
register_listener(const BMessenger& listener)
{
	if (!listener.IsValid())
		return;
	for (int32 i = 0; i < sListenerCount; i++) {
		if (sListeners[i] == listener)
			return;
	}
	if (sListenerCount < 16)
		sListeners[sListenerCount++] = listener;
}


static void
unregister_listener(const BMessenger& listener)
{
	for (int32 i = 0; i < sListenerCount; i++) {
		if (sListeners[i] != listener)
			continue;
		sListeners[i] = sListeners[sListenerCount - 1];
		sListenerCount--;
		return;
	}
}


static void
notify_listeners(BMessage* message)
{
	for (int32 i = 0; i < sListenerCount; i++)
		sListeners[i].SendMessage(message);
}


static DownloadSlot*
slot_for(int32 id)
{
	for (int32 i = 0; i < kMaxDownloads; i++) {
		if (sDownloads[i].id == id)
			return &sDownloads[i];
	}
	return NULL;
}


static DownloadSlot*
slot_for_download(BWebDownload* download)
{
	for (int32 i = 0; i < kMaxDownloads; i++) {
		if (sDownloads[i].download == download)
			return &sDownloads[i];
	}
	return NULL;
}


static BString
download_directory()
{
	BPath settings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK
		&& settings.Append("WebPositive/Aomi") == B_OK) {
		BFile file(settings.Path(), B_READ_ONLY);
		BMessage message;
		BString path;
		if (file.InitCheck() == B_OK && message.Unflatten(&file) == B_OK
			&& message.FindString("download path", &path) == B_OK
			&& path.Length() > 0) {
			return path;
		}
	}
	BPath desktop;
	if (find_directory(B_DESKTOP_DIRECTORY, &desktop) == B_OK)
		return desktop.Path();
	return BString("/boot/home/Desktop");
}


class DownloadHub : public BHandler {
public:
	DownloadHub()
		:
		BHandler("aomi downloads")
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case B_DOWNLOAD_ADDED:
			{
				BWebDownload* download = NULL;
				if (message->FindPointer("download",
						reinterpret_cast<void**>(&download)) != B_OK
					|| download == NULL) {
					message->SendReply(new BMessage());
					break;
				}
				DownloadSlot* slot = NULL;
				for (int32 i = 0; i < kMaxDownloads; i++) {
					if (sDownloads[i].download == NULL) {
						slot = &sDownloads[i];
						break;
					}
				}
				BMessage reply;
				message->SendReply(&reply);
				if (slot == NULL)
					break;
				slot->id = sNextDownload++;
				slot->download = download;
				slot->finished = false;
				slot->failed = false;
				slot->path = "";
				download->SetProgressListener(BMessenger(this));
				download->Start(BPath(download_directory().String()));
				BMessage added(AOMI_DOWNLOAD_ADDED);
				added.AddInt32("id", slot->id);
				added.AddString("name", download->Filename());
				added.AddInt64("size", download->ExpectedSize());
				notify_listeners(&added);
				break;
			}
			case B_DOWNLOAD_STARTED:
			{
				BString path;
				if (message->FindString("path", &path) != B_OK)
					break;
				DownloadSlot* slot = NULL;
				for (int32 i = 0; i < kMaxDownloads; i++) {
					if (sDownloads[i].download != NULL && !sDownloads[i].finished
						&& sDownloads[i].path.Length() == 0) {
						slot = &sDownloads[i];
						break;
					}
				}
				if (slot == NULL)
					break;
				slot->path = path;
				BMessage added(AOMI_DOWNLOAD_ADDED);
				added.AddInt32("id", slot->id);
				BPath leaf(path.String());
				added.AddString("name", leaf.Leaf());
				added.AddInt64("size", slot->download != NULL
					? slot->download->ExpectedSize() : (int64)-1);
				notify_listeners(&added);
				break;
			}
			case B_DOWNLOAD_PROGRESS:
			{
				int64 current = 0;
				int64 expected = -1;
				message->FindInt64("current size", &current);
				message->FindInt64("expected size", &expected);
				DownloadSlot* slot = NULL;
				for (int32 i = 0; i < kMaxDownloads; i++) {
					if (sDownloads[i].download != NULL && !sDownloads[i].finished
						&& sDownloads[i].path.Length() > 0) {
						slot = &sDownloads[i];
					}
				}
				if (slot == NULL)
					break;
				BMessage progress(AOMI_DOWNLOAD_PROGRESS);
				progress.AddInt32("id", slot->id);
				progress.AddInt64("current", current);
				progress.AddInt64("expected", expected);
				notify_listeners(&progress);
				break;
			}
			case B_DOWNLOAD_REMOVED:
			{
				BWebDownload* download = NULL;
				message->FindPointer("download",
					reinterpret_cast<void**>(&download));
				DownloadSlot* slot = slot_for_download(download);
				if (slot == NULL)
					break;
				slot->finished = true;
				slot->download = NULL;
				BMessage done(slot->failed
					? AOMI_DOWNLOAD_FAILED : AOMI_DOWNLOAD_FINISHED);
				done.AddInt32("id", slot->id);
				if (slot->failed)
					done.AddString("text", "Failed");
				else
					done.AddString("path", slot->path);
				notify_listeners(&done);
				break;
			}
			default:
				BHandler::MessageReceived(message);
				break;
		}
	}
};


static DownloadHub* sHub = NULL;


static void
ensure_hub()
{
	if (sHub != NULL || be_app == NULL)
		return;
	sHub = new DownloadHub();
	be_app->Lock();
	be_app->AddHandler(sHub);
	be_app->Unlock();
	BWebPage::SetDownloadListener(BMessenger(sHub));
}


status_t
aomi_download_cancel(int32 id)
{
	DownloadSlot* slot = slot_for(id);
	if (slot == NULL || slot->download == NULL)
		return B_BAD_INDEX;
	slot->download->Cancel();
	return B_OK;
}


status_t
aomi_download_open(int32 id)
{
	DownloadSlot* slot = slot_for(id);
	if (slot == NULL || slot->path.Length() == 0)
		return B_BAD_INDEX;
	entry_ref ref;
	status_t status = get_ref_for_path(slot->path.String(), &ref);
	if (status != B_OK)
		return status;
	status = be_roster->Launch(&ref);
	if (status == B_ALREADY_RUNNING)
		return B_OK;
	return status;
}


status_t
aomi_download_show(int32 id)
{
	DownloadSlot* slot = slot_for(id);
	if (slot == NULL || slot->path.Length() == 0)
		return B_BAD_INDEX;
	BPath path(slot->path.String());
	path.GetParent(&path);
	entry_ref ref;
	status_t status = get_ref_for_path(path.Path(), &ref);
	if (status != B_OK)
		return status;
	status = be_roster->Launch(&ref);
	if (status == B_ALREADY_RUNNING)
		return B_OK;
	return status;
}


status_t
aomi_webkit_start()
{
	static bool started = false;
	if (started)
		return B_OK;

	BPath settings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK) {
		settings.Append("WebPositive");
		create_directory(settings.Path(), 0777);
		BString cookies(settings.Path());
		cookies << "/cookie.jar.db";
		setenv("CURL_COOKIE_JAR_PATH", cookies.String(), 1);
		BWebSettings::SetPersistentStoragePath(settings.Path());
	}

	BWebPage::InitializeOnce();
	BWebPage::SetCacheModel(B_WEBKIT_CACHE_MODEL_WEB_BROWSER);
	ensure_hub();
	started = true;
	return B_OK;
}


static void
append_encoded(BString* url, const char* html)
{
	static const char* hex = "0123456789ABCDEF";
	for (const unsigned char* p = (const unsigned char*)html; *p != 0; p++) {
		unsigned char c = *p;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
			|| c == '~') {
			char text[2] = { (char)c, 0 };
			*url << text;
		} else {
			char text[4] = { '%', hex[c >> 4], hex[c & 0xf], 0 };
			*url << text;
		}
	}
}


BWebKitContentView::BWebKitContentView(const char* name, BWebSession* session)
	:
	BWebContentView(name, session),
	fWeb(NULL),
	fZoom(1.0f),
	fTextOnly(false)
{
	bool priv = session != NULL && session->IsPrivate();
	BPrivate::Network::BUrlContext* context = priv
		? new BPrivate::Network::BUrlContext() : NULL;
	_Adopt(new BWebView(name != NULL ? name : "page", context));
}


BWebKitContentView::BWebKitContentView(const char* name, BWebSession* session,
	BWebView* adopt)
	:
	BWebContentView(name, session),
	fWeb(NULL),
	fZoom(1.0f),
	fTextOnly(false)
{
	_Adopt(adopt);
}


BWebKitContentView::~BWebKitContentView()
{
	unregister_listener(fListener);
	ShutdownEngine();
}


void
BWebKitContentView::_Adopt(BWebView* web)
{
	fWeb = web;
	if (fWeb == NULL)
		return;
	fWeb->SetResizingMode(B_FOLLOW_ALL_SIDES);
	AddChild(fWeb);
	_Fit();
}


void
BWebKitContentView::_Fit()
{
	if (fWeb != NULL && fWeb->Parent() == this)
		fWeb->MoveTo(0, 0);
	if (fWeb != NULL && fWeb->Parent() == this)
		fWeb->ResizeTo(Bounds().Width(), Bounds().Height());
}


void
BWebKitContentView::_Listen()
{
	if (fWeb != NULL && fWeb->WebPage() != NULL)
		fWeb->WebPage()->SetListener(BMessenger(this));
	ensure_hub();
	register_listener(fListener);
	if (fWeb != NULL)
		fWeb->SetDarkMode(_Dark());
}


bool
BWebKitContentView::_Dark() const
{
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	return panel.red + panel.green + panel.blue < 128 * 3;
}


void
BWebKitContentView::AttachedToWindow()
{
	BWebContentView::AttachedToWindow();
	_Listen();
	_Fit();
}


void
BWebKitContentView::FrameResized(float width, float height)
{
	BWebContentView::FrameResized(width, height);
	_Fit();
}


void
BWebKitContentView::ShutdownEngine()
{
	if (fWeb == NULL)
		return;
	BWebView* web = fWeb;
	fWeb = NULL;
	if (web->Parent() == this)
		RemoveChild(web);
	web->Shutdown();
}


void
BWebKitContentView::_Deliver(BMessage* message)
{
	BLooper* looper = NULL;
	BHandler* handler = fListener.Target(&looper);
	if (handler != NULL && looper != NULL && looper == Looper()) {
		handler->MessageReceived(message);
		return;
	}
	fListener.SendMessage(message);
}


void
BWebKitContentView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case LOAD_NEGOTIATING:
		case LOAD_STARTED:
		{
			BString url;
			message->FindString("url", &url);
			BMessage report(AOMI_LOAD_STARTED);
			report.AddString("url", url);
			Report(&report);
			break;
		}
		case LOAD_COMMITTED:
		{
			BString url;
			message->FindString("url", &url);
			BMessage report(AOMI_LOAD_COMMITTED);
			report.AddString("url", url);
			Report(&report);
			report.what = AOMI_URL_CHANGED;
			Report(&report);
			int32 state = AOMI_SECURITY_UNKNOWN;
			if (url.StartsWith("https:"))
				state = AOMI_SECURITY_SECURE;
			else if (url.StartsWith("http:"))
				state = AOMI_SECURITY_NOT_SECURE;
			BMessage security(AOMI_SECURITY);
			security.AddInt32("state", state);
			security.AddString("text", "");
			Report(&security);
			break;
		}
		case LOAD_DOC_COMPLETED:
		case LOAD_FINISHED:
		case LOAD_DL_COMPLETED:
		{
			BString url;
			message->FindString("url", &url);
			BMessage report(AOMI_LOAD_FINISHED);
			report.AddString("url", url);
			Report(&report);
			if (message->what == LOAD_DL_COMPLETED) {
				BMessage progress(AOMI_PROGRESS);
				progress.AddFloat("progress", 1.0f);
				Report(&progress);
			}
			break;
		}
		case LOAD_FAILED:
		case MAIN_DOCUMENT_ERROR:
		{
			BString url;
			BString text;
			message->FindString("url", &url);
			message->FindString("error", &text);
			BMessage report(AOMI_LOAD_FAILED);
			report.AddString("url", url);
			report.AddString("text", text);
			report.AddInt32("error", (int32)message->what);
			Report(&report);
			break;
		}
		case LOAD_PROGRESS:
		{
			float progress = 0;
			if (message->FindFloat("progress", &progress) == B_OK) {
				if (progress > 1.0f)
					progress /= 100.0f;
				BMessage report(AOMI_PROGRESS);
				report.AddFloat("progress", progress);
				Report(&report);
			}
			break;
		}
		case TITLE_CHANGED:
		{
			BString title;
			message->FindString("title", &title);
			BMessage report(AOMI_TITLE_CHANGED);
			report.AddString("title", title);
			Report(&report);
			break;
		}
		case UPDATE_NAVIGATION_INTERFACE:
		{
			bool back = false;
			bool forward = false;
			bool stop = false;
			message->FindBool("can go backward", &back);
			message->FindBool("can go forward", &forward);
			message->FindBool("can stop", &stop);
			BMessage report(AOMI_NAVIGATION);
			report.AddBool("canBack", back);
			report.AddBool("canForward", forward);
			report.AddBool("loading", stop);
			Report(&report);
			break;
		}
		case SET_STATUS_TEXT:
		{
			BString text;
			message->FindString("text", &text);
			BMessage report(AOMI_HOVER_LINK);
			report.AddString("url", text);
			Report(&report);
			break;
		}
		case ICON_RECEIVED:
		{
			BString url;
			if (message->FindString("url", &url) == B_OK) {
				BMessage reply(kIconReady);
				BWebSettings::SendIconForURL(url, reply, BMessenger(this));
			}
			break;
		}
		case kIconReady:
		{
			BMessage icon;
			if (message->FindMessage("icon", &icon) == B_OK) {
				BMessage report(AOMI_FAVICON);
				report.AddMessage("icon", &icon);
				Report(&report);
			}
			break;
		}
		case SSL_CERT_ERROR:
		{
			BMessage dialog(AOMI_DIALOG);
			dialog.AddInt32("kind", AOMI_DIALOG_ALERT);
			BString text;
			message->FindString("text", &text);
			dialog.AddString("text", text);
			dialog.AddInt32("state", AOMI_SECURITY_CERTIFICATE);
			_Deliver(&dialog);
			bool cont = false;
			dialog.FindBool("continue", &cont);
			BMessage reply;
			reply.AddBool("continue", cont);
			message->SendReply(&reply);
			BMessage security(AOMI_SECURITY);
			security.AddInt32("state", AOMI_SECURITY_CERTIFICATE);
			security.AddString("text", text);
			Report(&security);
			break;
		}
		case SHOW_JS_ALERT:
		{
			BMessage dialog(AOMI_DIALOG);
			dialog.AddInt32("kind", AOMI_DIALOG_ALERT);
			BString text;
			message->FindString("text", &text);
			dialog.AddString("text", text);
			_Deliver(&dialog);
			break;
		}
		case SHOW_JS_CONFIRM:
		{
			BMessage dialog(AOMI_DIALOG);
			dialog.AddInt32("kind", AOMI_DIALOG_CONFIRM);
			BString text;
			message->FindString("text", &text);
			dialog.AddString("text", text);
			_Deliver(&dialog);
			bool result = false;
			dialog.FindBool("result", &result);
			BMessage reply;
			reply.AddBool("result", result);
			message->SendReply(&reply);
			break;
		}
		case AUTHENTICATION_CHALLENGE:
		{
			BMessage dialog(AOMI_DIALOG);
			dialog.AddInt32("kind", AOMI_DIALOG_AUTH);
			BString text;
			BString user;
			BString password;
			message->FindString("text", &text);
			message->FindString("user", &user);
			message->FindString("password", &password);
			dialog.AddString("text", text);
			dialog.AddString("user", user);
			dialog.AddString("password", password);
			_Deliver(&dialog);
			bool handled = false;
			dialog.FindBool("handled", &handled);
			BMessage reply;
			if (handled) {
				dialog.FindString("user", &user);
				dialog.FindString("password", &password);
				bool remember = false;
				dialog.FindBool("remember", &remember);
				reply.AddString("user", user);
				reply.AddString("password", password);
				reply.AddBool("rememberCredentials", remember);
			}
			message->SendReply(&reply);
			break;
		}
		case NEW_WINDOW_REQUESTED:
		{
			BString url;
			bool primary = true;
			message->FindString("url", &url);
			message->FindBool("primary", &primary);
			BMessage report(AOMI_NEW_WINDOW);
			report.AddString("url", url);
			report.AddBool("primary", primary);
			Report(&report);
			break;
		}
		case NEW_PAGE_CREATED:
		{
			BWebView* view = NULL;
			message->FindPointer("view", reinterpret_cast<void**>(&view));
			if (view != NULL) {
				BWebKitContentView* wrap = new BWebKitContentView("page",
					fSession, view);
				wrap->SetListener(fListener);
				BMessage report(AOMI_NEW_WINDOW);
				report.AddPointer("view", wrap);
				report.AddBool("primary", true);
				_Deliver(&report);
			}
			break;
		}
		case CLOSE_WINDOW_REQUESTED:
		{
			BMessage report(AOMI_CLOSE_WANTED);
			Report(&report);
			break;
		}
		case B_PAGE_SOURCE_RESULT:
		{
			BString source;
			if (message->FindString("source", &source) == B_OK) {
				BPath path;
				if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &path) == B_OK) {
					path.Append("aomi-source.html");
					BFile file(path.Path(),
						B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
					if (file.InitCheck() == B_OK) {
						file.Write(source.String(), source.Length());
						entry_ref ref;
						if (get_ref_for_path(path.Path(), &ref) == B_OK)
							be_roster->Launch(&ref);
					}
				}
			}
			break;
		}
		case B_COLORS_UPDATED:
			if (fWeb != NULL)
				fWeb->SetDarkMode(_Dark());
			BWebContentView::MessageReceived(message);
			break;
		default:
			BWebContentView::MessageReceived(message);
			break;
	}
}


status_t
BWebKitContentView::LoadURL(const char* url)
{
	if (url == NULL || url[0] == '\0')
		return B_BAD_VALUE;
	if (strncmp(url, "aomi:", 5) == 0) {
		const char* html = "<!doctype html><title>Aomi</title>"
			"<p style='font-family:sans-serif'>Aomi</p>";
		return LoadHTML(html, url);
	}
	if (fWeb == NULL)
		return B_NO_INIT;
	fWeb->LoadURL(url);
	return B_OK;
}


status_t
BWebKitContentView::LoadHTML(const char* html, const char* baseURL)
{
	if (html == NULL || fWeb == NULL)
		return B_BAD_VALUE;
	BString url("data:text/html;charset=utf-8,");
	append_encoded(&url, html);
	fWeb->LoadURL(url.String());
	(void)baseURL;
	return B_OK;
}


status_t
BWebKitContentView::Reload(bool bypassCache)
{
	if (fWeb == NULL)
		return B_NO_INIT;
	// BWebView::Reload() has no cache flag.
	if (bypassCache)
		return B_NOT_SUPPORTED;
	fWeb->Reload();
	return B_OK;
}


status_t
BWebKitContentView::StopLoading()
{
	if (fWeb == NULL)
		return B_NO_INIT;
	fWeb->StopLoading();
	return B_OK;
}


status_t
BWebKitContentView::GoBack()
{
	if (fWeb == NULL)
		return B_NO_INIT;
	fWeb->GoBack();
	return B_OK;
}


status_t
BWebKitContentView::GoForward()
{
	if (fWeb == NULL)
		return B_NO_INIT;
	fWeb->GoForward();
	return B_OK;
}


status_t
BWebKitContentView::GetBackList(BMessage* items)
{
	(void)items;
	// The 1.10.0 public headers have no back-forward list.
	return B_NOT_SUPPORTED;
}


status_t
BWebKitContentView::GetForwardList(BMessage* items)
{
	(void)items;
	return B_NOT_SUPPORTED;
}


status_t
BWebKitContentView::URL(BString* url) const
{
	if (url == NULL || fWeb == NULL)
		return B_BAD_VALUE;
	*url = fWeb->MainFrameURL();
	return B_OK;
}


status_t
BWebKitContentView::Title(BString* title) const
{
	if (title == NULL || fWeb == NULL)
		return B_BAD_VALUE;
	*title = fWeb->MainFrameTitle();
	return B_OK;
}


status_t
BWebKitContentView::SetZoom(float factor, bool textOnly)
{
	if (fWeb == NULL)
		return B_NO_INIT;
	fTextOnly = textOnly;
	if (factor < 0.3f)
		factor = 0.3f;
	if (factor > 5.0f)
		factor = 5.0f;
	if (factor > 0.95f && factor < 1.05f) {
		fWeb->ResetZoomFactor();
		fZoom = 1.0f;
		return B_OK;
	}
	int32 steps = 0;
	while (fZoom < factor - 0.08f && steps < 8) {
		fWeb->IncreaseZoomFactor(textOnly);
		fZoom *= 1.2f;
		steps++;
	}
	while (fZoom > factor + 0.08f && steps < 8) {
		fWeb->DecreaseZoomFactor(textOnly);
		fZoom /= 1.2f;
		steps++;
	}
	return B_OK;
}


status_t
BWebKitContentView::Find(const char* text, bool forward, bool matchCase)
{
	if (fWeb == NULL)
		return B_NO_INIT;
	fWeb->FindString(text != NULL ? text : "", forward, matchCase, true,
		true);
	return B_OK;
}


status_t
BWebKitContentView::SavePage(const char* path)
{
	if (path == NULL || fWeb == NULL || fWeb->WebPage() == NULL)
		return B_BAD_VALUE;
	BFile file(path, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();
	return fWeb->WebPage()->GetContentsAsMHTML(file);
}


status_t
BWebKitContentView::ViewSource()
{
	if (fWeb == NULL || fWeb->WebPage() == NULL)
		return B_NO_INIT;
	fWeb->WebPage()->SendPageSource();
	return B_OK;
}


status_t
BWebKitContentView::Print()
{
	return B_NOT_SUPPORTED;
}


status_t
BWebKitContentView::Protocol(BString* protocol) const
{
	// haikuwebkit 1.10.0, FrameLoaderClientHaiku::dispatchDidReceiveResponse:
	// RESPONSE_RECEIVED carries status, identifier, url and mimeType.
	// The HTTP version curl negotiated is not in that message, and it is
	// not on BWebView or BWebPage. Curl still asks for HTTP/2 on https
	// when CURL_VERSION_HTTP2 is set (docs/AOMI.md, "HTTP/2").
	(void)protocol;
	return B_NOT_SUPPORTED;
}


status_t
BWebKitContentView::Cancel(int32 id)
{
	return aomi_download_cancel(id);
}


status_t
BWebKitContentView::Open(int32 id)
{
	return aomi_download_open(id);
}


status_t
BWebKitContentView::ShowInTracker(int32 id)
{
	return aomi_download_show(id);
}
