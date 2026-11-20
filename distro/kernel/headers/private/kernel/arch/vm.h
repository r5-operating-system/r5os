/*
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef KERNEL_ARCH_VM_H
#define KERNEL_ARCH_VM_H


#include <arch_vm.h>

#include <OS.h>


struct kernel_args;
struct VMAddressSpace;
struct VMArea;


#ifdef __cplusplus
extern "C" {
#endif

status_t arch_vm_init(struct kernel_args *args);
status_t arch_vm_init_post_area(struct kernel_args *args);
status_t arch_vm_init_end(struct kernel_args *args);
status_t arch_vm_init_post_modules(struct kernel_args *args);
void arch_vm_aspace_swap(struct VMAddressSpace *from,
	struct VMAddressSpace *to);
bool arch_vm_supports_protection(team_id team, uint32 protection);

status_t arch_vm_set_memory_type(struct VMArea *area, phys_addr_t physicalBase,
	uint32 type, uint32 *effectiveType);
void arch_vm_unset_memory_type(struct VMArea *area);
/* SPDX-SnippetBegin
 * SPDX-License-Identifier: MIT
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * After the page tables change, flush the lines. AMD64 uses clflush
 * when PAT is on. A type the page tables cannot name is refused.
 * Other architectures keep the empty functions in vm.cpp.
 */
bool arch_vm_page_memory_type_supported(uint32 type);
void arch_vm_flush_memory_type_range(addr_t address, size_t bytes);
/* The AMD64 physical map of a write-combined RAM page is changed for
 * the life of that mapping and restored on the last release. Other
 * architectures leave the cookie empty and do nothing.
 */
status_t arch_vm_direct_map_prepare(size_t bytes, uint32 tables, void** cookie);
status_t arch_vm_direct_map_note(struct VMArea* area, void* cookie);
status_t arch_vm_direct_map_apply(struct VMArea* area, uint32 type,
	uint32 oldType, void* cookie);
void arch_vm_direct_map_finish(void* cookie);
void arch_vm_direct_map_drain(void);
void arch_vm_direct_map_flush(void* cookie);
void arch_vm_direct_map_release(struct VMArea* area);
void arch_vm_direct_map_release_range(struct VMArea* area, addr_t base,
	size_t size);
uint32 r5_wc_count_regions(struct VMArea* area, void* bits, size_t bitBytes);
status_t r5_wc_direct_map_selftest(int rounds);
/* SPDX-SnippetEnd */

#ifdef __cplusplus
}
#endif

#endif	/* KERNEL_ARCH_VM_H */
