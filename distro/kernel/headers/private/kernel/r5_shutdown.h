/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * R5 shutdown hooks: a driver stops its hardware before the machine
 * restarts or powers off. system_shutdown() calls them after sync(),
 * newest first, with interrupts on (a hook may snooze). At most 8.
 */
#ifndef _KERNEL_R5_SHUTDOWN_H
#define _KERNEL_R5_SHUTDOWN_H


#include <OS.h>


typedef void (*r5_shutdown_hook)(bool reboot, void* cookie);


#ifdef __cplusplus
extern "C" {
#endif

status_t r5_register_shutdown_hook(r5_shutdown_hook hook, void* cookie);
status_t r5_unregister_shutdown_hook(r5_shutdown_hook hook, void* cookie);
void r5_shutdown_trail(const char* step, bool reset);
void r5_shutdown_log_previous(void);

#ifdef __cplusplus
}
#endif


#endif	/* _KERNEL_R5_SHUTDOWN_H */
