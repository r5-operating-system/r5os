/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12: the Graphite context of app_server (SkiaGpu.h).
 */


#include "SkiaGpu.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>

#include <Autolock.h>
#include <FindDirectory.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkMilestone.h"
#include "include/core/SkSurface.h"
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/GraphiteTypes.h"
#include "include/gpu/graphite/PersistentPipelineStorage.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"
#include "include/gpu/graphite/vk/VulkanGraphiteTypes.h"
#include "include/gpu/MutableTextureState.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"

#include "SkiaPainter.h"


#ifndef R5_SKIA_VERSION
#	define R5_SKIA_VERSION "m158"
#endif


namespace ContextFactory = skgpu::graphite::ContextFactory;
using skgpu::graphite::BackendTexture;
using skgpu::graphite::ContextOptions;
using skgpu::graphite::InsertRecordingInfo;
using skgpu::graphite::InsertStatus;
using skgpu::graphite::Recorder;
using skgpu::graphite::RecorderOptions;
using skgpu::graphite::Recording;
using skgpu::graphite::SampleCount;
using skgpu::graphite::SubmitInfo;
using skgpu::graphite::SyncToCpu;
typedef skgpu::graphite::Context GraphiteContext;


static PFN_vkGetInstanceProcAddr sGetInstanceProc;
static PFN_vkGetDeviceProcAddr sGetDeviceProc;


// The smallest device-local heap: VRAM on a GPU with its own memory or a
// carve-out, the system memory on Lavapipe. 0 when there is none.
static size_t
smallest_local_heap(VkInstance instance, VkPhysicalDevice physical)
{
	PFN_vkGetPhysicalDeviceMemoryProperties getProperties
		= (PFN_vkGetPhysicalDeviceMemoryProperties)sGetInstanceProc(instance,
			"vkGetPhysicalDeviceMemoryProperties");
	if (getProperties == NULL)
		return 0;
	VkPhysicalDeviceMemoryProperties properties;
	getProperties(physical, &properties);
	VkDeviceSize smallest = 0;
	for (uint32_t i = 0; i < properties.memoryHeapCount; i++) {
		const VkMemoryHeap& heap = properties.memoryHeaps[i];
		if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0)
			continue;
		if (smallest == 0 || heap.size < smallest)
			smallest = heap.size;
	}
	return (size_t)smallest;
}


// One VkQueue, two threads: the draw thread submits Skia's work and the
// compose thread its frame. Vulkan wants every call that uses the queue
// externally synchronized, so Graphite's queue calls (through get_proc) and
// the compositor's (SkiaGpu::LockQueue()) take this lock.
static BLocker sQueueLock("vulkan queue");

// M13: signalled semaphores that wait for a compose, at most (each one is a
// sync object of the R5 GPU interface, 128 per program)
static const size_t kMaxSignalled = 8;

// M13 step 2: the pipeline cache on disk
static const bigtime_t kPipelineSyncInterval = 10000000;
static const size_t kPipelineDataLargest = 32 * 1024 * 1024;
static PFN_vkQueueSubmit sQueueSubmit;
static PFN_vkQueueWaitIdle sQueueWaitIdle;
static PFN_vkDeviceWaitIdle sDeviceWaitIdle;
static PFN_vkQueueBindSparse sQueueBindSparse;


static VKAPI_ATTR VkResult VKAPI_CALL
locked_queue_submit(VkQueue queue, uint32_t count, const VkSubmitInfo* submits,
	VkFence fence)
{
	BAutolock _(sQueueLock);
	return sQueueSubmit(queue, count, submits, fence);
}


static VKAPI_ATTR VkResult VKAPI_CALL
locked_queue_wait_idle(VkQueue queue)
{
	BAutolock _(sQueueLock);
	return sQueueWaitIdle(queue);
}


static VKAPI_ATTR VkResult VKAPI_CALL
locked_device_wait_idle(VkDevice device)
{
	BAutolock _(sQueueLock);
	return sDeviceWaitIdle(device);
}


static VKAPI_ATTR VkResult VKAPI_CALL
locked_queue_bind_sparse(VkQueue queue, uint32_t count,
	const VkBindSparseInfo* infos, VkFence fence)
{
	BAutolock _(sQueueLock);
	return sQueueBindSparse(queue, count, infos, fence);
}


static PFN_vkVoidFunction
get_proc(const char* name, VkInstance instance, VkDevice device)
{
	if (device == VK_NULL_HANDLE)
		return sGetInstanceProc(instance, name);
	PFN_vkVoidFunction function = sGetDeviceProc(device, name);
	if (function == NULL)
		return NULL;
	if (strcmp(name, "vkQueueSubmit") == 0) {
		sQueueSubmit = (PFN_vkQueueSubmit)function;
		return (PFN_vkVoidFunction)locked_queue_submit;
	}
	if (strcmp(name, "vkQueueWaitIdle") == 0) {
		sQueueWaitIdle = (PFN_vkQueueWaitIdle)function;
		return (PFN_vkVoidFunction)locked_queue_wait_idle;
	}
	if (strcmp(name, "vkDeviceWaitIdle") == 0) {
		sDeviceWaitIdle = (PFN_vkDeviceWaitIdle)function;
		return (PFN_vkVoidFunction)locked_device_wait_idle;
	}
	if (strcmp(name, "vkQueueBindSparse") == 0) {
		sQueueBindSparse = (PFN_vkQueueBindSparse)function;
		return (PFN_vkVoidFunction)locked_queue_bind_sparse;
	}
	return function;
}


// M13 step 2: the window images and the run's semaphores, made by
// app_server on the compositor's device
static VkDevice sDevice;
static VkQueue sQueue;
static PFN_vkQueueSubmit sRawQueueSubmit;
static PFN_vkCreateImage sCreateImage;
static PFN_vkDestroyImage sDestroyImage;
static PFN_vkGetImageMemoryRequirements sGetImageMemoryRequirements;
static PFN_vkAllocateMemory sAllocateMemory;
static PFN_vkFreeMemory sFreeMemory;
static PFN_vkBindImageMemory sBindImageMemory;
static PFN_vkCreateImageView sCreateImageView;
static PFN_vkDestroyImageView sDestroyImageView;
static PFN_vkCreateSemaphore sCreateSemaphore;
static PFN_vkDestroySemaphore sDestroySemaphore;
static PFN_vkCreateFence sCreateFence;
static PFN_vkDestroyFence sDestroyFence;
static PFN_vkResetFences sResetFences;
static PFN_vkWaitForFences sWaitForFences;
static VkPhysicalDeviceMemoryProperties sMemoryProperties;
static int32 sKeepImageMemory = 0;


SkiaImage::SkiaImage()
	:
	fImage(0),
	fView(0),
	fMemory(0),
	fWidth(0),
	fHeight(0),
	fTexture(NULL)
{
}


SkiaImage::~SkiaImage()
{
	// Every user is done: Graphite called the release proc after its last
	// command buffer with the image finished, and the last compose frame
	// that sampled it passed its fence.
	delete (BackendTexture*)fTexture;
	if (atomic_get(&sKeepImageMemory) != 0)
		return;
	if (fView != 0)
		sDestroyImageView(sDevice, (VkImageView)fView, NULL);
	if (fImage != 0)
		sDestroyImage(sDevice, (VkImage)fImage, NULL);
	if (fMemory != 0)
		sFreeMemory(sDevice, (VkDeviceMemory)fMemory, NULL);
}


/*static*/ void
SkiaGpu::KeepImageMemory()
{
	atomic_set(&sKeepImageMemory, 1);
}


/*static*/ void
SkiaGpu::LockQueue()
{
	sQueueLock.Lock();
}


/*static*/ void
SkiaGpu::UnlockQueue()
{
	sQueueLock.Unlock();
}


/*!	M13 step 2 (step 1's item, owner 16 Nov 2026): Graphite's Vulkan
	pipeline cache on disk, so the warm-up after the first boot is short.
	app_server runs as uid 0, so the file is in the system cache directory
	(/boot/system/cache/app_server), not in a user's ~/config/cache. Its
	header holds a key made of the Skia version, the device and the driver
	(driverVersion and pipelineCacheUUID, which change with every Mesa
	build), so a Mesa update throws it away. A file that is too short or
	too long, or has another key, another size or a wrong checksum, is
	ignored and replaced at the next store.
*/
class PipelineStorage : public skgpu::graphite::PersistentPipelineStorage {
public:
	PipelineStorage(const VkPhysicalDeviceProperties& properties)
	{
		snprintf(fKey, sizeof(fKey), "r5 skia %s vendor %#" B_PRIx32
			" device %#" B_PRIx32 " driver %#" B_PRIx32 " %s uuid ",
			R5_SKIA_VERSION, (uint32)properties.vendorID,
			(uint32)properties.deviceID, (uint32)properties.driverVersion,
			properties.deviceName);
		size_t length = strlen(fKey);
		for (uint32 i = 0; i < VK_UUID_SIZE && length + 3 < sizeof(fKey); i++) {
			snprintf(fKey + length, sizeof(fKey) - length, "%02x",
				properties.pipelineCacheUUID[i]);
			length += 2;
		}
		fPath[0] = '\0';
		char directory[B_PATH_NAME_LENGTH];
		if (find_directory(B_SYSTEM_CACHE_DIRECTORY, -1, true, directory,
				sizeof(directory)) == B_OK) {
			strlcat(directory, "/app_server", sizeof(directory));
			mkdir(directory, 0755);
			snprintf(fPath, sizeof(fPath), "%s/skia-pipelines", directory);
		}
	}

	sk_sp<SkData> load() override
	{
		if (fPath[0] == '\0')
			return nullptr;
		int fd = open(fPath, O_RDONLY);
		if (fd < 0) {
			syslog(LOG_INFO, "app_server: gpu draw pipeline cache none\n");
			return nullptr;
		}
		struct stat st;
		sk_sp<SkData> data;
		if (fstat(fd, &st) == 0 && st.st_size > (off_t)sizeof(Header)
			&& st.st_size <= (off_t)(sizeof(Header) + kLargest)) {
			Header header;
			size_t size = (size_t)st.st_size - sizeof(Header);
			sk_sp<SkData> bytes = SkData::MakeUninitialized(size);
			if (read(fd, &header, sizeof(header)) == (ssize_t)sizeof(header)
				&& read(fd, bytes->writable_data(), size) == (ssize_t)size
				&& memcmp(header.magic, kMagic, sizeof(header.magic)) == 0
				&& strncmp(header.key, fKey, sizeof(header.key)) == 0
				&& header.size == size
				&& header.checksum == _Checksum(bytes->data(), size))
				data = bytes;
		}
		close(fd);
		syslog(LOG_INFO, "app_server: gpu draw pipeline cache %s\n",
			data ? "loaded" : "ignored");
		return data;
	}

	void store(const SkData& data) override
	{
		if (fPath[0] == '\0' || data.size() == 0 || data.size() > kLargest)
			return;
		Header header;
		memset(&header, 0, sizeof(header));
		memcpy(header.magic, kMagic, sizeof(header.magic));
		strlcpy(header.key, fKey, sizeof(header.key));
		header.size = data.size();
		header.checksum = _Checksum(data.data(), data.size());
		// a new file, renamed over the old one: a crash leaves the old
		// file or the new one, never half of one
		char temporary[B_PATH_NAME_LENGTH + 8];
		snprintf(temporary, sizeof(temporary), "%s.new", fPath);
		int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0)
			return;
		bool ok = write(fd, &header, sizeof(header)) == (ssize_t)sizeof(header)
			&& write(fd, data.data(), data.size()) == (ssize_t)data.size();
		close(fd);
		if (!ok || rename(temporary, fPath) != 0) {
			unlink(temporary);
			return;
		}
		syslog(LOG_INFO, "app_server: gpu draw pipeline cache stored %zu KB\n",
			data.size() / 1024);
	}

private:
	struct Header {
		char		magic[8];
		char		key[256];
		uint64		size;
		uint64		checksum;
	};

	static uint64 _Checksum(const void* data, size_t size)
	{
		// FNV-1a: finds a damaged file, not an attack (only uid 0 writes)
		const uint8* bytes = (const uint8*)data;
		uint64 hash = 0xcbf29ce484222325ull;
		for (size_t i = 0; i < size; i++) {
			hash ^= bytes[i];
			hash *= 0x100000001b3ull;
		}
		return hash;
	}

	static const size_t	kLargest = 64 * 1024 * 1024;
	static constexpr char kMagic[8] = {'R', '5', 'S', 'K', 'P', 'C', '1',
		'\0'};
			char				fKey[256];
			char				fPath[B_PATH_NAME_LENGTH];
};


struct SkiaBackend {
	skgpu::VulkanExtensions			extensions;
	VkPhysicalDeviceFeatures2		features;
	skgpu::VulkanBackendContext		backend;
	// M13 step 2: outlives the context (ContextOptions)
	std::unique_ptr<PipelineStorage> pipelines;
};


struct WorkItem;
// review: items of a RunWork() whose submit failed; Graphite may still call
// read_done() for them until the context is destroyed
static std::vector<std::unique_ptr<WorkItem>> sLeftovers;


struct WorkItem {
	SkiaTarget*			target;
	std::unique_ptr<Recording> recording;
	sk_sp<SkSurface>	surface;
	BRegion				damage;
	clipping_rect		frame;
	// the target's origin at the snap, under its lock
	int32				left;
	int32				top;
	bigtime_t			firstDraw;
	void*				cookie;
	bool				read;
	bool				delivered;
	// M13 step 2: a window's image, which stays on the GPU
	BReference<SkiaImage> image;
};


static void
read_done(SkImage::ReadPixelsContext context,
	std::unique_ptr<const SkImage::AsyncReadResult> result)
{
	WorkItem* item = (WorkItem*)context;
	item->read = true;
	if (!result || result->count() < 1)
		return;
	item->target->Deliver(item->cookie, item->damage, item->frame,
		(const uint8*)result->data(0), (uint32)result->rowBytes(0));
	item->delivered = true;
}


SkiaGpu::SkiaGpu()
	:
	fLock("skia gpu"),
	fTargets(20),
	fContext(NULL),
	fBackend(NULL),
	fForceFail(false),
	fShutDown(false),
	fRecorderBudget(RecorderOptions::kDefaultRecorderBudget),
	fWake(NULL),
	fWakeCookie(NULL),
	fWakeRequested(0),
	fFailure(NULL),
	fComposeBlocked(0),
	fTicket(0),
	fDone(0),
	fDoneSem(-1),
	fWaiters(0),
	fStatsLock("skia stats"),
	fSampleCount(0),
	fQueueFamily(0),
	fRunFence(0),
	fRunInFlight(false),
	fPipelinesSynced(0)
{
	fDoneSem = create_sem(0, "skia done");
}


SkiaGpu::~SkiaGpu()
{
	DestroyContext();
	if (fDoneSem >= 0)
		delete_sem(fDoneSem);
}


void
SkiaGpu::DestroyContext()
{
	Shutdown();
	BAutolock _(fLock);
	// M13 step 2: what the session compiled, for the next boot
	if (fContext != NULL)
		((GraphiteContext*)fContext)->syncPipelineData(kPipelineDataLargest);
	delete (GraphiteContext*)fContext;
	fContext = NULL;
	sLeftovers.clear();
	_DestroySemaphores();
	SkiaBackend* backend = (SkiaBackend*)fBackend;
	if (backend != NULL)
		backend->backend.fMemoryAllocator.reset();
	delete backend;
	fBackend = NULL;
}


/*static*/ const char*
SkiaGpu::Version()
{
	return R5_SKIA_VERSION;
}


/*static*/ SkiaGpu*
SkiaGpu::Create(const skia_vulkan_device& device, bool forceFail,
	int samples, const char** _reason)
{
	SkiaGpu* gpu = new(std::nothrow) SkiaGpu;
	SkiaBackend* backend = new(std::nothrow) SkiaBackend;
	if (gpu == NULL || backend == NULL || gpu->fDoneSem < 0) {
		delete gpu;
		delete backend;
		*_reason = "no memory";
		return NULL;
	}
	gpu->fBackend = backend;
	gpu->fForceFail = forceFail;

	sGetInstanceProc = (PFN_vkGetInstanceProcAddr)device.getInstanceProc;
	VkInstance instance = (VkInstance)device.instance;
	sGetDeviceProc = (PFN_vkGetDeviceProcAddr)sGetInstanceProc(instance,
		"vkGetDeviceProcAddr");
	if (sGetDeviceProc == NULL) {
		delete gpu;
		*_reason = "no vkGetDeviceProcAddr";
		return NULL;
	}

	VkPhysicalDevice physical = (VkPhysicalDevice)device.physicalDevice;

	// M13 step 2: what app_server calls itself for the window images
	sDevice = (VkDevice)device.device;
	sQueue = (VkQueue)device.queue;
	gpu->fQueueFamily = device.queueFamily;
#define R5_DEVICE_PROC(variable, name) \
	variable = (PFN_##name)sGetDeviceProc(sDevice, #name)
	R5_DEVICE_PROC(sRawQueueSubmit, vkQueueSubmit);
	R5_DEVICE_PROC(sCreateImage, vkCreateImage);
	R5_DEVICE_PROC(sDestroyImage, vkDestroyImage);
	R5_DEVICE_PROC(sGetImageMemoryRequirements, vkGetImageMemoryRequirements);
	R5_DEVICE_PROC(sAllocateMemory, vkAllocateMemory);
	R5_DEVICE_PROC(sFreeMemory, vkFreeMemory);
	R5_DEVICE_PROC(sBindImageMemory, vkBindImageMemory);
	R5_DEVICE_PROC(sCreateImageView, vkCreateImageView);
	R5_DEVICE_PROC(sDestroyImageView, vkDestroyImageView);
	R5_DEVICE_PROC(sCreateSemaphore, vkCreateSemaphore);
	R5_DEVICE_PROC(sDestroySemaphore, vkDestroySemaphore);
	R5_DEVICE_PROC(sCreateFence, vkCreateFence);
	R5_DEVICE_PROC(sDestroyFence, vkDestroyFence);
	R5_DEVICE_PROC(sResetFences, vkResetFences);
	R5_DEVICE_PROC(sWaitForFences, vkWaitForFences);
#undef R5_DEVICE_PROC
	PFN_vkGetPhysicalDeviceMemoryProperties getMemory
		= (PFN_vkGetPhysicalDeviceMemoryProperties)sGetInstanceProc(instance,
			"vkGetPhysicalDeviceMemoryProperties");
	VkFence runFence = VK_NULL_HANDLE;
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (sRawQueueSubmit == NULL || sCreateImage == NULL
		|| sDestroyImage == NULL || sGetImageMemoryRequirements == NULL
		|| sAllocateMemory == NULL || sFreeMemory == NULL
		|| sBindImageMemory == NULL || sCreateImageView == NULL
		|| sDestroyImageView == NULL || sCreateSemaphore == NULL
		|| sDestroySemaphore == NULL || sCreateFence == NULL
		|| sDestroyFence == NULL || sResetFences == NULL
		|| sWaitForFences == NULL || getMemory == NULL
		|| sCreateFence(sDevice, &fenceInfo, NULL, &runFence)
			!= VK_SUCCESS) {
		delete gpu;
		*_reason = "no vulkan image calls";
		return NULL;
	}
	gpu->fRunFence = (uint64)runFence;
	getMemory(physical, &sMemoryProperties);

	backend->extensions.init(get_proc, instance, physical, 0, NULL,
		device.deviceExtensionCount,
		(const char* const*)device.deviceExtensions);
	// the compositor's device turns no feature on; Skia reads every one as
	// optional (part 2)
	memset(&backend->features, 0, sizeof(backend->features));
	backend->features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

	skgpu::VulkanBackendContext& context = backend->backend;
	context.fInstance = instance;
	context.fPhysicalDevice = physical;
	context.fDevice = (VkDevice)device.device;
	context.fQueue = (VkQueue)device.queue;
	context.fGraphicsQueueIndex = device.queueFamily;
	context.fMaxAPIVersion = device.apiVersion;
	context.fVkExtensions = &backend->extensions;
	context.fDeviceFeatures2 = &backend->features;
	context.fGetProc = get_proc;
	// every window records on its own thread (part 2)
	context.fMemoryAllocator = skgpu::VulkanMemoryAllocators::Make(context,
		static_cast<skgpu::ThreadSafe>(true));
	if (!context.fMemoryAllocator) {
		delete gpu;
		*_reason = "no memory allocator";
		return NULL;
	}

	ContextOptions options;
	// 5625U stick, boot 1: Skia's budgets are 256 MB for the context and
	// 256 MB for every recorder (one per window), more than the 256 MB of
	// VRAM of a Renoir with the default carve-out; the kernel refused the
	// next buffer and the drawing turned off ("insert failed"). The
	// budgets follow the smallest device-local heap the GPU reports: a
	// quarter for the context, a sixteenth for each window, never more
	// than Skia's own defaults (so Lavapipe, whose heap is system memory,
	// keeps them).
	size_t heap = smallest_local_heap(instance, physical);
	if (heap > 0) {
		options.fGpuBudgetInBytes = std::min(heap / 4,
			ContextOptions::kDefaultContextBudget);
		gpu->fRecorderBudget = std::min(heap / 16,
			RecorderOptions::kDefaultRecorderBudget);
	}
	syslog(LOG_INFO, "app_server: gpu draw budget heap %zu MB context %zu MB window "
		"%zu MB\n", heap >> 20, options.fGpuBudgetInBytes >> 20,
		gpu->fRecorderBudget >> 20);
	// M13 step 2: the pipeline cache on disk
	PFN_vkGetPhysicalDeviceProperties getProperties
		= (PFN_vkGetPhysicalDeviceProperties)sGetInstanceProc(instance,
			"vkGetPhysicalDeviceProperties");
	if (getProperties != NULL) {
		VkPhysicalDeviceProperties properties;
		getProperties(physical, &properties);
		backend->pipelines.reset(new(std::nothrow) PipelineStorage(
			properties));
		options.fPersistentPipelineStorage = backend->pipelines.get();
	}

	// Q3: curved shapes on the GPU with 4x MSAA, no CPU path atlas
	// M13 step 3: gpu_lavapipe_msaa 1 on Lavapipe: no MSAA, Skia's own
	// coverage for paths (4x MSAA is four times the software
	// rasterisation there)
	options.fInternalMultisampleCount = samples == 1 ? SampleCount::k1
		: SampleCount::k4;
	options.fMinimumPathSizeForMSAA = 0;
	std::unique_ptr<GraphiteContext> graphite = ContextFactory::MakeVulkan(context,
		options);
	if (!graphite) {
		delete gpu;
		*_reason = "no graphite context";
		return NULL;
	}
	gpu->fContext = graphite.release();
	return gpu;
}


static SkiaGpu* sCurrent = NULL;
static int64 sProfile[SkiaGpu::kProfileCount];


/*static*/ void
SkiaGpu::Profile(int which, int64 value)
{
	if (which >= 0 && which < kProfileCount)
		atomic_add64(&sProfile[which], value);
}


void
SkiaGpu::TakeProfile(int64 values[kProfileCount])
{
	for (int i = 0; i < kProfileCount; i++)
		values[i] = atomic_get_and_set64(&sProfile[i], 0);
}


/*static*/ SkiaGpu*
SkiaGpu::Current()
{
	return sCurrent;
}


/*static*/ void
SkiaGpu::SetCurrent(SkiaGpu* gpu)
{
	sCurrent = gpu;
}


void
SkiaGpu::SetWaker(void (*wake)(void*), void* cookie)
{
	fWake = wake;
	fWakeCookie = cookie;
}


// M13 step 2: device-local memory first (VRAM on RADV), any other type the
// image allows when that fails (the R5 DRM library itself moves a full
// VRAM to GTT, #4305; Lavapipe has only host memory).
static VkDeviceMemory
allocate_image_memory(const VkMemoryRequirements& requirements)
{
	for (int pass = 0; pass < 2; pass++) {
		for (uint32 i = 0; i < sMemoryProperties.memoryTypeCount; i++) {
			if ((requirements.memoryTypeBits & (1u << i)) == 0)
				continue;
			bool local = (sMemoryProperties.memoryTypes[i].propertyFlags
				& VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
			if (pass == 0 && !local)
				continue;
			VkMemoryAllocateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			info.allocationSize = requirements.size;
			info.memoryTypeIndex = i;
			VkDeviceMemory memory;
			if (sAllocateMemory(sDevice, &info, NULL, &memory) == VK_SUCCESS)
				return memory;
		}
	}
	return VK_NULL_HANDLE;
}


SkiaImage*
SkiaGpu::NewImage(int32 width, int32 height)
{
	if (fShutDown || width <= 0 || height <= 0)
		return NULL;
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		| VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		| VK_IMAGE_USAGE_TRANSFER_DST_BIT
		| VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = VK_FORMAT_B8G8R8A8_UNORM;
	info.extent.width = (uint32)width;
	info.extent.height = (uint32)height;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImage image;
	if (sCreateImage(sDevice, &info, NULL, &image) != VK_SUCCESS)
		return NULL;
	VkMemoryRequirements requirements;
	sGetImageMemoryRequirements(sDevice, image, &requirements);
	VkDeviceMemory memory = allocate_image_memory(requirements);
	if (memory == VK_NULL_HANDLE
		|| sBindImageMemory(sDevice, image, memory, 0) != VK_SUCCESS) {
		if (memory != VK_NULL_HANDLE)
			sFreeMemory(sDevice, memory, NULL);
		sDestroyImage(sDevice, image, NULL);
		return NULL;
	}
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = info.format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	VkImageView view;
	if (sCreateImageView(sDevice, &viewInfo, NULL, &view) != VK_SUCCESS) {
		sFreeMemory(sDevice, memory, NULL);
		sDestroyImage(sDevice, image, NULL);
		return NULL;
	}

	skgpu::graphite::VulkanTextureInfo textureInfo(VK_SAMPLE_COUNT_1_BIT,
		skgpu::Mipmapped::kNo, 0, info.format, info.tiling, usage,
		info.sharingMode, VK_IMAGE_ASPECT_COLOR_BIT,
		skgpu::VulkanYcbcrConversionInfo());
	skgpu::VulkanAlloc alloc;
	alloc.fMemory = memory;
	alloc.fSize = requirements.size;
	BackendTexture* texture = new(std::nothrow) BackendTexture(
		skgpu::graphite::BackendTextures::MakeVulkan(
			SkISize::Make(width, height), textureInfo,
			VK_IMAGE_LAYOUT_UNDEFINED, fQueueFamily, image, alloc));
	SkiaImage* result = new(std::nothrow) SkiaImage;
	if (texture == NULL || result == NULL || !texture->isValid()) {
		delete texture;
		delete result;
		sDestroyImageView(sDevice, view, NULL);
		sFreeMemory(sDevice, memory, NULL);
		sDestroyImage(sDevice, image, NULL);
		return NULL;
	}
	result->fImage = (uint64)image;
	result->fView = (uint64)view;
	result->fMemory = (uint64)memory;
	result->fWidth = width;
	result->fHeight = height;
	result->fTexture = texture;
	return result;
}


//!	Queue lock held: a semaphore for one run to signal.
uint64
SkiaGpu::_Signal()
{
	if (!fFreeSemaphores.empty()) {
		uint64 semaphore = fFreeSemaphores.back();
		fFreeSemaphores.pop_back();
		return semaphore;
	}
	VkSemaphoreCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkSemaphore semaphore;
	if (sCreateSemaphore(sDevice, &info, NULL, &semaphore) != VK_SUCCESS)
		return 0;
	return (uint64)semaphore;
}


void
SkiaGpu::TakeSignals(std::vector<uint64>& signals)
{
	BAutolock _(sQueueLock);
	signals.swap(fSignalled);
	fSignalled.clear();
}


void
SkiaGpu::ReturnSignals(const std::vector<uint64>& signals)
{
	BAutolock _(sQueueLock);
	for (size_t i = 0; i < signals.size(); i++)
		fFreeSemaphores.push_back(signals[i]);
}


//!	The device is idle or gone with the context: every semaphore goes.
void
SkiaGpu::_DestroySemaphores()
{
	BAutolock _(sQueueLock);
	if (sDestroySemaphore != NULL) {
		for (size_t i = 0; i < fFreeSemaphores.size(); i++)
			sDestroySemaphore(sDevice, (VkSemaphore)fFreeSemaphores[i], NULL);
		for (size_t i = 0; i < fSignalled.size(); i++)
			sDestroySemaphore(sDevice, (VkSemaphore)fSignalled[i], NULL);
	}
	fFreeSemaphores.clear();
	fSignalled.clear();
	if (fRunFence != 0 && sDestroyFence != NULL)
		sDestroyFence(sDevice, (VkFence)fRunFence, NULL);
	fRunFence = 0;
}


void*
SkiaGpu::NewRecorder()
{
	BAutolock _(fLock);
	if (fShutDown || fContext == NULL)
		return NULL;
	RecorderOptions options;
	options.fGpuBudgetInBytes = fRecorderBudget;
	std::unique_ptr<Recorder> recorder
		= ((GraphiteContext*)fContext)->makeRecorder(options);
	return recorder.release();
}


void
SkiaGpu::DeleteRecorder(void* recorder)
{
	BAutolock _(fLock);
	delete (Recorder*)recorder;
}


void
SkiaGpu::AddTarget(SkiaTarget* target)
{
	BAutolock _(fLock);
	fTargets.AddItem(target);
}


void
SkiaGpu::RemoveTarget(SkiaTarget* target)
{
	BAutolock _(fLock);
	fTargets.RemoveItem(target);
}


void
SkiaGpu::Wake()
{
	if (fWake != NULL && atomic_get_and_set(&fWakeRequested, 1) == 0)
		fWake(fWakeCookie);
}


bool
SkiaGpu::RunWork(const char** _reason)
{
	BAutolock _(fLock);
	// a target that records from now on wakes the thread again
	atomic_set(&fWakeRequested, 0);
	int64 ticket = atomic_get64(&fTicket);
	GraphiteContext* context = (GraphiteContext*)fContext;
	if (fShutDown || context == NULL) {
		atomic_set64(&fDone, ticket);
		_ReleaseWaiters();
		return true;
	}

	// review 5: a window whose image could not be made stops the GPU
	// drawing of the whole desktop (decisions 1 and 8)
	if (fFailure != NULL) {
		*_reason = fFailure;
		atomic_set64(&fDone, ticket);
		_ReleaseWaiters();
		return false;
	}

	// M13 step 2: one run on the GPU at a time. Without this wait the
	// draw thread sent two to three times as many, smaller runs as the GPU
	// could do (QEMU, Lavapipe: 143-177 runs per 10 s against 60 with
	// SyncToCpu), and the composes waited behind them (frame 205 ms
	// against 138 ms). Waiting here, before the snap, lets the windows'
	// drawing gather into the next run while the GPU works, as before;
	// the compose itself never waits on the CPU.
	if (fRunInFlight) {
		VkFence fence = (VkFence)fRunFence;
		bigtime_t waitStart = system_time();
		VkResult result;
		do {
			// a late GPU is not a failure (M11 decision 14)
			result = sWaitForFences(sDevice, 1, &fence, VK_TRUE,
				1000000000ull);
		} while (result == VK_TIMEOUT);
		fRunInFlight = false;
		Profile(kTimeDeliver, system_time() - waitStart);
		if (result != VK_SUCCESS) {
			*_reason = "device lost";
			atomic_set64(&fDone, ticket);
			_ReleaseWaiters();
			return false;
		}
	}
	// finished work calls Graphite's release procs (the window images it
	// let go of) here
	context->checkAsyncWorkCompletion();

	bigtime_t profileStart = system_time();
	// a Finish() waits for everything drawn so far: no window is held
	bool holds = atomic_get(&fWaiters) == 0;
	std::vector<std::unique_ptr<WorkItem>> items;
	for (int32 i = 0; i < fTargets.CountItems(); i++) {
		SkiaTarget* target = fTargets.ItemAt(i);
		if (!target->Lock())
			continue;
		if (target->HasWork() && target->HasSurface()
			&& !(holds && target->Held(profileStart))) {
			std::unique_ptr<WorkItem> item(new(std::nothrow) WorkItem);
			if (item) {
				item->target = target;
				item->recording = target->Snap(item->damage, &item->firstDraw);
				item->left = target->Left();
				item->top = target->Top();
				item->surface = sk_ref_sp(target->Surface());
				BRegion surface(BRect(target->Left(), target->Top(),
					target->Left() + target->Width() - 1,
					target->Top() + target->Height() - 1));
				item->damage.IntersectWith(&surface);
				item->frame = item->damage.FrameInt();
				item->read = false;
				item->delivered = false;
				item->image.SetTo(target->SharedImage());
				item->cookie = target->PrepareDelivery();
				items.push_back(std::move(item));
			}
		}
		target->Unlock();
	}

	bigtime_t profileSnapped = system_time();
	if (!items.empty()) {
		Profile(kCountRuns);
		Profile(kTimeSnap, profileSnapped - profileStart);
	}
	bool ok = true;
	if (!items.empty() && fForceFail) {
		// gpu_draw_fail: the first submit with work counts as failed
		fForceFail = false;
		*_reason = "forced failure";
		ok = false;
	}
	for (size_t i = 0; ok && i < items.size(); i++) {
		if (!items[i]->recording)
			continue;
		InsertRecordingInfo info;
		info.fRecording = items[i]->recording.get();
		// No second insert of the same recording after a failure: Skia's
		// QueueManager keeps the commands it already added (review,
		// 16 Nov 2026), so they would run twice. A full VRAM no longer
		// fails here since the R5 DRM library falls back to GTT (#4305).
		if (context->insertRecording(info) != InsertStatus::kSuccess) {
			*_reason = "insert failed";
			ok = false;
		}
	}
	bigtime_t profileInserted = system_time();
	if (!items.empty())
		Profile(kTimeInsert, profileInserted - profileSnapped);
	// M13 step 2: a window's image ends every run ready for the compose's
	// sampler; Graphite takes it from there for its next drawing
	bool shared = false;
	bool readBack = false;
	for (size_t i = 0; ok && i < items.size(); i++) {
		WorkItem* item = items[i].get();
		if (item->image.IsSet()) {
			shared = true;
			if (!context->setMutableState(
					*(BackendTexture*)item->image->Texture(),
					skgpu::MutableTextureStates::MakeVulkan(
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						fQueueFamily))) {
				*_reason = "image layout";
				ok = false;
			}
			continue;
		}
		if (item->damage.CountRects() == 0)
			continue;
		readBack = true;
		SkIRect rect = SkIRect::MakeLTRB(
			item->frame.left - item->left,
			item->frame.top - item->top,
			item->frame.right + 1 - item->left,
			item->frame.bottom + 1 - item->top);
		SkImageInfo info = SkImageInfo::Make(rect.width(), rect.height(),
			kBGRA_8888_SkColorType, kPremul_SkAlphaType, nullptr);
		context->asyncRescaleAndReadPixels(item->surface.get(), info, rect,
			SkImage::RescaleGamma::kSrc, SkImage::RescaleMode::kNearest,
			read_done, item);
	}
	if (ok && !items.empty()) {
		// M13 step 2: no CPU wait (SyncToCpu::kNo). Right after Graphite's
		// submit, under the same queue lock, an empty batch signals the
		// semaphore the compose waits on: its signal covers everything
		// submitted before it, and no compose submit lands in between.
		// Only a read-back waits, for a fence of that same batch.
		SubmitInfo submit;
		submit.fSync = SyncToCpu::kNo;
		bigtime_t submitStart = system_time();
		// the fence: the next run waits for it, a read-back right now
		VkFence fence = (VkFence)fRunFence;
		bool submitted;
		const char* failure = NULL;
		{
			BAutolock queueLock(sQueueLock);
			submitted = context->submit(submit);
			if (!submitted)
				failure = "submit failed: graphite";
			if (submitted) {
				// 5625U stick, 17 Nov 2026: at most kMaxSignalled wait for
				// a compose. A pass without a compose used to wait for them
				// in an empty batch; RADV turns an empty submit with waits
				// into sync files (radv_amdgpu_cs_submit_zero), which the
				// R5 DRM library has not, and the device was lost. Now they
				// wait for the next compose, and a run beyond the limit
				// signals none: it is done on the GPU before its damage
				// goes out anyway (the run's fence, below).
				bool signal = shared && fSignalled.size() < kMaxSignalled;
				VkSemaphore semaphore = signal
					? (VkSemaphore)_Signal() : VK_NULL_HANDLE;
				VkSubmitInfo batch = {};
				batch.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				batch.signalSemaphoreCount = semaphore != VK_NULL_HANDLE
					? 1 : 0;
				batch.pSignalSemaphores = &semaphore;
				if (fence != VK_NULL_HANDLE)
					sResetFences(sDevice, 1, &fence);
				VkResult batchResult = VK_SUCCESS;
				if (signal && semaphore == VK_NULL_HANDLE)
					failure = "submit failed: no semaphore";
				else if ((batchResult = sRawQueueSubmit(sQueue, 1, &batch,
						fence)) != VK_SUCCESS)
					failure = "submit failed: signal batch";
				if (failure != NULL) {
					// 5625U stick, 17 Nov 2026: say which and why
					syslog(LOG_INFO, "app_server: gpu draw %s, result %d, "
						"semaphores %zu free %zu signalled\n", failure,
						(int)batchResult, fFreeSemaphores.size(),
						fSignalled.size());
					if (semaphore != VK_NULL_HANDLE)
						sDestroySemaphore(sDevice, semaphore, NULL);
					submitted = false;
				} else {
					fRunInFlight = true;
					if (semaphore != VK_NULL_HANDLE)
						fSignalled.push_back((uint64)semaphore);
				}
			}
		}
		if (!submitted) {
			*_reason = failure != NULL ? failure : "submit failed";
			ok = false;
		} else {
			bigtime_t submitTime = system_time();
			Profile(kTimeSubmit, submitTime - submitStart);
			// The damage goes out only when the run is done on the GPU: on
			// Lavapipe a run after Sign in takes 11-15 s, and a compose
			// queued behind it at once ran into its 15 s fence limit and
			// turned the compositor off (QEMU, 2 of 2 boots). The compose
			// itself still waits only on the GPU (the semaphores); nothing
			// is read back.
			{
				// a late GPU is not a failure (M11 decision 14)
				VkResult result;
				do {
					result = sWaitForFences(sDevice, 1, &fence, VK_TRUE,
						1000000000ull);
				} while (result == VK_TIMEOUT);
				if (result != VK_SUCCESS) {
					*_reason = "device lost";
					ok = false;
				}
				fRunInFlight = false;
			}
			// read_done() copies into the bitmaps from here
			context->checkAsyncWorkCompletion();
			Profile(kTimeDeliver, system_time() - submitTime);
		}
		if (context->isDeviceLost()) {
			*_reason = "device lost";
			ok = false;
		}
	}

	bigtime_t now = system_time();
	for (size_t i = 0; i < items.size(); i++) {
		WorkItem* item = items[i].get();
		if (item->image.IsSet() && ok) {
			// the semaphore is signalled first, then the damage goes out:
			// the compose that takes it waits for this run on the GPU
			item->target->Drawn(item->cookie, item->damage,
				item->image.Get());
			item->delivered = true;
		}
		if (!item->delivered)
			item->target->DeliveryFailed(item->cookie);
		else if (item->firstDraw > 0)
			AddDrawSample(now - item->firstDraw);
		// the surface belongs to the target's recorder: let it go under
		// the target's lock
		if (item->target->Lock()) {
			item->surface.reset();
			item->target->Unlock();
		}
	}

	if (!ok) {
		for (size_t i = 0; i < items.size(); i++)
			sLeftovers.push_back(std::move(items[i]));
	}

	// M13 step 2: new pipelines go to disk, at most every 10 s (Graphite
	// returns at once when there are none)
	if (ok && now - fPipelinesSynced > kPipelineSyncInterval) {
		fPipelinesSynced = now;
		context->syncPipelineData(kPipelineDataLargest);
	}

	atomic_set64(&fDone, ticket);
	_ReleaseWaiters();
	return ok;
}


status_t
SkiaGpu::Finish(bigtime_t timeout)
{
	if (fShutDown)
		return B_ERROR;
	int64 ticket = atomic_add64(&fTicket, 1) + 1;
	atomic_add(&fWaiters, 1);
	Wake();
	bigtime_t deadline = system_time() + timeout;
	status_t status = B_OK;
	while (atomic_get64(&fDone) < ticket && !fShutDown) {
		// review 2: the compose thread waits for the window lock (a mode
		// change, the switch to the CPU mode); waiting here could hold
		// that up while the caller holds a window's lock
		if (atomic_get(&fComposeBlocked) != 0) {
			status = B_BUSY;
			break;
		}
		bigtime_t left = deadline - system_time();
		if (left <= 0) {
			status = B_TIMED_OUT;
			break;
		}
		if (acquire_sem_etc(fDoneSem, 1, B_RELATIVE_TIMEOUT, left)
				== B_TIMED_OUT) {
			status = B_TIMED_OUT;
			break;
		}
	}
	atomic_add(&fWaiters, -1);
	if (fShutDown)
		return B_ERROR;
	return status;
}


void
SkiaGpu::Shutdown()
{
	BAutolock _(fLock);
	if (fShutDown)
		return;
	for (int32 i = 0; i < fTargets.CountItems(); i++) {
		SkiaTarget* target = fTargets.ItemAt(i);
		if (target->Lock()) {
			target->DropGpu();
			target->Unlock();
		}
	}
	fShutDown = true;
	atomic_set64(&fDone, atomic_get64(&fTicket));
	_ReleaseWaiters();
}


void
SkiaGpu::_ReleaseWaiters()
{
	int32 waiters = atomic_get(&fWaiters);
	if (waiters > 0)
		release_sem_etc(fDoneSem, waiters, 0);
}


void
SkiaGpu::ReportFailure(const char* reason)
{
	if (fFailure == NULL)
		fFailure = reason;
	Wake();
}


void
SkiaGpu::SetComposeBlocked(bool blocked)
{
	atomic_set(&fComposeBlocked, blocked ? 1 : 0);
	if (blocked)
		_ReleaseWaiters();
}


void
SkiaGpu::AddDrawSample(bigtime_t time)
{
	BAutolock _(fStatsLock);
	if (fSampleCount < (int32)(sizeof(fSamples) / sizeof(fSamples[0])))
		fSamples[fSampleCount++] = time;
	else
		fSamples[(fSampleCount++) % 256] = time;
}


bool
SkiaGpu::TakeDrawMedian(bigtime_t* _median, int32* _count)
{
	BAutolock _(fStatsLock);
	int32 count = min_c(fSampleCount, 256);
	*_count = fSampleCount;
	if (count == 0)
		return false;
	std::sort(fSamples, fSamples + count);
	*_median = fSamples[count / 2];
	fSampleCount = 0;
	return true;
}
