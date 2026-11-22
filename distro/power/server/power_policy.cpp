/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work. The original is air/OS's power_policy.cpp (commit
 * a4ddc48124a9). R5 keeps its settings file (driver_settings), its profile
 * per power source and its lid handling at start; the rest is R5's
 * (docs/POWER.md, step 2).
 */


#include "power_policy.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Autolock.h>
#include <driver_settings.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <Screen.h>
#include <Server.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <errno.h>
#include <new>
#include <sys/stat.h>

#include <File.h>
#include <InterfaceDefs.h>
#include <PowerRoster.h>
#include <Roster.h>
#include <String.h>
#include <syslog.h>

#include <PowerProtocol.h>
#include <RosterPrivate.h>
#include <r5_power_profile.h>
#include <syscalls.h>


static const bigtime_t kTickInterval = 5000000;
static const bigtime_t kTickWhileOff = 500000;
	// while the timer turned the display off: notice the first input soon
static const bigtime_t kActiveIdle = 2000000;
	// input less than this long ago turns the display on again
static const char* kACDevice = "/dev/power/acpi_ac/0";

static BPowerRoster* sRoster = NULL;


static bool
valid_minutes(int32 value, const int32* choices, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (choices[i] == value)
			return true;
	}
	return false;
}


static bool
valid_action(const char* value, const char* const* actions)
{
	for (int32 i = 0; actions[i] != NULL; i++) {
		if (strcmp(value, actions[i]) == 0)
			return true;
	}
	return false;
}


static const char* const kLidActions[] = { "sleep", "display_off",
	"nothing", NULL };
static const char* const kButtonActions[] = { "sleep", "shutdown", "ask",
	"nothing", NULL };
static const char* const kProfileFields[] = { R5_POWER_PROFILE_BATTERY,
	R5_POWER_PROFILE_AC, NULL };
static const char* const kDisplayFields[] = { R5_POWER_DISPLAY_BATTERY,
	R5_POWER_DISPLAY_AC, NULL };
static const char* const kSleepFields[] = { R5_POWER_SLEEP_BATTERY,
	R5_POWER_SLEEP_AC, NULL };
static const char* const kBoolFields[] = { R5_POWER_EXTERNAL_AWAKE,
	R5_POWER_DIM_ON_BATTERY, R5_POWER_WAKE_LID, R5_POWER_WAKE_KEYBOARD,
	R5_POWER_WAKE_MOUSE, NULL };
// SPDX-SnippetEnd


PowerPolicy::PowerPolicy(BServer* server)
	:
	fServer(server),
	fLock("power policy"),
	fOnAC(true),
	fKnowSource(false),
	fLidClosed(false),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fKnowLid(false),
	fProfile(-1),
	fDisplayOffByTimer(false),
	fDisplayOffByLid(false)
// SPDX-SnippetEnd
{
	LoadSettings();
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*static*/ status_t
PowerPolicy::SettingsPath(BPath& path)
{
	// Machine-wide (owner, 22 Nov 2026), not air/OS's per-user file
	status_t status = find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path);
	if (status == B_OK)
		status = path.Append(R5_POWER_SETTINGS_NAME);
	return status;
}


void
PowerPolicy::Start()
{
	if (sRoster == NULL)
		sRoster = new(std::nothrow) BPowerRoster;

	bool known;
	{
		BAutolock _(fLock);
		known = fKnowSource;
	}
	// A computer without an AC device (a desktop) never hears from the AC
	// monitor: its profile is the plugged-in one.
	if (!known)
		SetOnAC(sRoster != NULL ? sRoster->IsOnAC() : true);
}


bool
PowerPolicy::_SleepWorks()
{
	return sRoster != NULL && sRoster->CanSleep();
}
// SPDX-SnippetEnd


/*!	The settings file has lines like "profile_on_battery 1",
	"display_off_on_ac 10" and "lid_closed display_off" (docs/POWER.md,
	section 3; the names are in PowerProtocol.h). A line that is missing or
	wrong keeps its default. A changed file applies to the current power
	source at once.
*/
void
PowerPolicy::LoadSettings()
{
	BAutolock _(fLock);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	BMessage settings;
	power_default_settings(settings, _SleepWorks());

	BPath path;
	void* handle = SettingsPath(path) == B_OK
		? load_driver_settings(path.Path()) : NULL;
	if (handle != NULL) {
		for (int32 i = 0; kProfileFields[i] != NULL; i++) {
			const char* value = get_driver_parameter(handle,
				kProfileFields[i], NULL, NULL);
			if (value != NULL && atoi(value) >= 0
				&& atoi(value) < R5_POWER_PROFILE_COUNT)
				settings.ReplaceInt32(kProfileFields[i], atoi(value));
		}
		for (int32 i = 0; kDisplayFields[i] != NULL; i++) {
			const char* value = get_driver_parameter(handle,
				kDisplayFields[i], NULL, NULL);
			if (value != NULL && valid_minutes(atoi(value),
					kPowerDisplayChoices, B_COUNT_OF(kPowerDisplayChoices)))
				settings.ReplaceInt32(kDisplayFields[i], atoi(value));
		}
		for (int32 i = 0; kSleepFields[i] != NULL; i++) {
			const char* value = get_driver_parameter(handle,
				kSleepFields[i], NULL, NULL);
			if (value != NULL && valid_minutes(atoi(value),
					kPowerSleepChoices, B_COUNT_OF(kPowerSleepChoices)))
				settings.ReplaceInt32(kSleepFields[i], atoi(value));
		}
		const char* value = get_driver_parameter(handle,
			R5_POWER_LID_ACTION, NULL, NULL);
		if (value != NULL && valid_action(value, kLidActions))
			settings.ReplaceString(R5_POWER_LID_ACTION, value);
		value = get_driver_parameter(handle, R5_POWER_BUTTON_ACTION, NULL,
			NULL);
		if (value != NULL && valid_action(value, kButtonActions))
			settings.ReplaceString(R5_POWER_BUTTON_ACTION, value);
		for (int32 i = 0; kBoolFields[i] != NULL; i++) {
			bool defaultValue = settings.GetBool(kBoolFields[i], false);
			settings.ReplaceBool(kBoolFields[i],
				get_driver_boolean_parameter(handle, kBoolFields[i],
					defaultValue, defaultValue));
		}
		unload_driver_settings(handle);
	}
	fSettings = settings;
// SPDX-SnippetEnd

	if (fKnowSource)
		_ApplyProfile();
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
PowerPolicy::GetSettings(BMessage& settings)
{
	BAutolock _(fLock);
	settings = fSettings;
}


/*!	An admin request from the Power page (the sender was checked). Every
	field that is there must be valid, or nothing changes. "Sleep" is only
	taken while sleep works on this computer.
*/
status_t
PowerPolicy::SetSettings(const BMessage& request)
{
	BAutolock _(fLock);
	BMessage settings(fSettings);
	bool sleepWorks = _SleepWorks();

	int32 number;
	for (int32 i = 0; kProfileFields[i] != NULL; i++) {
		if (request.FindInt32(kProfileFields[i], &number) != B_OK)
			continue;
		if (number < 0 || number >= R5_POWER_PROFILE_COUNT)
			return B_BAD_VALUE;
		settings.ReplaceInt32(kProfileFields[i], number);
	}
	for (int32 i = 0; kDisplayFields[i] != NULL; i++) {
		if (request.FindInt32(kDisplayFields[i], &number) != B_OK)
			continue;
		if (!valid_minutes(number, kPowerDisplayChoices,
				B_COUNT_OF(kPowerDisplayChoices)))
			return B_BAD_VALUE;
		settings.ReplaceInt32(kDisplayFields[i], number);
	}
	for (int32 i = 0; kSleepFields[i] != NULL; i++) {
		if (request.FindInt32(kSleepFields[i], &number) != B_OK)
			continue;
		if (!valid_minutes(number, kPowerSleepChoices,
				B_COUNT_OF(kPowerSleepChoices)))
			return B_BAD_VALUE;
		settings.ReplaceInt32(kSleepFields[i], number);
	}
	const char* value;
	if (request.FindString(R5_POWER_LID_ACTION, &value) == B_OK) {
		if (!valid_action(value, kLidActions)
			|| (!sleepWorks && strcmp(value, "sleep") == 0))
			return B_BAD_VALUE;
		settings.ReplaceString(R5_POWER_LID_ACTION, value);
	}
	if (request.FindString(R5_POWER_BUTTON_ACTION, &value) == B_OK) {
		if (!valid_action(value, kButtonActions)
			|| (!sleepWorks && strcmp(value, "sleep") == 0))
			return B_BAD_VALUE;
		settings.ReplaceString(R5_POWER_BUTTON_ACTION, value);
	}
	bool flag;
	for (int32 i = 0; kBoolFields[i] != NULL; i++) {
		if (request.FindBool(kBoolFields[i], &flag) == B_OK)
			settings.ReplaceBool(kBoolFields[i], flag);
	}

	status_t status = _Write(settings);
	if (status != B_OK)
		return status;

	fSettings = settings;
	syslog(LOG_INFO, "power_daemon: settings changed\n");
	_Broadcast(B_POWER_SETTINGS_CHANGED);
	if (fKnowSource)
		_ApplyProfile();
	return B_OK;
}


/*!	Writes the file next to the old one and renames it over it, so a
	reader never sees half a file.
*/
status_t
PowerPolicy::_Write(const BMessage& settings)
{
	BPath path;
	status_t status = SettingsPath(path);
	if (status != B_OK)
		return status;

	BString text("# R5 power settings (docs/POWER.md). Written by the power\n"
		"# service; change them on the Power page in Settings.\n");
	for (int32 i = 0; kProfileFields[i] != NULL; i++) {
		text << kProfileFields[i] << " "
			<< settings.GetInt32(kProfileFields[i], 1) << "\n";
	}
	for (int32 i = 0; kDisplayFields[i] != NULL; i++) {
		text << kDisplayFields[i] << " "
			<< settings.GetInt32(kDisplayFields[i], 0) << "\n";
	}
	for (int32 i = 0; kSleepFields[i] != NULL; i++) {
		text << kSleepFields[i] << " "
			<< settings.GetInt32(kSleepFields[i], 0) << "\n";
	}
	text << R5_POWER_LID_ACTION << " "
		<< settings.GetString(R5_POWER_LID_ACTION, "display_off") << "\n";
	text << R5_POWER_BUTTON_ACTION << " "
		<< settings.GetString(R5_POWER_BUTTON_ACTION, "ask") << "\n";
	for (int32 i = 0; kBoolFields[i] != NULL; i++) {
		text << kBoolFields[i] << " "
			<< (settings.GetBool(kBoolFields[i], false) ? "true" : "false")
			<< "\n";
	}

	BString temporary(path.Path());
	temporary << ".new";
	BFile file(temporary.String(), B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	ssize_t written = file.Write(text.String(), text.Length());
	if (written != text.Length()) {
		unlink(temporary.String());
		return written < 0 ? (status_t)written : B_IO_ERROR;
	}
	file.Sync();
	file.Unset();
	chmod(temporary.String(), 0644);
	if (rename(temporary.String(), path.Path()) != 0) {
		status = errno;
		unlink(temporary.String());
		return status;
	}
	return B_OK;
}
// SPDX-SnippetEnd


void
PowerPolicy::SetOnAC(bool onAC)
{
	BAutolock _(fLock);

	bool changed = !fKnowSource || onAC != fOnAC;
	fOnAC = onAC;
	fKnowSource = true;
	if (!changed)
		return;

	printf("power_daemon: on %s\n", onAC ? "AC" : "battery");
	_ApplyProfile();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// air/OS dims the display here; in R5 that is step 3 (docs/POWER.md).
// SPDX-SnippetEnd
}


void
PowerPolicy::SetLidOpen(bool open)
{
	lid_action action;
	bool clamshell;
	{
		BAutolock _(fLock);
		bool firstTime = !fKnowLid;
		fKnowLid = true;
		if (open == !fLidClosed)
			return;
		fLidClosed = !open;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		const char* value = fSettings.GetString(R5_POWER_LID_ACTION,
			"display_off");
		if (strcmp(value, "nothing") == 0)
			action = kLidNothing;
		else if (strcmp(value, "sleep") == 0 && _SleepWorks())
			action = kLidSleep;
		else
			action = kLidDisplayOff;
		clamshell = fSettings.GetBool(R5_POWER_EXTERNAL_AWAKE, true);
// SPDX-SnippetEnd

		// Only closing it does something: a machine that starts with its
		// lid closed (on an external display) stays awake.
		if (firstTime) {
			printf("power_daemon: lid closed at start\n");
			return;
		}
	}

	printf("power_daemon: lid %s\n", open ? "opened" : "closed");
	if (!_InitGUI())
		return;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (open) {
		bool wasOff;
		{
			BAutolock _(fLock);
			wasOff = fDisplayOffByLid;
			fDisplayOffByLid = false;
		}
		if (wasOff)
			_SetDisplay(true);
		return;
	}

	if (action == kLidNothing)
		return;
	if (clamshell && _HasExternalDisplay()) {
		printf("power_daemon: lid closed on an external display, staying "
			"awake\n");
		return;
	}
	// Sleep is step 4 of docs/POWER.md: until it works, the lid turns the
	// display off.
	{
		BAutolock _(fLock);
		fDisplayOffByLid = true;
	}
	_SetDisplay(false);
// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
PowerPolicy::PowerButtonPressed()
{
	BString action;
	{
		BAutolock _(fLock);
		action = fSettings.GetString(R5_POWER_BUTTON_ACTION, "ask");
	}
	// Until sleep works (docs/POWER.md, step 4), "sleep" asks.
	if (action == "sleep" && !_SleepWorks())
		action = "ask";

	syslog(LOG_INFO, "power_daemon: power button, %s\n", action.String());
	if (action == "nothing")
		return;

	BRoster roster;
	BRoster::Private rosterPrivate(roster);
	rosterPrivate.ShutDown(false, action == "ask", false);
}


bigtime_t
PowerPolicy::TickInterval()
{
	BAutolock _(fLock);
	return fDisplayOffByTimer ? kTickWhileOff : kTickInterval;
}


/*!	The display-off timeout ("Turn off the display after"): the display
	goes off when nobody used the keyboard or the mouse for that long, and
	on again with the next input. The power source is read again, too.
*/
void
PowerPolicy::Tick()
{
	int fd = open(kACDevice, O_RDONLY);
	if (fd >= 0) {
		uint8 status;
		if (read(fd, &status, 1) == 1)
			SetOnAC(status != 0);
		close(fd);
	} else if (sRoster != NULL)
		SetOnAC(sRoster->IsOnAC());

	if (!_InitGUI())
		return;

	bigtime_t idle = idle_time();
	bool turnOn = false;
	bool turnOff = false;
	{
		BAutolock _(fLock);
		int32 minutes = fSettings.GetInt32(
			fOnAC ? R5_POWER_DISPLAY_AC : R5_POWER_DISPLAY_BATTERY, 0);
		if (fDisplayOffByTimer) {
			if (idle < kActiveIdle) {
				fDisplayOffByTimer = false;
				turnOn = !fDisplayOffByLid;
			}
		} else if (minutes > 0 && !fDisplayOffByLid
			&& idle >= (bigtime_t)minutes * 60000000LL) {
			fDisplayOffByTimer = true;
			turnOff = true;
		}
	}
	if (turnOff) {
		printf("power_daemon: idle, turning the display off\n");
		_SetDisplay(false);
	} else if (turnOn)
		_SetDisplay(true);
}


void
PowerPolicy::AddWatcher(const BMessenger& target)
{
	BAutolock _(fLock);
	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i] == target)
			return;
	}
	fWatchers.push_back(target);
}


void
PowerPolicy::RemoveWatcher(const BMessenger& target)
{
	BAutolock _(fLock);
	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i] == target) {
			fWatchers.erase(fWatchers.begin() + i);
			return;
		}
	}
}


// Called with fLock held. A watcher that is gone is dropped.
void
PowerPolicy::_Broadcast(int32 change, int32 profile)
{
	BMessage message(kMsgPowerChanged);
	message.AddInt32("change", change);
	if (profile >= 0)
		message.AddInt32("profile", profile);
	for (size_t i = 0; i < fWatchers.size();) {
		if (fWatchers[i].SendMessage(&message, (BHandler*)NULL, 100000)
				== B_BAD_PORT_ID) {
			fWatchers.erase(fWatchers.begin() + i);
			continue;
		}
		i++;
	}
}
// SPDX-SnippetEnd


void
PowerPolicy::_ApplyProfile()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// docs/POWER.md, section 4: the profile syscall sets the scheduler mode
	// and the cpufreq module's profile. Called with fLock held.
	int32 profile = fSettings.GetInt32(
		fOnAC ? R5_POWER_PROFILE_AC : R5_POWER_PROFILE_BATTERY,
		R5_POWER_PROFILE_BALANCED);
	if (profile == fProfile)
		return;

	status_t status = _kern_r5_set_power_profile(profile);
	if (status != B_OK) {
		syslog(LOG_ERR, "power_daemon: setting profile %" B_PRId32
			" failed: %s\n", profile, strerror(status));
		return;
	}
	syslog(LOG_INFO, "power_daemon: profile %" B_PRId32 " (%s)\n", profile,
		fOnAC ? "plugged in" : "on battery");
	fProfile = profile;
	_Broadcast(B_POWER_PROFILE_CHANGED, profile);
// SPDX-SnippetEnd
}


bool
PowerPolicy::_InitGUI()
{
	return fServer->InitGUIContext() == B_OK;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	Whether a second screen shows the desktop. R5's app_server has no call
	that tells the built-in panel from an external display (air/OS added
	one), so this counts the screens: with only one, there is no telling,
	and the lid action applies.
*/
bool
PowerPolicy::_HasExternalDisplay()
{
	BScreen screen(B_MAIN_SCREEN_ID);
	if (!screen.IsValid())
		return false;
	int32 count = 1;
	while (screen.SetToNext() == B_OK)
		count++;
	return count > 1;
}


void
PowerPolicy::_SetDisplay(bool on)
{
	status_t status = BScreen().SetDPMS(on ? B_DPMS_ON : B_DPMS_OFF);
	if (status != B_OK) {
		syslog(LOG_INFO, "power_daemon: turning the display %s failed: %s\n",
			on ? "on" : "off", strerror(status));
	}
}
// SPDX-SnippetEnd
