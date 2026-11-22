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


#include "CompositorPrivate.h"

#include "r5_span_avx.h"


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
	SkiaGpu::SetDebug(fDebug);
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
		// The round outline: the cut corners are not the window's (the
		// full region above has them out already, the outline's has not);
		// the arc's pixels are blended over what lies behind.
		window_outline rounded;
		window->GetOutline(&rounded, true);
		if (!rounded.IsSquare())
			full.Exclude(&rounded.cut);
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
		if (!rounded.IsSquare()) {
			layer_round& round = layer->round;
			round.edge = rounded.edge;
			round.edge.IntersectWith(&full);
			round.area = rounded.area;
			round.area.IntersectWith(&full);
			round.cornerCount = rounded.cornerCount;
			memcpy(round.corners, rounded.corners,
				sizeof(window_corner) * rounded.cornerCount);
			// the decorator's own edge line, carried round the arc; none
			// for a window without one (review: no colour guessed from
			// the window's pixels)
			::Decorator* edgeDecorator = window->Decorator();
			rgb_color edgeColor;
			float edgeWidth = 0;
			if (edgeDecorator != NULL
				&& edgeDecorator->GetEdgeLine(&edgeColor, &edgeWidth)
				&& edgeWidth > 0) {
				round.edgeColor = (uint32)edgeColor.blue
					| ((uint32)edgeColor.green << 8)
					| ((uint32)edgeColor.red << 16)
					| ((uint32)edgeColor.alpha << 24);
				round.edgeWidth = (int32)ceilf(edgeWidth);
			}
		}
		// what hides the windows below: the shape without the arc
		BRegion solid(full);
		solid.Exclude(&layer->round.edge);

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
			// A round window gets the full soft shadow of the step, not a
			// decorator's short one (review: Haiku's bordered look cut a
			// 15 px blur at 1 to 2 px, a hard edge round the arc). The
			// blur of its round shape fades out round by itself.
			if (decorator != NULL && rounded.IsSquare()) {
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
				// under the arc too: its pixels are blended over the shadow
				layer->shadowArea.Exclude(&solid);
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
			&& before->glassAlpha == after->glassAlpha
			// the round outline: its corners and its edge line
			&& before->round.area == after->round.area
			&& before->round.edgeColor == after->round.edgeColor
			&& before->round.edgeWidth == after->round.edgeWidth) {
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
					ghost->round = layer->round;
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
				copy->CopySnapshotFrom(*source);
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
				// A window's first pixels: its shadow is composed too. It
				// was skipped while the window had none, and the window's
				// drawing damages only its own pixels (22 Nov 2026: a new
				// window showed no shadow until it moved).
				bool hasPixels = layer->HasPixels();
				if (hasPixels != layer->entry->hadPixels) {
					layer->entry->hadPixels = hasPixels;
					if (hasPixels && layer->shadow && !splash) {
						BRegion area(layer->shadowArea);
						area.IntersectWith(&screen);
						damage.Include(&area);
					}
				}
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
			// the arc of a round corner is blended over what lies below
			plain.Exclude(&upper->round.edge);
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
		// the arc of a round corner, blended after the plain part
		BRegion edgePart;
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
			if (layer->round.area.CountRects() > 0) {
				edgePart = part;
				edgePart.IntersectWith(&layer->round.area);
				part.Exclude(&edgePart);
			}
			bool shadowHit = withEffects && layer->shadow
				&& damage.Intersects(layer->shadowArea.Frame());
			// M13 step 4: an outline outside the window is drawn too
			bool outlineHit = layer->outline.CountRects() > 0
				&& damage.Intersects(layer->outline.Frame());
			if (part.CountRects() == 0 && edgePart.CountRects() == 0
				&& !shadowHit && !outlineHit)
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
			// glass in a round corner is drawn by _RecordGlass() with the
			// corner's coverage
			BRegion both(part);
			both.Include(&edgePart);
			glassPart.IntersectWith(&both);
			part.Exclude(&glassPart);
			edgePart.Exclude(&glassPart);
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
		if (edgePart.CountRects() > 0) {
			// A zero-copy shared view reaching into a corner draws those
			// pixels itself through _RecordShareCorners() over the untouched
			// background below, so _RecordCorners() skips them to avoid
			// double-blending.
			BRegion windowCorners(edgePart);
			for (int32 s = 0; s < shares.CountItems(); s++) {
				ShareDraw* draw = shares.ItemAt(s);
				if (draw->window == NULL
					|| draw->window != layer->entry->window)
					continue;
				BRegion shareArea(draw->shape);
				BRegion shareImage(BRect((float)draw->left, (float)draw->top,
					(float)draw->left + draw->width - 1,
					(float)draw->top + draw->height - 1));
				shareArea.IntersectWith(&shareImage);
				windowCorners.Exclude(&shareArea);
			}
			// on other pixels than the copy and the glass: no barrier
			if (windowCorners.CountRects() > 0) {
				_RecordCorners(layer, windowCorners, (uint64)frameInfo.buffer,
					rect.frameStride);
				vk->vkCmdBindPipeline(vk->commands,
					VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
			}
			// a shared view below is drawn over the corners too, so a
			// zero-copy view keeps its corner pixels (review)
			part.Include(&edgePart);
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
			BRegion shareCorners;
			if (layer->round.IsRound()) {
				shareCorners = view;
				shareCorners.IntersectWith(&layer->round.area);
				view.Exclude(&shareCorners);
			}
			if (view.CountRects() > 0) {
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
			}
			if (shareCorners.CountRects() > 0) {
				_RecordShareCorners(layer, draw, shareCorners,
					(uint64)frameInfo.buffer, rect.frameStride);
				vk->vkCmdBindPipeline(vk->commands,
					VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
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
		if (!compositor_allocate_host_buffer(vk,
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
		log_line("draw profile snap_parts targets %" B_PRId64
			" pipelines %" B_PRId64 " (%" B_PRId64 " us cpu)"
			" compute %" B_PRId64 " (%" B_PRId64 " us)"
			" shaders %" B_PRId64 " (%" B_PRId64 " us)"
			" alloc %" B_PRId64 " (%" B_PRId64 " us)",
			p[SkiaGpu::kCountTargets],
			p[SkiaGpu::kCountPipelines], p[SkiaGpu::kTimePipelines],
			p[SkiaGpu::kCountComputePipelines],
			p[SkiaGpu::kTimeComputePipelines],
			p[SkiaGpu::kCountShaders], p[SkiaGpu::kTimeShaders],
			p[SkiaGpu::kCountAllocMem], p[SkiaGpu::kTimeAllocMem]);
		log_line("draw profile ops text %" B_PRId64 " (%" B_PRId64 " us)"
			" path %" B_PRId64 " (%" B_PRId64 " us)"
			" bitmap %" B_PRId64 " (%" B_PRId64 " us)"
			" upload %" B_PRId64 " (%" B_PRId64 " us)",
			p[SkiaGpu::kCountText], p[SkiaGpu::kTimeText],
			p[SkiaGpu::kCountPath], p[SkiaGpu::kTimePath],
			p[SkiaGpu::kCountBitmap], p[SkiaGpu::kTimeBitmap],
			p[SkiaGpu::kCountUpload], p[SkiaGpu::kTimeUpload]);
		log_line("draw profile deliver_parts fence_wait %" B_PRId64
			" us async_check %" B_PRId64 " us",
			p[SkiaGpu::kTimeFenceWait] / runs,
			p[SkiaGpu::kTimeAsyncCheck] / runs);
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
