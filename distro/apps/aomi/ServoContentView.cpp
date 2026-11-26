/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "ServoContentView.h"

#include <Application.h>
#include <Bitmap.h>
#include <ByteOrder.h>
#include <Catalog.h>
#include <Handler.h>
#include <Looper.h>
#include <OS.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Locker.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>

#include <String.h>
#include <Window.h>

#include <R5Icons.h>

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Aomi"


// Matches r5_servo_abi.h in servo-haiku (distro/servo/servo-haiku.ref)
enum {
	R5_EV_LOAD_STARTED		= 1,
	R5_EV_LOAD_COMMITTED	= 2,
	R5_EV_LOAD_FINISHED		= 3,
	R5_EV_LOAD_FAILED		= 4,
	R5_EV_NAVIGATION		= 5,
	R5_EV_TITLE				= 6,
	R5_EV_URL				= 7,
	R5_EV_HOVER				= 8,
	R5_EV_PROTOCOL			= 9,
	R5_EV_PROGRESS			= 10,
	R5_EV_CURSOR			= 11,
	R5_EV_FULLSCREEN		= 12,
	R5_EV_NEW_WINDOW		= 13,
	R5_EV_CLOSE				= 14,
	R5_EV_FAVICON			= 15,
	R5_EV_DIALOG			= 16,
	R5_EV_PAGE_TEXT			= 17
};

struct r5_servo;

struct r5_servo_event {
	int32		kind;
	const char*	url;
	const char*	text;
	float		progress;
	int32		a;
	int32		b;
	int32		c;
	int64		current;
	int64		expected;
	const uint8* favicon;
	int32		favicon_width;
	int32		favicon_height;
	int32		favicon_format;
};

typedef void (*r5_servo_callback)(void* context, const r5_servo_event* event);

struct r5_servo_control_labels {
	const char*	ok;
	const char*	cancel;
	const char*	user;
	const char*	password;
	const char*	signIn;
	const char*	signInTo;
};

struct ServoApi {
	void*		handle;
	r5_servo*	(*create)(int32, int32, const char*, int32,
					r5_servo_callback, void*);
	void		(*resize)(r5_servo*, int32, int32);
	status_t	(*load_url)(r5_servo*, const char*);
	status_t	(*load_html)(r5_servo*, const char*);
	status_t	(*reload)(r5_servo*, int32);
	status_t	(*stop)(r5_servo*);
	status_t	(*back)(r5_servo*);
	status_t	(*forward)(r5_servo*);
	status_t	(*zoom)(r5_servo*, float, int32);
	status_t	(*focus)(r5_servo*, int32);
	status_t	(*frame)(r5_servo*, const uint8**, int32*, int32*);
	status_t	(*history)(r5_servo*, int32, int32, char*, int32);
	status_t	(*gl)(r5_servo*, char*, int32);
	status_t	(*find)(r5_servo*, const char*, int32, int32);
	void		(*destroy)(r5_servo*);
	// Since r5servo 0.8.0-3.
	void		(*set_waker)(void (*)(void*), void*);
	int32		(*pump)();
	int32		(*paint)(r5_servo*);
	status_t	(*key)(r5_servo*, int32, int32, const char*, uint32, int32);
	void		(*set_max_size)(int32, int32);
	status_t	(*control_answer)(r5_servo*, int64, int32, int32, const char*);
	status_t	(*input_message)(r5_servo*, const BMessage*, float, float);
	status_t	(*ask_control)(BView*, int32, const char*, float, float,
					const r5_servo_control_labels*, int32*, int32*, char**);
	void		(*free_answer)(char*);
	status_t	(*set_callback)(r5_servo*, r5_servo_callback, void*);
	status_t	(*set_visible)(r5_servo*, int32);
	status_t	(*editing_action)(r5_servo*, int32);
	void		(*shutdown)();
	// Since r5servo 0.8.0-4.
	r5_servo*	(*create_sharing)(r5_servo*, int32, int32, const char*, int32,
					r5_servo_callback, void*);
};

static ServoApi sApi = {};
static bool sStarted = false;
static bool sAvailable = false;
static BString sError;
static BLocker sInfoLock("aomi info");
static BString sGlInfo;
static BString sLastProtocol;

static BLocker sLogLock("aomi log");
static std::vector<BString> sLogRing;
static const size_t kMaxLogLines = 64;

struct DownloadItem {
	int32	id;
	BString	name;
	BString	path;
	bool	done;
	bool	failed;
};
static BLocker sDlLock("aomi dl");
static std::vector<DownloadItem> sDownloads;
static int32 sNextDlId = 1;


// The engine thread.
//
// Servo runs one instance per process, and every call into it must come from
// the thread that built it (its objects are not thread safe). All windows of
// this process share that one Servo, so all calls go through one looper,
// ServoEngine. A view sends it messages; the engine thread posts events and
// "new frame" back to the view. A tab's frame is copied, on the engine
// thread, into ServoTab::frame under ServoTab::lock; the view draws it from
// there on its own thread.

struct ServoTab {
	ServoTab()
		:
		engine(NULL),
		lock("servo tab"),
		frame(NULL),
		visible(false),
		framePending(0),
		parent(NULL),
		adopted(true)
	{
	}

	~ServoTab()
	{
		delete frame;
		for (size_t i = 0; i < outbox.size(); i++)
			delete outbox[i];
	}

	r5_servo*	engine;
	BMessenger	view;
	BLocker		lock;
	BBitmap*	frame;
	bool		visible;
	int32		framePending;
	// Events not yet delivered to the view, in order. Within one team a
	// message goes straight into the target's queue and never waits on a
	// full port (Message.cpp, _SendMessage, direct target), so a send fails
	// only when the view is gone. Engine thread only.
	std::vector<BMessage*> outbox;
	// A tab Servo made for a page (window.open()) until a view adopts it:
	// the tab that opened it. Destroyed with that tab if never adopted.
	ServoTab*	parent;
	bool		adopted;
};


// Delivers a tab's waiting events in order, without blocking; what does not
// fit stays for the next pump. Engine thread only.
static void
deliver_outbox(ServoTab* tab)
{
	while (!tab->outbox.empty()) {
		if (!tab->view.IsValid()) {
			for (size_t i = 0; i < tab->outbox.size(); i++)
				delete tab->outbox[i];
			tab->outbox.clear();
			return;
		}
		if (tab->view.SendMessage(tab->outbox.front(), (BHandler*)NULL, 0)
				!= B_OK)
			return;
		delete tab->outbox.front();
		tab->outbox.erase(tab->outbox.begin());
	}
}


// Messages to the engine looper. Sync ones wait for a reply.
enum {
	kEngCreate		= 'seCr',	// sync
	kEngAdopt		= 'seAd',	// sync
	kEngDestroy		= 'seDs',	// sync
	kEngCall		= 'seCl',
	kEngInput		= 'seIn',
	kEngHistory		= 'seHi',	// sync
	kEngAnswer		= 'seAn',
	kEngWake		= 'seWk',
	kEngTick		= 'seTk',
	kEngLater		= 'seLt',
	kEngShutdown	= 'seSd'	// sync
};

// kEngCall "op".
enum {
	kOpLoadURL = 1,
	kOpLoadHTML,
	kOpReload,
	kOpStop,
	kOpBack,
	kOpForward,
	kOpZoom,
	kOpFocus,
	kOpResize,
	kOpFind,
	kOpVisible,
	kOpEdit,
	kOpSelectAll
};

// Messages from the engine thread to a view.
static const uint32 kViewEvent = 'svEv';
static const uint32 kViewFrame = 'svFr';

static const bigtime_t kFrameTime = 16000;
static const bigtime_t kTickTime = 250000;

class ServoEngine;
static ServoEngine* sEngine = NULL;
static BMessenger sEngineMessenger;
static int32 sWakePending = 0;


// Called from Servo's threads when it has work. sEngineMessenger is set
// before the waker and never changes afterwards.
static void
servo_wake(void*)
{
	if (atomic_get_and_set(&sWakePending, 1) != 0)
		return;
	BMessage wake(kEngWake);
	if (sEngineMessenger.SendMessage(&wake, (BHandler*)NULL, 0) != B_OK)
		atomic_set(&sWakePending, 0);
}


static void tab_callback(void* context, const r5_servo_event* event);


class ServoEngine : public BLooper {
public:
	ServoEngine()
		:
		BLooper("servo engine", B_DISPLAY_PRIORITY),
		fLastPump(0),
		fLater(NULL),
		fTick(NULL)
	{
	}

	virtual ~ServoEngine()
	{
		delete fLater;
		delete fTick;
	}

	void StartTick()
	{
		BMessage tick(kEngTick);
		fTick = new BMessageRunner(BMessenger(this), &tick, kTickTime);
	}

	// Called on the engine thread from tab_callback for a page's
	// window.open(): the new engine gets a tab without a view until a view
	// adopts it.
	ServoTab* AddChild(ServoTab* parent, r5_servo* engine)
	{
		ServoTab* tab = new ServoTab();
		tab->engine = engine;
		tab->parent = parent;
		tab->adopted = false;
		fTabs.push_back(tab);
		return tab;
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kEngCreate:
				_Create(message);
				return;
			case kEngAdopt:
				_Adopt(message);
				return;
			case kEngDestroy:
				_Destroy(message);
				return;
			case kEngCall:
				_Call(message);
				_PumpSoon();
				return;
			case kEngInput:
			{
				ServoTab* tab = _Tab(message);
				BMessage event;
				float x = 0;
				float y = 0;
				message->FindFloat("x", &x);
				message->FindFloat("y", &y);
				if (tab != NULL && tab->engine != NULL
					&& message->FindMessage("event", &event) == B_OK)
					sApi.input_message(tab->engine, &event, x, y);
				_PumpSoon();
				return;
			}
			case kEngHistory:
				_History(message);
				return;
			case kEngAnswer:
			{
				ServoTab* tab = _Tab(message);
				// The answer buffer is handed over, not copied: it may hold
				// a password, and r5_servo_free_answer() clears it.
				char* answer = NULL;
				message->FindPointer("answer", (void**)&answer);
				if (tab != NULL && tab->engine != NULL) {
					sApi.control_answer(tab->engine,
						message->GetInt64("control", 0),
						message->GetBool("accept", false) ? 1 : 0,
						message->GetInt32("value", -1), answer);
				}
				sApi.free_answer(answer);
				_PumpSoon();
				return;
			}
			case kEngWake:
			case kEngTick:
			case kEngLater:
			{
				if (message->what == kEngWake)
					atomic_set(&sWakePending, 0);
				if (message->what == kEngLater) {
					delete fLater;
					fLater = NULL;
				}
				bigtime_t now = system_time();
				if (now - fLastPump < kFrameTime) {
					// At most one pump per frame; the rest waits for it.
					if (fLater == NULL) {
						BMessage later(kEngLater);
						fLater = new BMessageRunner(BMessenger(this), &later,
							kFrameTime - (now - fLastPump), 1);
					}
					return;
				}
				fLastPump = now;
				_Pump();
				return;
			}
			case kEngShutdown:
			{
				for (size_t i = 0; i < fTabs.size(); i++) {
					if (fTabs[i]->engine != NULL)
						sApi.destroy(fTabs[i]->engine);
					delete fTabs[i];
				}
				fTabs.clear();
				if (sApi.shutdown != NULL)
					sApi.shutdown();
				message->SendReply(B_REPLY);
				return;
			}
		}
		BLooper::MessageReceived(message);
	}

private:
	ServoTab* _Tab(BMessage* message)
	{
		ServoTab* tab = NULL;
		if (message->FindPointer("tab", (void**)&tab) != B_OK)
			return NULL;
		for (size_t i = 0; i < fTabs.size(); i++) {
			if (fTabs[i] == tab)
				return tab;
		}
		return NULL;
	}

	void _Create(BMessage* message)
	{
		BMessage reply(B_REPLY);
		ServoTab* tab = new ServoTab();
		message->FindMessenger("view", &tab->view);
		tab->visible = message->GetBool("visible", false);
		const char* document = message->GetString("document", "");
		sApi.set_max_size(message->GetInt32("maxWidth", 0),
			message->GetInt32("maxHeight", 0));
		// Another tab of the same window, if it still exists: the new tab
		// draws with its context, so Servo does not compile its shaders
		// again (r5_servo_create_sharing()).
		ServoTab* sibling = NULL;
		message->FindPointer("sibling", (void**)&sibling);
		r5_servo* siblingEngine = NULL;
		for (size_t i = 0; i < fTabs.size(); i++) {
			if (fTabs[i] == sibling)
				siblingEngine = sibling->engine;
		}
		tab->engine = sApi.create_sharing(siblingEngine,
			message->GetInt32("width", 16), message->GetInt32("height", 16),
			document, message->GetBool("html", false) ? 1 : 0, &tab_callback,
			tab);
		if (tab->engine == NULL) {
			delete tab;
			reply.AddPointer("tab", NULL);
		} else {
			fTabs.push_back(tab);
			sApi.set_visible(tab->engine, tab->visible ? 1 : 0);
			char gl[512];
			if (sApi.gl != NULL
				&& sApi.gl(tab->engine, gl, (int32)sizeof(gl)) == 0) {
				sInfoLock.Lock();
				sGlInfo = gl;
				sInfoLock.Unlock();
				aomi_log_add(gl);
			}
			reply.AddPointer("tab", tab);
		}
		message->SendReply(&reply);
		_PumpSoon();
	}

	void _Adopt(BMessage* message)
	{
		ServoTab* tab = _Tab(message);
		if (tab != NULL && tab->engine != NULL) {
			message->FindMessenger("view", &tab->view);
			tab->adopted = true;
			tab->parent = NULL;
			tab->visible = message->GetBool("visible", false);
			sApi.set_visible(tab->engine, tab->visible ? 1 : 0);
			// Its waiting events follow, to the view.
			sApi.set_callback(tab->engine, &tab_callback, tab);
		}
		message->SendReply(B_REPLY);
		_PumpSoon();
	}

	void _Destroy(BMessage* message)
	{
		ServoTab* tab = _Tab(message);
		if (tab != NULL)
			_DestroyTab(tab);
		message->SendReply(B_REPLY);
	}

	// Destroys a tab and the tabs it opened that no view adopted (their
	// event may have been lost with the closed view).
	void _DestroyTab(ServoTab* tab)
	{
		for (size_t i = 0; i < fTabs.size();) {
			if (fTabs[i]->parent == tab && !fTabs[i]->adopted)
				_DestroyTab(fTabs[i]);
			else
				i++;
		}
		for (size_t i = 0; i < fTabs.size(); i++) {
			if (fTabs[i] == tab) {
				fTabs.erase(fTabs.begin() + i);
				break;
			}
		}
		// After this no event names the tab.
		if (tab->engine != NULL)
			sApi.destroy(tab->engine);
		delete tab;
	}

	void _Call(BMessage* message)
	{
		ServoTab* tab = _Tab(message);
		if (tab == NULL || tab->engine == NULL)
			return;
		r5_servo* engine = tab->engine;
		const char* text = message->GetString("text", "");
		switch (message->GetInt32("op", 0)) {
			case kOpLoadURL:
				sApi.load_url(engine, text);
				break;
			case kOpLoadHTML:
				sApi.load_html(engine, text);
				break;
			case kOpReload:
				sApi.reload(engine, 0);
				break;
			case kOpStop:
				sApi.stop(engine);
				break;
			case kOpBack:
				sApi.back(engine);
				break;
			case kOpForward:
				sApi.forward(engine);
				break;
			case kOpZoom:
				sApi.zoom(engine, message->GetFloat("value", 1.0f), 0);
				break;
			case kOpFocus:
				sApi.focus(engine, message->GetBool("on", false) ? 1 : 0);
				break;
			case kOpResize:
				sApi.resize(engine, message->GetInt32("width", 16),
					message->GetInt32("height", 16));
				break;
			case kOpFind:
				sApi.find(engine, text,
					message->GetBool("forward", true) ? 1 : 0,
					message->GetBool("matchCase", false) ? 1 : 0);
				break;
			case kOpVisible:
				tab->visible = message->GetBool("on", false);
				sApi.set_visible(engine, tab->visible ? 1 : 0);
				// A tab shown again has a frame from before; paint now.
				if (tab->visible)
					_PaintTab(tab);
				break;
			case kOpEdit:
				sApi.editing_action(engine, message->GetInt32("value", 0));
				break;
			case kOpSelectAll:
				sApi.key(engine, 1, 0, "a", 2, 0);
				sApi.key(engine, 0, 0, "a", 2, 0);
				break;
		}
	}

	void _History(BMessage* message)
	{
		BMessage reply(B_REPLY);
		ServoTab* tab = _Tab(message);
		if (tab != NULL && tab->engine != NULL) {
			bool forward = message->GetBool("forward", false);
			char url[4096];
			for (int32 index = 0; index < 64; index++) {
				if (sApi.history(tab->engine, forward ? 1 : 0, index, url,
						(int32)sizeof(url)) != 0)
					break;
				reply.AddString("url", url);
			}
		}
		message->SendReply(&reply);
	}

	void _PumpSoon()
	{
		servo_wake(NULL);
	}

	void _Pump()
	{
		sApi.pump();
		for (size_t i = 0; i < fTabs.size(); i++) {
			deliver_outbox(fTabs[i]);
			if (fTabs[i]->visible && fTabs[i]->view.IsValid())
				_PaintTab(fTabs[i]);
		}
	}

	void _PaintTab(ServoTab* tab)
	{
		if (tab->engine == NULL || sApi.paint(tab->engine) != 1)
			return;
		const uint8* rgba = NULL;
		int32 width = 0;
		int32 height = 0;
		if (sApi.frame(tab->engine, &rgba, &width, &height) != 0
			|| rgba == NULL || width < 1 || height < 1)
			return;
		tab->lock.Lock();
		if (tab->frame == NULL
			|| tab->frame->Bounds().IntegerWidth() + 1 != width
			|| tab->frame->Bounds().IntegerHeight() + 1 != height) {
			delete tab->frame;
			tab->frame = new BBitmap(BRect(0, 0, width - 1, height - 1),
				B_RGBA32);
		}
		// Servo gives RGBA bytes; B_RGBA32 is B, G, R, A in memory. Swap
		// red and blue a word at a time and force the alpha opaque.
		uint8* dest = (uint8*)tab->frame->Bits();
		int32 stride = tab->frame->BytesPerRow();
		for (int32 y = 0; y < height; y++) {
			const uint32* src = (const uint32*)(rgba + (size_t)y * width * 4);
			uint32* row = (uint32*)(dest + (size_t)y * stride);
			for (int32 x = 0; x < width; x++) {
				uint32 p = B_LENDIAN_TO_HOST_INT32(src[x]);
				row[x] = B_HOST_TO_LENDIAN_INT32((p & 0x0000ff00)
					| ((p & 0x000000ff) << 16) | ((p >> 16) & 0x000000ff)
					| 0xff000000);
			}
		}
		tab->lock.Unlock();
		// One "new frame" message at a time; the view takes the latest.
		if (atomic_get_and_set(&tab->framePending, 1) == 0) {
			BMessage frame(kViewFrame);
			if (tab->view.SendMessage(&frame, (BHandler*)NULL, 0) != B_OK)
				atomic_set(&tab->framePending, 0);
		}
	}

	std::vector<ServoTab*>	fTabs;
	bigtime_t				fLastPump;
	BMessageRunner*			fLater;
	BMessageRunner*			fTick;
};


// Servo's events for a tab, on the engine thread: copied into a message for
// the tab's view, which handles them on its own thread.
static void
tab_callback(void* context, const r5_servo_event* event)
{
	ServoTab* tab = static_cast<ServoTab*>(context);
	if (tab == NULL || event == NULL)
		return;
	BMessage message(kViewEvent);
	message.AddInt32("kind", event->kind);
	message.AddString("url", event->url != NULL ? event->url : "");
	message.AddString("text", event->text != NULL ? event->text : "");
	message.AddFloat("progress", event->progress);
	message.AddInt32("a", event->a);
	message.AddInt32("b", event->b);
	message.AddInt32("c", event->c);
	if (event->kind == R5_EV_FAVICON && event->favicon != NULL
		&& event->favicon_width > 0 && event->favicon_height > 0) {
		message.AddData("favicon", B_RAW_TYPE, event->favicon,
			(ssize_t)event->favicon_width * event->favicon_height * 4);
		message.AddInt32("faviconWidth", event->favicon_width);
		message.AddInt32("faviconHeight", event->favicon_height);
		message.AddInt32("faviconFormat", event->favicon_format);
	}
	if (event->kind == R5_EV_NEW_WINDOW && event->current != 0
		&& sEngine != NULL) {
		message.AddPointer("child",
			sEngine->AddChild(tab, (r5_servo*)(addr_t)event->current));
	}
	if (event->kind == R5_EV_DIALOG) {
		// The id the answer must name.
		message.AddInt64("control", event->current);
	}
	tab->outbox.push_back(new BMessage(message));
	deliver_outbox(tab);

}


// Sends a message to the engine thread and waits for its reply.
static status_t
engine_request(BMessage* message, BMessage* reply)
{
	if (sEngine == NULL)
		return B_NO_INIT;
	return sEngineMessenger.SendMessage(message, reply);
}


void
aomi_log_add(const char* line)
{
	if (line == NULL || line[0] == '\0')
		return;
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char stamp[32];
	snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min,
		tm.tm_sec);
	BString entry(stamp);
	entry << line;
	sLogLock.Lock();
	if (sLogRing.size() >= kMaxLogLines)
		sLogRing.erase(sLogRing.begin());
	sLogRing.push_back(entry);
	// The same lines go to AOMI_LOG when it is set (tests), as the window's
	// own log does.
	const char* path = getenv("AOMI_LOG");
	if (path != NULL && path[0] != '\0') {
		FILE* file = fopen(path, "a");
		if (file != NULL) {
			fprintf(file, "%s\n", line);
			fclose(file);
		}
	}
	sLogLock.Unlock();
}



BString
aomi_log_dump_text()
{
	BString out;
	sLogLock.Lock();
	for (size_t i = 0; i < sLogRing.size(); i++) {
		out << sLogRing[i] << "\n";
	}
	sLogLock.Unlock();
	return out;
}


static void
append_json_escaped(BString* out, const char* s)
{
	*out << "\"";
	for (const unsigned char* p = (const unsigned char*)s; *p != 0; p++) {
		unsigned char c = *p;
		if (c == '"')
			*out << "\\\"";
		else if (c == '\\')
			*out << "\\\\";
		else if (c == '\n')
			*out << "\\n";
		else if (c == '\r')
			*out << "\\r";
		else if (c == '\t')
			*out << "\\t";
		else if (c < 0x20) {
			char buf[8];
			snprintf(buf, sizeof(buf), "\\u%04x", c);
			*out << buf;
		} else {
			char buf[2] = { (char)c, 0 };
			*out << buf;
		}
	}
	*out << "\"";
}


void
aomi_log_dump_json(BString* out)
{
	*out << "[";
	sLogLock.Lock();
	for (size_t i = 0; i < sLogRing.size(); i++) {
		if (i > 0)
			*out << ",";
		append_json_escaped(out, sLogRing[i].String());
	}
	sLogLock.Unlock();
	*out << "]";
}


status_t
aomi_servo_start()
{
	if (sStarted)
		return sAvailable ? B_OK : B_ERROR;
	sStarted = true;

	// libr5servo chooses the GL path itself (Zink on the GPU, or on Lavapipe).

	void* handle = dlopen("libr5servo.so", RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		const char* err = dlerror();
		sError = "Could not load libr5servo.so";
		if (err != NULL && err[0] != '\0') {
			sError << ": " << err;
		}
		aomi_log_add(sError.String());
		sAvailable = false;
		return B_ERROR;
	}

#define AOMI_SYM(field, name) \
	sApi.field = (decltype(sApi.field))dlsym(handle, name)
	sApi.handle = handle;
	AOMI_SYM(create, "r5_servo_create");
	AOMI_SYM(resize, "r5_servo_resize");
	AOMI_SYM(load_url, "r5_servo_load_url");
	AOMI_SYM(load_html, "r5_servo_load_html");
	AOMI_SYM(reload, "r5_servo_reload");
	AOMI_SYM(stop, "r5_servo_stop");
	AOMI_SYM(back, "r5_servo_back");
	AOMI_SYM(forward, "r5_servo_forward");
	AOMI_SYM(zoom, "r5_servo_zoom");
	AOMI_SYM(focus, "r5_servo_focus");
	AOMI_SYM(frame, "r5_servo_frame");
	AOMI_SYM(history, "r5_servo_history");
	AOMI_SYM(gl, "r5_servo_gl");
	AOMI_SYM(find, "r5_servo_find");
	AOMI_SYM(destroy, "r5_servo_destroy");
	AOMI_SYM(set_waker, "r5_servo_set_waker");
	AOMI_SYM(pump, "r5_servo_pump");
	AOMI_SYM(paint, "r5_servo_paint");
	AOMI_SYM(key, "r5_servo_key");
	AOMI_SYM(set_max_size, "r5_servo_set_max_size");
	AOMI_SYM(control_answer, "r5_servo_control_answer");
	AOMI_SYM(input_message, "r5_servo_input_message");
	AOMI_SYM(ask_control, "r5_servo_ask_control");
	AOMI_SYM(free_answer, "r5_servo_free_answer");
	AOMI_SYM(set_callback, "r5_servo_set_callback");
	AOMI_SYM(set_visible, "r5_servo_set_visible");
	AOMI_SYM(editing_action, "r5_servo_editing_action");
	AOMI_SYM(shutdown, "r5_servo_shutdown");
	AOMI_SYM(create_sharing, "r5_servo_create_sharing");
#undef AOMI_SYM

	// Every call is needed; an older libr5servo lacks some.
	BString missing;
#define AOMI_REQUIRE(field) \
	if (sApi.field == NULL) \
		missing << " " << #field
	AOMI_REQUIRE(create); AOMI_REQUIRE(resize); AOMI_REQUIRE(load_url);
	AOMI_REQUIRE(load_html); AOMI_REQUIRE(reload); AOMI_REQUIRE(stop);
	AOMI_REQUIRE(back); AOMI_REQUIRE(forward); AOMI_REQUIRE(zoom);
	AOMI_REQUIRE(focus); AOMI_REQUIRE(frame); AOMI_REQUIRE(history);
	AOMI_REQUIRE(gl); AOMI_REQUIRE(find); AOMI_REQUIRE(destroy);
	AOMI_REQUIRE(set_waker); AOMI_REQUIRE(pump); AOMI_REQUIRE(paint);
	AOMI_REQUIRE(key); AOMI_REQUIRE(set_max_size);
	AOMI_REQUIRE(control_answer); AOMI_REQUIRE(input_message);
	AOMI_REQUIRE(ask_control); AOMI_REQUIRE(free_answer);
	AOMI_REQUIRE(set_callback); AOMI_REQUIRE(set_visible);
	AOMI_REQUIRE(editing_action); AOMI_REQUIRE(shutdown);
	AOMI_REQUIRE(create_sharing);
#undef AOMI_REQUIRE
	if (missing.Length() > 0) {
		sError = "libr5servo.so is too old for this Aomi; missing:";
		sError << missing;
		aomi_log_add(sError.String());
		sAvailable = false;
		return B_ERROR;
	}

	sEngine = new ServoEngine();
	sEngine->Run();
	sEngineMessenger = BMessenger(sEngine);
	sApi.set_waker(servo_wake, NULL);
	sEngine->StartTick();

	sAvailable = true;
	aomi_log_add("Loaded libr5servo.so (Servo 0.8.0)");
	return B_OK;
}


void
aomi_servo_shutdown()
{
	// After every window is closed: Servo writes cookies, HSTS and logins
	// to the profile only while it shuts down.
	if (sEngine == NULL)
		return;
	BMessage request(kEngShutdown);
	BMessage reply;
	engine_request(&request, &reply);
	if (sEngine->Lock())
		sEngine->Quit();
	sEngine = NULL;
}


bool
aomi_servo_available()
{
	return sAvailable;
}


const char*
aomi_servo_error()
{
	return sError.String();
}


// The engine thread and the windows write these; a caller gets its own
// copy, kept per thread until its next call.
const char*
aomi_servo_gl_info()
{
	static thread_local BString copy;
	sInfoLock.Lock();
	copy = sGlInfo;
	sInfoLock.Unlock();
	return copy.String();
}


const char*
aomi_servo_last_protocol()
{
	static thread_local BString copy;
	sInfoLock.Lock();
	copy = sLastProtocol;
	sInfoLock.Unlock();
	return copy.String();
}



struct DownloadTask {
	int32	id;
	BString	url;
	BString	path;
	BString	name;
};


static int32
download_thread(void* arg)
{
	DownloadTask* task = static_cast<DownloadTask*>(arg);
	BString cmd("/bin/curl -fsSL -o '");
	BString safePath(task->path);
	safePath.ReplaceAll("'", "'\\''");
	BString safeUrl(task->url);
	safeUrl.ReplaceAll("'", "'\\''");
	cmd << safePath << "' '" << safeUrl << "'";
	int rc = system(cmd.String());

	sDlLock.Lock();
	for (size_t i = 0; i < sDownloads.size(); i++) {
		if (sDownloads[i].id == task->id) {
			sDownloads[i].done = true;
			sDownloads[i].failed = (rc != 0);
			break;
		}
	}
	sDlLock.Unlock();

	if (be_app != NULL) {
		BMessage msg(rc == 0 ? AOMI_DOWNLOAD_FINISHED : AOMI_DOWNLOAD_FAILED);
		msg.AddInt32("id", task->id);
		if (rc == 0)
			msg.AddString("path", task->path);
		else
			msg.AddString("text", "Download failed");
		be_app->PostMessage(&msg);
	}
	BString logLine;
	logLine.SetToFormat("download %s -> %s (%s)", task->url.String(),
		task->path.String(), rc == 0 ? "ok" : "failed");
	aomi_log_add(logLine.String());
	delete task;
	return 0;
}


status_t
aomi_start_download(const char* url, const char* destDir)
{
	if (url == NULL || url[0] == '\0')
		return B_BAD_VALUE;
	BString dir(destDir != NULL && destDir[0] != '\0' ? destDir : "/boot/home/Desktop");
	create_directory(dir.String(), 0755);
	const char* slash = strrchr(url, '/');
	BString name(slash != NULL && slash[1] != '\0' ? slash + 1 : "download.bin");
	int32 q = name.FindFirst('?');
	if (q >= 0)
		name.Truncate(q);
	if (name.Length() == 0)
		name = "download.bin";

	BString fullPath(dir);
	fullPath << "/" << name;

	sDlLock.Lock();
	int32 id = sNextDlId++;
	DownloadItem item;
	item.id = id;
	item.name = name;
	item.path = fullPath;
	item.done = false;
	item.failed = false;
	sDownloads.push_back(item);
	sDlLock.Unlock();

	if (be_app != NULL) {
		BMessage added(AOMI_DOWNLOAD_ADDED);
		added.AddInt32("id", id);
		added.AddString("name", name);
		added.AddInt64("size", -1);
		be_app->PostMessage(&added);
	}

	DownloadTask* task = new DownloadTask();
	task->id = id;
	task->url = url;
	task->path = fullPath;
	task->name = name;
	thread_id tid = spawn_thread(download_thread, "aomi download",
		B_NORMAL_PRIORITY, task);
	if (tid < 0) {
		delete task;
		return tid;
	}
	resume_thread(tid);
	return B_OK;
}


// Hidden by itself or by a parent view; unlike IsHidden(), a window that is
// not shown yet does not count (BView::IsHidden(lookingFrom) checks only the
// view's own show level when lookingFrom is the view).
static bool
hidden_in_window(const BView* view)
{
	for (const BView* v = view; v != NULL; v = v->Parent()) {
		if (v->IsHidden(v))
			return true;
	}
	return false;
}


status_t
aomi_start_download_to(const char* url, const char* path)

{
	// Save page: the user chose the name in a save panel, which already asked
	// about replacing an existing file.
	if (url == NULL || url[0] == '\0' || path == NULL || path[0] == '\0')
		return B_BAD_VALUE;
	BPath target(path);
	if (target.InitCheck() != B_OK)
		return B_BAD_VALUE;
	sDlLock.Lock();
	int32 id = sNextDlId++;
	DownloadItem item;
	item.id = id;
	item.name = target.Leaf();
	item.path = target.Path();
	item.done = false;
	item.failed = false;
	sDownloads.push_back(item);
	sDlLock.Unlock();
	if (be_app != NULL) {
		BMessage added(AOMI_DOWNLOAD_ADDED);
		added.AddInt32("id", id);
		added.AddString("name", target.Leaf());
		added.AddInt64("size", -1);
		be_app->PostMessage(&added);
	}
	DownloadTask* task = new DownloadTask();
	task->id = id;
	task->url = url;
	task->path = target.Path();
	task->name = target.Leaf();
	thread_id tid = spawn_thread(download_thread, "aomi download",
		B_NORMAL_PRIORITY, task);
	if (tid < 0) {
		delete task;
		return tid;
	}
	resume_thread(tid);
	return B_OK;
}


BServoContentView::BServoContentView(const char* name, BWebSession* session,
	void* adoptTab)
	:
	BWebContentView(name != NULL ? name : "servo", session),
	fTab(static_cast<ServoTab*>(adoptTab)),
	fAdopted(adoptTab == NULL),
	fLoading(false),
	fSawFrame(false),
	fSawDraw(false),
	fHasPending(false),
	fPendingHTML(false)
{
	if (!sStarted)
		aomi_servo_start();
	if (!sAvailable)
		fError = sError;
}


BServoContentView::~BServoContentView()
{
	if (fTab != NULL) {
		// Synchronous: after the reply the engine thread never names this
		// tab again, so its frame and messages cannot reach a deleted view.
		BMessage request(kEngDestroy);
		request.AddPointer("tab", fTab);
		BMessage reply;
		engine_request(&request, &reply);
		fTab = NULL;
	}
}


void
BServoContentView::AttachedToWindow()
{
	BWebContentView::AttachedToWindow();
	if (fTab != NULL && !fAdopted) {
		// An engine Servo made for a page: take it over now that this view
		// can be messaged.
		BMessage request(kEngAdopt);
		request.AddPointer("tab", fTab);
		request.AddMessenger("view", BMessenger(this));
		request.AddBool("visible", !hidden_in_window(this));
		BMessage reply;
		engine_request(&request, &reply);
		fAdopted = true;
	}
	if (fHasPending) {
		fHasPending = false;
		BString document(fPendingDocument);
		fPendingDocument.Truncate(0);
		_Start(document.String(), fPendingHTML);
	}
}


status_t
BServoContentView::_Start(const char* document, bool isHTML)
{
	if (!sAvailable) {
		fError = sError;
		Invalidate();
		return B_ERROR;
	}
	if (fTab != NULL)
		return B_OK;
	BWindow* window = Window();
	if (window == NULL) {
		// Started as soon as the view is in a window.
		fPendingDocument = document != NULL ? document : "";
		fPendingHTML = isHTML;
		fHasPending = true;
		return B_OK;
	}
	bool unlock = false;
	if (!window->IsLocked()) {
		if (!window->Lock())
			return B_ERROR;
		unlock = true;
	}
	int32 width = (int32)Bounds().Width() + 1;
	int32 height = (int32)Bounds().Height() + 1;
	BScreen screen(window);
	BRect frame = screen.Frame();
	BMessage request(kEngCreate);
	request.AddMessenger("view", BMessenger(this));
	request.AddString("document", document != NULL ? document : "");
	request.AddBool("html", isHTML);
	request.AddInt32("width", width < 16 ? 16 : width);
	request.AddInt32("height", height < 16 ? 16 : height);
	// libr5servo makes a tab's GL storage this large once; a view is never
	// larger (surface_size() in servo-haiku engine/src/lib.rs).
	request.AddInt32("maxWidth", (int32)frame.Width() + 1);
	request.AddInt32("maxHeight", (int32)frame.Height() + 1);
	request.AddBool("visible", !hidden_in_window(this));
	// Another tab of this window: the new one shares its GL context.
	ServoTab* sibling = NULL;
	for (int32 i = 0; sibling == NULL && i < window->CountChildren(); i++)
		sibling = _TabIn(window->ChildAt(i), this);
	if (sibling != NULL)
		request.AddPointer("sibling", sibling);
	if (unlock)
		window->Unlock();
	BMessage reply;
	ServoTab* tab = NULL;
	if (engine_request(&request, &reply) == B_OK)
		reply.FindPointer("tab", (void**)&tab);
	if (tab == NULL) {
		fError = B_TRANSLATE("Servo could not make a drawing surface for "
			"this page (EGL).");
		aomi_log_add(fError.String());
		Invalidate();
		return B_ERROR;
	}
	fTab = tab;
	fAdopted = true;
	fLoading = true;
	return B_OK;
}


// The engine tab of a BServoContentView in this view tree, other than
// `except`. Called with the window locked.
/*static*/ ServoTab*
BServoContentView::_TabIn(BView* view, const BServoContentView* except)
{
	BServoContentView* page = dynamic_cast<BServoContentView*>(view);
	if (page != NULL && page != except && page->fTab != NULL)
		return page->fTab;
	for (int32 i = 0; i < view->CountChildren(); i++) {
		ServoTab* tab = _TabIn(view->ChildAt(i), except);
		if (tab != NULL)
			return tab;
	}
	return NULL;
}


void
BServoContentView::_Call(int32 op, const char* text, float value, bool on,
	int32 width, int32 height)
{
	if (fTab == NULL)
		return;
	BMessage call(kEngCall);
	call.AddPointer("tab", fTab);
	call.AddInt32("op", op);
	if (text != NULL)
		call.AddString("text", text);
	call.AddFloat("value", value);
	call.AddInt32("value", (int32)value);
	call.AddBool("on", on);
	call.AddInt32("width", width);
	call.AddInt32("height", height);
	sEngineMessenger.SendMessage(&call);
}


void
BServoContentView::_Input(const BMessage* event, BPoint where)
{
	if (fTab == NULL || event == NULL)
		return;
	BMessage input(kEngInput);
	input.AddPointer("tab", fTab);
	input.AddMessage("event", event);
	input.AddFloat("x", where.x);
	input.AddFloat("y", where.y);
	sEngineMessenger.SendMessage(&input);
}


status_t
BServoContentView::LoadURL(const char* url)
{
	if (url == NULL || url[0] == '\0')
		return B_BAD_VALUE;
	fCurrentURL = url;
	if (!sAvailable) {
		fError = sError;
		Invalidate();
		_Report(AOMI_LOAD_FAILED, url, fError.String(), 0, B_ERROR, 0, 0);
		return B_ERROR;
	}
	fLoading = true;
	if (fTab == NULL)
		return _Start(url, false);
	_Call(kOpLoadURL, url);
	return B_OK;
}


status_t
BServoContentView::LoadHTML(const char* html, const char* baseURL)
{
	if (html == NULL)
		return B_BAD_VALUE;
	if (baseURL != NULL)
		fCurrentURL = baseURL;
	if (!sAvailable) {
		fError = sError;
		Invalidate();
		return B_ERROR;
	}
	fLoading = true;
	if (fTab == NULL)
		return _Start(html, true);
	_Call(kOpLoadHTML, html);
	return B_OK;
}


status_t
BServoContentView::Reload(bool bypassCache)
{
	if (bypassCache)
		return B_NOT_SUPPORTED;
	if (fTab == NULL)
		return B_NO_INIT;
	_Call(kOpReload);
	return B_OK;
}


status_t
BServoContentView::StopLoading()
{
	if (fTab == NULL)
		return B_NO_INIT;
	_Call(kOpStop);
	return B_OK;
}


status_t
BServoContentView::GoBack()
{
	if (fTab == NULL)
		return B_NO_INIT;
	_Call(kOpBack);
	return B_OK;
}


status_t
BServoContentView::GoForward()
{
	if (fTab == NULL)
		return B_NO_INIT;
	_Call(kOpForward);
	return B_OK;
}


status_t
BServoContentView::_FillHistory(BMessage* items, bool forward) const
{
	if (items == NULL)
		return B_BAD_VALUE;
	if (fTab == NULL)
		return B_NO_INIT;
	BMessage request(kEngHistory);
	request.AddPointer("tab", fTab);
	request.AddBool("forward", forward);
	BMessage reply;
	status_t status = engine_request(&request, &reply);
	if (status != B_OK)
		return status;
	const char* url = NULL;
	for (int32 i = 0; reply.FindString("url", i, &url) == B_OK; i++) {
		items->AddString("url", url);
		items->AddString("title", url);
	}
	return B_OK;
}


status_t
BServoContentView::GetBackList(BMessage* items)
{
	return _FillHistory(items, false);
}


status_t
BServoContentView::GetForwardList(BMessage* items)
{
	return _FillHistory(items, true);
}


status_t
BServoContentView::URL(BString* url) const
{
	if (url == NULL)
		return B_BAD_VALUE;
	*url = fCurrentURL;
	return B_OK;
}


status_t
BServoContentView::Title(BString* title) const
{
	if (title == NULL)
		return B_BAD_VALUE;
	*title = fCurrentTitle;
	return B_OK;
}


status_t
BServoContentView::SetZoom(float factor, bool textOnly)
{
	if (textOnly)
		return B_NOT_SUPPORTED;
	if (fTab == NULL)
		return B_NO_INIT;
	_Call(kOpZoom, NULL, factor);
	return B_OK;
}


status_t
BServoContentView::Find(const char* text, bool forward, bool matchCase)
{
	if (fTab == NULL || text == NULL)
		return B_NO_INIT;
	BMessage call(kEngCall);
	call.AddPointer("tab", fTab);
	call.AddInt32("op", kOpFind);
	call.AddString("text", text);
	call.AddBool("forward", forward);
	call.AddBool("matchCase", matchCase);
	return sEngineMessenger.SendMessage(&call);
}


status_t
BServoContentView::SavePage(const char* path)
{
	if (path == NULL || path[0] == '\0' || fCurrentURL.Length() == 0)
		return B_BAD_VALUE;
	if (fCurrentURL.StartsWith("data:") || fCurrentURL.StartsWith("aomi:"))
		return B_NOT_SUPPORTED;
	return aomi_start_download_to(fCurrentURL.String(), path);
}


status_t
BServoContentView::ViewSource()
{
	return B_NOT_SUPPORTED;
}


status_t
BServoContentView::Print()
{
	return B_NOT_SUPPORTED;
}


status_t
BServoContentView::Protocol(BString* protocol) const
{
	if (protocol == NULL)
		return B_BAD_VALUE;
	if (fProtocol.Length() == 0)
		return B_NOT_SUPPORTED;
	*protocol = fProtocol;
	return B_OK;
}


status_t
BServoContentView::Cancel(int32 id)
{
	(void)id;
	return B_NOT_SUPPORTED;
}


status_t
BServoContentView::Open(int32 id)
{
	BString path;
	sDlLock.Lock();
	for (size_t i = 0; i < sDownloads.size(); i++) {
		if (sDownloads[i].id == id) {
			path = sDownloads[i].path;
			break;
		}
	}
	sDlLock.Unlock();
	if (path.Length() == 0)
		return B_BAD_INDEX;
	entry_ref ref;
	status_t st = get_ref_for_path(path.String(), &ref);
	if (st != B_OK)
		return st;
	st = be_roster->Launch(&ref);
	return st == B_ALREADY_RUNNING ? B_OK : st;
}


status_t
BServoContentView::ShowInTracker(int32 id)
{
	BString path;
	sDlLock.Lock();
	for (size_t i = 0; i < sDownloads.size(); i++) {
		if (sDownloads[i].id == id) {
			path = sDownloads[i].path;
			break;
		}
	}
	sDlLock.Unlock();
	if (path.Length() == 0)
		return B_BAD_INDEX;
	BPath p(path.String());
	p.GetParent(&p);
	entry_ref ref;
	status_t st = get_ref_for_path(p.Path(), &ref);
	if (st != B_OK)
		return st;
	st = be_roster->Launch(&ref);
	return st == B_ALREADY_RUNNING ? B_OK : st;
}


void
BServoContentView::Show()
{
	BWebContentView::Show();
	// Shown only when no parent is hidden either. The window itself does
	// not count: a tab shown before its window appears is the visible one.
	if (!hidden_in_window(this))
		_Call(kOpVisible, NULL, 0, true);
	Invalidate();
}


void
BServoContentView::Hide()
{
	BWebContentView::Hide();
	_Call(kOpVisible, NULL, 0, false);
}


void
BServoContentView::Draw(BRect updateRect)
{
	(void)updateRect;
	if (!fSawDraw && getenv("R5_SERVO_DEBUG") != NULL) {
		fSawDraw = true;
		BString line;
		line.SetToFormat("view %p first draw, frame %s", this,
			fTab != NULL && fTab->frame != NULL ? "yes" : "no");
		aomi_log_add(line.String());
	}
	if (fTab != NULL && fTab->lock.Lock()) {
		BBitmap* page = fTab->frame;
		if (page != NULL) {
			// Never stretched: the frame is drawn at its own size from the
			// top left. While a resize waits for Servo's next frame, the
			// rest of the view keeps the page background.
			BRect frame = page->Bounds();
			DrawBitmap(page, frame, frame);
			fTab->lock.Unlock();
			SetHighColor(255, 255, 255);
			BRect b = Bounds();
			if (b.right > frame.right)
				FillRect(BRect(frame.right + 1, b.top, b.right, b.bottom));
			if (b.bottom > frame.bottom)
				FillRect(BRect(b.left, frame.bottom + 1, frame.right, b.bottom));
			return;
		}
		fTab->lock.Unlock();
	}
	BRect b = Bounds();
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	bool dark = (panel.red + panel.green + panel.blue) < 380;
	rgb_color bg = dark ? (rgb_color){ 21, 24, 30, 255 }
		: (rgb_color){ 246, 248, 251, 255 };
	rgb_color card = dark ? (rgb_color){ 31, 35, 43, 255 }
		: (rgb_color){ 255, 255, 255, 255 };
	rgb_color border = dark ? (rgb_color){ 53, 59, 70, 255 }
		: (rgb_color){ 211, 216, 223, 255 };
	rgb_color fg = dark ? (rgb_color){ 230, 233, 239, 255 }
		: (rgb_color){ 20, 25, 35, 255 };
	rgb_color sub = dark ? (rgb_color){ 163, 171, 184, 255 }
		: (rgb_color){ 91, 98, 112, 255 };

	SetLowColor(bg);
	FillRect(b, B_SOLID_LOW);
	if (fError.Length() == 0)
		return;

	// Every size follows the plain font (12 pt is the design size).
	float s = be_plain_font->Size() / 12.0f;
	float cw = floorf(540 * s);
	if (cw > b.Width() - 48 * s)
		cw = b.Width() - 48 * s;
	float ch = floorf(176 * s);
	BRect r(floorf((b.Width() - cw) / 2), floorf((b.Height() - ch) / 2 - 20 * s),
		floorf((b.Width() + cw) / 2), floorf((b.Height() + ch) / 2 - 20 * s));
	SetHighColor(card);
	FillRoundRect(r, 10 * s, 10 * s);
	SetHighColor(border);
	StrokeRoundRect(r, 10 * s, 10 * s);

	BFont titleFont(be_bold_font);
	titleFont.SetSize(18 * s);
	SetFont(&titleFont);
	SetHighColor(fg);
	DrawString(B_TRANSLATE("The web engine could not be started"),
		BPoint(r.left + 24 * s, r.top + 42 * s));

	BFont bodyFont(be_plain_font);
	bodyFont.SetSize(13.5f * s);
	SetFont(&bodyFont);
	SetHighColor(sub);
	const char* lines[] = {
		fError.String(),
		B_TRANSLATE("Reinstall the r5servo package and open Aomi again."),
		B_TRANSLATE("WebPositive is still installed and works as usual.")
	};
	for (int32 i = 0; i < 3; i++) {
		BString line(lines[i]);
		bodyFont.TruncateString(&line, B_TRUNCATE_END, cw - 48 * s);
		DrawString(line.String(),
			BPoint(r.left + 24 * s, r.top + (78 + 30 * i) * s));
	}
}


void
BServoContentView::FrameResized(float width, float height)
{
	BWebContentView::FrameResized(width, height);
	int32 w = (int32)width + 1;
	int32 h = (int32)height + 1;
	_Call(kOpResize, NULL, 0, false, w < 16 ? 16 : w, h < 16 ? 16 : h);
}


void
BServoContentView::MakeFocus(bool focused)
{
	BWebContentView::MakeFocus(focused);
	_Call(kOpFocus, NULL, 0, focused);
}


void
BServoContentView::MouseDown(BPoint where)
{
	MakeFocus(true);
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	fLastMouse = where;
	if (Window() != NULL)
		_Input(Window()->CurrentMessage(), where);
}


void
BServoContentView::MouseUp(BPoint where)
{
	fLastMouse = where;
	if (Window() != NULL)
		_Input(Window()->CurrentMessage(), where);
}


void
BServoContentView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	BWebContentView::MouseMoved(where, transit, dragMessage);
	fLastMouse = where;
	if (Window() != NULL)
		_Input(Window()->CurrentMessage(), where);
}


void
BServoContentView::KeyDown(const char* bytes, int32 numBytes)
{
	if (fTab == NULL || Window() == NULL) {
		BWebContentView::KeyDown(bytes, numBytes);
		return;
	}
	_Input(Window()->CurrentMessage(), fLastMouse);
}


void
BServoContentView::KeyUp(const char* bytes, int32 numBytes)
{
	if (fTab == NULL || Window() == NULL) {
		BWebContentView::KeyUp(bytes, numBytes);
		return;
	}
	_Input(Window()->CurrentMessage(), fLastMouse);
}


void
BServoContentView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kViewFrame:
			if (fTab != NULL)
				atomic_set(&fTab->framePending, 0);
			if (!fSawFrame && getenv("R5_SERVO_DEBUG") != NULL) {
				fSawFrame = true;
				BRect frame = Frame();
				BString line;
				line.SetToFormat("view %p frame in, hidden %d/%d, %gx%g at %g,%g",
					this, IsHidden(this) ? 1 : 0, IsHidden() ? 1 : 0,
					frame.Width() + 1, frame.Height() + 1, frame.left,
					frame.top);
				aomi_log_add(line.String());
			}
			Invalidate();
			return;
		case kViewEvent:
			_OnEvent(message);
			return;
		case B_MOUSE_WHEEL_CHANGED:
			_Input(message, fLastMouse);
			return;
		// The Edit shortcuts (Alt+C, X, V, A) reach the focus view as these
		// messages (BWindow adds them as shortcuts).
		case B_COPY:
			_Call(kOpEdit, NULL, 0);
			return;
		case B_CUT:
			_Call(kOpEdit, NULL, 1);
			return;
		case B_PASTE:
			_Call(kOpEdit, NULL, 2);
			return;
		case B_SELECT_ALL:
			_Call(kOpSelectAll);
			return;
		case B_COLORS_UPDATED:
			Invalidate();
			break;
	}
	BWebContentView::MessageReceived(message);
}


status_t
BServoContentView::_Report(uint32 what, const char* url, const char* text,
	float progress, int32 a, int32 b, int32 c)
{
	BMessage message(what);
	if (url != NULL && url[0] != '\0')
		message.AddString("url", url);
	if (text != NULL && text[0] != '\0')
		message.AddString("text", text);
	if (what == AOMI_PROGRESS)
		message.AddFloat("progress", progress);
	if (what == AOMI_NAVIGATION) {
		message.AddBool("canBack", a != 0);
		message.AddBool("canForward", b != 0);
		message.AddBool("loading", c != 0);
	}
	if (what == AOMI_LOAD_FAILED)
		message.AddInt32("error", a);
	if (what == AOMI_CURSOR)
		message.AddInt32("cursor", a);
	if (what == AOMI_FULLSCREEN)
		message.AddBool("on", a != 0);
	if (what == AOMI_NEW_WINDOW)
		message.AddBool("primary", a != 0);
	if (what == AOMI_SECURITY)
		message.AddInt32("state", a);
	if (what == AOMI_PROTOCOL && text != NULL)
		message.AddString("protocol", text);
	if (what == AOMI_DIALOG) {
		message.AddInt32("kind", a);
		message.AddPoint("where", BPoint(b, c));
	}
	return Report(&message);
}


void
BServoContentView::_AskControl(int32 kind, const char* text, BPoint where,
	int64 control)
{
	// The page waits for the answer. The window stays drawn while the
	// question is open; the engine thread keeps the other tabs going.
	r5_servo_control_labels labels = {
		B_TRANSLATE("OK"),
		B_TRANSLATE("Cancel"),
		B_TRANSLATE("User name:"),
		B_TRANSLATE("Password:"),
		B_TRANSLATE("Sign in"),
		B_TRANSLATE("Sign in to %s")
	};
	int32 accept = 0;
	int32 value = -1;
	char* answer = NULL;
	// The log (AOMI_LOG and the technical page) shows what a page asked
	// and whether it was answered; never the answer itself.
	BString line;
	line.SetToFormat("page asks %d", (int)kind);
	aomi_log_add(line.String());
	status_t asked = sApi.ask_control(this, kind, text, where.x, where.y,
		&labels, &accept, &value, &answer);
	line.SetToFormat("page answered %d %s", (int)kind,
		asked != B_OK ? "(not shown)" : accept != 0 ? "accept" : "cancel");
	aomi_log_add(line.String());
	if (asked != B_OK)
		return;

	if (fTab != NULL) {
		BMessage reply(kEngAnswer);
		reply.AddPointer("tab", fTab);
		reply.AddInt64("control", control);
		reply.AddBool("accept", accept != 0);
		reply.AddInt32("value", value);
		// Handed over: the engine thread answers and clears it.
		reply.AddPointer("answer", answer);
		if (sEngineMessenger.SendMessage(&reply) == B_OK)
			return;
	}
	sApi.free_answer(answer);
}


void
BServoContentView::_OnEvent(BMessage* event)
{
	int32 kind = event->GetInt32("kind", 0);
	const char* url = event->GetString("url", "");
	const char* text = event->GetString("text", "");
	float progress = event->GetFloat("progress", 0);
	int32 a = event->GetInt32("a", 0);
	int32 b = event->GetInt32("b", 0);
	int32 c = event->GetInt32("c", 0);
	switch (kind) {
		case R5_EV_LOAD_STARTED:
			fLoading = true;
			_Report(AOMI_LOAD_STARTED, url, NULL, 0, 0, 0, 0);
			_Report(AOMI_PROGRESS, NULL, NULL, 0.05f, 0, 0, 0);
			break;
		case R5_EV_LOAD_COMMITTED:
			_Report(AOMI_LOAD_COMMITTED, url, NULL, 0, 0, 0, 0);
			break;
		case R5_EV_LOAD_FINISHED:
			fLoading = false;
			_Report(AOMI_LOAD_FINISHED, url, NULL, 0, 0, 0, 0);
			_Report(AOMI_PROGRESS, NULL, NULL, 1.0f, 0, 0, 0);
			break;
		case R5_EV_LOAD_FAILED:
			fLoading = false;
			_Report(AOMI_LOAD_FAILED, url, text, 0, a, 0, 0);
			break;
		case R5_EV_NAVIGATION:
			_Report(AOMI_NAVIGATION, NULL, NULL, 0, a, b, c);
			break;
		case R5_EV_TITLE:
		{
			fCurrentTitle = text;
			BMessage message(AOMI_TITLE_CHANGED);
			message.AddString("title", fCurrentTitle);
			Report(&message);
			break;
		}
		case R5_EV_URL:
			fCurrentURL = url;
			fProtocol.Truncate(0);
			_Report(AOMI_URL_CHANGED, url, NULL, 0, 0, 0, 0);
			_Report(AOMI_LOAD_COMMITTED, url, NULL, 0, 0, 0, 0);
			if (strncmp(url, "https:", 6) == 0)
				_Report(AOMI_SECURITY, NULL, "https", 0, AOMI_SECURITY_SECURE, 0, 0);
			else if (strncmp(url, "http:", 5) == 0)
				_Report(AOMI_SECURITY, NULL, "http", 0, AOMI_SECURITY_NOT_SECURE, 0, 0);
			break;
		case R5_EV_HOVER:
		{
			BMessage message(AOMI_HOVER_LINK);
			message.AddString("url", url);
			Report(&message);
			break;
		}
		case R5_EV_PROTOCOL:
			if (text[0] != '\0') {
				fProtocol = text;
				sInfoLock.Lock();
				sLastProtocol = text;
				sInfoLock.Unlock();
				BString logLine("protocol ");
				logLine << fProtocol << " " << fCurrentURL;
				aomi_log_add(logLine.String());
			}
			_Report(AOMI_PROTOCOL, NULL, text, 0, 0, 0, 0);
			break;
		case R5_EV_PROGRESS:
			_Report(AOMI_PROGRESS, NULL, NULL, progress, 0, 0, 0);
			break;
		case R5_EV_CURSOR:
			_Report(AOMI_CURSOR, NULL, NULL, 0, a, 0, 0);
			break;
		case R5_EV_FULLSCREEN:
			_Report(AOMI_FULLSCREEN, NULL, NULL, 0, a, 0, 0);
			break;
		case R5_EV_NEW_WINDOW:
		{
			// The page opened a window: Servo already made its engine. A new
			// view adopts it; the window adds it as a tab.
			void* child = NULL;
			if (event->FindPointer("child", &child) != B_OK || child == NULL)
				break;
			BServoContentView* view = new BServoContentView("page", Session(),
				child);
			BMessage message(AOMI_NEW_WINDOW);
			message.AddPointer("view", static_cast<BWebContentView*>(view));
			message.AddBool("primary", a != 0);
			if (Report(&message) != B_OK) {
				// No window took it: the view frees its engine.
				delete view;
			}
			break;

		}
		case R5_EV_CLOSE:
			_Report(AOMI_CLOSE_WANTED, NULL, NULL, 0, 0, 0, 0);
			break;
		case R5_EV_FAVICON:
		{
			const void* data = NULL;
			ssize_t size = 0;
			int32 width = event->GetInt32("faviconWidth", 0);
			int32 height = event->GetInt32("faviconHeight", 0);
			int32 format = event->GetInt32("faviconFormat", 0);
			if (event->FindData("favicon", B_RAW_TYPE, &data, &size) != B_OK
				|| width < 1 || height < 1 || size < (ssize_t)width * height * 4)
				break;
			BBitmap icon(BRect(0, 0, width - 1, height - 1), B_RGBA32);
			uint8* dest = (uint8*)icon.Bits();
			const uint8* src = (const uint8*)data;
			int32 pixels = width * height;
			if (format == 2) {
				memcpy(dest, src, (size_t)pixels * 4);
			} else if (format == 1) {
				for (int32 i = 0; i < pixels; i++) {
					dest[i * 4 + 0] = src[i * 4 + 2];
					dest[i * 4 + 1] = src[i * 4 + 1];
					dest[i * 4 + 2] = src[i * 4 + 0];
					dest[i * 4 + 3] = src[i * 4 + 3];
				}
			} else
				break;
			BMessage archived;
			if (icon.Archive(&archived) == B_OK) {
				BMessage message(AOMI_FAVICON);
				message.AddMessage("icon", &archived);
				Report(&message);
			}
			break;
		}
		case R5_EV_DIALOG:
			_Report(AOMI_DIALOG, NULL, text, 0, a, b, c);
			if (a == AOMI_DIALOG_PERMISSION) {
				// Denied by the engine already: Servo on R5 has no camera,
				// location or notification service to grant.
				BString line("permission denied: ");
				line << text;
				aomi_log_add(line.String());
				break;
			}
			_AskControl(a, text, BPoint(b, c), event->GetInt64("control", 0));
			break;
		default:
			break;
	}
}
