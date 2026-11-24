/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Minimal Haiku SupportDefs.h compatibility header for host unit tests only.
 */
#ifndef _SUPPORT_DEFS_H
#define _SUPPORT_DEFS_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32_t status_t;

#ifndef B_PRIu32
#define B_PRIu32 PRIu32
#endif

enum {
	B_OK = 0,
	B_ERROR = -1,
	B_NO_MEMORY = -2147483647,
	B_IO_ERROR = -2147483646,
	B_BAD_VALUE = -2147483643,
	B_NOT_ALLOWED = -2147483633,
	B_BAD_DATA = -2147483632,
	B_BUFFER_OVERFLOW = -2147483629,
	B_ENTRY_NOT_FOUND = -2147459069
};

#endif
