/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * r5_level_glyph_test: the pass check of docs/BLUETOOTH.md, step 2.
 *
 * DrawWiFiSignalBars() now calls r5_level_glyph_draw(R5_LEVEL_SIGNAL). This
 * draws the old function (LegacySignalBars, below, as it shipped in
 * R5WiFiGlyphs.h) and the new call into two bitmaps for many sizes, every
 * level from -1 to 5, every style, and a light and a dark color pair, and
 * compares them byte for byte. It also checks that R5_LEVEL_BATTERY and
 * R5_GLYPH_BLUETOOTH draw something.
 *
 * Prints, and writes to the syslog, "r5_level_glyph_test: ok (<n> cases)"
 * or "r5_level_glyph_test: FAIL <reason>". Exit status 0 on ok.
 */


#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <Application.h>
#include <Bitmap.h>
#include <InterfaceDefs.h>
#include <View.h>

#include <algorithm>
#include <math.h>

#include <R5Icons.h>


// The function as it shipped in R5WiFiGlyphs.h before step 2, unchanged
// apart from its name and the style type.
static void
LegacySignalBars(BView* view, BRect frame, int32 bars,
	rgb_color foreground, rgb_color background, int style)
{
	const int32 kBars = 4;
	float gap = std::max(1.0f, floorf(frame.Width() / 12));
	float width = floorf((frame.Width() + 1 - gap * (kBars - 1)) / kBars);
	if (width < 1)
		width = 1;
	float totalWidth = width * kBars + gap * (kBars - 1);
	float left = floorf(frame.left + (frame.Width() + 1 - totalWidth) / 2);
	float bottom = floorf(frame.bottom);
	float height = frame.Height() + 1;

	rgb_color filled = foreground;
	rgb_color empty = mix_color(background, foreground, (uint8)(255 * 0.25f));
	if (style == 1) {
		filled = mix_color(background, foreground, (uint8)(255 * 0.45f));
		empty = mix_color(background, foreground, (uint8)(255 * 0.2f));
	} else if (style == 2)
		filled = empty = mix_color(background, foreground, (uint8)(255 * 0.45f));

	view->PushState();
	view->SetDrawingMode(B_OP_COPY);
	for (int32 i = 0; i < kBars; i++) {
		float barHeight = std::max(2.0f, floorf(height * (i + 1) / kBars));
		BRect bar(left, bottom - barHeight + 1, left + width - 1, bottom);
		view->SetHighColor(i < bars ? filled : empty);
		if (style == 2)
			view->StrokeRect(bar);
		else
			view->FillRect(bar);
		left += width + gap;
	}
	view->PopState();
}


class Canvas {
public:
	Canvas(BRect bounds)
		:
		fBitmap(bounds, B_BITMAP_ACCEPTS_VIEWS, B_RGBA32),
		fView(new BView(bounds, "canvas", B_FOLLOW_NONE, 0))
	{
		fBitmap.AddChild(fView);
	}

	~Canvas()
	{
		if (fBitmap.Lock()) {
			fBitmap.RemoveChild(fView);
			fBitmap.Unlock();
		}
		delete fView;
	}

	bool IsValid() { return fBitmap.InitCheck() == B_OK; }
	BView* View() { return fView; }
	BBitmap& Bitmap() { return fBitmap; }

	void Clear(rgb_color color)
	{
		fView->SetHighColor(color);
		fView->FillRect(fView->Bounds());
	}

private:
	BBitmap	fBitmap;
	BView*	fView;
};


static int sFailures = 0;


static void
fail(const char* what)
{
	if (sFailures++ < 10) {
		printf("r5_level_glyph_test: FAIL %s\n", what);
		syslog(LOG_ERR, "r5_level_glyph_test: FAIL %s", what);
	}
}


static bool
draws_something(Canvas& canvas, rgb_color background)
{
	const uint8* bits = (const uint8*)canvas.Bitmap().Bits();
	int32 length = canvas.Bitmap().BitsLength();
	for (int32 i = 0; i + 3 < length; i += 4) {
		// B_RGBA32 is stored as B, G, R, A.
		if (bits[i] != background.blue || bits[i + 1] != background.green
			|| bits[i + 2] != background.red)
			return true;
	}
	return false;
}


int
main()
{
	BApplication app("application/x-vnd.R5-r5_level_glyph_test");

	const BRect kBounds(0, 0, 63, 63);
	Canvas legacy(kBounds);
	Canvas current(kBounds);
	if (!legacy.IsValid() || !current.IsValid()) {
		fail("no bitmap");
		return 1;
	}

	const rgb_color kColors[][2] = {
		{ make_color(0, 0, 0), make_color(255, 255, 255) },
		{ make_color(230, 230, 230), make_color(43, 43, 43) },
		{ make_color(51, 102, 152), make_color(219, 219, 219) }
	};
	int32 cases = 0;

	for (int c = 0; c < 3; c++) {
		rgb_color foreground = kColors[c][0];
		rgb_color background = kColors[c][1];
		for (float width = 4; width <= 40; width += 3) {
			for (float height = 4; height <= 40; height += 5) {
				BRect frame(5.5f, 7, 5.5f + width, 7 + height);
				for (int32 level = -1; level <= 5; level++) {
					for (int style = 0; style < 3; style++) {
						legacy.Bitmap().Lock();
						legacy.Clear(background);
						LegacySignalBars(legacy.View(), frame, level,
							foreground, background, style);
						legacy.View()->Sync();
						bool legacyDrew = draws_something(legacy, background);
						legacy.Bitmap().Unlock();
						// Two blank bitmaps would compare equal: make sure the
						// old code really drew (a lit bar is never blank).
						if (level >= 1 && style == 0 && !legacyDrew) {
							char what[128];
							snprintf(what, sizeof(what), "nothing drawn:"
								" %gx%g level %d colors %d", width, height,
								(int)level, c);
							fail(what);
						}

						current.Bitmap().Lock();
						current.Clear(background);
						r5_level_glyph_draw(current.View(), R5_LEVEL_SIGNAL,
							frame, level, foreground, background,
							(r5_level_style)style);
						current.View()->Sync();
						current.Bitmap().Unlock();

						cases++;
						if (memcmp(legacy.Bitmap().Bits(),
								current.Bitmap().Bits(),
								legacy.Bitmap().BitsLength()) != 0) {
							char what[128];
							snprintf(what, sizeof(what), "signal differs:"
								" %gx%g level %d style %d colors %d", width,
								height, (int)level, style, c);
							fail(what);
						}
					}
				}
			}
		}
	}

	// The battery and the Bluetooth glyph draw something at a few sizes.
	rgb_color text = make_color(0, 0, 0);
	rgb_color paper = make_color(255, 255, 255);
	for (float side = 8; side <= 32; side += 8) {
		for (int32 level = 0; level <= 4; level++) {
			current.Bitmap().Lock();
			current.Clear(paper);
			r5_level_glyph_draw(current.View(), R5_LEVEL_BATTERY,
				BRect(2, 2, 2 + side * 2, 2 + side), level, text, paper,
				R5_LEVEL_STYLE_NORMAL);
			current.View()->Sync();
			bool drawn = draws_something(current, paper);
			current.Bitmap().Unlock();
			cases++;
			if (!drawn) {
				char what[64];
				snprintf(what, sizeof(what), "battery %g level %d is empty",
					side, (int)level);
				fail(what);
			}
		}

		BBitmap* glyph = r5_glyph_bitmap(R5_GLYPH_BLUETOOTH, side, text);
		cases++;
		bool visible = false;
		if (glyph != NULL) {
			const uint8* bits = (const uint8*)glyph->Bits();
			int32 length = glyph->BitsLength();
			for (int32 i = 3; i < length; i += 4) {
				if (bits[i] != 0) {
					visible = true;
					break;
				}
			}
		}
		delete glyph;
		if (!visible) {
			char what[64];
			snprintf(what, sizeof(what), "Bluetooth glyph %g is empty", side);
			fail(what);
		}
	}

	if (sFailures != 0) {
		printf("r5_level_glyph_test: %d of %d cases failed\n", sFailures,
			(int)cases);
		return 1;
	}

	printf("r5_level_glyph_test: ok (%d cases)\n", (int)cases);
	syslog(LOG_INFO, "r5_level_glyph_test: ok (%d cases)", (int)cases);
	return 0;
}
