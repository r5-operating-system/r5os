/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12: the SkSL of the R5 shaders (docs/APP-SERVER-GPU-DRAWING.md,
 * part 4, "The same blending as Painter").
 *
 * kR5BlendSkSL does, per pixel, what the drawing_modes headers of Painter do: it
 * reads the destination (a copy of the target under the drawing), the
 * coverage Skia computed for the shape, and the source (the pattern's
 * high or low colour, or a colour per pixel from a gradient or a bitmap),
 * and writes Haiku's integer result. A Skia blender gets one source colour
 * only; Haiku's formulas need the colour, its alpha and the coverage, so
 * the destination comes in as an image and the draw uses kSrc.
 *
 * Every value is an 8-bit integer, as in Painter; the arithmetic is in
 * int, so it is exact on every driver.
 */
#ifndef SKIA_SHADERS_H
#define SKIA_SHADERS_H


enum {
	kSpecialNone			= 0,
	kSpecialInvertRect		= 1,	// Painter::_InvertRect32: rgb only
	kSpecialCopy32			= 2,	// Bgr32Copy, CMap8Copy, nearest copy
	kSpecialOver32Magic		= 3,	// Bgr32Over: skip B_TRANSPARENT_MAGIC_RGBA32
	kSpecialCopyIfAlpha		= 4,	// CMap8Over
	kSpecialAlphaBlend		= 5,	// Bgr32Alpha
	kSpecialRgbCopy			= 6,	// DrawBitmapBilinear DrawModeCopy
	kSpecialAlphaOverlayRgb	= 7,	// DrawBitmapBilinear DrawModeAlphaOverlay
	kSpecialBlendLine32		= 8		// _BlendRect32, blend_line32()
};


static const char* kR5BlendSkSL = R"SKSL(
uniform shader dstImage;
uniform shader coverImage;
uniform shader srcColors;
uniform shader maskImage;

uniform int mode;
uniform int alphaKind;
uniform int solid;
uniform int solidLow;
uniform int useColors;
uniform int hasCover;
uniform int hasMask;
uniform int special;
uniform int blendAlpha;
uniform float4 high;
uniform float4 low;
uniform float4 pat0;
uniform float4 pat1;
uniform float2 patOffset;
// the bytes of B_TRANSPARENT_MAGIC_RGBA32 in memory (b, g, r, a)
uniform float4 magic;
// Painter's own scaled bitmap sampling (DrawBitmapNearestNeighbor.h,
// DrawBitmapBilinear.h): 0 Skia's, 1 nearest, 2 bilinear. srcColors is
// then the bitmap in texel coordinates.
uniform int sampling;
uniform float2 destOrigin;
uniform float2 srcSize;
uniform float2 bscale;
uniform float2 srcShift;


int4 toI(half4 c) { return int4(float4(c) * 255.0 + 0.5); }
half4 toH(int4 c) { return half4(float4(c) / 255.0); }

int imin(int a, int b) { return a < b ? a : b; }
int imax(int a, int b) { return a > b ? a : b; }

int3 blendRGB(int3 d, int3 s, int a) { return ((s - d) * a + d * 256) / 256; }
int3 blend16RGB(int3 d, int3 s, int a) { return ((s - d) * a + d * 65536) / 65536; }

int4 over(int4 d, int3 s, int cov) {
	if (cov == 255)
		return int4(s, 255);
	return int4(blendRGB(d.rgb, s, cov), 255);
}

int4 composite(int4 d, int3 s, int a) {
	if (d.a == 255)
		return int4(blendRGB(d.rgb, s, a), 255);
	if (d.a == 0)
		return int4(s, a);
	int rest = 255 - a;
	int temp = 65025 - rest * (255 - d.a);
	int dstA = d.a * rest;
	int srcA = 255 * a;
	return int4((d.rgb * dstA + s * srcA) / temp, temp / 255);
}

int4 alpha16(int4 d, int3 s, int a16, int compositeKind) {
	if (a16 == 0)
		return d;
	if (a16 == 65025)
		return int4(s, 255);
	if (compositeKind != 0)
		return composite(d, s, a16 / 255);
	return int4(blend16RGB(d.rgb, s, a16), 255);
}

int brightness(int3 c) { return (308 * c.r + 600 * c.g + 116 * c.b) / 1024; }

float patRow(int y) {
	if (y == 0) return pat0.x;
	if (y == 1) return pat0.y;
	if (y == 2) return pat0.z;
	if (y == 3) return pat0.w;
	if (y == 4) return pat1.x;
	if (y == 5) return pat1.y;
	if (y == 6) return pat1.z;
	return pat1.w;
}

bool isHigh(float2 px) {
	if (solid != 0)
		return solidLow == 0;
	float fy = px.y - patOffset.y;
	float fx = px.x - patOffset.x;
	int y = int(fy - 8.0 * floor(fy / 8.0));
	int x = int(fx - 8.0 * floor(fx / 8.0));
	float bit = floor(patRow(y) / exp2(float(7 - x)));
	return bit - 2.0 * floor(bit / 2.0) > 0.5;
}

int4 selectSwap(int4 d, int3 h, int3 l, int cov) {
	if (d.rgb == h)
		return over(d, l, cov);
	if (d.rgb == l)
		return over(d, h, cov);
	return d;
}

int4 texel(float x, float y) {
	return toI(srcColors.eval(float2(x + 0.5, y + 0.5)));
}

// Painter: index = i * w / (w * scale), rounded down
int4 sampleNearest(float2 px) {
	float2 i = px - destOrigin;
	float x = floor(i.x * srcSize.x / (srcSize.x * bscale.x)) + srcShift.x;
	float y = floor(i.y * srcSize.y / (srcSize.y * bscale.y)) + srcShift.y;
	return texel(x, y);
}

// Painter: the corners of source and destination meet; 8-bit weights,
// a weight of 255 takes the one pixel
int4 sampleBilinear(float2 px) {
	float2 i = px - destOrigin;
	float fx = i.x * (srcSize.x - 1.0) / (srcSize.x * bscale.x - 1.0);
	float fy = i.y * (srcSize.y - 1.0) / (srcSize.y * bscale.y - 1.0);
	float ix = floor(fx);
	float iy = floor(fy);
	int wl = 255 - int((fx - ix) * 255.0);
	int wt = 255 - int((fy - iy) * 255.0);
	float x = ix + srcShift.x;
	float y = iy + srcShift.y;
	int4 s00 = texel(x, y);
	int4 c0 = s00;
	int4 c1 = s00;
	if (wt != 255) {
		int4 s01 = texel(x, y + 1.0);
		c0 = (s00 * wt + s01 * (255 - wt)) / 256;
	}
	if (wl == 255)
		return c0;
	int4 s10 = texel(x + 1.0, y);
	c1 = s10;
	if (wt != 255) {
		int4 s11 = texel(x + 1.0, y + 1.0);
		c1 = (s10 * wt + s11 * (255 - wt)) / 256;
	}
	return (c0 * wl + c1 * (255 - wl)) / 256;
}

int4 source(float2 p) {
	if (sampling == 1)
		return sampleNearest(floor(p));
	if (sampling == 2)
		return sampleBilinear(floor(p));
	return toI(srcColors.eval(p));
}

int4 blendPixel(int4 d, int cov, float2 p, float2 px) {
	int4 H = int4(high);
	int4 L = int4(low);
	int4 r = d;

	if (special != 0) {
		int4 c = source(p);
		if (special == 1) {
			r = int4(255 - d.rgb, d.a);
		} else if (special == 2) {
			r = c;
		} else if (special == 3) {
			if (c.bgra != int4(magic))
				r = c;
		} else if (special == 4) {
			if (c.a != 0)
				r = c;
		} else if (special == 5) {
			if (c.a == 255)
				r = c;
			else
				r = int4(blendRGB(d.rgb, c.rgb, c.a), d.a);
		} else if (special == 6) {
			r = int4(c.rgb, d.a);
		} else if (special == 7) {
			if (c.a == 255)
				r = int4(c.rgb, d.a);
			else
				r = int4(blendRGB(d.rgb, c.rgb, c.a), d.a);
		} else if (special == 8) {
			int a = blendAlpha;
			int3 s = (H.rgb * a) / 256;
			r = int4((d.rgb * (255 - a)) / 256 + s, d.a);
		}
		return r;
	}

	bool hi = isHigh(px);
	if (useColors == 0) {
		int4 col = hi ? H : L;
		int4 rend = solidLow != 0 ? L : H;
		if (mode == 0) {
			if (solid != 0)
				r = over(d, rend.rgb, cov);
			else if (cov == 255)
				r = int4(col.rgb, 255);
			else
				r = int4(blendRGB(L.rgb, col.rgb, cov), 255);
		} else if (mode == 1) {
			if (hi)
				r = over(d, H.rgb, cov);
		} else if (mode == 2) {
			if (hi)
				r = over(d, L.rgb, cov);
		} else if (mode == 3) {
			if (hi)
				r = over(d, 255 - d.rgb, cov);
		} else if (mode == 4) {
			r = over(d, int3(imin(255, d.r + col.r), imin(255, d.g + col.g),
				imin(255, d.b + col.b)), cov);
		} else if (mode == 5) {
			r = over(d, int3(imax(0, d.r - col.r), imax(0, d.g - col.g),
				imax(0, d.b - col.b)), cov);
		} else if (mode == 6) {
			r = over(d, (d.rgb + col.rgb) / 2, cov);
		} else if (mode == 7) {
			if (brightness(col.rgb) < brightness(d.rgb))
				r = over(d, col.rgb, cov);
		} else if (mode == 8) {
			if (brightness(col.rgb) > brightness(d.rgb))
				r = over(d, col.rgb, cov);
		} else if (mode == 9) {
			if (hi)
				r = selectSwap(d, H.rgb, L.rgb, cov);
		} else if (mode == 10) {
			int3 c3 = solid != 0 ? rend.rgb : col.rgb;
			int ca = solid != 0 ? rend.a : col.a;
			if (alphaKind == 0 || alphaKind == 1) {
				int a16 = H.a * cov;
				if (solid != 0 && alphaKind == 0 && cov == 255 && a16 != 65025
					&& a16 != 0) {
					int a = a16 / 256;
					r = int4((d.rgb * (255 - a)) / 256 + (c3 * a) / 256, d.a);
				} else
					r = alpha16(d, c3, a16, alphaKind);
			} else {
				int a16 = ca * cov;
				if (solid != 0 && alphaKind == 2 && cov == 255 && a16 != 65025
					&& a16 != 0) {
					int a = a16 / 256;
					r = int4((d.rgb * (255 - a)) / 256 + (c3 * a) / 256, d.a);
				} else
					r = alpha16(d, c3, a16, alphaKind == 3 ? 1 : 0);
			}
		}
		return r;
	}

	// a colour per pixel: blend_color_hspan_*()
	int4 c = source(p);
	if (mode == 0) {
		if (solid != 0)
			r = over(d, c.rgb, cov);
		else if (cov == 255)
			r = c;
		else
			r = int4(blendRGB(L.rgb, c.rgb, cov), 255);
	} else if (mode == 10) {
		if (alphaKind == 0 || alphaKind == 1)
			r = alpha16(d, c.rgb, (H.a * c.a * cov) / 255, alphaKind);
		else
			r = alpha16(d, c.rgb, c.a * cov, alphaKind == 3 ? 1 : 0);
	} else if (c.a > 0) {
		if (mode == 1)
			r = over(d, c.rgb, cov);
		else if (mode == 2)
			r = over(d, L.rgb, cov);
		else if (mode == 3)
			r = over(d, 255 - d.rgb, cov);
		else if (mode == 4)
			r = over(d, int3(imin(255, d.r + c.r), imin(255, d.g + c.g),
				imin(255, d.b + c.b)), cov);
		else if (mode == 5)
			r = over(d, int3(imax(0, d.r - c.r), imax(0, d.g - c.g),
				imax(0, d.b - c.b)), cov);
		else if (mode == 6)
			r = over(d, (d.rgb + c.rgb) / 2, cov);
		else if (mode == 7) {
			if (brightness(c.rgb) < brightness(d.rgb))
				r = over(d, c.rgb, cov);
		} else if (mode == 8) {
			if (brightness(c.rgb) > brightness(d.rgb))
				r = over(d, c.rgb, cov);
		} else if (mode == 9)
			r = selectSwap(d, H.rgb, L.rgb, cov);
	}
	return r;
}

half4 main(float2 p) {
	float2 px = floor(p);
	half4 dstColor = dstImage.eval(p);
	int4 d = toI(dstColor);
	if (hasCover == 2) {
		// Skia's subpixel text: black on white, one coverage per channel
		// (the *SUBPIX forms: each channel blended with its own coverage)
		int4 c = toI(coverImage.eval(p));
		int3 cov3 = 255 - c.rgb;
		if (hasMask != 0) {
			int4 m = toI(maskImage.eval(p));
			int3 inside = (255 + cov3 * m.r) / 256;
			cov3 = int3(cov3.r == 0 ? 0 : m.g > 0 ? m.r : inside.r,
				cov3.g == 0 ? 0 : m.g > 0 ? m.r : inside.g,
				cov3.b == 0 ? 0 : m.g > 0 ? m.r : inside.b);
		}
		if (cov3 == int3(0))
			return dstColor;
		int4 rr = cov3.r == 0 ? d : blendPixel(d, cov3.r, p, px);
		int4 rg = cov3.g == 0 ? d : blendPixel(d, cov3.g, p, px);
		int4 rb = cov3.b == 0 ? d : blendPixel(d, cov3.b, p, px);
		return toH(int4(rr.r, rg.g, rb.b, rg.a));
	}
	int cov = 255;
	if (hasCover != 0)
		cov = toI(coverImage.eval(p)).r;
	if (hasMask != 0 && cov > 0) {
		int4 m = toI(maskImage.eval(p));
		cov = m.g > 0 ? m.r : (255 + cov * m.r) / 256;
	}
	if (cov == 0)
		return dstColor;
	return toH(blendPixel(d, cov, p, px));
}
)SKSL";


/*!	AGG's gradient functions as Painter uses them: the pixel centre through
	the inverse gradient transform, the distance in 1/16 pixel
	(gradient_subpixel_shift 4), the index into the 256 colours of
	_MakeGradient().
*/
static const char* kR5GradientSkSL = R"SKSL(
uniform shader ramp;
uniform int type;
uniform float3 row0;
uniform float3 row1;
uniform float d2;

half4 main(float2 p) {
	float2 c = floor(p) + 0.5;
	float x = row0.x * c.x + row0.y * c.y + row0.z;
	float y = row1.x * c.x + row1.y * c.y + row1.z;
	// span_interpolator_linear: iround(v * 256), then >> 4
	float xs = floor(floor(x * 256.0 + 0.5) / 16.0);
	float ys = floor(floor(y * 256.0 + 0.5) / 16.0);
	float d;
	if (type == 0) {
		d = xs;
	} else if (type == 1) {
		d = floor(sqrt(xs * xs + ys * ys));
	} else if (type == 2) {
		d = max(abs(xs), abs(ys));
	} else {
		d = floor(abs(atan(ys, xs)) * (d2 * 16.0) / 3.14159265358979);
	}
	float dd = d2 * 16.0;
	float index = floor((d * 256.0) / dd);
	if (d * 256.0 < 0.0)
		index = -floor((-d * 256.0) / dd);
	index = clamp(index, 0.0, 255.0);
	return ramp.eval(float2(index + 0.5, 0.5));
}
)SKSL";


#endif	// SKIA_SHADERS_H
