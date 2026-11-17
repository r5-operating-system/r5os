/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M12: SkiaDrawingEngine (SkiaDrawingEngine.h). In the GPU mode each call
 * computes its bounds as Haiku's DrawingEngine.cpp does (Copyright
 * 2001-2018 Haiku, Inc., MIT; followed, not copied) and draws with
 * SkiaPainter; the changed pixels reach the window buffer when the compose
 * thread has run the recording (SkiaGpu::RunWork), and only then are they
 * reported as damage.
 */


#include "SkiaDrawingEngine.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <Autolock.h>
#include <ObjectList.h>

#include <algorithm>

#include "Compositor.h"
#include "DrawState.h"
#include "ServerBitmap.h"
#include "WindowBuffer.h"
#include "WindowHWInterface.h"

#include "SkiaGpu.h"
#include "SkiaOffscreen.h"
#include "SkiaPainter.h"


static bool sDebug = false;

// Decision 9, Painter's side: one drawing session of a window is its first
// drawing call since the last compose until its last one returned (Painter
// writes the buffer directly). Window engines register here.
static BLocker sCpuLock("draw cpu");
static BObjectList<SkiaDrawingEngine, false> sCpuEngines(20);
static bigtime_t sCpuSamples[256];
static int32 sCpuCount = 0;


class SkiaDrawingEngine::CpuTimer {
public:
	CpuTimer(SkiaDrawingEngine* engine)
		:
		fEngine(sDebug && engine->fWindowInterface != NULL ? engine : NULL)
	{
		if (fEngine != NULL && fEngine->fCpuFirst == 0)
			fEngine->fCpuFirst = system_time();
	}

	~CpuTimer()
	{
		if (fEngine != NULL)
			fEngine->fCpuLast = system_time();
	}

private:
	SkiaDrawingEngine*	fEngine;
};


/*static*/ void
SkiaDrawingEngine::CloseCpuSessions()
{
	if (!sDebug)
		return;
	BAutolock _(sCpuLock);
	for (int32 i = 0; i < sCpuEngines.CountItems(); i++) {
		SkiaDrawingEngine* engine = sCpuEngines.ItemAt(i);
		bigtime_t first = engine->fCpuFirst;
		bigtime_t last = engine->fCpuLast;
		if (first == 0 || last < first)
			continue;
		engine->fCpuFirst = 0;
		sCpuSamples[sCpuCount++ % 256] = last - first;
	}
}


/*static*/ bool
SkiaDrawingEngine::TakeCpuMedian(bigtime_t* _median, int32* _count)
{
	BAutolock _(sCpuLock);
	int32 count = min_c(sCpuCount, 256);
	*_count = sCpuCount;
	sCpuCount = 0;
	if (count == 0)
		return false;
	std::sort(sCpuSamples, sCpuSamples + count);
	*_median = sCpuSamples[count / 2];
	return true;
}


static inline void
make_rect_valid(BRect& rect)
{
	if (rect.left > rect.right) {
		float temp = rect.left;
		rect.left = rect.right;
		rect.right = temp;
	}
	if (rect.top > rect.bottom) {
		float temp = rect.top;
		rect.top = rect.bottom;
		rect.bottom = temp;
	}
}


static inline void
extend_by_stroke_width(BRect& rect, float penSize)
{
	// "- 0.5" because if stroke width == 1, we don't need to extend
	float inset = -ceilf(penSize / 2.0 - 0.5);
	rect.InsetBy(inset, inset);
}


/*!	The window's image. M13 step 2: the compositor samples it; nothing is
	read back or copied into a window buffer.
*/
class SkiaDrawingEngine::WindowTarget : public SkiaTarget {
public:
	WindowTarget(SkiaGpu* gpu, Compositor* compositor,
			WindowHWInterface* windowInterface)
		:
		SkiaTarget(gpu),
		fCompositor(compositor),
		fInterface(windowInterface)
	{
	}

	virtual bool UsesSharedImage() const
	{
		return true;
	}

	// under the lock, from FrameBufferChanged()
	void SetBuffer(WindowBuffer* buffer, int32 left, int32 top)
	{
		// 5625U stick, 16 Nov 2026 (boot 3): a window that gets another
		// buffer (a new one, or one the compositor hands out again) must
		// show its whole image there. M13 step 2: the buffer shows the
		// image from the next run on (Drawn()), so a run is asked for.
		if (buffer != NULL && buffer != fBuffer.Get()) {
			AddDamage(BRect(left, top, left + (int32)buffer->Width() - 1,
				top + (int32)buffer->Height() - 1));
			MarkDrawn();
		}
		fBuffer.SetTo(buffer);
	}

	virtual void* PrepareDelivery()
	{
		return NULL;
	}

	virtual void Deliver(void* cookie, const BRegion& screenDamage,
		const clipping_rect& frame, const uint8* bits, uint32 bytesPerRow)
	{
		// never called: this target reads nothing back
	}

	virtual void Drawn(void* cookie, const BRegion& screenDamage,
		SkiaImage* image)
	{
		// The buffer the window has now: a slow run (seconds on Lavapipe)
		// can end after the compositor gave the window another buffer.
		BReference<WindowBuffer> current;
		int32 left = 0;
		int32 top = 0;
		if (fInterface->LockParallelAccess()) {
			current.SetTo(fInterface->Buffer());
			left = fInterface->Left();
			top = fInterface->Top();
			fInterface->UnlockParallelAccess();
		}
		WindowBuffer* buffer = current.Get();
		if (buffer == NULL)
			return;
		// An image of another size belongs to a buffer the window had
		// before a resize; the new buffer keeps showing the old image
		// (Compositor::ClippingChanged()) until its own image is drawn.
		if (image != NULL && image->Width() == (int32)buffer->Width()
			&& image->Height() == (int32)buffer->Height())
			buffer->SetImage(image, 0, 0);
		BRegion damage(screenDamage);
		BRegion inside(BRect(left, top, left + (int32)buffer->Width() - 1,
			top + (int32)buffer->Height() - 1));
		damage.IntersectWith(&inside);
		fCompositor->Damage(damage, fInterface);
	}

	virtual void DeliveryFailed(void* cookie)
	{
	}

private:
	Compositor*					fCompositor;
	WindowHWInterface*			fInterface;
	BReference<WindowBuffer>	fBuffer;
};


/*!	DrawTransaction of DrawingEngine.cpp for the GPU: the target is locked
	for the call, the dirty test is the same, and the damage is reported by
	the target when the pixels are in memory.
*/
class SkiaDrawingEngine::Draw {
public:
	Draw(SkiaDrawingEngine* engine, const BRect& bounds)
		:
		fEngine(engine),
		fLocked(engine->fTarget->Lock())
	{
		const BRegion* clip = engine->fSkia->ClippingRegion();
		if (fLocked)
			engine->fTarget->UploadIfPending();
		if (fLocked && clip != NULL) {
			fDirty.Set(bounds);
			fDirty.IntersectWith(clip);
		}
	}

	Draw(SkiaDrawingEngine* engine)
		:
		fEngine(engine),
		fLocked(engine->fTarget->Lock())
	{
		const BRegion* clip = engine->fSkia->ClippingRegion();
		if (fLocked)
			engine->fTarget->UploadIfPending();
		if (fLocked && clip != NULL)
			fDirty = *clip;
	}

	~Draw()
	{
		// M13 step 1: inside BeginUpdate()..EndUpdate() the run leaves the
		// window's drawing alone (SkiaTarget::Held()), so a wake per draw
		// call only makes runs for the other windows; EndUpdate()'s
		// CopyToFront() wakes once for the whole update. Drawing outside an
		// update, and an update held longer than 100 ms, wake as before,
		// so the input latency stays.
		bool held = false;
		if (fLocked) {
			held = fEngine->fTarget->Held(system_time());
			fEngine->fTarget->Unlock();
		}
		if (!held)
			fEngine->_Wake();
	}

	bool IsDirty() const
	{
		return fLocked && fDirty.CountRects() > 0
			&& fEngine->fTarget->HasSurface();
	}

private:
	SkiaDrawingEngine*	fEngine;
	bool				fLocked;
	BRegion				fDirty;
};


SkiaDrawingEngine::SkiaDrawingEngine(WindowHWInterface* windowInterface,
	SkiaGpu* gpu, Compositor* compositor)
	:
	DrawingEngine(NULL),
	fSkia(new(std::nothrow) SkiaPainter),
	fTarget(NULL),
	fOwnsTarget(true),
	fWindowInterface(windowInterface),
	fCompositor(compositor),
	fGpu(gpu),
	fOnGpu(false),
	fTrackedBitmap(NULL),
	fCpuFirst(0),
	fCpuLast(0)
{
	fTarget = new(std::nothrow) WindowTarget(gpu, compositor,
		windowInterface);
	// a new window has no glass view yet (Compositor::GlassViewChanged)
	if (fTarget != NULL)
		fTarget->SetOpaque(true);
	_Init();
	{
		BAutolock _(sCpuLock);
		sCpuEngines.AddItem(this);
	}
	SetHWInterface(windowInterface);
}


SkiaDrawingEngine::SkiaDrawingEngine(HWInterface* lockInterface,
	SkiaTarget* ownedTarget)
	:
	DrawingEngine(NULL),
	fSkia(new(std::nothrow) SkiaPainter),
	fTarget(ownedTarget),
	fOwnsTarget(true),
	fWindowInterface(NULL),
	fCompositor(NULL),
	fGpu(ownedTarget != NULL ? ownedTarget->Gpu() : NULL),
	fOnGpu(false),
	fTrackedBitmap(NULL),
	fCpuFirst(0),
	fCpuLast(0)
{
	_Init();
	SetHWInterface(lockInterface);
}


SkiaDrawingEngine::~SkiaDrawingEngine()
{
	{
		BAutolock _(sCpuLock);
		sCpuEngines.RemoveItem(this);
	}
	if (fTrackedBitmap != NULL)
		skia_offscreen_untrack(fTrackedBitmap);
	SetHWInterface(NULL);
	if (fGpu != NULL && fTarget != NULL)
		fGpu->RemoveTarget(fTarget);
	if (fOwnsTarget)
		delete fTarget;
	delete fSkia;
}


void
SkiaDrawingEngine::_Init()
{
	if (fSkia != NULL)
		fSkia->SetTarget(fTarget);
	if (fGpu != NULL && fTarget != NULL)
		fGpu->AddTarget(fTarget);
}


/*static*/ void
SkiaDrawingEngine::SetClassic(bool classic)
{
	SkiaPainter::SetClassic(classic);
}


/*static*/ void
SkiaDrawingEngine::SetHardwareBlend(bool on)
{
	SkiaPainter::SetHardwareBlend(on);
}


/*static*/ void
SkiaDrawingEngine::SetLinearEdges(bool linear)
{
	SkiaPainter::SetLinearEdges(linear);
}


/*static*/ void
SkiaDrawingEngine::SetDebug(bool debug)
{
	sDebug = debug;
}


bool
SkiaDrawingEngine::_Gpu() const
{
	// the GPU drawing went off (decision 8): Painter, as everywhere
	return fOnGpu && fGpu != NULL && !fGpu->IsShutDown();
}


void
SkiaDrawingEngine::SetOpaque(bool opaque)
{
	if (fTarget != NULL)
		fTarget->SetOpaque(opaque);
}


void
SkiaDrawingEngine::SetTrackedBitmap(ServerBitmap* bitmap)
{
	fTrackedBitmap = bitmap;
}


void
SkiaDrawingEngine::_Wake()
{
	if (fGpu != NULL && fOnGpu)
		fGpu->Wake();
}


status_t
SkiaDrawingEngine::Finish()
{
	if (!fOnGpu || fGpu == NULL)
		return B_OK;
	return fGpu->Finish();
}


bool
SkiaDrawingEngine::SetTargetSize(int32 width, int32 height)
{
	return AttachSurface(width, height, 0, 0, NULL, 0);
}


bool
SkiaDrawingEngine::AttachSurface(int32 width, int32 height, int32 left,
	int32 top, const uint8* bits, uint32 bytesPerRow)
{
	if (fTarget == NULL || fWindowInterface != NULL || fGpu == NULL
		|| fGpu->IsShutDown())
		return false;
	if (!fTarget->Lock())
		return false;
	bool ok = fTarget->SetSurface(width, height, left, top, bits,
		bytesPerRow);
	fTarget->Unlock();
	fOnGpu = ok;
	return ok;
}


void
SkiaDrawingEngine::SetScale(float scale)
{
	fSkia->SetScale(scale);
}


void
SkiaDrawingEngine::FrameBufferChanged()
{
	DrawingEngine::FrameBufferChanged();
	if (fSkia == NULL || fTarget == NULL)
		return;
	if (fWindowInterface == NULL) {
		// an engine on its own target is on the GPU once it has a size
		return;
	}

	HWInterface* card = GraphicsCard();
	bool gpu = card != NULL && card == fWindowInterface && fGpu != NULL
		&& !fGpu->IsShutDown();
	if (!fTarget->Lock())
		return;
	if (gpu) {
		WindowBuffer* buffer = fWindowInterface->Buffer();
		if (buffer == NULL)
			gpu = false;
		else {
			gpu = fTarget->SetSurface(buffer->Width(), buffer->Height(),
				fWindowInterface->Left(), fWindowInterface->Top(),
				(const uint8*)buffer->Bits(), buffer->BytesPerRow());
			((WindowTarget*)fTarget)->SetBuffer(buffer,
				fWindowInterface->Left(), fWindowInterface->Top());
			if (!gpu) {
				// never Painter for this one window while the others draw
				// on the GPU (decision 1): the whole desktop switches
				// (decision 8); until then this window draws nothing
				fGpu->ReportFailure("window image");
				gpu = true;
			}
		}
	}
	fOnGpu = gpu;
	fTarget->Unlock();
}


void
SkiaDrawingEngine::ReleaseBuffer()
{
	// WindowHWInterface::SetBuffer(NULL) does not call
	// FrameBufferChanged(), so the target let go of nothing and the
	// compositor's eviction freed no memory for a Skia window
	if (fWindowInterface == NULL || fTarget == NULL || !fTarget->Lock())
		return;
	((WindowTarget*)fTarget)->SetBuffer(NULL, 0, 0);
	// M13 step 2: the image is the window's memory now
	fTarget->ReleaseSurface();
	fTarget->Unlock();
}


void
SkiaDrawingEngine::SetCopyToFrontEnabled(bool enable)
{
	DrawingEngine::SetCopyToFrontEnabled(enable);
	// Window::_BeginUpdate() turns it off, EndUpdate() on again and then
	// calls CopyToFront(), which wakes the run that takes the update
	if (fTarget != NULL && fTarget->Lock()) {
		fTarget->SetHeld(!enable);
		fTarget->Unlock();
	}
}


void
SkiaDrawingEngine::CopyToFront(BRegion& region)
{
	// the GPU mode reports damage when the pixels are in the buffer
	if (!_Gpu())
		DrawingEngine::CopyToFront(region);
	else
		_Wake();
}


status_t
SkiaDrawingEngine::ReadBitmap(ServerBitmap* bitmap, bool drawCursor,
	BRect bounds)
{
	// a read point (part 6): the buffer holds everything drawn so far
	if (fOnGpu)
		Finish();
	return DrawingEngine::ReadBitmap(bitmap, drawCursor, bounds);
}


//	#pragma mark - state: both engines get it


void
SkiaDrawingEngine::ConstrainClippingRegion(const BRegion* region)
{
	DrawingEngine::ConstrainClippingRegion(region);
	fSkia->ConstrainClipping(region);
}


void
SkiaDrawingEngine::SetDrawState(const DrawState* state, int32 xOffset,
	int32 yOffset)
{
	DrawingEngine::SetDrawState(state, xOffset, yOffset);
	fSkia->SetDrawState(state, xOffset, yOffset);
}


void
SkiaDrawingEngine::SetHighColor(const rgb_color& color)
{
	DrawingEngine::SetHighColor(color);
	fSkia->SetHighColor(color);
}


void
SkiaDrawingEngine::SetLowColor(const rgb_color& color)
{
	DrawingEngine::SetLowColor(color);
	fSkia->SetLowColor(color);
}


void
SkiaDrawingEngine::SetPenSize(float size)
{
	DrawingEngine::SetPenSize(size);
	fSkia->SetPenSize(size);
}


void
SkiaDrawingEngine::SetStrokeMode(cap_mode lineCap, join_mode joinMode,
	float miterLimit)
{
	DrawingEngine::SetStrokeMode(lineCap, joinMode, miterLimit);
	fSkia->SetStrokeMode(lineCap, joinMode, miterLimit);
}


void
SkiaDrawingEngine::SetFillRule(int32 fillRule)
{
	DrawingEngine::SetFillRule(fillRule);
	fSkia->SetFillRule(fillRule);
}


void
SkiaDrawingEngine::SetPattern(const struct pattern& pattern)
{
	DrawingEngine::SetPattern(pattern);
	fSkia->SetPattern(pattern);
}


void
SkiaDrawingEngine::SetDrawingMode(drawing_mode mode)
{
	DrawingEngine::SetDrawingMode(mode);
	fSkia->SetDrawingMode(mode);
}


void
SkiaDrawingEngine::SetDrawingMode(drawing_mode mode, drawing_mode& oldMode)
{
	DrawingEngine::SetDrawingMode(mode, oldMode);
	fSkia->SetDrawingMode(mode);
}


void
SkiaDrawingEngine::SetBlendingMode(source_alpha srcAlpha,
	alpha_function alphaFunc)
{
	DrawingEngine::SetBlendingMode(srcAlpha, alphaFunc);
	fSkia->SetBlendingMode(srcAlpha, alphaFunc);
}


void
SkiaDrawingEngine::SetFont(const ServerFont& font)
{
	DrawingEngine::SetFont(font);
	fSkia->SetFont(font);
}


void
SkiaDrawingEngine::SetFont(const DrawState* state)
{
	DrawingEngine::SetFont(state);
	fSkia->SetFont(state);
}


void
SkiaDrawingEngine::SetTransform(const BAffineTransform& transform,
	int32 xOffset, int32 yOffset)
{
	DrawingEngine::SetTransform(transform, xOffset, yOffset);
	fSkia->SetTransform(transform, xOffset, yOffset);
}


//	#pragma mark - drawing


void
SkiaDrawingEngine::CopyRegion(BRegion* region, int32 xOffset, int32 yOffset)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::CopyRegion(region, xOffset, yOffset);
		return;
	}
	Draw draw(this);
	if (!draw.IsDirty() || region == NULL)
		return;
	BRegion touched;
	fSkia->CopyRegion(*region, xOffset, yOffset, touched);
}


void
SkiaDrawingEngine::InvertRect(BRect r)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::InvertRect(r);
		return;
	}
	make_rect_valid(r);
	Draw draw(this, fSkia->ClipRect(r));
	if (!draw.IsDirty())
		return;
	fSkia->InvertRect(r);
}


void
SkiaDrawingEngine::DrawBitmap(ServerBitmap* bitmap, const BRect& bitmapRect,
	const BRect& viewRect, uint32 options)
{
	// a read point of a bitmap the GPU draws into (part 6, point 3),
	// before this engine takes its own lock
	skia_offscreen_sync(bitmap);
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawBitmap(bitmap, bitmapRect, viewRect, options);
		return;
	}
	Draw draw(this, fSkia->TransformAndClipRect(viewRect));
	if (draw.IsDirty())
		fSkia->DrawBitmap(bitmap, bitmapRect, viewRect, options);
}


void
SkiaDrawingEngine::DrawArc(BRect r, const float& angle, const float& span,
	bool filled)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawArc(r, angle, span, filled);
		return;
	}
	make_rect_valid(r);
	fSkia->AlignEllipseRect(&r, filled);
	BRect clipped(r);
	if (!filled)
		extend_by_stroke_width(clipped, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(clipped));
	if (!draw.IsDirty())
		return;
	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius, r.top + yRadius);
	fSkia->DrawArc(center, xRadius, yRadius, angle, span, filled, NULL);
}


void
SkiaDrawingEngine::DrawArc(BRect r, const float& angle, const float& span,
	bool filled, const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawArc(r, angle, span, filled, gradient);
		return;
	}
	make_rect_valid(r);
	fSkia->AlignEllipseRect(&r, true);
	Draw draw(this, fSkia->TransformAndClipRect(r));
	if (!draw.IsDirty())
		return;
	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius, r.top + yRadius);
	fSkia->DrawArc(center, xRadius, yRadius, angle, span, filled, &gradient);
}


void
SkiaDrawingEngine::DrawBezier(BPoint* pts, bool filled)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawBezier(pts, filled);
		return;
	}
	Draw draw(this);
	if (draw.IsDirty())
		fSkia->DrawBezier(pts, filled, NULL);
}


void
SkiaDrawingEngine::DrawBezier(BPoint* pts, bool filled,
	const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawBezier(pts, filled, gradient);
		return;
	}
	Draw draw(this);
	if (draw.IsDirty())
		fSkia->DrawBezier(pts, filled, &gradient);
}


void
SkiaDrawingEngine::DrawEllipse(BRect r, bool filled)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawEllipse(r, filled);
		return;
	}
	make_rect_valid(r);
	BRect clipped = r;
	fSkia->AlignEllipseRect(&clipped, filled);
	if (!filled)
		extend_by_stroke_width(clipped, fSkia->PenSize());
	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);
	Draw draw(this, fSkia->TransformAndClipRect(clipped));
	if (!draw.IsDirty())
		return;
	fSkia->DrawEllipse(r, filled, NULL);
}


void
SkiaDrawingEngine::DrawEllipse(BRect r, bool filled,
	const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawEllipse(r, filled, gradient);
		return;
	}
	make_rect_valid(r);
	BRect clipped = r;
	fSkia->AlignEllipseRect(&clipped, filled);
	if (!filled)
		extend_by_stroke_width(clipped, fSkia->PenSize());
	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);
	Draw draw(this, fSkia->TransformAndClipRect(clipped));
	if (!draw.IsDirty())
		return;
	fSkia->DrawEllipse(r, filled, &gradient);
}


void
SkiaDrawingEngine::DrawPolygon(BPoint* ptlist, int32 numpts, BRect bounds,
	bool filled, bool closed)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawPolygon(ptlist, numpts, bounds, filled, closed);
		return;
	}
	make_rect_valid(bounds);
	if (!filled)
		extend_by_stroke_width(bounds, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(bounds));
	if (!draw.IsDirty())
		return;
	fSkia->DrawPolygon(ptlist, numpts, filled, closed, NULL);
}


void
SkiaDrawingEngine::DrawPolygon(BPoint* ptlist, int32 numpts, BRect bounds,
	bool filled, bool closed, const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawPolygon(ptlist, numpts, bounds, filled, closed,
			gradient);
		return;
	}
	make_rect_valid(bounds);
	if (!filled)
		extend_by_stroke_width(bounds, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(bounds));
	if (!draw.IsDirty())
		return;
	fSkia->DrawPolygon(ptlist, numpts, filled, closed, &gradient);
}


void
SkiaDrawingEngine::StrokePoint(const BPoint& point, const rgb_color& color)
{
	StrokeLine(point, point, color);
}


void
SkiaDrawingEngine::StrokeLine(const BPoint& start, const BPoint& end,
	const rgb_color& color)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::StrokeLine(start, end, color);
		return;
	}
	BRect touched(start, end);
	make_rect_valid(touched);
	touched = fSkia->ClipRect(touched);
	Draw draw(this, touched);
	if (!draw.IsDirty())
		return;
	if (!fSkia->StraightLine(start, end, color)) {
		rgb_color previousColor = fSkia->HighColor();
		drawing_mode previousMode = fSkia->DrawingMode();
		fSkia->SetHighColor(color);
		fSkia->SetDrawingMode(B_OP_OVER);
		fSkia->StrokeLine(start, end);
		fSkia->SetDrawingMode(previousMode);
		fSkia->SetHighColor(previousColor);
	}
}


void
SkiaDrawingEngine::StrokeRect(BRect r, const rgb_color& color)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::StrokeRect(r, color);
		return;
	}
	make_rect_valid(r);
	Draw draw(this, fSkia->ClipRect(r));
	if (!draw.IsDirty())
		return;
	fSkia->StrokeRect(r, color);
}


void
SkiaDrawingEngine::FillRect(BRect r, const rgb_color& color)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::FillRect(r, color);
		return;
	}
	make_rect_valid(r);
	r = fSkia->ClipRect(r);
	Draw draw(this, r);
	if (!draw.IsDirty())
		return;
	fSkia->FillRect(r, color);
}


void
SkiaDrawingEngine::FillRegion(BRegion& r, const rgb_color& color)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::FillRegion(r, color);
		return;
	}
	BRect frame = r.Frame();
	if (!fSkia->Bounds().Contains(frame))
		return;
	Draw draw(this, frame);
	if (!fTarget->HasSurface())
		return;
	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fSkia->FillRectNoClipping(r.RectAtInt(i), color);
}


void
SkiaDrawingEngine::StrokeRect(BRect r)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::StrokeRect(r);
		return;
	}
	make_rect_valid(r);
	BRect clipped(r);
	extend_by_stroke_width(clipped, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(clipped));
	if (!draw.IsDirty())
		return;
	fSkia->StrokeRect(r, NULL);
}


void
SkiaDrawingEngine::FillRect(BRect r)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::FillRect(r);
		return;
	}
	make_rect_valid(r);
	r = fSkia->AlignRect(r);
	Draw draw(this, fSkia->TransformAndClipRect(r));
	if (!draw.IsDirty())
		return;
	fSkia->FillRect(r, NULL);
}


void
SkiaDrawingEngine::StrokeRect(BRect r, const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::StrokeRect(r, gradient);
		return;
	}
	make_rect_valid(r);
	BRect clipped(r);
	extend_by_stroke_width(clipped, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(clipped));
	if (!draw.IsDirty())
		return;
	fSkia->StrokeRect(r, &gradient);
}


void
SkiaDrawingEngine::FillRect(BRect r, const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::FillRect(r, gradient);
		return;
	}
	make_rect_valid(r);
	r = fSkia->AlignRect(r);
	Draw draw(this, fSkia->TransformAndClipRect(r));
	if (!draw.IsDirty())
		return;
	fSkia->FillRect(r, &gradient);
}


void
SkiaDrawingEngine::FillRegion(BRegion& r)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::FillRegion(r);
		return;
	}
	BRect clipped = fSkia->TransformAndClipRect(r.Frame());
	Draw draw(this, clipped);
	if (!draw.IsDirty())
		return;
	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fSkia->FillRect(r.RectAt(i), NULL);
}


void
SkiaDrawingEngine::FillRegion(BRegion& r, const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::FillRegion(r, gradient);
		return;
	}
	BRect clipped = fSkia->TransformAndClipRect(r.Frame());
	Draw draw(this, clipped);
	if (!draw.IsDirty())
		return;
	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fSkia->FillRect(r.RectAt(i), &gradient);
}


void
SkiaDrawingEngine::DrawRoundRect(BRect r, float xrad, float yrad,
	bool filled)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawRoundRect(r, xrad, yrad, filled);
		return;
	}
	make_rect_valid(r);
	BRect bounds(r);
	if (!filled)
		extend_by_stroke_width(bounds, fSkia->PenSize());
	BRect clipped = fSkia->TransformAndClipRect(bounds);
	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);
	Draw draw(this, clipped);
	if (!draw.IsDirty())
		return;
	fSkia->DrawRoundRect(r, xrad, yrad, filled, NULL);
}


void
SkiaDrawingEngine::DrawRoundRect(BRect r, float xrad, float yrad,
	bool filled, const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawRoundRect(r, xrad, yrad, filled, gradient);
		return;
	}
	make_rect_valid(r);
	BRect bounds(r);
	if (!filled)
		extend_by_stroke_width(bounds, fSkia->PenSize());
	BRect clipped = fSkia->TransformAndClipRect(bounds);
	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);
	Draw draw(this, clipped);
	if (!draw.IsDirty())
		return;
	fSkia->DrawRoundRect(r, xrad, yrad, filled, &gradient);
}


void
SkiaDrawingEngine::DrawShape(const BRect& bounds, int32 opcount,
	const uint32* oplist, int32 ptcount, const BPoint* ptlist, bool filled,
	const BPoint& viewToScreenOffset, float viewScale)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawShape(bounds, opcount, oplist, ptcount, ptlist,
			filled, viewToScreenOffset, viewScale);
		return;
	}
	Draw draw(this);
	if (draw.IsDirty()) {
		fSkia->DrawShape(opcount, oplist, ptcount, ptlist, filled,
			viewToScreenOffset, viewScale, NULL);
	}
}


void
SkiaDrawingEngine::DrawShape(const BRect& bounds, int32 opcount,
	const uint32* oplist, int32 ptcount, const BPoint* ptlist, bool filled,
	const BGradient& gradient, const BPoint& viewToScreenOffset,
	float viewScale)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawShape(bounds, opcount, oplist, ptcount, ptlist,
			filled, gradient, viewToScreenOffset, viewScale);
		return;
	}
	Draw draw(this);
	if (draw.IsDirty()) {
		fSkia->DrawShape(opcount, oplist, ptcount, ptlist, filled,
			viewToScreenOffset, viewScale, &gradient);
	}
}


void
SkiaDrawingEngine::DrawTriangle(BPoint* pts, const BRect& bounds,
	bool filled)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawTriangle(pts, bounds, filled);
		return;
	}
	BRect clipped(bounds);
	if (!filled)
		extend_by_stroke_width(clipped, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(clipped));
	if (!draw.IsDirty())
		return;
	fSkia->DrawTriangle(pts[0], pts[1], pts[2], filled, NULL);
}


void
SkiaDrawingEngine::DrawTriangle(BPoint* pts, const BRect& bounds,
	bool filled, const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::DrawTriangle(pts, bounds, filled, gradient);
		return;
	}
	BRect clipped(bounds);
	if (!filled)
		extend_by_stroke_width(clipped, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(clipped));
	if (!draw.IsDirty())
		return;
	fSkia->DrawTriangle(pts[0], pts[1], pts[2], filled, &gradient);
}


void
SkiaDrawingEngine::StrokeLine(const BPoint& start, const BPoint& end)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::StrokeLine(start, end);
		return;
	}
	BRect touched(start, end);
	make_rect_valid(touched);
	extend_by_stroke_width(touched, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(touched));
	if (!draw.IsDirty())
		return;
	fSkia->StrokeLine(start, end);
}


void
SkiaDrawingEngine::StrokeLine(const BPoint& start, const BPoint& end,
	const BGradient& gradient)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::StrokeLine(start, end, gradient);
		return;
	}
	BRect touched(start, end);
	make_rect_valid(touched);
	extend_by_stroke_width(touched, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(touched));
	if (!draw.IsDirty())
		return;
	fSkia->StrokeLine(start, end, gradient);
}


void
SkiaDrawingEngine::StrokeLineArray(int32 numLines,
	const ViewLineArrayInfo* lineData)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		DrawingEngine::StrokeLineArray(numLines, lineData);
		return;
	}
	if (lineData == NULL || numLines <= 0)
		return;
	const ViewLineArrayInfo* data = &lineData[0];
	BRect touched(min_c(data->startPoint.x, data->endPoint.x),
		min_c(data->startPoint.y, data->endPoint.y),
		max_c(data->startPoint.x, data->endPoint.x),
		max_c(data->startPoint.y, data->endPoint.y));
	for (int32 i = 1; i < numLines; i++) {
		data = &lineData[i];
		BRect box(min_c(data->startPoint.x, data->endPoint.x),
			min_c(data->startPoint.y, data->endPoint.y),
			max_c(data->startPoint.x, data->endPoint.x),
			max_c(data->startPoint.y, data->endPoint.y));
		touched = touched | box;
	}
	extend_by_stroke_width(touched, fSkia->PenSize());
	Draw draw(this, fSkia->TransformAndClipRect(touched));
	if (!draw.IsDirty())
		return;

	rgb_color oldColor = fSkia->HighColor();
	struct pattern pattern = fSkia->Pattern();
	fSkia->SetPattern(B_SOLID_HIGH);
	for (int32 i = 0; i < numLines; i++) {
		data = &lineData[i];
		fSkia->SetHighColor(data->color);
		fSkia->StrokeLine(data->startPoint, data->endPoint);
	}
	fSkia->SetHighColor(oldColor);
	fSkia->SetPattern(pattern);
}


BPoint
SkiaDrawingEngine::DrawString(const char* string, int32 length,
	const BPoint& pt, escapement_delta* delta)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		return DrawingEngine::DrawString(string, length, pt, delta);
	}
	// The pen position stays ServerFont's (decision 5). M13 step 1: for
	// unrotated text without a view transform, DrawStringDry() is the
	// string's width, the x of the same GlyphLayoutEngine pass that the
	// drawing does (font, delta and spacing alike), so a drawn string is
	// laid out once. Otherwise, and when nothing is drawn, as before.
	bool fast = fSkia->Font().Rotation() == 0.0f
		&& fSkia->IsIdentityTransform();
	double advance = HUGE_VAL;
	{
		Draw draw(this);
		if (draw.IsDirty()) {
			fSkia->DrawString(string, length, pt, delta, NULL,
				fast ? &advance : NULL);
		}
	}
	if (fast && advance != HUGE_VAL)
		return BPoint(pt.x + (float)advance, pt.y);
	return DrawStringDry(string, length, pt, delta);
}


BPoint
SkiaDrawingEngine::DrawString(const char* string, int32 length,
	const BPoint* offsets)
{
	if (!_Gpu()) {
		CpuTimer cpuTimer(this);
		return DrawingEngine::DrawString(string, length, offsets);
	}
	BPoint penLocation = DrawStringDry(string, length, offsets);
	Draw draw(this);
	if (draw.IsDirty())
		fSkia->DrawString(string, length, BPoint(), NULL, offsets);
	return penLocation;
}
