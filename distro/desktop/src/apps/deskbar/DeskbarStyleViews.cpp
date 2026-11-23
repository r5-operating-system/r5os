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


/*!	The Default (R5) look of Deskbar's views (docs/design/DESKBAR-STYLES.md):
	the layouts Vertical (board A), Horizontal (board C) and Compact (board
	B), the hover and the hollow (board D, option E), the front application
	line, and the growing pill. Every function here runs only in Default
	(R5); Classic (Haiku) keeps the code paths of the Haiku files.

	The numbers are the mockup's, in pixels at the default font size,
	scaled with R5Deskbar::Size(). Coordinates are those of TBarView, which
	sits inside the edge view (R5Deskbar::CreateEdgeView()) that draws the
	1 px edge.
*/


#include <algorithm>

#include <stdio.h>
#include <string.h>

#include <Bitmap.h>
#include <ControlLook.h>
#include <MessageRunner.h>
#include <Region.h>
#include <Screen.h>
#include <String.h>
#include <Window.h>

#include <R5Icons.h>
#include <R5UI.h>
#include <WindowEffects.h>

#include "BarApp.h"
#include "BarMenuBar.h"
#include "BarMenuTitle.h"
#include "BarSettings.h"
#include "BarView.h"
#include "BarWindow.h"
#include "DeskbarMenu.h"
#include "DeskbarStyle.h"
#include "ExpandoMenuBar.h"
#include "InlineScrollView.h"
#include "StatusView.h"
#include "StatusViewShelf.h"
#include "TeamMenuItem.h"
#include "TimeView.h"


using namespace R5Deskbar;


// #pragma mark - the measures of the mockup


// Vertical (board A): the leaf row, a tray row, the list rows, the list's
// padding (none in the corner, board A3)
static float VerticalLeafHeight() { return Size(42) + (FlatCorner() ? 1 : 0); }
static float TrayRowHeight() { return Size(34); }
static float VerticalRowHeight() { return Size(34) + (FlatCorner() ? 1 : 0); }
// the rows keep clear of the round outer corners (external review A4)
static float ListPadding()
{
	return FlatCorner() ? 0 : Rounded() ? Size(7) : Size(4);
}

// The bar (board C) is 48 high and the pill (board B) 54, with its 1 px
// edge; the content is what is inside the edge.
static float
ContentHeight(const TBarView* view)
{
	BRect edges = view->EdgeInsets();
	float total = Compact() ? Size(54) : Size(48);
	return total - edges.top - edges.bottom;
}

// the pill: padding 0 10 0 12, a 42 button, a gap of 6, separators with a
// margin of 6
static float CompactMenuWidth() { return Size(12) + Size(42) + Size(6); }
static float CompactAppsLeft() { return CompactMenuWidth() + Size(13) + Size(6); }
static float CompactAppWidth() { return Size(6) + Size(42); }
// the bar: padding 0 8, a 48 leaf, a gap of 6, a separator with a margin of 4
static float BarMenuWidth() { return Size(8) + Size(48) + Size(6); }
static float BarAppsLeft() { return BarMenuWidth() + Size(9) + Size(6); }


static void
set_size(BFont* font, float pixels, bool bold)
{
	*font = bold ? *be_bold_font : *be_plain_font;
	font->SetSize(Size(pixels));
}


// #pragma mark - the edge view


namespace {


/*!	The window's only child in Default (R5), TBarView inside it: it owns the
	1 px edge, so no child view ever paints over the line. In Classic its
	insets are 0 and TBarView covers it.
*/
class TEdgeView : public BView {
public:
	TEdgeView(BRect frame)
		:
		BView(frame, "EdgeView", B_FOLLOW_ALL_SIDES, B_WILL_DRAW)
	{
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		SetViewUIColor(B_MENU_BACKGROUND_COLOR);
	}

	virtual void Draw(BRect)
	{
		TBarView* barView = dynamic_cast<TBarView*>(ChildAt(0));
		if (barView == NULL || !Modern())
			return;
		BRect edges = barView->EdgeInsets();
		BRect bounds = Bounds();
		rgb_color color = Floating() ? EdgeColor() : EdgeLineColor();
		PushState();
		SetDrawingMode(color.alpha < 255 ? B_OP_ALPHA : B_OP_COPY);
		SetHighColor(color);
		if (edges.left > 0)
			StrokeLine(bounds.LeftTop(), bounds.LeftBottom());
		if (edges.top > 0)
			StrokeLine(bounds.LeftTop(), bounds.RightTop());
		if (edges.right > 0)
			StrokeLine(bounds.RightTop(), bounds.RightBottom());
		if (edges.bottom > 0)
			StrokeLine(bounds.LeftBottom(), bounds.RightBottom());
		PopState();
	}
};


}	// namespace


BView*
R5Deskbar::CreateEdgeView(BRect frame)
{
	return new TEdgeView(frame);
}


// #pragma mark - TBarView


BRect
TBarView::EdgeInsets() const
{
	// left, top, right, bottom: where the 1 px edge is
	if (!Modern())
		return BRect(0, 0, 0, 0);
	if (Floating())
		return BRect(1, 1, 1, 1);
	if (R5Deskbar::Layout() == kLayoutHorizontal)
		return BRect(0, Top() ? 0 : 1, 0, Top() ? 1 : 0);
	// against an edge in a corner: the sides that face the desktop
	return BRect(Left() ? 0 : 1, Top() ? 0 : 1, Left() ? 1 : 0, Top() ? 1 : 0);
}


void
TBarView::_StyleViews(BView* view, bool glass)
{
	if (view == NULL)
		return;
	bool modern = Modern();
	rgb_color surface = SurfaceColor();
	bool tray = view->Parent() == fReplicantTray && view != fReplicantTray->fTime;
	if (tray) {
		// A tray item, maybe another program's: it only follows when it took
		// Deskbar's colour (review 1, 23 Nov 2026), and nothing below it is
		// touched; it draws itself.
		rgb_color own = view->ViewColor();
		if (!_TookDeskbarColor(own))
			return;
		if (modern) {
			view->SetViewColor(surface);
			view->SetLowColor(surface);
		} else {
			// back to the tray's colour, as AdoptParentColors() gave it
			view->SetViewUIColor(B_MENU_BACKGROUND_COLOR, 1.1);
			view->SetLowUIColor(B_MENU_BACKGROUND_COLOR, 1.1);
		}
		BAppearanceKit::set_view_glass(view, glass);
		view->Invalidate();
		return;
	}
	if (modern) {
		view->SetViewColor(surface);
		view->SetLowColor(surface);
	} else if (view == fDragRegion || view->Parent() == fDragRegion) {
		// the drag region, the tray and the resize control: Haiku's tint
		// (review 2)
		view->SetViewUIColor(B_MENU_BACKGROUND_COLOR, 1.1);
		view->SetLowUIColor(B_MENU_BACKGROUND_COLOR, 1.1);
	} else {
		view->SetViewUIColor(B_MENU_BACKGROUND_COLOR);
		view->SetLowUIColor(B_MENU_BACKGROUND_COLOR);
	}
	BAppearanceKit::set_view_glass(view, glass);
	view->Invalidate();
	for (int32 i = 0; i < view->CountChildren(); i++)
		_StyleViews(view->ChildAt(i), glass);
}


bool
TBarView::_TookDeskbarColor(rgb_color color) const
{
	// the colours Deskbar's tray has had: Haiku's menu colour (also the
	// drag region's tint of it) and the surfaces of Default (R5)
	rgb_color menu = ui_color(B_MENU_BACKGROUND_COLOR);
	return color == menu || color == tint_color(menu, 1.1)
		|| color == make_color(240, 245, 252)
		|| color == make_color(0xee, 0xf1, 0xf5)
		|| color == make_color(0xee, 0xf2, 0xf8)
		|| color == make_color(0x16, 0x1a, 0x22)
		|| color == make_color(0x1c, 0x20, 0x29);
}


void
TBarView::ApplyStyle()
{
	BWindow* window = Window();
	if (window == NULL)
		return;

	static bool sStyled = false;
	bool modern = Modern();
	if (!modern && !sStyled) {
		// Classic from the start: nothing of Default (R5) was applied, so
		// nothing has to be put back (Classic stays as Haiku draws it)
		return;
	}
	sStyled = true;

	UpdateEffects();

	// the window watcher: also for Default (R5) Horizontal, whose items show
	// the window count (external review, 23 Nov 2026)
	if (fExpandoMenuBar != NULL) {
		if (WatchWindows(Vertical()))
			fExpandoMenuBar->StartMonitoringWindows();
		else
			fExpandoMenuBar->StopMonitoringWindows();
	}

	window->SetLook(modern ? B_NO_BORDER_WINDOW_LOOK
		: B_BORDERED_WINDOW_LOOK);

	// fonts of the layout (boards A, B, C)
	BFont font;
	if (modern) {
		set_size(&font, 14, false);
		fExpandoMenuBar->SetFont(&font);
	} else
		fExpandoMenuBar->SetFont(be_plain_font);
	fReplicantTray->ApplyStyle();

	// the tray symbols of R5's own items (step 5)
	r5_set_deskbar_tray_style(modern ? R5_TRAY_STYLE_SYMBOLS
		: R5_TRAY_STYLE_CLASSIC, modern ? TextColor()
			: ui_color(B_MENU_ITEM_TEXT_COLOR));

	BView* root = Parent() != NULL ? Parent() : this;
	_StyleViews(root, Glass());

	BMessage changed(R5_DESKBAR_TRAY_STYLE_CHANGED);
	changed.AddInt32("style", (int32)r5_deskbar_tray_style());
	changed.AddColor("color", r5_deskbar_tray_color());
	for (int32 i = 0; i < fReplicantTray->CountChildren(); i++) {
		BView* item = fReplicantTray->ChildAt(i);
		if (item != fReplicantTray->fTime)
			window->PostMessage(&changed, item);
	}

	// the bar and the pill are not the full screen width
	static_cast<TBarWindow*>(window)->SetSizeLimits();
	UpdatePlacement();

	using namespace BAppearanceKit;
	set_window_corner_radius(window, WindowRadius());
	enum window_shadow_kind shadow = B_WINDOW_SHADOW_DEFAULT;
	if (modern) {
		shadow = Floating() || FlatCorner() ? B_WINDOW_SHADOW_FULL
			: B_WINDOW_SHADOW_EDGE;
	}
	set_window_shadow(window, shadow);
	// the line round the arc; in the CPU mode app_server paints its stepped
	// rim (Window::_DrawProgramRim(), external review, 23 Nov 2026)
	set_window_edge_line(window, EdgeColor(), WindowRadius() > 0 ? 1 : 0);
	if (root != this)
		root->Invalidate();
	// once more a moment later: the outline changes on the application's
	// link, the drawing on the window's, and corners that come back from a
	// round outline were drawn before it (QEMU, 22 Nov: dark specks)
	BMessage repaint(kMsgRepaint);
	BMessageRunner::StartSending(BMessenger(this), &repaint, 150000, 1);
}


void
TBarView::_PlaceModernMenu()
{
	float width;
	float height;
	if (Compact()) {
		width = CompactMenuWidth();
		height = ContentHeight(this);
	} else if (!Vertical()) {
		width = BarMenuWidth();
		height = ContentHeight(this);
	} else {
		BRect edges = EdgeInsets();
		width = fBarApp->Settings()->width - edges.left - edges.right + 1;
		height = VerticalLeafHeight();
	}

	if (fBarMenuBar == NULL) {
		fBarMenuBar = new TBarMenuBar(BRect(0, 0, width - 1, height - 1),
			"BarMenuBar", this);
		AddChild(fBarMenuBar);
	}

	fBarMenuBar->RemoveSeperatorItem();
	if (State() == kMiniState && Vertical())
		fBarMenuBar->AddTeamMenu();
	else
		fBarMenuBar->RemoveTeamMenu();

	fBarMenuBar->SmartResize(width - 1, height - 1);
	fBarMenuBar->MoveTo(B_ORIGIN);
}


void
TBarView::_PlaceModernTray()
{
	if (fReplicantTray->IsHidden())
		fReplicantTray->Show();

	// twice: the rows depend on the tray's width, which the first pass
	// may only now have (after a style change, QEMU 22 Nov 2026)
	for (int32 pass = 0; pass < 2; pass++) {
		fReplicantTray->RealignReplicants();
		fDragRegion->ResizeToPreferred();
			// the modern size of the tray (TReplicantTray::GetPreferredSize())
	}
	fReplicantTray->MoveTo(0, Vertical() && !Compact() ? 1 : 0);

	if (Vertical() && !Compact()) {
		fDragRegion->MoveTo(0, fBarMenuBar->Frame().bottom + 1);
		fResizeControl->ResizeTo(gDragWidth, fDragRegion->Bounds().Height() - 2);
		fResizeControl->MoveTo(
			Left() ? fDragRegion->Bounds().right - gDragWidth : 0, 1);
		if (fResizeControl->IsHidden())
			fResizeControl->Show();
	} else {
		if (!fResizeControl->IsHidden())
			fResizeControl->Hide();
		if (!Compact()) {
			// the bar: the tray at the right end
			BRect screenFrame = (BScreen(Window())).Frame();
			float width;
			float height;
			_ModernWindowSize(screenFrame, &width, &height);
			fDragRegion->MoveTo(width - fDragRegion->Bounds().Width(), 0);
		}
	}
	// the clock: in the first row (Vertical), centred (bar and pill)
	TTimeView* time = fReplicantTray->fTime;
	float rowHeight = Vertical() && !Compact() ? TrayRowHeight()
		: fReplicantTray->Bounds().Height() + 1;
	time->MoveTo(fReplicantTray->_ModernClockLeft(),
		floorf((rowHeight - (time->Bounds().Height() + 1)) / 2));
	fDragRegion->Invalidate();
	fReplicantTray->Invalidate();
}


void
TBarView::StyleView(BView* view)
{
	// a tray item added later gets the look of the others
	if (!Modern() || view == NULL || Window() == NULL)
		return;
	_StyleViews(view, Glass());
	BMessage changed(R5_DESKBAR_TRAY_STYLE_CHANGED);
	changed.AddInt32("style", (int32)r5_deskbar_tray_style());
	changed.AddColor("color", r5_deskbar_tray_color());
	Window()->PostMessage(&changed, view);
}


void
TBarView::_PlaceModernApps()
{
	BRect screenFrame = (BScreen(Window())).Frame();
	if (State() == kMiniState && Vertical()) {
		if (!fInlineScrollView->IsHidden())
			fInlineScrollView->Hide();
		SizeWindow(screenFrame);
		PositionWindow(screenFrame);
		Invalidate();
		return;
	}

	if (fInlineScrollView->IsHidden())
		fInlineScrollView->Show();

	float height = ContentHeight(this);
	if (Compact()) {
		fExpandoMenuBar->BuildItems();
		float appsWidth = fExpandoMenuBar->ModernItemsWidth();
		float left = CompactAppsLeft() - Size(6);
			// the first item brings its own gap of 6
		fInlineScrollView->DetachScrollers();
		fInlineScrollView->MoveTo(left, 0);
		fInlineScrollView->ResizeTo(std::max(appsWidth, 1.0f) - 1, height - 1);
		fExpandoMenuBar->MoveTo(0, 0);
		fExpandoMenuBar->ResizeTo(std::max(appsWidth, 1.0f) - 1, height - 1);
		if (appsWidth <= 0 && !fInlineScrollView->IsHidden())
			fInlineScrollView->Hide();
		float trayLeft = appsWidth > 0 ? left + appsWidth + Size(6 + 13 + 6)
			: CompactAppsLeft();
		fDragRegion->MoveTo(trayLeft, 0);
		SizeWindow(screenFrame);
		PositionWindow(screenFrame);
		Invalidate();
		return;
	}

	BRect expandoFrame(0, 0, 0, 0);
	if (Vertical()) {
		expandoFrame.left = 0;
		expandoFrame.top = fDragRegion->Frame().bottom + 1 + ListPadding();
		BRect edges = EdgeInsets();
		expandoFrame.right = fBarApp->Settings()->width - edges.left
			- edges.right;
	} else {
		expandoFrame.left = BarAppsLeft();
		expandoFrame.top = 0;
		expandoFrame.right = fDragRegion->Frame().left - 1;
	}

	fInlineScrollView->DetachScrollers();
	fInlineScrollView->MoveTo(expandoFrame.LeftTop());
	fInlineScrollView->ResizeTo(expandoFrame.Width(), Vertical()
		? screenFrame.bottom - expandoFrame.top : height - 1);
	fExpandoMenuBar->ResizeTo(expandoFrame.Width(), Vertical()
		? 0 : height - 1);
	fExpandoMenuBar->MoveTo(0, 0);
	fExpandoMenuBar->BuildItems();
	fExpandoMenuBar->SizeWindow(0);
}


void
TBarView::_ModernWindowSize(BRect screenFrame, float* _width, float* _height)
{
	// the size of TBarView (Bounds() values); the window adds the edges
	BRect edges = EdgeInsets();
	float width = 0;
	float height = 0;
	if (Compact()) {
		width = fDragRegion->Frame().right;
		height = ContentHeight(this) - 1;
	} else if (!Vertical()) {
		width = screenFrame.Width() - 2 * Margin() - edges.left - edges.right;
		height = ContentHeight(this) - 1;
	} else {
		width = fBarApp->Settings()->width - edges.left - edges.right;
		height = fDragRegion->Frame().bottom;
		if (State() != kMiniState && fExpandoMenuBar != NULL
			&& !fInlineScrollView->IsHidden()) {
			height = fInlineScrollView->Frame().top
				+ fExpandoMenuBar->Bounds().Height() + ListPadding();
		}
	}
	*_width = width;
	*_height = height;
}


void
TBarView::_DrawModern(BRect)
{
	float height = Bounds().Height();
	if (Compact()) {
		// the separators beside the applications, 30 high (board B)
		float line = Size(30);
		float top = floorf((height - line) / 2);
		float x = CompactMenuWidth() + Size(6);
		DrawSeparator(this, x, top, top + line - 1);
		if (!fInlineScrollView->IsHidden()) {
			x = fInlineScrollView->Frame().right + 1 + Size(6) + Size(6);
			DrawSeparator(this, x, top, top + line - 1);
		}
	} else if (!Vertical()) {
		// the separator after the wave pebble, 26 high (board C)
		float line = Size(26);
		float top = floorf((height - line) / 2);
		DrawSeparator(this, BarMenuWidth() + Size(4), top, top + line - 1);
	}
}


void
TBarView::StartGrow(TTeamMenuItem* item)
{
	// the pill grows and the new icon pops in (owner, 22 Nov 2026): only
	// with the compositor's effects and the "Animations" switch on
	if (!Compact() || !AnimationsAllowed() || item == NULL)
		return;
	fGrowItem = item;
	fGrowStart = system_time();
	fGrowProgress = 0;
	delete fGrowRunner;
	BMessage step(kMsgAnimate);
	fGrowRunner = new(std::nothrow) BMessageRunner(BMessenger(this), &step,
		16000);
}


float
TBarView::GrowProgress(const TTeamMenuItem* item) const
{
	if (item == NULL || item != fGrowItem)
		return -1;
	return fGrowProgress;
}


void
TBarView::_GrowStep()
{
	bigtime_t elapsed = system_time() - fGrowStart;
	float progress = (float)elapsed / (float)kGrowDuration;
	if (progress >= 1 || fGrowItem == NULL) {
		delete fGrowRunner;
		fGrowRunner = NULL;
		fGrowItem = NULL;
		fGrowProgress = -1;
	} else
		fGrowProgress = Ease(progress);

	if (!Compact())
		return;
	// only sizes change: the items keep their order
	fExpandoMenuBar->InvalidateLayout();
	float appsWidth = fExpandoMenuBar->ModernItemsWidth();
	float height = ContentHeight(this);
	fInlineScrollView->ResizeTo(std::max(appsWidth, 1.0f) - 1, height - 1);
	fExpandoMenuBar->ResizeTo(std::max(appsWidth, 1.0f) - 1, height - 1);
	float left = fInlineScrollView->Frame().left;
	fDragRegion->MoveTo(left + appsWidth + Size(6 + 13 + 6), 0);
	BRect screenFrame = (BScreen(Window())).Frame();
	SizeWindow(screenFrame);
	PositionWindow(screenFrame);
	Invalidate();
	fExpandoMenuBar->Invalidate();
}


BRect
TBarView::ChevronFrame() const
{
	// in the tray's coordinates: 30 wide, 36 high, before the padding of 10
	BRect bounds = fReplicantTray->Bounds();
	float height = Size(36);
	float top = floorf((bounds.Height() + 1 - height) / 2);
	return BRect(bounds.right - Size(10) - Size(30) + 1, top,
		bounds.right - Size(10), top + height - 1);
}


// #pragma mark - TBarMenuBar and TBarMenuTitle


void
TBarMenuBar::SetHoverAt(BPoint where)
{
	TBarMenuTitle* items[2] = { fDeskbarMenuItem, fAppListMenuItem };
	for (int32 i = 0; i < 2; i++) {
		if (items[i] == NULL)
			continue;
		bool hover = where.x >= 0 && items[i]->Frame().Contains(where);
		if (items[i]->Hover() != hover) {
			items[i]->SetHover(hover);
			Invalidate(items[i]->Frame());
		}
	}
}


void
TBarMenuTitle::_DrawModern()
{
	BMenu* menu = Menu();
	BRect frame = Frame();
	TBarMenuBar* bar = dynamic_cast<TBarMenuBar*>(menu);
	bool pebble = bar == NULL || bar->DeskbarMenuItem() == this;

	if (FlatCorner()) {
		// board A3: the leaf row has its own shade and a line under it
		menu->PushState();
		// dark: a step lighter than the surface, the line #3a404c
		menu->SetHighColor(Dark() ? make_color(0x23, 0x28, 0x33)
			: make_color(0xe3, 0xe8, 0xef));
		menu->FillRect(frame);
		menu->SetHighColor(Dark() ? make_color(0x3a, 0x40, 0x4c)
			: make_color(0xb9, 0xc2, 0xcf));
		menu->StrokeLine(frame.LeftBottom(), frame.RightBottom());
		menu->PopState();
		frame.bottom--;
	}

	BRect button;
	float radius;
	float iconSide;
	// external review A1 (the owner's version): the pill keeps its circle,
	// the vertical Deskbar's whole top strip is the button, the bar keeps
	// its rounded rectangle; none is taller than its row
	bool vertical = !Compact() && fBarView != NULL && fBarView->Vertical();
	if (Compact()) {
		// the 42 x 42 circle of the pill, fitted in the pill's height
		float side = std::min(Size(42), frame.Height() + 1 - Size(4));
		float top = frame.top + floorf((frame.Height() + 1 - side) / 2);
		button.Set(frame.left + Size(12), top, frame.left + Size(12) + side
			- 1, top + side - 1);
		radius = std::min(ButtonRadius(), floorf(side / 2));
		iconSide = std::min(Size(30), side - Size(8));
	} else if (vertical) {
		// the strip above the separator, with the Deskbar's top corners
		button = frame;
		// inside the floating Deskbar's 1 px edge: its radius less that
		radius = std::max(0.0f, WindowRadius()
			- (fBarView != NULL ? fBarView->EdgeInsets().top : 0));
		iconSide = std::min(Size(30), frame.Height() + 1 - Size(8));
	} else {
		// the bar's leaf: 48 x 40, corners of 10, never taller than the bar
		float height = std::min(Size(40), frame.Height() + 1 - Size(2));
		float top = frame.top + floorf((frame.Height() + 1 - height) / 2);
		button.Set(frame.left + Size(8), top, frame.left + Size(8 + 48) - 1,
			top + height - 1);
		radius = HighlightRadius();
		iconSide = std::min(Size(28), height - Size(8));
	}

	bool open = IsSelected();
	if (open || fHover) {
		menu->PushState();
		BRect shade(button);
		if (vertical) {
			// round only at the top: the strip runs on under the separator
			// by the radius and is cut at the row
			shade.bottom += radius;
			BRegion clip(frame);
			menu->ConstrainClippingRegion(&clip);
		}
		if (open)
			DrawHollow(menu, shade, radius);
		else
			DrawHover(menu, shade, radius);
		menu->PopState();
	}

	const BBitmap* icon = pebble ? Pebble(iconSide) : fIcon;
	if (icon != NULL) {
		BRect bounds = icon->Bounds();
		float width = pebble ? iconSide : bounds.Width() + 1;
		float height = pebble ? iconSide : bounds.Height() + 1;
		float x = button.left + floorf((button.Width() + 1 - width) / 2);
		float y = button.top + floorf((button.Height() + 1 - height) / 2)
			+ (open ? 1 : 0);
		BRect iconRect(x, y, x + width - 1, y + height - 1);
		if (pebble) {
			// the pebble's wave is a cut-out: a white disc under it makes
			// it a crisp white wave on the dark surface too (external
			// review A2); the disc stays inside the pebble's ring
			menu->PushState();
			menu->SetDrawingMode(B_OP_ALPHA);
			menu->SetHighColor(255, 255, 255);
			menu->FillEllipse(iconRect.InsetByCopy(floorf(width * 0.14f),
				floorf(height * 0.14f)));
			menu->PopState();
		}
		DrawIcon(menu, icon, iconRect);
	}
}


// #pragma mark - TExpandoMenuBar


void
TExpandoMenuBar::SetHoverItem(BMenuItem* item)
{
	if (item == fLastMousedOverItem)
		return;
	if (Modern() && fLastMousedOverItem != NULL
		&& IndexOf(fLastMousedOverItem) >= 0) {
		Invalidate(fLastMousedOverItem->Frame());
	}
	fLastMousedOverItem = item;
	if (Modern() && item != NULL)
		Invalidate(item->Frame());
}


float
TExpandoMenuBar::ModernItemsWidth()
{
	float width = 0;
	for (int32 i = 0; i < CountItems(); i++) {
		TTeamMenuItem* item = dynamic_cast<TTeamMenuItem*>(ItemAt(i));
		if (item == NULL)
			continue;
		float itemWidth;
		float itemHeight;
		item->ModernSize(&itemWidth, &itemHeight);
		width += itemWidth;
	}
	return width;
}


// #pragma mark - TTeamMenuItem


bool
TTeamMenuItem::IsFront() const
{
	team_id front = FrontTeam();
	return front >= 0 && fTeam != NULL
		&& fTeam->HasItem((void*)(addr_t)front);
}


void
TTeamMenuItem::_ModernContentSize(float* width, float* height)
{
	BRect edges = fBarView->EdgeInsets();
	if (Compact()) {
		float grow = fBarView->GrowProgress(this);
		*width = CompactAppWidth();
		if (grow >= 0)
			*width = floorf(*width * grow + 0.5f);
		*height = ContentHeight(fBarView) - 1;
		return;
	}
	if (fBarView->Vertical()) {
		*width = static_cast<TBarApp*>(be_app)->Settings()->width
			- edges.left - edges.right;
		*height = VerticalRowHeight() - 1;
		return;
	}
	// the bar: padding 0 12 0 8, icon 22, gap 8, the name, the count
	BFont font;
	set_size(&font, 14, IsFront());
	float labelWidth = ceilf(font.StringWidth(Label()));
	float buttonWidth = Size(8) + Size(22) + Size(8) + labelWidth + Size(12);
	if (fListedWindowCount > 1) {
		BFont small;
		set_size(&small, 12, false);
		char count[16];
		snprintf(count, sizeof(count), "%" B_PRId32, fListedWindowCount);
		buttonWidth += Size(8) + ceilf(small.StringWidth(count));
	}
	*width = buttonWidth + Size(6);
	*height = ContentHeight(fBarView) - 1;
}


void
TTeamMenuItem::_DrawModern()
{
	BMenu* menu = Menu();
	BRect frame = Frame();
	TExpandoMenuBar* bar = dynamic_cast<TExpandoMenuBar*>(menu);
	bool hover = bar != NULL && bar->HoverItem() == this;
	bool canHandle = !fBarView->Dragging()
		|| fBarView->AppCanHandleTypes(Signature());
	bool selected = _IsSelected() && canHandle;
	bool front = IsFront();

	menu->PushState();

	BRect button;
	float radius;
	float iconSide = fIcon != NULL ? fIcon->Bounds().Width() + 1 : Size(20);
	if (Compact()) {
		float side = Size(42);
		float grow = fBarView->GrowProgress(this);
		float top = frame.top + floorf((frame.Height() + 1 - side) / 2);
		button.Set(frame.left + Size(6), top, frame.left + Size(6) + side - 1,
			top + side - 1);
		radius = ButtonRadius();
		if (grow < 0) {
			if (selected)
				FillShade(menu, button, radius, 31);
			else if (hover)
				DrawHover(menu, button, radius);
		}
		// the icon pops in: from 0.3 of its size to its size
		float scale = grow >= 0 ? 0.3f + 0.7f * grow : 1.0f;
		float side30 = floorf(Size(30) * scale + 0.5f);
		if (side30 >= 2 && fIcon != NULL) {
			float x = button.left + floorf((side - side30) / 2);
			float y = button.top + floorf((side - side30) / 2);
			DrawIcon(menu, fIcon, BRect(x, y, x + side30 - 1, y + side30 - 1));
		}
		if (grow < 0) {
			// a dot under every running application, a line under the front
			float center = button.left + floorf(side / 2);
			if (front) {
				BRect line(center - Size(9), button.bottom - Size(3) + 1,
					center + Size(9) - 1, button.bottom);
				DrawFrontLine(menu, line);
			} else {
				BRect dot(center - Size(2), button.bottom - Size(1) - Size(4)
					+ 1, center + Size(2) - 1, button.bottom - Size(1));
				menu->SetDrawingMode(B_OP_OVER);
				menu->SetHighColor(TextColor());
				if (Rounded())
					menu->FillEllipse(dot);
				else
					menu->FillRect(dot);
			}
		}
		menu->PopState();
		return;
	}

	BFont font;
	set_size(&font, 14, front);
	font_height fontHeight;
	font.GetHeight(&fontHeight);
	char count[16] = "";
	if (fListedWindowCount > 1)
		snprintf(count, sizeof(count), "%" B_PRId32, fListedWindowCount);
	BFont small;
	set_size(&small, 12, false);

	if (fBarView->Vertical()) {
		// a row of 34 in the list's padding of 4 (board A)
		float padding = ListPadding();
		button.Set(frame.left + padding, frame.top, frame.right - padding,
			frame.top + Size(34) - 1);
		radius = RowRadius();
		if (FlatCorner()) {
			DrawLine(menu, BPoint(frame.left, frame.bottom),
				BPoint(frame.right, frame.bottom),
				Dark() ? make_color(0x2a, 0x2f, 0x39)
					: make_color(0xd5, 0xdb, 0xe4));
		}
	} else {
		// a button of 36 on the bar, a gap of 6 after it (board C)
		float top = frame.top + floorf((frame.Height() + 1 - Size(36)) / 2);
		button.Set(frame.left, top, frame.right - Size(6), top + Size(36) - 1);
		radius = HighlightRadius();
	}

	if (selected)
		FillShade(menu, button, radius, 31);
	else if (hover)
		DrawHover(menu, button, radius);
	else if (front && Dark()) {
		// dark: the front row rgba(255,255,255,.07) (mockup B1)
		FillShade(menu, button, radius, 18);
	}

	float iconLeft = button.left + (fBarView->Vertical() ? Size(10) : Size(8));
	if (fIcon != NULL) {
		float y = button.top + floorf((button.Height() + 1 - iconSide) / 2);
		DrawIcon(menu, fIcon, BRect(iconLeft, y, iconLeft + iconSide - 1,
			y + iconSide - 1));
	}

	float textLeft = iconLeft + iconSide
		+ (fBarView->Vertical() ? Size(10) : Size(8));
	float textRight = button.right - (fBarView->Vertical() ? Size(10)
		: Size(12));
	float countWidth = count[0] != '\0' ? ceilf(small.StringWidth(count)) : 0;
	if (countWidth > 0)
		textRight -= countWidth + Size(fBarView->Vertical() ? 10 : 8);

	float baseline = button.top + floorf((button.Height() + 1
		- (fontHeight.ascent + fontHeight.descent)) / 2 + fontHeight.ascent);
	SetTextMode(menu);
	menu->SetLowColor(SurfaceColor());
	// a row under the pointer or pressed has the full label colour, as
	// the front row (external review A4)
	menu->SetHighColor(front || hover || selected ? LabelColor()
		: RowTextColor());
	// BView::SetFont(): BMenu::SetFont() lays the menu out again, which
	// draws again (QEMU, 22 Nov: a redraw loop that ran out of memory)
	menu->BView::SetFont(&font);
	BString label(Label());
	font.TruncateString(&label, B_TRUNCATE_END,
		std::max(0.0f, textRight - textLeft));
	menu->DrawString(label.String(), BPoint(textLeft, baseline));

	if (countWidth > 0) {
		font_height smallHeight;
		small.GetHeight(&smallHeight);
		menu->BView::SetFont(&small);
		menu->SetHighColor(CountColor());
		float x = fBarView->Vertical() ? button.right - Size(10) - countWidth
			: textLeft + ceilf(font.StringWidth(label.String())) + Size(8);
		float smallBaseline = button.top + floorf((button.Height() + 1
			- (smallHeight.ascent + smallHeight.descent)) / 2
			+ smallHeight.ascent);
		menu->DrawString(count, BPoint(x, smallBaseline));
	}

	if (front) {
		if (fBarView->Vertical()) {
			// 3 x 16 at the left of the row
			float top = button.top + floorf((button.Height() + 1 - Size(16)) / 2);
			DrawFrontLine(menu, BRect(button.left + Size(2), top,
				button.left + Size(2) + Size(3) - 1, top + Size(16) - 1));
		} else {
			// 24 x 3 under the button
			float center = button.left + floorf((button.Width() + 1) / 2);
			DrawFrontLine(menu, BRect(center - Size(12),
				button.bottom - Size(3) + 1, center + Size(12) - 1,
				button.bottom));
		}
	}

	if (fBarView->Vertical()
		&& static_cast<TBarApp*>(be_app)->Settings()->superExpando
		&& fBarView->ExpandoState() && HasListedWindows()) {
		DrawExpanderArrow();
	}

	menu->PopState();
}


// #pragma mark - TReplicantTray


void
TReplicantTray::ApplyStyle()
{
	BFont font;
	if (Modern()) {
		float size = Compact() ? 17 : R5Deskbar::Layout() == kLayoutHorizontal ? 15 : 14;
		set_size(&font, size, true);
	} else
		font = *be_plain_font;
	fTime->SetFont(&font);
	fTime->ResizeToPreferred();
	fTime->CalculateTextPlacement();
}


void
TReplicantTray::_ModernPreferredSize(float* _width, float* _height)
{
	float width = 0;
	float height = 0;
	int32 count = ReplicantCount();
	float iconsWidth = 0;
	for (int32 i = 0; i < count; i++) {
		BView* view = NULL;
		fShelf->ReplicantAt(i, &view);
		if (view == NULL)
			continue;
		iconsWidth += view->Frame().Width() + 1;
	}
	bool clock = !fTime->IsHidden(fTime);
	float clockWidth = clock ? fTime->Frame().Width() + 1 : 0;

	if (Compact()) {
		// [icons, gap 6] [4, clock symbol 20, gap 9, the time, 6] gap 6
		// [chevron 30] padding 10 (board B)
		if (Unfolded() && count > 0)
			width += iconsWidth + (count - 1) * Size(7) + Size(6);
		width += Size(4) + Size(20) + Size(9) + clockWidth + Size(6);
		width += Size(6) + Size(30) + Size(10);
		height = ContentHeight(fBarView);
	} else if (fBarView->Vertical()) {
		BRect edges = fBarView->EdgeInsets();
		width = static_cast<TBarApp*>(be_app)->Settings()->width
			- edges.left - edges.right + 1;
		// one row of 34, more rows when the symbols do not fit
		float bottom = fRightBottomReplicant.IsValid()
			? fRightBottomReplicant.bottom : 0;
		int32 rows = 1 + (int32)(bottom / TrayRowHeight());
		height = rows * TrayRowHeight();
	} else {
		// gap 6 [padding 6, icons with gaps of 10, padding 6] gap 6
		// [separator 9] gap 6 [8, the time, 8] padding 8 (board C)
		width = Size(6) + Size(6) + iconsWidth
			+ (count > 0 ? (count - 1) * Size(10) : 0) + Size(6) + Size(6);
		width += Size(9) + Size(6);
		width += Size(8) + clockWidth + Size(8) + Size(8);
		height = ContentHeight(fBarView);
	}
	*_width = width - 1;
	*_height = height - 1;
}


float
TReplicantTray::_ModernClockLeft() const
{
	float clockWidth = fTime->Frame().Width() + 1;
	if (Compact()) {
		float left = Size(4) + Size(20) + Size(9);
		if (Unfolded()) {
			for (int32 i = 0; i < ReplicantCount(); i++) {
				BView* view = NULL;
				fShelf->ReplicantAt(i, &view);
				if (view != NULL)
					left += view->Frame().Width() + 1 + Size(7);
			}
			if (ReplicantCount() > 0)
				left += Size(6) - Size(7);
		}
		return left;
	}
	if (fBarView->Vertical())
		return Bounds().right + 1 - Size(10) - clockWidth;
	return Bounds().right + 1 - Size(8) - Size(8) - clockWidth;
}


BPoint
TReplicantTray::_ModernLocation(int32 index, float replicantWidth)
{
	float rowHeight = Compact() || !fBarView->Vertical()
		? ContentHeight(fBarView) : TrayRowHeight();
	float top = floorf((rowHeight - fMaxReplicantHeight) / 2);
	BPoint location(0, top);

	if (Compact()) {
		location.x = 0;
		for (int32 i = 0; i < index; i++) {
			BView* view = NULL;
			fShelf->ReplicantAt(i, &view);
			if (view != NULL)
				location.x += view->Frame().Width() + 1 + Size(7);
		}
	} else if (fBarView->Vertical()) {
		// padding 8, gaps of 5, rows of 34; the clock in the first row (five
		// items and the clock on one row: external review A5)
		float right = Bounds().right + 1 - Size(8);
		float rowRight = right;
		if (!fTime->IsHidden(fTime))
			rowRight = right - fTime->Frame().Width() - 1 - Size(5);
		location.x = Size(8);
		for (int32 i = 0; i < index; i++) {
			BView* view = NULL;
			fShelf->ReplicantAt(i, &view);
			if (view == NULL)
				continue;
			if (view->Frame().top > location.y + 1) {
				location.y = view->Frame().top;
				location.x = Size(8);
				rowRight = right;
			}
			location.x = view->Frame().right + 1 + Size(5);
		}
		if (location.x + replicantWidth > rowRight && location.x > Size(8)) {
			location.y += TrayRowHeight();
			location.x = Size(8);
		}
	} else {
		location.x = Size(12);
		for (int32 i = 0; i < index; i++) {
			BView* view = NULL;
			fShelf->ReplicantAt(i, &view);
			if (view != NULL)
				location.x += view->Frame().Width() + 1 + Size(10);
		}
	}

	if (location.y > fRightBottomReplicant.top
		|| (location.y == fRightBottomReplicant.top
			&& location.x > fRightBottomReplicant.left)) {
		fRightBottomReplicant.Set(location.x, location.y,
			location.x + replicantWidth, location.y + fMaxReplicantHeight);
		fLastReplicant = index;
	}
	return location;
}


void
TReplicantTray::Draw(BRect updateRect)
{
	if (!Modern())
		return;

	float height = Bounds().Height() + 1;
	if (Compact()) {
		// the clock symbol before the time, and the chevron (board B)
		float side = Size(20);
		float left = _ModernClockLeft() - Size(9) - side;
		float top = floorf((height - side) / 2);
		r5_glyph_draw(this, R5_GLYPH_CLOCK, BRect(left, top, left + side - 1,
			top + side - 1), TextColor());
		BRect chevron = fBarView->ChevronFrame();
		float chevronSide = Size(16);
		float x = chevron.left + floorf((chevron.Width() + 1 - chevronSide) / 2);
		float y = chevron.top + floorf((chevron.Height() + 1 - chevronSide) / 2);
		DrawChevron(this, BRect(x, y, x + chevronSide - 1, y + chevronSide - 1),
			Unfolded());
	} else if (!fBarView->Vertical()) {
		// the separator before the clock, 26 high (board C)
		float line = Size(26);
		float top = floorf((height - line) / 2);
		float x = _ModernClockLeft() - Size(8) - Size(6) - Size(4) - 1;
		DrawSeparator(this, x, top, top + line - 1);
	}
}


// #pragma mark - TDragRegion


void
TDragRegion::_DrawModern()
{
	// Vertical: the lines above and below the tray, rgba(30,45,70,.18)
	if (!fBarView->Vertical() || Compact())
		return;
	BRect bounds = Bounds();
	// dark: rgba(255,255,255,.08) (mockup B1)
	rgb_color line = Dark() ? make_color(255, 255, 255, 20)
		: make_color(30, 45, 70, 46);
	DrawLine(this, bounds.LeftTop(), bounds.RightTop(), line);
	DrawLine(this, bounds.LeftBottom(), bounds.RightBottom(), line);
}


// #pragma mark - TDeskbarMenu


void
R5Deskbar::StyleMenuWindow(BWindow* window)
{
	// The corners, the shadow and the edge of the menu panel, from the
	// menu's AttachedToWindow(). Set from its Draw() or from a thread of
	// their own they could hang Deskbar while the menu opened (QEMU, 22 Nov
	// 2026); here they never did. The first opening may still be square.
	if (window == NULL)
		return;
	UnstyleMenuWindow(window);
		// remembers that the menu window had the R5 look
	float radius = ButtonRadius();
	BAppearanceKit::set_window_corner_radius(window, radius);
	BAppearanceKit::set_window_shadow(window,
		BAppearanceKit::B_WINDOW_SHADOW_FULL);
	BAppearanceKit::set_window_edge_line(window, EdgeColor(),
		radius > 0 ? 1 : 0);
}


bool
R5Deskbar::UnstyleMenuWindow(BWindow* window)
{
	static bool sStyled = false;
	if (window == NULL)
		return false;
	if (Modern()) {
		sStyled = true;
		return false;
	}
	if (!sStyled)
		return false;
	BAppearanceKit::set_window_corner_radius(window, 0);
	BAppearanceKit::set_window_shadow(window,
		BAppearanceKit::B_WINDOW_SHADOW_DEFAULT);
	BAppearanceKit::set_window_edge_line(window, make_color(0, 0, 0), 0);
	return true;
}


BPoint
TDeskbarMenu::_ModernLocation()
{
	// beside the wave pebble, on the side that faces the screen, its top
	// level with the pebble's, a gap of 8 (board B); below or above the bar
	BWindow* barWindow = fBarView->Window();
	BRect frame = barWindow != NULL ? barWindow->Frame() : BRect();
	float gap = Size(8);
	BRect bounds = Bounds();
	BRect screen = BScreen(barWindow).Frame();
	BPoint point;
	if (R5Deskbar::Layout() == kLayoutHorizontal) {
		point.x = frame.left;
		point.y = fBarView->Top() ? frame.bottom + 1 + gap
			: frame.top - gap - bounds.Height() - 1;
	} else {
		point.y = fBarView->Top() ? frame.top
			: frame.bottom - bounds.Height();
		if (fBarView->Left())
			point.x = frame.right + 1 + gap;
		else
			point.x = frame.left - gap - bounds.Width() - 1;
	}
	point.x = std::max(screen.left, std::min(point.x,
		screen.right - bounds.Width()));
	point.y = std::max(screen.top, std::min(point.y,
		screen.bottom - bounds.Height()));
	return point;
}


void
TDeskbarMenu::DrawBackground(BRect updateRect)
{
	if (!Modern()) {
		BNavMenu::DrawBackground(updateRect);
		return;
	}
	// the glass panel: its own colour, and the edge where it is square
	if (ButtonRadius() <= 0) {
		PushState();
		rgb_color color = Glass() || Dark() ? EdgeColor()
			: make_color(0x9f, 0xb0, 0xc8);
		float menuInset = 1, top = 1, right = 1, bottom = 1;
		be_control_look->GetBackgroundInsets(BControlLook::B_MENU_BACKGROUND,
			0, menuInset, top, right, bottom);
		if (menuInset > 1) {
			// opaque in the R5 menu style (A6): the menu's edge colour, not
			// the glass's, which is nearly white on the light menu
			rgb_color menu = ui_color(B_MENU_BACKGROUND_COLOR);
			color = menu.IsLight()
				? mix_color(menu, make_color(15, 23, 42), 64)
				: mix_color(menu, make_color(255, 255, 255), 26);
		}
		SetDrawingMode(color.alpha < 255 ? B_OP_ALPHA : B_OP_COPY);
		SetHighColor(color);
		StrokeRect(Bounds());
		PopState();
	}
}
