/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12: SkiaOffscreen.h.
 */


#include "SkiaOffscreen.h"

#include <string.h>

#include <Autolock.h>

#include <map>

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
