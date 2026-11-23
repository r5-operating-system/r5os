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



/*!	Deskbar styles (docs/design/DESKBAR-STYLES.md, owner 22 Nov 2026):
	Classic (Haiku), Deskbar as Haiku draws it, and Default (R5), the new
	default, with three layouts. This file holds the R5 look: its numbers
	(from the mockup, at the default font size of 12, scaled with
	be_plain_font), its colours, and the small drawing helpers the Deskbar
	views share. Classic never calls into the drawing part.
*/
#ifndef DESKBAR_STYLE_H
#define DESKBAR_STYLE_H


#include <GraphicsDefs.h>
#include <OS.h>
#include <Rect.h>


class BBitmap;
class BView;
class BWindow;


namespace R5Deskbar {


enum {
	kStyleR5		= 0,
	kStyleClassic	= 1
};

enum {
	kLayoutVertical		= 0,
	kLayoutCompact		= 1,
	kLayoutHorizontal	= 2
};


// From the Settings page (distro/settings/DeskbarModule.cpp), absolute
// values, each field optional: int32 "style", int32 "layout", bool
// "rounded", bool "floating".
const uint32 kMsgSetStyle = 'R5ds';
// The growing pill: one step of the animation.
const uint32 kMsgAnimate = 'R5da';
// The chevron of the pill: show or hide the tray symbols.
const uint32 kMsgToggleUnfold = 'R5df';
// Repaint the whole window after a style change.
const uint32 kMsgRepaint = 'R5dr';


// The settings (desk_settings in BarSettings.h)
bool			Modern();
int32			Layout();
bool			Compact();
bool			Rounded();
bool			Floating();
bool			Unfolded();
void			SetUnfolded(bool unfolded);

// Whether the compositor shows effects (the GPU mode); asked at start
// and when the screen changes.
void			UpdateEffects();
bool			EffectsActive();
bool			AnimationsAllowed();

// Board A3: Vertical against the edge is flat and opaque, without glass.
bool			FlatCorner();
// The surfaces are glass (GPU mode, not the flat corner).
bool			Glass();

float			Scale();
float			Size(float pixels);
float			Margin();
float			WindowRadius();
float			ButtonRadius();
float			HighlightRadius();
float			RowRadius();

rgb_color		SurfaceColor();
rgb_color		TextColor();
rgb_color		LabelColor();
rgb_color		CountColor();
rgb_color		FrontColor();
rgb_color		EdgeColor();
rgb_color		EdgeLineColor();

// The team of the front application (the roster's active application),
// or -1.
void			SetFrontTeam(team_id team);
team_id			FrontTeam();

// Drawing. Shades are blended over what the view shows and keep a glass
// view's background glass; the blue line and icons are opaque.
void			FillShade(BView* view, BRect rect, float radius, uint8 alpha);
void			DrawHover(BView* view, BRect rect, float radius);
void			DrawHollow(BView* view, BRect rect, float radius);
void			DrawFrontLine(BView* view, BRect line);
void			DrawSeparator(BView* view, float x, float top, float bottom);
void			DrawLine(BView* view, BPoint from, BPoint to, rgb_color color);
void			DrawIcon(BView* view, const BBitmap* icon, BRect frame);
void			DrawChevron(BView* view, BRect frame, bool up);
const BBitmap*	Pebble(float side);

// The window's only child in Default (R5) and Classic, TBarView inside it:
// it draws the 1 px edge (DeskbarStyleViews.cpp).
BView*			CreateEdgeView(BRect frame);

// The corners, shadow and edge of the Deskbar menu's window, once.
void			StyleMenuWindow(BWindow* window);
// Classic: the same window square, with the decorator's shadow, once it
// had the R5 look.
bool			UnstyleMenuWindow(BWindow* window);

// The ease of the growing pill: CSS cubic-bezier(.2, .9, .3, 1.25).
float			Ease(float progress);
const bigtime_t	kGrowDuration = 340000;


}	// namespace R5Deskbar


#endif	// DESKBAR_STYLE_H
