/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BGameControllerMotionView: what a controller's motion sensors say, as a
 * level for the tilt (BAppearanceKit::BTiltLevel) and a bar each for the
 * turn around the three axes (BAppearanceKit::BAxisBar), labelled Pitch,
 * Roll and Yaw. libr5game.so, docs/LIBR5.md "Game Kit". The view only
 * shows samples; the program reads them with
 * BGameController::GetMotion().
 */
#ifndef _GAME_CONTROLLER_MOTION_VIEW_H
#define _GAME_CONTROLLER_MOTION_VIEW_H


#include <View.h>

#include <GameController.h>


class BGameControllerMotionView : public BView {
public:
								BGameControllerMotionView(const char* name);
	virtual						~BGameControllerMotionView();

	// Shows one sample: the tilt from the acceleration (x and z, in g,
	// clamped to 1), the turn from the rotation, full at TurnRange()
	// degrees per second either way.
			void				SetMotion(
									const game_controller_motion& motion);
	// Everything back to the middle, as with a controller at rest.
			void				Reset();

			void				SetTurnRange(float degreesPerSecond);
			float				TurnRange() const;

	// False draws the level, the bars and their labels gray, as a
	// disabled control (an example without a controller); true at first.
			void				SetEnabled(bool enabled);
			bool				IsEnabled() const;

private:
			struct Private;

	virtual	void				_ReservedGameControllerMotionView1();
	virtual	void				_ReservedGameControllerMotionView2();
	virtual	void				_ReservedGameControllerMotionView3();
	virtual	void				_ReservedGameControllerMotionView4();

								BGameControllerMotionView(
									const BGameControllerMotionView&);
			BGameControllerMotionView& operator=(
									const BGameControllerMotionView&);

			Private*			fPrivate;
			uint32				_reserved[4];
};


#endif	// _GAME_CONTROLLER_MOTION_VIEW_H
