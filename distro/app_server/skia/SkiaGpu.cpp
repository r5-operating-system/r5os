/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12: the Graphite context of app_server (SkiaGpu.h).
 */


#include "SkiaGpu.h"

#include <stdio.h>
#include <string.h>

#include <Autolock.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "include/core/SkImage.h"
#include "include/core/SkMilestone.h"
#include "include/core/SkSurface.h"
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/GraphiteTypes.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"

#include "SkiaPainter.h"


#ifndef R5_SKIA_VERSION
#	define R5_SKIA_VERSION "m158"
#endif


namespace ContextFactory = skgpu::graphite::ContextFactory;
using skgpu::graphite::ContextOptions;
using skgpu::graphite::InsertRecordingInfo;
using skgpu::graphite::InsertStatus;
using skgpu::graphite::Recorder;
using skgpu::graphite::Recording;
using skgpu::graphite::SampleCount;
using skgpu::graphite::SubmitInfo;
using skgpu::graphite::SyncToCpu;
typedef skgpu::graphite::Context GraphiteContext;


static PFN_vkGetInstanceProcAddr sGetInstanceProc;
static PFN_vkGetDeviceProcAddr sGetDeviceProc;


static PFN_vkVoidFunction
get_proc(const char* name, VkInstance instance, VkDevice device)
{
	if (device != VK_NULL_HANDLE)
		return sGetDeviceProc(device, name);
	return sGetInstanceProc(instance, name);
}


struct SkiaBackend {
	skgpu::VulkanExtensions			extensions;
	VkPhysicalDeviceFeatures2		features;
	skgpu::VulkanBackendContext		backend;
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
	bigtime_t			firstDraw;
	void*				cookie;
	bool				read;
	bool				delivered;
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
	fSampleCount(0)
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
	delete (GraphiteContext*)fContext;
	fContext = NULL;
	sLeftovers.clear();
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
	const char** _reason)
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
	// Q3: curved shapes on the GPU with 4x MSAA, no CPU path atlas
	options.fInternalMultisampleCount = SampleCount::k4;
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


void*
SkiaGpu::NewRecorder()
{
	BAutolock _(fLock);
	if (fShutDown || fContext == NULL)
		return NULL;
	std::unique_ptr<Recorder> recorder = ((GraphiteContext*)fContext)->makeRecorder();
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

	std::vector<std::unique_ptr<WorkItem>> items;
	for (int32 i = 0; i < fTargets.CountItems(); i++) {
		SkiaTarget* target = fTargets.ItemAt(i);
		if (!target->Lock())
			continue;
		if (target->HasWork() && target->HasSurface()) {
			std::unique_ptr<WorkItem> item(new(std::nothrow) WorkItem);
			if (item) {
				item->target = target;
				item->recording = target->Snap(item->damage, &item->firstDraw);
				item->surface = sk_ref_sp(target->Surface());
				BRegion surface(BRect(target->Left(), target->Top(),
					target->Left() + target->Width() - 1,
					target->Top() + target->Height() - 1));
				item->damage.IntersectWith(&surface);
				item->frame = item->damage.FrameInt();
				item->read = false;
				item->delivered = false;
				item->cookie = target->PrepareDelivery();
				items.push_back(std::move(item));
			}
		}
		target->Unlock();
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
		if (context->insertRecording(info) != InsertStatus::kSuccess) {
			*_reason = "insert failed";
			ok = false;
		}
	}
	for (size_t i = 0; ok && i < items.size(); i++) {
		WorkItem* item = items[i].get();
		if (item->damage.CountRects() == 0)
			continue;
		SkIRect rect = SkIRect::MakeLTRB(
			item->frame.left - item->target->Left(),
			item->frame.top - item->target->Top(),
			item->frame.right + 1 - item->target->Left(),
			item->frame.bottom + 1 - item->target->Top());
		SkImageInfo info = SkImageInfo::Make(rect.width(), rect.height(),
			kBGRA_8888_SkColorType, kPremul_SkAlphaType, nullptr);
		context->asyncRescaleAndReadPixels(item->surface.get(), info, rect,
			SkImage::RescaleGamma::kSrc, SkImage::RescaleMode::kNearest,
			read_done, item);
	}
	if (ok && !items.empty()) {
		SubmitInfo submit;
		submit.fSync = SyncToCpu::kYes;
		if (!context->submit(submit)) {
			*_reason = "submit failed";
			ok = false;
		} else
			context->checkAsyncWorkCompletion();
		if (context->isDeviceLost()) {
			*_reason = "device lost";
			ok = false;
		}
	}

	bigtime_t now = system_time();
	for (size_t i = 0; i < items.size(); i++) {
		WorkItem* item = items[i].get();
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
