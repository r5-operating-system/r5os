/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The R5 Power Kit, libr5power.so (docs/POWER.md, section 3; docs/LIBR5.md,
 * "Power Kit"; R5Book power-kit.html). Next to libbe, never part of it.
 *
 * A function that shipped does not change. New ones go at the end.
 */
#ifndef _R5_POWER_ROSTER_H
#define _R5_POWER_ROSTER_H


#include <Messenger.h>
#include <SupportDefs.h>


class BMessage;


// The one message a watching program gets. Its int32 "change" says what
// changed; for a battery change the program reads GetBatteryInfo() again.
const uint32 kMsgPowerChanged = 'r5pw';

enum power_change {
	B_POWER_BATTERY_CHANGED		= 1,
		// charge, state or time left of a battery; a battery came or went
	B_POWER_AC_CHANGED			= 2,
		// plugged in or unplugged; bool "on ac"
	B_POWER_PROFILE_CHANGED		= 3,
		// int32 "profile"
	B_POWER_SETTINGS_CHANGED	= 4,
		// the machine's power settings were changed
	B_POWER_WILL_SLEEP			= 5,
	B_POWER_DID_WAKE			= 6
		// reserved for sleep (docs/POWER.md, step 4); not sent yet
};

enum power_profile {
	B_POWER_PROFILE_SAVER		= 0,
	B_POWER_PROFILE_BALANCED	= 1,
	B_POWER_PROFILE_PERFORMANCE	= 2
};

enum power_battery_state {
	B_POWER_BATTERY_DISCHARGING	= 0x01,
	B_POWER_BATTERY_CHARGING	= 0x02,
	B_POWER_BATTERY_CRITICAL	= 0x04,
	B_POWER_BATTERY_NOT_CHARGING = 0x08
		// plugged in, and the firmware holds the charge where it is
};

enum power_sleep_state {
	B_POWER_SLEEP_NONE			= 0,
		// the firmware offers no sleep state
	B_POWER_SLEEP_S0IX			= 1,
		// suspend to idle (Modern Standby)
	B_POWER_SLEEP_S3			= 2
		// suspend to RAM
};

struct power_battery_info {
	uint32		state;
		// power_battery_state flags
	int32		percent;
		// charge, 0 to 100; -1 when unknown
	bigtime_t	time_left;
		// seconds until empty (discharging) or full (charging); -1 when
		// unknown
	int32		health;
		// full charge capacity in percent of the design capacity; -1 when
		// the firmware does not report both
	int32		capacity;
	int32		full_capacity;
	int32		design_capacity;
		// in mWh, or in mAh when "in_mah" is true
	bool		in_mah;
	uint32		_reserved[8];
};


class BPowerRoster {
public:
								BPowerRoster();
								~BPowerRoster();

	// Reading needs no rights and works in Safe mode.
			int32				CountBatteries() const;
			status_t			GetBatteryInfo(int32 index,
									power_battery_info* info) const;
			bool				IsOnAC() const;
				// true without a battery, or with one that is not
				// discharging
			power_profile		Profile() const;
			bool				CanChangeProfile() const;
				// false when the processor offers no speed control: the
				// profile menus are gray
			bool				CanSleep(
									power_sleep_state* state = NULL) const;
				// true when sleep works on this computer; "state" is the
				// state the firmware offers. False before docs/POWER.md
				// step 4

	// The machine's power settings, from the power service. Reading needs
	// no rights. Changing them is an admin request (libr5admin.so): the
	// service checks the sender and writes the file. B_NO_INIT when the
	// service is not running (Safe mode).
			status_t			GetSettings(BMessage* settings) const;
			status_t			SetSettings(const BMessage& settings);

	// Changes reach the target as kMsgPowerChanged. A program never polls.
	static	status_t			StartWatching(BMessenger target);
	static	status_t			StopWatching(BMessenger target);

private:
								BPowerRoster(const BPowerRoster&);
			BPowerRoster&		operator=(const BPowerRoster&);

			void*				fData;
			uint32				_reserved[8];
};


#endif	// _R5_POWER_ROSTER_H
