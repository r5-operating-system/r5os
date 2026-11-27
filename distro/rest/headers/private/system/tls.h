/*
 * Copyright 2003-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_TLS_H
#define _KERNEL_TLS_H


#include <support/TLS.h>


#define TLS_SIZE (TLS_MAX_KEYS * sizeof(void *))
#define TLS_COMPAT_SIZE (TLS_MAX_KEYS * sizeof(uint32))

enum {
	TLS_BASE_ADDRESS_SLOT = 0,
		// contains the address of the local storage space

	TLS_THREAD_ID_SLOT,
	TLS_ERRNO_SLOT,
	TLS_ON_EXIT_THREAD_SLOT,
	TLS_USER_THREAD_SLOT,
	TLS_DYNAMIC_THREAD_VECTOR,
	TLS_MALLOC_SLOT,
	TLS_LOCALE_SLOT,

	// Note: these entries can safely be changed between
	// releases; 3rd party code always calls tls_allocate()
	// to get a free slot

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// The static-TLS snapshot this thread has copied
	// (runtime_loader, elf_tls.cpp). Not handed out by tls_allocate():
	// runtime_loader links its own tls.o, so its gNextSlot is not
	// libroot's. This index is where TLS_FIRST_FREE_SLOT used to be.
	TLS_STATIC_TLS_READY,
	// SPDX-SnippetEnd

	TLS_FIRST_FREE_SLOT
		// the first free slot for user allocations
};

#endif	/* _KERNEL_TLS_H */
