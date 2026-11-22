/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_MODERN_DECORATOR_H
#define R5_MODERN_DECORATOR_H


#include "R5Decorator.h"


class R5ModernDecorAddOn : public DecorAddOn {
public:
								R5ModernDecorAddOn(image_id id,
									const char* name);

protected:
	virtual	Decorator*			_AllocateDecorator(DesktopSettings& settings,
									BRect rect, Desktop* desktop);
};


/*!	Default (R5 Modern): the R5 frame with four round corners, a soft tab
	card behind the window that the window overlaps, and Haiku's close and
	zoom buttons with a glass finish (docs/design/R5-MODERN-DECORATOR.md,
	board 4 "Soft" of the mockup).
*/
class R5ModernDecorator : public R5Decorator {
public:
								R5ModernDecorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);
	virtual						~R5ModernDecorator();

	virtual	Region				RegionAt(BPoint where, int32& tab) const;

	virtual	void				GetGlassRegion(BRegion* region);
	virtual	int32				GetOutline(window_outline_rect* rects,
									int32 maxCount);
	virtual	bool				GetEdgeLine(rgb_color* color, float* width);

	virtual	void				UpdateColors(DesktopSettings& settings);

protected:
	virtual	void				_DoLayout();
	virtual	void				_DoTabLayout();
	virtual	void				_DrawFrame(BRect rect);
	virtual	void				_DrawTab(Decorator::Tab* tab, BRect invalid);
	virtual	void				_DrawTitle(Decorator::Tab* tab, BRect rect);
	virtual	void				_DrawClose(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_DrawZoom(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_GetFootprint(BRegion* region);

	virtual	void				_ResizeBy(BPoint offset, BRegion* dirty);
	virtual	void				_SetFocus(Decorator::Tab* tab);
	virtual	bool				_SetTabLocation(Decorator::Tab* tab,
									float location, bool isShifting,
									BRegion* updateRegion = NULL);
	virtual	bool				_MoveTab(int32 from, int32 to, bool isMoving,
									BRegion* updateRegion = NULL);

private:
			bool				_IsModern() const;
			bool				_Blended() const;
			bool				_Soft() const;
			float				_Scale() const;
			void				_LayoutItems(Decorator::Tab* tab);
			void				_LayoutAllItems();
			BRect				_TabCard() const;
			void				_WedgePixels(BRegion* region,
									bool blended) const;
			rgb_color			_TabColorAt(Decorator::Tab* tab,
									float y) const;
			Decorator::Tab*		_TabAtX(float x) const;
			rgb_color			_StraightEdgeColor() const;
			void				_DrawWedges(BRect rect);
			void				_DrawTabBackground(Decorator::Tab* tab,
									BRect rect);
			void				_DrawGlassButton(Decorator::Tab* tab,
									BRect rect, bool zoom, bool pressed);

private:
			Desktop*			fModernDesktop;
			float				fRadius;
			float				fTabVisible;
			float				fTabUnder;
			rgb_color			fPanelColor;
};


#endif	// R5_MODERN_DECORATOR_H
