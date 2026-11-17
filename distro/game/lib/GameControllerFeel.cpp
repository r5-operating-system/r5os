/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Rumble, trigger effects and motion (docs/GAME-CONTROLLERS.md, option C
 * phase 1): PlayStation controllers through their own output reports,
 * every other controller with HID PID through its PID reports, both sent
 * with R5_JOYSTICK_SEND_REPORT. The report bytes are built in
 * GameControllerReports.cpp.
 */


#include "GameControllerFeel.h"

#include <errno.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <Autolock.h>

#include <joystick_driver.h>


namespace R5GameController {


static_assert((int)B_TRIGGER_EFFECT_OFF == (int)TRIGGER_EFFECT_OFF
	&& (int)B_TRIGGER_EFFECT_RESISTANCE == (int)TRIGGER_EFFECT_RESISTANCE
	&& (int)B_TRIGGER_EFFECT_CLICK == (int)TRIGGER_EFFECT_CLICK
	&& (int)B_TRIGGER_EFFECT_VIBRATION == (int)TRIGGER_EFFECT_VIBRATION,
	"trigger effect numbers");


static float
clamp_unit(float value)
{
	if (!(value > 0))
		return 0;
	return value > 1 ? 1 : value;
}


ControllerFeel::ControllerFeel()
	:
	fLock("game controller feel"),
	fFD(-1),
	fModel(PLAYSTATION_NONE),
	fBluetooth(false),
	fEffectsSent(false),
	fLayout(NULL),
	fPIDReady(false),
	fPIDBlock(1),
	fStopThread(-1),
	fStopSem(-1),
	fStopAt(0),
	fQuit(false),
	fMotionEnabled(false),
	fHasMotion(false)
{
	playstation_effects_init(fEffects);
	memset(&fPID, 0, sizeof(fPID));
	memset(&fMotion, 0, sizeof(fMotion));
}


ControllerFeel::~ControllerFeel()
{
	Close();
}


void
ControllerFeel::Open(int fd, uint16 vendor, uint16 product, uint16 bus)
{
	Close();

	BAutolock locker(fLock);
	fFD = fd;
	if (fd < 0)
		return;
	fBluetooth = bus == R5_JOYSTICK_BUS_BLUETOOTH;
	fModel = playstation_model_for(vendor, product);
	if (bus != R5_JOYSTICK_BUS_USB && bus != R5_JOYSTICK_BUS_BLUETOOTH)
		fModel = PLAYSTATION_NONE;
	playstation_effects_init(fEffects);

	if (fModel == PLAYSTATION_DUALSENSE && !fBluetooth) {
		// The DualSense takes the improved rumble from firmware 2.24 on;
		// its firmware report (feature 0x20) has the version in bytes 44
		// and 45. Over Bluetooth, and on the Edge, assume a newer one.
		uint8 info[64];
		size_t length = 0;
		bigtime_t when;
		if (_GetReport(R5_JOYSTICK_REPORT_FEATURE, 0x20, info, sizeof(info),
				&length, &when) == B_OK && length >= 46) {
			uint16 version = info[44] | (info[45] << 8);
			fEffects.improvedRumble = version == 0 || version >= 0x0224;
		}
	}

	if (fModel != PLAYSTATION_NONE)
		return;

	// Any other controller: HID PID force feedback, when its report
	// descriptor has the PID page.
	uint8* descriptor = (uint8*)malloc(R5_JOYSTICK_MAX_DESCRIPTOR);
	fLayout = new(std::nothrow) hid_layout;
	if (descriptor != NULL && fLayout != NULL) {
		r5_joystick_descriptor request;
		memset(&request, 0, sizeof(request));
		request.length = R5_JOYSTICK_MAX_DESCRIPTOR;
		request.data = descriptor;
		if (ioctl(fd, R5_JOYSTICK_GET_DESCRIPTOR, &request,
				sizeof(request)) != 0
			|| !parse_hid_descriptor(descriptor, request.length, *fLayout)
			|| !pid_find(*fLayout, fPID)) {
			memset(&fPID, 0, sizeof(fPID));
		}
	}
	free(descriptor);
	if (!fPID.usable) {
		delete fLayout;
		fLayout = NULL;
	}
}


void
ControllerFeel::Close()
{
	thread_id thread;
	{
		BAutolock locker(fLock);
		if (fFD >= 0) {
			// Nothing keeps rumbling or pushing back after the program:
			// on a PlayStation controller one report with the motors off
			// and both triggers free.
			fStopAt = 0;
			if (fModel != PLAYSTATION_NONE) {
				if (fEffectsSent) {
					fEffects.strong = 0;
					fEffects.weak = 0;
					dualsense_trigger_effect(TRIGGER_EFFECT_OFF, 0, 0,
						fEffects.leftTrigger);
					dualsense_trigger_effect(TRIGGER_EFFECT_OFF, 0, 0,
						fEffects.rightTrigger);
					_SendPlayStation();
				}
			} else
				_StopLocked();
		}
		fQuit = true;
		thread = fStopThread;
		if (fStopSem >= 0)
			release_sem(fStopSem);
	}
	if (thread >= 0) {
		status_t result;
		wait_for_thread(thread, &result);
	}

	BAutolock locker(fLock);
	if (fStopSem >= 0)
		delete_sem(fStopSem);
	fStopSem = -1;
	fStopThread = -1;
	fQuit = false;
	fFD = -1;
	fModel = PLAYSTATION_NONE;
	fBluetooth = false;
	fEffectsSent = false;
	delete fLayout;
	fLayout = NULL;
	memset(&fPID, 0, sizeof(fPID));
	fPIDReady = false;
	fPIDBlock = 1;
	fMotionEnabled = false;
	fHasMotion = false;
	memset(&fMotion, 0, sizeof(fMotion));
}


bool
ControllerFeel::CanRumble() const
{
	BAutolock locker(fLock);
	return fFD >= 0 && (fModel != PLAYSTATION_NONE || fPID.usable);
}


bool
ControllerFeel::HasTriggerEffects() const
{
	BAutolock locker(fLock);
	return fFD >= 0 && (fModel == PLAYSTATION_DUALSENSE
		|| fModel == PLAYSTATION_DUALSENSE_EDGE);
}


bool
ControllerFeel::HasMotion() const
{
	BAutolock locker(fLock);
	return fFD >= 0 && fModel != PLAYSTATION_NONE;
}


status_t
ControllerFeel::SetRumble(float strong, float weak, bigtime_t duration)
{
	if (!CanRumble())
		return B_NOT_SUPPORTED;
	strong = clamp_unit(strong);
	weak = clamp_unit(weak);

	BAutolock locker(fLock);
	status_t status;
	if (fModel != PLAYSTATION_NONE) {
		fEffects.strong = (uint8)(strong * 255 + 0.5f);
		fEffects.weak = (uint8)(weak * 255 + 0.5f);
		status = _SendPlayStation();
	} else
		status = _PIDStart(strong, weak, duration);
	if (status != B_OK)
		return status;

	bool running = strong > 0 || weak > 0;
	_ScheduleStop(running ? duration : 0);
	return B_OK;
}


status_t
ControllerFeel::StopRumble()
{
	if (!CanRumble())
		return B_NOT_SUPPORTED;
	BAutolock locker(fLock);
	fStopAt = 0;
	return _StopLocked();
}


status_t
ControllerFeel::SetTriggerEffect(uint32 trigger, uint32 effect, float start,
	float strength)
{
	if (!HasTriggerEffects())
		return B_NOT_SUPPORTED;
	if (trigger > B_CONTROLLER_TRIGGER_RIGHT
		|| effect > B_TRIGGER_EFFECT_VIBRATION)
		return B_BAD_VALUE;

	BAutolock locker(fLock);
	dualsense_trigger_effect(effect, clamp_unit(start), clamp_unit(strength),
		trigger == B_CONTROLLER_TRIGGER_LEFT
			? fEffects.leftTrigger : fEffects.rightTrigger);
	return _SendPlayStation();
}


status_t
ControllerFeel::SetMotionEnabled(bool enabled)
{
	if (!HasMotion())
		return enabled ? B_NOT_SUPPORTED : B_OK;

	BAutolock locker(fLock);
	if (enabled && !fMotionEnabled && fBluetooth && !fEffectsSent) {
		// Over Bluetooth the sensors are only in the full report, which
		// the controller sends after its first output report.
		status_t status = _SendPlayStation();
		if (status != B_OK)
			return status;
	}
	fMotionEnabled = enabled;
	if (!enabled)
		fHasMotion = false;
	return B_OK;
}


bool
ControllerFeel::IsMotionEnabled() const
{
	BAutolock locker(fLock);
	return fMotionEnabled;
}


void
ControllerFeel::Update()
{
	BAutolock locker(fLock);
	if (!fMotionEnabled || fFD < 0)
		return;

	uint8 report[R5_JOYSTICK_CACHED_REPORT];
	size_t length = 0;
	bigtime_t when = 0;
	uint8 id = playstation_motion_report(fModel, fBluetooth);
	if (_GetReport(R5_JOYSTICK_REPORT_INPUT, id, report, sizeof(report),
			&length, &when) != B_OK)
		return;
	motion_sample sample;
	if (!parse_playstation_motion(fModel, fBluetooth, report, length,
			sample))
		return;
	fMotion.timestamp = when;
	for (int i = 0; i < 3; i++) {
		fMotion.acceleration[i] = sample.acceleration[i];
		fMotion.rotation[i] = sample.rotation[i];
	}
	fHasMotion = true;
}


status_t
ControllerFeel::GetMotion(game_controller_motion* motion) const
{
	if (motion == NULL)
		return B_BAD_VALUE;
	if (!HasMotion())
		return B_NOT_SUPPORTED;
	BAutolock locker(fLock);
	if (!fMotionEnabled)
		return B_NOT_ALLOWED;
	if (!fHasMotion)
		return B_ENTRY_NOT_FOUND;
	*motion = fMotion;
	return B_OK;
}


// #pragma mark - private (fLock held)


status_t
ControllerFeel::_SendPlayStation()
{
	uint8 report[kPlayStationBluetoothSize];
	size_t length;
	if (fModel == PLAYSTATION_DUALSHOCK4)
		length = build_dualshock4_output(fEffects, fBluetooth, report,
			sizeof(report));
	else
		length = build_dualsense_output(fEffects, fBluetooth, report,
			sizeof(report));
	if (length == 0)
		return B_ERROR;
	status_t status = _SendReport(R5_JOYSTICK_REPORT_OUTPUT, report[0],
		report, length);
	if (status == B_OK)
		fEffectsSent = true;
	return status;
}


status_t
ControllerFeel::_SendReport(uint8 type, uint8 id, const uint8* data,
	size_t length)
{
	r5_joystick_report request;
	memset(&request, 0, sizeof(request));
	request.type = type;
	request.id = id;
	request.length = (uint32)length;
	request.data = (void*)data;
	if (ioctl(fFD, R5_JOYSTICK_SEND_REPORT, &request, sizeof(request)) != 0)
		return errno != 0 ? errno : B_ERROR;
	return B_OK;
}


status_t
ControllerFeel::_GetReport(uint8 type, uint8 id, uint8* data,
	size_t capacity, size_t* length, bigtime_t* when)
{
	r5_joystick_report request;
	memset(&request, 0, sizeof(request));
	request.type = type;
	request.id = id;
	request.length = (uint32)capacity;
	request.data = data;
	if (ioctl(fFD, R5_JOYSTICK_GET_REPORT, &request, sizeof(request)) != 0)
		return errno != 0 ? errno : B_ERROR;
	*length = request.length < capacity ? request.length : capacity;
	*when = request.timestamp;
	return B_OK;
}


status_t
ControllerFeel::_PIDStart(float strong, float weak, bigtime_t duration)
{
	if (fLayout == NULL || !fPID.usable)
		return B_NOT_SUPPORTED;
	float magnitude = strong > weak ? strong : weak;
	if (magnitude <= 0)
		return _PIDStop();

	if (!fPIDReady) {
		// Reset, actuators on, full gain; then one effect block, from the
		// device when it manages its own pool (USB PID 1.0, 5.2).
		hid_report reports[3];
		uint32 count = pid_init_reports(*fLayout, fPID, reports, 3);
		for (uint32 i = 0; i < count; i++) {
			status_t status = _SendReport(reports[i].type, reports[i].id,
				reports[i].data, reports[i].length);
			if (status != B_OK)
				return status;
		}
		fPIDBlock = 1;
		hid_report create;
		if (pid_create_report(*fLayout, fPID, create)) {
			status_t status = _SendReport(create.type, create.id,
				create.data, create.length);
			if (status != B_OK)
				return status;
			if (fPID.blockLoad.present) {
				uint8 data[kMaxReportBytes];
				size_t length = 0;
				bigtime_t when;
				status = _GetReport(R5_JOYSTICK_REPORT_FEATURE,
					fPID.blockLoad.id, data, sizeof(data), &length, &when);
				if (status != B_OK)
					return status;
				if (!pid_parse_block_load(*fLayout, fPID, data, length,
						fPIDBlock))
					return B_NO_MEMORY;
						// Block Load Full or Error: no room for an effect
			}
		}
		fPIDReady = true;
	}

	// The heavy motor as a slow sine, the light one as a fast one.
	uint32 period = weak > strong ? 20 : 60;
	uint32 milliseconds = duration > 0 ? (uint32)((duration + 999) / 1000)
		: 0;
	hid_report reports[3];
	uint32 count = pid_start_reports(*fLayout, fPID, fPIDBlock, magnitude,
		milliseconds, period, reports, 3);
	if (count == 0)
		return B_ERROR;
	for (uint32 i = 0; i < count; i++) {
		status_t status = _SendReport(reports[i].type, reports[i].id,
			reports[i].data, reports[i].length);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


status_t
ControllerFeel::_PIDStop()
{
	if (fLayout == NULL || !fPIDReady)
		return B_OK;
	hid_report stop;
	if (!pid_stop_report(*fLayout, fPID, fPIDBlock, stop))
		return B_ERROR;
	return _SendReport(stop.type, stop.id, stop.data, stop.length);
}


status_t
ControllerFeel::_StopLocked()
{
	if (fModel != PLAYSTATION_NONE) {
		if (!fEffectsSent && fEffects.strong == 0 && fEffects.weak == 0)
			return B_OK;
		fEffects.strong = 0;
		fEffects.weak = 0;
		return _SendPlayStation();
	}
	if (fPID.usable)
		return _PIDStop();
	return B_OK;
}


void
ControllerFeel::_ScheduleStop(bigtime_t duration)
{
	fStopAt = duration > 0 ? system_time() + duration : 0;
	if (fStopAt == 0)
		return;
	if (fStopThread < 0) {
		fStopSem = create_sem(0, "rumble stop");
		if (fStopSem < 0)
			return;
		fStopThread = spawn_thread(_StopThread, "rumble stop",
			B_NORMAL_PRIORITY, this);
		if (fStopThread < 0) {
			delete_sem(fStopSem);
			fStopSem = -1;
			return;
		}
		resume_thread(fStopThread);
	} else
		release_sem(fStopSem);
}


status_t
ControllerFeel::_StopThread(void* data)
{
	ControllerFeel* feel = (ControllerFeel*)data;
	while (true) {
		bigtime_t until;
		sem_id sem;
		{
			BAutolock locker(feel->fLock);
			if (feel->fQuit)
				return B_OK;
			if (feel->fStopAt != 0 && system_time() >= feel->fStopAt) {
				feel->fStopAt = 0;
				feel->_StopLocked();
			}
			until = feel->fStopAt != 0 ? feel->fStopAt : B_INFINITE_TIMEOUT;
			sem = feel->fStopSem;
		}
		if (until == B_INFINITE_TIMEOUT)
			acquire_sem(sem);
		else
			acquire_sem_etc(sem, 1, B_ABSOLUTE_TIMEOUT, until);
	}
}


}	// namespace R5GameController
