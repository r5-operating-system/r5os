/*
 * Copyright 2009, Michael Lotz, mmlr@mlotz.ch.
 * Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <locks.h>

#include <stdlib.h>
#include <string.h>

#include <syscalls.h>
#include <user_mutex_defs.h>


#if defined(__x86_64__)
#include <stdint.h>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static inline void
r5_pause(void)
{
	__asm__ __volatile__("pause");
}


static inline uint64_t
r5_rdtsc(void)
{
	uint32_t lo, hi;
	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32) | lo;
}


static bool
r5_lock_word_ready(int32* word, uint64_t budgetCycles)
{
	const uint64_t start = r5_rdtsc();
	for (;;) {
		const int32 value = atomic_get(word);
		if (value == 0 || (value & B_USER_MUTEX_DISABLED) != 0)
			return true;
		if (r5_rdtsc() - start >= budgetCycles)
			return false;
		r5_pause();
	}
}
// SPDX-SnippetEnd
#else
#define MAX_UNSUCCESSFUL_SPINS	100
#endif


extern int32 __gCPUCount;


// #pragma mark - mutex


void
__mutex_init(mutex *lock, const char *name)
{
	lock->name = name;
	lock->lock = 0;
	lock->flags = 0;
}


void
__mutex_init_etc(mutex *lock, const char *name, uint32 flags)
{
	lock->name = (flags & MUTEX_FLAG_CLONE_NAME) != 0 ? strdup(name) : name;
	lock->lock = 0;
	lock->flags = flags;

	if (__gCPUCount < 2)
		lock->flags &= ~uint32(MUTEX_FLAG_ADAPTIVE);
}


void
__mutex_destroy(mutex *lock)
{
	if ((lock->flags & MUTEX_FLAG_CLONE_NAME) != 0)
		free(const_cast<char*>(lock->name));
}


status_t
__mutex_lock(mutex *lock)
{
#if defined(__x86_64__)
	int32 oldValue = atomic_test_and_set(&lock->lock, B_USER_MUTEX_LOCKED, 0);
	if (oldValue == 0 || (oldValue & B_USER_MUTEX_DISABLED) != 0)
		return B_OK;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Adaptive locks wait longer. The bound is TSC cycles, not a loop of
	// pause, so a slow pause does not stretch the wait.
	const uint64_t budget
		= (lock->flags & MUTEX_FLAG_ADAPTIVE) != 0 ? 8000 : 2000;
	if (r5_lock_word_ready(&lock->lock, budget)) {
		oldValue = atomic_test_and_set(&lock->lock, B_USER_MUTEX_LOCKED, 0);
		if (oldValue == 0 || (oldValue & B_USER_MUTEX_DISABLED) != 0)
			return B_OK;
	}
	// SPDX-SnippetEnd
#else
	uint32 count = 0;
	const uint32 kMaxCount
		= (lock->flags & MUTEX_FLAG_ADAPTIVE) != 0 ? MAX_UNSUCCESSFUL_SPINS : 1;

	int32 oldValue;
	do {
		oldValue = atomic_test_and_set(&lock->lock, B_USER_MUTEX_LOCKED, 0);
		if (oldValue == 0 || (oldValue & B_USER_MUTEX_DISABLED) != 0)
			return B_OK;
	} while (count++ < kMaxCount && (oldValue & B_USER_MUTEX_WAITING) != 0);
#endif

	// we have to call the kernel
	status_t error;
	do {
		error = _kern_mutex_lock(&lock->lock, lock->name, 0, 0);
	} while (error == B_INTERRUPTED);

	return error;
}


void
__mutex_unlock(mutex *lock)
{
	// clear the locked flag
	int32 oldValue = atomic_and(&lock->lock, ~(int32)B_USER_MUTEX_LOCKED);
	if ((oldValue & B_USER_MUTEX_WAITING) != 0
			&& (oldValue & B_USER_MUTEX_DISABLED) == 0) {
		_kern_mutex_unblock(&lock->lock, 0);
	}

	if ((oldValue & B_USER_MUTEX_LOCKED) == 0)
		debugger("mutex was not actually locked!");
}
