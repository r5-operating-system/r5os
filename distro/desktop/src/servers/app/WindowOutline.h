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
#ifndef WINDOW_OUTLINE_H
#define WINDOW_OUTLINE_H


/*!	The round outline of a window (docs/APP-SERVER-COMPOSITOR.md, "The window
	outline"). A window's outline is a few rectangles with a radius per
	corner: from its decorator (Decorator::GetOutline(), decorator ABI 2) or,
	for a program's own window, from set_window_corner_radius() in libr5ui.

	From it come two regions in screen coordinates:
	- cut: the corner pixels that are not part of the window. They leave the
	  window's full region and its content region, so the windows and the
	  desktop behind draw there, in the CPU mode and in the GPU mode.
	- edge: the pixels the arc crosses (GPU mode only). They stay in the
	  window; the compositor blends them over what lies behind with the
	  coverage below. In the CPU mode there is no edge: a pixel more than half
	  covered is the window's, the rest is cut (a stepped arc).

	One formula decides both, here and in effects.comp (mode 6): a corner is a
	circle around an integer point, and a pixel's coverage is
	clamp(radius + 0.5 - distance of the pixel's centre, 0, 1).
*/


#include <math.h>

#include <Region.h>

#include "decorator/Decorator.h"


// The largest radius drawn; a larger one is drawn with this one.
#define R5_WINDOW_CORNER_MAX_RADIUS		64


struct window_corner {
	int32			centerX;	// the circle's centre, between pixels
	int32			centerY;
	int32			radius;
	clipping_rect	square;		// the radius x radius pixels of the corner
};


struct window_outline {
	window_outline()
		:
		cornerCount(0)
	{
	}

	bool IsSquare() const { return cornerCount == 0; }

	int32			cornerCount;
	window_corner	corners[R5_WINDOW_OUTLINE_MAX_RECTS * 4];
	BRegion			cut;
	BRegion			edge;
};


//! The share of pixel (x, y) that lies inside \a corner, 0 to 1.
static inline float
window_corner_coverage(const window_corner& corner, int32 x, int32 y)
{
	float dx = (float)x + 0.5f - (float)corner.centerX;
	float dy = (float)y + 0.5f - (float)corner.centerY;
	float coverage = (float)corner.radius + 0.5f - sqrtf(dx * dx + dy * dy);
	if (coverage < 0.0f)
		return 0.0f;
	if (coverage > 1.0f)
		return 1.0f;
	return coverage;
}


// Below this a pixel is outside, above 1 minus it inside (GPU mode).
static const float kWindowCornerEpsilon = 1.0f / 512.0f;


static inline void
window_outline_add_run(BRegion& region, int32 left, int32 right, int32 y)
{
	if (left > right)
		return;
	clipping_rect run;
	run.left = left;
	run.right = right;
	run.top = y;
	run.bottom = y;
	region.Include(run);
}


/*!	The corner of \a rect at \a kind (0 left top, 1 right top, 2 left bottom,
	3 right bottom) with \a radius, into \a corner. False for radius 0.
*/
static inline bool
window_outline_corner(const clipping_rect& rect, int32 kind, float radius,
	window_corner& corner)
{
	int32 width = rect.right - rect.left + 1;
	int32 height = rect.bottom - rect.top + 1;
	int32 r = (int32)floorf(radius + 0.5f);
	if (r > R5_WINDOW_CORNER_MAX_RADIUS)
		r = R5_WINDOW_CORNER_MAX_RADIUS;
	if (r > width / 2)
		r = width / 2;
	if (r > height / 2)
		r = height / 2;
	if (r <= 0)
		return false;

	bool right = kind == 1 || kind == 3;
	bool bottom = kind >= 2;
	corner.radius = r;
	corner.centerX = right ? rect.right + 1 - r : rect.left + r;
	corner.centerY = bottom ? rect.bottom + 1 - r : rect.top + r;
	corner.square.left = right ? rect.right - r + 1 : rect.left;
	corner.square.right = corner.square.left + r - 1;
	corner.square.top = bottom ? rect.bottom - r + 1 : rect.top;
	corner.square.bottom = corner.square.top + r - 1;
	return true;
}


/*!	Builds \a outline from \a count rectangles. \a blended: the GPU mode,
	where the compositor blends the edge; otherwise the arc is stepped.
	A pixel cut by one rectangle but inside another stays.
*/
static inline void
window_outline_build(const window_outline_rect* rects, int32 count,
	bool blended, window_outline& outline)
{
	outline.cornerCount = 0;
	outline.cut.MakeEmpty();
	outline.edge.MakeEmpty();
	if (count > R5_WINDOW_OUTLINE_MAX_RECTS)
		count = R5_WINDOW_OUTLINE_MAX_RECTS;

	BRegion solid;
	for (int32 i = 0; i < count; i++) {
		if (!rects[i].rect.IsValid())
			continue;
		clipping_rect rect;
		rect.left = (int32)floorf(rects[i].rect.left);
		rect.top = (int32)floorf(rects[i].rect.top);
		rect.right = (int32)floorf(rects[i].rect.right);
		rect.bottom = (int32)floorf(rects[i].rect.bottom);
		float radii[4] = { rects[i].leftTop, rects[i].rightTop,
			rects[i].leftBottom, rects[i].rightBottom };

		BRegion inside;
		inside.Set(rect);
		for (int32 kind = 0; kind < 4; kind++) {
			window_corner corner;
			if (!window_outline_corner(rect, kind, radii[kind], corner))
				continue;
			outline.corners[outline.cornerCount++] = corner;
			bool right = kind == 1 || kind == 3;
			for (int32 y = corner.square.top; y <= corner.square.bottom;
					y++) {
				// From the outer side in: cut, then edge, then inside.
				int32 cutEnd = -1;
				int32 edgeEnd = -1;
				for (int32 step = 0; step < corner.radius; step++) {
					int32 x = right ? corner.square.right - step
						: corner.square.left + step;
					float coverage = window_corner_coverage(corner, x, y);
					bool outside = blended
						? coverage <= kWindowCornerEpsilon : coverage < 0.5f;
					bool partly = blended
						&& coverage < 1.0f - kWindowCornerEpsilon;
					if (outside)
						cutEnd = step;
					else if (partly)
						edgeEnd = step;
					else
						break;
				}
				int32 edgeStart = cutEnd + 1;
				if (edgeEnd < edgeStart)
					edgeEnd = edgeStart - 1;
				clipping_rect cut;
				clipping_rect edge;
				if (right) {
					cut.left = corner.square.right - cutEnd;
					cut.right = corner.square.right;
					edge.left = corner.square.right - edgeEnd;
					edge.right = corner.square.right - edgeStart;
				} else {
					cut.left = corner.square.left;
					cut.right = corner.square.left + cutEnd;
					edge.left = corner.square.left + edgeStart;
					edge.right = corner.square.left + edgeEnd;
				}
				window_outline_add_run(outline.cut, cut.left, cut.right, y);
				window_outline_add_run(outline.edge, edge.left, edge.right,
					y);
				BRegion row;
				window_outline_add_run(row, cut.left, cut.right, y);
				window_outline_add_run(row, edge.left, edge.right, y);
				inside.Exclude(&row);
			}
		}
		solid.Include(&inside);
	}

	outline.edge.Exclude(&solid);
	outline.cut.Exclude(&solid);
	outline.cut.Exclude(&outline.edge);
}


#endif	// WINDOW_OUTLINE_H
