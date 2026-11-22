/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Internal declarations shared across the Compositor implementation files.
 */
#ifndef COMPOSITOR_PRIVATE_H
#define COMPOSITOR_PRIVATE_H


#include "Compositor.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <new>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <Autolock.h>
#include <FindDirectory.h>

#include <r5_gpu.h>
#include <driver_settings.h>
#include <safemode_defs.h>
#include <strings.h>
#include <syscalls.h>
#include <r5_share.h>
#include <TokenSpace.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "Desktop.h"
#include "DrawingEngine.h"
#include "AccelerantHWInterface.h"
#include "GPUCopy.h"
#include "HWInterface.h"
#include "RenderingBuffer.h"
#include "ServerApp.h"
#include "ServerWindow.h"
#include "View.h"
#include "Window.h"
#include "WindowBuffer.h"
#include "WindowHWInterface.h"
#include "WindowEffectLevels.h"
#include "WindowOutline.h"
#include "decorator/Decorator.h"

#include <WindowPrivate.h>

#include "compose_period.h"

// M12 steps 2-5: the GPU draws the windows (gpu_draw true)
#include "SkiaDrawingEngine.h"
#include "SkiaGpu.h"
#include "SkiaOffscreen.h"


static const bigtime_t kFenceSlice = 100000;
static const bigtime_t kFenceLimit = 15000000;
// M11 review: after a fence timeout, the last wait before memory is freed
static const uint64 kFenceGraceNs = 5000000000ULL;
static const uint64 kFenceStepNs = 100000000ULL;
static const bigtime_t kReportInterval = 10000000;
// M13 step 0: a frame within this time of the one before counts as motion
// (dragging, scrolling, resizing), and damage within this time of a mouse
// move counts as its answer.
static const bigtime_t kFrameGap = 400000;
static const bigtime_t kInputWindow = 400000;
// M13 step 4: while a window's footprint keeps changing (a resize), its
// shadow mask is stretched from the one before; it is blurred again once the
// footprint stayed the same for this long.
static const bigtime_t kShadowSettle = 100000;


static inline void
log_line(const char* format, ...)
{
	char line[256];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	fprintf(stderr, "app_server: %s\n", line);
	syslog(LOG_INFO, "app_server: %s\n", line);
}


//	#pragma mark - Vulkan


#define VK_INSTANCE_FUNCTIONS(F) \
	F(vkDestroyInstance) \
	F(vkEnumeratePhysicalDevices) \
	F(vkGetPhysicalDeviceProperties) \
	F(vkGetPhysicalDeviceQueueFamilyProperties) \
	F(vkGetPhysicalDeviceMemoryProperties) \
	F(vkEnumerateDeviceExtensionProperties) \
	F(vkCreateDevice) \
	F(vkGetDeviceProcAddr)

#define VK_DEVICE_FUNCTIONS(F) \
	F(vkDestroyDevice) \
	F(vkGetDeviceQueue) \
	F(vkCreateBuffer) \
	F(vkDestroyBuffer) \
	F(vkGetBufferMemoryRequirements) \
	F(vkAllocateMemory) \
	F(vkFreeMemory) \
	F(vkBindBufferMemory) \
	F(vkMapMemory) \
	F(vkCreateShaderModule) \
	F(vkDestroyShaderModule) \
	F(vkCreateDescriptorSetLayout) \
	F(vkDestroyDescriptorSetLayout) \
	F(vkCreatePipelineLayout) \
	F(vkDestroyPipelineLayout) \
	F(vkCreateComputePipelines) \
	F(vkDestroyPipeline) \
	F(vkCreateCommandPool) \
	F(vkDestroyCommandPool) \
	F(vkAllocateCommandBuffers) \
	F(vkBeginCommandBuffer) \
	F(vkEndCommandBuffer) \
	F(vkResetCommandBuffer) \
	F(vkCmdBindPipeline) \
	F(vkCmdPushConstants) \
	F(vkCmdPushDescriptorSetKHR) \
	F(vkCmdDispatch) \
	F(vkCmdPipelineBarrier) \
	F(vkCreateFence) \
	F(vkDestroyFence) \
	F(vkResetFences) \
	F(vkWaitForFences) \
	F(vkQueueSubmit) \
	F(vkDeviceWaitIdle) \
	F(vkCmdCopyBuffer) \
	F(vkCreateImage) \
	F(vkDestroyImage) \
	F(vkGetImageMemoryRequirements) \
	F(vkBindImageMemory) \
	F(vkCreateImageView) \
	F(vkDestroyImageView) \
	F(vkCreateSampler) \
	F(vkDestroySampler) \
	F(vkCmdCopyImageToBuffer) \
	F(vkCreateQueryPool) \
	F(vkDestroyQueryPool) \
	F(vkCmdResetQueryPool) \
	F(vkCmdWriteTimestamp) \
	F(vkGetQueryPoolResults)

#define VK_DECLARE(name) PFN_##name name;

// M11 step 3b: the R5 DRM library that RADV loaded (r5_translate.h, MIT).
typedef int (*r5_token_to_fd_hook)(int, const uint8_t[16], int32_t, int*,
	uint32_t*);
typedef int (*r5_mark_get_hook)(uint32_t, uint64_t*);
typedef int (*r5_import_size_hook)(uint32_t, uint64_t*);


struct CompositorVulkan {
	void*						library;
	PFN_vkGetInstanceProcAddr	vkGetInstanceProcAddr;
	PFN_vkCreateInstance		vkCreateInstance;
	VK_INSTANCE_FUNCTIONS(VK_DECLARE)
	VK_DEVICE_FUNCTIONS(VK_DECLARE)

	VkInstance					instance;
	VkPhysicalDevice			physical;
	VkDevice					device;
	VkQueue						queue;
	uint32						memoryType;
	VkDescriptorSetLayout		setLayout;
	VkPipelineLayout			pipelineLayout;
	VkPipeline					pipeline;
	VkCommandPool				commandPool;
	VkCommandBuffer				commands;
	VkFence						fence;

	// M13 step 2: the sampler for the window images (VK_FILTER_LINEAR),
	// and a 1x1 image for the binding when a pass reads a buffer
	VkSampler					sampler;
	VkImage						blankImage;
	VkDeviceMemory				blankMemory;
	VkImageView					blankView;

	// M13 step 2, gpu_debug: GPU timestamps between the compose's passes
	VkQueryPool					timestamps;
	float						timestampPeriod;

	VkBuffer					frame;
	VkDeviceMemory				frameMemory;
	uint32*						frameBits;

	// M11 step 4 (decision 16): the effects pipeline, and the levels of
	// the Dual Kawase blur.
	VkDescriptorSetLayout		effectsSetLayout;
	VkPipelineLayout			effectsPipelineLayout;
	VkPipeline					effectsPipeline;
	VkBuffer					scratch;
	VkDeviceMemory				scratchMemory;
	uint64						scratchBytes;

	// M11 step 4b: the workspace slide, the old picture and the new one
	VkBuffer					slideOld;
	VkDeviceMemory				slideOldMemory;
	VkBuffer					slideNew;
	VkDeviceMemory				slideNewMemory;
	uint64						slideBytes;
	char						deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
	// M12: what Skia is told about this device
	uint32						family;
	const char*					extensions[3];
	uint32						extensionCount;

	// M11 step 3b: imports of shared views (RADV only).
	bool						canImport;
	void*						drm;
	r5_token_to_fd_hook			tokenToFd;
	r5_mark_get_hook			markGet;
	r5_import_size_hook			importSize;

	// M11 step 3b, decision 4: GPUCopy's back buffer and the scanout,
	// imported from app_server's own exports.
	bool						scanActive;
	uint32						scanGeneration;
	// The GPUCopy generation of the last attempt that failed: no new
	// export until the back buffer or the scanout is a new one.
	uint32						scanFailedGeneration;
	VkBuffer					back;
	VkDeviceMemory				backMemory;
	uint32						backPitch;
	VkBuffer					scanout;
	VkDeviceMemory				scanoutMemory;
	uint32						scanPitch;
};


// M11 step 4: the push constants of effects.comp.
struct effect_rect {
	uint32	mode;
	uint32	width;
	uint32	height;
	uint32	targetX;
	uint32	targetY;
	uint32	targetStride;
	uint32	sourceX;
	uint32	sourceY;
	uint32	sourceStride;
	uint32	sourceWidth;
	uint32	sourceHeight;
	uint32	auxX;
	uint32	auxY;
	uint32	auxStride;
	uint32	amount;
	float	offset;
	int32	shape[16];		// mode 4: four rectangles, left top right bottom
};

enum {
	kEffectShadow = 0,
	kEffectDown = 1,
	kEffectUp = 2,
	kEffectGlass = 3,
	kEffectWindow = 4,
	kEffectSlide = 5,
	kEffectCorner = 6
};

static const uint32 kEffectFixedGlass = 0x100;


struct compose_rect {
	uint32	sourceX;
	uint32	sourceY;
	uint32	sourceStride;
	uint32	frameX;
	uint32	frameY;
	uint32	frameStride;
	uint32	width;
	uint32	height;
	uint32	fill;		// 1: the colour, 2: from the image (M13 step 2)
	uint32	color;
};

static const uint32 kComposeFromImage = 2;
// M13 step 2: effects.comp reads the window from its image
static const uint32 kEffectFromImage = 0x10;


//!	Pushes the two buffers of compose.comp and the image (M13 step 2; the
//!	blank one when \a image is VK_NULL_HANDLE).
static inline void
push_compose_buffers(CompositorVulkan* vk, VkBuffer source, VkBuffer frame,
	VkImageView image = VK_NULL_HANDLE)
{
	VkDescriptorBufferInfo infos[2] = {};
	infos[0].buffer = source;
	infos[0].range = VK_WHOLE_SIZE;
	infos[1].buffer = frame;
	infos[1].range = VK_WHOLE_SIZE;
	VkDescriptorImageInfo imageInfo = {};
	imageInfo.sampler = vk->sampler;
	imageInfo.imageView = image != VK_NULL_HANDLE ? image : vk->blankView;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet writes[3] = {};
	for (int i = 0; i < 3; i++) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		if (i < 2) {
			writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[i].pBufferInfo = &infos[i];
		} else {
			writes[i].descriptorType
				= VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo = &imageInfo;
		}
	}
	vk->vkCmdPushDescriptorSetKHR(vk->commands,
		VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipelineLayout, 0, 3, writes);
}



//!	Pushes the three buffers of effects.comp, and the image (M13 step 2;
//!	the blank one when \a image is VK_NULL_HANDLE).
static inline void
push_effect_buffers(CompositorVulkan* vk, VkBuffer source, VkBuffer target,
	VkBuffer aux, VkImageView image = VK_NULL_HANDLE)
{
	VkDescriptorBufferInfo infos[3] = {};
	infos[0].buffer = source;
	infos[1].buffer = target;
	infos[2].buffer = aux;
	VkDescriptorImageInfo imageInfo = {};
	imageInfo.sampler = vk->sampler;
	imageInfo.imageView = image != VK_NULL_HANDLE ? image : vk->blankView;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet writes[4] = {};
	for (int i = 0; i < 4; i++) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		if (i < 3) {
			infos[i].range = VK_WHOLE_SIZE;
			writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[i].pBufferInfo = &infos[i];
		} else {
			writes[i].descriptorType
				= VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo = &imageInfo;
		}
	}
	vk->vkCmdPushDescriptorSetKHR(vk->commands,
		VK_PIPELINE_BIND_POINT_COMPUTE, vk->effectsPipelineLayout, 0, 4,
		writes);
}


static inline void
dispatch_effect(CompositorVulkan* vk, const effect_rect& effect)
{
	vk->vkCmdPushConstants(vk->commands, vk->effectsPipelineLayout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(effect), &effect);
	vk->vkCmdDispatch(vk->commands, (effect.width + 15) / 16,
		(effect.height + 15) / 16, 1);
}



static inline void
effect_barrier(CompositorVulkan* vk)
{
	VkMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
		| VK_ACCESS_SHADER_WRITE_BIT;
	vk->vkCmdPipelineBarrier(vk->commands,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0,
		NULL);
}



//!	\a region with every rectangle grown by \a by on each side.
static inline BRegion
grow_region(const BRegion& region, int32 by)
{
	BRegion grown;
	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect r = region.RectAtInt(i);
		r.left -= by;
		r.top -= by;
		r.right += by;
		r.bottom += by;
		grown.Include(r);
	}
	return grown;
}



//	#pragma mark - Entry and Layer


/*!	A window the compositor knows. It owns the window's interface, which
	must outlive the window's engine and every compose that reads it.
*/
struct Compositor::Entry : public BReferenceable {
	Entry(Window* window, WindowHWInterface* iface)
		:
		window(window),
		iface(iface),
		lastShown(0),
		shareLogged(false),
		shareOnLogged(false),
		hasGlassViews(false),
		maskWidth(0),
		maskHeight(0),
		maskReach(0),
		maskSmall(false),
		maskStrength(0),
		maskStretched(false),
		maskChanged(0),
		glassLeft(0),
		glassTop(0),
		glassStrength(0),
		glassBelow(0),
		glassValid(false),
		animOpen(false),
		animStart(0),
		animFrom(0),
		animFrames(0)
	{
	}

	virtual ~Entry()
	{
		delete iface;
	}

	Window*				window;
	WindowHWInterface*	iface;
	bigtime_t			lastShown;
	// "window <id> zero-copy off: <reason>" was logged for this window.
	bool				shareLogged;
	// "window <id> zero-copy on" was logged for this window (M11 stick).
	bool				shareOnLogged;
	// M11 step 4: ContentDamage taken by the compose thread this frame.
	BRegion				takenDamage;

	// M11 step 4. Written with all windows locked: one of its views asked
	// for glass. Everything below is the compose thread's own.
	bool				hasGlassViews;

	// The shadow mask: one byte per pixel, the footprint blurred once
	// when its shape, its focus or the step changes; kept.
	BReference<WindowBuffer> mask;
	int32				maskWidth;
	int32				maskHeight;
	int32				maskReach;
	bool				maskSmall;
	int32				maskStrength;
	BRegion				maskShape;		// footprint, window coordinates
	// M13 step 4: the mask was stretched, not blurred, for maskShape; and
	// when maskShape last changed
	bool				maskStretched;
	bigtime_t			maskChanged;

	// The kept blur behind the glass: one full-size pixel per glass pixel,
	// the rectangles of glassShape one after another.
	BReference<WindowBuffer> glass;
	BRegion				glassShape;		// screen coordinates
	int32				glassLeft;
	int32				glassTop;
	int32				glassStrength;
	uint32				glassBelow;		// which windows lay below
	bool				glassValid;
	// review 17 Nov 2026: glass left unblurred while it was hidden; it is
	// blurred when it shows, even without damage from below
	BRegion				glassStale;

	// M11 step 4b, under fAnimLock: the open animation
	bool				animOpen;
	bigtime_t			animStart;
	float				animFrom;
	int32				animFrames;
	BRegion				animArea;
};


/*!	M11 step 4b: a window that closed, drawn from its last buffer while it
	fades and shrinks. Under fAnimLock.
*/
/*!	The round outline of a layer (WindowOutline.h), kept as one value so a
	copy of the layer (the compose thread's snapshot, a closing ghost) takes
	all of it. Review, 22 Nov 2026: the snapshot copied field by field and
	left these out, so no corner was ever drawn round.
*/
struct layer_round {
	layer_round()
		:
		cornerCount(0),
		edgeColor(0),
		edgeWidth(0)
	{
	}

	bool IsRound() const { return cornerCount > 0; }

	// the pixels the arc crosses: they do not hide what lies below
	BRegion			edge;
	// every pixel in a corner's square: drawn by _RecordCorners() (mode 6)
	// or, for glass, _RecordGlass() (mode 3)
	BRegion			area;
	int32			cornerCount;
	window_corner	corners[R5_WINDOW_OUTLINE_MAX_RECTS * 4];
	// the decorator's line along the outer edge (Decorator::GetEdgeLine()),
	// as a frame word; width 0: none
	uint32			edgeColor;
	int32			edgeWidth;
};


struct Compositor::Ghost {
	BReference<Entry>			entry;
	BReference<WindowBuffer>	buffer;
	int32						left;
	int32						top;
	BRegion						shape;
	layer_round					round;
	BRegion						area;
	bigtime_t					start;
	float						from;
	float						visible;
	int32						frames;
};


/*!	M11 step 3b: the images of one Vulkan view's swapchain, imported from
	the program. current is the image the compose samples, pending the one
	presented last and not shown yet. Every other image is the program's;
	its semaphore was released when it left current or pending. The
	compose waits for its fence every frame, so an image that left current
	at the start of a frame is read by no GPU work.
*/
struct Compositor::Share {
	struct Image {
		VkBuffer		buffer;
		VkDeviceMemory	memory;
		uint32			importId;
		sem_id			sem;
		uint32			width;
		uint32			height;
		uint32			pitch;
		// overtaken while pending; back to the program once its mark has
		// passed (_ShareRetireDone)
		bool			retiring;
	};

	Window*				window;
	int32				viewToken;
	team_id				team;
	int32				id;
	uint32				count;
	uint32				generation;
	Image				images[kMaxShareImages];
	int32				current;
	int32				pending;
	// No more semaphores released, no more presents taken; freed by the
	// compose thread at the start of its next frame.
	bool				ended;
	// The import is gone (mark_get failed): the program gets a refusal
	// at its next present and makes a copy swapchain.
	bool				broken;
	// M11 review: ended by app_server (its window went, or a newer
	// swapchain of the view), not by the program: the images app_server
	// holds go back when it is freed, or an acquire may wait for good.
	bool				giveBack;
	// From the last clipping rebuild: the view's part of the screen and
	// the screen position of the image's pixel (0, 0).
	BRegion				shape;
	int32				left;
	int32				top;
};


//!	One shared image in one frame, copied under fShareLock.
struct Compositor::ShareDraw {
	Window*				window;
	VkBuffer			buffer;
	uint32				width;
	uint32				height;
	uint32				pitch;
	BRegion				shape;
	int32				left;
	int32				top;
};


/*!	One window in the compose, back to front. A copy taken under the
	Desktop lock, so the compose thread never needs that lock.
*/
struct Compositor::Layer {
	Layer()
		:
		left(0),
		top(0),
		shadow(false),
		shadowSmall(false),
		belowHash(0),
		animatable(false),
		visible(-1)
	{
		imageX = 0;
		imageY = 0;
	}

	bool HasEffects() const
	{
		return shadow || glassFixed.CountRects() > 0
			|| glassAlpha.CountRects() > 0;
	}

	//! Copies every clipping-rebuild field for the compose thread's snapshot.
	void CopySnapshotFrom(const Layer& source)
	{
		entry = source.entry;
		buffer = source.buffer;
		left = source.left;
		top = source.top;
		shape = source.shape;
		shadow = source.shadow;
		shadowSmall = source.shadowSmall;
		shadowArea = source.shadowArea;
		outline = source.outline;
		glassFixed = source.glassFixed;
		glassAlpha = source.glassAlpha;
		animatable = source.animatable;
		round = source.round;
	}

	BReference<Entry>			entry;
	BReference<WindowBuffer>	buffer;
	int32						left;
	int32						top;
	BRegion						shape;

	// M13 step 2, per frame: the image of a window that Skia draws, and
	// where its pixel (0, 0) lies in the buffer; held until the fence
	BReference<SkiaImage>		image;
	int32						imageX;
	int32						imageY;

	//! The screen rectangle the window's pixels come from.
	BRect PixelRect() const
	{
		if (image.IsSet()) {
			return BRect((float)(left + imageX), (float)(top + imageY),
				(float)(left + imageX + image->Width() - 1),
				(float)(top + imageY + image->Height() - 1));
		}
		return BRect((float)left, (float)top,
			(float)left + buffer->Width() - 1,
			(float)top + buffer->Height() - 1);
	}

	//! Pixels to show: an image, or a buffer with memory.
	bool HasPixels() const
	{
		return image.IsSet()
			|| (buffer.IsSet() && buffer->VulkanBuffer() != 0);
	}

	//! Screen to source coordinates.
	int32 SourceLeft() const { return image.IsSet() ? left + imageX : left; }
	int32 SourceTop() const { return image.IsSet() ? top + imageY : top; }

	// M11 step 4, from the clipping rebuild: where the shadow falls
	// (outside the footprint, inside the decorator's extent), and the
	// glass of the tab and frame line (fixed) and of glass views (alpha).
	bool						shadow;
	bool						shadowSmall;
	BRegion						shadowArea;
	// M13 step 4: the outline of a resize on Lavapipe (screen coordinates),
	// drawn over the window by _RecordOutline(); not part of shape
	BRegion						outline;
	BRegion						glassFixed;
	BRegion						glassAlpha;

	// Per frame: what this window changed, what the blur is redone for,
	// and which windows lie below it.
	BRegion						contentDamage;
	BRegion						blurRegion;
	uint32						belowHash;

	// M11 step 4b: may be animated (a decorated window, not a menu, no
	// direct access), and this frame's visibility while it opens (-1:
	// not animating)
	bool						animatable;
	float						visible;

	// The round outline (WindowOutline.h); the cut corners are already out
	// of shape.
	layer_round					round;
};




/*!	The corner's circle (shape[0]) and the decorator's edge line (shape[1]:
	its colour as a frame word, its width) for effects modes 3 and 6.
*/
static inline void
set_corner(effect_rect& effect, const window_corner& corner,
	const layer_round& round)
{
	effect.shape[0] = corner.centerX;
	effect.shape[1] = corner.centerY;
	effect.shape[2] = corner.radius;
	effect.shape[3] = 1;
	effect.shape[4] = (int32)round.edgeColor;
	effect.shape[5] = round.edgeWidth;
	effect.shape[6] = 0;
	effect.shape[7] = 0;
}



//!	M11 step 4b: ease out, cubic.
static inline float
ease_out(float t)
{
	if (t >= 1.0f)
		return 1.0f;
	if (t <= 0.0f)
		return 0.0f;
	float rest = 1.0f - t;
	return 1.0f - rest * rest * rest;
}



// Defined in CompositorVulkan.cpp; used by the frame, scratch, slide and
// compare buffers.
bool	compositor_allocate_host_buffer(CompositorVulkan* vk, uint64 bytes,
			VkBuffer* _buffer, VkDeviceMemory* _memory, void** _bits);


#endif	// COMPOSITOR_PRIVATE_H
