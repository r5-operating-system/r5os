/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 (docs/APP-SERVER-GPU-DRAWING.md, part 4): the DrawingEngine that
 * draws with Skia on the GPU. While it is bound to its window buffer (the
 * GPU mode) every call goes to SkiaPainter and nothing to Painter; while it
 * is bound to the screen (the CPU mode, the direct-window pause) every call
 * is the base class, Painter, as before (decision 1: never mixed).
 *
 * No Skia type in this header (decision 3).
 */
#ifndef SKIA_DRAWING_ENGINE_H
#define SKIA_DRAWING_ENGINE_H


#include "DrawingEngine.h"


class Compositor;
class SkiaGpu;
class SkiaPainter;
class SkiaTarget;
class WindowHWInterface;


class SkiaDrawingEngine : public DrawingEngine {
public:
	// A window's engine: the GPU while bound to windowInterface.
								SkiaDrawingEngine(
									WindowHWInterface* windowInterface,
									SkiaGpu* gpu, Compositor* compositor);
	// An engine on a target of its own (r5-draw-compare, offscreen
	// bitmaps): the GPU while the GPU drawing runs. lockInterface is only
	// used for the locks and for the CPU mode.
								SkiaDrawingEngine(HWInterface* lockInterface,
									SkiaTarget* ownedTarget);
	virtual						~SkiaDrawingEngine();

			bool				OnGpu() const { return _Gpu(); }
	// An offscreen window's bitmap (skia_offscreen_sync() finds it).
			void				SetTrackedBitmap(ServerBitmap* bitmap);
	// Every pixel of the image has alpha 255 (no glass views): COPY and
	// OVER with one colour use Skia's own blending.
			void				SetOpaque(bool opaque);
			SkiaTarget*			Target() const { return fTarget; }

	// A read point (part 6): everything drawn so far is in memory when
	// this returns. No-op in the CPU mode.
			status_t			Finish();

	// The window target's surface size when it is not a window engine.
			bool				SetTargetSize(int32 width, int32 height);
	// The same with an origin (drawing coordinate of surface pixel 0, 0)
	// and the bytes the image starts with (may be NULL).
			bool				AttachSurface(int32 width, int32 height,
									int32 left, int32 top, const uint8* bits,
									uint32 bytesPerRow);
	// "Scaling later" point 1: the factor of this engine, 1 in M12 (only
	// r5-draw-compare --scale sets another).
			void				SetScale(float scale);

	virtual	void				FrameBufferChanged();

	virtual	void				SetCopyToFrontEnabled(bool enable);
	// The compositor took the window's buffer back (an off-screen window)
			void				ReleaseBuffer();
	virtual	void				CopyToFront(BRegion& region);

	virtual	status_t			ReadBitmap(ServerBitmap* bitmap,
									bool drawCursor, BRect bounds);

	virtual	void				ConstrainClippingRegion(const BRegion* region);
	virtual	void				SetDrawState(const DrawState* state,
									int32 xOffset = 0, int32 yOffset = 0);
	virtual	void				SetHighColor(const rgb_color& color);
	virtual	void				SetLowColor(const rgb_color& color);
	virtual	void				SetPenSize(float size);
	virtual	void				SetStrokeMode(cap_mode lineCap,
									join_mode joinMode, float miterLimit);
	virtual	void				SetFillRule(int32 fillRule);
	virtual	void				SetPattern(const struct pattern& pattern);
	virtual	void				SetDrawingMode(drawing_mode mode);
	virtual	void				SetDrawingMode(drawing_mode mode,
									drawing_mode& oldMode);
	virtual	void				SetBlendingMode(source_alpha srcAlpha,
									alpha_function alphaFunc);
	virtual	void				SetFont(const ServerFont& font);
	virtual	void				SetFont(const DrawState* state);
	virtual	void				SetTransform(const BAffineTransform& transform,
									int32 xOffset, int32 yOffset);

	virtual	void				CopyRegion(BRegion* region, int32 xOffset,
									int32 yOffset);
	virtual	void				InvertRect(BRect r);
	virtual	void				DrawBitmap(ServerBitmap* bitmap,
									const BRect& bitmapRect,
									const BRect& viewRect, uint32 options = 0);
	virtual	void				DrawArc(BRect r, const float& angle,
									const float& span, bool filled);
	virtual	void				DrawArc(BRect r, const float& angle,
									const float& span, bool filled,
									const BGradient& gradient);
	virtual	void				DrawBezier(BPoint* pts, bool filled);
	virtual	void				DrawBezier(BPoint* pts, bool filled,
									const BGradient& gradient);
	virtual	void				DrawEllipse(BRect r, bool filled);
	virtual	void				DrawEllipse(BRect r, bool filled,
									const BGradient& gradient);
	virtual	void				DrawPolygon(BPoint* ptlist, int32 numpts,
									BRect bounds, bool filled, bool closed);
	virtual	void				DrawPolygon(BPoint* ptlist, int32 numpts,
									BRect bounds, bool filled, bool closed,
									const BGradient& gradient);
	virtual	void				StrokePoint(const BPoint& point,
									const rgb_color& color);
	virtual	void				StrokeRect(BRect rect, const rgb_color& color);
	virtual	void				FillRect(BRect rect, const rgb_color& color);
	virtual	void				FillRegion(BRegion& region,
									const rgb_color& color);
	virtual	void				StrokeRect(BRect rect);
	virtual	void				StrokeRect(BRect rect,
									const BGradient& gradient);
	virtual	void				FillRect(BRect rect);
	virtual	void				FillRect(BRect rect, const BGradient& gradient);
	virtual	void				FillRegion(BRegion& region);
	virtual	void				FillRegion(BRegion& region,
									const BGradient& gradient);
	virtual	void				DrawRoundRect(BRect rect, float xrad,
									float yrad, bool filled);
	virtual	void				DrawRoundRect(BRect rect, float xrad,
									float yrad, bool filled,
									const BGradient& gradient);
	virtual	void				DrawShape(const BRect& bounds, int32 opcount,
									const uint32* oplist, int32 ptcount,
									const BPoint* ptlist, bool filled,
									const BPoint& viewToScreenOffset,
									float viewScale);
	virtual	void				DrawShape(const BRect& bounds, int32 opcount,
									const uint32* oplist, int32 ptcount,
									const BPoint* ptlist, bool filled,
									const BGradient& gradient,
									const BPoint& viewToScreenOffset,
									float viewScale);
	virtual	void				DrawTriangle(BPoint* points,
									const BRect& bounds, bool filled);
	virtual	void				DrawTriangle(BPoint* points,
									const BRect& bounds, bool filled,
									const BGradient& gradient);
	virtual	void				StrokeLine(const BPoint& start,
									const BPoint& end, const rgb_color& color);
	virtual	void				StrokeLine(const BPoint& start,
									const BPoint& end);
	virtual	void				StrokeLine(const BPoint& start,
									const BPoint& end,
									const BGradient& gradient);
	virtual	void				StrokeLineArray(int32 numlines,
									const ViewLineArrayInfo* data);
	virtual	BPoint				DrawString(const char* string, int32 length,
									const BPoint& pt,
									escapement_delta* delta = NULL);
	virtual	BPoint				DrawString(const char* string, int32 length,
									const BPoint* offsets);

	// gpu_debug: the whole GPU drawing of one window as one line, for the
	// compositor's report
	static	void				SetDebug(bool debug);
	// Step 5b: gpu_draw_classic (SkiaPainter::SetClassic()).
	static	void				SetClassic(bool classic);
	// M13, option E (SkiaPainter::SetHardwareBlend())
	static	void				SetHardwareBlend(bool on);
	// Step 5b: SkiaPainter::SetLinearEdges().
	static	void				SetLinearEdges(bool linear);
	// Decision 9: Painter's drawing sessions (the CPU path), closed by the
	// compose thread once per compose; the median per 10 s.
	static	void				CloseCpuSessions();
	static	bool				TakeCpuMedian(bigtime_t* _median,
									int32* _count);

private:
			class Draw;
			class WindowTarget;
			class CpuTimer;
			friend class CpuTimer;
			friend class Draw;

			void				_Init();
			bool				_Gpu() const;
			void				_Wake();

private:
			SkiaPainter*		fSkia;
			SkiaTarget*			fTarget;
			bool				fOwnsTarget;
			WindowHWInterface*	fWindowInterface;
			Compositor*			fCompositor;
			SkiaGpu*			fGpu;
			bool				fOnGpu;
			ServerBitmap*		fTrackedBitmap;
	volatile bigtime_t			fCpuFirst;
	volatile bigtime_t			fCpuLast;
};


#endif	// SKIA_DRAWING_ENGINE_H
