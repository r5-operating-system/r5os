/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include "DeskbarStyle.h"

#include <math.h>
#include <stdlib.h>

#include <algorithm>

#include <Application.h>
#include <Bitmap.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <View.h>

#include <R5Icons.h>
#include <R5UI.h>
#include <WindowEffects.h>

#include "BarApp.h"
#include "BarSettings.h"

#include "r5_pebble.inc"


namespace R5Deskbar {


static bool sEffectsActive = false;
static uint32 sEffects = 0;
static bool sEffectsKnown = false;
static bool sUnfolded = false;
static team_id sFrontTeam = -1;


static desk_settings*
settings()
{
	TBarApp* app = dynamic_cast<TBarApp*>(be_app);
	return app != NULL ? app->Settings() : NULL;
}


bool
Modern()
{
	desk_settings* current = settings();
	return current != NULL && current->style == kStyleR5;
}


int32
Layout()
{
	desk_settings* current = settings();
	if (current == NULL)
		return kLayoutVertical;
	if (current->style == kStyleR5 && current->layout == kLayoutCompact)
		return kLayoutCompact;
	return current->vertical ? kLayoutVertical : kLayoutHorizontal;
}


bool
Compact()
{
	return Modern() && Layout() == kLayoutCompact;
}


bool
Rounded()
{
	desk_settings* current = settings();
	return current != NULL && current->rounded;
}


bool
Floating()
{
	desk_settings* current = settings();
	return Modern() && current != NULL && current->floating;
}


bool
Unfolded()
{
	return sUnfolded;
}


void
SetUnfolded(bool unfolded)
{
	sUnfolded = unfolded;
}


void
UpdateEffects()
{
	sEffectsActive = BAppearanceKit::window_effects_active();
	sEffects = BAppearanceKit::window_effects();
	sEffectsKnown = true;
}


bool
EffectsActive()
{
	if (!sEffectsKnown)
		UpdateEffects();
	return sEffectsActive;
}


bool
AnimationsAllowed()
{
	return EffectsActive()
		&& (sEffects & BAppearanceKit::B_WINDOW_ANIMATIONS) != 0;
}


bool
WatchWindows(bool vertical)
{
	return vertical || (Modern() && Layout() == kLayoutHorizontal);
}


bool
FlatCorner()
{
	return Modern() && Layout() == kLayoutVertical && !Floating();
}


bool
Glass()
{
	// frosted glass on in Appearance (external review, 23 Nov 2026)
	return Modern() && EffectsActive()
		&& (sEffects & BAppearanceKit::B_WINDOW_FROSTED_GLASS) != 0
		&& !FlatCorner();
}


bool
Dark()
{
	// a theme in dark mode sets the dark ui_color set (docs/design/
	// THEMING.md); ui_color() reads the shared colours, so this follows a
	// change at once
	return Modern() && ui_color(B_PANEL_BACKGROUND_COLOR).IsDark();
}


float
Scale()
{
	float size = be_plain_font != NULL ? be_plain_font->Size() : 12.0f;
	return std::max(size, 12.0f) / 12.0f;
}


float
Size(float pixels)
{
	return floorf(pixels * Scale() + 0.5f);
}


float
Margin()
{
	return Floating() ? r5_floating_margin() : 0.0f;
}


float
WindowRadius()
{
	return Modern() && Rounded() && Floating() ? Size(18) : 0.0f;
}


float
ButtonRadius()
{
	return Rounded() ? Size(18) : 0.0f;
}


float
HighlightRadius()
{
	return Rounded() ? Size(10) : 0.0f;
}


float
RowRadius()
{
	return Rounded() ? Size(9) : 0.0f;
}


rgb_color
SurfaceColor()
{
	// rgba(240,245,252) under the glass of the effect step; opaque
	// #eef1f5 in the corner (board A3) and against an edge without GPU,
	// #eef2f8 floating without GPU (the brief, "Without GPU").
	// Dark (mockup B1/B2): #161a22 under the glass, opaque #1c2029.
	if (Dark())
		return Glass() ? make_color(0x16, 0x1a, 0x22)
			: make_color(0x1c, 0x20, 0x29);
	if (Glass())
		return make_color(240, 245, 252);
	if (FlatCorner() || !Floating())
		return make_color(0xee, 0xf1, 0xf5);
	return make_color(0xee, 0xf2, 0xf8);
}


rgb_color
TextColor()
{
	if (Dark())
		return make_color(0xe6, 0xe9, 0xef);
	return make_color(0x1d, 0x2a, 0x40);
}


rgb_color
LabelColor()
{
	if (Dark())
		return make_color(0xe6, 0xe9, 0xef);
	return make_color(0x16, 0x23, 0x3a);
}


rgb_color
CountColor()
{
	if (Dark())
		return make_color(0xa3, 0xab, 0xb8);
	return make_color(0x4d, 0x5b, 0x70);
}


rgb_color
FrontColor()
{
	// the theme's accent (#0084ff, dark #1f85f5 with the default Blue;
	// external review 2.2: Teal, Purple and the others follow)
	return ui_color(B_CONTROL_HIGHLIGHT_COLOR);
}


rgb_color
EdgeColor()
{
	// floating: rgba(255,255,255,.75) on the glass, #9fb0c8 without GPU
	// (opaque: white at .75 over the step's glass, as the browser draws
	// the border over the background)
	// dark: rgba(255,255,255,.08) over #161a22 on the glass, #3a404c
	// without GPU (mockup B1/B2)
	if (Dark()) {
		// on the glass a light hairline, white at 10 % (as the R5 Modern
		// decorator's dark glass edge; external review 1.5): drawn with
		// B_OP_ALPHA, and the compositor's edge line keeps the alpha
		if (Glass())
			return make_color(255, 255, 255, 26);
		return make_color(0x3a, 0x40, 0x4c);
	}
	if (Glass())
		return make_color(238, 243, 251);
	return make_color(0x9f, 0xb0, 0xc8);
}


rgb_color
EdgeLineColor()
{
	// against an edge: the line on the sides that face the desktop
	if (Dark())
		return make_color(0x3a, 0x40, 0x4c);
	return make_color(0x8c, 0x97, 0xa8);
}


rgb_color
ShadeColor(uint8 alpha)
{
	// dark: white, as the front row's rgba(255,255,255,.07) (mockup B1)
	if (Dark())
		return make_color(255, 255, 255, alpha);
	return make_color(30, 45, 70, alpha);
}


rgb_color
RowTextColor()
{
	if (Dark())
		return make_color(0xc9, 0xcf, 0xd9);
	return LabelColor();
}


void
SetFrontTeam(team_id team)
{
	sFrontTeam = team;
}


team_id
FrontTeam()
{
	return sFrontTeam;
}


static void
fill_round(BView* view, BRect rect, float radius)
{
	if (radius <= 0)
		view->FillRect(rect);
	else {
		radius = std::min(radius, std::min(rect.Width(), rect.Height()) / 2);
		view->FillRoundRect(rect, radius, radius);
	}
}


void
FillShade(BView* view, BRect rect, float radius, uint8 alpha)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(ShadeColor(alpha));
	fill_round(view, rect, radius);
	view->PopState();
}


void
DrawHover(BView* view, BRect rect, float radius)
{
	// rgba(30,45,70,.07) over the surface; on glass the shade's pixels
	// keep the step's see-through, so it is stronger there to look the same
	// (compared at 100 % with the mockup, 23 Nov 2026)
	FillShade(view, rect, radius, Glass() ? 26 : 18);
}


void
DrawHollow(BView* view, BRect rect, float radius)
{
	// rgba(30,45,70,.12), the inset shadow 0 2px 4px rgba(0,20,60,.28) and
	// an inner line rgba(30,45,70,.10) (board D, option E)
	FillShade(view, rect, radius, Glass() ? 44 : 31);
	// The inset shadow as rings inside the edge, darker at the top: each
	// ring a whole round rectangle (arcs of single pixels left a dotted
	// line, 23 Nov 2026), and the upper rings once more a pixel lower.
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	static const uint8 kRing[] = { 34, 18, 8 };
	for (int32 i = 0; i < 3; i++) {
		BRect ring = rect.InsetByCopy(i, i);
		float r = std::max(0.0f, std::min(radius - i,
			std::min(ring.Width(), ring.Height()) / 2));
		view->SetHighColor(Dark() ? make_color(0, 0, 0, kRing[i] * 2)
			: make_color(0, 20, 60, kRing[i]));
		if (r <= 0)
			view->StrokeRect(ring);
		else
			view->StrokeRoundRect(ring, r, r);
	}
	// the offset of 2: a soft band under the top edge, inside the corners
	float inner = std::max(radius, 0.0f) * 0.6f;
	static const uint8 kTop[] = { 30, 16 };
	for (int32 i = 0; i < 2; i++) {
		view->SetHighColor(Dark() ? make_color(0, 0, 0, kTop[i] * 2)
			: make_color(0, 20, 60, kTop[i]));
		view->StrokeLine(BPoint(rect.left + inner, rect.top + 1 + i),
			BPoint(rect.right - inner, rect.top + 1 + i));
	}
	view->PopState();
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(ShadeColor(26));
	if (radius <= 0)
		view->StrokeRect(rect);
	else {
		float r = std::min(radius, std::min(rect.Width(), rect.Height()) / 2);
		view->StrokeRoundRect(rect.InsetByCopy(0.5f, 0.5f), r - 0.5f,
			r - 0.5f);
	}
	view->PopState();
}


void
DrawFrontLine(BView* view, BRect line)
{
	view->PushState();
	view->SetDrawingMode(B_OP_OVER);
	view->SetHighColor(FrontColor());
	float radius = Rounded() ? std::min(line.Width(), line.Height()) / 2 : 0;
	fill_round(view, line, radius);
	view->PopState();
}


void
DrawSeparator(BView* view, float x, float top, float bottom)
{
	// 1 px, rgba(30,45,70,.22)
	DrawLine(view, BPoint(x, top), BPoint(x, bottom),
		Dark() ? make_color(255, 255, 255, 20) : make_color(30, 45, 70, 56));
}


void
DrawLine(BView* view, BPoint from, BPoint to, rgb_color color)
{
	view->PushState();
	if (color.alpha < 255) {
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	} else
		view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(color);
	view->StrokeLine(from, to);
	view->PopState();
}


void
DrawIcon(BView* view, const BBitmap* icon, BRect frame)
{
	if (icon == NULL)
		return;
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	// composite: an icon stays opaque on a glass view, and its edges keep
	// their coverage (docs/LIBR5.md, "Window effects")
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	BRect bounds = icon->Bounds();
	if (bounds.Width() == frame.Width() && bounds.Height() == frame.Height())
		view->DrawBitmap(icon, frame.LeftTop());
	else
		view->DrawBitmap(icon, bounds, frame, B_FILTER_BITMAP_BILINEAR);
	view->PopState();
}


void
DrawChevron(BView* view, BRect frame, bool up)
{
	// M6 9l6 6 6-6 on a 24 grid, 2.4 wide, round ends (board B)
	float side = std::min(frame.Width(), frame.Height());
	float scale = side / 24.0f;
	float left = frame.left + (frame.Width() - side) / 2;
	float top = frame.top + (frame.Height() - side) / 2;
	BPoint points[3];
	if (up) {
		points[0] = BPoint(left + 6 * scale, top + 15 * scale);
		points[1] = BPoint(left + 12 * scale, top + 9 * scale);
		points[2] = BPoint(left + 18 * scale, top + 15 * scale);
	} else {
		points[0] = BPoint(left + 6 * scale, top + 9 * scale);
		points[1] = BPoint(left + 12 * scale, top + 15 * scale);
		points[2] = BPoint(left + 18 * scale, top + 9 * scale);
	}
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(TextColor());
	view->SetPenSize(std::max(1.0f, 2.4f * scale));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	view->StrokePolygon(points, 3, false);
	view->PopState();
}


const BBitmap*
Pebble(float side)
{
	static BBitmap* sPebble = NULL;
	static float sSide = 0;
	if (sPebble == NULL || sSide != side) {
		delete sPebble;
		sPebble = r5_vector_icon_bitmap(kR5Pebble, sizeof(kR5Pebble), side);
		sSide = side;
	}
	return sPebble;
}


static float
bezier(float a, float b, float t)
{
	// one coordinate of a cubic from 0 over a and b to 1
	float u = 1 - t;
	return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
}


float
Ease(float progress)
{
	if (progress <= 0)
		return 0;
	if (progress >= 1)
		return 1;
	// cubic-bezier(.2, .9, .3, 1.25): find t for x, then take y
	float low = 0;
	float high = 1;
	float t = progress;
	for (int32 i = 0; i < 24; i++) {
		t = (low + high) / 2;
		if (bezier(0.2f, 0.3f, t) < progress)
			low = t;
		else
			high = t;
	}
	return bezier(0.9f, 1.25f, t);
}


}	// namespace R5Deskbar
