/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "LocalFile.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif


status_t
LocalFileSize(const char* path, off_t& size)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return B_ENTRY_NOT_FOUND;
	if (!S_ISREG(st.st_mode))
		return B_BAD_VALUE;
	size = (off_t)st.st_size;
	return B_OK;
}


status_t
LocalRead(const char* path, off_t offset, void* buffer, size_t want,
	size_t& got)
{
	got = 0;
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;
	if (fseeko(file, offset, SEEK_SET) != 0) {
		fclose(file);
		return B_IO_ERROR;
	}
	got = fread(buffer, 1, want, file);
	int error = ferror(file);
	fclose(file);
	if (error)
		return B_IO_ERROR;
	return B_OK;
}


status_t
LocalWriteAtomic(const char* path, const void* data, size_t length)
{
	status_t status = LocalMakeParents(path);
	if (status != B_OK)
		return status;
	BString temp;
	temp << path << ".r5partial";
	FILE* file = fopen(temp.String(), "wb");
	if (file == NULL)
		return B_IO_ERROR;
	size_t wrote = 0;
	if (length > 0)
		wrote = fwrite(data, 1, length, file);
	if (fflush(file) != 0) {
		fclose(file);
		remove(temp.String());
		return B_IO_ERROR;
	}
	fclose(file);
	if (wrote != length) {
		remove(temp.String());
		return B_IO_ERROR;
	}
	if (rename(temp.String(), path) != 0) {
		remove(temp.String());
		return B_IO_ERROR;
	}
	return B_OK;
}


status_t
LocalMakeParents(const char* filePath)
{
	BString path;
	path.SetTo(filePath);
	int32 slash = path.FindLast("/");
	if (slash <= 0)
		return B_OK;
	path.Truncate(slash);
	BString built;
	const char* cursor = path.String();
	if (*cursor == '/') {
		built << "/";
		cursor++;
	}
	while (*cursor != '\0') {
		const char* next = strchr(cursor, '/');
		size_t length = next != NULL ? (size_t)(next - cursor) : strlen(cursor);
		if (built.Length() > 1 && built.ByteAt(built.Length() - 1) != '/')
			built << "/";
		built.Append(cursor, (int32)length);
		if (!LocalExists(built.String())) {
#ifdef _WIN32
			if (_mkdir(built.String()) != 0 && errno != EEXIST)
				return B_IO_ERROR;
#else
			if (mkdir(built.String(), 0755) != 0 && errno != EEXIST)
				return B_IO_ERROR;
#endif
		}
		if (next == NULL)
			break;
		cursor = next + 1;
	}
	return B_OK;
}


status_t
LocalRename(const char* from, const char* to)
{
	status_t status = LocalMakeParents(to);
	if (status != B_OK)
		return status;
	if (rename(from, to) != 0)
		return B_IO_ERROR;
	return B_OK;
}


status_t
LocalRemoveFile(const char* path)
{
	if (unlink(path) != 0 && errno != ENOENT)
		return B_IO_ERROR;
	return B_OK;
}


status_t
LocalRemoveEmptyDir(const char* path)
{
	if (rmdir(path) != 0 && errno != ENOENT)
		return B_IO_ERROR;
	return B_OK;
}


bool
LocalIsDirectory(const char* path)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return false;
	return S_ISDIR(st.st_mode);
}


bool
LocalExists(const char* path)
{
	struct stat st;
	return lstat(path, &st) == 0;
}
