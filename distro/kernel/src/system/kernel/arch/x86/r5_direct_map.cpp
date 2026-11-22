/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * AMD64 only. A write-combined RAM mapping must not keep a write-back
 * translation of the same page in the kernel physical map (Intel SDM vol. 3,
 * memory-type aliasing; AMD APM vol. 2). This is the per-buffer path of
 * Linux set_pages_array_wc() in arch/x86/mm/pat/set_memory.c: split a 2 MiB
 * direct-map page, change those 4 KiB entries, shoot the TLB down, and
 * restore write-back on the last release. A TTM-style pool
 * (drivers/gpu/drm/ttm/ttm_pool.c) still needs that change when it fills,
 * and must not return a write-combined page to the general allocator.
 * ARM64, RISC-V and 32-bit x86 keep the empty hooks in vm.cpp.
 */


#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>

#include <arch/cpu.h>
#include <arch/vm.h>
#include <arch/x86/arch_kernel.h>
#include <smp.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>

#ifdef __x86_64__
#include "paging/64bit/paging.h"
#include "paging/64bit/X86PagingMethod64Bit.h"
#include "paging/64bit/X86PagingStructures64Bit.h"
#include "paging/64bit/X86VMTranslationMap64Bit.h"
#endif


#ifdef __x86_64__

struct r5_ref {
	phys_addr_t	page;
	uint32		count;
	uint32		type;
	uint32		state;
};

enum {
	kSlotEmpty = 0,
	kSlotLive = 1,
	kSlotTomb = 2
};

struct r5_direct_job {
	phys_addr_t*		pages;
	uint8*				created;
	uint32				capacity;
	uint32				count;
	uint32				tables;
	vm_page_reservation	reservation;
	bool				reserved;
	bool				filled;
};

static r5_ref* sRefs = NULL;
static uint32 sRefSlots = 0;
static uint32 sRefLive = 0;
static uint32 sRefReserved = 0;
static phys_addr_t* sOwned = NULL;
static uint32 sOwnedCount = 0;
static uint32 sOwnedSlots = 0;
static uint32 sOwnedReserved = 0;
static mutex sDirectLock = MUTEX_INITIALIZER("r5 direct map");


static uint32
r5_ref_index(phys_addr_t page, uint32 slots)
{
	return (uint32)(page * 0x9E3779B1u) & (slots - 1);
}


static status_t
r5_ref_account(uint32 pages, uint32 tables)
{
	if (pages > UINT32_MAX - sRefReserved)
		return B_NO_MEMORY;
	if (tables > UINT32_MAX - sOwnedReserved)
		return B_NO_MEMORY;
	sRefReserved += pages;
	sOwnedReserved += tables;
	return B_OK;
}


static void
r5_ref_unaccount(uint32 pages, uint32 tables)
{
	if (sRefReserved >= pages)
		sRefReserved -= pages;
	else
		sRefReserved = 0;
	if (sOwnedReserved >= tables)
		sOwnedReserved -= tables;
	else
		sOwnedReserved = 0;
}


/*!	The hash has to hold every live page plus every page a prepare has
 *	reserved but not inserted yet. malloc runs without sDirectLock: the
 *	caller of apply() already holds the address-space write lock, and the
 *	kernel heap takes that same lock when it grows.
 */
static status_t
r5_ensure_refs()
{
	for (;;) {
		uint32 need = 16;
		{
			MutexLocker locker(sDirectLock);
			uint64 sum = (uint64)sRefLive + (uint64)sRefReserved;
			if (sum > 0x40000000u)
				return B_NO_MEMORY;
			uint32 twice = (uint32)(sum * 2);
			if (twice > need)
				need = twice;
			if (sRefSlots >= need)
				return B_OK;
		}

		uint32 slots = 16;
		while (slots < need) {
			if (slots > 0x40000000u)
				return B_NO_MEMORY;
			slots <<= 1;
		}

		r5_ref* grown = (r5_ref*)malloc(sizeof(r5_ref) * slots);
		if (grown == NULL)
			return B_NO_MEMORY;
		memset(grown, 0, sizeof(r5_ref) * slots);

		r5_ref* retired = NULL;
		bool publish = false;
		bool done = false;
		bool retry = false;
		{
			MutexLocker locker(sDirectLock);
			uint64 sum = (uint64)sRefLive + (uint64)sRefReserved;
			if (sum > 0x40000000u) {
				retired = grown;
			} else {
				uint32 needNow = (uint32)(sum * 2);
				if (needNow < 16)
					needNow = 16;
				if (sRefSlots >= needNow) {
					retired = grown;
					done = true;
				} else if (slots < needNow) {
					retired = grown;
					retry = true;
				} else {
					for (uint32 i = 0; i < sRefSlots; i++) {
						if (sRefs[i].state != kSlotLive)
							continue;
						uint32 slot = r5_ref_index(sRefs[i].page, slots);
						while (grown[slot].state == kSlotLive)
							slot = (slot + 1) & (slots - 1);
						grown[slot] = sRefs[i];
					}
					retired = sRefs;
					sRefs = grown;
					sRefSlots = slots;
					publish = true;
				}
			}
		}
		free(retired);
		if (publish || done)
			return B_OK;
		if (!retry)
			return B_NO_MEMORY;
	}
}


static status_t
r5_ensure_owned()
{
	for (;;) {
		uint32 need = 0;
		{
			MutexLocker locker(sDirectLock);
			uint64 sum = (uint64)sOwnedCount + (uint64)sOwnedReserved;
			if (sum > 0x40000000u)
				return B_NO_MEMORY;
			need = (uint32)sum;
			if (sOwnedSlots >= need)
				return B_OK;
		}

		phys_addr_t* grown
			= (phys_addr_t*)malloc(sizeof(phys_addr_t) * need);
		if (grown == NULL)
			return B_NO_MEMORY;

		phys_addr_t* retired = NULL;
		bool publish = false;
		bool done = false;
		bool retry = false;
		{
			MutexLocker locker(sDirectLock);
			uint64 sum = (uint64)sOwnedCount + (uint64)sOwnedReserved;
			if (sum > 0x40000000u)
				retired = grown;
			else {
				uint32 needNow = (uint32)sum;
				if (sOwnedSlots >= needNow) {
					retired = grown;
					done = true;
				} else if (need < needNow) {
					retired = grown;
					retry = true;
				} else {
					if (sOwnedCount > 0) {
						memcpy(grown, sOwned,
							sizeof(phys_addr_t) * sOwnedCount);
					}
					retired = sOwned;
					sOwned = grown;
					sOwnedSlots = need;
					publish = true;
				}
			}
		}
		free(retired);
		if (publish || done)
			return B_OK;
		if (!retry)
			return B_NO_MEMORY;
	}
}


static r5_ref*
r5_ref_find(phys_addr_t page, bool insert)
{
	if (sRefSlots == 0)
		return NULL;

	uint32 mask = sRefSlots - 1;
	uint32 slot = r5_ref_index(page, sRefSlots);
	r5_ref* tomb = NULL;
	for (uint32 n = 0; n < sRefSlots; n++) {
		r5_ref* entry = &sRefs[slot];
		if (entry->state == kSlotEmpty) {
			if (!insert)
				return NULL;
			r5_ref* place = tomb != NULL ? tomb : entry;
			place->state = kSlotLive;
			place->page = page;
			place->count = 0;
			place->type = 0;
			sRefLive++;
			return place;
		}
		if (entry->state == kSlotTomb && tomb == NULL)
			tomb = entry;
		else if (entry->state == kSlotLive && entry->page == page)
			return entry;
		slot = (slot + 1) & mask;
	}

	// A full probe that never saw an empty slot still did not find the page.
	// A tombstone from an earlier free is a free slot. Leaving it unused fills
	// the table after one wave of buffers and every later insert fails.
	if (insert && tomb != NULL) {
		tomb->state = kSlotLive;
		tomb->page = page;
		tomb->count = 0;
		tomb->type = 0;
		sRefLive++;
		return tomb;
	}
	return NULL;
}


static void
r5_ref_drop_slot(r5_ref* entry)
{
	entry->state = kSlotTomb;
	if (sRefLive > 0)
		sRefLive--;
}


static bool
r5_owned_has(phys_addr_t table)
{
	for (uint32 i = 0; i < sOwnedCount; i++) {
		if (sOwned[i] == table)
			return true;
	}
	return false;
}


static void
r5_owned_add(phys_addr_t table)
{
	if (sOwnedCount < sOwnedSlots)
		sOwned[sOwnedCount++] = table;
}


static void
r5_owned_remove(phys_addr_t table)
{
	for (uint32 i = 0; i < sOwnedCount; i++) {
		if (sOwned[i] != table)
			continue;
		sOwned[i] = sOwned[sOwnedCount - 1];
		sOwnedCount--;
		return;
	}
}


static void
r5_direct_shootdown()
{
	smp_broadcast_ici(SMP_MSG_GLOBAL_INVALIDATE_PAGES, 0, 0, 0, NULL,
		SMP_MSG_FLAG_SYNC);
}


static void
r5_free_table(phys_addr_t tablePhys)
{
	vm_page* page = vm_lookup_page(tablePhys / B_PAGE_SIZE);
	if (page == NULL) {
		dprintf("R5 VM: wc direct map leaked table %#" B_PRIxPHYSADDR "\n",
			tablePhys);
		return;
	}
	vm_page_reservation freed;
	freed.count = 0;
	DEBUG_PAGE_ACCESS_START(page);
	vm_page_free_etc(NULL, page, &freed);
	vm_page_unreserve_pages(&freed);
}


static bool
r5_refuse_1gb(X86VMTranslationMap64Bit* kernelMap, addr_t va)
{
	uint64* pml4 = kernelMap->PagingStructures64Bit()->VirtualPMLTop();
	uint64 pml4e = pml4[VADDR_TO_PML4E(va)];
	if ((pml4e & X86_64_PML4E_PRESENT) == 0)
		return false;
	uint64* pdpt = (uint64*)((addr_t)KERNEL_PMAP_BASE
		+ (addr_t)(pml4e & X86_64_PML4E_ADDRESS_MASK));
	uint64 pdpte = pdpt[VADDR_TO_PDPTE(va)];
	if ((pdpte & X86_64_PDPTE_PRESENT) == 0
		|| (pdpte & X86_64_PDPTE_LARGE_PAGE) == 0) {
		return false;
	}
	static int32 sOnce = 0;
	if (atomic_add(&sOnce, 1) == 0) {
		dprintf("R5 VM: wc direct map refuses a 1 GiB physical-map page at %#"
			B_PRIxADDR "\n", va);
	}
	return true;
}


/*!	The caller holds sDirectLock. A 2 MiB direct-map page is split before one
 *	4 KiB entry changes. Write-back is restored in place, and a table this
 *	code allocated is collapsed when all 512 entries are write-back again.
 */
static status_t
r5_direct_install(phys_addr_t phys, uint32 type, vm_page_reservation* reservation,
	bool* changed)
{
	if (phys >= KERNEL_PMAP_SIZE || vm_lookup_page(phys / B_PAGE_SIZE) == NULL)
		return B_OK;

	bool toWriteBack = type == 0 || type == B_WRITE_BACK_MEMORY;
	if (toWriteBack)
		arch_vm_flush_memory_type_range((addr_t)phys, B_PAGE_SIZE);

	addr_t va = (addr_t)KERNEL_PMAP_BASE + (addr_t)phys;
	X86VMTranslationMap64Bit* kernelMap = static_cast<X86VMTranslationMap64Bit*>(
		VMAddressSpace::Kernel()->TranslationMap());
	int32 dummy = 0;
	kernelMap->Lock();
	ThreadCPUPinner pinner(thread_get_current_thread());

	if (r5_refuse_1gb(kernelMap, va)) {
		kernelMap->Unlock();
		return B_NOT_SUPPORTED;
	}

	uint64* pde = X86PagingMethod64Bit::PageDirectoryEntryForAddress(
		kernelMap->PagingStructures64Bit()->VirtualPMLTop(), va, true,
		false, NULL,
		X86PagingMethod64Bit::Method()->KernelPhysicalPageMapper(), dummy);
	if (pde == NULL || (*pde & X86_64_PDE_PRESENT) == 0) {
		kernelMap->Unlock();
		return B_OK;
	}

	uint64* table = NULL;
	if ((*pde & X86_64_PDE_LARGE_PAGE) != 0) {
		if (toWriteBack) {
			uint64 value = (*pde & ~X86_64_PDE_MEMORY_TYPE_MASK)
				| X86PagingMethod64Bit::MemoryTypeToPageDirectoryEntryFlags(
					B_WRITE_BACK_MEMORY);
			if (value != *pde) {
				X86PagingMethod64Bit::SetTableEntry(pde, value);
				*changed = true;
			}
			memory_write_barrier();
			kernelMap->Unlock();
			return B_OK;
		}
		if (reservation == NULL || reservation->count == 0
			|| sOwnedCount >= sOwnedSlots) {
			kernelMap->Unlock();
			return B_NO_MEMORY;
		}

		uint64 oldPDE = *pde;
		vm_page* page = vm_page_allocate_page(reservation,
			PAGE_STATE_WIRED | VM_PAGE_ALLOC_CLEAR);
		DEBUG_PAGE_ACCESS_END(page);
		phys_addr_t tablePhys
			= (phys_addr_t)page->physical_page_number * B_PAGE_SIZE;
		table = (uint64*)((addr_t)KERNEL_PMAP_BASE + (addr_t)tablePhys);

		phys_addr_t basePA = oldPDE & X86_64_PDE_LARGE_ADDRESS_MASK;
		uint64 pteFlags = (oldPDE & (X86_64_PTE_PRESENT | X86_64_PTE_WRITABLE
				| X86_64_PTE_USER | X86_64_PTE_WRITE_THROUGH
				| X86_64_PTE_CACHING_DISABLED | X86_64_PTE_ACCESSED
				| X86_64_PTE_DIRTY | X86_64_PTE_GLOBAL
				| X86_64_PTE_NOT_EXECUTABLE))
			| ((oldPDE & X86_64_PDE_PAT) != 0 ? X86_64_PTE_PAT : 0);
		for (uint32 i = 0; i < k64BitTableEntryCount; i++) {
			table[i] = ((basePA + (phys_addr_t)i * B_PAGE_SIZE)
					& X86_64_PTE_ADDRESS_MASK)
				| pteFlags;
		}
		memory_write_barrier();
		X86PagingMethod64Bit::SetTableEntry(pde,
			(tablePhys & X86_64_PDE_ADDRESS_MASK)
				| X86_64_PDE_PRESENT | X86_64_PDE_WRITABLE | X86_64_PDE_USER);
		memory_write_barrier();
		r5_direct_shootdown();
		r5_owned_add(tablePhys);
	} else {
		table = (uint64*)((addr_t)KERNEL_PMAP_BASE
			+ (addr_t)(*pde & X86_64_PDE_ADDRESS_MASK));
	}

	uint64* pte = &table[(va >> 12) & (k64BitTableEntryCount - 1)];
	uint64 typeFlags = X86PagingMethod64Bit::MemoryTypeToPageTableEntryFlags(
		toWriteBack ? B_WRITE_BACK_MEMORY : type);
	uint64 value = (*pte & ~X86_64_PTE_MEMORY_TYPE_MASK) | typeFlags;
	if (value != *pte) {
		X86PagingMethod64Bit::SetTableEntry(pte, value);
		*changed = true;
	}

	if (toWriteBack && r5_owned_has(*pde & X86_64_PDE_ADDRESS_MASK)) {
		phys_addr_t tablePhys = *pde & X86_64_PDE_ADDRESS_MASK;
		phys_addr_t basePA = table[0] & X86_64_PTE_ADDRESS_MASK;
		uint64 prot0 = table[0] & (X86_64_PTE_PRESENT | X86_64_PTE_WRITABLE
			| X86_64_PTE_USER | X86_64_PTE_GLOBAL | X86_64_PTE_NOT_EXECUTABLE);
		bool collapse = (table[0] & X86_64_PTE_MEMORY_TYPE_MASK) == 0
			&& (prot0 & X86_64_PTE_PRESENT) != 0;
		for (uint32 i = 0; collapse && i < k64BitTableEntryCount; i++) {
			uint64 prot = table[i] & (X86_64_PTE_PRESENT | X86_64_PTE_WRITABLE
				| X86_64_PTE_USER | X86_64_PTE_GLOBAL
				| X86_64_PTE_NOT_EXECUTABLE);
			phys_addr_t pagePA = table[i] & X86_64_PTE_ADDRESS_MASK;
			if (prot != prot0 || (table[i] & X86_64_PTE_MEMORY_TYPE_MASK) != 0
				|| pagePA != basePA + (phys_addr_t)i * B_PAGE_SIZE) {
				collapse = false;
			}
		}
		if (collapse) {
			uint64 large = (basePA & X86_64_PDE_LARGE_ADDRESS_MASK)
				| X86_64_PDE_LARGE_PAGE
				| (prot0 & ~X86_64_PTE_PAT)
				| X86_64_PDE_ACCESSED | X86_64_PDE_DIRTY;
			X86PagingMethod64Bit::SetTableEntry(pde, large);
			memory_write_barrier();
			r5_direct_shootdown();
			r5_owned_remove(tablePhys);
			r5_free_table(tablePhys);
			*changed = true;
		}
	}

	memory_write_barrier();
	kernelMap->Unlock();
	return B_OK;
}


static status_t
r5_direct_add(phys_addr_t phys, uint32 type, vm_page_reservation* reservation,
	bool* created, bool* changed)
{
	*created = false;
	if (phys >= KERNEL_PMAP_SIZE || vm_lookup_page(phys / B_PAGE_SIZE) == NULL)
		return B_OK;

	r5_ref* entry = r5_ref_find(phys / B_PAGE_SIZE, true);
	if (entry == NULL)
		return B_NO_MEMORY;
	if (entry->count > 0 && entry->type != type) {
		return B_BUSY;
	}
	if (entry->count == 0) {
		status_t status = r5_direct_install(phys, type, reservation, changed);
		if (status != B_OK) {
			r5_ref_drop_slot(entry);
			return status;
		}
		entry->type = type;
	}
	entry->count++;
	*created = true;
	return B_OK;
}


static status_t
r5_direct_drop(phys_addr_t phys, bool* created, bool* changed)
{
	*created = false;
	if (phys >= KERNEL_PMAP_SIZE || vm_lookup_page(phys / B_PAGE_SIZE) == NULL)
		return B_OK;

	r5_ref* entry = r5_ref_find(phys / B_PAGE_SIZE, false);
	if (entry == NULL || entry->count == 0)
		return B_OK;
	if (entry->count > 1) {
		entry->count--;
		*created = true;
		return B_OK;
	}

	status_t status = r5_direct_install(phys, B_WRITE_BACK_MEMORY, NULL,
		changed);
	if (status != B_OK)
		return status;
	entry->count = 0;
	r5_ref_drop_slot(entry);
	*created = true;
	return B_OK;
}


extern "C" status_t
arch_vm_direct_map_prepare(size_t bytes, uint32 tables, void** cookie)
{
	if (cookie == NULL)
		return B_BAD_VALUE;
	*cookie = NULL;
	if (bytes < B_PAGE_SIZE)
		return B_OK;

	uint32 pages = (uint32)(bytes / B_PAGE_SIZE);
	if ((size_t)pages != bytes / B_PAGE_SIZE)
		return B_NO_MEMORY;
	if (tables == 0)
		tables = 1;
	if (tables > pages)
		tables = pages;

	// `tables` is the number of 2 MiB direct-map regions the pages fall in.
	// One split consumes one page-table page. Reserving one page per buffer
	// page holds back as much RAM as the buffer.
	r5_direct_job* job = (r5_direct_job*)malloc(sizeof(r5_direct_job));
	if (job == NULL)
		return B_NO_MEMORY;
	memset(job, 0, sizeof(r5_direct_job));
	job->pages = (phys_addr_t*)malloc(sizeof(phys_addr_t) * pages);
	job->created = (uint8*)malloc(pages);
	if (job->pages == NULL || job->created == NULL) {
		free(job->pages);
		free(job->created);
		free(job);
		return B_NO_MEMORY;
	}
	memset(job->created, 0, pages);
	job->capacity = pages;
	job->count = 0;
	job->tables = tables;
	job->filled = false;
	job->reservation.count = 0;
	vm_page_reserve_pages(&job->reservation, tables, VM_PRIORITY_SYSTEM);
	job->reserved = true;

	status_t status = B_OK;
	bool accounted = false;
	{
		MutexLocker locker(sDirectLock);
		status = r5_ref_account(pages, tables);
		accounted = status == B_OK;
	}
	if (status == B_OK)
		status = r5_ensure_refs();
	if (status == B_OK)
		status = r5_ensure_owned();
	if (status != B_OK) {
		if (accounted) {
			MutexLocker locker(sDirectLock);
			r5_ref_unaccount(pages, tables);
		}
		vm_page_unreserve_pages(&job->reservation);
		free(job->pages);
		free(job->created);
		free(job);
		return status;
	}

	*cookie = job;
	return B_OK;
}


extern "C" void
arch_vm_direct_map_finish(void* cookie)
{
	r5_direct_job* job = (r5_direct_job*)cookie;
	if (job == NULL)
		return;
	{
		MutexLocker locker(sDirectLock);
		r5_ref_unaccount(job->capacity, job->tables);
	}
	if (job->reserved)
		vm_page_unreserve_pages(&job->reservation);
	free(job->pages);
	free(job->created);
	free(job);
}


static status_t
r5_direct_collect(VMArea* area, r5_direct_job* job, uint32* _count)
{
	if (area->Size() / B_PAGE_SIZE > job->capacity)
		return B_NO_MEMORY;

	VMTranslationMap* map = area->address_space->TranslationMap();
	addr_t base = area->Base();
	addr_t end = base + area->Size();
	uint32 count = 0;
	map->Lock();
	for (addr_t va = base; va < end; va += B_PAGE_SIZE) {
		phys_addr_t phys;
		uint32 flags;
		if (map->Query(va, &phys, &flags) != B_OK)
			continue;
		// Query() returns B_OK and physical 0 when the page is not present.
		if ((flags & PAGE_PRESENT) == 0)
			continue;
		if (phys >= KERNEL_PMAP_SIZE
			|| vm_lookup_page(phys / B_PAGE_SIZE) == NULL) {
			continue;
		}
		job->pages[count++] = phys;
	}
	map->Unlock();
	*_count = count;
	return B_OK;
}


extern "C" status_t
arch_vm_direct_map_note(VMArea* area, void* cookie)
{
	r5_direct_job* job = (r5_direct_job*)cookie;
	if (job == NULL || area == NULL || job->filled)
		return B_OK;
	status_t status = r5_direct_collect(area, job, &job->count);
	if (status == B_OK)
		job->filled = true;
	return status;
}


extern "C" void
arch_vm_direct_map_flush(void* cookie)
{
	r5_direct_job* job = (r5_direct_job*)cookie;
	if (job == NULL)
		return;
	for (uint32 i = 0; i < job->count; i++) {
		arch_vm_flush_memory_type_range((addr_t)job->pages[i], B_PAGE_SIZE);
	}
}


static void
r5_sfence_one(void* cookie, int cpu)
{
	(void)cookie;
	(void)cpu;
	asm volatile("sfence" ::: "memory");
}


extern "C" void
arch_vm_direct_map_drain(void)
{
	call_all_cpus_sync(&r5_sfence_one, NULL);
}


extern "C" status_t
arch_vm_direct_map_apply(VMArea* area, uint32 type, uint32 oldType, void* cookie)
{
	r5_direct_job* job = (r5_direct_job*)cookie;
	if (job == NULL || area == NULL)
		return B_OK;

	status_t status = B_OK;
	if (!job->filled) {
		status = arch_vm_direct_map_note(area, cookie);
		if (status != B_OK)
			return status;
	}
	uint32 count = job->count;

	bool adding = type != 0 && type != B_WRITE_BACK_MEMORY;
	if (!adding && (oldType == 0 || oldType == B_WRITE_BACK_MEMORY))
		return B_OK;

	MutexLocker locker(sDirectLock);
	memset(job->created, 0, count);
	bool changed = false;
	for (uint32 i = 0; i < count; i++) {
		bool created = false;
		if (adding) {
			status = r5_direct_add(job->pages[i], type,
				job->reserved ? &job->reservation : NULL, &created, &changed);
		} else {
			status = r5_direct_drop(job->pages[i], &created, &changed);
		}
		job->created[i] = created ? 1 : 0;
		if (status != B_OK) {
			for (uint32 j = 0; j < i; j++) {
				if (job->created[j] == 0)
					continue;
				bool ignored = false;
				if (adding)
					r5_direct_drop(job->pages[j], &ignored, &changed);
				else {
					r5_direct_add(job->pages[j], oldType,
						job->reserved ? &job->reservation : NULL, &ignored,
						&changed);
				}
			}
			if (changed)
				r5_direct_shootdown();
			return status;
		}
	}
	if (changed)
		r5_direct_shootdown();
	return B_OK;
}


extern "C" void
arch_vm_direct_map_release_range(VMArea* area, addr_t base, size_t size)
{
	if (area == NULL || size == 0)
		return;
	// A framebuffer or an MMIO area is device memory. Its pages are not in
	// the physical map, so unmapping it must not broadcast sfence or shoot
	// the TLB down. The caller unmaps it. The same is true of a null cache.
	if (area->cache_type == CACHE_TYPE_DEVICE
		|| area->cache_type == CACHE_TYPE_NULL) {
		return;
	}
	uint32 type = area->MemoryType();
	if (type == 0 || type == B_WRITE_BACK_MEMORY)
		return;

	VMTranslationMap* map = area->address_space->TranslationMap();
	addr_t end = base + size;
	static int32 sReleaseFail = 0;
	// Static 1024-page (4 MiB) scratch buffer avoids heap allocation while the
	// address-space lock is held and collapses a 4 MiB WC release from 96
	// global SMP IPIs (32-page batches) down to 1 unmap + 1 drain + 1 shootdown.
	static mutex sReleaseBufLock = MUTEX_INITIALIZER("r5 wc release buf");
	static const uint32 kReleaseBatch = 1024;
	static phys_addr_t sReleaseBatch[kReleaseBatch];

	MutexLocker bufLocker(sReleaseBufLock);
	for (addr_t va = base; va < end; ) {
		addr_t firstVa = 0;
		addr_t lastVa = 0;
		uint32 count = 0;
		map->Lock();
		for (; count < kReleaseBatch && va < end; va += B_PAGE_SIZE) {
			phys_addr_t phys;
			uint32 flags;
			if (map->Query(va, &phys, &flags) != B_OK)
				continue;
			if ((flags & PAGE_PRESENT) == 0)
				continue;
			if (count == 0)
				firstVa = va;
			lastVa = va;
			sReleaseBatch[count] = phys;
			count++;
		}
		map->Unlock();
		if (count == 0)
			continue;

		// The write-combined area entries go away, every CPU drains its
		// write-combining buffers, and only then does this batch return to
		// write-back. UnmapPages clears PTEs that are no longer present on
		// a second call, so delete_area's later UnmapArea does not free them
		// twice.
		map->UnmapPages(area, firstVa, (lastVa + B_PAGE_SIZE) - firstVa, true);
		arch_vm_direct_map_drain();

		MutexLocker locker(sDirectLock);
		bool changed = false;
		for (uint32 i = 0; i < count; i++) {
			bool created = false;
			status_t status = r5_direct_drop(sReleaseBatch[i], &created,
				&changed);
			if (status != B_OK && atomic_add(&sReleaseFail, 1) < 4) {
				dprintf("R5 VM: wc direct map release failed %#"
					B_PRIxPHYSADDR " status 0x%" B_PRIx32 "\n",
					sReleaseBatch[i], (uint32)status);
			}
		}
		if (changed)
			r5_direct_shootdown();
	}
}


extern "C" void
arch_vm_direct_map_release(VMArea* area)
{
	if (area == NULL)
		return;
	arch_vm_direct_map_release_range(area, area->Base(), area->Size());
}


extern "C" uint32
r5_wc_count_regions(VMArea* area, void* bits, size_t bitBytes)
{
	if (area == NULL || bits == NULL || bitBytes == 0)
		return 1;
	memset(bits, 0, bitBytes);
	const phys_addr_t kRegion = 0x200000;
	uint32 limit = (uint32)(KERNEL_PMAP_SIZE / kRegion);
	uint32 bitCount = (uint32)(bitBytes * 8);
	if (bitCount < limit)
		limit = bitCount;

	VMTranslationMap* map = area->address_space->TranslationMap();
	addr_t base = area->Base();
	addr_t end = base + area->Size();
	uint32 count = 0;
	bool overflow = false;
	map->Lock();
	for (addr_t va = base; va < end; va += B_PAGE_SIZE) {
		phys_addr_t phys;
		uint32 flags;
		if (map->Query(va, &phys, &flags) != B_OK)
			continue;
		if ((flags & PAGE_PRESENT) == 0 || phys >= KERNEL_PMAP_SIZE)
			continue;
		uint32 region = (uint32)(phys / kRegion);
		if (region >= limit) {
			overflow = true;
			continue;
		}
		uint8* byte = (uint8*)bits + (region >> 3);
		uint8 mask = (uint8)(1u << (region & 7));
		if ((*byte & mask) == 0) {
			*byte |= mask;
			count++;
		}
	}
	map->Unlock();
	if (overflow)
		return (uint32)(area->Size() / B_PAGE_SIZE);
	if (count == 0)
		return 1;
	return count;
}


static status_t
r5_virt_phys(addr_t va, phys_addr_t* _phys)
{
	X86VMTranslationMap64Bit* kernelMap = static_cast<X86VMTranslationMap64Bit*>(
		VMAddressSpace::Kernel()->TranslationMap());
	uint32 flags;
	kernelMap->Lock();
	status_t status = kernelMap->Query(va, _phys, &flags);
	kernelMap->Unlock();
	return status;
}


static status_t
r5_pte_expect(addr_t va, bool wantWc)
{
	X86VMTranslationMap64Bit* kernelMap = static_cast<X86VMTranslationMap64Bit*>(
		VMAddressSpace::Kernel()->TranslationMap());
	int32 dummy = 0;
	kernelMap->Lock();
	if (r5_refuse_1gb(kernelMap, va)) {
		kernelMap->Unlock();
		return B_ERROR;
	}
	uint64* pde = X86PagingMethod64Bit::PageDirectoryEntryForAddress(
		kernelMap->PagingStructures64Bit()->VirtualPMLTop(), va, true,
		false, NULL,
		X86PagingMethod64Bit::Method()->KernelPhysicalPageMapper(), dummy);
	if (pde == NULL || (*pde & X86_64_PDE_PRESENT) == 0) {
		kernelMap->Unlock();
		return B_ERROR;
	}
	bool large = (*pde & X86_64_PDE_LARGE_PAGE) != 0;
	uint64 entry = *pde;
	if (!large) {
		uint64* table = (uint64*)((addr_t)KERNEL_PMAP_BASE
			+ (addr_t)(*pde & X86_64_PDE_ADDRESS_MASK));
		entry = table[(va >> 12) & (k64BitTableEntryCount - 1)];
	}
	kernelMap->Unlock();

	if (wantWc) {
		if (large || (entry & X86_64_PTE_PAT) == 0)
			return B_ERROR;
		if ((entry & (X86_64_PTE_WRITE_THROUGH | X86_64_PTE_CACHING_DISABLED))
				!= 0) {
			return B_ERROR;
		}
		return B_OK;
	}
	uint64 mask = large
		? X86_64_PDE_MEMORY_TYPE_MASK : X86_64_PTE_MEMORY_TYPE_MASK;
	if ((entry & mask) != 0)
		return B_ERROR;
	return B_OK;
}


static status_t
r5_direct_expect(phys_addr_t phys, bool wantWc)
{
	if (phys >= KERNEL_PMAP_SIZE)
		return B_ERROR;
	return r5_pte_expect((addr_t)KERNEL_PMAP_BASE + (addr_t)phys, wantWc);
}


static uint64
r5_wc_word(uint32 index)
{
	return 0x3141592653589793ULL ^ ((uint64)index << 12);
}


static status_t
r5_wc_fill(addr_t base, size_t bytes, bool check)
{
	uint32 pages = (uint32)(bytes / B_PAGE_SIZE);
	for (uint32 i = 0; i < pages; i++) {
		volatile uint64* word = (volatile uint64*)(base + (addr_t)i * B_PAGE_SIZE);
		uint64 expect = r5_wc_word(i);
		if (!check)
			*word = expect;
		else if (*word != expect)
			return B_ERROR;
	}
	return B_OK;
}


static void
r5_wc_delete(area_id* ids, uint32 count)
{
	for (uint32 i = 0; i < count; i++) {
		if (ids[i] >= 0)
			vm_delete_area(VMAddressSpace::KernelID(), ids[i], true);
	}
}


static status_t
r5_wc_wave(uint32 count, uint32 seed)
{
	const size_t kSizes[5] = {
		B_PAGE_SIZE,
		2 * B_PAGE_SIZE,
		17 * B_PAGE_SIZE,
		512 * B_PAGE_SIZE,
		513 * B_PAGE_SIZE
	};
	area_id ids[16];
	void* bases[16];
	phys_addr_t phys[16];
	size_t sizes[16];
	if (count > 16)
		count = 16;
	for (uint32 i = 0; i < count; i++)
		ids[i] = -1;

	for (uint32 i = 0; i < count; i++) {
		sizes[i] = kSizes[(seed + i) % 5];
		bases[i] = NULL;
		ids[i] = create_area("r5 wc direct", &bases[i], B_ANY_KERNEL_ADDRESS,
			sizes[i], B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (ids[i] < 0 || bases[i] == NULL) {
			dprintf("R5 VM: wc direct map FAIL at step create\n");
			r5_wc_delete(ids, count);
			return B_ERROR;
		}
		if (r5_wc_fill((addr_t)bases[i], sizes[i], false) != B_OK
			|| vm_set_area_page_memory_type(ids[i], B_WRITE_COMBINING_MEMORY)
				!= B_OK
			|| r5_wc_fill((addr_t)bases[i], sizes[i], true) != B_OK
			|| r5_virt_phys((addr_t)bases[i], &phys[i]) != B_OK
			|| r5_direct_expect(phys[i], true) != B_OK) {
			dprintf("R5 VM: wc direct map FAIL at step wc_set\n");
			r5_wc_delete(ids, count);
			return B_ERROR;
		}
	}

	for (uint32 i = 0; i < count; i++) {
		if (vm_set_area_page_memory_type(ids[i], B_WRITE_BACK_MEMORY) != B_OK
			|| r5_wc_fill((addr_t)bases[i], sizes[i], true) != B_OK) {
			dprintf("R5 VM: wc direct map FAIL at step wb_set\n");
			r5_wc_delete(ids, count);
			return B_ERROR;
		}
		if (r5_direct_expect(phys[i], false) != B_OK) {
			dprintf("R5 VM: wc direct map FAIL at step wb_pte\n");
			r5_wc_delete(ids, count);
			return B_ERROR;
		}
	}

	r5_wc_delete(ids, count);
	return B_OK;
}


static status_t
r5_wc_clone()
{
	void* base = NULL;
	area_id id = create_area("r5 wc clone source", &base, B_ANY_KERNEL_ADDRESS,
		B_PAGE_SIZE, B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (id < 0 || base == NULL) {
		dprintf("R5 VM: wc direct map FAIL at step clone_create\n");
		return B_ERROR;
	}
	*(volatile uint64*)base = r5_wc_word(1);
	if (vm_set_area_page_memory_type(id, B_WRITE_COMBINING_MEMORY) != B_OK) {
		dprintf("R5 VM: wc direct map FAIL at step clone_wc\n");
		vm_delete_area(VMAddressSpace::KernelID(), id, true);
		return B_ERROR;
	}
	void* cloneBase = NULL;
	area_id clone = vm_clone_area(VMAddressSpace::KernelID(), "r5 wc clone",
		&cloneBase, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, REGION_NO_PRIVATE_MAP, id,
		true);
	if (clone < 0 || cloneBase == NULL
		|| r5_pte_expect((addr_t)cloneBase, true) != B_OK) {
		dprintf("R5 VM: wc direct map FAIL at step clone\n");
		if (clone >= 0)
			vm_delete_area(VMAddressSpace::KernelID(), clone, true);
		vm_delete_area(VMAddressSpace::KernelID(), id, true);
		return B_ERROR;
	}
	phys_addr_t phys = 0;
	if (r5_virt_phys((addr_t)base, &phys) != B_OK
		|| r5_direct_expect(phys, true) != B_OK
		|| *(volatile uint64*)cloneBase != r5_wc_word(1)) {
		dprintf("R5 VM: wc direct map FAIL at step clone_read\n");
		vm_delete_area(VMAddressSpace::KernelID(), clone, true);
		vm_delete_area(VMAddressSpace::KernelID(), id, true);
		return B_ERROR;
	}
	vm_delete_area(VMAddressSpace::KernelID(), clone, true);
	if (r5_direct_expect(phys, true) != B_OK) {
		dprintf("R5 VM: wc direct map FAIL at step clone_hold\n");
		vm_delete_area(VMAddressSpace::KernelID(), id, true);
		return B_ERROR;
	}
	vm_delete_area(VMAddressSpace::KernelID(), id, true);
	if (r5_direct_expect(phys, false) != B_OK) {
		dprintf("R5 VM: wc direct map FAIL at step clone_free\n");
		return B_ERROR;
	}
	return B_OK;
}


extern "C" status_t
r5_wc_direct_map_selftest(int rounds)
{
	static int32 sBusy = 0;
	if (atomic_add(&sBusy, 1) != 0) {
		atomic_add(&sBusy, -1);
		return B_BUSY;
	}
	if (!x86_use_pat()) {
		dprintf("R5 VM: wc direct map skipped, no PAT\n");
		atomic_add(&sBusy, -1);
		return B_OK;
	}
	if (rounds < 1)
		rounds = 1;

	uint32 wave = rounds >= 16 ? 16 : (uint32)rounds;
	uint32 done = 0;
	status_t status = B_OK;
	while (done < (uint32)rounds && status == B_OK) {
		uint32 count = (uint32)rounds - done;
		if (count > wave)
			count = wave;
		status = r5_wc_wave(count, done);
		done += count;
	}
	if (status == B_OK)
		status = r5_wc_clone();
	if (status == B_OK) {
		dprintf("R5 VM: wc direct map ok (%u areas)\n", (unsigned)rounds);
	}
	atomic_add(&sBusy, -1);
	return status;
}

#else

extern "C" status_t
r5_wc_direct_map_selftest(int rounds)
{
	(void)rounds;
	dprintf("R5 VM: wc direct map skipped, not amd64\n");
	return B_OK;
}

#endif
