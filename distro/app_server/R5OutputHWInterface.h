/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * A second (third, ...) screen of the desktop (docs/DISPLAY-DCN.md, step 4).
 *
 * It is a HWInterface so that ScreenManager, Screen and VirtualScreen treat
 * it as Haiku intends a screen to be treated: its own id, mode, frame and
 * monitor info. It does not draw by itself. The main screen's interface owns
 * the one drawing buffer of the whole desktop and copies into this
 * interface's front buffer (HWInterface::R5SetOutputs()).
 *
 * The kinds of output:
 * - the test output (`multi_screen_test_output <w>x<h>`): its front buffer
 *   is memory; it writes a PPM of what it shows to
 *   /tmp/r5-screen-<id>.ppm, so a QEMU run can check it;
 * - later, a display head of the GPU (DCN step 3), through the accelerant.
 */
#ifndef R5_OUTPUT_HW_INTERFACE_H
#define R5_OUTPUT_HW_INTERFACE_H


#include "HWInterface.h"


class MallocBuffer;


class R5OutputHWInterface : public HWInterface {
public:
								R5OutputHWInterface(uint32 width,
									uint32 height, const char* name);
	virtual						~R5OutputHWInterface();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();

	virtual	status_t			SetMode(const display_mode& mode);
	virtual	void				GetMode(display_mode* mode);

	virtual status_t			GetDeviceInfo(accelerant_device_info* info);
	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	virtual status_t			GetTimingConstraints(
									display_timing_constraints* constraints);
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);
	virtual status_t			GetPreferredMode(display_mode* mode);
	virtual status_t			GetMonitorInfo(monitor_info* info);

	virtual sem_id				RetraceSemaphore();
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	virtual status_t			SetDPMSMode(uint32 state);
	virtual uint32				DPMSMode();
	virtual uint32				DPMSCapabilities();

	virtual status_t			SetBrightness(float brightness);
	virtual status_t			GetBrightness(float* brightness);

	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

			void				SetScreenID(int32 id) { fScreenID = id; }

private:
	static	status_t			_DumpThread(void* data);
			void				_Dump();

			MallocBuffer*		fFront;
			display_mode		fMode;
			char				fName[48];
			int32				fScreenID;
			thread_id			fDumpThread;
			sem_id				fQuitSem;
};


// The step 4 switches, from ~/config/settings/kernel/drivers/radeon_hd (the
// file the compositor and boot-next use).
bool r5_multi_screen_enabled();
bool r5_multi_screen_test_output(uint32* width, uint32* height);


#endif	// R5_OUTPUT_HW_INTERFACE_H
