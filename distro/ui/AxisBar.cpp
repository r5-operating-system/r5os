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


#include "AxisBar.h"

#include <Font.h>
#include <InterfaceDefs.h>
#include <LayoutUtils.h>
#include <Message.h>

#include <algorithm>
#include <math.h>
#include <new>


namespace BAppearanceKit {


static const char* kAxisBarClassName = "BAppearanceKit::BAxisBar";


static float
clamp_value(float value)
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


BAxisBar::BAxisBar(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fValue(0),
	fEnabled(true)
{
	_Init();
}


BAxisBar::BAxisBar(BMessage* archive)
	:
	BView(archive),
	fValue(0),
	fEnabled(true)
{
	_Init();
	bool enabled;
	if (archive != NULL && archive->FindBool("r5:enabled", &enabled) == B_OK)
		fEnabled = enabled;
	float value;
	if (archive != NULL && archive->FindFloat("r5:value", &value) == B_OK)
		fValue = clamp_value(value);
}


BAxisBar::~BAxisBar()
{
}


BArchivable*
BAxisBar::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kAxisBarClassName))
		return NULL;
	return new(std::nothrow) BAxisBar(archive);
}


status_t
BAxisBar::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kAxisBarClassName) != B_OK)
		status = archive->AddString("class", kAxisBarClassName);
	if (status == B_OK)
		status = archive->AddFloat("r5:value", fValue);
	if (status == B_OK && !fEnabled)
		status = archive->AddBool("r5:enabled", false);
	return status;
}


void
BAxisBar::SetValue(float value)
{
	value = clamp_value(value);
	if (value == fValue)
		return;
	fValue = value;
	Invalidate();
}


float
BAxisBar::Value() const
{
	return fValue;
}


void
BAxisBar::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();
}


void
BAxisBar::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	float unit = font_unit();
	float height = floorf(std::min(bounds.Height(), unit * 0.8f));
	if (height < 4 || bounds.Width() < height * 2)
		return;

	BRect track(bounds.left, floorf(bounds.top
			+ (bounds.Height() - height) / 2),
		bounds.right, 0);
	track.bottom = track.top + height;
	float radius = height / 2;
	float middle = floorf((track.left + track.right) / 2);

	rgb_color background = LowColor();
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color outline = mix_color(background, text, fEnabled ? 200 : 90);
	rgb_color tick = mix_color(background, text, fEnabled ? 140 : 70);
	rgb_color fill = fEnabled ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
		: mix_color(background, text, 100);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	// The fill, from the middle to the value, inside the track's line.
	BRect inner = track.InsetByCopy(2, 2);
	float span = (inner.right - inner.left) / 2;
	float end = middle + fValue * span;
	if (fabsf(end - middle) >= 1) {
		BRect bar(std::min(middle, end), inner.top, std::max(middle, end),
			inner.bottom);
		float barRadius = std::min(inner.Height() / 2, bar.Width() / 2);
		SetHighColor(fill);
		FillRoundRect(bar, barRadius, barRadius);
	}

	// The tick at 0, over the fill, and the track.
	SetPenSize(1);
	SetHighColor(tick);
	StrokeLine(BPoint(middle + 0.5f, track.top - 2),
		BPoint(middle + 0.5f, track.bottom + 2));
	SetHighColor(outline);
	StrokeRoundRect(track, radius, radius);

	SetDrawingMode(B_OP_COPY);
}


void
BAxisBar::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	Invalidate();
}


BSize
BAxisBar::MinSize()
{
	float unit = font_unit();
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		BSize(ceilf(unit * 5), ceilf(unit)));
}


BSize
BAxisBar::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(B_SIZE_UNLIMITED, ceilf(font_unit())));
}


BSize
BAxisBar::PreferredSize()
{
	float unit = font_unit();
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		BSize(ceilf(unit * 14), ceilf(unit)));
}


void
BAxisBar::SetEnabled(bool enabled)
{
	if (enabled == fEnabled)
		return;
	fEnabled = enabled;
	Invalidate();
}


bool
BAxisBar::IsEnabled() const
{
	return fEnabled;
}


void
BAxisBar::_Init()
{
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
}


void BAxisBar::_ReservedAxisBar1() {}
void BAxisBar::_ReservedAxisBar2() {}
void BAxisBar::_ReservedAxisBar3() {}
void BAxisBar::_ReservedAxisBar4() {}


}	// namespace BAppearanceKit
