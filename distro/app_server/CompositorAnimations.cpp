/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Compositor boot start splash and open, close, and workspace slide animations.
 */


#include "CompositorPrivate.h"


//	#pragma mark - start splash


// The boot picture (src/system/boot/platform/generic/video_splash.cpp, the
// R5 copy in distro/boot): the logo and the icon row as the boot loader
// places them (headers/private/kernel/boot/platform/generic/video_splash.h,
// sizes and placement from images-sans-tm.h, which app_server does not
// include: its pictures are GPL-3.0-or-later). The pixels come from the
// screen itself, not from that header.
static const int32 kSplashLogoWidth = 372;
static const int32 kSplashLogoHeight = 96;
static const int32 kSplashIconsWidth = 308;
static const int32 kSplashIconsHalfHeight = 40;
static const int32 kSplashPlacement = 50;
// The wave pebble inside the logo: 58 pixels at x 22, with its shadow
// (scripts/generate-r5-artwork.py, render_boot_splash()); the text starts
// at x 94
static const int32 kSplashPebbleLeft = 10;
static const int32 kSplashPebbleRight = 92;
// The icon row fades out, then the pebble pulses (owner, 17 Nov 2026)
static const bigtime_t kSplashFade = 300000;
static const bigtime_t kSplashPulse = 1600000;
static const float kSplashPulseLowest = 0.55f;
// Built: nothing changed for this long after everything was drawn
static const bigtime_t kSplashQuiet = 300000;
// Never longer than this: longer than the longest first start measured
// on Lavapipe in QEMU (warm-up 47.2 s, CHANGELOG M13 step 4)
static const bigtime_t kSplashLongest = 60000000;


static inline uint32
splash_pixel(const uint8* bits, uint32 bytesPerRow, int32 x, int32 y)
{
	return ((const uint32*)(bits + (size_t)y * bytesPerRow))[x] & 0x00ffffff;
}


//!	Any thread, from Window::EndUpdate().
void
Compositor::WindowDrawn()
{
	if (!fSplashHolding)
		return;
	// the first one starts the wait; a later one (the window drew again)
	// makes it wait for that drawing too
	if (atomic_get_and_set(&fSplashAsked, 1) != 0)
		atomic_set(&fSplashRenew, 1);
	if (fWakeSem >= 0)
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	Create(), before the compose thread: copies the boot picture into
	three buffers (the text, the pebble, the icon row). The picture is the
	screen as the boot loader left it, copied before app_server's first
	mode switch (AccelerantHWInterface::TakeBootPicture()), or else the
	screen now. Its parts go where the boot loader would put them on this
	screen, so they keep their size when the mode changed. False when
	there is no boot picture (safe mode's picture, a restarted app_server):
	then there is no splash, as before.
*/
bool
Compositor::_CaptureSplash(const char** _reason)
{
	RenderingBuffer* front = fScreen->FrontBuffer();
	if (front == NULL) {
		*_reason = "no screen";
		return false;
	}
	int32 width = (int32)front->Width();
	int32 height = (int32)front->Height();

	// the source: the boot picture, or the screen
	uint32* boot = NULL;
	int32 sourceWidth = 0;
	int32 sourceHeight = 0;
	const uint8* bits;
	uint32 bytesPerRow;
	AccelerantHWInterface* accelerant
		= dynamic_cast<AccelerantHWInterface*>(fScreen);
	if (accelerant != NULL
		&& accelerant->TakeBootPicture(&boot, &sourceWidth, &sourceHeight)) {
		bits = (const uint8*)boot;
		bytesPerRow = (uint32)sourceWidth * 4;
	} else {
		if (front->Bits() == NULL || (front->ColorSpace() != B_RGB32
				&& front->ColorSpace() != B_RGBA32)) {
			*_reason = "no boot picture";
			return false;
		}
		bits = (const uint8*)front->Bits();
		bytesPerRow = front->BytesPerRow();
		sourceWidth = width;
		sourceHeight = height;
	}
	struct FreeBoot {
		uint32* bits;
		~FreeBoot() { free(bits); }
	} freeBoot = { boot };

	int32 block = kSplashLogoHeight + kSplashIconsHalfHeight;
	if (width < kSplashLogoWidth || height < block
		|| sourceWidth < kSplashLogoWidth || sourceHeight < block) {
		*_reason = "screen too small";
		return false;
	}

	// video_splash.h: compute_splash_logo_placement() and
	// compute_splash_icons_placement(), in the source and on the screen
	int32 logoLeft = (sourceWidth - kSplashLogoWidth) * kSplashPlacement
		/ 100;
	int32 logoTop = (sourceHeight - block) * kSplashPlacement / 100;
	int32 iconsLeft = (sourceWidth - kSplashIconsWidth) * kSplashPlacement
		/ 100;
	int32 iconsTop = kSplashLogoHeight + (sourceHeight - block)
		* kSplashPlacement / 100;
	int32 shiftX = (width - kSplashLogoWidth) * kSplashPlacement / 100
		- logoLeft;
	int32 shiftY = (height - block) * kSplashPlacement / 100 - logoTop;
	int32 iconsShiftX = (width - kSplashIconsWidth) * kSplashPlacement / 100
		- iconsLeft;

	// the boot loader fills the screen with white; the logo's text is dark
	if (splash_pixel(bits, bytesPerRow, 0, 0) != 0xffffff
		|| splash_pixel(bits, bytesPerRow, sourceWidth - 1, 0) != 0xffffff
		|| splash_pixel(bits, bytesPerRow, 0, sourceHeight - 1) != 0xffffff
		|| splash_pixel(bits, bytesPerRow, sourceWidth - 1,
			sourceHeight - 1) != 0xffffff) {
		*_reason = "no boot picture";
		return false;
	}
	int32 dark = 0;
	for (int32 y = logoTop; y < logoTop + kSplashLogoHeight; y++) {
		for (int32 x = logoLeft + kSplashPebbleRight;
				x < logoLeft + kSplashLogoWidth; x++) {
			uint32 pixel = splash_pixel(bits, bytesPerRow, x, y);
			if ((pixel & 0xff) + ((pixel >> 8) & 0xff)
					+ ((pixel >> 16) & 0xff) < 300)
				dark++;
		}
	}
	if (dark < 100) {
		*_reason = "no boot picture";
		return false;
	}

	struct {
		BReference<WindowBuffer>*	buffer;
		int32						left;
		int32						top;
		int32						width;
		int32						height;
	} parts[3] = {
		{ &fSplashText, logoLeft + kSplashPebbleRight, logoTop,
			kSplashLogoWidth - kSplashPebbleRight, kSplashLogoHeight },
		{ &fSplashPebble, logoLeft + kSplashPebbleLeft, logoTop,
			kSplashPebbleRight - kSplashPebbleLeft, kSplashLogoHeight },
		{ &fSplashIcons, iconsLeft, iconsTop, kSplashIconsWidth,
			min_c(kSplashIconsHalfHeight, sourceHeight - iconsTop) }
	};
	for (int32 i = 0; i < 3; i++) {
		WindowBuffer* buffer = _AllocateBuffer(parts[i].width,
			parts[i].height, true);
		if (buffer == NULL) {
			_EndSplash();
			*_reason = "window memory";
			return false;
		}
		parts[i].buffer->SetTo(buffer, true);
		uint32* target = (uint32*)buffer->Bits();
		for (int32 y = 0; y < parts[i].height; y++) {
			for (int32 x = 0; x < parts[i].width; x++) {
				target[y * parts[i].width + x] = splash_pixel(bits,
					bytesPerRow, parts[i].left + x, parts[i].top + y)
					| 0xff000000;
			}
		}
	}

	fSplashTextLeft = parts[0].left + shiftX;
	fSplashPebbleLeft = parts[1].left + shiftX;
	fSplashLogoTop = logoTop + shiftY;
	fSplashIconsLeft = iconsLeft + iconsShiftX;
	fSplashIconsTop = iconsTop + shiftY;
	fSplashArea = BRect((float)(logoLeft + shiftX), (float)fSplashLogoTop,
		(float)(logoLeft + shiftX + kSplashLogoWidth - 1),
		(float)(fSplashLogoTop + kSplashLogoHeight - 1));
	fSplashArea = fSplashArea | BRect((float)fSplashIconsLeft,
		(float)fSplashIconsTop,
		(float)(fSplashIconsLeft + kSplashIconsWidth - 1),
		(float)(fSplashIconsTop + parts[2].height - 1));
	if (boot != NULL && (sourceWidth != width || sourceHeight != height)) {
		log_line("start splash from %" B_PRId32 "x%" B_PRId32
			" to %" B_PRId32 "x%" B_PRId32, sourceWidth, sourceHeight,
			width, height);
	}
	return true;
}


/*!	Compose thread: the picture of this frame, as layers that the compose
	draws on white. The text stays; the icon row and the pebble are drawn
	as an opening window is (scaled about the centre and faded,
	_RecordWindowAnimation()), so no pipeline is new and none is Skia's.
*/
void
Compositor::_SplashLayers(LayerList& layers, bigtime_t now)
{
	bigtime_t since = now - fSplashStart;

	float icons = 1.0f - (float)since / kSplashFade;
	if (icons < 0.0f)
		icons = 0.0f;
	float pebble = -1.0f;
	if (since > kSplashFade) {
		float phase = (float)((since - kSplashFade) % kSplashPulse)
			/ kSplashPulse;
		float middle = (1.0f + kSplashPulseLowest) / 2;
		pebble = middle + (1.0f - middle) * cosf(phase * 2.0f * (float)M_PI);
	}

	struct {
		BReference<WindowBuffer>*	buffer;
		int32						left;
		int32						top;
		float						visible;
	} parts[3] = {
		{ &fSplashText, fSplashTextLeft, fSplashLogoTop, -1.0f },
		{ &fSplashPebble, fSplashPebbleLeft, fSplashLogoTop, pebble },
		{ &fSplashIcons, fSplashIconsLeft, fSplashIconsTop, icons }
	};
	for (int32 i = 0; i < 3; i++) {
		// a faded-out part is not drawn; the white is
		if (!parts[i].buffer->IsSet() || parts[i].visible == 0.0f)
			continue;
		Layer* layer = new(std::nothrow) Layer;
		if (layer == NULL)
			continue;
		WindowBuffer* buffer = parts[i].buffer->Get();
		layer->buffer.SetTo(buffer);
		layer->left = parts[i].left;
		layer->top = parts[i].top;
		layer->shape.Set(BRect((float)layer->left, (float)layer->top,
			(float)(layer->left + buffer->Width() - 1),
			(float)(layer->top + buffer->Height() - 1)));
		layer->visible = parts[i].visible;
		layers.AddItem(layer);
	}
}


/*!	Compose thread: whether the screen behind the picture is built. A
	window with a frame drew all it was asked to (WindowDrawn()); Skia has
	drawn everything recorded up to its last such drawing; every window
	has pixels; and no window's pixels changed for kSplashQuiet. Or
	kSplashLongest passed.
*/
bool
Compositor::_SplashDone(const LayerList& layers, bigtime_t now,
	const char** _reason)
{
	if (now - fSplashStart >= kSplashLongest) {
		*_reason = "time";
		return true;
	}
	// On Lavapipe, keep the pulsing splash up while Skia precompiles its
	// common shaders so the first screen does not draw half-built. RADV is
	// unchanged.
	if (fOnLavapipe && fSkia != NULL && !fSkia->IsShutDown()
		&& !fSkia->WarmUpDone()) {
		return false;
	}
	if (atomic_get(&fSplashAsked) == 0)
		return false;
	if (fSkia != NULL && !fSkia->IsShutDown()) {
		// one ticket at a time, so drawing again while Lavapipe is slow
		// does not move the goal on each frame
		if (fSplashTicket == 0
			|| (fSkia->TicketDone(fSplashTicket)
				&& atomic_get_and_set(&fSplashRenew, 0) != 0)) {
			fSplashTicket = fSkia->AskTicket();
		}
		if (!fSkia->TicketDone(fSplashTicket))
			return false;
	} else
		atomic_set(&fSplashRenew, 0);
	for (int32 i = 0; i < layers.CountItems(); i++) {
		if (!layers.ItemAt(i)->HasPixels())
			return false;
	}
	if (now - fSplashChanged < kSplashQuiet)
		return false;
	*_reason = "drawn";
	return true;
}


//!	The picture's buffers go (the hold ended, the CPU mode, the end).
void
Compositor::_EndSplash()
{
	fSplashHolding = false;
	fSplashText.Unset();
	fSplashPebble.Unset();
	fSplashIcons.Unset();
}


//	#pragma mark - animations (M11 step 4b)


/*!	Before a frame: damages where an animation draws, ends the ones that
	are over (one line each, with its length and frames), and says whether
	one still runs. An animation never holds input: the windows draw and
	get their events as always; only the compose is drawn differently.
*/
bool
Compositor::_AnimationDamage(bigtime_t now)
{
	BRegion area;
	bool running = false;
	{
		BAutolock entryLock(fEntryLock);
		BAutolock _(fAnimLock);
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (!entry->animOpen)
				continue;
			area.Include(&entry->animArea);
			bigtime_t took = now - entry->animStart;
			if (took < kOpenAnimation) {
				running = true;
				continue;
			}
			entry->animOpen = false;
			log_line("animation open %" B_PRId64 " ms frames %" B_PRId32,
				took / 1000, entry->animFrames);
		}
		for (int32 i = fGhosts.CountItems(); i-- > 0;) {
			Ghost* ghost = fGhosts.ItemAt(i);
			area.Include(&ghost->area);
			bigtime_t took = now - ghost->start;
			if (took < kCloseAnimation) {
				running = true;
				continue;
			}
			log_line("animation close %" B_PRId64 " ms frames %" B_PRId32,
				took / 1000, ghost->frames);
			delete fGhosts.RemoveItemAt(i);
		}
		if (fSlideActive) {
			area.Include(BRect(0, 0, (float)fFrameWidth - 1,
				(float)fFrameHeight - 1));
			bigtime_t took = now - fSlideStart;
			if (took < kWorkspaceAnimation)
				running = true;
			else {
				fSlideActive = false;
				log_line("animation workspace %" B_PRId64 " ms frames %"
					B_PRId32, took / 1000, fSlideFrames);
			}
		}
	}
	fAnimationsRunning = running;
	Damage(area);
	return running;
}


//!	This frame's visibility of each opening window, the ghosts, the slide.
void
Compositor::_AnimationFrame(LayerList& layers, bigtime_t now)
{
	LayerList* ghosts = new(std::nothrow) LayerList(4);
	fFrameAnimating = false;
	fFrameSlide = false;
	fFrameSlideSnapshot = false;
	{
		BAutolock _(fAnimLock);
		for (int32 i = 0; i < layers.CountItems(); i++) {
			Layer* layer = layers.ItemAt(i);
			Entry* entry = layer->entry.Get();
			layer->visible = -1;
			if (!entry->animOpen)
				continue;
			float t = (float)(now - entry->animStart) / kOpenAnimation;
			layer->visible = entry->animFrom
				+ (1.0f - entry->animFrom) * ease_out(t);
			entry->animFrames++;
			fFrameAnimating = true;
		}
		for (int32 i = 0; ghosts != NULL && i < fGhosts.CountItems(); i++) {
			Ghost* ghost = fGhosts.ItemAt(i);
			float t = (float)(now - ghost->start) / kCloseAnimation;
			ghost->visible = ghost->from * (1.0f - ease_out(t));
			ghost->frames++;
			Layer* layer = new(std::nothrow) Layer;
			if (layer == NULL)
				continue;
			layer->entry = ghost->entry;
			layer->buffer = ghost->buffer;
			layer->left = ghost->left;
			layer->top = ghost->top;
			layer->shape = ghost->shape;
			layer->round = ghost->round;
			layer->visible = ghost->visible;
			if (layer->buffer.IsSet()) {
				layer->image.SetTo(layer->buffer->AcquireImage(
					&layer->imageX, &layer->imageY), true);
			}
			ghosts->AddItem(layer);
			fFrameAnimating = true;
		}
		if (fSlideActive) {
			float t = (float)(now - fSlideStart) / kWorkspaceAnimation;
			fFrameSlide = true;
			fFrameSlideSnapshot = fSlideSnapshot;
			fSlideSnapshot = false;
			fFrameSlideShift = (int32)(ease_out(t) * fFrameWidth);
			if (fFrameSlideShift >= (int32)fFrameWidth)
				fFrameSlideShift = fFrameWidth;
			fSlideFrames++;
			fFrameAnimating = true;
		}
	}
	delete fFrameGhosts;
	fFrameGhosts = ghosts;
}


/*!	A window scaled about its centre (95 % to 100 %) and faded (\a visible
	from 0 to 1), where the damage is. Only its footprint is drawn.
*/
void
Compositor::_RecordWindowAnimation(const Layer* layer,
	const BRegion& damage, uint64 frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	float visible = layer->visible;
	if (visible <= 0.0f || !layer->HasPixels())
		return;
	// M13 step 2: a window that Skia draws is read from its image, which
	// may be the one before a resize (another size and place)
	bool fromImage = layer->image.IsSet();
	uint64 buffer = fromImage ? frame : layer->buffer->VulkanBuffer();
	uint32 width = fromImage ? layer->image->Width() : layer->buffer->Width();
	uint32 height = fromImage
		? layer->image->Height() : layer->buffer->Height();
	int32 left = layer->SourceLeft();
	int32 top = layer->SourceTop();
	const BRegion& shape = layer->shape;
	BRegion part(damage);
	BRegion bounds(BRect((float)left, (float)top,
		(float)left + width - 1, (float)top + height - 1));
	part.IntersectWith(&bounds);
	if (part.CountRects() == 0)
		return;

	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, (VkBuffer)buffer, (VkBuffer)frame,
		(VkBuffer)frame,
		fromImage ? (VkImageView)layer->image->View() : VK_NULL_HANDLE);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectWindow | (fromImage ? kEffectFromImage : 0);
	effect.targetStride = frameStride;
	effect.sourceStride = width;
	effect.sourceWidth = width;
	effect.sourceHeight = height;
	effect.offset = kOpenAnimationScale
		+ (1.0f - kOpenAnimationScale) * visible;
	effect.amount = (uint32)(visible * 255.0f + 0.5f);
	if (effect.amount > 255)
		effect.amount = 255;
	// The footprint in buffer coordinates; more than four rectangles (a
	// stack of tabs) are drawn as their bounding box.
	for (int32 i = 0; i < 4; i++) {
		effect.shape[i * 4] = 1;
		effect.shape[i * 4 + 2] = 0;
	}
	int32 count = shape.CountRects();
	// A round window: its bounding box with its four corners' radii,
	// anti-aliased in the shader (review: one radius, stepped, before).
	int32 radii[4] = { 0, 0, 0, 0 };
	if (layer->round.IsRound()) {
		count = 5;
		effect.auxStride = 1;
		for (int32 c = 0; c < layer->round.cornerCount; c++) {
			const window_corner& corner = layer->round.corners[c];
			int32 kind = corner.kind;
			if (kind >= 0 && kind < 4 && corner.radius > radii[kind])
				radii[kind] = corner.radius;
		}
	}
	for (int32 i = 0; i < count && i < 4; i++) {
		clipping_rect r = count > 4 ? shape.FrameInt() : shape.RectAtInt(i);
		effect.shape[i * 4] = r.left - left;
		effect.shape[i * 4 + 1] = r.top - top;
		effect.shape[i * 4 + 2] = r.right - left;
		effect.shape[i * 4 + 3] = r.bottom - top;
		if (count > 4)
			break;
	}
	if (effect.auxStride != 0) {
		effect.shape[4] = radii[0];
		effect.shape[5] = radii[1];
		effect.shape[6] = radii[2];
		effect.shape[7] = radii[3];
	}
	for (int32 i = 0; i < part.CountRects(); i++) {
		clipping_rect r = part.RectAtInt(i);
		effect.width = r.right - r.left + 1;
		effect.height = r.bottom - r.top + 1;
		effect.targetX = r.left;
		effect.targetY = r.top;
		effect.auxX = r.left - left;
		effect.auxY = r.top - top;
		dispatch_effect(vk, effect);
	}
}


//!	The old and the new picture of the slide, each as large as the frame.
bool
Compositor::_EnsureSlideBuffers(uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	uint64 bytes = (uint64)frameStride * fFrameHeight * 4;
	if (vk->slideOld != VK_NULL_HANDLE && vk->slideBytes == bytes)
		return true;
	if (vk->slideOld != VK_NULL_HANDLE) {
		// between two composes the GPU is idle
		vk->vkDestroyBuffer(vk->device, vk->slideOld, NULL);
		vk->vkFreeMemory(vk->device, vk->slideOldMemory, NULL);
		vk->vkDestroyBuffer(vk->device, vk->slideNew, NULL);
		vk->vkFreeMemory(vk->device, vk->slideNewMemory, NULL);
		vk->slideOld = VK_NULL_HANDLE;
		vk->slideNew = VK_NULL_HANDLE;
		vk->slideBytes = 0;
	}
	void* bits;
	if (!compositor_allocate_host_buffer(vk, bytes, &vk->slideOld, &vk->slideOldMemory,
			&bits))
		return false;
	if (!compositor_allocate_host_buffer(vk, bytes, &vk->slideNew, &vk->slideNewMemory,
			&bits)) {
		vk->vkDestroyBuffer(vk->device, vk->slideOld, NULL);
		vk->vkFreeMemory(vk->device, vk->slideOldMemory, NULL);
		vk->slideOld = VK_NULL_HANDLE;
		return false;
	}
	vk->slideBytes = bytes;
	return true;
}


//!	The whole frame into a slide buffer, with the copy shader.
void
Compositor::_RecordSlideCopy(uint64 from, uint64 to, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->pipeline);
	push_compose_buffers(vk, (VkBuffer)from, (VkBuffer)to);
	compose_rect rect;
	memset(&rect, 0, sizeof(rect));
	rect.sourceStride = frameStride;
	rect.frameStride = frameStride;
	rect.width = fFrameWidth;
	rect.height = fFrameHeight;
	vk->vkCmdPushConstants(vk->commands, vk->pipelineLayout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rect), &rect);
	vk->vkCmdDispatch(vk->commands, (rect.width + 15) / 16,
		(rect.height + 15) / 16, 1);
	effect_barrier(vk);
}


//!	The frame from the old picture and the new one, side by side.
void
Compositor::_RecordSlide(uint64 frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, vk->slideOld, (VkBuffer)frame, vk->slideNew);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectSlide;
	effect.width = fFrameWidth;
	effect.height = fFrameHeight;
	effect.targetStride = frameStride;
	effect.amount = fFrameSlideShift;
	effect.offset = fSlideDirection > 0 ? 1.0f : -1.0f;
	dispatch_effect(vk, effect);
	effect_barrier(vk);
}

