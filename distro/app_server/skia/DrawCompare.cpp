/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 step 2: DrawCompare.h.
 */


#include "DrawCompare.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "BitmapDrawingEngine.h"
#include "BitmapHWInterface.h"
#include "Canvas.h"
#include "DrawState.h"
#include "GlobalSubpixelSettings.h"
#include "ServerBitmap.h"
#include "ServerPicture.h"

#include "SkiaDrawingEngine.h"
#include "SkiaGpu.h"
#include "SkiaPainter.h"


/*!	An image of its own; RunWork copies all of it into fPixels. */
class CompareTarget : public SkiaTarget {
public:
	CompareTarget(SkiaGpu* gpu, int32 width, int32 height)
		:
		SkiaTarget(gpu),
		fPixelWidth(width),
		fPixelHeight(height),
		fPixels((uint8*)calloc(width * height, 4)),
		fGot(false)
	{
	}

	~CompareTarget()
	{
		free(fPixels);
	}

	virtual void* PrepareDelivery() { return this; }

	virtual void Deliver(void* cookie, const BRegion& damage,
		const clipping_rect& frame, const uint8* bits, uint32 bytesPerRow)
	{
		if (fPixels == NULL)
			return;
		for (int32 y = frame.top; y <= frame.bottom; y++) {
			if (y < 0 || y >= fPixelHeight)
				continue;
			int32 left = max_c(frame.left, 0);
			int32 right = min_c(frame.right, fPixelWidth - 1);
			if (left > right)
				continue;
			memcpy(fPixels + (y * fPixelWidth + left) * 4,
				bits + (y - frame.top) * bytesPerRow + (left - frame.left) * 4,
				(right - left + 1) * 4);
		}
		fGot = true;
	}

	virtual void DeliveryFailed(void* cookie) {}

	const uint8* Pixels() const { return fPixels; }
	bool Got() const { return fGot; }
	void Reset() { fGot = false; }

private:
	int32		fPixelWidth;
	int32		fPixelHeight;
	uint8*		fPixels;
	bool		fGot;
};


static void
play(DrawingEngine* engine, ServerPicture* picture, const IntRect& bounds,
	uint32 background)
{
	DrawState state;
	OffscreenCanvas canvas(engine, state, bounds);
	if (engine->LockParallelAccess()) {
		BRegion clipping;
		clipping.Set((clipping_rect)bounds);
		engine->ConstrainClippingRegion(&clipping);
		const uint8* b = (const uint8*)&background;
		rgb_color color = { b[2], b[1], b[0], b[3] };
		// the same background in both engines (a server call, class E)
		engine->FillRect(BRect(bounds.left, bounds.top, bounds.right,
			bounds.bottom), color);
		picture->Play(&canvas);
		engine->UnlockParallelAccess();
	}
}


/*!	--dump: the image as a PAM file (RGBA), for a look at one operation. */
static void
dump(const char* name, const uint8* bits, uint32 bytesPerRow, int32 width,
	int32 height)
{
	mkdir("/boot/home/draw-compare", 0755);
	char path[256];
	snprintf(path, sizeof(path), "/boot/home/draw-compare/%s.pam", name);
	FILE* file = fopen(path, "wb");
	if (file == NULL)
		return;
	fprintf(file, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\n"
		"TUPLTYPE RGB_ALPHA\nENDHDR\n", (int)width, (int)height);
	for (int32 y = 0; y < height; y++) {
		const uint8* row = bits + y * bytesPerRow;
		for (int32 x = 0; x < width; x++) {
			uint8 p[4] = { row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0],
				row[x * 4 + 3] };
			fwrite(p, 1, 4, file);
		}
	}
	fclose(file);
}


static inline bool
same(const uint8* a, const uint8* b)
{
	return memcmp(a, b, 4) == 0;
}


status_t
draw_compare(SkiaGpu* gpu, const draw_compare_request& request,
	ServerPicture* picture, ServerPicture* scaledPicture, int32 repeat,
	draw_compare_result& result)
{
	memset(&result, 0, sizeof(result));
	int32 width = request.width;
	int32 height = request.height;
	if (gpu == NULL || gpu->IsShutDown() || width <= 0 || height <= 0
		|| width > 2048 || height > 2048 || picture == NULL)
		return B_BAD_VALUE;
	IntRect bounds(0, 0, width - 1, height - 1);
	if (repeat < 1)
		repeat = 1;

	bool savedSubpixel = gSubpixelAntialiasing;

	// Painter, on a memory buffer
	BitmapDrawingEngine painter(B_RGBA32);
	if (painter.SetSize(width, height) != B_OK)
		return B_NO_MEMORY;
	ServerPicture* painterPicture = request.scale != 1.0f
		&& scaledPicture != NULL ? scaledPicture : picture;
	// Q12: Painter's grey shapes are the reference, also when subpixel
	// antialiasing is on (request 1); 2 keeps it for both (text)
	if (request.subpixel == 0 || request.subpixel == 1)
		gSubpixelAntialiasing = false;
	else if (request.subpixel == 2)
		gSubpixelAntialiasing = true;
	std::vector<bigtime_t> cpuTimes;
	for (int32 i = 0; i < repeat; i++) {
		bigtime_t start = system_time();
		play(&painter, painterPicture, bounds, request.background);
		cpuTimes.push_back(system_time() - start);
	}
	BReference<UtilityBitmap> painted(painter.ExportToBitmap(width, height,
		B_RGBA32), true);
	if (painted == NULL) {
		gSubpixelAntialiasing = savedSubpixel;
		return B_NO_MEMORY;
	}

	// Skia, on a GPU image of the same size
	if (request.subpixel == 1)
		gSubpixelAntialiasing = true;
	BReference<UtilityBitmap> lockBitmap(new(std::nothrow) UtilityBitmap(
		BRect(0, 0, width - 1, height - 1), B_RGBA32, 0), true);
	if (lockBitmap == NULL) {
		gSubpixelAntialiasing = savedSubpixel;
		return B_NO_MEMORY;
	}
	BitmapHWInterface lockInterface(lockBitmap);
	lockInterface.Initialize();
	CompareTarget* target = new(std::nothrow) CompareTarget(gpu, width,
		height);
	if (target == NULL || target->Pixels() == NULL) {
		delete target;
		gSubpixelAntialiasing = savedSubpixel;
		return B_NO_MEMORY;
	}
	std::vector<bigtime_t> gpuTimes;
	{
		SkiaDrawingEngine skia(&lockInterface, target);
		skia.SetScale(request.scale);
		if (!skia.SetTargetSize(width, height)) {
			gSubpixelAntialiasing = savedSubpixel;
			result.gpuFailed = true;
			return B_ERROR;
		}
		for (int32 i = 0; i < repeat; i++) {
			bigtime_t start = system_time();
			play(&skia, picture, bounds, request.background);
			if (target->Lock()) {
				target->AddDamage(BRect(0, 0, width - 1, height - 1));
				target->Unlock();
			}
			target->Reset();
			status_t status = skia.Finish();
			gpuTimes.push_back(system_time() - start);
			if (status != B_OK || !target->Got()) {
				gSubpixelAntialiasing = savedSubpixel;
				result.gpuFailed = true;
				return B_ERROR;
			}
		}
		gSubpixelAntialiasing = savedSubpixel;

		// part 5: the box of what either engine drew, grown by 2
		const uint8* a = (const uint8*)painted->Bits();
		uint32 aRow = painted->BytesPerRow();
		const uint8* g = target->Pixels();
		uint32 gRow = width * 4;
		if (request.dump != 0) {
			char name[32];
			snprintf(name, sizeof(name), "%d-painter", (int)request.dump);
			dump(name, a, aRow, width, height);
			snprintf(name, sizeof(name), "%d-gpu", (int)request.dump);
			dump(name, g, gRow, width, height);
		}
		const uint8* back = (const uint8*)&request.background;
		int32 left = width, top = height, right = -1, bottom = -1;
		for (int32 y = 0; y < height; y++) {
			for (int32 x = 0; x < width; x++) {
				if (!same(a + y * aRow + x * 4, back)
					|| !same(g + y * gRow + x * 4, back)) {
					left = min_c(left, x);
					right = max_c(right, x);
					top = min_c(top, y);
					bottom = max_c(bottom, y);
				}
			}
		}
		if (right < 0) {
			result.ok = true;
			return B_OK;
		}
		left = max_c(left - 2, 0);
		top = max_c(top - 2, 0);
		right = min_c(right + 2, width - 1);
		bottom = min_c(bottom + 2, height - 1);

		int32 klass = request.compareClass;
		int32 threshold = klass == kCompareText ? 16 : 1;
		int32 over = 0;
		int32 pixels = 0;
		int32 maxDifference = 0;
		int32 inner = 0;
		int32 nearestMiss = 0;
		for (int32 y = top; y <= bottom; y++) {
			for (int32 x = left; x <= right; x++) {
				const uint8* pa = a + y * aRow + x * 4;
				const uint8* pg = g + y * gRow + x * 4;
				pixels++;
				int32 difference = 0;
				for (int c = 0; c < 4; c++)
					difference = max_c(difference, abs((int)pa[c] - (int)pg[c]));
				if (difference == 0)
					continue;
				maxDifference = max_c(maxDifference, difference);
				if (difference > threshold)
					over++;
				// an inner pixel: its 3x3 neighbours in Painter's image are
				// all the same
				bool isInner = x > 0 && y > 0 && x < width - 1
					&& y < height - 1;
				for (int32 dy = -1; isInner && dy <= 1; dy++) {
					for (int32 dx = -1; isInner && dx <= 1; dx++) {
						if (!same(a + (y + dy) * aRow + (x + dx) * 4, pa))
							isInner = false;
					}
				}
				if (isInner && klass != kCompareNearest
					&& klass != kCompareGradient)
					inner++;
				if (klass == kCompareNearest) {
					bool neighbour = false;
					for (int32 dy = -1; !neighbour && dy <= 1; dy++) {
						for (int32 dx = -1; !neighbour && dx <= 1; dx++) {
							int32 nx = x + dx, ny = y + dy;
							if (nx < 0 || ny < 0 || nx >= width || ny >= height)
								continue;
							if (same(a + ny * aRow + nx * 4, pg))
								neighbour = true;
						}
					}
					if (!neighbour)
						nearestMiss++;
				}
			}
		}
		result.maxDifference = maxDifference;
		result.pixels = pixels;
		result.share = pixels > 0 ? 100.0f * over / pixels : 0;
		result.innerDifferences = inner;
		bool ok = inner == 0;
		switch (klass) {
			case kCompareExact:
				ok = ok && maxDifference == 0;
				break;
			case kCompareAnalytic:
				ok = ok && maxDifference <= 24 && result.share <= 3.0f;
				break;
			case kComparePath:
				ok = ok && maxDifference <= 64 && result.share <= 6.0f;
				break;
			case kCompareText:
				ok = ok && result.share <= 10.0f;
				break;
			case kCompareNearest:
				ok = result.share <= 1.0f && nearestMiss == 0;
				break;
			case kCompareBilinear:
				ok = ok && maxDifference <= 4 && result.share <= 5.0f;
				break;
			case kCompareGradient:
				ok = maxDifference <= 3;
				break;
		}
		result.ok = ok;
	}

	std::sort(cpuTimes.begin(), cpuTimes.end());
	std::sort(gpuTimes.begin(), gpuTimes.end());
	result.cpuTime = cpuTimes[cpuTimes.size() / 2];
	result.gpuTime = gpuTimes[gpuTimes.size() / 2];
	return B_OK;
}
