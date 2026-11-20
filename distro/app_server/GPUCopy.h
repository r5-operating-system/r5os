/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The last copy from the back buffer to the screen. On by default on a
 * release-supported GPU (M13), otherwise only when gpu_app_server is set.
 * gpu_app_server false turns it off. A failure stays on the CPU copy.
 */

#ifndef GPU_COPY_H
#define GPU_COPY_H

#include <Region.h>
#include <SupportDefs.h>

class RenderingBuffer;


class GPUCopy {
public:
	static	RenderingBuffer*	BackBuffer(uint32 width, uint32 height,
									uint32 bytesPerRow);
	static	bool				Copy(const BRegion& region,
									RenderingBuffer* back,
									RenderingBuffer* front,
									clipping_rect* bandBounds = NULL);
	static	bool				TookOff();
	static	void				NoteCpu(bigtime_t microseconds);

	// M11 step 3b, decision 4 (docs/APP-SERVER-COMPOSITOR.md): the back
	// buffer and the scanout as export numbers for the compositor's Vulkan
	// device (app_server is the target and its own team). False when the
	// back buffer is not this GTT buffer. generation changes whenever the
	// back buffer or the scanout is a new one (a mode change).
	static	bool				ExportForCompositor(RenderingBuffer* back,
									uint8 backToken[16], uint8 scanToken[16],
									uint32* scanPitch, uint32* generation);
	static	uint32				Generation();
	// The calling thread has put this frame on the front itself: its next
	// CopyBackToFront only draws the cursor. Per thread, so a cursor move
	// on another thread still copies.
	static	void				SetFrontCurrent(bool current);
	static	bool				FrontCurrent();
	// M13: the GPU passed its checks and is release-supported
	// (R5_GPU_CAP_AVAILABLE and R5_GPU_CAP_RELEASE). The default of the
	// GPU paths that were tested on that GPU.
	static	bool				ReleaseDefault();
};

#endif
