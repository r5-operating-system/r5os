/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 step 5 (docs/APP-SERVER-GPU-DRAWING.md, part 6, "Programs that read
 * without Sync()"): offscreen BBitmap views in the GPU mode. One line per
 * case:
 *
 *   offscreen sync ok            draw, Sync(), read: the pixels are there
 *   offscreen cpu write ok       write the bits, draw over half, Sync()
 *   offscreen drawbitmap ok      draw, no Sync(), DrawBitmap() it into a
 *                                window, read the screen back
 *   offscreen nosync stale|fresh draw, read at once (no pass or fail)
 *
 * and "offscreen <n>/3 ok" at the end. Works the same in the CPU mode.
 */


#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <OS.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>


static const rgb_color kRed = { 220, 40, 30, 255 };
static const rgb_color kGreen = { 40, 180, 60, 255 };
static const rgb_color kBlue = { 30, 60, 200, 255 };


static bool
is(const BBitmap* bitmap, int x, int y, rgb_color color)
{
	const uint8* p = (const uint8*)bitmap->Bits() + y * bitmap->BytesPerRow()
		+ x * 4;
	return p[0] == color.blue && p[1] == color.green && p[2] == color.red;
}


static BBitmap*
make()
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, 99, 59), B_BITMAP_ACCEPTS_VIEWS,
		B_RGBA32);
	BView* view = new BView(bitmap->Bounds(), "v", B_FOLLOW_NONE, 0);
	bitmap->AddChild(view);
	return bitmap;
}


class ShowView : public BView {
public:
	ShowView(BRect frame, BBitmap* bitmap)
		: BView(frame, "show", B_FOLLOW_NONE, B_WILL_DRAW), fBitmap(bitmap) {}
	void Draw(BRect) { DrawBitmap(fBitmap, BPoint(0, 0)); }
private:
	BBitmap* fBitmap;
};


int
main()
{
	BApplication app("application/x-vnd.r5-offscreen-test");
	int passed = 0;

	// 1: draw, Sync(), read
	{
		BBitmap* bitmap = make();
		bitmap->Lock();
		BView* view = bitmap->ChildAt(0);
		view->SetHighColor(kRed);
		view->FillRect(BRect(10, 10, 89, 49));
		view->Sync();
		bool ok = is(bitmap, 50, 30, kRed);
		bitmap->Unlock();
		printf("offscreen sync %s\n", ok ? "ok" : "FAIL");
		passed += ok;
		delete bitmap;
	}

	// 2: the program writes the bits, then draws over half
	{
		BBitmap* bitmap = make();
		bitmap->Lock();
		uint8* bits = (uint8*)bitmap->Bits();
		for (int i = 0; i < 100 * 60; i++) {
			bits[i * 4 + 0] = kBlue.blue;
			bits[i * 4 + 1] = kBlue.green;
			bits[i * 4 + 2] = kBlue.red;
			bits[i * 4 + 3] = 255;
		}
		BView* view = bitmap->ChildAt(0);
		view->SetHighColor(kGreen);
		view->FillRect(BRect(0, 0, 49, 59));
		view->Sync();
		bool ok = is(bitmap, 20, 30, kGreen) && is(bitmap, 80, 30, kBlue);
		bitmap->Unlock();
		printf("offscreen cpu write %s\n", ok ? "ok" : "FAIL");
		passed += ok;
		delete bitmap;
	}

	// 3: no Sync(); app_server reads the bitmap itself in DrawBitmap()
	{
		BBitmap* bitmap = make();
		bitmap->Lock();
		BView* view = bitmap->ChildAt(0);
		view->SetHighColor(kRed);
		view->FillRect(view->Bounds());
		bitmap->Unlock();

		BWindow* window = new BWindow(BRect(200, 200, 299, 259), "offscreen",
			B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
			B_NOT_MOVABLE | B_AVOID_FOCUS);
		window->AddChild(new ShowView(BRect(0, 0, 99, 59), bitmap));
		window->Show();
		window->Lock();
		window->ChildAt(0)->Invalidate();
		window->ChildAt(0)->Sync();
		window->Unlock();
		// the compose puts it on screen within a refresh period or two
		snooze(500000);
		BScreen screen;
		BBitmap* shot = NULL;
		bool ok = false;
		if (screen.GetBitmap(&shot, false, new BRect(200, 200, 299, 259))
				== B_OK && shot != NULL) {
			ok = is(shot, 50, 30, kRed);
			delete shot;
		}
		window->Lock();
		window->Quit();
		printf("offscreen drawbitmap %s\n", ok ? "ok" : "FAIL");
		passed += ok;
		delete bitmap;
	}

	// 4: draw and read at once; Haiku promises neither
	{
		BBitmap* bitmap = make();
		bitmap->Lock();
		BView* view = bitmap->ChildAt(0);
		view->SetHighColor(kRed);
		view->FillRect(view->Bounds());
		bool fresh = is(bitmap, 50, 30, kRed);
		bitmap->Unlock();
		printf("offscreen nosync %s\n", fresh ? "fresh" : "stale");
		delete bitmap;
	}

	printf("offscreen %d/3 ok\n", passed);
	return passed == 3 ? 0 : 1;
}
