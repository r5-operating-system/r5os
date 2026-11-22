/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 step 4 (docs/APP-SERVER-GPU-DRAWING.md, part 6, "Text"): text on the
 * GPU. The glyphs and their positions are ServerFont's, from Haiku's
 * GlyphLayoutEngine (decision 5), so the layout is the same in both modes;
 * Skia rasterises the letters from the same font files (grey, aliased, or
 * its own subpixel text, Q5), and the R5 shader blends them in the current
 * drawing mode, as AGGTextRenderer does with Painter's mode.
 */


#include "SkiaPainter.h"

#include <math.h>
#include <stdio.h>

#include <Autolock.h>
#include <String.h>

#include <map>
#include <string>
#include <vector>

#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_empty.h"

#include <agg_trans_affine.h>

#include "FontCacheEntry.h"
#include "FontManager.h"
#include "FontStyle.h"
#include "GlobalFontManager.h"
#include "GlobalSubpixelSettings.h"
#include "GlyphLayoutEngine.h"
#include "IntRect.h"


/*!	One SkTypeface per font file (or per font a program loaded from
	memory), made by the empty custom font manager from the same bytes
	FreeType reads for ServerFont.
*/
static sk_sp<SkTypeface>
typeface_for(const char* path, long faceIndex, const FT_Byte* data,
	uint32 dataSize)
{
	static BLocker sLock("skia typefaces");
	static std::map<std::string, sk_sp<SkTypeface>> sTypefaces;
	static sk_sp<SkFontMgr> sManager = SkFontMgr_New_Custom_Empty();

	char key[B_PATH_NAME_LENGTH + 64];
	if (data != NULL && dataSize > 0) {
		snprintf(key, sizeof(key), "mem:%p:%" B_PRIu32 ":%ld", data,
			dataSize, faceIndex);
	} else if (path != NULL && path[0] != '\0')
		snprintf(key, sizeof(key), "%s:%ld", path, faceIndex);
	else
		return nullptr;

	BAutolock _(sLock);
	auto found = sTypefaces.find(key);
	if (found != sTypefaces.end())
		return found->second;
	sk_sp<SkTypeface> typeface;
	if (data != NULL && dataSize > 0) {
		typeface = sManager->makeFromData(SkData::MakeWithCopy(data,
			dataSize), (int)faceIndex);
	} else
		typeface = sManager->makeFromFile(path, (int)faceIndex);
	sTypefaces[key] = typeface;
	return typeface;
}


static sk_sp<SkTypeface>
typeface_for(const ServerFont& font)
{
	return typeface_for(font.Path(), font.FaceIndex(), font.FontData(),
		font.FontDataSize());
}


/*!	GlyphLayoutEngine::PopulateFallbacks(): Haiku's fallback families, in
	its order. The first that has the character gives the glyph.
*/
static sk_sp<SkTypeface>
fallback_typeface_for(const ServerFont& font, uint32 charCode,
	SkGlyphID* _glyph)
{
	static const char* kFallbacks[] = {
		"Noto Sans", "Noto Sans Thai", "Noto Sans CJK JP",
		"Noto Sans Symbols", "Noto Sans Symbols2", "Noto Color Emoji", NULL
	};
	for (int i = 0; kFallbacks[i] != NULL; i++) {
		if (!gFontManager->Lock())
			return nullptr;
		FontStyle* style = gFontManager->GetStyle(kFallbacks[i],
			font.Style());
		if (style == NULL)
			style = gFontManager->GetStyle(kFallbacks[i], "Regular");
		BString path;
		long faceIndex = 0;
		if (style != NULL) {
			path = style->Path();
			faceIndex = style->FreeTypeFace()->face_index;
		}
		gFontManager->Unlock();
		if (style == NULL)
			continue;
		sk_sp<SkTypeface> typeface = typeface_for(path.String(), faceIndex,
			NULL, 0);
		if (typeface == nullptr)
			continue;
		SkGlyphID glyph = typeface->unicharToGlyph((SkUnichar)charCode);
		if (glyph != 0) {
			*_glyph = glyph;
			return typeface;
		}
	}
	return nullptr;
}


/*!	A GlyphLayoutEngine consumer that keeps each character and its pen
	position instead of drawing it.
*/
class GlyphCollector {
public:
	struct Glyph {
		uint32	charCode;
		double	x;
		double	y;
	};

	GlyphCollector()
		:
		fBounds(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN),
		fEndX(0),
		fEndY(0)
	{
	}

	bool NeedsVector() { return false; }
	void Start() {}
	void Finish(double x, double y) { fEndX = x; fEndY = y; }
	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y)
	{
	}

	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
		double advanceY)
	{
		Glyph g = { charCode, x, y };
		fGlyphs.push_back(g);
		// AGGTextRenderer's glyph box, for underline and strikeout
		const agg::rect_i& r = glyph->bounds;
		if (r.is_valid()) {
			fBounds = fBounds | IntRect(int32(r.x1 + x), int32(r.y1 + y - 1),
				int32(r.x2 + x + 1), int32(r.y2 + y + 1));
		}
		return true;
	}

	std::vector<Glyph>	fGlyphs;
	IntRect				fBounds;
	double				fEndX;
	double				fEndY;
};


struct GlyphRun {
	sk_sp<SkTypeface>		typeface;
	std::vector<SkGlyphID>	glyphs;
	std::vector<SkPoint>	positions;
};


/*!	AGGTextRenderer::StringRenderer::_DrawHorizontalLine(): from the left to
	the right of the glyph boxes, size / 12 wide.
*/
void
SkiaPainter::_DrawTextLines(SkCanvas* canvas, const GlyphCollector& collector,
	const SkPaint& ink)
{
	SkPaint line;
	line.setColor(ink.getColor());
	line.setAntiAlias(true);
	line.setStyle(SkPaint::kStroke_Style);
	line.setStrokeWidth(fFont.Size() / 12.0f);
	float y = (float)collector.fEndY;
	float left = collector.fBounds.left + 0.5f;
	float right = collector.fBounds.right + 0.5f;
	if ((fFont.Face() & B_UNDERSCORE_FACE) != 0)
		canvas->drawLine(left, y + 2.5f, right, y + 2.5f, line);
	if ((fFont.Face() & B_STRIKEOUT_FACE) != 0) {
		font_height height;
		fFont.GetHeight(height);
		float strike = y - (height.ascent + height.descent) / 4 + 0.5f;
		canvas->drawLine(left, strike, right, strike, line);
	}
}


/*!	Painter::DrawString() and AGGTextRenderer::RenderString(). */
BRect
SkiaPainter::DrawString(const char* string, uint32 length, BPoint baseLine,
	const escapement_delta* delta, const BPoint* offsets, double* _advance,
	BPoint* _pen)
{
	if (fClippingRegion == NULL || !fClippingRegion->Frame().IsValid()
		|| fTarget == NULL || !fTarget->HasSurface())
		return BRect(0, 0, -1, -1);

	struct TextProf {
		bigtime_t start;
		TextProf() : start(SkiaGpu::Debug() ? system_time() : 0) {}
		~TextProf() {
			if (start != 0) {
				SkiaGpu::Profile(SkiaGpu::kCountText);
				SkiaGpu::Profile(SkiaGpu::kTimeText, system_time() - start);
			}
		}
	} textProf;

	sk_sp<SkTypeface> typeface = typeface_for(fFont);
	if (typeface == nullptr)
		return BRect(0, 0, -1, -1);

	GlyphCollector collector;
	if (!GlyphLayoutEngine::LayoutGlyphs(collector, fFont, string, length,
			INT32_MAX, offsets != NULL ? NULL : delta, fFont.Spacing(),
			offsets, NULL)
		|| collector.fGlyphs.empty()) {
		return BRect(0, 0, -1, -1);
	}
	if (_advance != NULL)
		*_advance = collector.fEndX;
	if (_pen != NULL)
		_pen->Set(collector.fEndX, collector.fEndY);

	// AGGTextRenderer: shear and rotation, the baseline, the view's
	// transform; with offsets the positions are absolute
	if (offsets == NULL && !fSubpixelPrecise) {
		baseLine.x = roundf(baseLine.x);
		baseLine.y = roundf(baseLine.y);
	}
	agg::trans_affine text;
	text *= agg::trans_affine_skewing((90.0 - fFont.Shear()) * M_PI / 180.0,
		0.0);
	text *= agg::trans_affine_rotation(-fFont.Rotation() * M_PI / 180.0);
	if (offsets == NULL)
		text *= agg::trans_affine_translation(baseLine.x, baseLine.y);
	if (!fIdentityTransform) {
		text *= agg::trans_affine(fTransform.sx, fTransform.shy,
			fTransform.shx, fTransform.sy, fTransform.tx, fTransform.ty);
	}
	double m[6];
	text.store_to(m);
	SkMatrix matrix = SkMatrix::MakeAll(m[0], m[2], m[4], m[1], m[3], m[5],
		0, 0, 1);

	// the Antialiasing tab of Appearance (GlobalSubpixelSettings.h); step
	// 5b by default: light hinting and clean grayscale anti-aliasing, the
	// positions stay ServerFont's
	bool classic = SkiaPainter::Classic();
	bool subpixel = classic && gSubpixelAntialiasing && fAntialiasText;
	SkFont font(typeface, fFont.Size());
	font.setSubpixel(fSubpixelPrecise);
	font.setEdging(!fAntialiasText ? SkFont::Edging::kAlias
		: subpixel ? SkFont::Edging::kSubpixelAntiAlias
		: SkFont::Edging::kAntiAlias);
	if (classic) {
		font.setHinting(!fFont.Hinting() ? SkFontHinting::kNone
			: subpixel ? SkFontHinting::kFull : SkFontHinting::kNormal);
	} else
		font.setHinting(SkFontHinting::kSlight);
	font.setEmbolden(fFont.FalseBoldWidth() > 0);

	// one run per typeface: the font, or the fallback the glyph came from
	std::vector<GlyphRun> runs;
	for (const GlyphCollector::Glyph& g : collector.fGlyphs) {
		SkGlyphID id = typeface->unicharToGlyph((SkUnichar)g.charCode);
		sk_sp<SkTypeface> face = typeface;
		if (id == 0) {
			SkGlyphID fallbackId = 0;
			sk_sp<SkTypeface> fallback = fallback_typeface_for(fFont,
				g.charCode, &fallbackId);
			if (fallback != nullptr) {
				face = fallback;
				id = fallbackId;
			}
		}
		if (runs.empty() || runs.back().typeface != face) {
			GlyphRun run;
			run.typeface = face;
			runs.push_back(run);
		}
		runs.back().glyphs.push_back(id);
		runs.back().positions.push_back(SkPoint::Make((float)g.x,
			(float)g.y));
	}

	// the ink, in screen pixels
	SkRect local = SkRect::MakeEmpty();
	for (GlyphRun& run : runs) {
		SkFont runFont(font);
		runFont.setTypeface(run.typeface);
		std::vector<SkRect> boxes(run.glyphs.size());
		runFont.getBounds(run.glyphs, boxes, nullptr);
		for (size_t i = 0; i < boxes.size(); i++)
			local.join(boxes[i].makeOffset(run.positions[i]));
	}
	bool lines = (fFont.Face() & (B_UNDERSCORE_FACE | B_STRIKEOUT_FACE)) != 0
		&& collector.fBounds.IsValid();
	if (lines) {
		local.join(SkRect::MakeLTRB(collector.fBounds.left,
			collector.fBounds.top, collector.fBounds.right + 1,
			collector.fBounds.bottom + 4));
	}
	if (local.isEmpty())
		return BRect(0, 0, -1, -1);
	SkRect device = matrix.mapRect(local);
	SkIRect bounds = device.roundOut();
	bounds.outset(2, 2);
	BRect touched = _Clipped(BRect(device.fLeft, device.fTop, device.fRight,
		device.fBottom));
	SkIRect surfaceBounds = SkIRect::MakeXYWH(fTarget->Left(),
		fTarget->Top(), fTarget->Width(), fTarget->Height());
	clipping_rect clipFrame = fClippingRegion->FrameInt();
	SkIRect clip = SkIRect::MakeLTRB(clipFrame.left, clipFrame.top,
		clipFrame.right + 1, clipFrame.bottom + 1);
	if (!bounds.intersect(surfaceBounds) || !bounds.intersect(clip))
		return touched;

	// B_OP_COPY and B_OP_OVER on an opaque image: the letters straight into
	// it with Skia's own blending (owner, 15 Nov 2026), Painter's
	// SolidPatternGuard: the high colour
	bool directDraw;
	SkColor directColor;
	if (_DirectColor(true, &directDraw, &directColor)) {
		SkPaint ink;
		ink.setColor(directColor);
		ink.setAntiAlias(fAntialiasText);
		_DirectDraw(bounds, matrix, [&](SkCanvas* canvas) {
			for (GlyphRun& run : runs) {
				SkFont runFont(font);
				runFont.setTypeface(run.typeface);
				canvas->drawGlyphs(run.glyphs, run.positions,
					SkPoint::Make(0, 0), runFont, ink);
			}
			if (lines)
				_DrawTextLines(canvas, collector, ink);
		});
		return touched;
	}

	SkSurface* scratch = fTarget->Scratch(subpixel);
	if (scratch == NULL)
		return touched;
	SkCanvas* canvas = scratch->getCanvas();
	canvas->save();
	canvas->clipIRect(bounds);
	// grey: the text's own colour on clear, the coverage in the alpha (the
	// colour picks Skia's text contrast curve, as the direct draw did);
	// subpixel: black on opaque white, so Skia's own subpixel coverage
	// comes out per channel
	canvas->clear(subpixel ? SK_ColorWHITE : SK_ColorTRANSPARENT);
	canvas->concat(matrix);
	SkPaint ink;
	ink.setColor(subpixel ? SK_ColorBLACK : SkColorSetRGB(fHighColor.red,
		fHighColor.green, fHighColor.blue));
	ink.setAntiAlias(fAntialiasText);
	for (GlyphRun& run : runs) {
		SkFont runFont(font);
		runFont.setTypeface(run.typeface);
		canvas->drawGlyphs(run.glyphs, run.positions, SkPoint::Make(0, 0),
			runFont, ink);
	}
	if (lines)
		_DrawTextLines(canvas, collector, ink);
	canvas->restore();

	sk_sp<SkImage> coverage = scratch->makeImageSnapshot(bounds.makeOffset(
		-fTarget->Left(), -fTarget->Top()));
	if (coverage == nullptr)
		return touched;
	// Painter's SolidPatternGuard: text uses the high colour
	Source source;
	source.forceSolidHigh = true;
	source.lcd = subpixel;
	source.text = true;
	_Composite(bounds, coverage, source);
	return touched;
}
