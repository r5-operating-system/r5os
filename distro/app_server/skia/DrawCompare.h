/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 step 2 (docs/APP-SERVER-GPU-DRAWING.md, part 7): the comparing half
 * of r5-draw-compare, inside app_server, where both engines live. One
 * picture is played into Painter on a memory buffer and into a
 * SkiaDrawingEngine on a GPU image of the same size, from the same
 * background; the two results are compared with the tolerance of the
 * operation's class (part 5). Only with gpu_debug true.
 */
#ifndef DRAW_COMPARE_H
#define DRAW_COMPARE_H


#include <SupportDefs.h>


class ServerPicture;
class SkiaGpu;


enum draw_compare_class {
	kCompareExact = 0,		// E
	kCompareAnalytic,		// A1
	kComparePath,			// A2
	kCompareText,			// T
	kCompareNearest,		// B1
	kCompareBilinear,		// B2
	kCompareGradient		// G
};


struct draw_compare_request {
	int32		width;
	int32		height;
	uint32		background;		// B, G, R, A in memory order
	int32		compareClass;
	int32		subpixel;		// -1 leave, 0 grey, 1 subpixel
	float		scale;			// "Scaling later" point 6, 1 normally
	int32		dump;			// write both images to /boot/home/draw-compare
};


struct draw_compare_result {
	int32		maxDifference;
	float		share;			// percent
	int32		innerDifferences;
	int32		pixels;
	bool		ok;
	bool		gpuFailed;
	int64		gpuTime;		// us, --time
	int64		cpuTime;
};


// picture: the operation; scaledPicture: the same under a 1.5x transform
// for Painter when request.scale is not 1 (may be NULL otherwise).
status_t draw_compare(SkiaGpu* gpu, const draw_compare_request& request,
	ServerPicture* picture, ServerPicture* scaledPicture, int32 repeat,
	draw_compare_result& result);


#endif	// DRAW_COMPARE_H
