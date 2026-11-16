/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 */


#include "AudioSettings.h"

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>


static const char* kSessionFile = "/boot/system/settings/r5-desktop-session";
static const char* kRelativePath = "Media/bluetooth_audio";


status_t
bluetooth_audio_own_settings_path(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	return path.Append(kRelativePath);
}


status_t
bluetooth_audio_session_settings_path(BPath& path)
{
	if (geteuid() == 0) {
		FILE* file = fopen(kSessionFile, "r");
		if (file != NULL) {
			char name[64];
			name[0] = '\0';
			bool read = fgets(name, sizeof(name), file) != NULL;
			fclose(file);
			size_t length = read ? strlen(name) : 0;
			while (length > 0 && (name[length - 1] == '\n'
					|| name[length - 1] == '\r' || name[length - 1] == ' '))
				name[--length] = '\0';
			struct passwd* account = length > 0 ? getpwnam(name) : NULL;
			if (account != NULL && account->pw_dir != NULL
				&& account->pw_dir[0] == '/') {
				status_t status = path.SetTo(account->pw_dir,
					"config/settings");
				if (status == B_OK)
					status = path.Append(kRelativePath);
				return status;
			}
		}
	}
	return bluetooth_audio_own_settings_path(path);
}


status_t
bluetooth_audio_read_device(const BPath& path, bdaddr_t* device)
{
	BFile file(path.Path(), B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	// A settings file is small; a large one is not this file.
	off_t size;
	if (file.GetSize(&size) != B_OK || size > 4096)
		return B_BAD_DATA;

	BMessage settings;
	status = settings.Unflatten(&file);
	if (status != B_OK)
		return status;

	const void* data;
	ssize_t length;
	if (settings.FindData("device", B_RAW_TYPE, &data, &length) != B_OK)
		return B_ENTRY_NOT_FOUND;
	if (data == NULL || length != (ssize_t)sizeof(bdaddr_t))
		return B_BAD_DATA;
	memcpy(device, data, sizeof(bdaddr_t));
	return B_OK;
}


status_t
bluetooth_audio_write_device(const BPath& path, const bdaddr_t* device)
{
	BPath parent;
	status_t status = path.GetParent(&parent);
	if (status != B_OK)
		return status;
	status = create_directory(parent.Path(), 0755);
	if (status != B_OK)
		return status;

	BMessage settings;
	if (device != NULL)
		settings.AddData("device", B_RAW_TYPE, device, sizeof(bdaddr_t));

	BPath temporary(parent.Path(), "bluetooth_audio.new");
	BFile file(temporary.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status == B_OK)
		status = settings.Flatten(&file);
	if (status == B_OK)
		status = file.Sync();
	file.Unset();
	if (status == B_OK && rename(temporary.Path(), path.Path()) != 0)
		status = B_ERROR;
	if (status != B_OK)
		unlink(temporary.Path());
	return status;
}
