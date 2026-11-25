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
#ifndef ICON_COLUMN_MENU_ITEM_H
#define ICON_COLUMN_MENU_ITEM_H


#include <MenuItem.h>


class BBitmap;


/*!	A Deskbar menu item in the icon column (owner, variant A, 25 Nov 2026):
	left to right the mark, a column for a mini icon (empty when the item
	has none), the label, the submenu arrow. Every label of a menu built
	from these items, and from Tracker's ModelMenuItem or IconMenuItem with
	the same column, starts at the same x.
*/
class TIconColumnMenuItem : public BMenuItem {
public:
								TIconColumnMenuItem(const char* label,
									BMessage* message, float column,
									const BBitmap* icon = NULL);
								TIconColumnMenuItem(BMenu* submenu,
									BMessage* message, float column,
									const BBitmap* icon = NULL);
	virtual						~TIconColumnMenuItem();

	virtual	void				GetContentSize(float* _width, float* _height);
	virtual	void				DrawContent();

	// The column of Tracker's ModelMenuItem (folders, applications) and of
	// its IconMenuItem (volumes, the Settings pebble).
	static	float				ModelColumn();
	static	float				IconColumn();

	// Gives every plain item of the menu (no submenu, no icon class) the
	// column, so its label lines up with the icon items.
	static	void				AlignPlainItems(BMenu* menu, float column);

private:
			BBitmap*			fIcon;
				// a copy: a shared icon such as the pebble may be replaced
			float				fColumn;
			float				fHeightDelta;
};


#endif	// ICON_COLUMN_MENU_ITEM_H
