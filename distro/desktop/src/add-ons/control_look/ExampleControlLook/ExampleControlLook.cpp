/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	The Theme Kit template (R5Book, "Making themes"): the smallest control
	look add-on that fits an R5 theme. Copy this directory, rename the class
	and the add-on, and change only what you draw differently; everything
	else stays Haiku's through HaikuControlLook.

	The rules it shows:
	- every colour comes from ui_color() (or the base the control passes), so
	  the theme's light and dark sets both work;
	- sizes scale with be_plain_font, and every Draw*() method insets the
	  rect exactly as Haiku's does, on the sides in \a borders, so a window
	  lays out the same with any look;
	- the scroll bar width is GetScrollBarWidth(), Haiku's hook, so BScrollBar
	  and BScrollView follow it;
	- nothing needs the GPU: Painter draws the same in Safe mode.

	It is built with the system as a compile check and not installed. To try
	it, copy the built add-on to ~/config/non-packaged/add-ons/control_look/
	and choose it on the Theme page (Parts > Controls).
*/


#include <ControlLook.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <View.h>

#include <math.h>
#include <new>

#include "HaikuControlLook.h"


namespace BPrivate {


class ExampleControlLook : public HaikuControlLook {
public:
								ExampleControlLook();

	virtual	void				DrawButtonFrame(BView* view, BRect& rect,
									const BRect& updateRect,
									const rgb_color& base,
									const rgb_color& background,
									uint32 flags = 0,
									uint32 borders = B_ALL_BORDERS);
	virtual	void				DrawButtonBackground(BView* view, BRect& rect,
									const BRect& updateRect,
									const rgb_color& base,
									uint32 flags = 0,
									uint32 borders = B_ALL_BORDERS,
									orientation orientation = B_HORIZONTAL);
	virtual	float				GetScrollBarWidth(
									orientation orientation = B_VERTICAL);
};


static float
font_scale()
{
	return be_plain_font->Size() / 12.0f;
}


static void
inset_sides(BRect& rect, float inset, uint32 borders)
{
	if ((borders & BControlLook::B_LEFT_BORDER) != 0)
		rect.left += inset;
	if ((borders & BControlLook::B_TOP_BORDER) != 0)
		rect.top += inset;
	if ((borders & BControlLook::B_RIGHT_BORDER) != 0)
		rect.right -= inset;
	if ((borders & BControlLook::B_BOTTOM_BORDER) != 0)
		rect.bottom -= inset;
}


ExampleControlLook::ExampleControlLook()
	:
	HaikuControlLook()
{
}


/*!	A flat face with a one pixel line in the control border colour. The
	other DrawButtonFrame() variants (with radii) are Haiku's.
*/
void
ExampleControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	float left, top, right, bottom;
	GetFrameInsets(B_BUTTON_FRAME, flags, left, top, right, bottom);

	if (ShouldDraw(view, rect, updateRect)) {
		rgb_color line = ui_color(B_CONTROL_BORDER_COLOR);
		if ((flags & B_DISABLED) != 0)
			line = disable_color(line, background);
		if ((flags & B_FOCUSED) != 0)
			line = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

		view->PushState();
		view->SetHighColor(background);
		view->StrokeRect(rect);
		BRect face(rect);
		face.InsetBy(1, 1);
		view->SetHighColor(line);
		view->StrokeRect(face);
		view->PopState();
	}

	// Haiku's contract: the inset, on the sides asked for
	inset_sides(rect, left, borders);
}


void
ExampleControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	if (ShouldDraw(view, rect, updateRect)) {
		rgb_color fill = base;
		if ((flags & B_ACTIVATED) != 0)
			fill = tint_color(base, base.IsLight() ? B_DARKEN_1_TINT
				: B_LIGHTEN_1_TINT);
		view->SetHighColor(fill);
		view->FillRect(rect);
	}

	float left, top, right, bottom;
	GetBackgroundInsets(B_BUTTON_BACKGROUND, flags, left, top, right, bottom);
	inset_sides(rect, left, borders);
}


//! Scroll bars two pixels wider than Haiku's, scaled with the font.
float
ExampleControlLook::GetScrollBarWidth(orientation orientation)
{
	return roundf(16.0f * font_scale());
}


}	// namespace BPrivate


extern "C" BControlLook*
instantiate_control_look(image_id id)
{
	return new(std::nothrow) BPrivate::ExampleControlLook();
}
