/*
 * Copyright 2009, Olivier Coursière. All rights reserved.
 * Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * R5 changes (the shutdown hooks) are marked with SPDX-Snippet tags,
 * copyright 2026 R5OS Contributors, MIT.
 */


#include <kernel.h>
#include <syscalls.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <string.h>

#include <lock.h>
#include <util/AutoLock.h>
#include <r5_shutdown.h>


/* R5: a driver that leaves hardware running (a GPU's command processor,
 * its firmware rings, its interrupts) stops it here, before the machine
 * restarts or powers off. Haiku calls no driver at shutdown, and the
 * 5625U's firmware then starts with the GPU still running. */
static const int32 kMaxShutdownHooks = 8;

struct shutdown_hook {
	r5_shutdown_hook	hook;
	void*				cookie;
};

static shutdown_hook sShutdownHooks[kMaxShutdownHooks];
static mutex sShutdownHookLock = MUTEX_INITIALIZER("r5 shutdown hooks");


status_t
r5_register_shutdown_hook(r5_shutdown_hook hook, void* cookie)
{
	if (hook == NULL)
		return B_BAD_VALUE;
	MutexLocker locker(sShutdownHookLock);
	for (int32 i = 0; i < kMaxShutdownHooks; i++) {
		if (sShutdownHooks[i].hook == NULL) {
			sShutdownHooks[i].hook = hook;
			sShutdownHooks[i].cookie = cookie;
			return B_OK;
		}
	}
	return B_NO_MEMORY;
}


status_t
r5_unregister_shutdown_hook(r5_shutdown_hook hook, void* cookie)
{
	MutexLocker locker(sShutdownHookLock);
	for (int32 i = 0; i < kMaxShutdownHooks; i++) {
		if (sShutdownHooks[i].hook == hook
			&& sShutdownHooks[i].cookie == cookie) {
			sShutdownHooks[i].hook = NULL;
			sShutdownHooks[i].cookie = NULL;
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


static void
call_shutdown_hooks(bool reboot)
{
	shutdown_hook hooks[kMaxShutdownHooks];
	{
		MutexLocker locker(sShutdownHookLock);
		memcpy(hooks, sShutdownHooks, sizeof(hooks));
	}
	for (int32 i = kMaxShutdownHooks - 1; i >= 0; i--) {
		if (hooks[i].hook != NULL)
			hooks[i].hook(reboot, hooks[i].cookie);
	}
}
// SPDX-SnippetEnd


status_t
system_shutdown(bool reboot)
{
	int32 cookie = 0;
	team_info info;

	gKernelShutdown = true;

	// Now shutdown all system services!
	// TODO: Once we are sure we can shutdown the system on all hardware
	// checking reboot may not be necessary anymore.
	if (reboot) {
		while (get_next_team_info(&cookie, &info) == B_OK) {
			if (info.team == B_SYSTEM_TEAM)
				continue;
			kill_team(info.team);
		}
	}

	sync();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	call_shutdown_hooks(reboot);
// SPDX-SnippetEnd

	return arch_cpu_shutdown(reboot);
}


//	#pragma mark -


status_t
_user_shutdown(bool reboot)
{
	if (geteuid() != 0)
		return B_NOT_ALLOWED;
	return system_shutdown(reboot);
}

