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
	// bus: R5_JOYSTICK_BUS_*.
			void				Open(int fd, uint16 vendor, uint16 product,
									uint16 bus);
			void				Close();

			bool				CanRumble() const;
			bool				HasTriggerEffects() const;
			bool				HasMotion() const;

			status_t			SetRumble(float strong, float weak,
									bigtime_t duration);
			status_t			StopRumble();
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
			status_t			_StopLocked();
			void				_ScheduleStop(bigtime_t duration);
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
			bool				fQuit;

			bool				fMotionEnabled;
			bool				fHasMotion;
			game_controller_motion fMotion;
};


}	// namespace R5GameController


#endif	// _GAME_CONTROLLER_FEEL_H
