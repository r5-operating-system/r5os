/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 (docs/APP-SERVER-GPU-DRAWING.md, part 2): Skia's Graphite context on
 * the compositor's Vulkan device and queue. Every drawing engine records
 * on its own thread with a Recorder of its own; only the compose thread
 * inserts the recordings and submits them, so every vkQueueSubmit of
 * app_server stays on that one thread and the M11 lock order does not
 * change.
 *
 * This header has no Skia and no Vulkan type, so Compositor.cpp (which
 * uses its own Vulkan headers) can include it.
 */
#ifndef SKIA_GPU_H
#define SKIA_GPU_H


#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>


class SkiaTarget;


struct skia_vulkan_device {
	void*				getInstanceProc;	// PFN_vkGetInstanceProcAddr
	void*				instance;
	void*				physicalDevice;
	void*				device;
	void*				queue;
	uint32				queueFamily;
	uint32				apiVersion;
	const char* const*	deviceExtensions;
	uint32				deviceExtensionCount;
};


class SkiaGpu {
public:
	// NULL with a reason when Graphite does not start.
	static	SkiaGpu*			Create(const skia_vulkan_device& device,
									bool forceFail, const char** _reason);
								~SkiaGpu();

	// "m158-707e33875f"
	static	const char*			Version();

	// The context of this desktop, for engines that are not a window's
	// (alpha masks, offscreen bitmaps). Set by the compositor.
	static	SkiaGpu*			Current();
	static	void				SetCurrent(SkiaGpu* gpu);

	// The compositor wakes its compose thread when a target has work.
			void				SetWaker(void (*wake)(void*), void* cookie);

	// Any thread, no recorder lock held. The Recorder is opaque here
	// (skgpu::graphite::Recorder*).
			void*				NewRecorder();
			void				DeleteRecorder(void* recorder);

	// Any thread, no recorder lock held.
			void				AddTarget(SkiaTarget* target);
			void				RemoveTarget(SkiaTarget* target);

	// A target recorded something: the compose thread runs soon.
			void				Wake();

	// Compose thread only: snaps every target, inserts the recordings,
	// submits, waits for the GPU and copies what was drawn into the window
	// buffers and bitmaps. False with a reason on a lost device, a failed
	// insert or submit, or the forced failure (gpu_draw_fail).
			bool				RunWork(const char** _reason);

	// Any thread that holds no recorder lock and not the compose slot:
	// returns when everything recorded before the call is in memory.
	// B_TIMED_OUT when the compose thread did not get to it in time
	// (M11 decision 14: late is not a failure), B_ERROR once the GPU
	// drawing is off.
			status_t			Finish(bigtime_t timeout = 5000000);

	// Compose thread: the GPU drawing is off; every target lets go of its
	// GPU objects and waiters are released. Before the device goes.
			void				Shutdown();
			bool				IsShutDown() const { return fShutDown; }
	// Shutdown(), then the Graphite context and its allocator go; before
	// the Vulkan device is destroyed. The object itself stays, so engines
	// of windows that close later can still remove their targets.
			void				DestroyContext();

	// Any thread: the GPU drawing cannot go on (a window image that could
	// not be made); the next RunWork() fails with this reason.
			void				ReportFailure(const char* reason);
	// Compose thread, around its wait for all windows: Finish() returns
	// B_BUSY meanwhile instead of holding that wait up.
			void				SetComposeBlocked(bool blocked);

	// gpu_debug: draw times, one median line per 10 s.
			void				AddDrawSample(bigtime_t time);
			bool				TakeDrawMedian(bigtime_t* _median,
									int32* _count);

	// The opaque graphite Context (skgpu::graphite::Context*), compose
	// thread only.
			void*				GraphiteHandle() const { return fContext; }

private:
								SkiaGpu();
			void				_ReleaseWaiters();

			BLocker				fLock;
				// taken before any recorder lock, never after
			BObjectList<SkiaTarget, false> fTargets;
			void*				fContext;
			void*				fBackend;
			bool				fForceFail;
			bool				fShutDown;
			void				(*fWake)(void*);
			void*				fWakeCookie;
			// one wake until RunWork() starts
			int32				fWakeRequested;
	const char* volatile		fFailure;
			int32				fComposeBlocked;

			// Finish(): tickets handed out, and the last one done.
			int64				fTicket;
			int64				fDone;
			sem_id				fDoneSem;
			int32				fWaiters;

			BLocker				fStatsLock;
			bigtime_t			fSamples[256];
			int32				fSampleCount;
};


#endif	// SKIA_GPU_H
