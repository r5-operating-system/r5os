# EFI runtime services for kernel drivers

Some drivers need EFI variables after boot, for example the factory speaker calibration of the CS35L41 amplifiers. R5 offers them through a kernel module built the Haiku way, like the ACPI module. This page is the contract for drivers; how the module works is in `docs/EFI-RUNTIME.md`.

- Header: `distro/kernel/headers/os/drivers/EFI.h`, installed as `headers/os/drivers/EFI.h` (MIT).
- Module name: `B_EFI_MODULE_NAME` = `"generic/efi/v1"`.

## Calls

| Call | What it does |
|---|---|
| `get_variable(name, vendor, buffer, &size, &attributes)` | Reads EFI variable `name` (UTF-8) of vendor GUID `vendor`. `size` is the buffer size on entry and the variable size on return. `B_BUFFER_OVERFLOW` if the buffer is too small (size still set); `buffer` may be `NULL` with `size` 0 to ask for the size. `B_ENTRY_NOT_FOUND` if the variable does not exist. `attributes` may be `NULL` |
| `get_next_variable_name(name, nameSize, &vendor)` | Steps through all variable names, starting with an empty `name`; `B_ENTRY_NOT_FOUND` after the last |

`efi_vendor_guid` has the field layout of the UEFI specification (`data1`, `data2`, `data3`, `data4[8]`). `SetVariable` comes later, added at the end of `efi_module_info`.

## Using it

A driver calls `get_module(B_EFI_MODULE_NAME, ...)` and must work when that fails: on a legacy BIOS start, in safe mode, on a CPU without a backend, or when the firmware's runtime map is unusable. It must not list the module in `module_dependencies`, or the driver would not load at all. Calls are for thread context only, never from an interrupt handler, with interrupts off or with a spinlock held (see `EFI.h`).

First user: the CS35L41 driver reads `SmartAmpCalibrationData` (GUID `53559579-8753-4f5c-9130-e82acfb8d893`) and then `CirrusSmartAmpCalibrationData` (`02f9af02-7734-4233-b43d-93fe5aa35db3`). Without the module it logs `no factory calibration: generic/efi/v1 is not available` once and runs the firmware with its default calibration.
