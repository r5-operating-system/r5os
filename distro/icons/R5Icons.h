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
#ifndef R5_ICONS_H
#define R5_ICONS_H


#include <GraphicsDefs.h>
#include <Rect.h>
#include <SupportDefs.h>


class BBitmap;
class BButton;
class BView;


// Public icon helpers in libr5icons.so. The long description for
// developers is docs/ICONS.md.
//
// The soname is libr5icons.so. The regular image and the minimum image
// both install it in /system/lib. A Jam program links libr5icons.so and
// includes this header.
//
// Adding a function keeps the library compatible. Changing or removing
// a function, or renumbering r5_icon, breaks programs already built
// against this soname.
//
// The numbered values below are part of the call contract. Add a new
// icon at the end. Do not renumber one that already shipped.
//
// R5_ICON_BACK, R5_ICON_FORWARD, and R5_ICON_UP are the arrows Tracker
// draws in its location bar. R5_ICON_HOME is the house Tracker draws
// on the home folder. Those bytes are unmodified Tracker HVIF.
// R5_ICON_UNLOCKED is Haiku's Action_Login (open lock). R5_ICON_LOCKED
// is Haiku's Action_Logout (closed lock). None of these pictures
// includes the preferences wrench. About R5 does not use this header.
enum r5_icon {
	R5_ICON_BACK = 1,
	R5_ICON_FORWARD = 2,
	R5_ICON_UP = 3,
	R5_ICON_HOME = 4,
	R5_ICON_UNLOCKED = 5,
	R5_ICON_LOCKED = 6
};


// A new BBitmap of side x side pixels, or NULL. The caller deletes it.
// `side` is the edge in pixels and must be at least 1.
BBitmap* r5_icon_bitmap(r5_icon which, float side);

// Same, from HVIF bytes the caller already has. Any vector icon.
BBitmap* r5_vector_icon_bitmap(const uint8* data, size_t size, float side);

// The icon of a file or folder, through the public node API. Any path
// that has an icon. NULL if the path cannot be opened or has no icon.
BBitmap* r5_file_icon_bitmap(const char* path, float side);

// The icon of a MIME type, through the public MIME API. Any type the
// system knows, for example "application/x-vnd.Haiku-About".
BBitmap* r5_mime_icon_bitmap(const char* type, float side);


// Makes `button` a square of `buttonHeight` and draws the icon inside
// it, with the same outer height as a text button on that row. The
// label is left as the caller set it. An empty label keeps the icon
// centered. These calls do not draw the preferences wrench.
status_t r5_button_set_icon(BButton* button, r5_icon which,
	float buttonHeight);
status_t r5_button_set_vector(BButton* button, const uint8* data,
	size_t size, float buttonHeight);
status_t r5_button_set_file_icon(BButton* button, const char* path,
	float buttonHeight);
status_t r5_button_set_mime_icon(BButton* button, const char* type,
	float buttonHeight);

// Draws the open lock when `locked` is false, and the closed lock when
// `locked` is true. Same square and height as r5_button_set_icon().
// The label is left as the caller set it. No preferences wrench.
status_t r5_button_set_lock(BButton* button, bool locked,
	float buttonHeight);

// Line glyphs: small one-color symbols for which Haiku has no icon (the
// power symbol, a circular arrow, a clock face, an eye). They are drawn
// in code, in the color the caller passes, so they follow the text color
// and dark mode. The numbers are part of the call contract, the same as
// r5_icon. Add a new glyph at the end. docs/ICONS.md lists them.
enum r5_glyph {
	R5_GLYPH_POWER = 1,
	R5_GLYPH_RESTART = 2,
	R5_GLYPH_CLOCK = 3,
	R5_GLYPH_EYE = 4,
	R5_GLYPH_EYE_OFF = 5,
	R5_GLYPH_LOG_OUT = 6,
	R5_GLYPH_BLUETOOTH = 7,
	// The face button symbols of a PlayStation-style game controller.
	R5_GLYPH_CROSS = 8,
	R5_GLYPH_CIRCLE = 9,
	R5_GLYPH_SQUARE = 10,
	R5_GLYPH_TRIANGLE = 11,
	// Deskbar tray symbols (docs/design/DESKBAR-STYLES.md, step 5): one
	// colour, 2 px lines on the 24 grid, the state in the shape.
	R5_GLYPH_WIFI = 12,
	R5_GLYPH_VOLUME = 13,
	R5_GLYPH_VOLUME_OFF = 14,
	R5_GLYPH_NETWORK = 15,
	R5_GLYPH_NETWORK_OFF = 16,
	R5_GLYPH_AIRPLAY = 17,
	R5_GLYPH_MAIL = 18,
	R5_GLYPH_MAIL_NEW = 19,
	// The Light and Dark buttons of the Theme page (docs/design/THEMING.md).
	R5_GLYPH_SUN = 20,
	R5_GLYPH_MOON = 21
};

// Draws the glyph inside `frame` of `view`, in `color`. The view's
// drawing state is the same afterwards.
void r5_glyph_draw(BView* view, r5_glyph which, BRect frame,
	rgb_color color);

// A new B_RGBA32 bitmap of side x side pixels with the glyph on a
// transparent background, or NULL. The caller deletes it.
BBitmap* r5_glyph_bitmap(r5_glyph which, float side, rgb_color color);

// Puts the glyph before the label of `button`, in the control text
// color, at the size of the button's font. Unlike r5_button_set_icon(),
// the button keeps its own width and height. Call it again after
// B_COLORS_UPDATED so the glyph takes the new text color.
status_t r5_button_set_glyph(BButton* button, r5_glyph which);

// Level glyphs: a signal strength or a battery charge, drawn in code from a
// level. The numbers are part of the call contract; add new ones at the
// end. docs/ICONS.md describes them.
enum r5_level_glyph {
	R5_LEVEL_SIGNAL = 1,
		// Four bars of rising height, left to right
	R5_LEVEL_BATTERY = 2,
		// A battery body with a cap on the right, four segments inside
	R5_LEVEL_WIFI = 3,
		// The Wi-Fi symbol: a dot and three arcs, one step each (a line
		// symbol in the pen of r5_glyph_draw(), for the Deskbar tray)
	R5_LEVEL_BATTERY_SYMBOL = 4
		// A round battery with a cap and a fill that grows in four steps
		// (a line symbol, for the Deskbar tray)
};

enum r5_level_style {
	R5_LEVEL_STYLE_NORMAL = 0,
	R5_LEVEL_STYLE_DISABLED = 1,
		// Greyed, for example while disconnected
	R5_LEVEL_STYLE_OFF = 2
		// Outlines only
};

// Draws `which` inside `frame` of `view` with `level` of its four steps
// filled (0 to 4; values outside are clamped), in `foreground`, with the
// empty steps a faint tint of it over `background`. The view's drawing
// state is the same afterwards.
void r5_level_glyph_draw(BView* view, r5_level_glyph which, BRect frame,
	int32 level, rgb_color foreground, rgb_color background,
	r5_level_style style);


#endif
