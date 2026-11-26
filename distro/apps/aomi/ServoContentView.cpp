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


// Matches distro/servo/r5_servo_abi.h
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
	void		(*spin)(r5_servo*);
	status_t	(*frame)(r5_servo*, const uint8**, int32*, int32*);
	status_t	(*url)(r5_servo*, char*, int32);
	status_t	(*title)(r5_servo*, char*, int32);
	status_t	(*protocol)(r5_servo*, char*, int32);
	status_t	(*history)(r5_servo*, int32, int32, char*, int32);
	status_t	(*gl)(r5_servo*, char*, int32);
	status_t	(*find)(r5_servo*, const char*, int32, int32);
	void		(*destroy)(r5_servo*);
	// Since r5servo 0.8.0-3.
	void		(*set_waker)(void (*)(void*), void*);
	int32		(*pump)();
	int32		(*paint)(r5_servo*);
	status_t	(*mouse_move)(r5_servo*, float, float);
	status_t	(*mouse_buttons)(r5_servo*, float, float, uint32);
	status_t	(*wheel)(r5_servo*, float, float, float, float, float);
	status_t	(*key)(r5_servo*, int32, int32, const char*, uint32, int32);
	void		(*set_max_size)(int32, int32);
	status_t	(*control_answer)(r5_servo*, int32, int32, const char*);
};

static ServoApi sApi = {};
static bool sStarted = false;
static bool sAvailable = false;
static BString sError;
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

static const uint32 kSpinMessage = 'svSp';

// Servo runs one instance per process, on the thread of the one browser
// window (AomiApp opens every further window as its own process). Servo's
// threads call servo_wake(); it posts one message to the pump handler on
// that window, which runs the event loop and paints the visible tab, at
// most once per frame. A slow tick covers a wake-up that could not be posted.
static const uint32 kServoWake = 'svWk';
static const uint32 kServoTick = 'svTk';
static const uint32 kServoLater = 'svLt';
static const bigtime_t kFrameTime = 16000;
static const bigtime_t kTickTime = 250000;

static int32 sWakePending = 0;
static BMessenger* sWakeTarget = NULL;
// Every Servo view of this process. They all live in the one browser window,
// so the pump handler (on that window's thread) may touch them.
static std::vector<BServoContentView*> sViews;
static BHandler* sPumpHandler = NULL;
static bigtime_t sLastPump = 0;


static void
servo_wake(void*)
{
	// Called from Servo's threads. sWakeTarget is set once, before Servo
	// starts, and never freed: a wake-up after the window is gone only fails
	// to send.
	if (sWakeTarget == NULL || atomic_get_and_set(&sWakePending, 1) != 0)
		return;
	BMessage wake(kServoWake);
	if (sWakeTarget->SendMessage(&wake, (BHandler*)NULL, 0) != B_OK)
		atomic_set(&sWakePending, 0);
}


class ServoPumpHandler : public BHandler {
public:
	ServoPumpHandler()
		:
		BHandler("servo pump"),
		fLater(NULL),
		fTick(NULL)
	{
	}

	virtual ~ServoPumpHandler()
	{
		delete fLater;
		delete fTick;
		if (sPumpHandler == this)
			sPumpHandler = NULL;
	}

	void StartTick()
	{
		BMessage tick(kServoTick);
		fTick = new BMessageRunner(BMessenger(this), &tick, kTickTime);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kServoWake:
			case kServoTick:
			case kServoLater:
			{
				if (message->what == kServoWake)
					atomic_set(&sWakePending, 0);
				if (message->what == kServoLater) {
					delete fLater;
					fLater = NULL;
				}
				bigtime_t now = system_time();
				if (now - sLastPump < kFrameTime) {
					if (fLater == NULL) {
						BMessage later(kServoLater);
						fLater = new BMessageRunner(BMessenger(this), &later,
							kFrameTime - (now - sLastPump), 1);
					}
					return;
				}
				sLastPump = now;
				BServoContentView::PumpAll();
				return;
			}
			default:
				BHandler::MessageReceived(message);
				return;
		}
	}

private:
	BMessageRunner*	fLater;
	BMessageRunner*	fTick;
};


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


static status_t
engine_status(int32 code)
{
	switch (code) {
		case 0: return B_OK;
		case -1: return B_NO_INIT;
		case -2: return B_BAD_VALUE;
		case -3: return B_NOT_SUPPORTED;
		default: return B_ERROR;
	}
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

	sApi.handle = handle;
	sApi.create = (decltype(sApi.create))dlsym(handle, "r5_servo_create");
	sApi.resize = (decltype(sApi.resize))dlsym(handle, "r5_servo_resize");
	sApi.load_url = (decltype(sApi.load_url))dlsym(handle, "r5_servo_load_url");
	sApi.load_html = (decltype(sApi.load_html))dlsym(handle, "r5_servo_load_html");
	sApi.reload = (decltype(sApi.reload))dlsym(handle, "r5_servo_reload");
	sApi.stop = (decltype(sApi.stop))dlsym(handle, "r5_servo_stop");
	sApi.back = (decltype(sApi.back))dlsym(handle, "r5_servo_back");
	sApi.forward = (decltype(sApi.forward))dlsym(handle, "r5_servo_forward");
	sApi.zoom = (decltype(sApi.zoom))dlsym(handle, "r5_servo_zoom");
	sApi.focus = (decltype(sApi.focus))dlsym(handle, "r5_servo_focus");
	sApi.spin = (decltype(sApi.spin))dlsym(handle, "r5_servo_spin");
	sApi.frame = (decltype(sApi.frame))dlsym(handle, "r5_servo_frame");
	sApi.url = (decltype(sApi.url))dlsym(handle, "r5_servo_url");
	sApi.title = (decltype(sApi.title))dlsym(handle, "r5_servo_title");
	sApi.protocol = (decltype(sApi.protocol))dlsym(handle, "r5_servo_protocol");
	sApi.history = (decltype(sApi.history))dlsym(handle, "r5_servo_history");
	sApi.gl = (decltype(sApi.gl))dlsym(handle, "r5_servo_gl");
	sApi.find = (decltype(sApi.find))dlsym(handle, "r5_servo_find");
	sApi.destroy = (decltype(sApi.destroy))dlsym(handle, "r5_servo_destroy");

	sApi.set_waker = (decltype(sApi.set_waker))dlsym(handle, "r5_servo_set_waker");
	sApi.pump = (decltype(sApi.pump))dlsym(handle, "r5_servo_pump");
	sApi.paint = (decltype(sApi.paint))dlsym(handle, "r5_servo_paint");
	sApi.mouse_move = (decltype(sApi.mouse_move))dlsym(handle, "r5_servo_mouse_move");
	sApi.mouse_buttons
		= (decltype(sApi.mouse_buttons))dlsym(handle, "r5_servo_mouse_buttons");
	sApi.wheel = (decltype(sApi.wheel))dlsym(handle, "r5_servo_wheel");
	sApi.key = (decltype(sApi.key))dlsym(handle, "r5_servo_key");
	sApi.set_max_size
		= (decltype(sApi.set_max_size))dlsym(handle, "r5_servo_set_max_size");
	sApi.control_answer = (decltype(sApi.control_answer))dlsym(handle,
		"r5_servo_control_answer");

	if (sApi.create == NULL || sApi.spin == NULL || sApi.load_url == NULL
		|| sApi.frame == NULL || sApi.set_waker == NULL || sApi.pump == NULL
		|| sApi.paint == NULL || sApi.mouse_move == NULL
		|| sApi.mouse_buttons == NULL || sApi.wheel == NULL
		|| sApi.key == NULL || sApi.destroy == NULL
		|| sApi.set_max_size == NULL || sApi.control_answer == NULL) {
		sError = "libr5servo.so is missing required r5_servo_* symbols.";
		aomi_log_add(sError.String());
		sAvailable = false;
		return B_ERROR;
	}

	sAvailable = true;
	aomi_log_add("Loaded libr5servo.so (Servo 0.8.0)");
	return B_OK;
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


const char*
aomi_servo_gl_info()
{
	return sGlInfo.String();
}


const char*
aomi_servo_last_protocol()
{
	return sLastProtocol.String();
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


BServoContentView::BServoContentView(const char* name, BWebSession* session)
	:
	BWebContentView(name != NULL ? name : "servo", session),
	fEngine(NULL),
	fBitmap(NULL),
	fRunner(NULL),
	fLoading(false),
	fIdleSpins(0)
{
	if (!sStarted)
		aomi_servo_start();
	if (!sAvailable)
		fError = sError;
	sViews.push_back(this);
}



BServoContentView::~BServoContentView()
{
	for (size_t i = 0; i < sViews.size(); i++) {
		if (sViews[i] == this) {
			sViews.erase(sViews.begin() + i);
			break;
		}
	}
	delete fRunner;
	delete fBitmap;
	if (fEngine != NULL && sApi.destroy != NULL)
		sApi.destroy(fEngine);
	fEngine = NULL;
}


void
BServoContentView::AttachedToWindow()
{
	BWebContentView::AttachedToWindow();
	// A tab can be created before it is attached; the pump handler and the
	// waker need the window.
	_EnsurePump();
	_ScheduleSpin(0);
}


void
BServoContentView::DetachedFromWindow()
{
	delete fRunner;
	fRunner = NULL;
	BWebContentView::DetachedFromWindow();
}


void
BServoContentView::Show()
{
	BWebContentView::Show();
	// The tab may have painted while hidden; take its latest frame.
	if (fEngine != NULL && sApi.frame != NULL)
		_CopyFrame();
	_ScheduleSpin(0);
}


void
BServoContentView::Hide()
{
	BWebContentView::Hide();
}


void
BServoContentView::PumpAll()
{
	if (sApi.pump == NULL)
		return;
	sApi.pump();
	// A tab is visible when neither it nor a parent is hidden; the tab cards
	// hide their container, not this view.
	for (size_t i = 0; i < sViews.size(); i++) {
		BServoContentView* view = sViews[i];
		if (view->fEngine == NULL || view->Window() == NULL || view->IsHidden())
			continue;
		if (sApi.paint != NULL && sApi.paint(view->fEngine) == 1)
			view->_CopyFrame();
	}
}


void
BServoContentView::_ScheduleSpin(bigtime_t delay)
{
	// Everything is driven by Servo's wake-ups now; this only asks for one.
	(void)delay;
	servo_wake(NULL);
}


void
BServoContentView::_EnsurePump()
{
	if (sPumpHandler != NULL || Window() == NULL)
		return;
	ServoPumpHandler* handler = new ServoPumpHandler();
	Window()->AddHandler(handler);
	sPumpHandler = handler;
	if (sWakeTarget == NULL)
		sWakeTarget = new BMessenger(handler);
	else
		*sWakeTarget = BMessenger(handler);
	if (sApi.set_waker != NULL)
		sApi.set_waker(servo_wake, NULL);
	handler->StartTick();
}


uint32
BServoContentView::_Modifiers() const
{
	uint32 mods = modifiers();
	uint32 out = 0;
	if ((mods & B_SHIFT_KEY) != 0)
		out |= 1;
	// Haiku's shortcut key (Alt by default) acts as Control inside a page,
	// so copy and paste in a form use the same keys as everywhere in R5.
	if ((mods & (B_CONTROL_KEY | B_COMMAND_KEY)) != 0)
		out |= 2;
	if ((mods & B_OPTION_KEY) != 0)
		out |= 8;
	return out;
}


void
BServoContentView::MouseDown(BPoint where)
{
	MakeFocus(true);
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (Window() != NULL && Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	fLastMouse = where;
	if (fEngine != NULL && sApi.mouse_buttons != NULL)
		sApi.mouse_buttons(fEngine, where.x, where.y, (uint32)buttons);
	_ScheduleSpin(0);
}


void
BServoContentView::MouseUp(BPoint where)
{
	int32 buttons = 0;
	if (Window() != NULL && Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	fLastMouse = where;
	if (fEngine != NULL && sApi.mouse_buttons != NULL)
		sApi.mouse_buttons(fEngine, where.x, where.y, (uint32)buttons);
	_ScheduleSpin(0);
}


void
BServoContentView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	BWebContentView::MouseMoved(where, transit, dragMessage);
	fLastMouse = where;
	if (fEngine != NULL && sApi.mouse_move != NULL)
		sApi.mouse_move(fEngine, where.x, where.y);
	_ScheduleSpin(0);
}


void
BServoContentView::_SendKey(const char* bytes, int32 numBytes, bool down)
{
	if (fEngine == NULL || sApi.key == NULL || bytes == NULL || numBytes < 1)
		return;
	BMessage* current = Window() != NULL ? Window()->CurrentMessage() : NULL;
	int32 named = 0;
	if (numBytes == 1) {
		switch (bytes[0]) {
			case B_ENTER: named = 1; break;
			case B_BACKSPACE: named = 2; break;
			case B_TAB: named = 3; break;
			case B_ESCAPE: named = 4; break;
			case B_DELETE: named = 5; break;
			case B_LEFT_ARROW: named = 6; break;
			case B_RIGHT_ARROW: named = 7; break;
			case B_UP_ARROW: named = 8; break;
			case B_DOWN_ARROW: named = 9; break;
			case B_HOME: named = 10; break;
			case B_END: named = 11; break;
			case B_PAGE_UP: named = 12; break;
			case B_PAGE_DOWN: named = 13; break;
			case B_INSERT: named = 14; break;
			case B_FUNCTION_KEY:
			{
				int32 key = 0;
				if (current != NULL)
					current->FindInt32("key", &key);
				if (key >= B_F1_KEY && key <= B_F12_KEY)
					named = 21 + (key - B_F1_KEY);
				else
					return;
				break;
			}
			default:
				if ((uint8)bytes[0] < 0x20)
					return;
				break;
		}
	}
	int32 repeat = 0;
	if (current != NULL)
		current->FindInt32("be:key_repeat", &repeat);
	BString text(bytes, numBytes);
	sApi.key(fEngine, down ? 1 : 0, named, text.String(), _Modifiers(),
		repeat > 0 ? 1 : 0);
	_ScheduleSpin(0);
}


void
BServoContentView::KeyDown(const char* bytes, int32 numBytes)
{
	if (fEngine == NULL) {
		BWebContentView::KeyDown(bytes, numBytes);
		return;
	}
	_SendKey(bytes, numBytes, true);
}


void
BServoContentView::KeyUp(const char* bytes, int32 numBytes)
{
	if (fEngine == NULL) {
		BWebContentView::KeyUp(bytes, numBytes);
		return;
	}
	_SendKey(bytes, numBytes, false);
}


status_t
BServoContentView::_Start(const char* document, bool isHTML)
{
	if (!sAvailable) {
		fError = sError;
		Invalidate();
		return B_ERROR;
	}
	if (fEngine != NULL)
		return B_OK;
	BWindow* window = Window();
	bool unlock = false;
	if (window != NULL && !window->IsLocked()) {
		if (!window->Lock())
			return B_ERROR;
		unlock = true;
	}
	int32 width = (int32)Bounds().Width() + 1;
	int32 height = (int32)Bounds().Height() + 1;
	if (width < 16)
		width = 16;
	if (height < 16)
		height = 16;
	_EnsurePump();
	{
		// libr5servo makes each tab's GL storage this large once; a view is
		// never larger (see surface_size() in distro/servo/engine/src/lib.rs).
		BScreen screen(window);
		BRect frame = screen.Frame();
		sApi.set_max_size((int32)frame.Width() + 1, (int32)frame.Height() + 1);
	}
	fEngine = sApi.create(width, height, document, isHTML ? 1 : 0,
		&_Callback, this);
	status_t status = fEngine != NULL ? B_OK : B_ERROR;
	if (status != B_OK) {
		fError = "Servo could not create a rendering context (check EGL/Vulkan).";
		aomi_log_add(fError.String());
		Invalidate();
	} else if (sApi.gl != NULL) {
		char gl[512];
		if (sApi.gl(fEngine, gl, (int32)sizeof(gl)) == 0) {
			sGlInfo = gl;
			aomi_log_add(gl);
		}
		fLoading = true;
		fIdleSpins = 6;
		_ScheduleSpin(150000);
	} else {
		fLoading = true;
		fIdleSpins = 6;
		_ScheduleSpin(150000);
	}
	if (unlock)
		window->Unlock();
	return status;
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
	if (fEngine == NULL)
		return _Start(url, false);
	fLoading = true;
	fIdleSpins = 6;
	_ScheduleSpin(150000);
	return engine_status(sApi.load_url(fEngine, url));
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
	if (fEngine == NULL)
		return _Start(html, true);
	fLoading = true;
	fIdleSpins = 6;
	_ScheduleSpin(150000);
	return engine_status(sApi.load_html(fEngine, html));
}


status_t
BServoContentView::Reload(bool bypassCache)
{
	if (bypassCache)
		return B_NOT_SUPPORTED;
	if (fEngine == NULL || sApi.reload == NULL)
		return B_NO_INIT;
	return engine_status(sApi.reload(fEngine, 0));
}


status_t
BServoContentView::StopLoading()
{
	if (fEngine == NULL || sApi.stop == NULL)
		return B_NO_INIT;
	return engine_status(sApi.stop(fEngine));
}


status_t
BServoContentView::GoBack()
{
	if (fEngine == NULL || sApi.back == NULL)
		return B_NO_INIT;
	return engine_status(sApi.back(fEngine));
}


status_t
BServoContentView::GoForward()
{
	if (fEngine == NULL || sApi.forward == NULL)
		return B_NO_INIT;
	return engine_status(sApi.forward(fEngine));
}


status_t
BServoContentView::_FillHistory(BMessage* items, bool forward) const
{
	if (items == NULL)
		return B_BAD_VALUE;
	if (fEngine == NULL || sApi.history == NULL)
		return B_NO_INIT;
	char url[2048];
	for (int32 index = 0; index < 64; index++) {
		if (sApi.history(fEngine, forward ? 1 : 0, index, url,
				(int32)sizeof(url)) != 0)
			break;
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
	if (fEngine == NULL || sApi.url == NULL) {
		*url = fCurrentURL;
		return B_OK;
	}
	char buffer[4096];
	int32 st = sApi.url(fEngine, buffer, (int32)sizeof(buffer));
	if (st != 0)
		return engine_status(st);
	*url = buffer;
	return B_OK;
}


status_t
BServoContentView::Title(BString* title) const
{
	if (title == NULL)
		return B_BAD_VALUE;
	if (fEngine == NULL || sApi.title == NULL) {
		*title = fCurrentTitle;
		return B_OK;
	}
	char buffer[4096];
	int32 st = sApi.title(fEngine, buffer, (int32)sizeof(buffer));
	if (st != 0)
		return engine_status(st);
	*title = buffer;
	return B_OK;
}


status_t
BServoContentView::SetZoom(float factor, bool textOnly)
{
	if (textOnly)
		return B_NOT_SUPPORTED;
	if (fEngine == NULL || sApi.zoom == NULL)
		return B_NO_INIT;
	return engine_status(sApi.zoom(fEngine, factor, 0));
}


status_t
BServoContentView::Find(const char* text, bool forward, bool matchCase)
{
	if (fEngine == NULL || sApi.find == NULL)
		return B_NOT_SUPPORTED;
	return engine_status(sApi.find(fEngine, text, forward ? 1 : 0,
		matchCase ? 1 : 0));
}


status_t
BServoContentView::SavePage(const char* path)
{
	if (path == NULL || path[0] == '\0' || fCurrentURL.Length() == 0)
		return B_BAD_VALUE;
	BPath p(path);
	BPath parent;
	p.GetParent(&parent);
	return aomi_start_download(fCurrentURL.String(), parent.Path());
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
	if (fEngine != NULL && sApi.protocol != NULL) {
		char buffer[32];
		if (sApi.protocol(fEngine, buffer, (int32)sizeof(buffer)) == 0
			&& buffer[0] != '\0') {
			*protocol = buffer;
			return B_OK;
		}
	}
	if (fProtocol.Length() > 0) {
		*protocol = fProtocol;
		return B_OK;
	}
	return B_NOT_SUPPORTED;
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
BServoContentView::AnswerControl(bool accept, int32 value, const char* text)
{
	if (fEngine == NULL || sApi.control_answer == NULL)
		return B_NO_INIT;
	status_t status = engine_status(sApi.control_answer(fEngine, accept ? 1 : 0,
		value, text));
	_ScheduleSpin(0);
	return status;
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
BServoContentView::Draw(BRect updateRect)
{
	(void)updateRect;
	if (fBitmap != NULL) {
		// Never stretched: the frame is drawn at its own size from the top
		// left. While a resize waits for Servo's next frame, the rest of the
		// view keeps the page background.
		BRect frame = fBitmap->Bounds();
		DrawBitmap(fBitmap, frame, frame);
		SetHighColor(255, 255, 255);
		BRect b = Bounds();
		if (b.right > frame.right)
			FillRect(BRect(frame.right + 1, b.top, b.right, b.bottom));
		if (b.bottom > frame.bottom)
			FillRect(BRect(b.left, frame.bottom + 1, frame.right, b.bottom));
		return;
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

	if (fError.Length() > 0) {
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
}


void
BServoContentView::FrameResized(float width, float height)
{
	BWebContentView::FrameResized(width, height);
	if (fEngine == NULL || sApi.resize == NULL)
		return;
	int32 w = (int32)width + 1;
	int32 h = (int32)height + 1;
	if (w < 16)
		w = 16;
	if (h < 16)
		h = 16;
	sApi.resize(fEngine, w, h);
	_ScheduleSpin(0);
}


void
BServoContentView::MakeFocus(bool focused)
{
	BWebContentView::MakeFocus(focused);
	if (fEngine != NULL && sApi.focus != NULL)
		sApi.focus(fEngine, focused ? 1 : 0);
}


void
BServoContentView::MessageReceived(BMessage* message)
{
	if (message->what == kSpinMessage) {
		_ScheduleSpin(0);
		return;
	}
	if (message->what == B_MOUSE_WHEEL_CHANGED) {
		float dx = 0;
		float dy = 0;
		message->FindFloat("be:wheel_delta_x", &dx);
		message->FindFloat("be:wheel_delta_y", &dy);
		if (fEngine != NULL && sApi.wheel != NULL) {
			// Three lines of the plain font per notch, like BScrollView.
			float notch = ceilf(be_plain_font->Size() * 4.0f);
			sApi.wheel(fEngine, fLastMouse.x, fLastMouse.y, dx, dy, notch);
			_ScheduleSpin(0);
		}
		return;
	}
	if (message->what == B_COLORS_UPDATED) {
		Invalidate();
		BWebContentView::MessageReceived(message);
		return;
	}
	BWebContentView::MessageReceived(message);
}


void
BServoContentView::_CopyFrame()
{
	const uint8* rgba = NULL;
	int32 width = 0;
	int32 height = 0;
	if (sApi.frame(fEngine, &rgba, &width, &height) != 0 || rgba == NULL)
		return;
	if (fBitmap == NULL || fBitmap->Bounds().IntegerWidth() + 1 != width
		|| fBitmap->Bounds().IntegerHeight() + 1 != height) {
		delete fBitmap;
		fBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGBA32);
		const char* logPath = getenv("AOMI_LOG");
		if (logPath != NULL) {
			FILE* lf = fopen(logPath, "a");
			if (lf != NULL) {
				fprintf(lf, "frame %dx%d %s\n", (int)width, (int)height,
					fCurrentURL.String());
				fclose(lf);
			}
		}
	}
	// Servo gives RGBA bytes; B_RGBA32 is B, G, R, A in memory. Swap red
	// and blue a word at a time and force the alpha opaque.
	uint8* dest = (uint8*)fBitmap->Bits();
	int32 stride = fBitmap->BytesPerRow();
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
	Invalidate();

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
BServoContentView::_Callback(void* context, const r5_servo_event* event)
{
	if (context == NULL || event == NULL)
		return;
	static_cast<BServoContentView*>(context)->_OnEvent(event);
}


void
BServoContentView::_OnEvent(const r5_servo_event* event)
{
	switch (event->kind) {
		case R5_EV_LOAD_STARTED:
			_Report(AOMI_LOAD_STARTED, event->url, NULL, 0, 0, 0, 0);
			_Report(AOMI_PROGRESS, NULL, NULL, 0.05f, 0, 0, 0);
			break;
		case R5_EV_LOAD_COMMITTED:
			_Report(AOMI_LOAD_COMMITTED, event->url, NULL, 0, 0, 0, 0);
			break;
		case R5_EV_LOAD_FINISHED:
			fLoading = false;
			if (fIdleSpins < 3)
				fIdleSpins = 3;
			_Report(AOMI_LOAD_FINISHED, event->url, NULL, 0, 0, 0, 0);
			_Report(AOMI_PROGRESS, NULL, NULL, 1.0f, 0, 0, 0);
			break;
		case R5_EV_LOAD_FAILED:
			fLoading = false;
			fIdleSpins = 1;
			_Report(AOMI_LOAD_FAILED, event->url, event->text, 0, event->a, 0, 0);
			break;
		case R5_EV_NAVIGATION:
			_Report(AOMI_NAVIGATION, NULL, NULL, 0, event->a, event->b, event->c);
			break;
		case R5_EV_TITLE:
			fCurrentTitle = event->text != NULL ? event->text : "";
			{
				BMessage message(AOMI_TITLE_CHANGED);
				message.AddString("title", fCurrentTitle);
				Report(&message);
			}
			break;
		case R5_EV_URL:
			if (event->url != NULL)
				fCurrentURL = event->url;
			_Report(AOMI_URL_CHANGED, event->url, NULL, 0, 0, 0, 0);
			_Report(AOMI_LOAD_COMMITTED, event->url, NULL, 0, 0, 0, 0);
			if (event->url != NULL && strncmp(event->url, "https:", 6) == 0)
				_Report(AOMI_SECURITY, NULL, "https", 0, AOMI_SECURITY_SECURE, 0, 0);
			else if (event->url != NULL && strncmp(event->url, "http:", 5) == 0)
				_Report(AOMI_SECURITY, NULL, "http", 0, AOMI_SECURITY_NOT_SECURE, 0, 0);
			break;
		case R5_EV_HOVER:
			{
				BMessage message(AOMI_HOVER_LINK);
				message.AddString("url", event->url != NULL ? event->url : "");
				Report(&message);
			}
			break;
		case R5_EV_PROTOCOL:
			if (event->text != NULL && event->text[0] != '\0') {
				fProtocol = event->text;
				sLastProtocol = event->text;
				BString logLine("protocol ");
				logLine << fProtocol << " " << fCurrentURL;
				aomi_log_add(logLine.String());
			}
			_Report(AOMI_PROTOCOL, NULL, event->text, 0, 0, 0, 0);
			break;
		case R5_EV_PROGRESS:
			_Report(AOMI_PROGRESS, NULL, NULL, event->progress, 0, 0, 0);
			break;
		case R5_EV_CURSOR:
			_Report(AOMI_CURSOR, NULL, NULL, 0, event->a, 0, 0);
			break;
		case R5_EV_FULLSCREEN:
			_Report(AOMI_FULLSCREEN, NULL, NULL, 0, event->a, 0, 0);
			break;
		case R5_EV_NEW_WINDOW:
			_Report(AOMI_NEW_WINDOW, event->url, NULL, 0, event->a, 0, 0);
			break;
		case R5_EV_CLOSE:
			_Report(AOMI_CLOSE_WANTED, NULL, NULL, 0, 0, 0, 0);
			break;
		case R5_EV_FAVICON:
		{
			if (event->favicon == NULL || event->favicon_width < 1)
				break;
			BBitmap icon(BRect(0, 0, event->favicon_width - 1,
				event->favicon_height - 1), B_RGBA32);
			uint8* dest = (uint8*)icon.Bits();
			int32 pixels = event->favicon_width * event->favicon_height;
			const uint8* src = event->favicon;
			if (event->favicon_format == 2) {
				memcpy(dest, src, (size_t)pixels * 4);
			} else if (event->favicon_format == 1) {
				for (int32 i = 0; i < pixels; i++) {
					dest[i * 4 + 0] = src[i * 4 + 2];
					dest[i * 4 + 1] = src[i * 4 + 1];
					dest[i * 4 + 2] = src[i * 4 + 0];
					dest[i * 4 + 3] = src[i * 4 + 3];
				}
			} else {
				break;
			}
			BMessage archived;
			if (icon.Archive(&archived) == B_OK) {
				BMessage message(AOMI_FAVICON);
				message.AddMessage("icon", &archived);
				Report(&message);
			}
			break;
		}
		case R5_EV_DIALOG:
			_Report(AOMI_DIALOG, NULL, event->text, 0, event->a, event->b,
				event->c);
			break;
		default:
			break;
	}
}
