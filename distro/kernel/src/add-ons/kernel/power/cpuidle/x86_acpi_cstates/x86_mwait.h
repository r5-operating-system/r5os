/*
 * Copyright 2012, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */


#ifdef __cplusplus
extern "C" {
#endif


#define MWAIT_INTERRUPTS_BREAK		(1 << 0)


static inline void
x86_monitor(void* address, uint32 ecx, uint32 edx)
{
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// "memory": the load of the monitored word must stay after MONITOR.
	// MONITOR itself is ordered as a load (Intel SDM Vol. 2, MONITOR).
	asm volatile("monitor" : : "a" (address), "c" (ecx), "d"(edx) : "memory");
	// SPDX-SnippetEnd
}


static inline void
x86_mwait(uint32 eax, uint32 ecx)
{
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// "memory": clearing mwait_polling must stay after MWAIT.
	asm volatile("mwait" : : "a" (eax), "c" (ecx) : "memory");
	// SPDX-SnippetEnd
}


#ifdef __cplusplus
}
#endif
