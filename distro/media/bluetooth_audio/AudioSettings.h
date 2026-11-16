/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The audio output choice of one person: which headset bluetooth_audio
 * plays to, in ~/config/settings/Media/bluetooth_audio (docs/BLUETOOTH.md,
 * section 4, choice 2). A flattened BMessage with data "device" (bdaddr_t).
 *
 * The add-on runs in media_addon_server, a system service (uid 0), where
 * find_directory(B_USER_SETTINGS_DIRECTORY) would always give the
 * administrator's settings. It reads the file of the person who signed in
 * (Login names that account in /boot/system/settings/r5-desktop-session,
 * as the registrar uses it), and never creates or writes it: the person's
 * own programs (bt_audio, later the Bluetooth page) write it with
 * find_directory().
 */
#ifndef R5_BLUETOOTH_AUDIO_SETTINGS_H
#define R5_BLUETOOTH_AUDIO_SETTINGS_H


#include <Path.h>

#include <bluetooth/bluetooth.h>


// The file of the calling account (find_directory), for the person's own
// programs.
status_t	bluetooth_audio_own_settings_path(BPath& path);
// The file of the person signed in now; for the add-on (uid 0). Falls back
// to find_directory() when nobody is signed in.
status_t	bluetooth_audio_session_settings_path(BPath& path);

// Reads "device" from the file at path. B_ENTRY_NOT_FOUND: no choice.
status_t	bluetooth_audio_read_device(const BPath& path, bdaddr_t* device);
// Writes or, with device NULL, clears the choice (the person's programs).
status_t	bluetooth_audio_write_device(const BPath& path,
				const bdaddr_t* device);


#endif	// R5_BLUETOOTH_AUDIO_SETTINGS_H
