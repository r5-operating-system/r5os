/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M11 A1: compose only, with exactly the pixels of the CPU mode. Every
 * window draws into its own buffer (WindowHWInterface). The compose
 * thread copies the damaged parts of the background and of each window,
 * back to front, into a frame with one compute shader, waits for the
 * fence, and puts the frame on the screen through the screen's back
 * buffer and CopyBackToFront: the CPU row copy on Lavapipe, the M8
 * DMA_DATA copy on RADV when gpu_app_server is on.
 */


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

#include "r5_span_avx.h"
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
#include "decorator/Decorator.h"

#include <WindowPrivate.h>

#include "compose_period.h"
#include "compositor_shaders.h"

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
// The earliest mouse move that no damage has answered yet, or 0.
static int64 sInputWhen = 0;


static void
sort_samples(bigtime_t* samples, int32 count)
{
	// insertion sort, as for the compose median: at most 1024 samples,
	// once per report
	for (int32 i = 1; i < count; i++) {
		bigtime_t value = samples[i];
		int32 j = i - 1;
		while (j >= 0 && samples[j] > value) {
			samples[j + 1] = samples[j];
			j--;
		}
		samples[j + 1] = value;
	}
}


static void
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


struct settings {
	bool	want;
	bool	lavapipe;
	bool	fail;
	bool	debug;
	// M11 step 3b, decision 4: the frame is the back buffer and one
	// vkCmdCopyBuffer puts it on the scanout. M13 (stick 65): on by
	// default on a release-supported GPU, otherwise opt-in.
	// gpu_compositor_scanout false turns it off.
	bool	scanout;
	// M11 stick: gpu_compositor_zero_copy false, a test switch. Every
	// program keeps the copy path, so the stick can measure both ways in
	// one boot.
	bool	noZeroCopy;
	// M12: Skia draws the windows on the GPU. Off by default until the
	// stick (decision 1); gpu_draw_fail is the test switch of decision 8.
	bool	draw;
	bool	drawFail;
	// M12 step 5b: gpu_draw_classic true keeps the classic look;
	// gpu_draw_linear_edges: 0 the default (off), 1 true, 2 false
	bool	drawClassic;
	bool	noHardwareBlend;
	int		drawLinearEdges;
	// M13 step 3, Lavapipe only, opt-in (0: Mesa's and Skia's defaults):
	// gpu_lavapipe_msaa 1|4 (Skia's sample count for paths),
	// gpu_lavapipe_threads <n> (LP_NUM_THREADS; -1 without the line: 2,
	// 0: Mesa's own count), gpu_lavapipe_vector
	// 128|256 (LP_NATIVE_VECTOR_WIDTH)
	int		lavapipeMsaa;
	bool	noCull;
	int		lavapipeThreads;
	int		lavapipeVector;
};


/*!	Read the way GPUCopy reads gpu_app_server: the radeon_hd settings of
	the account app_server runs as.
*/
static void
load_settings(settings& out)
{
	memset(&out, 0, sizeof(out));
	// M13: no gpu_lavapipe_threads line: the R5 default (2)
	out.lavapipeThreads = -1;
	// M13 step 5 (owner, 16 Nov 2026, done 19 Nov 2026): Skia on for
	// RADV and Lavapipe. gpu_draw false turns it off. Safe mode never
	// reaches this: the compositor is not created, and Painter draws.
	out.draw = true;
	// On by default (user, 15 Nov 2026): RADV when the GPU passed its
	// checks, Lavapipe otherwise. gpu_compositor false is the opt-out.
	out.want = true;
	out.scanout = GPUCopy::ReleaseDefault();
	char path[B_PATH_NAME_LENGTH];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, path,
			sizeof(path)) != B_OK)
		return;
	if (strlcat(path, "/kernel/drivers/radeon_hd", sizeof(path))
			>= sizeof(path))
		return;
	FILE* file = fopen(path, "r");
	if (file == NULL) {
		fprintf(stderr, "app_server: radeon_hd settings none\n");
		syslog(LOG_INFO, "app_server: radeon_hd settings none\n");
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, "gpu_compositor true", 19) == 0)
			out.want = true;
		if (strncmp(line, "gpu_compositor false", 20) == 0)
			out.want = false;
		if (strncmp(line, "gpu_compositor_lavapipe true", 28) == 0)
			out.lavapipe = true;
		if (strncmp(line, "gpu_compositor_fail true", 24) == 0)
			out.fail = true;
		if (strncmp(line, "gpu_debug true", 14) == 0)
			out.debug = true;
		// M13 step 3: a test switch for the occlusion culling
		if (strncmp(line, "gpu_compositor_cull false", 25) == 0)
			out.noCull = true;
		if (strncmp(line, "gpu_compositor_scanout true", 27) == 0)
			out.scanout = true;
		if (strncmp(line, "gpu_compositor_scanout false", 28) == 0)
			out.scanout = false;
		if (strncmp(line, "gpu_compositor_zero_copy false", 30) == 0)
			out.noZeroCopy = true;
		if (strncmp(line, "gpu_draw true", 13) == 0)
			out.draw = true;
		if (strncmp(line, "gpu_draw false", 14) == 0)
			out.draw = false;
		if (strncmp(line, "gpu_draw_fail true", 18) == 0)
			out.drawFail = true;
		if (strncmp(line, "gpu_draw_classic true", 21) == 0)
			out.drawClassic = true;
		if (strncmp(line, "gpu_draw_hardware_blend false", 29) == 0)
			out.noHardwareBlend = true;
		if (strncmp(line, "gpu_draw_linear_edges true", 26) == 0)
			out.drawLinearEdges = 1;
		if (strncmp(line, "gpu_draw_linear_edges false", 27) == 0)
			out.drawLinearEdges = 2;
		int value;
		if (sscanf(line, "gpu_lavapipe_msaa %d", &value) == 1
			&& (value == 1 || value == 4))
			out.lavapipeMsaa = value;
		if (sscanf(line, "gpu_lavapipe_threads %d", &value) == 1
			&& value >= 0 && value <= 64)
			out.lavapipeThreads = value;
		if (sscanf(line, "gpu_lavapipe_vector %d", &value) == 1
			&& (value == 128 || value == 256))
			out.lavapipeVector = value;
	}
	fclose(file);
	fprintf(stderr, "app_server: radeon_hd settings %s\n", path);
	syslog(LOG_INFO, "app_server: radeon_hd settings %s\n", path);
}


/*!	The CPU time of the whole app_server team, user and kernel. */
static bigtime_t
team_cpu_time()
{
	team_usage_info usage;
	if (get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &usage)
			!= B_OK)
		return 0;
	return usage.user_time + usage.kernel_time;
}


/*!	Whether the GPU passed its checks at boot (R5_GPU_CAP_AVAILABLE) and
	is not lost now. The bit decides RADV or Lavapipe, not the settings.
*/
static bool
gpu_available(uint64* _gttBytes, const char** _reason)
{
	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	if (fd < 0) {
		*_reason = "no render node";
		return false;
	}
	r5_gpu_info info;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	info.version = 1;
	bool ok = ioctl(fd, R5_GPU_GET_INFO, &info, sizeof(info)) == 0;
	close(fd);
	if (!ok) {
		*_reason = "no gpu info";
		return false;
	}
	if ((info.capabilities & R5_GPU_CAP_AVAILABLE) == 0) {
		*_reason = "gpu not available";
		return false;
	}
	if ((info.capabilities & R5_GPU_CAP_LOST) != 0) {
		*_reason = "gpu lost";
		return false;
	}
	if (_gttBytes != NULL)
		*_gttBytes = info.gtt_bytes;
	return true;
}


static bigtime_t
thread_cpu_time()
{
	thread_info info;
	if (get_thread_info(find_thread(NULL), &info) != B_OK)
		return 0;
	return info.user_time + info.kernel_time;
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
	kEffectSlide = 5
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
static void
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
struct Compositor::Ghost {
	BReference<Entry>			entry;
	BReference<WindowBuffer>	buffer;
	int32						left;
	int32						top;
	BRegion						shape;
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
};


//!	M11 step 4b: ease out, cubic.
static float
ease_out(float t)
{
	if (t >= 1.0f)
		return 1.0f;
	if (t <= 0.0f)
		return 0.0f;
	float rest = 1.0f - t;
	return 1.0f - rest * rest * rest;
}


/*!	Whether the boot menu asked for safe mode or the fail-safe video
	mode. Both keep the desktop in the CPU mode, the way back when the
	composed desktop does not start.
*/
static bool
safe_mode_option(const char* name)
{
	char buffer[B_FILE_NAME_LENGTH];
	size_t size = sizeof(buffer);
	if (_kern_get_safemode_option(name, buffer, &size) != B_OK)
		return false;
	return strncasecmp(buffer, "true", size) == 0
		|| strncasecmp(buffer, "yes", size) == 0
		|| strncasecmp(buffer, "on", size) == 0
		|| strncasecmp(buffer, "enabled", size) == 0;
}


//	#pragma mark - Compositor


Compositor*
Compositor::Create(Desktop* desktop, HWInterface* screen)
{
	settings options;
	load_settings(options);
	if (!options.want) {
		log_line("gpu compositor off: setting");
		return NULL;
	}
	if (safe_mode_option(B_SAFEMODE_SAFE_MODE)
		|| safe_mode_option(B_SAFEMODE_FAIL_SAFE_VIDEO_MODE)) {
		log_line("gpu compositor off: safe mode");
		return NULL;
	}

	Compositor* compositor = new(std::nothrow) Compositor(desktop, screen);
	if (compositor == NULL)
		return NULL;
	compositor->fDebug = options.debug;
	compositor->fForceFail = options.fail;
	// 5625U stick, 17 Nov 2026: RADV and the R5 DRM library print their
	// errors on stderr, which did not reach the syslog. With gpu_debug
	// app_server's stderr goes to a file next to it. Rotate any prior log
	// instead of unbounded append, and use line buffering off the submit path.
	if (options.debug) {
		char path[B_PATH_NAME_LENGTH];
		if (find_directory(B_SYSTEM_LOG_DIRECTORY, -1, true, path,
				sizeof(path)) == B_OK
			&& strlcat(path, "/app_server-stderr.log", sizeof(path))
				< sizeof(path)) {
			struct stat st;
			if (stat(path, &st) == 0 && st.st_size > 0) {
				char oldPath[B_PATH_NAME_LENGTH + 8];
				snprintf(oldPath, sizeof(oldPath), "%s.old", path);
				rename(path, oldPath);
			}
			if (freopen(path, "w", stderr) != NULL) {
				setvbuf(stderr, NULL, _IOLBF, 0);
				fprintf(stderr, "app_server: stderr of this session\n");
				log_line("stderr into %s", path);
			}
		}
	}
	compositor->fLavapipeMsaa = options.lavapipeMsaa;
	compositor->fNoCull = options.noCull;
	if (options.noCull)
		log_line("compose culling off (setting)");
	// M13 step 3: llvmpipe reads these when the driver starts; RADV does
	// not look at them
	// M13 (owner, 17 Nov 2026): 2 threads by default. QEMU (8 virtual
	// CPUs, the Painter desktop): drag 18.5 ms per frame with 2, 57 ms
	// with 4, about 170 ms with Mesa's one per CPU; Lavapipe's threads and
	// app_server's own crowded each other out. gpu_lavapipe_threads <n>
	// sets another count, 0 Mesa's own; an LP_NUM_THREADS already in the
	// environment (a developer's) stays.
	if (options.lavapipeThreads > 0) {
		char value[16];
		snprintf(value, sizeof(value), "%d", options.lavapipeThreads);
		setenv("LP_NUM_THREADS", value, 1);
		log_line("lavapipe threads %d (setting)", options.lavapipeThreads);
	} else if (options.lavapipeThreads < 0
		&& setenv("LP_NUM_THREADS", "2", 0) == 0) {
		log_line("lavapipe threads %s (default)", getenv("LP_NUM_THREADS"));
	}
	if (options.lavapipeVector > 0) {
		char value[16];
		snprintf(value, sizeof(value), "%d", options.lavapipeVector);
		setenv("LP_NATIVE_VECTOR_WIDTH", value, 1);
		log_line("lavapipe vector width %d (setting)", options.lavapipeVector);
	} else if (getenv("LP_NATIVE_VECTOR_WIDTH") == NULL) {
#if defined(__x86_64__)
		/* x86-64-v2 stays the baseline. AVX2 picks Lavapipe's 256-bit
		 * vectors; a CPU without it leaves Mesa's own choice. An
		 * environment value already set is left alone, and the setting
		 * above wins over both. */
		if ((r5_x86_cpu_flags() & R5_CPU_AVX2) != 0
			&& setenv("LP_NATIVE_VECTOR_WIDTH", "256", 0) == 0) {
			log_line("lavapipe vector width 256 (avx2)");
		}
#endif
	}
	compositor->fWantScanout = options.scanout;
	compositor->fZeroCopyOff = options.noZeroCopy ? 1 : 0;
	if (options.noZeroCopy)
		log_line("zero-copy setting off");

	const char* reason = NULL;
	uint64 gttBytes = 0;
	bool radv = !options.lavapipe && gpu_available(&gttBytes, &reason);
	if (options.lavapipe)
		reason = "forced";

	if (radv) {
		const char* radvReason = NULL;
		if (compositor->_InitVulkan(true, &radvReason) == B_OK) {
			compositor->fBudget = gttBytes / 2;
			if (compositor->fBudget == 0) {
				// no GTT size from the driver: the Lavapipe rule
				system_info info;
				get_system_info(&info);
				compositor->fBudget = (uint64)info.max_pages * B_PAGE_SIZE / 4;
			}
			log_line("gpu compositor on radv (%s)",
				compositor->fVulkan->deviceName);
			// M11 step 3b: FENCE_WAIT with timeout 0 for the present mark
			// of a shared view. The sequence is the one GFX ring's, so any
			// file of the render node can ask.
			compositor->fFenceFd = open("/dev/gpu/radeon_hd/0",
				O_RDWR | O_CLOEXEC);
		} else {
			reason = radvReason;
			radv = false;
		}
	}
	if (!radv) {
		const char* lavapipeReason = NULL;
		if (compositor->_InitVulkan(false, &lavapipeReason) != B_OK) {
			// Lavapipe cannot start either: the CPU mode, nothing else
			// logged (docs/APP-SERVER-COMPOSITOR.md).
			delete compositor;
			return NULL;
		}
		compositor->fOnLavapipe = true;
		system_info info;
		get_system_info(&info);
		compositor->fBudget = (uint64)info.max_pages * B_PAGE_SIZE / 4;
		log_line("gpu compositor on lavapipe: %s", reason);
	}

	SkiaDrawingEngine::SetDebug(options.debug);
	SkiaDrawingEngine::SetClassic(options.drawClassic);
	SkiaDrawingEngine::SetHardwareBlend(!options.noHardwareBlend);
	if (options.noHardwareBlend)
		log_line("gpu draw hardware blend off (setting)");
	SkiaDrawingEngine::SetLinearEdges(options.drawLinearEdges == 1);
	if (options.draw)
		compositor->_InitSkia(options.drawFail);

	// Start splash (owner, 17 Nov 2026): the boot picture stays until the
	// first window with a frame is drawn, so Sign in never shows half
	// built while Lavapipe compiles its shaders
	const char* splashReason = NULL;
	if (compositor->_CaptureSplash(&splashReason)) {
		compositor->fSplashStart = system_time();
		compositor->fSplashHolding = true;
		log_line("start splash on");
	} else
		log_line("start splash off: %s", splashReason);

	if (compositor->_Init() != B_OK) {
		delete compositor;
		return NULL;
	}
	return compositor;
}


/*!	M12: Skia's Graphite context on this device and queue (decision 4).
	When it does not start, Painter keeps drawing into the window buffers,
	as in M11.
*/
void
Compositor::_InitSkia(bool forceFail)
{
	CompositorVulkan* vk = fVulkan;
	skia_vulkan_device device;
	memset(&device, 0, sizeof(device));
	device.getInstanceProc = (void*)vk->vkGetInstanceProcAddr;
	device.instance = (void*)vk->instance;
	device.physicalDevice = (void*)vk->physical;
	device.device = (void*)vk->device;
	device.queue = (void*)vk->queue;
	device.queueFamily = vk->family;
	device.apiVersion = VK_API_VERSION_1_1;
	device.deviceExtensions = vk->extensions;
	device.deviceExtensionCount = vk->extensionCount;
	const char* reason = NULL;
	// M13 step 3: gpu_lavapipe_msaa, on Lavapipe only
	int samples = fOnLavapipe && fLavapipeMsaa > 0 ? fLavapipeMsaa : 4;
	if (fOnLavapipe) {
		const char* thr = getenv("LP_NUM_THREADS");
		const char* vec = getenv("LP_NATIVE_VECTOR_WIDTH");
		log_line("lavapipe config threads=%s vector=%s msaa=%d",
			thr != NULL ? thr : "auto", vec != NULL ? vec : "auto", samples);
	}
	if (samples != 4)
		log_line("lavapipe msaa %d (setting)", samples);
	fSkia = SkiaGpu::Create(device, forceFail, samples, &reason);
	if (fSkia == NULL) {
		log_line("gpu draw off: %s", reason);
		return;
	}
	fSkia->SetWaker(&_SkiaWake, this);
	SkiaGpu::SetCurrent(fSkia);
	fDrawSem = create_sem(0, "skia draw wake");
	if (fDrawSem >= 0) {
		fDrawThread = spawn_thread(_DrawThread, "skia draw",
			B_DISPLAY_PRIORITY, this);
		if (fDrawThread >= 0)
			resume_thread(fDrawThread);
	}
	// without the thread the compose thread runs the work, as before
	// step 5c: the pipelines compile now, not when the first window draws
	thread_id warmUp = spawn_thread(&_SkiaWarmUp, "skia warm up",
		B_LOW_PRIORITY, this);
	if (warmUp >= 0)
		resume_thread(warmUp);
	SkiaDrawingEngine::SetDebug(fDebug);
	log_line("gpu draw on %s skia %s", vk->deviceName, SkiaGpu::Version());
}


/*static*/ status_t
Compositor::_SkiaWarmUp(void* cookie)
{
	skia_warm_up();
	return B_OK;
}


/*static*/ void
Compositor::_SkiaWake(void* cookie)
{
	// SkiaGpu wakes once per RunWork(); fWakePending is the damage's flag
	// and may stay set while the loop runs without damage
	Compositor* compositor = (Compositor*)cookie;
	if (compositor->fDrawThread >= 0)
		release_sem_etc(compositor->fDrawSem, 1, B_DO_NOT_RESCHEDULE);
	else
		release_sem_etc(compositor->fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*static*/ status_t
Compositor::_DrawThread(void* data)
{
	((Compositor*)data)->_DrawLoop();
	return B_OK;
}


/*!	M12: runs what the windows drew (SkiaGpu::RunWork, the only thread
	that submits Skia's work). Deliver() copies the pixels into the window
	buffers and reports their damage, which wakes the compose. A failure
	turns the GPU drawing off through the compose thread (decision 8).
*/
void
Compositor::_DrawLoop()
{
	while (!fDrawQuit) {
		// a wake may be missed while a run is busy; look again anyway
		status_t status = acquire_sem_etc(fDrawSem, 1, B_RELATIVE_TIMEOUT,
			100000);
		if (status != B_OK && status != B_TIMED_OUT && status != B_INTERRUPTED)
			break;
		if (fDrawQuit || fSkia == NULL || fSkia->IsShutDown())
			break;
		const char* reason = NULL;
		if (!fSkia->RunWork(&reason)) {
			fDrawFailure = true;
			if (fFailure == NULL)
				fFailure = reason != NULL ? reason : "draw";
			release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
			break;
		}
	}
}


//!	Stops the draw thread; Skia is shut down or the compositor ends.
void
Compositor::_StopDrawThread()
{
	if (fDrawThread < 0)
		return;
	fDrawQuit = true;
	release_sem(fDrawSem);
	status_t result;
	wait_for_thread(fDrawThread, &result);
	fDrawThread = -1;
}


Compositor::Compositor(Desktop* desktop, HWInterface* screen)
	:
	fDesktop(desktop),
	fScreen(screen),
	fVulkan(NULL),
	fActive(false),
	fOnLavapipe(false),
	fDebug(false),
	fForceFail(false),
	fWantScanout(false),
	fScanoutLogged(false),
	fFrontByVulkan(false),
	fPresentCount(0),
	fRestarted(false),
	fGeneration(0),
	fEntryLock("compositor entries"),
	fEntries(20),
	fShareLock("compositor shares"),
	fShares(10),
	fShareCounter(0),
	fFenceFd(-1),
	fShareRetiring(false),
	fLayerLock("compositor layers"),
	fLayers(NULL),
	fBackground(0xff000000),
	fDamageLock("compositor damage"),
	fWakePending(false),
	fWakeSem(-1),
	fThread(-1),
	fQuit(false),
	fOutOfMemory(false),
	fPeriod(0),
	fLastCompose(0),
	fGridEpoch(0),
	fGridPeriod(0),
	fGridSlot(-1),
	fShadowDue(0),
	fFrameWidth(0),
	fFrameHeight(0),
	fScratch(NULL),
	fWindowBytes(0),
	fBudget(0),
	fFrames(0),
	fSampleCount(0),
	fFrameCount(0),
	fLastFrame(0),
	fInputCount(0),
	fFrameInput(0),
	fReportStart(0),
	fCpuAtReport(0),
	fTeamCpuAtReport(0),
	fZeroCopyOff(0),
	fLateLogged(0),
	fEffects(kWindowEffectShadow | kWindowEffectFrostedGlass
		| kWindowEffectAnimations),
	fStrength(kWindowEffectsBalanced),
	fEffectsLogged(false),
	fAnimLock("compositor animations"),
	fGhosts(4),
	fLastWorkspace(-1),
	fClipPassTime(0),
	fSlideActive(false),
	fSlideSnapshot(false),
	fSlideStart(0),
	fSlideDirection(1),
	fSlideFrames(0),
	fAnimationsRunning(false),
	fFrameGhosts(NULL),
	fFrameAnimating(false),
	fFrameSlide(false),
	fFrameSlideSnapshot(false),
	fFrameSlideShift(0),
	fComposeSlot("compositor slot"),
	fPaused(false),
	fModePending(false),
	fPauseRedraw(false),
	fDirectWindows(4),
	fDirectScreenWanted(0),
	fFailure(NULL),
	fGpuBusy(0),
	fSkia(NULL),
	fDrawFailure(false),
	fDrawThread(-1),
	fDrawSem(-1),
	fDrawQuit(false),
	fSplashHolding(false),
	fSplashAsked(0),
	fSplashRenew(0),
	fSplashTicket(0),
	fSplashStart(0),
	fSplashChanged(0),
	fFrameSplash(false),
	fSplashTextLeft(0),
	fSplashPebbleLeft(0),
	fSplashLogoTop(0),
	fSplashIconsLeft(0),
	fSplashIconsTop(0)
{
}


Compositor::~Compositor()
{
	AccelerantHWInterface* accelerant
		= dynamic_cast<AccelerantHWInterface*>(fScreen);
	// Review after M11: cleared with the slot held, so a SetMode that
	// already holds it ends first.
	if (accelerant != NULL) {
		fComposeSlot.Lock();
		accelerant->SetModeGuard(NULL);
		fComposeSlot.Unlock();
	}
	if (fThread >= 0) {
		fQuit = true;
		release_sem(fWakeSem);
		status_t result;
		wait_for_thread(fThread, &result);
	}
	_StopDrawThread();
	if (fDrawSem >= 0)
		delete_sem(fDrawSem);
	if (fWakeSem >= 0)
		delete_sem(fWakeSem);
	delete fLayers;
	delete fFrameGhosts;
	fGhosts.MakeEmpty();
	// M13 step 4: freed while the device is still there
	fOutlineMask.Unset();
	_EndSplash();
	_ExitVulkan();
	free(fScratch);
	if (fFenceFd >= 0)
		close(fFenceFd);
}


status_t
Compositor::_Init()
{
	fLayers = new(std::nothrow) LayerList(20);
	fWakeSem = create_sem(0, "compositor wake");
	if (fLayers == NULL || fWakeSem < 0)
		return B_NO_MEMORY;

	_UpdatePeriod();
	fActive = true;
	fReportStart = system_time();
	// M11 review: a mode change waits for the frame in the compose slot
	AccelerantHWInterface* accelerant
		= dynamic_cast<AccelerantHWInterface*>(fScreen);
	if (accelerant != NULL)
		accelerant->SetModeGuard(&fComposeSlot);

	fThread = spawn_thread(_ComposeThread, "compositor",
		B_URGENT_DISPLAY_PRIORITY, this);
	if (fThread < 0) {
		fActive = false;
		return fThread;
	}
	resume_thread(fThread);
	return B_OK;
}


status_t
Compositor::_InitVulkan(bool radv, const char** _reason)
{
	_ExitVulkan();

	CompositorVulkan* vk = new(std::nothrow) CompositorVulkan;
	if (vk == NULL) {
		*_reason = "no memory";
		return B_NO_MEMORY;
	}
	memset(vk, 0, sizeof(*vk));
	fVulkan = vk;
	// buffers of an earlier device are not freed through this one
	fGeneration++;

	// M13 step 2 (owner, 16 Nov 2026): Mesa's own shader cache for
	// app_server (uid 0) goes in the system cache directory, not in the
	// administrator's ~/config/cache. Set before the driver loads; a
	// value already in the environment (a developer's) stays.
	{
		char cache[B_PATH_NAME_LENGTH];
		if (find_directory(B_SYSTEM_CACHE_DIRECTORY, -1, true, cache,
				sizeof(cache)) == B_OK) {
			strlcat(cache, "/app_server", sizeof(cache));
			mkdir(cache, 0755);
			strlcat(cache, "/mesa", sizeof(cache));
			mkdir(cache, 0755);
			unsetenv("MESA_SHADER_CACHE_DISABLE");
			setenv("MESA_SHADER_CACHE_DIR", cache, 0);
		}
	}
	vk->library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (vk->library == NULL)
		vk->library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	if (vk->library == NULL) {
		*_reason = "no vulkan loader";
		return B_ERROR;
	}
	vk->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(vk->library,
		"vkGetInstanceProcAddr");
	if (vk->vkGetInstanceProcAddr == NULL) {
		*_reason = "no vulkan loader";
		return B_ERROR;
	}
	vk->vkCreateInstance = (PFN_vkCreateInstance)vk->vkGetInstanceProcAddr(
		NULL, "vkCreateInstance");

	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "app_server";
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &app;
	if (vk->vkCreateInstance == NULL
		|| vk->vkCreateInstance(&instanceInfo, NULL, &vk->instance)
			!= VK_SUCCESS) {
		*_reason = "no vulkan instance";
		return B_ERROR;
	}

#define VK_LOAD_INSTANCE(name) \
	vk->name = (PFN_##name)vk->vkGetInstanceProcAddr(vk->instance, #name); \
	if (vk->name == NULL) { *_reason = "vulkan " #name; return B_ERROR; }
	VK_INSTANCE_FUNCTIONS(VK_LOAD_INSTANCE)

	// RADV is any device that is not a CPU; Lavapipe is the CPU device.
	uint32 count = 8;
	VkPhysicalDevice devices[8];
	if (vk->vkEnumeratePhysicalDevices(vk->instance, &count, devices)
			< VK_SUCCESS || count == 0) {
		*_reason = "no vulkan device";
		return B_ERROR;
	}
	vk->physical = VK_NULL_HANDLE;
	for (uint32 i = 0; i < count; i++) {
		VkPhysicalDeviceProperties properties;
		vk->vkGetPhysicalDeviceProperties(devices[i], &properties);
		bool isCpu = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
		if (isCpu != radv) {
			vk->physical = devices[i];
			vk->timestampPeriod = properties.limits.timestampPeriod;
			strlcpy(vk->deviceName, properties.deviceName,
				sizeof(vk->deviceName));
			break;
		}
	}
	if (vk->physical == VK_NULL_HANDLE) {
		*_reason = radv ? "radv not found" : "lavapipe not found";
		return B_ERROR;
	}

	uint32 extensionCount = 0;
	vk->vkEnumerateDeviceExtensionProperties(vk->physical, NULL,
		&extensionCount, NULL);
	VkExtensionProperties* extensions
		= new(std::nothrow) VkExtensionProperties[extensionCount + 1];
	bool pushDescriptor = false;
	bool memoryFd = false;
	bool dmaBuf = false;
	if (extensions != NULL) {
		vk->vkEnumerateDeviceExtensionProperties(vk->physical, NULL,
			&extensionCount, extensions);
		for (uint32 i = 0; i < extensionCount; i++) {
			if (strcmp(extensions[i].extensionName,
					VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)
				pushDescriptor = true;
			if (strcmp(extensions[i].extensionName,
					VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) == 0)
				memoryFd = true;
			if (strcmp(extensions[i].extensionName,
					VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME) == 0)
				dmaBuf = true;
		}
		delete[] extensions;
	}
	// M11 step 3b: shared views are imported on RADV only.
	vk->canImport = radv && memoryFd && dmaBuf;
	if (!pushDescriptor) {
		*_reason = "no push descriptor";
		return B_ERROR;
	}

	uint32 familyCount = 8;
	VkQueueFamilyProperties families[8];
	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical, &familyCount,
		families);
	// The queue stays a graphics queue (decision 16): the R5 DRM library
	// accepts only GFX submissions, so a compute-only family would make
	// every submit fail. The compose is a compute dispatch on that queue.
	uint32 family = UINT32_MAX;
	for (uint32 i = 0; i < familyCount; i++) {
		log_line("vulkan queue family %" B_PRIu32 " flags %#" B_PRIx32, i,
			(uint32)families[i].queueFlags);
		const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT
			| VK_QUEUE_COMPUTE_BIT;
		if (family == UINT32_MAX
			&& (families[i].queueFlags & wanted) == wanted)
			family = i;
	}
	if (family == UINT32_MAX) {
		*_reason = "no graphics queue";
		return B_ERROR;
	}

	// Host-visible and coherent memory for the window buffers and the
	// frame: Painter writes the window buffers, the compose reads them.
	// Host-cached first. DEVICE_LOCAL is not excluded: RADV may mark GTT
	// that way on an APU (decision 5).
	VkPhysicalDeviceMemoryProperties memory;
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &memory);
	vk->memoryType = UINT32_MAX;
	const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (int pass = 0; pass < 2 && vk->memoryType == UINT32_MAX; pass++) {
		for (uint32 i = 0; i < memory.memoryTypeCount; i++) {
			VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
			if ((flags & wanted) != wanted)
				continue;
			// first pass: also cached, because Painter reads what it
			// blends
			if (pass == 0 && (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == 0)
				continue;
			vk->memoryType = i;
			break;
		}
	}
	if (vk->memoryType == UINT32_MAX) {
		*_reason = "no host memory";
		return B_ERROR;
	}

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = family;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	const char* deviceExtensions[] = { VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME };
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = vk->canImport ? 3 : 1;
	deviceInfo.ppEnabledExtensionNames = deviceExtensions;
	vk->family = family;
	vk->extensionCount = deviceInfo.enabledExtensionCount;
	for (uint32 i = 0; i < vk->extensionCount; i++)
		vk->extensions[i] = deviceExtensions[i];
	if (vk->vkCreateDevice(vk->physical, &deviceInfo, NULL, &vk->device)
			!= VK_SUCCESS) {
		*_reason = "no vulkan device";
		return B_ERROR;
	}

#define VK_LOAD_DEVICE(name) \
	vk->name = (PFN_##name)vk->vkGetDeviceProcAddr(vk->device, #name); \
	if (vk->name == NULL) { *_reason = "vulkan " #name; return B_ERROR; }
	VK_DEVICE_FUNCTIONS(VK_LOAD_DEVICE)

	vk->vkGetDeviceQueue(vk->device, family, 0, &vk->queue);

	// The R5 DRM library RADV loaded: dlopen of a loaded library returns
	// that image, so these calls see RADV's import table.
	if (vk->canImport) {
		vk->drm = dlopen("libdrm.so.2", RTLD_NOW | RTLD_LOCAL);
		if (vk->drm != NULL) {
			vk->tokenToFd = (r5_token_to_fd_hook)dlsym(vk->drm,
				"r5_drm_token_to_fd");
			vk->markGet = (r5_mark_get_hook)dlsym(vk->drm, "r5_drm_mark_get");
			vk->importSize = (r5_import_size_hook)dlsym(vk->drm,
				"r5_drm_import_size");
		}
		if (vk->tokenToFd == NULL || vk->markGet == NULL
			|| vk->importSize == NULL)
			vk->canImport = false;
	}

	VkShaderModuleCreateInfo shaderInfo = {};
	shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize = sizeof(kComposeSpirv);
	shaderInfo.pCode = kComposeSpirv;
	VkShaderModule shader;
	if (vk->vkCreateShaderModule(vk->device, &shaderInfo, NULL, &shader)
			!= VK_SUCCESS) {
		*_reason = "shader";
		return B_ERROR;
	}

	// M13 step 2: two storage buffers and the window's image
	VkDescriptorSetLayoutBinding bindings[3] = {};
	for (int i = 0; i < 3; i++) {
		bindings[i].binding = i;
		bindings[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
			: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	setInfo.bindingCount = 3;
	setInfo.pBindings = bindings;
	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	range.size = sizeof(compose_rect);
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &vk->setLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	VkComputePipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage.sType
		= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module = shader;
	pipelineInfo.stage.pName = "main";
	bool pipelineOk
		= vk->vkCreateDescriptorSetLayout(vk->device, &setInfo, NULL,
			&vk->setLayout) == VK_SUCCESS
		&& vk->vkCreatePipelineLayout(vk->device, &layoutInfo, NULL,
			&vk->pipelineLayout) == VK_SUCCESS;
	if (pipelineOk) {
		pipelineInfo.layout = vk->pipelineLayout;
		pipelineOk = vk->vkCreateComputePipelines(vk->device, VK_NULL_HANDLE,
			1, &pipelineInfo, NULL, &vk->pipeline) == VK_SUCCESS;
	}
	vk->vkDestroyShaderModule(vk->device, shader, NULL);
	if (!pipelineOk) {
		*_reason = "pipeline";
		return B_ERROR;
	}

	// M11 step 4 (decision 16): the second compute pipeline, for the
	// effects. Three storage buffers: a source, the target, and the kept
	// blur for the glass.
	shaderInfo.codeSize = sizeof(kEffectsSpirv);
	shaderInfo.pCode = kEffectsSpirv;
	if (vk->vkCreateShaderModule(vk->device, &shaderInfo, NULL, &shader)
			!= VK_SUCCESS) {
		*_reason = "effects shader";
		return B_ERROR;
	}
	// M13 step 2: and the window's image
	VkDescriptorSetLayoutBinding effectBindings[4] = {};
	for (int i = 0; i < 4; i++) {
		effectBindings[i].binding = i;
		effectBindings[i].descriptorType = i < 3
			? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
			: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		effectBindings[i].descriptorCount = 1;
		effectBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	setInfo.bindingCount = 4;
	setInfo.pBindings = effectBindings;
	range.size = sizeof(effect_rect);
	layoutInfo.pSetLayouts = &vk->effectsSetLayout;
	pipelineInfo.stage.module = shader;
	pipelineOk
		= vk->vkCreateDescriptorSetLayout(vk->device, &setInfo, NULL,
			&vk->effectsSetLayout) == VK_SUCCESS
		&& vk->vkCreatePipelineLayout(vk->device, &layoutInfo, NULL,
			&vk->effectsPipelineLayout) == VK_SUCCESS;
	if (pipelineOk) {
		pipelineInfo.layout = vk->effectsPipelineLayout;
		pipelineOk = vk->vkCreateComputePipelines(vk->device, VK_NULL_HANDLE,
			1, &pipelineInfo, NULL, &vk->effectsPipeline) == VK_SUCCESS;
	}
	vk->vkDestroyShaderModule(vk->device, shader, NULL);
	if (!pipelineOk) {
		*_reason = "effects pipeline";
		return B_ERROR;
	}

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = family;
	VkCommandBufferAllocateInfo commandInfo = {};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandBufferCount = 1;
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (vk->vkCreateCommandPool(vk->device, &poolInfo, NULL, &vk->commandPool)
			!= VK_SUCCESS) {
		*_reason = "command pool";
		return B_ERROR;
	}
	commandInfo.commandPool = vk->commandPool;
	if (vk->vkAllocateCommandBuffers(vk->device, &commandInfo, &vk->commands)
			!= VK_SUCCESS
		|| vk->vkCreateFence(vk->device, &fenceInfo, NULL, &vk->fence)
			!= VK_SUCCESS) {
		*_reason = "command buffer";
		return B_ERROR;
	}
	if (!_InitImages(_reason))
		return B_ERROR;
	return B_OK;
}


/*!	M13 step 2: the sampler of the window images, and the 1x1 image bound
	when a pass reads a buffer, made ready for the sampler once.
*/
bool
Compositor::_InitImages(const char** _reason)
{
	CompositorVulkan* vk = fVulkan;
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (vk->vkCreateSampler(vk->device, &samplerInfo, NULL, &vk->sampler)
			!= VK_SUCCESS) {
		*_reason = "sampler";
		return false;
	}

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	imageInfo.extent.width = 1;
	imageInfo.extent.height = 1;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (vk->vkCreateImage(vk->device, &imageInfo, NULL, &vk->blankImage)
			!= VK_SUCCESS) {
		*_reason = "blank image";
		return false;
	}
	VkMemoryRequirements requirements;
	vk->vkGetImageMemoryRequirements(vk->device, vk->blankImage,
		&requirements);
	VkPhysicalDeviceMemoryProperties memory;
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &memory);
	bool bound = false;
	for (uint32 i = 0; i < memory.memoryTypeCount && !bound; i++) {
		if ((requirements.memoryTypeBits & (1u << i)) == 0)
			continue;
		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = i;
		if (vk->vkAllocateMemory(vk->device, &allocateInfo, NULL,
				&vk->blankMemory) != VK_SUCCESS)
			continue;
		bound = vk->vkBindImageMemory(vk->device, vk->blankImage,
			vk->blankMemory, 0) == VK_SUCCESS;
		if (!bound) {
			vk->vkFreeMemory(vk->device, vk->blankMemory, NULL);
			vk->blankMemory = VK_NULL_HANDLE;
		}
	}
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = vk->blankImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = imageInfo.format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	if (!bound || vk->vkCreateImageView(vk->device, &viewInfo, NULL,
			&vk->blankView) != VK_SUCCESS) {
		*_reason = "blank image";
		return false;
	}

	if (fDebug && vk->timestampPeriod > 0) {
		// M13 step 2: the compose time per pass (gpu_debug only); without
		// it the frame has no timestamps
		VkQueryPoolCreateInfo queryInfo = {};
		queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryInfo.queryCount = kPassStamps;
		if (vk->vkCreateQueryPool(vk->device, &queryInfo, NULL,
				&vk->timestamps) != VK_SUCCESS)
			vk->timestamps = VK_NULL_HANDLE;
	}

	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vk->vkBeginCommandBuffer(vk->commands, &begin);
	VkImageMemoryBarrier toRead = {};
	toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	toRead.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toRead.image = vk->blankImage;
	toRead.subresourceRange = viewInfo.subresourceRange;
	vk->vkCmdPipelineBarrier(vk->commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
		&toRead);
	vk->vkEndCommandBuffer(vk->commands);
	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &vk->commands;
	vk->vkResetFences(vk->device, 1, &vk->fence);
	SkiaGpu::LockQueue();
	VkResult result = vk->vkQueueSubmit(vk->queue, 1, &submit, vk->fence);
	SkiaGpu::UnlockQueue();
	if (result != VK_SUCCESS
		|| vk->vkWaitForFences(vk->device, 1, &vk->fence, VK_TRUE,
			kFenceLimit * 1000) != VK_SUCCESS) {
		*_reason = "blank image";
		return false;
	}
	return true;
}


void
Compositor::_ExitVulkan()
{
	CompositorVulkan* vk = fVulkan;
	if (vk == NULL)
		return;
	fVulkan = NULL;
	// M11 review: a job that never ended; waiting for idle would block
	// app_server, freeing would hand the GPU's memory to someone else.
	if (atomic_get(&fGpuBusy) != 0) {
		_ReturnSharedImages();
		log_line("compositor device kept: the gpu is still busy");
		return;
	}

	// A lost device: WaitIdle does not return while the ring is stuck,
	// and vkDestroyDevice then runs while a window still has a recorder
	// (stick 61, VulkanResourceProvider). Leave the device in place.
	// The engines are already on the screen.
	if (fSkia != NULL && fSkia->Abandoned()) {
		_StopDrawThread();
		fSkia->DestroyContext();
		if (!fOnLavapipe)
			log_line("left radv");
		delete vk;
		fFrameWidth = 0;
		fFrameHeight = 0;
		return;
	}

	// A device that failed half way has some functions not loaded; they
	// are loaded in this order, so the last one tells all were.
	if (vk->device != VK_NULL_HANDLE && vk->vkDeviceWaitIdle != NULL) {
		SkiaGpu::LockQueue();
		vk->vkDeviceWaitIdle(vk->device);
		SkiaGpu::UnlockQueue();
		// M12: Skia's context and every GPU image of it, before the device
		if (fSkia != NULL) {
			_StopDrawThread();
			fSkia->DestroyContext();
		}
		_ScanoutFree(vk);
		// M11 step 3b: the imports of shared views go with this device;
		// the programs get a refusal at their next present. Step 5: the
		// images app_server holds go back first.
		_ReturnSharedImages();
		{
			BAutolock _(fShareLock);
			fVulkan = vk;
			for (int32 i = fShares.CountItems(); i-- > 0;) {
				Share* share = fShares.ItemAt(i);
				_ShareFree(share);
				fShares.RemoveItemAt(i);
				delete share;
			}
			fVulkan = NULL;
		}
		if (vk->frame != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->frame, NULL);
		if (vk->frameMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->frameMemory, NULL);
		if (vk->scratch != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->scratch, NULL);
		if (vk->scratchMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->scratchMemory, NULL);
		if (vk->slideOld != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->slideOld, NULL);
		if (vk->slideOldMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->slideOldMemory, NULL);
		if (vk->slideNew != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->slideNew, NULL);
		if (vk->slideNewMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->slideNewMemory, NULL);
		if (vk->effectsPipeline != VK_NULL_HANDLE)
			vk->vkDestroyPipeline(vk->device, vk->effectsPipeline, NULL);
		if (vk->effectsPipelineLayout != VK_NULL_HANDLE) {
			vk->vkDestroyPipelineLayout(vk->device, vk->effectsPipelineLayout,
				NULL);
		}
		if (vk->effectsSetLayout != VK_NULL_HANDLE) {
			vk->vkDestroyDescriptorSetLayout(vk->device, vk->effectsSetLayout,
				NULL);
		}
		if (vk->timestamps != VK_NULL_HANDLE)
			vk->vkDestroyQueryPool(vk->device, vk->timestamps, NULL);
		if (vk->blankView != VK_NULL_HANDLE)
			vk->vkDestroyImageView(vk->device, vk->blankView, NULL);
		if (vk->blankImage != VK_NULL_HANDLE)
			vk->vkDestroyImage(vk->device, vk->blankImage, NULL);
		if (vk->blankMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->blankMemory, NULL);
		if (vk->sampler != VK_NULL_HANDLE)
			vk->vkDestroySampler(vk->device, vk->sampler, NULL);
		if (vk->fence != VK_NULL_HANDLE)
			vk->vkDestroyFence(vk->device, vk->fence, NULL);
		if (vk->commandPool != VK_NULL_HANDLE)
			vk->vkDestroyCommandPool(vk->device, vk->commandPool, NULL);
		if (vk->pipeline != VK_NULL_HANDLE)
			vk->vkDestroyPipeline(vk->device, vk->pipeline, NULL);
		if (vk->pipelineLayout != VK_NULL_HANDLE)
			vk->vkDestroyPipelineLayout(vk->device, vk->pipelineLayout, NULL);
		if (vk->setLayout != VK_NULL_HANDLE)
			vk->vkDestroyDescriptorSetLayout(vk->device, vk->setLayout, NULL);
	}
	// M13 step 2: a window image still held (a closing window's fade)
	// must not reach a destroyed device
	SkiaGpu::KeepImageMemory();
	if (vk->device != VK_NULL_HANDLE && vk->vkDestroyDevice != NULL)
		vk->vkDestroyDevice(vk->device, NULL);
	if (vk->instance != VK_NULL_HANDLE && vk->vkDestroyInstance != NULL)
		vk->vkDestroyInstance(vk->instance, NULL);
	if (vk->drm != NULL)
		dlclose(vk->drm);
	if (vk->library != NULL)
		dlclose(vk->library);
	delete vk;
	fFrameWidth = 0;
	fFrameHeight = 0;
}


static bool
allocate_host_buffer(CompositorVulkan* vk, uint64 bytes, VkBuffer* _buffer,
	VkDeviceMemory* _memory, void** _bits)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bytes;
	// M13: the frame is also a transfer target (the image copy), and the
	// compare's copies are written by vkCmdCopyImageToBuffer
	bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
		| VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, _buffer)
			!= VK_SUCCESS)
		return false;

	VkMemoryRequirements requirements;
	vk->vkGetBufferMemoryRequirements(vk->device, *_buffer, &requirements);
	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = vk->memoryType;
	if ((requirements.memoryTypeBits & (1u << vk->memoryType)) == 0
		|| vk->vkAllocateMemory(vk->device, &allocateInfo, NULL, _memory)
			!= VK_SUCCESS) {
		vk->vkDestroyBuffer(vk->device, *_buffer, NULL);
		return false;
	}
	if (vk->vkBindBufferMemory(vk->device, *_buffer, *_memory, 0)
			!= VK_SUCCESS
		|| vk->vkMapMemory(vk->device, *_memory, 0, VK_WHOLE_SIZE, 0, _bits)
			!= VK_SUCCESS) {
		vk->vkDestroyBuffer(vk->device, *_buffer, NULL);
		vk->vkFreeMemory(vk->device, *_memory, NULL);
		return false;
	}
	return true;
}


/*!	One pass the way Painter writes a window: a fill (stores only) or a
	blend (each pixel read, mixed and written back). Returns the time of
	the faster of three runs. The sum keeps the stores from being dropped.
*/
static bigtime_t
time_write(uint32* bits, uint64 pixels, bool blend, volatile uint32* sink)
{
	bigtime_t best = B_INFINITE_TIMEOUT;
	for (int run = 0; run < 3; run++) {
		bigtime_t start = system_time();
		if (blend)
			r5_span_blend(bits, pixels);
		else {
			uint32 color = 0xff336699 + run;
			for (uint64 i = 0; i < pixels; i++)
				bits[i] = color;
		}
		bigtime_t took = system_time() - start;
		if (took < best)
			best = took;
		*sink += bits[pixels / 2];
	}
	return best;
}


/*!	M11 stick, gpu_debug: the window buffer memory against malloc, once,
	with the size of the first window buffer. A separate buffer, so no
	window shows the test pattern. Decision 5 wants the window buffer
	cached; an uncached one is many times slower here.
*/
static void
log_write_test(CompositorVulkan* vk, uint64 bytes)
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	void* bits;
	if (!allocate_host_buffer(vk, bytes, &buffer, &memory, &bits)) {
		log_line("window buffer write test skipped: no memory");
		return;
	}
	uint32* heap = (uint32*)malloc(bytes);
	if (heap == NULL) {
		vk->vkDestroyBuffer(vk->device, buffer, NULL);
		vk->vkFreeMemory(vk->device, memory, NULL);
		log_line("window buffer write test skipped: no memory");
		return;
	}
	uint64 pixels = bytes / 4;
	volatile uint32 sink = 0;
	bigtime_t bufferFill = time_write((uint32*)bits, pixels, false, &sink);
	bigtime_t bufferBlend = time_write((uint32*)bits, pixels, true, &sink);
	bigtime_t heapFill = time_write(heap, pixels, false, &sink);
	bigtime_t heapBlend = time_write(heap, pixels, true, &sink);
	free(heap);
	vk->vkDestroyBuffer(vk->device, buffer, NULL);
	vk->vkFreeMemory(vk->device, memory, NULL);
	log_line("window buffer write fill %" B_PRId64 " us blend %" B_PRId64
		" us, malloc fill %" B_PRId64 " us blend %" B_PRId64 " us, %"
		B_PRIu64 " KB", bufferFill, bufferBlend, heapFill, heapBlend,
		bytes / 1024);
}


WindowBuffer*
Compositor::_AllocateBuffer(uint32 width, uint32 height, bool withMemory)
{
	if (fVulkan == NULL || width == 0 || height == 0)
		return NULL;
	uint64 bytes = (uint64)width * height * 4;
	if (fWindowBytes + bytes > fBudget)
		return NULL;
	if (!withMemory) {
		// M13 step 2: a window that Skia draws; its image (made by the
		// engine, SkiaTarget::SetSurface()) is what counts in the window
		// memory, with the same size
		WindowBuffer* windowBuffer = new(std::nothrow) WindowBuffer(this,
			NULL, width, height, 0, 0, fGeneration);
		if (windowBuffer != NULL)
			atomic_add64((int64*)&fWindowBytes, bytes);
		return windowBuffer;
	}

	VkBuffer buffer;
	VkDeviceMemory memory;
	void* bits;
	if (!allocate_host_buffer(fVulkan, bytes, &buffer, &memory, &bits))
		return NULL;
	// Vulkan does not promise cleared memory: Lavapipe hands out reused
	// heap memory (RADV's GTT comes cleared from the kernel). Skia takes a
	// new buffer's pixels as the window's first image (SkiaTarget::
	// SetSurface()), so the old pixels showed until the window drew
	// (QEMU and the 5625U's boot 3, 16 Nov 2026). Transparent instead.
	memset(bits, 0, (size_t)bytes);

	WindowBuffer* windowBuffer = new(std::nothrow) WindowBuffer(this, bits,
		width, height, (uint64)buffer, (uint64)memory, fGeneration);
	if (windowBuffer == NULL) {
		fVulkan->vkDestroyBuffer(fVulkan->device, buffer, NULL);
		fVulkan->vkFreeMemory(fVulkan->device, memory, NULL);
		return NULL;
	}
	atomic_add64((int64*)&fWindowBytes, bytes);

	static bool sLogged = false;
	if (!sLogged) {
		sLogged = true;
		VkPhysicalDeviceMemoryProperties memory;
		fVulkan->vkGetPhysicalDeviceMemoryProperties(fVulkan->physical,
			&memory);
		log_line("window memory type %" B_PRIu32 " flags %#" B_PRIx32,
			fVulkan->memoryType,
			(uint32)memory.memoryTypes[fVulkan->memoryType].propertyFlags);
		if (fDebug)
			log_write_test(fVulkan, bytes);
	}
	return windowBuffer;
}


void
Compositor::_FreeBuffer(WindowBuffer* buffer, uint64 vulkanBuffer,
	uint64 vulkanMemory)
{
	// Review after M11: a buffer kept because the GPU may still use it
	// (fGpuBusy) still counts in the window memory.
	if (atomic_get(&fGpuBusy) != 0)
		return;
	atomic_add64((int64*)&fWindowBytes, -(int64)buffer->Bytes());
	if (fVulkan == NULL || buffer->Generation() != fGeneration
		|| vulkanBuffer == 0)
		return;
	fVulkan->vkDestroyBuffer(fVulkan->device, (VkBuffer)vulkanBuffer, NULL);
	fVulkan->vkFreeMemory(fVulkan->device, (VkDeviceMemory)vulkanMemory,
		NULL);
}


/*!	Allocates a window buffer. Over the budget, the buffers of windows that
	are not on screen now (hidden, minimised, on another workspace) go
	first, the one shown longest ago first; such a window draws all of
	itself again when it shows. Called with all windows and the entry list
	locked. A window without a buffer draws nothing: its drawable regions
	are empty (Window::_ClipToBuffer).
*/
WindowBuffer*
Compositor::_AllocateOrMakeRoom(uint32 width, uint32 height,
	const Entry* keep, bool memory)
{
	WindowBuffer* buffer = _AllocateBuffer(width, height, memory);
	int32 workspace = fDesktop->CurrentWorkspace();
	while (buffer == NULL) {
		Entry* oldest = NULL;
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (entry == keep || entry->iface->Buffer() == NULL
				|| entry->window == NULL)
				continue;
			// only windows that are not on screen now
			if (!entry->window->IsHidden()
				&& entry->window->InWorkspace(workspace))
				continue;
			if (oldest == NULL || entry->lastShown < oldest->lastShown)
				oldest = entry;
		}
		if (oldest == NULL)
			return NULL;
		oldest->iface->SetBuffer(NULL, 0, 0);
		if (SkiaDrawingEngine* skia = dynamic_cast<SkiaDrawingEngine*>(
				oldest->window->GetDrawingEngine()))
			skia->ReleaseBuffer();
		// M11 review: the last layer list still holds it, or nothing is
		// freed and every off-screen window loses its buffer for nothing.
		// The compose skips a layer without a buffer; a frame in flight
		// keeps its own reference until its fence.
		{
			BAutolock layerLock(fLayerLock);
			for (int32 i = 0; i < fLayers->CountItems(); i++) {
				Layer* layer = fLayers->ItemAt(i);
				if (layer->entry.Get() == oldest)
					layer->buffer.Unset();
			}
		}
		// M11 review: a closing window's fade holds it too; it ends early.
		// (A frame in flight still holds it until its fence: near the
		// budget that can still end the GPU mode.)
		BRegion faded;
		{
			BAutolock animLock(fAnimLock);
			for (int32 i = fGhosts.CountItems(); i-- > 0;) {
				Ghost* ghost = fGhosts.ItemAt(i);
				if (ghost->entry.Get() != oldest)
					continue;
				faded.Include(&ghost->area);
				delete fGhosts.RemoveItemAt(i);
			}
		}
		Damage(faded);
		buffer = _AllocateBuffer(width, height, memory);
	}
	return buffer;
}


bool
Compositor::_EnsureFrame(uint32 width, uint32 height)
{
	CompositorVulkan* vk = fVulkan;
	if (width == fFrameWidth && height == fFrameHeight && vk->frame != 0)
		return true;

	if (vk->frame != VK_NULL_HANDLE) {
		vk->vkDestroyBuffer(vk->device, vk->frame, NULL);
		vk->vkFreeMemory(vk->device, vk->frameMemory, NULL);
		vk->frame = VK_NULL_HANDLE;
		vk->frameMemory = VK_NULL_HANDLE;
	}
	free(fScratch);
	fScratch = NULL;
	fFrameWidth = 0;
	fFrameHeight = 0;

	void* bits;
	if (!allocate_host_buffer(vk, (uint64)width * height * 4, &vk->frame,
			&vk->frameMemory, &bits))
		return false;
	vk->frameBits = (uint32*)bits;
	fFrameWidth = width;
	fFrameHeight = height;
	if (fDebug)
		fScratch = (uint32*)malloc((size_t)width * height * 4);
	_UpdatePeriod();
	return true;
}


/*!	One refresh period of the current display mode. Without a pixel clock
	there is no period, and the compose runs as soon as damage arrives.
*/
void
Compositor::_UpdatePeriod()
{
	display_mode mode;
	memset(&mode, 0, sizeof(mode));
	fScreen->GetMode(&mode);
	// A timing that gives no display refresh counts as none (VirtualBox:
	// pixel clock 60 read as kHz, a 15 s period that held every mode change
	// and every frame; compose_period.h).
	bigtime_t period = compose_period(mode.timing.pixel_clock,
		mode.timing.h_total, mode.timing.v_total);
	if (period == 0) {
		static bool sLogged = false;
		if (!sLogged) {
			sLogged = true;
			log_line("compose period unknown: pixel clock %" B_PRIu32
				" kHz, total %u x %u", mode.timing.pixel_clock,
				(unsigned)mode.timing.h_total, (unsigned)mode.timing.v_total);
		}
	}
	fPeriod = period;
}


//	#pragma mark - windows


DrawingEngine*
Compositor::CreateWindowEngine(WindowHWInterface** _iface)
{
	WindowHWInterface* iface = new(std::nothrow) WindowHWInterface(this);
	if (iface == NULL || iface->Initialize() != B_OK) {
		delete iface;
		return NULL;
	}
	DrawingEngine* engine;
	if ((fSkia != NULL && !fSkia->IsShutDown()) || fDebug) {
		// M12: the GPU draws this window while it is bound to its buffer;
		// without gpu_draw (fSkia NULL) the same engine is Painter and only
		// measures with gpu_debug (decision 9)
		engine = new(std::nothrow) SkiaDrawingEngine(iface,
			fSkia != NULL && !fSkia->IsShutDown() ? fSkia : NULL, this);
	} else
		engine = iface->CreateDrawingEngine();
	if (engine == NULL) {
		delete iface;
		return NULL;
	}
	*_iface = iface;
	return engine;
}


void
Compositor::AddWindow(Window* window, WindowHWInterface* iface)
{
	DrawingEngine* engine = window->GetDrawingEngine();
	Entry* entry = new(std::nothrow) Entry(window, iface);
	BAutolock _(fEntryLock);
	if (entry == NULL || !fEntries.AddItem(entry)) {
		// M11 review: no entry, no buffer; the engine must not keep the
		// interface ~Entry deletes. The window draws to the screen.
		if (engine != NULL)
			engine->SetHWInterface(fScreen);
		if (entry != NULL)
			entry->ReleaseReference();
		else
			delete iface;
		return;
	}
	// M11 step 5: made during a pause; the resume binds it to its buffer.
	// M11 review: or after the switch to the CPU mode, which rebound only
	// the windows it had. fActive and fPaused change under fEntryLock.
	if ((!fActive || fPaused) && engine != NULL)
		engine->SetHWInterface(fScreen);
}


void
Compositor::RemoveWindow(Window* window)
{
	BAutolock _(fEntryLock);
	// M11 step 3b: the window's shared views end with it. M11 review:
	// also a window without an entry (an offscreen window, a failed
	// engine), or a share would keep the freed Window.
	bool ended = false;
	{
		BAutolock shareLock(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			if (share->window == window) {
				if (!share->ended)
					share->giveBack = true;
				share->ended = true;
				share->window = NULL;
				ended = true;
			}
		}
	}
	// M11 review: a hidden view has no damage; wake the frame that
	// gives its images back and frees its imports
	if (ended)
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	Entry* entry = _FindEntry(window);
	if (entry == NULL)
		return;
	fEntries.RemoveItem(entry);
	entry->window = NULL;
	entry->ReleaseReference();

	// M11 step 5: it cannot hold direct access any more
	if (fDirectWindows.RemoveItem(window) && fDirectWindows.IsEmpty()) {
		fModePending = true;
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	}
}


bool
Compositor::BufferBounds(Window* window, clipping_rect* _bounds)
{
	BAutolock _(fEntryLock);
	Entry* entry = _FindEntry(window);
	if (entry == NULL || entry->iface->Buffer() == NULL)
		return false;
	WindowBuffer* buffer = entry->iface->Buffer();
	_bounds->left = entry->iface->Left();
	_bounds->top = entry->iface->Top();
	_bounds->right = _bounds->left + (int32)buffer->Width() - 1;
	_bounds->bottom = _bounds->top + (int32)buffer->Height() - 1;
	return true;
}


Compositor::Entry*
Compositor::_FindEntry(Window* window) const
{
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window == window)
			return entry;
	}
	return NULL;
}


/*!	Called at the end of every clipping rebuild, with all windows locked.
	Allocates the buffer of a window on its first show and on a resize,
	moves the Painter view on a move, and damages what changed on screen.
*/
void
Compositor::ClippingChanged()
{
	if (!IsActive())
		return;

	LayerList* layers = new(std::nothrow) LayerList(20);
	if (layers == NULL)
		return;

	BObjectList<Window, false> fresh(10);
	BRegion damage;
	int32 workspace = fDesktop->CurrentWorkspace();
	// M11 review: a window that was not on screen in the last pass
	// (hidden, another workspace) was resized without a redraw.
	bigtime_t previousPass = fClipPassTime;
	fClipPassTime = system_time();

	// M11 step 4b: a workspace switch slides; it opens and closes nothing
	bool animations = (fEffects & kWindowEffectAnimations) != 0;
	bool switched = fLastWorkspace >= 0 && workspace != fLastWorkspace;
	if (switched && animations) {
		BAutolock _(fAnimLock);
		fSlideDirection = workspace > fLastWorkspace ? 1 : -1;
		fSlideStart = system_time();
		fSlideSnapshot = true;
		fSlideFrames = 0;
		fSlideActive = true;
	}
	fLastWorkspace = workspace;

	fEntryLock.Lock();
	for (Window* window = fDesktop->CurrentWindows().FirstWindow();
			window != NULL; window = window->NextWindow(workspace)) {
		if (window->IsHidden() || window->IsOffscreenWindow())
			continue;
		Entry* entry = _FindEntry(window);
		if (entry == NULL)
			continue;

		BRegion full;
		window->GetFullRegion(&full);
		// M13 step 4 (owner, 17 Nov 2026): the outline of a resize on
		// Lavapipe is not the window's: its buffer and shape stay the
		// window's own, so nothing is reallocated or redrawn while the
		// outline moves; _RecordOutline() draws it over the window
		BRegion outline;
		::Decorator* outlined = window->Decorator();
		if (outlined != NULL && outlined->IsOutlineResizing()) {
			outlined->GetOutlineRegion(&outline);
			outlined->GetFootprintWithoutOutline(&full);
			full.Include(window->Frame());
		}
		if (!full.Frame().IsValid())
			continue;
		clipping_rect bounds = full.FrameInt();
		uint32 width = bounds.right - bounds.left + 1;
		uint32 height = bounds.bottom - bounds.top + 1;

		WindowHWInterface* iface = entry->iface;
		WindowBuffer* buffer = iface->Buffer();
		bool firstBuffer = buffer == NULL;
		bool wasOnScreen = entry->lastShown >= previousPass;
		if (buffer == NULL || buffer->Width() != width
			|| buffer->Height() != height) {
			// M13 step 2: a window that Skia draws gets no memory; the
			// compositor samples its image
			bool skiaWindow = fSkia != NULL && !fSkia->IsShutDown()
				&& dynamic_cast<SkiaDrawingEngine*>(
					window->GetDrawingEngine()) != NULL;
			WindowBuffer* newBuffer = _AllocateOrMakeRoom(width, height,
				entry, !skiaWindow);
			if (newBuffer == NULL) {
				fEntryLock.Unlock();
				delete layers;
				// Not enough room for a window on this workspace: the
				// CPU mode for the rest of the session. The compose
				// thread switches, because that takes the window lock.
				BAutolock damageLock(fDamageLock);
				fOutOfMemory = true;
				release_sem(fWakeSem);
				return;
			}
			if (buffer != NULL) {
				// Keep what was drawn where the two overlap on screen; the
				// window draws the new parts itself. The overlap is found
				// in screen coordinates, so a window that grows to the left
				// or to the top (an outline, an edge drag) keeps its pixels
				// in place.
				int32 left = max_c(bounds.left, iface->Left());
				int32 top = max_c(bounds.top, iface->Top());
				int32 right = min_c(bounds.right,
					iface->Left() + (int32)buffer->Width() - 1);
				int32 bottom = min_c(bounds.bottom,
					iface->Top() + (int32)buffer->Height() - 1);
				if (newBuffer->Bits() == NULL || buffer->Bits() == NULL)
					bottom = top - 1;
				// M13 step 2: the new buffer shows the old image, at its
				// place on screen, until the window's new one is drawn
				int32 imageX;
				int32 imageY;
				SkiaImage* image = buffer->AcquireImage(&imageX, &imageY);
				if (image != NULL) {
					newBuffer->SetImage(image,
						iface->Left() + imageX - bounds.left,
						iface->Top() + imageY - bounds.top);
					image->ReleaseReference();
				}
				for (int32 y = top; y <= bottom && left <= right; y++) {
					memcpy((uint8*)newBuffer->Bits()
							+ (y - bounds.top) * newBuffer->BytesPerRow()
							+ (left - bounds.left) * 4,
						(uint8*)buffer->Bits()
							+ (y - iface->Top()) * buffer->BytesPerRow()
							+ (left - iface->Left()) * 4,
						(right - left + 1) * 4);
				}
				// M11 review: its new parts got no redraw while it was
				// off screen; _ShowWindow redraws only what is visible.
				if (!wasOnScreen)
					fresh.AddItem(window);
			} else {
				fresh.AddItem(window);
			}
			iface->SetBuffer(newBuffer, bounds.left, bounds.top);
			newBuffer->ReleaseReference();
		} else if (iface->Left() != bounds.left || iface->Top() != bounds.top)
			iface->SetBuffer(buffer, bounds.left, bounds.top);

		entry->lastShown = system_time();
		Layer* layer = new(std::nothrow) Layer;
		if (layer == NULL)
			continue;
		layer->entry.SetTo(entry);
		layer->buffer.SetTo(iface->Buffer());
		layer->left = bounds.left;
		layer->top = bounds.top;
		layer->shape = full;
		layer->outline = outline;

		// M11 step 4: the shadow and the glass of this window, from its
		// decorator's hooks (decision 10) and the glass views.
		uint32 effects = fEffects;
		int32 strength = fStrength;
		::Decorator* decorator = window->Decorator();
		bool menu = window->Feel() == kMenuWindowFeel;
		if ((effects & kWindowEffectShadow) != 0
			&& (decorator != NULL || menu)) {
			layer->shadowSmall = !window->IsFocus() || menu;
			window_shadow_geometry shadow = window_shadow_for(strength,
				layer->shadowSmall);
			int32 extentLeft;
			int32 extentTop;
			int32 extentRight;
			int32 extentBottom;
			if (decorator != NULL) {
				BRect extent;
				decorator->GetShadowExtent(&extent);
				extentLeft = (int32)extent.left;
				extentTop = (int32)extent.top;
				extentRight = (int32)extent.right;
				extentBottom = (int32)extent.bottom;
			} else {
				window_shadow_extent(strength, &extentLeft, &extentTop,
					&extentRight, &extentBottom);
			}
			clipping_rect area;
			area.left = max_c(bounds.left - shadow.reach,
				bounds.left - extentLeft);
			area.top = max_c(bounds.top - shadow.reach + shadow.offset,
				bounds.top - extentTop);
			area.right = min_c(bounds.right + shadow.reach,
				bounds.right + extentRight);
			area.bottom = min_c(bounds.bottom + shadow.reach + shadow.offset,
				bounds.bottom + extentBottom);
			if (area.left <= area.right && area.top <= area.bottom) {
				layer->shadowArea.Set(area);
				layer->shadowArea.Exclude(&full);
			}
			layer->shadow = layer->shadowArea.CountRects() > 0;
		}
		if ((effects & kWindowEffectFrostedGlass) != 0) {
			if (decorator != NULL) {
				decorator->GetGlassRegion(&layer->glassFixed);
				layer->glassFixed.IntersectWith(&full);
			}
			// A window with a glass view uses the alpha of its content;
			// only glass views are filled with alpha 0 (View::Draw).
			if (entry->hasGlassViews) {
				window->GetContentRegion(&layer->glassAlpha);
				layer->glassAlpha.IntersectWith(&full);
				layer->glassAlpha.Exclude(&layer->glassFixed);
			}
		}

		// M11 step 4b: a window that shows for the first time opens
		layer->animatable = decorator != NULL && !menu
			&& !window->ServerWindow()->IsDirectlyAccessing();
		if (firstBuffer && animations && !switched && layer->animatable) {
			BAutolock _(fAnimLock);
			entry->animOpen = true;
			entry->animStart = system_time();
			entry->animFrom = 0;
			entry->animFrames = 0;
			entry->animArea = layer->shape;
			entry->animArea.Include(&layer->shadowArea);
		} else if (!firstBuffer) {
			// A window that grows or moves while it opens (a layout after
			// its first show): the last frame must cover where it is now.
			BAutolock _(fAnimLock);
			if (entry->animOpen) {
				entry->animArea.Include(&layer->shape);
				entry->animArea.Include(&layer->shadowArea);
			}
		}
		layers->AddItem(layer);
	}
	fEntryLock.Unlock();

	// M11 step 3b: where each shared view is now. A view that moved or
	// changed its shape is damaged where it was and where it is.
	{
		BAutolock shareLock(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			BRegion before = share->shape;
			int32 left = share->left;
			int32 top = share->top;
			_ShareShape(share);
			if (before == share->shape && left == share->left
				&& top == share->top)
				continue;
			damage.Include(&before);
			damage.Include(&share->shape);
		}
	}

	// What changed on screen: a window that came, went, moved, changed
	// shape or changed its place in the stack. M11 step 4: with its
	// shadow, and told to the compose as that window's own change, so the
	// glass of the windows above it is blurred again there; a window that
	// went is nobody's.
	struct WindowDamage {
		WindowHWInterface*	iface;
		BRegion				region;
	};
	BObjectList<WindowDamage, true> windowDamage(10);
	fLayerLock.Lock();
	LayerList* old = fLayers;
	int32 count = max_c(old->CountItems(), layers->CountItems());
	for (int32 i = 0; i < count; i++) {
		Layer* before = old->ItemAt(i);
		Layer* after = layers->ItemAt(i);
		if (before != NULL && after != NULL
			&& before->entry.Get() == after->entry.Get()
			&& before->buffer.Get() == after->buffer.Get()
			&& before->left == after->left && before->top == after->top
			&& before->shape == after->shape
			&& before->shadow == after->shadow
			&& before->shadowSmall == after->shadowSmall
			&& before->shadowArea == after->shadowArea
			&& before->glassFixed == after->glassFixed
			&& before->glassAlpha == after->glassAlpha) {
			// M13 step 4 (review): a step of the outline alone damages
			// where it was and where it is, not the window
			if (!(before->outline == after->outline)) {
				damage.Include(&before->outline);
				damage.Include(&after->outline);
			}
			continue;
		}
		for (int32 side = 0; side < 2; side++) {
			Layer* layer = side == 0 ? before : after;
			if (layer == NULL)
				continue;
			BRegion area(layer->shape);
			area.Include(&layer->shadowArea);
			// M13 step 4: where the outline was and where it is
			area.Include(&layer->outline);
			bool stays = false;
			for (int32 j = 0; !stays && j < layers->CountItems(); j++)
				stays = layers->ItemAt(j)->entry.Get() == layer->entry.Get();
			// M11 step 4b: a window that closed (not one that went to
			// Deskbar or to another workspace) fades and shrinks away
			Window* gone = layer->entry->window;
			if (side == 0 && !stays && animations && !switched
				&& layer->animatable && layer->buffer.IsSet()
				&& (gone == NULL
					|| (gone->IsHidden() && !gone->IsMinimized()))) {
				Ghost* ghost = new(std::nothrow) Ghost;
				if (ghost != NULL) {
					BAutolock _(fAnimLock);
					Entry* entry = layer->entry.Get();
					ghost->entry = layer->entry;
					ghost->buffer = layer->buffer;
					ghost->left = layer->left;
					ghost->top = layer->top;
					ghost->shape = layer->shape;
					ghost->area = area;
					ghost->start = system_time();
					// from where a running open animation is
					ghost->from = 1.0f;
					if (entry->animOpen) {
						ghost->from = entry->animFrom + (1.0f - entry->animFrom)
							* ease_out((float)(ghost->start - entry->animStart)
								/ kOpenAnimation);
						entry->animOpen = false;
					}
					ghost->visible = ghost->from;
					ghost->frames = 0;
					if (!fGhosts.AddItem(ghost))
						delete ghost;
				}
			}
			WindowDamage* item = stays ? new(std::nothrow) WindowDamage
				: NULL;
			if (item != NULL) {
				item->iface = layer->entry->iface;
				item->region = area;
				if (!windowDamage.AddItem(item)) {
					delete item;
					item = NULL;
				}
			}
			if (item == NULL)
				damage.Include(&area);
		}
	}
	fLayers = layers;
	fLayerLock.Unlock();
	// old still holds its entries, so each interface is alive here
	for (int32 i = 0; i < windowDamage.CountItems(); i++) {
		WindowDamage* item = windowDamage.ItemAt(i);
		Damage(item->region, item->iface);
	}
	delete old;

	// A window that got its first buffer draws all of it once.
	for (int32 i = 0; i < fresh.CountItems(); i++) {
		Window* window = fresh.ItemAt(i);
		BRegion full;
		window->GetFullRegion(&full);
		window->ProcessDirtyRegion(full, full);
	}

	Damage(damage);
}


void
Compositor::SetBackgroundColor(rgb_color color)
{
	uint32 pixel = 0xff000000 | ((uint32)color.red << 16)
		| ((uint32)color.green << 8) | color.blue;
	if (pixel == fBackground)
		return;
	fBackground = pixel;
	BRegion screen(BRect(0, 0, (float)fFrameWidth - 1,
		(float)fFrameHeight - 1));
	Damage(screen);
}


void
Compositor::Damage(const BRegion& region)
{
	Damage(region, NULL);
}


void
Compositor::Damage(const BRegion& region, WindowHWInterface* source)
{
	if (!IsActive() || region.CountRects() == 0)
		return;
	BAutolock _(fDamageLock);
	fDamage.Include(&region);
	// M13 step 0: the mouse move this damage answers, if it is recent.
	// Use a 50 ms window so an idle mouse move is not attributed to an
	// unrelated 250 ms cursor blink.
	int64 input = atomic_get_and_set64(&sInputWhen, 0);
	if (input != 0 && fFrameInput == 0 && system_time() - input < 50000)
		fFrameInput = input;
	// M11 step 4: who changed it, for the kept blur of the glass
	if (source != NULL)
		source->ContentDamage().Include(&region);
	else
		fGenericDamage.Include(&region);
	if (!fWakePending) {
		fWakePending = true;
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	}
}


//	#pragma mark - window effects (M11 step 4)


void
Compositor::SetInitialEffects(uint32 effects, int32 strength)
{
	fEffects = effects & kWindowEffectsAll;
	fStrength = window_effect_strength_valid(strength);
}


/*!	All windows locked. The layers get their new shadow and glass, the
	whole screen is composed again, and windows with glass views draw their
	background again (alpha 0 only while frosted glass is on).
*/
void
Compositor::SetEffects(uint32 effects, int32 strength)
{
	effects &= kWindowEffectsAll;
	strength = window_effect_strength_valid(strength);
	bool glassChanged = ((fEffects ^ effects) & kWindowEffectFrostedGlass)
		!= 0;
	fEffects = effects;
	fStrength = strength;
	if (!IsActive())
		return;

	ClippingChanged();
	if (glassChanged) {
		BObjectList<Window, false> redraw(10);
		fEntryLock.Lock();
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (entry->hasGlassViews && entry->window != NULL)
				redraw.AddItem(entry->window);
		}
		fEntryLock.Unlock();
		for (int32 i = 0; i < redraw.CountItems(); i++) {
			BRegion content;
			redraw.ItemAt(i)->GetContentRegion(&content);
			redraw.ItemAt(i)->ProcessDirtyRegion(content, content);
		}
	}
	BRegion screen(BRect(0, 0, (float)fFrameWidth - 1,
		(float)fFrameHeight - 1));
	Damage(screen);
}


bool
Compositor::GlassActive() const
{
	return IsActive() && (fEffects & kWindowEffectFrostedGlass) != 0;
}


//!	All windows locked: a view of \a window was marked or unmarked as glass.
void
Compositor::GlassViewChanged(Window* window, View* view)
{
	(void)view;
	if (!IsActive() || window == NULL)
		return;
	bool hasGlass = window->TopView() != NULL
		&& window->TopView()->HasGlass();
	fEntryLock.Lock();
	Entry* entry = _FindEntry(window);
	if (entry != NULL)
		entry->hasGlassViews = hasGlass;
	fEntryLock.Unlock();
	// M12: a window with glass has pixels with alpha below 255; its GPU
	// drawing then always goes through the R5 shader
	SkiaDrawingEngine* skia
		= dynamic_cast<SkiaDrawingEngine*>(window->GetDrawingEngine());
	if (skia != NULL)
		skia->SetOpaque(!hasGlass);

	if (GlassActive()) {
		BRegion content;
		window->GetContentRegion(&content);
		window->ProcessDirtyRegion(content, content);
	}
	ClippingChanged();
}


//	#pragma mark - compose thread


status_t
Compositor::_ComposeThread(void* data)
{
	Compositor* compositor = (Compositor*)data;
	compositor->_ComposeLoop();
	if (!compositor->fActive)
		compositor->_ExitVulkan();
	return B_OK;
}


void
Compositor::_ComposeLoop()
{
	fCpuAtReport = thread_cpu_time();
	fTeamCpuAtReport = team_cpu_time();

	while (true) {
		// A compose that failed, outside the compose slot: the switch to
		// the CPU mode and the restart take the window lock (step 5).
		if (fFailure != NULL) {
			const char* failure = fFailure;
			fFailure = NULL;
			// one restart after a successful reset (RADV only). M12: not
			// while Skia draws; its images went with the device, so the
			// GPU drawing goes off (decision 8; step 6 brings the ladder).
			if (!fOnLavapipe && !fRestarted && fSkia == NULL
				&& strcmp(failure, "device lost") == 0
				&& _RestartAfterReset()) {
				Damage(BRegion(BRect(0, 0, (float)fFrameWidth - 1,
					(float)fFrameHeight - 1)));
				continue;
			}
			_TurnOff(failure);
			return;
		}

		// An overtaken image goes back once the GPU is done with it; until
		// then the thread looks again every refresh period, also without
		// damage (a hidden window).
		bigtime_t wait = fDebug ? kReportInterval : B_INFINITE_TIMEOUT;
		// M11 step 4b: an animation composes every refresh period
		if (fShareRetiring || fAnimationsRunning || fSplashHolding) {
			bigtime_t slice = fPeriod > 0 ? fPeriod : 16667;
			if (wait == B_INFINITE_TIMEOUT || slice < wait)
				wait = slice;
		}
		// M13 step 4: a stretched shadow mask is blurred again when its
		// window stays the same, also without new damage
		if (fShadowDue > 0 && !fPaused) {
			bigtime_t until = max_c(fShadowDue - system_time(), 1);
			if (wait == B_INFINITE_TIMEOUT || until < wait)
				wait = until;
		}
		status_t status = acquire_sem_etc(fWakeSem, 1, B_RELATIVE_TIMEOUT,
			wait);
		if (status != B_OK && status != B_TIMED_OUT)
			return;
		if (fDebug)
			_Report();
		else
			_ReadZeroCopySwitch();

		// app_server ends: no switch, the desktop is going away
		if (fQuit)
			return;
		if (fOutOfMemory) {
			_TurnOff("window memory");
			return;
		}
		_ShareRetireDone();
		// M12: Skia's work runs on the draw thread (_DrawLoop); only when
		// that thread could not be made does the compose thread run it
		// before the compose. A failure turns the GPU drawing off.
		if (fDrawThread < 0 && fSkia != NULL && !fSkia->IsShutDown()) {
			const char* drawReason = NULL;
			if (!fSkia->RunWork(&drawReason)) {
				fDrawFailure = true;
				fFailure = drawReason != NULL ? drawReason : "draw";
				continue;
			}
		}
		// M12, decision 9: Painter's drawing sessions end at a compose
		SkiaDrawingEngine::CloseCpuSessions();
		// M11 step 5: the pause's redraw, and the resume
		if (fModePending) {
			bool wasPaused = fPaused;
			_ApplyMode();
			// M12: the resume binds every window to a buffer again, which
			// still holds other pixels; Skia's next run fills it with the
			// whole window (WindowTarget::SetBuffer()). Compose after that
			// run, not now, or the old pixels show for a frame.
			if (wasPaused && !fPaused && fSkia != NULL
				&& !fSkia->IsShutDown()) {
				if (fDrawThread >= 0) {
					// one run of the draw thread, at most 250 ms; a slower
					// run does not keep the screen from coming back
					fSkia->Finish(250000);
				} else {
					release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
					continue;
				}
			}
		}
		if (fPaused) {
			// M11 review: no frame runs while paused, but a program whose
			// window app_server ended still waits for its images. The
			// last frame waited for its fence, so none is read. Third
			// review: their imports go now too, not at the resume.
			// Fourth review: the last frame's closing windows go too, or
			// their buffers stay held for the whole pause.
			delete fFrameGhosts;
			fFrameGhosts = NULL;
			BAutolock _(fShareLock);
			_ShareRetire();
			continue;
		}
		bool animating = _AnimationDamage(system_time());
		bool settling = _ShadowSettleDamage(system_time());
		if (status == B_TIMED_OUT && !animating && !settling
			&& !fSplashHolding)
			continue;

		// At most once per refresh period. The wait is outside the compose
		// slot, so a mode change (SetMode takes the slot) never waits for
		// it; the damage is taken after it, so nothing waits unseen.
		// M13 step 4: on a fixed grid of periods, so the time this thread
		// spends before a compose no longer pushes every next frame later.
		// A slot that already began and has not composed goes at once (as
		// a frame after a pause did); a slot composes once at most.
		int64 gridSlot = -1;
		if (fPeriod > 0) {
			bigtime_t now = system_time();
			if (fGridPeriod != fPeriod || fGridEpoch == 0) {
				fGridPeriod = fPeriod;
				fGridEpoch = now;
				fGridSlot = -1;
			}
			gridSlot = (now - fGridEpoch) / fGridPeriod;
			if (gridSlot <= fGridSlot)
				gridSlot = fGridSlot + 1;
			bigtime_t next = fGridEpoch + gridSlot * fGridPeriod;
			if (now < next)
				snooze_until(next, B_SYSTEM_TIMEBASE);
		}

		// One frame, in the compose slot: a pause waits for it to end.
		BAutolock slot(fComposeSlot);
		if (fPaused) {
			// Fourth review: the pause began after the check above; the
			// ended shares still go now, not at the resume.
			slot.Unlock();
			delete fFrameGhosts;
			fFrameGhosts = NULL;
			BAutolock _(fShareLock);
			_ShareRetire();
			continue;
		}

		BRegion damage;
		BRegion generic;
		bigtime_t frameInput = 0;
		{
			// M11 step 4: with it, what each window changed itself
			// (WindowHWInterface::ContentDamage) and what nobody did.
			BAutolock entryLock(fEntryLock);
			BAutolock _(fDamageLock);
			fWakePending = false;
			damage = fDamage;
			fDamage.MakeEmpty();
			frameInput = fFrameInput;
			fFrameInput = 0;
			generic = fGenericDamage;
			fGenericDamage.MakeEmpty();
			for (int32 i = 0; i < fEntries.CountItems(); i++) {
				Entry* entry = fEntries.ItemAt(i);
				entry->takenDamage = entry->iface->ContentDamage();
				entry->iface->ContentDamage().MakeEmpty();
			}
		}

		// the frame follows the screen size (a mode change)
		RenderingBuffer* back = fScreen->BackBuffer();
		if (back == NULL)
			continue;
		if (back->Width() != fFrameWidth || back->Height() != fFrameHeight) {
			if (!_EnsureFrame(back->Width(), back->Height())) {
				fFailure = "window memory";
				continue;
			}
			log_line("frame follows the screen %" B_PRIu32 "x%" B_PRIu32
				", period %" B_PRId64 " us", fFrameWidth, fFrameHeight,
				fPeriod);
			damage.Set(BRect(0, 0, (float)fFrameWidth - 1,
				(float)fFrameHeight - 1));
		}
		// Decision 4: between two composes the GPU is idle (each one waits
		// for its fence), so a new back buffer or scanout is imported here.
		if (_ScanoutPrepare(back))
			damage.Set(BRect(0, 0, (float)fFrameWidth - 1,
				(float)fFrameHeight - 1));
		BRegion screen(BRect(0, 0, (float)fFrameWidth - 1,
			(float)fFrameHeight - 1));
		// Start splash: the windows draw unseen; each frame redraws the
		// picture where it moves
		bool splash = fSplashHolding;
		if (splash) {
			// a window's new pixels: the screen counts as built only after
			// a quiet spell
			if (damage.CountRects() > 0)
				fSplashChanged = system_time();
			BRegion area(fSplashArea);
			if (fFrames == 0)
				area = screen;
			damage = area;
		}
		damage.IntersectWith(&screen);
		if (damage.CountRects() == 0) {
			// Fourth review: the wake of an ended share has no damage when
			// its view is hidden; what ended is still freed and given back.
			BAutolock _(fShareLock);
			_ShareRetire();
			continue;
		}

		LayerList layers(20);
		{
			BAutolock _(fLayerLock);
			for (int32 i = 0; i < fLayers->CountItems(); i++) {
				Layer* source = fLayers->ItemAt(i);
				Layer* copy = new(std::nothrow) Layer;
				if (copy == NULL)
					continue;
				copy->entry = source->entry;
				copy->buffer = source->buffer;
				copy->left = source->left;
				copy->top = source->top;
				copy->shape = source->shape;
				copy->shadow = source->shadow;
				copy->shadowSmall = source->shadowSmall;
				copy->shadowArea = source->shadowArea;
				copy->outline = source->outline;
				copy->glassFixed = source->glassFixed;
				copy->glassAlpha = source->glassAlpha;
				copy->animatable = source->animatable;
				// M13 step 2: the image this frame samples, kept until its
				// fence even when the window shows a newer one meanwhile
				if (copy->buffer.IsSet()) {
					copy->image.SetTo(copy->buffer->AcquireImage(
						&copy->imageX, &copy->imageY), true);
				}
				layers.AddItem(copy);
			}
		}
		bool effects = false;
		{
			BAutolock _(fEntryLock);
			// M11 third review: a window whose buffer was taken to make
			// room gives its shadow mask and glass back too; they count
			// in the window memory. Only this thread uses them.
			for (int32 i = 0; i < fEntries.CountItems(); i++) {
				Entry* entry = fEntries.ItemAt(i);
				if (entry->iface != NULL && entry->iface->Buffer() == NULL
					&& (entry->mask.IsSet() || entry->glass.IsSet())) {
					entry->mask.Unset();
					entry->glass.Unset();
					entry->glassValid = false;
				}
			}
			for (int32 i = 0; i < layers.CountItems(); i++) {
				Layer* layer = layers.ItemAt(i);
				layer->contentDamage = layer->entry->takenDamage;
				layer->entry->takenDamage.MakeEmpty();
				effects |= layer->HasEffects();
			}
		}
		// M11 step 4b: where each animation is in this frame
		_AnimationFrame(layers, system_time());

		if (splash) {
			const char* reason = NULL;
			if (_SplashDone(layers, system_time(), &reason)) {
				log_line("start splash %" B_PRId64 " ms (%s)",
					(system_time() - fSplashStart) / 1000, reason);
				splash = false;
				_EndSplash();
				// the windows were never shown: all of the screen, and
				// the glass blurred again everywhere
				damage = screen;
				generic = screen;
			}
		}
		if (splash) {
			// the picture instead of the windows; their animations and
			// closing windows go on unseen
			LayerList pictures(3);
			_SplashLayers(pictures, system_time());
			LayerList* ghosts = fFrameGhosts;
			fFrameGhosts = NULL;
			bool slideFrame = fFrameSlide;
			fFrameSlide = false;
			fFrameSplash = true;
			ShareDrawList noShares(1);
			fLastCompose = system_time();
			if (gridSlot >= 0)
				fGridSlot = gridSlot;
			bool ok = _Compose(damage, pictures, noShares, false);
			fFrameSplash = false;
			fFrameSlide = slideFrame;
			fFrameGhosts = ghosts;
			if (!ok) {
				slot.Unlock();
				continue;
			}
			fLastFrame = system_time();
			continue;
		}

		// M11 step 4: the shadow masks and the kept blur, and the damage
		// grown where the blur needs fresh input. Without an effect on
		// screen nothing of this runs, and the compose is step 2's.
		if (effects && !_PrepareEffects(damage, layers, generic))
			effects = false;

		// M11 step 3b: shared views. A presented image that is not ready
		// yet keeps the previous one, and its view comes back next period.
		ShareDrawList shares(4);
		BRegion retry;
		_ShareFrame(shares, retry);

		fLastCompose = system_time();
		// M13 step 4: this slot has composed (a frame without damage
		// above leaves its slot free)
		if (gridSlot >= 0)
			fGridSlot = gridSlot;
		if (!_Compose(damage, layers, shares, effects)) {
			// M11 review: the job may still run. The buffers of this
			// frame are freed after the device is idle or lost
			// (docs/APP-SERVER-COMPOSITOR.md), so wait once more, with
			// the slot, the screen and the windows unlocked (a pause does
			// not wait for it), before the layers go at the end of this
			// pass; a job that still runs keeps every buffer for good.
			slot.Unlock();
			if (fFailure != NULL && strcmp(fFailure, "fence timeout") == 0) {
				// Review after M11: in steps, so app_server's quit does
				// not wait the whole grace.
				VkResult waited = VK_TIMEOUT;
				for (uint64 spent = 0; spent < kFenceGraceNs && !fQuit
						&& waited == VK_TIMEOUT; spent += kFenceStepNs) {
					waited = fVulkan->vkWaitForFences(fVulkan->device, 1,
						&fVulkan->fence, VK_TRUE, kFenceStepNs);
				}
				if (waited != VK_SUCCESS) {
					atomic_set(&fGpuBusy, 1);
					SkiaGpu::KeepImageMemory();
					log_line("compose fence still busy: window memory kept");
				}
			}
			// fFailure is set; the top of the loop handles it
			continue;
		}
		// M13 step 0: this frame is on the screen
		bigtime_t shown = system_time();
		if (fLastFrame > 0 && shown - fLastFrame < kFrameGap
			&& fFrameCount < (int32)(sizeof(fFrameSamples)
				/ sizeof(fFrameSamples[0])))
			fFrameSamples[fFrameCount++] = shown - fLastFrame;
		fLastFrame = shown;
		if (frameInput != 0 && fInputCount < (int32)(sizeof(fInputSamples)
				/ sizeof(fInputSamples[0])))
			fInputSamples[fInputCount++] = shown - frameInput;
		Damage(retry);
	}
}


/*static*/ void
Compositor::NoteInput()
{
	// keep the earliest move not answered yet; one that no damage answered
	// is replaced once it is too old to count
	bigtime_t now = system_time();
	int64 old = atomic_get64(&sInputWhen);
	if (old == 0 || now - old >= kInputWindow)
		atomic_test_and_set64(&sInputWhen, now, old);
}


/*!	Records one frame: the background in the damage, then each window's
	part of the damage, back to front. Waits for the fence, then puts the
	damage on the screen. Returns false when the GPU mode ended.
*/
static BRegion grow_region(const BRegion& region, int32 by);

// M13 step 3, gpu_debug: what the compose records, summed per 10 s
static int64 sComposeDispatches = 0;
static int64 sComposeRegions = 0;
static int64 sComposeCopied = 0;
static int64 sComposeFrames = 0;


bool
Compositor::_Compose(const BRegion& damage, const LayerList& layers,
	const ShareDrawList& shares, bool effects)
{
	CompositorVulkan* vk = fVulkan;
	fFrames++;
	// M11 step 4: the CPU compare knows no effects; it runs only on a
	// frame without them.
	bool compare = fDebug && fFrames % 100 == 1 && !effects
		&& !fFrameAnimating && !fFrameSplash;
	// M13 step 4 (review): nor the outline of a resize on Lavapipe
	for (int32 i = 0; compare && i < layers.CountItems(); i++) {
		if (layers.ItemAt(i)->outline.CountRects() > 0)
			compare = false;
	}
	// start splash: the boot picture's white
	uint32 background = fFrameSplash ? 0xffffffff : fBackground;

	// A compare frame keeps the windows from drawing until the CPU copy
	// is done, so both read the same pixels. The window lock is never
	// held here.
	if (compare) {
		for (int32 i = 0; i < layers.CountItems(); i++)
			layers.ItemAt(i)->entry->iface->LockExclusiveAccess();
	}

	vk->vkResetCommandBuffer(vk->commands, 0);
	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vk->vkBeginCommandBuffer(vk->commands, &begin);
	// Review 17 Nov 2026: a semaphore wait makes Skia's writes visible to
	// its own batch only. Every write submitted before this frame on the
	// queue (Skia's runs, which compose need not wait for) is visible to
	// its reads; RADV's flush at the start of a submit did this so far.
	VkMemoryBarrier fromBefore = {};
	fromBefore.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	fromBefore.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	fromBefore.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
		| VK_ACCESS_TRANSFER_READ_BIT;
	vk->vkCmdPipelineBarrier(vk->commands,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 1, &fromBefore, 0, NULL, 0, NULL);
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->pipeline);

	VkMemoryBarrier between = {};
	between.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	between.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	between.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
		| VK_ACCESS_SHADER_WRITE_BIT;

	VkDescriptorBufferInfo frameInfo = {};
	frameInfo.buffer = vk->scanActive ? vk->back : vk->frame;
	frameInfo.range = VK_WHOLE_SIZE;

	// M13 step 2: the compare reads a window that Skia draws from a copy
	// of its image, made in this command buffer before the compose reads
	// the same image: the one read-back left besides screenshots.
	CompareCopies copies;
	if (compare)
		_RecordCompareCopies(layers, copies);

	// M13 step 2, gpu_debug: a timestamp after each pass
	fStampCount = 0;
	if (vk->timestamps != VK_NULL_HANDLE) {
		vk->vkCmdResetQueryPool(vk->commands, vk->timestamps, 0,
			kPassStamps);
		_Stamp(kPassCopy);
	}

	compose_rect rect;
	memset(&rect, 0, sizeof(rect));
	rect.frameStride = vk->scanActive ? vk->backPitch / 4 : fFrameWidth;

	// M11 step 4b: the picture shown now is the old side of the slide
	bool slide = fFrameSlide && _EnsureSlideBuffers(rect.frameStride);
	if (slide && fFrameSlideSnapshot) {
		_RecordSlideCopy((uint64)frameInfo.buffer, (uint64)vk->slideOld,
			rect.frameStride);
	}

	// M13 step 3: what the layers above each one overwrite with their
	// plain copy, so it is not copied first and then overwritten (a pixel
	// under three windows was written four times). An upper layer covers
	// its shape without its glass; what an effect of an upper layer reads
	// of the frame below stays: the glass with the blur's reach, an
	// opening window's rectangle, a closing window's fade. No culling
	// during the workspace slide. The frame is the same pixels.
	std::vector<BRegion> covered(layers.CountItems() + 1);
	if (!slide && !fNoCull) {
		BRegion over;
		BRegion read;
		int32 reach = window_glass_for(fStrength).reach;
		for (int32 i = 0; fFrameGhosts != NULL
				&& i < fFrameGhosts->CountItems(); i++) {
			Layer* ghost = fFrameGhosts->ItemAt(i);
			if (ghost->HasPixels()) {
				BRegion area(ghost->PixelRect());
				read.Include(&area);
			}
		}
		for (int32 i = layers.CountItems() - 1; i >= -1; i--) {
			covered[i + 1] = over;
			covered[i + 1].Exclude(&read);
			if (i < 0)
				break;
			Layer* upper = layers.ItemAt(i);
			if (!upper->HasPixels())
				continue;
			BRegion pixels(upper->PixelRect());
			if (upper->visible >= 0) {
				read.Include(&pixels);
				continue;
			}
			BRegion plain(upper->shape);
			plain.IntersectWith(&pixels);
			BRegion glass(upper->glassFixed);
			glass.Include(&upper->glassAlpha);
			plain.Exclude(&glass);
			over.Include(&plain);
			if (glass.CountRects() > 0) {
				BRegion grown = grow_region(glass, reach);
				read.Include(&grown);
			}
		}
	}

	for (int32 layerIndex = -1; layerIndex < layers.CountItems();
			layerIndex++) {
		Layer* layer = layerIndex >= 0 ? layers.ItemAt(layerIndex) : NULL;
		BRegion part(damage);
		part.Exclude(&covered[layerIndex + 1]);
		// M11 step 4: a window with a shadow or glass. Without one, this
		// loop is step 2's copy.
		bool withEffects = effects && layer != NULL && layer->HasEffects()
			&& layer->HasPixels();
		BRegion glassPart;
		if (layer != NULL && layer->visible >= 0 && layer->HasPixels()) {
			// M11 step 4b: opening, scaled and faded, without its effects
			_RecordWindowAnimation(layer, damage, (uint64)frameInfo.buffer,
				rect.frameStride);
			vk->vkCmdPipelineBarrier(vk->commands,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, NULL,
				0, NULL);
			vk->vkCmdBindPipeline(vk->commands,
				VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
			continue;
		}
		if (layer != NULL) {
			if (!layer->HasPixels())
				continue;
			part.IntersectWith(&layer->shape);
			bool shadowHit = withEffects && layer->shadow
				&& damage.Intersects(layer->shadowArea.Frame());
			// M13 step 4: an outline outside the window is drawn too
			bool outlineHit = layer->outline.CountRects() > 0
				&& damage.Intersects(layer->outline.Frame());
			if (part.CountRects() == 0 && !shadowHit && !outlineHit)
				continue;
		}
		if (withEffects) {
			// The blur reads what lies below, before this window's own
			// shadow darkens it; then the shadow; then the window.
			_Stamp(kPassCopy);
			if (layer->blurRegion.CountRects() > 0) {
				_RecordBlur(layer, (uint64)frameInfo.buffer, rect.frameStride);
				_Stamp(kPassBlur);
			}
			if (layer->shadow) {
				_RecordShadow(layer, damage, (uint64)frameInfo.buffer,
					rect.frameStride);
				_Stamp(kPassShadow);
			}
			glassPart = layer->glassFixed;
			glassPart.Include(&layer->glassAlpha);
			glassPart.IntersectWith(&part);
			part.Exclude(&glassPart);
			vk->vkCmdBindPipeline(vk->commands,
				VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
		}

		// M13 step 2: a window that Skia draws is read from its image; a
		// part its image does not reach (a resize not drawn yet) keeps
		// what lies below
		bool fromImage = layer != NULL && layer->image.IsSet();
		VkBuffer source = frameInfo.buffer;
		if (layer != NULL && !fromImage)
			source = (VkBuffer)layer->buffer->VulkanBuffer();
		push_compose_buffers(vk, source, frameInfo.buffer,
			fromImage ? (VkImageView)layer->image->View() : VK_NULL_HANDLE);
		if (fromImage) {
			BRegion pixels(layer->PixelRect());
			part.IntersectWith(&pixels);
		}

		rect.fill = layer == NULL ? 1 : fromImage ? kComposeFromImage : 0;
		rect.color = background;
		rect.sourceStride = layer != NULL ? layer->buffer->Width() : 0;
		bool copied = false;
		if (fromImage && part.CountRects() > 0) {
			// M13 (step 3's copy path, done with step 2): the plain part of
			// a Skia window is a transfer copy from its image into the
			// frame, the same bytes as the copy shader. On Lavapipe
			// texelFetch in compose.comp cost 150-160 ms per frame while
			// dragging (QEMU, "compose passes"), a copy is a row memcpy.
			// review 17 Nov 2026: without memory for the list the shader
			// below copies the part
			copied = _RecordImageCopy(layer, part, frameInfo.buffer,
				rect.frameStride);
		} else if (layer != NULL && part.CountRects() > 0) {
			// M13 step 3: a Painter window's plain part too, row by row
			// from its buffer (vkCmdCopyBuffer), the same words
			copied = _RecordBufferCopy(layer, part, frameInfo.buffer,
				rect.frameStride);
		}
		// 5625U stick, 17 Nov 2026: part stays as it is; the window's
		// shared views below are drawn inside it (part was emptied after
		// the copy, and VulkanTeapot's zero-copy view stayed white)
		for (int32 i = 0; !copied && i < part.CountRects(); i++) {
			clipping_rect r = part.RectAtInt(i);
			rect.frameX = r.left;
			rect.frameY = r.top;
			rect.width = r.right - r.left + 1;
			rect.height = r.bottom - r.top + 1;
			if (layer != NULL) {
				rect.sourceX = r.left - layer->SourceLeft();
				rect.sourceY = r.top - layer->SourceTop();
			}
			vk->vkCmdPushConstants(vk->commands, vk->pipelineLayout,
				VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rect), &rect);
			vk->vkCmdDispatch(vk->commands, (rect.width + 15) / 16,
				(rect.height + 15) / 16, 1);
			sComposeDispatches++;
			sComposeCopied += (int64)rect.width * rect.height;
		}
		if (withEffects && glassPart.CountRects() > 0) {
			// next to the copy above, on other pixels: no barrier between
			_Stamp(kPassCopy);
			_RecordGlass(layer, glassPart, (uint64)frameInfo.buffer,
				rect.frameStride);
			_Stamp(kPassGlass);
			vk->vkCmdBindPipeline(vk->commands,
				VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
			// a shared view below is drawn over all of the window's part
			part.Include(&glassPart);
		}
		// the next layer writes over this one
		vk->vkCmdPipelineBarrier(vk->commands,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, NULL,
			0, NULL);

		// M11 step 3b: the window's shared views, over its own pixels and
		// under the next window.
		for (int32 s = 0; layer != NULL && s < shares.CountItems(); s++) {
			ShareDraw* draw = shares.ItemAt(s);
			if (draw->window == NULL || draw->window != layer->entry->window)
				continue;
			BRegion view(part);
			view.IntersectWith(&draw->shape);
			BRegion image(BRect((float)draw->left, (float)draw->top,
				(float)draw->left + draw->width - 1,
				(float)draw->top + draw->height - 1));
			view.IntersectWith(&image);
			if (view.CountRects() == 0)
				continue;
			push_compose_buffers(vk, draw->buffer, frameInfo.buffer);
			rect.fill = 0;
			rect.sourceStride = draw->pitch / 4;
			for (int32 i = 0; i < view.CountRects(); i++) {
				clipping_rect r = view.RectAtInt(i);
				rect.frameX = r.left;
				rect.frameY = r.top;
				rect.width = r.right - r.left + 1;
				rect.height = r.bottom - r.top + 1;
				rect.sourceX = r.left - draw->left;
				rect.sourceY = r.top - draw->top;
				vk->vkCmdPushConstants(vk->commands, vk->pipelineLayout,
					VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rect), &rect);
				vk->vkCmdDispatch(vk->commands, (rect.width + 15) / 16,
					(rect.height + 15) / 16, 1);
				sComposeDispatches++;
				sComposeCopied += (int64)rect.width * rect.height;
			}
			vk->vkCmdPipelineBarrier(vk->commands,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, NULL,
				0, NULL);
		}

		// M13 step 4: the outline of a resize on Lavapipe, over the window
		// and its shared views, under the next window
		if (layer != NULL && layer->outline.CountRects() > 0
			&& _RecordOutline(layer, damage, (uint64)frameInfo.buffer,
				rect.frameStride)) {
			vk->vkCmdBindPipeline(vk->commands,
				VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
		}
	}

	// M11 step 4b: windows that closed, over the others, then the slide
	for (int32 i = 0; fFrameGhosts != NULL && i < fFrameGhosts->CountItems();
			i++) {
		Layer* ghost = fFrameGhosts->ItemAt(i);
		_RecordWindowAnimation(ghost, damage, (uint64)frameInfo.buffer,
			rect.frameStride);
		vk->vkCmdPipelineBarrier(vk->commands,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, NULL, 0,
			NULL);
	}
	if (slide) {
		_RecordSlideCopy((uint64)frameInfo.buffer, (uint64)vk->slideNew,
			rect.frameStride);
		_RecordSlide((uint64)frameInfo.buffer, rect.frameStride);
	}
	_Stamp(fFrameAnimating ? kPassAnimation : kPassCopy);

	// Decision 4: the damaged rows of the back buffer onto the scanout, in
	// this submit, under this fence. Each pitch is its buffer's own.
	// The race with the software cursor: from here until _Present has drawn
	// the cursor, the GPU writes the back buffer and the front, which the
	// mouse thread reads and writes (MoveCursorTo, under the parallel
	// lock). The exclusive lock keeps it out for that time, usually a
	// millisecond; a late fence holds the cursor that long.
	bool exclusive = false;
	if (vk->scanActive) {
		exclusive = fScreen->LockExclusiveAccess();
		// The software cursor's rectangle is not copied: _DrawCursor puts
		// it on the front, blended from the new back buffer, as
		// CopyBackToFront leaves it out. So it never shows without cursor.
		BRegion copy(damage);
		clipping_rect cursor;
		if (static_cast<AccelerantHWInterface*>(fScreen)
				->SoftwareCursorFrame(&cursor))
			copy.Exclude(cursor);
		VkMemoryBarrier toCopy = {};
		toCopy.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		toCopy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vk->vkCmdPipelineBarrier(vk->commands,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &toCopy, 0, NULL, 0, NULL);
		uint32 count = 0;
		for (int32 i = 0; i < copy.CountRects(); i++) {
			clipping_rect r = copy.RectAtInt(i);
			count += r.bottom - r.top + 1;
		}
		VkBufferCopy* regions = new(std::nothrow) VkBufferCopy[count + 1];
		uint32 n = 0;
		for (int32 i = 0; regions != NULL && i < copy.CountRects(); i++) {
			clipping_rect r = copy.RectAtInt(i);
			uint32 widthBytes = (uint32)(r.right - r.left + 1) * 4;
			// One linear copy only when the rows are contiguous in both
			// buffers. A partial row has a gap, and one copy would overwrite it.
			if (vk->backPitch == vk->scanPitch && r.left == 0
				&& widthBytes == vk->backPitch) {
				regions[n].srcOffset = (uint64)r.top * vk->backPitch;
				regions[n].dstOffset = (uint64)r.top * vk->scanPitch;
				regions[n].size = (uint64)(r.bottom - r.top + 1)
					* vk->backPitch;
				n++;
				continue;
			}
			for (int32 y = r.top; y <= r.bottom; y++) {
				regions[n].srcOffset = (uint64)y * vk->backPitch
					+ (uint64)r.left * 4;
				regions[n].dstOffset = (uint64)y * vk->scanPitch
					+ (uint64)r.left * 4;
				regions[n].size = widthBytes;
				n++;
			}
		}
		if (regions != NULL && n > 0)
			vk->vkCmdCopyBuffer(vk->commands, vk->back, vk->scanout, n,
				regions);
		delete[] regions;
		// Without memory for the list the back buffer still holds the
		// frame; _Present then lets CopyBackToFront take it to the front.
		fFrontByVulkan = regions != NULL;
	} else
		fFrontByVulkan = false;

	VkMemoryBarrier toHost = {};
	toHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	toHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT
		| VK_ACCESS_TRANSFER_WRITE_BIT;
	toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vk->vkCmdPipelineBarrier(vk->commands,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, NULL, 0, NULL);
	// M13 step 2: Skia's next run draws into images this frame reads.
	// Graphite's barrier out of the sampler's layout waits for its own
	// shader stages only, so everything after this frame waits for its
	// reads here.
	vk->vkCmdPipelineBarrier(vk->commands,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 0, NULL);
	vk->vkEndCommandBuffer(vk->commands);

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &vk->commands;
	vk->vkResetFences(vk->device, 1, &vk->fence);
	bigtime_t started = system_time();
	// M13 step 2 (replaces SyncToCpu::kYes): the frame waits on the GPU for
	// every Skia run submitted before it. Taken under the queue lock, so
	// no run's submit lands between taking the semaphores and this one.
	std::vector<uint64> signals;
	std::vector<VkSemaphore> waits;
	std::vector<VkPipelineStageFlags> waitStages;
	SkiaGpu::LockQueue();
	if (fSkia != NULL && !fSkia->IsShutDown())
		fSkia->TakeSignals(signals);
	for (size_t i = 0; i < signals.size(); i++) {
		waits.push_back((VkSemaphore)signals[i]);
		waitStages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
			| VK_PIPELINE_STAGE_TRANSFER_BIT);
	}
	submit.waitSemaphoreCount = (uint32)waits.size();
	submit.pWaitSemaphores = waits.empty() ? NULL : &waits[0];
	submit.pWaitDstStageMask = waitStages.empty() ? NULL : &waitStages[0];
	VkResult result = vk->vkQueueSubmit(vk->queue, 1, &submit, vk->fence);
	SkiaGpu::UnlockQueue();
	bool forced = fForceFail;
	if (forced) {
		// gpu_compositor_fail: the first submit counts as failed
		fForceFail = false;
		result = VK_ERROR_UNKNOWN;
	}

	// Decision 14: a late fence is not a failure. Keep the last frame,
	// keep collecting damage, and wait on; the watchdog decides first.
	const char* failure = NULL;
	if (result != VK_SUCCESS)
		failure = forced ? "forced failure" : "submit";
	while (failure == NULL) {
		result = vk->vkWaitForFences(vk->device, 1, &vk->fence, VK_TRUE,
			kFenceSlice * 1000);
		if (result == VK_SUCCESS)
			break;
		if (result != VK_TIMEOUT) {
			failure = "device lost";
			break;
		}
		bigtime_t late = system_time() - started;
		// M13 (owner, 16 Nov 2026): Lavapipe is the CPU, not a GPU that
		// can hang; a late frame there is a busy machine (QEMU: frames of
		// more than 15 s right after Sign in turned the compositor off).
		// It waits on, without a limit. RADV keeps the 15 s limit and the
		// reset ladder.
		if (late >= kFenceLimit && !fOnLavapipe) {
			failure = "fence timeout";
			break;
		}
		if (system_time() - fLateLogged
				>= (late >= kFenceLimit ? 10000000 : 1000000)) {
			fLateLogged = system_time();
			log_line("compose late %" B_PRId64 " ms", late / 1000);
		}
	}
	if (failure != NULL) {
		// M13 step 2: the semaphores and the compare copies stay; the GPU
		// may still use them, and the GPU drawing goes off now
		if (exclusive)
			fScreen->UnlockExclusiveAccess();
		if (compare) {
			for (int32 i = 0; i < layers.CountItems(); i++)
				layers.ItemAt(i)->entry->iface->UnlockExclusiveAccess();
		}
		// handled by _ComposeLoop() once the compose slot is free: the
		// restart after a reset, or the CPU mode (step 5)
		fFailure = failure;
		return false;
	}

	bigtime_t took = system_time() - started;
	if (fSampleCount < (int32)(sizeof(fSamples) / sizeof(fSamples[0])))
		fSamples[fSampleCount++] = took;
	if (!signals.empty())
		fSkia->ReturnSignals(signals);
	_TakeStamps();
	sComposeFrames++;

	if (compare) {
		// The CPU cannot read an imported image; those parts are left out.
		BRegion shared;
		for (int32 s = 0; s < shares.CountItems(); s++)
			shared.Include(&shares.ItemAt(s)->shape);
		_Compare(damage, layers, background, shared, copies);
		for (int32 i = 0; i < layers.CountItems(); i++)
			layers.ItemAt(i)->entry->iface->UnlockExclusiveAccess();
	}
	_FreeCompareCopies(copies);

	_Present(damage);
	if (exclusive)
		fScreen->UnlockExclusiveAccess();
	return true;
}


/*!	M13 step 2, a compare frame: each window image into host memory, in the
	frame's command buffer, before the compose reads it. The image goes
	back to the sampler's layout after the copy.
*/
void
Compositor::_RecordCompareCopies(const LayerList& layers,
	CompareCopies& copies)
{
	CompositorVulkan* vk = fVulkan;
	for (int32 l = 0; l < layers.CountItems(); l++) {
		Layer* layer = layers.ItemAt(l);
		if (!layer->image.IsSet())
			continue;
		SkiaImage* image = layer->image.Get();
		CompareCopy copy;
		copy.layer = layer;
		VkBuffer buffer;
		VkDeviceMemory memory;
		if (!allocate_host_buffer(vk,
				(uint64)image->Width() * image->Height() * 4, &buffer,
				&memory, &copy.bits))
			continue;
		copy.buffer = (uint64)buffer;
		copy.memory = (uint64)memory;
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = (VkImage)image->Image();
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;
		vk->vkCmdPipelineBarrier(vk->commands,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				| VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = image->Width();
		region.imageExtent.height = image->Height();
		region.imageExtent.depth = 1;
		vk->vkCmdCopyImageToBuffer(vk->commands, (VkImage)image->Image(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vk->vkCmdPipelineBarrier(vk->commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
			&barrier);
		copies.push_back(copy);
	}
}


//!	After the fence (or with the GPU idle): the compare copies go.
void
Compositor::_FreeCompareCopies(CompareCopies& copies)
{
	CompositorVulkan* vk = fVulkan;
	for (size_t i = 0; i < copies.size(); i++) {
		vk->vkDestroyBuffer(vk->device, (VkBuffer)copies[i].buffer, NULL);
		vk->vkFreeMemory(vk->device, (VkDeviceMemory)copies[i].memory, NULL);
	}
	copies.clear();
}


/*!	M13: \a region (screen pixels inside the window's image) copied from
	the image into the frame with vkCmdCopyImageToBuffer. The image goes
	to TRANSFER_SRC_OPTIMAL and back to the sampler's layout; the frame's
	writes before and after are ordered with the compute passes.
*/
bool
Compositor::_RecordImageCopy(const Layer* layer, const BRegion& region,
	VkBuffer_T* frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	SkiaImage* image = layer->image.Get();
	int32 count = region.CountRects();
	VkBufferImageCopy* copies = new(std::nothrow) VkBufferImageCopy[count];
	if (copies == NULL)
		return false;
	int32 sourceLeft = layer->SourceLeft();
	int32 sourceTop = layer->SourceTop();
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = region.RectAtInt(i);
		VkBufferImageCopy& copy = copies[i];
		memset(&copy, 0, sizeof(copy));
		copy.bufferOffset = ((uint64)r.top * frameStride + r.left) * 4;
		copy.bufferRowLength = frameStride;
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.layerCount = 1;
		copy.imageOffset.x = r.left - sourceLeft;
		copy.imageOffset.y = r.top - sourceTop;
		copy.imageExtent.width = r.right - r.left + 1;
		copy.imageExtent.height = r.bottom - r.top + 1;
		copy.imageExtent.depth = 1;
	}

	VkMemoryBarrier before = {};
	before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT
		| VK_ACCESS_TRANSFER_WRITE_BIT;
	before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	VkImageMemoryBarrier toCopy = {};
	toCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toCopy.srcAccessMask = 0;
	toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	toCopy.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toCopy.image = (VkImage)image->Image();
	toCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toCopy.subresourceRange.levelCount = 1;
	toCopy.subresourceRange.layerCount = 1;
	vk->vkCmdPipelineBarrier(vk->commands,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 1, &toCopy);
	vk->vkCmdCopyImageToBuffer(vk->commands, (VkImage)image->Image(),
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame, count, copies);
	sComposeRegions += count;
	for (int32 i = 0; i < count; i++) {
		sComposeCopied += (int64)copies[i].imageExtent.width
			* copies[i].imageExtent.height;
	}
	delete[] copies;

	VkMemoryBarrier after = {};
	after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
		| VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT
		| VK_ACCESS_TRANSFER_WRITE_BIT;
	VkImageMemoryBarrier toRead = toCopy;
	toRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vk->vkCmdPipelineBarrier(vk->commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 1, &after, 0, NULL, 1, &toRead);
	return true;
}


/*!	M13 step 3: \a region (screen pixels inside the window's buffer)
	copied row by row from the buffer into the frame with vkCmdCopyBuffer;
	ordered with the compute passes before and after.
*/
bool
Compositor::_RecordBufferCopy(const Layer* layer, const BRegion& region,
	VkBuffer_T* frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	uint32 count = 0;
	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect r = region.RectAtInt(i);
		count += r.bottom - r.top + 1;
	}
	VkBufferCopy* copies = new(std::nothrow) VkBufferCopy[count];
	if (copies == NULL)
		return false;
	uint32 sourceStride = layer->buffer->Width();
	uint32 n = 0;
	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect r = region.RectAtInt(i);
		uint32 sourceX = r.left - layer->left;
		uint32 width = (uint32)(r.right - r.left + 1);
		// Pitches are in pixels. One copy per rectangle when they match and
		// the rectangle is the whole row; otherwise the gap between rows
		// would be copied too.
		if (sourceStride == frameStride && sourceX == 0 && r.left == 0
			&& width == sourceStride) {
			uint32 sourceY = r.top - layer->top;
			copies[n].srcOffset = (uint64)sourceY * sourceStride * 4;
			copies[n].dstOffset = (uint64)r.top * frameStride * 4;
			copies[n].size = (uint64)(r.bottom - r.top + 1) * sourceStride * 4;
			n++;
			continue;
		}
		for (int32 y = r.top; y <= r.bottom; y++) {
			uint32 sourceY = y - layer->top;
			copies[n].srcOffset = ((uint64)sourceY * sourceStride + sourceX) * 4;
			copies[n].dstOffset = ((uint64)y * frameStride + r.left) * 4;
			copies[n].size = (uint64)width * 4;
			n++;
		}
	}
	VkMemoryBarrier before = {};
	before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT
		| VK_ACCESS_TRANSFER_WRITE_BIT;
	before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vk->vkCmdPipelineBarrier(vk->commands,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
	vk->vkCmdCopyBuffer(vk->commands, (VkBuffer)layer->buffer->VulkanBuffer(),
		frame, n, copies);
	sComposeRegions += n;
	for (uint32 i = 0; i < n; i++)
		sComposeCopied += (int64)copies[i].size / 4;
	delete[] copies;
	VkMemoryBarrier after = {};
	after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
		| VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT
		| VK_ACCESS_TRANSFER_WRITE_BIT;
	vk->vkCmdPipelineBarrier(vk->commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 1, &after, 0, NULL, 0, NULL);
	return true;
}


//!	M13 step 2, gpu_debug: a timestamp; the time since the last one counts
//!	for \a pass.
void
Compositor::_Stamp(int32 pass)
{
	CompositorVulkan* vk = fVulkan;
	if (vk->timestamps == VK_NULL_HANDLE || fStampCount >= kPassStamps)
		return;
	vk->vkCmdWriteTimestamp(vk->commands,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vk->timestamps, fStampCount);
	fStampPass[fStampCount++] = pass;
}


//!	After the fence: the frame's timestamps into the per-pass sums.
void
Compositor::_TakeStamps()
{
	CompositorVulkan* vk = fVulkan;
	if (vk->timestamps == VK_NULL_HANDLE || fStampCount < 2)
		return;
	uint64 values[kPassStamps];
	if (vk->vkGetQueryPoolResults(vk->device, vk->timestamps, 0, fStampCount,
			sizeof(values), values, sizeof(uint64), VK_QUERY_RESULT_64_BIT)
			!= VK_SUCCESS)
		return;
	for (int32 i = 1; i < fStampCount; i++) {
		if (values[i] < values[i - 1])
			continue;
		fPassTime[fStampPass[i]] += (bigtime_t)((values[i] - values[i - 1])
			* (double)vk->timestampPeriod / 1000.0);
	}
	fPassFrames++;
}


/*!	Copies the composed damage into the screen's back buffer and lets the
	screen interface put it on the front: the row copy, or the M8 DMA_DATA
	copy when gpu_app_server is on. The cursor is drawn there, as before.
*/
void
Compositor::_Present(const BRegion& damage)
{
	if (!fScreen->LockParallelAccess())
		return;

	bigtime_t started = system_time();
	RenderingBuffer* back = fScreen->BackBuffer();
	if (fFrontByVulkan && back != NULL) {
		// Decision 4: the back buffer and the front are both this frame
		// already. Invalidate only draws the cursor where the copy went.
		GPUCopy::SetFrontCurrent(true);
		fScreen->InvalidateRegion(damage);
		GPUCopy::SetFrontCurrent(false);
	} else if (fVulkan->scanActive && back != NULL) {
		// The compose wrote the back buffer itself: no CPU copy.
		fScreen->InvalidateRegion(damage);
	} else if (back != NULL && back->Width() == fFrameWidth
		&& back->Height() == fFrameHeight) {
		const uint8* frame = (const uint8*)fVulkan->frameBits;
		uint32 frameRow = fFrameWidth * 4;
		uint8* bits = (uint8*)back->Bits();
		uint32 backRow = back->BytesPerRow();
		for (int32 i = 0; i < damage.CountRects(); i++) {
			clipping_rect r = damage.RectAtInt(i);
			uint32 bytes = (r.right - r.left + 1) * 4;
			for (int32 y = r.top; y <= r.bottom; y++) {
				r5_span_copy(bits + y * backRow + r.left * 4,
					frame + y * frameRow + r.left * 4, bytes);
			}
		}
		fScreen->InvalidateRegion(damage);
	}
	bigtime_t took = system_time() - started;
	if (fPresentCount < (int32)(sizeof(fPresentSamples)
			/ sizeof(fPresentSamples[0])))
		fPresentSamples[fPresentCount++] = took;
	fScreen->UnlockParallelAccess();
}


/*!	gpu_debug: the same frame on the CPU, from the same window buffers,
	compared with what the GPU wrote. Colour channels only.
*/
bool
Compositor::_Compare(const BRegion& damage, const LayerList& layers,
	uint32 background, const BRegion& shared, const CompareCopies& copies)
{
	if (fScratch == NULL)
		return false;
	uint32 stride = fFrameWidth;
	// Decision 4: the GPU wrote into the back buffer, with its own pitch.
	const uint32* frame = fVulkan->frameBits;
	uint32 frameStride = fFrameWidth;
	RenderingBuffer* back = fScreen->BackBuffer();
	if (fVulkan->scanActive && back != NULL) {
		frame = (const uint32*)back->Bits();
		frameStride = back->BytesPerRow() / 4;
	}
	for (int32 i = 0; i < damage.CountRects(); i++) {
		clipping_rect r = damage.RectAtInt(i);
		for (int32 y = r.top; y <= r.bottom; y++) {
			for (int32 x = r.left; x <= r.right; x++)
				fScratch[y * stride + x] = background;
		}
	}
	for (int32 l = 0; l < layers.CountItems(); l++) {
		Layer* layer = layers.ItemAt(l);
		if (!layer->HasPixels())
			continue;
		BRegion part(damage);
		part.IntersectWith(&layer->shape);
		const uint32* source = (const uint32*)layer->buffer->Bits();
		uint32 sourceStride = layer->buffer->Width();
		if (layer->image.IsSet()) {
			// M13 step 2: the copy of its image made in this frame
			source = NULL;
			for (size_t c = 0; c < copies.size(); c++) {
				if (copies[c].layer == layer)
					source = (const uint32*)copies[c].bits;
			}
			if (source == NULL)
				return false;
			sourceStride = layer->image->Width();
			BRegion pixels(layer->PixelRect());
			part.IntersectWith(&pixels);
		}
		int32 sourceLeft = layer->SourceLeft();
		int32 sourceTop = layer->SourceTop();
		for (int32 i = 0; i < part.CountRects(); i++) {
			clipping_rect r = part.RectAtInt(i);
			for (int32 y = r.top; y <= r.bottom; y++) {
				memcpy(fScratch + y * stride + r.left,
					source + (y - sourceTop) * sourceStride
						+ (r.left - sourceLeft),
					(r.right - r.left + 1) * 4);
			}
		}
	}
	BRegion checked(damage);
	checked.Exclude(&shared);
	for (int32 i = 0; i < checked.CountRects(); i++) {
		clipping_rect r = checked.RectAtInt(i);
		for (int32 y = r.top; y <= r.bottom; y++) {
			for (int32 x = r.left; x <= r.right; x++) {
				uint32 at = y * stride + x;
				uint32 framed = frame[y * frameStride + x];
				if (((fScratch[at] ^ framed) & 0x00ffffff) != 0) {
					log_line("compose compare MISMATCH at %" B_PRId32 ",%"
						B_PRId32, x, y);
					return false;
				}
			}
		}
	}
	log_line("compose compare ok");
	return true;
}


/*!	Decision 14: after the GPU was reset and passed its self-test, make the
	device once more on RADV. Every window buffer is new, so every window
	draws once. Returns false when the GPU does not come back.
*/
bool
Compositor::_RestartAfterReset()
{
	fRestarted = true;
	bigtime_t until = system_time() + kFenceLimit;
	const char* reason = NULL;
	while (!gpu_available(NULL, &reason)) {
		if (system_time() >= until)
			return false;
		snooze(500000);
	}

	if (!fDesktop->LockAllWindows())
		return false;

	// Every engine points at memory of the lost device. Park each engine
	// on the screen first; Painter keeps that attachment until the window
	// gets a new buffer, so a hidden window never writes freed memory.
	fEntryLock.Lock();
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window != NULL
			&& entry->window->GetDrawingEngine() != NULL)
			entry->window->GetDrawingEngine()->SetHWInterface(fScreen);
		entry->iface->SetBuffer(NULL, 0, 0);
	}
	fEntryLock.Unlock();
	{
		BAutolock _(fLayerLock);
		fLayers->MakeEmpty();
	}

	_DropEffects();
	const char* initReason = NULL;
	RenderingBuffer* back = fScreen->BackBuffer();
	bool ok = back != NULL && _InitVulkan(true, &initReason) == B_OK
		&& _EnsureFrame(back->Width(), back->Height());
	if (ok) {
		// all buffers are new now: every window draws once
		ClippingChanged();
		fEntryLock.Lock();
		// M11 third review: a pause that began during the restart keeps
		// the windows on the screen (as AddWindow does); the resume binds
		// them to their buffers.
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (entry->window != NULL
				&& entry->window->GetDrawingEngine() != NULL)
				entry->window->GetDrawingEngine()->SetHWInterface(
					fPaused ? (HWInterface*)fScreen : entry->iface);
		}
		fEntryLock.Unlock();
		log_line("gpu compositor restarted after reset");
	}
	fDesktop->UnlockAllWindows();
	return ok;
}


/*!	The CPU mode for the rest of the session (decision 11): every window's
	engine goes back to the screen, the desktop redraws, and the window
	buffers are freed. Never back to the GPU mode in this session.
*/
void
Compositor::_TurnOff(const char* reason)
{
	if (fSkia != NULL)
		fSkia->SetComposeBlocked(true);
	bool locked = fDesktop->LockAllWindows();
	if (fSkia != NULL)
		fSkia->SetComposeBlocked(false);
	if (!locked)
		return;

	// M11 review: under fEntryLock, so AddWindow sees the CPU mode
	fEntryLock.Lock();
	fActive = false;
	fPaused = false;
	fEntryLock.Unlock();
	_DropEffects();
	fEntryLock.Lock();
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window != NULL
			&& entry->window->GetDrawingEngine() != NULL)
			entry->window->GetDrawingEngine()->SetHWInterface(fScreen);
		entry->iface->SetBuffer(NULL, 0, 0);
	}
	fEntryLock.Unlock();
	{
		// M11 step 3b: the programs get a refusal at their next present
		// and make copy swapchains; the imports go with the device.
		_ReturnSharedImages();
	}
	{
		BAutolock _(fLayerLock);
		fLayers->MakeEmpty();
	}
	// M12: every engine is on the screen now (Painter). The device stays
	// allocated: destroying it waits on the hung ring, and a window that
	// closes afterwards faulted in Graphite (stick 61). Decision 8: one
	// line when the drawing failed.
	if (fSkia != NULL) {
		fSkia->Abandon();
		_StopDrawThread();
	}
	if (fDrawFailure)
		log_line("gpu draw off: %s", reason);
	else
		log_line("gpu compositor off: %s", reason);

	fDesktop->CompositorTurnedOff();
	fDesktop->UnlockAllWindows();
	// The back buffer is the last frame the CPU still has. Copy it to the
	// front now, on this thread, so a striped scanout does not stay up
	// until the window threads have redrawn.
	GPUCopy::SetFrontCurrent(false);
	if (fScreen != NULL && fScreen->LockParallelAccess()) {
		if (fFrameWidth > 0 && fFrameHeight > 0) {
			BRegion whole(BRect(0, 0, (float)fFrameWidth - 1,
				(float)fFrameHeight - 1));
			fScreen->InvalidateRegion(whole);
		}
		fScreen->UnlockParallelAccess();
	}
	// The device goes when the compose thread ends, after its last
	// references to the window buffers are gone. Abandon() leaves it.
}


//	#pragma mark - effects (M11 step 4, decision 16)


//!	\a region with every rectangle grown by \a by on each side.
static BRegion
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
	if (!allocate_host_buffer(vk, bytes, &vk->scratch, &vk->scratchMemory,
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


//!	Pushes the three buffers of effects.comp, and the image (M13 step 2;
//!	the blank one when \a image is VK_NULL_HANDLE).
static void
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


static void
dispatch_effect(CompositorVulkan* vk, const effect_rect& effect)
{
	vk->vkCmdPushConstants(vk->commands, vk->effectsPipelineLayout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(effect), &effect);
	vk->vkCmdDispatch(vk->commands, (effect.width + 15) / 16,
		(effect.height + 15) / 16, 1);
}


static void
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
	BRegion pixels(layer->PixelRect());

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
			effect.amount = geometry.seeThrough
				| (kind == 0 ? kEffectFixedGlass : 0);
			effect.auxStride = areaWidth;
			for (int32 i = 0; i < part.CountRects(); i++) {
				clipping_rect r = part.RectAtInt(i);
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
		cacheOffset += areaWidth * (area.bottom - area.top + 1);
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
	for (int32 i = 0; i < count && i < 4; i++) {
		clipping_rect r = count > 4 ? shape.FrameInt() : shape.RectAtInt(i);
		effect.shape[i * 4] = r.left - left;
		effect.shape[i * 4 + 1] = r.top - top;
		effect.shape[i * 4 + 2] = r.right - left;
		effect.shape[i * 4 + 3] = r.bottom - top;
		if (count > 4)
			break;
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
	if (!allocate_host_buffer(vk, bytes, &vk->slideOld, &vk->slideOldMemory,
			&bits))
		return false;
	if (!allocate_host_buffer(vk, bytes, &vk->slideNew, &vk->slideNewMemory,
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


//	#pragma mark - direct access (M11 step 5, decision 9)


/*!	A program is about to get B_DIRECT_START (a BDirectWindow) or has the
	screen (BWindowScreen). Called with fWindowLock write-locked, before the
	program learns the front buffer: waits for the compose in flight (a late
	fence is not a failure, decision 14), then every window draws to the
	screen again. The redraw the CPU-mode way follows from the compose
	thread once this write lock is released (_ApplyMode), because this can
	be called from inside a clipping rebuild.
*/
void
Compositor::DirectAccessStarting()
{
	if (!fActive || fPaused)
		return;
	{
		BAutolock slot(fComposeSlot);
		fPaused = true;
	}
	_RebindEngines(false);
	{
		BAutolock _(fLayerLock);
		fLayers->MakeEmpty();
	}
	_DropAnimations();
	log_line("compositor paused: direct window");

	fPauseRedraw = true;
	fModePending = true;
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	fWindowLock write-locked. When the last direct access stops, the resume
	is left to the compose thread: it takes the window lock after the one
	held now, so a direct window that is moved (stop, then start, under one
	write lock) never resumes and pauses the compositor.
*/
void
Compositor::DirectAccessChanged(Window* window, bool accessing)
{
	BAutolock _(fEntryLock);
	if (accessing) {
		if (!fDirectWindows.HasItem(window))
			fDirectWindows.AddItem(window);
		return;
	}
	if (fDirectWindows.RemoveItem(window) && fDirectWindows.IsEmpty()
		&& fPaused) {
		fModePending = true;
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	}
}


//!	BWindowScreen: Desktop::LockDirectScreen() and UnlockDirectScreen().
void
Compositor::DirectScreenChanged(bool locked)
{
	// M11 fourth review: an unlock gives its count back below even when
	// the GPU mode has ended meanwhile.
	if (!fActive && locked)
		return;
	if (locked) {
		if (!fDesktop->LockAllWindows())
			return;
		// set before the pause, so a resume the pause schedules sees it
		atomic_add(&fDirectScreenWanted, 1);
		DirectAccessStarting();
		fDesktop->UnlockAllWindows();
		return;
	}
	// M11 review: a count, not a flag. A team that waits for the lock
	// while another gives it up keeps the pause.
	// Review after M11: atomic_add, not a compare-and-swap. Haiku's inline
	// atomic_test_and_set is a weak CAS and returns the expected value
	// also when it failed (ARM64), which would resume while a team still
	// holds the screen. A count that was already 0 (a lock taken while the
	// GPU mode was off) is given back.
	int32 wanted = atomic_add(&fDirectScreenWanted, -1);
	if (wanted <= 0) {
		atomic_add(&fDirectScreenWanted, 1);
		wanted = 0;
	}
	if (wanted > 1 || !fActive)
		return;
	fModePending = true;
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	The compose thread, outside the compose slot: after a pause, the desktop
	is redrawn the CPU-mode way; once no program has direct access, the
	windows go back to their buffers, every window draws once, and the
	compose starts again.
*/
void
Compositor::_ApplyMode()
{
	if (fSkia != NULL)
		fSkia->SetComposeBlocked(true);
	bool locked = fDesktop->LockAllWindows();
	if (fSkia != NULL)
		fSkia->SetComposeBlocked(false);
	if (!locked)
		return;
	fModePending = false;
	fEntryLock.Lock();
	bool direct = !fDirectWindows.IsEmpty()
		|| atomic_get(&fDirectScreenWanted) > 0;
	fEntryLock.Unlock();
	if (fActive && fPaused && !direct) {
		_RebindEngines(true);
		fPaused = false;
		fPauseRedraw = false;
		// the clipping is rebuilt with the GPU mode's regions, the buffers
		// are checked, and every window draws all of itself once
		fDesktop->CompositorTurnedOff();
		Damage(BRegion(BRect(0, 0, (float)fFrameWidth - 1,
			(float)fFrameHeight - 1)));
		log_line("compositor resumed");
	} else if (fPaused && fPauseRedraw) {
		fPauseRedraw = false;
		fDesktop->CompositorTurnedOff();
	}
	fDesktop->UnlockAllWindows();
}


//!	All windows locked: every window draws into its buffer, or to the screen.
void
Compositor::_RebindEngines(bool toBuffers)
{
	BAutolock _(fEntryLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window == NULL || entry->window->GetDrawingEngine() == NULL)
			continue;
		if (toBuffers)
			entry->window->GetDrawingEngine()->SetHWInterface(entry->iface);
		else
			entry->window->GetDrawingEngine()->SetHWInterface(fScreen);
	}
	// M11 review: the resume ends the pause under the same lock, so a
	// window AddWindow binds in between is not left on the screen
	if (toBuffers)
		fPaused = false;
}


//!	No animation while paused (step 5) or after the switch to the CPU mode.
void
Compositor::_DropAnimations()
{
	BAutolock entryLock(fEntryLock);
	BAutolock _(fAnimLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++)
		fEntries.ItemAt(i)->animOpen = false;
	fGhosts.MakeEmpty();
	fSlideActive = false;
	fAnimationsRunning = false;
}


/*!	The CPU mode, or the device goes: every shared image that app_server
	holds goes back to its program, so none waits in its acquire for an
	image that will never be shown. Its next present is refused and it makes
	a copy swapchain (docs/APP-SERVER-COMPOSITOR.md, "The reset path").
*/
void
Compositor::_ReturnSharedImages()
{
	BAutolock _(fShareLock);
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		share->broken = true;
		if (share->ended) {
			// M11 review: app_server ended it, the program still waits
			if (share->giveBack)
				_ShareGiveBack(share);
			continue;
		}
		for (uint32 n = 0; n < share->count; n++) {
			Share::Image& image = share->images[n];
			if ((int32)n == share->current || (int32)n == share->pending
				|| image.retiring) {
				image.retiring = false;
				release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
			}
		}
		share->current = -1;
		share->pending = -1;
	}
}


//	#pragma mark - scanout (M11 step 3b, decision 4)


//!	Frees the imports of the back buffer and the scanout. GPU idle.
void
Compositor::_ScanoutFree(CompositorVulkan* vk)
{
	if (vk->back != VK_NULL_HANDLE)
		vk->vkDestroyBuffer(vk->device, vk->back, NULL);
	if (vk->backMemory != VK_NULL_HANDLE)
		vk->vkFreeMemory(vk->device, vk->backMemory, NULL);
	if (vk->scanout != VK_NULL_HANDLE)
		vk->vkDestroyBuffer(vk->device, vk->scanout, NULL);
	if (vk->scanoutMemory != VK_NULL_HANDLE)
		vk->vkFreeMemory(vk->device, vk->scanoutMemory, NULL);
	vk->back = VK_NULL_HANDLE;
	vk->backMemory = VK_NULL_HANDLE;
	vk->scanout = VK_NULL_HANDLE;
	vk->scanoutMemory = VK_NULL_HANDLE;
	vk->scanActive = false;
}


//!	One import of app_server's own export: a VkBuffer over it.
static bool
import_own(CompositorVulkan* vk, const uint8 token[16], uint64 size,
	VkBufferUsageFlags usage, bool deviceLocal, VkBuffer* _buffer,
	VkDeviceMemory* _memory)
{
	VkExternalMemoryBufferCreateInfo external = {};
	external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
	external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = &external;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, _buffer)
			!= VK_SUCCESS) {
		*_buffer = VK_NULL_HANDLE;
		return false;
	}
	VkMemoryRequirements requirements;
	vk->vkGetBufferMemoryRequirements(vk->device, *_buffer, &requirements);
	VkPhysicalDeviceMemoryProperties memory;
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &memory);
	uint32 type = UINT32_MAX;
	for (uint32 i = 0; i < memory.memoryTypeCount; i++) {
		if ((requirements.memoryTypeBits & (1u << i)) == 0)
			continue;
		bool local = (memory.memoryTypes[i].propertyFlags
			& VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
		if (type == UINT32_MAX || (deviceLocal && local)
			|| (!deviceLocal && i == vk->memoryType)) {
			type = i;
			if ((deviceLocal && local) || (!deviceLocal && i == vk->memoryType))
				break;
		}
	}
	int fd = -1;
	uint32 id = 0;
	team_id self = getpid();
	if (type == UINT32_MAX
		|| vk->tokenToFd(-1, token, self, &fd, &id) != 0)
		return false;
	VkImportMemoryFdInfoKHR import = {};
	import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
	import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	import.fd = fd;
	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.pNext = &import;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = type;
	if (vk->vkAllocateMemory(vk->device, &allocateInfo, NULL, _memory)
			!= VK_SUCCESS) {
		*_memory = VK_NULL_HANDLE;
		close(fd);
		return false;
	}
	uint64 bytes = 0;
	if (vk->importSize(id, &bytes) != 0 || requirements.size > bytes
		|| size > bytes)
		return false;
	return vk->vkBindBufferMemory(vk->device, *_buffer, *_memory, 0)
		== VK_SUCCESS;
}


/*!	Before a compose: imports GPUCopy's back buffer and the scanout when
	gpu_compositor_scanout is on and they are new (the first frame, a mode
	change). True when the way changed, so the whole screen is damaged.
	The GPU is idle here: the last compose waited for its fence.
*/
bool
Compositor::_ScanoutPrepare(RenderingBuffer* back)
{
	CompositorVulkan* vk = fVulkan;
	if (!fWantScanout || fOnLavapipe || vk == NULL || !vk->canImport
		|| back == NULL)
		return false;
	uint32 current = GPUCopy::Generation();
	if (vk->scanActive && vk->scanGeneration == current)
		return false;
	if (!vk->scanActive && vk->scanFailedGeneration == current)
		return false;

	bool was = vk->scanActive;
	_ScanoutFree(vk);
	uint8 backToken[16];
	uint8 scanToken[16];
	uint32 scanPitch = 0;
	uint32 generation = 0;
	const char* reason = NULL;
	if (!GPUCopy::ExportForCompositor(back, backToken, scanToken, &scanPitch,
			&generation))
		reason = "back buffer not on the gpu";
	else if (!import_own(vk, backToken,
			(uint64)back->BytesPerRow() * back->Height(),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
				| VK_BUFFER_USAGE_TRANSFER_SRC_BIT
				| VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, &vk->back,
			&vk->backMemory)
		|| !import_own(vk, scanToken, (uint64)scanPitch * back->Height(),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &vk->scanout,
			&vk->scanoutMemory))
		reason = "import";
	if (reason != NULL) {
		_ScanoutFree(vk);
		vk->scanFailedGeneration = GPUCopy::Generation();
		if (!fScanoutLogged) {
			fScanoutLogged = true;
			log_line("scanout through vulkan off: %s", reason);
		}
		return was;
	}
	vk->backPitch = back->BytesPerRow();
	vk->scanPitch = scanPitch;
	vk->scanGeneration = generation;
	vk->scanActive = true;
	log_line("scanout through vulkan %" B_PRIu32 "x%" B_PRIu32
		" back pitch %" B_PRIu32 " scanout pitch %" B_PRIu32,
		back->Width(), back->Height(), vk->backPitch, vk->scanPitch);
	return true;
}


//	#pragma mark - shared views (M11 step 3b)


/*!	"app_server: window <id> zero-copy off: <reason>", once per window.
	Called with all windows locked.
*/
void
Compositor::_ShareLog(Window* window, const char* reason)
{
	BAutolock _(fEntryLock);
	Entry* entry = _FindEntry(window);
	if (entry == NULL || entry->shareLogged)
		return;
	entry->shareLogged = true;
	int32 id = window->ServerWindow() != NULL
		? window->ServerWindow()->ClientToken() : -1;
	log_line("window %" B_PRId32 " zero-copy off: %s", id, reason);
}


/*!	"app_server: window <id> zero-copy on", once per window, when its
	first swapchain is imported (M11 stick, the counterpart of _ShareLog).
	Takes fEntryLock, so it is never called with fShareLock held.
*/
void
Compositor::_ShareLogOn(Window* window)
{
	BAutolock _(fEntryLock);
	Entry* entry = _FindEntry(window);
	if (entry == NULL || entry->shareOnLogged)
		return;
	entry->shareOnLogged = true;
	int32 id = window->ServerWindow() != NULL
		? window->ServerWindow()->ClientToken() : -1;
	log_line("window %" B_PRId32 " zero-copy on", id);
}


/*!	The view's part of the screen and the screen position of its pixel
	(0, 0), from its window. Called with all windows and fShareLock locked.
	A view that is gone, or that is in another window now, has no shape.
*/
void
Compositor::_ShareShape(Share* share)
{
	share->shape.MakeEmpty();
	Window* window = share->window;
	// M11 review: ~ServerWindow clears App() before ~Window ends the share.
	// Read once (third review): it can be cleared between two reads.
	if (window == NULL || share->ended || window->ServerWindow() == NULL)
		return;
	ServerApp* app = window->ServerWindow()->App();
	if (app == NULL)
		return;
	View* view = NULL;
	if (app->ViewTokens().GetToken(share->viewToken,
			B_HANDLER_TOKEN, (void**)&view) != B_OK
		|| view == NULL || view->Window() != window || !view->IsVisible())
		return;
	BRegion content;
	window->GetContentRegion(&content);
	share->shape = view->ScreenAndUserClipping(&content);
	IntRect bounds = view->Bounds();
	IntPoint origin(bounds.left, bounds.top);
	view->LocalToScreenTransform().Apply(&origin);
	share->left = origin.x;
	share->top = origin.y;
}


/*!	Frees the imports of a share, with the device they were made on. The
	library's GEM_CLOSE ends each import's id. Called with fShareLock.
*/
void
Compositor::_ShareFree(Share* share)
{
	CompositorVulkan* vk = fVulkan;
	for (uint32 i = 0; i < share->count; i++) {
		Share::Image& image = share->images[i];
		if (vk != NULL && share->generation == fGeneration) {
			if (image.buffer != VK_NULL_HANDLE)
				vk->vkDestroyBuffer(vk->device, image.buffer, NULL);
			if (image.memory != VK_NULL_HANDLE)
				vk->vkFreeMemory(vk->device, image.memory, NULL);
		}
		image.buffer = VK_NULL_HANDLE;
		image.memory = VK_NULL_HANDLE;
	}
	share->count = 0;
}


/*!	M11 review: gives every image app_server holds of this share back to
	the program (current, pending, overtaken), as _ReturnSharedImages does
	for all. Compose thread at the start of a frame, with fShareLock: the
	frame before waited for its fence, so no GPU work reads them.
*/
void
Compositor::_ShareGiveBack(Share* share)
{
	share->giveBack = false;
	for (uint32 n = 0; n < share->count; n++) {
		Share::Image& image = share->images[n];
		if ((int32)n == share->current || (int32)n == share->pending
			|| image.retiring) {
			image.retiring = false;
			release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
		}
	}
	share->current = -1;
	share->pending = -1;
}


//!	Frees the shares that ended. Compose thread, between frames.
void
Compositor::_ShareRetire()
{
	for (int32 i = fShares.CountItems(); i-- > 0;) {
		Share* share = fShares.ItemAt(i);
		if (!share->ended)
			continue;
		if (share->giveBack)
			_ShareGiveBack(share);
		_ShareFree(share);
		fShares.RemoveItemAt(i);
		delete share;
	}
}


/*!	Whether the ring has passed an image's present mark (from
	r5_drm_mark_get). FENCE_WAIT with timeout 0: the compose never waits
	for a program.
*/
bool
Compositor::_ShareReady(uint64 sequence)
{
	// 0: no submit was ever marked on this image (a new buffer, or a mark
	// set before the file's first submit). It is not shown; the WSI marks
	// after the copy into the image, so its mark is never 0.
	if (sequence == 0)
		return false;
	r5_gpu_fence wait;
	memset(&wait, 0, sizeof(wait));
	wait.size = sizeof(wait);
	wait.version = 1;
	wait.sequence = sequence;
	wait.timeout_us = 0;
	return fFenceFd >= 0
		&& ioctl(fFenceFd, R5_GPU_FENCE_WAIT, &wait, sizeof(wait)) == 0;
}


/*!	Gives back each overtaken image whose present mark the ring has
	passed: until then the copy into it may still run (review, M11 step
	3b). Compose thread; sets fShareRetiring while one still waits.
*/
void
Compositor::_ShareRetireDone()
{
	if (!fShareRetiring)
		return;
	BAutolock _(fShareLock);
	bool waiting = false;
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		for (uint32 n = 0; n < share->count; n++) {
			Share::Image& image = share->images[n];
			if (!image.retiring)
				continue;
			uint64 sequence = 0;
			bool gone = share->ended || share->broken || fVulkan == NULL
				|| share->generation != fGeneration
				|| fVulkan->markGet(image.importId, &sequence) != 0;
			if (gone) {
				// nothing reads it any more; an ended share's program
				// has stopped waiting on it. M11 review: not when
				// app_server ended it (giveBack): that program still waits.
				// Fourth review: so does the program of a share that did
				// not end (broken, a restart, a failed mark read).
				image.retiring = false;
				if (share->giveBack || !share->ended)
					release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
				continue;
			}
			if (!_ShareReady(sequence)) {
				waiting = true;
				continue;
			}
			image.retiring = false;
			release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
		}
	}
	fShareRetiring = waiting;
}


/*!	The start of a frame: frees what ended, shows each pending image whose
	present mark the ring has passed, gives the image it replaces back to
	the program, and copies what this frame draws. A pending image that is
	not ready yet keeps the previous one; its view goes into retry.
*/
void
Compositor::_ShareFrame(ShareDrawList& draws, BRegion& retry)
{
	BAutolock _(fShareLock);
	_ShareRetire();
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		if (share->broken || share->generation != fGeneration)
			continue;
		if (share->pending >= 0) {
			Share::Image& next = share->images[share->pending];
			uint64 sequence = 0;
			// M11 review: mark 0 never passes (the WSI marks after its
			// copy, so it is never 0); retrying it would compose every
			// refresh period for good. Such a share is broken.
			if (fVulkan->markGet(next.importId, &sequence) != 0
				|| sequence == 0) {
				share->broken = true;
				// M11 review: the program still waits on these
				_ShareGiveBack(share);
				continue;
			}
			if (_ShareReady(sequence)) {
				int32 old = share->current;
				share->current = share->pending;
				share->pending = -1;
				// the last frame that read it is done (its fence)
				if (old >= 0)
					release_sem_etc(share->images[old].sem, 1,
						B_DO_NOT_RESCHEDULE);
			} else
				retry.Include(&share->shape);
		}
		if (share->current < 0 || share->shape.CountRects() == 0)
			continue;
		Share::Image& image = share->images[share->current];
		ShareDraw* draw = new(std::nothrow) ShareDraw;
		if (draw == NULL)
			continue;
		draw->window = share->window;
		draw->buffer = image.buffer;
		draw->width = image.width;
		draw->height = image.height;
		draw->pitch = image.pitch;
		draw->shape = share->shape;
		draw->left = share->left;
		draw->top = share->top;
		if (!draws.AddItem(draw))
			delete draw;
	}
}


void
Compositor::ShareDeclined(Window* window)
{
	_ShareLog(window, "lavapipe");
}


/*!	Imports a Vulkan view's swapchain images. app_server believes the
	kernel: each image's real size (BO_IMPORT, through the library) must
	hold its pitch times its height, and its semaphore must belong to the
	connection's team. Any refusal refuses the whole swapchain, and the
	program keeps the copy path.
*/
status_t
Compositor::ShareImages(Window* window, int32 viewToken, team_id team,
	uint32 count, const compositor_share_image* images, int32* _shareId)
{
	if (!fActive)
		return B_NOT_ALLOWED;
	if (fOnLavapipe) {
		_ShareLog(window, "lavapipe");
		return B_NOT_ALLOWED;
	}
	if (atomic_get(&fZeroCopyOff) != 0) {
		_ShareLog(window, "setting");
		return B_NOT_ALLOWED;
	}
	CompositorVulkan* vk = fVulkan;
	if (vk == NULL || !vk->canImport || fFenceFd < 0) {
		_ShareLog(window, "no import");
		return B_NOT_ALLOWED;
	}
	if (count == 0 || count > kMaxShareImages)
		return B_BAD_VALUE;

	for (uint32 i = 0; i < count; i++) {
		sem_info info;
		if (get_sem_info(images[i].sem, &info) != B_OK
			|| !r5_share_sem_ok(info.team, team)) {
			_ShareLog(window, "sem");
			return B_NOT_ALLOWED;
		}
		if (images[i].format != (uint32)B_RGBA32
			&& images[i].format != (uint32)B_RGB32) {
			_ShareLog(window, "format");
			return B_BAD_VALUE;
		}
		if (images[i].pitch % 4 != 0
			|| !r5_share_size_ok(images[i].width, images[i].height,
				images[i].pitch, R5_SHARE_MAX_IMAGE_BYTES)) {
			_ShareLog(window, "size");
			return B_BAD_VALUE;
		}
	}

	Share* share = new(std::nothrow) Share;
	if (share == NULL)
		return B_NO_MEMORY;
	share->window = window;
	share->viewToken = viewToken;
	share->team = team;
	share->count = 0;
	share->generation = fGeneration;
	share->current = -1;
	share->pending = -1;
	share->ended = false;
	share->broken = false;
	share->giveBack = false;
	share->left = 0;
	share->top = 0;

	const char* reason = NULL;
	for (uint32 i = 0; i < count && reason == NULL; i++) {
		const compositor_share_image& in = images[i];
		Share::Image& image = share->images[i];
		memset(&image, 0, sizeof(image));
		share->count = i + 1;
		image.sem = in.sem;
		image.width = in.width;
		image.height = in.height;
		image.pitch = in.pitch;

		VkExternalMemoryBufferCreateInfo external = {};
		external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
		external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.pNext = &external;
		bufferInfo.size = (uint64)in.pitch * in.height;
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, &image.buffer)
				!= VK_SUCCESS) {
			image.buffer = VK_NULL_HANDLE;
			reason = "import";
			break;
		}
		VkMemoryRequirements requirements;
		vk->vkGetBufferMemoryRequirements(vk->device, image.buffer,
			&requirements);

		int fd = -1;
		if (vk->tokenToFd(-1, in.token, team, &fd, &image.importId) != 0) {
			reason = "import";
			break;
		}
		// RADV takes the fd on success; on a failure it stays ours.
		VkImportMemoryFdInfoKHR import = {};
		import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
		import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
		import.fd = fd;
		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.pNext = &import;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = vk->memoryType;
		if ((requirements.memoryTypeBits & (1u << vk->memoryType)) == 0
			|| vk->vkAllocateMemory(vk->device, &allocateInfo, NULL,
				&image.memory) != VK_SUCCESS) {
			image.memory = VK_NULL_HANDLE;
			close(fd);
			reason = "import";
			break;
		}
		// The kernel's size, not the program's: pitch times height, and
		// the buffer RADV binds, must fit in it.
		uint64 bytes = 0;
		if (vk->importSize(image.importId, &bytes) != 0
			|| !r5_share_size_ok(in.width, in.height, in.pitch, bytes)
			|| requirements.size > bytes) {
			reason = "size";
			break;
		}
		if (vk->vkBindBufferMemory(vk->device, image.buffer, image.memory, 0)
				!= VK_SUCCESS) {
			reason = "import";
			break;
		}
	}

	if (reason != NULL) {
		// Not in the list yet, so no lock: and _ShareLog takes fEntryLock,
		// which comes before fShareLock (RemoveWindow).
		_ShareFree(share);
		delete share;
		_ShareLog(window, reason);
		return B_NOT_ALLOWED;
	}
	{
		BAutolock _(fShareLock);
		// A view has one swapchain: an older one of the same view ends.
		// Fourth review: the compose thread is woken to free it, as for
		// ShareEnd, or it waits for the next damage.
		bool endedOld = false;
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* old = fShares.ItemAt(i);
			if (old->team == team && old->viewToken == viewToken
				&& !old->ended) {
				old->ended = true;
				old->giveBack = true;
				endedOld = true;
			}
		}
		if (endedOld)
			release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
		share->id = ++fShareCounter;
		if (!fShares.AddItem(share)) {
			_ShareFree(share);
			delete share;
			return B_NO_MEMORY;
		}
		_ShareShape(share);
		*_shareId = share->id;
	}
	// After fShareLock: _ShareLogOn takes fEntryLock, which comes first.
	_ShareLogOn(window);
	return B_OK;
}


/*!	A present: the image becomes pending. An image that was pending and
	was never shown goes back to the program at once. The compose shows
	the new one once its mark has passed.
*/
status_t
Compositor::SharePresent(team_id team, int32 viewToken, int32 shareId,
	uint32 index)
{
	BRegion damage;
	{
		BAutolock _(fShareLock);
		Share* share = NULL;
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* scan = fShares.ItemAt(i);
			if (scan->id == shareId && scan->team == team
				&& scan->viewToken == viewToken) {
				share = scan;
				break;
			}
		}
		// B_BAD_VALUE: not the view's swapchain any more (a newer one
		// replaced it); the program's new swapchain still shares.
		// B_NOT_ALLOWED: this view takes no shared images now (a lost
		// import, a reset, the CPU mode); the program takes the copy path.
		if (share == NULL || share->ended || index >= share->count)
			return B_BAD_VALUE;
		if (share->broken || !fActive)
			return B_NOT_ALLOWED;
		if ((int32)index != share->current && (int32)index != share->pending) {
			// The overtaken image is never shown, but the copy into it
			// may still run: it goes back once its mark has passed
			// (_ShareRetireDone), not at once.
			if (share->pending >= 0) {
				share->images[share->pending].retiring = true;
				fShareRetiring = true;
			}
			share->pending = index;
		}
		damage = share->shape;
	}
	Damage(damage);
	if (fShareRetiring)
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	return B_OK;
}


/*!	M11 fourth review: the window's ServerWindow is going, and its app may
	go before ~Window ends the shares (RemoveWindow). Until then
	ClippingChanged would look the views up in the app's token space. The
	shares end here; RemoveWindow finds none left.
*/
void
Compositor::ShareWindowGone(Window* window)
{
	bool ended = false;
	{
		BAutolock shareLock(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			if (share->window != window)
				continue;
			if (!share->ended)
				share->giveBack = true;
			share->ended = true;
			share->window = NULL;
			ended = true;
		}
	}
	if (ended)
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	M11 fourth review: a view of this window was hidden, shown, moved,
	resized, scrolled or removed. That changes no window's clipping, so
	ClippingChanged does not run; the shared views of the window get their
	place again here, or the last image stays where the view was. From the
	window's ServerWindow thread, with the window read-locked.
*/
void
Compositor::ShareViewsChanged(Window* window)
{
	BRegion damage;
	{
		BAutolock shareLock(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			if (share->window != window || share->ended)
				continue;
			BRegion before = share->shape;
			int32 left = share->left;
			int32 top = share->top;
			_ShareShape(share);
			if (before == share->shape && left == share->left
				&& top == share->top)
				continue;
			damage.Include(&before);
			damage.Include(&share->shape);
		}
	}
	if (damage.CountRects() > 0)
		Damage(damage);
}


/*!	The program's swapchain is gone. From the reply on, no semaphore of it
	is released; the imports are freed at the start of the next frame.
*/
void
Compositor::ShareEnd(team_id team, int32 viewToken, int32 shareId)
{
	BRegion damage;
	{
		BAutolock _(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			if (share->id == shareId && share->team == team
				&& share->viewToken == viewToken) {
				share->ended = true;
				// its semaphores may be gone: release none of them
				share->giveBack = false;
				damage = share->shape;
			}
		}
	}
	Damage(damage);
	// M11 review: a hidden view has no damage; the frame frees the imports
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	M11 stick: the zero-copy test switch, read again at most every report
	interval, so the stick can change it without a restart. Only new
	swapchains follow it. Review after M11: also without gpu_debug.
	Compose thread only.
*/
void
Compositor::_ReadZeroCopySwitch()
{
	if (!fDebug)
		return;
	static bigtime_t sLastRead = 0;
	bigtime_t now = system_time();
	if (now - sLastRead < kReportInterval)
		return;
	sLastRead = now;
	settings options;
	load_settings(options);
	int32 off = options.noZeroCopy ? 1 : 0;
	if (atomic_get_and_set(&fZeroCopyOff, off) != off)
		log_line("zero-copy setting %s", off != 0 ? "off" : "on");
}


void
Compositor::_Report()
{
	bigtime_t now = system_time();
	if (now - fReportStart < kReportInterval)
		return;

	bigtime_t cpu = thread_cpu_time();
	bigtime_t median = 0;
	if (fSampleCount > 0) {
		// insertion sort: at most 128 samples
		for (int32 i = 1; i < fSampleCount; i++) {
			bigtime_t value = fSamples[i];
			int32 j = i - 1;
			while (j >= 0 && fSamples[j] > value) {
				fSamples[j + 1] = fSamples[j];
				j--;
			}
			fSamples[j + 1] = value;
		}
		median = fSamples[fSampleCount / 2];
	}
	log_line("compose median %" B_PRId64 " us cpu %" B_PRId64
		" ms per 10 s", median, (cpu - fCpuAtReport) / 1000);
	// M13 step 2: where the compose's GPU time goes, per frame
	if (fPassFrames > 0) {
		log_line("compose passes us per frame copy %" B_PRId64 " blur %"
			B_PRId64 " shadow %" B_PRId64 " glass %" B_PRId64 " animation %"
			B_PRId64 " frames %" B_PRId32,
			fPassTime[kPassCopy] / fPassFrames,
			fPassTime[kPassBlur] / fPassFrames,
			fPassTime[kPassShadow] / fPassFrames,
			fPassTime[kPassGlass] / fPassFrames,
			fPassTime[kPassAnimation] / fPassFrames, fPassFrames);
	}
	memset(fPassTime, 0, sizeof(fPassTime));
	fPassFrames = 0;
	if (sComposeFrames > 0) {
		log_line("compose work per frame dispatches %" B_PRId64 " regions %"
			B_PRId64 " pixels %" B_PRId64 " frames %" B_PRId64,
			sComposeDispatches / sComposeFrames,
			sComposeRegions / sComposeFrames, sComposeCopied / sComposeFrames,
			sComposeFrames);
	}
	sComposeDispatches = 0;
	sComposeRegions = 0;
	sComposeCopied = 0;
	sComposeFrames = 0;
	// M11 stick: the whole app_server, for the FPS compare of zero-copy
	// and the copy path. A standard account cannot read another uid's
	// team usage, so app_server logs it itself.
	bigtime_t teamCpu = team_cpu_time();
	// M12, decision 9: the median of the drawing sessions (first command
	// until the pixels are in the window buffer) and app_server's CPU time
	if (fSkia != NULL && !fSkia->IsShutDown()) {
		bigtime_t drawMedian = 0;
		int32 drawCount = 0;
		fSkia->TakeDrawMedian(&drawMedian, &drawCount);
		log_line("draw median %" B_PRId64 " us cpu %" B_PRId64
			" ms per 10 s", drawMedian, (teamCpu - fTeamCpuAtReport) / 1000);
		// M12 step 5c: the profile of these 10 s
		int64 p[SkiaGpu::kProfileCount];
		fSkia->TakeProfile(p);
		int64 runs = p[SkiaGpu::kCountRuns] > 0 ? p[SkiaGpu::kCountRuns] : 1;
		log_line("draw profile runs %" B_PRId64 " per run us snap %" B_PRId64
			" insert %" B_PRId64 " submit %" B_PRId64 " deliver %" B_PRId64,
			p[SkiaGpu::kCountRuns], p[SkiaGpu::kTimeSnap] / runs,
			p[SkiaGpu::kTimeInsert] / runs, p[SkiaGpu::kTimeSubmit] / runs,
			p[SkiaGpu::kTimeDeliver] / runs);
		log_line("draw profile composite %" B_PRId64 " coverage %" B_PRId64
			" direct %" B_PRId64 " upload %" B_PRId64,
			p[SkiaGpu::kCountComposite], p[SkiaGpu::kCountCoverage],
			p[SkiaGpu::kCountDirect], p[SkiaGpu::kCountUpload]);
	} else {
		bigtime_t drawMedian = 0;
		int32 drawCount = 0;
		SkiaDrawingEngine::TakeCpuMedian(&drawMedian, &drawCount);
		log_line("draw cpu median %" B_PRId64 " us cpu %" B_PRId64
			" ms per 10 s", drawMedian, (teamCpu - fTeamCpuAtReport) / 1000);
	}
	log_line("team cpu %" B_PRId64 " ms per 10 s",
		(teamCpu - fTeamCpuAtReport) / 1000);
	fTeamCpuAtReport = teamCpu;
	_ReadZeroCopySwitch();
	// M11 step 4: what the median above was measured with
	uint32 effects = fEffects;
	log_line("compose effects shadow %s frosted %s strength %s",
		(effects & kWindowEffectShadow) != 0 ? "on" : "off",
		(effects & kWindowEffectFrostedGlass) != 0 ? "on" : "off",
		window_effect_level_for(fStrength).name);
	// Decision 4: the time after the fence, per way, for the stick report
	// (vulkan: the cursor only; cpu: the copy into the back buffer plus
	// CopyBackToFront, the M8 DMA_DATA copy when gpu_app_server is on).
	if (fPresentCount > 0) {
		for (int32 i = 1; i < fPresentCount; i++) {
			bigtime_t value = fPresentSamples[i];
			int32 j = i - 1;
			while (j >= 0 && fPresentSamples[j] > value) {
				fPresentSamples[j + 1] = fPresentSamples[j];
				j--;
			}
			fPresentSamples[j + 1] = value;
		}
		log_line("present median %" B_PRId64 " us (%s)",
			fPresentSamples[fPresentCount / 2],
			fVulkan != NULL && fVulkan->scanActive ? "vulkan" : "cpu");
		fPresentCount = 0;
	}
	// M13 step 0: only when something moved in these 10 s
	if (fFrameCount > 0) {
		sort_samples(fFrameSamples, fFrameCount);
		log_line("frame interval median %" B_PRId64 " us p95 %" B_PRId64
			" us frames %" B_PRId32, fFrameSamples[fFrameCount / 2],
			fFrameSamples[fFrameCount * 95 / 100], fFrameCount);
		fFrameCount = 0;
	}
	if (fInputCount > 0) {
		sort_samples(fInputSamples, fInputCount);
		log_line("input latency median %" B_PRId64 " us p95 %" B_PRId64
			" us moves %" B_PRId32, fInputSamples[fInputCount / 2],
			fInputSamples[fInputCount * 95 / 100], fInputCount);
		fInputCount = 0;
	}
	log_line("window memory %" B_PRIu64 " MB", fWindowBytes / (1024 * 1024));

	fSampleCount = 0;
	fCpuAtReport = cpu;
	fReportStart = now;
}
