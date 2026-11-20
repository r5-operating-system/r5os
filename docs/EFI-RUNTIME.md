# EFI runtime services in the R5 kernel

The module `generic/efi/v1` gives kernel drivers the variable services of the UEFI firmware on any UEFI computer. Its public interface is `headers/os/drivers/EFI.h` (MIT), the same file as in the CS35L41 change (#4482). This page describes how the module works and its limits.

## Interface

`EFI.h`, `B_EFI_MODULE_NAME` = `"generic/efi/v1"`, module `B_KEEP_LOADED`:

| Call | Result |
|---|---|
| `get_variable(name, vendor, buffer, &size, &attributes)` | `B_OK`; `B_BUFFER_OVERFLOW` with the needed size (`buffer` may be `NULL` with size 0); `B_ENTRY_NOT_FOUND`; `B_BAD_VALUE`; `B_NOT_ALLOWED` (security violation); `B_IO_ERROR`; `B_NOT_SUPPORTED` |
| `get_next_variable_name(name, nameSize, &vendor)` | Start with `""`. `B_ENTRY_NOT_FOUND` after the last name; `B_BUFFER_OVERFLOW` if the name does not fit |

Names are UTF-8; the module converts them to UCS-2. A value is at most 64 KiB, a name at most 1023 UCS-2 characters. Only kernel code can call the module. There is no userland interface and no `SetVariable` yet. When `SetVariable` comes, it goes at the end of `efi_module_info`, and userland reaches it only through the admin path (`r5_admin_send()`), never for Secure Boot variables.

A driver loads the module with `get_module()` and must work when that fails. It must not list the module in `module_dependencies`.

## Who may call

Call the module from thread context only.

- A call can block, because it waits for the module lock.
- While it holds the lock, it keeps interrupts off for the length of one firmware call.
- In QEMU with OVMF, `efi_test` prints how long a `BootOrder` read takes. Firmware that reads the variables from SPI flash can take longer.
- The module logs every new longest call above 1 ms (`efi: a firmware call kept interrupts off for N us`).
- Never call from an interrupt handler, with a spinlock held, or with interrupts off. A call with interrupts off is refused with `B_NOT_ALLOWED` and one log line.

## When the module is absent

`get_module()` fails, and the driver runs without EFI data, in the cases below. Each one leaves a single `efi:` line in the syslog, except the BIOS start, which has no hand-over at all.

- The computer started through the legacy BIOS loader.
- Safe mode is on, or `disable_efi_runtime true` is in the safe mode settings.
- `efi_runtime false` is in `~/config/settings/kernel/drivers/kernel`.
- The firmware has no variable services after boot (`EFI_RT_PROPERTIES_TABLE`).
- `SetVirtualAddressMap` failed, or a runtime range is not at `VirtualStart == PhysicalStart`.
  - The loader then leaves `R5_EFI_RUNTIME_IDENTITY` unset.
  - The module logs `runtime map incomplete or not identity mapped`, with the flags and the status of the call.
  - The firmware is never called in that case.
- There are more runtime ranges than fit in the block.
- A runtime range lies above the lower half, or would need more than 8 MiB of page tables.
- CR4.PCIDE is set (see PCID below).
- The CPU is not x86_64. arm64 and riscv64 build the module, but it does not load.

## Path

1. **Loader** (`src/system/boot/platform/efi/r5_efi_runtime.cpp`, called from `platform_load_ucode()` in `cpu.cpp`, where SMBIOS is handed over too).
   - At that point the loader heap exists and the boot volume message has not been cloned yet.
   - It allocates a 24 KiB block in kernel_args memory and stores its physical address under `_boot_efi r5 runtime`.
   - It copies the EFI memory attributes table and the runtime properties mask into the block.
2. **Loader, x86_64** (`arch/x86_64/arch_mmu.cpp`, `arch_start.cpp`).
   - Every `EFI_MEMORY_RUNTIME` range gets `VirtualStart = PhysicalStart` before `SetVirtualAddressMap`.
   - Haiku did this only for the runtime code and data types. Runtime MMIO ranges, such as the flash that holds the variables, kept the firmware's value.
   - After the call, the runtime descriptors of the final map go into the block, with the call's status.
3. **Kernel** (`arch/x86/arch_platform.cpp`). In `arch_platform_init_post_vm` it copies the block into the heap and publishes it as the boot item `R5_EFI_RUNTIME`, the same route the ACPI root takes.
4. **Module** (`src/add-ons/kernel/generic/efi`). It checks the block, reads the runtime services table through its physical address, and builds the firmware's address space.

Format of the block: `headers/private/kernel/boot/r5_efi_runtime.h`.

## Calling the firmware (x86_64)

- **Address space.** The firmware gets its own PML4.
  - The lower half holds only the runtime ranges, at VA = PA, supervisor-only and never global.
  - The upper half is copied from the current kernel PML4 before each call.
  - User memory is not mapped during a call.
- **Page attributes.**
  - With an attributes table, RO and XP are used exactly as given. Ranges not in the table are NX, except runtime code.
  - Without the table every runtime range is executable and writable, as other systems do, because firmware often mixes code and data.
  - Runtime MMIO is always NX, and uncached unless it says WB.
- **One call at a time.** A mutex is held, interrupts are off, and the CPU stays the same for the whole call. The call runs on a 64 KiB stack of its own, because kernel stacks are 16 KiB.
- **Calling convention.** Microsoft x64, through `efi_call5` (`efi_call_x86_64.S`):
  - arguments in `rcx`, `rdx`, `r8`, `r9`, the fifth on the stack,
  - 32 bytes of shadow space,
  - `%rsp` 16-byte aligned, DF clear.
- **FPU.** The module saves the whole FPU state of the calling thread before the call and restores it after.
  - **With XSAVE:** `xsave64`/`xrstor64` with the kernel's own mask `gXsaveMask`. That is the XCR0 that `arch_cpu.cpp` programs and that the interrupt and context-switch paths in `interrupts.S` save. So x87, SSE, AVX and, where enabled, AVX-512 are covered.
  - **The buffer:** the size CPUID leaf 0xD (EBX) reports for that XCR0, 64-byte aligned, with the XSAVE header zeroed before each save.
  - **Why not the kernel's own code:** its code-patched XSAVE sequence is inline in `interrupts.S` and cannot be called. The module does the same with the same mask, in the standard (not compacted) format.
  - **Without XSAVE:** `fxsave64`.
  - **What the firmware gets:** `fninit` and MXCSR `0x1f80`.
- **Buffers.** The firmware only sees the module's own buffers in wired kernel memory, never the caller's memory.
- **SMAP/SMEP.** The pages are supervisor pages, so neither check applies to normal firmware accesses.
- **PCID.** R5 does not set CR4.PCIDE and has no KPTI. The kernel only uses `invpcid` for a global flush, when the CPU has it.
  - So the CR3 writes carry no PCID, and switching back flushes all non-global entries, which are exactly the firmware's pages.
  - If CR4.PCIDE is ever set, the module refuses to load (`efi_arch_init`) until the switch handles PCIDs.

## Faults

During the call the thread's `fault_handler` points at `efi_call_fault`.

1. `r5_firmware_fault()` in `kernel/arch/x86/arch_int.cpp` recognises firmware code: kernel mode, an instruction pointer in the lower half, interrupts off, and a fault handler set. Only the firmware runs like that.
2. It is checked first in `x86_page_fault_exception`, before the SMEP and SMAP panics. It is also checked first in `x86_unexpected_exception`, which takes #DE, #UD, #GP, #SS, #MF, #XF and the others routed there.
3. Every such fault therefore returns to `efi_call_fault`. The saved registers, the kernel CR3 and the FPU state come back, and the call returns `B_NOT_SUPPORTED`.
4. The module then refuses all further calls until the next start, with one syslog line.

The check sits in the kernel, not only in the module, because otherwise every fault except a page fault would panic the machine for a firmware bug.

Not recovered: a double fault (#DF, which has its own IST handler), an NMI, and a machine check. Nothing here can catch a firmware bug that corrupts kernel memory.

## Firmware quirks

| Quirk | What R5 does |
|---|---|
| Runtime MMIO (the flash) without a virtual address in the old loader | All `EFI_MEMORY_RUNTIME` ranges are identity mapped |
| Firmware that keeps using physical addresses after `SetVirtualAddressMap` | Harmless: virtual and physical addresses are the same |
| Code in data ranges, or data in code ranges, without an attributes table | Everything RWX (except MMIO) without a table |
| Firmware touches boot-services memory after `ExitBootServices` | Not mapped; the page fault is recovered and the module turns off |
| `SetVirtualAddressMap` returns an error | Module absent, one log line |
| No variable services after boot (`EFI_RT_PROPERTIES_TABLE`) | Module absent, one log line |

The design names no vendors that misbehave with a 1:1 map, and no vendor-specific workaround is built in. A new quirk goes into this table, with the machine it was seen on.

## arm64 (designed, not built)

- The arm64 loader never sets `VirtualStart` before `SetVirtualAddressMap`, so it first needs the same identity loop as x86_64 in `arch/arm64/arch_mmu.cpp`.
- Then it needs the hand-over call in `arch/arm64/arch_start.cpp`, and the boot item in `kernel/arch/arm64/arch_platform.cpp`.
- Then a backend `arch_arm64.cpp`: its own TTBR0 table with the runtime ranges (EL1-only, UXN on all, PXN off only on code) and the MAIR of the kernel. AAPCS64 arguments go in x0 to x7. x18 must be saved and checked after the call, and the NEON state saved.
- Until then `arch_none.cpp` makes the module refuse to load.

## Test

`efi_test.cpp` is a test driver in no package (`jam -q efi_test`). Loading it, by opening `/dev/misc/efi_test`, logs `efi_test:` lines:

- the `BootOrder` size query,
- `BootOrder` itself,
- `SmartAmpCalibrationData` (`53559579-8753-4f5c-9130-e82acfb8d893`),
- a missing variable,
- the first 12 variable names and the count.
