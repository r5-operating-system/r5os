/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 (docs/APP-SERVER-GPU-DRAWING.md, part 4): Painter's drawing calls on
 * the GPU. Each function says which Painter function it follows; the
 * geometry is that function's, the AGG rasterisation is replaced by Skia
 * coverage and the R5 shader (SkiaShaders.h).
 *
 * The geometry follows Haiku's src/servers/app/drawing/Painter/Painter.cpp,
 * Copyright 2005-2014 Stephan Aßmus, 2008 Andrej Spielmann, 2009 Christian
 * Packmann, 2015 Julian Harnath, distributed under the MIT License; no
 * code of it is copied here verbatim.
 */


#include "SkiaPainter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Bitmap.h>
#include <GradientConic.h>
#include <GradientDiamond.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <ShapePrivate.h>

#include "include/core/SkBitmap.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/Image.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/Surface.h"

#include <agg_color_rgba.h>
#include <agg_trans_affine.h>

#include "AlphaMask.h"
#include "DrawState.h"
#include "ServerBitmap.h"
#include "GlobalSubpixelSettings.h"
#include "drawing_support.h"

#include "SkiaGpu.h"
#include "SkiaShaders.h"


static const pattern kR5SolidHigh = { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff } };
static const pattern kR5SolidLow = { { 0, 0, 0, 0, 0, 0, 0, 0 } };


static BRegion
region_of(const clipping_rect& rect)
{
	BRegion region;
	region.Set(rect);
	return region;
}


static bool
same_pattern(const pattern& a, const pattern& b)
{
	return memcmp(a.data, b.data, 8) == 0;
}


static SkImageInfo
target_info(int32 width, int32 height)
{
	// Haiku's bytes, as they are: Skia never converts (part 4, "Straight
	// alpha in the image").
	return SkImageInfo::Make(width, height, kBGRA_8888_SkColorType,
		kPremul_SkAlphaType, nullptr);
}


//! A texture from CPU memory (counted for the step 5c profile).
static sk_sp<SkImage>
upload_texture(skgpu::graphite::Recorder* recorder, sk_sp<SkImage> image)
{
	SkiaGpu::Profile(SkiaGpu::kCountUpload);
	return SkImages::TextureFromImage(recorder, image.get());
}


static sk_sp<SkRuntimeEffect>
blend_effect()
{
	static sk_sp<SkRuntimeEffect> sEffect;
	static bool sTried = false;
	if (!sTried) {
		sTried = true;
		SkRuntimeEffect::Result result
			= SkRuntimeEffect::MakeForShader(SkString(kR5BlendSkSL));
		if (!result.effect) {
			fprintf(stderr, "app_server: r5 blend shader: %s\n",
				result.errorText.c_str());
		}
		sEffect = result.effect;
	}
	return sEffect;
}


/*!	A colour written as it is: an SkPaint colour is premultiplied by Skia,
	which turns the colour of an alpha 0 pixel into black; Haiku keeps it.
*/
static sk_sp<SkRuntimeEffect>
color_effect()
{
	static sk_sp<SkRuntimeEffect> sEffect;
	static bool sTried = false;
	if (!sTried) {
		sTried = true;
		SkRuntimeEffect::Result result = SkRuntimeEffect::MakeForShader(
			SkString("uniform float4 color;"
				"half4 main(float2 p) { return half4(color / 255.0); }"));
		sEffect = result.effect;
	}
	return sEffect;
}


static sk_sp<SkShader>
color_shader(const rgb_color& color)
{
	sk_sp<SkRuntimeEffect> effect = color_effect();
	if (effect == nullptr)
		return nullptr;
	SkRuntimeShaderBuilder builder(effect);
	builder.uniform("color") = SkV4{ (float)color.red, (float)color.green,
		(float)color.blue, (float)color.alpha };
	return builder.makeShader();
}


static sk_sp<SkRuntimeEffect>
gradient_effect()
{
	static sk_sp<SkRuntimeEffect> sEffect;
	static bool sTried = false;
	if (!sTried) {
		sTried = true;
		SkRuntimeEffect::Result result
			= SkRuntimeEffect::MakeForShader(SkString(kR5GradientSkSL));
		if (!result.effect) {
			fprintf(stderr, "app_server: r5 gradient shader: %s\n",
				result.errorText.c_str());
		}
		sEffect = result.effect;
	}
	return sEffect;
}


static bool sClassic = false;
static bool sLinearEdges = false;


/*static*/ void
SkiaPainter::SetLinearEdges(bool linear)
{
	sLinearEdges = linear;
}


/*static*/ void
SkiaPainter::SetClassic(bool classic)
{
	sClassic = classic;
}


static bool sHardwareBlend = true;


/*static*/ void
SkiaPainter::SetHardwareBlend(bool on)
{
	sHardwareBlend = on;
}


/*static*/ bool
SkiaPainter::Classic()
{
	return sClassic;
}


/*!	The text settings of an image: classic is linear coverage as AGG's;
	otherwise Skia's own contrast and gamma for clean grayscale text.
*/
static SkSurfaceProps
text_props()
{
	SkPixelGeometry geometry = gSubpixelOrderingRGB ? kRGB_H_SkPixelGeometry
		: kBGR_H_SkPixelGeometry;
	if (sClassic)
		return SkSurfaceProps(0, geometry, 0, 1);
	// step 5b: Skia's own text gamma (1.2) with a little more contrast than
	// its default 0.2, so light-hinted grayscale text at 9 to 12 points does
	// not go pale (judged on the side-by-side sheet, 16 Nov 2026)
	return SkSurfaceProps(0, geometry, 0.4f, 1.2f);
}


//	#pragma mark - SkiaTarget


//!	M13 step 2: Graphite is done with a window image (SkiaTarget::SetSurface).
static void
release_image(void* context)
{
	((SkiaImage*)context)->ReleaseReference();
}


SkiaTarget::SkiaTarget(SkiaGpu* gpu)
	:
	fGpu(gpu),
	fLock("skia target"),
	fRecorder(NULL),
	fScratchLcdRGB(true),
	fLeft(0),
	fTop(0),
	fWidth(0),
	fHeight(0),
	fRecorded(false),
	fFirstDraw(0),
	fHeldSince(0),
	fUploadPending(false),
	fOpaque(false)
{
	if (fGpu != NULL)
		fRecorder = (skgpu::graphite::Recorder*)fGpu->NewRecorder();
}


SkiaTarget::~SkiaTarget()
{
	// The owner removed this target from SkiaGpu first.
	fSurface.reset();
	fScratch.reset();
	fScratchLcd.reset();
	if (fGpu != NULL && fRecorder != NULL)
		fGpu->DeleteRecorder(fRecorder);
	fRecorder = NULL;
}


SkCanvas*
SkiaTarget::Canvas() const
{
	return fSurface != nullptr ? fSurface->getCanvas() : NULL;
}


bool
SkiaTarget::SetSurface(int32 width, int32 height, int32 left, int32 top,
	const uint8* initialBits, uint32 bytesPerRow)
{
	if (fRecorder == NULL)
		return false;
	if (fSurface != nullptr && width == fWidth && height == fHeight) {
		SetOrigin(left, top);
		return true;
	}

	// the text settings (step 5b) for text drawn straight into the image
	SkSurfaceProps props = text_props();
	sk_sp<SkSurface> surface;
	BReference<SkiaImage> image;
	if (UsesSharedImage()) {
		// M13 step 2: an image the compositor samples. Graphite keeps a
		// reference until its last work with it is done (the release
		// proc, also called when the wrap fails).
		image.SetTo(fGpu != NULL ? fGpu->NewImage(width, height) : NULL,
			true);
		if (!image.IsSet())
			return false;
		image->AcquireReference();
		surface = SkSurfaces::WrapBackendTexture(fRecorder,
			*(const skgpu::graphite::BackendTexture*)image->Texture(),
			kBGRA_8888_SkColorType, nullptr, &props, &release_image,
			image.Get());
	} else {
		surface = SkSurfaces::RenderTarget(fRecorder,
			target_info(width, height), skgpu::Mipmapped::kNo, &props);
	}
	if (surface == nullptr)
		return false;

	SkCanvas* canvas = surface->getCanvas();
	if (image.IsSet() && initialBits == NULL) {
		// a new image has no defined pixels: transparent, as the
		// compositor's new buffers are
		canvas->clear(SK_ColorTRANSPARENT);
	}
	if (initialBits != NULL) {
		SkPixmap pixmap(target_info(width, height), initialBits, bytesPerRow);
		surface->writePixels(pixmap, 0, 0);
	}
	if (fSurface != nullptr) {
		// keep what was drawn where the old and the new image overlap on
		// screen, as the compositor does with the buffers
		sk_sp<SkImage> old = fSurface->makeImageSnapshot();
		if (old != nullptr) {
			SkPaint copy;
			copy.setBlendMode(SkBlendMode::kSrc);
			canvas->drawImage(old, (float)(fLeft - left), (float)(fTop - top),
				SkSamplingOptions(), &copy);
		}
	}
	fSurface = surface;
	fImage = image;
	fScratch.reset();
	fScratchLcd.reset();
	fWidth = width;
	fHeight = height;
	fLeft = left;
	fTop = top;
	canvas->resetMatrix();
	canvas->translate((float)-fLeft, (float)-fTop);
	return true;
}


bool
SkiaTarget::Upload(const uint8* bits, uint32 bytesPerRow)
{
	if (fSurface == nullptr || bits == NULL)
		return false;
	SkPixmap pixmap(target_info(fWidth, fHeight), bits, bytesPerRow);
	fSurface->writePixels(pixmap, 0, 0);
	return true;
}


void
SkiaTarget::UploadIfPending()
{
	if (!fUploadPending)
		return;
	fUploadPending = false;
	const uint8* bits;
	uint32 bytesPerRow;
	if (UploadSource(&bits, &bytesPerRow))
		Upload(bits, bytesPerRow);
}


void
SkiaTarget::SetOrigin(int32 left, int32 top)
{
	// the window moved with its image: what is still to be copied out
	// moved with it
	fDamage.OffsetBy(left - fLeft, top - fTop);
	fLeft = left;
	fTop = top;
	if (fSurface != nullptr) {
		SkCanvas* canvas = fSurface->getCanvas();
		canvas->resetMatrix();
		canvas->translate((float)-fLeft, (float)-fTop);
	}
	if (fScratch != nullptr) {
		SkCanvas* canvas = fScratch->getCanvas();
		canvas->resetMatrix();
		canvas->translate((float)-fLeft, (float)-fTop);
	}
	if (fScratchLcd != nullptr) {
		SkCanvas* canvas = fScratchLcd->getCanvas();
		canvas->resetMatrix();
		canvas->translate((float)-fLeft, (float)-fTop);
	}
}


void
SkiaTarget::ReleaseSurface()
{
	fSurface.reset();
	fImage.Unset();
	fScratch.reset();
	fScratchLcd.reset();
	fWidth = 0;
	fHeight = 0;
}


void
SkiaTarget::DropGpu()
{
	// SkiaGpu::Shutdown(), with its lock held (BLocker is recursive):
	// nothing of Graphite outlives the device
	fSurface.reset();
	fImage.Unset();
	fScratch.reset();
	fScratchLcd.reset();
	fDamage.MakeEmpty();
	fRecorded = false;
	fTextures.clear();
	fTextureBytes = 0;
	fGradients.clear();
	if (fGpu != NULL && fRecorder != NULL)
		fGpu->DeleteRecorder(fRecorder);
	fRecorder = NULL;
}


static const size_t kTextureCount = 64;
static const size_t kTextureBytes = 96 * 1024 * 1024;
static const int32 kTextureLargest = 64 * 1024 * 1024;


/*!	M13 step 1: a fast 64-bit hash of a bitmap's bytes, so a cached texture
	is checked without a copy of the bitmap and a memcmp against it. A
	program writes its BBitmap's memory at any time, so every byte counts:
	four lanes of 8-byte words (multiply and rotate), the tail byte by
	byte. Not for an attacker: a collision only shows the program's own
	old picture.
*/
static uint64
bitmap_hash(const uint8* bits, int32 length)
{
	const uint64 kPrime = 0x9e3779b97f4a7c15ull;
	uint64 lanes[4] = { kPrime, kPrime * 3, kPrime * 5, kPrime * 7 };
	int32 words = length / 8;
	int32 i = 0;
	for (; i + 4 <= words; i += 4) {
		for (int lane = 0; lane < 4; lane++) {
			uint64 word;
			memcpy(&word, bits + (size_t)(i + lane) * 8, 8);
			uint64 h = (lanes[lane] ^ word) * kPrime;
			lanes[lane] = (h << 31) | (h >> 33);
		}
	}
	uint64 hash = (uint64)length;
	for (int lane = 0; lane < 4; lane++)
		hash = (hash ^ lanes[lane]) * kPrime;
	for (; i < words; i++) {
		uint64 word;
		memcpy(&word, bits + (size_t)i * 8, 8);
		hash = (hash ^ word) * kPrime;
	}
	for (int32 j = words * 8; j < length; j++)
		hash = (hash ^ bits[j]) * kPrime;
	return hash ^ (hash >> 29);
}


sk_sp<SkImage>
SkiaTarget::FindTexture(const void* id, const uint8* bits, int32 length,
	int32 width, int32 height, int32 bytesPerRow, int32 space, bool raw)
{
	for (Texture& texture : fTextures) {
		if (texture.id != id || texture.width != width
			|| texture.height != height || texture.bytesPerRow != bytesPerRow
			|| texture.space != space || texture.raw != raw
			|| texture.length != length)
			continue;
		if (texture.hash != bitmap_hash(bits, length))
			return nullptr;
		texture.used = system_time();
		return texture.image;
	}
	return nullptr;
}


/*!	M13 step 1: FillRectVerticalGradient() and _GradientShader() made and
	uploaded a new image of their colours on every draw. The colours are
	still computed (cheap); the same colours reuse the uploaded image.
	Compared byte for byte (a ramp is 1 KB, a column a few KB).
*/
sk_sp<SkImage>
SkiaTarget::GradientImage(const SkBitmap& colors)
{
	static const size_t kGradientCount = 32;
	static const size_t kGradientLargest = 64 * 1024;
	const uint8* bits = (const uint8*)colors.getPixels();
	size_t length = colors.computeByteSize();
	if (bits == NULL || fRecorder == NULL)
		return nullptr;
	for (Gradient& gradient : fGradients) {
		if (gradient.width == colors.width()
			&& gradient.height == colors.height()
			&& gradient.bits.size() == length
			&& memcmp(gradient.bits.data(), bits, length) == 0) {
			gradient.used = system_time();
			return gradient.image;
		}
	}
	sk_sp<SkImage> image = upload_texture(fRecorder, colors.asImage());
	if (image == nullptr || length > kGradientLargest)
		return image;
	if (fGradients.size() >= kGradientCount) {
		size_t oldest = 0;
		for (size_t i = 1; i < fGradients.size(); i++) {
			if (fGradients[i].used < fGradients[oldest].used)
				oldest = i;
		}
		fGradients.erase(fGradients.begin() + oldest);
	}
	try {
		Gradient gradient;
		gradient.bits.assign(bits, bits + length);
		gradient.width = colors.width();
		gradient.height = colors.height();
		gradient.image = image;
		gradient.used = system_time();
		fGradients.push_back(std::move(gradient));
	} catch (...) {
	}
	return image;
}


void
SkiaTarget::KeepTexture(const void* id, const uint8* bits, int32 length,
	int32 width, int32 height, int32 bytesPerRow, int32 space, bool raw,
	const sk_sp<SkImage>& image)
{
	if (length <= 0 || length > kTextureLargest || image == nullptr)
		return;
	for (size_t i = 0; i < fTextures.size(); i++) {
		if (fTextures[i].id == id && fTextures[i].raw == raw) {
			fTextureBytes -= fTextures[i].length;
			fTextures.erase(fTextures.begin() + i);
			break;
		}
	}
	while (!fTextures.empty() && (fTextures.size() >= kTextureCount
			|| fTextureBytes + length > kTextureBytes)) {
		size_t oldest = 0;
		for (size_t i = 1; i < fTextures.size(); i++) {
			if (fTextures[i].used < fTextures[oldest].used)
				oldest = i;
		}
		fTextureBytes -= fTextures[oldest].length;
		fTextures.erase(fTextures.begin() + oldest);
	}
	Texture texture;
	texture.id = id;
	texture.width = width;
	texture.height = height;
	texture.bytesPerRow = bytesPerRow;
	texture.space = space;
	texture.raw = raw;
	try {
		texture.length = length;
		texture.hash = bitmap_hash(bits, length);
		texture.image = image;
		texture.used = system_time();
		fTextureBytes += length;
		fTextures.push_back(std::move(texture));
	} catch (...) {
	}
}


SkSurface*
SkiaTarget::Scratch(bool lcd)
{
	if (lcd) {
		if (fScratchLcd != nullptr && fScratchLcdRGB != gSubpixelOrderingRGB)
			fScratchLcd.reset();
		if (fScratchLcd == nullptr && fRecorder != NULL && fWidth > 0) {
			fScratchLcdRGB = gSubpixelOrderingRGB;
			SkSurfaceProps props = text_props();
			fScratchLcd = SkSurfaces::RenderTarget(fRecorder,
				target_info(fWidth, fHeight), skgpu::Mipmapped::kNo, &props);
			if (fScratchLcd != nullptr) {
				SkCanvas* canvas = fScratchLcd->getCanvas();
				canvas->translate((float)-fLeft, (float)-fTop);
			}
		}
		return fScratchLcd.get();
	}
	if (fScratch == nullptr && fRecorder != NULL && fWidth > 0) {
		SkSurfaceProps props = text_props();
		fScratch = SkSurfaces::RenderTarget(fRecorder,
			target_info(fWidth, fHeight), skgpu::Mipmapped::kNo, &props);
		if (fScratch != nullptr) {
			SkCanvas* canvas = fScratch->getCanvas();
			canvas->translate((float)-fLeft, (float)-fTop);
		}
	}
	return fScratch.get();
}


void
SkiaTarget::AddDamage(const BRegion& screen)
{
	fDamage.Include(&screen);
	MarkDrawn();
}


void
SkiaTarget::AddDamage(const BRect& screen)
{
	fDamage.Include(screen);
	MarkDrawn();
}


void
SkiaTarget::MarkDrawn()
{
	if (!fRecorded)
		fFirstDraw = system_time();
	fRecorded = true;
}


void
SkiaTarget::SetHeld(bool held)
{
	if (!held)
		fHeldSince = 0;
	else if (fHeldSince == 0)
		fHeldSince = system_time();
}


bool
SkiaTarget::Held(bigtime_t now) const
{
	// a program that never ends its update does not stop the screen:
	// after 100 ms its drawing goes as it is
	return fHeldSince != 0 && now - fHeldSince < 100000;
}


std::unique_ptr<skgpu::graphite::Recording>
SkiaTarget::Snap(BRegion& screenDamage, bigtime_t* _firstDraw)
{
	screenDamage = fDamage;
	fDamage.MakeEmpty();
	if (_firstDraw != NULL)
		*_firstDraw = fFirstDraw;
	fRecorded = false;
	if (fRecorder == NULL)
		return nullptr;
	return fRecorder->snap();
}


//	#pragma mark - SkiaPainter




SkiaPainter::SkiaPainter()
	:
	fTarget(NULL),
	fScale(1.0f),
	fSubpixelPrecise(false),
	fIdentityTransform(true),
	fPenSize(1.0f),
	fClippingRegion(NULL),
	fDrawingMode(B_OP_COPY),
	fAlphaSrcMode(B_PIXEL_ALPHA),
	fAlphaFncMode(B_ALPHA_OVERLAY),
	fLineCapMode(B_BUTT_CAP),
	fLineJoinMode(B_MITER_JOIN),
	fMiterLimit(B_DEFAULT_MITER_LIMIT),
	fFillRule(B_NONZERO),
	fPattern(kR5SolidHigh),
	fPatternX(0),
	fPatternY(0),
	fRendererX(0),
	fRendererY(0),
	fAntialiasText(true),
	fHasAlphaMask(false),
	fAlphaMask(NULL)
{
	fHighColor = (rgb_color){ 0, 0, 0, 255 };
	fLowColor = (rgb_color){ 255, 255, 255, 255 };
}


SkiaPainter::~SkiaPainter()
{
}


void
SkiaPainter::SetTarget(SkiaTarget* target)
{
	fTarget = target;
}


void
SkiaPainter::SetDrawState(const DrawState* state, int32 xOffset,
	int32 yOffset)
{
	// Painter::SetDrawState()
	SetTransform(state->CombinedTransform(), xOffset, yOffset);
	fPenSize = state->PenSize();
	SetFont(state);
	fSubpixelPrecise = state->SubPixelPrecise();
	// part 4, "Alpha masks": the mask itself is drawn by Skia in the GPU
	// mode (AlphaMask.cpp, skia_offscreen_engine()); here it only limits
	// the coverage, as Painter's masked scanline does
	fAlphaMask = state->GetAlphaMask();
	fHasAlphaMask = fAlphaMask != NULL;
	fDrawingMode = state->GetDrawingMode();
	fAlphaSrcMode = state->AlphaSrcMode();
	fAlphaFncMode = state->AlphaFncMode();
	fPattern = state->GetPattern().GetPattern();
	fPatternX = xOffset;
	fPatternY = yOffset;
	fLineCapMode = state->LineCapMode();
	fLineJoinMode = state->LineJoinMode();
	fMiterLimit = state->MiterLimit();
	fFillRule = state->FillRule();
	fHighColor = state->HighColor();
	fLowColor = state->LowColor();
}


void
SkiaPainter::ConstrainClipping(const BRegion* region)
{
	fClippingRegion = region;
}


void
SkiaPainter::SetTransform(BAffineTransform transform, int32 xOffset,
	int32 yOffset)
{
	// Painter::SetTransform(): translate(-offset), the transform,
	// translate(offset)
	fIdentityTransform = transform.IsIdentity() && fScale == 1.0f;
	if (fIdentityTransform) {
		fTransform.Reset();
		return;
	}
	// the same agg::trans_affine as Painter, so the numbers are its own
	agg::trans_affine combined = agg::trans_affine_translation(-xOffset,
		-yOffset);
	combined *= agg::trans_affine(transform.sx, transform.shy, transform.shx,
		transform.sy, transform.tx, transform.ty);
	combined *= agg::trans_affine_translation(xOffset, yOffset);
	// "Scaling later" point 1: the window's factor is the outermost matrix
	if (fScale != 1.0f)
		combined *= agg::trans_affine_scaling(fScale);
	double m[6];
	combined.store_to(m);
	fTransform = BAffineTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
}


void
SkiaPainter::SetStrokeMode(cap_mode cap, join_mode join, float miterLimit)
{
	fLineCapMode = cap;
	fLineJoinMode = join;
	fMiterLimit = miterLimit;
}


void
SkiaPainter::SetBlendingMode(source_alpha src, alpha_function fnc)
{
	fAlphaSrcMode = src;
	fAlphaFncMode = fnc;
}


void
SkiaPainter::SetFont(const ServerFont& font)
{
	fFont = font;
	fAntialiasText = (font.Flags() & B_DISABLE_ANTIALIASING) == 0;
}


void
SkiaPainter::SetFont(const DrawState* state)
{
	fFont = state->Font();
	fAntialiasText = !state->ForceFontAliasing()
		&& (state->Font().Flags() & B_DISABLE_ANTIALIASING) == 0;
}


void
SkiaPainter::SetRendererOffset(int32 x, int32 y)
{
	fRendererX = x;
	fRendererY = y;
}


//	#pragma mark - geometry (Painter.h, Painter.cpp)


float
SkiaPainter::_Align(float coord, bool round, bool centerOffset) const
{
	if (round)
		coord = (int32)coord;
	if (centerOffset)
		coord += 0.5;
	return coord;
}


void
SkiaPainter::_Align(BPoint* point, bool centerOffset) const
{
	_Align(point, !fSubpixelPrecise, centerOffset);
}


void
SkiaPainter::_Align(BPoint* point, bool round, bool centerOffset) const
{
	point->x = _Align(point->x, round, centerOffset);
	point->y = _Align(point->y, round, centerOffset);
}


BRect
SkiaPainter::_Clipped(const BRect& rect) const
{
	if (rect.IsValid() && fClippingRegion != NULL)
		return BRect(rect & fClippingRegion->Frame());
	return BRect(rect);
}


BRect
SkiaPainter::_TransformBounds(const BRect& rect) const
{
	if (fIdentityTransform)
		return rect;
	// Transformable::TransformBounds(): the box around the four corners
	BPoint corners[4] = { rect.LeftTop(), rect.RightTop(), rect.LeftBottom(),
		rect.RightBottom() };
	fTransform.Apply(corners, 4);
	BRect bounds(corners[0], corners[0]);
	for (int i = 1; i < 4; i++) {
		bounds.left = min_c(bounds.left, corners[i].x);
		bounds.top = min_c(bounds.top, corners[i].y);
		bounds.right = max_c(bounds.right, corners[i].x);
		bounds.bottom = max_c(bounds.bottom, corners[i].y);
	}
	return bounds;
}


SkMatrix
SkiaPainter::_Matrix() const
{
	if (fIdentityTransform)
		return SkMatrix::I();
	return SkMatrix::MakeAll(fTransform.sx, fTransform.shx, fTransform.tx,
		fTransform.shy, fTransform.sy, fTransform.ty, 0, 0, 1);
}


BRect
SkiaPainter::AlignRect(BRect rect) const
{
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	if (fSubpixelPrecise) {
		rect.right = ceilf(rect.right);
		rect.bottom = ceilf(rect.bottom);
	} else {
		rect.right = floorf(rect.right);
		rect.bottom = floorf(rect.bottom);
	}
	return rect;
}


BRect
SkiaPainter::ClipRect(BRect rect) const
{
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = ceilf(rect.right);
	rect.bottom = ceilf(rect.bottom);
	return _Clipped(rect);
}


BRect
SkiaPainter::TransformAndClipRect(BRect rect) const
{
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = ceilf(rect.right);
	rect.bottom = ceilf(rect.bottom);
	return _Clipped(_TransformBounds(rect));
}


BRect
SkiaPainter::AlignAndClipRect(BRect rect) const
{
	return _Clipped(AlignRect(rect));
}


BRect
SkiaPainter::TransformAlignAndClipRect(BRect rect) const
{
	return _Clipped(_TransformBounds(AlignRect(rect)));
}


void
SkiaPainter::AlignEllipseRect(BRect* rect, bool filled) const
{
	if (!fSubpixelPrecise) {
		align_rect_to_pixels(rect);
		rect->right++;
		rect->bottom++;
		if (!filled && fmodf(fPenSize, 2.0) != 0.0)
			rect->InsetBy(0.5, 0.5);
	}
}


BRect
SkiaPainter::Bounds() const
{
	if (fTarget == NULL)
		return BRect(0, 0, -1, -1);
	return BRect(fTarget->Left(), fTarget->Top(),
		fTarget->Left() + fTarget->Width() - 1,
		fTarget->Top() + fTarget->Height() - 1);
}


//	#pragma mark - the composite


void
SkiaPainter::_ClipRects(const SkIRect& bounds, BRegion& _rects) const
{
	_rects.MakeEmpty();
	if (fClippingRegion == NULL)
		return;
	_rects.Set(clipping_rect{ bounds.fLeft, bounds.fTop, bounds.fRight - 1,
		bounds.fBottom - 1 });
	_rects.IntersectWith(fClippingRegion);
}


/*!	Draws the R5 shader over bounds (screen pixels), inside the clipping
	region, with the coverage image (NULL: full coverage) and the source.
*/
bool
SkiaPainter::_Composite(const SkIRect& _bounds, const sk_sp<SkImage>& coverage,
	const Source& source)
{
	if (fTarget == NULL || !fTarget->HasSurface())
		return false;
	sk_sp<SkRuntimeEffect> effect = blend_effect();
	if (effect == nullptr)
		return false;

	SkIRect surfaceBounds = SkIRect::MakeXYWH(fTarget->Left(),
		fTarget->Top(), fTarget->Width(), fTarget->Height());
	// the coverage image was taken at the caller's bounds (review)
	SkIPoint coverageOrigin = SkIPoint::Make(_bounds.fLeft, _bounds.fTop);
	SkIRect bounds = _bounds;
	if (!bounds.intersect(surfaceBounds))
		return false;
	BRegion rects;
	_ClipRects(bounds, rects);
	if (rects.CountRects() == 0)
		return false;
	// only what is drawn needs the copy of the destination
	clipping_rect frame = rects.FrameInt();
	bounds = SkIRect::MakeLTRB(frame.left, frame.top, frame.right + 1,
		frame.bottom + 1);

	SkSurface* surface = fTarget->Surface();
	sk_sp<SkImage> dst = surface->makeImageSnapshot(bounds.makeOffset(
		-fTarget->Left(), -fTarget->Top()));
	if (dst == nullptr)
		return false;

	SkMatrix at = SkMatrix::Translate((float)bounds.fLeft,
		(float)bounds.fTop);
	SkSamplingOptions nearest(SkFilterMode::kNearest);

	SkRuntimeShaderBuilder builder(effect);
	builder.child("dstImage") = dst->makeShader(SkTileMode::kClamp,
		SkTileMode::kClamp, nearest, &at);
	sk_sp<SkShader> none = SkShaders::Color(SK_ColorTRANSPARENT);
	if (coverage != nullptr) {
		SkMatrix coverAt = SkMatrix::Translate((float)coverageOrigin.fX,
			(float)coverageOrigin.fY);
		builder.child("coverImage") = coverage->makeShader(SkTileMode::kDecal,
			SkTileMode::kDecal, nearest, &coverAt);
	} else
		builder.child("coverImage") = none;
	builder.child("srcColors") = source.colors != nullptr ? source.colors
		: none;
	sk_sp<SkShader> mask = fHasAlphaMask ? _MaskShader(bounds) : nullptr;
	builder.child("maskImage") = mask != nullptr ? mask : none;

	bool solidHigh = source.forceSolidHigh || same_pattern(fPattern,
		kR5SolidHigh);
	bool solidLow = !source.forceSolidHigh && same_pattern(fPattern,
		kR5SolidLow);
	int alphaKind = _AlphaKind();

	builder.uniform("mode") = (int)fDrawingMode;
	builder.uniform("alphaKind") = alphaKind;
	builder.uniform("solid") = (solidHigh || solidLow) ? 1 : 0;
	builder.uniform("solidLow") = solidLow ? 1 : 0;
	builder.uniform("useColors") = source.useColors ? 1 : 0;
	builder.uniform("hasCover") = coverage == nullptr ? 0
		: source.lcd ? 2 : 1;
	builder.uniform("hasMask") = mask != nullptr ? 1 : 0;
	builder.uniform("special") = source.special;
	builder.uniform("blendAlpha") = source.blendAlpha;
	rgb_color high = source.special == kSpecialBlendLine32
		? source.specialColor : fHighColor;
	builder.uniform("high") = SkV4{ (float)high.red, (float)high.green,
		(float)high.blue, (float)high.alpha };
	builder.uniform("low") = SkV4{ (float)fLowColor.red,
		(float)fLowColor.green, (float)fLowColor.blue,
		(float)fLowColor.alpha };
	const uint8* bits = fPattern.data;
	builder.uniform("pat0") = SkV4{ (float)bits[0], (float)bits[1],
		(float)bits[2], (float)bits[3] };
	builder.uniform("pat1") = SkV4{ (float)bits[4], (float)bits[5],
		(float)bits[6], (float)bits[7] };
	// PatternHandler::SetOffsets() keeps them as uint16; only & 7 counts
	builder.uniform("patOffset") = SkV2{ (float)(fPatternX & 7),
		(float)(fPatternY & 7) };
	builder.uniform("sampling") = source.sampling;
	// step 5b; text keeps Skia's own text contrast and gamma instead
	builder.uniform("linear") = !sClassic && !source.text ? 1 : 0;
	builder.uniform("destOrigin") = SkV2{ source.destOrigin[0],
		source.destOrigin[1] };
	builder.uniform("srcSize") = SkV2{ source.srcSize[0], source.srcSize[1] };
	builder.uniform("bscale") = SkV2{ source.scale[0], source.scale[1] };
	builder.uniform("srcShift") = SkV2{ source.srcShift[0],
		source.srcShift[1] };
	const uint32 magicValue = B_TRANSPARENT_MAGIC_RGBA32;
	const uint8* magic = (const uint8*)&magicValue;
	builder.uniform("magic") = SkV4{ (float)magic[0], (float)magic[1],
		(float)magic[2], (float)magic[3] };

	sk_sp<SkShader> shader = builder.makeShader();
	if (shader == nullptr)
		return false;
	SkPaint paint;
	paint.setShader(shader);
	paint.setBlendMode(SkBlendMode::kSrc);
	paint.setAntiAlias(false);
	SkiaGpu::Profile(SkiaGpu::kCountComposite);
	if (coverage != nullptr)
		SkiaGpu::Profile(SkiaGpu::kCountCoverage);

	SkCanvas* canvas = fTarget->Canvas();
	for (int32 i = 0; i < rects.CountRects(); i++) {
		clipping_rect r = rects.RectAtInt(i);
		canvas->drawIRect(SkIRect::MakeLTRB(r.left, r.top, r.right + 1,
			r.bottom + 1), paint);
	}
	fTarget->AddDamage(rects);
	return true;
}


/*!	The alpha mask over bounds, as Painter's masked scanline reads it
	(agg::clipped_alpha_mask::combine_hspan()): inside the mask's buffer a
	coverage c becomes (255 + c * m) >> 8, outside it becomes the outside
	opacity. Red is m or the outside opacity, green says "outside". The
	mask's values are data the GPU drew (AlphaMask.cpp); they are only
	uploaded here.
*/
sk_sp<SkShader>
SkiaPainter::_MaskShader(const SkIRect& bounds)
{
	if (fAlphaMask == NULL || fAlphaMask->Mask() == NULL)
		return nullptr;
	int32 width = bounds.width();
	int32 height = bounds.height();
	SkBitmap pixels;
	if (!pixels.tryAllocPixels(target_info(width, height)))
		return nullptr;
	uint8* full = new(std::nothrow) uint8[width * 2];
	if (full == NULL)
		return nullptr;
	uint8* zero = full + width;
	agg::clipped_alpha_mask* mask = fAlphaMask->Mask();
	for (int32 y = 0; y < height; y++) {
		memset(full, 255, width);
		memset(zero, 0, width);
		mask->combine_hspan(bounds.fLeft, bounds.fTop + y, full, width);
		mask->combine_hspan(bounds.fLeft, bounds.fTop + y, zero, width);
		uint8* row = (uint8*)pixels.getAddr32(0, y);
		for (int32 x = 0; x < width; x++) {
			row[x * 4 + 0] = 0;
			row[x * 4 + 1] = zero[x] != 0 ? 255 : 0;
			row[x * 4 + 2] = full[x];
			row[x * 4 + 3] = 255;
		}
	}
	delete[] full;
	pixels.setImmutable();
	sk_sp<SkImage> image = upload_texture(fTarget->Recorder(),
		pixels.asImage());
	if (image == nullptr)
		return nullptr;
	SkMatrix at = SkMatrix::Translate((float)bounds.fLeft,
		(float)bounds.fTop);
	return image->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
		SkSamplingOptions(SkFilterMode::kNearest), &at);
}


/*!	The solid fills of Painter that write the colour straight into the
	buffer (FillRect(rect, color), StraightLine(), FillRectNoClipping()):
	kSrc, exact. The region is in screen pixels.
*/
void
SkiaPainter::_FillSolid(const BRegion& region, const rgb_color& color,
	bool clip)
{
	if (fTarget == NULL || !fTarget->HasSurface())
		return;
	BRegion rects(region);
	if (clip) {
		if (fClippingRegion == NULL)
			return;
		rects.IntersectWith(fClippingRegion);
	}
	BRegion surface(BRect(fTarget->Left(), fTarget->Top(),
		fTarget->Left() + fTarget->Width() - 1,
		fTarget->Top() + fTarget->Height() - 1));
	rects.IntersectWith(&surface);
	if (rects.CountRects() == 0)
		return;

	SkPaint paint;
	paint.setBlendMode(SkBlendMode::kSrc);
	paint.setAntiAlias(false);
	if (color.alpha == 255) {
		// M13 step 1: an opaque colour is the same premultiplied and
		// straight, and an 8-bit value goes through Skia's float colour
		// unchanged, so a plain colour writes Haiku's bytes without the
		// runtime shader
		paint.setColor(SkColorSetARGB(255, color.red, color.green,
			color.blue));
	} else {
		// straight bytes, no conversion: the colour as Haiku writes it
		sk_sp<SkShader> shader = color_shader(color);
		if (shader == nullptr)
			return;
		paint.setShader(shader);
	}
	SkCanvas* canvas = fTarget->Canvas();
	for (int32 i = 0; i < rects.CountRects(); i++) {
		clipping_rect r = rects.RectAtInt(i);
		canvas->drawIRect(SkIRect::MakeLTRB(r.left, r.top, r.right + 1,
			r.bottom + 1), paint);
	}
	fTarget->AddDamage(rects);
}


/*!	B_OP_COPY and B_OP_OVER with one colour on an opaque image: Haiku's
	copy_solid and over_solid write the colour at full coverage and
	BLEND(d, c, cover) with alpha 255 at the edges, which is Skia's
	kSrcOver with that opaque colour (alpha 255 stays 255). _draw is false
	when nothing is drawn at all (B_OP_OVER with B_SOLID_LOW).
*/
bool
SkiaPainter::_DirectColor(bool forceSolidHigh, bool* _draw,
	SkColor* _color) const
{
	if (fTarget == NULL || !fTarget->IsOpaque() || fHasAlphaMask)
		return false;
	if (fDrawingMode != B_OP_COPY && fDrawingMode != B_OP_OVER)
		return false;
	bool high = forceSolidHigh || same_pattern(fPattern, kR5SolidHigh);
	bool low = !forceSolidHigh && same_pattern(fPattern, kR5SolidLow);
	if (!high && !low)
		return false;
	*_draw = true;
	rgb_color color = fHighColor;
	if (low) {
		if (fDrawingMode == B_OP_OVER) {
			*_draw = false;
			return true;
		}
		color = fLowColor;
	}
	*_color = SkColorSetARGB(255, color.red, color.green, color.blue);
	return true;
}


int
SkiaPainter::_AlphaKind() const
{
	// PixelFormat::SetDrawingMode(): overlay and composite per alpha
	// source; the other functions are AGG's comp ops (pixel alpha)
	switch (fAlphaFncMode) {
		case B_ALPHA_OVERLAY:
			return fAlphaSrcMode == B_CONSTANT_ALPHA ? 0 : 2;
		case B_ALPHA_COMPOSITE:
			return fAlphaSrcMode == B_CONSTANT_ALPHA ? 1 : 3;
		case B_ALPHA_COMPOSITE_SOURCE_IN: return 4;
		case B_ALPHA_COMPOSITE_SOURCE_OUT: return 5;
		case B_ALPHA_COMPOSITE_SOURCE_ATOP: return 6;
		case B_ALPHA_COMPOSITE_DESTINATION_OVER: return 7;
		case B_ALPHA_COMPOSITE_DESTINATION_IN: return 8;
		case B_ALPHA_COMPOSITE_DESTINATION_OUT: return 9;
		case B_ALPHA_COMPOSITE_DESTINATION_ATOP: return 10;
		case B_ALPHA_COMPOSITE_XOR: return 11;
		case B_ALPHA_COMPOSITE_CLEAR: return 12;
		case B_ALPHA_COMPOSITE_DIFFERENCE: return 13;
		case B_ALPHA_COMPOSITE_LIGHTEN: return 14;
		case B_ALPHA_COMPOSITE_DARKEN: return 15;
		default:
			return 2;
	}
}


/*!	Draws straight into the target, once per clipping rectangle inside
	bounds (screen pixels), under matrix.
*/
void
SkiaPainter::_DirectDraw(const SkIRect& _bounds, const SkMatrix& matrix,
	const std::function<void(SkCanvas*)>& draw)
{
	SkIRect surfaceBounds = SkIRect::MakeXYWH(fTarget->Left(),
		fTarget->Top(), fTarget->Width(), fTarget->Height());
	SkIRect bounds = _bounds;
	if (!bounds.intersect(surfaceBounds))
		return;
	BRegion rects;
	_ClipRects(bounds, rects);
	SkiaGpu::Profile(SkiaGpu::kCountDirect);
	SkCanvas* canvas = fTarget->Canvas();
	for (int32 i = 0; i < rects.CountRects(); i++) {
		clipping_rect r = rects.RectAtInt(i);
		canvas->save();
		canvas->clipIRect(SkIRect::MakeLTRB(r.left, r.top, r.right + 1,
			r.bottom + 1));
		canvas->concat(matrix);
		draw(canvas);
		canvas->restore();
	}
	fTarget->AddDamage(rects);
}


/*!	Skia computes the coverage of the shape (white on clear, in the scratch
	image) under the current transform; the R5 shader blends it.
*/
BRect
SkiaPainter::_DrawCoverage(const SkRect& deviceBounds, const SkPath* path,
	const SkPaint* shapePaint, const SkRRect* rrect, bool oval,
	const BGradient* gradient)
{
	if (fTarget == NULL || !fTarget->HasSurface())
		return BRect(0, 0, -1, -1);
	SkIRect bounds = deviceBounds.roundOut();
	bounds.outset(1, 1);
	SkIRect surfaceBounds = SkIRect::MakeXYWH(fTarget->Left(),
		fTarget->Top(), fTarget->Width(), fTarget->Height());
	if (fClippingRegion == NULL)
		return BRect(0, 0, -1, -1);
	clipping_rect clipFrame = fClippingRegion->FrameInt();
	SkIRect clip = SkIRect::MakeLTRB(clipFrame.left, clipFrame.top,
		clipFrame.right + 1, clipFrame.bottom + 1);
	if (!bounds.intersect(surfaceBounds) || !bounds.intersect(clip))
		return _Clipped(BRect(deviceBounds.fLeft, deviceBounds.fTop,
			deviceBounds.fRight, deviceBounds.fBottom));

	bool directDraw;
	SkColor directColor;
	// step 5b: the new look mixes the edges in linear light, so Skia draws
	// only the coverage there (below)
	bool linearEdges = !sClassic && sLinearEdges;
	if (!linearEdges && gradient == NULL
		&& _DirectColor(false, &directDraw, &directColor)) {
		if (directDraw) {
			SkPaint paint;
			if (shapePaint != NULL)
				paint = *shapePaint;
			paint.setColor(directColor);
			paint.setBlendMode(SkBlendMode::kSrcOver);
			paint.setAntiAlias(true);
			_DirectDraw(bounds, _Matrix(), [&](SkCanvas* canvas) {
				if (rrect != NULL)
					canvas->drawRRect(*rrect, paint);
				else if (path != NULL)
					canvas->drawPath(*path, paint);
			});
		}
		return _Clipped(BRect(deviceBounds.fLeft, deviceBounds.fTop,
			deviceBounds.fRight, deviceBounds.fBottom));
	}

	SkSurface* scratch = fTarget->Scratch();
	if (scratch == NULL)
		return BRect(0, 0, -1, -1);
	SkCanvas* canvas = scratch->getCanvas();
	canvas->save();
	canvas->clipIRect(bounds);
	canvas->clear(SK_ColorTRANSPARENT);
	canvas->concat(_Matrix());
	SkPaint paint;
	if (shapePaint != NULL)
		paint = *shapePaint;
	paint.setColor(SK_ColorWHITE);
	paint.setBlendMode(SkBlendMode::kSrcOver);
	paint.setAntiAlias(true);
	if (rrect != NULL)
		canvas->drawRRect(*rrect, paint);
	else if (oval && path == NULL)
		canvas->drawOval(deviceBounds, paint);
	else if (path != NULL)
		canvas->drawPath(*path, paint);
	canvas->restore();

	sk_sp<SkImage> coverage = scratch->makeImageSnapshot(bounds.makeOffset(
		-fTarget->Left(), -fTarget->Top()));
	if (coverage == nullptr)
		return BRect(0, 0, -1, -1);

	Source source;
	if (gradient != NULL) {
		bool ok = false;
		source.colors = _GradientShader(*gradient, &ok);
		if (!ok)
			return BRect(0, 0, -1, -1);
		source.useColors = true;
		source.forceSolidHigh = true;
	}
	_Composite(bounds, coverage, source);
	return _Clipped(BRect(deviceBounds.fLeft, deviceBounds.fTop,
		deviceBounds.fRight, deviceBounds.fBottom));
}


static SkPaint::Cap
skia_cap(cap_mode mode)
{
	switch (mode) {
		case B_SQUARE_CAP:
			return SkPaint::kSquare_Cap;
		case B_ROUND_CAP:
			return SkPaint::kRound_Cap;
		case B_BUTT_CAP:
		default:
			return SkPaint::kButt_Cap;
	}
}


static SkPaint::Join
skia_join(join_mode mode)
{
	// Painter's agg_line_join_mode_for(): bevel, butt and square are bevel
	switch (mode) {
		case B_ROUND_JOIN:
			return SkPaint::kRound_Join;
		case B_BEVEL_JOIN:
		case B_BUTT_JOIN:
		case B_SQUARE_JOIN:
			return SkPaint::kBevel_Join;
		case B_MITER_JOIN:
		default:
			return SkPaint::kMiter_Join;
	}
}


BRect
SkiaPainter::_FillPath(const SkPath& _path, const BGradient* gradient)
{
	SkPath path(_path);
	path.setFillType(fFillRule == B_EVEN_ODD ? SkPathFillType::kEvenOdd
		: SkPathFillType::kWinding);
	SkRect bounds = _Matrix().mapRect(path.getBounds());
	SkPaint paint;
	paint.setStyle(SkPaint::kFill_Style);
	return _DrawCoverage(bounds, &path, &paint, NULL, false, gradient);
}


BRect
SkiaPainter::_StrokePath(const SkPath& path, cap_mode cap,
	const BGradient* gradient)
{
	SkPaint paint;
	paint.setStyle(SkPaint::kStroke_Style);
	paint.setStrokeWidth(fPenSize);
	paint.setStrokeCap(skia_cap(cap));
	paint.setStrokeJoin(skia_join(fLineJoinMode));
	paint.setStrokeMiter(fMiterLimit);
	SkRect local = path.getBounds();
	float reach = fPenSize / 2 * fmaxf(1.0f, fMiterLimit) + 1;
	local.outset(reach, reach);
	SkRect bounds = _Matrix().mapRect(local);
	return _DrawCoverage(bounds, &path, &paint, NULL, false, gradient);
}


//	#pragma mark - drawing


#define CHECK_CLIPPING \
	if (fClippingRegion == NULL || !fClippingRegion->Frame().IsValid() \
		|| fTarget == NULL || !fTarget->HasSurface()) \
		return BRect(0, 0, -1, -1);
#define CHECK_CLIPPING_NO_RETURN \
	if (fClippingRegion == NULL || !fClippingRegion->Frame().IsValid() \
		|| fTarget == NULL || !fTarget->HasSurface()) \
		return;


//! Painter::StrokeLine(BPoint, BPoint)
void
SkiaPainter::StrokeLine(BPoint a, BPoint b)
{
	CHECK_CLIPPING_NO_RETURN

	_Align(&a, false);
	_Align(&b, false);

	if (fPenSize == 1.0 && fIdentityTransform
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& !fHasAlphaMask) {
		if (same_pattern(fPattern, kR5SolidHigh)
			&& StraightLine(a, b, fHighColor)) {
			return;
		} else if (same_pattern(fPattern, kR5SolidLow)
			&& StraightLine(a, b, fLowColor)) {
			return;
		}
	}

	if (a == b) {
		if (fPenSize == 1.0 && !fSubpixelPrecise && fIdentityTransform) {
			if (fClippingRegion->Contains(a)) {
				int32 x = (int32)a.x;
				int32 y = (int32)a.y;
				_Composite(SkIRect::MakeXYWH(x, y, 1, 1), nullptr, Source());
			}
		} else {
			SkPathBuilder builder;
			builder.moveTo(a.x, a.y);
			builder.lineTo(a.x + 1, a.y);
			builder.lineTo(a.x + 1, a.y + 1);
			builder.lineTo(a.x, a.y + 1);
			builder.close();
			_FillPath(builder.detach(), NULL);
		}
		return;
	}

	if (!fSubpixelPrecise && fmodf(fPenSize, 2.0) != 0.0) {
		_Align(&a, true);
		_Align(&b, true);
	}
	SkPathBuilder builder;
	builder.moveTo(a.x, a.y);
	builder.lineTo(b.x, b.y);
	if (!fSubpixelPrecise && fPenSize == 1.0f)
		_StrokePath(builder.detach(), B_SQUARE_CAP, NULL);
	else
		_StrokePath(builder.detach(), fLineCapMode, NULL);
}


//! Painter::StrokeLine(BPoint, BPoint, const BGradient&)
void
SkiaPainter::StrokeLine(BPoint a, BPoint b, const BGradient& gradient)
{
	CHECK_CLIPPING_NO_RETURN

	_Align(&a, false);
	_Align(&b, false);

	SkPathBuilder builder;
	if (a == b) {
		builder.moveTo(a.x, a.y);
		builder.lineTo(a.x + 1, a.y);
		builder.lineTo(a.x + 1, a.y + 1);
		builder.lineTo(a.x, a.y + 1);
		builder.close();
		_FillPath(builder.detach(), &gradient);
		return;
	}
	if (!fSubpixelPrecise && fmodf(fPenSize, 2.0) != 0.0) {
		_Align(&a, true);
		_Align(&b, true);
	}
	builder.moveTo(a.x, a.y);
	builder.lineTo(b.x, b.y);
	if (!fSubpixelPrecise && fPenSize == 1.0f)
		_StrokePath(builder.detach(), B_SQUARE_CAP, &gradient);
	else
		_StrokePath(builder.detach(), fLineCapMode, &gradient);
}


//! Painter::StraightLine()
bool
SkiaPainter::StraightLine(BPoint a, BPoint b, const rgb_color& c)
{
	if (fClippingRegion == NULL || !fClippingRegion->Frame().IsValid()
		|| fTarget == NULL || !fTarget->HasSurface())
		return false;

	rgb_color color = c;
	color.alpha = 255;
	if (a.x == b.x) {
		int32 x = (int32)a.x;
		int32 y1 = (int32)min_c(a.y, b.y);
		int32 y2 = (int32)max_c(a.y, b.y);
		_FillSolid(region_of(clipping_rect{ x, y1, x, y2 }), color, true);
		return true;
	}
	if (a.y == b.y) {
		int32 y = (int32)a.y;
		if (y < fTarget->Top() || y >= fTarget->Top() + fTarget->Height())
			return true;
		int32 x1 = (int32)min_c(a.x, b.x);
		int32 x2 = (int32)max_c(a.x, b.x);
		_FillSolid(region_of(clipping_rect{ x1, y, x2, y }), color, true);
		return true;
	}
	return false;
}


//! Painter::_DrawTriangle()
BRect
SkiaPainter::DrawTriangle(BPoint p1, BPoint p2, BPoint p3, bool fill,
	const BGradient* gradient)
{
	CHECK_CLIPPING

	_Align(&p1);
	_Align(&p2);
	_Align(&p3);
	SkPathBuilder builder;
	builder.moveTo(p1.x, p1.y);
	builder.lineTo(p2.x, p2.y);
	builder.lineTo(p3.x, p3.y);
	builder.close();
	if (fill)
		return _FillPath(builder.detach(), gradient);
	return _StrokePath(builder.detach(), fLineCapMode, gradient);
}


//! Painter::DrawPolygon()
BRect
SkiaPainter::DrawPolygon(BPoint* p, int32 count, bool filled, bool closed,
	const BGradient* gradient)
{
	CHECK_CLIPPING

	if (count == 0)
		return BRect(0, 0, -1, -1);
	bool centerOffset = !filled && fIdentityTransform
		&& fmodf(fPenSize, 2.0) != 0.0;
	SkPathBuilder builder;
	_Align(p, centerOffset);
	builder.moveTo(p->x, p->y);
	for (int32 i = 1; i < count; i++) {
		p++;
		_Align(p, centerOffset);
		builder.lineTo(p->x, p->y);
	}
	if (closed)
		builder.close();
	if (filled)
		return _FillPath(builder.detach(), gradient);
	return _StrokePath(builder.detach(), fLineCapMode, gradient);
}


//! Painter::DrawBezier()
BRect
SkiaPainter::DrawBezier(BPoint* p, bool filled, const BGradient* gradient)
{
	CHECK_CLIPPING

	_Align(&p[0]);
	_Align(&p[1]);
	_Align(&p[2]);
	_Align(&p[3]);
	SkPathBuilder builder;
	builder.moveTo(p[0].x, p[0].y);
	builder.cubicTo(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);
	if (filled) {
		builder.close();
		return _FillPath(builder.detach(), gradient);
	}
	return _StrokePath(builder.detach(), fLineCapMode, gradient);
}


//! Painter::DrawShape() and Painter::_IterateShapeData()
BRect
SkiaPainter::DrawShape(int32 opCount, const uint32* opList, int32 ptCount,
	const BPoint* points, bool filled, const BPoint& offset, float scale,
	const BGradient* gradient)
{
	CHECK_CLIPPING

	SkPathBuilder builder;
	for (int32 i = 0; i < opCount; i++) {
		uint32 op = opList[i] & 0xFF000000;
		if ((op & OP_MOVETO) != 0) {
			builder.moveTo(points->x * scale + offset.x,
				points->y * scale + offset.y);
			points++;
		}
		if ((op & OP_LINETO) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count--) {
				builder.lineTo(points->x * scale + offset.x,
					points->y * scale + offset.y);
				points++;
			}
		}
		if ((op & OP_BEZIERTO) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count) {
				builder.cubicTo(points[0].x * scale + offset.x,
					points[0].y * scale + offset.y,
					points[1].x * scale + offset.x,
					points[1].y * scale + offset.y,
					points[2].x * scale + offset.x,
					points[2].y * scale + offset.y);
				points += 3;
				count -= 3;
			}
		}
		if ((op & OP_LARGE_ARC_TO_CW) != 0 || (op & OP_LARGE_ARC_TO_CCW) != 0
			|| (op & OP_SMALL_ARC_TO_CW) != 0
			|| (op & OP_SMALL_ARC_TO_CCW) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			bool large = (op & (OP_LARGE_ARC_TO_CW | OP_LARGE_ARC_TO_CCW))
				!= 0;
			bool sweep = (op & (OP_SMALL_ARC_TO_CW | OP_LARGE_ARC_TO_CW))
				!= 0;
			while (count > 0) {
				// AGG's arc_to() takes the angle in radians, Skia in degrees;
				// the SVG sweep flag 1 is Skia's clockwise
				builder.arcTo(SkPoint::Make(points[0].x * scale,
						points[0].y * scale),
					points[1].x * 180.0f / (float)M_PI,
					large ? SkPathBuilder::kLarge_ArcSize
						: SkPathBuilder::kSmall_ArcSize,
					sweep ? SkPathDirection::kCW : SkPathDirection::kCCW,
					SkPoint::Make(points[2].x * scale + offset.x,
						points[2].y * scale + offset.y));
				points += 3;
				count -= 3;
			}
		}
		if ((op & OP_CLOSE) != 0)
			builder.close();
	}
	SkPath path = builder.detach();
	if (filled)
		return _FillPath(path, gradient);
	return _StrokePath(path, fLineCapMode, gradient);
}


//! Painter::StrokeRect(const BRect&) and the gradient form
BRect
SkiaPainter::StrokeRect(const BRect& r, const BGradient* gradient)
{
	CHECK_CLIPPING

	BPoint a(r.left, r.top);
	BPoint b(r.right, r.bottom);
	_Align(&a, false);
	_Align(&b, false);

	if (gradient == NULL && fPenSize == 1.0 && fIdentityTransform
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& !fHasAlphaMask) {
		if (same_pattern(fPattern, kR5SolidHigh)) {
			BRect rect(a, b);
			StrokeRect(rect, fHighColor);
			return _Clipped(rect);
		} else if (same_pattern(fPattern, kR5SolidLow)) {
			BRect rect(a, b);
			StrokeRect(rect, fLowColor);
			return _Clipped(rect);
		}
	}

	if (fIdentityTransform && fmodf(fPenSize, 2.0) != 0.0) {
		a.x += 0.5;
		a.y += 0.5;
		b.x += 0.5;
		b.y += 0.5;
	}
	SkPathBuilder builder;
	builder.moveTo(a.x, a.y);
	if (a.x == b.x || a.y == b.y) {
		builder.lineTo(b.x, b.y);
	} else {
		builder.lineTo(b.x, a.y);
		builder.lineTo(b.x, b.y);
		builder.lineTo(a.x, b.y);
	}
	builder.close();
	return _StrokePath(builder.detach(), fLineCapMode, gradient);
}


//! Painter::StrokeRect(const BRect&, const rgb_color&)
void
SkiaPainter::StrokeRect(const BRect& r, const rgb_color& c)
{
	StraightLine(BPoint(r.left, r.top), BPoint(r.right - 1, r.top), c);
	StraightLine(BPoint(r.right, r.top), BPoint(r.right, r.bottom - 1), c);
	StraightLine(BPoint(r.right, r.bottom), BPoint(r.left + 1, r.bottom), c);
	StraightLine(BPoint(r.left, r.bottom), BPoint(r.left, r.top + 1), c);
}


//! Painter::FillRect(const BRect&) and the gradient form
BRect
SkiaPainter::FillRect(const BRect& r, const BGradient* gradient)
{
	CHECK_CLIPPING

	BPoint a(min_c(r.left, r.right), min_c(r.top, r.bottom));
	BPoint b(max_c(r.left, r.right), max_c(r.top, r.bottom));
	_Align(&a, true, false);
	_Align(&b, true, false);

	if (gradient == NULL && (fDrawingMode == B_OP_COPY
			|| fDrawingMode == B_OP_OVER)
		&& !fHasAlphaMask && fIdentityTransform) {
		if (same_pattern(fPattern, kR5SolidHigh)) {
			BRect rect(a, b);
			FillRect(rect, fHighColor);
			return _Clipped(rect);
		} else if (same_pattern(fPattern, kR5SolidLow)) {
			BRect rect(a, b);
			FillRect(rect, fLowColor);
			return _Clipped(rect);
		}
	}

	if (gradient == NULL && fDrawingMode == B_OP_ALPHA
		&& fAlphaFncMode == B_ALPHA_OVERLAY && !fHasAlphaMask
		&& fIdentityTransform) {
		bool high = same_pattern(fPattern, kR5SolidHigh);
		bool low = same_pattern(fPattern, kR5SolidLow);
		if (high || low) {
			// Painter::_BlendRect32(): blend_line32() per row
			rgb_color c = high ? fHighColor : fLowColor;
			if (low && fAlphaSrcMode == B_CONSTANT_ALPHA)
				c.alpha = fHighColor.alpha;
			BRect rect(a, b);
			SkIRect bounds = SkIRect::MakeLTRB((int32)rect.left,
				(int32)rect.top, (int32)rect.right + 1, (int32)rect.bottom + 1);
			Source source;
			source.special = kSpecialBlendLine32;
			source.blendAlpha = c.alpha;
			source.specialColor = c;
			_Composite(bounds, nullptr, source);
			return _Clipped(rect);
		}
	}

	if (gradient != NULL && gradient->GetType() == BGradient::TYPE_LINEAR
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& !fHasAlphaMask && fIdentityTransform) {
		const BGradientLinear* linear
			= dynamic_cast<const BGradientLinear*>(gradient);
		if (linear != NULL && linear->Start().x == linear->End().x
			&& linear->Start().y <= linear->End().y) {
			// Painter::FillRectVerticalGradient(): the colours of
			// _MakeGradient() straight into the rows
			BRect rect(a, b);
			rect = rect & fClippingRegion->Frame();
			int32 size = rect.IntegerHeight() + 1;
			int32 top = (int32)linear->Start().y;
			int32 bottom = (int32)linear->End().y;
			int32 colorCount = bottom - top + 1;
			if (colorCount < 0 || size <= 0)
				return _Clipped(BRect(a, b));
			SkBitmap column;
			column.allocPixels(target_info(1, size));
			uint32* colors = (uint32*)column.getPixels();
			memset(colors, 0, size * 4);
			// Painter::_MakeGradient(gradient, count, colors, offset, size)
			BGradient::ColorStop* from = linear->ColorStopAt(0);
			if (from != NULL) {
				int32 arrayOffset = top - (int32)rect.top;
				int32 index = (int32)floorf(colorCount * from->offset / 255
					+ 0.5) + arrayOffset;
				if (index > size)
					index = size;
				for (int32 i = 0; i < index; i++) {
					uint8* c = (uint8*)&colors[i];
					c[0] = from->color.blue;
					c[1] = from->color.green;
					c[2] = from->color.red;
					c[3] = from->color.alpha;
				}
				int32 stopCount = linear->CountColorStops();
				for (int32 i = 1; i < stopCount; i++) {
					BGradient::ColorStop* to = linear->ColorStopAtFast(i);
					int32 offset = (int32)floorf((colorCount - 1) * to->offset
						/ 255 + 0.5);
					if (offset > colorCount - 1)
						offset = colorCount - 1;
					offset += arrayOffset;
					int32 dist = offset - index;
					if (dist >= 0) {
						int32 start = max_c(index, 0);
						int32 stop = min_c(offset, size - 1);
						for (int32 j = start; j <= stop; j++) {
							uint8* c = (uint8*)&colors[j];
							float f = (float)(offset - j) / (float)(dist + 1);
							float t = 1.0 - f;
							c[0] = (uint8)floorf(from->color.blue * f
								+ to->color.blue * t + 0.5);
							c[1] = (uint8)floorf(from->color.green * f
								+ to->color.green * t + 0.5);
							c[2] = (uint8)floorf(from->color.red * f
								+ to->color.red * t + 0.5);
							c[3] = (uint8)floorf(from->color.alpha * f
								+ to->color.alpha * t + 0.5);
						}
					}
					index = offset + 1;
					from = to;
				}
				for (int32 i = max_c(index, 0); i < size; i++) {
					uint8* c = (uint8*)&colors[i];
					c[0] = from->color.blue;
					c[1] = from->color.green;
					c[2] = from->color.red;
					c[3] = from->color.alpha;
				}
			}
			column.setImmutable();
			sk_sp<SkImage> image = fTarget->GradientImage(column);
			if (image == nullptr)
				return _Clipped(BRect(a, b));
			SkMatrix at = SkMatrix::Translate((float)(int32)rect.left,
				(float)(int32)rect.top);
			// the colours as they are: kSrc (step 5c, no destination copy)
			SkPaint copy;
			copy.setBlendMode(SkBlendMode::kSrc);
			copy.setShader(image->makeShader(SkTileMode::kClamp,
				SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest),
				&at));
			SkIRect bounds = SkIRect::MakeLTRB((int32)rect.left,
				(int32)rect.top, (int32)rect.right + 1, (int32)rect.bottom + 1);
			_DirectDraw(bounds, SkMatrix::I(), [&](SkCanvas* canvas) {
				canvas->drawIRect(bounds, copy);
			});
			return _Clipped(BRect(a, b));
		}
	}

	b.x += 1.0;
	b.y += 1.0;
	if (fIdentityTransform && !fHasAlphaMask) {
		// whole pixels: full coverage, no coverage image
		Source source;
		if (gradient != NULL) {
			bool ok = false;
			source.colors = _GradientShader(*gradient, &ok);
			if (!ok)
				return BRect(0, 0, -1, -1);
			source.useColors = true;
			source.forceSolidHigh = true;
		}
		SkIRect bounds = SkIRect::MakeLTRB((int32)a.x, (int32)a.y,
			(int32)b.x, (int32)b.y);
		_Composite(bounds, nullptr, source);
		return _Clipped(BRect(a.x, a.y, b.x, b.y));
	}
	SkPathBuilder builder;
	builder.addRect(SkRect::MakeLTRB(a.x, a.y, b.x, b.y));
	return _FillPath(builder.detach(), gradient);
}


//! Painter::FillRect(const BRect&, const rgb_color&)
void
SkiaPainter::FillRect(const BRect& r, const rgb_color& c)
{
	if (fClippingRegion == NULL || !fClippingRegion->Frame().IsValid())
		return;
	_FillSolid(region_of(clipping_rect{ (int32)r.left, (int32)r.top,
		(int32)r.right, (int32)r.bottom }), c, true);
}


//! Painter::FillRectNoClipping()
void
SkiaPainter::FillRectNoClipping(const clipping_rect& r, const rgb_color& c)
{
	_FillSolid(region_of(r), c, false);
}


//! Painter::StrokeRoundRect() and Painter::FillRoundRect()
BRect
SkiaPainter::DrawRoundRect(const BRect& r, float xRadius, float yRadius,
	bool filled, const BGradient* gradient)
{
	CHECK_CLIPPING

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	if (filled) {
		_Align(&lt, false);
		_Align(&rb, false);
		rb.x += 1.0;
		rb.y += 1.0;
	} else {
		bool centerOffset = fmodf(fPenSize, 2.0) != 0.0;
		_Align(&lt, centerOffset);
		_Align(&rb, centerOffset);
	}
	SkRRect rrect = SkRRect::MakeRectXY(SkRect::MakeLTRB(lt.x, lt.y, rb.x,
		rb.y), xRadius, yRadius);
	SkPaint paint;
	SkRect local = rrect.rect();
	if (filled)
		paint.setStyle(SkPaint::kFill_Style);
	else {
		paint.setStyle(SkPaint::kStroke_Style);
		paint.setStrokeWidth(fPenSize);
		paint.setStrokeJoin(skia_join(fLineJoinMode));
		paint.setStrokeMiter(fMiterLimit);
		local.outset(fPenSize / 2 + 1, fPenSize / 2 + 1);
	}
	return _DrawCoverage(_Matrix().mapRect(local), NULL, &paint, &rrect,
		false, gradient);
}


//! Painter::DrawEllipse()
BRect
SkiaPainter::DrawEllipse(BRect r, bool fill, const BGradient* gradient)
{
	CHECK_CLIPPING

	AlignEllipseRect(&r, fill);
	SkRect oval = SkRect::MakeLTRB(r.left, r.top, r.right, r.bottom);
	SkPathBuilder builder;
	builder.addOval(oval);
	SkPath path = builder.detach();
	SkPaint paint;
	SkRect local = oval;
	if (fill)
		paint.setStyle(SkPaint::kFill_Style);
	else {
		paint.setStyle(SkPaint::kStroke_Style);
		paint.setStrokeWidth(fPenSize);
		local.outset(fPenSize / 2 + 1, fPenSize / 2 + 1);
	}
	// an oval under a transform is a path; without one Skia's analytic oval
	SkRRect rrect = SkRRect::MakeOval(oval);
	return _DrawCoverage(_Matrix().mapRect(local), NULL, &paint, &rrect,
		false, gradient);
}


//! Painter::StrokeArc() and Painter::FillArc()
BRect
SkiaPainter::DrawArc(BPoint center, float xRadius, float yRadius,
	float angle, float span, bool filled, const BGradient* gradient)
{
	CHECK_CLIPPING

	_Align(&center);
	SkRect oval = SkRect::MakeLTRB(center.x - xRadius, center.y - yRadius,
		center.x + xRadius, center.y + yRadius);
	// Haiku's angles run counter-clockwise, Skia's clockwise
	SkPathBuilder builder;
	if (filled) {
		builder.moveTo(center.x, center.y);
		builder.arcTo(oval, -angle, -span, false);
		builder.close();
		return _FillPath(builder.detach(), gradient);
	}
	builder.addArc(oval, -angle, -span);
	return _StrokePath(builder.detach(), fLineCapMode, gradient);
}


//! Painter::FillRegion()
BRect
SkiaPainter::FillRegion(const BRegion* region, const BGradient* gradient)
{
	CHECK_CLIPPING

	BRegion copy(*region);
	int32 count = copy.CountRects();
	if (count == 0)
		return BRect(0, 0, -1, -1);
	BRect touched = FillRect(copy.RectAt(0), gradient);
	for (int32 i = 1; i < count; i++)
		touched = touched | FillRect(copy.RectAt(i), gradient);
	return touched;
}


//! Painter::InvertRect()
BRect
SkiaPainter::InvertRect(const BRect& r)
{
	CHECK_CLIPPING

	BRegion region(r);
	region.IntersectWith(fClippingRegion);
	Source source;
	source.special = kSpecialInvertRect;
	for (int32 i = 0; i < region.CountRects(); i++) {
		BRect rect = region.RectAt(i);
		_Composite(SkIRect::MakeLTRB((int32)rect.left, (int32)rect.top,
			(int32)rect.right + 1, (int32)rect.bottom + 1), nullptr, source);
	}
	return _Clipped(r);
}


/*!	DrawingEngine::CopyRegion(): every rectangle is copied from the image as
	it was before the call (a snapshot is a copy), so the order does not
	matter. kSrc, exact.
*/
void
SkiaPainter::CopyRegion(const BRegion& region, int32 xOffset, int32 yOffset,
	BRegion& _touched)
{
	_touched.MakeEmpty();
	if (fTarget == NULL || !fTarget->HasSurface())
		return;
	SkIRect surface = SkIRect::MakeXYWH(fTarget->Left(), fTarget->Top(),
		fTarget->Width(), fTarget->Height());
	int32 count = region.CountRects();
	struct Copy {
		sk_sp<SkImage>	image;
		SkIRect			to;
	};
	Copy* copies = new(std::nothrow) Copy[count > 0 ? count : 1];
	if (copies == NULL)
		return;
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = region.RectAtInt(i);
		SkIRect src = SkIRect::MakeLTRB(r.left, r.top, r.right + 1,
			r.bottom + 1);
		SkIRect dst = src.makeOffset(xOffset, yOffset);
		// DrawingEngine::CopyRect(): both inside the buffer
		if (!src.intersect(surface) || !dst.intersect(surface))
			continue;
		SkIRect back = dst.makeOffset(-xOffset, -yOffset);
		if (!src.intersect(back))
			continue;
		copies[i].image = fTarget->Surface()->makeImageSnapshot(
			src.makeOffset(-fTarget->Left(), -fTarget->Top()));
		copies[i].to = src.makeOffset(xOffset, yOffset);
	}
	SkPaint paint;
	paint.setBlendMode(SkBlendMode::kSrc);
	SkCanvas* canvas = fTarget->Canvas();
	for (int32 i = 0; i < count; i++) {
		if (copies[i].image == nullptr)
			continue;
		canvas->drawImage(copies[i].image, (float)copies[i].to.fLeft,
			(float)copies[i].to.fTop, SkSamplingOptions(), &paint);
		_touched.Include(clipping_rect{ copies[i].to.fLeft,
			copies[i].to.fTop, copies[i].to.fRight - 1,
			copies[i].to.fBottom - 1 });
	}
	delete[] copies;
	fTarget->AddDamage(_touched);
}


//	#pragma mark - bitmaps


// M13, option E: defined with the gradients below
static sk_sp<SkRuntimeEffect> bitmap_over_effect();


/*!	Painter::DrawBitmap() and Painter::BitmapPainter: the same colour-space
	conversion on the CPU (BitmapPainter::_ConvertColorSpace(), it is data,
	not drawing), one upload, and the drawing on the GPU with the variant
	Painter picks.
*/
BRect
SkiaPainter::DrawBitmap(const ServerBitmap* bitmap, BRect bitmapRect,
	BRect viewRect, uint32 options)
{
	CHECK_CLIPPING

	BRect touched = TransformAlignAndClipRect(viewRect);
	if (!touched.IsValid() || bitmap == NULL || !bitmap->IsValid())
		return touched;

	// BitmapPainter::_DetermineTransform()
	BRect bitmapBounds = bitmap->Bounds();
	bitmapBounds.OffsetBy(-bitmapBounds.left, -bitmapBounds.top);
	BRect sourceRect = bitmapRect;
	if (!sourceRect.IsValid()
		|| ((options & B_TILE_BITMAP) == 0
			&& !sourceRect.Intersects(bitmapBounds))
		|| !viewRect.IsValid())
		return touched;
	BRect destinationRect = viewRect;
	if (!fSubpixelPrecise) {
		align_rect_to_pixels(&sourceRect);
		align_rect_to_pixels(&destinationRect);
	}
	double scaleX = 1.0;
	double scaleY = 1.0;
	if ((options & B_TILE_BITMAP) == 0) {
		scaleX = (destinationRect.Width() + 1) / (sourceRect.Width() + 1);
		scaleY = (destinationRect.Height() + 1) / (sourceRect.Height() + 1);
		if (scaleX == 0.0 || scaleY == 0.0)
			return touched;
		if (sourceRect.left < bitmapBounds.left) {
			float diff = bitmapBounds.left - sourceRect.left;
			destinationRect.left += diff * scaleX;
			sourceRect.left = bitmapBounds.left;
		}
		if (sourceRect.top < bitmapBounds.top) {
			float diff = bitmapBounds.top - sourceRect.top;
			destinationRect.top += diff * scaleY;
			sourceRect.top = bitmapBounds.top;
		}
		if (sourceRect.right > bitmapBounds.right) {
			float diff = sourceRect.right - bitmapBounds.right;
			destinationRect.right -= diff * scaleX;
			sourceRect.right = bitmapBounds.right;
		}
		if (sourceRect.bottom > bitmapBounds.bottom) {
			float diff = sourceRect.bottom - bitmapBounds.bottom;
			destinationRect.bottom -= diff * scaleY;
			sourceRect.bottom = bitmapBounds.bottom;
		}
	}
	bool hasScale = scaleX != 1.0 || scaleY != 1.0;
	bool tile = (options & B_TILE_BITMAP) != 0;
	color_space space = bitmap->ColorSpace();

	// BitmapPainter::_ConvertColorSpace()
	int32 width = bitmap->Width();
	int32 height = bitmap->Height();
	bool raw = space == B_RGBA32
		|| (space == B_RGB32 && (fDrawingMode == B_OP_COPY
			|| fDrawingMode == B_OP_ALPHA))
		|| (space == B_RGB32 && fDrawingMode == B_OP_OVER && !hasScale
			&& fIdentityTransform && !fHasAlphaMask && !tile);
	// step 5c: the texture of the same bytes, from an earlier drawing
	sk_sp<SkImage> image = fTarget->FindTexture(bitmap, bitmap->Bits(),
		bitmap->BitsLength(), width, height, bitmap->BytesPerRow(), space, raw);
	if (image == nullptr) {
	SkBitmap pixels;
	if (!pixels.tryAllocPixels(target_info(width, height)))
		return touched;
	if (raw) {
		const uint8* from = bitmap->Bits();
		for (int32 y = 0; y < height; y++) {
			memcpy((uint8*)pixels.getAddr32(0, y),
				from + y * bitmap->BytesPerRow(), width * 4);
		}
	} else {
		BBitmap converted(BRect(0, 0, width - 1, height - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGBA32);
		if (converted.InitCheck() != B_OK
			|| converted.ImportBits(bitmap->Bits(), bitmap->BitsLength(),
				bitmap->BytesPerRow(), 0, space) < B_OK)
			return touched;
		for (int32 y = 0; y < height; y++) {
			memcpy((uint8*)pixels.getAddr32(0, y),
				(const uint8*)converted.Bits() + y * converted.BytesPerRow(),
				width * 4);
		}
		// BitmapPainter::_TransparentMagicToAlpha()
		if (space == B_RGB32 || space == B_RGB15) {
			for (int32 y = 0; y < height; y++) {
				const uint8* row = bitmap->Bits() + y * bitmap->BytesPerRow();
				uint32* out = pixels.getAddr32(0, y);
				for (int32 x = 0; x < width; x++) {
					bool magic = space == B_RGB32
						? ((const uint32*)row)[x] == B_TRANSPARENT_MAGIC_RGBA32
						: ((const uint16*)row)[x] == B_TRANSPARENT_MAGIC_RGBA15;
					if (magic)
						out[x] &= 0x00ffffff;
				}
			}
		}
	}
	pixels.setImmutable();
	image = upload_texture(fTarget->Recorder(), pixels.asImage());
	if (image == nullptr)
		return touched;
	fTarget->KeepTexture(bitmap, bitmap->Bits(), bitmap->BitsLength(), width,
		height, bitmap->BytesPerRow(), space, raw, image);
	}

	// bitmap pixel (sx, sy) covers screen [offset + s * scale, ...)
	float offsetX = destinationRect.left - sourceRect.left * scaleX;
	float offsetY = destinationRect.top - sourceRect.top * scaleY;
	SkMatrix place = SkMatrix::Translate(offsetX, offsetY);
	place.preScale(scaleX, scaleY);
	SkMatrix at = _Matrix();
	at.preConcat(place);
	bool bilinear = (options & B_FILTER_BITMAP_BILINEAR) != 0;
	SkSamplingOptions sampling(bilinear && (hasScale || !fIdentityTransform)
		? SkFilterMode::kLinear : SkFilterMode::kNearest);
	SkTileMode tileMode = tile ? SkTileMode::kRepeat : SkTileMode::kDecal;

	Source source;
	source.useColors = true;
	source.colors = image->makeShader(tileMode, tileMode, sampling, &at);

	SkRect dest = SkRect::MakeLTRB(destinationRect.left, destinationRect.top,
		destinationRect.right + 1, destinationRect.bottom + 1);
	if (fIdentityTransform && !fHasAlphaMask && !tile) {
		SkIRect bounds = dest.round();
		if (!hasScale) {
			if (fDrawingMode == B_OP_COPY && !fHasAlphaMask) {
				// Bgr32Copy, CMap8Copy: the bytes as they are, kSrc
				SkPaint copy;
				copy.setBlendMode(SkBlendMode::kSrc);
				copy.setShader(source.colors);
				_DirectDraw(bounds, SkMatrix::I(), [&](SkCanvas* canvas) {
					canvas->drawRect(dest, copy);
				});
				return touched;
			}
			if (space == B_CMAP8 && fDrawingMode == B_OP_OVER)
				source.special = kSpecialCopyIfAlpha;
			else if (fDrawingMode == B_OP_COPY)
				source.special = kSpecialCopy32;
			else if (space == B_RGB32 && fDrawingMode == B_OP_OVER)
				source.special = kSpecialOver32Magic;
			else if (fDrawingMode == B_OP_OVER
				|| (fDrawingMode == B_OP_ALPHA
					&& fAlphaSrcMode == B_PIXEL_ALPHA
					&& fAlphaFncMode == B_ALPHA_OVERLAY))
				source.special = kSpecialAlphaBlend;
			// M13 (option E of docs/M13-PLAN.md): on an opaque image in the
			// new look these three are the hardware's own blend over the
			// bitmap made premultiplied, with no copy of the destination.
			// B_OP_OVER on B_CMAP8 (alpha 0 or 255) writes a pixel whole
			// or not at all; B_OP_ALPHA and B_OP_OVER with an alpha channel
			// round in floating point (a step of 1 at most). B_OP_OVER on
			// B_RGB32 (the magic colour) keeps the R5 shader: done this way
			// it failed bitmap-rgb32-over/opaque in the new-look compare
			// (QEMU, 17 Nov 2026). The classic look, where the compare set
			// runs, keeps the R5 shader.
			if (sHardwareBlend && fTarget->IsOpaque() && !SkiaPainter::Classic()
				&& (source.special == kSpecialAlphaBlend
					|| source.special == kSpecialCopyIfAlpha)) {
				sk_sp<SkRuntimeEffect> effect = bitmap_over_effect();
				if (effect != nullptr) {
					SkRuntimeShaderBuilder builder(effect);
					builder.child("image") = source.colors;
					builder.uniform("mode") = source.special == kSpecialAlphaBlend
						? 0 : source.special == kSpecialOver32Magic ? 1 : 2;
					SkPaint over;
					over.setBlendMode(SkBlendMode::kSrcOver);
					over.setShader(builder.makeShader());
					_DirectDraw(bounds, SkMatrix::I(), [&](SkCanvas* canvas) {
						canvas->drawRect(dest, over);
					});
					return touched;
				}
			}
			if (source.special != kSpecialNone) {
				_Composite(bounds, nullptr, source);
				return touched;
			}
		} else if (fDrawingMode == B_OP_COPY
			|| (fDrawingMode == B_OP_ALPHA && bilinear
				&& fAlphaSrcMode == B_PIXEL_ALPHA
				&& fAlphaFncMode == B_ALPHA_OVERLAY)) {
			// DrawBitmapNearestNeighborCopy and DrawBitmapBilinear: Painter's
			// own index and weights, on the texels as they are
			source.special = fDrawingMode == B_OP_COPY
				? (bilinear ? kSpecialRgbCopy : kSpecialCopy32)
				: kSpecialAlphaOverlayRgb;
			source.sampling = bilinear ? 2 : 1;
			// DrawBitmapBilinear::Draw(): on x86_64 the low filter ratio
			// variant for these scales in B_OP_COPY
			if (bilinear && fDrawingMode == B_OP_COPY && scaleX == scaleY
				&& (scaleX == 1.5 || scaleX == 2.0 || scaleX == 2.5
					|| scaleX == 3.0))
				source.sampling = 3;
			source.destOrigin[0] = destinationRect.left;
			source.destOrigin[1] = destinationRect.top;
			source.srcSize[0] = sourceRect.IntegerWidth() + 1;
			source.srcSize[1] = sourceRect.IntegerHeight() + 1;
			source.scale[0] = scaleX;
			source.scale[1] = scaleY;
			source.srcShift[0] = sourceRect.left;
			source.srcShift[1] = sourceRect.top;
			source.colors = image->makeShader(SkTileMode::kClamp,
				SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest));
			_Composite(bounds, nullptr, source);
			return touched;
		}
		// DrawBitmapGeneric<Fill>: the mode's colour span, full coverage
		_Composite(bounds, nullptr, source);
		return touched;
	}

	// DrawBitmapGeneric: the destination rectangle under the transform
	SkPathBuilder builder;
	if (tile) {
		builder.addRect(SkRect::MakeLTRB(viewRect.left, viewRect.top,
			viewRect.right + 1, viewRect.bottom + 1));
	} else
		builder.addRect(dest);
	SkPath path = builder.detach();
	SkRect bounds = _Matrix().mapRect(path.getBounds());
	if (fTarget->Scratch() == NULL)
		return touched;
	SkIRect ibounds = bounds.roundOut();
	ibounds.outset(1, 1);
	SkCanvas* canvas = fTarget->Scratch()->getCanvas();
	canvas->save();
	canvas->clipIRect(ibounds);
	canvas->clear(SK_ColorTRANSPARENT);
	canvas->concat(_Matrix());
	SkPaint paint;
	paint.setColor(SK_ColorWHITE);
	paint.setAntiAlias(true);
	canvas->drawPath(path, paint);
	canvas->restore();
	SkIRect surfaceBounds = SkIRect::MakeXYWH(fTarget->Left(),
		fTarget->Top(), fTarget->Width(), fTarget->Height());
	if (!ibounds.intersect(surfaceBounds))
		return touched;
	sk_sp<SkImage> coverage = fTarget->Scratch()->makeImageSnapshot(
		ibounds.makeOffset(-fTarget->Left(), -fTarget->Top()));
	_Composite(ibounds, coverage, source);
	return touched;
}


//	#pragma mark - gradients


/*!	Painter::_RasterizePath(path, gradient): AGG's gradient function for
	the type, on the 256 colours of Painter::_MakeGradient(array, gradient).
*/
/*!	M13, option E: Haiku's straight-alpha bitmap made premultiplied for the
	hardware's kSrcOver. mode 0: the alpha channel; 1: B_RGB32, the magic
	colour (B_TRANSPARENT_MAGIC_RGBA32 as bytes B 0x77, G 0x74, R 0x74,
	A 0x77) is transparent and every other pixel opaque; 2: alpha 0 is
	transparent and every other alpha opaque (B_CMAP8, converted).
*/
static sk_sp<SkRuntimeEffect>
bitmap_over_effect()
{
	static sk_sp<SkRuntimeEffect> sEffect = SkRuntimeEffect::MakeForShader(
		SkString(
			"uniform shader image;\n"
			"uniform int mode;\n"
			"half4 main(float2 p) {\n"
			"  half4 c = image.eval(p);\n"
			"  if (mode == 1) {\n"
			"    half3 d = abs(c.rgb - half3(116.0, 116.0, 119.0) / 255.0);\n"
			"    bool magic = max(max(d.r, d.g), d.b) < 0.5 / 255.0\n"
			"      && abs(c.a - 119.0 / 255.0) < 0.5 / 255.0;\n"
			"    return magic ? half4(0) : half4(c.rgb, 1);\n"
			"  }\n"
			"  if (mode == 2)\n"
			"    return c.a > 0.0 ? half4(c.rgb, 1) : half4(0);\n"
			"  return half4(c.rgb * c.a, c.a);\n"
			"}\n")).effect;
	return sEffect;
}


sk_sp<SkShader>
SkiaPainter::_GradientShader(const BGradient& gradient, bool* _ok)
{
	*_ok = false;
	sk_sp<SkRuntimeEffect> effect = gradient_effect();
	if (effect == nullptr || fTarget == NULL)
		return nullptr;

	// the transform as Painter builds it, inverted (agg::trans_affine)
	agg::trans_affine transform;
	if (!fIdentityTransform) {
		transform = agg::trans_affine(fTransform.sx, fTransform.shy,
			fTransform.shx, fTransform.sy, fTransform.tx, fTransform.ty);
	}
	agg::trans_affine matrix;
	int type = 0;
	float d2 = 100;
	switch (gradient.GetType()) {
		case BGradient::TYPE_LINEAR:
		{
			const BGradientLinear& linear = (const BGradientLinear&)gradient;
			BPoint start = linear.Start();
			BPoint end = linear.End();
			float dx = end.x - start.x;
			float dy = end.y - start.y;
			matrix *= agg::trans_affine_scaling(sqrt(dx * dx + dy * dy) / 100);
			matrix *= agg::trans_affine_rotation(atan2(dy, dx));
			matrix *= agg::trans_affine_translation(start.x, start.y);
			type = 0;
			break;
		}
		case BGradient::TYPE_RADIAL:
		{
			const BGradientRadial& radial = (const BGradientRadial&)gradient;
			matrix *= agg::trans_affine_translation(radial.Center().x,
				radial.Center().y);
			type = 1;
			d2 = radial.Radius();
			break;
		}
		case BGradient::TYPE_RADIAL_FOCUS:
		{
			// Painter uses agg::gradient_radial_focus with its defaults,
			// focus in the centre: the radial function
			const BGradientRadialFocus& radial
				= (const BGradientRadialFocus&)gradient;
			matrix *= agg::trans_affine_translation(radial.Center().x,
				radial.Center().y);
			type = 1;
			d2 = radial.Radius();
			break;
		}
		case BGradient::TYPE_DIAMOND:
		{
			const BGradientDiamond& diamond = (const BGradientDiamond&)gradient;
			matrix *= agg::trans_affine_translation(diamond.Center().x,
				diamond.Center().y);
			type = 2;
			break;
		}
		case BGradient::TYPE_CONIC:
		{
			const BGradientConic& conic = (const BGradientConic&)gradient;
			matrix *= agg::trans_affine_translation(conic.Center().x,
				conic.Center().y);
			type = 3;
			break;
		}
		default:
			return nullptr;
	}
	matrix *= transform;
	matrix.invert();
	if (d2 <= 0)
		return nullptr;

	// Painter::_MakeGradient(Array&, gradient): agg::rgba8::gradient()
	// between the stops; entries outside the stops are left unset in
	// Painter (stack memory), here they get the nearest stop
	SkBitmap ramp;
	ramp.allocPixels(target_info(256, 1));
	uint32* colors = (uint32*)ramp.getPixels();
	memset(colors, 0, 256 * 4);
	int32 stops = gradient.CountColorStops();
	if (stops <= 0)
		return nullptr;
	for (int32 i = 0; i < stops - 1; i++) {
		BGradient::ColorStop* from = gradient.ColorStopAtFast(i);
		BGradient::ColorStop* to = gradient.ColorStopAtFast(i + 1);
		agg::rgba8 fromColor(from->color.red, from->color.green,
			from->color.blue, from->color.alpha);
		agg::rgba8 toColor(to->color.red, to->color.green, to->color.blue,
			to->color.alpha);
		float dist = to->offset - from->offset;
		if (dist > 0) {
			for (int j = (int)from->offset; j <= (int)to->offset; j++) {
				if (j < 0 || j > 255)
					continue;
				float f = (float)(to->offset - j) / (float)(dist + 1);
				agg::rgba8 c = toColor.gradient(fromColor, f);
				uint8* out = (uint8*)&colors[j];
				out[0] = c.b;
				out[1] = c.g;
				out[2] = c.r;
				out[3] = c.a;
			}
		}
	}
	BGradient::ColorStop* first = gradient.ColorStopAtFast(0);
	BGradient::ColorStop* last = gradient.ColorStopAtFast(stops - 1);
	for (int j = 0; j < 256; j++) {
		BGradient::ColorStop* stop = NULL;
		if (j < (int)first->offset)
			stop = first;
		else if (j > (int)last->offset)
			stop = last;
		if (stop == NULL)
			continue;
		uint8* out = (uint8*)&colors[j];
		out[0] = stop->color.blue;
		out[1] = stop->color.green;
		out[2] = stop->color.red;
		out[3] = stop->color.alpha;
	}
	ramp.setImmutable();
	sk_sp<SkImage> image = fTarget->GradientImage(ramp);
	if (image == nullptr)
		return nullptr;

	double m[6];
	matrix.store_to(m);
	SkRuntimeShaderBuilder builder(effect);
	builder.child("ramp") = image->makeShader(SkTileMode::kClamp,
		SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest));
	builder.uniform("type") = type;
	// store_to: sx, shy, shx, sy, tx, ty
	builder.uniform("row0") = SkV3{ (float)m[0], (float)m[2], (float)m[4] };
	builder.uniform("row1") = SkV3{ (float)m[1], (float)m[3], (float)m[5] };
	builder.uniform("d2") = d2;
	sk_sp<SkShader> shader = builder.makeShader();
	*_ok = shader != nullptr;
	return shader;
}
