/*
 * Copyright 2005-2013, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */


#include "VirtualScreen.h"

#include "HWInterface.h"
#include "Desktop.h"

#include <new>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <Region.h>
// SPDX-SnippetEnd


VirtualScreen::VirtualScreen()
	:
	fScreenList(4),
	fDrawingEngine(NULL),
	fHWInterface(NULL)
{
}


VirtualScreen::~VirtualScreen()
{
	_Reset();
}


void
VirtualScreen::_Reset()
{
	ScreenList list;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		list.AddItem(item->screen);
	}

	gScreenManager->ReleaseScreens(list);
	fScreenList.MakeEmpty();

	fFrame.Set(0, 0, 0, 0);
	fDrawingEngine = NULL;
	fHWInterface = NULL;
}


status_t
VirtualScreen::SetConfiguration(Desktop& desktop, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations, uint32* _changedScreens)
{
	// Remember previous screen modes

	typedef std::map<Screen*, display_mode> ScreenModeMap;
	ScreenModeMap previousModes;
	bool previousModesFailed = false;

	if (_changedScreens != NULL) {
		*_changedScreens = 0;

		try {
			for (int32 i = 0; i < fScreenList.CountItems(); i++) {
				Screen* screen = fScreenList.ItemAt(i)->screen;

				display_mode mode;
				screen->GetMode(mode);

				previousModes.insert(std::make_pair(screen, mode));
			}
		} catch (...) {
			previousModesFailed = true;
			*_changedScreens = ~0L;
		}
	}

	_Reset();

	ScreenList list;
	status_t status = gScreenManager->AcquireScreens(&desktop, NULL, 0,
		desktop.TargetScreen(), false, list);
	if (status != B_OK) {
		// TODO: we would try again here with force == true
		return status;
	}

	for (int32 i = 0; i < list.CountItems(); i++) {
		Screen* screen = list.ItemAt(i);

		AddScreen(screen, configurations, currentConfigurations);

		if (!previousModesFailed && _changedScreens != NULL) {
			// Figure out which screens have changed their mode
			display_mode mode;
			screen->GetMode(mode);

			ScreenModeMap::const_iterator found = previousModes.find(screen);
			if (found != previousModes.end()
				&& memcmp(&mode, &found->second, sizeof(display_mode)))
				*_changedScreens |= 1 << i;
		}
	}

	UpdateFrame();
	return B_OK;
}


status_t
VirtualScreen::AddScreen(Screen* screen, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations)
{
	screen_item* item = new(std::nothrow) screen_item;
	if (item == NULL)
		return B_NO_MEMORY;

	item->screen = screen;

	status_t status = B_ERROR;
	display_mode mode;
	if (_GetMode(screen, configurations, mode) == B_OK) {
		// we found settings for this screen, and try to apply them now
		status = screen->SetMode(mode);
	}

	if (status != B_OK) {
		// We found no configuration or it wasn't valid, try to fallback to
		// sane values
		status = screen->SetPreferredMode();
		if (status == B_OK) {
			screen->GetMode(mode);
		}
		if (status != B_OK)
			status = screen->SetBestMode(1024, 768, B_RGB32, 60.f);
		if (status != B_OK)
			status = screen->SetBestMode(800, 600, B_RGB32, 60.f, false);
		if (status != B_OK) {
			debug_printf("app_server: Failed to set mode: %s\n",
				strerror(status));

			delete item;
			return status;
		}
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Step 4 (docs/DISPLAY-DCN.md): the first screen is the main screen at
	// (0, 0) and owns the drawing engine and the drawing buffer of the whole
	// desktop. Every further screen sits right of the one before, top
	// aligned. UpdateFrame() hands them to the main screen's interface.
	BRect frame = screen->Frame();
	if (fScreenList.CountItems() == 0) {
		fDrawingEngine = screen->GetDrawingEngine();
		fHWInterface = screen->HWInterface();
	} else {
		float right = 0;
		for (int32 i = 0; i < fScreenList.CountItems(); i++)
			right = max_c(right, fScreenList.ItemAt(i)->frame.right + 1);
		frame.OffsetTo(right, 0);
	}
	item->frame = frame;

	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;
	currentConfigurations.Set(screen->ID(), hasInfo ? &info : NULL, frame,
		mode);
	fFrame = fFrame.IsValid() && fScreenList.CountItems() > 0
		? (fFrame | frame) : frame;
// SPDX-SnippetEnd

	fScreenList.AddItem(item);

	return B_OK;
}


status_t
VirtualScreen::RemoveScreen(Screen* screen)
{
	// not implemented yet (config changes when running)
	return B_ERROR;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
VirtualScreen::SetOffscreen(::HWInterface* interface, ::DrawingEngine* engine,
	BRect frame)
{
	_Reset();
	fHWInterface = interface;
	fDrawingEngine = engine;
	fFrame = frame;
}
// SPDX-SnippetEnd


void
VirtualScreen::UpdateFrame()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Step 4: the desktop is the union of the screens at their places, as
	// the TODO here asked; the main screen's interface gets the others.
	BRect frame;
	HWInterface::r5_output outputs[HWInterface::kR5MaxOutputs];
	int32 outputCount = 0;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		uint16 width, height;
		uint32 colorSpace;
		float frequency;
		item->screen->GetMode(width, height, colorSpace, frequency);
		item->frame.right = item->frame.left + width - 1;
		item->frame.bottom = item->frame.top + height - 1;
		frame = i == 0 ? item->frame : (frame | item->frame);
		if (i > 0 && outputCount < HWInterface::kR5MaxOutputs
			&& item->screen->HWInterface() != NULL
			&& item->screen->HWInterface()->FrontBuffer() != NULL) {
			outputs[outputCount].front
				= item->screen->HWInterface()->FrontBuffer();
			outputs[outputCount].left = (int32)item->frame.left;
			outputs[outputCount].top = (int32)item->frame.top;
			outputCount++;
		}
	}
	fFrame = frame;
	if (fHWInterface != NULL && fScreenList.CountItems() > 0) {
		fHWInterface->R5SetOutputs(outputs, outputCount,
			frame.IntegerWidth() + 1, frame.IntegerHeight() + 1);
	}
// SPDX-SnippetEnd
}


/*!	Returns the smallest frame that spans over all screens
*/
BRect
VirtualScreen::Frame() const
{
	return fFrame;
}


Screen*
VirtualScreen::ScreenAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen;

	return NULL;
}


Screen*
VirtualScreen::ScreenByID(int32 id) const
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Step 4: the exact id; B_MAIN_SCREEN_ID is the first screen. With one
	// screen this is what the loop here returned.
	return ScreenByExactID(id);
// SPDX-SnippetEnd
}


BRect
VirtualScreen::ScreenFrameAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->frame;

	return BRect(0, 0, 0, 0);
}


int32
VirtualScreen::CountScreens() const
{
	return fScreenList.CountItems();
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
Screen*
VirtualScreen::ScreenByExactID(int32 id) const
{
	if (id == B_MAIN_SCREEN_ID.id)
		return ScreenAt(0);
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		if (fScreenList.ItemAt(i)->screen->ID() == id)
			return fScreenList.ItemAt(i)->screen;
	}
	return NULL;
}


bool
VirtualScreen::ScreenFrameByID(int32 id, BRect& frame) const
{
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen->ID() == id || (i == 0 && id == B_MAIN_SCREEN_ID.id)) {
			frame = item->frame;
			return true;
		}
	}
	return false;
}


bool
VirtualScreen::NextScreenID(int32 id, int32& next) const
{
	for (int32 i = 0; i + 1 < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen->ID() == id || (i == 0 && id == B_MAIN_SCREEN_ID.id)) {
			next = fScreenList.ItemAt(i + 1)->screen->ID();
			return true;
		}
	}
	return false;
}


void
VirtualScreen::GetScreenRegion(BRegion& region) const
{
	region.MakeEmpty();
	for (int32 i = 0; i < fScreenList.CountItems(); i++)
		region.Include(fScreenList.ItemAt(i)->frame);
}


Screen*
VirtualScreen::ScreenFor(BRect frame) const
{
	// The screen that holds most of the frame; the main screen otherwise.
	Screen* best = ScreenAt(0);
	float bestArea = 0;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		BRect part = item->frame & frame;
		if (!part.IsValid())
			continue;
		float area = (part.Width() + 1) * (part.Height() + 1);
		if (area > bestArea) {
			bestArea = area;
			best = item->screen;
		}
	}
	return best;
}
// SPDX-SnippetEnd


status_t
VirtualScreen::_GetMode(Screen* screen, const ScreenConfigurations& configurations,
	display_mode& mode) const
{
	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;

	screen_configuration* configuration = configurations.BestFit(screen->ID(),
		hasInfo ? &info : NULL);
	if (configuration == NULL)
		return B_NAME_NOT_FOUND;

	mode = configuration->mode;
	return B_OK;
}
