/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The controller picture of docs/GAME-CONTROLLERS.md. A known controller
 * gets the drawing of its model, chosen by vendor and product ID
 * (GameControllerPictures.h): the nine drawings of the Gamepad Line Art
 * Pack (CC0, by gidedin) and R5's own Genesis / M30 drawing, filled in the
 * text color, so dark mode follows. A pressed control fills blue under the
 * line, a moved stick shows a blue ring off centre, and the analog
 * triggers are small caps over the shoulders. A controller without a
 * mapping gets a picture built from what it reports. The drawings are
 * filled outlines: a stroked BShape is not drawn on every app_server path
 * (R5Icons.cpp), so the generator turns strokes into outlines.
 */


#include "GameControllerView.h"

#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <InterfaceDefs.h>
#include <LayoutUtils.h>
#include <Shape.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Window.h>

#include <R5Icons.h>

#include "GameControllerPictures.h"

#include <algorithm>
#include <vector>


static const uint32 kMsgPoll = 'gcvP';
static const bigtime_t kPollInterval = 16000;

// The picture's own coordinates for a controller without a mapping.
static const BRect kLiveBox(-30, -26, 430, 244);
static const BRect kShapeBox(0, 36, 400, 236);

// The drawings: the trigger caps and the band above the picture for them,
// as parts of the drawing's width (the approved mockup: 34 by 40 pixels on
// a picture 580 pixels wide). Below kBoldWidth pixels the line is drawn
// thicker, as the pack's bold drawings are.
static const float kCapWidth = 0.0586f;
static const float kCapHeight = 0.069f;
static const float kCapGap = 0.03f;
static const float kCapBand = 0.12f;
static const float kBoldWidth = 250;

using namespace R5GameController;


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
		float side = p.Size(radius * 1.45f);
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


// #pragma mark - the drawings


// A drawing in view coordinates: data units (a quarter of a viewBox unit)
// times scale, from origin.
struct model_painter {
	const gc_model*	model;
	BPoint			origin;
	float			scale;

	BPoint Map(float x, float y) const
	{
		return BPoint(origin.x + x * scale, origin.y + y * scale);
	}
};


// The outline of a control, in view coordinates.
bool
control_bounds(const model_painter& m, int32 button, BRect& bounds)
{
	const gc_model* model = m.model;
	for (int32 i = 0; i < model->controlCount; i++) {
		const gc_control& control = model->controls[i];
		if (control.button != button || control.count == 0)
			continue;
		const int16* points = model->controlPoints + control.first * 2;
		BPoint first = m.Map(points[0], points[1]);
		bounds.Set(first.x, first.y, first.x, first.y);
		for (int32 k = 1; k < control.count; k++) {
			BPoint point = m.Map(points[k * 2], points[k * 2 + 1]);
			bounds.left = std::min(bounds.left, point.x);
			bounds.right = std::max(bounds.right, point.x);
			bounds.top = std::min(bounds.top, point.y);
			bounds.bottom = std::max(bounds.bottom, point.y);
		}
		return true;
	}
	return false;
}


void
fill_control(BView* view, const model_painter& m, const gc_control& control)
{
	if (control.count < 3)
		return;
	std::vector<BPoint> points(control.count);
	const int16* data = m.model->controlPoints + control.first * 2;
	for (int32 k = 0; k < control.count; k++)
		points[k] = m.Map(data[k * 2], data[k * 2 + 1]);
	view->FillPolygon(&points[0], control.count);
}


// The drawing as one shape per path, from the drawing's corner at this
// scale; FillShape() draws a shape at the pen location.
void
build_art(const gc_model* model, float scale, std::vector<BShape*>& shapes)
{
	const int16* data = model->points;
	int32 run = 0;
	while (run < model->runCount) {
		int32 subpaths = model->runs[run++];
		BShape* shape = new(std::nothrow) BShape;
		for (int32 i = 0; i < subpaths && run < model->runCount; i++) {
			int32 count = model->runs[run++];
			for (int32 k = 0; k < count; k++, data += 2) {
				if (shape == NULL)
					continue;
				BPoint point(data[0] * scale, data[1] * scale);
				if (k == 0)
					shape->MoveTo(point);
				else
					shape->LineTo(point);
			}
			if (shape != NULL)
				shape->Close();
		}
		if (shape != NULL)
			shapes.push_back(shape);
	}
}


void
model_text(BView* view, const model_painter& m, const gc_text& text,
	rgb_color color)
{
	BFont font(be_bold_font);
	font.SetSize(std::max(5.0f, text.size * m.scale));
	view->SetFont(&font);
	font_height height;
	font.GetHeight(&height);
	BPoint center = m.Map(text.x, text.y);
	float width = font.StringWidth(text.text);
	view->SetHighColor(color);
	view->DrawString(text.text, BPoint(center.x - width / 2,
		center.y + (height.ascent - height.descent) / 2));
}


// A moved stick: a ring around the cap, moved the way the stick points.
void
stick_ring(BView* view, const model_painter& m, const gc_stick& stick,
	int16 x, int16 y, rgb_color color)
{
	if (stick.radius == 0 || (abs(x) <= 6000 && abs(y) <= 6000))
		return;
	BPoint center = m.Map(stick.x + stick.travel * x / 32768.0f,
		stick.y + stick.travel * y / 32768.0f);
	float radius = stick.radius * m.scale;
	view->SetHighColor(color);
	view->SetPenSize(std::max(1.5f, 0.006f * m.model->width * m.scale));
	view->StrokeEllipse(center, radius, radius);
}


// An analog trigger: a small cap over the shoulder, filled from the
// bottom by how far it is pulled, with "L2 60%" beside it.
void
trigger_cap(BView* view, const painter& p, const model_painter& m,
	BRect shoulder, const char* name, int16 value, bool left)
{
	float width = m.model->width * m.scale;
	float capWidth = kCapWidth * width;
	float capHeight = kCapHeight * width;
	float center = (shoulder.left + shoulder.right) / 2;
	float bottom = shoulder.top - kCapGap * width;
	BRect cap(center - capWidth / 2, bottom - capHeight,
		center + capWidth / 2, bottom);
	float radius = capWidth * 0.3f;

	view->SetHighColor(p.key);
	view->FillRoundRect(cap, radius, radius);
	float fraction = std::min(1.0f, std::max(0.0f, value / 32767.0f));
	if (fraction > 0) {
		BRect fill = cap;
		fill.top = cap.bottom - cap.Height() * fraction;
		view->PushState();
		view->ClipToRect(fill);
		view->SetHighColor(p.lit);
		view->FillRoundRect(cap, radius, radius);
		view->PopState();
	}
	view->SetHighColor(p.line);
	view->SetPenSize(std::max(1.5f, capWidth * 0.08f));
	view->StrokeRoundRect(cap, radius, radius);

	char text[32];
	snprintf(text, sizeof(text), "%s %d%%", name,
		(int)(fraction * 100 + 0.5f));
	BFont font(be_bold_font);
	font.SetSize(std::max(be_plain_font->Size() * 0.75f, capWidth * 0.33f));
	view->SetFont(&font);
	font_height height;
	font.GetHeight(&height);
	float textWidth = font.StringWidth(text);
	float gap = capWidth * 0.25f;
	float baseline = (cap.top + cap.bottom) / 2
		+ (height.ascent - height.descent) / 2;
	view->DrawString(text, BPoint(left ? cap.left - gap - textWidth
		: cap.right + gap, baseline));
}


}	// namespace


// #pragma mark - BGameControllerView


struct BGameControllerView::Private {
	BGameController*		controller;
	game_controller_family	family;
	game_controller_labels	labels;
	BMessageRunner*			runner;
	picture_state			state;
	// The drawing as shapes, for artModel at artScale.
	std::vector<BShape*>	art;
	const gc_model*			artModel;
	float					artScale;

	Private()
		:
		controller(NULL),
		family(B_GAME_CONTROLLER_FAMILY_XBOX),
		labels(B_GAME_CONTROLLER_LABELS_AUTOMATIC),
		runner(NULL),
		artModel(NULL),
		artScale(0)
	{
		memset(&state, 0, sizeof(state));
		state.battery = -1;
	}

	~Private()
	{
		ClearArt();
	}

	void ClearArt()
	{
		for (size_t i = 0; i < art.size(); i++)
			delete art[i];
		art.clear();
		artModel = NULL;
		artScale = 0;
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

	uint32 b = s.buttons;
#define DOWN(button) (live && (b & ((uint32)1 << (button))) != 0)

	const gc_model* model = live
		? PictureFor(controller->VendorID(), controller->ProductID(), family)
		: PictureForFamily(family);
	if (model != NULL) {
		// The drawing of the model, centred, with a band above it for the
		// trigger caps when the controller has analog triggers.
		bool leftTrigger = live && model->leftCap != NULL
			&& controller->HasAxis(B_CONTROLLER_AXIS_LEFT_TRIGGER);
		bool rightTrigger = live && model->rightCap != NULL
			&& controller->HasAxis(B_CONTROLLER_AXIS_RIGHT_TRIGGER);
		float width = model->width;
		float band = leftTrigger || rightTrigger ? kCapBand * width : 0;
		float height = model->height + band;
		model_painter m;
		m.model = model;
		m.scale = std::min(bounds.Width() / width, bounds.Height() / height);
		m.origin.x = roundf(bounds.left
			+ (bounds.Width() - width * m.scale) / 2);
		m.origin.y = roundf(bounds.top
			+ (bounds.Height() - height * m.scale) / 2 + band * m.scale);

		// What is pressed fills under the line.
		SetHighColor(p.lit);
		for (int32 i = 0; live && i < model->controlCount; i++) {
			const gc_control& control = model->controls[i];
			bool on;
			if (control.button == PICTURE_LEFT_TRIGGER) {
				on = controller->HasAxis(B_CONTROLLER_AXIS_LEFT_TRIGGER)
					&& s.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER] > 16384;
			} else if (control.button == PICTURE_RIGHT_TRIGGER) {
				on = controller->HasAxis(B_CONTROLLER_AXIS_RIGHT_TRIGGER)
					&& s.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER] > 16384;
			} else
				on = DOWN(control.button);
			if (on)
				fill_control(this, m, control);
		}

		// The line, in the text color; thicker when the picture is small.
		if (fPrivate->artModel != model || fPrivate->artScale != m.scale) {
			fPrivate->ClearArt();
			build_art(model, m.scale, fPrivate->art);
			fPrivate->artModel = model;
			fPrivate->artScale = m.scale;
		}
		int32 rule = FillRule();
		SetFillRule(B_EVEN_ODD);
		SetHighColor(p.line);
		static const BPoint kBold[] = {
			BPoint(0, 0), BPoint(0.45f, 0), BPoint(-0.45f, 0),
			BPoint(0, 0.45f), BPoint(0, -0.45f)
		};
		int32 passes = width * m.scale < kBoldWidth ? 5 : 1;
		for (int32 pass = 0; pass < passes; pass++) {
			MovePenTo(m.origin + kBold[pass]);
			for (size_t i = 0; i < fPrivate->art.size(); i++)
				FillShape(fPrivate->art[i]);
		}
		MovePenTo(B_ORIGIN);
		SetFillRule(rule);
		for (int32 i = 0; i < model->textCount; i++)
			model_text(this, m, model->texts[i], p.line);

		if (live) {
			stick_ring(this, m, model->leftStick,
				s.axes[B_CONTROLLER_AXIS_LEFT_X],
				s.axes[B_CONTROLLER_AXIS_LEFT_Y], p.lit);
			stick_ring(this, m, model->rightStick,
				s.axes[B_CONTROLLER_AXIS_RIGHT_X],
				s.axes[B_CONTROLLER_AXIS_RIGHT_Y], p.lit);
		}
		BRect shoulder;
		if (leftTrigger
			&& control_bounds(m, B_CONTROLLER_BUTTON_LEFT_SHOULDER, shoulder)) {
			trigger_cap(this, p, m, shoulder, model->leftCap,
				s.axes[B_CONTROLLER_AXIS_LEFT_TRIGGER], true);
		}
		if (rightTrigger
			&& control_bounds(m, B_CONTROLLER_BUTTON_RIGHT_SHOULDER,
				shoulder)) {
			trigger_cap(this, p, m, shoulder, model->rightCap,
				s.axes[B_CONTROLLER_AXIS_RIGHT_TRIGGER], false);
		}
		return;
	}

	// No mapping: a picture built from what the controller reports.
	{
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

#undef DOWN
}


void BGameControllerView::_ReservedGameControllerView1() {}
void BGameControllerView::_ReservedGameControllerView2() {}
void BGameControllerView::_ReservedGameControllerView3() {}
void BGameControllerView::_ReservedGameControllerView4() {}
