/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	x86_64 backend of generic/efi (docs/EFI-RUNTIME.md).

	The boot loader called SetVirtualAddressMap with VirtualStart ==
	PhysicalStart, so the firmware expects its runtime ranges at their
	physical addresses. They lie in the lower half, where user programs live,
	so the firmware gets its own PML4: the lower half holds only the runtime
	ranges (supervisor pages, never global), the upper half is a copy of the
	kernel half, taken before each call. A call switches CR3 to it, with
	interrupts off, on a 64 KiB stack of its own, with the FPU state saved
	(XSAVE with the kernel mask, or FXSAVE).
	The firmware never sees user memory.
*/


#include "efi_arch.h"

#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>

#include <arch/x86/arch_cpu.h>
#include <arch/x86/arch_cpuasm.h>
#include <efi/types.h>
#include <efi/boot-services.h>
#include <smp.h>
#include <thread.h>
#include <vm/vm.h>


#define ERROR(x...) dprintf("efi: " x)


// Page table bits (Intel SDM vol. 3, 4.5).
static const uint64 kPresent = 1ULL << 0;
static const uint64 kWritable = 1ULL << 1;
static const uint64 kWriteThrough = 1ULL << 3;
static const uint64 kCacheDisabled = 1ULL << 4;
static const uint64 kNotExecutable = 1ULL << 63;
static const uint64 kAddressMask = 0x000ffffffffff000ULL;

static const uint64 kLowerHalfEnd = 0x0000800000000000ULL;
static const size_t kStackSize = 64 * 1024;
static const uint32 kMaxTablePages = 2048;
	// 8 MiB of tables maps 4 GiB of runtime ranges in 4 KiB pages


extern "C" uint64 efi_call5(uint64 function, uint64 arg1, uint64 arg2,
	uint64 arg3, uint64 arg4, uint64 arg5, uint64 stackTop);
extern "C" void efi_call_fault(void);

// The kernel's firmware-fault hook (kernel/arch/x86/arch_int.cpp).
extern void (*gR5FirmwareFaultHandler)(void);
extern int32 gR5FirmwareCallCPU;


static area_id sTableArea = -1;
static uint64* sTables = NULL;
static phys_addr_t sTablesPhysical = 0;
static uint32 sTableCount = 0;
static uint32 sTablesUsed = 0;

static area_id sStackArea = -1;
static uint64 sStackTop = 0;

// The FPU state of the calling thread. With XSAVE, the kernel's own mask
// (gXsaveMask, the XCR0 that the interrupt and context switch paths save) and
// a buffer of the size CPUID leaf 0xD reports for it, 64-byte aligned, so
// AVX and AVX-512 registers survive firmware that uses them. Without XSAVE,
// FXSAVE (x87 and SSE are all there is).
extern uint64 gXsaveMask;
extern bool gHasXsave;

static void* sFPUBuffer = NULL;
static uint8* sFPUState = NULL;
static size_t sFPUSize = 0;


static void
save_fpu()
{
	if (gHasXsave) {
		// XSAVE leaves the header's reserved bytes alone; XRSTOR faults
		// unless they are zero.
		memset(sFPUState + 512, 0, 64);
		asm volatile("xsave64 (%0)" : : "r" (sFPUState),
			"a" ((uint32)gXsaveMask), "d" ((uint32)(gXsaveMask >> 32))
			: "memory");
	} else
		asm volatile("fxsave64 (%0)" : : "r" (sFPUState) : "memory");
}


static void
restore_fpu()
{
	if (gHasXsave) {
		asm volatile("xrstor64 (%0)" : : "r" (sFPUState),
			"a" ((uint32)gXsaveMask), "d" ((uint32)(gXsaveMask >> 32))
			: "memory");
	} else
		asm volatile("fxrstor64 (%0)" : : "r" (sFPUState) : "memory");
}


static status_t
init_fpu_buffer()
{
	sFPUSize = 512;
	if (gHasXsave) {
		// CPUID leaf 0xD, subleaf 0, EBX: bytes for the features enabled in
		// XCR0 now (the kernel's gXsaveMask).
		uint32 eax, ebx, ecx, edx;
		asm volatile("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
			: "a" (IA32_CPUID_LEAF_XSTATE), "c" (0));
		sFPUSize = ebx;
		if (sFPUSize < 576)
			return B_ERROR;
	}

	sFPUBuffer = malloc(sFPUSize + 63);
	if (sFPUBuffer == NULL)
		return B_NO_MEMORY;
	sFPUState = (uint8*)(((addr_t)sFPUBuffer + 63) & ~(addr_t)63);
	memset(sFPUState, 0, sFPUSize);
	return B_OK;
}


static inline const efi_memory_descriptor*
descriptor_at(const r5_efi_runtime_args* args, uint32 offset, uint32 size,
	uint32 index)
{
	return (const efi_memory_descriptor*)((const uint8*)args + offset
		+ (size_t)index * size);
}


static uint32
windows(uint64 start, uint64 end, uint64 windowSize)
{
	return (uint32)((end - 1) / windowSize - start / windowSize + 1);
}


static uint64*
table_at(phys_addr_t physical)
{
	return sTables + (physical - sTablesPhysical) / sizeof(uint64);
}


static status_t
next_table(uint64* entry)
{
	if ((*entry & kPresent) != 0)
		return B_OK;
	if (sTablesUsed >= sTableCount)
		return B_NO_MEMORY;

	phys_addr_t physical = sTablesPhysical + (phys_addr_t)sTablesUsed
		* B_PAGE_SIZE;
	memset(table_at(physical), 0, B_PAGE_SIZE);
	sTablesUsed++;
	*entry = physical | kPresent | kWritable;
	return B_OK;
}


static status_t
map_page(uint64 address, uint64 flags)
{
	uint64* pml4 = sTables;
	uint64* entry = &pml4[(address >> 39) & 511];
	status_t status = next_table(entry);
	if (status != B_OK)
		return status;

	uint64* pdpt = table_at(*entry & kAddressMask);
	entry = &pdpt[(address >> 30) & 511];
	if ((status = next_table(entry)) != B_OK)
		return status;

	uint64* pd = table_at(*entry & kAddressMask);
	entry = &pd[(address >> 21) & 511];
	if ((status = next_table(entry)) != B_OK)
		return status;

	uint64* pt = table_at(*entry & kAddressMask);
	pt[(address >> 12) & 511] = (address & kAddressMask) | flags;
	return B_OK;
}


/*!	The attribute table entry that covers "address", or NULL. */
static const efi_memory_descriptor*
attribute_for(const r5_efi_runtime_args* args, uint64 address)
{
	if ((args->flags & R5_EFI_RUNTIME_ATTRIBUTES) == 0)
		return NULL;

	for (uint32 i = 0; i < args->attribute_count; i++) {
		const efi_memory_descriptor* entry = descriptor_at(args,
			args->attribute_offset, args->attribute_descriptor_size, i);
		uint64 start = entry->PhysicalStart;
		if (address >= start
			&& address - start < entry->NumberOfPages * B_PAGE_SIZE) {
			return entry;
		}
	}
	return NULL;
}


static uint64
page_flags(const r5_efi_runtime_args* args,
	const efi_memory_descriptor* range, uint64 address, bool noExecute)
{
	uint64 flags = kPresent | kWritable;

	// Caching: write-back unless the range only allows less.
	if ((range->Attribute & EFI_MEMORY_WB) == 0) {
		if ((range->Attribute & (EFI_MEMORY_WT | EFI_MEMORY_WC)) != 0)
			flags |= kWriteThrough;
		else
			flags |= kCacheDisabled | kWriteThrough;
	}

	bool executable;
	if (range->Type == EfiMemoryMappedIO
		|| range->Type == EfiMemoryMappedIOPortSpace) {
		executable = false;
	} else if ((args->flags & R5_EFI_RUNTIME_ATTRIBUTES) != 0) {
		// The firmware said exactly what is code and what is read-only.
		const efi_memory_descriptor* attribute = attribute_for(args, address);
		if (attribute != NULL) {
			executable = (attribute->Attribute & EFI_MEMORY_XP) == 0;
			if ((attribute->Attribute & EFI_MEMORY_RO) != 0)
				flags &= ~kWritable;
		} else
			executable = range->Type == EfiRuntimeServicesCode;
	} else {
		// Without the table, firmware often keeps data in its code ranges
		// and code in its data ranges; map both executable, like other
		// systems do.
		executable = true;
	}

	if (!executable && noExecute)
		flags |= kNotExecutable;
	return flags;
}


status_t
efi_arch_init(const r5_efi_runtime_args* args)
{
	// The CR3 switch below writes no PCID. R5 does not set CR4.PCIDE and has
	// no KPTI; if that changes, this must change first.
	if ((x86_read_cr4() & IA32_CR4_PCIDE) != 0) {
		ERROR("CR4.PCIDE is set; the firmware address space does not handle "
			"PCIDs yet\n");
		return B_NOT_SUPPORTED;
	}

	status_t fpuStatus = init_fpu_buffer();
	if (fpuStatus != B_OK)
		return fpuStatus;

	// Count the tables we may need: one PML4, and per range one PDPT, PD and
	// PT for every 512 GiB, 1 GiB and 2 MiB window it touches.
	uint64 tableCount = 1;
	for (uint32 i = 0; i < args->descriptor_count; i++) {
		const efi_memory_descriptor* range = descriptor_at(args,
			args->descriptor_offset, args->descriptor_size, i);
		uint64 start = range->PhysicalStart;
		uint64 size = range->NumberOfPages * B_PAGE_SIZE;
		if (size == 0)
			continue;
		if ((start & (B_PAGE_SIZE - 1)) != 0 || start >= kLowerHalfEnd
			|| size > kLowerHalfEnd - start) {
			ERROR("runtime range %#" B_PRIx64 " + %#" B_PRIx64 " cannot be "
				"identity mapped\n", start, size);
			efi_arch_uninit();
			return B_BAD_ADDRESS;
		}
		uint64 end = start + size;
		tableCount += windows(start, end, 1ULL << 39)
			+ windows(start, end, 1ULL << 30) + windows(start, end, 1ULL << 21);
	}
	if (tableCount > kMaxTablePages) {
		ERROR("runtime ranges need %" B_PRIu64 " page tables, too many\n",
			tableCount);
		efi_arch_uninit();
		return B_NO_MEMORY;
	}

	sTableCount = (uint32)tableCount;
	sTableArea = create_area("efi runtime page tables", (void**)&sTables,
		B_ANY_KERNEL_ADDRESS, sTableCount * B_PAGE_SIZE, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (sTableArea < 0) {
		status_t status = sTableArea;
		efi_arch_uninit();
		return status;
	}

	physical_entry entry;
	status_t status = get_memory_map(sTables, B_PAGE_SIZE, &entry, 1);
	if (status != B_OK) {
		efi_arch_uninit();
		return status;
	}
	sTablesPhysical = entry.address;
	memset(sTables, 0, B_PAGE_SIZE);
	sTablesUsed = 1;

	bool noExecute = x86_check_feature(IA32_FEATURE_AMD_EXT_NX,
		FEATURE_EXT_AMD);
	uint64 pages = 0;
	for (uint32 i = 0; i < args->descriptor_count; i++) {
		const efi_memory_descriptor* range = descriptor_at(args,
			args->descriptor_offset, args->descriptor_size, i);
		for (uint64 page = 0; page < range->NumberOfPages; page++) {
			uint64 address = range->PhysicalStart + page * B_PAGE_SIZE;
			status = map_page(address,
				page_flags(args, range, address, noExecute));
			if (status != B_OK) {
				ERROR("mapping %#" B_PRIx64 " failed\n", address);
				efi_arch_uninit();
				return status;
			}
			pages++;
		}
	}

	void* stack;
	sStackArea = create_area("efi runtime stack", &stack, B_ANY_KERNEL_ADDRESS,
		kStackSize, B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (sStackArea < 0) {
		status = sStackArea;
		efi_arch_uninit();
		return status;
	}
	sStackTop = (addr_t)stack + kStackSize;

	dprintf("efi: %" B_PRIu64 " pages identity mapped, %" B_PRIu32 " of %"
		B_PRIu32 " page tables, NX %s\n", pages, sTablesUsed, sTableCount,
		noExecute ? "on" : "off");
	return B_OK;
}


void
efi_arch_uninit()
{
	if (sStackArea >= 0)
		delete_area(sStackArea);
	if (sTableArea >= 0)
		delete_area(sTableArea);
	sStackArea = sTableArea = -1;
	free(sFPUBuffer);
	sFPUBuffer = NULL;
	sFPUState = NULL;
	sTables = NULL;
	sTablesUsed = sTableCount = 0;
}


uint64
efi_arch_call(uint64 function, uint64 arg1, uint64 arg2, uint64 arg3,
	uint64 arg4, uint64 arg5)
{
	if (sTables == NULL || function == 0 || function >= kLowerHalfEnd)
		return EFI_ARCH_FAULTED;

	// The kernel half is the same in every address space; copy it from the
	// current one so the firmware can reach the arguments, the stack and the
	// interrupt handlers.
	uint64 kernelCR3 = x86_read_cr3();
	if (vm_memcpy_from_physical(sTables + 256,
			(kernelCR3 & kAddressMask) + 256 * sizeof(uint64),
			256 * sizeof(uint64), false) != B_OK) {
		return EFI_ARCH_FAULTED;
	}

	cpu_status state = disable_interrupts();

	save_fpu();
	uint32 mxcsr = 0x1f80;
	asm volatile("fninit; ldmxcsr %0" : : "m" (mxcsr));

	// Only while the firmware runs on this CPU does a fault go to
	// efi_call_fault (r5_firmware_fault() in arch_int.cpp); everything else,
	// even under a thread fault handler, keeps its normal path.
	gR5FirmwareCallCPU = smp_get_current_cpu();
	gR5FirmwareFaultHandler = efi_call_fault;

	// No PCID (checked in efi_arch_init): a plain CR3 write flushes the
	// non-global entries, which are all the firmware's pages.
	x86_write_cr3(sTablesPhysical);
	uint64 status = efi_call5(function, arg1, arg2, arg3, arg4, arg5,
		sStackTop);
	x86_write_cr3(kernelCR3);

	gR5FirmwareFaultHandler = NULL;
	gR5FirmwareCallCPU = -1;

	restore_fpu();
	restore_interrupts(state);
	return status;
}
