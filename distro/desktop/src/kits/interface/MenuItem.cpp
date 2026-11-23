/*
 * Copyright 2001-2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Marc Flerackers, mflerackers@androme.be
 *		Bill Hayden, haydentech@users.sourceforge.net
 *		Olivier Milla
 *		John Scipione, jscipione@gmail.com
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */


#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <Bitmap.h>
#include <ControlLook.h>
#include <MenuItem.h>
#include <Shape.h>
#include <String.h>
#include <Window.h>

#include <MenuPrivate.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <MenuBar.h>

#include <math.h>
// SPDX-SnippetEnd

#include "utf8_functions.h"


static const float kMarkTint = 0.75f;

// map control key shortcuts to drawable Unicode characters
// cf. http://unicode.org/charts/PDF/U2190.pdf
const char* kUTF8ControlMap[] = {
	NULL,
	"\xe2\x86\xb8", /* B_HOME U+21B8 */
	NULL, NULL,
	NULL, /* B_END */
	NULL, /* B_INSERT */
	NULL, NULL,
	"\xe2\x8c\xab", /* B_BACKSPACE U+232B */
	"\xe2\x86\xb9", /* B_TAB U+21B9 */
	"\xe2\x8f\x8e", /* B_ENTER, U+23CE */
	NULL, /* B_PAGE_UP */
	NULL, /* B_PAGE_DOWN */
	NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	"\xe2\x86\x90", /* B_LEFT_ARROW */
	"\xe2\x86\x92", /* B_RIGHT_ARROW */
	"\xe2\x86\x91", /* B_UP_ARROW */
	"\xe2\x86\x93", /* B_DOWN_ARROW */
	"\xe2\x90\xa3"  /* B_SPACE */
};

static const char* kDeleteShortcutUTF8 = "\xe2\x8c\xa6"; /* B_DELETE U+2326 */


using BPrivate::MenuPrivate;


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	The R5 menu style (docs/design/THEMING.md, "Menus"): the check mark or,
	in a radio menu, a dot in the 12 px column after the row's own 7 px; the
	shortcut as text at the right, a little smaller and muted; the submenu
	chevron at the right, drawn by the control look.
*/
static void
r5_draw_item_symbols(BMenu* menu, BRect frame, const BRect& padding,
	bool marked, char shortcut, uint32 modifiers, bool submenu,
	rgb_color text, rgb_color background, bool selected)
{
	const float scale = MenuPrivate::R5Scale();
	const float pad = roundf(7 * scale);
	const float right = frame.right - padding.right;
	const float middle = floorf((frame.top + frame.bottom + 1) / 2);

	menu->PushState();
	menu->SetDrawingMode(B_OP_ALPHA);
	menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	menu->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	if (marked) {
		float size = roundf(12 * scale);
		BRect column(frame.left + pad, middle - size / 2,
			frame.left + pad + size, middle + size / 2);
		menu->SetHighColor(text);
		if (menu->IsRadioMode()) {
			float dot = roundf(3 * scale);
			BPoint center((column.left + column.right) / 2, middle);
			menu->FillEllipse(center, dot, dot);
		} else {
			// the mockup's "M2 6.3 4.8 9 10 3" in a 12 box, drawn at 11
			float unit = size * 11 / 12 / 12;
			BPoint origin(column.left + (size - 12 * unit) / 2,
				column.top + (size - 12 * unit) / 2);
			BShape shape;
			shape.MoveTo(origin + BPoint(2 * unit, 6.3f * unit));
			shape.LineTo(origin + BPoint(4.8f * unit, 9 * unit));
			shape.LineTo(origin + BPoint(10 * unit, 3 * unit));
			menu->SetPenSize(std::max(1.0f, roundf(1.9f * unit * 4) / 4));
			menu->MovePenTo(B_ORIGIN);
			menu->StrokeShape(&shape);
		}
	}

	float end = right;
	if (submenu) {
		float size = roundf(10 * scale);
		BRect chevron(right - size + 1, middle - size / 2, right,
			middle + size / 2 - 1);
		be_control_look->DrawArrowShape(menu, chevron, chevron, text,
			BControlLook::B_RIGHT_ARROW, 0, B_NO_TINT);
		end = chevron.left - roundf(6 * scale);
	}

	if (shortcut != 0) {
		char label[64];
		MenuPrivate::R5ShortcutLabel(shortcut, modifiers, label,
			sizeof(label));
		BFont font;
		menu->GetFont(&font);
		font.SetSize(MenuPrivate(menu).R5ShortcutFontSize());
		font_height height;
		font.GetHeight(&height);
		menu->SetFont(&font, B_FONT_SIZE);
		menu->SetHighColor(selected
			? MenuPrivate::R5Blend(background, text, 0.85f)
			: MenuPrivate::R5Blend(background, text, 0.6f));
		menu->SetDrawingMode(B_OP_OVER);
		float width = font.StringWidth(label);
		menu->DrawString(label, BPoint(end - width,
			floorf(middle + (height.ascent - height.descent) / 2)));
	}

	menu->PopState();
}
// SPDX-SnippetEnd


BMenuItem::BMenuItem(const char* label, BMessage* message, char shortcut, uint32 modifiers)
{
	_InitData();
	if (label != NULL)
		fLabel = strdup(label);

	SetMessage(message);

	fShortcutChar = shortcut;
	fModifiers = (fShortcutChar != 0 ? modifiers : 0);
}


BMenuItem::BMenuItem(BMenu* menu, BMessage* message)
{
	_InitData();
	SetMessage(message);
	_InitMenuData(menu);
}


BMenuItem::BMenuItem(BMessage* data)
{
	_InitData();

	if (data->HasString("_label")) {
		const char* string;

		data->FindString("_label", &string);
		SetLabel(string);
	}

	bool disable;
	if (data->FindBool("_disable", &disable) == B_OK)
		SetEnabled(!disable);

	bool marked;
	if (data->FindBool("_marked", &marked) == B_OK)
		SetMarked(marked);

	int32 userTrigger;
	if (data->FindInt32("_user_trig", &userTrigger) == B_OK)
		SetTrigger(userTrigger);

	if (data->HasInt32("_shortcut")) {
		int32 shortcut, mods;

		data->FindInt32("_shortcut", &shortcut);
		data->FindInt32("_mods", &mods);

		SetShortcut(shortcut, mods);
	}

	if (data->HasMessage("_msg")) {
		BMessage* message = new BMessage;
		data->FindMessage("_msg", message);
		SetMessage(message);
	}

	BMessage subMessage;
	if (data->FindMessage("_submenu", &subMessage) == B_OK) {
		BArchivable* object = instantiate_object(&subMessage);
		if (object != NULL) {
			BMenu* menu = dynamic_cast<BMenu*>(object);
			if (menu != NULL)
				_InitMenuData(menu);
		}
	}
}


BArchivable*
BMenuItem::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BMenuItem"))
		return new BMenuItem(data);

	return NULL;
}


status_t
BMenuItem::Archive(BMessage* data, bool deep) const
{
	status_t status = BArchivable::Archive(data, deep);

	if (status == B_OK && fLabel)
		status = data->AddString("_label", Label());

	if (status == B_OK && !IsEnabled())
		status = data->AddBool("_disable", true);

	if (status == B_OK && IsMarked())
		status = data->AddBool("_marked", true);

	if (status == B_OK && fUserTrigger)
		status = data->AddInt32("_user_trig", fUserTrigger);

	if (status == B_OK && fShortcutChar != 0) {
		status = data->AddInt32("_shortcut", fShortcutChar);
		if (status == B_OK)
			status = data->AddInt32("_mods", fModifiers);
	}

	if (status == B_OK && Message() != NULL)
		status = data->AddMessage("_msg", Message());

	if (status == B_OK && deep && fSubmenu) {
		BMessage submenu;
		if (fSubmenu->Archive(&submenu, true) == B_OK)
			status = data->AddMessage("_submenu", &submenu);
	}

	return status;
}


BMenuItem::~BMenuItem()
{
	if (fSuper != NULL)
		fSuper->RemoveItem(this);

	free(fLabel);
	delete fSubmenu;
}


void
BMenuItem::SetLabel(const char* string)
{
	if (fLabel != NULL) {
		free(fLabel);
		fLabel = NULL;
	}

	if (string != NULL)
		fLabel = strdup(string);

	if (fSuper != NULL) {
		fSuper->InvalidateLayout();

		if (fSuper->LockLooper()) {
			fSuper->Invalidate();
			fSuper->UnlockLooper();
		}
	}
}


void
BMenuItem::SetEnabled(bool enable)
{
	if (fEnabled == enable)
		return;

	fEnabled = enable;

	if (fSubmenu != NULL)
		fSubmenu->SetEnabled(enable);

	BMenu* menu = fSuper;
	if (menu != NULL && menu->LockLooper()) {
		menu->Invalidate(fBounds);
		menu->UnlockLooper();
	}
}


void
BMenuItem::SetMarked(bool mark)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The R5 menu style keeps room for the mark only while a menu has one.
	bool r5Style = fSuper != NULL && fMark != mark
		&& MenuPrivate(fSuper).R5Style();
	float markColumn = r5Style ? MenuPrivate(fSuper).R5MarkColumn() : 0;
// SPDX-SnippetEnd
	fMark = mark;

	if (mark && fSuper != NULL) {
		MenuPrivate priv(fSuper);
		priv.ItemMarked(this);
	}
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (r5Style && MenuPrivate(fSuper).R5MarkColumn() != markColumn
		&& fSuper->LockLooper()) {
		fSuper->InvalidateLayout();
		fSuper->Invalidate();
		fSuper->UnlockLooper();
	}
// SPDX-SnippetEnd
}


void
BMenuItem::SetTrigger(char trigger)
{
	fUserTrigger = trigger;

	// try uppercase letters first

	const char* pos = strchr(Label(), toupper(trigger));
	trigger = tolower(trigger);

	if (pos == NULL) {
		// take lowercase, too
		pos = strchr(Label(), trigger);
	}

	if (pos != NULL) {
		fTriggerIndex = UTF8CountChars(Label(), pos - Label());
		fTrigger = trigger;
	} else {
		fTrigger = 0;
		fTriggerIndex = -1;
	}

	if (fSuper != NULL)
		fSuper->InvalidateLayout();
}


void
BMenuItem::SetShortcut(char shortcut, uint32 modifiers)
{
	if (fShortcutChar != 0 && fWindow != NULL)
		fWindow->RemoveShortcut(fShortcutChar, fModifiers);

	uint32 key = (uint32)shortcut;

	if (key != 0 && fWindow != NULL)
		fWindow->_AddShortcut(&key, &modifiers, this);

	fShortcutChar = (char)key;
	fModifiers = (fShortcutChar != 0 ? modifiers : 0);

	if (fSuper != NULL) {
		fSuper->InvalidateLayout();

		if (fSuper->LockLooper()) {
			fSuper->Invalidate();
			fSuper->UnlockLooper();
		}
	}
}


const char*
BMenuItem::Label() const
{
	return fLabel;
}


bool
BMenuItem::IsEnabled() const
{
	if (fSubmenu)
		return fSubmenu->IsEnabled();

	if (!fEnabled)
		return false;

	return fSuper != NULL ? fSuper->IsEnabled() : true;
}


bool
BMenuItem::IsMarked() const
{
	return fMark;
}


char
BMenuItem::Trigger() const
{
	return fUserTrigger;
}


char
BMenuItem::Shortcut(uint32* modifiers) const
{
	if (modifiers)
		*modifiers = fModifiers;

	return fShortcutChar;
}


BMenu*
BMenuItem::Submenu() const
{
	return fSubmenu;
}


BMenu*
BMenuItem::Menu() const
{
	return fSuper;
}


BRect
BMenuItem::Frame() const
{
	return fBounds;
}


void
BMenuItem::GetContentSize(float* _width, float* _height)
{
	// TODO: Get rid of this. BMenu should handle this
	// automatically. Maybe it's not even needed, since our
	// BFont::Height() caches the value locally
	MenuPrivate(fSuper).CacheFontInfo();

	fCachedWidth = fSuper->StringWidth(fLabel);

	if (_width)
		*_width = (float)ceil(fCachedWidth);
	if (_height)
		*_height = MenuPrivate(fSuper).FontHeight();
}


void
BMenuItem::TruncateLabel(float maxWidth, char* newLabel)
{
	BFont font;
	fSuper->GetFont(&font);

	BString string(fLabel);

	font.TruncateString(&string, B_TRUNCATE_MIDDLE, maxWidth);

	string.CopyInto(newLabel, 0, string.Length());
	newLabel[string.Length()] = '\0';
}


void
BMenuItem::DrawContent()
{
	MenuPrivate menuPrivate(fSuper);
	menuPrivate.CacheFontInfo();

	fSuper->MovePenBy(0, menuPrivate.Ascent());
	BPoint lineStart = fSuper->PenLocation();

	fSuper->SetDrawingMode(B_OP_OVER);

	float labelWidth;
	float labelHeight;
	GetContentSize(&labelWidth, &labelHeight);

	const BRect& padding = menuPrivate.Padding();
	float maxContentWidth = fSuper->MaxContentWidth();
	float frameWidth = maxContentWidth > 0 ? maxContentWidth
		: fSuper->Frame().Width() - padding.left - padding.right;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The R5 menu style: the label ends where the shortcut and the chevron
	// begin (the room the layout gave them).
	BRect r5Padding;
	if (menuPrivate.R5Style(&r5Padding)) {
		const float scale = MenuPrivate::R5Scale();
		float end = fBounds.right - r5Padding.right;
		if (fSubmenu != NULL)
			end -= roundf(10 * scale) + roundf(6 * scale);
		if (fShortcutChar != 0) {
			char label[64];
			MenuPrivate::R5ShortcutLabel(fShortcutChar, fModifiers, label,
				sizeof(label));
			BFont font;
			fSuper->GetFont(&font);
			font.SetSize(menuPrivate.R5ShortcutFontSize());
			end -= roundf(16 * scale) + ceilf(font.StringWidth(label));
		}
		float room = end - fSuper->PenLocation().x;
		frameWidth = maxContentWidth > 0 ? std::min(maxContentWidth, room)
			: room;
	}
// SPDX-SnippetEnd

	if (roundf(frameWidth) >= roundf(labelWidth))
		fSuper->DrawString(fLabel);
	else {
		// truncate label to fit
		char* truncatedLabel = new char[strlen(fLabel) + 4];
		TruncateLabel(frameWidth, truncatedLabel);
		fSuper->DrawString(truncatedLabel);
		delete[] truncatedLabel;
	}

	if (fSuper->AreTriggersEnabled() && fTriggerIndex != -1) {
		float escapements[fTriggerIndex + 1];
		BFont font;
		fSuper->GetFont(&font);

		font.GetEscapements(fLabel, fTriggerIndex + 1, escapements);

		for (int32 i = 0; i < fTriggerIndex; i++)
			lineStart.x += escapements[i] * font.Size();

		lineStart.x--;
		lineStart.y++;

		BPoint lineEnd(lineStart);
		lineEnd.x += escapements[fTriggerIndex] * font.Size();

		fSuper->StrokeLine(lineStart, lineEnd);
	}
}


void
BMenuItem::Draw()
{
	const color_which lowColor = fSuper->LowUIColor();
	const color_which highColor = fSuper->HighUIColor();

	fSuper->SetLowColor(_LowColor());
	fSuper->SetHighColor(_HighColor());

	if (_IsActivated()) {
		// fill in the background
		BRect frame(Frame());
		be_control_look->DrawMenuItemBackground(fSuper, frame, frame,
			fSuper->LowColor(), BControlLook::B_ACTIVATED);
	}

	// draw content
	fSuper->MovePenTo(ContentLocation());
	DrawContent();

	// draw extra symbols
	MenuPrivate privateAccessor(fSuper);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	BRect r5Padding;
	if (privateAccessor.R5Style(&r5Padding)) {
		uint32 modifiers = 0;
		char shortcut = Shortcut(&modifiers);
		r5_draw_item_symbols(fSuper, Frame(), r5Padding, IsMarked(),
			shortcut, modifiers, Submenu() != NULL, _HighColor(),
			_LowColor(), _IsActivated());
		fSuper->SetLowUIColor(lowColor);
		fSuper->SetHighUIColor(highColor);
		return;
	}
// SPDX-SnippetEnd
	const menu_layout layout = privateAccessor.Layout();
	if (layout != B_ITEMS_IN_ROW) {
		if (IsMarked())
			_DrawMarkSymbol();
	}

	if (layout == B_ITEMS_IN_COLUMN) {
		if (fShortcutChar != 0)
			_DrawShortcutSymbol(privateAccessor.HasSubmenus());

		if (Submenu() != NULL)
			_DrawSubmenuSymbol();
	}

	// restore the parent menu's low color and high color
	fSuper->SetLowUIColor(lowColor);
	fSuper->SetHighUIColor(highColor);
}


void
BMenuItem::Highlight(bool highlight)
{
	fSuper->Invalidate(Frame());
}


bool
BMenuItem::IsSelected() const
{
	return fSelected;
}


BPoint
BMenuItem::ContentLocation() const
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The R5 menu style: after the look's left padding, the content in the
	// middle of the row.
	BRect r5Padding;
	if (fSuper != NULL && MenuPrivate(fSuper).R5Style(&r5Padding)) {
		float width;
		float height;
		const_cast<BMenuItem*>(this)->GetContentSize(&width, &height);
		return BPoint(fBounds.left + r5Padding.left
			+ MenuPrivate(fSuper).R5MarkColumn(), fBounds.top
			+ floorf((fBounds.Height() + 1 - ceilf(height)) / 2));
	}
// SPDX-SnippetEnd
	const BRect& padding = MenuPrivate(fSuper).Padding();

	return BPoint(fBounds.left + padding.left, fBounds.top + padding.top);
}


void BMenuItem::_ReservedMenuItem1() {}
void BMenuItem::_ReservedMenuItem2() {}
void BMenuItem::_ReservedMenuItem3() {}
void BMenuItem::_ReservedMenuItem4() {}


BMenuItem::BMenuItem(const BMenuItem &)
{
}


BMenuItem&
BMenuItem::operator=(const BMenuItem &)
{
	return *this;
}


void
BMenuItem::_InitData()
{
	fLabel = NULL;
	fSubmenu = NULL;
	fWindow = NULL;
	fSuper = NULL;
	fModifiers = 0;
	fCachedWidth = 0;
	fTriggerIndex = -1;
	fUserTrigger = 0;
	fTrigger = 0;
	fShortcutChar = 0;
	fMark = false;
	fEnabled = true;
	fSelected = false;
}


void
BMenuItem::_InitMenuData(BMenu* menu)
{
	fSubmenu = menu;

	MenuPrivate(fSubmenu).SetSuperItem(this);

	BMenuItem* item = menu->FindMarked();

	if (menu->IsRadioMode() && menu->IsLabelFromMarked() && item != NULL)
		SetLabel(item->Label());
	else
		SetLabel(menu->Name());
}


void
BMenuItem::Install(BWindow* window)
{
	if (fSubmenu != NULL)
		MenuPrivate(fSubmenu).Install(window);

	fWindow = window;

	uint32 key = (uint32)fShortcutChar;
	uint32 modifiers = fModifiers;

	if (fShortcutChar != 0 && fWindow != NULL)
		fWindow->_AddShortcut(&key, &modifiers, this);

	fShortcutChar = (char)key;
	fModifiers = (fShortcutChar != 0 ? modifiers : 0);

	if (!Messenger().IsValid())
		SetTarget(fWindow);
}


status_t
BMenuItem::Invoke(BMessage* message)
{
	if (!IsEnabled())
		return B_ERROR;

	if (fSuper->IsRadioMode())
		SetMarked(true);

	bool notify = false;
	uint32 kind = InvokeKind(&notify);

	BMessage clone(kind);
	status_t err = B_BAD_VALUE;

	if (message == NULL && !notify)
		message = Message();

	if (message == NULL) {
		if (!fSuper->IsWatched())
			return err;
	} else
		clone = *message;

	clone.AddInt32("index", fSuper->IndexOf(this));
	clone.AddInt64("when", (int64)system_time());
	clone.AddPointer("source", this);
	clone.AddMessenger("be:sender", BMessenger(fSuper));

	if (message != NULL)
		err = BInvoker::Invoke(&clone);

//	TODO: assynchronous messaging
//	SendNotices(kind, &clone);

	return err;
}


void
BMenuItem::Uninstall()
{
	if (fSubmenu != NULL)
		MenuPrivate(fSubmenu).Uninstall();

	if (Target() == fWindow)
		SetTarget(BMessenger());

	if (fShortcutChar != 0 && fWindow != NULL)
		fWindow->RemoveShortcut(fShortcutChar, fModifiers);

	fWindow = NULL;
}


void
BMenuItem::SetSuper(BMenu* super)
{
	if (fSuper != NULL && super != NULL) {
		debugger("Error - can't add menu or menu item to more than 1 container"
			" (either menu or menubar).");
	}

	if (fSubmenu != NULL)
		MenuPrivate(fSubmenu).SetSuper(super);

	fSuper = super;
}


void
BMenuItem::Select(bool selected)
{
	if (fSelected == selected)
		return;

	if (Submenu() != NULL || IsEnabled()) {
		fSelected = selected;
		Highlight(selected);
	}
}


bool
BMenuItem::_IsActivated()
{
	return IsSelected() && (IsEnabled() || fSubmenu != NULL);
}


rgb_color
BMenuItem::_LowColor()
{
	return _IsActivated() ? ui_color(B_MENU_SELECTED_BACKGROUND_COLOR)
		: ui_color(B_MENU_BACKGROUND_COLOR);
}


rgb_color
BMenuItem::_HighColor()
{
	rgb_color highColor;

	bool isEnabled = IsEnabled();
	bool isSelected = IsSelected();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The R5 menu style: a title of a menu bar keeps its colour on the
	// light pill; a disabled item is the text mixed with the background.
	if (fSuper != NULL && MenuPrivate::R5LookStyle()) {
		if (isEnabled && isSelected
			&& dynamic_cast<BMenuBar*>(fSuper) != NULL) {
			return ui_color(B_MENU_ITEM_TEXT_COLOR);
		}
		if (!isEnabled && MenuPrivate(fSuper).R5Style()) {
			return MenuPrivate::R5Blend(ui_color(B_MENU_BACKGROUND_COLOR),
				ui_color(B_MENU_ITEM_TEXT_COLOR), 0.4f);
		}
	}
// SPDX-SnippetEnd
	if (isEnabled && isSelected)
		highColor = ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR);
	else if (isEnabled)
		highColor = ui_color(B_MENU_ITEM_TEXT_COLOR);
	else {
		rgb_color bgColor = fSuper->LowColor();
		if (bgColor.red + bgColor.green + bgColor.blue > 128 * 3)
			highColor = tint_color(bgColor, B_DISABLED_LABEL_TINT);
		else
			highColor = tint_color(bgColor, B_LIGHTEN_2_TINT);
	}

	return highColor;
}


void
BMenuItem::_DrawMarkSymbol()
{
	fSuper->PushState();

	BRect r(fBounds);
	float leftMargin;
	MenuPrivate(fSuper).GetItemMargins(&leftMargin, NULL, NULL, NULL);
	float gap = leftMargin / 4;
	r.right = r.left + leftMargin - gap;
	r.left += gap / 3;

	BPoint center(floorf((r.left + r.right) / 2.0),
		floorf((r.top + r.bottom) / 2.0));

	float size = std::min(r.Height() - 2, r.Width());
	r.top = floorf(center.y - size / 2 + 0.5);
	r.bottom = floorf(center.y + size / 2 + 0.5);
	r.left = floorf(center.x - size / 2 + 0.5);
	r.right = floorf(center.x + size / 2 + 0.5);

	BShape arrowShape;
	center.x += 0.5;
	center.y += 0.5;
	size *= 0.3;
	arrowShape.MoveTo(BPoint(center.x - size, center.y - size * 0.25));
	arrowShape.LineTo(BPoint(center.x - size * 0.25, center.y + size));
	arrowShape.LineTo(BPoint(center.x + size, center.y - size));

	fSuper->SetHighColor(tint_color(_HighColor(), kMarkTint));
	fSuper->SetDrawingMode(B_OP_OVER);
	fSuper->SetPenSize(2.0);
	// NOTE: StrokeShape() offsets the shape by the current pen position,
	// it is not documented in the BeBook, but it is true!
	fSuper->MovePenTo(B_ORIGIN);
	fSuper->StrokeShape(&arrowShape);

	fSuper->PopState();
}


void
BMenuItem::_DrawShortcutSymbol(bool submenus)
{
	BMenu* menu = fSuper;
	BFont font;
	menu->GetFont(&font);
	BPoint where = ContentLocation();
	// Start from the right and walk our way back
	where.x = fBounds.right - font.Size();

	// Leave space for the submenu arrow if any item in the menu has a submenu
	if (submenus)
		where.x -= fBounds.Height() / 2;

	const float ascent = MenuPrivate(fSuper).Ascent();
	if ((fShortcutChar <= B_SPACE && kUTF8ControlMap[(int)fShortcutChar])
		|| fShortcutChar == B_DELETE) {
		_DrawControlChar(fShortcutChar, where + BPoint(0, ascent));
	} else
		fSuper->DrawChar(fShortcutChar, where + BPoint(0, ascent));

	where.y += (fBounds.Height() - 11) / 2 - 1;
	where.x -= 4;

	// TODO: It would be nice to draw these taking into account the text (low)
	// color.
	if ((fModifiers & B_COMMAND_KEY) != 0) {
		const BBitmap* command = MenuPrivate::MenuItemCommand();
		const BRect &rect = command->Bounds();
		where.x -= rect.Width() + 1;
		fSuper->DrawBitmap(command, where);
	}

	if ((fModifiers & B_CONTROL_KEY) != 0) {
		const BBitmap* control = MenuPrivate::MenuItemControl();
		const BRect &rect = control->Bounds();
		where.x -= rect.Width() + 1;
		fSuper->DrawBitmap(control, where);
	}

	if ((fModifiers & B_OPTION_KEY) != 0) {
		const BBitmap* option = MenuPrivate::MenuItemOption();
		const BRect &rect = option->Bounds();
		where.x -= rect.Width() + 1;
		fSuper->DrawBitmap(option, where);
	}

	if ((fModifiers & B_SHIFT_KEY) != 0) {
		const BBitmap* shift = MenuPrivate::MenuItemShift();
		const BRect &rect = shift->Bounds();
		where.x -= rect.Width() + 1;
		fSuper->DrawBitmap(shift, where);
	}
}


void
BMenuItem::_DrawSubmenuSymbol()
{
	fSuper->PushState();

	float symbolSize = roundf(Frame().Height() * 2 / 3);

	BRect rect(fBounds);
	rect.left = rect.right - symbolSize;

	// 14px by default, scaled with font size up to right margin - padding
	BRect symbolRect(0, 0, symbolSize, symbolSize);
	symbolRect.OffsetTo(BPoint(rect.left,
		fBounds.top + (fBounds.Height() - symbolSize) / 2));

	be_control_look->DrawArrowShape(Menu(), symbolRect, symbolRect,
		_HighColor(), BControlLook::B_RIGHT_ARROW, 0, kMarkTint);

	fSuper->PopState();
}


void
BMenuItem::_DrawControlChar(char shortcut, BPoint where)
{
	// TODO: If needed, take another font for the control characters
	//	(or have font overlays in the app_server!)
	const char* symbol = " ";
	if (shortcut == B_DELETE)
		symbol = kDeleteShortcutUTF8;
	else if (kUTF8ControlMap[(int)fShortcutChar])
		symbol = kUTF8ControlMap[(int)fShortcutChar];

	fSuper->DrawString(symbol, where);
}


void
BMenuItem::SetAutomaticTrigger(int32 index, uint32 trigger)
{
	fTriggerIndex = index;
	fTrigger = trigger;
}
