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


#include "IconColumnMenuItem.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <Menu.h>

#include <algorithm>
#include <math.h>
#include <typeinfo>

#include "Utilities.h"


TIconColumnMenuItem::TIconColumnMenuItem(const char* label, BMessage* message,
	float column, const BBitmap* icon)
	:
	BMenuItem(label, message),
	fIcon(icon != NULL ? new BBitmap(icon) : NULL),
	fColumn(column),
	fHeightDelta(0)
{
}


TIconColumnMenuItem::TIconColumnMenuItem(BMenu* submenu, BMessage* message,
	float column, const BBitmap* icon)
	:
	BMenuItem(submenu, message),
	fIcon(icon != NULL ? new BBitmap(icon) : NULL),
	fColumn(column),
	fHeightDelta(0)
{
}


TIconColumnMenuItem::~TIconColumnMenuItem()
{
	delete fIcon;
}


void
TIconColumnMenuItem::GetContentSize(float* _width, float* _height)
{
	float width;
	float height;
	BMenuItem::GetContentSize(&width, &height);

	// only a row with an icon becomes as high as the icon, as with
	// Tracker's IconMenuItem
	fHeightDelta = 0;
	if (fIcon != NULL) {
		float iconHeight = fIcon->Bounds().Height() + 1;
		fHeightDelta = iconHeight - height;
		height = std::max(height, iconHeight);
	}

	if (_width != NULL)
		*_width = width + fColumn;
	if (_height != NULL)
		*_height = height;
}


void
TIconColumnMenuItem::DrawContent()
{
	BMenu* menu = Menu();
	BPoint where(ContentLocation());

	BPoint text(where.x + fColumn, where.y);
	if (fHeightDelta > 0)
		text.y += ceilf(fHeightDelta / 2);
	menu->MovePenTo(text);
	BMenuItem::DrawContent();

	if (fIcon == NULL)
		return;

	menu->PushState();
	if (fHeightDelta < 0)
		where.y += ceilf(-fHeightDelta / 2);
	menu->SetDrawingMode(B_OP_ALPHA);
	if (!IsEnabled()) {
		menu->SetHighColor(0, 0, 0, 64);
		menu->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	}
	menu->DrawBitmapAsync(fIcon, where);
	menu->PopState();
}


/*static*/ float
TIconColumnMenuItem::ModelColumn()
{
	// ModelMenuItem: the icon and a quarter of it
	const float iconSize = BPrivate::ListIconSize();
	return iconSize + iconSize / 4;
}


/*static*/ float
TIconColumnMenuItem::IconColumn()
{
	// IconMenuItem: the icon and the label spacing
	return be_control_look->ComposeIconSize(B_MINI_ICON).Width()
		+ be_control_look->DefaultLabelSpacing();
}


/*static*/ void
TIconColumnMenuItem::AlignPlainItems(BMenu* menu, float column)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (typeid(*item) != typeid(BMenuItem) || item->Submenu() != NULL)
			continue;

		BMessage* message = item->Message() != NULL
			? new BMessage(*item->Message()) : NULL;
		TIconColumnMenuItem* aligned = new TIconColumnMenuItem(item->Label(),
			message, column);
		uint32 modifiers = 0;
		char shortcut = item->Shortcut(&modifiers);
		if (shortcut != 0)
			aligned->SetShortcut(shortcut, modifiers);
		aligned->SetEnabled(item->IsEnabled());
		aligned->SetTarget(item->Messenger());

		menu->RemoveItem(i);
		menu->AddItem(aligned, i);
		aligned->SetMarked(item->IsMarked());
		delete item;
	}
}
