/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Rumble, trigger effects and motion (docs/GAME-CONTROLLERS.md, option C):
 * PlayStation controllers through their own output reports (phase 1), the
 * Switch Pro Controller and the Joy-Cons, and Xbox controllers over
 * Bluetooth and over USB (GIP) through theirs (phase 2), every other
 * controller with HID PID through its PID reports, all sent with
 * R5_JOYSTICK_SEND_REPORT. The report bytes are built in
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


// A Switch controller stops a rumble it gets no news of (SDL refreshes it
// every 50 ms, RUMBLE_REFRESH_FREQUENCY_MS).
static const bigtime_t kSwitchRefresh = 50000;


static float
clamp_unit(float value)
{
	if (!(value > 0))
		return 0;
	return value > 1 ? 1 : value;
}


static bigtime_t
earliest(bigtime_t a, bigtime_t b)
{
	if (a == 0)
		return b;
	if (b == 0)
		return a;
	return a < b ? a : b;
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
	fTriggerStopAt(0),
	fRefreshAt(0),
	fQuit(false),
	fMotionEnabled(false),
	fHasMotion(false),
	fSwitch(SWITCH_NONE),
	fSwitchCounter(0),
	fSwitchStrong(0),
	fSwitchWeak(0),
	fSwitchSent(false),
	fSwitchCalibrated(false),
	fXbox(XBOX_NONE),
	fXboxTriggers(false),
	fXboxSequence(0x40),
	fXboxSent(false)
{
	playstation_effects_init(fEffects);
	memset(&fPID, 0, sizeof(fPID));
	memset(&fMotion, 0, sizeof(fMotion));
	memset(fXboxMotor, 0, sizeof(fXboxMotor));
	switch_imu_nominal(fSwitchIMU);
}


ControllerFeel::~ControllerFeel()
{
	Close();
}


void
ControllerFeel::Open(int fd, uint16 vendor, uint16 product, uint16 bus,
	uint32 flags)
{
	Close();

	BAutolock locker(fLock);
	fFD = fd;
	if (fd < 0)
		return;
	fBluetooth = bus == R5_JOYSTICK_BUS_BLUETOOTH;
	bool known = bus == R5_JOYSTICK_BUS_USB
		|| bus == R5_JOYSTICK_BUS_BLUETOOTH;
	fModel = known ? playstation_model_for(vendor, product)
		: PLAYSTATION_NONE;
	playstation_effects_init(fEffects);
	fSwitch = known ? switch_model_for(vendor, product) : SWITCH_NONE;
	fXbox = known ? xbox_model_for(vendor, product, fBluetooth,
		(flags & R5_JOYSTICK_FLAG_GIP) != 0) : XBOX_NONE;
	fXboxTriggers = fXbox != XBOX_NONE && xbox_has_trigger_rumble(vendor);
	switch_imu_nominal(fSwitchIMU);

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

	// The report descriptor: the declared sizes of a Switch controller's
	// output reports, and on any other controller HID PID force feedback
	// when it has the PID page.
	uint8* descriptor = (uint8*)malloc(R5_JOYSTICK_MAX_DESCRIPTOR);
	fLayout = new(std::nothrow) hid_layout;
	bool parsed = false;
	if (descriptor != NULL && fLayout != NULL) {
		r5_joystick_descriptor request;
		memset(&request, 0, sizeof(request));
		request.length = R5_JOYSTICK_MAX_DESCRIPTOR;
		request.data = descriptor;
		parsed = ioctl(fd, R5_JOYSTICK_GET_DESCRIPTOR, &request,
				sizeof(request)) == 0
			&& parse_hid_descriptor(descriptor, request.length, *fLayout);
		if (!parsed || fSwitch != SWITCH_NONE || fXbox != XBOX_NONE
			|| !pid_find(*fLayout, fPID))
			memset(&fPID, 0, sizeof(fPID));
	}
	free(descriptor);
	if (!parsed || (!fPID.usable && fSwitch == SWITCH_NONE)) {
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
			fTriggerStopAt = 0;
			fRefreshAt = 0;
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
				_StopLocked(true);
			if (fSwitch == SWITCH_PRO && fMotionEnabled) {
				// The sensors off again; no need to wait for the answer.
				uint8 off = 0x00;
				uint8 report[kSwitchUSBSize];
				size_t length = build_switch_subcommand(fSwitchCounter++, 0,
					0, 0x40, &off, 1, _SwitchLength(), report,
					sizeof(report));
				if (length > 0)
					_SendReport(R5_JOYSTICK_REPORT_OUTPUT, 0x01, report,
						length);
			}
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
	fSwitch = SWITCH_NONE;
	fSwitchStrong = 0;
	fSwitchWeak = 0;
	fSwitchSent = false;
	fSwitchCalibrated = false;
	switch_imu_nominal(fSwitchIMU);
	fXbox = XBOX_NONE;
	fXboxTriggers = false;
	fXboxSent = false;
	memset(fXboxMotor, 0, sizeof(fXboxMotor));
}


bool
ControllerFeel::CanRumble() const
{
	BAutolock locker(fLock);
	return fFD >= 0 && (fModel != PLAYSTATION_NONE || fSwitch != SWITCH_NONE
		|| fXbox != XBOX_NONE || fPID.usable);
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
	// A Joy-Con has the sensors too, but only in the full report mode,
	// which would change its buttons: not offered (phase 2).
	return fFD >= 0 && (fModel != PLAYSTATION_NONE || fSwitch == SWITCH_PRO);
}


bool
ControllerFeel::HasTriggerRumble() const
{
	BAutolock locker(fLock);
	return fFD >= 0 && fXboxTriggers;
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
	} else if (fSwitch != SWITCH_NONE) {
		fSwitchStrong = strong;
		fSwitchWeak = weak;
		status = _SendSwitchRumble();
	} else if (fXbox != XBOX_NONE) {
		fXboxMotor[2] = strong;
		fXboxMotor[3] = weak;
		status = _SendXbox();
	} else
		status = _PIDStart(strong, weak, duration);
	if (status != B_OK)
		return status;

	bool running = strong > 0 || weak > 0;
	fStopAt = running && duration > 0 ? system_time() + duration : 0;
	_Wake();
	return B_OK;
}


status_t
ControllerFeel::StopRumble()
{
	if (!CanRumble())
		return B_NOT_SUPPORTED;
	BAutolock locker(fLock);
	fStopAt = 0;
	fTriggerStopAt = 0;
	return _StopLocked(true);
}


status_t
ControllerFeel::SetTriggerRumble(float left, float right,
	bigtime_t duration)
{
	if (!HasTriggerRumble())
		return B_NOT_SUPPORTED;
	BAutolock locker(fLock);
	fXboxMotor[0] = clamp_unit(left);
	fXboxMotor[1] = clamp_unit(right);
	status_t status = _SendXbox();
	if (status != B_OK)
		return status;
	bool running = fXboxMotor[0] > 0 || fXboxMotor[1] > 0;
	fTriggerStopAt = running && duration > 0 ? system_time() + duration : 0;
	_Wake();
	return B_OK;
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
	if (fSwitch == SWITCH_PRO) {
		if (enabled && !fMotionEnabled) {
			// The full report mode (the driver asked for it already; once
			// more costs nothing), the scale from the factory calibration,
			// then the sensors on.
			uint8 mode = 0x30;
			_SwitchSubcommand(0x03, &mode, 1, NULL, 0, NULL);
			if (!fSwitchCalibrated)
				_ReadSwitchCalibration();
			uint8 on = 0x01;
			status_t status = _SwitchSubcommand(0x40, &on, 1, NULL, 0, NULL);
			if (status != B_OK)
				return status;
		} else if (!enabled && fMotionEnabled) {
			uint8 off = 0x00;
			_SwitchSubcommand(0x40, &off, 1, NULL, 0, NULL);
		}
	} else if (enabled && !fMotionEnabled && fBluetooth && !fEffectsSent) {
		// Over Bluetooth a PlayStation controller has the sensors only in
		// the full report, which it sends after its first output report.
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
	motion_sample sample;
	if (fSwitch == SWITCH_PRO) {
		if (_GetReport(R5_JOYSTICK_REPORT_INPUT, 0x30, report,
				sizeof(report), &length, &when) != B_OK
			|| !parse_switch_motion(fSwitchIMU, report, length, sample))
			return;
	} else {
		uint8 id = playstation_motion_report(fModel, fBluetooth);
		if (_GetReport(R5_JOYSTICK_REPORT_INPUT, id, report, sizeof(report),
				&length, &when) != B_OK
			|| !parse_playstation_motion(fModel, fBluetooth, report, length,
				sample))
			return;
	}
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


/*!	The motors off; with triggers also the trigger motors of an Xbox
	controller (StopRumble(), Close()), else only the two main ones (the
	end of a SetRumble() duration).
*/
status_t
ControllerFeel::_StopLocked(bool triggers)
{
	if (fModel != PLAYSTATION_NONE) {
		if (!fEffectsSent && fEffects.strong == 0 && fEffects.weak == 0)
			return B_OK;
		fEffects.strong = 0;
		fEffects.weak = 0;
		return _SendPlayStation();
	}
	if (fSwitch != SWITCH_NONE) {
		fRefreshAt = 0;
		if (!fSwitchSent)
			return B_OK;
		fSwitchStrong = 0;
		fSwitchWeak = 0;
		return _SendSwitchRumble();
	}
	if (fXbox != XBOX_NONE) {
		if (!fXboxSent)
			return B_OK;
		fXboxMotor[2] = 0;
		fXboxMotor[3] = 0;
		if (triggers) {
			fXboxMotor[0] = 0;
			fXboxMotor[1] = 0;
		}
		return _SendXbox();
	}
	if (fPID.usable)
		return _PIDStop();
	return B_OK;
}


/*!	Starts the thread that ends rumbles and refreshes a Switch rumble, or
	tells it that a time changed.
*/
void
ControllerFeel::_Wake()
{
	if (fStopAt == 0 && fTriggerStopAt == 0 && fRefreshAt == 0)
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
			bigtime_t now = system_time();
			if (feel->fStopAt != 0 && now >= feel->fStopAt) {
				feel->fStopAt = 0;
				feel->_StopLocked(false);
			}
			if (feel->fTriggerStopAt != 0 && now >= feel->fTriggerStopAt) {
				feel->fTriggerStopAt = 0;
				feel->fXboxMotor[0] = 0;
				feel->fXboxMotor[1] = 0;
				feel->_SendXbox();
			}
			if (feel->fRefreshAt != 0 && now >= feel->fRefreshAt) {
				// Sets the next time when it was sent; a failed send ends
				// the refresh.
				feel->fRefreshAt = 0;
				feel->_SendSwitchRumble();
			}
			until = earliest(earliest(feel->fStopAt, feel->fTriggerStopAt),
				feel->fRefreshAt);
			if (until == 0)
				until = B_INFINITE_TIMEOUT;
			sem = feel->fStopSem;
		}
		if (until == B_INFINITE_TIMEOUT)
			acquire_sem(sem);
		else
			acquire_sem_etc(sem, 1, B_ABSOLUTE_TIMEOUT, until);
	}
}


// #pragma mark - Switch and Xbox controllers (phase 2, fLock held)


/*!	The length of the Switch output reports: what the descriptor declares
	for 0x01 (the Pro Controller's own over Bluetooth, R5's over USB),
	else SDL's size for the transport.
*/
size_t
ControllerFeel::_SwitchLength() const
{
	size_t length = fBluetooth ? kSwitchBluetoothSize : kSwitchUSBSize;
	if (fLayout != NULL) {
		size_t declared = hid_report_length(*fLayout, HID_OUTPUT, 0x01);
		if (declared >= 11 && declared <= kSwitchUSBSize)
			length = declared;
	}
	return length;
}


status_t
ControllerFeel::_SendSwitchRumble()
{
	uint8 report[kSwitchUSBSize];
	size_t length = build_switch_rumble(fSwitchCounter, fSwitchStrong,
		fSwitchWeak, _SwitchLength(), report, sizeof(report));
	if (length == 0)
		return B_ERROR;
	fSwitchCounter = (fSwitchCounter + 1) & 0x0f;
	status_t status = _SendReport(R5_JOYSTICK_REPORT_OUTPUT, 0x10, report,
		length);
	if (status != B_OK)
		return status;
	fSwitchSent = true;
	bool running = fSwitchStrong > 0 || fSwitchWeak > 0;
	fRefreshAt = running ? system_time() + kSwitchRefresh : 0;
	return B_OK;
}


/*!	A subcommand, then up to 120 ms for its reply (a report 0x21 newer than
	the request, with the subcommand in byte 14), twice. The reply is copied
	to reply when one is wanted.
*/
status_t
ControllerFeel::_SwitchSubcommand(uint8 command, const uint8* args,
	size_t argLength, uint8* reply, size_t replyCapacity,
	size_t* replyLength)
{
	uint8 report[kSwitchUSBSize];
	for (int attempt = 0; attempt < 2; attempt++) {
		size_t length = build_switch_subcommand(fSwitchCounter, fSwitchStrong,
			fSwitchWeak, command, args, argLength, _SwitchLength(), report,
			sizeof(report));
		if (length == 0)
			return B_BAD_VALUE;
		fSwitchCounter = (fSwitchCounter + 1) & 0x0f;
		bigtime_t sent = system_time();
		status_t status = _SendReport(R5_JOYSTICK_REPORT_OUTPUT, 0x01, report,
			length);
		if (status != B_OK)
			return status;
		fSwitchSent = true;

		for (int poll = 0; poll < 30; poll++) {
			snooze(4000);
			uint8 answer[R5_JOYSTICK_CACHED_REPORT];
			size_t answerLength = 0;
			bigtime_t when = 0;
			if (_GetReport(R5_JOYSTICK_REPORT_INPUT, 0x21, answer,
					sizeof(answer), &answerLength, &when) != B_OK
				|| when < sent || answerLength < 15 || answer[14] != command)
				continue;
			if (reply != NULL && replyLength != NULL) {
				size_t copy = answerLength < replyCapacity
					? answerLength : replyCapacity;
				memcpy(reply, answer, copy);
				*replyLength = copy;
			}
			return B_OK;
		}
	}
	return B_TIMED_OUT;
}


/*!	The factory calibration of the IMU (SPI flash 0x6020, 24 bytes);
	without an answer, or with data that cannot be right, the nominal scale
	stays (docs/GAME-CONTROLLERS.md). The user calibration at 0x8026 is not
	read.
*/
void
ControllerFeel::_ReadSwitchCalibration()
{
	fSwitchCalibrated = true;
	uint8 args[5] = { (uint8)kSwitchIMUCalibrationAddress,
		(uint8)(kSwitchIMUCalibrationAddress >> 8),
		(uint8)(kSwitchIMUCalibrationAddress >> 16),
		(uint8)(kSwitchIMUCalibrationAddress >> 24),
		kSwitchIMUCalibrationSize };
	uint8 reply[R5_JOYSTICK_CACHED_REPORT];
	size_t replyLength = 0;
	if (_SwitchSubcommand(0x10, args, sizeof(args), reply, sizeof(reply),
			&replyLength) != B_OK)
		return;
	uint8 data[kSwitchIMUCalibrationSize];
	if (parse_switch_spi_reply(reply, replyLength,
			kSwitchIMUCalibrationAddress, kSwitchIMUCalibrationSize, data))
		switch_imu_from_factory(data, fSwitchIMU);
}


status_t
ControllerFeel::_SendXbox()
{
	uint8 report[kXboxGIPRumbleSize];
	size_t length;
	uint8 id;
	if (fXbox == XBOX_GIP) {
		if (++fXboxSequence == 0)
			fXboxSequence = 1;
		length = build_xbox_gip_rumble(fXboxSequence, fXboxMotor[0],
			fXboxMotor[1], fXboxMotor[2], fXboxMotor[3], report,
			sizeof(report));
		id = 0x09;
	} else {
		length = build_xbox_bluetooth_rumble(fXboxMotor[0], fXboxMotor[1],
			fXboxMotor[2], fXboxMotor[3], report, sizeof(report));
		id = 0x03;
	}
	if (length == 0)
		return B_ERROR;
	status_t status = _SendReport(R5_JOYSTICK_REPORT_OUTPUT, id, report,
		length);
	if (status == B_OK)
		fXboxSent = true;
	return status;
}


}	// namespace R5GameController
