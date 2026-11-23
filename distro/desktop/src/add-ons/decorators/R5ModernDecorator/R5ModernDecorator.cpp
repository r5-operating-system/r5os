/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	Default (R5 Modern), the round R5 window decorator
	(docs/design/R5-MODERN-DECORATOR.md; the numbers are those of board 4
	"Soft" of the mockup, at the default font size, and scale with it like
	Haiku's, times size / 12).

	It is the R5 frame (R5Decorator: a 1 px line, no resize knob, the input
	margin) with:
	- four round corners on the frame (radius 18), given to the compositor
	  as the window outline (Decorator::GetOutline(), ABI 2), and a 1 px
	  light edge carried round them (GetEdgeLine());
	- a soft tab card behind the window: 32 px visible, 14 px under the
	  window, which overlaps it; its top corners are round (18). Where the
	  window's own top corners are cut, the card shows, so those pixels
	  (the wedge) are the decorator's;
	- Haiku's close (left, 15 px) and zoom (right, 16 px: a 9 px square
	  behind a 12 px one) with a glass finish and square corners, blended
	  against the tab here, so they are opaque in every mode.
	With the compositor's effects on (a GPU, or Lavapipe with shadow or
	glass) the tab has a soft gradient and the edge is light; without them
	the tab is one colour and the edge a plain grey-blue line. The round
	corners stay in every mode.
*/


#include "R5ModernDecorator.h"

#include <new>
#include <math.h>
#include <string.h>
#include <syslog.h>

#include <InterfaceDefs.h>
#include <WindowPrivate.h>

#include "Compositor.h"
#include "Desktop.h"
#include "DesktopSettings.h"
#include "DrawingEngine.h"
#include "WindowEffectLevels.h"
#include "WindowOutline.h"


// Board 4 "Soft", at 12 pt (docs/design/R5-MODERN-DECORATOR.md).
static const float kRadius = 18;
static const float kTabVisible = 32;
static const float kTabUnder = 14;
static const float kTabMinWidth = 250;
static const float kPaddingLeft = 10;
static const float kPaddingRight = 8;
static const float kGap = 10;
static const float kTitleEnd = 28;
static const float kCloseSize = 15;
static const float kZoomSize = 16;
static const float kZoomBack = 9;
static const float kZoomFront = 12;

// The tab's gradient: the tab colour this far towards white at the top of
// the card and at its bottom (board 4: #ffdb45 to #ffcf1f from Haiku's
// yellow, 255 203 0). Without effects the tab is the bottom colour (board
// "no GPU": #ffd21f).
static const float kTabTopLight = 0.27f;
static const float kTabBottomLight = 0.12f;

// The edge (board 4): white at 75 % with the effects, #9fb0c8 without.
static const rgb_color kPlainEdge = { 0x9f, 0xb0, 0xc8, 255 };
static const uint8 kSoftEdgeAlpha = 191;

static const rgb_color kWhite = { 255, 255, 255, 255 };
static const rgb_color kBlack = { 0, 0, 0, 255 };


static rgb_color
mix_color(rgb_color a, rgb_color b, float amount)
{
	if (amount <= 0)
		return a;
	if (amount >= 1)
		return b;
	rgb_color result;
	result.red = (uint8)(a.red + (b.red - a.red) * amount + 0.5f);
	result.green = (uint8)(a.green + (b.green - a.green) * amount + 0.5f);
	result.blue = (uint8)(a.blue + (b.blue - a.blue) * amount + 0.5f);
	result.alpha = 255;
	return result;
}


static bool
is_modern_tab_look(window_look look)
{
	return look == B_TITLED_WINDOW_LOOK || look == B_DOCUMENT_WINDOW_LOOK
		|| look == B_FLOATING_WINDOW_LOOK;
}


/*!	Fills one row of pixels, one rectangle per run of the same colour. */
static void
fill_row(DrawingEngine* engine, const rgb_color* pixels, int32 count,
	int32 left, int32 y)
{
	int32 start = 0;
	for (int32 x = 1; x <= count; x++) {
		if (x < count && pixels[x] == pixels[start])
			continue;
		engine->FillRect(BRect(left + start, y, left + x - 1, y),
			pixels[start]);
		start = x;
	}
}


/*!	A glass square of the buttons, into \a pixels (\a width wide): the fill
	over what is there, the 1 px line over the fill (the fill reaches under
	the line, as in the mockup's CSS), and the white highlight on the first
	row inside the line.
*/
static void
paint_glass_square(rgb_color* pixels, int32 width, int32 left, int32 top,
	int32 size, rgb_color line, float lineAlpha, float fillAlpha,
	float highlightAlpha)
{
	for (int32 y = top; y < top + size; y++) {
		for (int32 x = left; x < left + size; x++) {
			rgb_color& pixel = pixels[y * width + x];
			pixel = mix_color(pixel, kWhite, fillAlpha);
			bool edge = x == left || y == top || x == left + size - 1
				|| y == top + size - 1;
			if (edge)
				pixel = mix_color(pixel, line, lineAlpha);
			else if (y == top + 1 && highlightAlpha > 0)
				pixel = mix_color(pixel, kWhite, highlightAlpha);
		}
	}
}


//	#pragma mark - R5ModernDecorAddOn


R5ModernDecorAddOn::R5ModernDecorAddOn(image_id id, const char* name)
	:
	DecorAddOn(id, name)
{
}


Decorator*
R5ModernDecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow) R5ModernDecorator(settings, rect, desktop);
}


//	#pragma mark - R5ModernDecorator


R5ModernDecorator::R5ModernDecorator(DesktopSettings& settings, BRect frame,
	Desktop* desktop)
	:
	R5Decorator(settings, frame, desktop),
	fModernDesktop(desktop),
	fRadius(kRadius),
	fTabVisible(kTabVisible),
	fTabUnder(kTabUnder)
{
	fPanelColor = settings.UIColor(B_PANEL_BACKGROUND_COLOR);
}


R5ModernDecorator::~R5ModernDecorator()
{
}


Decorator::Region
R5ModernDecorator::RegionAt(BPoint where, int32& tab) const
{
	if (!_IsModern())
		return R5Decorator::RegionAt(where, tab);

	// A point outside the arc of one of the frame's round corners: on the
	// tab card it is the tab (the wedge); otherwise it is the resize
	// corner (step 0: Decorator::InputMarginContains() gives it to this
	// window).
	// Review 1: every pixel of the wedge (the footprint's), not only the
	// ones less than half covered.
	if (_InWedge(where)) {
		Decorator::Tab* hit = _TabAtX(where.x);
		tab = fTabList.IndexOf(hit);
		return REGION_TAB;
	}
	window_outline_rect frame;
	frame.rect = fBorderRect;
	frame.leftTop = frame.rightTop = frame.leftBottom = frame.rightBottom
		= fRadius;
	int32 kind = window_outline_cut_corner(&frame, 1, where);
	if (kind < 0 || (kind < 2 && fTitleBarRect.IsValid()
			&& _TabCard().Contains(where)))
		return R5Decorator::RegionAt(where, tab);

	if ((fTopTab->flags & B_NOT_RESIZABLE) != 0)
		return REGION_NONE;
	switch (kind) {
		case 0:
			return REGION_LEFT_TOP_CORNER;
		case 1:
			return REGION_RIGHT_TOP_CORNER;
		case 2:
			return REGION_LEFT_BOTTOM_CORNER;
		default:
			return REGION_RIGHT_BOTTOM_CORNER;
	}
}


/*!	Glass is the tab of an inactive window and the wedge of its card, not
	the line (the brief: "Glass: the tab (via GetGlassRegion)"). The active
	tab stays opaque, as on board 4: glass let the desktop wash its yellow
	out (QEMU, 22 Nov 2026). The compositor asks again at a focus change
	(Desktop::SetFocusWindow() rebuilds the layers).
*/
void
R5ModernDecorator::GetGlassRegion(BRegion* region)
{
	// external review, 23 Nov 2026: _IsModern() reads the top tab, so the
	// lock comes first; the base class takes it itself
	AutoReadLocker locker(fLocker);
	if (!_IsModern()) {
		locker.Unlock();
		R5Decorator::GetGlassRegion(region);
		return;
	}

	region->MakeEmpty();
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (!tab->buttonFocus && tab->tabRect.IsValid())
			region->Include(tab->tabRect);
	}
	if (fTopTab != NULL && !fTopTab->buttonFocus) {
		BRegion wedge;
		_WedgePixels(&wedge);
		region->Include(&wedge);
	}
}


/*!	Decorator ABI 2: the frame with four round corners, then the tab card
	with round top corners. The card reaches under the window, so the
	window's own top corners are not cut where the card lies behind them.
*/
int32
R5ModernDecorator::GetOutline(window_outline_rect* rects, int32 maxCount)
{
	AutoReadLocker _(fLocker);

	if (!_IsModern() || maxCount < 1 || !fBorderRect.IsValid())
		return 0;

	rects[0].rect = fBorderRect;
	rects[0].leftTop = rects[0].rightTop = fRadius;
	rects[0].leftBottom = rects[0].rightBottom = fRadius;
	if (maxCount < 2 || !fTitleBarRect.IsValid())
		return 1;

	rects[1].rect = _TabCard();
	rects[1].leftTop = rects[1].rightTop = fRadius;
	rects[1].leftBottom = rects[1].rightBottom = 0;
	return 2;
}


/*!	Decorator ABI 2: the light edge. With the effects white at 75 %, which
	the compositor mixes over the window's pixels round the arc; without
	them the plain line of board "no GPU".
*/
bool
R5ModernDecorator::GetEdgeLine(rgb_color* color, float* width)
{
	AutoReadLocker locker(fLocker);
	if (!_IsModern()) {
		locker.Unlock();
		return R5Decorator::GetEdgeLine(color, width);
	}

	if (_Soft()) {
		*color = kWhite;
		color->alpha = kSoftEdgeAlpha;
	} else
		*color = kPlainEdge;
	*width = 1;
	return true;
}


void
R5ModernDecorator::UpdateColors(DesktopSettings& settings)
{
	R5Decorator::UpdateColors(settings);
	fPanelColor = settings.UIColor(B_PANEL_BACKGROUND_COLOR);
}


void
R5ModernDecorator::_DoLayout()
{
	float scale = _Scale();
	fRadius = roundf(kRadius * scale);
	fTabVisible = roundf(kTabVisible * scale);
	fTabUnder = roundf(kTabUnder * scale);

	// The R5 frame first; it calls _DoTabLayout() below.
	R5Decorator::_DoLayout();
}


void
R5ModernDecorator::_DoTabLayout()
{
	if (!fThinFrame || fTopTab == NULL || !is_modern_tab_look(fTopTab->look)) {
		R5Decorator::_DoTabLayout();
		return;
	}

	float scale = _Scale();
	float padding = roundf(kPaddingLeft * scale)
		+ roundf(kPaddingRight * scale);
	float gap = roundf(kGap * scale);
	float closeSize = roundf(kCloseSize * scale);
	float zoomSize = roundf(kZoomSize * scale);
	float titleEnd = roundf(kTitleEnd * scale);
	float minWidth = roundf(kTabMinWidth * scale);

	// as TabDecorator::_SingleTabOffsetAndSize() (inline in its source)
	float tabOffset = 0;
	if (fTabList.CountItems() == 1) {
		float maxLocation = fRightBorder.right - fLeftBorder.left
			- _TabAt(0)->maxTabSize;
		if (maxLocation < 0)
			maxLocation = 0;
		tabOffset = floorf(_TabAt(0)->tabLocation * maxLocation);
	}

	// As TabDecorator::_DoTabLayout(), with the card's sizes: a tab is as
	// wide as its content (at least 250 px) as long as the window is.
	float windowWidth = fBorderRect.Width();
	float sumTabWidth = 0;
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = _TabAt(i);

		float buttons = 0;
		if ((tab->flags & B_NOT_CLOSABLE) == 0)
			buttons += closeSize + gap;
		if ((tab->flags & B_NOT_ZOOMABLE) == 0)
			buttons += gap + zoomSize;
		tab->minTabSize = padding + buttons - 1;

		float title = fDrawingEngine != NULL
			? ceilf(fDrawingEngine->StringWidth(Title(tab), strlen(Title(tab)),
				fDrawState.Font())) : 0;
		float content = padding + buttons + title + titleEnd;
		if (content < minWidth)
			content = minWidth;
		tab->maxTabSize = content - 1;
		if (tab->maxTabSize < tab->minTabSize)
			tab->maxTabSize = tab->minTabSize;

		float tabSize = windowWidth;
		if (tabSize < tab->minTabSize)
			tabSize = tab->minTabSize;
		if (tabSize > tab->maxTabSize)
			tabSize = tab->maxTabSize;

		BRect& tabRect = tab->tabRect;
		tabRect.Set(fBorderRect.left, fBorderRect.top - fTabVisible,
			fBorderRect.left + tabSize, fBorderRect.top - 1);

		// External review, 23 Nov 2026: a window narrower than its tab
		// made the room negative, and the cast to uint32 wrapped.
		float room = max_c(0.0f, windowWidth - tabRect.Width());
		tab->tabOffset = (uint32)tabOffset;
		if (tab->tabLocation != 0.0 && fTabList.CountItems() == 1
			&& tab->tabOffset > room) {
			tab->tabOffset = (uint32)room;
		}
		tabRect.OffsetBy(tab->tabOffset, 0);
		tabOffset += tabRect.Width();
		sumTabWidth += tabRect.Width();
	}

	if (CountTabs() > 1 && sumTabWidth > windowWidth)
		_DistributeTabSize(sumTabWidth - windowWidth);

	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (i == 0)
			fTitleBarRect = tab->tabRect;
		else
			fTitleBarRect = fTitleBarRect | tab->tabRect;
	}
	fTabsRegion = fTitleBarRect;
	_LayoutAllItems();
}


void
R5ModernDecorator::_DrawFrame(BRect rect)
{
	if (!_IsModern()) {
		R5Decorator::_DrawFrame(rect);
		return;
	}

	if (!rect.Intersects(fBorderRect))
		return;

	// The line, light or plain; a side that is highlighted (resize, stack
	// and tile) keeps Haiku's highlight colour, as in Default (R5).
	const BRect& r = fBorderRect;
	const struct {
		Component	component;
		Region		region;
		BPoint		start;
		BPoint		end;
	} sides[] = {
		{ COMPONENT_LEFT_BORDER, REGION_LEFT_BORDER, r.LeftTop(),
			r.LeftBottom() },
		{ COMPONENT_TOP_BORDER, REGION_TOP_BORDER, r.LeftTop(),
			r.RightTop() },
		{ COMPONENT_RIGHT_BORDER, REGION_RIGHT_BORDER, r.RightTop(),
			r.RightBottom() },
		{ COMPONENT_BOTTOM_BORDER, REGION_BOTTOM_BORDER, r.LeftBottom(),
			r.RightBottom() }
	};
	rgb_color edge = _StraightEdgeColor();
	for (size_t i = 0; i < sizeof(sides) / sizeof(sides[0]); i++) {
		uint8 highlight = RegionHighlight(sides[i].region);
		rgb_color color = edge;
		if (highlight != 0) {
			ComponentColors colors;
			GetComponentColors(sides[i].component, highlight, colors,
				fTopTab);
			color = colors[5];
		}
		fDrawingEngine->StrokeLine(sides[i].start, sides[i].end, color);
	}

	_DrawWedges(rect);
}


void
R5ModernDecorator::_DrawTab(Decorator::Tab* tab, BRect invalid)
{
	if (!_IsModern() || !is_modern_tab_look(tab->look)) {
		R5Decorator::_DrawTab(tab, invalid);
		return;
	}

	const BRect& tabRect = tab->tabRect;
	if (!tabRect.IsValid() || !invalid.Intersects(tabRect))
		return;

	_DrawTabBackground(tab, tabRect & invalid);
	_DrawTitle(tab, tabRect);
	_DrawButtons(tab, invalid);
}


void
R5ModernDecorator::_DrawTitle(Decorator::Tab* tab, BRect rect)
{
	if (!_IsModern() || !is_modern_tab_look(tab->look)) {
		R5Decorator::_DrawTitle(tab, rect);
		return;
	}

	ComponentColors colors;
	GetComponentColors(COMPONENT_TAB, RegionHighlight(REGION_TAB), colors,
		tab);

	const BRect& tabRect = tab->tabRect;
	float scale = _Scale();

	font_height fontHeight;
	fDrawState.Font().GetHeight(fontHeight);

	BPoint position;
	position.x = (tab->flags & B_NOT_CLOSABLE) == 0
		? tab->closeRect.right + 1 + roundf(kGap * scale)
		: tabRect.left + roundf(kPaddingLeft * scale);
	position.y = floorf(tabRect.top + (tabRect.Height() + 1
		+ fontHeight.ascent - fontHeight.descent) / 2 + 0.5f);

	fDrawingEngine->SetDrawingMode(B_OP_OVER);
	fDrawingEngine->SetHighColor(colors[COLOR_TAB_TEXT]);
	fDrawingEngine->SetFont(fDrawState.Font());
	fDrawingEngine->DrawString(tab->truncatedTitle, tab->truncatedTitleLength,
		position);
	fDrawingEngine->SetDrawingMode(B_OP_COPY);
}


void
R5ModernDecorator::_DrawClose(Decorator::Tab* tab, bool direct, BRect rect)
{
	if (!_IsModern() || !is_modern_tab_look(tab->look)) {
		R5Decorator::_DrawClose(tab, direct, rect);
		return;
	}

	bool copyToFront = fDrawingEngine->CopyToFrontEnabled();
	fDrawingEngine->SetCopyToFrontEnabled(direct);
	_DrawGlassButton(tab, rect, false, tab->closePressed);
	fDrawingEngine->SetCopyToFrontEnabled(copyToFront);
}


void
R5ModernDecorator::_DrawZoom(Decorator::Tab* tab, bool direct, BRect rect)
{
	if (!_IsModern() || !is_modern_tab_look(tab->look)) {
		R5Decorator::_DrawZoom(tab, direct, rect);
		return;
	}
	if (rect.IntegerWidth() < 1)
		return;

	bool copyToFront = fDrawingEngine->CopyToFrontEnabled();
	fDrawingEngine->SetCopyToFrontEnabled(direct);
	_DrawGlassButton(tab, rect, true, tab->zoomPressed);
	fDrawingEngine->SetCopyToFrontEnabled(copyToFront);
}


void
R5ModernDecorator::_GetFootprint(BRegion* region)
{
	R5Decorator::_GetFootprint(region);
	if (!_IsModern())
		return;

	// the wedge: the card where the window's top corners are cut
	BRegion wedge;
	_WedgePixels(&wedge);
	region->Include(&wedge);
}


void
R5ModernDecorator::_ResizeBy(BPoint offset, BRegion* dirty)
{
	if (!_IsModern()) {
		R5Decorator::_ResizeBy(offset, dirty);
		return;
	}

	BRect oldBorder = fBorderRect;
	BRect oldTabs = fTitleBarRect;
	R5Decorator::_ResizeBy(offset, dirty);
	_LayoutAllItems();

	if (dirty == NULL)
		return;
	// The corners moved: the wedge and the arcs are drawn again at the
	// old and the new place, and the whole tab (its width may change).
	const BRect* borders[2] = { &oldBorder, &fBorderRect };
	for (int32 i = 0; i < 2; i++) {
		const BRect& border = *borders[i];
		float r = fRadius;
		dirty->Include(BRect(border.left, border.top, border.left + r,
			border.top + r));
		dirty->Include(BRect(border.right - r, border.top, border.right,
			border.top + r));
		dirty->Include(BRect(border.left, border.bottom - r,
			border.left + r, border.bottom));
		dirty->Include(BRect(border.right - r, border.bottom - r,
			border.right, border.bottom));
	}
	if (oldTabs.IsValid())
		dirty->Include(oldTabs);
	if (fTitleBarRect.IsValid())
		dirty->Include(fTitleBarRect);
}


void
R5ModernDecorator::_SetFocus(Decorator::Tab* tab)
{
	R5Decorator::_SetFocus(tab);
	if (_IsModern())
		_LayoutAllItems();
}


bool
R5ModernDecorator::_SetTabLocation(Decorator::Tab* tab, float location,
	bool isShifting, BRegion* updateRegion)
{
	bool result = R5Decorator::_SetTabLocation(tab, location, isShifting,
		updateRegion);
	if (_IsModern()) {
		_LayoutAllItems();
		// the wedge follows the card
		if (result)
			_IncludeWedgeRows(updateRegion);
	}
	return result;
}


bool
R5ModernDecorator::_MoveTab(int32 from, int32 to, bool isMoving,
	BRegion* updateRegion)
{
	bool result = R5Decorator::_MoveTab(from, to, isMoving, updateRegion);
	if (_IsModern()) {
		_LayoutAllItems();
		// review 1: the wedge takes the colour of the tab now behind it
		_IncludeWedgeRows(updateRegion);
	}
	return result;
}


// Review 2: a new title, flags or tab can change the width of the card, so
// the card may now lie behind a top corner of the window, or no longer.
// The wedge rows are drawn again; Window drops its content region.


void
R5ModernDecorator::_SetTitle(Decorator::Tab* tab, const char* string,
	BRegion* updateRegion)
{
	R5Decorator::_SetTitle(tab, string, updateRegion);
	if (_IsModern())
		_IncludeWedgeRows(updateRegion);
}


void
R5ModernDecorator::_SetFlags(Decorator::Tab* tab, uint32 flags,
	BRegion* updateRegion)
{
	R5Decorator::_SetFlags(tab, flags, updateRegion);
	if (_IsModern())
		_IncludeWedgeRows(updateRegion);
}


bool
R5ModernDecorator::_AddTab(DesktopSettings& settings, int32 index,
	BRegion* updateRegion)
{
	bool result = R5Decorator::_AddTab(settings, index, updateRegion);
	if (_IsModern())
		_IncludeWedgeRows(updateRegion);
	return result;
}


bool
R5ModernDecorator::_RemoveTab(int32 index, BRegion* updateRegion)
{
	bool result = R5Decorator::_RemoveTab(index, updateRegion);
	if (_IsModern())
		_IncludeWedgeRows(updateRegion);
	return result;
}


//	#pragma mark - private


//!	The look of this decorator: the R5 thin frame, without the left tab.
bool
R5ModernDecorator::_IsModern() const
{
	return fThinFrame && fTopTab != NULL
		&& fTopTab->look != kLeftTitledWindowLook;
}


//!	The compositor draws (anti-aliased corners): the GPU mode or Lavapipe.
bool
R5ModernDecorator::_Blended() const
{
	Compositor* compositor = fModernDesktop != NULL
		? fModernDesktop->GetCompositor() : NULL;
	return compositor != NULL && compositor->IsActive();
}


//!	The soft look: the compositor draws, with shadow or glass on.
bool
R5ModernDecorator::_Soft() const
{
	return _Blended() && (gWindowEffects
		& (kWindowEffectShadow | kWindowEffectFrostedGlass)) != 0;
}


float
R5ModernDecorator::_Scale() const
{
	// as TabDecorator::_DoLayout()
	return max_c(fDrawState.Font().Size() / 12.0f, 1.0f);
}


/*!	The close and zoom rectangles and the title of \a tab, from its tab
	rectangle: close on the left after the padding, zoom on the right
	before it, both centred in the visible tab; the title after the close
	button and the gap, left aligned, truncated before the zoom button.
*/
void
R5ModernDecorator::_LayoutItems(Decorator::Tab* tab)
{
	const BRect& tabRect = tab->tabRect;
	float scale = _Scale();
	float paddingLeft = roundf(kPaddingLeft * scale);
	float paddingRight = roundf(kPaddingRight * scale);
	float gap = roundf(kGap * scale);
	float closeSize = roundf(kCloseSize * scale);
	float zoomSize = roundf(kZoomSize * scale);
	float visible = tabRect.Height() + 1;

	// centred as the board's flex box does: 8.5 for close rounds to 9
	float closeTop = tabRect.top + floorf((visible - closeSize) / 2 + 0.5f);
	float zoomTop = tabRect.top + floorf((visible - zoomSize) / 2 + 0.5f);

	BRect& closeRect = tab->closeRect;
	BRect& zoomRect = tab->zoomRect;
	closeRect.Set(tabRect.left + paddingLeft, closeTop,
		tabRect.left + paddingLeft + closeSize - 1, closeTop + closeSize - 1);
	zoomRect.Set(tabRect.right - paddingRight - zoomSize + 1, zoomTop,
		tabRect.right - paddingRight, zoomTop + zoomSize - 1);
	// hidden buttons have no width
	if ((tab->flags & B_NOT_CLOSABLE) != 0)
		closeRect.right = closeRect.left - 1;
	bool stackMode = fTabList.CountItems() > 1;
	if ((tab->flags & B_NOT_ZOOMABLE) != 0 || (stackMode && !IsFocus(tab)))
		zoomRect.left = zoomRect.right + 1;
	tab->minimizeRect.Set(0, 0, -1, -1);

	float titleLeft = (tab->flags & B_NOT_CLOSABLE) == 0
		? closeRect.right + 1 + gap : tabRect.left + paddingLeft;
	float titleRight = (zoomRect.IsValid() ? zoomRect.left - gap
		: tabRect.right - paddingRight + 1) - roundf(kTitleEnd * scale);
	if (!stackMode) {
		// the title may use the end padding when the window is narrow
		float full = (zoomRect.IsValid() ? zoomRect.left - gap
			: tabRect.right - paddingRight + 1);
		if (titleRight < titleLeft)
			titleRight = full;
	}

	tab->textOffset = gap;
	tab->truncatedTitle = Title(tab);
	fDrawState.Font().TruncateString(&tab->truncatedTitle,
		stackMode ? B_TRUNCATE_END : B_TRUNCATE_MIDDLE,
		titleRight > titleLeft ? titleRight - titleLeft : 0);
	tab->truncatedTitleLength = tab->truncatedTitle.Length();
}


void
R5ModernDecorator::_LayoutAllItems()
{
	if (fTopTab == NULL || !is_modern_tab_look(fTopTab->look))
		return;
	for (int32 i = 0; i < fTabList.CountItems(); i++)
		_LayoutItems(fTabList.ItemAt(i));
}


//!	The rows of the window's top corners, where the wedge can be.
void
R5ModernDecorator::_IncludeWedgeRows(BRegion* region) const
{
	if (region == NULL || !fBorderRect.IsValid())
		return;
	region->Include(BRect(fBorderRect.left, fBorderRect.top,
		fBorderRect.right, fBorderRect.top + fRadius));
}


//!	The tab card: the tabs, continued under the window by fTabUnder.
BRect
R5ModernDecorator::_TabCard() const
{
	return BRect(fTitleBarRect.left, fTitleBarRect.top, fTitleBarRect.right,
		fBorderRect.top + fTabUnder - 1);
}


/*!	The wedge: pixels of the frame's top corners that the arc does not
	fully cover, on the card. The same in both modes (review 1: a footprint
	that depended on the mode stayed the GPU one after a fall back to the
	CPU mode, and pixels were drawn by nobody). In the CPU mode the pixels
	at least half covered are the stepped edge (Window's rim, the same
	colour).
*/
void
R5ModernDecorator::_WedgePixels(BRegion* region) const
{
	if (!fTitleBarRect.IsValid() || !fBorderRect.IsValid())
		return;

	clipping_rect frame = window_outline_clipping_rect(fBorderRect);
	clipping_rect card = window_outline_clipping_rect(_TabCard());
	for (int32 kind = 0; kind < 2; kind++) {
		window_corner corner;
		if (!window_outline_corner(frame, kind, fRadius, corner))
			continue;
		for (int32 y = corner.square.top; y <= corner.square.bottom
				&& y <= card.bottom; y++) {
			for (int32 x = corner.square.left; x <= corner.square.right;
					x++) {
				if (x < card.left || x > card.right)
					continue;
				float coverage = window_corner_coverage(corner, x, y);
				if (coverage < 1.0f - kWindowCornerEpsilon)
					window_outline_add_run(*region, x, x, y);
			}
		}
	}
}


/*!	The tab colour of \a tab at screen row \a y: the soft gradient over the
	whole card with the effects, one colour without them.
*/
rgb_color
R5ModernDecorator::_TabColorAt(Decorator::Tab* tab, float y) const
{
	ComponentColors colors;
	const_cast<R5ModernDecorator*>(this)->GetComponentColors(COMPONENT_TAB,
		RegionHighlight(REGION_TAB), colors, tab);
	rgb_color base = colors[COLOR_TAB];
	rgb_color bottom = mix_color(base, kWhite, kTabBottomLight);
	if (!_Soft())
		return bottom;

	rgb_color top = mix_color(base, kWhite, kTabTopLight);
	float height = fTabVisible + fTabUnder - 1;
	float position = height > 0 ? (y - fTitleBarRect.top) / height : 0;
	return mix_color(top, bottom, position);
}


//!	Whether \a where is a pixel of the wedge (_WedgePixels()).
bool
R5ModernDecorator::_InWedge(BPoint where) const
{
	if (!fTitleBarRect.IsValid() || !fBorderRect.IsValid()
		|| !_TabCard().Contains(where))
		return false;
	int32 x = (int32)floorf(where.x);
	int32 y = (int32)floorf(where.y);
	clipping_rect frame = window_outline_clipping_rect(fBorderRect);
	for (int32 kind = 0; kind < 2; kind++) {
		window_corner corner;
		if (!window_outline_corner(frame, kind, fRadius, corner))
			continue;
		if (x < corner.square.left || x > corner.square.right
			|| y < corner.square.top || y > corner.square.bottom)
			continue;
		return window_corner_coverage(corner, x, y)
			< 1.0f - kWindowCornerEpsilon;
	}
	return false;
}


Decorator::Tab*
R5ModernDecorator::_TabAtX(float x) const
{
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (x >= tab->tabRect.left && x <= tab->tabRect.right)
			return tab;
	}
	return fTopTab;
}


/*!	The straight edge: with the effects white at 75 % over the panel colour
	(the compositor puts the same white over the window's own pixels round
	the arc), without them the plain line.
*/
rgb_color
R5ModernDecorator::_StraightEdgeColor() const
{
	if (!_Soft())
		return kPlainEdge;
	return mix_color(fPanelColor, kWhite, kSoftEdgeAlpha / 255.0f);
}


/*!	Paints the wedge: the card, and with the compositor the arc's edge line
	over it by coverage (as the compositor blends the line elsewhere); in
	the CPU mode the pixels at least half covered in the plain edge colour
	(the stepped line Window paints as its rim). One run of rectangles per
	row (review 1: a rectangle and a colour lookup per pixel).
*/
void
R5ModernDecorator::_DrawWedges(BRect rect)
{
	if (!fTitleBarRect.IsValid())
		return;

	bool blended = _Blended();
	rgb_color edge = _StraightEdgeColor();
	clipping_rect frame = window_outline_clipping_rect(fBorderRect);
	clipping_rect card = window_outline_clipping_rect(_TabCard());
	for (int32 kind = 0; kind < 2; kind++) {
		window_corner corner;
		if (!window_outline_corner(frame, kind, fRadius, corner))
			continue;
		int32 left = max_c(corner.square.left, card.left);
		int32 right = min_c(corner.square.right, card.right);
		if (left > right)
			continue;
		if (!rect.Intersects(BRect(left, corner.square.top, right,
				corner.square.bottom)))
			continue;
		rgb_color row[R5_WINDOW_CORNER_MAX_RADIUS];
		for (int32 y = corner.square.top; y <= corner.square.bottom
				&& y <= card.bottom; y++) {
			Decorator::Tab* lastTab = NULL;
			rgb_color tabColor = kWhite;
			int32 start = -1;
			for (int32 x = left; x <= right + 1; x++) {
				float coverage = x <= right
					? window_corner_coverage(corner, x, y) : 1.0f;
				bool mine = coverage < 1.0f - kWindowCornerEpsilon;
				if (mine) {
					Decorator::Tab* tab = _TabAtX(x);
					if (tab != lastTab) {
						tabColor = _TabColorAt(tab, y);
						lastTab = tab;
					}
					rgb_color color;
					if (blended)
						color = mix_color(tabColor, edge, coverage);
					else
						color = coverage < 0.5f ? tabColor : edge;
					if (start < 0)
						start = x;
					row[x - start] = color;
				} else if (start >= 0) {
					fill_row(fDrawingEngine, row, x - start, start, y);
					start = -1;
				}
			}
		}
	}
}


void
R5ModernDecorator::_DrawTabBackground(Decorator::Tab* tab, BRect rect)
{
	if (!rect.IsValid())
		return;

	// one rectangle per row: the colour changes per row with the effects
	bool separator = fTabList.CountItems() > 1
		&& tab != fTabList.ItemAt(fTabList.CountItems() - 1);
	rgb_color dark = mix_color(_TabColorAt(tab, tab->tabRect.top), kBlack,
		0.65f);
	for (float y = rect.top; y <= rect.bottom; y++) {
		rgb_color color = _TabColorAt(tab, y);
		fDrawingEngine->FillRect(BRect(rect.left, y, rect.right, y), color);
		if (separator && rect.right >= tab->tabRect.right) {
			// stacked tabs: a thin line between them
			fDrawingEngine->FillRect(BRect(tab->tabRect.right, y,
				tab->tabRect.right, y), mix_color(color, dark, 0.35f));
		}
	}
}


/*!	A button of board 4 (finish "glass", square corners). Active: white at
	55 % (the zoom's back square 35 %), a line of the tab's dark yellow at
	35 % and a white highlight at 90 % under the top line. Inactive: a line
	of black at 20 % and white at 60 %. Pressed (not on the board): less
	white, a stronger line, no highlight. Everything is blended against the
	tab here, so the button is opaque in every mode.
*/
void
R5ModernDecorator::_DrawGlassButton(Decorator::Tab* tab, BRect rect,
	bool zoom, bool pressed)
{
	int32 width = rect.IntegerWidth() + 1;
	int32 height = rect.IntegerHeight() + 1;
	if (width <= 0 || height <= 0)
		return;
	rgb_color* pixels = new (std::nothrow) rgb_color[width * height];
	if (pixels == NULL)
		return;

	for (int32 y = 0; y < height; y++) {
		rgb_color color = _TabColorAt(tab, rect.top + y);
		for (int32 x = 0; x < width; x++)
			pixels[y * width + x] = color;
	}

	bool active = tab->buttonFocus;
	// board 4: rgba(90, 65, 0, .35) over Haiku's yellow, the tab colour
	// 65 % towards black
	ComponentColors colors;
	GetComponentColors(COMPONENT_TAB, RegionHighlight(REGION_TAB), colors,
		tab);
	rgb_color line = active ? mix_color(colors[COLOR_TAB], kBlack, 0.65f)
		: kBlack;
	float lineAlpha = active ? 0.35f : 0.20f;
	float fill = active ? 0.55f : 0.60f;
	float backFill = active ? 0.35f : 0.60f;
	float highlight = 0.90f;
	if (pressed) {
		lineAlpha += 0.20f;
		fill = 0.25f;
		backFill = 0.20f;
		highlight = 0;
	}

	if (!zoom) {
		paint_glass_square(pixels, width, 0, 0, min_c(width, height), line,
			lineAlpha, fill, highlight);
	} else {
		float scale = _Scale();
		int32 size = min_c(width, height);
		int32 back = (int32)roundf(kZoomBack * scale);
		int32 front = (int32)roundf(kZoomFront * scale);
		if (back > size)
			back = size;
		if (front > size)
			front = size;
		paint_glass_square(pixels, width, 0, 0, back, line, lineAlpha,
			backFill, 0);
		paint_glass_square(pixels, width, size - front, size - front, front,
			line, lineAlpha, fill, highlight);
	}

	for (int32 y = 0; y < height; y++) {
		fill_row(fDrawingEngine, pixels + y * width, width,
			(int32)rect.left, (int32)rect.top + y);
	}
	delete[] pixels;
}


//	#pragma mark - add-on entry


R5_DECORATOR_ABI_EXPORT


extern "C" DecorAddOn*
(instantiate_decor_addon)(image_id id, const char* name)
{
	syslog(LOG_INFO, "decorator: r5 modern loaded\n");
	return new (std::nothrow) R5ModernDecorAddOn(id, name);
}
