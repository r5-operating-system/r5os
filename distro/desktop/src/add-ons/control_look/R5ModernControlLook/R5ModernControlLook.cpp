/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	R5 Modern controls (docs/design/THEMING.md, mockup A1/A2).

	A BControlLook add-on on top of HaikuControlLook, like Haiku's own
	FlatControlLook. It changes how a control is painted, never its size:
	every frame and background inset is Haiku's, so a window lays out the same
	with either look. The numbers come from the mockup at 12 pt and scale with
	the plain font (radius 7 on buttons and fields, 4 on check boxes, 8 on
	frames and tabs). Every colour is a ui_color: the light and the dark
	colour set of a theme give light and dark controls, and the accent is
	B_CONTROL_HIGHLIGHT_COLOR.

	Painter (CPU) draws the same shapes antialiased; nothing here needs the
	GPU.
*/


#include "R5ModernControlLook.h"

#include <algorithm>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <Menu.h>
#include <MenuBar.h>
#include <OS.h>
#include <FindDirectory.h>
#include <driver_settings.h>
#include <Control.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Region.h>
#include <View.h>

#include <AppServerLink.h>
#include <BMCPrivate.h>
#include <MenuPrivate.h>
#include <ServerProtocol.h>


namespace BPrivate {


static const float kButtonPopUpIndicatorWidth = 11;

static const rgb_color kWhite = { 255, 255, 255, 255 };
static const rgb_color kBlack = { 0, 0, 0, 255 };
static const rgb_color kInk = { 15, 23, 42, 255 };
	// the mockup's shadow and line colour (#0f172a)


static float
scale()
{
	float size = be_plain_font != NULL ? be_plain_font->Size() : 12.0f;
	return std::max(size, 9.0f) / 12.0f;
}


static float
radius(float pixels)
{
	return roundf(pixels * scale());
}


static rgb_color
blend(const rgb_color& from, const rgb_color& to, float amount)
{
	rgb_color color;
	color.red = static_cast<uint8>(roundf(from.red
		+ (to.red - from.red) * amount));
	color.green = static_cast<uint8>(roundf(from.green
		+ (to.green - from.green) * amount));
	color.blue = static_cast<uint8>(roundf(from.blue
		+ (to.blue - from.blue) * amount));
	color.alpha = 255;
	return color;
}


/*!	The thin line of frames, lists and tabs: #d5dae2 on #eef1f5 (light),
	#3a404c on #1f232b (dark).
*/
static rgb_color
line_color(const rgb_color& base)
{
	if (base.IsLight())
		return blend(base, kInk, 0.11f);
	return blend(base, kWhite, 0.13f);
}


//! The line of fields, buttons and check boxes (#b4bcc8, dark #454c59).
static rgb_color
control_line_color(const rgb_color& base)
{
	rgb_color line = ui_color(B_CONTROL_BORDER_COLOR);
	// a colour set that keeps Haiku's mid-grey border on a dark panel
	if (base.IsDark() && line.IsLight())
		return blend(base, kWhite, 0.2f);
	return line;
}


/*!	The line of fields, check boxes and radio buttons: in light the mockup's
	#b8c0cc, one step lighter than the button line (#b4bcc8).
*/
static rgb_color
field_line_color(const rgb_color& base)
{
	rgb_color line = control_line_color(base);
	if (line.IsDark() || base.IsDark())
		return line;
	return blend(line, kWhite, 0.03f);
}


static rgb_color
accent_color()
{
	return ui_color(B_CONTROL_HIGHLIGHT_COLOR);
}


//! The edge of an accent face (#0070db on #0084ff, about #1b73d6 on #1f85f5).
static rgb_color
accent_line_color()
{
	return blend(accent_color(), kInk, 0.15f);
}


// #pragma mark - menus


/*!	A menu in the R5 menu style (R5's BMenu: a column menu in a menu
	window while this look asks for the style). BMenu gives its view the
	menu colour, so app_server erases it and the look does not paint over
	it.
*/
static bool
r5_menu(BView* view)
{
	BMenu* menu = dynamic_cast<BMenu*>(view);
	return menu != NULL && BPrivate::MenuPrivate(menu).R5Style();
}


/*!	True while the compositor shows the window effects (the GPU mode). The
	answer of app_server (AS_R5_WINDOW_EFFECTS_ACTIVE, the call behind
	libr5ui's window_effects_active()) is kept for a second. Without effects
	the highlight and the pill are square, as in Safe mode (mockup M3, M6).
*/
static bool
effects_active()
{
	// shared by every window thread: the time and the answer are atomic
	static int64 sAsked = -1;
	static int32 sActive = 0;
	bigtime_t now = system_time();
	int64 asked = atomic_get64(&sAsked);
	if (asked < 0 || now - asked > 1000000) {
		BPrivate::AppServerLink link;
		link.StartMessage(AS_R5_WINDOW_EFFECTS_ACTIVE);
		int32 code;
		bool active = false;
		if (link.FlushWithReply(code) == B_OK && code == B_OK)
			link.Read<bool>(&active);
		atomic_set(&sActive, active ? 1 : 0);
		atomic_set64(&sAsked, now);
	}
	return atomic_get(&sActive) != 0;
}


//! The track of a scroll bar (#e9edf3 on #eef1f5, #23272f on #1f232b).
static rgb_color
track_color(const rgb_color& base)
{
	if (base.IsLight())
		return blend(base, kInk, 0.025f);
	return blend(base, kWhite, 0.02f);
}


static rgb_color
field_color()
{
	return ui_color(B_DOCUMENT_BACKGROUND_COLOR);
}


static rgb_color
hover_color(const rgb_color& face)
{
	if (face.IsLight())
		return blend(face, kWhite, 0.6f);
	return blend(face, kWhite, 0.06f);
}


static rgb_color
pressed_color(const rgb_color& face)
{
	if (face.IsLight())
		return blend(face, kInk, 0.08f);
	return blend(face, kWhite, 0.12f);
}


/*!	The scroll bar style is a part of a theme of its own (Haiku's or R5
	Modern's, docs/design/THEMING.md). The theme in use says which, in
	~/config/settings/r5/theme ("scroll_bars haiku"); a program reads it
	when it loads the look, at start and on a live switch.
*/
/*!	Haiku's contract: a frame or background insets \a rect only on the sides
	in \a borders (a spinner's buttons and a tab view's plain border name
	some sides only).
*/
static void
inset_sides(BRect& rect, float left, float top, float right, float bottom,
	uint32 borders)
{
	if ((borders & BControlLook::B_LEFT_BORDER) != 0)
		rect.left += left;
	if ((borders & BControlLook::B_TOP_BORDER) != 0)
		rect.top += top;
	if ((borders & BControlLook::B_RIGHT_BORDER) != 0)
		rect.right -= right;
	if ((borders & BControlLook::B_BOTTOM_BORDER) != 0)
		rect.bottom -= bottom;
}


/*!	A face that is open on the sides not in \a borders: the shape grows past
	those sides by \a grow, and the caller clips it back to the rect, so the
	corners there are cut off and the line on that side is not drawn.
*/
static BRect
open_sides(BRect rect, uint32 borders, float grow)
{
	if ((borders & BControlLook::B_LEFT_BORDER) == 0)
		rect.left -= grow;
	if ((borders & BControlLook::B_TOP_BORDER) == 0)
		rect.top -= grow;
	if ((borders & BControlLook::B_RIGHT_BORDER) == 0)
		rect.right += grow;
	if ((borders & BControlLook::B_BOTTOM_BORDER) == 0)
		rect.bottom += grow;
	return rect;
}


R5ModernControlLook::R5ModernControlLook()
	:
	HaikuControlLook(),
	fHaikuScrollBars(false),
	fScrollBarWidth(0)
{
	char path[B_PATH_NAME_LENGTH];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, path,
			sizeof(path)) != B_OK
		|| strlcat(path, "/r5/theme", sizeof(path)) >= sizeof(path))
		return;

	void* handle = load_driver_settings(path);
	if (handle == NULL)
		return;
	// the file's scroll bar style belongs to its controls: after a theme
	// with other controls, R5 Modern chosen elsewhere (Appearance) keeps
	// its own scroll bars
	const char* controls = get_driver_parameter(handle, "controls", NULL,
		NULL);
	const char* style = get_driver_parameter(handle, "scroll_bars", NULL,
		NULL);
	bool ours = controls != NULL
		&& strcmp(controls, "R5ModernControlLook") == 0;
	fHaikuScrollBars = ours && style != NULL && strcmp(style, "haiku") == 0;
	// the theme's scroll bar width (8 to 32 at 12 points); 0 is Haiku's
	const char* width = get_driver_parameter(handle, "scroll_bar_width", NULL,
		NULL);
	if (ours && width != NULL) {
		int value = atoi(width);
		if (value >= 8 && value <= 32)
			fScrollBarWidth = value;
	}
	unload_driver_settings(handle);
}


R5ModernControlLook::~R5ModernControlLook()
{
}


// #pragma mark - shapes


/*!	A round face: \a fill inside a 1 px \a line, both inside \a rect. The
	line sits on the outer pixel row, like a CSS border.
*/
void
R5ModernControlLook::_DrawFace(BView* view, BRect rect, float radius,
	const rgb_color& fill, const rgb_color& line)
{
	if (!rect.IsValid())
		return;

	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	if (radius <= 0) {
		view->SetHighColor(line);
		view->StrokeRect(rect);
		rect.InsetBy(1, 1);
		if (rect.IsValid()) {
			view->SetHighColor(fill);
			view->FillRect(rect);
		}
	} else {
		// the line is the outer ring of a filled round rect, the fill the
		// inner one, so the arc has no gap between the two
		BRect outer(rect.left, rect.top, rect.right + 1, rect.bottom + 1);
		view->SetHighColor(line);
		view->FillRoundRect(outer, radius, radius);
		outer.InsetBy(1, 1);
		view->SetHighColor(fill);
		view->FillRoundRect(outer, std::max(radius - 1, 0.0f),
			std::max(radius - 1, 0.0f));
	}

	view->PopState();
}


/*!	A down chevron centred in \a rect, the mockup's field chevron
	"M2 3.8 5 6.8 8 3.8" in a 10 px box (M6; external review 1.1).
*/
void
R5ModernControlLook::_DrawChevron(BView* view, BRect rect,
	const rgb_color& color)
{
	float size = radius(10);
	BPoint center(floorf((rect.left + rect.right + 1) / 2),
		floorf((rect.top + rect.bottom + 1) / 2));
	// 6 of 10 wide, 3 of 10 high
	float halfWidth = size * 3 / 10;
	float halfHeight = size * 1.5f / 10;

	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(color);
	view->SetPenSize(std::max(1.0f, roundf(1.6f * scale() * 4) / 4));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	view->StrokeLine(BPoint(center.x - halfWidth, center.y - halfHeight),
		BPoint(center.x, center.y + halfHeight));
	view->StrokeLine(BPoint(center.x, center.y + halfHeight),
		BPoint(center.x + halfWidth, center.y - halfHeight));
	view->PopState();
}


// #pragma mark - buttons


/*!	The colours of a button face for \a flags: \a base (the accent on a
	default button), lighter under the mouse, darker when pressed. Returns
	false for a flat button that is not hovered, which has no face.
*/
bool
R5ModernControlLook::_ButtonColors(BView* view, const rgb_color& base,
	uint32 flags, rgb_color& fill, rgb_color& line)
{
	bool disabled = (flags & B_DISABLED) != 0;
	bool flat = (flags & B_FLAT) != 0
		&& (flags & (B_ACTIVATED | B_PARTIALLY_ACTIVATED)) == 0
		&& ((flags & (B_HOVER | B_FOCUSED)) == 0 || disabled);
	if (flat)
		return false;

	rgb_color background = view->LowColor();
	if (view->Parent() != NULL)
		background = view->Parent()->ViewColor();

	fill = base;
	line = control_line_color(base);

	if ((flags & B_DEFAULT_BUTTON) != 0 && !disabled) {
		fill = accent_color();
		line = accent_line_color();
	}

	if (!disabled) {
		if ((flags & (B_ACTIVATED | B_CLICKED)) != 0)
			fill = pressed_color(fill);
		else if ((flags & B_HOVER) != 0)
			fill = hover_color(fill);
		if ((flags & B_FOCUSED) != 0 && (flags & B_DEFAULT_BUTTON) == 0)
			line = accent_color();
	} else {
		fill = disable_color(fill, background);
		line = disable_color(line, background);
	}
	return true;
}


/*!	Fills \a rect (a button inside its frame's inset) with the face colour,
	round so it meets the face's arc: the face is round 7 - 1 at an inset of
	1, so the inside at an inset of n is round 7 - n.
*/
void
R5ModernControlLook::_FillButtonInside(BView* view, const BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	rgb_color fill;
	rgb_color line;
	if (!_ButtonColors(view, base, flags, fill, line))
		return;

	float inset = (flags & B_DEFAULT_BUTTON) != 0 ? 5 : 2;
	inset = ceilf(inset * scale());
	float r = std::max(0.0f, radius(7) - inset);

	BRect shape = open_sides(rect, borders, r + 1);
	view->PushState();
	view->ClipToRect(BRect(rect.left, rect.top, rect.right + 1,
		rect.bottom + 1));
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(fill);
	view->FillRoundRect(BRect(shape.left, shape.top, shape.right + 1,
		shape.bottom + 1), r, r);
	view->PopState();
}


void
R5ModernControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	DrawButtonFrame(view, rect, updateRect, 0, 0, 0, 0, base, background,
		flags, borders);
}


void
R5ModernControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	DrawButtonFrame(view, rect, updateRect, 0, 0, 0, 0, base, background,
		flags, borders);
}


void
R5ModernControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, const rgb_color& background, uint32 flags,
	uint32 borders)
{
	float left, top, right, bottom;
	GetFrameInsets(B_BUTTON_FRAME, flags, left, top, right, bottom);
	if (!ShouldDraw(view, rect, updateRect)) {
		inset_sides(rect, left, top, right, bottom, borders);
		return;
	}

	rgb_color fill;
	rgb_color line;
	if (_ButtonColors(view, base, flags, fill, line)) {
		// the frame owns the outer pixel: Haiku's outer recessed edge
		BRect face(rect);
		inset_sides(face, 1, 1, 1, 1, borders);
		view->PushState();
		// ClipToRect() keeps the area up to the rect's right and bottom
		// edges, not their pixels; the face's line is on those pixels
		// (external review 1.2)
		view->ClipToRect(BRect(face.left, face.top, face.right + 1,
			face.bottom + 1));
		_DrawFace(view, open_sides(face, borders, radius(7) + 1),
			radius(7) - 1, fill, line);
		view->PopState();
	}

	inset_sides(rect, left, top, right, bottom, borders);
}


void
R5ModernControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	DrawButtonBackground(view, rect, updateRect, 0, 0, 0, 0, base, flags,
		borders, orientation);
}


void
R5ModernControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	DrawButtonBackground(view, rect, updateRect, 0, 0, 0, 0, base, flags,
		borders, orientation);
}


/*!	The frame painted the whole face; the background paints it again inside
	the frame's inset, round on the same centres, so a caller that draws a
	background without a frame (a column title, a tool bar) still gets a
	face. Then the inset, as Haiku's.
*/
void
R5ModernControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, uint32 flags, uint32 borders,
	orientation orientation)
{
	_FillButtonInside(view, rect, updateRect, base, flags, borders);

	float left, top, right, bottom;
	GetBackgroundInsets(B_BUTTON_BACKGROUND, flags, left, top, right, bottom);
	inset_sides(rect, left, top, right, bottom, borders);
}


void
R5ModernControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	DrawButtonWithPopUpBackground(view, rect, updateRect, 0, 0, 0, 0, base,
		flags, borders, orientation);
}


void
R5ModernControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	DrawButtonWithPopUpBackground(view, rect, updateRect, 0, 0, 0, 0, base,
		flags, borders, orientation);
}


void
R5ModernControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, uint32 flags, uint32 borders,
	orientation orientation)
{
	_FillButtonInside(view, rect, updateRect, base, flags, borders);

	BRect marker(rect);
	marker.left = marker.right - ComposeSpacing(kButtonPopUpIndicatorWidth);
	if (ShouldDraw(view, marker, updateRect)) {
		rgb_color text = ui_color(B_CONTROL_TEXT_COLOR);
		if ((flags & B_DISABLED) != 0)
			text = disable_color(text, base);
		_DrawChevron(view, marker, text);
	}

	float left, top, right, bottom;
	GetBackgroundInsets(B_BUTTON_WITH_POP_UP_BACKGROUND, flags, left, top,
		right, bottom);
	rect.left += left;
	rect.top += top;
	rect.right -= right;
	rect.bottom -= bottom;
}


// #pragma mark - menus


void
R5ModernControlLook::DrawMenuBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	if ((flags & (B_ACTIVATED | B_FOCUSED)) != 0) {
		// the open title of a menu bar: the selected item's face
		DrawMenuItemBackground(view, rect, updateRect,
			ui_color(B_MENU_SELECTED_BACKGROUND_COLOR), flags | B_ACTIVATED,
			borders);
		return;
	}

	view->SetHighColor(base);
	view->FillRect(rect);

	if ((borders & B_BOTTOM_BORDER) != 0) {
		view->SetHighColor(line_color(base));
		view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
	}
	// Haiku's bevel takes one pixel on each side asked for
	inset_sides(rect, 1, 1, 1, 1, borders);
}


void
R5ModernControlLook::_DrawField(BView* view, BRect& rect,
	const BRect& updateRect, uint32 flags, float inset)
{
	rgb_color background = view->LowColor();
	if (view->Parent() != NULL)
		background = view->Parent()->ViewColor();

	rgb_color fill = field_color();
	rgb_color line = field_line_color(background);
	if ((flags & B_DISABLED) != 0) {
		fill = disable_color(fill, background);
		line = disable_color(line, background);
	} else if ((flags & B_INVALID) != 0)
		line = ui_color(B_FAILURE_COLOR);
	else if ((flags & B_FOCUSED) != 0)
		line = accent_color();

	view->PushState();
	view->SetHighColor(background);
	view->StrokeRect(rect);
	if ((flags & B_FOCUSED) != 0 && (flags & B_DISABLED) == 0
		&& inset > 0) {
		// the soft accent ring of the mockup's focused field (A3), on the
		// outer pixel the face leaves free
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetHighColor(blend(background, accent_color(), 0.25f));
		view->StrokeRoundRect(rect, radius(7), radius(7));
	}
	view->PopState();

	BRect face(rect);
	face.InsetBy(inset, inset);
	_DrawFace(view, face, radius(7) - inset, fill, line);
}


void
R5ModernControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	DrawMenuFieldFrame(view, rect, updateRect, 0, 0, 0, 0, base, background,
		flags, borders);
}


void
R5ModernControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	DrawMenuFieldFrame(view, rect, updateRect, 0, 0, 0, 0, base, background,
		flags, borders);
}


void
R5ModernControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, const rgb_color& background, uint32 flags,
	uint32 borders)
{
	if (ShouldDraw(view, rect, updateRect))
		_DrawField(view, rect, updateRect, flags, 1);

	float left, top, right, bottom;
	GetFrameInsets(B_MENU_FIELD_FRAME, flags, left, top, right, bottom);
	rect.left += left;
	rect.top += top;
	rect.right -= right;
	rect.bottom -= bottom;
}


void
R5ModernControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, bool popupIndicator,
	uint32 flags)
{
	DrawMenuFieldBackground(view, rect, updateRect, 0, 0, 0, 0, base,
		popupIndicator, flags);
}


void
R5ModernControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base,
	bool popupIndicator, uint32 flags)
{
	DrawMenuFieldBackground(view, rect, updateRect, 0, 0, 0, 0, base,
		popupIndicator, flags);
}


/*!	The inside of a menu field: the field colour, and the chevron where Haiku
	draws its pop-up triangle. The frame already drew the round face.
*/
void
R5ModernControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, bool popupIndicator, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color fill = field_color();
	rgb_color text = ui_color(B_CONTROL_TEXT_COLOR);
	if ((flags & B_DISABLED) != 0) {
		fill = disable_color(fill, base);
		text = disable_color(text, fill);
	}

	// the round face is the frame's (radius 7 - 1 at an inset of 1); the
	// inside is round on the same centres, so the face's arcs stay
	// (external review 1.1)
	float r = std::max(0.0f, radius(7) - 3);
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(fill);
	view->FillRoundRect(BRect(rect.left, rect.top, rect.right + 1,
		rect.bottom + 1), r, r);
	view->PopState();

	if (popupIndicator) {
		// the mockup: a 10 px chevron 10 from the field's right edge, in the
		// middle of the field's height
		BRect field = view->Bounds();
		float size = radius(10);
		BRect marker(rect.right + 3 - radius(10) - size, field.top,
			rect.right + 3 - radius(10), field.bottom);
		_DrawChevron(view, marker, text);
		rect.right = marker.left - 1;
	}
}


void
R5ModernControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color fill = field_color();
	if ((flags & B_DISABLED) != 0)
		fill = disable_color(fill, base);
	float r = std::max(0.0f, radius(7) - 3);
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(fill);
	view->FillRoundRect(BRect(rect.left, rect.top, rect.right + 1,
		rect.bottom + 1), r, r);
	view->PopState();
}


void
R5ModernControlLook::DrawMenuBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// the R5 menu style: app_server erased the view in the menu colour,
	// and the window's line is the decorator's; the rect comes back by the
	// margin GetBackgroundInsets() gives
	if (r5_menu(view)) {
		float margin = roundf(5 * scale());
		inset_sides(rect, margin, margin, margin, margin, borders);
		return;
	}
	view->SetHighColor(base);
	view->FillRect(rect);
	// Haiku's bevel takes one pixel on each side asked for
	inset_sides(rect, 1, 1, 1, 1, borders);
}


static void menu_item_background(BView* view, const BRect& rect,
	const rgb_color& base, uint32 flags);


//! A selected menu item: the accent, round, a little inside the row.
void
R5ModernControlLook::DrawMenuItemBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// BMenuItem set the colour of its label before this call
	view->PushState();
	menu_item_background(view, rect, base, flags);
	view->PopState();

	// the R5 menu style: the rect comes back by the item padding
	// GetBackgroundInsets() gives (the content of the row)
	if (r5_menu(view)) {
		float left, top, right, bottom;
		GetBackgroundInsets(B_MENU_ITEM_BACKGROUND, flags, left, top, right,
			bottom);
		inset_sides(rect, left, top, right, bottom, borders);
	}
}


static void
menu_item_background(BView* view, const BRect& rect, const rgb_color& base,
	uint32 flags)
{
	const bool round = effects_active();

	if (dynamic_cast<_BMCMenuBar_*>(view) != NULL) {
		// the bar inside a pop-up field: the field draws its own face
		return;
	}
	if (dynamic_cast<BMenuBar*>(view) != NULL) {
		// a title of a menu bar: a quiet pill (mockup M4, M6) on the bar,
		// which BMenuBar::Draw() painted; the text keeps its colour (R5's
		// BMenuItem)
		rgb_color bar = ui_color(B_MENU_BACKGROUND_COLOR);
		rgb_color pill = bar.IsLight() ? blend(bar, kBlack, 0.07f)
			: blend(bar, kWhite, 0.10f);
		BRect face(rect);
		face.InsetBy(0, std::min(radius(2), floorf(rect.Height() / 4)));
		view->PushState();
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetHighColor(pill);
		float r = round ? radius(6) : 0;
		view->FillRoundRect(BRect(face.left, face.top, face.right + 1,
			face.bottom + 1), r, r);
		view->PopState();
		return;
	}

	if (r5_menu(view)) {
		// the R5 menu style: the row is the highlight, radius 6 (square
		// without effects); the background is app_server's
		rgb_color fill = base;
		if ((flags & BControlLook::B_DISABLED) != 0)
			fill = disable_color(fill, ui_color(B_MENU_BACKGROUND_COLOR));
		view->PushState();
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetHighColor(fill);
		float r = round ? std::min(radius(6), floorf(rect.Height() / 2)) : 0;
		view->FillRoundRect(BRect(rect.left, rect.top, rect.right + 1,
			rect.bottom + 1), r, r);
		view->PopState();
		return;
	}

	rgb_color background = view->ViewColor();
	if (background == B_TRANSPARENT_COLOR)
		background = ui_color(B_MENU_BACKGROUND_COLOR);

	view->SetHighColor(background);
	view->FillRect(rect);

	rgb_color fill = base;
	if ((flags & BControlLook::B_DISABLED) != 0)
		fill = disable_color(fill, background);

	BRect face(rect);
	face.InsetBy(std::min(radius(3), floorf(rect.Width() / 4)), 1);
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(fill);
	view->FillRoundRect(BRect(face.left, face.top, face.right + 1,
		face.bottom + 1), radius(5), radius(5));
	view->PopState();
}


/*!	The submenu chevron of the R5 menu style: the mockup's
	"M3.5 1.8 6.8 5 3.5 8.2" in a 10 px box, stroke 1.6, in the colour the
	item gives. Every other arrow is Haiku's.
*/
void
R5ModernControlLook::DrawArrowShape(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 direction,
	uint32 flags, float tint)
{
	if (direction != B_RIGHT_ARROW || !r5_menu(view)) {
		HaikuControlLook::DrawArrowShape(view, rect, updateRect, base,
			direction, flags, tint);
		return;
	}
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	float unit = (rect.Width() + 1) / 10;
	BPoint origin(rect.left, rect.top);
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(base);
	view->SetPenSize(std::max(1.0f, roundf(1.6f * unit * 4) / 4));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	view->StrokeLine(origin + BPoint(3.5f * unit, 1.8f * unit),
		origin + BPoint(6.8f * unit, 5 * unit));
	view->StrokeLine(origin + BPoint(6.8f * unit, 5 * unit),
		origin + BPoint(3.5f * unit, 8.2f * unit));
	view->PopState();
}


void
R5ModernControlLook::DrawStatusBar(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& barColor,
	float progressPosition)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->SetHighColor(base);
	view->FillRect(rect);

	float r = std::min(radius(4), floorf(rect.Height() / 2));
	BRect track(rect.left, rect.top, rect.right + 1, rect.bottom + 1);
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(blend(base, base.IsLight() ? kInk : kWhite, 0.08f));
	view->FillRoundRect(track, r, r);

	if (progressPosition > rect.left) {
		// round at its leading end too (external review 1.10)
		BRect filled(track);
		filled.right = std::min(track.right,
			std::max(track.left + 2 * r, progressPosition));
		view->SetHighColor(barColor);
		view->FillRoundRect(filled, r, r);
	}
	view->PopState();
}


// #pragma mark - check boxes and radio buttons


void
R5ModernControlLook::DrawCheckBox(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color background = view->LowColor();
	bool disabled = (flags & B_DISABLED) != 0;
	bool on = (flags & (B_ACTIVATED | B_PARTIALLY_ACTIVATED)) != 0;

	rgb_color fill = on ? accent_color() : field_color();
	rgb_color line = on ? accent_color() : field_line_color(background);
	if (!disabled && (flags & B_FOCUSED) != 0 && !on)
		line = accent_color();
	if (!disabled && (flags & B_CLICKED) != 0)
		fill = pressed_color(fill);
	if (disabled) {
		fill = disable_color(fill, background);
		line = disable_color(line, background);
	}

	_DrawFace(view, rect, radius(4), fill, line);

	if ((flags & B_FOCUSED) != 0 && on && !disabled) {
		// focus on a checked box: a ring of the accent around it
		view->PushState();
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetHighColor(blend(background, accent_color(), 0.35f));
		BRect ring(rect);
		ring.InsetBy(-1, -1);
		view->StrokeRoundRect(ring, radius(4) + 1, radius(4) + 1);
		view->PopState();
	}

	if (!on)
		return;

	// the tick "M5 12l5 5 9-10" of a 24 unit box, at 10/16 of the box
	rgb_color mark = kWhite;
	if (disabled)
		mark = disable_color(mark, fill);

	float size = rect.Width() + 1;
	float unit = size * 10.0f / 16.0f / 24.0f;
	BPoint origin(rect.left + (size - 24 * unit) / 2,
		rect.top + (size - 24 * unit) / 2);

	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(mark);
	view->SetPenSize(std::max(1.5f, 4 * unit));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	if ((flags & B_PARTIALLY_ACTIVATED) != 0) {
		view->StrokeLine(BPoint(origin.x + 6 * unit, origin.y + 12 * unit),
			BPoint(origin.x + 18 * unit, origin.y + 12 * unit));
	} else {
		BPoint points[3] = {
			BPoint(origin.x + 5 * unit, origin.y + 12 * unit),
			BPoint(origin.x + 10 * unit, origin.y + 17 * unit),
			BPoint(origin.x + 19 * unit, origin.y + 7 * unit)
		};
		view->StrokePolygon(points, 3, false);
	}
	view->PopState();
}


void
R5ModernControlLook::DrawRadioButton(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color background = view->LowColor();
	bool disabled = (flags & B_DISABLED) != 0;
	bool on = (flags & B_ACTIVATED) != 0;

	rgb_color fill = on ? accent_color() : field_color();
	rgb_color line = on ? accent_color() : field_line_color(background);
	if (!disabled && (flags & B_FOCUSED) != 0 && !on)
		line = accent_color();
	if (!disabled && (flags & B_CLICKED) != 0)
		fill = pressed_color(fill);
	if (disabled) {
		fill = disable_color(fill, background);
		line = disable_color(line, background);
	}

	BRect outer(rect.left, rect.top, rect.right + 1, rect.bottom + 1);
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(line);
	view->FillEllipse(outer);
	outer.InsetBy(1, 1);
	view->SetHighColor(fill);
	view->FillEllipse(outer);

	if (on) {
		rgb_color mark = disabled ? disable_color(kWhite, fill) : kWhite;
		float inset = roundf(rect.Width() * 0.3f);
		outer.InsetBy(inset - 1, inset - 1);
		view->SetHighColor(mark);
		view->FillEllipse(outer);
	}
	view->PopState();
}


// #pragma mark - scroll bars


/*!	No line round the bar: the track sits on the panel. The bar keeps its
	Haiku width and its border pixel, painted in the panel colour.
*/
void
R5ModernControlLook::DrawScrollBarBorder(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (fHaikuScrollBars) {
		HaikuControlLook::DrawScrollBarBorder(view, rect, updateRect, base,
			flags, orientation);
		return;
	}
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();
	view->SetHighColor(base);
	view->StrokeRect(rect);
	if ((flags & B_FOCUSED) != 0 && (flags & B_DISABLED) == 0) {
		// the scroll target has the focus: a thin accent line on the side
		// that faces it, as Haiku marks it
		view->SetHighColor(accent_color());
		if (orientation == B_HORIZONTAL)
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
		else
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
	}
	view->PopState();
}


//! An arrow: no button face, the arrow on the track.
void
R5ModernControlLook::DrawScrollBarButton(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& text,
	uint32 flags, int32 direction, orientation orientation, bool down)
{
	if (fHaikuScrollBars) {
		HaikuControlLook::DrawScrollBarButton(view, rect, updateRect, base,
			text, flags, direction, orientation, down);
		return;
	}
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	view->PushState();
	view->ClipToRect(rect);
	view->SetHighColor(panel);
	view->FillRect(rect);

	BRect face(rect);
	face.InsetBy(1, 1);
	rgb_color fill = track_color(panel);
	if (down)
		fill = pressed_color(fill);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(fill);
	view->FillRoundRect(BRect(face.left, face.top, face.right + 1,
		face.bottom + 1), radius(4), radius(4));

	rgb_color arrow = ui_color(B_PANEL_TEXT_COLOR);
	arrow = blend(fill, arrow, (flags & B_DISABLED) != 0 ? 0.3f : 0.6f);
	BRect arrowRect(rect);
	arrowRect.InsetBy(-1, -1);
	HaikuControlLook::DrawArrowShape(view, arrowRect, updateRect, arrow,
		direction, flags, 1.0f);
	view->PopState();
}


/*!	Paints the panel and the round track; \a part limits the pixels to one
	piece of the bar, \a track is the whole track so its ends are round.
*/
void
R5ModernControlLook::_FillTrackPart(BView* view, BRect part, BRect track,
	const rgb_color& base, orientation orientation)
{
	if (!part.IsValid())
		return;

	view->PushState();
	view->ClipToRect(part);
	view->SetHighColor(base);
	view->FillRect(part);

	BRect face(track);
	face.InsetBy(1, 1);
	float r = orientation == B_HORIZONTAL
		? floorf((face.Height() + 1) / 2) : floorf((face.Width() + 1) / 2);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(track_color(base));
	view->FillRoundRect(BRect(face.left, face.top, face.right + 1,
		face.bottom + 1), r, r);
	view->PopState();
}


void
R5ModernControlLook::DrawScrollBarBackground(BView* view, BRect& rect1,
	BRect& rect2, const BRect& updateRect, const rgb_color& base,
	uint32 flags, orientation orientation)
{
	if (fHaikuScrollBars) {
		HaikuControlLook::DrawScrollBarBackground(view, rect1, rect2,
			updateRect, base, flags, orientation);
		return;
	}

	BRect track = rect1 | rect2;
	if (!rect1.IsValid())
		track = rect2;
	else if (!rect2.IsValid())
		track = rect1;

	// the track runs on under the thumb, which is drawn on top
	BRect all(track);
	if (ShouldDraw(view, all, updateRect))
		_FillTrackPart(view, all, track, base, orientation);
}


void
R5ModernControlLook::DrawScrollBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (fHaikuScrollBars) {
		HaikuControlLook::DrawScrollBarBackground(view, rect, updateRect,
			base, flags, orientation);
		return;
	}
	if (ShouldDraw(view, rect, updateRect))
		_FillTrackPart(view, rect, rect, base, orientation);
}


//! The thumb: a round bar, 2 px inside the track on each side.
void
R5ModernControlLook::DrawScrollBarThumb(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation, uint32 knobStyle)
{
	if (fHaikuScrollBars) {
		HaikuControlLook::DrawScrollBarThumb(view, rect, updateRect, base,
			flags, orientation, knobStyle);
		return;
	}
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color thumb = base;
	// a colour set whose thumb colour is the panel's (Haiku's factory
	// colours) still gets a visible thumb
	if (abs(static_cast<int>(thumb.Brightness())
			- static_cast<int>(panel.Brightness())) < 24)
		thumb = blend(panel, panel.IsLight() ? kInk : kWhite, 0.25f);
	if ((flags & B_DISABLED) != 0)
		thumb = blend(thumb, track_color(panel), 0.6f);

	BRect face(rect);
	if (orientation == B_HORIZONTAL)
		face.InsetBy(0, 3);
	else
		face.InsetBy(3, 0);
	if (!face.IsValid())
		return;

	float r = orientation == B_HORIZONTAL
		? floorf((face.Height() + 1) / 2) : floorf((face.Width() + 1) / 2);

	view->PushState();
	view->ClipToRect(rect);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(thumb);
	view->FillRoundRect(BRect(face.left, face.top, face.right + 1,
		face.bottom + 1), r, r);
	view->PopState();
}


void
R5ModernControlLook::DrawScrollViewFrame(BView* view, BRect& rect,
	const BRect& updateRect, BRect verticalScrollBarFrame,
	BRect horizontalScrollBarFrame, const rgb_color& base,
	border_style borderStyle, uint32 flags, uint32 borders)
{
	if ((borders & B_ALL_BORDERS) != B_ALL_BORDERS) {
		// a scroll view that shares sides with its neighbours keeps
		// Haiku's square frame
		HaikuControlLook::DrawScrollViewFrame(view, rect, updateRect,
			verticalScrollBarFrame, horizontalScrollBarFrame, base,
			borderStyle, flags, borders);
		return;
	}

	BRect corner(rect.right, rect.bottom, rect.right, rect.bottom);
	if (horizontalScrollBarFrame.IsValid())
		corner.left = horizontalScrollBarFrame.right + 1;
	if (verticalScrollBarFrame.IsValid())
		corner.top = verticalScrollBarFrame.bottom + 1;

	if (borderStyle == B_NO_BORDER) {
		if (corner.IsValid()) {
			view->SetHighColor(base);
			view->FillRect(corner);
		}
		return;
	}

	rgb_color line = (flags & B_FOCUSED) != 0
		? accent_color() : line_color(base);

	if (borderStyle == B_FANCY_BORDER) {
		// Haiku's two pixels: the outer one is the panel, the inner one the
		// line, round at the corners
		view->PushState();
		view->SetHighColor(base);
		view->StrokeRect(rect);
		view->PopState();
		rect.InsetBy(1, 1);
	}

	view->PushState();
	view->SetHighColor(base);
	view->StrokeRect(rect);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(line);
	view->StrokeRoundRect(rect, radius(3), radius(3));
	view->PopState();
	rect.InsetBy(1, 1);

	if (corner.IsValid()) {
		view->SetHighColor(base);
		view->FillRect(corner);
	}
}


// #pragma mark - sliders


rgb_color
R5ModernControlLook::SliderBarColor(const rgb_color& base)
{
	// #d5dae2-ish on a light panel, #3a404c-ish on a dark one
	return base.IsLight() ? blend(base, kInk, 0.12f)
		: blend(base, kWhite, 0.12f);
}


void
R5ModernControlLook::DrawSliderBar(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, rgb_color leftFillColor,
	rgb_color rightFillColor, float sliderScale, uint32 flags,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// a 4 px round bar in the middle of Haiku's bar rect
	BRect bar(rect);
	float thickness = std::max(3.0f, radius(4));
	if (orientation == B_HORIZONTAL) {
		float middle = floorf((rect.top + rect.bottom) / 2);
		bar.top = middle - floorf(thickness / 2) + 1;
		bar.bottom = bar.top + thickness - 1;
	} else {
		float middle = floorf((rect.left + rect.right) / 2);
		bar.left = middle - floorf(thickness / 2) + 1;
		bar.right = bar.left + thickness - 1;
	}

	// the track is muted on either panel; the value side is the accent
	// unless the program chose its own fill colour (external review 1.8)
	rgb_color track = SliderBarColor(base);
	if (leftFillColor == rightFillColor) {
		leftFillColor = accent_color();
		rightFillColor = track;
	}
	if ((flags & B_DISABLED) != 0) {
		leftFillColor = disable_color(leftFillColor, base);
		rightFillColor = disable_color(rightFillColor, base);
	}

	// a vertical slider's value runs from the bottom (Haiku: "position is
	// reverse of coords")
	float split = orientation == B_HORIZONTAL
		? floorf(rect.left + rect.Width() * sliderScale)
		: floorf(rect.top + rect.Height() * (1.0f - sliderScale));
	BRect shape(bar.left, bar.top, bar.right + 1, bar.bottom + 1);
	float r = floorf(thickness / 2);

	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);

	BRect leftPart(shape);
	BRect rightPart(shape);
	if (orientation == B_HORIZONTAL) {
		leftPart.right = split;
		rightPart.left = split;
	} else {
		// the filled part (leftFillColor) is below the split
		leftPart.top = split;
		rightPart.bottom = split;
	}

	BRegion clip(leftPart);
	view->ConstrainClippingRegion(&clip);
	view->SetHighColor(leftFillColor);
	view->FillRoundRect(shape, r, r);
	clip.Set(rightPart);
	view->ConstrainClippingRegion(&clip);
	view->SetHighColor(rightFillColor);
	view->FillRoundRect(shape, r, r);
	view->PopState();
}


void
R5ModernControlLook::DrawSliderBar(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, rgb_color fillColor,
	uint32 flags, orientation orientation)
{
	DrawSliderBar(view, rect, updateRect, base, fillColor, fillColor, 1.0f,
		flags, orientation);
}


//! The thumb: a white disc with the control line (#e6e9ef in dark).
void
R5ModernControlLook::DrawSliderThumb(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	float size = std::min(rect.Width(), rect.Height()) + 1;
	BRect disc(0, 0, size, size);
	disc.OffsetTo(roundf((rect.left + rect.right + 1 - size) / 2),
		roundf((rect.top + rect.bottom + 1 - size) / 2));

	rgb_color background = view->LowColor();
	rgb_color fill = field_color().IsLight() ? kWhite
		: ui_color(B_CONTROL_TEXT_COLOR);
	rgb_color line = field_line_color(background);
	if ((flags & B_FOCUSED) != 0)
		line = accent_color();
	if ((flags & B_DISABLED) != 0) {
		fill = disable_color(fill, background);
		line = disable_color(line, background);
	}

	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	// a soft shadow under the disc, one pixel down
	rgb_color shadow = kInk;
	shadow.alpha = (flags & B_DISABLED) != 0 ? 0 : 38;
	view->SetHighColor(shadow);
	view->FillEllipse(disc.OffsetByCopy(0, 1));
	view->SetHighColor(line);
	view->FillEllipse(disc);
	disc.InsetBy(1, 1);
	view->SetHighColor(fill);
	view->FillEllipse(disc);
	view->PopState();
}


// #pragma mark - tabs


void
R5ModernControlLook::DrawTabFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, border_style borderStyle, uint32 side)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// the line under the tabs; the tab view's own frame runs on from it
	view->SetHighColor(base);
	view->FillRect(rect);
	view->SetHighColor(line_color(base));
	if (side == B_TOP_BORDER)
		view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
	else if (side == B_BOTTOM_BORDER)
		view->StrokeLine(rect.LeftTop(), rect.RightTop());
	else if (side == B_LEFT_BORDER)
		view->StrokeLine(rect.RightTop(), rect.RightBottom());
	else
		view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
}


static void
draw_tab(BView* view, BRect rect, const rgb_color& fill,
	const rgb_color& line, uint32 side, bool active)
{
	float r = radius(8);
	BRect shape(rect.left, rect.top, rect.right + 1, rect.bottom + 1);
	// round on the side away from the content only: grow the shape past
	// the content side by the radius and clip it back
	switch (side) {
		case BControlLook::B_TOP_BORDER:
			shape.bottom += r;
			break;
		case BControlLook::B_BOTTOM_BORDER:
			shape.top -= r;
			break;
		case BControlLook::B_LEFT_BORDER:
			shape.right += r;
			break;
		default:
			shape.left -= r;
			break;
	}

	view->PushState();
	// a region is in pixels: the rect itself (external review 1.9)
	BRegion clip(rect);
	view->ConstrainClippingRegion(&clip);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(line);
	view->FillRoundRect(shape, r, r);
	BRect inner(shape);
	inner.InsetBy(1, 1);
	view->SetHighColor(fill);
	view->FillRoundRect(inner, r - 1, r - 1);

	if (!active) {
		// an inactive tab stands on the line
		view->SetHighColor(line);
		if (side == BControlLook::B_TOP_BORDER)
			view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
		else if (side == BControlLook::B_BOTTOM_BORDER)
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
		else if (side == BControlLook::B_LEFT_BORDER)
			view->StrokeLine(rect.RightTop(), rect.RightBottom());
		else
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
	}
	view->PopState();
}


void
R5ModernControlLook::DrawActiveTab(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, uint32 side, int32 index, int32 selected, int32 first,
	int32 last)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color fill = base;
	draw_tab(view, rect, fill, line_color(base), side, true);
	// the label goes where Haiku's goes
	inset_sides(rect, 3, 3, 3, 3, borders);
}


void
R5ModernControlLook::DrawInactiveTab(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, uint32 side, int32 index, int32 selected, int32 first,
	int32 last)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// #e6eaf0 under #ffffff, #20242c under #262b34 (A1, A2)
	rgb_color fill = base.IsLight() ? blend(base, kInk, 0.06f)
		: blend(base, make_color(0, 0, 0), 0.17f);
	if ((flags & B_HOVER) != 0)
		fill = blend(fill, base, 0.5f);
	rgb_color background = view->LowColor();
	view->SetHighColor(background);
	view->FillRect(rect);

	BRect tab(rect);
	// inactive tabs are one pixel lower than the active one
	if (side == B_TOP_BORDER)
		tab.top += 1;
	else if (side == B_BOTTOM_BORDER)
		tab.bottom -= 1;
	draw_tab(view, tab, fill, line_color(base), side, false);
	// the label goes where Haiku's goes: 4 away from the open side, then 2
	switch (side) {
		case B_TOP_BORDER:
			rect.top += 4;
			break;
		case B_BOTTOM_BORDER:
			rect.bottom -= 4;
			break;
		case B_LEFT_BORDER:
			rect.left += 4;
			break;
		default:
			rect.right -= 4;
			break;
	}
	inset_sides(rect, 2, 2, 2, 2, borders);
}


// #pragma mark - borders


void
R5ModernControlLook::DrawBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, border_style borderStyle,
	uint32 flags, uint32 borders)
{
	if (borderStyle == B_NO_BORDER)
		return;

	rgb_color line = (flags & B_FOCUSED) != 0
		? accent_color() : line_color(base);
	rgb_color colors[2] = { base, line };
	int32 first = borderStyle == B_FANCY_BORDER ? 0 : 1;
	for (int32 i = first; i < 2; i++) {
		view->BeginLineArray(4);
		if ((borders & B_LEFT_BORDER) != 0)
			view->AddLine(rect.LeftTop(), rect.LeftBottom(), colors[i]);
		if ((borders & B_TOP_BORDER) != 0)
			view->AddLine(rect.LeftTop(), rect.RightTop(), colors[i]);
		if ((borders & B_RIGHT_BORDER) != 0)
			view->AddLine(rect.RightTop(), rect.RightBottom(), colors[i]);
		if ((borders & B_BOTTOM_BORDER) != 0)
			view->AddLine(rect.LeftBottom(), rect.RightBottom(), colors[i]);
		view->EndLineArray();
		inset_sides(rect, 1, 1, 1, 1, borders);
	}
}


void
R5ModernControlLook::DrawRaisedBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	DrawBorder(view, rect, updateRect, base, B_PLAIN_BORDER, flags, borders);
}


//! A box: one line, round 8 (the mockup's Parts box).
void
R5ModernControlLook::DrawGroupFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 borders)
{
	float r = radius(8);
	BRect shape(rect.left, rect.top, rect.right + 1, rect.bottom + 1);
	if ((borders & B_TOP_BORDER) == 0)
		shape.top -= r;
	if ((borders & B_BOTTOM_BORDER) == 0)
		shape.bottom += r;
	if ((borders & B_LEFT_BORDER) == 0)
		shape.left -= r;
	if ((borders & B_RIGHT_BORDER) == 0)
		shape.right += r;

	view->PushState();
	view->ClipToRect(rect);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(line_color(base));
	view->StrokeRoundRect(BRect(shape.left, shape.top, shape.right - 1,
		shape.bottom - 1), r, r);
	view->PopState();

	// Haiku draws three pixels here whatever the font size
	inset_sides(rect, 3, 3, 3, 3, borders);
}


/*!	A text field: the field colour inside the control line, round 7; the
	accent line and a soft ring when it has the focus (A3).
*/
void
R5ModernControlLook::DrawTextControlBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (ShouldDraw(view, rect, updateRect)) {
		if ((borders & B_ALL_BORDERS) == B_ALL_BORDERS)
			_DrawField(view, rect, updateRect, flags, 1);
		else {
			BRect field(rect);
			view->PushState();
			view->ClipToRect(BRect(rect.left, rect.top, rect.right + 1,
				rect.bottom + 1));
			field = open_sides(field, borders, radius(7) + 1);
			_DrawField(view, field, updateRect, flags, 1);
			view->PopState();
		}
	}

	// Haiku draws two pixels here whatever the font size
	inset_sides(rect, 2, 2, 2, 2, borders);
}


/*!	The theme's scroll bar width, scaled with the font like Haiku's; Haiku's
	own width when the theme sets none. BScrollBar and BScrollView ask this
	(Haiku's hook), so a program that lays out with them follows; one that
	uses B_V_SCROLL_BAR_WIDTH keeps Haiku's width.
*/
float
R5ModernControlLook::GetScrollBarWidth(orientation orientation)
{
	if (fScrollBarWidth <= 0)
		return HaikuControlLook::GetScrollBarWidth(orientation);
	return roundf(fScrollBarWidth * scale());
}


/*!	Haiku's insets, and 8 more on the left of a menu bar (at 12 points):
	R5's BMenuBar starts its first title there when the bar is on the left
	edge of a window with a frame, so the title and its menu clear the
	round corner of the R5 Modern window. For menus, the R5 menu style
	(docs/design/THEMING.md, "Menus"): more than 1 for B_MENU_BACKGROUND,
	and the item padding.
*/
void
R5ModernControlLook::GetBackgroundInsets(background_type backgroundType,
	uint32 flags, float& _left, float& _top, float& _right, float& _bottom)
{
	HaikuControlLook::GetBackgroundInsets(backgroundType, flags, _left, _top,
		_right, _bottom);
	if (backgroundType == B_MENU_BAR_BACKGROUND)
		_left += roundf(8 * scale());
	else if (backgroundType == B_MENU_BACKGROUND) {
		// more than 1 asks R5's BMenu for the R5 menu style: 5 inside the
		// menu window (mockup M6)
		_left = _top = _right = _bottom = roundf(5 * scale());
	} else if (backgroundType == B_MENU_ITEM_BACKGROUND) {
		// the padding round an item's content in that style: 11 at the left
		// (R5's BMenu adds 16 for the mark column when a menu has marks),
		// 9 at the right, and a row of 26 (mockup M4, M6)
		font_height height;
		be_plain_font->GetHeight(&height);
		float text = ceilf(height.ascent + height.descent);
		float extra = std::max(0.0f, roundf(26 * scale()) - text);
		_left = roundf(11 * scale());
		_right = roundf(9 * scale());
		_top = floorf(extra / 2);
		_bottom = extra - _top;
	}
}


// #pragma mark - labels


//! The label of a default button is white on the accent.
void
R5ModernControlLook::DrawLabel(BView* view, const char* label,
	const BBitmap* icon, BRect rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags, const BAlignment& alignment,
	const rgb_color* textColor)
{
	if ((flags & B_DEFAULT_BUTTON) != 0 && (flags & B_DISABLED) == 0) {
		HaikuControlLook::DrawLabel(view, label, icon, rect, updateRect,
			accent_color(), flags, alignment, &kWhite);
		return;
	}

	HaikuControlLook::DrawLabel(view, label, icon, rect, updateRect, base,
		flags, alignment, textColor);
}


}	// namespace BPrivate


extern "C" BControlLook*
instantiate_control_look(image_id id)
{
	return new(std::nothrow) BPrivate::R5ModernControlLook();
}
