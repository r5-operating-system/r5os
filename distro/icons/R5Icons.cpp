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
 *
 * The pictures in standard_icons.inc are unmodified Tracker HVIF.
 * Their license is distro/icons/TRACKER_ICON_NOTICE.txt, not this header.
 * The pictures in lock_icons.inc stay under distro/icons/HAIKU_ICON_NOTICE.txt.
 */


#include "R5Icons.h"

#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Font.h>
#include <IconUtils.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Shape.h>
#include <View.h>

#include <algorithm>
#include <math.h>
#include <new>
#include <string.h>


#include "standard_icons.inc"
#include "lock_icons.inc"


static int32
icon_pixels(float side)
{
	if (side < 1.0f)
		return 0;
	return (int32)side;
}


static BBitmap*
bitmap_from_vector(const uint8* data, size_t size, int32 pixels)
{
	if (data == NULL || size == 0 || pixels < 1)
		return NULL;

	BBitmap* bitmap = new BBitmap(BRect(0, 0, pixels - 1, pixels - 1),
		B_RGBA32);
	if (bitmap->InitCheck() != B_OK
		|| BIconUtils::GetVectorIcon(data, size, bitmap) != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


static float
icon_side_for_button(float buttonHeight)
{
	float left;
	float top;
	float right;
	float bottom;
	be_control_look->GetInsets(BControlLook::B_BUTTON_FRAME,
		BControlLook::B_BUTTON_BACKGROUND, 0, left, top, right, bottom);
	// One pixel of air so the glyph does not sit on the button border.
	return buttonHeight - top - bottom - 2.0f;
}


static status_t
apply_button_bitmap(BButton* button, BBitmap* icon, float buttonHeight)
{
	if (button == NULL || buttonHeight < 1.0f) {
		delete icon;
		return B_BAD_VALUE;
	}

	status_t status = B_ERROR;
	if (icon != NULL)
		status = button->SetIcon(icon);
	delete icon;

	BSize box(buttonHeight, buttonHeight);
	button->SetExplicitMinSize(box);
	button->SetExplicitMaxSize(box);
	button->SetExplicitPreferredSize(box);
	return status;
}


BBitmap*
r5_icon_bitmap(r5_icon which, float side)
{
	const uint8* data = NULL;
	size_t size = 0;
	switch (which) {
		case R5_ICON_BACK:
			data = kBackIcon;
			size = kBackIconSize;
			break;
		case R5_ICON_FORWARD:
			data = kForwardIcon;
			size = kForwardIconSize;
			break;
		case R5_ICON_UP:
			data = kUpIcon;
			size = kUpIconSize;
			break;
		case R5_ICON_HOME:
			data = kHomeIcon;
			size = kHomeIconSize;
			break;
		case R5_ICON_UNLOCKED:
			data = kUnlockedIcon;
			size = kUnlockedIconSize;
			break;
		case R5_ICON_LOCKED:
			data = kLockedIcon;
			size = kLockedIconSize;
			break;
		default:
			return NULL;
	}
	return bitmap_from_vector(data, size, icon_pixels(side));
}


BBitmap*
r5_vector_icon_bitmap(const uint8* data, size_t size, float side)
{
	return bitmap_from_vector(data, size, icon_pixels(side));
}


BBitmap*
r5_file_icon_bitmap(const char* path, float side)
{
	int32 pixels = icon_pixels(side);
	if (path == NULL || path[0] == '\0' || pixels < 1)
		return NULL;

	BNode node(path);
	if (node.InitCheck() != B_OK)
		return NULL;

	BBitmap* bitmap = new BBitmap(BRect(0, 0, pixels - 1, pixels - 1),
		B_RGBA32);
	BNodeInfo info(&node);
	if (bitmap->InitCheck() != B_OK
		|| info.GetTrackerIcon(bitmap, static_cast<icon_size>(pixels))
			!= B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


BBitmap*
r5_mime_icon_bitmap(const char* type, float side)
{
	if (type == NULL || type[0] == '\0')
		return NULL;

	BMimeType mime(type);
	uint8* data = NULL;
	size_t size = 0;
	if (mime.InitCheck() != B_OK || mime.GetIcon(&data, &size) != B_OK
		|| data == NULL || size == 0) {
		delete[] data;
		return NULL;
	}

	BBitmap* bitmap = bitmap_from_vector(data, size, icon_pixels(side));
	delete[] data;
	return bitmap;
}


status_t
r5_button_set_icon(BButton* button, r5_icon which, float buttonHeight)
{
	float side = icon_side_for_button(buttonHeight);
	return apply_button_bitmap(button, r5_icon_bitmap(which, side),
		buttonHeight);
}


status_t
r5_button_set_vector(BButton* button, const uint8* data, size_t size,
	float buttonHeight)
{
	float side = icon_side_for_button(buttonHeight);
	return apply_button_bitmap(button,
		r5_vector_icon_bitmap(data, size, side), buttonHeight);
}


status_t
r5_button_set_file_icon(BButton* button, const char* path,
	float buttonHeight)
{
	float side = icon_side_for_button(buttonHeight);
	return apply_button_bitmap(button, r5_file_icon_bitmap(path, side),
		buttonHeight);
}


status_t
r5_button_set_mime_icon(BButton* button, const char* type,
	float buttonHeight)
{
	float side = icon_side_for_button(buttonHeight);
	return apply_button_bitmap(button, r5_mime_icon_bitmap(type, side),
		buttonHeight);
}


status_t
r5_button_set_lock(BButton* button, bool locked, float buttonHeight)
{
	return r5_button_set_icon(button,
		locked ? R5_ICON_LOCKED : R5_ICON_UNLOCKED, buttonHeight);
}


// #pragma mark - line glyphs


// The glyphs are drawn on a 24 x 24 grid, with a 2.2 unit pen and round
// ends. The shapes follow the common open line symbols for power,
// restart, clock, and show or hide password.
static const float kGlyphGrid = 24.0f;


struct glyph_space {
	BRect	frame;
	float	scale;

	BPoint Map(float x, float y) const
	{
		return BPoint(frame.left + x * scale, frame.top + y * scale);
	}

	float Size(float v) const
	{
		return v * scale;
	}
};


static void
glyph_arc(BView* view, const glyph_space& space, float cx, float cy,
	float radius, float startAngle, float arcAngle)
{
	// One open polygon in the same coordinates as glyph_line(), so a ring
	// and a line through its center line up. StrokeArc() places the arc
	// by its bounding rectangle, half a pixel off from a line.
	const int32 kMaxPoints = 65;
	int32 count = (int32)ceilf(fabsf(arcAngle) / 360.0f * 64) + 1;
	if (count < 2)
		count = 2;
	if (count > kMaxPoints)
		count = kMaxPoints;
	BPoint points[kMaxPoints];
	for (int32 i = 0; i < count; i++) {
		float angle = (startAngle + arcAngle * i / (count - 1)) * M_PI / 180;
		points[i] = space.Map(cx + radius * cosf(angle),
			cy - radius * sinf(angle));
	}
	view->StrokePolygon(points, count, fabsf(arcAngle) >= 360.0f);
}


static void
glyph_line(BView* view, const glyph_space& space, float x1, float y1,
	float x2, float y2)
{
	view->StrokeLine(space.Map(x1, y1), space.Map(x2, y2));
}


static void
glyph_eye(BView* view, const glyph_space& space)
{
	// The outline is two arcs that meet at the corners (1, 12) and
	// (23, 12): the upper lid reaches y 4.5, the lower lid y 19.5. Arcs,
	// because a stroked BShape is not drawn on every app_server path.
	glyph_arc(view, space, 12, 16.32f, 11.82f, 21.4f, 137.2f);
	glyph_arc(view, space, 12, 7.68f, 11.82f, 201.4f, 137.2f);

	glyph_arc(view, space, 12, 12, 3, 0, 360);
}


static void
glyph_box(BView* view, const glyph_space& space, float left, float top,
	float right, float bottom)
{
	BPoint points[4] = { space.Map(left, top), space.Map(right, top),
		space.Map(right, bottom), space.Map(left, bottom) };
	view->StrokePolygon(points, 4, true);
}


static void
glyph_wifi(BView* view, const glyph_space& space, int32 steps)
{
	// the dot and three arcs of the mockup: M2.5 9a14 14 0 0 1 19 0,
	// M5.5 12.5a9.5 9.5 0 0 1 13 0, M8.7 16a5 5 0 0 1 6.6 0, and a dot of
	// 1.4 at (12, 19.2); \a steps of them, the dot first
	if (steps >= 1) {
		BPoint center = space.Map(12, 19.2f);
		float radius = space.Size(1.4f);
		view->FillEllipse(center, radius, radius);
	}
	if (steps >= 2)
		glyph_arc(view, space, 12, 19.76f, 5, 48.7f, 82.6f);
	if (steps >= 3)
		glyph_arc(view, space, 12, 19.43f, 9.5f, 46.8f, 86.4f);
	if (steps >= 4)
		glyph_arc(view, space, 12, 19.28f, 14, 47.3f, 85.4f);
}


static void
glyph_paint(BView* view, r5_glyph which, BRect frame)
{
	glyph_space space;
	float side = std::min(frame.Width(), frame.Height());
	space.scale = side / kGlyphGrid;
	space.frame = BRect(frame.left + (frame.Width() - side) / 2,
		frame.top + (frame.Height() - side) / 2, 0, 0);
	space.frame.right = space.frame.left + side;
	space.frame.bottom = space.frame.top + side;

	view->SetPenSize(std::max(1.0f, space.Size(2.2f)));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	switch (which) {
		case R5_GLYPH_POWER:
			// An open ring with the gap at the top, and the stem.
			glyph_arc(view, space, 12, 13, 9, 135, 270);
			glyph_line(view, space, 12, 2, 12, 12);
			break;

		case R5_GLYPH_RESTART:
			// A ring that is open at the upper right, with the arrow
			// head on its upper end.
			glyph_arc(view, space, 12, 12, 9.5f, 30, 305);
			glyph_line(view, space, 20.5f, 3, 20.5f, 8.5f);
			glyph_line(view, space, 20.5f, 8.5f, 15, 8.5f);
			break;

		case R5_GLYPH_CLOCK:
			glyph_arc(view, space, 12, 12, 10, 0, 360);
			glyph_line(view, space, 12, 6, 12, 12);
			glyph_line(view, space, 12, 12, 16, 14);
			break;

		case R5_GLYPH_EYE:
			glyph_eye(view, space);
			break;

		case R5_GLYPH_EYE_OFF:
		{
			// The eye, with a band cut out around the slash, then the
			// slash itself.
			view->PushState();
			BShape band;
			float w = 2.4f;
			band.MoveTo(space.Map(1 - w, 1 + w));
			band.LineTo(space.Map(1 + w, 1 - w));
			band.LineTo(space.Map(23 + w, 23 - w));
			band.LineTo(space.Map(23 - w, 23 + w));
			band.Close();
			view->ClipToInverseShape(&band);
			glyph_eye(view, space);
			view->PopState();
			glyph_line(view, space, 2, 2, 22, 22);
			break;
		}

		case R5_GLYPH_LOG_OUT:
			// An open door frame on the left, an arrow out to the right.
			glyph_line(view, space, 9, 3, 4, 3);
			glyph_line(view, space, 4, 3, 4, 21);
			glyph_line(view, space, 4, 21, 9, 21);
			glyph_line(view, space, 9, 12, 21, 12);
			glyph_line(view, space, 16, 7, 21, 12);
			glyph_line(view, space, 21, 12, 16, 17);
			break;

		case R5_GLYPH_BLUETOOTH:
		{
			// The Bluetooth rune: the stem, and the two arrow heads that
			// cross it, as one line, in the tray's weight.
			view->SetPenSize(std::max(1.0f, space.Size(1.6f)));
			BPoint points[6] = {
				space.Map(7, 7.5f), space.Map(17, 16.5f), space.Map(12, 21),
				space.Map(12, 3), space.Map(17, 7.5f), space.Map(7, 16.5f)
			};
			view->StrokePolygon(points, 6, false);
			break;
		}

		// The four face button symbols of a PlayStation-style game
		// controller (docs/GAME-CONTROLLERS.md): plain geometric shapes in
		// the same pen, no logo.
		case R5_GLYPH_CROSS:
			glyph_line(view, space, 5, 5, 19, 19);
			glyph_line(view, space, 19, 5, 5, 19);
			break;

		case R5_GLYPH_CIRCLE:
			glyph_arc(view, space, 12, 12, 8, 0, 360);
			break;

		case R5_GLYPH_SQUARE:
		{
			BPoint points[4] = {
				space.Map(5, 5), space.Map(19, 5), space.Map(19, 19),
				space.Map(5, 19)
			};
			view->StrokePolygon(points, 4, true);
			break;
		}

		case R5_GLYPH_TRIANGLE:
		{
			BPoint points[3] = {
				space.Map(12, 4), space.Map(20.5f, 19), space.Map(3.5f, 19)
			};
			view->StrokePolygon(points, 3, true);
			break;
		}

		// The Deskbar tray symbols: a pen of 2 on the grid, as the mockup
		// (Design canvas "R5 Deskbar styles", 22 Nov 2026) draws them
		case R5_GLYPH_WIFI:
			view->SetPenSize(std::max(1.0f, space.Size(1.6f)));
			glyph_wifi(view, space, 4);
			break;

		case R5_GLYPH_VOLUME:
		case R5_GLYPH_VOLUME_OFF:
		{
			view->SetPenSize(std::max(1.0f, space.Size(1.6f)));
			// the speaker: M4 9.5h4l5-4v13l-5-4H4z, filled
			BPoint body[6] = {
				space.Map(4, 9.5f), space.Map(8, 9.5f), space.Map(13, 5.5f),
				space.Map(13, 18.5f), space.Map(8, 14.5f), space.Map(4, 14.5f)
			};
			view->FillPolygon(body, 6);
			if (which == R5_GLYPH_VOLUME) {
				// one wave: M16.5 9a4.5 4.5 0 0 1 0 6
				glyph_arc(view, space, 13.15f, 12, 4.5f, -41.8f, 83.6f);
			} else {
				// muted: a cross where the wave was
				glyph_line(view, space, 16, 9.5f, 21, 14.5f);
				glyph_line(view, space, 21, 9.5f, 16, 14.5f);
			}
			break;
		}

		case R5_GLYPH_NETWORK:
		case R5_GLYPH_NETWORK_OFF:
		{
			// three boxes on a wire, the common wired-network symbol; a
			// thinner line and larger boxes, so they stay open at tray size
			// (external review A5)
			view->SetPenSize(std::max(1.0f, space.Size(1.6f)));
			glyph_box(view, space, 8.5f, 2, 15.5f, 9);
			glyph_box(view, space, 2, 15, 9, 22);
			glyph_box(view, space, 15, 15, 22, 22);
			glyph_line(view, space, 12, 9, 12, 12);
			glyph_line(view, space, 5.5f, 12, 18.5f, 12);
			glyph_line(view, space, 5.5f, 12, 5.5f, 15);
			glyph_line(view, space, 18.5f, 12, 18.5f, 15);
			if (which == R5_GLYPH_NETWORK_OFF)
				glyph_line(view, space, 2.5f, 2.5f, 21.5f, 21.5f);
			break;
		}

		case R5_GLYPH_AIRPLAY:
		{
			// the tray's line weight, 1.6 (external review A5)
			view->SetPenSize(std::max(1.0f, space.Size(1.6f)));
			// a screen open at the bottom, and the triangle under it
			BPoint screen[6] = {
				space.Map(6, 17), space.Map(3, 17), space.Map(3, 4),
				space.Map(21, 4), space.Map(21, 17), space.Map(18, 17)
			};
			view->StrokePolygon(screen, 6, false);
			BPoint triangle[3] = {
				space.Map(12, 14.5f), space.Map(17, 20.5f), space.Map(7, 20.5f)
			};
			view->StrokePolygon(triangle, 3, true);
			break;
		}

		case R5_GLYPH_MAIL:
		case R5_GLYPH_MAIL_NEW:
		{
			view->SetPenSize(std::max(1.0f, space.Size(1.6f)));
			float right = which == R5_GLYPH_MAIL_NEW ? 19.5f : 21.5f;
			float top = which == R5_GLYPH_MAIL_NEW ? 7.5f : 5.5f;
			glyph_box(view, space, 2.5f, top, right, 19.5f);
			BPoint flap[3] = {
				space.Map(2.5f, top + 1.5f), space.Map((2.5f + right) / 2,
					top + 7.5f), space.Map(right, top + 1.5f)
			};
			view->StrokePolygon(flap, 3, false);
			if (which == R5_GLYPH_MAIL_NEW) {
				// a new message: a filled dot at the corner
				BPoint center = space.Map(19.5f, 5.5f);
				float radius = space.Size(3.5f);
				view->FillEllipse(center, radius, radius);
			}
			break;
		}

		case R5_GLYPH_SUN:
		{
			// the mockup's sun: a ring of 4 and eight rays of 2
			view->SetPenSize(std::max(1.0f, space.Size(2.2f)));
			glyph_arc(view, space, 12, 12, 4, 0, 360);
			glyph_line(view, space, 12, 2, 12, 4);
			glyph_line(view, space, 12, 20, 12, 22);
			glyph_line(view, space, 4.9f, 4.9f, 6.3f, 6.3f);
			glyph_line(view, space, 17.7f, 17.7f, 19.1f, 19.1f);
			glyph_line(view, space, 2, 12, 4, 12);
			glyph_line(view, space, 20, 12, 22, 12);
			glyph_line(view, space, 4.9f, 19.1f, 6.3f, 17.7f);
			glyph_line(view, space, 17.7f, 6.3f, 19.1f, 4.9f);
			break;
		}

		case R5_GLYPH_MOON:
		{
			// the mockup's crescent: M20 14.5 A8 8 0 0 1 9.5 4
			// a8 8 0 1 0 10.5 10.5 z
			view->SetPenSize(std::max(1.0f, space.Size(2.2f)));
			float radius = space.Size(8);
			BShape moon;
			moon.MoveTo(space.Map(20, 14.5f));
			moon.ArcTo(radius, radius, 0, false, false, space.Map(9.5f, 4));
			moon.ArcTo(radius, radius, 0, true, true, space.Map(20, 14.5f));
			moon.Close();
			view->StrokeShape(&moon);
			break;
		}

		default:
			break;
	}
}


// Paints the glyph into `paint` of a new transparent B_RGBA32 bitmap of
// `bounds`. The coverage stays in the alpha channel.
static BBitmap*
glyph_render(r5_glyph which, BRect paint, BRect bounds, rgb_color color)
{
	// The last glyph of enum r5_glyph; update it with every new glyph.
	if (which < R5_GLYPH_POWER || which > R5_GLYPH_MOON)
		return NULL;

	BBitmap* bitmap = new(std::nothrow) BBitmap(bounds,
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (bitmap == NULL || bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}
	memset(bitmap->Bits(), 0, bitmap->BitsLength());

	BView* view = new BView(bounds, "glyph", B_FOLLOW_NONE, 0);
	bitmap->AddChild(view);
	if (bitmap->Lock()) {
		color.alpha = 255;
		view->SetHighColor(color);
		// Composite keeps the coverage in the alpha channel, so the
		// edges stay smooth on any background.
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
		glyph_paint(view, which, paint);
		view->Sync();
		bitmap->RemoveChild(view);
		bitmap->Unlock();
	} else
		bitmap->RemoveChild(view);
	delete view;
	return bitmap;
}


void
r5_glyph_draw(BView* view, r5_glyph which, BRect frame, rgb_color color)
{
	if (view == NULL || !frame.IsValid())
		return;

	color.alpha = 255;

	// The glyph is painted into a bitmap first and drawn from there. Drawn
	// straight into the view, its shapes take the subpixel antialiasing
	// of the CPU mode (app_server Painter, with the subpixel setting on)
	// and get colored fringes; the bitmap's alpha composite does not.
	BPoint origin(floorf(frame.left), floorf(frame.top));
	BRect bounds(0, 0, ceilf(frame.right) - origin.x,
		ceilf(frame.bottom) - origin.y);
	BBitmap* bitmap = glyph_render(which,
		frame.OffsetByCopy(-origin.x, -origin.y), bounds, color);
	if (bitmap != NULL) {
		view->PushState();
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		view->DrawBitmap(bitmap, origin);
		view->PopState();
		delete bitmap;
		return;
	}

	view->PushState();
	view->SetHighColor(color);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	glyph_paint(view, which, frame);
	view->PopState();
}


BBitmap*
r5_glyph_bitmap(r5_glyph which, float side, rgb_color color)
{
	int32 pixels = icon_pixels(side);
	if (pixels < 1)
		return NULL;

	return glyph_render(which, BRect(0, 0, pixels, pixels),
		BRect(0, 0, pixels - 1, pixels - 1), color);
}


status_t
r5_button_set_glyph(BButton* button, r5_glyph which)
{
	if (button == NULL)
		return B_BAD_VALUE;

	BFont font;
	button->GetFont(&font);
	float side = floorf(font.Size() * 1.15f + 0.5f);
	BBitmap* glyph = r5_glyph_bitmap(which, side,
		ui_color(B_CONTROL_TEXT_COLOR));
	if (glyph == NULL)
		return B_NO_MEMORY;

	status_t status = button->SetIcon(glyph);
	delete glyph;
	return status;
}


// #pragma mark - Level glyphs


static const int32 kLevelSteps = 4;


static rgb_color
level_tint(rgb_color foreground, rgb_color background, float amount)
{
	// amount is the share of the foreground color (R5WiFiGlyphs.h used the
	// same mix before it moved here).
	return mix_color(background, foreground, (uint8)(255 * amount));
}


static void
level_colors(rgb_color foreground, rgb_color background, r5_level_style style,
	rgb_color& filled, rgb_color& empty)
{
	filled = foreground;
	empty = level_tint(foreground, background, 0.25f);
	if (style == R5_LEVEL_STYLE_DISABLED) {
		filled = level_tint(foreground, background, 0.45f);
		empty = level_tint(foreground, background, 0.2f);
	} else if (style == R5_LEVEL_STYLE_OFF)
		filled = empty = level_tint(foreground, background, 0.45f);
}


static void
level_signal(BView* view, BRect frame, int32 level, rgb_color filled,
	rgb_color empty, bool outline)
{
	// The geometry of DrawWiFiSignalBars() as it shipped, so the Wi-Fi list
	// and the Deskbar replicant draw the same pixels after the move.
	float gap = std::max(1.0f, floorf(frame.Width() / 12));
	float width = floorf((frame.Width() + 1 - gap * (kLevelSteps - 1))
		/ kLevelSteps);
	if (width < 1)
		width = 1;
	float totalWidth = width * kLevelSteps + gap * (kLevelSteps - 1);
	float left = floorf(frame.left + (frame.Width() + 1 - totalWidth) / 2);
	float bottom = floorf(frame.bottom);
	float height = frame.Height() + 1;

	for (int32 i = 0; i < kLevelSteps; i++) {
		float barHeight = std::max(2.0f,
			floorf(height * (i + 1) / kLevelSteps));
		BRect bar(left, bottom - barHeight + 1, left + width - 1, bottom);
		view->SetHighColor(i < level ? filled : empty);
		if (outline)
			view->StrokeRect(bar);
		else
			view->FillRect(bar);
		left += width + gap;
	}
}


static void
level_battery(BView* view, BRect frame, int32 level, rgb_color filled,
	rgb_color empty, bool outline)
{
	// A body twice as wide as it is high, centered, with a cap on the right
	// and four segments inside a one-pixel margin.
	float height = floorf(std::min(frame.Height() + 1,
		(frame.Width() + 1) / 2));
	if (height < 5)
		height = 5;
	float capWidth = std::max(1.0f, floorf(height / 5));
	float bodyWidth = floorf(height * 2) - capWidth;
	float left = floorf(frame.left + (frame.Width() + 1 - bodyWidth
		- capWidth) / 2);
	float top = floorf(frame.top + (frame.Height() + 1 - height) / 2);
	BRect body(left, top, left + bodyWidth - 1, top + height - 1);
	float capHeight = std::max(1.0f, floorf(height / 2));
	float capTop = floorf(body.top + (height - capHeight) / 2);
	BRect cap(body.right + 1, capTop, body.right + capWidth,
		capTop + capHeight - 1);

	// The outline is always in the filled color, so an empty battery still
	// shows; only the segments go faint.
	view->SetHighColor(filled);
	view->StrokeRect(body);
	view->FillRect(cap);

	BRect inside = body.InsetByCopy(2, 2);
	if (!inside.IsValid())
		return;
	float gap = 1;
	float segment = floorf((inside.Width() + 1 - gap * (kLevelSteps - 1))
		/ kLevelSteps);
	if (segment < 1)
		segment = 1;
	float x = inside.left;
	for (int32 i = 0; i < kLevelSteps; i++) {
		BRect cell(x, inside.top, std::min(x + segment - 1, inside.right),
			inside.bottom);
		if (cell.IsValid()) {
			view->SetHighColor(i < level ? filled : empty);
			if (outline)
				view->StrokeRect(cell);
			else
				view->FillRect(cell);
		}
		x += segment + gap;
	}
}


static void
level_symbol(BView* view, r5_level_glyph which, BRect frame, int32 level,
	rgb_color filled, rgb_color empty)
{
	// line symbols in the pen of the tray glyphs: the empty steps faint
	glyph_space space;
	float side = std::min(frame.Width(), frame.Height());
	space.scale = side / kGlyphGrid;
	space.frame = BRect(frame.left + (frame.Width() - side) / 2,
		frame.top + (frame.Height() - side) / 2, 0, 0);
	space.frame.right = space.frame.left + side;
	space.frame.bottom = space.frame.top + side;
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	view->SetPenSize(std::max(1.0f, space.Size(2)));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	if (which == R5_LEVEL_WIFI) {
		// the faint arcs first, then the filled ones over them
		view->SetHighColor(empty);
		glyph_wifi(view, space, kLevelSteps);
		view->SetHighColor(filled);
		glyph_wifi(view, space, level);
		return;
	}

	// the battery of the mockup: a body of 17 x 10 with corners of 2, a cap
	// line, and a fill of up to 10 x 5 inside
	view->SetHighColor(filled);
	BRect body(space.Map(2.5f, 7).x, space.Map(2.5f, 7).y,
		space.Map(19.5f, 17).x, space.Map(19.5f, 17).y);
	float radius = space.Size(2);
	view->StrokeRoundRect(body, radius, radius);
	glyph_line(view, space, 21.5f, 10.5f, 21.5f, 13.5f);
	if (level > 0) {
		float width = 12.0f * level / kLevelSteps;
		BRect fill(space.Map(5, 9.5f).x, space.Map(5, 9.5f).y,
			space.Map(5 + width, 14.5f).x, space.Map(5, 14.5f).y);
		float inner = space.Size(1);
		view->FillRoundRect(fill, inner, inner);
	}
}


void
r5_level_glyph_draw(BView* view, r5_level_glyph which, BRect frame,
	int32 level, rgb_color foreground, rgb_color background,
	r5_level_style style)
{
	if (view == NULL || !frame.IsValid())
		return;
	if (level < 0)
		level = 0;
	if (level > kLevelSteps)
		level = kLevelSteps;

	rgb_color filled;
	rgb_color empty;
	level_colors(foreground, background, style, filled, empty);
	bool outline = style == R5_LEVEL_STYLE_OFF;

	view->PushState();
	view->SetDrawingMode(B_OP_COPY);
	switch (which) {
		case R5_LEVEL_SIGNAL:
			level_signal(view, frame, level, filled, empty, outline);
			break;
		case R5_LEVEL_BATTERY:
			level_battery(view, frame, level, filled, empty, outline);
			break;
		case R5_LEVEL_WIFI:
		case R5_LEVEL_BATTERY_SYMBOL:
			level_symbol(view, which, frame, level, filled, empty);
			break;
	}
	view->PopState();
}
