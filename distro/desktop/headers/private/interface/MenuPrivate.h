/*
 * Copyright 2006-2015 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Stefano Ceccherini (stefano.ceccherini@gmail.com)
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */

#ifndef __MENU_PRIVATE_H
#define __MENU_PRIVATE_H


#include <Menu.h>


enum menu_states {
	MENU_STATE_TRACKING = 0,
	MENU_STATE_TRACKING_SUBMENU = 1,
	MENU_STATE_KEY_TO_SUBMENU = 2,
	MENU_STATE_KEY_LEAVE_SUBMENU = 3,
	MENU_STATE_CLOSED = 5
};


class BBitmap;
class BMenu;
class BWindow;


namespace BPrivate {

extern const char* kEmptyMenuLabel;

class MenuPrivate {
public:
								MenuPrivate(BMenu* menu);

			menu_layout			Layout() const;
			void				SetLayout(menu_layout layout);

			void				ItemMarked(BMenuItem* item);
			void				CacheFontInfo();

			float				FontHeight() const;
			float				Ascent() const;
			BRect				Padding() const;
			void				GetItemMargins(float*, float*, float*, float*)
									const;
			void				SetItemMargins(float, float, float, float);

			int					State(BMenuItem** item = NULL) const;

			void				Install(BWindow* window);
			void				Uninstall();
			void				SetSuper(BMenu* menu);
			void				SetSuperItem(BMenuItem* item);
			void				InvokeItem(BMenuItem* item, bool now = false);
			void				QuitTracking(bool thisMenuOnly = true);
			bool				HasSubmenus() { return fMenu->fHasSubmenus; }

	static	status_t			CreateBitmaps();
	static	void				DeleteBitmaps();

	static	const BBitmap*		MenuItemShift();
	static	const BBitmap*		MenuItemControl();
	static	const BBitmap*		MenuItemOption();
	static	const BBitmap*		MenuItemCommand();
	static	const BBitmap*		MenuItemMenu();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The R5 menu style (docs/design/THEMING.md, "Menus"). A control look
	// that returns more than 1 for BControlLook::B_MENU_BACKGROUND in
	// GetBackgroundInsets() asks for it: that inset is the space inside the
	// menu window, and B_MENU_ITEM_BACKGROUND is the padding around the
	// content of an item. Only for a column menu in a menu window.
			bool				R5Style(BRect* padding = NULL,
									float* margin = NULL) const;
	static	bool				R5LookStyle(BRect* padding = NULL,
									float* margin = NULL);
	static	float				R5Scale();
			float				R5MarkColumn() const;
			float				R5ShortcutFontSize() const;
	static	void				R5ShortcutLabel(char shortcut,
									uint32 modifiers, char* buffer,
									size_t size);
	static	rgb_color			R5Blend(const rgb_color& from,
									const rgb_color& to, float amount);
	static	rgb_color			R5EdgeColor(const rgb_color& background);
	static	rgb_color			R5SeparatorColor(
									const rgb_color& background);
// SPDX-SnippetEnd

private:
			BMenu*				fMenu;

	static	BBitmap*			sMenuItemShift;
	static	BBitmap*			sMenuItemControl;
	static	BBitmap*			sMenuItemOption;
	static	BBitmap*			sMenuItemAlt;
	static	BBitmap*			sMenuItemMenu;

};

};	// namespace BPrivate


// Note: since sqrt is slow, we don't use it and return the square of the
// distance
#define square(x) ((x) * (x))
static inline float
point_distance(const BPoint &pointA, const BPoint &pointB)
{
	return square(pointA.x - pointB.x) + square(pointA.y - pointB.y);
}
#undef square


#endif	// __MENU_PRIVATE_H
