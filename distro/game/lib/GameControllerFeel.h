/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Rumble, trigger effects and motion of one open BGameController
 * (docs/GAME-CONTROLLERS.md, option C phase 1). Private to libr5game.
 */
#ifndef _GAME_CONTROLLER_FEEL_H
#define _GAME_CONTROLLER_FEEL_H


#include <Locker.h>
#include <OS.h>

#include "GameController.h"
#include "GameControllerReports.h"


namespace R5GameController {


class ControllerFeel {
public:
								ControllerFeel();
								~ControllerFeel();

	// fd: the open joystick node (R5_JOYSTICK_SEND_REPORT and friends);
	// bus: R5_JOYSTICK_BUS_*; flags: R5_JOYSTICK_FLAG_* of the driver.
			void				Open(int fd, uint16 vendor, uint16 product,
									uint16 bus, uint32 flags);
			void				Close();

			bool				CanRumble() const;
			bool				HasTriggerEffects() const;
			bool				HasMotion() const;
			bool				HasTriggerRumble() const;

			status_t			SetRumble(float strong, float weak,
									bigtime_t duration);
			status_t			StopRumble();
			status_t			SetTriggerRumble(float left, float right,
									bigtime_t duration);
			status_t			SetTriggerEffect(uint32 trigger,
									uint32 effect, float start,
									float strength);
			status_t			SetMotionEnabled(bool enabled);
			bool				IsMotionEnabled() const;

	// Called by BGameController::Update(): reads the motion report.
			void				Update();
			status_t			GetMotion(game_controller_motion* motion)
									const;

private:
			status_t			_SendPlayStation();
			status_t			_SendReport(uint8 type, uint8 id,
									const uint8* data, size_t length);
			status_t			_GetReport(uint8 type, uint8 id, uint8* data,
									size_t capacity, size_t* length,
									bigtime_t* when);
			status_t			_PIDStart(float strong, float weak,
									bigtime_t duration);
			status_t			_PIDStop();
			status_t			_StopLocked(bool triggers);
			void				_Wake();

			// Phase 2: Switch and Xbox controllers.
			size_t				_SwitchLength() const;
			status_t			_SendSwitchRumble();
			status_t			_SwitchSubcommand(uint8 command,
									const uint8* args, size_t argLength,
									uint8* reply, size_t replyCapacity,
									size_t* replyLength);
			void				_ReadSwitchCalibration();
			status_t			_SendXbox();
	static	status_t			_StopThread(void* data);

	mutable	BLocker				fLock;
			int					fFD;
			playstation_model	fModel;
			bool				fBluetooth;
			playstation_effects	fEffects;
			bool				fEffectsSent;

			hid_layout*			fLayout;
			pid_device			fPID;
			bool				fPIDReady;
			uint8				fPIDBlock;

			thread_id			fStopThread;
			sem_id				fStopSem;
			bigtime_t			fStopAt;
			bigtime_t			fTriggerStopAt;
			bigtime_t			fRefreshAt;
			bool				fQuit;

			bool				fMotionEnabled;
			bool				fHasMotion;
			game_controller_motion fMotion;

			switch_model		fSwitch;
			uint8				fSwitchCounter;
			float				fSwitchStrong;
			float				fSwitchWeak;
			bool				fSwitchSent;
			bool				fSwitchCalibrated;
			switch_imu_calibration fSwitchIMU;

			xbox_model			fXbox;
			bool				fXboxTriggers;
			uint8				fXboxSequence;
			bool				fXboxSent;
			float				fXboxMotor[4];
				// left trigger, right trigger, strong, weak
};


}	// namespace R5GameController


#endif	// _GAME_CONTROLLER_FEEL_H
