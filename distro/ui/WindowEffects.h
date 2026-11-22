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
#ifndef _WINDOW_EFFECTS_H
#define _WINDOW_EFFECTS_H


#include <SupportDefs.h>


class BView;
class BWindow;


namespace BAppearanceKit {


// The user's window effects (Settings and the Appearance program). They
// show in the GPU mode only; the CPU mode accepts and stores them.
enum {
	B_WINDOW_SHADOW			= 0x00000001,
	B_WINDOW_FROSTED_GLASS	= 0x00000002,
	B_WINDOW_ANIMATIONS		= 0x00000004
};

uint32		window_effects();
status_t	set_window_effects(uint32 effects);

// A program asks for glass behind its own content. Opt-in. The window
// must exist on the screen side (after the BWindow constructor); a view
// must be attached to a window, and that window must be locked.
status_t	set_window_glass(BWindow* window, bool glass);
status_t	set_view_glass(BView* view, bool glass);

// One step for both the shadow and the glass. The function has the name
// of the type, as stat() has that of struct stat: where the type is meant
// after this point, write "enum window_effect_strength".
enum window_effect_strength {
	B_WINDOW_EFFECTS_QUIET		= 1,
	B_WINDOW_EFFECTS_BALANCED	= 2,
	B_WINDOW_EFFECTS_GLASSY		= 3
};

window_effect_strength	window_effect_strength();
status_t	set_window_effect_strength(
				enum window_effect_strength strength);

// M12 (Q13): true while the GPU draws the letters (Skia, its own subpixel
// filter). Painter's average weight then has no effect. False in the CPU
// mode and when app_server cannot be asked.
bool		gpu_draws_text();

// A program rounds the corners of its own window, radius in pixels (0:
// square, the default; at most 64). The corners leave the window in both
// modes: blended in the GPU mode, stepped in the CPU mode. Content in a
// corner is clipped. For a window whose decorator draws round corners
// itself, the decorator's outline counts. Scale the radius with the font
// size, like other R5 measures (18 at 12 points).
status_t	set_window_corner_radius(BWindow* window, float radius);


}	// namespace BAppearanceKit


#endif	// _WINDOW_EFFECTS_H
