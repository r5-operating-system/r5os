/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12 step 2: DrawCompare.h.
 */


#include "DrawCompare.h"

#include <stdio.h>
#include <math.h>
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

#include <LinkReceiver.h>
#include <Referenceable.h>

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


static BPoint
play(DrawingEngine* engine, ServerPicture* picture, const IntRect& bounds,
	uint32 background)
{
	BPoint pen(-1, -1);
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
		// the pen after DrawString() (CanvasCallbacks::DrawString())
		pen = canvas.CurrentState()->PenLocation();
		engine->UnlockParallelAccess();
	}
	return pen;
}


/*!	Text: the ink's left, top, right and bottom (pixels that moved at least
	half way from the background), the largest difference in pixels.
*/
static float
ink_box_difference(const uint8* a, uint32 aRow, const uint8* g, uint32 gRow,
	int32 width, int32 height, uint32 background)
{
	const uint8* back = (const uint8*)&background;
	auto move = [back](const uint8* p) {
		int m = 0;
		for (int c = 0; c < 4; c++)
			m = max_c(m, abs((int)p[c] - (int)back[c]));
		return m;
	};
	int maxMove = 0;
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++)
			maxMove = max_c(maxMove, move(a + y * aRow + x * 4));
	}
	if (maxMove == 0)
		return 0;
	int box[2][4] = { { width, height, -1, -1 }, { width, height, -1, -1 } };
	for (int i = 0; i < 2; i++) {
		const uint8* bits = i == 0 ? a : g;
		uint32 row = i == 0 ? aRow : gRow;
		for (int32 y = 0; y < height; y++) {
			for (int32 x = 0; x < width; x++) {
				if (move(bits + y * row + x * 4) * 2 < maxMove)
					continue;
				box[i][0] = min_c(box[i][0], x);
				box[i][1] = min_c(box[i][1], y);
				box[i][2] = max_c(box[i][2], x);
				box[i][3] = max_c(box[i][3], y);
			}
		}
	}
	float worst = 0;
	for (int k = 0; k < 4; k++)
		worst = max_c(worst, (float)abs(box[0][k] - box[1][k]));
	return worst;
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


static uint32
back_premultiplied(uint32 background)
{
	const uint8* b = (const uint8*)&background;
	if (b[3] == 255)
		return background;
	uint32 result;
	uint8* r = (uint8*)&result;
	for (int c = 0; c < 3; c++)
		r[c] = (b[c] * b[3] + 127) / 255;
	r[3] = b[3];
	return result;
}


/*!	The edge rule of part 5. The ink of a pixel is how far it moved from
	the background (the largest channel), divided by the largest move in
	Painter's image, so a fully covered pixel is 1. Summed per row, the ink
	is the width of the shape in that row (or of its strokes); per column
	its height. The result is the largest difference, in pixels, between
	the two images, of a row's or a column's ink, or of its centre (for
	rows and columns with at least one pixel of ink): position, size and
	stroke width, not single pixel values.
*/
static float
edge_difference(const uint8* a, uint32 aRow, const uint8* g, uint32 gRow,
	int32 width, int32 height, uint32 background)
{
	const uint8* back = (const uint8*)&background;
	auto move = [back](const uint8* p) {
		int m = 0;
		for (int c = 0; c < 4; c++)
			m = max_c(m, abs((int)p[c] - (int)back[c]));
		return m;
	};
	int maxMove = 0;
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++)
			maxMove = max_c(maxMove, move(a + y * aRow + x * 4));
	}
	if (maxMove == 0)
		return 0;
	std::vector<double> rowA(height, 0), rowG(height, 0);
	std::vector<double> rowCA(height, 0), rowCG(height, 0);
	std::vector<double> colA(width, 0), colG(width, 0);
	std::vector<double> colCA(width, 0), colCG(width, 0);
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			double inkA = (double)move(a + y * aRow + x * 4) / maxMove;
			double inkG = (double)move(g + y * gRow + x * 4) / maxMove;
			rowA[y] += inkA;
			rowG[y] += inkG;
			rowCA[y] += inkA * x;
			rowCG[y] += inkG * x;
			colA[x] += inkA;
			colG[x] += inkG;
			colCA[x] += inkA * y;
			colCG[x] += inkG * y;
		}
	}
	double worst = 0;
	auto check = [&worst](const std::vector<double>& sumA,
		const std::vector<double>& sumG, const std::vector<double>& centreA,
		const std::vector<double>& centreG) {
		for (size_t i = 0; i < sumA.size(); i++) {
			worst = std::max(worst, fabs(sumA[i] - sumG[i]));
			if (sumA[i] >= 1 && sumG[i] >= 1) {
				worst = std::max(worst, fabs(centreA[i] / sumA[i]
					- centreG[i] / sumG[i]));
			}
		}
	};
	check(rowA, rowG, rowCA, rowCG);
	check(colA, colG, colCA, colCG);
	return (float)worst;
}


static inline bool
same(const uint8* a, const uint8* b)
{
	return memcmp(a, b, 4) == 0;
}


status_t
draw_compare_import(BPrivate::LinkReceiver& link,
	BReference<ServerPicture>& _picture)
{
	BReference<ServerPicture> picture(new(std::nothrow) ServerPicture, true);
	if (picture == NULL)
		return B_NO_MEMORY;
	int32 count = 0;
	if (link.Read<int32>(&count) != B_OK || count < 0 || count > 64)
		return B_BAD_DATA;
	for (int32 i = 0; i < count; i++) {
		BReference<ServerPicture> nested;
		status_t status = draw_compare_import(link, nested);
		if (status != B_OK)
			return status;
		if (picture->NestPicture(nested.Get()) < 0)
			return B_NO_MEMORY;
	}
	status_t status = picture->ImportData(link);
	if (status == B_OK)
		_picture = picture;
	return status;
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
	BPoint painterPen;
	BPoint gpuPen;
	for (int32 i = 0; i < repeat; i++) {
		bigtime_t start = system_time();
		painterPen = play(&painter, painterPicture, bounds,
			request.background);
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
		skia.SetOpaque(((const uint8*)&request.background)[3] == 255);
		if (!skia.SetTargetSize(width, height)) {
			gSubpixelAntialiasing = savedSubpixel;
			result.gpuFailed = true;
			return B_ERROR;
		}
		for (int32 i = 0; i < repeat; i++) {
			bigtime_t start = system_time();
			gpuPen = play(&skia, picture, bounds, request.background);
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
		const uint8* rawA = (const uint8*)painted->Bits();
		uint32 aRow = painted->BytesPerRow();
		const uint8* rawG = target->Pixels();
		uint32 gRow = width * 4;
		const uint8* a = rawA;
		const uint8* g = rawG;
		// part 5 (owner, 15 Nov 2026): on a transparent background both
		// images are compared premultiplied (colour times alpha), with the
		// same limits; a colour under alpha 0 is not seen
		std::vector<uint8> premulA;
		std::vector<uint8> premulG;
		if (((const uint8*)&request.background)[3] == 0) {
			premulA.resize(height * aRow);
			premulG.resize(height * gRow);
			for (int32 y = 0; y < height; y++) {
				for (int32 x = 0; x < width; x++) {
					const uint8* pa = rawA + y * aRow + x * 4;
					const uint8* pg = rawG + y * gRow + x * 4;
					uint8* qa = &premulA[y * aRow + x * 4];
					uint8* qg = &premulG[y * gRow + x * 4];
					for (int c = 0; c < 3; c++) {
						qa[c] = (pa[c] * pa[3] + 127) / 255;
						qg[c] = (pg[c] * pg[3] + 127) / 255;
					}
					qa[3] = pa[3];
					qg[3] = pg[3];
				}
			}
			a = premulA.data();
			g = premulG.data();
		}
		if (request.dump != 0) {
			char name[32];
			snprintf(name, sizeof(name), "%d-painter", (int)request.dump);
			dump(name, rawA, aRow, width, height);
			snprintf(name, sizeof(name), "%d-gpu", (int)request.dump);
			dump(name, rawG, gRow, width, height);
		}
		const uint8* back = (const uint8*)&request.background;
		int32 left = width, top = height, right = -1, bottom = -1;
		for (int32 y = 0; y < height; y++) {
			for (int32 x = 0; x < width; x++) {
				if (!same(rawA + y * aRow + x * 4, back)
					|| !same(rawG + y * gRow + x * 4, back)) {
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
					&& (klass != kCompareGradient || difference > 3))
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
			case kComparePath:
			case kCompareText:
			case kCompareGradient:
			{
				// part 5 (owner, 15 Nov 2026): the interior keeps its
				// limits; an edge is judged on its position, its size and
				// the stroke width only, not on single pixel values
				float limit = klass == kCompareText ? 1.0f : 0.5f;
				result.edge = edge_difference(a, aRow, g, gRow, width,
					height, back_premultiplied(request.background));
				ok = ok && result.edge <= limit;
				if (klass == kCompareText) {
					// the letters may differ, the metrics may not: the pen
					// after the string (advance) is the same, and the ink's
					// box (baseline, height, width) within one pixel
					result.penEqual = painterPen == gpuPen;
					result.inkBox = ink_box_difference(a, aRow, g, gRow,
						width, height, back_premultiplied(request.background));
					ok = ok && result.penEqual && result.inkBox <= 1.0f;
				}
				break;
			}
			case kCompareNearest:
				ok = result.share <= 1.0f && nearestMiss == 0;
				break;
			case kCompareBilinear:
				ok = ok && maxDifference <= 4 && result.share <= 5.0f;
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
