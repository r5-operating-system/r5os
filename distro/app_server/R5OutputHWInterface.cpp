/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * A second screen of the desktop (docs/DISPLAY-DCN.md, step 4). See the
 * header. The mode helpers follow Haiku's own: the display_mode fields as
 * AccelerantHWInterface fills them, monitor_info as edid_decode() fills it.
 */


#include "R5OutputHWInterface.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <FindDirectory.h>

#include "MallocBuffer.h"


static const char* kSettingsFile = "/kernel/drivers/radeon_hd";
static const uint32 kMinSize = 320;
static const uint32 kMaxSize = 8192;


static bool
r5_settings_line(const char* prefix, char* rest, size_t restSize)
{
	char path[B_PATH_NAME_LENGTH];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, path,
			sizeof(path)) != B_OK
		|| strlcat(path, kSettingsFile, sizeof(path)) >= sizeof(path))
		return false;
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;
	char line[256];
	bool found = false;
	size_t length = strlen(prefix);
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, prefix, length) == 0
			&& (line[length] == ' ' || line[length] == '\t')) {
			strlcpy(rest, line + length + 1, restSize);
			found = true;
		}
	}
	fclose(file);
	return found;
}


bool
r5_multi_screen_enabled()
{
	char value[32];
	if (!r5_settings_line("multi_screen", value, sizeof(value)))
		return false;
	return strncmp(value, "true", 4) == 0;
}


bool
r5_multi_screen_test_output(uint32* width, uint32* height)
{
	char value[64];
	if (!r5_settings_line("multi_screen_test_output", value, sizeof(value)))
		return false;
	unsigned w = 0;
	unsigned h = 0;
	if (sscanf(value, "%ux%u", &w, &h) != 2 || w < kMinSize || h < kMinSize
		|| w > kMaxSize || h > kMaxSize) {
		syslog(LOG_INFO, "app_server: multi_screen_test_output %s is not"
			" <width>x<height> between %u and %u\n", value, kMinSize, kMaxSize);
		return false;
	}
	*width = w;
	*height = h;
	return true;
}


// #pragma mark -


R5OutputHWInterface::R5OutputHWInterface(uint32 width, uint32 height,
	const char* name)
	:
	HWInterface(),
	fFront(NULL),
	fScreenID(-1),
	fDumpThread(-1),
	fQuitSem(-1)
{
	memset(&fMode, 0, sizeof(fMode));
	fMode.virtual_width = width;
	fMode.virtual_height = height;
	fMode.space = B_RGB32;
	fMode.timing.h_display = width;
	fMode.timing.v_display = height;
	// A 60 Hz frame for anyone who computes the refresh rate.
	fMode.timing.h_total = width + 160;
	fMode.timing.v_total = height + 45;
	fMode.timing.pixel_clock = (uint32)((uint64)fMode.timing.h_total
		* fMode.timing.v_total * 60 / 1000);
	fMode.timing.h_sync_start = width + 48;
	fMode.timing.h_sync_end = width + 80;
	fMode.timing.v_sync_start = height + 3;
	fMode.timing.v_sync_end = height + 8;
	strlcpy(fName, name, sizeof(fName));
}


R5OutputHWInterface::~R5OutputHWInterface()
{
	Shutdown();
	delete fFront;
}


status_t
R5OutputHWInterface::Initialize()
{
	status_t status = HWInterface::Initialize();
	if (status != B_OK)
		return status;
	fFront = new(std::nothrow) MallocBuffer(fMode.virtual_width,
		fMode.virtual_height);
	if (fFront == NULL || fFront->InitCheck() != B_OK)
		return B_NO_MEMORY;
	memset(fFront->Bits(), 0, fFront->BitsLength());

	fQuitSem = create_sem(0, "r5 output dump");
	if (fQuitSem >= 0) {
		fDumpThread = spawn_thread(&_DumpThread, "r5 output dump",
			B_LOW_PRIORITY, this);
		if (fDumpThread >= 0)
			resume_thread(fDumpThread);
	}
	syslog(LOG_INFO, "app_server: screen output \"%s\" %" B_PRIu32 "x%"
		B_PRIu32 "\n", fName, (uint32)fMode.virtual_width,
		(uint32)fMode.virtual_height);
	return B_OK;
}


status_t
R5OutputHWInterface::Shutdown()
{
	if (fQuitSem >= 0) {
		delete_sem(fQuitSem);
		fQuitSem = -1;
		if (fDumpThread >= 0) {
			status_t result;
			wait_for_thread(fDumpThread, &result);
			fDumpThread = -1;
		}
	}
	return B_OK;
}


status_t
R5OutputHWInterface::SetMode(const display_mode& mode)
{
	// The test output has one mode, the size it was made with.
	if (mode.virtual_width != fMode.virtual_width
		|| mode.virtual_height != fMode.virtual_height)
		return B_BAD_VALUE;
	_NotifyFrameBufferChanged();
	return B_OK;
}


void
R5OutputHWInterface::GetMode(display_mode* mode)
{
	if (mode != NULL)
		*mode = fMode;
}


status_t
R5OutputHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	info->version = B_ACCELERANT_VERSION;
	strlcpy(info->name, fName, sizeof(info->name));
	strlcpy(info->chipset, "R5 output", sizeof(info->chipset));
	strlcpy(info->serial_no, "0", sizeof(info->serial_no));
	info->memory = fMode.virtual_width * fMode.virtual_height * 4;
	return B_OK;
}


status_t
R5OutputHWInterface::GetModeList(display_mode** _modeList, uint32* _count)
{
	if (_modeList == NULL || _count == NULL)
		return B_BAD_VALUE;
	// Callers free() the list.
	display_mode* list = (display_mode*)malloc(sizeof(display_mode));
	if (list == NULL)
		return B_NO_MEMORY;
	*list = fMode;
	*_modeList = list;
	*_count = 1;
	return B_OK;
}


status_t
R5OutputHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low,
	uint32* _high)
{
	if (_low == NULL || _high == NULL)
		return B_BAD_VALUE;
	*_low = fMode.timing.pixel_clock;
	*_high = fMode.timing.pixel_clock;
	return B_OK;
}


status_t
R5OutputHWInterface::GetTimingConstraints(
	display_timing_constraints* constraints)
{
	return B_NOT_SUPPORTED;
}


status_t
R5OutputHWInterface::ProposeMode(display_mode* candidate,
	const display_mode* low, const display_mode* high)
{
	if (candidate == NULL)
		return B_BAD_VALUE;
	*candidate = fMode;
	return B_OK;
}


status_t
R5OutputHWInterface::GetPreferredMode(display_mode* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;
	*mode = fMode;
	return B_OK;
}


status_t
R5OutputHWInterface::GetMonitorInfo(monitor_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	strlcpy(info->vendor, "R5", sizeof(info->vendor));
	strlcpy(info->name, fName, sizeof(info->name));
	strlcpy(info->serial_number, "1", sizeof(info->serial_number));
	return B_OK;
}


sem_id
R5OutputHWInterface::RetraceSemaphore()
{
	return B_NOT_SUPPORTED;
}


status_t
R5OutputHWInterface::WaitForRetrace(bigtime_t timeout)
{
	return B_NOT_SUPPORTED;
}


status_t
R5OutputHWInterface::SetDPMSMode(uint32 state)
{
	return B_NOT_SUPPORTED;
}


uint32
R5OutputHWInterface::DPMSMode()
{
	return B_DPMS_ON;
}


uint32
R5OutputHWInterface::DPMSCapabilities()
{
	return 0;
}


status_t
R5OutputHWInterface::SetBrightness(float brightness)
{
	return B_NOT_SUPPORTED;
}


status_t
R5OutputHWInterface::GetBrightness(float* brightness)
{
	return B_NOT_SUPPORTED;
}


RenderingBuffer*
R5OutputHWInterface::FrontBuffer() const
{
	return fFront;
}


RenderingBuffer*
R5OutputHWInterface::BackBuffer() const
{
	return NULL;
}


bool
R5OutputHWInterface::IsDoubleBuffered() const
{
	return false;
}


// #pragma mark - the test dump


status_t
R5OutputHWInterface::_DumpThread(void* data)
{
	R5OutputHWInterface* self = (R5OutputHWInterface*)data;
	// 20 s, 40 s, 60 s after the start, then every minute: enough to see
	// the first desktop and later changes, little disk work.
	bigtime_t delay = 20000000;
	int32 dumps = 0;
	while (acquire_sem_etc(self->fQuitSem, 1, B_RELATIVE_TIMEOUT, delay)
			== B_TIMED_OUT) {
		self->_Dump();
		if (++dumps >= 3)
			delay = 60000000;
	}
	return B_OK;
}


void
R5OutputHWInterface::_Dump()
{
	if (fFront == NULL)
		return;
	char path[64];
	snprintf(path, sizeof(path), "/tmp/r5-screen-%" B_PRId32 ".ppm",
		fScreenID);
	char temporary[72];
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	FILE* file = fopen(temporary, "wb");
	if (file == NULL)
		return;
	uint32 width = fFront->Width();
	uint32 height = fFront->Height();
	fprintf(file, "P6\n%" B_PRIu32 " %" B_PRIu32 "\n255\n", width, height);
	uint8* row = new(std::nothrow) uint8[width * 3];
	bool ok = row != NULL;
	// A torn frame is fine for a test picture; the copy takes no lock.
	for (uint32 y = 0; ok && y < height; y++) {
		const uint8* src = (const uint8*)fFront->Bits()
			+ y * fFront->BytesPerRow();
		for (uint32 x = 0; x < width; x++) {
			row[x * 3 + 0] = src[x * 4 + 2];
			row[x * 3 + 1] = src[x * 4 + 1];
			row[x * 3 + 2] = src[x * 4 + 0];
		}
		ok = fwrite(row, 1, width * 3, file) == width * 3;
	}
	delete[] row;
	fclose(file);
	if (ok)
		rename(temporary, path);
	else
		unlink(temporary);
}
