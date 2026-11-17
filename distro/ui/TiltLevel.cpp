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


#include "TiltLevel.h"

#include <LayoutUtils.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>

#include <algorithm>
#include <math.h>
#include <new>


namespace BAppearanceKit {


static const char* kTiltLevelClassName = "BAppearanceKit::BTiltLevel";


static float
clamp_axis(float value)
{
	if (!(value > -1))
		return value != value ? 0 : -1;
	return value > 1 ? 1 : value;
}


static float
font_unit()
{
	font_height height;
	be_plain_font->GetHeight(&height);
	return ceilf(height.ascent + height.descent);
}


BTiltLevel::BTiltLevel(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fX(0),
	fY(0)
{
	_Init();
}


BTiltLevel::BTiltLevel(BMessage* archive)
	:
	BView(archive),
	fX(0),
	fY(0)
{
	_Init();
	float value;
	if (archive != NULL && archive->FindFloat("r5:x", &value) == B_OK)
		fX = clamp_axis(value);
	if (archive != NULL && archive->FindFloat("r5:y", &value) == B_OK)
		fY = clamp_axis(value);
}


BTiltLevel::~BTiltLevel()
{
}


BArchivable*
BTiltLevel::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kTiltLevelClassName))
		return NULL;
	return new(std::nothrow) BTiltLevel(archive);
}


status_t
BTiltLevel::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kTiltLevelClassName) != B_OK)
		status = archive->AddString("class", kTiltLevelClassName);
	if (status == B_OK)
		status = archive->AddFloat("r5:x", fX);
	if (status == B_OK)
		status = archive->AddFloat("r5:y", fY);
	return status;
}


void
BTiltLevel::SetValue(float x, float y)
{
	x = clamp_axis(x);
	y = clamp_axis(y);
	if (x == fX && y == fY)
		return;
	fX = x;
	fY = y;
	Invalidate();
}


float
BTiltLevel::X() const
{
	return fX;
}


float
BTiltLevel::Y() const
{
	return fY;
}


void
BTiltLevel::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();
}


void
BTiltLevel::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	float unit = font_unit();
	float line = floorf(unit / 8);
	if (line < 1)
		line = 1;

	// The largest circle that fits, with room for its line.
	float side = floorf(std::min(bounds.Width(), bounds.Height())) - line;
	if (side < 4)
		return;
	BPoint center(floorf(bounds.left + bounds.Width() / 2) + 0.5f,
		floorf(bounds.top + bounds.Height() / 2) + 0.5f);
	float radius = side / 2;

	rgb_color background = LowColor();
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color outline = mix_color(background, text, 200);
	rgb_color guide = mix_color(background, text, 110);
	rgb_color bubble = ui_color(B_CONTROL_HIGHLIGHT_COLOR);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	// The cross and the centre ring.
	SetPenSize(1);
	SetHighColor(guide);
	StrokeLine(BPoint(center.x - radius, center.y),
		BPoint(center.x + radius, center.y));
	StrokeLine(BPoint(center.x, center.y - radius),
		BPoint(center.x, center.y + radius));
	float ring = radius * 0.3f;
	StrokeEllipse(center, ring, ring);

	// The bubble: inside the circle wherever the value points.
	float bubbleRadius = std::max(radius * 0.12f, unit / 3);
	float reach = radius - bubbleRadius - line;
	float dx = fX;
	float dy = fY;
	float length = sqrtf(dx * dx + dy * dy);
	if (length > 1) {
		dx /= length;
		dy /= length;
	}
	BPoint where(center.x + dx * reach, center.y + dy * reach);
	SetHighColor(bubble);
	FillEllipse(where, bubbleRadius, bubbleRadius);

	SetPenSize(line);
	SetHighColor(outline);
	StrokeEllipse(center, radius, radius);

	SetPenSize(1);
	SetDrawingMode(B_OP_COPY);
}


void
BTiltLevel::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	Invalidate();
}


BSize
BTiltLevel::MinSize()
{
	float side = ceilf(font_unit() * 5);
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), BSize(side, side));
}


BSize
BTiltLevel::MaxSize()
{
	float side = ceilf(font_unit() * 10);
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), BSize(side, side));
}


BSize
BTiltLevel::PreferredSize()
{
	float side = ceilf(font_unit() * 7.5f);
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		BSize(side, side));
}


void
BTiltLevel::_Init()
{
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
}


void BTiltLevel::_ReservedTiltLevel1() {}
void BTiltLevel::_ReservedTiltLevel2() {}
void BTiltLevel::_ReservedTiltLevel3() {}
void BTiltLevel::_ReservedTiltLevel4() {}


}	// namespace BAppearanceKit
