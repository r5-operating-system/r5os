/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Private ABI between libr5servo's Rust engine and BServoView.
 * The numbers are not the Aomi message codes. Keep them in sync with
 * distro/servo/engine/src/lib.rs (R5_EV_*).
 */
#ifndef R5_SERVO_ABI_H
#define R5_SERVO_ABI_H


#include <SupportDefs.h>


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


struct r5_servo;

typedef void (*r5_servo_callback)(void* context, const r5_servo_event* event);


extern "C" {


r5_servo*	r5_servo_create(int32 width, int32 height, const char* document,
	int32 document_is_html, r5_servo_callback callback, void* context);
void		r5_servo_resize(r5_servo* engine, int32 width, int32 height);
status_t	r5_servo_load_url(r5_servo* engine, const char* url);
status_t	r5_servo_load_html(r5_servo* engine, const char* html);
status_t	r5_servo_reload(r5_servo* engine, int32 bypass_cache);
status_t	r5_servo_stop(r5_servo* engine);
status_t	r5_servo_back(r5_servo* engine);
status_t	r5_servo_forward(r5_servo* engine);
status_t	r5_servo_zoom(r5_servo* engine, float factor, int32 text_only);
status_t	r5_servo_focus(r5_servo* engine, int32 focused);
void		r5_servo_spin(r5_servo* engine);
status_t	r5_servo_frame(r5_servo* engine, const uint8** rgba, int32* width,
	int32* height);
status_t	r5_servo_url(r5_servo* engine, char* buffer, int32 length);
status_t	r5_servo_title(r5_servo* engine, char* buffer, int32 length);
status_t	r5_servo_protocol(r5_servo* engine, char* buffer, int32 length);
status_t	r5_servo_history(r5_servo* engine, int32 forward, int32 index,
	char* buffer, int32 length);
status_t	r5_servo_gl(r5_servo* engine, char* buffer, int32 length);
status_t	r5_servo_find(r5_servo* engine, const char* text, int32 forward,
	int32 match_case);
void		r5_servo_destroy(r5_servo* engine);

// Since 0.8.0-3. Servo runs one instance per process, on the thread of the
// one browser window. The waker is called from Servo's own threads; it should
// only post a message to that window, which then calls r5_servo_pump() (or
// r5_servo_spin() for the visible tab) on the window thread.
typedef void (*r5_servo_wake_function)(void* cookie);
void		r5_servo_set_waker(r5_servo_wake_function wake, void* cookie);
int32		r5_servo_pump(void);
// Paints the tab if Servo has a new frame for it (no event loop). Returns 1
// when r5_servo_frame() now holds a new frame.
int32		r5_servo_paint(r5_servo* engine);

// Input, in view pixels. Buttons is Haiku's bit field after the change.
// Wheel deltas are Haiku's (one notch is 1.0, positive is down or right).
status_t	r5_servo_mouse_move(r5_servo* engine, float x, float y);
status_t	r5_servo_mouse_buttons(r5_servo* engine, float x, float y,
	uint32 buttons);
status_t	r5_servo_wheel(r5_servo* engine, float x, float y, float dx,
	float dy, float pixels_per_notch);

enum {
	R5_SERVO_KEY_NONE		= 0,
	R5_SERVO_KEY_ENTER		= 1,
	R5_SERVO_KEY_BACKSPACE	= 2,
	R5_SERVO_KEY_TAB		= 3,
	R5_SERVO_KEY_ESCAPE		= 4,
	R5_SERVO_KEY_DELETE		= 5,
	R5_SERVO_KEY_LEFT		= 6,
	R5_SERVO_KEY_RIGHT		= 7,
	R5_SERVO_KEY_UP			= 8,
	R5_SERVO_KEY_DOWN		= 9,
	R5_SERVO_KEY_HOME		= 10,
	R5_SERVO_KEY_END		= 11,
	R5_SERVO_KEY_PAGE_UP	= 12,
	R5_SERVO_KEY_PAGE_DOWN	= 13,
	R5_SERVO_KEY_INSERT		= 14,
	R5_SERVO_KEY_F1			= 21	// F1 to F12 are 21 to 32
};

enum {
	R5_SERVO_MOD_SHIFT		= 1,
	R5_SERVO_MOD_CONTROL	= 2,
	R5_SERVO_MOD_ALT		= 4,
	R5_SERVO_MOD_META		= 8
};

// A named key wins over text. Text is the key's UTF-8.
status_t	r5_servo_key(r5_servo* engine, int32 down, int32 named_key,
	const char* text, uint32 modifiers, int32 repeat);

// The largest view this process shows, normally the screen. Call before
// r5_servo_create(): each tab's GL storage is made this large once, and a view
// is never larger (resizing the storage would mean destroying it).
void		r5_servo_set_max_size(int32 width, int32 height);

// The answer to the last R5_EV_DIALOG (alert, confirm, prompt, select list).
// accept = 0 cancels. value: the chosen option id of a select list. text: a
// prompt's text, or NULL.
status_t	r5_servo_control_answer(r5_servo* engine, int32 accept, int32 value,
	const char* text);



}


#endif
