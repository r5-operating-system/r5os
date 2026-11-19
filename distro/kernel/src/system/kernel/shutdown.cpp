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
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <lock.h>
#include <team.h>
#include <util/AutoLock.h>
#include <r5_shutdown.h>


static const char* kShutdownTrailPath = "/boot/system/var/log/r5-shutdown-trail";


void
r5_shutdown_trail(const char* step, bool reset)
{
	if (step == NULL || step[0] == '\0')
		return;
	dprintf("r5 shutdown: %s\n", step);
	if (!are_interrupts_enabled())
		return;

	int flags = O_WRONLY | O_CREAT | (reset ? O_TRUNC : O_APPEND);
	int fd = _kern_open(-1, kShutdownTrailPath, flags, 0644);
	if (fd < 0)
		return;

	char line[128];
	int len = snprintf(line, sizeof(line), "%s\n", step);
	if (len > 0) {
		if ((size_t)len >= sizeof(line))
			len = (int)sizeof(line) - 1;
		_kern_write(fd, -1, line, (size_t)len);
		_kern_fsync(fd, false);
	}
	_kern_close(fd);
}


void
r5_shutdown_log_previous(void)
{
	int fd = _kern_open(-1, kShutdownTrailPath, O_RDONLY, 0);
	if (fd < 0)
		return;

	char raw[512];
	ssize_t got = _kern_read(fd, 0, raw, sizeof(raw) - 1);
	_kern_close(fd);
	_kern_unlink(-1, kShutdownTrailPath);

	if (got <= 0)
		return;
	raw[got] = '\0';

	char summary[600];
	size_t out = 0;
	bool needSep = false;
	for (ssize_t i = 0; i < got && out + 5 < sizeof(summary); i++) {
		char c = raw[i];
		if (c == '\r')
			continue;
		if (c == '\n') {
			needSep = (out > 0);
			continue;
		}
		if (needSep) {
			summary[out++] = ' ';
			summary[out++] = '-';
			summary[out++] = '>';
			summary[out++] = ' ';
			needSep = false;
		}
		summary[out++] = c;
	}
	summary[out] = '\0';
	if (out > 0)
		dprintf("r5 previous shutdown: %s\n", summary);
}


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
	gKernelShutdown = true;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	r5_shutdown_trail(reboot ? "enter reboot=1" : "enter reboot=0", true);
// SPDX-SnippetEnd

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Restart used to kill_team() every other team, then sync(). kill_team()
	// only signals the team's main thread and does not wait, so sync() walked
	// vnodes and the block cache while those teams were tearing them down.
	// The next boot's trail stopped at "teams_killed": sync() never returned.
	// Power off never killed teams and did reach the end. Both paths now
	// sync, run the shutdown hooks, then reset or power off. A team is not
	// killed: a dying app can take a lock sync() needs, or touch a device
	// the hook just stopped.
	// SPDX-SnippetEnd

	sync();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	r5_shutdown_trail("sync_done", false);
// SPDX-SnippetEnd

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	r5_shutdown_trail("hooks_start", false);
	call_shutdown_hooks(reboot);
	r5_shutdown_trail("hooks_done", false);
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

