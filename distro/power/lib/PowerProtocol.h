/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Private: the messages between libr5power.so, the Power page in Settings
 * and the power service (power_daemon), and the machine settings with their
 * defaults (docs/POWER.md, sections 3 and 7). A code that shipped does not
 * change.
 */
#ifndef _R5_POWER_PROTOCOL_H
#define _R5_POWER_PROTOCOL_H


#include <Message.h>


#define R5_POWER_SERVICE_SIGNATURE	"application/x-vnd.Haiku-powermanagement"
	// Haiku's own power_daemon (BServer signature); its launch job is
	// x-vnd.Haiku-power_daemon
#define R5_POWER_SETTINGS_NAME		"power"
	// in B_SYSTEM_SETTINGS_DIRECTORY; driver_settings text

// To the service's normal messenger (no rights needed)
const uint32 kMsgPowerGetSettings	= 'r5pg';
	// reply: the settings fields below
const uint32 kMsgPowerWatch			= 'r5pa';
const uint32 kMsgPowerUnwatch		= 'r5pu';
	// messenger "target": gets kMsgPowerChanged for the profile and the
	// settings

// To the service's admin port (r5_admin_send(), administrator or grant)
const uint32 kMsgPowerSetSettings	= 'r5ps';
	// the settings fields; reply: int32 "status"

// The settings fields; the same names as the lines of the file.
#define R5_POWER_PROFILE_BATTERY	"profile_on_battery"
#define R5_POWER_PROFILE_AC			"profile_on_ac"
	// int32, power_profile
#define R5_POWER_DISPLAY_BATTERY	"display_off_on_battery"
#define R5_POWER_DISPLAY_AC			"display_off_on_ac"
#define R5_POWER_SLEEP_BATTERY		"sleep_on_battery"
#define R5_POWER_SLEEP_AC			"sleep_on_ac"
	// int32, minutes; 0 is never
#define R5_POWER_LID_ACTION			"lid_closed"
	// string: "sleep", "display_off", "nothing"
#define R5_POWER_BUTTON_ACTION		"power_button"
	// string: "sleep", "shutdown", "ask", "nothing"
#define R5_POWER_EXTERNAL_AWAKE		"lid_closed_external_display_awake"
#define R5_POWER_DIM_ON_BATTERY		"dim_on_battery"
#define R5_POWER_WAKE_LID			"wake_lid"
#define R5_POWER_WAKE_KEYBOARD		"wake_keyboard"
#define R5_POWER_WAKE_MOUSE			"wake_mouse"
	// bool

// The display-off and sleep choices of the page, in minutes
static const int32 kPowerDisplayChoices[] = { 0, 1, 2, 5, 10, 15, 30 };
static const int32 kPowerSleepChoices[] = { 0, 5, 10, 15, 30, 60 };


/*!	The factory setup (docs/POWER.md, section 7). The lid and the power
	button fall back to what works while sleep does not.
*/
static inline void
power_default_settings(BMessage& settings, bool sleepWorks)
{
	settings.MakeEmpty();
	settings.AddInt32(R5_POWER_PROFILE_BATTERY, 1);
	settings.AddInt32(R5_POWER_PROFILE_AC, 1);
	settings.AddInt32(R5_POWER_DISPLAY_BATTERY, 5);
	settings.AddInt32(R5_POWER_DISPLAY_AC, 10);
	settings.AddInt32(R5_POWER_SLEEP_BATTERY, 15);
	settings.AddInt32(R5_POWER_SLEEP_AC, 30);
	settings.AddString(R5_POWER_LID_ACTION,
		sleepWorks ? "sleep" : "display_off");
	settings.AddString(R5_POWER_BUTTON_ACTION, sleepWorks ? "sleep" : "ask");
	settings.AddBool(R5_POWER_EXTERNAL_AWAKE, true);
	settings.AddBool(R5_POWER_DIM_ON_BATTERY, true);
	settings.AddBool(R5_POWER_WAKE_LID, true);
	settings.AddBool(R5_POWER_WAKE_KEYBOARD, true);
	settings.AddBool(R5_POWER_WAKE_MOUSE, false);
}


#endif	// _R5_POWER_PROTOCOL_H
