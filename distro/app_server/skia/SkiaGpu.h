/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 (docs/APP-SERVER-GPU-DRAWING.md, part 2): Skia's Graphite context on
 * the compositor's Vulkan device and queue. Every drawing engine records
 * on its own thread with a Recorder of its own; the draw thread
 * (Compositor::_DrawLoop()) inserts the recordings and submits them. It
 * shares the VkQueue with the compose thread, so every queue call of both
 * takes one lock (LockQueue()).
 *
 * This header has no Skia and no Vulkan type, so Compositor.cpp (which
 * uses its own Vulkan headers) can include it.
 */
#ifndef SKIA_GPU_H
#define SKIA_GPU_H


#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>
#include <Referenceable.h>

#include <vector>


class SkiaTarget;


/*!	M13 step 2: a window's image, which Skia draws into and the compositor
	samples (VK_FORMAT_B8G8R8A8_UNORM, Haiku's bytes). app_server makes and
	frees the VkImage itself, so it does not depend on the Graphite context.
	Between two runs it is in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
	Freed when the last reference goes: the target, Graphite (until its
	work with the image is done), the window buffer it is shown in, and
	every compose frame that samples it (until that frame's fence).
*/
class SkiaImage : public BReferenceable {
public:
	// VkImage, VkImageView
			uint64				Image() const { return fImage; }
			uint64				View() const { return fView; }
			int32				Width() const { return fWidth; }
			int32				Height() const { return fHeight; }
	// skgpu::graphite::BackendTexture*
			void*				Texture() const { return fTexture; }
	// M13 step 4: the VkImage's own size. Width() and Height() are the
	// window's size, which the compositor reads and samples from pixel
	// (0, 0); after a resize the VkImage may be larger, by up to two
	// 64-pixel steps (SkiaGpu::NewImage(), SkiaGpu::ShareImage()).
			int32				AllocatedWidth() const
									{ return fAllocatedWidth; }
			int32				AllocatedHeight() const
									{ return fAllocatedHeight; }

protected:
	virtual						~SkiaImage();

private:
	friend class SkiaGpu;
								SkiaImage();

			uint64				fImage;
			uint64				fView;
			uint64				fMemory;
			int32				fWidth;
			int32				fHeight;
			void*				fTexture;
			// the Skia device this image was made on (KeepImageMemory)
			int32				fGeneration;
			// M13 step 4: the size of the VkImage, and the image whose
			// VkImage, view and texture this one shows at another window
			// size (NULL: this one owns them)
			int32				fAllocatedWidth;
			int32				fAllocatedHeight;
			SkiaImage*			fBacking;
};


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
	// M13 step 3: samples, Skia's sample count for paths (4, or 1 with
	// gpu_lavapipe_msaa 1 on Lavapipe)
	static	SkiaGpu*			Create(const skia_vulkan_device& device,
									bool forceFail, int samples,
									const char** _reason);
								~SkiaGpu();

	// "m158-707e33875f"
	static	const char*			Version();

	// The context of this desktop, for engines that are not a window's
	// (alpha masks, offscreen bitmaps). Set by the compositor.
	// Any thread: around every use of the compositor's VkQueue outside
	// Skia (vkQueueSubmit, vkDeviceWaitIdle). Graphite's own queue calls
	// take the same lock.
	static	void				LockQueue();
	static	void				UnlockQueue();
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

	// Draw thread only: snaps every target, inserts the recordings and
	// submits. M13 step 2: a window's image stays on the GPU; the submit
	// signals a semaphore that the compose waits on, and the damage goes
	// to the compositor without a CPU wait. Only targets that need the
	// bytes in memory (offscreen bitmaps, r5-draw-compare) still wait for
	// the GPU and get a read-back. False with a reason on a lost device, a
	// failed insert or submit, or the forced failure (gpu_draw_fail).
	// M13 step 4: the wait for the run's fence is without fLock, so new
	// recorders and targets do not wait for the GPU.
			bool				RunWork(const char** _reason);

	// Any thread that holds no recorder lock and not the compose slot:
	// returns when everything recorded before the call is in memory.
	// B_TIMED_OUT when the compose thread did not get to it in time
	// (M11 decision 14: late is not a failure), B_ERROR once the GPU
	// drawing is off.
			status_t			Finish(bigtime_t timeout = 5000000);
	// Start splash, compose thread: Finish() without the wait. The ticket
	// is done once the run that covers everything recorded before the
	// call has finished (or the GPU drawing is off).
			int64				AskTicket();
			bool				TicketDone(int64 ticket);

	// Compose thread: the GPU drawing is off; every target lets go of its
	// GPU objects and waiters are released. Before the device goes.
			void				Shutdown();
	// A lost device: recorders and the context stay allocated and are
	// never destroyed, so a window that closes later does not call Vulkan.
			void				Abandon();
			bool				Abandoned()
									{ return atomic_get(&fAbandoned) != 0; }
			bool				IsShutDown() const { return fShutDown; }
	// M13 step 4: the device is a CPU (Lavapipe); a run that drew a
	// window has finished (the warm-up waits for that on Lavapipe).
			bool				OnCpu() const { return fOnCpu; }
			bool				WindowRunDone()
									{ return atomic_get(&fWindowRunDone)
										!= 0; }
			bool				WarmUpDone()
									{ return atomic_get(&fWarmUpDone)
										!= 0; }
			void				SetWarmUpDone()
									{ atomic_set(&fWarmUpDone, 1); }
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

	// M12 step 5c, the profile: where a compose's GPU drawing spends its
	// time (us, summed), and what the drawing calls did (counts). Any
	// thread adds; the compositor reports and clears every 10 s.
	enum {
		kTimeSnap = 0,		// snapping the recordings
		kTimeInsert,		// Context::insertRecording()
		kTimeSubmit,		// submit and wait for the GPU
		kTimeDeliver,		// M13 step 2: waiting for the run before, and
							// the read-backs (bitmaps, r5-draw-compare)
		kCountComposite,	// R5 shader passes (with a copy of the destination)
		kCountDirect,		// draws with Skia's own blending
		kCountCoverage,		// coverage images drawn for the R5 shader
		kCountUpload,		// textures made from CPU memory
		kCountRuns,			// RunWork() calls with work
		// Perf round 2 (gpu_debug only): split snap and deliver
		kCountTargets,
		kCountPipelines,
		kTimePipelines,
		kCountComputePipelines,
		kTimeComputePipelines,
		kCountShaders,
		kTimeShaders,
		kCountAllocMem,
		kTimeAllocMem,
		kTimeFenceWait,
		kTimeAsyncCheck,
		kTimeUpload,
		kCountText,
		kTimeText,
		kCountPath,
		kTimePath,
		kCountBitmap,
		kTimeBitmap,
		kProfileCount
	};
	static	void				SetDebug(bool debug);
	static	bool				Debug();
	static	void				Profile(int which, int64 value = 1);
			void				TakeProfile(int64 values[kProfileCount]);
	static	void				PeekProfile(int64 values[kProfileCount]);

	// gpu_debug: draw times, one median line per 10 s.
			void				AddDrawSample(bigtime_t time);
			bool				TakeDrawMedian(bigtime_t* _median,
									int32* _count);

	// The opaque graphite Context (skgpu::graphite::Context*), compose
	// thread only.
			void*				GraphiteHandle() const { return fContext; }

	// M13 step 2: a new window image, under the target's lock; NULL when
	// it cannot be made. Its contents are undefined until drawn.
			SkiaImage*			NewImage(int32 width, int32 height);
	// M13 step 4: the same on a VkImage of allocatedWidth by
	// allocatedHeight (at least width by height), for a window that is
	// being resized.
			SkiaImage*			NewImage(int32 width, int32 height,
									int32 allocatedWidth,
									int32 allocatedHeight);
	// M13 step 4: an image of width by height on the VkImage of backing
	// (no new Vulkan object), or NULL when that size does not fit. It
	// keeps a reference to the image that owns the VkImage.
			SkiaImage*			ShareImage(SkiaImage* backing, int32 width,
									int32 height);
	// A GPU that does not finish (M11 "compose fence still busy"): no
	// image is freed from now on, its memory stays.
	static	void				KeepImageMemory();

	// M13 step 2: the semaphores the runs signalled since the last call
	// (VkSemaphore each), for the compose's submit to wait on. Call with
	// LockQueue() held, around that vkQueueSubmit, so no run's submit
	// lands between taking them and the wait. Give them back with
	// ReturnSignals() after that submit's fence. After a failed submit
	// they are not given back (the GPU drawing goes off).
			void				TakeSignals(std::vector<uint64>& signals);
			void				ReturnSignals(
									const std::vector<uint64>& signals);

private:
								SkiaGpu();
			void				_ReleaseWaiters();
			// stick 72: true when the run fence reports anything but
			// signalled or not yet (the device was lost under us)
			bool				_RunFenceBroken();
			uint64				_Signal();
			void				_DestroySemaphores();

			BLocker				fLock;
				// taken before any recorder lock, never after
			BObjectList<SkiaTarget, false> fTargets;
			// M13 step 4, under fLock: the items of the run whose fence
			// RunWork() waits for without fLock (its WorkItem list), NULL
			// otherwise; RemoveTarget() and Shutdown() let go of their
			// targets in it. fWaitingUnlocked: that wait is going on.
			void*				fInFlight;
			bool				fWaitingUnlocked;
			bool				fOnCpu;
			int32				fWindowRunDone;
			int32				fWarmUpDone;
			void*				fContext;
			void*				fBackend;
			bool				fForceFail;
			bool				fShutDown;
			// Set in Abandon(), read from the draw thread's fence wait
			// without fLock. atomic_get / atomic_set, like fDone.
			int32				fAbandoned;
			// 5625U stick, boot 1: the GPU's own memory sets the budgets
			size_t				fRecorderBudget;
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

			// M13 step 2, under the queue lock: unsignalled semaphores,
			// and the signalled ones no compose waited on yet
			std::vector<uint64>	fFreeSemaphores;
			std::vector<uint64>	fSignalled;
			uint32				fQueueFamily;
			uint64				fRunFence;
			// one run on the GPU at a time
			bool				fRunInFlight;
			// the run fence has been submitted at least once
			bool				fRunSubmitted;
			// M13 step 2: the pipeline cache's last store
			bigtime_t			fPipelinesSynced;
};


#endif	// SKIA_GPU_H
