/*
 * Copyright 2005-2008, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Author:
 *		Stephan Aßmus <superstippi@gmx.de>
 */


#include "OffscreenWindow.h"

#include <new>
#include <stdio.h>

#include <Debug.h>

#include <WindowPrivate.h>

#include "BitmapHWInterface.h"
#include "DrawingEngine.h"
#include "ServerBitmap.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "SkiaOffscreen.h"


// M12 (docs/APP-SERVER-GPU-DRAWING.md, part 6): in the GPU mode the view
// bitmap is drawn by Skia on an image of its own; its memory is brought up
// to date at the read points (Sync(), app_server reading it).
static DrawingEngine*
offscreen_engine(ServerBitmap* bitmap)
{
	DrawingEngine* engine = skia_offscreen_engine(NULL, bitmap,
		IntRect(0, 0, bitmap->Width() - 1, bitmap->Height() - 1), true);
	if (engine != NULL)
		return engine;
	return new (std::nothrow) DrawingEngine();
}
// SPDX-SnippetEnd

using std::nothrow;


OffscreenWindow::OffscreenWindow(ServerBitmap* bitmap,
		const char* name, ::ServerWindow* window)
	: Window(bitmap->Bounds(), name,
			B_NO_BORDER_WINDOW_LOOK, kOffscreenWindowFeel,
			0, 0, window, offscreen_engine(bitmap)),
	fBitmap(bitmap),
	fHWInterface(new (nothrow) BitmapHWInterface(fBitmap))
{
	if (!fHWInterface.IsSet() || !GetDrawingEngine())
		return;

	fHWInterface->Initialize();
	GetDrawingEngine()->SetHWInterface(fHWInterface.Get());

	fVisibleRegion.Set(fFrame);
	fVisibleContentRegion.Set(fFrame);
	fVisibleContentRegionValid = true;
	fContentRegion.Set(fFrame);
	fContentRegionValid = true;
}


OffscreenWindow::~OffscreenWindow()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// M12 (part 6, point 3): nothing stays pending when the view goes
	if (GetDrawingEngine())
		skia_offscreen_finish(GetDrawingEngine());
// SPDX-SnippetEnd
	if (GetDrawingEngine())
		GetDrawingEngine()->SetHWInterface(NULL);

	if (fHWInterface.IsSet()) {
		fHWInterface->LockExclusiveAccess();
		fHWInterface->Shutdown();
		fHWInterface->UnlockExclusiveAccess();
	}
}

