/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The controller picture of docs/GAME-CONTROLLERS.md: four family shapes
 * (PlayStation, Xbox, Nintendo, Sega Genesis) and, for a controller
 * without a mapping, a picture built from what it reports. Simplified
 * silhouettes in R5's thin-line style, drawn for R5 from the owner-approved
 * mockup (docs/design/gamecontrollers-mockup), no logos. Everything is
 * drawn with polygons, ellipses and round rectangles: a stroked BShape is
 * not drawn on every app_server path (R5Icons.cpp).
 */


#include "GameControllerView.h"

#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <InterfaceDefs.h>
#include <LayoutUtils.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Window.h>

#include <R5Icons.h>

#include <algorithm>
#include <vector>


static const uint32 kMsgPoll = 'gcvP';
static const bigtime_t kPollInterval = 16000;

// The picture's own coordinates (the mockup's viewBox). A live picture
// has room for the triggers above the body.
static const BRect kLiveBox(-30, -26, 430, 244);
static const BRect kShapeBox(0, 36, 400, 236);


namespace {


struct face_label {
	const char*	text;
	r5_glyph	glyph;		// 0: text
};


// Which picture, and what is pressed now.
struct picture_state {
	uint32	buttons;
	int16	axes[B_CONTROLLER_AXIS_COUNT];
	int32	rawAxisCount;
	int16	rawAxes[8];
	int32	rawButtonCount;
	uint32	rawButtons;
	int32	rawHatCount;
	uint8	rawHat;
	int32	battery;
};


struct painter {
	BView*		view;
	float		scale;
	BPoint		origin;
	rgb_color	background;
	rgb_color	line;
	rgb_color	body;
	rgb_color	key;
	rgb_color	lit;
	rgb_color	litLine;
	rgb_color	quiet;
	rgb_color	onLit;

	BPoint Map(float x, float y) const
	{
		return BPoint(origin.x + x * scale, origin.y + y * scale);
	}

	BRect MapRect(float x, float y, float width, float height) const
	{
		BPoint a = Map(x, y);
		BPoint b = Map(x + width, y + height);
		return BRect(a, b);
	}

	float Size(float v) const
	{
		return v * scale;
	}
};


// A path flattened into points, for FillPolygon() and StrokePolygon().
class Outline {
public:
	void Move(float x, float y)
	{
		fPoints.clear();
		fPoints.push_back(BPoint(x, y));
	}

	void Line(float x, float y)
	{
		fPoints.push_back(BPoint(x, y));
	}

	void Curve(float x1, float y1, float x2, float y2, float x, float y)
	{
		BPoint p0 = fPoints.back();
		const int kSteps = 14;
		for (int i = 1; i <= kSteps; i++) {
			float t = (float)i / kSteps;
			float u = 1 - t;
			float a = u * u * u;
			float b = 3 * u * u * t;
			float c = 3 * u * t * t;
			float d = t * t * t;
			fPoints.push_back(BPoint(a * p0.x + b * x1 + c * x2 + d * x,
				a * p0.y + b * y1 + c * y2 + d * y));
		}
	}

	void Draw(const painter& p, rgb_color fill, rgb_color stroke,
		float pen) const
	{
		std::vector<BPoint> mapped;
		for (size_t i = 0; i < fPoints.size(); i++)
			mapped.push_back(p.Map(fPoints[i].x, fPoints[i].y));
		p.view->SetHighColor(fill);
		p.view->FillPolygon(&mapped[0], mapped.size());
		p.view->SetHighColor(stroke);
		p.view->SetPenSize(std::max(1.0f, p.Size(pen)));
		p.view->StrokePolygon(&mapped[0], mapped.size(), true);
	}

private:
	std::vector<BPoint>	fPoints;
};


void
grip_body(const painter& p)
{
	Outline o;
	o.Move(90, 40);
	o.Line(310, 40);
	o.Curve(350, 40, 372, 60, 383, 100);
	o.Line(397, 168);
	o.Curve(405, 208, 382, 232, 352, 226);
	o.Curve(330, 221, 317, 203, 304, 182);
	o.Line(284, 152);
	o.Line(116, 152);
	o.Line(96, 182);
	o.Curve(83, 203, 70, 221, 48, 226);
	o.Curve(18, 232, -5, 208, 3, 168);
	o.Line(17, 100);
	o.Curve(28, 60, 50, 40, 90, 40);
	o.Draw(p, p.body, p.line, 2);
}


void
genesis_body(const painter& p)
{
	Outline o;
	o.Move(70, 48);
	o.Line(330, 48);
	o.Curve(376, 48, 396, 92, 396, 128);
	o.Curve(396, 178, 364, 206, 322, 206);
	o.Curve(292, 206, 276, 186, 252, 186);
	o.Line(148, 186);
	o.Curve(124, 186, 108, 206, 78, 206);
	o.Curve(36, 206, 4, 178, 4, 128);
	o.Curve(4, 92, 24, 48, 70, 48);
	o.Draw(p, p.body, p.line, 2);
}


void
box_body(const painter& p)
{
	p.view->SetHighColor(p.body);
	BRect r = p.MapRect(10, 44, 380, 170);
	float radius = p.Size(26);
	p.view->FillRoundRect(r, radius, radius);
	p.view->SetHighColor(p.line);
	p.view->SetPenSize(std::max(1.0f, p.Size(2)));
	p.view->StrokeRoundRect(r, radius, radius);
}


/*!	A plus-shaped d-pad around (cx, cy): arms halfWidth and halfHeight long,
	2 * thickness wide. mask: the lit arms (1 up, 2 right, 4 down, 8 left).
*/
void
dpad(const painter& p, float cx, float cy, float halfWidth, float halfHeight,
	float thickness, uint8 mask)
{
	float t = thickness;
	if (mask != 0) {
		p.view->SetHighColor(p.lit);
		if (mask & 1)
			p.view->FillRect(p.MapRect(cx - t, cy - halfHeight, 2 * t,
				halfHeight - t));
		if (mask & 2)
			p.view->FillRect(p.MapRect(cx + t, cy - t, halfWidth - t, 2 * t));
		if (mask & 4)
			p.view->FillRect(p.MapRect(cx - t, cy + t, 2 * t,
				halfHeight - t));
		if (mask & 8)
			p.view->FillRect(p.MapRect(cx - halfWidth, cy - t, halfWidth - t,
				2 * t));
	}
	BPoint points[12] = {
		p.Map(cx - t, cy - halfHeight), p.Map(cx + t, cy - halfHeight),
		p.Map(cx + t, cy - t), p.Map(cx + halfWidth, cy - t),
		p.Map(cx + halfWidth, cy + t), p.Map(cx + t, cy + t),
		p.Map(cx + t, cy + halfHeight), p.Map(cx - t, cy + halfHeight),
		p.Map(cx - t, cy + t), p.Map(cx - halfWidth, cy + t),
		p.Map(cx - halfWidth, cy - t), p.Map(cx - t, cy - t)
	};
	p.view->SetHighColor(p.line);
	p.view->SetPenSize(std::max(1.0f, p.Size(2)));
	p.view->StrokePolygon(points, 12, true);
}


void
key(const painter& p, float cx, float cy, float radius, bool pressed,
	const face_label* label, float fontSize)
{
	BRect r = p.MapRect(cx - radius, cy - radius, 2 * radius, 2 * radius);
	p.view->SetHighColor(pressed ? p.lit : p.key);
	p.view->FillEllipse(r);
	p.view->SetHighColor(pressed ? p.litLine : p.line);
	p.view->SetPenSize(std::max(1.0f, p.Size(1.6f)));
	p.view->StrokeEllipse(r);
	if (label == NULL)
		return;

	rgb_color color = pressed ? p.onLit : p.line;
	if (label->glyph != 0) {
		float side = p.Size(radius * 1.15f);
		BPoint center = p.Map(cx, cy);
		r5_glyph_draw(p.view, label->glyph, BRect(center.x - side / 2,
			center.y - side / 2, center.x + side / 2, center.y + side / 2),
			color);
		return;
	}
	BFont font(be_bold_font);
	font.SetSize(std::max(6.0f, p.Size(fontSize)));
	p.view->SetFont(&font);
	font_height height;
	font.GetHeight(&height);
	BPoint center = p.Map(cx, cy);
	float width = font.StringWidth(label->text);
	p.view->SetHighColor(color);
	p.view->DrawString(label->text, BPoint(center.x - width / 2,
		center.y + (height.ascent - height.descent) / 2));
}


void
pill(const painter& p, float x, float y, float width, float height,
	bool pressed)
{
	BRect r = p.MapRect(x, y, width, height);
	float radius = std::min(r.Width(), r.Height()) / 2;
	p.view->SetHighColor(pressed ? p.lit : p.key);
	p.view->FillRoundRect(r, radius, radius);
	p.view->SetHighColor(pressed ? p.litLine : p.line);
	p.view->SetPenSize(std::max(1.0f, p.Size(1.6f)));
	p.view->StrokeRoundRect(r, radius, radius);
}


void
small_text(const painter& p, const char* text, float cx, float baseline)
{
	BFont font(be_bold_font);
	font.SetSize(std::max(6.0f, p.Size(10)));
	p.view->SetFont(&font);
	float width = font.StringWidth(text);
	p.view->SetHighColor(p.quiet);
	BPoint at = p.Map(cx, baseline);
	p.view->DrawString(text, BPoint(at.x - width / 2, at.y));
}


/*!	A stick: a ring with a dot that follows it. The ring fills when the
	stick is pressed in; the dot lights up once it leaves the middle.
*/
void
stick(const painter& p, float cx, float cy, float radius, int16 x, int16 y,
	bool pressed, bool live)
{
	BRect ring = p.MapRect(cx - radius, cy - radius, 2 * radius, 2 * radius);
	if (pressed) {
		p.view->SetHighColor(p.lit);
		p.view->FillEllipse(ring);
	}
	p.view->SetHighColor(p.line);
	p.view->SetPenSize(std::max(1.0f, p.Size(2)));
	p.view->StrokeEllipse(ring);
	if (!live)
		return;

	float travel = radius - 8;
	float dx = x / 32768.0f * travel;
	float dy = y / 32768.0f * travel;
	bool moved = abs(x) > 6000 || abs(y) > 6000;
	p.view->SetHighColor(pressed ? p.onLit : (moved ? p.lit : p.line));
	float dot = 8;
	p.view->FillEllipse(p.MapRect(cx + dx - dot, cy + dy - dot, 2 * dot,
		2 * dot));
}


// A trigger: a bar that fills as far as it is pulled, with its name and,
// while pulled, how far.
void
trigger(const painter& p, float x, float y, const char* name, int16 value)
{
	BRect r = p.MapRect(x, y, 70, 10);
	float radius = p.Size(3);
	if (value > 0) {
		BRect fill = r;
		fill.right = fill.left + r.Width() * value / 32767.0f;
		p.view->SetHighColor(p.lit);
		p.view->FillRoundRect(fill, radius, radius);
	}
	p.view->SetHighColor(p.line);
	p.view->SetPenSize(std::max(1.0f, p.Size(2)));
	p.view->StrokeRoundRect(r, radius, radius);

	char text[32];
	if (value > 300)
		snprintf(text, sizeof(text), "%s \xc2\xb7 %d %%", name,
			(int)(value * 100.0f / 32767 + 0.5f));
	else
		snprintf(text, sizeof(text), "%s", name);
	small_text(p, text, x + 35, y + 24);
}


void
shoulder(const painter& p, float x, float y, const char* name, bool pressed)
{
	BRect r = p.MapRect(x, y, 56, 12);
	float radius = p.Size(5);
	p.view->SetHighColor(pressed ? p.lit : p.key);
	p.view->FillRoundRect(r, radius, radius);
	p.view->SetHighColor(pressed ? p.litLine : p.line);
	p.view->SetPenSize(std::max(1.0f, p.Size(1.6f)));
	p.view->StrokeRoundRect(r, radius, radius);
	small_text(p, name, x + 28, y - 4);
}


}	// namespace


// #pragma mark - BGameControllerView


struct BGameControllerView::Private {
	BGameController*		controller;
	game_controller_family	family;
	game_controller_labels	labels;
	BMessageRunner*			runner;
	picture_state			state;

	Private()
		:
		controller(NULL),
		family(B_GAME_CONTROLLER_FAMILY_XBOX),
		labels(B_GAME_CONTROLLER_LABELS_AUTOMATIC),
		runner(NULL)
	{
		memset(&state, 0, sizeof(state));
		state.battery = -1;
	}
};


BGameControllerView::BGameControllerView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_SUBPIXEL_PRECISE),
	fPrivate(new(std::nothrow) Private)
{
	memset(_reserved, 0, sizeof(_reserved));
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
}


BGameControllerView::~BGameControllerView()
{
	_StopPolling();
	if (fPrivate != NULL)
		delete fPrivate->controller;
	delete fPrivate;
}


status_t
BGameControllerView::SetDevice(const char* deviceName)
{
	if (fPrivate == NULL)
		return B_NO_MEMORY;
	_StopPolling();
	delete fPrivate->controller;
	fPrivate->controller = NULL;
	memset(&fPrivate->state, 0, sizeof(fPrivate->state));
	fPrivate->state.battery = -1;

	status_t status = B_OK;
	if (deviceName != NULL) {
		BGameController* controller = new(std::nothrow) BGameController;
		if (controller == NULL)
			status = B_NO_MEMORY;
		else {
			status = controller->Open(deviceName);
			if (status == B_OK) {
				fPrivate->controller = controller;
				fPrivate->family = controller->Family();
			} else
				delete controller;
		}
	}
	if (fPrivate->controller != NULL && Window() != NULL)
		_StartPolling();
	Invalidate();
	return status;
}


BGameController*
BGameControllerView::Controller()
{
	return fPrivate != NULL ? fPrivate->controller : NULL;
}


void
BGameControllerView::SetFamily(game_controller_family family)
{
	if (fPrivate == NULL)
		return;
	SetDevice(NULL);
	fPrivate->family = family;
	Invalidate();
}


game_controller_family
BGameControllerView::Family() const
{
	return fPrivate != NULL ? fPrivate->family
		: B_GAME_CONTROLLER_FAMILY_UNKNOWN;
}


void
BGameControllerView::SetLabels(game_controller_labels labels)
{
	if (fPrivate == NULL || fPrivate->labels == labels)
		return;
	fPrivate->labels = labels;
	Invalidate();
}


void
BGameControllerView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (fPrivate != NULL && fPrivate->controller != NULL)
		_StartPolling();
}


void
BGameControllerView::DetachedFromWindow()
{
	_StopPolling();
	BView::DetachedFromWindow();
}


void
BGameControllerView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	Invalidate();
}


void
BGameControllerView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPoll:
			if (_Poll())
				Invalidate();
			break;
		case B_COLORS_UPDATED:
			Invalidate();
			BView::MessageReceived(message);
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


BSize
BGameControllerView::MinSize()
{
	float unit = be_plain_font->Size() / 13.0f;
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		BSize(roundf(104 * unit), roundf(62 * unit)));
}


BSize
BGameControllerView::PreferredSize()
{
	float unit = be_plain_font->Size() / 13.0f;
	if (fPrivate != NULL && fPrivate->controller != NULL)
		return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
			BSize(roundf(400 * unit), roundf(236 * unit)));
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		BSize(roundf(104 * unit), roundf(62 * unit)));
}


void
BGameControllerView::_StartPolling()
{
	if (fPrivate == NULL || fPrivate->runner != NULL)
		return;
	BMessage poll(kMsgPoll);
	fPrivate->runner = new(std::nothrow) BMessageRunner(BMessenger(this),
		&poll, kPollInterval);
	_Poll();
}


void
BGameControllerView::_StopPolling()
{
	if (fPrivate == NULL)
		return;
	delete fPrivate->runner;
	fPrivate->runner = NULL;
}


bool
BGameControllerView::_Poll()
{
	BGameController* controller = fPrivate != NULL ? fPrivate->controller
		: NULL;
	if (controller == NULL || controller->Update() != B_OK)
		return false;

	picture_state now;
	memset(&now, 0, sizeof(now));
	now.buttons = controller->ButtonValues();
	for (int32 i = 0; i < B_CONTROLLER_AXIS_COUNT; i++)
		now.axes[i] = controller->AxisValue((game_controller_axis)i);
	now.rawAxisCount = std::min((int32)8, controller->CountRawAxes());
	for (int32 i = 0; i < now.rawAxisCount; i++)
		now.rawAxes[i] = controller->RawAxisValue(i);
	now.rawButtonCount = std::min((int32)32, controller->CountRawButtons());
	for (int32 i = 0; i < now.rawButtonCount; i++) {
		if (controller->IsRawButtonDown(i))
			now.rawButtons |= (uint32)1 << i;
	}
	now.rawHatCount = controller->CountRawHats();
	now.rawHat = now.rawHatCount > 0 ? controller->RawHatValue(0) : 0;
	now.battery = fPrivate->state.battery;

	if (memcmp(&now, &fPrivate->state, sizeof(now)) == 0)
		return false;
	fPrivate->state = now;
	return true;
}


void
BGameControllerView::Draw(BRect updateRect)
{
	(void)updateRect;
	if (fPrivate == NULL)
		return;

	BGameController* controller = fPrivate->controller;
	bool live = controller != NULL;
	game_controller_family family = fPrivate->family;
	const picture_state& s = fPrivate->state;

	painter p;
	p.view = this;
	BRect box = live ? kLiveBox : kShapeBox;
	BRect bounds = Bounds();
	p.scale = std::min(bounds.Width() / box.Width(),
		bounds.Height() / box.Height());
	p.origin.x = bounds.left + (bounds.Width() - box.Width() * p.scale) / 2
		- box.left * p.scale;
	p.origin.y = bounds.top + (bounds.Height() - box.Height() * p.scale) / 2
		- box.top * p.scale;

	rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
	p.background = ViewColor();
	p.line = mix_color(document, text, 190);
	p.body = mix_color(document, text, 8);
	p.key = document;
	p.lit = ui_color(B_STATUS_BAR_COLOR);
	p.litLine = tint_color(p.lit, B_DARKEN_2_TINT);
	p.quiet = mix_color(document, text, 140);
	p.onLit = make_color(255, 255, 255);

	SetDrawingMode(B_OP_OVER);
	SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	SetHighColor(p.background);
	FillRect(bounds);

	game_controller_labels labels = fPrivate->labels;
	if (labels == B_GAME_CONTROLLER_LABELS_AUTOMATIC)
		labels = live ? controller->LabelStyle()
			: (family == B_GAME_CONTROLLER_FAMILY_PLAYSTATION
				? B_GAME_CONTROLLER_LABELS_PLAYSTATION
				: B_GAME_CONTROLLER_LABELS_XBOX);
	bool playstation = labels == B_GAME_CONTROLLER_LABELS_PLAYSTATION;
	bool nintendo = family == B_GAME_CONTROLLER_FAMILY_NINTENDO;

	uint32 b = s.buttons;
#define DOWN(button) (live && (b & ((uint32)1 << (button))) != 0)
	uint8 dpadMask = 0;
	if (DOWN(B_CONTROLLER_BUTTON_DPAD_UP))
		dpadMask |= 1;
	if (DOWN(B_CONTROLLER_BUTTON_DPAD_RIGHT))
		dpadMask |= 2;
	if (DOWN(B_CONTROLLER_BUTTON_DPAD_DOWN))
		dpadMask |= 4;
	if (DOWN(B_CONTROLLER_BUTTON_DPAD_LEFT))
		dpadMask |= 8;

	float faceRadius = live ? 12 : 10;

	if (family == B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS) {
		if (live && controller->HasAxis(B_CONTROLLER_AXIS_LEFT_TRIGGER)) {
			shoulder(p, 64, 30, "L",
				s.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER] > 16384);
		}
		if (live && controller->HasAxis(B_CONTROLLER_AXIS_RIGHT_TRIGGER)) {
			shoulder(p, 280, 30, "R",
				s.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER] > 16384);
		}
		genesis_body(p);
		dpad(p, 92, 124, 30, 24, 8, dpadMask);
		// X Y Z over A B C. C and Z are the shoulders of the standard
		// layout, where the database puts them.
		static const face_label kX = { "X", (r5_glyph)0 };
		static const face_label kY = { "Y", (r5_glyph)0 };
		static const face_label kZ = { "Z", (r5_glyph)0 };
		static const face_label kA = { "A", (r5_glyph)0 };
		static const face_label kB = { "B", (r5_glyph)0 };
		static const face_label kC = { "C", (r5_glyph)0 };
		float r = live ? 12 : 11;
		key(p, 266, 104, r, DOWN(B_CONTROLLER_BUTTON_X), live ? &kX : NULL,
			13);
		key(p, 300, 96, r, DOWN(B_CONTROLLER_BUTTON_Y), live ? &kY : NULL,
			13);
		key(p, 334, 88, r, DOWN(B_CONTROLLER_BUTTON_LEFT_SHOULDER),
			live ? &kZ : NULL, 13);
		key(p, 266, 140, r, DOWN(B_CONTROLLER_BUTTON_A), live ? &kA : NULL,
			13);
		key(p, 300, 132, r, DOWN(B_CONTROLLER_BUTTON_B), live ? &kB : NULL,
			13);
		key(p, 334, 124, r, DOWN(B_CONTROLLER_BUTTON_RIGHT_SHOULDER),
			live ? &kC : NULL, 13);
		pill(p, 186, 120, 28, 10, DOWN(B_CONTROLLER_BUTTON_START));
		return;
	}

	if (family == B_GAME_CONTROLLER_FAMILY_UNKNOWN) {
		box_body(p);
		// Built from what the controller reports: a hat as a d-pad, two
		// pairs of axes as sticks, the other axes as bars, the buttons as
		// numbered keys.
		int32 axes = live ? s.rawAxisCount : 4;
		int32 hats = live ? s.rawHatCount : 1;
		int32 buttons = live ? s.rawButtonCount : 4;
		if (hats > 0) {
			uint8 mask = 0;
			if (live) {
				static const uint8 kMasks[9] = { 0, 1, 3, 2, 6, 4, 12, 8, 9 };
				mask = s.rawHat <= 8 ? kMasks[s.rawHat] : 0;
			}
			dpad(p, 82, 90, 22, 18, 6, mask);
		}
		int32 sticks = std::min((int32)2, axes / 2);
		for (int32 i = 0; i < sticks; i++) {
			float cx = sticks == 1 ? 200 : (i == 0 ? 110 : 290);
			stick(p, cx, 150, 24, live ? s.rawAxes[i * 2] : 0,
				live ? s.rawAxes[i * 2 + 1] : 0, false, true);
		}
		int32 bars = std::min((int32)4, axes - sticks * 2);
		for (int32 i = 0; i < bars; i++) {
			BRect r = p.MapRect(130, 68 + i * 18, 84, 9);
			if (live) {
				int16 value = s.rawAxes[sticks * 2 + i];
				BRect fill = r;
				fill.right = fill.left
					+ r.Width() * (value + 32768) / 65535.0f;
				SetHighColor(p.lit);
				FillRect(fill);
			}
			SetHighColor(p.line);
			SetPenSize(std::max(1.0f, p.Size(1.6f)));
			StrokeRoundRect(r, p.Size(2), p.Size(2));
		}
		int32 shown = std::min((int32)24, buttons);
		int32 columns = shown > 15 ? 6 : 5;
		float spacing = shown > 15 ? 22 : 28;
		float radius = shown > 15 ? 8 : 9;
		for (int32 i = 0; i < shown; i++) {
			int32 row = i / columns;
			int32 column = i % columns;
			float cx = 244 + column * spacing;
			float cy = 76 + row * (spacing - 2);
			char number[8];
			snprintf(number, sizeof(number), "%d", (int)i + 1);
			face_label label = { number, (r5_glyph)0 };
			key(p, cx, cy, radius,
				live && (s.rawButtons & ((uint32)1 << i)) != 0,
				live ? &label : NULL, 10);
		}
		return;
	}

	// The grip shapes: PlayStation, Xbox and Nintendo.
	if (live) {
		const char* names[4];
		if (playstation) {
			names[0] = "L1"; names[1] = "R1"; names[2] = "L2"; names[3] = "R2";
		} else if (nintendo) {
			names[0] = "L"; names[1] = "R"; names[2] = "ZL"; names[3] = "ZR";
		} else {
			names[0] = "LB"; names[1] = "RB"; names[2] = "LT"; names[3] = "RT";
		}
		if (controller->HasAxis(B_CONTROLLER_AXIS_LEFT_TRIGGER))
			trigger(p, -24, -20, names[2],
				s.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER]);
		if (controller->HasAxis(B_CONTROLLER_AXIS_RIGHT_TRIGGER))
			trigger(p, 354, -20, names[3],
				s.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER]);
		if (controller->HasButton(B_CONTROLLER_BUTTON_LEFT_SHOULDER))
			shoulder(p, 60, 22, names[0],
				DOWN(B_CONTROLLER_BUTTON_LEFT_SHOULDER));
		if (controller->HasButton(B_CONTROLLER_BUTTON_RIGHT_SHOULDER))
			shoulder(p, 284, 22, names[1],
				DOWN(B_CONTROLLER_BUTTON_RIGHT_SHOULDER));
	}
	grip_body(p);

	bool psShape = family == B_GAME_CONTROLLER_FAMILY_PLAYSTATION;
	if (psShape) {
		// Touchpad, d-pad high on the left, both sticks low.
		BRect pad = p.MapRect(160, 52, 80, 48);
		bool touch = DOWN(B_CONTROLLER_BUTTON_TOUCHPAD);
		if (touch) {
			SetHighColor(p.lit);
			FillRoundRect(pad, p.Size(6), p.Size(6));
		}
		SetHighColor(p.line);
		SetPenSize(std::max(1.0f, p.Size(2)));
		StrokeRoundRect(pad, p.Size(6), p.Size(6));
		dpad(p, 112, 89, 27, 21, 7, dpadMask);
		stick(p, 152, 140, 22, s.axes[B_CONTROLLER_AXIS_LEFT_X],
			s.axes[B_CONTROLLER_AXIS_LEFT_Y],
			DOWN(B_CONTROLLER_BUTTON_LEFT_STICK), live);
		stick(p, 248, 140, 22, s.axes[B_CONTROLLER_AXIS_RIGHT_X],
			s.axes[B_CONTROLLER_AXIS_RIGHT_Y],
			DOWN(B_CONTROLLER_BUTTON_RIGHT_STICK), live);
		if (live) {
			pill(p, 140, 54, 12, 8, DOWN(B_CONTROLLER_BUTTON_BACK));
			pill(p, 248, 54, 12, 8, DOWN(B_CONTROLLER_BUTTON_START));
			key(p, 200, 140, 6, DOWN(B_CONTROLLER_BUTTON_GUIDE), NULL, 0);
		}
	} else {
		// Xbox and Nintendo: the left stick high, the d-pad and the right
		// stick low.
		stick(p, 95, 88, 22, s.axes[B_CONTROLLER_AXIS_LEFT_X],
			s.axes[B_CONTROLLER_AXIS_LEFT_Y],
			DOWN(B_CONTROLLER_BUTTON_LEFT_STICK), live);
		dpad(p, 158.5f, 136.5f, 24.5f, 19.5f, 6.5f, dpadMask);
		stick(p, 248, 140, 22, s.axes[B_CONTROLLER_AXIS_RIGHT_X],
			s.axes[B_CONTROLLER_AXIS_RIGHT_Y],
			DOWN(B_CONTROLLER_BUTTON_RIGHT_STICK), live);
		if (live) {
			pill(p, 170, 98, 16, 8, DOWN(B_CONTROLLER_BUTTON_BACK));
			pill(p, 214, 98, 16, 8, DOWN(B_CONTROLLER_BUTTON_START));
		}
		key(p, 200, 70, 9, DOWN(B_CONTROLLER_BUTTON_GUIDE), NULL, 0);
	}

	// The face buttons by place: A low, B right, X left, Y high. The
	// Nintendo shape has A and B (and X and Y) the other way round.
	face_label south = { "A", (r5_glyph)0 };
	face_label east = { "B", (r5_glyph)0 };
	face_label west = { "X", (r5_glyph)0 };
	face_label north = { "Y", (r5_glyph)0 };
	if (playstation) {
		south.glyph = R5_GLYPH_CROSS;
		east.glyph = R5_GLYPH_CIRCLE;
		west.glyph = R5_GLYPH_SQUARE;
		north.glyph = R5_GLYPH_TRIANGLE;
	} else if (nintendo) {
		south.text = "B";
		east.text = "A";
		west.text = "Y";
		north.text = "X";
	}
	key(p, 315, 64, faceRadius, DOWN(B_CONTROLLER_BUTTON_Y),
		live ? &north : NULL, 13);
	key(p, 341, 90, faceRadius, DOWN(B_CONTROLLER_BUTTON_B),
		live ? &east : NULL, 13);
	key(p, 315, 116, faceRadius, DOWN(B_CONTROLLER_BUTTON_A),
		live ? &south : NULL, 13);
	key(p, 289, 90, faceRadius, DOWN(B_CONTROLLER_BUTTON_X),
		live ? &west : NULL, 13);
#undef DOWN
}


void BGameControllerView::_ReservedGameControllerView1() {}
void BGameControllerView::_ReservedGameControllerView2() {}
void BGameControllerView::_ReservedGameControllerView3() {}
void BGameControllerView::_ReservedGameControllerView4() {}
