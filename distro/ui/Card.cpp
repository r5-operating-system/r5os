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


#include "Card.h"

#include "R5ModernStyle.h"

#include <InterfaceDefs.h>
#include <Layout.h>
#include <Message.h>
#include <TextView.h>

#include <math.h>
#include <new>


namespace BAppearanceKit {


struct BCard::Data {
};


static const char* kCardClassName = "BAppearanceKit::BCard";


static rgb_color
card_fill(card_style style)
{
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	if (style == B_CARD_PANEL) {
		return tint_color(panel, B_DARKEN_1_TINT);
	}
	if (!panel.IsLight())
		return mix_color(panel, make_color(255, 255, 255), 14);
	return ui_color(B_DOCUMENT_BACKGROUND_COLOR);
}


// The view color is the card's own face, so a child that adopts its
// parent's colors (BStringView does) gets the card face, not gray. The
// B_TRANSPARENT_BACKGROUND flag keeps the rounded corners showing the
// parent.
static void
apply_card_view_color(BView* view, card_style style)
{
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	if (style == B_CARD_PANEL) {
		view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
		view->SetLowUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	} else if (!panel.IsLight()) {
		rgb_color elevated = mix_color(panel, make_color(255, 255, 255), 14);
		view->SetViewColor(elevated);
		view->SetLowColor(elevated);
	} else {
		view->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		view->SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	}
}


struct card_colors {
	color_which	view;
	float		viewTint;
	rgb_color	viewColor;
	color_which	low;
	float		lowTint;
	rgb_color	lowColor;
};


static void
get_card_colors(BView* view, card_colors& colors)
{
	colors.view = view->ViewUIColor(&colors.viewTint);
	colors.viewColor = view->ViewColor();
	colors.low = view->LowUIColor(&colors.lowTint);
	colors.lowColor = view->LowColor();
}


static bool
same_color(color_which which, float tint, rgb_color color,
	color_which oldWhich, float oldTint, rgb_color oldColor)
{
	if (which != B_NO_COLOR || oldWhich != B_NO_COLOR)
		return which == oldWhich && tint == oldTint;
	return color == oldColor;
}


// A child that took the card's colors when it was attached (BStringView
// adopts its parent's) kept the old face after a change of theme: on the
// dark theme the card face is a fixed color, so labels stayed dark blocks
// on a light card (Users and Accounts, 25 Nov 2026). Give such children,
// and theirs, the card's new face. A child with colors of its own, and a
// card inside the card, are left alone; so is a text field's own text
// view, which keeps the document color.
static void
follow_card_colors(BView* card, BView* parent, const card_colors& before)
{
	card_colors now;
	get_card_colors(card, now);
	for (int32 i = 0; BView* child = parent->ChildAt(i); i++) {
		if (dynamic_cast<BCard*>(child) != NULL
			|| dynamic_cast<BTextView*>(child) != NULL) {
			continue;
		}
		card_colors old;
		get_card_colors(child, old);
		bool changed = false;
		if (same_color(old.view, old.viewTint, old.viewColor, before.view,
				before.viewTint, before.viewColor)) {
			if (now.view != B_NO_COLOR)
				child->SetViewUIColor(now.view, now.viewTint);
			else
				child->SetViewColor(now.viewColor);
			changed = true;
		}
		if (same_color(old.low, old.lowTint, old.lowColor, before.low,
				before.lowTint, before.lowColor)) {
			if (now.low != B_NO_COLOR)
				child->SetLowUIColor(now.low, now.lowTint);
			else
				child->SetLowColor(now.lowColor);
			changed = true;
		}
		if (changed)
			child->Invalidate();
		follow_card_colors(card, child, before);
	}
}


static rgb_color
card_border()
{
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	if (!panel.IsLight())
		return mix_color(panel, make_color(255, 255, 255), 34);
	return tint_color(panel, B_DARKEN_2_TINT);
}


BCard::BCard(const char* name, card_style style, orientation orientation)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE
		| B_TRANSPARENT_BACKGROUND,
		new BGroupLayout(orientation, B_USE_DEFAULT_SPACING)),
	fStyle(style),
	fData(NULL)
{
	_Init();
}


BCard::BCard(BMessage* archive)
	:
	BView(archive),
	fStyle(B_CARD_DOCUMENT),
	fData(NULL)
{
	int32 style = B_CARD_DOCUMENT;
	if (archive != NULL && archive->FindInt32("r5:style", &style) == B_OK) {
		if (style == B_CARD_PANEL)
			fStyle = B_CARD_PANEL;
	}
	_Init();
}


BCard::~BCard()
{
	delete fData;
}


BArchivable*
BCard::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kCardClassName))
		return NULL;
	return new BCard(archive);
}


status_t
BCard::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kCardClassName) != B_OK)
		status = archive->AddString("class", kCardClassName);
	if (status == B_OK)
		status = archive->AddInt32("r5:style", (int32)fStyle);
	return status;
}


void
BCard::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	rgb_color backdrop = ui_color(B_PANEL_BACKGROUND_COLOR);
	if (Parent() != NULL) {
		rgb_color parentColor = Parent()->ViewColor();
		if (parentColor.alpha > 0)
			backdrop = parentColor;
	}
	SetHighColor(backdrop);
	FillRect(bounds);
	// R5 Modern: the mockup's round card (8 at 12 points); R5 and Classic
	// are square (0 px)
	float radius = r5ui_modern_controls() ? roundf(be_plain_font->Size() * 8 / 12)
		: 0.0f;
	SetHighColor(card_fill(fStyle));
	FillRoundRect(bounds, radius, radius);
	SetHighColor(card_border());
	StrokeRoundRect(bounds, radius, radius);
}


void
BCard::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED) {
		card_colors before;
		get_card_colors(this, before);
		apply_card_view_color(this, fStyle);
		follow_card_colors(this, this, before);
		Invalidate();
	}
	BView::MessageReceived(message);
}


// A child that was given the document background because that is the card
// face on the light theme showed as a darker band on the dark theme, where
// the face is a color of its own (Sign in, Users and Accounts, 25 Nov 2026).
// When the card and its children are attached, such children take the
// card's face, as they do on a change of theme.
void
BCard::AllAttached()
{
	BView::AllAttached();
	if (fStyle != B_CARD_DOCUMENT
		|| ViewUIColor() == B_DOCUMENT_BACKGROUND_COLOR) {
		return;
	}
	card_colors document;
	document.view = B_DOCUMENT_BACKGROUND_COLOR;
	document.viewTint = B_NO_TINT;
	document.viewColor = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	document.low = B_DOCUMENT_BACKGROUND_COLOR;
	document.lowTint = B_NO_TINT;
	document.lowColor = document.viewColor;
	follow_card_colors(this, this, document);
}


void
BCard::SetStyle(card_style style)
{
	if (fStyle == style)
		return;
	fStyle = style;
	apply_card_view_color(this, fStyle);
	Invalidate();
}


card_style
BCard::Style() const
{
	return fStyle;
}


void
BCard::_Init()
{
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	apply_card_view_color(this, fStyle);
	BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout());
	if (layout != NULL)
		layout->SetInsets(B_USE_BIG_SPACING);
}


void
BCard::_ReservedCard1()
{
}


void
BCard::_ReservedCard2()
{
}


void
BCard::_ReservedCard3()
{
}


void
BCard::_ReservedCard4()
{
}


}	// namespace BAppearanceKit
