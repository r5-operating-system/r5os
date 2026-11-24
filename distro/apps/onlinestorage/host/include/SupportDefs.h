/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host stand-in so the engine tests can run on the build machine.
 * The Haiku build compiles the same sources against the real headers.
 */

#ifndef _SUPPORT_DEFS_H
#define _SUPPORT_DEFS_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef int32 status_t;
typedef uint32 type_code;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef int64 bigtime_t;

#define B_PRId32 "d"
#define B_PRIu32 "u"

#define B_GENERAL_ERROR_BASE INT_MIN
#define B_STORAGE_ERROR_BASE (B_GENERAL_ERROR_BASE + 0x6000)
#define B_OK ((int)0)
#define B_ERROR (-1)
#define B_NO_MEMORY (B_GENERAL_ERROR_BASE + 0)
#define B_IO_ERROR (B_GENERAL_ERROR_BASE + 1)
#define B_PERMISSION_DENIED (B_GENERAL_ERROR_BASE + 2)
#define B_BAD_INDEX (B_GENERAL_ERROR_BASE + 3)
#define B_BAD_TYPE (B_GENERAL_ERROR_BASE + 4)
#define B_BAD_VALUE (B_GENERAL_ERROR_BASE + 5)
#define B_NAME_NOT_FOUND (B_GENERAL_ERROR_BASE + 7)
#define B_TIMED_OUT (B_GENERAL_ERROR_BASE + 9)
#define B_CANCELED (B_GENERAL_ERROR_BASE + 12)
#define B_BUSY (B_GENERAL_ERROR_BASE + 14)
#define B_NOT_ALLOWED (B_GENERAL_ERROR_BASE + 15)
#define B_BAD_DATA (B_GENERAL_ERROR_BASE + 16)
#define B_FILE_EXISTS (B_STORAGE_ERROR_BASE + 2)
#define B_ENTRY_NOT_FOUND (B_STORAGE_ERROR_BASE + 3)

/* Same packing GCC gives the Haiku four-character constants. */
enum {
	B_ANY_TYPE = 0x414E5954,
	B_STRING_TYPE = 0x43535452,
	B_INT64_TYPE = 0x4C4C4E47,
	B_BOOL_TYPE = 0x424F4F4C,
	B_DOUBLE_TYPE = 0x44424C45,
	B_POINTER_TYPE = 0x504E5452,
	B_MESSAGE_TYPE = 0x4D534747
};

#define B_JSON_MESSAGE_WHAT_OBJECT 0x5F4A544D
#define B_JSON_MESSAGE_WHAT_ARRAY 0x5F4A5441

#endif
