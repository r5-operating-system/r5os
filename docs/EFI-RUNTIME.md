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

## When the module is absent

`get_module()` fails (the driver runs without EFI data) when:

- the computer started through the legacy BIOS loader,
- safe mode is on, or `disable_efi_runtime true` is in the safe mode settings,
- `efi_runtime false` is in `~/config/settings/kernel/drivers/kernel`,
- the firmware has no variable services after boot (`EFI_RT_PROPERTIES_TABLE`),
- the hand-over is incomplete (more runtime ranges than fit, or `SetVirtualAddressMap` failed),
- the CPU is not x86_64 (arm64 and riscv64 build the module, but it does not load).

## Path

1. **Loader** (`src/system/boot/platform/efi/r5_efi_runtime.cpp`, called from `platform_load_ucode()` in `cpu.cpp`, where SMBIOS is handed over too): before the loader clones the boot volume message, it allocates a 24 KiB block in kernel_args memory and stores its physical address under `_boot_efi r5 runtime`. It copies the EFI memory attributes table and the runtime properties mask into the block.
2. **Loader, x86_64** (`arch/x86_64/arch_mmu.cpp`, `arch_start.cpp`): every `EFI_MEMORY_RUNTIME` range gets `VirtualStart = PhysicalStart` before `SetVirtualAddressMap`. Haiku did this only for the runtime code and data types. Runtime MMIO ranges, such as the flash that holds the variables, kept the firmware's value. After the call, the runtime descriptors of the final map go into the block, with the call's status.
3. **Kernel** (`arch/x86/arch_platform.cpp`): in `arch_platform_init_post_vm` it copies the block into the heap and publishes it as the boot item `R5_EFI_RUNTIME`. This is the same route the ACPI root takes.
4. **Module** (`src/add-ons/kernel/generic/efi`): checks the block, reads the runtime services table through its physical address, and builds the firmware's address space.

Format of the block: `headers/private/kernel/boot/r5_efi_runtime.h`.

## Calling the firmware (x86_64)

- **Address space.** The firmware gets its own PML4. The lower half holds only the runtime ranges, at VA = PA, supervisor-only and never global. The upper half is copied from the current kernel PML4 before each call. User memory is not mapped during a call.
- **Page attributes.** With an attributes table, RO and XP are used exactly as given, and ranges not in the table are NX except runtime code. Without the table every runtime range is executable and writable (firmware often mixes code and data), as other systems do. Runtime MMIO is always NX and uncached unless it says WB.
- **One call at a time.** A mutex is held, interrupts are off, and the CPU stays the same for the whole call. The call runs on a 64 KiB stack of its own, because kernel stacks are 16 KiB.
- **Calling convention.** Microsoft x64, through `efi_call5` (`efi_call_x86_64.S`): `rcx, rdx, r8, r9`, the fifth argument on the stack, 32 bytes of shadow space, `%rsp` 16-byte aligned, DF clear.
- **FPU.** The x87/SSE state is saved with `fxsave64`, the firmware gets `fninit` and MXCSR `0x1f80`, and the state is restored afterwards.
- **Buffers.** The firmware only sees the module's own buffers in wired kernel memory, never the caller's memory.
- **SMAP/SMEP.** The pages are supervisor pages, so neither check applies. During the call `AC` is set: the address space has no user pages, so this opens nothing, but it lets a protection fault reach the recovery below instead of the SMAP panic.

## Faults

The thread's `fault_handler` points at `efi_call_fault` during the call. A page fault in the firmware (interrupts off) jumps there. The saved registers and the kernel CR3 come back, the call returns `B_NOT_SUPPORTED`, and the module refuses all further calls until the next start, with one syslog line.

Not recovered yet:

- an instruction-fetch fault while SMEP is on (panics in `x86_page_fault_exception`),
- a #GP or #UD in the firmware (kernel trap panic).

Both need a check in `arch_int.cpp`.

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
