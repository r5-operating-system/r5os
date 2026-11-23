/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Compositor window effects: blurred shadows, frosted glass, resize outline,
 * and anti-aliased round corners.
 */


#include "CompositorPrivate.h"


//	#pragma mark - effects (M11 step 4, decision 16)


//!	One box blur of radius \a radius along rows or columns, zero outside.
static void
box_blur(uint8* bits, int32 width, int32 height, int32 radius, bool rows,
	uint8* line)
{
	int32 count = rows ? height : width;
	int32 length = rows ? width : height;
	int32 step = rows ? 1 : width;
	int32 divisor = 2 * radius + 1;
	for (int32 n = 0; n < count; n++) {
		uint8* start = rows ? bits + n * width : bits + n;
		for (int32 i = 0; i < length; i++)
			line[i] = start[i * step];
		int32 sum = 0;
		for (int32 i = 0; i <= radius && i < length; i++)
			sum += line[i];
		for (int32 i = 0; i < length; i++) {
			start[i * step] = (uint8)(sum / divisor);
			int32 add = i + radius + 1;
			int32 drop = i - radius;
			if (add < length)
				sum += line[add];
			if (drop >= 0)
				sum -= line[drop];
		}
	}
}


/*!	The shadow mask of a layer's window: its footprint, blurred three
	times along the rows and three times along the columns. Made again only
	when the footprint, the focus or the step changes (decision 16).
*/
bool
Compositor::_EnsureShadowMask(Layer* layer)
{
	Entry* entry = layer->entry.Get();
	int32 strength = fStrength;
	BRegion local(layer->shape);
	local.OffsetBy(-layer->left, -layer->top);
	bool same = entry->mask.IsSet() && entry->mask->Generation() == fGeneration
		&& entry->maskSmall == layer->shadowSmall
		&& entry->maskStrength == strength;
	bool changed = !same || !(entry->maskShape == local);
	// M13 step 4: a footprint that changes again within kShadowSettle of its
	// last change (a resize) gets the mask before it, stretched; it is
	// blurred again once the footprint stayed the same that long.
	bigtime_t now = system_time();
	if (same && !changed) {
		if (!entry->maskStretched || now - entry->maskChanged < kShadowSettle)
			return true;
	} else if (same && now - entry->maskChanged < kShadowSettle
		&& _StretchShadowMask(layer, local)) {
		entry->maskChanged = now;
		return true;
	}

	window_shadow_geometry shadow = window_shadow_for(strength,
		layer->shadowSmall);
	clipping_rect frame = local.FrameInt();
	int32 width = frame.right + 1 + 2 * shadow.reach;
	int32 height = frame.bottom + 1 + 2 * shadow.reach;
	uint64 bytes = (uint64)width * height;
	uint8* line = (uint8*)malloc(max_c(width, height));
	WindowBuffer* buffer = line != NULL
		? _AllocateBuffer((uint32)((bytes + 3) / 4), 1) : NULL;
	if (buffer == NULL) {
		free(line);
		entry->mask.Unset();
		entry->maskStretched = false;
		if (!fEffectsLogged) {
			fEffectsLogged = true;
			log_line("shadow skipped: window memory");
		}
		return false;
	}
	uint8* bits = (uint8*)buffer->Bits();
	memset(bits, 0, buffer->Bytes());
	for (int32 i = 0; i < local.CountRects(); i++) {
		clipping_rect r = local.RectAtInt(i);
		for (int32 y = r.top; y <= r.bottom; y++) {
			memset(bits + (y + shadow.reach) * width + r.left + shadow.reach,
				255, r.right - r.left + 1);
		}
	}
	for (int32 pass = 0; pass < 3; pass++)
		box_blur(bits, width, height, shadow.radius, true, line);
	for (int32 pass = 0; pass < 3; pass++)
		box_blur(bits, width, height, shadow.radius, false, line);
	free(line);

	entry->mask.SetTo(buffer, true);
	entry->maskWidth = width;
	entry->maskHeight = height;
	entry->maskReach = shadow.reach;
	entry->maskSmall = layer->shadowSmall;
	entry->maskStrength = strength;
	entry->maskShape = local;
	// M13 step 4
	entry->maskStretched = false;
	if (changed)
		entry->maskChanged = now;
	return true;
}


/*!	M13 step 4: the mask for \a local made from the window's mask before it,
	without a blur: the right and bottom 2 * reach of it (the blur around
	those edges) move with the new size, the rest stays where it was, and a
	larger window repeats the last column and row before them, which lie
	inside the old window, away from its edges. Right for a rectangle; for
	the tab and a small window close enough until the blur is made again.
*/
bool
Compositor::_StretchShadowMask(Layer* layer, const BRegion& local)
{
	Entry* entry = layer->entry.Get();
	window_shadow_geometry shadow = window_shadow_for(entry->maskStrength,
		entry->maskSmall);
	if (local.CountRects() == 0 || shadow.reach != entry->maskReach
		|| entry->mask->Bits() == NULL)
		return false;
	clipping_rect frame = local.FrameInt();
	if (frame.left < 0 || frame.top < 0)
		return false;
	int32 band = 2 * shadow.reach;
	int32 width = frame.right + 1 + band;
	int32 height = frame.bottom + 1 + band;
	int32 oldWidth = entry->maskWidth;
	int32 oldHeight = entry->maskHeight;
	if (oldWidth <= band || oldHeight <= band)
		return false;
	uint64 bytes = (uint64)width * height;

	// The GPU is idle between two composes, so a mask large enough is
	// written again in place, from a copy; a larger one gets room to grow.
	BReference<WindowBuffer> target = entry->mask;
	const uint8* source = (const uint8*)entry->mask->Bits();
	uint8* copy = NULL;
	if (bytes <= target->Bytes()) {
		uint64 oldBytes = (uint64)oldWidth * oldHeight;
		copy = (uint8*)malloc(oldBytes);
		if (copy == NULL)
			return false;
		memcpy(copy, source, oldBytes);
		source = copy;
	} else {
		uint64 room = bytes + bytes / 4;
		if (room > UINT32_MAX)
			return false;
		WindowBuffer* buffer = _AllocateBuffer((uint32)((room + 3) / 4), 1);
		if (buffer == NULL)
			return false;
		target.SetTo(buffer, true);
	}
	uint8* bits = (uint8*)target->Bits();

	int32 keepX = oldWidth - band;
	int32 keepY = oldHeight - band;
	int32 left = width - band;
	int32 direct = min_c(left, keepX);
	for (int32 y = 0; y < height; y++) {
		int32 sourceY = y >= height - band
			? y - (height - oldHeight) : min_c(y, keepY - 1);
		const uint8* from = source + (uint64)sourceY * oldWidth;
		uint8* to = bits + (uint64)y * width;
		memcpy(to, from, direct);
		if (left > direct)
			memset(to + direct, from[keepX - 1], left - direct);
		memcpy(to + left, from + keepX, band);
	}
	free(copy);

	entry->mask = target;
	entry->maskWidth = width;
	entry->maskHeight = height;
	entry->maskShape = local;
	entry->maskStretched = true;
	return true;
}


/*!	M13 step 4: once a stretched mask's window stayed the same for
	kShadowSettle, its shadow is damaged, so the next frame blurs the mask
	again even when nothing else changes. Says whether it damaged one.
*/
bool
Compositor::_ShadowSettleDamage(bigtime_t now)
{
	if (fShadowDue == 0 || now < fShadowDue)
		return false;
	fShadowDue = 0;
	BRegion area;
	{
		BAutolock _(fLayerLock);
		for (int32 i = 0; i < fLayers->CountItems(); i++) {
			Layer* source = fLayers->ItemAt(i);
			Entry* entry = source->entry.Get();
			if (entry != NULL && source->shadow && entry->mask.IsSet()
				&& entry->maskStretched
				&& now - entry->maskChanged >= kShadowSettle)
				area.Include(&source->shadowArea);
		}
	}
	if (area.CountRects() == 0)
		return false;
	Damage(area);
	return true;
}


//!	A kept blur buffer as large as the glass of the window.
bool
Compositor::_EnsureGlassCache(Layer* layer, const BRegion& glass)
{
	Entry* entry = layer->entry.Get();
	uint64 pixels = 0;
	for (int32 i = 0; i < glass.CountRects(); i++) {
		clipping_rect r = glass.RectAtInt(i);
		pixels += (uint64)(r.right - r.left + 1) * (r.bottom - r.top + 1);
	}
	if (pixels == 0 || pixels > UINT32_MAX)
		return false;
	if (!entry->glass.IsSet() || entry->glass->Generation() != fGeneration
		|| entry->glass->Width() < pixels) {
		entry->glass.Unset();
		WindowBuffer* buffer = _AllocateBuffer((uint32)pixels, 1);
		if (buffer == NULL) {
			entry->glassValid = false;
			if (!fEffectsLogged) {
				fEffectsLogged = true;
				log_line("frosted skipped: window memory");
			}
			return false;
		}
		entry->glass.SetTo(buffer, true);
	}
	entry->glassShape = glass;
	entry->glassLeft = layer->left;
	entry->glassTop = layer->top;
	entry->glassStrength = fStrength;
	entry->glassBelow = layer->belowHash;
	entry->glassValid = true;
	return true;
}


//!	The levels of the Dual Kawase blur, for a frame of the current size.
bool
Compositor::_EnsureScratch()
{
	CompositorVulkan* vk = fVulkan;
	uint64 bytes = 0;
	for (int32 k = 1; k <= 4; k++) {
		bytes += 2 * (uint64)((fFrameWidth >> k) + 2)
			* ((fFrameHeight >> k) + 2) * 4;
	}
	if (vk->scratch != VK_NULL_HANDLE && vk->scratchBytes >= bytes)
		return true;
	if (vk->scratch != VK_NULL_HANDLE) {
		// between two composes the GPU is idle
		vk->vkDestroyBuffer(vk->device, vk->scratch, NULL);
		vk->vkFreeMemory(vk->device, vk->scratchMemory, NULL);
		vk->scratch = VK_NULL_HANDLE;
		vk->scratchMemory = VK_NULL_HANDLE;
		vk->scratchBytes = 0;
	}
	void* bits;
	if (!compositor_allocate_host_buffer(vk, bytes, &vk->scratch, &vk->scratchMemory,
			&bits))
		return false;
	vk->scratchBytes = bytes;
	return true;
}


/*!	Before the compose: the shadow masks, the kept blur of each glass
	window, and the damage grown so the blur gets fresh input. The blur of
	a window is made again only where something below it changed (grown by
	the blur's reach), or all of it when the window moved, changed its
	glass or the windows below it, or the step changed.
*/
bool
Compositor::_PrepareEffects(BRegion& damage, const LayerList& layers,
	const BRegion& generic)
{
	BRegion screen(BRect(0, 0, (float)fFrameWidth - 1,
		(float)fFrameHeight - 1));
	int32 strength = fStrength;
	window_glass_geometry glassGeometry = window_glass_for(strength);

	// M13 (owner, 17 Nov 2026): the blur is made only for the glass that
	// shows; what the windows above cover with their plain pixels is left
	// out. The kept blur there is made again when it shows, because that
	// is damage below the glass.
	std::vector<BRegion> hidden(layers.CountItems());
	{
		BRegion over;
		for (int32 i = layers.CountItems() - 1; i >= 0; i--) {
			hidden[i] = over;
			Layer* upper = layers.ItemAt(i);
			if (!upper->HasPixels() || upper->visible >= 0)
				continue;
			BRegion plain(upper->shape);
			BRegion pixels(upper->PixelRect());
			plain.IntersectWith(&pixels);
			plain.Exclude(&upper->glassFixed);
			plain.Exclude(&upper->glassAlpha);
			over.Include(&plain);
		}
	}
	bool scratch = _EnsureScratch();
	// M13 step 4: set again below for each mask still stretched
	fShadowDue = 0;

	BRegion below(generic);
	uint32 belowHash = 0;
	BRegion grown;
	for (int32 i = 0; i < layers.CountItems(); i++) {
		Layer* layer = layers.ItemAt(i);
		Entry* entry = layer->entry.Get();
		layer->belowHash = belowHash;
		layer->blurRegion.MakeEmpty();

		if (layer->shadow && !_EnsureShadowMask(layer))
			layer->shadow = false;
		// M13 step 4: when the stretched mask is blurred again
		if (layer->shadow && entry->maskStretched) {
			bigtime_t due = entry->maskChanged + kShadowSettle;
			if (fShadowDue == 0 || due < fShadowDue)
				fShadowDue = due;
		}

		BRegion glass(layer->glassFixed);
		glass.Include(&layer->glassAlpha);
		glass.IntersectWith(&screen);
		if (glass.CountRects() > 0 && layer->buffer.IsSet() && scratch) {
			bool valid = entry->glassValid && entry->glass.IsSet()
				&& entry->glass->Generation() == fGeneration
				&& entry->glassShape == glass
				&& entry->glassLeft == layer->left
				&& entry->glassTop == layer->top
				&& entry->glassStrength == strength
				&& entry->glassBelow == belowHash;
			if (!valid) {
				if (_EnsureGlassCache(layer, glass))
					layer->blurRegion = glass;
				else {
					layer->glassFixed.MakeEmpty();
					layer->glassAlpha.MakeEmpty();
				}
				entry->glassStale.MakeEmpty();
			} else {
				layer->blurRegion = grow_region(below, glassGeometry.reach);
				layer->blurRegion.Include(&entry->glassStale);
				layer->blurRegion.IntersectWith(&glass);
			}
			BRegion skipped(layer->blurRegion);
			skipped.IntersectWith(&hidden[i]);
			layer->blurRegion.Exclude(&hidden[i]);
			entry->glassStale.Include(&skipped);
			entry->glassStale.Exclude(&layer->blurRegion);
			// The blur is made per glass rectangle, for the box around
			// what is new in it; its input must be composed fresh.
			for (int32 g = 0; g < glass.CountRects()
					&& layer->blurRegion.CountRects() > 0; g++) {
				BRegion part(layer->blurRegion);
				BRegion rectangle;
				rectangle.Set(glass.RectAtInt(g));
				part.IntersectWith(&rectangle);
				if (part.CountRects() == 0)
					continue;
				BRegion box(part.Frame());
				BRegion input = grow_region(box, glassGeometry.reach);
				grown.Include(&input);
			}
		} else if (glass.CountRects() > 0) {
			layer->glassFixed.MakeEmpty();
			layer->glassAlpha.MakeEmpty();
		}

		below.Include(&layer->contentDamage);
		belowHash = belowHash * 31 + (uint32)((addr_t)entry >> 4);
	}
	damage.Include(&grown);
	damage.IntersectWith(&screen);
	return true;
}



/*!	Dual Kawase over what is composed below the window (the frame now),
	into the window's kept blur: per glass rectangle, the box around what is
	new in it, down the step's levels and up again (a quarter of the
	resolution with two levels).
*/
void
Compositor::_RecordBlur(Layer* layer, uint64 frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	Entry* entry = layer->entry.Get();
	window_glass_geometry geometry = window_glass_for(entry->glassStrength);
	VkBuffer frameBuffer = (VkBuffer)frame;
	VkBuffer cache = (VkBuffer)entry->glass->VulkanBuffer();
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);

	uint32 cacheOffset = 0;
	const BRegion& glass = entry->glassShape;
	for (int32 g = 0; g < glass.CountRects(); g++) {
		clipping_rect area = glass.RectAtInt(g);
		uint32 areaWidth = area.right - area.left + 1;
		uint32 areaPixels = areaWidth * (area.bottom - area.top + 1);
		BRegion part(layer->blurRegion);
		BRegion rectangle;
		rectangle.Set(area);
		part.IntersectWith(&rectangle);
		if (part.CountRects() == 0) {
			cacheOffset += areaPixels;
			continue;
		}
		clipping_rect box = part.FrameInt();
		clipping_rect input;
		input.left = max_c(box.left - geometry.reach, 0);
		input.top = max_c(box.top - geometry.reach, 0);
		input.right = min_c(box.right + geometry.reach,
			(int32)fFrameWidth - 1);
		input.bottom = min_c(box.bottom + geometry.reach,
			(int32)fFrameHeight - 1);

		uint32 width[5];
		uint32 height[5];
		uint32 down[5];
		uint32 up[5];
		width[0] = input.right - input.left + 1;
		height[0] = input.bottom - input.top + 1;
		uint32 next = 0;
		for (int32 k = 1; k <= geometry.levels; k++) {
			width[k] = (width[k - 1] + 1) / 2;
			height[k] = (height[k - 1] + 1) / 2;
			down[k] = next;
			next += width[k] * height[k];
			up[k] = next;
			next += width[k] * height[k];
		}

		effect_rect effect;
		memset(&effect, 0, sizeof(effect));
		effect.offset = geometry.offset;
		for (int32 k = 1; k <= geometry.levels; k++) {
			effect.mode = kEffectDown;
			effect.width = width[k];
			effect.height = height[k];
			effect.targetX = down[k];
			effect.targetY = 0;
			effect.targetStride = width[k];
			if (k == 1) {
				effect.sourceX = input.left;
				effect.sourceY = input.top;
				effect.sourceStride = frameStride;
				push_effect_buffers(vk, frameBuffer, vk->scratch, frameBuffer);
			} else {
				effect.sourceX = down[k - 1];
				effect.sourceY = 0;
				effect.sourceStride = width[k - 1];
				push_effect_buffers(vk, vk->scratch, vk->scratch, frameBuffer);
			}
			effect.sourceWidth = width[k - 1];
			effect.sourceHeight = height[k - 1];
			effect.auxX = 0;
			effect.auxY = 0;
			dispatch_effect(vk, effect);
			effect_barrier(vk);
		}
		for (int32 k = geometry.levels; k >= 1; k--) {
			effect.mode = kEffectUp;
			effect.sourceX = k == geometry.levels ? down[k] : up[k];
			effect.sourceY = 0;
			effect.sourceStride = width[k];
			effect.sourceWidth = width[k];
			effect.sourceHeight = height[k];
			if (k > 1) {
				effect.width = width[k - 1];
				effect.height = height[k - 1];
				effect.targetX = up[k - 1];
				effect.targetY = 0;
				effect.targetStride = width[k - 1];
				effect.auxX = 0;
				effect.auxY = 0;
				push_effect_buffers(vk, vk->scratch, vk->scratch, frameBuffer);
			} else {
				// the last level writes only the box, into the kept blur
				effect.width = box.right - box.left + 1;
				effect.height = box.bottom - box.top + 1;
				effect.targetX = cacheOffset + (box.left - area.left);
				effect.targetY = box.top - area.top;
				effect.targetStride = areaWidth;
				effect.auxX = box.left - input.left;
				effect.auxY = box.top - input.top;
				push_effect_buffers(vk, vk->scratch, cache, frameBuffer);
			}
			dispatch_effect(vk, effect);
			effect_barrier(vk);
		}
		cacheOffset += areaPixels;
	}
}


//!	The shadow under the window, where the damage touches it.
void
Compositor::_RecordShadow(Layer* layer, const BRegion& damage, uint64 frame,
	uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	Entry* entry = layer->entry.Get();
	BRegion part(damage);
	part.IntersectWith(&layer->shadowArea);
	if (part.CountRects() == 0 || !entry->mask.IsSet())
		return;
	window_shadow_geometry shadow = window_shadow_for(entry->maskStrength,
		entry->maskSmall);
	// Deskbar styles: a window against a screen edge, no offset
	if (layer->shadowFlat)
		shadow.offset = 0;
	int32 maskLeft = layer->left - shadow.reach;
	int32 maskTop = layer->top - shadow.reach + shadow.offset;

	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, (VkBuffer)entry->mask->VulkanBuffer(),
		(VkBuffer)frame, (VkBuffer)frame);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectShadow;
	effect.targetStride = frameStride;
	effect.sourceStride = entry->maskWidth;
	effect.amount = shadow.strength;
	for (int32 i = 0; i < part.CountRects(); i++) {
		clipping_rect r = part.RectAtInt(i);
		// the mask covers the whole shadow area by construction
		if (r.left < maskLeft || r.top < maskTop
			|| r.right >= maskLeft + entry->maskWidth
			|| r.bottom >= maskTop + entry->maskHeight)
			continue;
		effect.width = r.right - r.left + 1;
		effect.height = r.bottom - r.top + 1;
		effect.targetX = r.left;
		effect.targetY = r.top;
		effect.sourceX = r.left - maskLeft;
		effect.sourceY = r.top - maskTop;
		dispatch_effect(vk, effect);
	}
	effect_barrier(vk);
}


/*!	M13 step 4 (owner, 17 Nov 2026): the outline of a resize on Lavapipe,
	where the damage is. The shadow pass with a mask of one row of 255 and
	a row stride of 0 darkens the outline's pixels; no shader of its own.
	False when nothing was recorded (the caller binds the compose pipeline
	again otherwise).
*/
bool
Compositor::_RecordOutline(Layer* layer, const BRegion& damage, uint64 frame,
	uint32 frameStride)
{
	// one row of the mask; wider than any screen this compositor runs on
	static const uint32 kOutlineMaskWidth = 16384;
	// how dark: 160 of 255, about the shadow's darkest
	static const uint32 kOutlineAmount = 160;

	CompositorVulkan* vk = fVulkan;
	BRegion part(damage);
	part.IntersectWith(&layer->outline);
	if (part.CountRects() == 0)
		return false;
	if (!fOutlineMask.IsSet() || fOutlineMask->Generation() != fGeneration) {
		fOutlineMask.Unset();
		WindowBuffer* mask = _AllocateBuffer(kOutlineMaskWidth / 4, 1, true);
		if (mask == NULL)
			return false;
		memset(mask->Bits(), 255, mask->Bytes());
		fOutlineMask.SetTo(mask, true);
	}

	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, (VkBuffer)fOutlineMask->VulkanBuffer(),
		(VkBuffer)frame, (VkBuffer)frame);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectShadow;
	effect.targetStride = frameStride;
	effect.sourceStride = 0;
	effect.amount = kOutlineAmount;
	for (int32 i = 0; i < part.CountRects(); i++) {
		clipping_rect r = part.RectAtInt(i);
		if (r.right - r.left + 1 > (int32)kOutlineMaskWidth)
			r.right = r.left + kOutlineMaskWidth - 1;
		effect.width = r.right - r.left + 1;
		effect.height = r.bottom - r.top + 1;
		effect.targetX = r.left;
		effect.targetY = r.top;
		dispatch_effect(vk, effect);
	}
	effect_barrier(vk);
	return true;
}


//!	The window's glass pixels over its kept blur, where the damage is.
void
Compositor::_RecordGlass(Layer* layer, const BRegion& damage, uint64 frame,
	uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	Entry* entry = layer->entry.Get();
	if (!entry->glass.IsSet())
		return;
	window_glass_geometry geometry = window_glass_for(entry->glassStrength);
	// Dark glass (a theme's dark colours) shows half the blur through the
	// view colour: over a light wallpaper the dark surface stays a deep
	// slate instead of a flat grey (owner's review of the dark Deskbar,
	// 23 Nov 2026; mockup B1). The glass of pixels with their own alpha is
	// unchanged.
	int32 fixedSeeThrough = geometry.seeThrough;
	if (atomic_get(&fDarkGlass) != 0)
		fixedSeeThrough /= 2;
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	// M13 step 2: a window that Skia draws is read from its image
	bool fromImage = layer->image.IsSet();
	push_effect_buffers(vk, fromImage
			? (VkBuffer)frame : (VkBuffer)layer->buffer->VulkanBuffer(),
		(VkBuffer)frame, (VkBuffer)entry->glass->VulkanBuffer(),
		fromImage ? (VkImageView)layer->image->View() : VK_NULL_HANDLE);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectGlass | (fromImage ? kEffectFromImage : 0);
	effect.targetStride = frameStride;
	effect.sourceStride = layer->buffer->Width();
	BRect pixelRect = layer->PixelRect();
	effect.sourceWidth = pixelRect.IntegerWidth() + 1;
	effect.sourceHeight = pixelRect.IntegerHeight() + 1;
	BRegion pixels(pixelRect);

	uint32 cacheOffset = 0;
	const BRegion& glass = entry->glassShape;
	for (int32 g = 0; g < glass.CountRects(); g++) {
		clipping_rect area = glass.RectAtInt(g);
		uint32 areaWidth = area.right - area.left + 1;
		for (int32 kind = 0; kind < 2; kind++) {
			BRegion part(damage);
			BRegion rectangle;
			rectangle.Set(area);
			part.IntersectWith(&rectangle);
			if (kind == 0)
				part.IntersectWith(&layer->glassFixed);
			else
				part.IntersectWith(&layer->glassAlpha);
			part.IntersectWith(&pixels);
			effect.amount = (kind == 0 ? fixedSeeThrough
					: geometry.seeThrough)
				| (kind == 0 ? kEffectFixedGlass : 0);
			effect.auxStride = areaWidth;
			// The round outline: glass in a corner's square is drawn with
			// that corner (index c), the rest plain (c = -1).
			BRegion rest(part);
			for (int32 c = -1; c < layer->round.cornerCount; c++) {
				BRegion here;
				if (c < 0) {
					here = rest;
					here.Exclude(&layer->round.area);
					effect.shape[3] = 0;
				} else {
					BRegion square;
					square.Set(layer->round.corners[c].square);
					here = rest;
					here.IntersectWith(&square);
					set_corner(effect, layer->round.corners[c],
						layer->round);
				}
				rest.Exclude(&here);
				for (int32 i = 0; i < here.CountRects(); i++) {
					clipping_rect r = here.RectAtInt(i);
					effect.width = r.right - r.left + 1;
					effect.height = r.bottom - r.top + 1;
					effect.targetX = r.left;
					effect.targetY = r.top;
					effect.sourceX = r.left - layer->SourceLeft();
					effect.sourceY = r.top - layer->SourceTop();
					effect.auxX = cacheOffset + (r.left - area.left);
					effect.auxY = r.top - area.top;
					dispatch_effect(vk, effect);
				}
			}
		}
		cacheOffset += areaWidth * (area.bottom - area.top + 1);
	}
}


/*!	The round outline: \a part of the pixels in the corners' squares, drawn
	with the window's edge line carried round the arc and blended over what
	is composed below by their coverage (mode 6, the formula of
	WindowOutline.h). Every such pixel lies in one corner's square; the
	shader gets that corner.
*/
void
Compositor::_RecordCorners(Layer* layer, const BRegion& part, uint64 frame,
	uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	if (!layer->HasPixels() || !layer->round.IsRound())
		return;
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	bool fromImage = layer->image.IsSet();
	push_effect_buffers(vk, fromImage
			? (VkBuffer)frame : (VkBuffer)layer->buffer->VulkanBuffer(),
		(VkBuffer)frame, (VkBuffer)frame,
		fromImage ? (VkImageView)layer->image->View() : VK_NULL_HANDLE);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectCorner | (fromImage ? kEffectFromImage : 0);
	effect.targetStride = frameStride;
	effect.sourceStride = layer->buffer->Width();
	BRect pixelRect = layer->PixelRect();
	effect.sourceWidth = pixelRect.IntegerWidth() + 1;
	effect.sourceHeight = pixelRect.IntegerHeight() + 1;
	BRegion pixels(pixelRect);
	BRegion rest(part);
	rest.IntersectWith(&pixels);

	for (int32 c = 0; c < layer->round.cornerCount && rest.CountRects() > 0;
			c++) {
		const window_corner& corner = layer->round.corners[c];
		BRegion square;
		square.Set(corner.square);
		BRegion here(rest);
		here.IntersectWith(&square);
		rest.Exclude(&here);
		set_corner(effect, corner, layer->round);
		for (int32 i = 0; i < here.CountRects(); i++) {
			clipping_rect r = here.RectAtInt(i);
			effect.width = r.right - r.left + 1;
			effect.height = r.bottom - r.top + 1;
			effect.targetX = r.left;
			effect.targetY = r.top;
			effect.sourceX = r.left - layer->SourceLeft();
			effect.sourceY = r.top - layer->SourceTop();
			dispatch_effect(vk, effect);
		}
	}
}


/*!	The round outline for a zero-copy Vulkan view that reaches into a
	corner's square: blends the shared view's buffer with the corner's
	coverage and the decorator's edge line (mode 6), matching _RecordCorners().
*/
void
Compositor::_RecordShareCorners(const Layer* layer, const ShareDraw* draw,
	const BRegion& part, uint64 frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	if (!layer->round.IsRound() || part.CountRects() == 0)
		return;
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, draw->buffer, (VkBuffer)frame, (VkBuffer)frame);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectCorner;
	effect.targetStride = frameStride;
	effect.sourceStride = draw->pitch / 4;
	effect.sourceWidth = draw->width;
	effect.sourceHeight = draw->height;
	BRegion rest(part);

	for (int32 c = 0; c < layer->round.cornerCount && rest.CountRects() > 0;
			c++) {
		const window_corner& corner = layer->round.corners[c];
		BRegion square;
		square.Set(corner.square);
		BRegion here(rest);
		here.IntersectWith(&square);
		rest.Exclude(&here);
		set_corner(effect, corner, layer->round);
		for (int32 i = 0; i < here.CountRects(); i++) {
			clipping_rect r = here.RectAtInt(i);
			effect.width = r.right - r.left + 1;
			effect.height = r.bottom - r.top + 1;
			effect.targetX = r.left;
			effect.targetY = r.top;
			effect.sourceX = r.left - draw->left;
			effect.sourceY = r.top - draw->top;
			dispatch_effect(vk, effect);
		}
	}
}


//!	After a restart or the switch to the CPU mode: no mask, no kept blur.
void
Compositor::_DropEffects()
{
	BAutolock _(fEntryLock);
	// M13 step 4: before the device goes, as the masks below
	fOutlineMask.Unset();
	_EndSplash();
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		entry->mask.Unset();
		entry->glass.Unset();
		entry->glassValid = false;
	}
	BAutolock animLock(fAnimLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++)
		fEntries.ItemAt(i)->animOpen = false;
	fGhosts.MakeEmpty();
	fSlideActive = false;
	fAnimationsRunning = false;
	delete fFrameGhosts;
	fFrameGhosts = NULL;
}

