/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12: SkiaOffscreen.h.
 */


#include "SkiaOffscreen.h"

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <OS.h>

#include <driver_settings.h>

#include <AutoDeleter.h>

#include <Autolock.h>

#include <map>

#include <GradientLinear.h>

#include "BitmapHWInterface.h"
#include "DrawState.h"
#include "ServerBitmap.h"

#include "SkiaDrawingEngine.h"
#include "SkiaGpu.h"
#include "SkiaPainter.h"


static BLocker sTrackLock("skia offscreen");
static std::map<const ServerBitmap*, SkiaDrawingEngine*> sTracked;


/*!	A GPU image whose pixels go into a ServerBitmap's memory at each read
	point. Surface pixel (0, 0) is bitmap pixel (0, 0).
*/
class MemoryTarget : public SkiaTarget {
public:
	MemoryTarget(SkiaGpu* gpu, ServerBitmap* bitmap)
		:
		SkiaTarget(gpu),
		fBitmap(bitmap)
	{
	}

	// part 6, point 2: the first drawing after a read point starts from
	// the memory, which the program may have written itself
	virtual bool UploadSource(const uint8** _bits, uint32* _bytesPerRow)
	{
		if (fBitmap.Get() == NULL)
			return false;
		*_bits = fBitmap->Bits();
		*_bytesPerRow = fBitmap->BytesPerRow();
		return true;
	}

	ServerBitmap* Bitmap() const { return fBitmap.Get(); }

	virtual void* PrepareDelivery() { return this; }

	virtual void Deliver(void* cookie, const BRegion& damage,
		const clipping_rect& frame, const uint8* bits, uint32 bytesPerRow)
	{
		ServerBitmap* bitmap = fBitmap.Get();
		if (bitmap == NULL)
			return;
		BRegion copy(damage);
		BRegion inside(BRect(Left(), Top(), Left() + bitmap->Width() - 1,
			Top() + bitmap->Height() - 1));
		copy.IntersectWith(&inside);
		uint8* to = bitmap->Bits();
		uint32 toRow = bitmap->BytesPerRow();
		for (int32 i = 0; i < copy.CountRects(); i++) {
			clipping_rect r = copy.RectAtInt(i);
			for (int32 y = r.top; y <= r.bottom; y++) {
				memcpy(to + (y - Top()) * toRow + (r.left - Left()) * 4,
					bits + (y - frame.top) * bytesPerRow
						+ (r.left - frame.left) * 4,
					(r.right - r.left + 1) * 4);
			}
		}
	}

	virtual void DeliveryFailed(void* cookie) {}

private:
	BReference<ServerBitmap>	fBitmap;
};


/* Opt-in. The first Skia warm-up on the 5625U starts in the same
 * second as the USB and Bluetooth drivers. This waits that out. The
 * default warm-up does not. */
static void
warm_up_settle()
{
	void* settings = load_driver_settings("radeon_hd");
	if (settings == NULL)
		return;
	bool settle = get_driver_boolean_parameter(settings, "gpu_draw_settle",
		false, false);
	unload_driver_settings(settings);
	if (!settle)
		return;
	const bigtime_t kSettleUs = 8000000;
	snooze(kSettleUs);
	fprintf(stderr, "app_server: gpu draw warm up settled %" B_PRId64
		" ms\n", kSettleUs / 1000);
	syslog(LOG_INFO, "app_server: gpu draw warm up settled %" B_PRId64
		" ms\n", kSettleUs / 1000);
}


void
skia_warm_up()
{
	warm_up_settle();

	// M13 step 4: on Lavapipe the warm-up competed with the first real
	// drawing (5625U stick 3: a run of 19.5 s, and the warm-up ran into two
	// Finish() timeouts, 10160 ms). There it starts after the first run
	// that drew a window, or after 30 s without one.
	// After stick 4 (10448 ms, the same two timeouts): the cost is the
	// warm-up's own work, Lavapipe compiling its shaders while it runs
	// (two runs of 9.1 s each on the GPU). So on Lavapipe it is drawn in
	// parts, one Finish() per drawing mode, each waiting up to 30 s: a
	// window's drawing waits for one part at most, not for all of it.
	SkiaGpu* gpu = SkiaGpu::Current();
	bool inParts = gpu != NULL && gpu->OnCpu();
	if (inParts) {
		bigtime_t waitStart = system_time();
		while (system_time() - waitStart < 30000000) {
			gpu = SkiaGpu::Current();
			if (gpu == NULL || gpu->IsShutDown() || gpu->WindowRunDone())
				break;
			snooze(100000);
		}
		bigtime_t waited = (system_time() - waitStart) / 1000;
		fprintf(stderr, "app_server: gpu draw warm up after %" B_PRId64
			" ms (lavapipe)\n", waited);
		syslog(LOG_INFO, "app_server: gpu draw warm up after %" B_PRId64
			" ms (lavapipe)\n", waited);
	}

	bigtime_t start = system_time();
	BReference<UtilityBitmap> bitmap(new(std::nothrow) UtilityBitmap(
		BRect(0, 0, 63, 63), B_RGBA32, 0), true);
	if (bitmap == NULL)
		return;
	BitmapHWInterface lock(bitmap);
	lock.Initialize();
	ObjectDeleter<DrawingEngine> engine(skia_offscreen_engine(&lock, bitmap,
		IntRect(0, 0, 63, 63)));
	if (!engine.IsSet())
		return;
	BReference<UtilityBitmap> image(new(std::nothrow) UtilityBitmap(
		BRect(0, 0, 7, 7), B_RGBA32, 0), true);
	BRegion clip(BRect(0, 0, 63, 63));
	DrawState state;
	BGradientLinear gradient(BPoint(0, 0), BPoint(0, 63));
	gradient.AddColor((rgb_color){ 255, 0, 0, 255 }, 0);
	gradient.AddColor((rgb_color){ 0, 0, 255, 128 }, 255);
	static const drawing_mode kModes[] = { B_OP_COPY, B_OP_OVER, B_OP_ALPHA,
		B_OP_ALPHA };
	// M13 step 4: the first Finish() status that is not B_OK
	status_t finishStatus = B_OK;
	const bigtime_t kPartTimeout = 30000000;
	int32 parts = 0;
	bigtime_t longestPart = 0;
	for (int pass = 0; pass < 2; pass++) {
		// pass 0: an opaque image (Skia's own blending), 1: not opaque
		static_cast<SkiaDrawingEngine*>(engine.Get())->SetOpaque(pass == 0);
		for (int m = 0; m < 4; m++) {
			engine->SetDrawState(&state);
			engine->ConstrainClippingRegion(&clip);
			engine->SetDrawingMode(kModes[m]);
			engine->SetBlendingMode(B_PIXEL_ALPHA,
				m == 3 ? B_ALPHA_COMPOSITE : B_ALPHA_OVERLAY);
			engine->SetHighColor((rgb_color){ 20, 40, 60, 160 });
			engine->FillRect(BRect(2, 2, 30, 30));
			engine->DrawEllipse(BRect(3, 3, 40, 30), true);
			engine->DrawEllipse(BRect(3, 3, 40, 30), false);
			engine->DrawRoundRect(BRect(4, 4, 50, 40), 6, 6, true);
			engine->StrokeLine(BPoint(1, 1), BPoint(60, 50));
			BPoint points[3] = { BPoint(2, 60), BPoint(30, 2), BPoint(60, 60) };
			engine->DrawPolygon(points, 3, BRect(2, 2, 60, 60), true, true);
			engine->DrawString("Aa", 2, BPoint(4, 40));
			engine->FillRect(BRect(0, 0, 63, 63), gradient);
			if (image != NULL) {
				engine->DrawBitmap(image, image->Bounds(), BRect(8, 8, 15, 15));
				engine->DrawBitmap(image, image->Bounds(), BRect(8, 8, 31, 31),
					B_FILTER_BITMAP_BILINEAR);
			}
			if (inParts) {
				bigtime_t partStart = system_time();
				status_t status = static_cast<SkiaDrawingEngine*>(
					engine.Get())->Finish(kPartTimeout);
				longestPart = max_c(longestPart, system_time() - partStart);
				parts++;
				if (status != B_OK && finishStatus == B_OK)
					finishStatus = status;
			}
		}
		status_t status = skia_offscreen_finish(engine.Get());
		if (status != B_OK && finishStatus == B_OK)
			finishStatus = status;
	}
	// as Compositor.cpp's log_line(): stderr and the syslog
	bigtime_t took = (system_time() - start) / 1000;
	fprintf(stderr, "app_server: gpu draw warm up %" B_PRId64 " ms\n", took);
	syslog(LOG_INFO, "app_server: gpu draw warm up %" B_PRId64 " ms\n", took);
	if (inParts) {
		// "warm-up", so the checks that read the line above skip this one
		fprintf(stderr, "app_server: gpu draw warm-up parts %" B_PRId32
			", longest %" B_PRId64 " ms\n", parts, longestPart / 1000);
		syslog(LOG_INFO, "app_server: gpu draw warm-up parts %" B_PRId32
			", longest %" B_PRId64 " ms\n", parts, longestPart / 1000);
	}
	if (finishStatus != B_OK) {
		// M13 step 4: a timeout is late, not failed (M11 decision 14)
		fprintf(stderr, "app_server: gpu draw warm up status %s\n",
			strerror(finishStatus));
		syslog(LOG_INFO, "app_server: gpu draw warm up status %s\n",
			strerror(finishStatus));
	}
}


bool
skia_gpu_drawing()
{
	SkiaGpu* gpu = SkiaGpu::Current();
	return gpu != NULL && !gpu->IsShutDown();
}


DrawingEngine*
skia_offscreen_engine(HWInterface* lockInterface, ServerBitmap* bitmap,
	const IntRect& origin, bool track)
{
	SkiaGpu* gpu = SkiaGpu::Current();
	if (gpu == NULL || gpu->IsShutDown() || bitmap == NULL
		|| !bitmap->IsValid())
		return NULL;
	// 32-bit memory only; other colour spaces keep Painter's back buffer
	// (BitmapHWInterface converts) and are a gap of the GPU drawing
	if (bitmap->ColorSpace() != B_RGBA32 && bitmap->ColorSpace() != B_RGB32)
		return NULL;
	MemoryTarget* target = new(std::nothrow) MemoryTarget(gpu, bitmap);
	if (target == NULL)
		return NULL;
	SkiaDrawingEngine* engine = new(std::nothrow) SkiaDrawingEngine(
		lockInterface, target);
	if (engine == NULL) {
		delete target;
		return NULL;
	}
	if (!engine->AttachSurface(bitmap->Width(), bitmap->Height(),
			origin.left, origin.top, bitmap->Bits(), bitmap->BytesPerRow())) {
		delete engine;
		return NULL;
	}
	engine->SetPixelBitmap(bitmap);
	if (track) {
		BAutolock _(sTrackLock);
		sTracked[bitmap] = engine;
		engine->SetTrackedBitmap(bitmap);
	}
	return engine;
}


void
skia_offscreen_untrack(const ServerBitmap* bitmap)
{
	BAutolock _(sTrackLock);
	sTracked.erase(bitmap);
}


void
skia_offscreen_sync(const ServerBitmap* bitmap)
{
	// the wait itself is global (everything recorded so far), so it needs
	// no engine and is done without the track lock (review)
	SkiaGpu* gpu = NULL;
	{
		BAutolock _(sTrackLock);
		auto found = sTracked.find(bitmap);
		if (found == sTracked.end() || !found->second->OnGpu())
			return;
		gpu = SkiaGpu::Current();
	}
	if (gpu == NULL || gpu->Finish() != B_OK)
		return;
	// the next drawing into it starts from the memory again (point 2),
	// if the bitmap is still there
	BAutolock _(sTrackLock);
	auto found = sTracked.find(bitmap);
	if (found == sTracked.end())
		return;
	SkiaTarget* target = found->second->Target();
	if (target->Lock()) {
		target->SetUploadPending();
		target->Unlock();
	}
}


status_t
skia_offscreen_finish(DrawingEngine* _engine)
{
	SkiaDrawingEngine* engine = dynamic_cast<SkiaDrawingEngine*>(_engine);
	if (engine == NULL || !engine->OnGpu())
		return B_OK;
	// what was drawn is copied out (each drawing call added its damage);
	// afterwards the next drawing starts from the memory again (point 2)
	status_t status = engine->Finish();
	SkiaTarget* target = engine->Target();
	if (status == B_OK && target->Lock()) {
		target->SetUploadPending();
		target->Unlock();
	}
	return status;
}


status_t
skia_offscreen_upload(DrawingEngine* _engine)
{
	SkiaDrawingEngine* engine = dynamic_cast<SkiaDrawingEngine*>(_engine);
	if (engine == NULL || !engine->OnGpu())
		return B_OK;
	MemoryTarget* target = dynamic_cast<MemoryTarget*>(engine->Target());
	if (target == NULL || target->Bitmap() == NULL)
		return B_BAD_VALUE;
	if (!target->Lock())
		return B_ERROR;
	ServerBitmap* bitmap = target->Bitmap();
	status_t status = target->Upload(bitmap->Bits(), bitmap->BytesPerRow())
		? B_OK : B_ERROR;
	target->Unlock();
	return status;
}
