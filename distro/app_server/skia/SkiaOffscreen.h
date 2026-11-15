/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 (docs/APP-SERVER-GPU-DRAWING.md, part 4 "Alpha masks" and part 6
 * "Offscreen BBitmap views"): a drawing engine that draws on the GPU into
 * a ServerBitmap's memory. The pixels are in the bitmap after
 * skia_offscreen_finish() (a read point). No Skia type here, so Haiku's
 * own files (AlphaMask.cpp, OffscreenWindow.cpp) can call it.
 */
#ifndef SKIA_OFFSCREEN_H
#define SKIA_OFFSCREEN_H


#include <SupportDefs.h>

#include "IntRect.h"


class DrawingEngine;
class HWInterface;
class ServerBitmap;


// The GPU engine of `bitmap` (B_RGBA32 or B_RGB32 memory), whose pixel
// (0, 0) is drawing coordinate (origin.left, origin.top). lockInterface
// gives the locks. NULL when the GPU does not draw now (gpu_draw off, the
// CPU mode, no memory): the caller then uses Painter, as before.
// track: an offscreen window's bitmap, which skia_offscreen_sync() finds.
DrawingEngine*	skia_offscreen_engine(HWInterface* lockInterface,
					ServerBitmap* bitmap, const IntRect& origin,
					bool track = false);

// app_server is about to read this bitmap's pixels (DrawBitmap(), a drag
// bitmap): if the GPU draws into it, its memory is brought up to date.
// The caller holds no drawing engine's lock.
void			skia_offscreen_sync(const ServerBitmap* bitmap);
void			skia_offscreen_untrack(const ServerBitmap* bitmap);

// A read point: everything this engine drew is in the bitmap afterwards.
// upload: the bitmap's memory is copied into the GPU image first (the
// program may have written the bits itself).
status_t		skia_offscreen_finish(DrawingEngine* engine);
status_t		skia_offscreen_upload(DrawingEngine* engine);

// Whether the GPU draws now.
bool			skia_gpu_drawing();


#endif	// SKIA_OFFSCREEN_H
