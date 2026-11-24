/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_LOCAL_FILE_H
#define ONLINE_STORAGE_LOCAL_FILE_H

#include <String.h>

#include <stddef.h>


status_t		LocalFileSize(const char* path, off_t& size);
status_t		LocalRead(const char* path, off_t offset, void* buffer,
					size_t want, size_t& got);
status_t		LocalWriteAtomic(const char* path, const void* data,
					size_t length);
status_t		LocalMakeParents(const char* filePath);
status_t		LocalRename(const char* from, const char* to);
status_t		LocalRemoveFile(const char* path);
status_t		LocalRemoveEmptyDir(const char* path);
bool			LocalIsDirectory(const char* path);
bool			LocalExists(const char* path);

#endif
