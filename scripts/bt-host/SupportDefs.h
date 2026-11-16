/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The few Haiku types the Bluetooth kernel parsers need, for the host fuzz
 * build (scripts/fuzz-bt.sh).
 */
#ifndef _BT_HOST_SUPPORT_DEFS_H
#define _BT_HOST_SUPPORT_DEFS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int32_t status_t;
typedef int32_t int32;
typedef int8_t int8;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int64_t int64;
typedef int64_t bigtime_t;

#define B_OK			0
#define B_NO_MEMORY		((status_t)0x80000000)
#define B_BAD_VALUE		((status_t)0x80000005)

#endif
