/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BGameController: a game controller with named buttons and axes, on top of
 * BJoystick (docs/GAME-CONTROLLERS.md, docs/LIBR5.md "Game Kit").
 * libr5game.so. BJoystick stays as it is; a program that only uses
 * BJoystick is unchanged.
 */
#ifndef _GAME_CONTROLLER_H
#define _GAME_CONTROLLER_H


#include <Messenger.h>
#include <OS.h>
#include <SupportDefs.h>


// The buttons and axes of the standard layout, in the order of SDL's
// standard gamepad, so a ported game finds them where it expects. A is the
// lower face button, B the right one, X the left one, Y the upper one,
// whatever is printed on them. The numbers are part of the contract.
enum game_controller_button {
	B_CONTROLLER_BUTTON_A = 0,
	B_CONTROLLER_BUTTON_B,
	B_CONTROLLER_BUTTON_X,
	B_CONTROLLER_BUTTON_Y,
	B_CONTROLLER_BUTTON_BACK,
	B_CONTROLLER_BUTTON_GUIDE,
	B_CONTROLLER_BUTTON_START,
	B_CONTROLLER_BUTTON_LEFT_STICK,
	B_CONTROLLER_BUTTON_RIGHT_STICK,
	B_CONTROLLER_BUTTON_LEFT_SHOULDER,
	B_CONTROLLER_BUTTON_RIGHT_SHOULDER,
	B_CONTROLLER_BUTTON_DPAD_UP,
	B_CONTROLLER_BUTTON_DPAD_DOWN,
	B_CONTROLLER_BUTTON_DPAD_LEFT,
	B_CONTROLLER_BUTTON_DPAD_RIGHT,
	B_CONTROLLER_BUTTON_MISC1,
	B_CONTROLLER_BUTTON_PADDLE1,
	B_CONTROLLER_BUTTON_PADDLE2,
	B_CONTROLLER_BUTTON_PADDLE3,
	B_CONTROLLER_BUTTON_PADDLE4,
	B_CONTROLLER_BUTTON_TOUCHPAD,

	B_CONTROLLER_BUTTON_COUNT
};

// Sticks are -32768 to 32767 (left and up negative), triggers 0 to 32767.
enum game_controller_axis {
	B_CONTROLLER_AXIS_LEFT_X = 0,
	B_CONTROLLER_AXIS_LEFT_Y,
	B_CONTROLLER_AXIS_RIGHT_X,
	B_CONTROLLER_AXIS_RIGHT_Y,
	B_CONTROLLER_AXIS_LEFT_TRIGGER,
	B_CONTROLLER_AXIS_RIGHT_TRIGGER,

	B_CONTROLLER_AXIS_COUNT
};

// Which shape the controller has: chosen from the vendor and product ID
// and the mapping database. Unknown: no mapping, only raw buttons and axes.
enum game_controller_family {
	B_GAME_CONTROLLER_FAMILY_UNKNOWN = 0,
	B_GAME_CONTROLLER_FAMILY_XBOX = 1,
	B_GAME_CONTROLLER_FAMILY_PLAYSTATION = 2,
	B_GAME_CONTROLLER_FAMILY_NINTENDO = 3,
	B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS = 4
};

// The labels a game shows for the face buttons. Automatic: those of the
// controller's family. The person chooses in Settings, Game controllers.
enum game_controller_labels {
	B_GAME_CONTROLLER_LABELS_AUTOMATIC = 0,
	B_GAME_CONTROLLER_LABELS_XBOX = 1,
		// A B X Y
	B_GAME_CONTROLLER_LABELS_PLAYSTATION = 2
		// cross, circle, square, triangle
};

enum game_controller_connection {
	B_GAME_CONTROLLER_CONNECTION_OTHER = 0,
	B_GAME_CONTROLLER_CONNECTION_USB = 1,
	B_GAME_CONTROLLER_CONNECTION_BLUETOOTH = 2
};

// Feel and motion (added 17 Nov 2026, docs/GAME-CONTROLLERS.md, option C
// phase 1). The numbers are part of the contract.
enum game_controller_trigger {
	B_CONTROLLER_TRIGGER_LEFT = 0,
	B_CONTROLLER_TRIGGER_RIGHT = 1
};

// What a DualSense trigger does against the finger.
enum game_controller_trigger_effect {
	B_TRIGGER_EFFECT_OFF = 0,
		// free, as without a game
	B_TRIGGER_EFFECT_RESISTANCE = 1,
		// pushes back from the start position on
	B_TRIGGER_EFFECT_CLICK = 2,
		// resists, then gives way, like a trigger that fires
	B_TRIGGER_EFFECT_VIBRATION = 3
		// buzzes from the start position on
};

struct game_controller_motion {
	bigtime_t	timestamp;
		// system_time() when the controller sent it
	float		acceleration[3];
		// in g: x to the right, y up, z toward the player; about (0, 1, 0)
		// when the controller lies flat
	float		rotation[3];
		// in degrees per second around x (pitch), y (yaw) and z (roll)
	uint32		_reserved[4];
};

// To the messenger of StartWatching(), with the string "device" (the name
// for Open(), such as "usb/0").
enum {
	B_GAME_CONTROLLER_ADDED = 'gcad',
	B_GAME_CONTROLLER_REMOVED = 'gcrm'
};


class BGameController {
public:
								BGameController();
	virtual						~BGameController();

	// The controllers under /dev/joystick, by the name Open() takes
	// ("usb/0", "bluetooth/0"), as BJoystick lists them.
			int32				CountDevices();
			status_t			GetDeviceName(int32 index, char* name,
									size_t bufferSize = B_OS_NAME_LENGTH);
			status_t			RescanDevices();

			status_t			Open(const char* deviceName);
			void				Close();
			bool				IsOpen() const;

	// Reads the controller once; call it every frame, as with BJoystick.
			status_t			Update();

	// The standard layout. Without a mapping (HasMapping() false) these
	// stay up and centered; use the raw values below.
			bool				HasMapping() const;
			bool				HasButton(game_controller_button button) const;
			bool				HasAxis(game_controller_axis axis) const;
			bool				IsButtonDown(game_controller_button button)
									const;
			uint32				ButtonValues() const;
									// bit n: button n of the standard layout
			int16				AxisValue(game_controller_axis axis) const;

	// What the controller itself reports, in the driver's order.
			int32				CountRawAxes() const;
			int16				RawAxisValue(int32 index) const;
			int32				CountRawButtons() const;
			bool				IsRawButtonDown(int32 index) const;
			int32				CountRawHats() const;
			uint8				RawHatValue(int32 index) const;
									// 0 centered, 1 up, 3 right, 5 down,
									// 7 left, the even ones in between

	// About the controller.
			const char*			Name() const;
			const char*			DeviceName() const;
			uint16				VendorID() const;
			uint16				ProductID() const;
			game_controller_connection Connection() const;
			game_controller_family Family() const;
			int32				BatteryLevel() const;
									// percent, or -1 when it does not say
			game_controller_labels LabelStyle() const;
									// never AUTOMATIC

	// The label choice per controller model, in the person's settings.
	static	game_controller_labels LabelStyleFor(uint16 vendorID,
									uint16 productID);
	static	status_t			SetLabelStyle(uint16 vendorID,
									uint16 productID,
									game_controller_labels labels);
	static	status_t			ResetLabelStyles();

	// B_GAME_CONTROLLER_ADDED and B_GAME_CONTROLLER_REMOVED when a
	// controller is plugged in, paired, unplugged or turned off.
	static	status_t			StartWatching(BMessenger target);
	static	status_t			StopWatching(BMessenger target);

	// The person's own setup per controller model, in the same settings
	// file as the label choice (added after step 8; docs/LIBR5.md). The
	// standard layout of an open controller comes from, in this order: a
	// saved mapping, a chosen layout, the mapping database. Open() reads
	// them, so a change counts from the next Open(). BJoystick programs
	// see the raw buttons as before.

	// The layout chosen in Settings: the shape, and a fixed mapping in the
	// HID order of that family's controllers. UNKNOWN: automatic (the
	// database and the shape it gives).
	static	game_controller_family LayoutFor(uint16 vendorID,
									uint16 productID);
	static	status_t			SetLayout(uint16 vendorID, uint16 productID,
									game_controller_family layout);

	// A mapping set up button by button: "a:b0,b:b1,dpup:h0.1,leftx:a0,
	// +righttrigger:a4,..." (SDL's mapping keys; buttons and hats as the
	// raw calls above number them, axes in the order of RawAxisValue()).
	// NULL or "" removes it. B_ENTRY_NOT_FOUND when none is saved.
	static	status_t			GetSavedMapping(uint16 vendorID,
									uint16 productID, char* mapping,
									size_t size);
	static	status_t			SetSavedMapping(uint16 vendorID,
									uint16 productID, const char* mapping);
			bool				HasSavedMapping() const;
									// the open controller uses one

	// Feel and motion (added 17 Nov 2026; docs/LIBR5.md, "Game Kit").
	// Phase 1: the DualSense, the DualSense Edge and the DualShock 4 over
	// USB and Bluetooth, and rumble on any controller with HID PID force
	// feedback. A call the open controller cannot do returns
	// B_NOT_SUPPORTED; the Can and Has calls say so beforehand.

	// Rumble: strong is the heavy, low motor, weak the light, fast one,
	// each 0 to 1. It stops by itself after duration (0: when the next
	// call changes it). Close() stops it.
			bool				CanRumble() const;
			status_t			SetRumble(float strong, float weak,
									bigtime_t duration);
			status_t			StopRumble();

	// Trigger effects (DualSense and Edge): start is where on the pull the
	// effect begins, strength how hard it is, each 0 to 1. Close() sets
	// both triggers free again.
			bool				HasTriggerEffects() const;
			status_t			SetTriggerEffect(
									game_controller_trigger trigger,
									game_controller_trigger_effect effect,
									float start, float strength);

	// Motion: switch it on, then call Update() each frame and read the
	// last sample. Over Bluetooth this switches a PlayStation controller
	// to its full reports until it disconnects; its buttons keep working
	// for every program. B_ENTRY_NOT_FOUND until the first sample came.
			bool				HasMotion() const;
			status_t			SetMotionEnabled(bool enabled);
			bool				IsMotionEnabled() const;
			status_t			GetMotion(game_controller_motion* motion)
									const;

private:
			struct Private;

	virtual	void				_ReservedGameController1();
	virtual	void				_ReservedGameController2();
	virtual	void				_ReservedGameController3();
	virtual	void				_ReservedGameController4();

								BGameController(const BGameController&);
			BGameController&	operator=(const BGameController&);

			Private*			fPrivate;
			uint32				_reserved[8];
};


#endif	// _GAME_CONTROLLER_H
