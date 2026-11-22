/*
 * Copyright 2016-2022 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "debug.h"

#include <string.h>

#include <boot/platform.h>
#include <boot/stage2.h>
#include <boot/stdio.h>
#include <util/ring_buffer.h>

#include "efi_platform.h"
#include "serial.h"


static char sBuffer[16384];
static uint32 sBufferPosition;

static ring_buffer* sDebugSyslogBuffer = NULL;


static void
syslog_write(const char* buffer, size_t length)
{
	if (sDebugSyslogBuffer != NULL) {
		ring_buffer_write(sDebugSyslogBuffer, (const uint8*)buffer, length);
	} else if (sBufferPosition + length < sizeof(sBuffer)) {
		memcpy(sBuffer + sBufferPosition, buffer, length);
		sBufferPosition += length;
	}
}


static void
dprintf_args(const char *format, va_list args)
{
	char buffer[512];
	int length = vsnprintf(buffer, sizeof(buffer), format, args);
	if (length == 0)
		return;

	if (length >= (int)sizeof(buffer))
		length = sizeof(buffer) - 1;

	syslog_write(buffer, length);
	serial_puts(buffer, length);
}


extern "C" void
dprintf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	dprintf_args(format, args);
	va_end(args);
}


extern "C" void
panic(const char *format, ...)
{
	va_list args;

	platform_switch_to_text_mode();

	puts("*** PANIC ***");

	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	while (true)
		kBootServices->Stall(1000000);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static const char* const kDebugSyslogSignature = "Haiku Debug Syslog\n";
static void* sDebugSyslogBase = NULL;
// SPDX-SnippetEnd


static void
allocate_ring_buffer(void)
{
	// allocate 1 MB memory
	void* buffer = NULL;
	size_t size = 1024 * 1024;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Match bios_ia32: try the fixed 63 MiB physical address first so a
	// warm reboot can recover the previous boot's debug syslog (including
	// any KDL panic message and stack trace). Fall back to any address if
	// 63 MiB is already occupied by firmware.
	bool fixed = platform_allocate_region_at(&buffer, size,
		63 * 1024 * 1024) == B_OK;
	if (!fixed && platform_allocate_region(&buffer, size, 0) != B_OK)
		return;

	sDebugSyslogBase = buffer;

	size_t signatureLength = strlen(kDebugSyslogSignature);
	bool recover = fixed
		&& memcmp(buffer, kDebugSyslogSignature, signatureLength) == 0;

	size_t headerSize = ROUNDUP(signatureLength, sizeof(void*));
	size -= headerSize;
	buffer = (uint8*)buffer + headerSize;

	sDebugSyslogBuffer = create_ring_buffer_etc(buffer, size,
		recover ? RING_BUFFER_INIT_FROM_BUFFER : 0);
	// SPDX-SnippetEnd

	gKernelArgs.debug_output = sDebugSyslogBuffer;
	gKernelArgs.debug_size = sDebugSyslogBuffer->size;
}


void
debug_cleanup(void)
{
	allocate_ring_buffer();

	if (sDebugSyslogBuffer != NULL) {
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Recover the previous boot's syslog ring buffer if present.
		size_t bytesReadable = 0;
		if (gKernelArgs.previous_debug_size != 0) {
			bytesReadable = ring_buffer_readable(sDebugSyslogBuffer);
			gKernelArgs.previous_debug_size = bytesReadable;
		}
		if (bytesReadable != 0) {
			if (uint8* buffer = (uint8*)kernel_args_malloc(bytesReadable)) {
				ring_buffer_read(sDebugSyslogBuffer, buffer, bytesReadable);
				gKernelArgs.previous_debug_output = buffer;
			} else
				gKernelArgs.previous_debug_size = 0;
		}

		size_t signatureLength = strlen(kDebugSyslogSignature);
		if (gKernelArgs.keep_debug_output_buffer) {
			// copy the output gathered so far into the ring buffer
			ring_buffer_clear(sDebugSyslogBuffer);
			ring_buffer_write(sDebugSyslogBuffer, (uint8*)sBuffer,
				sBufferPosition);
			if (sDebugSyslogBase != NULL)
				memcpy(sDebugSyslogBase, kDebugSyslogSignature,
					signatureLength);
		} else if (sDebugSyslogBase != NULL) {
			memset(sDebugSyslogBase, 0, signatureLength);
		}
		// SPDX-SnippetEnd
	} else {
		gKernelArgs.keep_debug_output_buffer = false;
	}

	if (!gKernelArgs.keep_debug_output_buffer) {
		gKernelArgs.debug_output = kernel_args_malloc(sBufferPosition);
		if (gKernelArgs.debug_output != NULL) {
			memcpy(gKernelArgs.debug_output, sBuffer, sBufferPosition);
			gKernelArgs.debug_size = sBufferPosition;
		}
	}
}


char*
platform_debug_get_log_buffer(size_t *_size)
{
	if (_size != NULL)
		*_size = sizeof(sBuffer);

	return sBuffer;
}
