/*
 * Copyright (c) 2001-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Stefano Ceccherini, burton666@libero.it
 *		Marc Flerackers, mflerackers@androme.be
 *		Bill Hayden, haydentech@users.sourceforge.net
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */


#include <SeparatorItem.h>

#include <Font.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <MenuPrivate.h>

#include <math.h>
// SPDX-SnippetEnd


BSeparatorItem::BSeparatorItem()
	:
	BMenuItem("", NULL)
{
	BMenuItem::SetEnabled(false);
}


BSeparatorItem::BSeparatorItem(BMessage* data)
	:
	BMenuItem(data)
{
	BMenuItem::SetEnabled(false);
}


BSeparatorItem::~BSeparatorItem()
{
}


status_t
BSeparatorItem::Archive(BMessage* data, bool deep) const
{
	return BMenuItem::Archive(data, deep);
}


BArchivable*
BSeparatorItem::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BSeparatorItem"))
		return new BSeparatorItem(data);

	return NULL;
}


void
BSeparatorItem::SetEnabled(bool enable)
{
	// Don't do anything - we don't want to get enabled ever
}


void
BSeparatorItem::GetContentSize(float* _width, float* _height)
{
	if (Menu() != NULL && Menu()->Layout() == B_ITEMS_IN_ROW) {
		if (_width != NULL)
			*_width = 2.0;

		if (_height != NULL)
			*_height = 2.0;
	} else {
		if (_width != NULL)
			*_width = 2.0;

		if (_height != NULL) {
			BFont font(be_plain_font);
			if (Menu() != NULL)
				Menu()->GetFont(&font);

			const float height = floorf((font.Size() * 0.8) / 2) * 2;
			*_height = max_c(4, height);
		}
	}
}


void
BSeparatorItem::Draw()
{
	BMenu *menu = Menu();
	if (menu == NULL)
		return;

	BRect bounds = Frame();
	rgb_color oldColor = menu->HighColor();
	rgb_color lowColor = menu->LowColor();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The R5 menu style: one thin line in the middle of the row, 8 px in
	// from each side of the row.
	if (BPrivate::MenuPrivate(menu).R5Style()) {
		float inset = roundf(8 * BPrivate::MenuPrivate::R5Scale());
		float middle = floorf((bounds.top + bounds.bottom) / 2);
		menu->SetHighColor(BPrivate::MenuPrivate::R5SeparatorColor(
			ui_color(B_MENU_BACKGROUND_COLOR)));
		menu->StrokeLine(BPoint(bounds.left + inset, middle),
			BPoint(bounds.right - inset, middle));
		menu->SetHighColor(oldColor);
		return;
	}
// SPDX-SnippetEnd

	if (menu->Layout() == B_ITEMS_IN_ROW) {
		const float startLeft = bounds.left + (floor(bounds.Width())) / 2;
		menu->SetHighColor(tint_color(lowColor, B_DARKEN_1_TINT));
		menu->StrokeLine(BPoint(startLeft, bounds.top + 1.0f),
			BPoint(startLeft, bounds.bottom - 1.0f));
		menu->SetHighColor(tint_color(lowColor, B_LIGHTEN_2_TINT));
		menu->StrokeLine(BPoint(startLeft + 1.0f, bounds.top + 1.0f),
			BPoint(startLeft + 1.0f, bounds.bottom - 1.0f));
	} else {
		const float startTop = bounds.top + (floor(bounds.Height())) / 2;
		menu->SetHighColor(tint_color(lowColor, B_DARKEN_1_TINT));
		menu->StrokeLine(BPoint(bounds.left + 1.0f, startTop),
			BPoint(bounds.right - 1.0f, startTop));
		menu->SetHighColor(tint_color(lowColor, B_LIGHTEN_2_TINT));
		menu->StrokeLine(BPoint(bounds.left + 1.0f, startTop + 1.0f),
			BPoint(bounds.right - 1.0f, startTop + 1.0f));
	}
	menu->SetHighColor(oldColor);
}


//	#pragma mark - private


void BSeparatorItem::_ReservedSeparatorItem1() {}
void BSeparatorItem::_ReservedSeparatorItem2() {}


BSeparatorItem &
BSeparatorItem::operator=(const BSeparatorItem &)
{
	return *this;
}
