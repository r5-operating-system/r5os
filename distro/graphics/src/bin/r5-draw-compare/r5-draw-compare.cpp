/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 step 2 (docs/APP-SERVER-GPU-DRAWING.md, part 7): the compare set.
 * Each operation is recorded into a BPicture with the public BView calls and
 * sent to app_server, which plays it into Painter and into Skia and
 * compares the two with the tolerance of the operation's class. app_server
 * accepts it only with gpu_debug true and gpu_draw true.
 *
 *   r5-draw-compare [--time] [--scale <f>] [--only <text>]
 *
 * One line per operation and background:
 *   draw-compare <op> ok max <n> share <p>%
 *   draw-compare <op> FAIL max <n> share <p>% class <c> [inner <n>]
 * and at the end
 *   draw-compare <n>/<n> ok
 * --time adds draw-time <op> gpu <n> us cpu <n> us (median of 100).
 * --scale draws Skia at <f> against Painter under a <f> transform (not a
 * pass line; "Scaling later" point 6).
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Application.h>
#include <Bitmap.h>
#include <DataIO.h>
#include <Font.h>
#include <GradientConic.h>
#include <GradientDiamond.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <Picture.h>
#include <Region.h>
#include <Shape.h>
#include <View.h>

#include <AppServerLink.h>
#include <ServerProtocol.h>

#include <functional>
#include <string>
#include <vector>


enum {
	kE = 0, kA1, kA2, kT, kB1, kB2, kG
};

static const char* kClassNames[] = { "E", "A1", "A2", "T", "B1", "B2", "G" };


struct Operation {
	std::string						name;
	int								klass;
	int								width;
	int								height;
	int								subpixel;
	std::function<void(BView*)>		draw;
};


static std::vector<Operation> sOperations;
static BBitmap* sAlphaBitmap;
static std::vector<BBitmap*> sBitmaps;


static void
add(const std::string& name, int klass, std::function<void(BView*)> draw,
	int width = 96, int height = 64, int subpixel = 0)
{
	Operation op;
	op.name = name;
	op.klass = klass;
	op.width = width;
	op.height = height;
	op.subpixel = subpixel;
	op.draw = draw;
	sOperations.push_back(op);
}


static BBitmap*
make_bitmap(color_space space, int size, bool magic)
{
	BBitmap* rgba = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	for (int y = 0; y < size; y++) {
		uint8* row = (uint8*)rgba->Bits() + y * rgba->BytesPerRow();
		for (int x = 0; x < size; x++) {
			uint8* p = row + x * 4;
			p[0] = (uint8)(x * 255 / size);
			p[1] = (uint8)(y * 255 / size);
			p[2] = (uint8)(((x / 4 + y / 4) & 1) ? 230 : 40);
			p[3] = (uint8)(64 + (x + y) * 191 / (2 * size));
		}
	}
	if (space == B_RGBA32)
		return rgba;
	BBitmap* bitmap = new BBitmap(rgba->Bounds(), space);
	bitmap->ImportBits(rgba);
	delete rgba;
	if (magic && space == B_RGB32) {
		uint32* bits = (uint32*)bitmap->Bits();
		for (int i = 0; i < size; i++)
			bits[i * size + i] = B_TRANSPARENT_MAGIC_RGBA32;
	}
	return bitmap;
}


static BShape*
make_shape()
{
	BShape* shape = new BShape;
	shape->MoveTo(BPoint(10, 50));
	shape->LineTo(BPoint(30, 10));
	BPoint curve[3] = { BPoint(50, 0), BPoint(70, 60), BPoint(86, 30) };
	shape->BezierTo(curve);
	shape->ArcTo(12, 8, 0, false, true, BPoint(60, 56));
	shape->Close();
	return shape;
}


static const pattern kCustomPattern = { { 0x81, 0x42, 0x24, 0x18, 0x18,
	0x24, 0x42, 0x81 } };

static const rgb_color kRed = { 220, 40, 30, 255 };
static const rgb_color kBlue = { 30, 80, 220, 255 };
static const rgb_color kHalf = { 40, 160, 90, 128 };


static void
define_operations()
{
	sAlphaBitmap = make_bitmap(B_RGBA32, 32, false);

	// group 1: the DrawingEngine rows, B_OP_COPY, pen 1 and 3
	for (float pen : { 1.0f, 3.0f }) {
		std::string p = pen == 1.0f ? "pen1" : "pen3";
		add("line-diagonal-" + p, kA1, [pen](BView* v) {
			v->SetPenSize(pen); v->StrokeLine(BPoint(8, 8), BPoint(80, 50)); });
		add("line-straight-" + p, kE, [pen](BView* v) {
			v->SetPenSize(pen); v->StrokeLine(BPoint(8, 20), BPoint(80, 20));
			v->StrokeLine(BPoint(40, 4), BPoint(40, 60)); });
		add("stroke-rect-" + p, kE, [pen](BView* v) {
			v->SetPenSize(pen); v->StrokeRect(BRect(10, 10, 70, 50)); });
		add("stroke-roundrect-" + p, kA1, [pen](BView* v) {
			v->SetPenSize(pen);
			v->StrokeRoundRect(BRect(10, 10, 80, 54), 10, 8); });
		add("stroke-ellipse-" + p, kA1, [pen](BView* v) {
			v->SetPenSize(pen); v->StrokeEllipse(BRect(10, 8, 82, 56)); });
		add("stroke-arc-" + p, kA2, [pen](BView* v) {
			v->SetPenSize(pen);
			v->StrokeArc(BRect(10, 8, 82, 56), 30, 220); });
		add("stroke-bezier-" + p, kA2, [pen](BView* v) {
			v->SetPenSize(pen);
			BPoint pts[4] = { BPoint(8, 50), BPoint(30, 0), BPoint(60, 64),
				BPoint(88, 10) };
			v->StrokeBezier(pts); });
		add("stroke-polygon-" + p, kA2, [pen](BView* v) {
			v->SetPenSize(pen);
			BPoint pts[5] = { BPoint(10, 50), BPoint(30, 8), BPoint(50, 40),
				BPoint(70, 10), BPoint(86, 54) };
			v->StrokePolygon(pts, 5, false); });
		add("stroke-triangle-" + p, kA2, [pen](BView* v) {
			v->SetPenSize(pen);
			v->StrokeTriangle(BPoint(10, 54), BPoint(48, 6), BPoint(86, 50)); });
		add("stroke-shape-" + p, kA2, [pen](BView* v) {
			v->SetPenSize(pen); BShape* s = make_shape(); v->StrokeShape(s);
			delete s; });
	}
	add("fill-rect", kE, [](BView* v) { v->FillRect(BRect(10, 10, 70, 50)); });
	add("fill-rect-fraction", kE, [](BView* v) {
		v->FillRect(BRect(10.4, 10.6, 70.5, 50.2)); });
	add("fill-roundrect", kA1, [](BView* v) {
		v->FillRoundRect(BRect(10, 10, 80, 54), 10, 8); });
	add("fill-ellipse", kA1, [](BView* v) {
		v->FillEllipse(BRect(10, 8, 82, 56)); });
	add("fill-ellipse-small", kA1, [](BView* v) {
		v->FillEllipse(BRect(10, 10, 16, 15)); });
	add("fill-arc", kA2, [](BView* v) {
		v->FillArc(BRect(10, 8, 82, 56), 30, 220); });
	add("fill-bezier", kA2, [](BView* v) {
		BPoint pts[4] = { BPoint(8, 50), BPoint(30, 0), BPoint(60, 64),
			BPoint(88, 10) };
		v->FillBezier(pts); });
	add("fill-polygon", kA2, [](BView* v) {
		BPoint pts[5] = { BPoint(10, 50), BPoint(30, 8), BPoint(50, 40),
			BPoint(70, 10), BPoint(86, 54) };
		v->FillPolygon(pts, 5); });
	add("fill-triangle", kA2, [](BView* v) {
		v->FillTriangle(BPoint(10, 54), BPoint(48, 6), BPoint(86, 50)); });
	add("fill-shape", kA2, [](BView* v) {
		BShape* s = make_shape(); v->FillShape(s); delete s; });
	add("fill-region", kE, [](BView* v) {
		BRegion r; r.Include(BRect(5, 5, 30, 30)); r.Include(BRect(20, 25, 60, 50));
		r.Include(BRect(65, 10, 90, 20)); v->FillRegion(&r); });
	add("invert-rect", kE, [](BView* v) {
		v->SetHighColor(kRed); v->FillRect(BRect(5, 5, 50, 40));
		v->InvertRect(BRect(20, 20, 80, 55)); });
	add("line-array", kA1, [](BView* v) {
		v->BeginLineArray(3);
		v->AddLine(BPoint(5, 5), BPoint(90, 5), kRed);
		v->AddLine(BPoint(5, 10), BPoint(90, 60), kBlue);
		v->AddLine(BPoint(50, 2), BPoint(50, 62), kHalf);
		v->EndLineArray(); });
	add("bitmap-unscaled", kE, [](BView* v) {
		v->DrawBitmap(sAlphaBitmap, BPoint(20, 16)); });

	// group 2: every drawing mode on four shapes
	struct ModeName { drawing_mode mode; const char* name; };
	static const ModeName kModes[] = {
		{ B_OP_COPY, "copy" }, { B_OP_OVER, "over" }, { B_OP_ERASE, "erase" },
		{ B_OP_INVERT, "invert" }, { B_OP_ADD, "add" },
		{ B_OP_SUBTRACT, "subtract" }, { B_OP_BLEND, "blend" },
		{ B_OP_MIN, "min" }, { B_OP_MAX, "max" }, { B_OP_SELECT, "select" },
		{ B_OP_ALPHA, "alpha" } };
	for (const ModeName& m : kModes) {
		drawing_mode mode = m.mode;
		std::string n = m.name;
		auto prepare = [mode](BView* v) {
			// a background to blend with, then the shape in the mode
			v->SetHighColor(kBlue); v->FillRect(BRect(0, 0, 47, 63));
			v->SetHighColor(kRed); v->SetLowColor(kBlue);
			if (mode == B_OP_ALPHA)
				v->SetHighColor(kHalf);
			v->SetDrawingMode(mode);
		};
		add("mode-" + n + "-rect", kE, [prepare](BView* v) {
			prepare(v); v->FillRect(BRect(10, 10, 80, 50)); });
		add("mode-" + n + "-ellipse", kA1, [prepare](BView* v) {
			prepare(v); v->FillEllipse(BRect(10, 6, 86, 58)); });
		add("mode-" + n + "-text", kT, [prepare](BView* v) {
			prepare(v); v->SetFontSize(18); v->DrawString("Mode", BPoint(8, 40)); });
		add("mode-" + n + "-bitmap", kE, [prepare](BView* v) {
			prepare(v); v->DrawBitmap(sAlphaBitmap, BPoint(30, 16)); });
	}

	// group 3: B_OP_ALPHA, both sources, every alpha function
	struct AlphaName { alpha_function fn; const char* name; };
	static const AlphaName kAlphas[] = {
		{ B_ALPHA_OVERLAY, "overlay" }, { B_ALPHA_COMPOSITE, "composite" },
		{ B_ALPHA_COMPOSITE_SOURCE_IN, "source-in" },
		{ B_ALPHA_COMPOSITE_SOURCE_OUT, "source-out" },
		{ B_ALPHA_COMPOSITE_SOURCE_ATOP, "source-atop" },
		{ B_ALPHA_COMPOSITE_DESTINATION_OVER, "destination-over" },
		{ B_ALPHA_COMPOSITE_DESTINATION_IN, "destination-in" },
		{ B_ALPHA_COMPOSITE_DESTINATION_OUT, "destination-out" },
		{ B_ALPHA_COMPOSITE_DESTINATION_ATOP, "destination-atop" },
		{ B_ALPHA_COMPOSITE_XOR, "xor" }, { B_ALPHA_COMPOSITE_CLEAR, "clear" },
		{ B_ALPHA_COMPOSITE_DIFFERENCE, "difference" },
		{ B_ALPHA_COMPOSITE_LIGHTEN, "lighten" },
		{ B_ALPHA_COMPOSITE_DARKEN, "darken" } };
	for (source_alpha src : { B_PIXEL_ALPHA, B_CONSTANT_ALPHA }) {
		for (const AlphaName& a : kAlphas) {
			if (src == B_CONSTANT_ALPHA && a.fn != B_ALPHA_OVERLAY
				&& a.fn != B_ALPHA_COMPOSITE)
				continue;
			alpha_function fn = a.fn;
			std::string n = std::string(src == B_PIXEL_ALPHA ? "pixel-"
				: "constant-") + a.name;
			auto prepare = [src, fn](BView* v) {
				v->SetHighColor(kBlue); v->FillRect(BRect(0, 0, 47, 63));
				v->SetHighColor(kHalf);
				v->SetDrawingMode(B_OP_ALPHA);
				v->SetBlendingMode(src, fn);
			};
			add("alpha-" + n + "-rect", kE, [prepare](BView* v) {
				prepare(v); v->FillRect(BRect(10, 10, 80, 50)); });
			add("alpha-" + n + "-ellipse", kA1, [prepare](BView* v) {
				prepare(v); v->FillEllipse(BRect(10, 6, 86, 58)); });
		}
	}

	// group 4: patterns with an odd offset
	for (int which = 0; which < 2; which++) {
		pattern pat = which == 0 ? B_MIXED_COLORS : kCustomPattern;
		std::string n = which == 0 ? "mixed" : "custom";
		auto prepare = [](BView* v) {
			v->SetOrigin(3, 5); v->SetHighColor(kRed); v->SetLowColor(kBlue); };
		add("pattern-" + n + "-rect", kE, [prepare, pat](BView* v) {
			prepare(v); v->FillRect(BRect(5, 3, 70, 45), pat); });
		add("pattern-" + n + "-ellipse", kA1, [prepare, pat](BView* v) {
			prepare(v); v->FillEllipse(BRect(4, 2, 80, 52), pat); });
		add("pattern-" + n + "-line", kA1, [prepare, pat](BView* v) {
			prepare(v); v->SetPenSize(3);
			v->StrokeLine(BPoint(4, 4), BPoint(80, 50), pat); });
	}

	// group 5: the five gradients with alpha stops
	for (int type = 0; type < 5; type++) {
		static const char* kNames[] = { "linear", "radial", "radial-focus",
			"diamond", "conic" };
		auto make = [type]() -> BGradient* {
			BGradient* g;
			switch (type) {
				case 0: g = new BGradientLinear(BPoint(10, 10), BPoint(80, 50));
					break;
				case 1: g = new BGradientRadial(BPoint(48, 32), 36); break;
				case 2: g = new BGradientRadialFocus(BPoint(48, 32), 36,
					BPoint(40, 28)); break;
				case 3: g = new BGradientDiamond(BPoint(48, 32)); break;
				default: g = new BGradientConic(BPoint(48, 32), 30); break;
			}
			g->AddColor(kRed, 0);
			g->AddColor(kHalf, 128);
			g->AddColor(kBlue, 255);
			return g;
		};
		std::string n = kNames[type];
		add("gradient-" + n + "-rect", kG, [make](BView* v) {
			BGradient* g = make(); v->FillRect(BRect(6, 6, 88, 58), *g);
			delete g; });
		add("gradient-" + n + "-roundrect", kA2, [make](BView* v) {
			BGradient* g = make();
			v->FillRoundRect(BRect(6, 6, 88, 58), 12, 10, *g); delete g; });
		add("gradient-" + n + "-bezier", kA2, [make](BView* v) {
			BGradient* g = make();
			BPoint pts[4] = { BPoint(8, 50), BPoint(30, 0), BPoint(60, 64),
				BPoint(88, 10) };
			v->FillBezier(pts, *g); delete g; });
	}
	add("gradient-vertical-rect", kE, [](BView* v) {
		BGradientLinear g(BPoint(0, 8), BPoint(0, 56));
		g.AddColor(kRed, 0); g.AddColor(kBlue, 255);
		v->FillRect(BRect(6, 6, 88, 58), g); });

	// group 6: transforms
	struct Transform { const char* name; BAffineTransform t; };
	std::vector<Transform> transforms;
	Transform rotate = { "rotate30", BAffineTransform() };
	rotate.t.RotateBy(30 * M_PI / 180);
	transforms.push_back(rotate);
	Transform scale = { "scale1.5", BAffineTransform() };
	scale.t.ScaleBy(1.5);
	transforms.push_back(scale);
	Transform shear = { "shear", BAffineTransform() };
	shear.t.ShearBy(0.3, 0);
	transforms.push_back(shear);
	for (const Transform& t : transforms) {
		BAffineTransform at = t.t;
		std::string n = t.name;
		add("transform-" + n + "-rect", kA1, [at](BView* v) {
			v->SetTransform(at); v->FillRect(BRect(20, 4, 60, 30)); },
			128, 96);
		add("transform-" + n + "-ellipse", kA1, [at](BView* v) {
			v->SetTransform(at); v->FillEllipse(BRect(20, 4, 70, 40)); },
			128, 96);
		add("transform-" + n + "-text", kT, [at](BView* v) {
			v->SetTransform(at); v->SetFontSize(16);
			v->DrawString("Skia", BPoint(20, 30)); }, 128, 96);
		add("transform-" + n + "-bitmap", kB2, [at](BView* v) {
			v->SetTransform(at); v->DrawBitmap(sAlphaBitmap, BPoint(30, 6)); },
			128, 96);
	}

	// group 7: clipping
	add("clip-region", kE, [](BView* v) {
		BRegion r; r.Include(BRect(5, 5, 30, 30)); r.Include(BRect(25, 25, 70, 55));
		r.Include(BRect(60, 5, 90, 20)); v->ConstrainClippingRegion(&r);
		v->SetHighColor(kRed); v->FillRect(v->Bounds());
		v->FillEllipse(BRect(10, 10, 80, 60)); });
	add("clip-picture", kA2, [](BView* v) {
		BPicture mask; v->BeginPicture(&mask);
		v->FillEllipse(BRect(10, 4, 86, 60)); v->EndPicture();
		v->ClipToPicture(&mask); v->SetHighColor(kRed);
		v->FillRect(v->Bounds()); });
	add("clip-picture-inverse", kA2, [](BView* v) {
		BPicture mask; v->BeginPicture(&mask);
		v->FillEllipse(BRect(10, 4, 86, 60)); v->EndPicture();
		v->ClipToInversePicture(&mask); v->SetHighColor(kRed);
		v->FillRect(v->Bounds()); });
	add("clip-shape", kA2, [](BView* v) {
		BShape* s = make_shape(); v->ClipToShape(s); delete s;
		v->SetHighColor(kRed); v->FillRect(v->Bounds()); });
	add("clip-shape-inverse", kA2, [](BView* v) {
		BShape* s = make_shape(); v->ClipToInverseShape(s); delete s;
		v->SetHighColor(kRed); v->FillRect(v->Bounds()); });
	add("clip-nested", kA2, [](BView* v) {
		v->PushState(); BShape* s = make_shape(); v->ClipToShape(s); delete s;
		v->PushState(); v->ClipToRect(BRect(20, 10, 70, 50));
		v->SetHighColor(kRed); v->FillRect(v->Bounds());
		v->PopState(); v->PopState(); });

	// group 8: bitmaps
	struct Space { color_space space; const char* name; };
	static const Space kSpaces[] = { { B_RGB32, "rgb32" },
		{ B_RGBA32, "rgba32" }, { B_CMAP8, "cmap8" }, { B_RGB16, "rgb16" },
		{ B_GRAY8, "gray8" } };
	for (const Space& sp : kSpaces) {
		BBitmap* bitmap = make_bitmap(sp.space, 24, true);
		sBitmaps.push_back(bitmap);
		std::string n = sp.name;
		add("bitmap-" + n + "-unscaled", kE, [bitmap](BView* v) {
			v->DrawBitmap(bitmap, BPoint(20, 12)); });
		add("bitmap-" + n + "-over", kE, [bitmap](BView* v) {
			v->SetDrawingMode(B_OP_OVER); v->DrawBitmap(bitmap, BPoint(20, 12)); });
		for (float scale : { 2.0f, 0.7f }) {
			std::string s = scale == 2.0f ? "x2" : "x0.7";
			add("bitmap-" + n + "-" + s + "-nearest", kB1, [bitmap, scale](BView* v) {
				v->DrawBitmap(bitmap, bitmap->Bounds(),
					BRect(10, 6, 10 + 24 * scale - 1, 6 + 24 * scale - 1)); },
				96, 64);
			add("bitmap-" + n + "-" + s + "-bilinear", kB2, [bitmap, scale](BView* v) {
				v->DrawBitmap(bitmap, bitmap->Bounds(),
					BRect(10, 6, 10 + 24 * scale - 1, 6 + 24 * scale - 1),
					B_FILTER_BITMAP_BILINEAR); }, 96, 64);
		}
	}
	add("bitmap-alpha-overlay-bilinear", kB2, [](BView* v) {
		v->SetDrawingMode(B_OP_ALPHA);
		v->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		v->DrawBitmap(sAlphaBitmap, sAlphaBitmap->Bounds(),
			BRect(8, 4, 71, 59), B_FILTER_BITMAP_BILINEAR); });

	// group 9: text
	struct Face { const char* name; const BFont* font; };
	const Face kFaces[] = { { "plain", be_plain_font }, { "bold", be_bold_font },
		{ "fixed", be_fixed_font } };
	for (const Face& f : kFaces) {
		for (float size : { 9.0f, 12.0f, 24.0f, 72.0f }) {
			BFont font(f.font);
			font.SetSize(size);
			std::string n = std::string(f.name) + "-" + std::to_string((int)size);
			int w = size > 30 ? 320 : 200;
			int h = size > 30 ? 100 : 48;
			add("text-" + n, kT, [font, size](BView* v) {
				v->SetFont(&font); v->DrawString("Haiku R5 gq",
					BPoint(4, size + 4)); }, w, h);
			add("text-" + n + "-aliased", kT, [font, size](BView* v) {
				BFont aliased(font); aliased.SetFlags(B_DISABLE_ANTIALIASING);
				v->SetFont(&aliased); v->DrawString("Haiku R5 gq",
					BPoint(4, size + 4)); }, w, h);
			add("text-" + n + "-subpixel", kT, [font, size](BView* v) {
				v->SetFont(&font); v->DrawString("Haiku R5 gq",
					BPoint(4, size + 4)); }, w, h, 2);
		}
	}
	add("text-shear", kT, [](BView* v) {
		BFont font(be_plain_font); font.SetSize(18); font.SetShear(60);
		v->SetFont(&font); v->DrawString("Shear", BPoint(8, 30)); }, 128, 48);
	add("text-rotation", kT, [](BView* v) {
		BFont font(be_plain_font); font.SetSize(18); font.SetRotation(30);
		v->SetFont(&font); v->DrawString("Turn", BPoint(10, 60)); }, 128, 72);
	add("text-underline-strikeout", kT, [](BView* v) {
		BFont font(be_plain_font); font.SetSize(18);
		font.SetFace(B_UNDERSCORE_FACE | B_STRIKEOUT_FACE);
		v->SetFont(&font); v->DrawString("Lines", BPoint(8, 30)); }, 128, 48);
	add("text-fallback", kT, [](BView* v) {
		v->SetFontSize(18); v->DrawString("A\xe2\x86\x92\xe2\x98\x85 z",
			BPoint(8, 30)); }, 128, 48);
	add("text-offsets", kT, [](BView* v) {
		v->SetFontSize(16);
		BPoint offsets[4] = { BPoint(8, 30), BPoint(30, 20), BPoint(50, 34),
			BPoint(70, 26) };
		v->DrawString("abcd", offsets, 4); }, 128, 48);
	add("text-escapement", kT, [](BView* v) {
		v->SetFontSize(16); escapement_delta delta = { 4, 2 };
		v->DrawString("spaced out", BPoint(4, 30), &delta); }, 160, 48);

	// group 10: subpixel setting, shapes stay grey on the GPU (Q12)
	add("subpixel-ellipse", kA1, [](BView* v) {
		v->FillEllipse(BRect(10, 8, 82, 56)); }, 96, 64, 1);
	add("subpixel-polygon", kA2, [](BView* v) {
		BPoint pts[5] = { BPoint(10, 50), BPoint(30, 8), BPoint(50, 40),
			BPoint(70, 10), BPoint(86, 54) };
		v->FillPolygon(pts, 5); }, 96, 64, 1);

	// group 11: strokes, caps and joins, miter limits, pen sizes
	struct Cap { cap_mode cap; const char* name; };
	struct Join { join_mode join; const char* name; };
	static const Cap kCaps[] = { { B_BUTT_CAP, "butt" },
		{ B_ROUND_CAP, "round" }, { B_SQUARE_CAP, "square" } };
	static const Join kJoins[] = { { B_MITER_JOIN, "miter" },
		{ B_ROUND_JOIN, "round" }, { B_BEVEL_JOIN, "bevel" },
		{ B_BUTT_JOIN, "butt" }, { B_SQUARE_JOIN, "square" } };
	for (const Cap& c : kCaps) {
		for (const Join& j : kJoins) {
			cap_mode cap = c.cap;
			join_mode join = j.join;
			add(std::string("stroke-") + c.name + "-" + j.name, kA2,
				[cap, join](BView* v) {
					v->SetLineMode(cap, join); v->SetPenSize(6);
					BPoint pts[4] = { BPoint(10, 50), BPoint(30, 10),
						BPoint(60, 50), BPoint(86, 12) };
					v->StrokePolygon(pts, 4, false); });
		}
	}
	for (float limit : { 2.0f, 10.0f }) {
		add("stroke-miter-" + std::to_string((int)limit), kA2, [limit](BView* v) {
			v->SetLineMode(B_BUTT_CAP, B_MITER_JOIN, limit); v->SetPenSize(5);
			BPoint pts[3] = { BPoint(10, 54), BPoint(48, 8), BPoint(86, 54) };
			v->StrokePolygon(pts, 3, false); });
	}
	for (float pen : { 0.5f, 1.0f, 2.5f }) {
		std::string n = pen == 0.5f ? "0.5" : pen == 1.0f ? "1" : "2.5";
		add("stroke-pen-" + n, kA2, [pen](BView* v) {
			v->SetPenSize(pen);
			BPoint pts[4] = { BPoint(10, 50), BPoint(30, 10), BPoint(60, 50),
				BPoint(86, 12) };
			v->StrokePolygon(pts, 4, false); });
	}
}


static BBitmap* sCanvas;
static BView* sView;


static BPicture*
record(const Operation& op, float scale)
{
	sCanvas->Lock();
	sView->ResizeTo(op.width - 1, op.height - 1);
	BPicture* picture = new BPicture;
	sView->BeginPicture(picture);
	bool text = op.klass == kT;
	if (!text)
		sView->PushState();
	if (scale != 1.0f)
		sView->SetTransform(BAffineTransform().ScaleBy(scale));
	op.draw(sView);
	if (!text)
		sView->PopState();
	sView->EndPicture();
	if (text) {
		sView->SetTransform(BAffineTransform());
		sView->SetFont(be_plain_font);
		sView->SetHighColor(0, 0, 0);
		sView->SetLowColor(255, 255, 255);
		sView->SetDrawingMode(B_OP_COPY);
		sView->SetOrigin(0, 0);
		sView->MovePenTo(0, 0);
	}
	sCanvas->Unlock();
	return picture;
}


/*!	One flattened picture at bytes (BPicture::Flatten(): two header words,
	the count of nested pictures, each of them flattened the same way, then
	the size and the data) as AS_R5_DRAW_COMPARE sends it: the count, each
	nested picture in this form, the size, the data. ClipToPicture() and
	DrawPicture() refer to the nested ones by their place.
*/
static bool
picture_blob(const uint8* bytes, size_t length, size_t& offset,
	BMallocIO& out, int depth)
{
	if (depth > 8 || offset + 12 > length)
		return false;
	offset += 8;
	int32 count;
	memcpy(&count, bytes + offset, 4);
	offset += 4;
	if (count < 0 || count > 64)
		return false;
	out.Write(&count, 4);
	for (int32 i = 0; i < count; i++) {
		if (!picture_blob(bytes, length, offset, out, depth + 1))
			return false;
	}
	if (offset + 4 > length)
		return false;
	int32 size;
	memcpy(&size, bytes + offset, 4);
	offset += 4;
	if (size < 0 || offset + size > length)
		return false;
	out.Write(&size, 4);
	out.Write(bytes + offset, size);
	offset += size;
	return true;
}


static bool
picture_data(BPicture* picture, BMallocIO& out)
{
	BMallocIO flat;
	if (picture->Flatten(&flat) != B_OK)
		return false;
	size_t offset = 0;
	return picture_blob((const uint8*)flat.Buffer(), flat.BufferLength(),
		offset, out, 0);
}


int
main(int argc, char** argv)
{
	bool timing = false;
	float scale = 1.0f;
	const char* only = NULL;
	// --dump: app_server writes both images of each operation to
	// /boot/home/draw-compare/<n>-painter.pam and <n>-gpu.pam
	bool dumpImages = false;
	// --new-look: Skia draws with the look of step 5b (light hinting, clean
	// grayscale text) instead of the classic one; for the side-by-side
	// sheet, the limits are not meant for it
	bool newLook = false;
	int32 dumpCount = (getpid() % 1000) * 100;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--time") == 0)
			timing = true;
		else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
			scale = atof(argv[++i]);
		else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
			only = argv[++i];
		else if (strcmp(argv[i], "--dump") == 0)
			dumpImages = true;
		else if (strcmp(argv[i], "--new-look") == 0)
			newLook = true;
	}

	BApplication app("application/x-vnd.r5-draw-compare");
	define_operations();
	sCanvas = new BBitmap(BRect(0, 0, 511, 255), B_BITMAP_ACCEPTS_VIEWS,
		B_RGBA32);
	sView = new BView(sCanvas->Bounds(), "compare", B_FOLLOW_NONE, 0);
	sCanvas->AddChild(sView);

	// opaque, and transparent with a colour (a frosted-glass view)
	static const uint32 kBackgrounds[2] = { 0xffd8d8d8, 0x00785028 };
	static const char* kBackgroundNames[2] = { "opaque", "clear" };

	int total = 0;
	int passed = 0;
	for (const Operation& op : sOperations) {
		if (only != NULL && strstr(op.name.c_str(), only) == NULL)
			continue;
		BPicture* picture = record(op, 1.0f);
		BPicture* scaled = scale != 1.0f ? record(op, scale) : record(op, 1.0f);
		for (int b = 0; b < 2; b++) {
			std::string name = op.name + "/" + kBackgroundNames[b];
			BMallocIO data;
			BMallocIO scaledData;
			if (!picture_data(picture, data)
				|| !picture_data(scaled, scaledData)) {
				printf("draw-compare %s FAIL no picture\n", name.c_str());
				total++;
				continue;
			}
			int32 width = (int32)(op.width * (scale > 1 ? scale : 1));
			int32 height = (int32)(op.height * (scale > 1 ? scale : 1));
			BPrivate::AppServerLink link;
			link.StartMessage(AS_R5_DRAW_COMPARE);
			link.Attach<int32>(width);
			link.Attach<int32>(height);
			link.Attach<uint32>(kBackgrounds[b]);
			link.Attach<int32>(op.klass);
			link.Attach<int32>(op.subpixel);
			link.Attach<float>(scale);
			link.Attach<int32>(timing ? 100 : 1);
			link.Attach<int32>(dumpImages ? ++dumpCount : 0);
			link.Attach<int32>(newLook ? 1 : 0);
			if (dumpImages)
				printf("draw-compare dump %d %s\n", (int)dumpCount, name.c_str());
			link.Attach(data.Buffer(), data.BufferLength());
			link.Attach(scaledData.Buffer(), scaledData.BufferLength());
			int32 code;
			if (link.FlushWithReply(code) != B_OK) {
				printf("draw-compare %s FAIL no reply\n", name.c_str());
				total++;
				continue;
			}
			if (code != B_OK) {
				char reason[128] = "";
				link.ReadString(reason, sizeof(reason));
				printf("draw-compare refused: %s\n", reason);
				if (code == B_NOT_ALLOWED)
					return 1;
				printf("draw-compare %s FAIL %s\n", name.c_str(), reason);
				total++;
				continue;
			}
			int32 maxDifference, inner, pixels, ok;
			float share, edge;
			int64 gpuTime, cpuTime;
			link.Read<int32>(&maxDifference);
			link.Read<float>(&share);
			link.Read<int32>(&inner);
			link.Read<float>(&edge);
			int32 penEqual;
			float inkBox;
			link.Read<int32>(&penEqual);
			link.Read<float>(&inkBox);
			link.Read<int32>(&pixels);
			link.Read<int32>(&ok);
			link.Read<int64>(&gpuTime);
			link.Read<int64>(&cpuTime);
			total++;
			if (ok) {
				passed++;
				printf("draw-compare %s ok max %d share %.1f%% edge %.2f px\n",
					name.c_str(), (int)maxDifference, share, edge);
			} else {
				printf("draw-compare %s FAIL max %d share %.1f%% class %s"
					" inner %d edge %.2f px\n", name.c_str(), (int)maxDifference,
					share, kClassNames[op.klass], (int)inner, edge);
			}
			if (op.klass == kT) {
				printf("draw-compare %s metrics %s pen %s ink box %.0f px\n",
					name.c_str(), penEqual && inkBox <= 1 ? "ok" : "FAIL",
					penEqual ? "same" : "different", inkBox);
			}
			if (timing && b == 0) {
				printf("draw-time %s gpu %lld us cpu %lld us\n", op.name.c_str(),
					(long long)gpuTime, (long long)cpuTime);
			}
			fflush(stdout);
		}
		delete picture;
		delete scaled;
	}
	printf("draw-compare %d/%d ok\n", passed, total);
	return passed == total ? 0 : 1;
}
