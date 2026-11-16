/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 (docs/APP-SERVER-GPU-DRAWING.md, part 4): Painter's drawing calls on
 * the GPU. The geometry before rasterisation (pixel alignment, pen size
 * rules, the ellipse and rectangle alignment, the shape iteration) follows
 * Painter.cpp line for line, so both engines start from the same numbers.
 * Skia only computes coverage; Haiku's integer blend formulas of every
 * drawing mode run in one R5 runtime shader on the GPU.
 *
 * Only app_server's Skia code includes this header (decision 3).
 */
#ifndef SKIA_PAINTER_H
#define SKIA_PAINTER_H


#include <AffineTransform.h>
#include <Font.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <Rect.h>
#include <Region.h>

#include <functional>
#include <memory>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPath.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkBlender.h"
#include "include/core/SkShader.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"

#include "ServerFont.h"


class AlphaMask;
class BGradient;
class DrawState;
class ServerBitmap;
class SkiaGpu;
struct escapement_delta;

namespace skgpu { namespace graphite { class Recorder; class Recording; } }


/*!	One GPU image that a SkiaPainter draws into: a window's image (its
	pixels end up in the M11 window buffer), an offscreen bitmap's image,
	or a test image of r5-draw-compare. Surface pixel (0, 0) is screen
	pixel (Left(), Top()).
*/
class SkiaTarget {
public:
								SkiaTarget(SkiaGpu* gpu);
	virtual						~SkiaTarget();

			bool				Lock() { return fLock.Lock(); }
			void				Unlock() { fLock.Unlock(); }

	// Under the lock. A new size makes a new image: the pixels of the
	// old one that overlap on screen are copied on the GPU, the rest is
	// set from initialBits (the buffer's current bytes, may be NULL).
			bool				SetSurface(int32 width, int32 height,
									int32 left, int32 top,
									const uint8* initialBits,
									uint32 bytesPerRow);
			void				SetOrigin(int32 left, int32 top);
	// Under the lock: the memory's bytes into the image (an offscreen
	// bitmap the program wrote itself, part 6 point 2).
			bool				Upload(const uint8* bits, uint32 bytesPerRow);
	// After a read point: the next drawing uploads UploadSource() first.
			void				SetUploadPending() { fUploadPending = true; }
			void				UploadIfPending();
	virtual	bool				UploadSource(const uint8** _bits,
									uint32* _bytesPerRow) { return false; }
			void				DropGpu();

	// Step 5c: textures of bitmaps drawn into this target, kept while the
	// bitmap's bytes stay the same (compared with a copy; a program may
	// write a bitmap's memory at any time).
			sk_sp<SkImage>		FindTexture(const void* id, const uint8* bits,
									int32 length, int32 width, int32 height,
									int32 bytesPerRow, int32 space, bool raw);
			void				KeepTexture(const void* id, const uint8* bits,
									int32 length, int32 width, int32 height,
									int32 bytesPerRow, int32 space, bool raw,
									const sk_sp<SkImage>& image);
			bool				HasSurface() const
									{ return fSurface != nullptr; }
	// Every pixel has alpha 255 (a window without glass views, an opaque
	// compare background): Skia's own blending then gives Haiku's result
	// for B_OP_COPY and B_OP_OVER with one colour, without a copy of the
	// destination (owner, 15 Nov 2026). Set without the lock.
			void				SetOpaque(bool opaque) { fOpaque = opaque; }
			bool				IsOpaque() const { return fOpaque; }

			int32				Left() const { return fLeft; }
			int32				Top() const { return fTop; }
			int32				Width() const { return fWidth; }
			int32				Height() const { return fHeight; }

			SkSurface*			Surface() const { return fSurface.get(); }
			SkCanvas*			Canvas() const;
			// The coverage image; lcd: with Skia's subpixel geometry from
			// the Antialiasing tab, for its own subpixel text (Q5).
			SkSurface*			Scratch(bool lcd = false);
			skgpu::graphite::Recorder* Recorder() const
									{ return fRecorder; }
			SkiaGpu*			Gpu() const { return fGpu; }

	// Under the lock: screen pixels that changed and must be copied out.
			void				AddDamage(const BRegion& screen);
			void				AddDamage(const BRect& screen);
			void				MarkDrawn();

	// SkiaGpu::RunWork, under the lock.
			bool				HasWork() const { return fRecorded; }
			std::unique_ptr<skgpu::graphite::Recording> Snap(
									BRegion& screenDamage,
									bigtime_t* _firstDraw);

	// Where the pixels go; called by RunWork under the lock, right after
	// Snap(). The returned cookie is handed to Deliver().
	virtual	void*				PrepareDelivery() = 0;
	// Compose thread, without the target lock: the pixels of screenFrame
	// (rows of bytesPerRow, BGRA, straight alpha as Haiku keeps them) for
	// the damage of that snap. Must also release the cookie.
	virtual	void				Deliver(void* cookie,
									const BRegion& screenDamage,
									const clipping_rect& screenFrame,
									const uint8* bits, uint32 bytesPerRow)
									= 0;
	virtual	void				DeliveryFailed(void* cookie) = 0;

protected:
			SkiaGpu*			fGpu;
			BLocker				fLock;
			skgpu::graphite::Recorder* fRecorder;
			sk_sp<SkSurface>	fSurface;
			sk_sp<SkSurface>	fScratch;
			sk_sp<SkSurface>	fScratchLcd;
			bool				fScratchLcdRGB;
			int32				fLeft;
			int32				fTop;
			int32				fWidth;
			int32				fHeight;
			BRegion				fDamage;
			bool				fRecorded;
			bigtime_t			fFirstDraw;
			bool				fUploadPending;
	volatile bool				fOpaque;

			struct Texture {
				const void*			id;
				int32				width;
				int32				height;
				int32				bytesPerRow;
				int32				space;
				bool				raw;
				std::vector<uint8>	bits;
				sk_sp<SkImage>		image;
				bigtime_t			used;
			};
			std::vector<Texture>	fTextures;
			size_t				fTextureBytes = 0;
};


class SkiaPainter {
public:
	// Step 5b: the classic look (AGG-like linear coverage, Haiku's hinting
	// and subpixel choice) instead of light hinting and clean grayscale
	// text. gpu_draw_classic true in the radeon_hd settings.
	static	void				SetClassic(bool classic);
	static	bool				Classic();

								SkiaPainter();
								~SkiaPainter();

			void				SetTarget(SkiaTarget* target);
			SkiaTarget*			Target() const { return fTarget; }
	// The scale of "Scaling later" point 1: always 1 in M12.
			void				SetScale(float scale) { fScale = scale; }

	// state, as Painter
			void				SetDrawState(const DrawState* state,
									int32 xOffset, int32 yOffset);
			void				ConstrainClipping(const BRegion* region);
			const BRegion*		ClippingRegion() const
									{ return fClippingRegion; }
			void				SetTransform(BAffineTransform transform,
									int32 xOffset, int32 yOffset);
			bool				IsIdentityTransform() const
									{ return fIdentityTransform; }
			void				SetHighColor(const rgb_color& color)
									{ fHighColor = color; }
			rgb_color			HighColor() const { return fHighColor; }
			void				SetLowColor(const rgb_color& color)
									{ fLowColor = color; }
			rgb_color			LowColor() const { return fLowColor; }
			void				SetPenSize(float size) { fPenSize = size; }
			float				PenSize() const { return fPenSize; }
			void				SetStrokeMode(cap_mode cap, join_mode join,
									float miterLimit);
			void				SetFillRule(int32 rule) { fFillRule = rule; }
			void				SetPattern(const pattern& p) { fPattern = p; }
			pattern				Pattern() const { return fPattern; }
			void				SetDrawingMode(drawing_mode mode)
									{ fDrawingMode = mode; }
			drawing_mode		DrawingMode() const { return fDrawingMode; }
			void				SetBlendingMode(source_alpha src,
									alpha_function fnc);
			void				SetFont(const ServerFont& font);
			void				SetFont(const DrawState* state);
			const ServerFont&	Font() const { return fFont; }
			void				SetRendererOffset(int32 x, int32 y);

	// geometry, as Painter
			BRect				AlignRect(BRect rect) const;
			BRect				ClipRect(BRect rect) const;
			BRect				TransformAndClipRect(BRect rect) const;
			BRect				AlignAndClipRect(BRect rect) const;
			BRect				TransformAlignAndClipRect(BRect rect) const;
			void				AlignEllipseRect(BRect* rect,
									bool filled) const;
			BRect				Bounds() const;

	// drawing, as Painter; the touched rectangle in screen coordinates
			void				StrokeLine(BPoint a, BPoint b);
			void				StrokeLine(BPoint a, BPoint b,
									const BGradient& gradient);
			bool				StraightLine(BPoint a, BPoint b,
									const rgb_color& c);
			BRect				DrawTriangle(BPoint p1, BPoint p2, BPoint p3,
									bool fill, const BGradient* gradient);
			BRect				DrawPolygon(BPoint* p, int32 count,
									bool filled, bool closed,
									const BGradient* gradient);
			BRect				DrawBezier(BPoint* p, bool filled,
									const BGradient* gradient);
			BRect				DrawShape(int32 opCount, const uint32* opList,
									int32 ptCount, const BPoint* points,
									bool filled, const BPoint& offset,
									float scale, const BGradient* gradient);
			BRect				StrokeRect(const BRect& r,
									const BGradient* gradient);
			void				StrokeRect(const BRect& r,
									const rgb_color& c);
			BRect				FillRect(const BRect& r,
									const BGradient* gradient);
			void				FillRect(const BRect& r, const rgb_color& c);
			void				FillRectNoClipping(const clipping_rect& r,
									const rgb_color& c);
			BRect				DrawRoundRect(const BRect& r, float xRadius,
									float yRadius, bool filled,
									const BGradient* gradient);
			BRect				DrawEllipse(BRect r, bool fill,
									const BGradient* gradient);
			BRect				DrawArc(BPoint center, float xRadius,
									float yRadius, float angle, float span,
									bool filled, const BGradient* gradient);
			BRect				DrawBitmap(const ServerBitmap* bitmap,
									BRect bitmapRect, BRect viewRect,
									uint32 options);
			BRect				FillRegion(const BRegion* region,
									const BGradient* gradient);
			BRect				InvertRect(const BRect& r);
			void				CopyRegion(const BRegion& region,
									int32 xOffset, int32 yOffset,
									BRegion& _touched);

	// text (step 4); the pen position stays ServerFont's
			BRect				DrawString(const char* string, uint32 length,
									BPoint baseLine,
									const escapement_delta* delta,
									const BPoint* offsets);

private:
			// what the R5 shader blends (SkiaPainter.cpp, _Composite())
			struct Source {
				Source()
					:
					useColors(false),
					forceSolidHigh(false),
					special(0),
					blendAlpha(0),
					lcd(false),
					sampling(0)
				{
				}

				bool				useColors;
				sk_sp<SkShader>		colors;
				bool				forceSolidHigh;
				int					special;
				int					blendAlpha;
				rgb_color			specialColor;
				bool				lcd;
				// text: Skia's text gamma, not the linear blend (step 5b)
				bool				text = false;
				// Painter's scaled bitmap sampling (1 nearest, 2 bilinear)
				int					sampling;
				float				destOrigin[2];
				float				srcSize[2];
				float				scale[2];
				float				srcShift[2];
			};

			float				_Align(float coord, bool round,
									bool centerOffset) const;
			void				_Align(BPoint* point,
									bool centerOffset = true) const;
			void				_Align(BPoint* point, bool round,
									bool centerOffset) const;
			BRect				_Clipped(const BRect& rect) const;
			BRect				_TransformBounds(const BRect& rect) const;
			SkMatrix			_Matrix() const;

			BRect				_FillPath(const SkPath& path,
									const BGradient* gradient);
			void				_DrawTextLines(SkCanvas* canvas,
									const class GlyphCollector& collector,
									const SkPaint& ink);
			// step 5c: an R5 blender special (SkiaShaders.h) straight into
			// the target, colors the bitmap or nullptr
			bool				_SpecialPaint(int special, int blendAlpha,
									const rgb_color& high,
									const sk_sp<SkShader>& colors,
									SkPaint* paint) const;
			bool				_BlenderPaint(bool forceSolidHigh,
									const sk_sp<SkShader>& colors,
									SkPaint* paint,
									bool text = false,
									const sk_sp<SkShader>& coverage
										= nullptr) const;
			int					_AlphaKind() const;
			bool				_DirectColor(bool forceSolidHigh,
									bool* _draw, SkColor* _color) const;
			void				_DirectDraw(const SkIRect& bounds,
									const SkMatrix& matrix,
									const std::function<void(SkCanvas*)>& draw);
			BRect				_StrokePath(const SkPath& path,
									cap_mode cap, const BGradient* gradient);
			BRect				_DrawCoverage(const SkRect& bounds,
									const SkPath* path, const SkPaint* paint,
									const SkRRect* rrect, bool oval,
									const BGradient* gradient);
			void				_FillSolid(const BRegion& region,
									const rgb_color& color, bool clip);
			bool				_Composite(const SkIRect& bounds,
									const sk_sp<SkImage>& coverage,
									const Source& source);
			void				_ClipRects(const SkIRect& bounds,
									BRegion& _rects) const;
			sk_sp<SkShader>		_MaskShader(const SkIRect& bounds);
			sk_sp<SkShader>		_GradientShader(const BGradient& gradient,
									bool* _ok);

private:
			SkiaTarget*			fTarget;
			float				fScale;

			bool				fSubpixelPrecise;
			bool				fIdentityTransform;
			BAffineTransform	fTransform;
			float				fPenSize;
			const BRegion*		fClippingRegion;
			drawing_mode		fDrawingMode;
			source_alpha		fAlphaSrcMode;
			alpha_function		fAlphaFncMode;
			cap_mode			fLineCapMode;
			join_mode			fLineJoinMode;
			float				fMiterLimit;
			int32				fFillRule;
			pattern				fPattern;
			rgb_color			fHighColor;
			rgb_color			fLowColor;
			int32				fPatternX;
			int32				fPatternY;
			int32				fRendererX;
			int32				fRendererY;
			ServerFont			fFont;
			bool				fAntialiasText;
			bool				fHasAlphaMask;
			AlphaMask*			fAlphaMask;
};


#endif	// SKIA_PAINTER_H
