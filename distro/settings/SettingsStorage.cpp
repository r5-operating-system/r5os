/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "SettingsStorage.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <MallocIO.h>
#include <Message.h>
#include <Path.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


SettingsStorage::SettingsStorage()
{
}


bool
SettingsStorage::_ValidId(const char* moduleId) const
{
	if (moduleId == NULL || moduleId[0] == '\0')
		return false;
	size_t length = strlen(moduleId);
	if (length > 64)
		return false;
	for (size_t i = 0; i < length; i++) {
		char c = moduleId[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
		if (!ok)
			return false;
	}
	return true;
}


status_t
SettingsStorage::_PathFor(const char* moduleId, char* path,
	size_t pathLength) const
{
	if (!_ValidId(moduleId))
		return B_BAD_VALUE;

	BPath settings;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &settings,
		true);
	if (status != B_OK)
		return status;
	status = settings.Append("r5-settings");
	if (status != B_OK)
		return status;
	status = create_directory(settings.Path(), 0755);
	if (status != B_OK)
		return status;
	status = settings.Append(moduleId);
	if (status != B_OK)
		return status;
	if (strlen(settings.Path()) + 1 > pathLength)
		return B_NAME_TOO_LONG;
	strlcpy(path, settings.Path(), pathLength);
	return B_OK;
}


status_t
SettingsStorage::Read(const char* moduleId, BMessage& settings) const
{
	char path[B_PATH_NAME_LENGTH];
	status_t status = _PathFor(moduleId, path, sizeof(path));
	if (status != B_OK)
		return status;

	BFile file(path, B_READ_ONLY);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return settings.Unflatten(&file);
}


status_t
SettingsStorage::Write(const char* moduleId, const BMessage& settings) const
{
	char path[B_PATH_NAME_LENGTH];
	status_t status = _PathFor(moduleId, path, sizeof(path));
	if (status != B_OK)
		return status;

	BMallocIO io;
	status = settings.Flatten(&io);
	if (status != B_OK)
		return status;

	char tempPath[B_PATH_NAME_LENGTH];
	if (snprintf(tempPath, sizeof(tempPath), "%s.tmp", path)
			>= (int)sizeof(tempPath))
		return B_NAME_TOO_LONG;
	int fd = open(tempPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return errno != 0 ? errno : B_ERROR;
	ssize_t written = write(fd, io.Buffer(), io.BufferLength());
	if (written != (ssize_t)io.BufferLength())
		status = written < 0 && errno != 0 ? errno : B_ERROR;
	else if (fsync(fd) != 0)
		status = errno != 0 ? errno : B_ERROR;
	close(fd);
	if (status == B_OK && rename(tempPath, path) != 0)
		status = errno != 0 ? errno : B_ERROR;
	if (status != B_OK)
		unlink(tempPath);
	return status;
}
