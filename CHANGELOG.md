# R5OS changelog (lokaal)

<!-- changelog-archive: begin -->
Older entries are in `docs/changelog/`, one file per ISO week (Monday to Sunday), moved word for word by `scripts/changelog-archive.py`. Search there too (`grep -r <term> CHANGELOG.md docs/changelog/`).

- [2026-W46](docs/changelog/2026-W46.md): 2026-11-09 to 2026-11-15, 183 entries
- [2026-W44](docs/changelog/2026-W44.md): 2026-10-26 to 2026-11-01, 2 entries
<!-- changelog-archive: end -->

## 2026-11-22 - Power after stick 74: no brightness hang on DCN, the right AC state, the battery in the Deskbar

Problem (stick 74, 5625U, `generated/boot-5625u-74/` in the stick worktree):
- Moving the brightness slider froze the desktop for about 30 s each time: `radeon_set_brightness (0.590000)`, `brightness level = ffffffff`, `transmitter_dig_setup: connector 0, pixelClock: 0`, `AtomBIOS stuck in loop for more then 5 seconds`, `parser was aborted in table DIG1TransmitterControl (UNIPHY)`. Every later GPU start hung at the boot screen after `radeon_set_brightness (1.000000)`.
- Cause (sure: the log shows each step, and the code has no DCN check there): `radeon_set_brightness()` is Haiku's DCE path. On DCN it read and wrote the DCE scratch register `0x172c` (it read `ffffffff`, not a DCE scratch value) and ran AtomBIOS `DIG1TransmitterControl` twice per call with pixel clock 0 (the DCN path keeps the firmware scanout and never sets a PLL), and each call ran into the AtomBIOS 5 s loop guard. `Desktop.cpp` ("Restore brightness") calls `SetBrightness()` at every start when a stored screen configuration exists, and a configuration without a brightness reads as 1.0, so the restore ran into the same call with the Desktop not yet up.
- `power_daemon: profile 1 (on battery)` at every start with the charger in; pulling the charger changed nothing; the Power page did not follow. Cause (sure: Haiku's driver code shows it): `acpi_ac` set `last_status` to 0 when it started and only read `_PSR` on a notification, so every reader got "offline" until the first change. The daemon, `BPowerRoster::IsOnAC()` and the page's profile line all read that device.
- No battery in the Deskbar: R5's first-login script adds no Deskbar items, and PowerStatus is not started anywhere.

What changed:
- `radeon_hd` (`mode.cpp`, R5 snippets): `radeon_set_brightness()` and `radeon_get_brightness()` return `B_NOT_SUPPORTED` unless the chip is DCE, display 0 is attached, its connector is LVDS or eDP, and a mode with a pixel clock is set. The Power page already hides the slider when `GetBrightness()` fails. Chosen path for DCN: report no brightness now. ACPI `_BCM`/`_BCL` stays in Power step 3 as planned (it is the general fallback for every GPU, but firmware that sees a Windows 8 or later `_OSI` often leaves `_BCM` to the GPU driver, so it may not work on the 5625U either). A DCN backlight path is GPU display work that needs its own clean-room source (no Linux amdgpu display code), so it is not started here.
- `app_server` (`AccelerantHWInterface.cpp`, R5 snippets): `SetBrightness()` only calls the accelerant when its `GetBrightness()` works, skips a value that is already set, and after a call that fails or takes more than 1 s turns brightness off for the session (`app_server: brightness off for this session (...)` in the syslog). So the restore at start can cost at most one slow call on any GPU, and a later slider move costs none.
- `acpi_ac` copied into `distro/kernel` (Haiku, MIT; R5 snippets; in `scripts/apply-kernel-bugs.py`): reads `_PSR` when the driver starts and on every read. The `acpi_ac: status` line is logged only when the state changes, since the daemon reads every 5 s.
- `default_deskbar_items.sh` (first sign-in, once per person): starts `PowerStatus --deskbar` when `/dev/power/acpi_battery` has an entry. PowerStatus adds its item without asking, and quits quietly without a battery or when the item is there. A person who removes it keeps it removed. What's new of build 10067 says so.
- Checked and left: the Power page does follow AC and battery changes: `libr5power.so`'s watcher reads both every 2 s and sends `B_POWER_AC_CHANGED` and `B_POWER_BATTERY_CHANGED`. With `acpi_ac` stuck at 0, the AC change it saw was wrong; the battery line follows `_BST`, which the driver reads fresh.

Limits:
- The 5625U has no brightness control until step 3 (`_BCM`) or a DCN backlight path. intel_extreme and DCE radeon keep their paths, with the app_server guard.
- A user who already signed in once (an existing home) does not get the Deskbar battery: the first-login script has run for them. A new stick image has a new home.
- Whether `_BST` follows the charger at once on the 5625U is not shown; the stick boot below shows it.

Tested (22 Nov 2026):
- Compiled: `scripts/build-locked.sh` on `r5/power-step2` (merged with `origin/main`), `jam_exit=0`, `mesa_exit=0`; `mode.o`, `AccelerantHWInterface.o` and `acpi_ac` were rebuilt from the `distro/` copies.
- QEMU (KVM, `-cpu host,+smap,+smep`, `-vga std`, own copy of the image): first start and Sign in, desktop. Settings, System, Power: QEMU's VESA accelerant reports a brightness, so the slider shows at 100 %. Moved it to about 40 %: no freeze, Revert turned on. Restart from the Deskbar: the start with the stored brightness reached Sign in and the desktop. No battery item in the Deskbar (QEMU has no battery).
- Not shown in QEMU: the DCN path (QEMU has no radeon_hd), `acpi_ac` (QEMU's `pc` machine has no `ACPI0003`), and PowerStatus in the Deskbar with a battery. These need the 5625U.

## 2026-11-22 - Power, step 2: profiles, the power service and the Power page (build 10067)

Problem: R5 had only the scheduler mode (two states, no settings file) to steer the processor, no AMD path without the CPPC MSR, a power button that shut down without asking, no display timeout, and battery reading private to PowerStatus (`docs/POWER.md`, section 1).

What changed (`docs/POWER.md`, section 8, step 2):

- Kernel. Two private syscalls next to `_kern_set_scheduler_mode()`: `_kern_r5_set_power_profile()` (uid 0 only) and `_kern_r5_get_power_info()` (`<r5_power_profile.h>`: profile, method, flags; the user buffer is checked with `IS_USER_ADDRESS` and written with `user_memcpy()`). Version 2 of the cpufreq module interface (`cpufreq_module_info_v2`, a module name ending in `/v2`); `cpufreq_module_info` did not change, and a version 1 module still follows the scheduler mode. Setting a profile sets the scheduler mode (Power saver is power saving, the others low latency), the idle module's mode and the module's profile, without also calling the module's version 1 hook. The information block says which sleep state the firmware offers (FADT low-power S0 plus a `PNP0D80` device, or `\_S3_`); "sleep works" stays off until step 4.
- `amd_pstates` (now `/v2`): Power saver = max nominal, EPP `AMD_CPPC_EPP_BALANCE_POWERSAVE` (0xbf; until now power saving used balance-performance, 0x80); Balanced = max highest, EPP balance-performance; Performance = max highest, EPP performance. Values from `CPPC_CAP1` per CPU. Without the CPPC MSR the module now tries the ACPI `_CPC` registers (`amd_cppc_acpi.cpp`, written from the ACPI specification): system memory and generic PCC subspaces (PCCT types 0 to 2) only, the same limits and EPP, desired 0 under autonomous selection. Anything else makes the init fail: no module, gray menus. The ACPI and device-manager modules are loaded with `get_module()` only on that path, so the MSR path has no new dependency. Mistake fixed: the `R5 CPPC:` line printed min and max the wrong way round (max is bits 0-7 of the request); it now also prints the profile.
- `intel_pstates` copied into `distro/` (`/v2`): HWP Power saver = max guaranteed, EPP halfway between the SDM's balance (0x80) and energy (0xff) ends; Balanced = highest, 0x80; Performance = highest, 0. Without HWP, Power saver keeps the P-state below the boost. One `R5 HWP:` line per CPU.
- Service: Haiku's `power_daemon` as air/OS changed it, in `distro/power/server`, with R5's policy: the machine file `B_SYSTEM_SETTINGS_DIRECTORY/power` (driver_settings, found with `find_directory()`), written only by the service after an admin request on its port (`r5_admin_port_start()`; the sender is checked with the new `r5_admin_sender_allowed()`, added at the end of `libr5admin.so`); the profile per power source; "Turn off the display after" through `idle_time()` and `BScreen::SetDPMS()`, on again at the next input; the lid action (display off; sleep falls back to display off); the power button (shut down, ask, nothing; sleep falls back to ask). The AC state is also read on every tick, because a read by another program can clear the device's "updated" flag. Watchers get `kMsgPowerChanged`. air/OS's sleep, deep sleep, network handling, display-layout call and thermal monitor are not taken (`THIRD_PARTY_NOTICES.md`, Power service).
- `libr5power.so` (`distro/power/lib`, both images): `BPowerRoster` and `kMsgPowerChanged` (`docs/LIBR5.md`, "Power Kit"; R5Book `power-kit.html`). PowerStatus's `Battery`, `RateBuffer` and driver search moved in as `BPrivate::BatteryReader`; PowerStatus and its Deskbar item now read through it (owner, 22 Nov 2026: in step 2, not step 3). Fixed on the way: the battery cache was compared before it was ever filled.
- Settings: the Power page under System (`PowerModule.cpp`), as the mockup: Battery, Sleep, Performance with the brightness slider (owner: in step 2), Wake. Profile, Sleep and Wake are gray until the lock is open; Sleep and Wake stay gray until sleep works. The icon is `application/x-vnd.Haiku-PowerStatus` with the wrench, in `module_icon()`. CPU Info shows a "Power profile" line (profile, method, sleep state).
- Behaviour changes: the default profile is Balanced on both power sources (was the low-latency mode, which is Performance); the power button asks before it shuts down by default (POWER.md section 7: Ask while sleep does not work).

Limits: a processor with only ACPI `_PSS` has no profile yet (gray menus). The `_CPC` path was not run on any computer: the 5625U has the MSR and QEMU has no `_CPC`. "Keep working with an external display" can only count screens; R5's app_server cannot tell the built-in panel apart. Dimming on battery, `_BCM`, thermal trips and the low-battery notification are step 3. `SetDPMS()` and `SetBrightness()` only act where the accelerant has the hook.

Mistakes found on the way: the first jam failed in `PowerModule.cpp` (`B_SAFEMODE_SAFE_MODE` is in `<driver_settings.h>`, not `<safemode_defs.h>`; `PowerView::Show(const BMessage&)` hid `BView::Show()`, now `ShowSettings()`). The first QEMU boot ran an image another session had rebuilt in the shared tree after this jam (no Power page); the image is now copied inside the build lock. In QEMU the Performance box was cut off under the profile text (a `BTextView` gives no height for wrapped text in a layout); it now asks for four lines. The `_CPC` path was silent when it gave up; it now logs why.

Tested (22 Nov 2026): compiled (`build-locked.sh`, jam_exit=0, mesa_exit=0). QEMU with KVM, `-cpu host,+smap,+smep` (SMAP on in CPU Info), own copy of the image, window: no panic. Serial: `R5 CPPC: no CPPC MSR, trying the ACPI _CPC registers`, `R5 CPPC: _CPC found for 0 of 2 CPUs`, no cpufreq module (QEMU has no CPPC). Settings, System, Power (owner watching): no Battery box and no lid row (QEMU has neither), one column, profile menus gray with the no-speed line, Sleep and Wake gray, Wake says S3 (QEMU offers `_S3_`), the button on Ask. "Turn off the display after" set to 1 minute: the service logged `power_daemon: settings changed` and wrote `/boot/system/settings/power` with `display_off_on_ac 1`; Revert and Defaults turned on. CPU Info: "Balanced, no speed control, profiles off, sleep: S3 offered, not working yet", so the service set Balanced through the new syscall (the kernel's own start is Performance).

Not shown in QEMU: that the display goes off (QEMU's display and the timer were not watched long enough), the brightness (QEMU reports a brightness, so the slider shows at 100 %, but nothing visible changes), the profile on real hardware (the stick: `R5 CPPC:` lines with `profile`, and CPU Info, on the 5625U), PowerStatus with a battery.
## 2026-11-22 - Card reader after stick 75: SD 2.0 timing on the RTS5228, failed commands in the log, a bounded card scan

Problem (stick 75, 5625U, `generated/boot-5625u-75/` in the stick worktree): the RTS5228 attached and saw the card, but `CMD0` passed, `CMD8` ended after exactly one second with neither "done" nor "failed" from the chip, and `MMC CMD1 failed`; no `/dev/disk/mmc`. After the card went out and in again: four times `cannot power the card slot`, then `Card did not enter ACMD mode` / `Card is busy` 489 times. The owner notes that this reader also works badly in Windows.

Causes:
- `CMD8` (likely, not proven): the RTS5228 power-on sequence (spec section 3.2, step 11) leaves the SD engine in SD 3.0 mode (`SD_CFG1` bits `0x18`) with the CRC clock from variable-phase clock 0 (`0xFC2E` = `0x21`), which only SDR50 and SDR104 set up. The RTS5228 branch of `rtsx_bus_clock()` skipped OpenBSD's `rtsx_switch_sd_clock()`, which puts every other chip in SD 2.0 timing. `CMD0` needs no answer, so it completed; `CMD8` waits for an answer and its CRC, and the chip never ended it. The earlier `docs/RTSX.md` already named this as the first place to look. It fits the log; only the next stick shows whether it was the cause.
- 489 tries (sure: the code shows it): Haiku's `mmc_bus` ignored a failed `CMD55` or `ACMD41` and looped on with an old or uninitialized OCR, every 100 ms, also after the card was gone. A failed scan ended with `break` or `return`, which stopped the scan thread for good, and a card put in later got no `CMD0`.
- `cannot power the card slot` after the reinsert: not known; the log did not say which step failed.

What changed:
- `rtsx.cpp` (R5 snippet): on the RTS5228, every clock change first sets OpenBSD's SD 2.0 timing: SD 2.0 mode, clock source `0x24` (CRC on the fixed clock), RX on the rising edge, TX on the falling edge (spec section 7.9: the SD path is the same as on the older chips).
- `rtsx_haiku.cpp`: a failed command is logged with its argument and one line of chip state (`bipr`, `bier`, SD configuration, status and transfer registers, clock registers, and on the RTS5228 the LDO and overcurrent status): every failure up to 32, then one in 256; a removed card is not logged. "cannot power the card slot" names the power or clock step and prints the same state.
- `rtsx_5228.cpp`: a failing step names its line.
- `mmc_bus.cpp` copied into `distro/kernel` (Haiku, MIT; R5 snippets; in `install_rtsx()` of `scripts/apply-kernel-bugs.py`): `CMD0` at the start of every scan; `CMD1` and `ACMD41` stop after 20 tries (2 s; the SD specification gives the card 1 s); a failed `CMD55` or `ACMD41` ends the scan; `ocr` starts at 0; a failed scan, a card outside the voltage range and a card that does not enter the data state wait for the next card instead of ending the thread. This also applies to sdhci readers.
- `docs/RTSX.md`: sections "SD 2.0 timing on the RTS5228", "Diagnostics" and "mmc_bus".

Limits: the SD 2.0 timing is a fix by reasoning from the log, the spec and OpenBSD; it is not shown on the 5625U. If `CMD8` still fails, the state line now says where. The slot power failure after a reinsert is not fixed, only logged. The reader works badly in Windows too, so the card or the slot may share the blame; a second card is worth trying.

Tested (22 Nov 2026): compiled (`scripts/build-locked.sh`, `jam_exit=0`; `mmc_bus.o`, `rtsx.o`, `rtsx_5228.o` and `rtsx_haiku.o` rebuilt, no warnings). QEMU (KVM, own copy of the image) with an emulated sdhci reader and a 64 MiB FAT card: `mmc_bus` ran `CMD0`, `CMD8`, one `CMD55`/`ACMD41`, got RCA `664f`, `mmc_disk` set 4-bit mode and published `disk/mmc/0/raw`, with no errors; the desktop started, no panic. QEMU has no RTS5228; a card removal and a reinsert in the loop were not run in QEMU.

## 2026-11-22 - Card reader: a register timeout is reported as one

Problem: the countdown loops taken from OpenBSD (`while (tries--)`) end with the counter at -1 when they run out and at 0 when the last try succeeds. The checks tested `== 0`, so a real timeout in `rtsx_read()`, `rtsx_read_cfg()`, `rtsx_read_phy()` and `rtsx_write_phy()` was taken as success, and a success on the last try as a timeout. Found on the paper check for stick 75. The loops are bounded either way, so this never hung.

What changed: those four checks test `< 0` (R5 snippets in `rtsx.cpp`).

Limits: not on stick 75 (built before this fix); it only changes the error case. `rtsx_read_cfg()` also has `func & 0x03 << 4` (`func & 0x30`), as OpenBSD; the only caller passes 0, so it is left as it is.

Tested: compiled (`scripts/build-locked.sh`, `jam_exit=0`, `rtsx.o` rebuilt). Not booted.

## 2026-11-22 - Card reader after stick 74: the RTS5228 is probed

Problem: on stick 74 (5625U, SD card in before power on, then out and in again) there was no `rtsx` or `mmc` line in the syslog and no `/dev/disk/mmc`, although the add-on was in the image. The reader is `10ec:5228`, class `ff/00/00`, subsystem `103c:8a31`.

Cause (sure: the device manager code shows it, and no `rtsx` line at all means `supports_device()` never ran or never matched; the attach always logs one line): a PCI device node is `B_FIND_CHILD_ON_DEMAND`. It is only probed when `devfs` scans a directory, and `device_node::Probe()` matches the path against a short list of PCI classes. For `disk` that list was mass storage and the two SD host classes, so a class `0xff` node was never probed. The R5 snippet in `_GetNextDriverPath()` that adds `busses/mmc` for class `0xff` was therefore never reached. `supports_device()` (Realtek, class `0xff`, a chip in `kChips`), the module names and the package lines were correct, and the device_manager copy was in the built kernel (`install_rtsx()` in `scripts/apply-kernel-bugs.py` on the stick branch).

What changed: `device_manager.cpp` (R5 snippet): `Probe("disk")` also matches a PCI device of class `0xff` (`PCI_undefined`). Such a node then gets the paths `drivers`, `busses/virtio` and `busses/mmc` once per scan; drivers there check their own vendor and device IDs. `docs/RTSX.md` says so.

Limits: insert and remove, and a read and write on a FAT card, can only be shown on the 5625U; QEMU has no RTS5228. Removing a card does not take its disk away yet (`mmc_bus` cannot unpublish a card; the old disk only returns errors), as already noted in the driver; a card put back in is announced to `mmc_bus` again.

Tested (22 Nov 2026): compiled with `scripts/build-locked.sh` on `claude/rtsx-card-reader` (merged with `origin/main`), `jam_exit=0`; `device_manager.o` and the kernel were rebuilt. QEMU (KVM, own copy of the image): first start, Sign in and the desktop, no panic and no KDL in the serial log. QEMU has no RTS5228, so attach, insert, remove, read and write were not run.

Tested on the 5625U (stick 75, 22 Nov 2026, branch `r5/stick-75`, SD card in before the start; `generated/boot-5625u-75/`): the probe fix works. `rtsx: RTS5228 (10ec:5228), MSI vector 58`, `rtsx: RTS5228 version B, ASPM 0x2, L1SS 0x5, LTR 1`, `mmc_bus` created, `rtsx: card inserted` at 2.15 s, and `rtsx: card removed` when the owner took it out. The card did not come up: `CMD0` passed, then `Card does not implement CMD8` and `MMC CMD1 failed` (every SD card since version 2 answers CMD8, so a command or its answer is lost), no `/dev/disk/mmc`. After the card went back in: four times `rtsx: cannot power the card slot`, then `Card did not enter ACMD mode` / `Card is busy` 489 times. The reason is not in the log: a failed command is only logged with `TRACE_RTSX`. The owner notes that this reader works badly in Windows too, so the card or the slot may share the blame. Next: log every failed command with its status (not only under `TRACE_RTSX`), stop the `mmc_bus` retry loop after a bounded number of tries, find why the slot power fails after a removal, and try a second card. Insert, remove, read and write on a card are not shown.

## 2026-11-22 - SD cards in Realtek card readers, including the RTS5228

Problem: the HP ENVY x360 15 (Ryzen 5 5625U) has a Realtek RTS5228 card reader (PCI `10ec:5228`, class `0xff`) and R5 had no driver for it. The closed PR #4118 had an OpenBSD base but its 5228 part came from the GPL Linux driver.

What changed: new `busses/mmc/rtsx` driver. The base is OpenBSD rtsx(4) (ISC) on Haiku's `mmc_bus_interface`, with one device table: the OpenBSD chips (RTS5209, 5227, 5229, 522A, 5249, 525A, RTL8402, 8411, 8411B, exactly the list of OpenBSD `rtsx_pci.c` 1.16, their code paths unchanged) and the RTS5228. The RTS5228 part (init, slot power, clock and SSC, card detect, overcurrent, shutdown hook) is MIT, written clean room from `RTS5228-SPEC.md`; no Linux source and no Linux-derived file of #4118 was read. `device_manager.cpp` (copied into `distro/kernel`, MIT snippet) now searches `busses/mmc` for PCI class `0xff`. The driver is in the system package (regular and minimum image) and is a boot module. Attach logs the chip once; the OpenBSD chips say `(experimental)`. Docs: `docs/RTSX.md`, `docs/LICENSING.md`, `THIRD_PARTY_NOTICES.md`, About credits.

Limits: 400 kHz and 25 MHz, 4-bit, 3.3 V only; no high speed, UHS-I, SDIO, suspend or runtime power management (the RTS5228 keeps ASPM forced off). `mmc_bus` does not unpublish a removed card: the old disk stays and returns errors; unmount before removing. A card pulled out during `mmc_bus`'s ACMD41 loop can leave that loop running until a card is inserted. The RTS5228 write path does not compare the echoed byte (the spec does not promise it). The OpenBSD chips are experimental: buildable, not tested on R5. Later work: RTS5208, 5260, 5261, 5264, RTS5765DL (neither OpenBSD nor the spec covers them).

Tested: `scripts/build-locked.sh` jam exit 0, no warnings in `rtsx`; `rtsx` and its boot-module link are in `haiku.hpkg`. QEMU (no Realtek reader) boots to the first-start window, no rtsx line in the serial log, no panic. Not tested: the RTS5228 on the 5625U (card at boot, inserted later, removed, read and write); that needs the stick.

## 2026-11-22 - Shutdown: fix app_server syslog crash after syslog_daemon exits, preserve EFI panic logs

Problem:
- On stick 73 boot 5 (5625U, RADV), shutdown stopped before `enter reboot` and opened the CLI debugger on screen (`debugger for team 108 on interface Local (r5os)` / `[thread stopped: 141 "compositor"]`). The last syslog lines were `hda` freeing MSI vectors 62 and 63 (normal `media_addon_server` exit, also present in boot 6), and `app_server-stderr.old.log` ended right after `compose median 308 us cpu 161 ms per 10 s` (`Compositor::_Report()`).
- Root cause: `haiku/src/system/libroot/posix/syslog.cpp` (`send_syslog_message`) passes `va_list args` to `vsnprintf` without `va_copy`, then—when `write_port` fails after `syslog_daemon` exits in `BACKGROUND_APP_TERMINATION_PHASE` and `LOG_CONS` is set (the default `sTeamContext` inherited by `app_server`'s `"compositor"` thread)—passes the already-consumed `args` to `message_to_console` (`vfprintf`). On x86_64 SysV ABI this dereferences a garbage `%s` pointer and triggers `SIGSEGV`. `debug_server` then stops `app_server` in the CLI debugger, blocking `ShutdownProcess` before `_kern_shutdown()`.
- Separately, on UEFI boots `efi/debug.cpp` allocated a fresh random 1 MiB debug ring buffer on every boot and never recovered `previous_debug_output` (unlike `bios_ia32/debug.cpp`), and `syslog_init_post_modules()` only wrote `/var/log/previous_syslog` without logging any previous panic into the new boot's `syslog` or pulling it in `read-stick-logs.py`.

What changed:
- `distro/kernel/src/system/libroot/posix/syslog.cpp` (copied from Haiku, R5 change marked SPDX-Snippet MIT, wired in `scripts/apply-kernel-bugs.py`): use `va_copy` / `va_end` before `vsnprintf` and before both `message_to_console` paths (`LOG_PERROR`/`port < 0` and the `write_port` failure `LOG_CONS` fallback).
- `distro/kernel/src/system/boot/platform/efi/{efi_platform.h,mmu.cpp,debug.cpp,start.cpp}` (copied from Haiku, SPDX-Snippet MIT): add `platform_allocate_region_at()` and mirror `bios_ia32/debug.cpp` by trying the fixed 63 MiB physical address for the 1 MiB debug syslog ring buffer (falling back to any address if occupied), recovering `previous_debug_output` on warm reboot, and fixing up `gKernelArgs.previous_debug_output` in `convert_kernel_args()`.
- `distro/kernel/src/system/kernel/debug/debug.cpp` (copied from Haiku, SPDX-Snippet MIT): in `syslog_init_post_modules()`, after writing `/var/log/previous_syslog`, scan the recovered buffer for `PANIC:`, `Welcome to Kernel Debugging Land`, or (if no kernel panic) `debug_server: Thread `, and log up to 4 KiB / 64 lines via `dprintf("r5 previous panic: ...")` into the new boot's `syslog` without changing KDL itself.
- `scripts/read-stick-logs.py`: add `system/var/log/previous_syslog` to `WANTED`.

Tested:
- Built AMD64 with `scripts/build-locked.sh` (`jam_exit=0`).
- Booted private copy `/home/nickr/r5-os-part-b.image` in QEMU (`-machine q35,accel=kvm -cpu host -m 2048 -smp 4 -display none`): reached desktop (`R5 tracker: Tracker from distro`, `R5 deskbar: BarApp from distro`), then ACPI `system_powerdown` completed cleanly through `r5 shutdown: enter reboot=0 -> sync_done +80ms -> hooks_start +95ms -> hooks_done +97ms -> acpi_poweroff` with no crash or KDL.

Limits:
- Tested on BIOS QEMU only; UEFI 63 MiB warm-reboot persistence on the 5625U (whether HP firmware preserves 63 MiB across warm reset; cold power-off clears RAM) and real 5625U RADV shutdown with `gpu_debug true` need a stick test. ARM64 and RISC-V were not built.

## 2026-11-22 - Perf round 2 Part A: Lavapipe breakdown counters, parallel pipeline compilation, ordered recordings

Problem:
- On stick 73 boots 3 and 4 (5625U, `gpu false`, Lavapipe), cold warm-up took 30.2 s (part 1 alone 20.0 s, part 3 9.5 s), warm-cache warm-up took 1.44 s, and the first 10 s of desktop use averaged 10.7 ms/run in `snap` and 4.8 ms/run in `deliver`.
- Measuring with new `gpu_debug`-gated counters in QEMU (`-smp 6`, Lavapipe, 256-bit AVX2) showed:
  - 97.4% of `snap` in the first 10 s of desktop use (`4.89 s` out of `5.03 s`) and 82% of warm-cache warm-up (`3.30 s` out of `4.01 s`) was synchronous `vkCreateGraphicsPipelines` on one thread (SkSL->SPIR-V + Lavapipe uncached SPIR-V->NIR->optimize), while steady-state `snap` CPU recording was only `98-144 us/run`.
  - Cold warm-up was dominated by llvmpipe `lp_state_fs.c` `generate_variant()` MCJIT inside `fence_wait` (~29 s serialized on `device->queue`).
  - Shipping a precompiled Mesa llvmpipe disk cache in the OS image cannot match arbitrary machines: `lp_disk_cache_create()` hashes `util_cpu_caps_t`'s first 4 dwords (`nr_cpus`, `max_cpus`, `x86_cpu_type`, `cacheline`, full ISA feature bitmask) plus `libvulkan_lvp.so` and `libLLVM.so` build-IDs and `GALLIVM_PERF`.
  - Benchmarking `gpu_lavapipe_threads` 2, 4, 6, 8 on 6 vCPUs (warm cache) showed 2 threads stays fastest: warm-up 4005 ms (2) vs 4921 ms (4), 5318 ms (6), 6029 ms (8); scroll 27.0 ms (2) vs 27.7 ms (6), 29.5 ms (8); drag compose ~9.4-9.7 ms across all. Default stays 2.

What changed (Lavapipe only; RADV and Painter untouched):
- `distro/app_server/skia/SkiaGpu.{h,cpp}`, `SkiaPainter.cpp`, `SkiaText.cpp`, `SkiaOffscreen.cpp`, `Compositor.cpp`: add `gpu_debug`-only counters (`snap_parts`: targets, graphics/compute pipelines, shader modules, memory allocations; `ops`: text, path, bitmap, texture upload; `deliver_parts`: `fence_wait` vs `async_check`; and `gpu draw warm-up detail`). Zero wrapper overhead when `gpu_debug` is off.
- `distro/app_server/skia/SkiaGpu.cpp`: on CPU/Lavapipe devices (`fOnCpu`), enable `options.fRequireOrderedRecordings = true` (avoiding per-snap Recorder cache flushes; `RunWork()` always inserts FIFO) and attach a Haiku-threaded `PipelineExecutor : public SkExecutor` (`min(4, max(2, cpu_count / 2))` threads, `borrow()` supported) to `options.fExecutor` so SkSL->SPIR-V and Lavapipe's lock-free SPIR-V->NIR->optimize compile in parallel across cores.
- `distro/app_server/skia/SkiaOffscreen.cpp`: on Lavapipe, skip Pass 1 (`SetOpaque(false)`, which compiled 0 pipelines in measurement: 1 ms/part on 5625U, 3-4 ms/part in QEMU) and warm up opaque `alpha=255` `setColor` paths in Pass 0 (`m == 0`).

Measured (QEMU `-machine q35,accel=kvm -cpu host -m 2048 -smp 6`, `qemu-m13-measure.py`, cold + warm `--again`):
- Warm-cache warm-up: `4005 ms` -> `2964 ms` (**-26%**, 12 pipelines in 3208 ms CPU time across 3 threads).
- Cold warm-up: `47895 ms` -> `44064 ms` (**-8%**; remaining ~29 s is llvmpipe fragment-variant MCJIT inside `fence_wait`).
- First 10 s desktop `snap + insert`: `5.17 s` -> `3.81 s` (**-26%**); `text` 38.7 -> 18.6 ms, `bitmap` 479.4 -> 148.5 ms, `upload` 135.5 -> 41.4 ms.
- Cold drag `draw`: `4.9 ms` -> `3.3 ms`; cold resize p95: `38.8 ms` -> `37.0 ms` (input p95 `39.6 ms` -> `29.8 ms`).
- Also booted QEMU with `gpu false` in `/boot/home/config/settings/kernel/drivers/radeon_hd`: desktop reached cleanly, no panic/KDL.

Limits:
- Measured in QEMU (`-smp 6`), not yet on the 5625U (6 cores / 12 threads); `gpu_lavapipe_threads` 2 vs 4 and cold/warm warm-up on real hardware need stick verification. llvmpipe fragment-variant JIT in `fence_wait` remains serialized inside Mesa. ARM64 and RISC-V were not built.

## 2026-11-22 - Warm restart no longer hangs in OVMF firmware

Problem: under QEMU + OVMF (`edk2-stable202511`+, Ubuntu `OVMF_CODE_4M.fd`), a warm reset (`system_reset`, RAM preserved) after R5 had booted hung inside OVMF's `CpuMpPei` (`RIP=0x7bee35d3`, `IoRead32(0x608)` inside `TimedWaitForApFinish` with timeout `0xFFFFFFFF` us ≈ 71 min), even though all 5 APs were already awake and parked in `HLT` (`FinishedCount == 5`). Disassembly and a GDB watchpoint on OVMF's PEI PCD database (`0x00814b00`, `PcdCpuBootLogicalProcessorNumber`) traced this to an upstream OVMF IGVM bug: `PlatformPei` reads physical `0x008120c0` (IGVM `VpCount`, inside the 4 KiB IGVM Parameter Area at `0x00812000`, plus `0x00812000` for `MemoryMap` and `0x00813000` for IGVM HOBs), yet OVMF's EFI memory map hands `0x0080c000-0x00814000` (32 KiB) to the OS as `EfiConventionalMemory` and does not zero it on warm reset. When R5's VM allocator used physical page `0x00812000` and left a non-zero value `<= 0xffff` at `0x008120c0` (observed `0x00000800` = 2048 and `0x000001b8` = 440 on stick 74), `PlatformPei` skipped `fw_cfg` and set `PcdCpuBootLogicalProcessorNumber` to that stale value. On clean `main` without #4498, `0x008120c0` happened to remain `0x00000000` at 25-60 s because physical allocations shifted without the 1 MiB 63 MiB ring, so `main` did not trigger it in 8 test resets, though `0x00812000` was still exposed as usable RAM.

What changed: `distro/kernel/src/system/boot/platform/efi/arch/x86_64/arch_mmu.cpp` adds `reserve_ovmf_memfd_holes()`, called from `arch_mmu_post_efi_setup()`. When `kSystemTable->FirmwareVendor` is `"EDK II"` and the EFI memory map has OVMF's `EfiACPIMemoryNVS` MEMFD blocks covering `0x00800000` and `0x00820000`, any `EfiConventionalMemory` holes inside `0x00800000-0x00820000` (44 KiB total: `0x00808000-0x0080b000` and `0x0080c000-0x00814000`) are removed from `gKernelArgs.physical_memory_range` so the kernel never writes to them.

Limits: x86_64 EFI workaround for an upstream OVMF bug; non-EDK II firmware (such as the 5625U) is untouched. ARM64 and RISC-V were not changed or tested. Note on #4498 (tested separately, not in this change): once the OVMF hang is fixed, #4498 still does not recover `/var/log/previous_syslog` or log `r5 previous panic:` because `distro/kernel/src/system/boot/platform/efi/debug.cpp` computes `recover` in `allocate_ring_buffer()` without setting `gKernelArgs.previous_debug_size`, so `debug_cleanup()` skips reading the ring.

Tested:
- Built AMD64 anyboot via `scripts/build-locked.sh @r5-anyboot` (`jam_exit=0`, `mesa_exit=0`). `tree-owner.sh` cleanly restored 790 paths (including `headers/private/system/syscalls.h`) when switching from `R5OS-amd64-stick` to this worktree.
- QEMU (`-machine q35,accel=kvm -cpu host -m 4096 -smp 6`, `OVMF_CODE_4M.fd` + fresh `OVMF_VARS_4M.fd`, `-vga std -display none`):
  - Clean `main` control (without #4498): 2 runs of 60 s + `system_reset` passed, plus 6 consecutive warm resets passed (`0x008120c0 == 0`).
  - Stick 74 (`r5-74-anyboot.iso`) repro: hung in `CpuMpPei` on 5/8 first-attempt warm resets (`0x008120c0` = `0x800` or `0x1b8`).
  - Fixed `main` (`r5-fix-main.iso`): 2 runs of 60 s without KDL + 2 runs of 60 s with `Alt+SysRq+D` KDL all passed (`BdsDxe: starting` x2, `ps2: keyboard found` x2, `0x008120c0 == 0`).
  - Temporary test build of #4498 + this fix (`r5-fix-4498.iso`, reverted from branch): 2 runs without KDL + 2 runs with KDL all passed OVMF warm reset with `0x008120c0 == 0` (the 2nd KDL run was still mid-kernel-boot at the 25 s cutoff).
## 2026-11-22 - Microcode for every CPU R5 supports (#4550)

Problem: the image only had the AMD family 19h microcode. Every other AMD and Intel processor kept its BIOS microcode. The Intel loader is Haiku code that never ran on R5. Two faults showed up in the review and on the host: a Zen 5 (family 1Ah) patch is 14368 bytes and did not fit the 8192-byte aligned copy, so Zen 5 would never update (the old code missed all 9 Zen 5 entries in the host check), and an Intel file with `total_size` 0 kept `find_microcode_intel()` on the same patch forever (the host run of the old code hung until the timeout).

What changed:
- AMD families 15h, 16h, 17h and 1Ah next to 19h, unmodified from linux-firmware tag `20261019` (`fam1ah` exists there; `fam19h` is the same file as before). Not `microcode_amd.bin` (10h to 14h, below x86-64-v2). One AMD license covers all of them (one `WHENCE` entry, the same text as before).
- Intel: 97 files of `intel-ucode/`, family 06 from model 0x1A (Nehalem) up, Atom from Silvermont, unmodified from `intel/Intel-Linux-Processor-Microcode-Data-Files` release `microcode-20261110`. Left out: the Atoms before Silvermont (0x1C, 0x26, no SSE4.2) and `06-4f-01`, which Intel keeps in its folder with caveats. License: Intel's own text (binary only, unmodified, notice and disclaimer reproduced, no reverse engineering), owner accepted on 22 Nov 2026. It ships beside the files as `LICENSE.intel-ucode` and is in `LICENSES/`, `docs/LICENSING.md` and `THIRD_PARTY_NOTICES.md`.
- Image: `scripts/apply-amd-ucode.py` copies both folders and adds one block with Jam `Glob` to the minimum image, which the regular image includes. It removes the old family 19h block from the regular image when it finds it. The microcode on the image grows from 128,644 to 19,113,302 bytes (AMD 292,182, Intel 18,821,120), plus the two license texts.
- Kernel, Intel (`arch_cpu.cpp`, R5 snippets): the walk stops on a total size below the header and data or not in whole dwords; the extended signature table is read only when its header and all its entries lie inside `total_size` (and so inside the file); a matching patch that is not 16-byte aligned moves to the aligned start of the buffer instead of being skipped (a patch is up to 1.6 MB, too large for a static copy); the platform ID check (MSR 0x17) stays. Loading was checked against the SDM ("Microcode Update Facilities"): revision read (0 to 0x8B, CPUID 1, read 0x8B) and the 0x79 write with the address after the header were right; an update now counts as loaded only when the CPU reports the patch's own revision, as Linux does. New lines: `CPU n: already at revision 0x…`, and `CPU n: no update found (revision 0x…)`, so a BIOS newer than the file shows.
- Kernel, AMD: the aligned copy is 16 KiB.
- `scripts/fuzz-microcode.sh`: takes `find_microcode_intel()` and `find_microcode_amd()` unchanged out of `arch_cpu.cpp`, checks that every shipped file is found for its own signature, and fuzzes both under AddressSanitizer.
- `docs/KERNEL.md`, "Microcode": families, where the files are, log lines, how to update.

Tested: `scripts/build-locked.sh` jam 0, every Jam line once after apply. Host check: 102 files, 0 failures (all 157 files of the Intel release also pass, except `06-03-02`, a Pentium II that is not shipped). Fuzz 60 s each: Intel 173,027 rounds, AMD 997,407 rounds, no fault. QEMU (own copy, TCG, `-cpu EPYC`): `ucode_load: …amd-ucode/microcode_amd_fam17h.bin`, `microcode file read in memory`. QEMU `-cpu Skylake-Client`: `ucode_load: …intel-ucode/06-5e-03`, `microcode file read in memory`. QEMU `-cpu Nehalem,+smap,+smep`, 590 s: `ucode_load: …intel-ucode/06-1a-03`, `couldn't find microcode` (Intel ships only steppings 04 and 05), `CPU: no microcode provided`, no panic, and the desktop with the first-start welcome window. Under QEMU the kernel applies nothing (hypervisor), as expected.

Limits: no family has booted on hardware with this change. Owner tests proposed: the 5625U still logs `updated from revision 0xa50000f to 0xa500012` (or `already at revision`); the X1 Carbon Gen 8 logs `ucode_load: …intel-ucode/06-8e-0c` and `updated from revision … to …` on CPU 0. If the X1's BIOS is newer than the file, the log shows `no update found (revision …)`: that proves the loader, not the update. QEMU and VirtualBox apply nothing (hypervisor). No SHA-256 check of AMD patches as Linux 6.14 and newer do; the files are taken unmodified from linux-firmware.

## 2026-11-22 - README: highlights up front, and owner results on the 5625U

Problem: the README only told what R5 can do after several long sections, still listed Skia drawing, Wi-Fi 6, WPA3 and the AMD64 CPU work as coming next, and showed build 10064.

What changed: `README.md` opens with an "At a glance" list in a cheekier tone, and the feature sections are shorter. Skia window drawing (build 10066), the R5 window frame, Wi-Fi 6 with WPA3, game controller rumble and trigger effects, and the AMD64 CPU tuning (ongoing) are described as what R5 does now. "What's coming next" lists HiDPI, the Deskbar redesign, Bluetooth LE Audio and Auracast, and more GPUs and Wi-Fi cards. MIDI is described as "MIDI 2.0 support", not "full": the open points are #4519 (no real USB MIDI 2.0 device tested), #4520 (MIDI 1.0 devices as UMP for MIDI 2.0 programs) and #4521 (Appendix D.2.8 messages). Current build is 10066.

Tested (owner, 22 Nov 2026, Ryzen 5 5625U): Wi-Fi 6 works on the MT7921; game controller rumble works; DualSense trigger effects work. Motion sensing was not reported and the README does not claim it.

Limits: text only. No speed numbers for the CPU work.

## 2026-11-22 - CPU Info's icon is sharp again

Problem: the icon in CPU Info's heading looked jagged. CPU Info rendered it at `ComposeIconSize(40).Width()` (39 pixels at the default font), and `BPageHeader` draws its icon in a square of 48 units, so the bitmap was stretched. CPU Info followed `docs/LIBR5.md` and R5Book, which said `ComposeIconSize(40)`; the class has drawn at 48 units all along.

What changed: CPU Info renders the icon at `be_plain_font` size / 13 * 48, as Settings does. `docs/LIBR5.md` (Page header) and R5Book `appearance-settings.html` now say what `BPageHeader` does and that a bitmap of another size is stretched. `BPageHeader` itself is unchanged.

Limits: `BPageHeader` still stretches a bitmap of the wrong size, and places the icon on a fractional row when the heading height is odd; drawing it pixel for pixel is left for the owner's go (`libr5ui.so`).

Tested: compiled in the next build; seen in QEMU (owner) before the fix.

## 2026-11-22 - The changelog is split per week

Problem: `CHANGELOG.md` had grown to 1.3 MB (392 entries), too large to read or search comfortably.

What changed: `scripts/changelog-archive.py` moves every finished ISO week, word for word, into `docs/changelog/YYYY-Www.md` (newest first) and keeps the list at the top of `CHANGELOG.md`. It writes nothing unless every entry is still there exactly once; a second run changes nothing. `AGENTS.md` (Tracker and changelog) says to search both and to run the script when a new week starts. First run: 185 entries into `2026-W44.md` and `2026-W46.md`; the 207 entries of week 47 stay.

Limits: the 29 documents that cite "CHANGELOG.md, <date>" were not changed; the date leads to the right week file through the list.

Tested: run twice on a copy of `origin/main`; an independent check found the same 392 entries before and after.

## 2026-11-22 - The desktop leaves RADV instead of crashing after a GPU reset

Problem: on stick 72 boot 2 (the GPU hang test), the hang was detected and reset (`gpu reset mode2 ok`, `gpu selftest after reset ok`), and then app_server stopped in the debugger: Skia Graphite `VulkanCommandBuffer::isFinished` called `abort()`, from `SkiaGpu::RunWork` → `checkAsyncWorkCompletion` on the compositor draw thread. After a reset that another client caused, app_server's fence waits fail (`DRM_IOCTL_SYNCOBJ_WAIT` returns `ECANCELED` in app_server-stderr.log), RADV reports that as an error other than `VK_ERROR_DEVICE_LOST`, and Graphite aborts on an unexpected fence status. The fallback of #4471 only ran when app_server's own submit failed.

What changed: `SkiaGpu::RunWork` asks its run fence (a zero-timeout wait) before Graphite polls its fences. Anything but signalled or not yet means the device is gone: the run returns "device lost" and the compositor leaves RADV the way a failed submit does. After a failed run-fence wait, Graphite is not polled either.

Limits: compiled only; not booted. The deeper cause (a lost client's syncobj wait returning `ECANCELED`, where Linux returns success and reports the loss through the context query) is in the R5 DRM library and kernel and is left for its own change. Proof: the next stick's hang-test boot must show "left radv" and no debugger.

## 2026-11-21 - Perf round 1 Part B: graphics, Lavapipe warm-up, libdrm-2.4.124-5

Problem: from docs/reviews/PERF-REVIEW-2026-11.md (#4484):
- libdrm-2.4.124-4-x86_64.hpkg still shipped an older libdrm.so without gpu_trace, so debug sticks logged 221,596+ successful ioctls unbuffered to an endlessly growing app_server-stderr.log (12.4 MB on stick 68).
- On Lavapipe, skia_warm_up() waited up to 30 s for WindowRunDone() before starting, did not log per-part durations or Mesa disk-cache before/after counts, and the boot splash did not wait for Lavapipe shader precompilation.
- PipelineStorage used one filename (skia-pipelines) for every device, so a Lavapipe start overwrote RADV cache with Lavapipe 312-byte stub header; store() also rewrote and rehashed even when unchanged.
- Compositor::_ReadZeroCopySwitch() opened radeon_hd settings from disk and logged every 10 s on the urgent compose thread even without gpu_debug.
- Compositor::NoteInput() used a 400 ms window, attributing idle mouse moves to unrelated 250 ms cursor blinks (inflating reported input latency).
- Lavapipe thread/vector/MSAA settings were not logged together for measurement.
- r5_gpu_core.cpp gpu_retire_thread woke every 1000 ms even when no GPU jobs were in flight.

What changed:
- Rebuilt libdrm.so.2.124.0 and libdrm_amdgpu.so.1.124.0 from distro/graphics/libdrm-r5 and packed distro/packages/x86_64/libdrm-2.4.124-5-x86_64.hpkg (verified by extracting lib/libdrm.so.2.124.0 from the .hpkg: sha256 16a193fddcda1146, pkg sha256 f6cfd26acb35838b, strings contain both gpu_debug and gpu_trace); updated build-gpu-packages.py, install-mesa-packages.sh, pin-mesa-packages.sh, check-egl-vendor.py, and distro/gpu/COMPAT.
- distro/app_server/Compositor.cpp: rotate app_server-stderr.log to .old per session (open mode w) with line buffering (_IOLBF); unset MESA_SHADER_CACHE_DISABLE before loading Vulkan; hold boot splash on Lavapipe until fSkia->WarmUpDone() (RADV unchanged); gate _ReadZeroCopySwitch() behind fDebug; tighten NoteInput() damage attribution window to 50 ms; log active Lavapipe threads/vector/msaa configuration.
- distro/app_server/skia/SkiaGpu.{h,cpp}: add WarmUpDone()/SetWarmUpDone(); key PipelineStorage file per vendor/device (skia-pipelines-<vendor>-<device>), skip load/store on CPU/Lavapipe (where lvp_CreatePipelineCache is a stub), and skip store() when size and FNV-1a checksum match.
- distro/app_server/skia/SkiaOffscreen.cpp: start Lavapipe warm-up immediately while the splash holds (no 30 s wait), log all 8 part durations and /boot/system/cache/app_server/mesa file/KB counts before and after warm-up, then call SetWarmUpDone(). RADV path unchanged.
- distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp: sleep with B_INFINITE_TIMEOUT in gpu_retire_thread() when no GPU jobs are in flight (woken immediately on job submit via r5_gpu_retire_wake()); keep 1 s watchdog timeout while jobs are live. Owner-approved (21 Nov 2026) pending 5625U stick verification with gpu_hang_test proving a hang is still detected and reset; documented in docs/GRAPHICS.md (GFX9 reset section). GFX9 ring/PSP/PTE/reset frame/stop contracts untouched.

Tested:
- Built AMD64 with scripts/build-locked.sh (jam_exit=0, mesa_exit=0, mesa_packages_ok, ok package paths, ok egl vendor).
- Booted private copy /home/nickr/r5-os-perf-b.image in QEMU (-machine q35,accel=kvm -cpu host -m 2048 -smp 2 -display none): Lavapipe desktop reached (R5 tracker: Tracker from distro, R5 deskbar: BarApp from distro); no panic/KDL.

Limits: the 5625U has not run this (RADV, libdrm-2.4.124-5 on hardware, and second-boot Lavapipe Mesa disk-cache hit rate need stick boots). ARM64 and RISC-V were not built (app_server and r5_gpu_core changes are arch-independent; libdrm-2.4.124-5 package is x86_64).

## 2026-11-21 - Perf round 1 Part C: servers, apps, Wi-Fi and Bluetooth

Problem: from docs/reviews/PERF-REVIEW-2026-11.md (#4484):
- Bluetooth SDPServer::Stop() closed only the listening socket while an active client recv() sat in a 30 s SO_RCVTIMEO, blocking shutdown for up to 30 s when a client stayed silent.
- TermView::_HandleInputMethodChanged() had a legacy debug-only snooze(100000) (100 ms) on every input-method update.
- Deskbar ExpandoMenuBar::monitor_team_windows() polled every 150 ms with no CPU/change measurement.
- mt7922wifi mtk_tick() polled RX rings every 10 ms even when completely idle with no frames and no scan/join active, with no empty-tick ratio counter.
- AirPlayServer listen_thread() woke every 1000 ms (poll timeout 1000) while idle.
- BFS block_cache had no measurement of unused_block_count / cached KiB.

What changed:
- distro/bluetooth/server/SDPServer.{h,cpp}: track active client socket (fClientSocket), call shutdown(SHUT_RDWR) in Stop(), poll in 500 ms steps up to the 30 s idle deadline while checking fIsShuttingDown, and skip the trailing snooze(50000) on shutdown.
- distro/apps/terminal/src/apps/terminal/TermView.cpp: remove the debug-only snooze(100000) in _HandleInputMethodChanged().
- distro/desktop/src/apps/deskbar/ExpandoMenuBar.cpp: add opt-in 30 s measurement (kernel setting r5_deskbar_stats true) logging iteration count, modified count, and thread CPU us; 150 ms interval unchanged pending numbers.
- distro/network/wlan/mt7922wifi/dev/mtk/if_mtkvar.h and if_mtk.c: count total/empty ticks, log periodic stats under MTK_DEBUG, and back off poll interval from 10 ms (hz/100) to 50 ms (hz/20) after 20 idle ticks with no RX/TX and no scan/join; mtk_intr() and traffic immediately reset back to 10 ms.
- distro/airplay/AirPlayServer.cpp: change listen_thread() poll(pfd, n, 1000) to poll(pfd, n, -1) (zero idle wakeups).
- distro/kernel/src/system/kernel/cache/block_cache.cpp (registered in scripts/apply-kernel-bugs.py): add opt-in 30 s measurement (kernel setting r5_bfs_cache_stats true) logging unused_block_count and unused KiB on block_cache_sync() (measurement only; no cache cap changed).

Tested:
- Built AMD64 with scripts/build-locked.sh (jam_exit=0, mesa_exit=0).
- Booted private copy /home/nickr/r5-os-perf-c.image in QEMU (-machine q35,accel=kvm -cpu host -m 2048 -smp 2 -display none): reached desktop (R5 tracker: Tracker from distro, R5 deskbar: BarApp from distro); no panic/KDL.

Limits: the 5625U has not run this (mt7922wifi, Bluetooth SDP with a real headset, and BFS cache stats need hardware). ARM64 and RISC-V were not built (all changes are arch-independent).

## 2026-11-21 - Perf round 1 Part A: boot, shutdown, kernel and drivers

Problem: from docs/reviews/PERF-REVIEW-2026-11.md (#4484):
- Two empty AMD FCH AHCI controllers (1022:7901) burned 3.03 s on every boot in AHCIPort::PortReset() waiting 1.0 s for TFD.BSY and 0.5 s for COMRESET.
- ahci.c hardcoded a 32-bit B_DMA_HIGH_ADDRESS even when CAP.S64A was set, forcing SCSI bounce buffers above 4 GiB. usb_disk also caps at 32-bit because usb_disk_do_io passes physical addresses directly to UHCI/OHCI/EHCI/xHCI without a per-HCD 64-bit DMA query (and xhci.cpp:1036 requires 32-bit DMA); usb_disk stays 32-bit to avoid DMA corruption on 32-bit USB controllers.
- r5_pstate_log ran a 1.0 s 100% CPU spin (burn_cpu) and four fsync passes (with KERN: in line_matters()) on every normal boot.
- i2c_hid and i2c_elan polled continuously at 1000 Hz (1 ms) when idle.
- r5_shutdown_trail never showed sync_done, hooks_start, or hooks_done on the next boot because BFS Inode::Sync() (_kern_fsync) flushes file data pages but not the BFS metadata journal (Volume::Sync()).
- package_daemon first-boot processing (CommitTransactionHandler::_PrepareFirstBootPackages) never wrote administrative/activated-packages, so boot 2+ fell back to scanning all 137 packages.
- arch_vm_direct_map_release_range() released write-combined RAM in 32-page (128 KiB) batches, broadcasting 3 SMP IPIs per batch (96 IPIs for a 4 MiB buffer).
- C-state residency and decoded AMD CPPC EPP values were not logged.

What changed:
- distro/kernel/src/add-ons/kernel/busses/scsi/ahci/ahci_port.cpp: skip the 1.0 s TFD.BSY wait when SStatus.DET == SSTS_PORT_DET_NODEV before reset (saves 2.0 s across the two empty AMD FCH AHCI controllers), while keeping the full 500 ms post-COMRESET link-up wait so slow or cold SATA drives are never missed.
- distro/kernel/src/add-ons/kernel/busses/scsi/ahci/ahci.c: probe BAR5 CAP.S64A in ahci_register_device() and set B_DMA_HIGH_ADDRESS to UINT64_MAX when 64-bit addressing is supported.
- distro/cpu/r5_pstate_log.cpp: gate execution behind kernel setting r5_pstate_log true or CLI --run/--now (and --watch for repeated passes); remove KERN: from line_matters().
- distro/kernel/src/add-ons/kernel/drivers/input/i2c_hid/HIDDevice.cpp and i2c_elan/ELANDevice.{h,cpp}: adaptive poll backoff (1 ms active -> 4 ms after 200 ms idle -> 16 ms after 1 s idle; GPIO interrupts remain the longer-term fix).
- distro/kernel/src/system/kernel/shutdown.cpp: add +Nms elapsed timestamps to shutdown trail steps and call sync() after sync_done and hooks_done so BFS commits the journal before reset/power-off.
- distro/rest/src/servers/package/CommitTransactionHandler.cpp: write initial administrative/activated-packages during _PrepareFirstBootPackages() on writable volumes (non-fatal on read-only media).
- distro/kernel/src/system/kernel/arch/x86/r5_direct_map.cpp: increase release batch to a static 1024-page (4 MiB) buffer under sReleaseBufLock (zero malloc() while address-space lock is held), preserving the #4478 unmap -> drain -> write-back -> shootdown order.
- distro/kernel/src/add-ons/kernel/power/cpuidle/x86_acpi_cstates/acpi_cpuidle.cpp: opt-in C-state residency counter (r5_cstate_stats true in kernel settings, 30 s summary from CPU 0, measurement only).
- distro/kernel/src/add-ons/kernel/power/cpufreq/amd_pstates/amd_pstates.cpp: include decoded epp, min, and max in the existing R5 CPPC: log line (measurement only).

Tested:
- Built AMD64 with scripts/build-locked.sh (jam_exit=0, mesa_exit=0).
- Booted private copy /home/nickr/r5-os-perf-a.image in QEMU (-machine q35,accel=kvm -cpu host -m 2048 -smp 2 -display none): Intel ICH9 AHCI 6-port controller probed real disk on port 0 (1.5Gb/s, Supports 64-bit Addressing: yes, zero SCSI bounce-buffer warnings) and reset 4 empty ports without hang; r5_pstate_log did not run burn_cpu; reached desktop (R5 tracker: Tracker from distro); no panic/KDL.

Limits: the 5625U has not run this. usb_disk remains 32-bit DMA until the USB bus exposes per-HCD 64-bit DMA capability. ARM64 and RISC-V were not built (AHCI, i2c_hid, package_daemon, and shutdown.cpp are arch-independent; r5_direct_map.cpp, acpi_cpuidle.cpp, and amd_pstates.cpp are AMD64/x86 only).

## 2026-11-21 - CS35L41 firmware loads after boot

Problem: the performance review (`docs/reviews/PERF-REVIEW-2026-11.md`, row 12) measured on stick 70 that the CS35L41 firmware download over I²C took about 2.68 s of the boot, synchronously, amplifier 0 then amplifier 1, before the driver finished probing.

What changed: the probe only powers the amplifiers up in bypass with the cap, as before, so the speakers play at once. With `cs35l41_firmware true` a low-priority thread then reads the EFI calibration and loads program, tuning and calibration into each amplifier while that amplifier is not playing (it waits while a stream runs). When the DSP reaches state 2 the amplifier switches to the protected path from its next stream. Logged: `DSP bypass until the firmware is loaded`, per amplifier `speaker protection on from the next stream, loaded in N ms`, and `firmware thread done: N of M amplifiers protected`. Any failure keeps bypass with the cap. Shutdown waits for the thread.

Not in parallel: both amplifiers sit on one I²C bus, so two loaders would only take turns on it. Instead each transfer now carries up to 32 data bytes instead of 16 (halved on the controller's refusal down to 4), which lowers the share of address bytes.

Limits: a stream that starts during the download of an amplifier waits until that amplifier is loaded (about 1.3 s, measured on stick 70), once per boot. Not booted.

Tested: built with `scripts/build-locked.sh` (jam exit 0). QEMU boots, no panic, `hda: 0 speaker amplifiers` (no CS35L41 there).

## 2026-11-21 - CS35L41 firmware: the firmware's own algorithm and its state control

Problem: stick 70 stopped at `firmware off (no firmware state control)`. The runtime table lists 2 algorithms, but the state control belongs to the firmware's own algorithm (ID = firmware ID 0x400a4), which is not in that table.

What changed (from `CS35L41-SPEC-2.md`, "Round 5 answers"): after the program loads, the driver registers the firmware's own algorithm from the ID header at 0x02800000: XM base from bytes 20-23, YM base from bytes 28-31. It is not a table entry and not counted in the table's "number of algorithms". A control now matches on algorithm, region type and its first stored name (case-sensitive, stored length); acked controls (type 0x1000) with the system flag (0x8000) are skipped. The state address stays 0x02800000 + (XM base + control offset) x 4, polled every 1 ms up to 15 ms for 2. The logging from the previous entry and the fallback to bypass stay. The firmware's own algorithm base also applies to tuning blocks and calibration controls that name it.

Tested: built with `scripts/build-locked.sh` (jam exit 0). Not booted (no stick for now).

## 2026-11-21 - CS35L41 firmware: log the algorithms and the state lookup

Problem: on stick 70 the tuning file and the factory calibration loaded on both amplifiers (left resistance 10050, right 10336, ambient 0, status 1), but both stopped at `firmware off (no firmware state control)` and fell back to bypass with the cap. The runtime table reported 2 algorithms and firmware id 0x400a4, the same number as the firmware algorithm, so the state control's algorithm base is the open question.

What changed: diagnostics only, no rule changed. When the firmware loads the driver logs once: every algorithm in the runtime table (id, version, XM and YM base and size), and for amplifier 0 every algorithm and control from the program's 0xF2 descriptors (id, name, control name, offset, region). The state lookup now says which step failed: algorithm not in the runtime table, algorithm not in the descriptors, control name not found, or region mismatch. Where the base of algorithm 0x400a4 comes from waits for the reader's answer (spec round 5); no rule is guessed.

Tested: built with `scripts/build-locked.sh` (jam exit 0). Not booted (no stick for now).

## 2026-11-21 - CS35L41 tuning file: block offsets in register bytes

Problem: on stick 69 with `cs35l41_firmware true` the program loaded on both amplifiers and the EFI calibration was read, but the tuning file failed with `cs35l41-dsp1-spk-prot-103c8a29-spkid1-l0.bin: block at word 322 is not aligned` (and r0), so the driver fell back to bypass with the cap. The loader treated a `.bin` block offset as DSP words and added it before the region formula.

What changed (from `CS35L41-SPEC-2.md`, "Round 4 answers"): the algorithm's base word goes through the region formula (packed XM/YM: region base + base word × 3, rounded down to 4) and the block offset, in bytes of register address, is added to that address as it is. Alignment is no longer an error, in the `.bin` or the `.wmfw` loader. A `.bin` block for PM or an unknown region is an error ("no algorithm region"), as the spec says; it was skipped before. A failing block is logged with its index, type, length and address.

I²C: a block that starts on a register and has whole registers is still split into transfers of 16 data bytes, each with its own address. A block that does not start on a register or is not whole registers cannot be split that way; it goes out in one transfer, which works only while it fits the controller FIFO; a longer one is logged and not written (falls back to bypass). The stored tuning files do not have such blocks: on the host, with the YM base word 14 from the stick 69 log, every block lands register-aligned (YM packed 0x02C0015C, 3000 bytes; YM unpacked 0x03401170, 12 bytes), inside the spec's YM ranges.

Tested: built with `scripts/build-locked.sh` (jam exit 0). Host check of the target addresses as above. Not booted; the next stick shows whether the tuning loads and the DSP reaches state 2.

## 2026-11-20 - GPIO module: keep AMD interrupt and wake status

Problem (review of #4482): `write_pin()` and `set_output_enabled()` in `generic/gpio/v1` read the AMD FCH pin register and wrote it back whole. Bits 28 (interrupt status) and 29 (wake status) are write-1-to-clear, so a set bit written back cleared a pending interrupt or wake on that pin.

What changed: both calls mask those two bits out of the value they write. `docs/GPIO.md` says so.

Limits: not jammed on its own (stick 68 was being built from 46ef9b8837 in the shared tree); the next build is the compile check. Stick 68 does not have this fix. For the CS35L41 reset pin, which is an output, the old code would only have cleared status bits nobody waits for.

## 2026-11-20 - CS35L41 phase 2: speaker protection firmware, opt-in

Problem: phase 1 plays in DSP bypass without speaker protection, so its gain is capped at the lowest step.

What changed:

- The cs35l41 driver can load and start the Cirrus speaker protection firmware (`CS35L41-SPEC.md` section 3, `CS35L41-SPEC-2.md` sections 2 and 4): it reads the `.wmfw` program and the `.bin` tuning, writes their blocks into DSP memory (XM/YM unpacked and packed, PM packed, absolute), reads the algorithm table, finds the firmware state control `HALO_STATE` in the program's descriptors, writes the stream rates, opens the DSP memory protection, starts the core, waits for state 2 (running, 15 ms), checks the mailbox and pauses the firmware. Playback resumes it with the DSP routing and VMON/IMON on, and pauses it after the stream. With the firmware running the gain is the default PCM gain (0x84; no tuning configuration file exists). Shutdown stops the core (watchdog off, core off, soft reset).
- Off by default: a `cs35l41` driver settings file with `cs35l41_firmware true` turns it on. Any failure falls back to bypass with the phase 1 cap and a log line.
- File search order from the spec, with firmware ID `103c8a29` for subsystem `103c:8a31` (the files Windows uses on this laptop) and the speaker ID from the GPIO. `scripts/apply-cirrus-fw.py` installs `distro/firmware/cirrus` unmodified (sha256 checked against `THIRD_PARTY_NOTICES.md`) with `LICENSE.cirrus`, and a symlink `cs35l41-dsp1-spk-prot-103c8a29.wmfw` to `halo_cspl_RAM_revB2_29.63.1.wmfw`.
- Factory calibration: the driver reads the EFI variable `SmartAmpCalibrationData` or `CirrusSmartAmpCalibrationData`, picks the entry by silicon ID or amplifier index, and writes `CAL_AMBIENT`, `CAL_R`, `CAL_STATUS` and `CAL_CHECKSUM` (resistance + 1) before the core starts. It reads the variable through `generic/efi/v1`, whose public header `EFI.h` is added now as an interface only (`docs/EFI.md`). The R5 kernel does not keep the EFI runtime services after boot, so the module does not exist yet: today the driver logs `no factory calibration: generic/efi/v1 is not available` once and runs with the firmware's default calibration. When the module lands, calibration works without a driver change. No boot-loader workaround was added.

Interpretations of the spec, to check on the stick: the DSP memory protection writes 0xFFFFFFFF to every 32-bit register of the union of the two ranges the spec gives (0x02BC3000-0x02BC305C); the DSP clock (0x2C08 = 3) is set before the download; a `.bin` block's DSP word is the algorithm base plus the block offset, then the region formula; a `.bin` absolute block at offset 0 for the firmware ID (the spec's "global region base", no address given) is skipped and logged. Firmware blocks go out in transfers of 16 data bytes (4 if the I²C controller refuses).

The firmware formats were also checked against the stored files on the host: the `.wmfw` has 4 XM packed, 1 YM packed, 1 PM packed and 3 descriptor blocks, `HALO_STATE` is control 0 of algorithm 0x400A4 in XM, and each tuning `.bin` has one YM packed and one YM unpacked block for algorithm 0xCD at word offsets that are multiples of 4.

Limits: not booted on the 5625U; the DSP path has never run. Calibration, suspend and resume are missing. QEMU has no CS35L41.

Tested: built with `scripts/build-locked.sh` (jam exit 0, also after the calibration and `EFI.h` were added); the image holds the nine Cirrus files and the symlink (their names are in `r5-os.image`). QEMU boots to the first-start window, no panic, `hda: 0 speaker amplifiers for subsystem 1af40022`.

## 2026-11-20 - CS35L41 speaker amplifiers, phase 1: DSP bypass with a hard limit

Problem: the internal speakers of the HP ENVY x360 15-ey0xxx (Ryzen 5 5625U, HDA `1002:1637`, codec subsystem `103c:8a31`) sit behind two Cirrus Logic CS35L41 amplifiers on I²C (ACPI `CSC3551`). Nothing powered them, so the speakers were silent; only the headphone jack played.

What changed:

- A vendor-neutral kernel interface between HD Audio and speaker amplifiers: `AudioAmp.h` and the module `generic/audio_amp/v1` (MIT, `docs/AUDIO-AMP.md`). An amplifier driver registers per codec subsystem. The R5 `hda` copy calls prepare before the playback stream runs, start after, stop before it stops and unprepare after; it mutes the amplifiers while the headphone jack senses a plug, and offers only the rates the amplifiers accept (48 kHz). Codecs without an analog output (HDMI) never call it. No Cirrus names on the HDA side.
- A new MIT driver `cs35l41` (`drivers/audio/hmulti`, x86_64). Haiku's `pch_i2c` registers a node for the first `I2cSerialBus` of an ACPI device only, so the driver reads every address from `_CRS` and talks to each amplifier through `i2c_bus_interface::exec_command()` with its own address. GPIO 0 of `_CRS` (the shared reset) is pulsed low for 2 ms unless the firmware already drives it high, and its state is logged once; GPIO 1 (speaker ID) is logged. `_SUB` selects the machine table (boost 1000 nH, 24 µF, 4100 mA; slots 0 and 1); an unknown machine leaves the amplifiers off.
- Power-up per amplifier: soft reset, OTP boot wait, chip ID and revision check, errata for 0xA0 or 0xB0/0xB2 (others are left off), the full OTP trim (99 fields, map A or B by OTP ID), initial mute, boost, chip GPIO pads, TDM slot. Playback uses the DSP bypass path. Faults (boost, temperature, short) are polled every 100 ms while playing; a fault mutes that amplifier until the next stream, three faults turn it off until restart. Each stop logs `0x00010010` and releases latched faults.
- A GPIO module for kernel drivers, the Haiku way: `generic/gpio/v1` with `GPIO.h` next to `ACPI.h` (MIT, `docs/GPIO.md`). It resolves a GpioIo or GpioInt resource from `_CRS` to a pin and reads it, drives it and sets its output enable. The vendor part is AMD FCH (same registers as `i2c_hid`'s interrupt line). The CS35L41 driver has no controller code of its own. `i2c_hid` is unchanged and can move to it later; Intel is a later item.
- The ACPI mute override (`_DSM` function 5, Notify `0x91`) mutes both amplifiers while it is set.
- Hard limit: without the DSP firmware there is no speaker protection. The PCM gain field of `0x00006C04` (bits 9:5) is written as 0, its lowest step, instead of the bypass default 4 (named 4.5 dB in the spec). The spec does not give the step size, so no dB value is claimed for the cap. No setting raises it. The driver logs once per boot that it runs in bypass with the cap.

Clean room: written from `generated/CS35L41-SPEC.md` and `CS35L41-SPEC-2.md` (reader Gemini, from Linux v7.3.5, facts only), `generated/CS35L41-DESIGN.md`, R5's `distro/` and Haiku's own code. No Linux code was opened by the builder. `docs/LICENSING.md` describes the method.

Limits: phase 1 has no DSP firmware, no speaker protection, no calibration, no suspend or resume. The firmware in `distro/firmware/cirrus` stays uninstalled. The first boot also decides whether the device manager probes the driver when media_server opens `/dev/audio/hmulti`, whether the reset GPIO is driven correctly, and whether the Realtek codec feeds the amplifiers without extra verbs. Volume is the codec's; only the amplifier's own gain is capped.

Mistake on the way: the first jam failed in `Driver.cpp` (`hmulti_audio.h` and the kernel `lock.h` both define `ASSERT`, and `ByteOrder.h` was missing). The rate now comes from `Amplifier.cpp`, which does not include `lock.h`.

Tested: built with `scripts/build-locked.sh` on AMD64 (jam exit 0; `cs35l41` and `audio_amp` are in `haiku.hpkg`). QEMU (KVM, `intel-hda` + `hda-duplex`, own copy of the image): boots to the first-start window, no panic; the serial log has `hda: 0 speaker amplifiers for subsystem 1af40022`. QEMU has no I²C controller and no `CSC3551`, so the CS35L41 driver itself never ran and no playback with amplifiers ran. The 5625U has not booted this. ARM64 and RISC-V were not built (the driver is packaged for x86_64 only).

## 2026-11-21 - EFI runtime services on the 5625U

Sticks 69 and 70 carried this branch (`11dd98fc7d`) together with the CS35L41 driver (#4482), on the stick branch only, and booted on the HP ENVY x360 15 (Ryzen 5 5625U) through UEFI. Both boots: `efi: 37383 pages identity mapped, 80 of 104 page tables, NX on` and `efi: runtime services ready, revision 2.70, 11 ranges, attributes table yes`. The CS35L41 driver read `CirrusSmartAmpCalibrationData` through `get_variable` (48 bytes: the header and one entry per amplifier), and on stick 70 applied it (left resistance 10050, right 10336). No fault, no "called with interrupts off", and the boot loader change started the system normally.

Limits: one laptop and one firmware vendor. AVX-512 is not present on the 5625U. ARM64 and RISC-V get the module absent (`arch_none.cpp`); they were not built. The fault recovery was shown in QEMU only. SetVariable is not implemented.

## 2026-11-20 - Stick 67: write-combined GTT on the 5625U, and M13 closes

Stick 67 is the head of #4478 (`84deb90fee`, merged as `25da2f35a3`), the regular stick settings (`gpu_draw`, `gpu_debug`, `gpu_selftest`) and no `gfx9_wc_gtt` line, so write-combined GTT was on by default. Boot 1: `gpu available`, K1 ok, scratch `0xDEADBEEF`, K2 seq 3, compositor, Skia and scanout on RADV. The 10 s lines counted write-combined buffer creates (`wc 12`, `wc 5`, `wc 175`, `wc 51`, `wc 42`). `/system/bin/r5_wc_gtt` printed PASS and the syslog has `R5 VM: wc direct map ok (48 areas)`. No panic, no page fault, no `radeon_hd: wait` line, no `left radv` and no `gpu copy off`. Boot 2 was meant to run with `gfx9_wc_gtt false`, but the settings file on the stick did not have that line, so boot 2 repeated boot 1 (K2 seq 6, `wc 37`, `wc 155`, `wc direct map ok (48 areas)`, no fault).

Limits: `gfx9_wc_gtt false` has not been booted. It uses the same `r5_gpu_switch_named` path as the other GPU switches. One laptop, two boots. The gain of write-combined GTT is not measured. ARM64 and RISC-V were not built.

M13 is closed (`docs/M13-PLAN.md`). The Lavapipe warm-up stays open as its own item.

## 2026-11-20 - Write-combined memory only for a wired RAM area

Review of #4478 (Jarvis): the physical map follows only the pages an area has when it becomes write-combined. A RAM area that is not wired (`B_FULL_LOCK` or `B_CONTIGUOUS`) can get a page later through a fault or copy on write, and `map_page()` takes no direct-map reference for it, so that page would keep a write-back alias. `vm_set_area_page_memory_type()` now refuses a special type for such an area with `B_NOT_SUPPORTED`, as `vm_clone_area()` already only carries the direct map for wired areas. Device areas are unchanged. Every caller in the tree uses a wired area: the GPU buffers and their user clones (`B_FULL_LOCK`), the direct-map self-test (`B_FULL_LOCK`) and the large-page self-test (`B_CONTIGUOUS`).

Tested: compiled with `scripts/build-locked.sh`. Not booted.

## 2026-11-20 - A non-write-back area is unmapped on every architecture

`unmap_page()` and `unmap_pages()` returned after `arch_vm_direct_map_release_range()` for every area that was not write-back. On AMD64 that function unmaps write-combined RAM itself. On ARM64, RISC-V and 32-bit x86 it is the empty stub, so a cut, a shrink, `vm_unmap_address_range()`, discarding a range, or `vm_change_cache_of_clones()` left a write-combined, uncached or device area mapped. On AMD64 the same path also ran `sfence` on every CPU for a framebuffer or an MMIO area, which has no physical-map entry to restore.

`unmap_page()` and `unmap_pages()` now always call the translation map. The direct-map release runs first, and only for a RAM or file area whose type is not write-back: it unmaps, drains every CPU, then restores write-back. The translation-map call then finds those entries gone. A device area, including a framebuffer, returns from the release without a drain or a shootdown, and the translation map unmaps it. On the other architectures the release is the empty stub, and `UnmapPage` / `UnmapPages` of that architecture unmap the area. `delete_area()` still releases and then calls `UnmapArea`.

Callers of `unmap_page()`: the copy-on-write fault. Callers of `unmap_pages()`: cutting the end, the head or the middle of an area, discarding a range, `vm_change_cache_of_clones()`, and shrinking an area. `vm_unmap_address_range()` cuts the area. `vm_delete_areas()` calls `delete_area()`.

Compiled: `scripts/build-locked.sh` exited 0 on 20 Nov 2026, about 14:05 Europe/Amsterdam. ARM64 and RISC-V were not built. The shared change is the call that was already how those architectures unmapped a write-back area.

QEMU, a private copy, KVM, host CPU, 2 CPUs, no display. With the product default the serial showed `using PAT for memory type configuration`, no `wc direct map` line, and `package_daemon`. The same image with only `r5_wc_direct_map_test true` in its kernel settings printed `R5 VM: wc direct map ok (32 areas)` and reached `package_daemon`. No panic. `/system/bin/r5_wc_gtt` was not started. QEMU has no `radeon_hd`. The 5625U has not run it.

## 2026-11-20 - Write-combined direct map: unmap first, then change the type

Review of `b2fa05bcce`. Turning write-combining on set the area's type and flushed through the physical map while that map was still write-back, so a prefetch could fill the line again before the physical map became write-combined. Releasing wrote write-back back into the physical map while the area's write-combined entries were still present, and other CPUs were not drained first. The reservation held one page-table page per buffer page for the whole call. The 32-area check ran on every AMD64 boot. A 1 GiB physical-map page would have been walked as a page directory.

What changed: `vm_set_area_page_memory_type()` unmaps the area and notes the physical pages, changes the physical map, shoots that TLB entry down, and runs `clflush` after the shootdown. Only then is the area mapped again. Leaving write-combining runs `sfence` on every CPU before the physical map returns to write-back. `delete_area()` does that release after the cache lock is held and before `UnmapArea`. A shrink, a cut, and `vm_unmap_address_range()` go through `unmap_pages()` and `unmap_page()`, which release the same way: unmap the batch, `sfence` on every CPU, then drop the count. `vm_delete_areas()` calls `delete_area()`. A failed clone puts the type back before `delete_area()`, so that release does nothing; the apply already dropped the counts it added.

The reservation is one page-table page per 2 MiB region the pages fall in. A 64 MiB run is 32 pages, not another 64 MiB. If the region bitmap cannot be allocated, the call reserves one page per buffer page. A 1 GiB physical-map page is refused and logged once (`R5 VM: wc direct map refuses a 1 GiB physical-map page`). It is not split. The EFI loader builds this map with 2 MiB pages.

The boot check is off unless the kernel setting `r5_wc_direct_map_test` is `true`. `gfx9_wc_gtt` and `gpu_debug` are graphics-driver settings, read after this init, and the check would split physical-map pages on VirtualBox, QEMU, the X1, and a machine with no GPU. The stick boots are unchanged: the default on, then `/system/bin/r5_wc_gtt` (48 areas), and a second boot with `gfx9_wc_gtt false`.

Compiled: `scripts/build-locked.sh` exited 0 on 20 Nov 2026, about 13:40 Europe/Amsterdam.

QEMU, a private copy, KVM, host CPU, 2 CPUs, no display. With the product default the serial showed `using PAT for memory type configuration` and no `wc direct map` line, and the boot reached `package_daemon`. The same image, with only `r5_wc_direct_map_test true` added to its kernel settings file, printed `R5 VM: wc direct map ok (32 areas)` and reached `package_daemon`. No panic, and no 1 GiB refusal. `/system/bin/r5_wc_gtt` was not started. QEMU has no `radeon_hd`. The 5625U has not run it.

A page that is not fully locked can be faulted in after the call. GTT areas are fully locked. A 1 GiB page is refused rather than split.

## 2026-11-20 - Write-combined direct map: review fixes

The first QEMU boot of this path passed 8 areas. Two reviews of that diff found the same holes. A freed hash slot was a tombstone, and the next insert used it only when the probe also saw an empty slot, so after one wave every later buffer failed. `malloc` and `free` of that table ran while its lock was held, and the kernel heap takes the address-space lock, which `apply` and `release` already hold. The page-table reservation was `pages / 512 + 2`, which is one run, not a scattered buffer. A failed call left a count it had only incremented. `Query()` of a hole returns physical address 0, and that was treated as RAM page 0. A fully locked write-combined RAM clone was mapped write-back and took no count.

A freed slot is reused. A prepare reserves hash and table space for its own pages before the next prepare, and grows that storage without holding the lock across `malloc` or `free`. The reservation is one page-table page per RAM page. A failed call drops every count it added. A hole is skipped. `vm_clone_area()` maps a fully locked or contiguous write-combined RAM clone with that type and takes its own count. The boot check is now 32 areas, two waves, and the clone's own page table must already be write-combined.

Compiled: `scripts/build-locked.sh` exited 0 on 20 Nov 2026, about 13:20 Europe/Amsterdam.

QEMU, on a private copy of that image (KVM, host CPU, 2 CPUs, no display): serial showed `using PAT for memory type configuration`, `R5 VM: amd64_large_pages enabled`, then `R5 VM: wc direct map ok (32 areas)`. The boot continued into `package_daemon`. No panic. `/system/bin/r5_wc_gtt` was not started in the guest. QEMU has no `radeon_hd`. The 5625U has not run it.

## 2026-11-20 - Write-combined GTT has no write-back alias, and is the default on the 5625U

Problem: a write-combined RAM area on AMD64 kept a write-back translation in the kernel physical map. Two memory types for one physical page are not allowed, and one clean boot does not prove otherwise. `gfx9_wc_gtt` stayed off for that reason. Stick 65 boot 3 ran it once, with the alias still there.

What changed: on AMD64, `vm_set_area_page_memory_type()` now follows Linux `set_pages_array_wc()`. After the existing `clflush` of the still write-back direct map, a 2 MiB direct-map page is split and those RAM pages become write-combined. A count per page covers the kernel area and the user clone. The last release writes the direct map back to write-back, shoots the TLB down, and collapses a table this code allocated when all 512 entries are write-back. A TTM-style pool was not used, because it needs the same change and must not return a write-combined page to the allocator. If the split cannot allocate, the area type is put back and the driver clears `R5_GPU_BO_WC`. The GPU page-table entry stays `MTYPE_NC` and snooped. ARM64, RISC-V and 32-bit x86 are unchanged.

`gfx9_wc_gtt` now uses `r5_gpu_switch_named`. On a release-supported GPU (the 5625U, `0x15e7`) it is on unless the setting says `false`. Every other GPU stays off unless the setting says `true`. `gpu false` still turns it off.

The boot check allocates and frees eight areas (4 KiB through 2 MiB plus one page, then a clone of one page), checks the bytes, and checks the direct-map type. The line is `R5 VM: wc direct map ok`. `r5_wc_direct_map_test false` skips it. `/system/bin/r5_wc_gtt` runs the same check for 48 areas. It is on the image. It was not started in the guest.

Compiled: `scripts/build-locked.sh` exited 0 on 20 Nov 2026, about 12:34 Europe/Amsterdam. The image is `generated.x86_64/r5-os.image`. The known translator header errors are still printed; jam still exited 0.

QEMU: a private copy of that image, KVM, host CPU, 2 CPUs, no display. Serial showed `using PAT for memory type configuration`, `R5 VM: amd64_large_pages enabled`, then `R5 VM: wc direct map ok (8 areas)`. The boot continued into `package_daemon`. No panic. QEMU has no `radeon_hd`, so this does not show a write-combined GTT buffer on the GPU. The 5625U has not run it.

Stick boots, not a change to `boot-next m13f`: one boot with no `gfx9_wc_gtt` line, and one with `gfx9_wc_gtt false`. Both should print `r5_wc_gtt PASS`. On the first, a USWC buffer is write-combined. On the second, it stays cached.

## 2026-11-20 - Direct scanout and the GPU copy are on by default on the 5625U

Problem: the compositor still copied every frame with the CPU on its way to the screen unless the radeon_hd settings said `gpu_app_server true` and `gpu_compositor_scanout true`. Stick 65 ran both clean on the 5625U (boot 2 of `boot-next m13f`).

What changed: `GET_INFO` has a new capability bit, `R5_GPU_CAP_RELEASE` (16), added at the end. The kernel sets it only together with `R5_GPU_CAP_AVAILABLE`, on a release-supported GPU (the default of a GPU switch, `r5_gpu_switch_named(NULL, ...)`), and never with `gpu false`. `GPUCopy::ReleaseDefault()` reads both bits and no `R5_GPU_CAP_LOST`. `gpu_app_server` and `gpu_compositor_scanout` now default to that value; `false` in the settings turns either off, and `true` still turns it on on any GPU. On every other GPU and on Lavapipe nothing changes. This replaces M10 decision 1 (the GPU copy stays opt-in) for release-supported GPUs, by the owner's choice of 20 Nov 2026.

`gfx9_wc_gtt` stays opt-in, although stick 65 boot 3 ran it clean: the kernel direct map stays write-back, so a write-combined area keeps a write-back alias (`docs/KERNEL.md`). It becomes the default after that is covered.

`GPUCopy::ReleaseDefault()` asks `GET_INFO` once per app_server start and keeps the answer. The compositor reads its settings again every 10 s for the M11 zero-copy test switch, and without that cache every read would also open the render node. A GPU that is lost later is handled by the copy and the compositor themselves, as before.

Tested: stick 66 (`88ce792b1d`), one cold boot on the 5625U with the stick settings (`gpu_draw`, `gpu_debug`, `gpu_selftest`) and no scanout or app_server line: `gpu available`, K1 ok, scratch `0xDEADBEEF`, K2 seq 3, `gpu compositor on radv`, `gpu draw on ... RADV RENOIR ... skia`, `scanout through vulkan 1920x1080`, `gpu copy on scanout`, warm-up 2086 ms, the self-test done with no failure, GLTeapot, VulkanTeapot and vkcube `device lost no`, no `left radv` and no `gpu copy off`. The cache was added after that stick; `scripts/build-locked.sh app_server` compiled it, and it has not been booted. One laptop, one boot.

## 2026-11-20 - Stick 65: the user-fence clear holds on the 5625U

Stick 65 is the head of #4475 (`56f8512a63`, merged as `3aa387f809`) with mesa_radv 25.3.6-11 repacked from it, `boot-next m13f`, five boots on the 5625U. On every boot K1 was ok (`client 20 src 181 ring 0`), scratch `0xDEADBEEF` landed, and the K2 fence landed (`k2_ib wb deadbeef seq N seen N`). The sequence started at 16777218 and then went to 21, 24 and 27, so the fence page still held old values from earlier boots. That is the case that stalled on sticks 61, 63 and 64.

- Boot 1 (defaults): compositor and Skia on RADV, warm-up 1988 ms.
- Boot 2 (`gpu_app_server`, `gpu_compositor_scanout`): RADV, scanout through Vulkan 1920x1080, warm-up 65 ms.
- Boot 3 (`gfx9_wc_gtt`): RADV, warm-up 64 ms.
- Boot 4 (`gpu false`): Lavapipe, warm-up 27943 ms (longest part 16383 ms).
- Boot 5 (`gpu_draw_settle`): RADV, warm-up 17 ms after the 8 s wait.

No `radeon_hd: wait` or `room` line, no `gpu draw off`, no `left radv` and no GPU fault on any boot. This records the boot that the GFX9 ring contract asks for, and `docs/GRAPHICS.md` now says so.

Limits: one laptop, one stick, five boots. Scanout and write-combined GTT stay opt-in; this stick is the first clean run of those two boots. The Lavapipe warm-up is still about 28 s. The logs are in `generated/boot-5625u-65/` (local).

## 2026-11-20 - GFX9 ring contract records the user-fence clear

The owner OK for the stores in `acf812183a` is now in `docs/GRAPHICS.md`, `GFX9 ring on GC 9.3 (fixed)`. Programming the ring stores 0 at `+2048` (user fence, 64 bit) and `+2064` (irq fence, 32 bit). The cond-exec dword at `+2080` is not written. The sequence slot at `+1088` is not cleared. `r5_next_sequence` is one more than the low 32 bits there, and K1, the K2 DMA and the K2 indirect buffer each take one. Sticks 60 and 62 ended at 3. Stick 61 ended at 16843012 (`0x01010104`). Stick 63 ended at 3132733624 (`0xBABABAB8`). Stick 64's four boots were 2, 5, 8 and 11, three apart, from an all-ones slot that wrapped. The user fence on those boots read `0xffffffffffffffff`. The 5625U has not booted the clear.

## 2026-11-20 - Stick 64: the first pipeline sync waits on an uncleared fence, and K1 loses the EOP count

Stick 64 is `0c08a6c012` with mesa_radv 25.3.6-11, `boot-next m13f`, five boots. Boots 2, 3 and 5 (RADV) each stopped at about 10.8 s with the PFP in `WAIT_REG_MEM`. Boot 4 (`gpu false`) warmed up in 1778 ms. Boot 1 reported `gpu not available: selftest`.

The user fence at `kR5UserFenceOffset` (2048) read `0xffffffffffffffff` on every boot, including the K2 baseline line `seen 18446744073709551615`, before any user job. The ring program cleared the read pointer at `+ 256` and the poll slot at `+ 512` and did not clear this slot. The first VM flush's pipeline sync is `WAIT_REG_MEM` control `0x113`, memory, equal, reference `sUserSequence`. That sequence starts at 0. The low 32 bits of the slot are `0xffffffff`, so the compare does not match and the PFP stays there. The logged wait at ring index 785, control `0x143`, reference `0x007c0040`, is the invalidate of a later job. The walker kept the last `WAIT_REG_MEM` in the ring, and the read pointer was still at `0x50`. `gpu_draw_settle` slept inside `skia_warm_up`. The compositor had already submitted. The line `gpu draw warm up settled 8000 ms` is 8 s after `left radv`.

Boot 1's K1 interrupt did arrive. The fail line is `client 20 src 181 ring 0`, which is the CP end-of-pipe, and `k1_wait_us` is 175, not the 200 ms timeout. The handler released the semaphore and then incremented `sEopCount`. The waiter woke, read 0, and `r5_gpu_check_reason` reported `selftest`. Boots 2, 3 and 5 won the other way and printed `k1 ok`. This is not the userspace self-test file, and `boot-next` is unchanged.

What changed: the ring program stores 0 in the user fence and in the 32-bit irq fence at 2064 before the same `flush_visible` of that page. The cond-exec dword at 2080 is not written. `sEopCount` is incremented before `release_sem_etc`. The stall dump keeps the `WAIT_REG_MEM` at or before the read pointer, and prints the eight dwords before it. The ring program, the PSP load, the page tables, the invalidate word, the reset and the stop are unchanged. `scripts/check-pre-stick.py` from `2a36e5d9ff` knows the m13f order. `8afb3f1712` is not taken.

Tested: `scripts/build-locked.sh` from `R5OS-gpu-stall`, shared WSL tree not reset. The shell exited 0. `gfx9_k12.o` and the accelerant `gfx9_start.o` were rebuilt 20 Nov 2026, 10:54 Europe/Amsterdam. The captured jam log was cut before the `jam_exit` line; the script exits with that status, and a mesa install failure exits 1. QEMU was not started: it has no `radeon_hd`. The 5625U has not run this.

## 2026-11-20 - The GFX room check reads CP_RB0_RPTR only

The read pointer for free space is `CP_RB0_RPTR`. The writeback is not part of that choice. `0xffffffff` is not a read pointer. `r5_ring_has_room()` returns false, so the submit does not run and does not mask that value down to a ring offset. The dump prints the register as `0xffffffff` and does not use it as an index into the ring. The ring program is unchanged.

Tested: `scripts/build-locked.sh` from `R5OS-gpu-stall`, shared WSL tree not reset. `jam_exit=0`. The translator headers that were already missing (`jpeglib`, `avif`, `png`, `tiff`, `webp`, `jasper`) printed the same fatal errors as the previous jam; jam still exited 0. QEMU was not started again: it has no `radeon_hd`, so it cannot read `CP_RB0_RPTR`. The 5625U has not run this.

## 2026-11-20 - Debug reads of the GFX ring must not drain write-combining

Problem: the dump added on 19 Nov flushed the read-pointer line, the user-fence line and the whole ring with `clflush` and `mfence`. The comment on the room check said a flush would write a dirty line back over the GPU's copy, and the dump did that flush anyway.

What the mapping is: the ring and the fence page come from `kvram()` (`gfx9_k12.cpp`). The pointer is `kernel_window`, the visible pool. `map_pool()` maps that window with `vm_set_area_memory_type(..., B_WRITE_COMBINING_MEMORY)`. It is not write-back and it is not uncached. The carve-out used by `r5_gfx9_vram_cpu()` is the same type. CPU stores into that page are the ring packets, a zero at the read-pointer slot when the ring is programmed (`fenceAddress + 256`), the poll slot at `+ 512`, and the cond-exec dword at `kR5CondExecOffset` (2080). That dword shares its 64-byte line with the user fence at `kR5UserFenceOffset` (2048), which the PFP polls. `radeon_gfx9_flush_visible()` only reads. A `clflush` or an `mfence` drains a write-combining buffer. A drain writes only the bytes the CPU stored.

What changed: `r5_gfx9_debug_cache()` and `r5_gfx9_debug_ring()` only load. No `clflush`, no `mfence`. The room line is `mmio`, `wb`, `wptr`, `free_mmio`, `free_wb`, `cp` again. `wb` is the volatile load. `gpu_draw_settle` is read with `load_driver_settings("radeon_hd")` and `get_driver_boolean_parameter`. Same file, same key. The ring program, the PSP load, the page tables, the reset and the stop at shutdown are unchanged.

Tested: `scripts/build-locked.sh` from `R5OS-gpu-stall`, shared WSL tree not reset. `jam_exit=0`, `mesa_exit=0`. `gfx9_start.o` and `SkiaOffscreen.o` rebuilt 20 Nov 2026, 09:34 Europe/Amsterdam. `r5-os.image` written 09:36.

QEMU, KVM, private copy `/home/nickr/r5-os-gpu-stall.image` (the disk that already had a signed-in session), GTK window, no other QEMU, `scripts/run-qemu-r5.sh` not used. The desktop came up: Deskbar, Tracker, home and Trash. 20 Nov 2026, about 09:37–09:38 Europe/Amsterdam. Lavapipe. No `radeon_hd`, so the room check and the dump did not run. The 5625U has not run this.

## 2026-11-19 - Skia warm-up stall on the 5625U: stale read pointer, and the wait dump

Problem: sticks 61 and 63 on the 5625U (Try R5, boot 1, `gpu_draw` / `gpu_debug` / `gpu_selftest` / `gpu_m13_boot 1`) stopped during the first Skia warm-up. The last accepted submits were the 24-dword buffer and the 1712-dword buffer. `CS_SUBMIT` then returned `B_BUSY` (`0x8000000e`, ring room). Stick 61 at 10.825 s, stick 63 at 10.833 s. app_server left RADV. Stick 60 (e0f8560010, no radeon_hd settings) finished in 2570 ms. Stick 62 (92e9856b40, the same settings as 61, including the room log from #4471) finished in 1815 ms and kept submitting that 1712-dword buffer through `ps2_hid`, `wacom` and `h2generic`.

What the logs already showed, checked against the code:

- The command processor itself had stopped. On stick 63 the room line was `mmio 00000050 wb 3b6 wptr 1792 free_mmio 335 free_wb 181 cp 94008200`. `0x3b6` is the read pointer the kernel stage left (stick 61: `k2 baseline rptr/wptr 0x3b6`). The register had moved. Fourteen seconds later stick 61 still read `CP_RB0_RPTR` `0x50`, `CP_STAT` `0x94808200` (PFP busy, ME idle). The PFP header FIFO ended in `WAIT_REG_MEM`, the ME FIFO in `DMA_DATA`. Accepting one more 256-dword job would not have moved that register.
- The room check trusted the writeback whenever it was non-zero. A writeback stuck at `0x3b6` reports 181 dwords free. A 256-dword job needs more than that, so the submit was refused while the register still had 335 free. That is a real false `B_BUSY`. It is not what froze the command processor.
- No single change from e0f8560010 to 1a74e375d6 is the cause. Stick 62 had #4468 through #4471 and did not hang. `libvulkan_radeon.so` in mesa_radv -11 is the same file as -10.
- The 2 MB split is not the common cause. Stick 63 has none. Stick 61's split is 4 ms after the refusal. Sticks 60 and 62 split after a warm-up that finished.
- Loading `h2generic`, `ps2_hid`, `i2c_hid` and `wacom` in that same second is not sufficient. Stick 62 submitted straight through it.
- `assuming memory type 20000000` is the VRAM BAR being mapped again. It is on the good boots too.
- The self-test dump is at 24.8 s, after the refusal. It does not build the warm-up's ring program.
- The user fence, the irq fence and the cond-exec dword share one 64-byte line. `flush_visible` reads that line. It does not store it. The only CPU store of the user fence is after a reset. That line was not changed.
- The ring program, the PSP load, the page-table base, the PTE flags, the invalidate word, the reset and the stop at shutdown were not changed.

What changed:

- The room check uses whichever read pointer has consumed more, the register or the writeback. A writeback left at the kernel-stage value can no longer hide a register that has moved. The hot path does not `clflush` that line: a dirty line would be written back over the GPU's copy. With `gpu_debug`, the room line now also prints `wb_mem`, the writeback after one `clflush`. `wb` is still the value the CPU saw before that flush. `free_wb` is the free space from `wb`.
- The first time a boot gives up on ring room, and `gpu_debug` is on, the driver prints the wait once. The line has the register, both fence views (`seen` through the carve-out, `pool` through the fence page after `clflush`), the user-fence MC address, `CP_STAT`, and `VM_L2_PROTECTION_FAULT_STATUS` with its address. Then the ME and PFP header FIFOs, eight ring dwords at the register, and `CP_IB1` / `CP_IB2`. It walks the ring and, when the indirect-buffer register has a size, up to 2048 dwords of that buffer, for `WAIT_REG_MEM` (opcode `0x3c`). It prints the control word (function, memory-or-register, PFP engine), and for a memory poll the address, the reference, and one dword read through the VMID 1 mapping. A VRAM address read that way can still be stale. Nothing is written. The interrupt ring pointers and the EOP count are one more line.
- A submitted indirect buffer in VRAM is flushed when it has a kernel mapping. One chained indirect buffer found in the submitted buffer is flushed too. A second chain is not. The PTE flags are unchanged. The 1712-dword buffer on these sticks was GTT and was already flushed, so this is not shown to be the stall.
- `gpu_draw_settle true` makes the Skia warm-up wait 8 s before it draws. The default does not. `boot-next m13f` boots 2, 3 and 4 are unchanged. Boot 5 writes `gpu_debug true`, `gpu_draw_settle true` and `gpu_m13f_boot 5`. `m13-measure` files that boot under `~/m13f/boot5`.

What the next stick must show, on the 5625U, Try R5, one stick, `boot-next m13f`:

- Boot 1, defaults. Either the warm-up finishes and no `CS_SUBMIT` is refused for ring room, or the first refusal prints `radeon_hd: wait` with a control word and a memory value. `0x113` is the kernel pipeline sync (PFP, memory, equal). `0x143` is the invalidate acknowledgement (registers). `0x13` is RADV's flush fence (memory, equal, ME engine). The value against the reference says whether the fence write was visible.
- Boots 2, 3 and 4 stay the scanout, write-combined GTT, and `gpu false` boots.
- Boot 5 waits 8 s (`gpu draw warm up settled 8000 ms`) and then draws. It says whether separating the warm-up from USB probe stops the refusal. One boot of each does not prove a race.

The 5625U has not booted this. QEMU has no `radeon_hd`, so it cannot show the stall or the dump.

Tested: `scripts/build-locked.sh` from the worktree `R5OS-gpu-stall` (WSL, the shared Haiku tree was not reset). The process exited 0. `gfx9_k12.o`, `r5_gpu_core.o` and `SkiaOffscreen.o` were rebuilt, and `r5-os.image` was written, 19 Nov 2026, 22:32–22:34 Europe/Amsterdam.

QEMU, KVM, a private copy at `/home/nickr/r5-os-gpu-stall.image`, GTK window, no other QEMU was running, and `scripts/run-qemu-r5.sh` was not used. The first window was Welcome to R5 Operating System. Try R5 then Sign in reached the desktop: Deskbar, Tracker, and the home and Trash icons. 19 Nov 2026, 22:36–22:42 Europe/Amsterdam. Lavapipe only. No stick.

## 2026-11-19 - gpu_debug no longer fills the syslog

Problem: stick 62 on the 5625U (`gpu_debug true`, `gpu_selftest true`) wrote 52,657 `radeon_hd` lines between kernel time 8.779 s and 236.050 s (227 s, about 13,900 lines a minute). 49,552 are `submit ib va`, 1,145 are `va map`, 1,113 are `bo domain` and 430 are `gfx9 cu cc`. The syslog rotated at 4 MB, so a longer session loses the boot start and the self-test. The `dprintf` is also on the submit path.

What changed: with `gpu_debug` or `gpu_selftest`, the first 20 of each of those four kinds are still printed in full. After that they are counted, and one line is printed every 10 seconds (the same interval as app_server's `gpu_debug` report): `radeon_hd: 10 s submits … dw max … va maps … bo creates … (gtt … vram …, wc …) cu …`. `gpu_trace` (default off), read next to `gpu_debug` in the kernel settings and in libdrm, prints every one of those lines and the translator's successful ioctl, and does not summarize. `gpu_debug` no longer does. Refusals, `gpu lost` and `syncobj limit` stay, still capped at 32 a boot. The ring-room line stays at most once a second. The self-test stage lines are unchanged. The ring, the PSP load, the reset, the stop at shutdown and the page tables are unchanged.

On a session shaped like stick 62 that is 20 full lines of each kind (80) plus one summary every 10 s (about 23 over those 227 s) plus the 417 other `radeon_hd` lines that log already had. That is about 140 `radeon_hd` lines a minute, and 6 a minute from these four kinds once the first 20 are done. The 5625U itself is unproven until a stick.

The strings the stick checks look for are still in the source. `distro/graphics/boot/check` matches `radeon_hd: bo domain .* snooped 1` and `radeon_hd: bo domain 2 .*snooped 0`. `scripts/check-pre-stick.py` matches `bytes %llu snooped %d` in `r5_gpu_core.cpp`. Those lines are the first 20 buffer creates. `m13-measure` and `r5-gpu-selftest` do not look for `submit ib va`, `va map`, `bo domain` or `gfx9 cu cc`.

Tested: `scripts/build-locked.sh` on this machine (WSL). The first jam failed to compile `r5_gpu_core.cpp` (`atomic_add` takes `int32*`, not `vint32*`). The counters are `int32`. The rebuild exited 0 (`...updated 8633 target(s)...`, `jam_exit=0`, `mesa_exit=0`, 19 Nov 2026, 21:16 Europe/Amsterdam).

QEMU, KVM, a private copy of `r5-os.image` at `/home/nickr/r5-os-gpu-debug.image`, no other QEMU was running. `-display none` and Enter over QMP. The guest settings file was `gpu_debug true`. Lavapipe (`gpu compositor on lavapipe: no render node`, `gpu draw on llvmpipe (LLVM 21.1.8, 256 bits)`). Tracker and Deskbar started. The syslog has `compose median 1041 us cpu 44 ms per 10 s`, a second `compose median … per 10 s`, and `draw median … per 10 s`. There is no radeon_hd device in this guest, so the kernel summary is not in this log. 19 Nov 2026, 21:19–21:27 Europe/Amsterdam. No stick.

## 2026-11-19 - CS35L41 firmware for the ENVY speakers, stored only

Problem: the HP ENVY x360 15-ey0xxx (Ryzen 5 5625U, ACPI `CSC3551`, subsystem `103C8A31`) plays through two Cirrus CS35L41 amplifiers. The HDA controller is already driven. The speakers stay silent because nothing powers those amplifiers or loads their firmware.

What changed: the playback firmware and the eight tuning files Windows uses for this model are in `distro/firmware/cirrus`, unmodified, with `LICENSE.cirrus`. They are the linux-firmware files (playback 6.61.1, release 29.63.1, tunings `103c8a29`). On this laptop their sha256 matches the Windows `csaudioext` driver. The diagnostic wmfw and `103C8A29_211103_diag.bin` are not in that publication and are not included.

Limits: not installed, and no driver loads them. The speakers still do not play. The register programming is not here. The Linux driver is GPL-2.0 and was not used as a source. Not built and not booted.

## 2026-11-19 - GPU hang on stick 61, and the desktop after the device is lost

Problem: Stick 61 on the 5625U (main 0640813f88, Try R5, boot 1) hung the GFX ring during the Skia warm-up. The last accepted submit was at 10.705 s (`ib va ffff800100001000`, 1712 dwords). At 10.825 s `CS_SUBMIT` returned `B_BUSY` (`0x8000000e`, ring room, 100 ms). app_server logged `gpu draw off: submit` and `gpu draw warm up 198 ms`, status General system error. At 24.8 s the self-test read the command processor and marked the GPU lost. The screen stayed a striped moiré pattern. vkcube asserted on the error and the debugger opened. Stick 60 (e0f8560010, no radeon_hd settings) finished the same warm-up in 2570 ms with no refused submit.

What the dump shows (Linux 6.12 packet names from Mesa `sid.h`, `CP_STAT` bits as this driver already names them):

- The ring is 1024 dwords. The kernel write pointer was `0x700` (1792). The writeback read pointer was still `0x3b6`, the value from the kernel fence. The MMIO read pointer was `0x50`. That is 154 dwords past `0x3b6` after one wrap of the 1024-dword ring, and it was still there 14 s later. The room check trusts the writeback, so it saw 181 free dwords and refused the next job. The command processor had not moved on.
- The eight dwords at the MMIO read pointer are `0xffff1000`, the one-dword `PACKET3` NOP this ring uses as padding. The IB1 base was `0xf3f0000000` with size 0, so the prefetch was not inside an indirect buffer. `CP_ROQ_IB1_STAT` was `0x00a100a1`.
- `CP_STAT` was `0x94808200`. Bit 15 (PFP) was set. Bit 17 (ME) and bit 14 (the VM walker) were clear. `GRBM_STATUS` was `0xa0003028` (bit 31 is the `GUI_ACTIVE` bit this driver already tests). `CP_STALLED_STAT1/3/2` were `0x00000c00`, `0x00000800` and then `0x00100000` on the second read. The ME instruction pointer was `0x0a` and the PFP instruction pointer was `0xf0b` (firmware program counters, not ring offsets).
- The header FIFO, oldest first, ends in `DMA_DATA` (`0xc0055000`, opcode `0x50`) on the ME and `WAIT_REG_MEM` (`0xc0053c00`, opcode `0x3c`) on the PFP. The PFP was inside that wait. The ME was not executing. Earlier headers in the same FIFO are `RELEASE_MEM` (`0x49`), `WRITE_DATA` (`0x37`), `INDIRECT_BUFFER` (`0x3f`), and a count-0 type-3 packet `0xc0001700` (opcode `0x17`), which `sid.h` does not name.

What did not cause it: #4468 and #4469 do not change the GPU. `libvulkan_radeon.so` in mesa_radv 25.3.6-11 is the -10 library. `gpu_selftest` and `gpu_m13_boot` run the self-test at 24.8 s, after the refuse. `gpu_debug` prints a line per map and per submit, reads `CC_RB_SHADER_ARRAY_CONFIG` when a client asks for the active CUs, and on the compose path adds timestamp queries. None of those ran a test submit, and none of them change the ring program, the PSP load, the reset, or the page tables. The CU reads in this boot are before the first submit. The hang is the PFP stuck in `WAIT_REG_MEM` inside a job the ring had already accepted. Stick 60 ran that same job stream without the debug settings and did not stick. One boot cannot show that the settings caused it. It is the hang stick 60 did not hit. The GFX9 contracts are unchanged.

What changed:

- When the compositor gives up on the GPU, Skia drops its recorder and surface pointers and does not destroy them. `vkDeviceWaitIdle` and `vkDestroyDevice` are not called. A window that closes later no longer enters Graphite's destructor. The draw thread's fence wait notices that and returns, so the switch is not stuck behind the ring. The back buffer is copied to the front on the CPU before the compose thread finishes. The log line is `left radv`. Painter keeps drawing. vkcube is unchanged.
- With `gpu_debug`, a refused `CS_SUBMIT` that waited out the ring room logs one line a second: `room mmio … wb … wptr … free_mmio … free_wb … cp …`. `free_wb` is what the room check used. `free_mmio` is the same sum from the register. A stall and a stale writeback do not look the same on the next stick.

The fence wait keeps its first second under the Skia lock. The lock is dropped only after that wait returns a timeout, so a running GPU does not change who can enter Skia. `fAbandoned` is an `int32` updated with `atomic_set` and read with `atomic_get`.

Tested: `scripts/build-locked.sh` on this machine (WSL). `jam` exited 0 (`...updated 8638 target(s)...`, 19 Nov 2026, 19:18 Europe/Amsterdam) and `install-mesa-packages.sh` exited 0. That check looks for `mesa_radv-25.3.6-1-` and requires `mesa_radv-25.3.6-11`. An earlier note that it rejected `-11` came from a stale checkout of the script. Stick 61's build reported `mesa_exit=0`.

QEMU, KVM, a private copy of `r5-os.image`, no other QEMU was running. A GTK window wedged WSL, so this boot used `-display none` and sent Enter over QMP. Lavapipe (`gpu compositor on lavapipe: no render node`, `gpu draw on llvmpipe`). The desktop came up (Tracker and Deskbar). The session then opened a Tracker folder, Terminal and About, moved About (`hey` set the frame, error 0) and closed it (`hey` quit returned true). The syslog has `gpu draw warm up 42246 ms` and has no `left radv` and no `device lost`. 19 Nov 2026, 19:37–19:45 Europe/Amsterdam. No stick. Why the PFP stopped in `WAIT_REG_MEM` is still open; the `gpu_debug` room line stays so the next stick can show what that wait was polling.

Limits: the warm-up hang is not fixed. A and B stay unproven until a stick. The lost device is leaked for the rest of the boot; the ring is not reset here.

## 2026-11-19 - mesa_radv 25.3.6-11 repacked with the real package tool

Problem: the -11 package from #4468 was made without Haiku's `package` tool. Four scripts were right (`boot-next`, `check`, `check-user`, `m13-measure`), but nine had the right size and the wrong content: `boot2.sh`, `check.sh`, `m8-run`, `m8-boot2`, `m8-show`, `m11-vulkan`, `m11-scroll`, `m12-measure` and `m12-scenario`. The m13f stick did not use them, but `check-pre-stick.py` compares every script and would fail.

What changed: `distro/mesa/pack-radv.py --tools-only` was run in WSL with the tree's `package` tool. The library is byte-identical to -10. The version stays 25.3.6-11.

Tested: all 13 scripts were extracted from the new package and match `distro/graphics/boot`. In a WSL sandbox, `boot-next m13f` gives boots 1 to 4 and then clears the settings. Not on a stick.

## 2026-11-19 - mesa_radv 25.3.6-11 repacked with the real package tool

## 2026-11-20 - EFI runtime services: review round 2 (#4483), fault recovery only for the firmware

**Problem (review by Jarvis).** `r5_firmware_fault()` redirected any kernel-mode fault at a lower-half instruction pointer with interrupts off to *any* `thread->fault_handler`. `user_memcpy()` also sets that handler. A kernel bug inside such a section would then have been "recovered" into the wrong handler instead of panicking.

**What changed.**
- **The hook.** The kernel now has its own hook, `gR5FirmwareFaultHandler` and `gR5FirmwareCallCPU` in `arch_int.cpp`. `efi_arch_call()` sets it right before `efi_call5()`, with interrupts off, and clears it right after.
- **When it redirects.** `r5_firmware_fault()` redirects only on that CPU, while the hook is set, and only for kernel mode, a lower-half instruction pointer and interrupts off.
- **The module.** It no longer touches `thread->fault_handler`. Every other fault keeps the normal panic path.
- **Fault test.** With `fault_test true` in `~/config/settings/kernel/drivers/efi`, the module jumps once into an unmapped page of the firmware address space and logs whether that call came back.
- **Docs.** `docs/EFI-RUNTIME.md` lists which faults are recovered.

**Tested on 20 Nov 2026.** `scripts/build-locked.sh @r5-anyboot`: jam exit 0. QEMU with KVM, `-cpu host`, SMAP/SMEP on:
- **OVMF:** as in round 1. `BootOrder` read in 3 us, a call with interrupts off refused, 28 variables listed, no panic, app_server started.
- **OVMF with `fault_test true`:**
  - `efi: fault test: jump to 0x7ff000000000 recovered`. That is an instruction-fetch page fault under SMEP, which used to panic.
  - After it the module stayed usable: `BootOrder` read and 28 variables listed. No panic, app_server started.
- **SeaBIOS:** module absent, boot continues.

**Not tested.**
- A #GP or #UD inside the firmware: only the page-fault path was provoked. The exception path runs the same check.
- AVX-512: the host CPU has none.
- Firmware that really uses AVX.
- Real hardware.
- arm64.

## 2026-11-20 - EFI runtime services: review round 1 (#4483)

Review by Jarvis:
- **FPU.** `fxsave64` saved only x87/SSE. Firmware that uses AVX could clobber the YMM upper halves of the thread. The module now saves with `xsave64`/`xrstor64` and the kernel's own `gXsaveMask`, the XCR0 that the interrupt and context-switch paths in `interrupts.S` save. The buffer has the size CPUID leaf 0xD reports, is 64-byte aligned, and gets its header zeroed. `fxsave64` stays for CPUs without XSAVE. The kernel's code-patched XSAVE sequence is inline and cannot be called.
- **Faults.** Only a page fault reached the recovery; a #GP, #UD or an NX fetch under SMEP panicked. `r5_firmware_fault()` in `arch_int.cpp` now sends any kernel-mode fault at a lower-half instruction pointer, with interrupts off and a thread fault handler set, to that handler. Only the firmware runs like that. It is checked first in `x86_page_fault_exception` (before the SMEP/SMAP panics) and in `x86_unexpected_exception`. The `stac` workaround is gone.
- **Callers.** The module refuses a call with interrupts already off (`B_NOT_ALLOWED`, one log line). `EFI.h` and `docs/EFI-RUNTIME.md` say: thread context only, may block, no spinlock. The longest firmware call above 1 ms is logged.
- **PCID.** R5 does not set CR4.PCIDE and has no KPTI. If CR4.PCIDE is ever set, the module refuses to load. Documented.
- **Quirks.** `docs/EFI-RUNTIME.md` now has a quirk table, and says that a failed `SetVirtualAddressMap` or a non-identity range leaves the module absent with one log line. No vendor-specific workaround.

`EFI.h` now has the caller rules in its comment, so #4482 should take the same file.

Mistake: the first build of this round called `get_current_cpuid()`, which is not exported to modules. The module now runs `cpuid` itself.

Tested on 20 Nov 2026: `scripts/build-locked.sh @r5-anyboot` gave jam exit 0. Then QEMU with KVM, `-cpu host` (Ryzen: AVX2, XSAVEC, SMAP, SMEP; the kernel logs `enable XSAVEC 0x7 832`):
- **OVMF:**
  - `BootOrder` read, 16 bytes, in 4 us;
  - the size query, the missing variable and `SmartAmpCalibrationData` as before;
  - a call with interrupts off gave `B_NOT_ALLOWED`;
  - 28 variables listed;
  - no panic, app_server started.
- **SeaBIOS:** module absent, boot continues.

Not tested:
- the fault recovery (no firmware fault was provoked);
- AVX-512 (the host CPU has none);
- firmware that really uses AVX;
- real hardware;
- arm64.

## 2026-11-20 - EFI runtime services for kernel drivers (generic/efi/v1)

Problem: the kernel dropped the UEFI runtime services after boot, so a driver could not read a firmware variable such as the CS35L41 factory calibration (#4482). The owner chose real runtime services for every UEFI computer, not a list of variables read by the boot loader.

What changed:
- **The module.** New MIT kernel module `generic/efi/v1`, built the Haiku way like ACPI, with `get_variable` and `get_next_variable_name`. `EFI.h` is byte-identical to the header in #4482.
- **Hand-over.** The EFI loader passes the runtime services table, the runtime ranges of the final memory map, the memory attributes table and the runtime properties to the kernel. It does this in one kernel_args block (`<boot/r5_efi_runtime.h>`), from `platform_load_ucode()` beside SMBIOS. The kernel publishes the block as a boot item, the same route as the ACPI root.
- **Loader fix (x86_64).** Before `SetVirtualAddressMap` only runtime code and data got `VirtualStart = PhysicalStart`. Runtime MMIO, such as the variable flash, kept the firmware's value. Every `EFI_MEMORY_RUNTIME` range is now identity mapped.
- **How the firmware runs.** It gets its own PML4: only the runtime ranges in the lower half, and the kernel half copied before each call. One call runs at a time, with interrupts off, on a 64 KiB stack of its own. The x87/SSE state is saved, and NX and RO come from the attributes table.
- **Fault handling.** A page fault in the firmware switches the module off for that start instead of crashing.
- **Test driver.** `efi_test`, in no package.

Mistake: the first hand-over ran in `efi_main`, before the loader heap existed ("no memory for the hand-over block"). It moved to `platform_load_ucode()`.

Limits:
- Read-only: no `SetVariable`, no userland interface.
- An instruction-fetch fault under SMEP, or a #GP/#UD in the firmware, still panics (needs `arch_int.cpp`).
- arm64 and riscv64 build `arch_none.cpp` and do not load the module. The arm64 loader also does not set `VirtualStart` (`docs/EFI-RUNTIME.md`). arm64 was not built.

Tested on 20 Nov 2026: `scripts/build-locked.sh @r5-anyboot`, jam exit 0. Then own copies of the anyboot image in QEMU (KVM, `-cpu host`, SMAP/SMEP on):
- **OVMF** (`OVMF_CODE_4M.fd`, pflash variables):
  - `efi: runtime services ready, revision 2.70, 5 ranges, attributes table yes`;
  - `BootOrder` read, 16 bytes, attributes 0x7;
  - the size query gave `B_BUFFER_OVERFLOW` with 16;
  - `SmartAmpCalibrationData` and a missing variable gave `B_ENTRY_NOT_FOUND`;
  - 28 variables listed;
  - no panic, app_server started.
- **SeaBIOS**, legacy boot of the same image: `get_module` gives `B_ENTRY_NOT_FOUND` and the boot continues (input drivers, package_daemon).

Not tested: real hardware (the 5625U stick), the fault recovery, arm64.

Problem: the -11 package from #4468 was made without Haiku's `package` tool. Four scripts were right (`boot-next`, `check`, `check-user`, `m13-measure`), but nine had the right size and the wrong content: `boot2.sh`, `check.sh`, `m8-run`, `m8-boot2`, `m8-show`, `m11-vulkan`, `m11-scroll`, `m12-measure` and `m12-scenario`. The m13f stick did not use them, but `check-pre-stick.py` compares every script and would fail.

What changed: `distro/mesa/pack-radv.py --tools-only` was run in WSL with the tree's `package` tool. The library is byte-identical to -10. The version stays 25.3.6-11.

Tested: all 13 scripts were extracted from the new package and match `distro/graphics/boot`. In a WSL sandbox, `boot-next m13f` gives boots 1 to 4 and then clears the settings. Not on a stick.

## 2026-11-19 - WPA3 join, analog audio default, AVDTP logs, mesa_radv-25.3.6-11

Problem: On the 5625U (stick 60, main e0f8560010):
1. A user join of "Skynet Guest" (SAE, key_mode 0x400) was dropped. wpa_supplicant logged "No suitable network found" on the first scans, then started SAE, and while SAE was held net_server joined the remembered WPA2 network "Skynet" (auth 3, key_mode 0x42). That join is a local DEAUTH_LEAVING. The mt7922 line "the firmware was not ready for AUTH/ASSOC: -2147483643" is B_TIMED_OUT on the join command that the deauth aborted.
2. The owner chose an output node and media_server then logged that "Bluetooth audio" was not present and connected the mixer to the other "HD Audio" (the HDMI controller, node 2). Both controllers were named "HD Audio". An earlier choice of the analog node stayed connected and still made no sound. Linux names 103c:8a31 as ALC287_FIXUP_CS35L41_I2C_2_HP_MUTE_LEDS: the speakers are two CS35L41 amplifiers on I2C.
3. The WH-1000XM3 opened HFP, then AVDTP logged discover and "the headset did not answer", and a later open of PSM 0x19 was "Connection refused". The syslog had no L2CAP or AVDTP signal lines, so the stop was not visible.
4. mesa_radv-25.3.6-10 still carried a boot-next from before m13 and m13f.

What changed:
- Part A: An explicit join records the device and the network name (not the password) and net_server skips auto-join of that device for 60 seconds, or until the join fails or the user leaves. IEEE80211_IOC_HAIKU_PREFER_SSID (0x6009) asks the scan copy to put that SSID first in wpa_supplicant's 24 KB buffer, without the ENETRESET that IEEE80211_IOC_SSID does. "RSN: clearing own RSNXE" on the SAE attempt is the SME clearing its copy before set_suites builds it again; the Association Request still takes RSNXE from the WPA IE. The firmware timeout is named in the log and the ROC sequence is unchanged.
- Part B: A chosen output is kept. SaveState writes it in place of a stored Bluetooth preference, and a fallback that was already searching cannot commit over it or mark it as a stand-in. A controller with no headphone, speaker, or line-out pin is named "HDMI"; the analog codec stays "HD Audio", and a fallback or a first choice prefers "HD Audio". The headphone pin and its EAPD stay enabled when jack sense says empty; speaker pins mute only while a jack is present. No CS35L41 driver is added. The codec logs the subsystem, and 103c:8a31 logs that the speakers need the two amplifiers.
- Part C: PSM 0x19 logs the connection result and the config response. Each configuration option is logged, including retransmission mode 3 (ERTM). AVDTP logs every signal sent and received (signal id, transaction label, message type) and a signal that is dropped. Classic ERTM is still not implemented. BlueZ opens AVDTP on PSM 0x19 in ERTM; this stack accepts basic mode only. The configuration response is still matched on the local channel id, as in #4465.
- Part D: mesa_radv-25.3.6-11 is 25.3.6-10 with the same libvulkan_radeon.so and the current stick scripts, so boot-next knows m13 and m13f. pin-mesa-packages.sh, check-pre-stick.py, install-mesa-packages.sh and distro/gpu/COMPAT name -11. The GFX9 contracts are unchanged. Mesa itself is unchanged, so the libdrm symbol check does not apply.

Tested:
- The mesa_radv-25.3.6-11 heap was read back: libvulkan_radeon.so matches -10, boot-next and m13-measure match distro/graphics/boot, and the package version is 25.3.6-11.
- `scripts/build-locked.sh` exited 1 before jam: `build-lock.sh` cannot create `/home/nickr/r5os/.r5-build.lock` on this machine, and there is no generated Haiku tree or host `package` tool. `mesa_radv-25.3.6-11` was packed by rewriting the `-10` heap. No QEMU. No USB stick. `scripts/check-pre-stick.py` was not run: it reads the anyboot image through `wsl` and `bfs_shell`. A local `boot-next m13f` wrote `gpu_m13f_boot 1` and exited 0.

Limits:
- Parts A, B and C are not proven until the next stick. WPA2 with a large download is unchanged in the join and ROC path and was not retested here. The speakers stay silent until a CS35L41 driver exists; the headphone jack is the path that does not need it. If the headset asked for ERTM, the next syslog shows the mode byte and the AVDTP signal that got no answer.

## 2026-11-19 - Boot 59 fixes: WPA3 RSNXE, Bluetooth audio presence, mixer routing, L2CAP response routing

Problem: On the 5625U (stick 59):
1. WPA3 ("Skynet Guest") completed SAE commit/confirm and associated, then immediately received DISASSOC after sending EAPOL-Key 2/4. `bsd_set_opt_ie` passes RSN IE + RSNXE (`0xf4 0x01 0x20`, 31 bytes) in `IEEE80211_APPIE_WPA`, but `setwparsnie()` dropped RSNXE so the Association Request omitted it while EAPOL-Key 2/4 included it. The brief `No buffer space available` lines right after `link up` (also present on stick 55) come from `mtk_encap()` before the 4-way handshake installs PTK.
2. Audio was silent: `media_server` logged `"Bluetooth audio" not present; using "HD Audio"` and immediately `"is present again; using it"` when no headset was connected because the `bluetooth_audio` node is always registered. On output switches `_ConnectMixerToOutput()` also did not update the mixer's time source before `MakeTimeSourceFor(mixer)`, and default-output changes / mixer connect-disconnect results were only logged under `TRACE`.
3. Bluetooth L2CAP regressed after #4461: `L2capEndpoint::_HandleConfigurationRsp()` compared `scid` against `fDestinationChannelID` instead of `fChannelID`, rejecting valid responses (`cid=65, remote=2881, scid=65`) and revealing that `l2cap_signal.cpp` routed responses solely by `ident` (delivering `scid=65` to `cid=66`).

What changed:
- Part 1 (Wi-Fi): `ieee80211_add_rsn()` in `distro/kernel/src/libs/compat/freebsd_wlan/net80211/ieee80211_output.c` now emits any `IEEE80211_ELEMID_RSNXE` (244) from `vap->iv_appie_wpa` in Association Requests. `mtk_tx.c` handles Connac2 `TX_FREE` v3 (`ver == 4`) and logs rate-limited TX slot/ring stall diagnostics (`txfull`, `txstale`, `head`, `tail`, `dma`, `txdone`). `WiFiController::_MaybeScan()` in `distro/wifi/lib/WiFiController.cpp` skips periodic active scans while associated (leaving on-demand `kMsgWiFiScan` and disconnected scans unchanged) so full-band HW scans do not stall active traffic/downloads.
- Part 2 (Audio): `A2DPSource` / `BluetoothAudioNode` notify `media_server` (`'BTAR'`, `ready`) when A2DP transport is open vs down; `DefaultManager` only counts `"Bluetooth audio"` as present while A2DP is up, falling back to `"HD Audio"` while keeping the saved choice and switching back when the headset connects. `_ConnectMixerToOutput()` now sets the time source on both the soundcard and the mixer, and logs default-output changes and mixer connect/disconnect results to syslog.
- Part 3 (Bluetooth L2CAP): Restored `scid != fChannelID` in `L2capEndpoint::_HandleConfigurationRsp()`, and updated `l2cap_signal.cpp` (`resolve_response_endpoint()`) so Connection, Configuration, and Disconnection responses whose `ident` lookup misses or hits a different endpoint are delivered to the endpoint on the same link whose `ChannelID() == scid` while preserving the other endpoint's `ident`.

Tested:
- `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`.
- Short QEMU KVM boot on an isolated copy of `r5-os.image` with `-device intel-hda -device hda-duplex`: HDA initialized cleanly, `bluetooth_audio` listened without stealing default output, and syslog logged `media_server: mixer (5) connected to output (2, input 0, ts 2): 96000 Hz 2 ch fmt 0x2 buf 24576, start=No error`.

Limits:
- Parts 1 (WPA2 large download / WPA3 SAE + 4-way handshake), 2a (live WH-1000XM3 A2DP connect/disconnect switching), 2b (audible sound playback after Sign in and on AMD HDA hardware), and 3 (WH-1000XM3 AVDTP open and audio) are only proven on the 5625U. The QEMU test ran headless before Sign in so no audible beep WAV was captured.

## 2026-11-19 - M13 follow-up, part 5: one stick, four boots

No stick was written here. The owner writes it later with `scripts/write-stick.py`.

On the laptop, after Sign in. One command, then restart. On each boot, once the desktop is up, run `m13-measure` (drag, scroll, resize). The syslog line `radeon_hd settings` is the settings file in use, and `m13-measure` copies that file to `~/m13f/bootN/measure/radeon_hd`. `read-stick-logs.py` copies `~/m13f`.

The system kernel settings need `syslog_time_stamps true` or the measure cannot match the syslog. That is the line `scripts/plant-m10-stick.sh` already writes into the image; this boot does not create a user kernel settings file, because that file would hide the system one.

- Boot 1: `boot-next m13f`, restart. Defaults. Skia on, 2 MB areas. Look for `R5 VM: area` and `gpu draw on`.
- Boot 2: `boot-next`, restart. Adds `gpu_compositor_scanout`. `gpu_app_server` is on with it, because scanout copies that back buffer. Write-combined GTT stays off.
- Boot 3: `boot-next`, restart. Scanout off. Adds `gfx9_wc_gtt`.
- Boot 4: `boot-next`, restart. `gpu false`. Lavapipe and Skia.
- After boot 4: `boot-next` clears the settings.

`gpu_debug true` is on every boot so the 10 second lines are in the syslog.

## 2026-11-19 - M13 follow-up, part 4: Skia draws by default

Problem: Skia still drew only when the radeon_hd settings said `gpu_draw true`. The owner's decision of 16 Nov 2026 was that Skia is the default for RADV and Lavapipe together, and Painter draws only in Safe mode.

What changed: `gpu_draw` defaults to true. `gpu_draw false` turns it off. Safe mode still does not create the compositor. What's new says so in ordinary language (build 10066). The plan, the drawing document and the GPU architecture document record step 5 as the default, which step 3 items are built and still opt-in, and that the old CPU read-back is history.

Limits: Not booted on the 5625U. That RADV stays on with the default, and that Safe mode still draws with Painter, are only proven there. QEMU can show Lavapipe with Skia and the compose compare.

## 2026-11-19 - M13 follow-up, part 3: Lavapipe's vector width follows the CPU

Problem: Lavapipe's vector width stayed at Mesa's own choice unless the radeon_hd setting named 128 or 256. On a CPU with AVX2 that leaves the wider vectors unused. On a plain x86-64-v2 CPU a forced 256 would be wrong.

What changed: When the CPU has AVX2 and `LP_NATIVE_VECTOR_WIDTH` is not set yet, app_server sets it to 256 before Lavapipe starts. A CPU without AVX2 sets nothing. `gpu_lavapipe_vector` still overwrites either choice, and a value already in the environment is left as it is.

Limits: Not measured. The line `lavapipe vector width 256 (avx2)` is the check. QEMU's virtual CPU may or may not advertise AVX2.

## 2026-11-19 - M13 follow-up, part 2: 2 MB pages for the GPU areas

Problem: `amd64_large_pages` is on by default, and on the 5625U no real area was logged as using a 2 MB page. The GFX9 pool starts at an offset that is not a multiple of 2 MB. The GART CPU area is 1 MB. A GTT buffer is rounded to 4 KB, so the full-lock path, which asks for a 2 MB run only when the size is a multiple of 2 MB, never sees it. There was no line that said, once per area, whether that area got 2 MB pages.

What changed: The pool start is rounded up and its size down to 2 MB when that still leaves 2 MB; otherwise it stays 4 KB aligned. The GART CPU area is 2 MB, with a fall back to 1 MB, and the GPU page table still maps the first 1 MB with the same PTE flags. A GTT buffer of 2 MB or more gets a CPU area rounded up to a multiple of 2 MB; the size told to the client stays the 4 KB rounding, and a failed larger area is tried again at that size. Each area of 2 MB or more logs `R5 VM: area "<name>" 2MB pages` or `4KB pages` once. If a 2 MB-aligned virtual hole cannot be found, the map is tried again at 4 KB. `amd64_large_pages false` still forces 4 KB pages.

Limits: Not booted on the 5625U. QEMU has no radeon_hd, so the pool, the GART and the GTT lines are only proven there. The BAR and the pool on the 5625U are the areas that can show `2MB pages`.

## 2026-11-19 - M13 follow-up, part 1: the review fixes

Problem: The review of the Skia drawing path found six costs. The ioctl wait for a sync object was 5 seconds while the RADV compose fence allows 15, so a compose still inside its own limit failed the submit and turned GPU drawing off. Finding a cached bitmap hashed every pixel on every lookup. A buffer copy of a rectangle submitted one Vulkan copy per row even when the rows were one contiguous span. DrawString with an offset array laid the glyphs out twice. Each submit walked every buffer handle to flush the indirect buffer. Skia's AVX2 and FMA functions are selected at run time only when the library itself is built below that level.

What changed: The submit wait is 15 seconds, the same as the compose fence. It stays in the ioctl. Damage is still published after the fence, because a timeout still fails the submit. A bitmap app_server owns is recognised by a generation counter, bumped when its pixels are replaced or drawn. A client bitmap is recognised by a sample of 64 words, and a full hash every 32 lookups. A rectangle is one Vulkan copy when the pitches match and the rectangle is the whole row; a partial row stays one copy per row, because one linear copy would overwrite the gap. DrawString with offsets uses the pen from that one layout. The indirect buffer's kernel pointer is remembered and only its lines are flushed, with clflushopt and sfence when the CPU has them. The page-table flags and the snooped GTT mapping are unchanged. The Skia build pins the rest of the library to x86-64-v2 and checks that Init_ml3 is in libskia.a and that the ml3 objects contain an FMA instruction. app_server is not built above x86-64-v2.

Limits: Not booted on the 5625U. The shipped libskia.a was not rebuilt by this change; the check runs when distro/skia/build-skia.sh runs. Compose compare on Lavapipe is the QEMU check for the bitmap cache and the copies.

## 2026-11-19 - CPU Info is tall enough for both cards

Problem: CPU Info still opened a little short. The bottom card, Enabled features, was cut off. The wrapping lines (Extra features, Kernel) report a height of one line until they have a width, so the preferred window height was short of the wrapped text. There is no scrollbar.

What changed: After the layout has given those lines their width, their height is the wrapped text, and the window is sized from that. Both cards fit.

Limits: Not opened on the 5625U.

## 2026-11-19 - The headset stream configures when the channel numbers differ

Problem: On the 5625U, stick 57, the WH-1000XM3 stayed connected. Hands-free reached "service level connection up" and "battery 100 %". bluetooth_audio logged "opening AVDTP" and then nothing about discover, capabilities, configuration, open, or start. The kernel logged "l2cap: unexpected configuration rsp: invalid source channel (cid=67, scid=66)". A configuration response carries the destination CID of the request, which is the remote endpoint. The check compared it with the local CID. When those two numbers differ the answer was rejected and the AVDTP channel never opened. Also "hfp: 1 commands not known" did not say which command.

What changed: The configuration response is accepted when its source CID is the remote endpoint. bluetooth_audio logs each AVDTP step (discover, get capabilities, set configuration, open, start) and a notice when a command is not sent. An HFP command the agent does not know is logged with its text and still answered with ERROR, which is what Hands-Free Profile 1.8 specifies for a command it does not define.

Limits: Not booted on the 5625U. That the WH-1000XM3 plays audio is only proven there. The unknown command on stick 57 was not recorded, so this build is what names it.

## 2026-11-19 - A Wi-Fi join keeps its station while the lock is down

Problem: On the 5625U a WPA3 join panicked in mtk_sta_update, called from mtk_newstate on ic_taskq. The instruction at mtk_sta_update+0x73 loads ni_chan (offset 0x2b8 of the station node). The fault address 0xfffffffe37002b8 is that field of a node pointer 0xfffffffe3700000, which is not a live object. mtk_newstate takes vap->iv_bss, drops the stack lock, and then mtk_join_prepare calls mtk_sta_update. WPA3 replaces the node while SAE is still joining, so the pointer is dead by the time the field is read. WPA2 uses the same function and has been connecting.

What changed: The node is referenced before the lock is dropped and released after the stack's own state change. WPA2 and WPA3 both take that reference.

Limits: Not booted on the 5625U. The SAE hold, the firmware, and the ring are unchanged. WPA3 associating, and WPA2 still connecting, are only proven on the 5625U.

## 2026-11-19 - A shared font face is locked while its character map is read

Problem: On the 5625U, stick 57, app_server died with a segment violation in libfreetype tt_cmap12_char_map_binary, called from FT_Get_Char_Index, FontCacheEntry::CanCreateGlyph, GlyphLayoutEngine::_CreateGlyph, ServerFont::GetEscapements. The thread was WebPositive's app_server drawing thread. CanCreateGlyph calls FT_Get_Char_Index on the entry's face and takes no lock. _CreateGlyph calls it before the entry write lock, and GetHasGlyphs calls it with no lock either. A FreeType face is not safe on two threads. The font cache holds 128 entries (raised earlier); a larger cache does not stop two threads from walking the same character map. The debugger line about libvulkan and compilation unit version 5 is DWARF 5, which that debugger does not read.

What changed: CanCreateGlyph takes the entry write lock for the character-map lookup and releases it before returning. CreateGlyph still does its own lookup while it already holds that lock, so this does not nest. The cache stays at 128 entries.

Limits: Not booted on the 5625U. WebPositive has to draw text that misses the cache, on two threads, to show the crash is gone.

## 2026-11-19 - A caught user copy names its thread

Problem: On the 5625U, stick 56 logged eight kernel page faults at 0xb17091c000, thread 901, just after the GLTeapot open animation. The kernel image symbols put the instruction pointers in the user-copy routines: 0xffffffff8014c3f6 is the 8-byte load in _arch_cpu_user_memcpy (a copy from user memory, shorter than 64 bytes), 0xffffffff8014c422 is rep movsb in that function (a write to user memory of at least 64 bytes), 0xffffffff8014c4c9 is the lods in _arch_cpu_user_strlcpy (a string read from user memory), and 0xffffffff8014c46f is the 8-byte store in _arch_cpu_user_memset (a write of 8 to 63 bytes). The fault handler was set, so each call returned B_BAD_ADDRESS and the machine did not panic. The log line carried only the thread id. Stick 56 has no other mention of thread 901, so the program is not named there. Stick 57 does not repeat the fault.

What changed: That log line now includes the thread name and the team name and id.

Limits: The caller is not changed. The next stick has to show which program passed the unmapped pointer. Not booted on the 5625U.

Tested: symbols checked against kernel_x86_64 built 19 Nov 2026 14:01 Europe/Amsterdam. The addresses match the stick-56 instruction pointers. No boot.

## 2026-11-19 - A controller without a link-key store

Problem: On the 5625U, hci125 (the MT7961) logged "command 0x0c0d answered with status 0x1". 0x0c0d is Read Stored Link Key (controller and baseband, OCF 0x000D). Write Link Policy Settings is 0x080d and is not sent. The Bluetooth kit asks for the controller's key store each time a LocalDevice is opened. The MT7961 answers with Command Status Unknown Command. The server logged that as a notice. Link keys are already kept by the server.

What changed: That answer is not an error. The program that asked is told the read succeeded and there is nothing to add, and the log says the controller does not store link keys.

Limits: Not booted on the 5625U. The kit still sends the command once per LocalDevice. The kernel page fault after the GLTeapot animation is not in this commit.

Tested: not on the 5625U.

## 2026-11-19 - The Wi-Fi dialog and CPU Info sit on the glass

Problem: On the 5625U the Wi-Fi connect dialog drew the signal icon inside a dark square, the status line ("Connecting to … checking the password") on a grey bar, and the password field only partly white. The icon was a bitmap of zeros copied with B_OP_COPY, which paints black. The status line was a BTextView, which fills with its own colour. The password field was an alpha fill, so only the text view's white showed. CPU Info opened shorter than its two cards and showed a scrollbar. The cards were titled "CPU hardware" and "What R5 uses".

What changed: The signal bars are drawn on the glass. The status line is a string view with a glass fill. The password field is an opaque document colour. CPU Info has no scrollbar and sizes to both cards. The cards are "Processor" and "Enabled features".

Limits: The 5625U screenshots are of the previous build. The short QEMU boot does not open these windows.

Tested: not on the 5625U.

## 2026-11-19 - A paired headset opens A2DP before hands-free

Problem: On the 5625U the WH-1000XM3 paired, the link was authenticated and encrypted, and SDP found A2DP (0x110b) and HFP (0x111e, RFCOMM channel 10). The next line was "RFCOMM: cannot open PSM 3: Connection refused", then "hfp: trying again (1 of 2)", and about two seconds later "link down (reason 0x13, the device ended it)". A2DP was not opened. The class of device was not yet Audio/Video, so bluetooth_audio skipped the headset, and hands-free was the first profile channel. The headset refused RFCOMM and ended the link.

What changed: A paired device with an Audio Sink record is a headset even when the class is still 0. bluetooth_audio logs "opening AVDTP" and connects that channel. Hands-free waits one second when an Audio Sink record is present, and still retries. A hands-free connect runs only while the link is encrypted.

Limits: That the WH-1000XM3 stays connected and plays audio is only proven on the 5625U. The microphone (HFP service level, then the voice link) was not reached. The next stick should show "bluetooth_audio: opening AVDTP", no "cannot open PSM 3" before that, the link still up, and for the microphone "hfp: service level connection up" followed by "hfp: microphone wanted".

Tested: not on the 5625U.

## 2026-11-19 - SAE authentication is sent after the radio is on the channel

Problem: On the 5625U a WPA3 join of "Skynet Guest" selected SAE and management-frame protection, derived the password element, and logged Event EXTERNAL_AUTH. The commit scalar was computed and then "Authentication with ... timed out" after 10 seconds. No SAE Authentication frame left the machine. wpa_supplicant does call send_mlme, and that ioctl calls ieee80211_send_sae_auth(). The join that tunes the MT7921 (stay-on-channel) is only queued by ieee80211_sta_join(), so the commit was handed to the firmware while the radio was still on the scan channel. The access point never answered.

`ioctl[SIOCS80211, op=20, val=0, arg_len=7]: Invalid Argument` is bsd_del_key for key index 4 and 5 at interface start. Those indexes are IGTK slots. The group-key array has four slots (IEEE80211_WEP_NKID), so the delete returned EINVAL. Indexes 0 to 3 succeed. It is not the SAE failure.

What changed: An SAE body that arrives before the station is in AUTH is kept and transmitted when the state machine enters AUTH, which is after mtk_join_prepare() has been granted the channel. The stay is 4 seconds so the commit and the confirm fit. Deleting key index 4 or 5 is a no-op success. WPA2 still uses slots 0 to 3 and the same associate path.

Limits: That "Skynet Guest" completes SAE, associates, and passes traffic is only proven on the 5625U. QEMU has no MT7921.

Tested: not on the 5625U.

## 2026-11-19 - Kernel (AMD64): 2 MB pages on by default

Problem: `amd64_large_pages` (#4435) was off unless the kernel settings said `amd64_large_pages true`. On the 5625U the self-test passed (sticks 53 and 56) and whole sessions ran with it on, without a fault.

What changed: `arch_vm.cpp` (`arch_vm_init_end`) now starts from `true`, also when there is no kernel settings file. `amd64_large_pages false` turns it off. The self-test stays opt-in (`amd64_large_pages_selftest`). `docs/AMD64-LARGE-PAGES.md` and `docs/M13-PLAN.md` say so. Owner decision, 19 Nov 2026.

Limits: on the 5625U no real area used 2 MB pages yet (no `R5 VM: 2MB map` line on those sticks); aligning the GPU areas is M13 step 3. In QEMU, VirtualBox or on other machines a 2 MB aligned area (such as the VESA framebuffer in QEMU) now does get 2 MB pages; that path was proven in QEMU and by the self-test, not on Intel hardware.

Tested: no separate jam (owner); the next stick build compiles it.

## 2026-11-19 - CPU Info and the Wi-Fi dialog draw their text once, on the glass

Problem: On the 5625U, CPU Info still drew a grey bar behind every label and value. BStringView::AttachedToWindow() copies the parent's view colour after the card's AttachedToWindow() had marked the children transparent, so each label was an opaque panel rectangle on the glass. The Wi-Fi connect dialog's "WPA3 Personal" line, "Password" and "Connecting to … checking the password" looked drawn twice. Those views were B_TRANSPARENT_COLOR with B_OP_ALPHA, so app_server did not clear them and the next draw composited onto the previous glyphs. The subtitle was also stored at alpha 180 and blended again by the glass compose.

What changed: CPU Info applies the glass colours in AllAttached, after the string views have copied the parent colour (distro/apps/cpuinfo/CPUInfoWindow.cpp). The Wi-Fi dialog keeps a real panel colour, marks the view glass so the fill is alpha 0, and draws with B_OP_COPY. The subtitle is an opaque dimmed colour. The status line is not invalidated a second time (distro/wifi/lib/WiFiConnectWindow.cpp).

Limits: The 5625U screenshots are of the previous build. This change is what the code does; the short QEMU boot does not open these windows.

Tested: not on the 5625U. One jam (`scripts/build-locked.sh`) and one short QEMU KVM boot of a private copy of `r5-os.image` (`q35`, `-display none`, not `scripts/run-qemu-r5.sh`) reached package activation, vesa and PS/2 with no panic. That boot does not open these windows.

## 2026-11-19 - A WPA3 join no longer deletes its own network

Problem: Joining "Skynet Guest" (WPA3-Personal) on the 5625U reached wpa_supplicant ("haiku: join ssid='Skynet Guest' auth=5 key_mode=0x400 sae=1") and net_server returned success, then the supplicant only scanned. Every scan ended with "No suitable network found" and "Short-circuit new scan request since there are no enabled networks", and the state went back to INACTIVE. No SAE commit was sent. wpa_config_set(network, "sae_pwe", "2") fails: sae_pwe is a global setting, not a network field, and the line number 0 hid the error. The join then removed the network and returned before wpa_supplicant_select_network(). The ioctl SIOCS80211 op 20 Invalid Argument lines are bsd_del_key for key index 4 and 5 at interface start (the driver has four key slots). They are not the join. WPA2 "Skynet" has no MLD address and still associates. Every "Skynet Guest" BSS in that scan is an MLD BSS; that match is only reached once a network exists.

What changed: The Haiku join sets interface->conf->sae_pwe to SAE_PWE_BOTH (hunt-and-peck and hash-to-element) and no longer calls wpa_config_set() for sae_pwe. The package is wpa_supplicant 2.11.r5.8. WPA2 still uses the passphrase path with management-frame protection off.

Limits: SAE authentication against "Skynet Guest" is only proven on the 5625U. If the RSN element the supplicant sees has no SAE AKM or no management-frame protection, or if wpa 2.11 then refuses the MLD BSS because the driver has no MLO, the join still will not associate. The DELKEY index 4 and 5 errors are unchanged so the WPA2 key path is not touched.

Tested: not on hardware in this change. The hpkg is built before the image jam. QEMU has no MT7921, so SAE was not run.

## 2026-11-19 - A direct window's view bitmap is drawn into the front buffer

Problem: GLTeapot on Zink and RADV on the 5625U stayed on one frame while the counter climbed (751 fps, then 999 fps, two screenshots a second apart). VulkanTeapot ran, and GLTeapot on llvmpipe was smooth. The log showed "egl: opengl zink" and "app_server: compositor paused: direct window". Clearing fUpdateRequested in Window::EndUpdate() (the previous 1 fps symptom) did not change this: the swaps return. Zink's present copies the frame into a BBitmap and calls BitmapHook::SetBitmap(). It does not write the BDirectWindow front buffer, which is what llvmpipe's flush_frontbuffer does. While the window has direct access, app_server leaves that rectangle to the client, so the bitmap never reaches the buffer on the display.

What changed: When a directly accessing window sets a view bitmap, the server draws that view and copies the view's screen rectangle to the front. distro/desktop/src/servers/app/ServerWindow.cpp. The compositor pause and the Zink present hook are unchanged.

Limits: That the teapot moves on the 5625U is unproven here. A real BDirectWindow that writes the front buffer itself still has that rectangle drawn by the server when it also sets a view bitmap.

Tested: not on hardware in this change. QEMU has no AMD GPU, so the direct-window blit did not run.

## 2026-11-19 - L2CAP send timer no longer outlives its connection

Problem: Power off on the 5625U panicked in thread "net timer": vm_page_fault at 0x60, L2capEndpoint::_SendTimer -> _SendQueued. The next boot's trail was only "enter reboot=0". RemoveConnection() took the HciConnection off the list and then asked L2CAP to drop its endpoints. connection_for() only accepts a connection still in that list, so Disconnected() never ran, the endpoints stayed OPEN, and the send timer used the freed connection. The log line "Couldn't find the connection to complete l2cap disconnection" is that miss.

The WH-1000XM3 was not ended by R5 with reason 0x16. In boot-5625u-55, 0x16 ("this computer ended it") is logged twice while the game controllers are up, before the headset. Nobody in this tree sends HCI reason 0x16; MediaTek firmware reports it for a disconnect the controller started. The headset itself then sent 0x13 ("the device ended it") about when SDP finished and before any profile channel existed. A2DP connected only once sound was playing, and HFP waited half a second, so the headset dropped the idle link. The microphone (HFP) was not reached.

What changed: l2cap_error_received() accepts the still-living HciConnection after it has left the list, because RemoveConnection() deletes it only after this returns. _MarkClosed() cancels the send timer and frees the queued buffers. bluetooth_audio opens AVDTP as soon as a paired headset's link is up, not only when audio plays. HFP no longer waits half a second before its first RFCOMM connect.

Limits: The panic, the XM3 staying connected, and A2DP playback are only proven on the 5625U. Why the controllers' links were reported as 0x16 is not fixed: this tree never sends that reason.

Tested: not on hardware in this change. QEMU has no Bluetooth controller, so the timer and the headset path did not run.

## 2026-11-19 - Restart syncs before anything else

Problem: On the 5625U a restart's next-boot trail was "r5 previous shutdown: enter reboot=1 -> teams_killed" and nothing after it. Power off, which does not kill teams, reached the end of the trail. sync() never returned. kill_team() only signals a team's main thread and does not wait, so the vnode walk and the block cache ran while those teams were tearing the same objects down. No network share was mounted. The HP CMOS error only followed the forced power-off after that hang.

What changed: system_shutdown() now syncs, runs the shutdown hooks, then resets or powers off. A restart and power off use that same order. Teams are not killed. distro/kernel/src/system/kernel/shutdown.cpp.

Limits: A restart that passes sync() and the hooks is only proven on the 5625U.

Tested: not on the 5625U, so a restart that used to stop at teams_killed is unproven. One short QEMU KVM boot of a private image copy (q35, display none) accepted ACPI power off and the serial log showed enter reboot=0, sync_done, hooks_start, hooks_done, acpi_poweroff, then QEMU exited. Restart (reboot=1) was not run. QEMU is not the 5625U hang.

## 2026-11-19 - GFX9 interrupt ring (IH): barrier, carve-out read, overflow handling, and re-read counters

Problem (`generated/boot-5625u-53/syslog`, 5625U): while RADV/Zink ran, `radeon_hd` logged `ih client 0 src 0 ring 0 vmid 0 0000b514 ...` about once a second. Dword 0 `0x0000b514` decodes to CP (`0x14`), EOP (`181`), ring 0 — a normal end-of-pipe interrupt that `r5_gfx9_ih_parse()` decodes correctly. That means `r5_gfx9_ih_parse()` read zero before the GPU's write was visible, and the entry had landed by the time `dprintf()` printed it.

Cause:
- `ih_writeback()` read `wptr` from the direct DRAM carve-out (`r5_gfx9_vram_cpu(sIhWbOffset)`), while `ih_handler()` read the 32-byte IV entries from `sIhRing + rptr` (BAR mapping) with no `lfence` (`rmb()`) after reading `wptr` and non-volatile reads in `r5_gfx9_ih_parse()`.
- `ih_writeback()` masked `wptr` with `0x0003fffc` (256 KB) on a 64 KB ring (`kIhBytes = 64 * 1024`, mask `0xfffc`) and dropped `RB_OVERFLOW` (bit 0) instead of advancing `rptr` and setting `WPTR_OVERFLOW_CLEAR`.
- Wrapped ring slots were not cleared after processing, so stale non-zero entries could mask incomplete writes on wrap-around.

What changed (`distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/gfx9_k12.cpp`):
- Aligned the IH walk with Linux `amdgpu_ih_process()` / `vega10_ih_get_wptr()`:
  - `ih_get_wptr()` reads writeback, checks `RB_OVERFLOW` (bit 0), advances `rptr = (wptr + 32) & (kIhBytes - 1)` and sets `WPTR_OVERFLOW_CLEAR` (`1u << 31`) in `mmIH_RB_CNTL` on overflow, and masks with `kIhPtrMask` (`0xfffc`).
  - Added `lfence` after reading `wptr` before reading the IV ring.
  - `ih_ring_cpu()` reads the IV ring through `r5_gfx9_vram_cpu(sIhRingOffset)` (same carve-out path as writeback, with `sIhRing` fallback).
  - Snapshots each 32-byte entry via `volatile uint32*` into local `iv[8]`; if `iv[0] == 0`, spins up to 16 × `spin(1)` with `lfence` and re-reads all 8 dwords, counting `sIhRereadCount` and `sIhEmptyCount`. Parses and logs exclusively from `iv[0..7]`, then clears `slot[0] = 0`.
  - Added `sfence` in `ih_set_rptr()` before writing `mmIH_RB_RPTR`.
- Added once-per-boot summary logging (`radeon_hd: ih walk reread ... empty ... overflow ... eop ...` once `sEopCount >= 64`, plus a one-shot `ih walk anomaly` line if any counter becomes non-zero).
- Open-view fixes in the same handler: made `sFaultR` `volatile` with a compiler barrier before advancing it in `r5_gfx9_fault_take()`, and removed the duplicate `r5_gpu_log_error()` check that silenced `radeon_hd: ih ib1 ... ib2 ...`.
- Open-view findings noted (not changed here): `sEopSem` is released on every EOP interrupt forever even though only `run_k1()` acquires it, and `r5_gpu_retire_wake()` / `gpu_retire_thread` wakes once per EOP count without draining/coalescing excess semaphore counts.
- GFX9 start order, registers in `ih_on()`, PSP load, firmware blobs, ring contents, and stop-at-shutdown contract are unchanged.

Tested: `scripts/build-locked.sh` (`jam_exit=0`, `mesa_exit=0`; `gfx9_k12.o` compiled cleanly). Short QEMU KVM boot (`-cpu host,+smap`, 25 s, headless, own copy of `r5-os.image`): 960 serial lines, `enable SMAP`, `radeon_hd: init_hardware`, reached `package_daemon` post-install and PS/2 input init with no panic, KDL, or `vm_page_fault`.

Limits: QEMU has no AMD GPU, so the GFX9 IH walk did not run. Per `AGENTS.md`, this change needs a 5625U boot that lands the K fences (K1/K2) and runs RADV/Zink to prove zero `ih client 0 src 0` lines and inspect the `ih walk` counters. ARM64 and RISC-V 64 not built or tested.

## 2026-11-19 - Stick 53 fixes: Settings/CPU Info UI, Wi-Fi connect/wpa_supplicant, reboot path & trail, GLTeapot updates

Problem: Findings from 5625U `boot-5625u-53`:
1. Settings Information placed "CPU details…" in the bottom row instead of inside the Processor card, labeled GPU acceleration "Drawn by the GPU (hardware)", and CPU Info drew opaque grey cards with dark punch-through highlights on multi-line `BTextView` rows (and leaked its header icon bitmap).
2. `WiFiConnectWindow` drew uninitialized pixel garbage in its offscreen signal tile, overlapping header text (no glass erase), an opaque grey bar for empty status, and opaque grey boxes behind labels/checkbox. Joining WPA2 or WPA3 networks failed immediately without launching `wpa_supplicant` because `build-wpa.sh` skipped `rc`/`xres`/`resattr`, shipping `wpa_supplicant-2.11.r5.6` without `BEOS:APP_SIG`; `CONFIG_DEBUG_SYSLOG` was also off and `wpa_debug_syslog` was 0.
3. `shutdown -r` hung on a black screen on the 5625U (with a CMOS error after forced power-off): `arch_cpu_shutdown(true)` lacked PCI `0xCF9` reset, called `snooze(500000)` with interrupts disabled after KBC `0xFE` (deadlocking without timer IRQs), `system_shutdown(true)` killed its own team, and no shutdown trail survived a hang.
4. GLTeapot on Zink + RADV reported 623 fps while updating ~1 frame/s on screen: `libglvnd` `BGLView` posts `B_INVALIDATE` on every frame into the 100-slot window port; when `app_server` `Window::EndUpdate()` called `_SendUpdateMessage()` for `fPendingUpdateSession` without first clearing `fUpdateRequested`, a non-blocking `B_WOULD_BLOCK` left `fUpdateRequested` stuck true and blocked subsequent `_UPDATE_` messages.

What changed:
- Part 1 (Settings & CPU Info): moved "CPU details…" inside `InfoTile` (Processor card) and renamed the hardware graphics detail to "Hardware accelerated" in `distro/settings/InformationModule.cpp`. Rebuilt CPU Info section cards in `distro/apps/cpuinfo/CPUInfoWindow.cpp` using About R5's frosted-glass card pattern (`set_view_glass`, alpha-composited face/border, transparent glass children including `BTextView`) and freed the header icon bitmap.
- Part 2 (Wi-Fi): zeroed the signal tile bitmap, enabled `set_view_glass`/transparent glass styling on header/labels/checkbox/security/status, and hid empty status in `distro/wifi/lib/WiFiConnectWindow.cpp`. Updated `distro/wpa_supplicant/build-wpa.sh` to run Haiku host `rc`/`xres`/`resattr` so `wpa_supplicant-2.11.r5.7-1-x86_64.hpkg` carries `BEOS:APP_SIG`/`APP_FLAGS`/`ICON`/`APP_VERSION`; enabled `CONFIG_DEBUG_SYSLOG=y` and syslog logging in `distro/wpa_supplicant/patch-src.py` (`MSG_INFO` default, `MSG_DEBUG` with `WIFI_DEBUG` or `WiFi_debug`). Added `/boot/system/bin/wpa_supplicant` fallback launch and syslog join logging (never logging passwords) in `distro/rest/src/servers/net/NetServer.cpp` and `distro/wifi/lib/WiFiController.cpp`.
- Part 3 (Shutdown/reboot): added persistent shutdown trail (`/boot/system/var/log/r5-shutdown-trail`, `fsync`ed per step, logged as `r5 previous shutdown: ...` at `cpu_init_post_modules()` and unlinked) in `distro/kernel/src/system/kernel/shutdown.cpp`, `cpu.cpp`, and `r5_shutdown.h`. Skipped current team in `system_shutdown(true)` `kill_team` loop. In `distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp`, added PCI `0xCF9` reset (`0x02` -> `0x06` -> `0x0e`) between ACPI reset and 8042 KBC, replaced `snooze()` under disabled IRQs with `spin()`, and kept CMOS/RTC untouched and GFX9 stop contract unchanged.
- Part 4 (GLTeapot / app_server): cleared `fUpdateRequested` before calling `_SendUpdateMessage()` in `Window::EndUpdate()` (`distro/desktop/src/servers/app/Window.cpp`) so a momentary `B_WOULD_BLOCK` never wedges future window updates.

Limits: Parts 2 (real MT7921/WPA3-SAE join), 3 (real 5625U restart and persistent trail), and 4 (GLTeapot on Zink + RADV) are only proven on the 5625U stick.

Tested (19 Nov 2026, Europe/Amsterdam): `wpa_supplicant-2.11.r5.7-1-x86_64.hpkg` built and verified with `package list -a` (`BEOS:APP_SIG` present). `scripts/build-locked.sh` in WSL (`jam_exit=0`, `mesa_exit=0`). One short QEMU KVM boot of an own `r5-os.image` copy (`-machine q35,accel=kvm -cpu host,+smap -smp 2 -m 2048 -display none`): 902-line serial log reached `vesa.accelerant`, `R5 interface: HaikuControlLook from distro`, `bluetooth_hid`, `package_daemon` post-install scripts (`result 0`), audio, and PS/2 input without panic.

## 2026-11-19 - AMD64 review fixes: rwlock wake-up, FPU signal validation, math/SIMD/TLB/lock paths

Problem: code review of the AMD64 PRs (#4435–#4450) found a lost wake-up deadlock in `pthread_rwlock` (`WriteLock` / `_Wait` vs `Unlock`) and a non-atomic `owner` read, unmasked `mxcsr` and unsanitized non-XSAVEC headers in `copy_fpu_from_signal_frame()`, `floor`/`ceil`/`trunc` raising `FE_INEXACT` via `roundsd`/`roundss`, `R5_CPU_FMA4` missing the `ymm` (OSXSAVE/XCR0) check, `ClearAccessedAndModified()` clearing shared `ACCESSED`/`DIRTY` bits on 2 MB PDEs, missing `_KERNEL_MODE` guards in the x86-64 SIMD string files (`strlen`, `memchr`, `strchrnul`, `memcmp`, `strcmp`), dead 32-byte-aligned-only AVX2 paths in `memcmp`/`strcmp`, a 15-way switch in `r5_load16()`, global kernel TLB invalidation on user range flushes in `arch_cpu_invalidate_tlb_range()`, redundant 3x bottom-row SSE computation and non-constant shift in `r5_bilinear_xloop_sse2()`, byte-by-byte `memmove` for overlap distances 4–15, `rep movsb`/`rep stosb` startup overhead on small `user_memcpy`/`user_memset` calls, unconditional TSC spinning in `mutex`/`pthread_mutex` on single-CPU or waiter-queued locks, and mismatched `CPUInfo` / `r5_pstate` feature reporting.

What changed:
- `distro/kernel/src/system/libroot/posix/pthread/pthread_rwlock.cpp`: publish `writer_count` before checking `reader_count` in `WriteLock()` (eliminating the lost wake-up against concurrent reader `Unlock()`), call `_Unblock()` on `trywrlock` (`timeout == 0`) rollback, and access `owner` with acquire/release atomics.
- `distro/kernel/src/system/kernel/arch/x86/64/thread.cpp`: mask `fp_fxsave.mxcsr` against `gInitialState`'s `mxcsr_mask` (fallback `0x0000ffbf`) on all paths in `copy_fpu_from_signal_frame()`, and sanitize `xstate_bv` / zero reserved header bytes on `!gHasXsavec` XSAVE systems.
- `distro/kernel/src/system/libroot/posix/musl/math/x86_64/r5_round.c`: pass precision-override bit `0x08` (`1|8`, `2|8`, `3|8`) to `roundsd`/`roundss` for `floor`/`ceil`/`trunc` (and `f` variants); keep `4` for `rint`/`rintf`.
- `distro/kernel/headers/private/system/r5_x86_cpu.h`: gate `R5_CPU_FMA4` on `ymm` (OSXSAVE + XCR0 XMM/YMM).
- `distro/kernel/src/system/kernel/arch/x86/paging/64bit/X86VMTranslationMap64Bit.cpp`: split 2 MB large pages in `ClearAccessedAndModified()` so per-page `ACCESSED`/`DIRTY` bits are preserved across all 512 4 KB PTEs.
- `distro/kernel/src/system/libroot/posix/musl/string/{strlen,memchr,strchrnul}.c` & `distro/kernel/src/system/libroot/posix/string/{memcmp,strcmp}.c`: add `defined(_KERNEL_MODE)` guard to keep the kernel off user-space AVX2 dispatch; remove the 32-byte alignment gate in `memcmp_avx2`/`strcmp_avx2` and use `_mm256_loadu_si256` under the existing `r5_page_room >= 32` check.
- `distro/kernel/headers/private/system/r5_string_x86.h`: simplify `r5_load16()` to `_mm_loadu_si128()` (all callers already guarantee `r5_page_room >= 16`).
- `distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp`: use `arch_cpu_user_tlb_invalidate(context)` instead of `arch_cpu_global_tlb_invalidate()` for user-space range flushes (`context != 0`).
- `distro/app_server/drawing/Painter/bitmap_painter/DrawBitmapBilinear.h`: hoist `bottomSum` calculation out of the 3-channel loop and shift by constant `4` per channel.
- `distro/kernel/src/system/libroot/posix/musl/string/memmove.c`: add 8-byte (`apart >= 8`) and 4-byte (`apart >= 4`) overlapping copy paths before byte-by-byte fallback.
- `distro/kernel/src/system/kernel/arch/x86/64/arch.S`: add `< 64` byte qword/byte fast path in `_arch_cpu_user_memcpy` and `_arch_cpu_user_memset` before `rep movsb`/`rep stosb`.
- `distro/kernel/src/system/libroot/os/locks/mutex.cpp` & `distro/kernel/src/system/libroot/posix/pthread/pthread_mutex.cpp`: skip TSC spin when `__gCPUCount <= 1` or when a non-adaptive lock already has `B_USER_MUTEX_WAITING`.
- `distro/apps/cpuinfo/CpuReport.cpp` & `distro/kernel/x86/r5_pstate.cpp`: report actual `memcpy` (`FSRM/ERMS`/`ERMS`/`SSE2`), `strings` (`AVX2`/`SSE4.2`), and `libm` (`FMA`/`FMA4`/`baseline`) dispatch paths, and gate `R5_KERNEL_F_MWAIT_IDLE` on `IA32_FEATURE_EXT_MONITOR`.

Limits: `x86_cstates.cpp` was left untouched (already fixed by PR #4453). Hardware-specific paths (Intel Comet Lake X1 Carbon Gen 8, AMD FMA4/non-XSAVEC CPUs, 2 MB `amd64_large_pages` opt-in, and high-contention `pthread_rwlock` stress) were not tested on physical hardware here.

Tested (19 Nov 2026, Europe/Amsterdam, ~09:42): `scripts/build-locked.sh` in WSL (`jam_exit=0`, `mesa_exit=0`). One short QEMU KVM boot of an own image copy (`-machine q35,accel=kvm -cpu host,+smap -smp 2 -m 2048 -display none`): serial log reached `enable SMAP`, `enable XSAVEC 0x7 832`, `APIC timer: using TSC-deadline mode`, `R5 kernel: arch_cpu from distro`, `R5 net_server: NetServer from distro`, `R5 interface: HaikuControlLook from distro`, `package_daemon` post-install scripts (`result 0`), and audio/PS2 input drivers without panic.

## 2026-11-19 - AMD64: MWAIT wake, 2 MB page-table reuse, C-state hint

Problem: an idle CPU arms MONITOR on invoke_scheduler and then loads that word. Without a compiler barrier the load can move above MONITOR. The waker stores the word, sees mwait_polling, and skips the reschedule IPI, so MWAIT sleeps on a line that was already written. MapLarge() wrote an empty 4 KB page table (the standby-list link in entry 0) while a CPU could still walk it through a cached PDE. The MWAIT C-state hint used the packed table index. Intel SDM MWAIT says EAX[7:4] is 0 for C1, so the hint is the CPUID C-state number minus 1. Haiku's original x86_cstates.cpp has the same class of bug: sCStateCount * 0x10.

What changed:
- x86_monitor and x86_mwait take a "memory" clobber, in x86_cstates.cpp and in x86_mwait.h (the ACPI idle path). The reschedule IPI is still skipped when mwait_polling is set. The load stays after MONITOR, and atomic_get_and_set stores the monitored int32, so the store hits the monitored line.
- MapLarge() installs the 2 MB PDE, does a local INVLPG and the same synchronous shootdown as _SplitLargePage(), and only then reuses the old table.
- The C-state hint is `(i - 1) << 4`. The loop index i is the CPUID nibble (1 = C1), and MWAIT's EAX[7:4] is 0 for C1.

Checked, not changed:
- The XSAVE header on signal return is not a bug. Intel SDM Vol. 1 section 13.8.2: compacted XRSTOR raises #GP if XCOMP_BV[62:0] sets a bit that is not set in XCR0, or if XSTATE_BV sets a bit (including bits 0 and 1) that is not set in XCOMP_BV. Setting bits 0 and 1 does not fault. XCR0[1:0] are 1 when XSAVE is enabled, and a set XSTATE_BV bit must also be set in XCOMP_BV. Linux sets the same mask. thread.cpp is unchanged.
- User-space non-temporal stores are unchanged in this change. MOVNTDQ to write-back memory does not leave a stale cached line; glibc uses the same three-quarter last-level-cache cutoff. No wrong result found.

Limits: a QEMU boot only shows that the kernel starts. Whether the MWAIT ordering stops a restart from hanging is only proven on the 5625U. 2 MB pages stay off unless amd64_large_pages is set, so MapLarge() does not run on the default boot.

Tested (19 Nov 2026, Europe/Amsterdam, about 09:20): `scripts/build-locked.sh` after the hint correction, `jam_exit=0`, `mesa_exit=0`. An earlier jam in this session still had `i << 4` and was rebuilt. One QEMU KVM boot of an own image copy, `-cpu host,+smap`, `-display none`, `-smp 2`. The serial log contains `enable SMAP` and `R5 net_server: NetServer from distro`, and the boot reaches the input drivers and package scripts. No panic. `app_server` was not in the log before the boot was stopped.

## 2026-11-19 - Deskbar: fix expander clicks, thread/lock safety, leaks, settings sync, and window caching

Problem: code review of Deskbar found that the R5OS primary-click shortcut blocked the expander arrow in vertical `superExpando` mode, `BList::RemoveItem(int32)` left stale `team_id`s in multi-instance teams, `monitor_team_windows()` had a detach NULL dereference, lock-order inversion with `RemoveTeam()`, and stale loop bounds, `kLogoutSession`/`kCheckHungApp` ran synchronous `BAlert::Go()` on `be_app`, Deskbar settings IPC changes were not persisted immediately, `HasListedWindows()` ran `get_token_list()`/`get_window_info()` IPC on every `DrawContent()`/`ExpanderBounds()` call, and several memory/add-on leaks existed.

What changed:
- `distro/desktop/src/apps/deskbar/ExpandoMenuBar.cpp` & `TeamMenu.cpp`: check `ExpanderBounds().Contains(where)` before the primary-click shortcut in `MouseDown()` and guard `Teams()->CountItems() > 0`; cast `team_id` to `(void*)(addr_t)` in `Teams()->RemoveItem()`; stop window monitoring before `BMenuBar::DetachedFromWindow()`; lock `Window()->LockWithTimeout(50000)` before `sMonLocker` to eliminate lock-order inversion with `RemoveTeam()`; adjust `totalItems` / `i--` when adding/removing `TWindowMenuItem`s in `monitor_team_windows()`; for collapsed teams, only re-query window info when the team's token list hash/count changed (`RefreshListedWindowsIfChanged()`).
- `distro/desktop/src/apps/deskbar/BarApp.cpp`, `BarWindow.cpp`, `BarSettings.h`, `ShowHideMenuItem.cpp`: use async `BAlert::Go(new BInvoker(...))` for `kLogoutSession` and `kCheckHungApp`; persist settings via `SaveSettings()` / `kSaveSettings` on all settings-changing IPC messages; initialize `count = 0` in `kUpdateRecentCounts`; check `GetRunningAppInfo() == B_OK` in `ShowHideMenuItem.cpp`; free `BarTeamInfo` copies and `AddTeam()` error-path allocations (`tList`, `icon`).
- `distro/desktop/src/apps/deskbar/BarView.cpp`, `StatusView.cpp`, `DeskbarMenu.cpp`, `TimeView.cpp`, `TeamMenuItem.h/.cpp`: keep the original `kHiddenDimension` square auto-hide sizing in `BarView.cpp`; fix `CheckForScrolling()` (`||`); call `SaveTimeSettings()` before `be_app->PostMessage(kSaveSettings)` for `kShowSeconds`, `kShowDayOfWeek`, and `kShowTimeZone` in `StatusView.cpp`; delete `BPopUpMenu` in `ShowReplicantMenu()`, unload failed add-ons in `LoadAddOn()`, and replace the `snooze(50000)` polling loop in `TReplicantTray::MouseDown()` with a `BMessageRunner`; delete disabled `recentItem[i]` in `DeskbarMenu.cpp`; post `kShowHideTime` via `be_app->PostMessage()` and reuse cached `BTimeFormat` in `TimeView.cpp`; cache `fHasListedWindows` on `TTeamMenuItem`.

Limits: compile-tested via `scripts/build-locked.sh` (`jam_exit=0`); runtime QEMU/hardware Deskbar UI interactions (expander click, clock settings, auto-hide) were not booted or exercised here.

## 2026-11-19 - Media server and media_addon_server robustness fixes

Problem: code review of `media_server` and `media_addon_server` found synchronization races in `DefaultManager` between `_RescanThread()` and `Set()`/`Get()`/`SaveState()`, missing port-message size/count validation and invalid cross-team `delete_area()` calls in `media_server.cpp`, `NotificationManager` holding `fLocker` across blocking `BMessenger::SendMessage()`, unpurged dormant flavors when an add-on rescans to 0 flavors, `ReleaseNodeAll()` skipped on error in `_DestroyInstantiatedFlavors()`, and `MediaFilePlayer::PlayFunction()` leaving trailing EOF buffer bytes unpadded / `fFinished` non-atomic / `fPlayTrack` unreleased.

What changed:
- `distro/media/src/servers/media/DefaultManager.h` & `DefaultManager.cpp`: added `fLock` and `fAudioOutGeneration` to synchronize `_RescanThread()`, `Set()`, `Get()`, and `SaveState()`, snapshotting state before `_ConnectMixerToOutput()` IPC; replaced `strcpy` on `fPhysicalAudioOutInputName` with `strlcpy(..., B_MEDIA_NAME_LENGTH)`; ensured `SaveState()` always calls `ReleaseNodeReference()` on cloned nodes and frees `list` items on early error; waited for `fRescanThread` and freed `fMsgList` in `~DefaultManager()`.
- `distro/media/src/servers/media/media_server.cpp`: validated message sizes in `_HandleMessage()`, `flattened_size` in `SERVER_REGISTER_DORMANT_NODE`, `count` and cloned area sizes in `SERVER_PUBLISH_INPUTS`/`OUTPUTS`, and `max_count` (`< 0` -> `B_BAD_VALUE`, `== 0` -> `B_OK` with count 0, keeping blocking `write_port` for the dormant-node list) in `SERVER_GET_DORMANT_NODES`; initialized `reply` fields in `SERVER_GET_MEDIA_FILE_TYPES`; removed invalid `delete_area(reply.area)` after `_kern_transfer_area()`; assigned `err = be_roster->Launch(&ref)` in `_LaunchAddOnServer()`; continued `_ControlThread()` on `B_INTERRUPTED`.
- `distro/media/src/servers/media/NotificationManager.cpp`: copied matching `BMessenger` targets under `fLocker` before sending notifications with `TIMEOUT`, and checked `FindMessenger`/`FindInt32` return values.
- `distro/media/src/servers/media_addon/MediaAddonServer.cpp`: validated message sizes in `_HandleMessage()`; continued `_ControlThread()` on `B_INTERRUPTED`; purged dormant nodes in `_ScanAddOnFlavors()` only when `!purged && !allocFailed && newFlavorCount <= 0`; avoided skipping `ReleaseNodeAll(node)` in `_DestroyInstantiatedFlavors()`; replaced `strcpy` with `strlcpy(..., B_MEDIA_NAME_LENGTH)`.
- `distro/media/src/servers/media_addon/MediaFilePlayer.h` & `MediaFilePlayer.cpp`: padded trailing buffer bytes on short read/EOF in `PlayFunction()` (`0x80` for `B_AUDIO_UCHAR`, `0` otherwise), made `fFinished` atomic via `atomic_set`/`atomic_get`, and released `fPlayTrack` in `~MediaFilePlayer()`.

Limits: compile-tested via `scripts/build-locked.sh` (`jam_exit=0`); runtime QEMU/hardware audio playback and hot-unplug behavior were not booted or exercised here.

Tested: `scripts/build-locked.sh` in WSL after merging `origin/main` (`jam_exit=0`).

## 2026-11-19 - AMD64 review: dispatch races, kernel NT guard, shutdown trail, Information GPU state

Problem (review of #4435..#4448 and `boot-5625u-52` stick logs):
- `memcpy.cpp`, `memset.cpp`, `strlen.c`, `memcmp.c`, `strcmp.c`, `memchr.c`, `strchrnul.c`, and `r5_span_blend()` in `r5_span_avx.h` had non-atomic one-time dispatch initialization races on multi-threaded first use.
- `memcpy.cpp` and `memset.cpp` are also built into the kernel, where `_mm_stream_si128` (`memcpy_nt`/`memset_nt`) could run on uncacheable memory for >= 3/4 LLC transfers.
- `distro/settings/InformationModule.cpp` always displayed `"Drawn by the processor (software)"` instead of checking GPU availability.
- The first restart on the 5625U stick hung (with CMOS corruption) after `syslog` ended before shutdown; no clear root cause was found in `#4443`/`#4445` by inspection.
- GLTeapot on RADV showed frame by frame (300 fps reported, llvmpipe smooth at 700 fps; RADV was smooth before this round): **not found** in `#4435..#4448`. Ruled out by inspection and `boot-5625u-52` logs: GTT `memcpy` from uncached memory (`gfx9_wc_gtt` is off, so GTT is cached, and both RADV and llvmpipe copy rows to `BDirectWindow`), `Compositor.cpp` / `Painter.cpp` / `ColorConversion.cpp` (`syslog` shows `app_server: compositor paused: direct window`), MWAIT IPI skip / `cpu_idle` reschedule / APIC one-shot / `XSAVEC` (`r5_idle_pingpong`, `r5_timer_test`, `r5_fpu_test` all passed, and multi-threaded llvmpipe is smooth), `pthread_mutex`/`pthread_rwlock`/`malloc` (no x86-64 stall found, shared with llvmpipe), and 2 MB large pages (`2MB pages: off`).

What changed:
- `distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp` & `memset.cpp`: compute dispatch values into locals, publish with `__atomic_store_n`, and set `sMemcpyChosen`/`sMemsetChosen` last with `__ATOMIC_RELEASE` (no spinning/waiting, safe in signal handlers and interrupts); guarded `memcpy_nt`/`memset_nt` out under `#ifndef _KERNEL_MODE`.
- `distro/kernel/src/system/kernel/arch/x86/timers/x86_apic.cpp`: used `__uint128_t` in the APIC one-shot count calculation to prevent `uint64` overflow on large timeouts before clamping to `UINT32_MAX`.
- `distro/kernel/src/system/libroot/posix/musl/string/{strlen,memchr,strchrnul}.c`, `posix/string/{memcmp,strcmp}.c`, `distro/app_server/r5_span_avx.h`: atomic load/store on the function pointer / `useAvx` flag.
- `distro/kernel/src/system/kernel/shutdown.cpp` & `arch/x86/arch_cpu.cpp`: added minimal `dprintf` trail across `system_shutdown` and `arch_cpu_shutdown` steps (GFX9 stop contract untouched).
- `distro/settings/InformationModule.cpp` & `Jamfile`: added `graphics_detail()` querying `/dev/gpu/radeon_hd/0` (`R5_GPU_GET_INFO`, `R5_GPU_CAP_AVAILABLE` and not `R5_GPU_CAP_LOST`).
- `generated/AMD64-REVIEW.md`: full review report.

Limits: The 5625U restart hang and GLTeapot RADV frame-by-frame behavior are not proven fixed here; a restart fix can only be proven on the 5625U.

## 2026-11-19 - Input Server: thread safety, device retry notifications, and mouse/touchpad fixes

What changed:
- `distro/rest/src/servers/input/InputServer.cpp`:
  - `SetNextMethod(bool)`: use `BAutolock` on `gInputMethodListLocker` so `B_BAD_INDEX` does not leak the lock.
  - Synchronized `_AcquireInput()`, `_ReleaseInput()`, and `_DispatchEvent()` with `sEventStateLocker`; reset `fAppServerPort = -1` and `fAppServerTeam = -1` in `_ReleaseInput()`, clean up `fCursorSem` on `clone_area()` failure, and call `_ReleaseInput()` before re-acquiring.
  - Synchronized `fActiveMethod` in `_MethodizeEvents()` / `_UpdateMouseAndKeys()` via `gInputMethodListLocker`, and synchronized `fFrame`, `fMousePos`, and `fKeyInfo` between `MessageReceived()` and `_EventLoop()` via `sInputStateLocker`.
  - `IS_RETRY_UNSTARTED_DEVICES`: call `_DeviceStarted(*item)` and post `IS_NOTIFY_DEVICE` (`started = true`) to `fAddOnManager` when a retried device starts.
  - `HandleGetSetMouseMap()`: validate `size >= (ssize_t)sizeof(mouse_map)` before dereferencing `map`.
  - `_EventLoop()`: delete queued `events` items before `continue` on `Unflatten()` failure.
  - `_StartEventLoop()`, `QuitRequested()`, `~InputServer()`: track `sEventLoopThread` and `wait_for_thread()` before `_ReleaseInput()`; free `fChars` in `~InputServer()`.
  - `_UpdateMouseAndKeys()`: decrement `index` after `events.RemoveItemAt(index)`.
- `distro/rest/src/add-ons/input_server/devices/mouse/movement_maker.h` + `movement_maker.cpp`:
  - Added `MovementMaker::MovementMaker()` and initialized all fields in both `MovementMaker` and `TouchpadMovement`.
  - Guarded `MovementMaker::SetSpecs()` against non-positive `fAreaWidth`/`fAreaHeight` and clamped `fSpeed` to at least `1` (within `int8`) to prevent divide-by-zero.
- `distro/rest/src/add-ons/input_server/devices/mouse/MouseInputDevice.cpp`:
  - `_UpdateSettings()`: copy `accel` into `fSettings.accel` before `MS_SET_ACCEL` `ioctl`.
  - `Start()` / `Stop()`: fail early if `open()` returns `< 0`, guard `close(fDevice)` with `fDevice >= 0`, and reset `fThread = -1` after `wait_for_thread()`.
  - `_ControlThread()`: reset `nextTransferTime` after `ioctl` returns from an idle gap to avoid a catch-up burst.

Limits: not built or booted yet.

## 2026-11-19 - AMD64 stick: the FPU, user-copy and timer tests are in the image

Problem: `distro/cpu/r5_fpu_test.cpp`, `r5_usercopy_test.cpp` and `r5_timer_test.cpp` (#4445) were never copied into the tree or listed in the image, although that CHANGELOG section said the user-copy test was. On the 5625U stick XSAVEC and the SMAP user copies could not be checked.

What changed:
- `distro/kernel/src/bin/Jamfile`: `r5_timer_test.cpp` and `r5_usercopy_test.cpp` join the standard commands. `r5_fpu_test.cpp` uses x86 instructions without a guard, so it is built only when `TARGET_ARCH` is `x86_64`.
- `scripts/apply-kernel-bugs.py`: copies the three sources to `src/bin` and lists them in the minimum image after `r5_idle_pingpong`, with `r5_fpu_test` in an `x86_64 @{ }@` block.
- `scripts/read-stick-logs.py`: also copies `r5-cpu.txt`, `r5-hardware.txt`, `r5-amd64-check.txt` and the `kernel` driver settings (home and system), for `amd64_large_pages`.

Limits: the tests run only when started from Terminal; nothing runs them at boot.

## 2026-11-19 - Kernel (AMD64): /dev/misc/r5_pstate kernel-info ioctl voor CPU Info (stap 3)

What changed:
- `distro/kernel/headers/private/system/r5_pstate_info.h` + `distro/kernel/x86/r5_pstate.cpp`: nieuw read-only ioctl `R5_PSTATE_GET_KERNEL_INFO` (`struct r5_pstate_kernel_info`) op `/dev/misc/r5_pstate` met SMEP, SMAP, NX, PAT, x2APIC, INVPCID, CLFLUSHOPT, PCID, 2 MB pages (`amd64_large_pages`), CPPC/HWP, XSAVE-variant, TSC-deadline vs APIC-timer, cpuidle/MWAIT-idle, release vs debug kernel en `XCR0` (alleen kernelvariabelen, CR4 en XCR0 lezen; geen MSR-writes; `IS_USER_ADDRESS()` + `user_memcpy()`). Bestaande `read()`-rapportage blijft ongewijzigd.
- `distro/kernel/src/system/kernel/arch/x86/timers/x86_apic.cpp` + `cpu.cpp`: `r5_apic_timer_uses_tsc_deadline()` en `r5_cpu_idle_has_module()` toegevoegd.
- `distro/apps/cpuinfo/CpuReport.cpp`: leest `R5_PSTATE_GET_KERNEL_INFO` voor de kernelregel onder `What R5 uses`, met fallback `Not reported`.
- `docs/KERNEL.md`: `R5_PSTATE_GET_KERNEL_INFO` gedocumenteerd.

Tested:
- `scripts/jam-r5-raw.sh` onder `build-lock.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU `-cpu Skylake-Client,+smap,+smep,+tsc-deadline -smp 2`: `enable SMEP`, `enable SMAP`, `R5 net_server: NetServer from distro`, `/boot/system/apps/CPUInfo --cli` (`exit=0`) toont `x86-64-v3`, `User-space paths: memcpy: ERMS, strings: AVX2, libm: FMA`, `XCR0: 0x7 (x87, SSE, AVX)` en `Kernel: release, SMEP, SMAP, NX, PAT, x2APIC, INVPCID, 2MB pages: off, XSAVEC, TSC-deadline, MWAIT idle: off, XCR0 0x7`.

## 2026-11-19 - Apps: CPU Info en 'CPU details…' in Settings (stap 2)

What changed:
- `distro/apps/cpuinfo/` (`Jamfile`, `CPUInfo.rdef`, `CpuReport.h/.cpp`, `CPUInfoWindow.h/.cpp`, `CPUInfoApp.cpp`, MIT): nieuw programma `CPU Info` met Mockup 1-layout (`BPageHeader`, twee `BCard`-blokken voor `CPU hardware` en `What R5 uses`, `set_window_glass(this, true)`, `Copy report` naar `be_clipboard` en `--cli` op stdout).
- `distro/settings/InformationModule.cpp`: knop `CPU details…` toegevoegd die `application/x-vnd.R5-CPUInfo` start; rest van Settings ongewijzigd.
- `scripts/apply-cpuinfo.py` + `scripts/apply-overlay.sh`: kopieert `distro/apps/cpuinfo`, voegt `HaikuSubInclude cpuinfo ;` toe aan `src/apps/Jamfile` en `CPUInfo` aan `SYSTEM_APPS` in `regular`.

## 2026-11-19 - AMD64 user space: x86 code stays off the other architectures

The TSC spin in the kernel mutex and the pthread mutex is compiled only for AMD64. ARM64 and RISC-V keep Haiku's own lock loop: the counted spin in the kernel mutex, and one try then the syscall in the pthread mutex. `ConvertBits` includes `<r5_x86_cpu.h>` through the interface Jamfile's private system headers, and the row loops with `immintrin.h` and the `target` attributes are AMD64 only. The other architectures take the generic per-pixel path. The present copy and the window-buffer blend use AVX2 only on AMD64; elsewhere they are `memcpy` and the C loop. The 16-byte share token in `GPUCopy` is `memcpy` again.

The host `libbe` compiles the same `ColorConversion.cpp` with the host compiler. That Jamfile now asks for the private system headers as well, so `<r5_x86_cpu.h>` is found there too.

Checked (19 Nov 2026, Europe/Amsterdam, about 00:42): `scripts/build-locked.sh` `jam_exit=0`, `mesa_exit=0`, AMD64 only. ARM64 and RISC-V were not built. The first jam stopped because the host `libbe` did not yet have the header path.

## 2026-11-18 - AMD64 user space, step 10: AVX2 row copy and blend

The CPU present copies each damaged row with AVX2 when the CPU has it, and with `memcpy` otherwise. The choice is one function pointer, set once. The window-buffer blend in the write test uses the same formula, eight pixels at a time when AVX2 is on: shift right one, mask `0x7f7f7f7f`, add `0x20202020`. The GPU scanout in `GPUCopy` is unchanged. The share-token copy goes through the same helper, so a 16-byte token still takes the plain path. Skia stays off unless the settings turn it on.

The first jam died while applying the tree (`Cannot allocate memory` on one settings header). The next jam stopped on the helper: `memcpy` returns a pointer, so it could not share the function-pointer type, and the blend helper was unused in `GPUCopy`. Both are fixed. That jam also saw another session's `x86_cstates.cpp` (`mwait_polling`) and an image line for `r5_idle_pingpong`. Those were set aside for this jam only. The following jam was `jam_exit=0`, `mesa_exit=0`.

Checked (18 Nov 2026, Europe/Amsterdam, about 18:54): one QEMU KVM boot of an own image copy, `-cpu host,+smap`, display none, virtio-rng. The serial log contains `enable SMAP`, and the bootscript prints `R5_STEP10_DESKTOP` after Tracker and Deskbar are up. A pixel compare of the AVX2 rows against `memcpy` and the scalar blend was not run.

## 2026-11-18 - AMD64 user space, step 9: Painter bilinear, -O3, font cache

The bilinear scale for a straight RGB copy uses an SSE2 row on AMD64. It does the same multiply-add as `ColorTypeRgb::Interpolate` and writes the same three bytes as `DrawModeCopy`. A bitmap with one row does not read a second row. The 32-bit nasm loop stays the x86 path. `detect_simd` reports MMX and SSE on AMD64, because the baseline is x86-64-v2. Painter, the drawing library and AGG are compiled with `-O3`. The font cache keeps 128 entries instead of 30.

Checked (18 Nov 2026, Europe/Amsterdam, about 18:42): `scripts/build-locked.sh` `jam_exit=0`, `mesa_exit=0`. One QEMU KVM boot of an own image copy, `-cpu host,+smap`, display none, virtio-rng. The serial log contains `enable SMAP`, and the bootscript prints `R5_STEP9_DESKTOP` after Tracker and Deskbar are up. The pixel comparison with the C loop was not run.

## 2026-11-18 - AMD64 user space, step 8: ConvertBits row loops

`ConvertBits` keeps the generic per-pixel path for offsets, byte swaps, and every pair that is not listed here. Four pairs with a zero offset take a row loop instead. RGB32 to RGBA32 and RGBA32 to RGB32 copy the pixel and force the alpha byte to 255, with SSSE3 `pshufb` and an AVX2 row when `R5_CPU_AVX2` is set. RGB24 to RGB32 uses the same shuffle to insert that alpha. RGB16 to RGB32 uses the generic path's left shifts (8, 5 and 3) and the same masks, four pixels at a time with SSE2 and eight with AVX2. CMAP8 to RGB32 reads the palette entry and forces the same alpha. The pointer for each row kind is chosen once.

Checked (18 Nov 2026, Europe/Amsterdam): `scripts/build-locked.sh` `jam_exit=0`, `mesa_exit=0`. The first jam failed because `ColorConversion.cpp` could not see `r5_x86_cpu.h`; the include is now a relative path. The pixel comparison with the generic path was not run. No QEMU boot.

## 2026-11-18 - AMD64 user space, step 7: fma dispatch and SSE4.1 rounding

`fma` and `fmaf` stay the compile-time FMA3 or FMA4 body when the compiler already defines `__FMA__` or `__FMA4__`. On the x86-64-v2 build neither is defined, so the call uses a function pointer chosen once: FMA3 (`vfmadd132sd` / `vfmadd132ss`) when `R5_CPU_FMA` is set, otherwise FMA4 (`vfmaddsd` / `vfmaddss`) when CPUID leaf `0x80000001` ECX bit 16 is set, otherwise musl's C function. `R5_CPU_FMA4` is a new bit in the private CPU word. FMA3 still requires YMM in XCR0, which the existing helper already checks.

`floor`, `ceil`, `trunc` and `rint`, and the float forms, use `roundsd` / `roundss` (SSE4.1, in the v2 baseline, no dispatch). The immediate is 1, 2, 3, or 4 (current rounding mode). The generic C files are not linked on AMD64.

Checked (18 Nov 2026, Europe/Amsterdam): `scripts/build-locked.sh` `jam_exit=0`, `mesa_exit=0`. The bit-identical comparison with musl's C versions was not run. No QEMU boot.

## 2026-11-18 - AMD64 user space, step 6: short lock spin and reader fast path

`__mutex_lock` and `__pthread_mutex_lock` still try the lock once. If that fails, and the call is not a trylock, they read the lock word and execute `pause` until the word is clear or 2000 TSC cycles have passed (8000 when the mutex is adaptive). Then they try once more and, if it is still taken, enter the kernel. The bound is the time stamp counter, so a CPU where `pause` is expensive does not spin longer. `mutex.cpp` is also linked into the runtime loader, which does not provide `system_time_nsecs`, so the spin reads the counter directly.

A private read-write lock takes the reader count with an atomic add and then checks the writer count. If a writer is present the reader drops the count. The last reader takes the internal mutex and wakes a queued writer. A writer publishes its count before it looks at the readers again, so a reader cannot pass that check and keep the lock. The slow path is unchanged when a writer is already waiting.

Checked (18 Nov 2026, Europe/Amsterdam): the first `scripts/build-locked.sh` failed in the runtime loader (`system_time_nsecs` was not linked there) and again on files another session had left in the shared tree. After the spin used the time stamp counter, the next jam was `jam_exit=0`, `mesa_exit=0`. The 12-thread stress test was not run. No QEMU boot.

## 2026-11-18 - AMD64 user space, step 5: heap pools and adaptive mutexes

The heap starts with one pool per CPU, rounded up to a power of two, and never more than `_MALLOC_MUTEXES` (32). One CPU still uses two pools, which is the smallest count the allocator already allowed. Each heap mutex is created with `MUTEX_FLAG_ADAPTIVE`. That is the init in `__init_heap`, the extra mutexes in `init_threaded_malloc`, and the re-init after `fork`.

The pool count is read with `get_system_info`, which is the system-info syscall, so it does not allocate while the heap is still starting.

Checked (18 Nov 2026, Europe/Amsterdam): `scripts/build-locked.sh` `jam_exit=0`, `mesa_exit=0`. The 12-thread stress test from the prompt was not run; this step is jam only. No QEMU boot.

## 2026-11-18 - String scans use the C path in the loader

The EFI loader and the BIOS loader compile the string files with _BOOT_MODE, and not as x86-64-v2. strlen, strchrnul, memchr, strcmp and memcmp take the C path when _BOOT_MODE is set. strchr was already the C call of strchrnul.

The Jamfiles for those scans are copies under distro: musl string, posix string, the kernel lib, the boot loader, and src/bin. Apply copies them. It no longer inserts the private-header line or r5_string_fuzz.cpp into a Jamfile. The image list still names r5_string_fuzz.

Checked (18 Nov 2026, Europe/Amsterdam): `scripts/build-locked.sh` `jam_exit=0`, `mesa_exit=0`. Two earlier jams failed before this one, on files another session had left in the shared Haiku tree (`gHasXsaves` in `arch/x86/64/thread.cpp`, then `r5_fpu_test` / `r5_timer_test` named by the image list and not by the copied `src/bin` Jamfile). Those files are not part of this change. The passing jam is the one after `thread.cpp` was put back to Haiku HEAD. No QEMU boot for this leftover.

## 2026-11-19 - Kernel (AMD64): review-fixes PR #4445 (XSAVEC/XSAVE, cpu_idle reschedule, TSC-deadline fence)

Problem:
- `XSAVES`/`XSAVEOPT` gebruiken de modified optimization op de kernelstack terwijl syscalls de stack tussentijds overschrijven (Intel SDM Vol. 1, 13.6/13.11).
- Na `MWAIT`-wake-up via `invoke_scheduler` zonder IPI reschedulde `cpu_idle()` niet.
- `IA32_MSR_TSC_DEADLINE` miste `mfence; lfence` vóór `WRMSR`.

What changed:
- `distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp`, `64/thread.cpp`, `64/stubs.S`: `XSAVES` en `XSAVEOPT` verwijderd; `XSAVEC`/`XSAVE` behouden zoals in Haiku, samen met XSAVE-header-zeroing en Intel-only `vzeroupper`.
- `distro/kernel/src/system/kernel/cpu.cpp`: `cpu_idle()` controleert na terugkeer van de idle-module `thread->cpu->invoke_scheduler` met uitgeschakelde interrupts en roept `scheduler_reschedule(B_THREAD_READY)` onder `thread->scheduler_lock` aan. Dit MWAIT-pad heeft in QEMU geen cpuidle-module en is alleen op de 5625U te bewijzen.
- `distro/kernel/src/system/kernel/arch/x86/timers/x86_apic.cpp`: `mfence; lfence` vóór `x86_write_msr(IA32_MSR_TSC_DEADLINE, deadline)`.
- `CHANGELOG.md`: stap 6 (`user_strlcpy` is een byte-loop) en stap 7 (QEMU heeft geen cpuidle-module; MWAIT-pad draaide niet in QEMU) gecorrigeerd.

Tested:
- `scripts/jam-r5-raw.sh` onder `build-lock.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU `-cpu Skylake-Client,+smap,+smep,+tsc-deadline -smp 2`: `enable SMEP`, `enable SMAP`, `enable XSAVEC 0x7 832`, `APIC timer: using TSC-deadline mode`, `R5 scheduler topology: 2 CPUs, 2 cores, 1 cache domains, 1 packages`, `R5 net_server: NetServer from distro`.

## 2026-11-18 - Kernel (AMD64): scheduler cache-domein topologie en log (stap 8)

Problem:
- `scheduler` gebruikte wel SMT/core/package en P/E-core-capaciteit (`0x1a`), maar groepeerde cores niet op gedeelde cache (`cache_id`, zoals AMD CCX / Intel LLC) en logde de gebouwde schedulertopologie niet bij boot.

What changed (Step 8):
- `distro/kernel/src/system/kernel/scheduler/scheduler_cpu.h` + `scheduler_cpu.cpp`: `CoreEntry::CacheDomain()`/`SetCacheDomain()` en `gCacheDomainCount`; `FastestIdleCore()` kiest bij meerdere cache-domeinen eerst een idle core in hetzelfde cache-domein (met behoud van hoogste capaciteit).
- `distro/kernel/src/system/kernel/scheduler/low_latency.cpp`: `find_idle_core()` geeft bij meerdere cache-domeinen voorrang aan een idle core in het huidige cache-domein.
- `distro/kernel/src/system/kernel/scheduler/scheduler.cpp`: berekent `gCacheDomainCount` en het cache-domein per core uit `gCPU[i].cache_id[gCPUCacheLevelCount - 1]` (fallback naar `sCPUToPackage[i]` zonder cache-info) en logt eenmalig `R5 scheduler topology: <N> CPUs, <N> cores, <N> cache domains, <N> packages`.

Tested (Step 8):
- `scripts/jam-r5-raw.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU `-cpu EPYC-Rome,+smap,+smep -smp 4,sockets=1,cores=2,threads=2`: `R5 scheduler topology: 4 CPUs, 2 cores, 1 cache domains, 1 packages`, `enable SMEP`, `enable SMAP`, `R5 net_server`, `r5_idle_pingpong PASS rounds=20000 us=1238402` (`exit=0`).

## 2026-11-18 - Kernel (AMD64): MWAIT idle wake-up zonder IPI (stap 7)

Problem:
- `acpi_cstate_ffh_enter` en `x86_cstates` deden `x86_monitor(&dummy, 0, 0)` op een dummy-variabele terwijl `enqueue_thread` in `scheduler.cpp` altijd een `SMP_MSG_RESCHEDULE`-IPI stuurde.

What changed (Step 7):
- `distro/kernel/headers/private/kernel/cpu.h`: `int32 mwait_polling` achteraan `cpu_ent` toegevoegd.
- `distro/kernel/src/add-ons/kernel/power/cpuidle/x86_acpi_cstates/acpi_cpuidle.cpp` + `distro/kernel/src/add-ons/kernel/power/cpuidle/x86_cstates/x86_cstates.cpp`: zetten `mwait_polling = 1` (`__ATOMIC_SEQ_CST`), monitoren `&cpu->invoke_scheduler`, checken `invoke_scheduler == 0` vóór `x86_mwait`, en zetten `mwait_polling = 0` na terugkeer.
- `distro/kernel/src/system/kernel/scheduler/scheduler.cpp`: `enqueue_thread` slaat `smp_send_ici(..., SMP_MSG_RESCHEDULE, ...)` over wanneer `mwait_polling != 0` (de write naar `invoke_scheduler` wekt `MWAIT`); bij HLT-idle blijft de IPI behouden.
- `distro/cpu/r5_idle_pingpong.cpp` + `scripts/apply-kernel-bugs.py`: pingpongtest met twee threads over 20.000 rondes via twee semaphores.

Tested (Step 7):
- `scripts/jam-r5-raw.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU `-cpu EPYC-Rome,+smap,+smep -smp 2`: `no valid cpuidle module found` (het MWAIT-pad zonder IPI draaide niet in QEMU en is alleen op de 5625U te bewijzen; QEMU testte alleen het HLT-pad met `r5_idle_pingpong PASS rounds=20000 us=923680`, `exit=0`).

## 2026-11-18 - Kernel (AMD64): x86_64 user copies en STAC/CLAC (stap 6)

Problem:
- `arch_cpu_user_memcpy`, `arch_cpu_user_memset` en `arch_cpu_user_strlcpy` op x86_64 misten snelle `rep movsb`/`rep stosb`-paden, gebruikten een aparte functie in plaats van één `stac`/`clac`-paar rond de kopie, of misten een assemblerspecifiek strlcpy-pad met fault-handler.

What changed (Step 6):
- `distro/kernel/headers/private/kernel/arch/user_memory.h` + `distro/kernel/src/system/kernel/arch/x86/64/arch.S`: `_arch_cpu_user_memcpy` (`rep movsb`), `_arch_cpu_user_memset` (`rep stosb`) en `_arch_cpu_user_strlcpy` (byte-loop met `-2147478783` / `B_BAD_ADDRESS` bij pagefault), elk met één `stac`/`clac`-paar en `fault_handler`.
- `distro/cpu/r5_usercopy_test.cpp`: test `_kern_*` syscalls op `NULL`, kerneladres, unmapped page en page-straddling.
- `scripts/apply-kernel-bugs.py`: kopieert `user_memory.h`, `arch.S` en `r5_usercopy_test.cpp` en voegt `r5_usercopy_test` toe aan `src/bin/Jamfile` en `minimum`.

Tested (Step 6):
- `scripts/jam-r5-raw.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU `-cpu Nehalem,+smap,+smep`: `enable SMEP`, `enable SMAP`, `R5 net_server`, `r5_usercopy_test PASS` (`exit=0`).

## 2026-11-18 - Kernel (AMD64): TSC-deadline timer en 1-write APIC one-shot fallback (stap 5)

Problem:
- `src/system/kernel/arch/x86/timers/x86_apic.cpp:50-74` deed vier APIC-writes per timer-set (mask, count=0, unmask, count=ticks) en gebruikte geen TSC-deadline mode.

What changed (Step 5):
- `distro/kernel/src/system/kernel/arch/x86/timers/x86_apic.cpp`: gebruikt TSC-deadline mode (`IA32_MSR_TSC_DEADLINE`, één `WRMSR`) wanneer CPUID leaf 1 ECX bit 24 gezet is, `gCPUClockSpeed > 0` en de CPU niet op de bekende Intel-erratalijst staat (Ivy Bridge/Haswell/Broadwell/Skylake zonder hypervisor); als fallback blijft de APIC-one-shot teller unmasked met één `apic_set_lvt_initial_timer_count(ticks)`-write.
- `distro/cpu/r5_timer_test.cpp`: test `snooze()` op 1 ms, 10 ms en 1 s tegen `system_time()`.

Tested (Step 5):
- `scripts/jam-r5-raw.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU `-cpu Nehalem,+smap,+smep`: `APIC timer: using one-shot count mode`, `R5 net_server`, `r5_timer_test req=1000 got=1016`, `req=10000 got=10035`, `req=1000000 got=1000023` (`PASS`, `exit=0`).
- QEMU `-cpu Skylake-Client,+smap,+smep,+tsc-deadline`: `APIC timer: using TSC-deadline mode`, `R5 net_server`, `r5_timer_test req=1000 got=1017`, `req=10000 got=10029`, `req=1000000 got=1000043` (`PASS`, `exit=0`).

## 2026-11-18 - Kernel (AMD64): XSAVE-varianten, 64-byte header-zeroing en Intel `vzeroupper` (stap 4)

Problem:
- `intr_bottom` in `interrupts.S` zeroede de volledige FPU-save-area (`gFPUSaveLength`) vóór XSAVE.
- `arch_cpu.cpp` koos alleen tussen `XSAVEC` en `XSAVE`, zonder `XSAVES`/`XRSTORS` of `XSAVEOPT`, en `64/thread.cpp` kopieerde signaalframes zonder compacted-to-standard conversie.
- `x86_64_syscall_entry` miste `vzeroupper` op Intel bij ingeschakelde AVX.
- `build-locked.sh --debug` herbouwde onvoldoende doordat Jam geen `-D`-defines op de command-line volgt.

What changed (Step 4 + `r5_kdebug_level.h` fix):
- `scripts/jam-r5-raw.sh` schrijft `build/config_headers/r5_kdebug_level.h` met `#define KDEBUG_LEVEL <0|2>` alleen bij wijziging; `distro/kernel/build/config_headers/kernel_debug_config.h` includeert dit bestand vóór `#ifndef KDEBUG_LEVEL`; de define in `distro/UserBuildConfig` is verwijderd.
- `distro/kernel/src/system/kernel/arch/x86/64/interrupts.S`: zeroet in `intr_bottom` alleen de 64-byte XSAVE-header (`512(%rsp)`) bij `gFPUSaveLength >= 576`, en voegt `ALTCODEPATCH_TAG_VZEROUPPER` toe bij `x86_64_syscall_entry`.
- `distro/kernel/headers/private/kernel/arch/x86/arch_altcodepatch.h` en `64/stubs.S`: `ALTCODEPATCH_TAG_VZEROUPPER` (6), `_xsaveopt`, `_xsaves`, `_xrstors`, `_vzeroupper` en `_xrstors_initial`.
- `distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp`: kiest bij boot `XSAVES`/`XRSTORS` (met `IA32_MSR_XSS = 0` op alle CPU's) > `XSAVEOPT` > `XSAVEC` > `XSAVE`, en patcht `_vzeroupper` alleen op Intel met AVX actief.
- `distro/kernel/src/system/kernel/arch/x86/64/thread.cpp`: `arch_thread_init()` gebruikt de actieve XSAVE-variant; `arch_setup_signal_frame()` en `arch_restore_signal_frame()` converteren tussen compacted format en standaard `struct savefpu`.
- `distro/cpu/r5_fpu_test.cpp`: testprogramma voor XMM0–XMM15 en YMM0–YMM15 over syscalls, signalen (`SIGUSR1` + clobber) en interrupts/preemptie.

Tested (Step 4):
- `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU `-cpu Nehalem,+smap,+smep`: `enable SMEP`, `enable SMAP`, `R5 net_server`, `r5_fpu_test PASS avx=0 signals=71` (`exit=0`).
- QEMU `-cpu EPYC-Rome,+smap,+smep`: `enable SMEP`, `enable SMAP`, `enable XSAVES 0x7 832`, `R5 net_server`, `r5_fpu_test PASS avx=1 signals=106` (`exit=0`).

## 2026-11-18 - AMD64 user space, step 4: page-safe string scans

`strlen`, `strchr` (through `strchrnul`), `strchrnul`, `memchr`, `strcmp` and `memcmp` scan 16 bytes at a time with SSE4.2. The load is aligned, or it is two aligned halves joined with `palignr` only when the next 16 bytes stay in the same page, so a read does not cross into an unmapped page. Strings and counts that finish within 32 bytes stay on that path and do not go through a function pointer. Longer work uses a pointer chosen once: AVX2, 32 bytes at a time, when XCR0 has XMM and YMM, otherwise the same SSE4.2 loop. `strchr` is still the one-line call of `strchrnul`.

The same sources are built into the kernel (`src/system/kernel/lib/Jamfile`: memchr 124, memcmp 125, strchr 132, strchrnul 133, strcmp 134, strlen 141) and into the bootloader (`src/system/boot/Jamfile`: memchr 323, memcmp 324, strlen 329, strcmp 331, strchr 339, strchrnul 340). AVX2 stays off unless the CPU helper says the OS enabled YMM, so the bootloader keeps the 16-byte path until that is true.

`r5_string_fuzz` compares each function with the byte version, including strings and counts that end on the last byte of a page, for 60 seconds each.

Checked (18 Nov 2026, Europe/Amsterdam):
- Host, Ryzen 5 5625U, about 16:30: all six PASS for 60 seconds.
- `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU KVM, own image copy, window, virtio-rng, SMAP on. The fuzz prints PASS for each function at 60 seconds, then `PASS all`. Serial has `enable SMAP` on every boot. The script runs only after Tracker and Deskbar are up. `-cpu host,+smap`, `-cpu Nehalem,+smap,+smep` (KVM accepted this model) and `-cpu EPYC-Rome,+smap`. Nehalem has no AVX2, so that guest stays on the 16-byte path. Host and EPYC-Rome take AVX2 for the long scans.

## 2026-11-18 - Kernel (AMD64): syscall-tijdregistratie zonder spinlock (`time_seq`)

Problem:
- `src/system/kernel/arch/x86/64/interrupts.S` (`UPDATE_THREAD_USER_TIME` / `UPDATE_THREAD_KERNEL_TIME`) en `thread.cpp` namen bij elke syscall-entry, syscall-exit, `thread_at_kernel_entry`/`exit` en elke `get_thread_info`/`CPUTime`-lezing `thread->time_lock` (spinlock).

What changed (Step 3):
- `distro/kernel/headers/private/kernel/thread_types.h`: `uint32 time_seq` toegevoegd aan `Thread` met `BeginTimeWrite()`, `EndTimeWrite()` en `ReadTimes()` (standaard seqlock-ordening met `__atomic_thread_fence(__ATOMIC_RELEASE)` / `__ATOMIC_ACQUIRE` en `arch_cpu_pause()`).
- `distro/kernel/src/system/kernel/arch/x86/asm_offsets.cpp` en `distro/kernel/src/system/kernel/arch/x86/64/interrupts.S`: `UPDATE_THREAD_USER_TIME` en `UPDATE_THREAD_KERNEL_TIME` gebruiken `THREAD_time_seq` (`addl $1`) in plaats van `acquire_spinlock`/`release_spinlock` op `THREAD_time_lock`.
- `distro/kernel/src/system/kernel/thread.cpp`, `team.cpp`, `scheduler/scheduler.cpp`, `scheduler/scheduler_cpu.cpp`, `scheduler/scheduler_thread.h`: alle lezers van `user_time`/`kernel_time`/`last_time`/`in_kernel` omgezet naar `ReadTimes()`; schrijvers gebruiken `BeginTimeWrite()`/`EndTimeWrite()`. `thread_reset_for_exec()` houdt `InterruptsSpinLocker` op `thread->time_lock` rond `cpu_clock_offset`.
- `distro/cpu/r5_pstate_log.cpp`: meet `user_time + kernel_time` van een 1-seconde drukke lus via `get_thread_info()` en logt `R5 time check:`.

Tested (Step 3):
- `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`.
- Korte QEMU-boot (`-cpu Nehalem,+smap,+smep`, eigen kopie van `r5-os.image`): schakelt SMEP/SMAP in, bereikt `net_server` zonder panic en logt `R5 time check: busy thread wall=1000000 us user=928103 us kernel=54447 us sum=982550 us`.

## 2026-11-18 - Kernel (AMD64): 64-bits add-with-carry netwerk-checksum (`compute_checksum`)

Problem:
- `src/add-ons/kernel/network/stack/utility.cpp:102` (`compute_checksum`) telde een pakketbuffer per 16-bit woord op in een 32-bit teller.

What changed (Step 2):
- `distro/kernel/src/add-ons/kernel/network/stack/utility.cpp` (gekopieerd via `scripts/apply-kernel-bugs.py`) vervangt de 16-bits lus op `x86_64` door een uitgerolde 64-bits add-with-carry-lus (`addq` + 7× `adcq`, 64 bytes per iteratie, gevolgd door 8/4/2/1-byte staart en 64→32→16-bit vouwen, met byte-swap bij een oneven startadres zoals Linux `csum_partial`).
- Host-fuzz-target `distro/tests/kernel/checksum_fuzz.cpp` (MIT) vergelijkt de oude 16-bits en nieuwe 64-bits implementatie op deterministische patronen en willekeurige buffers, oneven lengtes en oneven startadressen.

Tested (Step 2):
- Host-fuzz-test gedurende 60 s: `PASS: compute_checksum fuzz 60s, iterations=162298176 (odd_len=81007278, odd_addr=81144479), 0 mismatches`.
- `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`. Korte QEMU-boot (`-cpu Nehalem,+smap,+smep`, eigen kopie van `r5-os.image`): schakelt SMEP en SMAP in en bereikt `net_server` in 20 s zonder panic.

## 2026-11-18 - Kernel (AMD64): release-kernel (`KDEBUG_LEVEL 0`) en debug-profiel (`KDEBUG_LEVEL 2`)

Problem:
- `build/config_headers/kernel_debug_config.h:8` stond vast op `#define KDEBUG_LEVEL 2`, waardoor elk image werd gebouwd met paranoïde kernel-heap-fills (`PARANOID_KERNEL_MALLOC`, `PARANOID_KERNEL_FREE`), spinlock-caller-tracking (`DEBUG_SPINLOCKS`), block-cache dirty-checks (`BLOCK_CACHE_DEBUG_CHANGED`) en debug-mutexen (`KDEBUG`, waardoor elke `mutex_lock` / `mutex_unlock` het trage functiepad nam in plaats van de inline fast path).

What changed (Step 1):
- `distro/kernel/build/config_headers/kernel_debug_config.h` zet het standaardniveau op `#ifndef KDEBUG_LEVEL` / `#define KDEBUG_LEVEL 0` (gekopieerd via `scripts/apply-kernel-bugs.py`).
- `distro/UserBuildConfig`, `scripts/build-locked.sh` en `scripts/jam-r5-raw.sh` voegen het debug-profiel `@r5-debug` (`scripts/build-locked.sh --debug` of `R5_KERNEL_DEBUG=1`) toe met `KDEBUG_LEVEL 2` voor QEMU- en test-runs, terwijl `scripts/build-locked.sh` standaard het release-profiel (`@r5-raw`, `KDEBUG_LEVEL 0`) bouwt. `docs/KERNEL.md` documenteert beide profielen.

Tested (Step 1):
- Release-profiel (`scripts/build-locked.sh`, `KDEBUG_LEVEL 0`): `jam_exit=0`, `mesa_exit=0`. Korte QEMU-boot (`-cpu Nehalem,+smap,+smep`, eigen kopie van `r5-os.image`): schakelt SMEP en SMAP in (`enable SMEP`, `enable SMAP`) en bereikt `net_server` zonder panic.
- Debug-profiel (`scripts/build-locked.sh --debug`, `KDEBUG_LEVEL 2`): `jam_exit=0`, `mesa_exit=0`. Korte QEMU-boot (`-cpu Nehalem,+smap,+smep`, eigen kopie van `r5-os.image`): schakelt SMEP en SMAP in en bereikt `net_server` zonder panic.

## 2026-11-18 - Game controllers and Bluetooth devices tested on real hardware

Tested by the owner on the HP ENVY x360 (Ryzen 5 5625U), build 10065, on a stick after stick 54:

- Sony DualSense over USB: buttons, sticks, rumble, trigger feedback and motion work.
- 8BitDo M30 over USB and Bluetooth: works. It has no rumble motor.
- Microsoft Designer Mouse over Bluetooth: works, also after a restart.
- Sony WH-1000XM3 over Bluetooth: connects. Sound (A2DP) and the microphone (HFP) were not tested yet.

For the DualSense over USB this replaces the "only tested in QEMU" limit of build 10065. Still not tested on hardware: the DualSense over Bluetooth, the DualShock 4, the DualSense Edge, and the Switch and Xbox controllers. The logs of this boot were not read off the stick.

## 2026-11-18 - memcpy dispatch also builds into the kernel

The step 3 files are not only libroot. The kernel builds `memcpy.cpp` and `memset.cpp` (`src/system/kernel/lib/arch/x86_64/Jamfile`, lines 32-33) and `memmove.c` (`src/system/kernel/lib/Jamfile`, line 127). The kernel therefore uses the CPUID choice and the non-temporal stores. The QEMU boots of step 3 already ran that kernel. The bootloader builds `memmove.c` (`src/system/boot/Jamfile`, line 325) and keeps `generic_memcpy.c` and `generic_memset.c`, so the bootloader does not take the CPUID or `movntdq` path. Its overlapping `memmove` is the 16-byte SSE loop, and a non-overlapping `memmove` calls the generic `memcpy`. Prompt 4 is also editing the kernel, so this share is recorded here.

`memcpy_choose_bulk` and `memset_choose_bulk` now store the streaming cut, and memcpy also stores the `rep` cut, in statics. A later call above the small size does not read the flags again. The small sizes stay inline.

`rep movsb` with the FSRM cut at 128 bytes ran on the Ryzen 5 5625U: the host fuzz, and the QEMU guest with `-cpu host,+smap` (flags `0x7f`). QEMU `-cpu EPYC-Rome` advertises `0x1f`, so no ERMS and no FSRM. `-cpu Nehalem` advertises none either. Those two guests took the SSE loop and, for the large sizes, the non-temporal stores.

The agreed fuzz length is 60 seconds. On the host it finished in 19 seconds, and again in 20 seconds after the cuts were cached (17043584 checks, flags on and flags forced off). The three QEMU runs were about that minute: 61, 68 and 58 seconds, same check count, all PASS.

## 2026-11-18 - AMD64 user space, step 3: memcpy, memset, memmove

The same `memcpy.cpp`, `memset.cpp` and `memmove.c` are linked into the kernel, and `memmove.c` into the bootloader. Detail is in the entry above.

`rep movsb` and `rep stosb` run only when ERMS is set. Without it the existing SSE loop is used. With FSRM, `rep movsb` starts at 128 bytes instead of 2048. `memset` keeps the 2048 cut. Copies and fills at or above three quarters of the last-level cache use `movntdq` and an `sfence`. The 4K-alias window (distance 1..31 or the last 16 bytes of the page) still stays on the SSE loop. Sizes up to 16 bytes (`memcpy`) and 31 bytes (`memset`) stay on the old inline path, with no function pointer.

Overlapping `memmove` uses 16-byte SSE loads and stores. A backward copy steps by 16 only when the regions are at least 16 bytes apart; a closer overlap is copied byte by byte, because a 16-byte store would reach the next load. It does not use `std` or `rep movsb`. No overlap still calls `memcpy`. Other architectures keep the musl word loop.

`r5_mem_fuzz` compares all three with byte copies: every size 0..4096, every source and destination alignment 0..63, 64 random sizes up to 16 MiB (the last ones sit above the non-temporal cut), and every memmove overlap 1..64 in both directions.

Checked (18 Nov 2026, about 15:30 Europe/Amsterdam):
- Host, 5625U: the fuzz passes in 19 seconds, 17043584 checks. The same binary with the feature flags forced off (SSE and non-temporal only) also passes, same count, 19 seconds.
- `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`. `memcpy.cpp`, `memset.cpp` and `memmove.c` were recompiled.
- QEMU KVM, own image copy. The fuzz does the same 17043584 checks and prints PASS: `-cpu host,+smap` in 61 seconds (serial has `enable SMAP`), `-cpu Nehalem` in 68 seconds (no SMAP on that CPU), `-cpu EPYC-Rome` in 58 seconds (serial has `enable SMAP`). Tracker, Deskbar, Terminal, WebPositive, StyledEdit, Settings and GLTeapot start on each boot. The guest is slower than the host; the matrix is complete on all three.

## 2026-11-18 - AMD64 user space, step 2: one CPUID helper

`headers/private/system/r5_x86_cpu.h` is the only place that reads CPUID for the later user-space dispatch. It is a private header. libroot exports no new symbol. The first call stores the result; later calls in that translation unit reuse it.

What it reads, once: leaf 1 and leaf 7, and XCR0 when OSXSAVE is set. AVX, AVX2 and FMA are set only when XMM and YMM are both enabled in XCR0. BMI1, BMI2, ERMS and FSRM come from leaf 7. The last-level cache is leaf 4 on GenuineIntel and leaf `0x8000001D` on AuthenticAMD (highest data or unified level). Anything else, or a size that does not fit, uses 8 MiB. x86-64-v2 is the baseline and is not a flag.

`r5_cpu_flags` prints the flags and the cache size. It is the check program for this step.

Checked (18 Nov 2026, about 15:00 Europe/Amsterdam):
- Host, Ryzen 5 5625U: flags `0x7f` (AVX, AVX2, FMA, BMI1, BMI2, ERMS and FSRM) and a 16 MiB last-level cache. A second call returns the same value.
- `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`.
- QEMU KVM, own image copy, `-cpu host,+smap`: serial has `enable SMAP` and `R5 net_server`. After Sign in, `r5_cpu_flags` prints the same `0x7f` and 16 MiB, and Tracker, Deskbar, Terminal, WebPositive, StyledEdit, Settings and GLTeapot are running.
- QEMU KVM `-cpu Nehalem`: flags `0` (no AVX, AVX2, FMA, BMI, ERMS or FSRM). Leaf 4 has no cache entry in this QEMU model, so the helper uses the 8 MiB fallback. The same apps start. This CPU has no SMAP; the kernel does not print `enable SMAP`.
- QEMU KVM `-cpu EPYC-Rome`: flags `0x1f` (AVX, AVX2, FMA, BMI1, BMI2; no ERMS or FSRM, which this model does not advertise) and 16 MiB from leaf `0x8000001D`. Serial has `enable SMAP`. The same apps start.

## 2026-11-18 - AMD64 user space, step 1: GNU hash and local binding

Problem: user-space binaries used only the SysV hash (`DT_HASH`). Calls inside `libbe` and `app_server` could go through the PLT, and the compiler assumed another library might interpose those functions. `malloc` must stay interposable, so `libroot` is left alone.

What changed:
- `distro/jam/ArchitectureRules`: on x86_64, user-space `HAIKU_LINKFLAGS` gain `-Wl,--hash-style=both` (SysV hash and `DT_GNU_HASH`). The runtime loader already reads `DT_GNU_HASH` in `elf_load_image.cpp`. Kernel add-ons append `HAIKU_KERNEL_ADDON_LINKFLAGS` (`--hash-style=sysv`) after that, so the last flag wins and they stay SysV.
- `libbe` (`distro/desktop/src/kits/` Jamfiles): `-fno-semantic-interposition` on the kit sources, and `-Wl,-Bsymbolic-functions` on `libbe.so`. `BString`'s counted `strncpy` into a buffer that already has a NUL at that length warns under the new flag; that warning is not an error (`-Wno-error=stringop-truncation` on the support kit).
- `app_server`, its drawing library and Painter: the same compile flag, plus `-Wl,-Bsymbolic-functions -Wl,--gc-sections` on the executable.

Checked (18 Nov 2026, about 14:38 Europe/Amsterdam):
- `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`. A `ColumnListView.cpp` compile line contains `-fno-semantic-interposition`. `readelf -d` on `libbe.so` shows both `(HASH)` and `(GNU_HASH)`. The vesa kernel add-on still has only `(HASH)`. `libbe.so` has no `JUMP_SLOT` relocation to a function it defines. BeDecorator's 65 undefined symbols that `app_server` exports (Decorator, TabDecorator, SATDecorator, DecorAddOn) are still exported after `--gc-sections`.
- QEMU, KVM, `-cpu host,+smap`, an own copy of `r5-os.image`: serial has `enable SMAP` and `R5 net_server`. After Sign in, `ps` shows Tracker and Deskbar as the signed-in account, and Terminal, WebPositive, StyledEdit, Settings and GLTeapot. VulkanTeapot was not at `/boot/system/apps` or the demos path, so it was not started. No speed measurement.

## 2026-11-18 - AMD64 baseline is x86-64-v2, including the kernel

Problem: user space and the kernel were built as x86-64-v1 (`-march=x86-64`). POPCNT, SSE4.2 and the rest of x86-64-v2 were not available to the compiler. A higher baseline with no check in the loader stops an older CPU with `#UD` and no message.

What changed:
- `distro/jam/ArchitectureRules` adds `case x86_64 : archFlags += -march=x86-64-v2`. `archFlags` is in `ccBaseFlags` and in `HAIKU_CCFLAGS`, so user space, the kernel and kernel add-ons are v2. Chosen 18 Nov 2026.
- The same file strips `-march=x86-64-v2` from the boot-loader flags (the loaders inherit `HAIKU_CCFLAGS`) and prints `R5 boot loader flags` for each loader. `bios_ia32` stays `-march=pentium` and `-m32`.
- One check in `distro/boot/arch/x86/arch_cpu.cpp` (`boot_arch_cpu_init`). The EFI loader already calls it. The BIOS loader calls it from `distro/boot/bios_ia32/cpu.cpp`. Missing SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B or LAHF/SAHF prints `This R5 Operating System build requires an x86-64-v2 CPU (SSE4.2 / POPCNT).` and halts. LAHF/SAHF is part of GCC's x86-64-v2; the screen text names SSE4.2 and POPCNT.
- `scripts/run-qemu-r5.sh` uses `-cpu Nehalem,+smap,+smep` when KVM is absent, and `-cpu host` when KVM is present. The Skia and Bluetooth QEMU scripts use the same CPU. `qemu64` is below v2 (no SSSE3, SSE4.1, SSE4.2 or POPCNT on QEMU 10.2.1).

Tested: `scripts/build-locked.sh` gives `jam_exit=0`, `mesa_exit=0` (18 Nov 2026, about 13:31 Europe/Amsterdam). The jam log prints `R5 boot loader flags` for `bios_ia32`, `efi` and `pxe_ia32`. None of those lines contains `-march=x86-64-v2`. `bios_ia32` and `pxe_ia32` have `-march=pentium -m32`. The kernel line is `-march=x86-64-v2` plus `-mno-red-zone -fno-omit-frame-pointer -fno-tree-vectorize`, and it has no `-mno-sse`. QEMU 10.2.1, KVM, an own copy of `r5-os.image` (not `scripts/run-qemu-r5.sh`):
- `-cpu Nehalem,+smap,+smep`, 150 s: serial has `enable SMAP` and `R5 net_server`. The screen is the first-boot window ("Welcome to R5 Operating System") on the blue desktop. No panic and no invalid-opcode line.
- `-cpu qemu64`, BIOS: serial is only `This R5 Operating System build requires an x86-64-v2 CPU (SSE4.2 / POPCNT).` The guest stays at that text screen until it is killed (the loader waits for a key).
- OVMF (`/usr/share/ovmf/OVMF.fd`) starting the built `haiku_loader.efi`: `-cpu qemu64` prints the same sentence and `*** PANIC ***`. `-cpu Nehalem,+smap,+smep` prints `Welcome to the Haiku boot loader!` and then finds no boot volume, because that disk is only the loader. The raw `r5-os.image` has no EFI system partition, so OVMF on that image stops with `No bootable option or device was found` and never enters the loader.

Limits: no stick boot and no step 0 benchmark, so no speed claim. The PXE loader is not on the v2 march, and it does not call the CPU check. VirtualBox was not booted. ARM64 and RISC-V rules were not edited.

## 2026-11-18 - Kernel (AMD64): a missing standby page table stops with a panic, not silently

Problem (review of #4435): `_SplitLargePage()` checked for an empty standby page-table list only with `ASSERT_PRINT`. A release kernel (`KDEBUG_LEVEL 0`, prompt 4) compiles that away. On an empty list the split would then write 512 entries into physical page 0. It should not happen, because every 2 MB PDE takes one standby table when it is mapped, but the result would be silent memory damage.

What changed: `X86VMTranslationMap64Bit.cpp` `_SplitLargePage()` calls `panic("R5 VM: no standby page table to split the 2 MB PDE at va … (reason)")` when the list is empty, in every kernel. Every caller checks that the PDE is present and large before splitting, so this is the only unchecked case.

Tested: `scripts/build-locked.sh` gives `jam_exit=0`, `mesa_exit=0`. QEMU (KVM, `-cpu host,+smap`, a copy of the image, 120 s): `enable SMAP`, `R5 VM: amd64_large_pages disabled`, boots to `net_server`, no panic. The new branch itself was not reached (it should never be).

## 2026-11-18 - Kernel (AMD64): 2 MB large pages (`PS` PDE's) voor fysiek aaneengesloten en device-geheugen

Problem:
- `X86PagingMethod64Bit.cpp:387` en `X86VMTranslationMap64Bit.cpp` lieten buiten `KERNEL_PMAP_BASE` alleen 4 KB PTE's toe, zodat grote MMIO-/framebuffer-/VRAM-mappings (`vm_map_physical_memory`, `vm_clone_area`) en aaneengesloten kernelbuffers (`B_CONTIGUOUS` en `B_FULL_LOCK` met 2 MB run) altijd 512 4 KB PTE's per 2 MB gebruikten.
- `X86_64_PDE_ADDRESS_MASK` (`0x000ffffffffff000L`) nam op een 2 MB PDE bit 12 (`X86_64_PDE_PAT`) mee in het fysieke adres, waardoor `Query()` en `DebugGetReverseMappingInfo()` bij Write-Combining (`WC`) 2 MB PDE's een fysiek adres met `+0x1000` teruggeven. Daarnaast ontbrak `X86_64_PTE_PAT` in `X86_64_PTE_MEMORY_TYPE_MASK` en controleerde `~X86VMTranslationMap64Bit()` niet op `X86_64_PDE_LARGE_PAGE` in user-space.

What changed:
- Aparte commit eerst: `X86_64_PDE_LARGE_ADDRESS_MASK` (`0x000fffffffe00000L`) toegevoegd aan `paging.h` en gebruikt in `Query()` en `DebugGetReverseMappingInfo()`, plus `X86_64_PTE_PAT` toegevoegd aan `X86_64_PTE_MEMORY_TYPE_MASK`.
- Implementatie van `docs/AMD64-LARGE-PAGES.md`:
  - Kernelsetting `amd64_large_pages` (standaard `false`, gelogd bij boot als `R5 VM: amd64_large_pages enabled/disabled`).
  - `MapLarge()` op `VMTranslationMap` / `X86VMTranslationMap64Bit` voor 2 MB-uitgelijnde mappings in `vm_map_physical_memory()`, `vm_clone_area()` (zowel `CACHE_TYPE_DEVICE` als 2 MB-aaneengesloten `B_CONTIGUOUS`/`B_FULL_LOCK`), `B_CONTIGUOUS` en `B_FULL_LOCK` met 2 MB run (met 4 KB als fallback).
  - Eén standby 4 KB paginatabel per 2 MB PDE uit de bestaande `MaxPagesNeededToMap()`-reservering (`fStandbyPageTableHead`), zodat een latere split nooit hoeft te alloceren.
  - Splitsen zonder gat (`_SplitLargePage()`): geen break-before-make (het tijdelijk wissen van de PDE zou op een andere CPU of in een interrupt een fatale kernel-pagefault op VRAM/ringen kunnen veroorzaken). Conform Intel SDM Vol. 3A sectie 4.10.4.2 (*"Software should not write to a paging-structure entry in a way that would change, for any linear address, both the page size and either the page frame, access rights, or other attributes"*) en AMD64 APM Vol. 2 sectie 5.5.4 / 7.3.4 wordt eerst de standby 4 KB paginatabel gevuld met 512 PTE's met exact dezelfde fysieke frames en attributen (`X86_64_PDE_PAT` bit 12 → `X86_64_PTE_PAT` bit 7) gevolgd door een write-barrier, daarna de PDE in één atomaire store omgezet naar die tabel, vervolgens de 2 MB range synchroon op alle CPU's geïnvalideerd, en pas daarna de `Unmap`-/`Protect`-wijziging op de 4 KB PTE's toegepast.
  - Alle `VMTranslationMap`-methodes (`Map`, `MapLarge`, `Unmap`, `UnmapPage`, `UnmapPages`, `UnmapArea`, `Query`, `QueryInterrupt`, `Protect`, `ClearFlags`, `ClearAccessedAndModified`, en de `X86_64_PDE_LARGE_PAGE`-guard in `~X86VMTranslationMap64Bit()`) verwerken 2 MB PDE's met `fMapCount` in 4 KB pagina's.
  - Rate-limited logging per 2 MB map (`R5 VM: 2MB map ...`) en split (`R5 VM: 2MB split ... (reason)`), plus boot-zelftest achter `amd64_large_pages_selftest`.

Tested: `scripts/build-locked.sh` geeft `jam_exit=0`, `mesa_exit=0`. Drie korte QEMU-boots (KVM, `-cpu host,+smap`, eigen kopie van `r5-os.image`):
1. `amd64_large_pages` uit (standaard): logt `R5 VM: amd64_large_pages disabled`, boot naar `net_server` en `vesa_init()`, geen panic.
2. `amd64_large_pages true` (zonder zelftest): logt `R5 VM: amd64_large_pages enabled`, mapt o.a. de 16 MB VESA-framebuffer (`0xfd000000`, `type 0x20000000` = `WC`) in zowel kernel-space als de `app_server` user-clone met 8 × 2 MB PDE's (`R5 VM: 2MB map ...`), boot naar `net_server`, geen panic.
3. `amd64_large_pages true` + `amd64_large_pages_selftest true`: voert de 4 MB `B_CONTIGUOUS` map, `B_WRITE_COMBINING_MEMORY` `Query()`-maskertest, user-space clone + read onder SMAP + destructor-guard, en 1 MB split-protect (`R5 VM: 2MB split ... (protect_partial)`) + unmap uit en logt `R5 VM: amd64_large_pages self-test PASS (4 MB map, WC mask check, user clone/exit, 1 MB split-protect, unmap)`, gevolgd door `net_server` en `vesa_init()`, geen panic.

Limits: niet getest op de Ryzen 5 5625U; de USB-stick op hardware (sectie 6 van `docs/AMD64-LARGE-PAGES.md`) volgt later.

## 2026-11-18 - Kernel (AMD64): microcode-grenzen, INVPCID type 2, TLB-range-limiet, clflushopt en amd_pstates

Problem:
- `find_microcode_amd()` in `arch_cpu.cpp` las `table[i].installed_cpu` tot een `0`-sentinel zonder `section->size` of de resterende containergrootte te begrenzen.
- `arch_cpu_global_tlb_invalidate()` las `CR4` in een `uint32` en schakelde altijd tweemaal `CR4.PGE`, ook wanneer `INVPCID` beschikbaar is.
- `arch_cpu_invalidate_tlb_range()` riep `invlpg` per pagina aan zonder bovengrens op het aantal pagina's. Op x86 wordt deze functie alleen aangeroepen vanuit `SMP_MSG_INVALIDATE_PAGE_RANGE` (`smp.cpp:802`), dat alleen vanuit `x86_physical_page_mapper_large_memory.cpp:506` voor globale kernelpagina's wordt verstuurd.
- `arch_vm_flush_memory_type_range()` gebruikte altijd geserialiseerde `clflush`, ook wanneer `CLFLUSHOPT` beschikbaar is.
- `amd_pstates` forceerde in `init_pstates()` altijd `SCHEDULER_MODE_LOW_LATENCY` (met dubbele IPI) en herstelde in `uninit_pstates()` niet de schedulermodus die vóór `init_pstates()` actief was.

What changed:
- `find_microcode_amd()` controleert dat de eerste sectie binnen `size` past en begrenst de equivalentietabel-lus met `(i + 1) * sizeof(amd_equiv_cpu_entry) <= section->size`.
- `arch_cpu_global_tlb_invalidate()` gebruikt `invpcid` type 2 wanneer `IA32_FEATURE_INVPCID` (`FEATURE_7_EBX`) aanwezig is, en bewaart `x86_read_cr4()` in `addr_t` als fallback. Geen PCID (5625U heeft CPUID leaf 1 ECX bit 17 = 0).
- `arch_cpu_invalidate_tlb_range()` valt bij `start > end` of `>= 64` pagina's terug op `arch_cpu_global_tlb_invalidate()` (niet een CR3-reload, omdat de pagina's globaal zijn). Op AMD64 heeft dit geen effect: `src/system/kernel/arch/x86/Jamfile` bouwt op `x86_64` `x86_physical_page_mapper_mapped.cpp` en niet `x86_physical_page_mapper_large_memory.cpp`, zodat `SMP_MSG_INVALIDATE_PAGE_RANGE` en `arch_cpu_invalidate_tlb_range()` op AMD64 nooit worden aangeroepen.
- `arch_vm_flush_memory_type_range()` gebruikt `clflushopt` gevolgd door `sfence` wanneer `IA32_FEATURE_CLFLUSHOPT` (`FEATURE_7_EBX`) beschikbaar is, met `clflush` + `sfence` als fallback.
- `amd_pstates` bewaart in `init_pstates()` de huidige schedulermodus (`_user_get_scheduler_mode()`) in `sSavedSchedulerMode` en herstelt die modus in `uninit_pstates()` synchroon op alle CPU's. `MSR_AMD_CPPC_ENABLE` blijft ongewijzigd (kan op AMD CPPC wel op 1 worden gezet maar niet naar 0 worden gewist).

Tested: `scripts/build-locked.sh` geeft `jam_exit=0`, `mesa_exit=0`. Korte QEMU-boot (KVM, `-cpu host,+smap`, eigen kopie van `r5-os.image`): rapporteert `invpcid`, `smap` en `cflushopt`, schakelt SMAP in (`enable SMAP`), doorloopt `arch_cpu` en `arch_vm` (PAT) en `vesa_init()` zonder panic.

Limits: QEMU onder KVM geeft het hypervisor-bit door en biedt geen AMD CPPC-MSR's of microcode-container-update op echte cores; het `amd_pstates`- en microcodepad zijn niet op de 5625U geboot. `arch_cpu_invalidate_tlb_range()` is op AMD64 onbereikbaar.

## 2026-11-18 - IPv6: review fixes for DHCPv6, SLAAC, the resolver, UDP sources and the default route

Problem (review of #4425, before merge):
- One stray UDP packet on port 546 removed the DHCPv6 address.
- DHCPv6 sent Solicit every 2 s forever.
- An IPv6 resolver update dropped the IPv4 DNS servers and the domain.
- A UDP socket kept the source of its first connect() (127.0.0.1, or an old DHCP address) for every later peer. This is an IPv4 regression.
- Removing a global address deleted the IPv6 default route.
- DAD before the link-local address treated any socket error as a collision, which could turn IPv6 off on link-local-only links.

What changed:
- DHCPv6 (`DHCPv6Client.cpp`):
  - A packet with a wrong transaction id, type or server is dropped without touching the lease.
  - A retransmission keeps its transaction id, and RT doubles with jitter up to the RFC 8415 maximum (Solicit and Information-Request 3600 s, Request 30 s, Renew and Rebind 600 s). Request gives up after ten tries.
  - A refusal starts over after a delay that doubles, up to an hour.
  - NoBinding sends a Request for the same lease.
  - T1 > T2 discards the IA. T1 and T2 default to 0.5 and 0.8 of the preferred lifetime; an infinite preferred lifetime means no renewal, never an immediate one.
  - A new address replaces the old one instead of leaking it.
  - The lease is removed when the client is deleted (link down).
- Resolver (`NetServer.cpp`):
  - Every source writes its own block: "# interface ipv4" for IPv4 DHCP, the device for SLAAC and DHCPv6. An update replaces only its own servers and domain, and untagged lines from an older file belong to ipv4.
  - resolv.conf is written to a temporary file and renamed.
- UDP (`udp.cpp`):
  - The endpoint remembers whether bind() named an address.
  - Only then is the source kept. Otherwise connect() takes it from the route each time, and disconnect (AF_UNSPEC) forgets it.
  - A bound source is never replaced on send.
- IPv6 (`ipv6.cpp`): the IP layer only fills an empty source and never replaces one a transport chose, so TCP no longer registers on one address and sends from another.
- Routes (`routes.cpp`): when an address goes, an IPv6 gateway or default route bound to it moves to another address on the same interface (global first, else link-local) instead of being deleted. The replacement is found before the domain lock is taken.
- SLAAC (`SLAACClient.cpp`):
  - Only a DAD collision stops the link-local address.
  - An RA must come from a link-local address, with hop limit 255 when the header is visible.
  - A PIO with preferred > valid is ignored.
  - The two-hour rule protects an address we hold.
  - The default route is not removed and re-added on every RA, ends with the router lifetime, and is not taken over by a second router while the first is alive.
  - Addresses, on-link routes and the default route are removed when the client is deleted.

Tested: `scripts/build-locked.sh` gives `jam_exit=0`, `mesa_exit=0`. QEMU (KVM, `-cpu host,+smap`, usernet with `ipv6=on`, 150 s): `enable SMAP`, boots to `net_server`, no panic.

Not proven: the guest syslog could not be read back from the image after the QEMU run, so SLAAC's behaviour on QEMU's RA was not seen. No DHCPv6 server, no real router, and no IPv4/IPv6 dual-stack LAN were tried. Still open from the review: RFC 6724 source selection beyond "first global" (ULA versus GUA), the DHCPv6 elapsed-time option, RDNSS lifetime and DNSSL, and the zone suffix format of a link-local DNS server.

## 2026-11-18 - IPv6: the default route sorts last, and the checksum follows the source

Problem: an all-zero IPv6 mask was counted like a host mask, so `::/0` did not sit under the more specific prefixes. A default route bound to `fe80::` was still the source after TCP or unconnected UDP had already checksummed the packet. A socket bound to one local address was rewritten to the destination when that destination was also local. SLAAC kept only the last prefix, skipped duplicate-address detection for the link-local address, and did not install an on-link route for `L=1`. DHCPv6 installed an `IA_NA` address as `/64`, ignored status codes, and stopped after the first reply. Stopping either client killed the worker, which could leave its socket open in the network server. `resolv.conf` filled up with old servers, and a link-local DNS address was written without a zone.

Changed: the mask bit is the least significant set bit, matching IPv4, so `::/0` returns 128 and sorts last. An off-link route is bound to a global or ULA address when the interface has one, including after SLAAC or DHCPv6 adds that address. UDP chooses that source before the checksum. If IPv6 still changes the source, the TCP or UDP checksum is updated with the incremental rule. A bound source is left alone when the destination is local; only `::` is filled in. SLAAC applies every `/64` autonomous prefix, runs duplicate-address detection before the link-local address, and keeps `L=1` prefixes as on-link routes until the lifetime is withdrawn or runs out. DHCPv6 uses a `/128` mask, logs status codes, and renews or rebinds until the valid lifetime, then removes the address. Both clients wake their own thread and wait for it to close the socket. New resolver servers are written first, per interface, and a bare `fe80::` DNS address gets `%<device>` or is dropped. The address walk in `SIOCADDRT` holds the interface lock. `0064:ff9b::/96` is no longer treated as an empty source.

Limits: jammed (`jam_exit=0`, 11:14 Europe/Amsterdam). Not booted. QEMU has no IPv6 router, so SLAAC routes, DHCPv6, and an off-link TCP or UDP checksum were not tried on a wire. The kernel paths do not read a user pointer themselves; route control still copies the request with `user_memcpy`.

## 2026-11-18 - Build: main builds again after the Wi-Fi merges

Problem: #4419, #4421 and #4422 were merged without a jam. Main (`d2e4038d0b`) did not build:
- `WiFiView.cpp` (4 places) and `WiFiConnectWindow.cpp` (1 place) called `BMessenger::PostMessage`, which does not exist.
- `SLAACClient.cpp` (2 places) passed a `BNetworkAddress` to `BNetworkRoute::SetGateway(const sockaddr&)`; the conversion is ambiguous.
- `scripts/apply-wireless.py` did not copy the new `WiFiConnectWindow.cpp` into the tree ("don't know how to make").
- The image could not resolve `wpa_supplicant-2.11.r5.6-1`: it required `lib:libnetwork`, `lib:libbe` and `lib:libbnetapi`, which the haiku package does not provide as such. That has been true since r5.1 (#4419), so no image was ever built with R5's supplicant.

What changed:
- `BMessenger::SendMessage` instead (asynchronous, as `PostMessage` would have been).
- The `sockaddr_in6` is passed to `SetGateway` directly.
- `WiFiConnectWindow.cpp` is in `LIB_SOURCES`.
- `build-wpa.sh` requires only `haiku` and `lib:libcrypto`, as Haiku's own package does. The r5.6 package was repacked with that `.PackageInfo`; the binary is unchanged and the version stays.

Tested: `scripts/build-locked.sh` gives `jam_exit=0`, `mesa_exit=0`. QEMU (KVM, `-cpu host,+smap`, a copy of `r5-os.image`, 150 s): `enable SMAP`, boots to `net_server`, no panic. QEMU has no Wi-Fi adapter and no IPv6 router, so the Wi-Fi changes and the SLAAC route removal did not run.

Limits: nothing of #4419, #4421 or #4422 was tried on hardware.

## 2026-11-18 - Wi-Fi: the connect window

Problem: the Wi-Fi page asked for a password in the page itself, and the Deskbar asked in a second window. The Deskbar stored the password as soon as the join was requested, before the handshake. A join that timed out was described as a wrong password, which is what the earlier HT join showed.

Changed: Settings, the Wi-Fi program and the Deskbar open "Connect to Wi-Fi". The password field is the shared password control, with the eye. A refused key draws that field's border in the failure color. The window asks for glass once, on the window. Connect automatically is on. The password is stored only after the link is up, and only when that box is on, and only for a join the user started. A 4-way handshake mismatch and a rejected SAE confirm send `key_failed` at once. The package is `wpa_supplicant-2.11.r5.6-1`. A timeout says the router was not reached and the password was not checked. The muted line names the security, the signal, and the router standard when this station is already associated to that network. The Deskbar keeps one dialog. The typed password is cleared in the field and in the join message.

Limits: not jammed and not booted. The glass was not looked at on a screen. A timeout is still not a rejected key. QEMU has no Wi-Fi adapter. The right password, a wrong password and an out-of-range router have not been tried on the 5625U. `WiFiView` no longer keeps the old inline password fields. Settings and the kit are rebuilt together.

## 2026-11-18 - Wi-Fi: do not turn management protection on for a passphrase

Problem: every WPA2 network was told that management protection is optional. A router that offers it would then install an integrity key. That path has not been booted, and it would replace the WPA2 join that already works. A block-ack stop ran without a reference to the station and without the receive lock. An integrity key and SAE frames stayed after a roam or a return to scan, so a later open network still ignored deauthentication. A router advertisement with lifetime 0 compared the gateway's scope with the raw address, so the route was never the one removed, and a new advertisement still removed every IPv6 default route. The firmware starts before the stack is attached, and a frame could be handed to the stack in between.

Changed: a passphrase no longer sets `ieee80211w`. SAE and OWE still require it. The package is `wpa_supplicant-2.11.r5.5-1`. The block-ack stop holds a reference taken under the com lock, and it runs while the receive lock is held. No DELBA is sent when the firmware has already refused the session. The integrity key and the SAE frames are dropped when authentication starts, and when the station leaves RUN or ASSOC for anything other than sleep or RUN. The IPv6 route is matched after the link-local scope is filled in, and only that route is removed. Frames are not given to the stack until `ieee80211_ifattach` has returned.

Limits: not jammed and not booted. A protected deauth is still discarded. The station record was not widened. The detach order was not changed. Fast transition stays off.

Tested: the host fuzz targets ies, rsn, action, mtkrx and he each ran 60 seconds, with no crash. The supplicant package for this change is `wpa_supplicant-2.11.r5.5-1`. The cross compiler rebuilt it. `ieee80211w` is set only for SAE and OWE.

## 2026-11-18 - Wi-Fi: the rest of the review that could be checked

Problem: several notes from the same review were still open. An integrity key had no packet number. A beacon's HE element could claim PPE thresholds this builder does not send. A router advertisement with lifetime 0 removed every IPv6 default route. An old DNS domain was written back when the new configuration had none. The link rate stayed on screen after the station left. A device named `10` matched a notification for `0`. "Wi-Fi 6E" sorted above "Wi-Fi 7" because the comparison used the last character.

Changed: the integrity key's packet number goes in a new request, `IEEE80211_IOC_HAIKU_IGTK_PN` (`0x6008`). The older request is unchanged. The driver stores the number. The firmware record copied from mt76 has no field for it, so it is not sent there. The outgoing HE capability clears PPE Thresholds Present. A lifetime of 0 removes only the default route whose gateway is that router. A resolver update writes a domain only when the new configuration has one. The reported link is empty unless the station is up, and the last rate is cleared when the join ends. A notification matches the device name only at a `/` boundary. Router standards are ordered by name. WPA2 may use protected management frames. SAE sets `sae_pwe=2`. The package is `wpa_supplicant-2.11.r5.4-1`. The fuzz harness allocates the buffer it claims to pass, and also calls the HE update. The wpa_supplicant license text is in `LICENSES/`.

Limits: not jammed and not booted. A protected deauth is still discarded. The station record sent to the firmware was not widened: a wrong record would break the join that already works, and that needs a stick. Fast transition stays off. The detach order was not reordered: the panic path needs a backtrace from the stick. The password is still written when the join is requested from the Deskbar; saving it after a successful join is the password window. `IEEE80211_MODE_MAX` is 16 (HE added modes 14 and 15). The roam and transmit parameter arrays are that long. `MODE_BYTES` stays 2.

## 2026-11-18 - Wi-Fi: the SAE queue, who may set a key, and an unprotected deauth

Problem: one SAE slot served every station. A second frame replaced the first before it was read, and a read left the frame in place. Any local program could set keys, send SAE, or change the authentication mode. After an integrity key was installed, an unprotected deauth or disassociation still dropped the join. An SAE frame was also sent without the management rate.

Changed: each station keeps four SAE frames under the com lock. A read removes the oldest, and the queue is cleared when the station returns to INIT. A frame is stored only while authenticating, and only from the BSS address. A program that is not root may start a scan. Setting a key, SAE, the authentication mode, or the MAC address requires root. Once an integrity key is installed, an unprotected deauth or disassociation is ignored. An SAE frame uses the management rate, the retry count, and the node power.

Limits: not jammed and not booted. A protected deauth is still discarded. This stack does not check the BIP MIC, so a real protected deauth does not disconnect the station. The signed-in person still scans. The network server runs as root and still joins.

## 2026-11-18 - Wi-Fi: a beacon does not turn HE on

Problem: parsing an HE capability set the HE flag on the station even when `he` is off. The association reply did the same.

Changed: the capability is still stored. The flag is set only when this image is sending HE.

Limits: not jammed and not booted. `he` stays off. The 160 MHz bit in that element is bit 3 (`0x08`), the same value the published HE PHY map uses. It was left as it is.

## 2026-11-18 - Wi-Fi: one password window, and a saved WPA3 network stays WPA3

Problem: a failed join opened Haiku's password window as well as R5's, and the message said the password was wrong when the join had simply not finished. A WPA3 or OWE network had no name in the Wi-Fi Kit, so the join was sent without an authentication mode and the supplicant asked again. A remembered WPA3 network was read back as an open network.

Changed: the supplicant leaves the network on failure and does not open a dialog. The package is `wpa_supplicant-2.11.r5.3-1`. The Wi-Fi Kit labels WPA3, the hidden-network menu offers WPA3 Personal, and a join carries the scanned key mode. Settings read and write `wpa3` and `owe`.

Limits: not jammed and not booted. OWE still has no entry in the hidden-network menu.

## 2026-11-18 - Wi-Fi: the HT join no longer waits for a block-ack that does not exist

Problem: with `ht_vht` on, a WPA2 join on the 5625U never finishes and the supplicant then says the password is wrong. The password is right. The review of that path found the station record and the transmit description disagree with what the radio actually does.
- Unicast data left block-ack enabled. There is no transmit block-ack session, so the handshake frame can be held. EAPOL was also sent at a rate the firmware chose.
- The HT capability was built with no streams, so the MCS set was empty.
- The stack was attached before the card reported HT and VHT, so the channel list stayed 11a/b/g and was then rebuilt underneath the attached stack.
- 40 MHz was advertised while the radio stayed at 20 MHz.
- VHT was filled in but `IEEE80211_FEXT_VHT` was never set, so the stack sent no VHT element.
- A block-ack start used the raw sequence word, and a window of 0 was accepted.
- The integrity key was installed on the access point's entry together with the pairwise key, which can make the firmware encrypt frames the stack already encrypted. HE MCS 10 and 11 were dropped from the reported transmit rate.

Changed: every transmit description disables block-ack, and an EAPOL frame uses the fixed basic rate. The stream count is taken from the card, at least one and at most four, before the stack is attached. The firmware is started and the channel list is built before `ieee80211_ifattach`. The advertisement stays at 20 MHz, matching the radio. VHT is marked on when the card reports it. A block-ack session uses the stack's start and window, and is dropped if the firmware refuses it. The integrity key is only the BIP key, on our own entry. The pairwise key and the integrity key are cleared when the join is torn down. HE rates through MCS 11 are reported.

Limits: not jammed and not booted. `ht_vht` and `he` stay off until the settings say otherwise. The pass is still a stick with `ht_vht true` that joins the WPA2 network.

## 2026-11-18 - Wi-Fi: an integrity key, a block-ack reply, and a mixed network

Problem: a review of the merged Wi-Fi and WPA3 changes found three places a join stops.
- The supplicant sent an IGTK with `IEEE80211_IOC_HAIKU_IGTK`. That number has no setter in `ieee80211_ioctl_set80211`, so the call returns EINVAL. The kernel already accepts the same key as a WPA key of type BIP-CMAC-128, index 4 or 5.
- Opening or closing a block-ack session runs on the receive path, which holds `sc_rxmtx`. Telling the firmware waits for a reply and takes that lock again. The lock is not recursive, so the thread hangs. With `ht_vht` on, an access point that opens block-ack hits this.
- A network that offers both a passphrase and SAE was asked for as WPA3 only. A transition-mode router that joined as WPA2 then took the SAE path.

Changed: the integrity key is sent as `IEEE80211_IOC_WPAKEY`. The package is `wpa_supplicant-2.11.r5.2-1`. A block-ack change is remembered under its own lock and sent from the driver's thread, which holds `sc_rxmtx` only while it waits for the reply. A network that also offers a passphrase stays WPA-PSK. SAE on its own is still WPA3. The key bytes are not written to the log.

Limits: not jammed and not booted. This does not by itself make an HT or VHT join succeed. `ht_vht` and `he` stay off. A stick on the 5625U is still the pass.

Tested: the cross compiler rebuilt the supplicant package. The driver was not compiled.

## 2026-11-18 - Wi-Fi: the supplicant can ask for WPA3

Problem: the image installed HaikuPorts `wpa_supplicant-2.11.haiku.1-1`. That build has SAE, protected management frames and OWE off, so a WPA3 network was joined as a passphrase network. The kernel already carries SAE frames (algorithm 3) and an IGTK, and a second join while SAE is in progress sends the association.

Changed: `distro/wpa_supplicant` fetches the same Haiku source (commit `bdf3144`, sha256 `e7e36b4ed5d6ff0e6a233b427b312c6984ab01d79e219d0d4b9c9e2ccf86f8ab`) and builds it with SAE, PMF and OWE. The Haiku driver glue sends an SAE frame through `IEEE80211_IOC_HAIKU_SAE_AUTH`, polls the reply without writing it to the log, and on success joins a second time so the kernel sends the association. The external-auth event carries the RSN selector 00-0F-AC:8. A frame already stored from an earlier attempt is marked seen and is not delivered again. An IGTK (BIP-CMAC-128, index 4 or 5) uses `IEEE80211_IOC_HAIKU_IGTK`. `IEEE80211_IOC_AUTHMODE` accepts SAE (6), which has no net80211 authenticator, so the first join does not send an open authentication frame. A WPA3 network is configured with `key_mgmt=SAE`, `sae_password` and required management-frame protection, not a PSK. An OWE-only network uses `key_mgmt=OWE` and is not joined as an open network while the supplicant is still starting. The scan reports SAE, including a network that also offers a passphrase, as WPA3. `scripts/pin-mesa-packages.sh` selects `wpa_supplicant-2.11.r5.1-1` instead of the HaikuPorts package. WPA2 still uses the existing associate path: the driver does not advertise the SME flag.

Limits: not jammed and not booted. QEMU has no Wi-Fi, so this was not joined. The pass is still a stick on the 5625U that joins a WPA3-only network and a WPA2 network on the same boot, with `sae: authenticated` and `pmf: on` in the syslog. FT-SAE is reported as SAE and fast transition is not implemented. The cross build has no `rc`, so the binary has no resource icon and no catalogs. A second identical SAE frame is not delivered again. The password is not written to the log.

Tested: the cross compiler `x86_64-unknown-haiku-gcc` 13.3.0 produced a Haiku ELF and `distro/packages/x86_64/wpa_supplicant-2.11.r5.1-1-x86_64.hpkg` (1.1 MB). The binary contains the SAE handshake and the IGTK rejection text. No image was built.

## 2026-11-17 - Wi-Fi: a short beacon element and a second adapter

Problem: a review of the whole Wi-Fi stack, read without the design notes, found places a frame, a firmware file, or a second adapter was handled wrongly.

Changed:
- A vendor HT information element is stored as the information element. It was stored as the capabilities element, and the length check then dropped HT.
- A Quiet element shorter than 6 bytes is ignored, and the quiet callback is not called when the driver never set it. A TIM shorter than 4 bytes is ignored. Both used to be read past the element.
- A patch section or a firmware region whose offset and length leave the file is refused. The old check added up the lengths and did not look at the offset.
- The station and network records set HT, VHT, and HE only when the card's own report says so, the same gate as the channel list. VHT in the station record also requires 5 GHz, as the network record already did.
- The pairwise CCMP key kept for an integrity update is not replaced by the group key. The group key was sent back as pairwise key 0.
- The Deskbar list and Join target use the adapter that is associated. The password window floats, so it does not freeze the Deskbar. A network that joined but was not saved is reported as a failure. A scan notification for interface 1 no longer updates interface 10.

Limits: no jam and no boot. `ht_vht` and `he` still default to off.

## 2026-11-17 - Deskbar: the window watcher no longer calls a missing window

Problem: the thread that watches open windows called `Window()->LockWithTimeout()` on the team menu. While the Deskbar is closing, that menu is already detached, so `Window()` is NULL and the call crashed (Haiku #6846).

Changed: the watcher locks the window only when there is one.

Limits: a replicant wider than 129 px (#8166) already stops on an empty row instead of looping. #11510, #11511 and #14026 have no crash site in the current menu code. Not jammed and not booted.

## 2026-11-17 - Tracker: unmounting a volume no longer deadlocks the space bar

Problem: the pulse that redraws a volume space bar held its list lock and then locked the window. Unmount runs on that window and takes the same list lock from the pose destructor. Each side waits for the other (Haiku #12050).

Changed: adding and removing a pose take the list lock for the whole change. The pulse runs the space-bar callback under that lock, drops it, then locks the window and redraws only a pose that is still in the list.

Limits: #14473 (checkfs), #16427 (duplicate poses), #17198 (query window), #19318 (package removal) and #20020 (an unnamed freeze) are not this lock. They stay open. Not jammed and not booted.

## 2026-11-17 - MediaPlayer: a clip with no frame rate no longer divides by zero

Problem: turning a video frame into an audio frame divided by `fVideoFrameRate`. A clip that reports 0 crashed there. `StreamBase` is gone; this is the same conversion (Haiku #15293).

Changed: a video rate of 0 or less uses 25, the same stand-in `Controller` already uses. An audio rate of 0 or less returns frame 0.

Limits: a stream URL (#13078), Alt+V (#14483), an MPEG-4 file (#15791), the end of a streamed mp3 (#17406) and quit (#19680) are not this division. Not jammed and not booted.

## 2026-11-17 - app_server: the boot picture stays until the first screen is built (start splash)

Problem (owner, M13 stick 5 boot 3): on a first start on Lavapipe, Skia's warm-up compiles shaders for about 33 s (5625U) to 47 s (QEMU), and Sign in appeared in pieces. In QEMU the Try R5 window did the same (black rectangles, then its contents). Outside M13; Skia on by default (M13 step 5) is not part of this.

What changed (owner's choice: the boot picture continues, the icon row fades, the wave pebble pulses):
- `AccelerantHWInterface::SetMode()`: before the first mode switch it copies the screen as the boot loader left it, and right after the switch puts it back, so no black screen shows (VESA in QEMU clears the screen at that switch; the first QEMU build had a black pause, owner).
- `Compositor`: takes that copy (`TakeBootPicture()`), checks it is the boot picture (white corners, dark logo text) and cuts out the text, the pebble and the icon row, placed by the boot loader's own formulas (`video_splash.h`). While it holds, each frame draws them on white with the existing effects pipeline (`_RecordWindowAnimation()`, as an opening window): the icons fade in 0.3 s, then the pebble pulses (1.6 s, down to 55 %). No Skia pipeline, no new shader. The windows draw unseen.
- It ends when a window with a frame ended its last update (`Window::EndUpdate()` → `Compositor::WindowDrawn()`), Skia finished everything recorded up to that drawing (`SkiaGpu::AskTicket()` / `TicketDone()`, one ticket at a time), every window has pixels, and no window's pixels changed for 0.3 s (owner: only when the screen is fully built). At the latest after 60 s. Only at app_server's start; not after Log out; not in the CPU mode.
- The R5 artwork (GPL-3.0-or-later) is not compiled into app_server; only the pixels on the screen are used. Log lines: `start splash on`, `start splash off: <reason>`, `start splash <ms> ms (drawn|time)`. `docs/APP-SERVER-COMPOSITOR.md`, "The start splash".

Mistake on the way: the first build took the picture from the screen in `Compositor::Create()`; in QEMU the mode switch had already cleared it (`start splash off: no boot picture`). Now it is copied before that switch.

Tested in QEMU (KVM, 8 CPUs, 1280x800, Lavapipe forced, gpu_draw true, own image copy, window shown to the owner):
- First start: `start splash on`, `start splash 33658 ms (drawn)`, warm-up 46715 ms. The Try R5 window appeared whole. Owner: "yessss" after the black pause was fixed (that run's syslog lost its last lines at the power off).
- Second start on that image: `start splash 17037 ms (drawn)`, warm-up 16401 ms (the first start was powered off before its warm-up ended, so Mesa's cache was not full).
- Third start: `start splash 3104 ms (drawn)`, warm-up 3750 ms.

Limits: not booted on the 5625U and not on RADV (QEMU has no RADV); there radeon_hd keeps the boot scanout, so the copy should be the same picture. The splash ends after the warm-up's part that the first window waits for, so on Lavapipe a later start still shows it for about 3 s. In the CPU mode the boot copy (width × height × 4 bytes) stays in memory until app_server ends. ARM64 and RISC-V 64 not built.

## 2026-11-17 - Graphics (M13): VulkanTeapot on RADV no longer tears under a Lavapipe compositor (mesa_radv -10), and stick 5 on paper

Problem (owner, stick 4): VulkanTeapot tears when the compositor runs on Lavapipe and the program on RADV. Cause, from the Mesa 25.3.6 source: on R5 RADV has no separate blit queue (`radv_wsi_get_prime_blit_queue` is under `#ifndef VK_USE_PLATFORM_VIEW_RFIVE`), so `blit.queue` is NULL and the copy into the host-visible buffer runs in the program's own submit, which signals the per-image fence. Mesa's `wsi_common_queue_present` waits that fence only for a software device (`wsi->sw`). `r5_queue_present` then read the buffer before the GPU had finished writing it. The existing `QueueWaitIdle` branch (for a blit queue) never runs on R5; left as it is.

What changed:
- `distro/mesa/wsi/wsi_common_haiku.cpp`: with a buffer blit, no blit queue and a hardware device, `r5_queue_present` waits the image's fence (at most 1 s) before it copies the rows. A timeout returns `VK_ERROR_DEVICE_LOST`. Lavapipe (`wsi->sw`) and the shared (zero-copy) path are unchanged. From `generated/m13/wsi-blit-fence.patch`, checked against the 25.3.6 source (`blit.queue`, `wsi->sw`, `fences[]`, `WaitForFences`).
- `mesa_radv-25.3.6-10`: `pack-radv.py` takes `-9` as its base in both modes, writes `-10`; `check-pre-stick.py`, `install-mesa-packages.sh`, `pin-mesa-packages.sh` and `distro/gpu/COMPAT` name `-10`. The old-package filter of `install-mesa-packages.sh` matched `mesa_radv-25.3.6-1` as a prefix, which would also have matched `-10`; it now matches `mesa_radv-25.3.6-1-`, and `-9` is added. Lavapipe stays `-5` (the change does nothing on a software device).
- M13 stick tools: a boot 4 (boot 3's settings again, `gpu_m13_boot 4`: the second Lavapipe start, Mesa's shader cache) in `boot-next`, `check`, `m13-measure` and `check-pre-stick.py`. `check` on M13 boots now also runs `contract 1` (`GFX9 scratch 0xdeadbeef`, fence, K2), on boots 3 and 4 wants `gpu draw warm-up parts 8` and no `gpu draw warm up status`, and asks for the outline resize and VulkanTeapot tearing (boot 3), the warm up against boot 3 (boot 4) and the caps lock at the restart before (boots 2 to 4). `check-pre-stick.py` wants `gpu draw warm-up parts` in app_server and `tmr destroy` (#4363) in radeon_hd.

Not seen: the `shutdown rlc ..., tmr destroy ..., gart off` line is printed after the syslog's last write; stick 4's logs have no `shutdown gpu stop` line either. The stick shows #4363 by the caps lock and the next start's scratch, not by that line.

Built: `distro/mesa/build-radv.sh` under the build lock, exit 0; `check-libdrm-symbols.py`: `ok libdrm symbols`. `mesa_radv-25.3.6-10`: version `25.3.6-10`, `libvulkan_radeon.so` differs from `-9`'s, no `/home/nickr` in it, the tools have boot 4. The script also repacks `mesa_lavapipe-25.3.6-5` and `mesa-25.3.6-5`; those were put back to the committed files, so a package name keeps its contents.

Stick 5 written 17 Nov 21:38 at `56e06d7b7e`: `build-locked.sh` (`jam_exit=0`, `mesa_packages_ok`), `@r5-anyboot`, boot 1 planted, `check-pre-stick.py` on the image `pre-stick ok`, `write-stick.py` exit 0, stick sha256 `dd5c7931…` matches the image, all under one held build lock.

Stick 5 on the 5625U (17 Nov, logs `generated/boot-5625u-51` in the worktree; the folder number is per worktree): `check` PASS on all 4 boots. Every boot `GFX9 scratch 0xdeadbeef`, fence landed, `k2 ok gart`, no gpu hang or lost, no `gpu draw off`, no `MISMATCH`. Shutdown to the last heartbeat 3–4 s.
- Boot 1 (RADV): warm up 2332 ms, cache none; drag 16.8, resize 16.8 ms per frame (p95 19.6).
- Boot 2 (RADV, scanout): scanout through Vulkan, warm up 84 ms, cache loaded; resize 16.7 (p95 23.0).
- Boot 3 (Lavapipe, first start): `gpu draw warm-up parts 8, longest 9844 ms`, warm up 32722 ms, no `warm up status` (stick 4: 10448 ms and `Operation timed out`). Resize median 16.7 ms, p95 236.6 ms (not met); drag 16.7.
- Boot 4 (Lavapipe, second start): warm up 2046 ms, longest part 1386 ms: Mesa's shader cache works on the 5625U. Resize 16.7 ms, p95 18.1 (met).
- Owner, after the boots: VulkanTeapot no longer tears (boot 3: compositor on Lavapipe, VulkanTeapot on RADV). Mesa `-10` fixes it.
- Owner, caps lock (#4363 **not** fixed): restart after boot 1 (RADV): caps lock blinks. After boot 2 (RADV): no blink, but the machine did not restart. After boot 3 (Lavapipe): blinks (stick 4: no blink after the Lavapipe boot). After boot 4: not watched, did not come back either. The next boot each time came up after a power cycle. So the shutdown stop of #4363 does not stop the blink, and a restart can now also hang. The OS side ends in 3–4 s (boot-next to the last heartbeat). The outline-resize question was not answered.
- Owner: GLTeapot (Zink) on RADV runs slower than on Lavapipe (llvmpipe). No numbers or boot recorded. Not caused by `-10` as far as the source shows: Zink on R5 presents through `r5_present_frame()` (`haiku-zink-present.patch`, a CPU copy), not through the Vulkan WSI that `-10` changes. Not looked into.

Limits: the boot 3 resize p95 (236.6 ms) is most likely Lavapipe compiling on its first start (boot 4 with the cache: 18.1); not proven. VulkanTeapot's frame rate on RADV with the fence wait was not measured. Scroll p95 stays 170–240 ms on every boot (not judged). The present now waits for the GPU on RADV without a shared image, so VulkanTeapot's frame rate on that path may drop; measure it on the stick. A frame over 1 s on that path ends the swapchain. ARM64 and RISC-V 64 not built.

Tested: `git apply --check` of the patch on `origin/main`; `sh -n` of `check`, `boot-next` and `m13-measure`; `py scripts/check-pre-stick.py --paper` at `331843fef0`: `m13 lines 9 have a source`, `m13 boot 1/2/3/4/done settings ok`, M11 and M12 walks unchanged, `pre-stick paper ok`.

## 2026-11-17 - Tracker: selection, columns and broken links do not crash on a stale pose

Problem: five Haiku Tracker crash tickets still followed from the R5 copies of PoseView and ContainerWindow: a NULL pose after a file vanished during a list-mode rubber-band (#11031), StopWatching on a Model that already left the pose list (#17837), BBitmap::Lock on a failed drag bitmap and an unbounded attribute column width (#9434), and GetFont on a menu whose looper was not locked (#14390). Replicant removal (#14880) lives in BShelf, which is not in distro/.

What changed: SelectPoses skips a missing pose; TryUpdatingBrokenLinks only watches models that still have a pose; MakeDragBitmap returns NULL if the bitmap did not initialise; attribute widths are clamped 20–600; attribute menus lock the looper before GetFont; ShowContextMenu takes the window lock.

Limits: BShelf / ActivityMonitor replicant (#14880) is unchanged. NavMenu.cpp is still Haiku's copy, so ModelMenuItem::Highlight can still hit the looper assert.

Tested: owner boot 17 Nov 2026 (Europe/Amsterdam); everything still works.

## 2026-11-17 - Bluetooth: the ASUS USB-BT600 dongle starts in R5 (Bluetooth plan 2, step B0)

Problem: the ASUS USB-BT600 (USB `0b05:1d70`, a Realtek RTL8761CU), the radio the owner bought for LE Audio and Auracast (`docs/BLUETOOTH-2.md`), needs a patch from the host. R5 had no Realtek loader, so `docs/BLUETOOTH.md` listed Realtek dongles as not working.

What changed:
- `h2generic`: a new Realtek loader, `h2realtek.cpp` with the parser `h2rtkfw.h` (R5, MIT), called in `device_open` right after `mediatek_setup()`. It only talks to USB vendor `0x0bda` or a USB ID in its table, and only patches when Read Local Version names Realtek. The firmware comes from the USB ID table (one entry: `0b05:1d70` → `rtl8761cu`, project id `0x33`) or else from the chip's report (FreeBSD's chip table). The file's project id is checked, the ROM version is read (`0xfc6d`), and the patch plus `_config.bin` are sent with `0xfc20` in 252-byte pieces. Written from FreeBSD `rtlbtfw` (BSD-2-Clause); its chip table and project ids are a marked BSD-2-Clause snippet. No Linux source read. A missing, wrong or refused firmware gives one `h2generic: realtek: ... It stays off.` line and that radio's open fails; MediaTek radios never get a Realtek command. After the patch the driver logs version, manufacturer, LMP features and the LE features with the ISO bits.
- `h2mediatek.cpp`: `h2_firmware_load()` (MIT snippet), so both loaders read firmware the same way.
- `bluetooth_server` (`LEManager.cpp`): one `hci<n> le features ...` line for every radio, with 2M, Coded, extended and periodic advertising, CIS central and peripheral, ISO broadcaster and synchronized receiver.
- Firmware: `rtl8761cu_fw.bin` and `rtl8761cu_config.bin` from linux-firmware (commit `35e542439e`), unmodified, in `distro/firmware/realtek`, installed by `scripts/apply-bluetooth.py` at `system/non-packaged/data/firmware/h2generic` in the regular image. Licence `LICENCE.rtlwifi_firmware.txt`: binary redistribution without modification, with the notice; no reverse engineering (R5 reads only the file headers). `THIRD_PARTY_NOTICES.md`, `docs/LICENSING.md`, `LICENSES/LICENCE.rtlwifi_firmware.txt`, `LICENSES/rtlbtfw-BSD-2-Clause.txt`.
- The question of the earlier research, a 252-byte patch in a 10 KB file: the file holds four patch entries (chip ids 0 to 3) that all point at the same 252 bytes at `0x30`; the rest up to the trailer is zeros. The patch's last four bytes are the file version, as `rtlbtfw` expects.
- `scripts/run-vbox-r5.ps1 -PassDongle`: a USB filter for `0b05:1d70` on the xHCI, removed when the switch is not given.
- `scripts/test-rtkfw.sh` (host test) and `scripts/fuzz-bt.sh rtkfw` (new fuzz target).

Tested:
- `scripts/test-rtkfw.sh`: ok (ASan, UBSan; the shipped files and hand-made damaged ones). `scripts/fuzz-bt.sh rtkfw 60`: 5.2 million files, 0 crashes.
- AMD64 build with `scripts/build-locked.sh`: ok, no warnings in the new files. QEMU on an own image copy with `SMAP=1 LOGIN=1` (`run-qemu-smoke.sh`): SMAP on, no panic, all Bluetooth tests exit 0 (QEMU has no radio).
- VirtualBox 7.2.22 with `-PassDongle`, the dongle in the owner's PC: first try R5 saw no radio, because Windows had bound its own BTHUSB driver to the dongle (code 31, failed) and VirtualBox never attached it (`VBox.log`: only the mouse). The second time the owner plugged the dongle in while R5 ran. Syslog: from ROM HCI `0x0e` revision `0x0e`, LMP subversion `0x8761`, ROM version 1; `rtl8761cu`, 263 bytes in 11 ms; after the patch HCI `0x0e` revision `0x7bf1`, LMP subversion `0x5762`, manufacturer `0x5d`; LMP features `0x877bfddbfeffffff`; LE features `0x0000008ef7fe77fd`: 2M yes, Coded no, extended and periodic advertising yes, CIS central and peripheral yes, ISO broadcaster and synchronized receiver yes. `bluetooth_server` set the radio up, the Bluetooth page showed it, and a scan heard 13 LE devices and one Classic device.

Limits and not tested:
- Not tested on the 5625U itself, and not with the MediaTek radio and the dongle at the same time (that needs a stick). The MediaTek path itself is unchanged.
- Pairing and links over the dongle were not tried. `bluetooth_server` logged `secure connections host failed (0xc)` on the dongle; not looked into in this change.
- RTBTCore (v2) files are refused for now. A chip that kept its patch through a warm restart is not detected; if it then refuses the download it stays off until it is unplugged.
- In VirtualBox the dongle must be plugged in after the VM started, as long as Windows binds its BTHUSB driver to it.

## 2026-11-17 - Wi-Fi: sticks 51 and 52 on the 5625U, stick commands, join logging and a shutdown panic

Sticks 51 and 52 (5625U, MT7961, WPA2 network "Skynet" on channel 40, FT-PSK/CCMP, owner's home).

- Stick 51: boot 1 (no `mt7922wifi` file) joined over 802.11a; `wifi-check` 5 PASS. Boots 2 and 3 (`ht_vht true`, then also `he true`): `ht/vht on, 2 streams, bandwidth step 2`, and on boot 3 `he on, 2 streams` (the card reports HE). Neither joined. Every join sent 16 block-ack-off commands that the firmware refused (`status 0x10003`), then `the firmware was not ready for AUTH: -2147483643` (EINVAL) with no line saying why. The page asked for the password again, and so did Haiku's own `net_server` dialog. The password was right.
- Stick 52 (boot 2 planted, with the changes below): no refused commands; the driver no longer stops the join (`join: channel 40, flags 0x40140, node flags 0xaf8c40`, six times); the join still failed, with the same password dialogs. Restarting with `wifi-next` panicked in KDL: `vm_page_fault` in `mtk_write` on thread `ic_taskq`, after `msi_free_vectors` (the driver's detach).

What changed:
- `wifi-next` and `wifi-check` (`distro/wifi/stick`, in `/boot/system/non-packaged/bin` of the regular image through `apply-wireless.py`): write the settings for the next boot and restart; PASS or FAIL per driver line, files in `~/wifi-stick`. `wifi-check` reads `syslog.1` too, and takes the boot from the settings file when there is no note. `read-stick-logs.py` copies `~/wifi-stick` and both `mt7922wifi` files. `scripts/plant-wifi-stick.sh` plants the boot 2 settings and a 4 MiB syslog into a stick image only.
- `mtk_ampdu_rx_stop()` tells the firmware only about a session that was running (`IEEE80211_AGGR_RUNNING`) on an added station.
- `mtk_join_prepare()` logs each stop (no node, no channel, card not owned) and the channel, its flags and the node flags of every join.
- Shutdown: `sc_detaching` is set first in `mtk_detach()`; `mtk_newstate()` and `mtk_parent()` then leave the card alone. `sc_gone` is set just before the register window is released, and `mtk_raw_read()`/`mtk_raw_write()` do nothing after it. The stack walks its vaps to INIT on `ic_taskq` during `ieee80211_ifdetach()`, and that work reached the card after its registers were gone.

Tested: `scripts/build-locked.sh` (`jam_exit=0`, `mesa_exit=0`, `if_mtk.o` built). QEMU (KVM, `-cpu host,+smap`, a copy of `r5-os.image`, 120 s): `enable SMAP`, boots to `net_server`, no panic. QEMU has no Wi-Fi adapter, so neither the join nor the detach path ran.

Not proven: the shutdown fix on the 5625U (no stick for it yet). Why the access point does not accept the join with HT/VHT on: the auth and association frames are only logged with `debug true`. The width is a guess (the driver's `bandwidth step 2` against the AP's 40 MHz on channel 40), not checked. The second password dialog (Haiku's `net_server` "Connect to a WiFi network") is separate UI work.

## 2026-11-17 - Game controllers: Switch Pro, Joy-Con and Xbox controllers rumble and move too (option C, phase 2)

Problem: phase 1 gave rumble, trigger effects and motion only to the PlayStation controllers and to HID PID force feedback. The owner asked for the Switch Pro Controller and the Xbox controllers over Bluetooth anyway ("zet het er sowieso in"), and the same day for Xbox One and Series controllers over USB (GIP), without having any of them. Haiku has no Xbox One or Series USB driver (only an Xbox 360 quirk in usb_hid); RenkuOS has none either (its kernel driver tree checked, no xbox, gip or 045e driver), so the GIP part is written new. A Switch Pro Controller over USB sent nothing before its handshake, and its own report descriptor does not describe its reports; a GIP controller is not HID at all; an Xbox controller with Low Energy firmware had no vendor and product ID on R5 (the PnP ID was not read).

What changed:
- `R5HIDReports.h` (hid_shared): the Switch subcommand and USB command reports, the simple report `0x3F` built from a full report `0x30`/`0x31` or a reply `0x21`, the Switch battery, the Bluetooth start sequence (rumble on, full report mode on the Pro Controller, player light 1, each after the reply, sent again after 40 reports without one, three times), the Xbox Bluetooth battery (report `0x04`), the GIP header decoder, start packets, acknowledgements and input packet to an R5 report, and the R5 report descriptors for the Switch Pro Controller over USB and for GIP.
- usb_hid: `QuirkyDevices.cpp` copied from Haiku with two R5 entries (the Switch Pro Controller `057e:2009`, and class `0xFF`/`0x47`/`0xD0` for GIP). `HIDDevice.cpp`: the Switch handshake `80 02`, `80 03`, `80 02`, `80 04` and the three subcommands when the device is added (each waits up to 100 ms for its answer); GIP power on, guide light, security and motor set-up, again after an announcement, acknowledgements from the transfer callback (asynchronous, freed in their own callback); each packet turned into the R5 report before the parser sees it (the raw one stays in the `R5_JOYSTICK_GET_REPORT` cache). A GIP controller gets `R5_JOYSTICK_FLAG_STANDARD_LAYOUT | R5_JOYSTICK_FLAG_GIP` in the new `flags` field at the end of `r5_joystick_info`.
- bluetooth_hid: starts a Switch Pro Controller or Joy-Con when its IDs come, builds `0x3F` from the Pro Controller's full reports when its descriptor has that report in the expected size, and keeps the Switch and Xbox battery for `R5_BTHID_GET_BATTERY` (the Bluetooth item in Deskbar) and `R5_JOYSTICK_GET_INFO`.
- bluetooth_server: reads the PnP ID of a Low Energy device's Device Information service and sends it with `R5_BTHID_SET_INFO` (`HIDHost::SetLEDeviceInfo()`).
- libr5game: HD rumble for the Switch controllers (output `0x10`, resent every 50 ms while it runs), motion on the Pro Controller (subcommand `0x40`, the factory IMU calibration from SPI flash `0x6020`, else the nominal scale; the user calibration is not read), Xbox rumble over Bluetooth (`0x03`) and GIP (`0x09`), and the new calls `HasTriggerRumble()` and `SetTriggerRumble()` at the end of `BGameController` for the trigger motors of Microsoft's Xbox controllers. A GIP controller uses the fixed Xbox layout instead of the database.
- Settings, Game controllers: a "Triggers" button in the Rumble group, only for a controller with trigger motors. Trigger feedback stays DualSense only.
- Sources: SDL's `SDL_hidapi_switch.c` and `SDL_hidapi_xboxone.c` at commit `4cc7adcaee482b63b4e1a0de77f52e200aa87b90` (zlib, facts only, no code or table copied) and Microsoft's GIP description; no GPL driver read. `docs/GAME-CONTROLLERS.md` section 9, `docs/LIBR5.md`, `docs/SETTINGS.md`, R5Book `game-kit.html`, `docs/LICENSING.md`, `THIRD_PARTY_NOTICES.md`.

Tested: `scripts/test-game-feel.sh` (host, ASan and UBSan) passes with the new `feel_phase2_test.h`: exact bytes of the Switch subcommands, USB commands, simple report, rumble and SPI read, the SPI reply, IMU calibration and motion scaling, the start sequence with replies, resends and giving up, the Xbox battery and rumble reports, the GIP header, start packets, acknowledgement and input report (guide and Share), and the sizes of both R5 descriptors. `scripts/fuzz-bt.sh hid 60`: 16.7 million rounds, 0 crashes, with the new kernel and library parsers. AMD64 built with `scripts/build-locked.sh` (jam exit 0). QEMU (`run-qemu-smoke.sh`, SMAP=1 LOGIN=1 GAMEPAD=1, own image copy): all ten tests exit 0, no panic; `gamecontroller_feel_test` now also checks a virtual Switch Pro Controller on bluetooth_hid (the driver's start subcommands, B, the stick and the battery from full reports only, the HD rumble report, motion at the nominal scale) and a virtual Xbox Wireless Controller (trigger rumble and stop reports, the battery of report `0x04`).

Limits and not tested: no Switch Pro Controller, Joy-Con or Xbox controller was available, so nothing of this ran on a real controller; the Switch USB handshake and start order, the GIP start and acknowledgements, the motion axes and calibration, and the Xbox Bluetooth and Low Energy rumble reports are the parts most likely to need a fix. The Switch Pro Controller over USB and GIP did not run at all (QEMU has no USB game controller here). The Bluetooth descriptor of the virtual Switch Pro Controller is R5's assumption of the real one. Joy-Cons get no motion and no battery (only in the full report mode, which changes their buttons); not done: Joy-Cons in the charging grip or as a pair, Switch 2 controllers, the Switch user and stick calibration, Elite paddles, Xbox 360 rumble, GIP accessories, the Xbox battery over USB. ARM64 and RISC-V 64 not built.

## 2026-11-17 - Bluetooth: Classic devices answer the scan again, and HFP starts before a headset gives up

Problem (stick 54, build 10065 with #4398, HP ENVY x360 with the Ryzen 5 5625U and the MediaTek MT7961 over h2generic): every scan ended with `inquiry heard 0 classic devices` (Wi-Fi off and on), while the LE scan in the same 13 s heard 31 to 37 devices. Classic-only devices in pairing mode (the 8BitDo M30 in Classic mode, a DualSense) were never found. The WH-1000XM3 (found by its dual-mode LE advert, paired over Classic) went: connection up, SSP pairing, authentication and encryption ok, `sdp: 0 records:`, `sdp: hands-free search, 1 records: 0x111e`, `hands-free service on RFCOMM channel 10`, and 2 s later `link down (reason 0x13, the device ended it)`.

Cause, Classic scan (found in the code, not yet proven on the radio): since the stick-51 fix R5 writes Write Inquiry Mode 2 (Extended Inquiry Result) on a controller that has EIR, but its Set Event Mask never had bit 46, Extended Inquiry Result (event 0x2F; Core 5.4, Vol 4, Part E, 7.3.1: bit n is event code n + 1). The default after a reset is bits 0 to 44. The logged mask `0x3cbf9fffffffffff` has 0x9f in bits 40 to 47, so bit 46 is off. In extended mode a device with EIR data (every recent one) answers only with event 0x2F, and the controller does not send a masked event. Inquiry Complete still came, so every scan ran its full length and heard nothing. A device without EIR answers with event 0x22 (bit 33 is in the default mask); that is probably why the M30 was found on stick 52. The brief's guess, that the inquiry and the extended LE scan running at the same time starve each other on the MT7961, is not needed for this and was not changed: the two still run together.

Cause, headset drop (most likely, not proven): nothing opened a profile within the headset's wait. HFP waited 5 s after SDP (`HFPGateway` connectDelay, so A2DP and HID would go first), but A2DP only connects when sound plays (`BT_MSG_R5_AUDIO_CONNECT`), and the XM3 left after 2 s. The general SDP browse with 0 records is legal (a record without a BrowseGroupList is not in the public browse group), and the hands-free search found the record anyway.

What changed:
- `bluetooth_server`: the event mask gets the answer events of the inquiry mode it writes (`scan_inquiry_mode()` and `scan_inquiry_event_bits()` in `scan_log.h`): 0x02 always, 0x22 in modes 1 and 2, 0x2F in mode 2. The log line becomes `event mask 0x3cbfdfffffffffff` on the MT7961.
- New syslog lines: `inquiry command status 0x0`, `inquiry complete, status 0x0`, and the scan's last line counts every raw inquiry event from the radio before any check or parse: `...; events from the radio: N standard, M with rssi, K extended)`.
- HFP starts 0.5 s after SDP instead of 5 s (retries unchanged: twice, 10 s apart, only while the link is up).
- SDP: when the public browse brings no records, a Service Search Attribute Request for L2CAP (0x0100, all attributes) follows, which every profile on L2CAP names in its protocol list; logged as `sdp: l2cap search, N records: ...`.

Not fixed, reported: R5's SDP server has no Hands-Free Audio Gateway record (0x111F) and no RFCOMM server channel for it (only A2DP source and AVRCP, `SDPRecords.h`); HFP 1.8 asks the AG to have one. A headset that checks it before it takes the SLC would still leave. Adding it is a profile change and was not done here.

Limits: hardware untested. QEMU has no Bluetooth radio, so the mask, the Classic answers, the earlier HFP start and the L2CAP search were not seen on a radio. The next stick shows it: `inquiry heard N classic devices` with N > 0 and `K extended` > 0, and for the XM3 an `hfp: service level connection up` line before any `link down`. ARM64 and RISC-V 64 not built or tested.

## 2026-11-17 - Wi-Fi: the page says what the link uses, and the router's standard apart

Problem (owner, 5625U with the MT7921, a Wi-Fi 6 router, no `mt7922wifi` settings file): the Wi-Fi page and Deskbar said "Wi-Fi 6", but a download measured about 16 Mbit/s. Without the settings file `ht_vht` and `he` are off, so the link runs 802.11a/g (at most 54 Mbit/s). net80211 sets `IEEE80211_NODE_HE` on the access point's node from its beacon (`ieee80211_he_update()` in `ieee80211_node.c`), also when the driver keeps HE off, and `mtk_ioctl()` (`IEEE80211_IOC_HAIKU_LINK`) reported the node flags as the link's standard.

What changed:
- `if_mtk.c`: the link's standard counts a node's HE, VHT or HT flag only when `mtk_he_on()` or `mtk_ht_on()` says that mode is on; otherwise the rate the firmware reported decides, as before. A legacy link reports no standard, so the page and Deskbar show only the rate.
- New request `IEEE80211_IOC_HAIKU_ROUTER` (`0x6007`, `struct ieee80211_haiku_router`) in R5's `ieee80211_ioctl.h`, answered by net80211 itself from the access point's captured HE, VHT and HT capability elements. It is a new request, so the shipped `IEEE80211_IOC_HAIKU_LINK` struct stays as it is.
- Wi-Fi Kit: `WiFiLinkDetails::routerStandard`, added at the end, message field `"router standard"`. `WiFiView` puts "Router: Wi-Fi 6" in the tool tip of the current network, and "Your router supports Wi-Fi 6." when the link uses less. The status line and the Deskbar line show only what the link uses.

Tested: `scripts/build-locked.sh` (`jam_exit=0`, `mesa_exit=0`; `if_mtk.o`, `ieee80211_ioctl.o`, `WiFiController.o` and `WiFiView.o` compiled). QEMU (KVM, `-cpu host,+smap`, a copy of the new `r5-os.image`, 120 s, headless): the serial log has `enable SMAP` and reaches `R5 net_server: NetServer from distro`, no panic and no `vm_page_fault`. QEMU has no Wi-Fi adapter, so the new request and the tool tip did not run.

Limits: not tried on hardware. VirtualBox and QEMU have no Wi-Fi adapter; the next stick with the Wi-Fi boots (W-Boot1 to 3) shows it. The hint compares the last digit of the two strings. WPA3 still needs a `wpa_supplicant` with SAE; this change does not touch it.

## 2026-11-17 - Bluetooth: a syslog line per device a scan hears

Problem (stick 53, build 10065, 5625U with the MediaTek MT7961): in Settings > Bluetooth the first scan found nothing and the second only a few devices without a name; the Sony WH-1000XM3 and the 8BitDo M30, both in pairing mode, were not found. On stick 52 the same morning both were found and paired. The syslog of both sticks has the same server start and a full "le scan on/off" (13 s) per scan, but no line per device, so it could not tell whether the radio heard nothing or the page did not show it.

Cause: not found in the code. The discovery path is unchanged between the stick-52 source (`b0996c4ce2`, #4346) and build 10065: `git diff b0996c4ce2 9afc112f9d` shows no change in the inquiry and LE advertising code of `bluetooth_server`, in `BluetoothController.cpp`, in libbluetooth (`distro/bluetooth/kit`) or in the kernel Bluetooth drivers (h2generic, HCI, L2CAP). Commit `9f35689d68` (the dual-mode headset over Classic) changes only `LEManager::Pair()` and the name in `LocalDeviceImpl::CreateConnection()`, both run only after Pair is pressed, never during a scan, and the 200-wide minimum of the My devices list, which is not the Devices nearby list. #4360 (bluetooth_hid battery and output reports) reads the battery only for a connected Classic device; none was connected. #4388's clipping change in app_server (`View.cpp`) only removes area outside a view's visible bounds, and Settings showed some devices in the list. The one difference in the two syslogs is Wi-Fi: on stick 53 the MT7961's Wi-Fi side (same chip and antenna as the Bluetooth side) scanned and joined a network at 17:59, between the Bluetooth scans; on stick 52 it stayed down. Coexistence of the two is a guess, not proven.

What changed: `bluetooth_server` writes one line per device per scan (`scan_log.h`), for a Classic inquiry answer (`scan heard classic xx:xx:xx:xx:B1:B0 "name" rssi N class 0x...: sent to the list`) and an LE advertisement (`scan heard le xx:xx:xx:xx:B1:B0 (public, dual-mode) "name" rssi N flags 0x.. class 0x...: sent to the list` or `not sent: not connectable / no name yet / no list waits / the list did not take it`), at most two lines per device per scan (first heard, first sent) and at most 48 devices per scan. A Classic answer while no scan waits is logged once per scan. A name read after the scan gets `name of classic ...: sent to the list`. Each scan ends with `inquiry heard N classic devices, M sent to the list, K names to ask` and `le scan heard N devices, M sent to the list`. The address shows only its last two bytes; no keys. The dual-mode headset fix of `9f35689d68` stays.

Limits: no fix of the discovery itself, because no cause was found in the code; the next stick shows what the radio hears. Hardware untested: QEMU has no Bluetooth radio, so the new lines were not seen in a real scan. A device name (for example a phone's) now appears in the syslog during a scan.

Tested: `scripts/fuzz-bt.sh le` 60 s, 0 crashes (also checks the scan lines: dedup, the 48-device limit, the masked address, names cut on a UTF-8 boundary and without control characters). Built with `scripts/build-locked.sh` (jam exit 0). QEMU (KVM, SMAP=1, LOGIN=1, own image copy) with `run-qemu-smoke.sh SETTINGS_PAGE=bluetooth`: the 10 Bluetooth and game controller tests exit 0, no panic or SMAP line, Settings still runs on the Bluetooth page, and its screendump shows My devices and Devices nearby laid out (no radio, so no scan).

## 2026-11-17 - Build 10065: What's new for game controllers

What changed: `distro/BUILD` 10064 -> 10065 and a What's new section for the Game controllers page (Bluetooth steps 7a-8, #4356 pictures by model, #4360 rumble, trigger effects, motion, HID PID, layout A, #4388 the clipping fix, #4381 the RFCOMM test race). Headset voice stays opt-in and is not mentioned.

Limits: rumble, trigger effects and motion were only tested in QEMU (no real controller); this build is the stick for that test.

## 2026-11-17 - Network: AirPlay mDNS features match /info

Problem: `_airplay._tcp` advertised `features=0x5A7FFFF7,0x1E` while GET `/info` returned `0x4A7FFFF7`. Clients that trust the TXT then see a different set on the HTTP reply. The extra bit and `0x1E` are AirPlay 2; this receiver is AirPlay 1.

What changed: the TXT uses `features=0x4A7FFFF7,0x0`, the same 32-bit value as `/info`.

Limits: QEMU usernet is not a LAN. Pairing and AirPlay 2 are still not implemented. No AirPlay client on a LAN was named.

Tested: owner boot 17 Nov 2026 (Europe/Amsterdam); reported OK.

## 2026-11-17 - Media: USB layout menu follows a plugged-in card

Problem: `fHasUsbAudio` was only set in the Audio settings constructor. A USB audio device that appeared later left the channel-layout menu grey until the window was reopened.

What changed: after a device list refresh (`_InitMedia`), the view checks `/dev/audio/hmulti/usb` again and enables the menu when a card is there.

Limits: a USB-audio hotplug while Media prefs stays open was not named separately.

Tested: owner boot 17 Nov 2026 (Europe/Amsterdam); reported OK.

## 2026-11-17 - Media: UVC errors go to syslog, not a home log

Problem: every UVC error appended to `/boot/home/uvc.log` from uid 0. The file grew without bound and sat in the administrator home.

What changed: `uvc_note` only writes syslog.

Limits: a camera was not named.

Tested: owner boot 17 Nov 2026 (Europe/Amsterdam); reported OK.

## 2026-11-17 - Media: HDA widget lookup stops at the last node

Problem: `hda_audio_group_get_widget` treated `widget_start + widget_count` as a valid node id. That index is one past the allocated array, so a look-up of the last codec node could read off the end.

What changed: the look-up uses an index and returns NULL when it is `>= widget_count`.

Limits: the last-node path was not traced in the syslog.

Tested: owner boot 17 Nov 2026 (Europe/Amsterdam); reported OK.

## 2026-11-17 - Bluetooth: the headset voice test passes every time

Problem: in one of four QEMU smoke runs `bt_rfcomm_test` failed "voice: +BCS 2 answered, mSBC link asked". The cause is a race in the test, not in the gateway. The test waited only for the voice callback (`voiceCalls`), then checked `bcsSeen` too. The gateway sends the OK to the headset's `AT+BCS=2` and then asks for the voice link. The test's headset thread sets `bcsSeen` only after it has read that OK, so the main thread could see the call before `bcsSeen` was set. In the same way `HFPGateway::VoiceOpen()` turns true or false only after the callback returns. The gateway order (OK, then the link, then the flag) is correct, and `VoiceOpen()` is only used by this test. It was not a timeout.

What changed: `bt_rfcomm_test` waits for the whole state (bcsSeen 2, the call, and `VoiceOpen()`; on close the second call and `VoiceOpen()` false) before it checks it. Same deadlines as before. No change to the server.

Tested: AMD64 `scripts/build-locked.sh`, `jam_exit=0`. One QEMU smoke run (SMAP and SMEP enabled, LOGIN=1, own image copy) with a temporary loop of 50 voice rounds that judged each round both ways: the old check passed 47 of 50 (all three failures with `bcsSeen` still 0 while the link was already open), the new check 50 of 50; the close check passed 50 of 50 both ways. `bt_rfcomm_test: 0 failed`, every test in the smoke run exit 0, no panic. The loop was removed again before the PR; the PR build was not booted separately (it only lacks the loop). No parser changed, so no fuzz run.

## 2026-11-17 - Game controllers: no stray bar in a small Settings window

Problem: at 910 by 560 a light bar, 3 px high, showed just above the controller strip on the Game controllers page, in the box colour (231) and exactly the width of the picture column. A whole-window redraw did not remove it.

Cause: app_server (Haiku `View::_ScreenClipping()`) clipped a view to the visible part of its parents only when that part was narrower or lower than the frame of the view's own clipping. The own clipping of a group view is the spacing between its children. At 910 by 560 the area above the strip scrolls, so the picture column is taller than what shows; the frame of its own clipping (the gaps between the test, the hint and the vendor line) was not larger than the visible part, the check was skipped, and the 6 px gap between the hint and the vendor line was painted below the scroll area each time the column drew. In an earlier build the same gap landed in the bottom row beside the lock. #4373 (the lock and views that are not `BControl`) only changes input and is not involved.

What changed:
- app_server: in R5's existing copy `distro/desktop/src/servers/app/View.cpp` (the one with the glass snippets, applied by `scripts/apply-desktop-native.py`), `_ScreenClipping()` now always intersects with the visible bounds (SPDX snippet, MIT). A first version of this PR added a second copy, `distro/app_server/View.cpp`, which would have overwritten the glass code depending on the apply order; it is removed. This applies to every window: a view inside a scrolled or smaller parent no longer paints its background outside that parent.
- Settings: `SettingsStorage.cpp` included `<MallocIO.h>`, which Haiku does not have (`BMallocIO` is in `<DataIO.h>`), so main did not build after the atomic-write change; it now includes `<DataIO.h>`.

Tested: AMD64 `scripts/build-locked.sh`, `jam_exit=0`; after the apply the tree's `src/servers/app/View.cpp` has the glass code (`fGlass`) and the new clipping once, the old size check zero times. QEMU (SMAP and SMEP enabled, LOGIN=1, own image copy): with a virtual DualSense at 910 by 560 no box-coloured row below the scroll area any more (checked pixel by pixel; the area still scrolls); at 1030 by 610 the page looks as before, with Trigger feedback and the vendor line visible and no scroll bar; the Display page at 1030 by 610 and the desktop background and window frames look as before. No panic in the serial logs.

Limits: Haiku upstream was not checked for the same fix. The Sign in window covers the middle of the Display screendump, so the screen picture was not seen; the Deskbar and other apps were not looked at with this change, and not on real hardware. ARM64 and RISC-V 64 were not built.

## 2026-11-17 - Graphics (M13): step 4 on Lavapipe, outline resize and a warm-up that finishes

Problem: on stick 4 (5625U, boot 3, Lavapipe) a resize took 35.1 ms per frame (stick 3: 30.9), and the Skia warm-up ended after 10448 ms with `Operation timed out`. Waiting for the first window run (step 4) did not help: stick 3 had the same 10160 ms. The cost is the warm-up's own work: after it, two runs waited 9.1 s each for the GPU (`deliver 9137298`), Lavapipe compiling its shaders, against a 5 s `Finish` limit, twice. The resize: compose (20.4 to 16.1 ms) and draw (34.7 to 24.2 ms) both got faster, the frame slower; most likely the fixed time grid of step 4 (a late slot composes at once, next to Skia on the two Lavapipe threads). Inferred, not measured.

What changed:
- Owner's decision (17 Nov): on Lavapipe a resize shows only the window's outline, and the window is drawn at its new size when the mouse is released. No setting; RADV and the CPU mode resize live as before.
  - Haiku's own outline resize (`B_OUTLINE_RESIZE`, `DefaultWindowBehaviour`) is used for every window when the compositor runs on Lavapipe (`Compositor::OutlineResize()`, asked once when a resize starts).
  - Haiku's outline only grew to the right and the bottom, and its border resize ended wrong for the left and top edges. The outline now moves for those edges (`Decorator::MoveOutlinesBy()`, `Window::fOutlinesShift`), and the end resizes per edge and moves by what the window really grew.
  - The compositor draws the outline over the window (`_RecordOutline()`: the shadow pass with a one-row mask of 255, no new shader). The window's buffer, shape and shadow stay its own, and nothing below redraws while the outline moves. The first version drew the outline into the window: every step made a new buffer and redrew the whole window (QEMU: draw 122 to 207 ms), and an outline to the right or bottom did not show.
  - New non-virtual `Decorator` calls (`MoveOutlinesBy`, `GetFootprintWithoutOutline`, `GetOutlineRegion`, `ResetOutlines`); the decorator add-on ABI and class layout are unchanged.
- The warm-up on Lavapipe is drawn in 8 parts, one `Finish` per drawing mode, each with a 30 s limit. A new line `gpu draw warm-up parts 8, longest N ms`.
- Mesa's shader cache (in `/boot/system/cache/app_server/mesa` since step 2) works for Lavapipe on R5. It was never measured before: the next start reuses what the first one compiled.
- `scripts/qemu-m13-measure.py`: `--again` starts the same image once more; ACPI power off instead of a kill, so the cache reaches the image; pictures during the resize and a left-edge drag. Two faults found and fixed: the resize grabbed 7 pixels inside the window (the scroll bar corner, no resize in QEMU at all), and the drag ended a third of a circle away, so the resize grabbed the desktop. QEMU resize numbers from before this change were no resize.

Two independent reviews of the diff, fixed before the commit: a title or look change during the drag shifted the next outline (`ResetOutlines` lays it out again at the end); a window hidden during the drag kept its outline; a call per mouse event without a change; `--again` without an image; a false `compose compare MISMATCH` with an outline (the compare is skipped then); a whole-window compose on every outline step (now only the old and the new outline).

Tested in QEMU only (Lavapipe 256-bit, 8 CPUs, `scripts/build-locked.sh` and `scripts/qemu-m13-measure.py`; built on `main` before #4388):
- Warm-up, first start: 31.9 to 47.2 s in QEMU, longest part 18.3 to 25.1 s, no timeout. Second start (`--again`, same image): 3.4 and 5.7 s, longest part 1.7 and 2.4 s. Drag on the second start: 15.9 to 19.0 ms per frame (first start 18.5 to 23.8).
- Resize with the outline (final build, first start): 19.8 ms per frame (p95 32.4), compose 18.8, draw 7.0. Pictures: the outline follows the bottom right corner and the left edge, and the window has its new size and place after the release. No `gpu draw off`, fence timeout or `MISMATCH`.

Limits: not on the 5625U; that needs the next stick (with #4363). The first start after an install or a Mesa update pays the whole warm-up (Lavapipe compiles everything once); in QEMU a part took up to 25 s of the 30 s limit. The outline is darkened pixels, hard to see on a dark background. The top edge was not dragged in QEMU (the left edge uses the same code). Windows with `B_OUTLINE_RESIZE` now get this outline on RADV too.

## 2026-11-17 - Network: mDNS browse keeps only the requested type

Problem: `browse_cb` took every PTR, SRV and TXT in the packet. A browse for `_ipp._tcp` could pick up AirPlay or other services from additional records.

What changed: PTR, SRV and TXT are ignored unless the name matches the requested type (with or without `.local`). A records are still matched by host.

Limits: QEMU usernet is not multicast. Needs a LAN.

Tested: owner boot 17 Nov 2026 (Europe/Amsterdam); reported OK. No LAN multicast test named.

## 2026-11-17 - Network: IPv6 mDNS sends on a real interface

Problem: `sendto` of ff02::fb used `sin6_scope_id` 0, so the packet often never left the host.

What changed: the send and the IPv6 join use the same interface as link-local AAAA results (`mdns_link_scope()`), and `IPV6_MULTICAST_IF` is set when that index is known.

Limits: several such interfaces pick the first. QEMU usernet is not multicast.

Tested: owner boot 17 Nov 2026 (Europe/Amsterdam); reported OK. No LAN multicast test named.

## 2026-11-17 - Network: link-local mDNS results carry a scope

Problem: AAAA answers from mDNS were returned with `sin6_scope_id` 0, so a link-local address could not be used.

What changed: a link-local result gets the first up, non-loopback interface that already has a link-local IPv6 address.

Limits: several such interfaces pick the first. QEMU usernet is not this test.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Network: mDNS joins the multicast group

Problem: `mdns_query` sent to 224.0.0.251 and ff02::fb but never joined those groups, so multicast replies could be missed.

What changed: the IPv4 socket joins 224.0.0.251, the IPv6 socket joins ff02::fb. A failed join still sends the query.

Limits: QEMU usernet is not multicast. Needs a LAN.

## 2026-11-17 - Network: a missed .local name is not sent to DNS

Problem: after mDNS missed a `.local` name, `getaddrinfo` fell through to unicast DNS, against RFC 6762.

What changed: a `.local` lookup returns the mDNS error (or `EAI_NONAME`) and does not query DNS.

Limits: QEMU usernet is not multicast. Needs a LAN.

## 2026-11-17 - Settings: a page file is replaced, not erased

Problem: `SettingsStorage::Write` erased the module file and then Flatten. A short write could leave an empty page file.

What changed: Flatten goes to memory, then a temp file, `fsync`, and rename. Same pattern as notifications.

Limits: no crash injected during write.

## 2026-11-17 - Login: the session log is not world-readable

Problem: `/tmp/r5-session.log` was created with `fopen("a")` and the umask. Session names were readable by every account.

What changed: both log paths are opened `0600` and `fchmod`ed to 0600.

Limits: a file that already exists with a looser mode is tightened on the next write. No boot test.

## 2026-11-17 - Network: DHCP keeps the resolv.conf domain line

Problem: net_server rewrote `resolv.conf` from remembered nameservers and only wrote `domain` when DHCP sent one. An existing domain line disappeared.

What changed: the previous `domain` line is kept when DHCP does not send a domain.

Limits: QEMU usernet is not this test. No packet capture.

## 2026-11-17 - Settings: a failed Deskbar apply rolls back

Problem: Desktop and Deskbar wrote each field as it succeeded. A later `SetLocation` failure left sort or icon size already changed.

What changed: a failed `_Apply` puts the previous committed choices back.

Limits: a restore that also fails leaves Deskbar where the first failure left it. No boot test.

## 2026-11-17 - Settings: the lock also stops the screen picture

Problem: a closed administrator lock greys `BControl` widgets. `BScreenArrangement` is a `BView`, so Display's screen picture stayed clickable.

What changed: `BScreenArrangement::SetEnabled(false)` ignores mouse and keys. Settings calls that on a locked admin page. Additive; existing binaries still run.

Limits: no boot test of the lock on Display.

## 2026-11-17 - Settings: Display menus stay put during keep-picture

Problem: during the 12-second keep-picture dialog the resolution menus still wrote `fSelected`. Keep then stored that later choice as the boot mode.

What changed: those menus are ignored and disabled until the dialog ends. Workspace rows and columns still apply at once.

Limits: no boot test of the 12-second dialog.

## 2026-11-17 - Login: leftover apps leave with the session

Problem: Logout sent `B_QUIT_REQUESTED` to user apps and did not wait. Tracker and Deskbar were waited on; Terminal and other session apps could stay up on the Sign in screen.

What changed: after that quit, Login waits three seconds, then `kill_team` on what is left, then one more second.

Limits: servers, daemons, and `B_BACKGROUND_APP` teams are still kept. No boot test of Log out.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Game controllers: fits at 1030 by 610, Motion gray without a controller

Problem: on the screendumps of the layout-A page (1) at 1030 by 610 with a controller the area above the strip still scrolled by about 15 px, hiding the last line of Trigger feedback and the vendor and product line; (2) without a controller the Motion bubble of the gray example stayed blue (the level, the bars and their labels drew as enabled); (3) at 910 by 560 a light bar showed in the bottom row next to the lock.

What changed:
- Appearance Kit (libr5ui): `BTiltLevel` and `BAxisBar` get `SetEnabled(bool)` and `IsEnabled()`, after the existing calls; `false` draws them gray, the bubble and the fill included. `Archive()` keeps a disabled state as `"r5:enabled"`. The tilt level's minimum is 4 line heights (was 5). Both classes are new in this PR, so nothing shipped changes.
- libr5game: `BGameControllerMotionView::SetEnabled()` and `IsEnabled()` pass it on and gray the Tilt, Turn, Pitch, Roll and Yaw labels. The Pitch, Roll and Yaw rows use half item spacing.
- Settings, Game controllers: Motion is disabled for the example without a controller. The test box is 6 instead of 8 at the top, the bottom and between the menus and the picture; the three groups are 2 apart instead of 5, and 4 instead of 6 at their bottom.
- Docs: `docs/LIBR5.md`, `docs/APPEARANCE-KIT.md`, R5Book (`appearance-kit.html`, `game-kit.html`).

Tested: AMD64 `scripts/build-locked.sh` twice, `jam_exit=0`. QEMU (SMAP and SMEP enabled, LOGIN=1, own image copy): with a virtual DualSense (GAMEPAD=1) at 1030 by 610 everything fits, no scroll bar, Trigger feedback and "Vendor 054C · Product 0CE6" visible (the first build still overflowed by about 3 px, hence the closer groups); without a controller (SIGNIN_AWAY=1) the Motion bubble, the bars and the labels are gray; at 910 by 560 the page lays out and scrolls as before. No panic, no SMAP or SMEP fault in the serial logs.

Fixed later (3): the light strip at 910 by 560 came from app_server clipping, not from this page; see "Game controllers: no stray bar in a small Settings window" above.

## 2026-11-17 - Game controllers: controllers in a strip at the bottom (layout A), and an example without a controller

Problem: the Game controllers page put the controllers in a column of cards at the left and Rumble, Motion and Trigger feedback under the picture, below the fold. The owner approved layout A on 17 Nov 2026 (`docs/design/gamecontrollers-mockup/strip-A-*.png`) and a page for when no controller is connected (`strip-empty-*.png`, without the line "Connect a controller to try this.").

What changed:
- libr5ui (Appearance Kit): `BCardStrip` and `BCardStripItem` (`<CardStrip.h>`, `<CardStripItem.h>`), a horizontal row of selectable cards with a picture, a title and a subtitle; mouse and arrow keys, Home and End, one selection, a message with `"index"`, sideways scrolling, and a dashed card with a text and a button while it is empty. Nothing in Haiku or libr5ui fitted: Haiku has no horizontal `BListView`, and `BCard` and `BActionCard` are single cards. `docs/LIBR5.md`, "Card strip"; `docs/APPEARANCE-KIT.md`; R5Book.
- libr5game: `BGameControllerView::PictureBitmap()`, added at the end: the still drawing of a model as a transparent `B_RGBA32` bitmap in a given line colour, for a card or a list row.
- Settings, Game controllers: the controllers are a `BCardStrip` at the bottom, full width, with a small picture of each model and "Bluetooth · 80 %". Above it one box: Layout, Button labels and Set up buttons… in one row, the picture at the left, the three groups at the right. The part above the strip scrolls by itself, so the strip stays; where the picture and the groups do not fit side by side, the groups go under the picture. Without a controller: a gray Xbox Series example (`PictureBitmap()`), the menus, buttons, sliders and the three groups disabled and gray, and the dashed card "No controller" with "Open Bluetooth…", which opens the Bluetooth page. The private `ControllerCard` and the Open Bluetooth link are gone. The label menu items are now "Xbox" and "PlayStation" (without the symbols), so the row fits at 910 by 560. "Pull L2 or R2 to feel it" moved onto the Effect line, so the three groups fit at 1030 by 610.
- `gamecontroller_feel_test` also checks `PictureBitmap()` (size, a line on a transparent background, no model, an empty size) and `BCardStrip` (adding, inserting, the order, `Select()`, the arrow keys, Home and End, the selection message once per change, removing around the selection, the archive, drawing with cards and empty).
- The drawing check of the earlier feel test (the entry below: it counted nearly every pixel as drawn) now compares with a tolerance of 8 per channel and counts the highlight colour; it passes (level 1452 drawn, 110 highlight of 10000; bar at -0.5 954/465, at 0 470/0 of 3200).

Tested: AMD64 `scripts/build-locked.sh` twice, `jam_exit=0`. QEMU (SMAP and SMEP enabled, LOGIN=1, own image copy), three runs on the first build and one on the second: no panic, no SMAP fault, Settings kept running. `gamecontroller_test: ok`; `gamecontroller_feel_test: ok` in all four runs, with "picture bitmap 432 line, 1141 clear of 2128" and "card strip ok". With the virtual DualSense (GAMEPAD=1) the screendump at 1030 by 610 shows the strip at the bottom with one chosen card, and Rumble, Motion and Trigger feedback beside the picture with all their controls visible; the box still scrolls by about 15 pixels (the bottom line of Trigger feedback and the vendor and product ID). Without a controller (SIGNIN_AWAY=1) the gray example, the disabled menus and groups, and the dashed card. At 910 by 560 (BT_WINDOW, on the first build): the groups stay beside the picture, the part above the strip scrolls, no horizontal overflow, the strip stays. In one of the four runs `bt_rfcomm_test` failed one check ("+BCS 2 answered, mSBC link asked"); the other three passed it, and this change does not touch Bluetooth.

Not tested: a real controller; more than one controller in the strip (QEMU has one virtual pad), so the sideways scrolling was only tested in the unit test by key, not seen; dark mode and font size 18; clicking "Open Bluetooth…" in QEMU (the Sign in window covers it in the screendump). The 910 by 560 run used the build before the spacing change. The Motion view's bubble stays blue in the gray example (it is drawn by `BGameControllerMotionView`). The 910 by 560 screendump shows a light bar in the bottom row beside the lock, outside the page; not looked into. ARM64 and RISC-V 64 were not built.

## 2026-11-17 - Game controllers: rumble, trigger effects and motion (option C, phase 1)

Problem: `docs/GAME-CONTROLLERS.md` left rumble, trigger effects and motion for later (option C). The owner approved phase 1 on 17 Nov 2026: DualSense, DualSense Edge and DualShock 4 over USB and Bluetooth, generic HID PID force feedback for rumble, shared widgets, and a Test box per the mockup (`docs/design/gamecontrollers-mockup/feedback-*.png`).

What changed:
- Driver: three additive calls in `<joystick_driver.h>`, answered by hid_shared for usb_hid and bluetooth_hid (i2c_hid and hyperv/hid answer `B_NOT_SUPPORTED`). `R5_JOYSTICK_SEND_REPORT` sends an output or feature report. `R5_JOYSTICK_GET_REPORT` returns the last input report per ID, or a feature report on USB. `R5_JOYSTICK_GET_DESCRIPTOR` returns the report descriptor. Only reports the descriptor declares are accepted, at most their declared length. User memory goes through `IS_USER_ADDRESS` on both ends and `user_memcpy`, sizes from the caller. bluetooth_hid's output queue entries grew from 64 to 512 bytes. Feature reports over Bluetooth are not carried yet.
- A DualShock 4 or DualSense over Bluetooth sends only its full report after its first output report. bluetooth_hid builds the short report from each full one, so `BJoystick` programs keep buttons and sticks (`R5HIDReports.h`).
- libr5game: `CanRumble()`, `SetRumble()`, `StopRumble()`, `HasTriggerEffects()`, `SetTriggerEffect()`, `HasMotion()`, `SetMotionEnabled()`, `IsMotionEnabled()`, `GetMotion()` at the end of `BGameController`. Report builders and the Bluetooth CRC-32 are in user space (`GameControllerReports.cpp`), written from SDL's hidapi drivers as a reference for layouts (zlib, facts only); HID PID from the USB PID 1.0 specification. No GPL code was read. A thread of the library stops a timed rumble. `Close()` stops the rumble and frees the triggers in one report. `BGameControllerMotionView` is new, and `BGameControllerView::SetTriggerMark()` was added at the end.
- libr5ui: `BTiltLevel` and `BAxisBar` (Appearance Kit). Nothing in Haiku or libr5ui fitted.
- Settings, Game controllers: the Test box has Rumble, Motion and Trigger feedback, each hidden without the feature, under the picture so the page fits 910 by 560 (it scrolls).
- Mistakes on the way: an enum comparison in a `static_assert` broke the first jam (`-Werror=enum-compare`). The first feel test lost its output reports to bluetooth_server, which waits on the same queue; the test now starts waiting before it sends.

Tested: `scripts/test-game-feel.sh` (host, ASan and UBSan): `feel_host_test: ok`. It checks the exact bytes of every report, the CRC against a second implementation, motion, the short report, the cache and the PID reports of a synthetic descriptor. `scripts/fuzz-bt.sh hid` 60 s: about 19.4 million rounds, 0 crashes. AMD64 `scripts/build-locked.sh`: `jam_exit=0`. QEMU (SMAP enabled, LOGIN=1, GAMEPAD=1, own image copy): no panic, no SMAP fault. `bt_hid_driver_test` and `gamecontroller_test` ok. `gamecontroller_feel_test` passed the driver calls (including the refused ones and kernel addresses), buttons and stick from full reports only, motion (0, 1, -0.5 g and 1, -2, 100 deg/s), and the exact rumble, trigger, stop, timed-stop and Close reports with their CRC in bluetooth_hid's queue. Its last check, the drawing of the two widgets, fails twice: it counts nearly every pixel as drawn (the level 8024 of 10000, the bar 2778 at -0.5 and at 0), so the check measures the bitmap background, not the widget. Settings showed the page with the virtual DualSense and the Rumble group in screendumps, and kept running. Motion and Trigger feedback are below the fold there and were not seen.

Not tested: any real controller. DualSense and DualShock 4 rumble, trigger effects and motion need the owner's DualSense on a stick. The click and vibration trigger parameters, the sensor axes' signs and the DualShock 4 switch to full reports without its calibration report are the likely fixes. No PID controller was tested. The usb_hid output, feature and cache paths did not run (no USB game controller in QEMU). The widget drawing check is open. ARM64 and RISC-V 64 were not built.

## 2026-11-17 - Network: an RA with lifetime 0 is not a default router

Problem: SLAAC still called `AddDefaultRoute` when the router advertisement had Router Lifetime 0. RFC 4861 forbids using that router as a default.

What changed: lifetime 0 now removes the IPv6 default route and does not add one. Prefixes in the same RA are still applied.

Limits: no packet capture. VirtualBox usernet is not this test.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Settings: Revert closes the display keep-picture dialog

Problem: window Revert during the 12-second keep-picture dialog called `Settle(false)` but left `DisplayView::fConfirming` set, so Apply stayed disabled and the alert stayed up.

What changed: `RevertSettings` asks the view to quit that alert, drop the timeout, and clear `fConfirming` before it restores the previous mode.

Limits: no boot test of the 12-second dialog.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Settings: the lock also blocks views that are not BControl

Problem: `BAdminLockBar::AcceptsInput` returned true for a view that was not a `BControl`. A screen picture stayed draggable while the machine lock was closed.

What changed: a locked page now refuses that input unless the view is the lock bar, a scrollbar, or `SetExempt`. `BControl` widgets were already blocked.

Limits: `_DisableTree` still only greys `BControl`. The filter is what stops the drag.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Login: r5-desktop stops if initgroups fails

Problem: `r5-desktop` logged an `initgroups` failure and still called `setresuid`. An administrator could run Tracker without group `admin` or `wheel`.

What changed: the helper exits with status 1 after that log, the same as a failed `setresgid`.

Limits: no boot test.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Login: r5-desktop reads only the system session file

Problem: `r5-desktop` and Login fell back to `/tmp/r5-desktop-session` when the system file was missing. Anyone who could write `/tmp` could pick the session account.

What changed: both read only `/boot/system/settings/r5-desktop-session`. `clear_desktop_session` still unlinks the old `/tmp` path so a leftover file is not a trap later.

Limits: no boot test.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Login: a failed desktop start clears the session file

Problem: Sign in wrote `r5-desktop-session` and then launched Tracker. If that launch failed, Sign in stayed on screen but the session file still named the account, so the next `r5-desktop` helper trusted it.

What changed: `TryLogin` calls `clear_desktop_session()` when the session file was written and the desktop did not start.

Limits: a helper that already started from the stale name is not killed.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Login: machine settings stay group-writable, not world-writable

Problem: for the five-minute administrator grant, Login set network, printers, repos and the swap file to 0777/0666 so any local uid could write them.

What changed: those paths are 0770/0660 owned by the granted team's uid and group `admin`. Without a uid they stay 0775/0664.

Limits: only the first live grant's uid is used. Haiku has no ACL here.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Login: halt, lock and sign-in only from this team

Problem: Login accepted logout, lock, unlock, halt, reboot and sign-in from any team that sent a `BMessenger` to the Login application. The admin port already checked the sender.

What changed: those actions return without running when `BMessage::IsSourceRemote()` is true. The Sign in window is the same team.

Limits: a local add-on in the Login team can still send them. Not proven on a running desktop.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Settings: notifications file is replaced, not erased

Problem: the Notifications page flattened the live `~/config/settings/system/notifications` file after `B_ERASE_FILE`. A failed Flatten left the notification server with an empty file.

What changed: the message is flattened into memory first, then written to a `.tmp` file, `fsync`ed, and renamed over the path.

Limits: the separate Notifications program still erases then writes. The server still watches that node.

Tested: the C++ was not compiled on this host (needs libbe).

## 2026-11-17 - Settings: virtual memory file is replaced, not erased

Problem: Settings wrote `/boot/system/settings/kernel/drivers/virtual_memory` with `B_ERASE_FILE` and then `Write`. A crash or a short write left an empty or truncated swap file. The kernel reads that file on the next start.

What changed: the page writes a `.tmp` file, `fsync`s it, and `rename`s it over the real path, the same way `r5_driver_settings_write` already does.

Limits: the separate VirtualMemory program still erases then writes. A change still waits for the next start.

Tested: the C++ was not compiled on this host (needs libbe). Jam on this branch is recorded on the pull request.

## 2026-11-17 - Graphics (M13): step 4, resize on RADV at the refresh rate, stick 4 results

Problem: on stick 3 a window resize took 25.8 to 30.9 ms per frame on RADV and Lavapipe. Every frame re-blurred the window shadow, Skia held its lock while it waited for the GPU, and each new window size made a new VkImage. `check` judged a frame count that also counted the pauses in the mouse motion.

What changed:
- Compositor: during a resize the shadow mask is stretched and only rebuilt after 100 ms of stable size; frames are on a fixed time grid.
- Skia: `RunWork` waits for the fence without `fLock`. On Lavapipe the warm-up runs after the first real drawing, and a failed `Finish` status is logged. Window VkImages grow in 64-pixel steps and are reused when the size fits and the origin is unchanged.
- Stick tools: frame median <= 1.02 x the refresh period and p95 <= 1.5 x the period for drag and resize. Scroll input is not judged. `m13-measure` writes to `~/m13/boot<k>/measure`. Boot 2 also gets `gpu_app_server true`. `mesa_radv-25.3.6-9` was repacked with those tools.
- After stick 4: the frame count is shown but no longer judged. The compositor makes no frame while the mouse pauses, so stick 4 had 487 to 565 frames in the window with a 16.7 ms median. The next stick needs a tools repack (`pack-radv.py --tools-only`) for this.

Tested on the 5625U, stick 4 (image `c7e3d042`, `pre-stick ok` at the stick branch `e3e11ed483`), logs `generated/boot-5625u-54`:
- Boot 1 (RADV): warm-up 2326 ms. Drag 16.8 ms (p95 17.4), resize 16.7 ms (p95 26.1), was 25.8 to 30.4.
- Boot 2 (RADV, `gpu_app_server`, `gpu_compositor_scanout`, `gfx9_wc_gtt`): warm-up 94 ms, `scanout through vulkan 1920x1080`. Resize 16.7 ms (p95 65.1).
- Boot 3 (Lavapipe, 128-bit): drag 16.7 ms (compose 6.3). Resize 35.1 ms (p95 213, compose 16.1, draw 24.2), was 30.9. Warm-up `after 200 ms (lavapipe)`, then 10448 ms with `warm up status Operation timed out`.
- Every boot: `check` PASS on no gpu lost and no gpu hang. No `gpu draw off`, fence timeout or `compose compare MISMATCH` in the logs.
- The caps lock blinked after the restarts of boots 1 and 2 (RADV), not after boot 3 (owner). The shutdown stop is a separate change.

Limits: Lavapipe resize is slower than on stick 3, and the Lavapipe warm-up still times out (the 5 s `Finish` while LLVM compiles). The owner wants only the window outline during a resize on Lavapipe; open question: Lavapipe only or a setting. VulkanTeapot still tears with the compositor on Lavapipe and the client on RADV (fix in `generated/m13/wsi-blit-fence.patch`, needs Mesa `-10`). QEMU `r5-draw-compare` 447/490 (was 448): `gradient-linear-roundrect/clear` "gpu failed", probably the same `Finish` timeout, not proven.

## 2026-11-17 - Graphics (M13): the GPU stop at shutdown also stops the RLC, destroys the PSP TMR and turns the GART off

Problem: after a restart from a RADV boot the 5625U's caps lock still blinked (3 long, 2 short, HP 3.2 memory), on sticks 3 and 4, but not after the Lavapipe boot. A comparison of Linux v6.12 `amdgpu_pci_shutdown` → `amdgpu_device_ip_suspend` for Renoir with `gfx9_shutdown_hook` found what the hook skipped. The suspect from the handover, `mp1_state` UNLOAD, sends nothing on Renoir: `renoir_ppt.c` has no `set_mp1_state` and no PrepareMp1ForUnload.

What changed (the hook, in Linux order):
- After the CP halt, the RLC halt of `gfx_v9_0_rlc_stop` (`RLC_CNTL` bit 0 off, the serdes wait from the start), `r5_gfx9_rlc_halt`.
- Before the PSP ring stop, DESTROY_TMR (command 0x7, `psp_suspend` → `psp_tmr_terminate`) on the GPCOM ring, `r5_gfx9_psp_tmr_destroy`; only when SETUP_TMR landed and the ring is still up. The start keeps the ring, command and fence buffers for it; the PSP load itself is unchanged. `r5_gfx9_kernel_stop` forgets them (`r5_gfx9_psp_forget`). The PSP fence number now goes up on every submit, also a timed-out one.
- After VM 0 is put back: `VM_CONTEXT1..15_CNTL` = 0, and after the VM 0 invalidate the GFXHUB L1 TLB, advanced driver model and L2 cache off (`gfxhub_v1_0_gart_disable`).
- A second serial line: `radeon_hd: shutdown rlc quiet|busy, tmr destroy ok|failed <hex>, gart off`.
- The contract in `docs/GRAPHICS.md` and the stop order in `AGENTS.md` (owner's go, 17 Nov).

An independent review found three faults in the first draft, fixed before the commit: VMIDs 1 to 15 were "restored" from a copy taken after R5's own VMID 1 setup (now 0, as Linux); stale PSP pointers after a kernel-stage stop; a reused fence number after a timeout.

Limits: the link between the TMR or the GART and the HP 3.2 code is inferred, not proven. The reset after a hang still sends no DESTROY_TMR. Not done: MMHUB GART off (not checked whether R5 programs MMHUB), clock-gating ungate.

Tested: `scripts/build-locked.sh` (raw image) only; not booted. Proof needs the next stick: a restart and a power off without the caps lock blinking, the new serial line, and the next start's `GFX9 scratch 0xdeadbeef`.

## 2026-11-17 - Graphics (M13): stick 3 on the 5625U, RADV stays on

Problem: sticks 1 and 2 of M13 lost the GPU drawing on RADV (an empty submit with waits, then too many waits in one CS). Before stick 3 an independent review of the whole M13 diff found four more faults, fixed in one round: window images were never freed after a failed RADV start; glass hidden under a window kept an old blur when it showed; without memory for a copy list a window part stayed old; and a compose relied on RADV's flush for Skia's earlier writes (now a barrier at the start of each compose). The semaphore wait that is a no-op in the R5 DRM library (point 0) is left: the barrier orders it on the one queue.

Tested on the 5625U, image `9d8e786f`, branch `claude/m13-stick` `a18507be3e`, logs `generated/boot-5625u-53`:
- Boots 1 and 2 (RADV): no `gpu draw off`, no fence timeout, no device lost; VulkanTeapot, vkcube and the WSI clear ok. Warm-up 2292 ms (pipeline cache none, then stored 531 KB), boot 2 136 ms (loaded). Drag 16.9 ms per frame (compose 1.6, draw 1.1), scroll 16.9 ms, resize 25.8 and 30.4 ms. The owner: scrolling felt fine, the rest felt like Haiku; fonts (Tracker, windows) could be better.
- Boot 3 (Lavapipe, 128-bit): drag 16.7 ms (compose 4.9), resize 30.9 ms (compose 20.4). Warm-up 10160 ms, as on the M12 stick (10163): two `Finish()` waits of 5 s while LLVM compiles; Login comes up late. VulkanTeapot (on RADV) tears, because zero-copy is off with the compositor on Lavapipe.
- Open: `check` marks the numbers FAIL at a 16.7 ms target against a 16.67 ms refresh (too strict); scroll p95 ~190 ms is the Terminal's bursts (felt smooth); `scanout through vulkan off: back buffer not on the gpu` on boot 2; the caps lock blinked once (3 long, 2 short, after boot 1's restart), not after boot 2's restart or boot 3's power off, with the GPU stop of #4355 in the image.

## 2026-11-17 - Wi-Fi: SAE length, channel lookup, short HT elements

Problem: a review of the station path found three mistakes that matter once SAE or `he` is used, and two on the menu. An SAE authentication frame was copied into an mbuf whose length stayed 0, so only the 24-byte header was sent. `IEEE80211_CHAN_HE` was part of the channel-match mask, so after the mark was set an HT lookup no longer found that channel. A short HT or VHT element was still cast to the full structure. The Deskbar join waited on Deskbar's own thread for the password. The rate line could name a different adapter than "Connected to".

What changed:
- The SAE body length is set on the mbuf before the header is prepended.
- `IEEE80211_CHAN_HE` stays a mark on the HT or VHT entry and is no longer part of `IEEE80211_CHAN_ALL`.
- A short HT or VHT element is dropped. The HT or VHT flag set just before the parse is cleared, and the rate setup is not run. A short standard HT element also clears a leftover vendor mark.
- Joining from the Deskbar menu asks for the password on its own thread.
- The rate line is taken from the associated adapter, the same one as the network name.

Limits: SAE still has no supplicant. HE is still off until `he` and `ht_vht` are both set. The password window is still modal for Deskbar's windows. `WiFiLinkDetails` grew at the end, which moves `networks` inside `WiFiState` for a program built against the previous header; the image builds both together, and the message fields are named.

Tested: not built and not booted in this pass.

## 2026-11-17 - Wi-Fi: key slot size, BSS mode, capability length

Problem: a review of the MT7921 station and network records found four mistakes. The integrity-key record told the firmware each key slot was 16 bytes, so a second key was read from the middle of the first. The network record stayed a legacy mode when `ht_vht` was on, and it could say Wi-Fi 6 when `he` was on without `ht_vht`. A capability reply whose length wrapped a 32-bit sum was treated as inside the buffer. A transmit-status mode above the HT or HE range was stored as a rate.

What changed:
- `cipher_len` is 36, the size of one key slot, matching mt76 `sec_key`. `key_len` stays 16.
- The network record now sets GN or AN, and AC on 5 GHz, when `ht_vht` is on and the station is that kind. Wi-Fi 6 on that record, and the HE bit on the station PHY byte, require `he` and `ht_vht` together, the same gate as the HE station records.
- The capability walk rejects a tag whose size does not fit in the bytes still left.
- HT MCS above 31 is ignored. Only modes 8 to 11 are stored as HE. Any other unknown mode is ignored.

Limits: still off unless the settings say `ht_vht` or `he`. The receive rate is still not reported. No stick boot, so the firmware has not accepted the 36-byte slot or the extra mode bits.

Tested: compared with mt76 `sec_key` (`cipher_len = sizeof(sec_key)`) and `mt76_connac_get_phy_mode`. Not built and not booted in this pass.

## 2026-11-17 - Game controllers: the picture of your own controller model

Problem: the Game controllers page drew one of four simplified family shapes (PlayStation, Xbox, Nintendo, Sega Genesis), so a DualShock 4, a DualSense and a DualSense Edge, or an Xbox One and an Elite, all looked the same and not much like the real thing; triggers were bars above the picture and a stick was a ring with a dot. The owner chose a picture per model by vendor and product ID from the CC0 Gamepad Line Art Pack, R5's own drawing for the Sega Genesis family, trigger caps (variant A) and lit sticks (17 Nov 2026, `docs/design/gamecontrollers-mockup/lineart-*.png`, `triggers-*.png` part 1, `triggers2-*.png` part A).

What changed:
- `distro/game/art/gamepad-line-art-pack`: the pack by gidedin (CC0 1.0), SVGs, `LICENSE.txt` and `README.md` unmodified. `distro/game/art/r5/genesis-m30.svg`: R5's Genesis / Mega Drive 2 / 8BitDo M30 drawing (MIT) in the same line style.
- `scripts/gen-controller-pictures.py` (Pillow only) writes `distro/game/lib/GameControllerPictureData.h`: each drawing as filled outlines (flattened, simplified to 0.375 viewBox units, even-odd per path; the M30's and the DualSense guide button's strokes turned into outlines, the M30's letters kept as text for the bold UI font), the outline of each control (a button's own outer edge, or a flood fill from a point inside for shoulders, touchpads and d-pad arms, widened to run under the line), the stick caps and their travel, and the ID table: 601 IDs from SDL's `controller_list.h` (commit `b2de7a19ad3c`, facts only) and the Sega Genesis style entries of the shipped SDL_GameControllerDB. About 2000 to 3900 points per drawing, 400 KB of source.
- `GameControllerPictures.cpp` (private to `libr5game.so` and its test): the model for a vendor and product ID; an unknown product of Sony, Microsoft or Nintendo gets the DualSense, Xbox Series or Switch Pro; a model of another family than the controller's (a chosen layout) gives way to the family's picture. No public call changed.
- `BGameControllerView` draws the model's picture in the text color (five passes a fraction of a pixel apart below 250 pixels wide, as the pack's bold drawings), a pressed control filled `B_STATUS_BAR_COLOR` under the line, a pressed stick's cap filled, a ring around a moved stick's cap moved the way it points, and two trigger caps over the shoulders with "L2 60%" and "R2 15%" beside them (LT/RT, ZL/ZR per model; none for the M30 and the Joy-Cons). The M30's C and Z light for the shoulders and L and R for the triggers, as the database maps them. A controller without a mapping keeps the picture built from what it reports. The old family shapes are gone from the code.
- Lines and lit parts are filled with one-color gradients: app_server fills a plain shape with the font subpixel setting, and the first screendump showed red and blue edges on the thin lines.
- `gamecontroller_test`: the ID table is sorted, 17 ID and family cases give the expected picture, no picture without a mapping, every drawing has its face buttons, Start, d-pad and shoulders inside the drawing, sticks except on the M30. `--hold` now presses Cross, L1 and L3, moves the right stick up and right, and holds L2 at 60 % and R2 at 15 %, as the approved mockup.
- Docs: `docs/GAME-CONTROLLERS.md` section 7 (decision, component report, as built), `docs/LIBR5.md` and the R5Book Game Kit page, `THIRD_PARTY_NOTICES.md`, `docs/LICENSING.md`, the mockup README with the approved PNGs.

Mistakes on the way: the first flood fill limit (8 % of the picture) rejected the DualShock 4 touchpad, which is larger; it is 15 % now. The first image showed colored line edges (see above).

Limits: the control places are measured from the drawings and checked on rendered check pictures, not with real controllers. The pictures show what is printed on the model; Button labels changes only what games get and the built picture. Saturn, SN30, SF30 and Steam controllers get their family's picture. The Switch Pro's ZL and ZR are digital, so their caps show 0 or 100 %. The Elite's and Edge's back paddles and the Edge's Fn buttons are not lit (not in a front view).

Tested: `scripts/build-locked.sh` twice, jam_exit=0 (x86_64). QEMU (KVM, SMAP and SMEP, Login, an own image copy, `GAMEPAD=1 SETTINGS_PAGE=gamecontrollers SHOTS="140 160"`), both builds: `gamecontroller_test: ok`, "pictures ok (601 IDs)", each of the ten drawings with its point and control count (2020 points and 16 controls for the Xbox Series up to 3924 and 17 for the DualSense Edge), the virtual DualSense and the two M30 pads as before, the five still pictures draw (1490 to 5098 line pixels), the other Bluetooth tests exit 0, no panic, no SMAP line. The screendump at 160 s (second build) shows the page with the DualSense picture: Cross, L1 and the left stick's cap lit, a blue ring around the right stick moved up and right, the L2 cap filled to 60 % with "L2 60%" left of it and the R2 cap at 15 % with "R2 15%" right of it, gray line edges. Light mode, font size 12, the window at 1030 by 610 only. The other nine drawings were checked on the generator's check pictures (every control lit, sticks pushed, caps drawn), not in QEMU.

Not tested: a real controller (QEMU has no game controller; the DualSense, the 8BitDo M30 and any Xbox or Nintendo controller), dark mode in QEMU (the check pictures are light only), ARM64 and RISC-V 64 (not built).

## 2026-11-17 - Graphics: the GPU is stopped before a restart or power off

Problem: after a restart from R5 the 5625U's caps lock blinked. Haiku's shutdown calls no driver, so the GPU's command processor, PSP ring and interrupts ran on into the firmware's start. Linux stops them at shutdown (`amdgpu_pci_shutdown`).

What changed: the kernel has R5 shutdown hooks (`r5_shutdown.h`, `shutdown.cpp` in `distro/kernel`, copied by `apply-kernel-bugs.py`), called after `sync()`. `radeon_hd` registers one with the GFX9 path: CP halt, PSP ring stop, interrupts off, VM 0 back, as `docs/GRAPHICS.md` "GFX9 stop at shutdown on GC 9.3". An addition after the reset contract; no fixed step changed.

Limits: not built or booted yet. Whether the blinking was this cause is not proven; the HP blink count was not read.

## 2026-11-17 - Bluetooth: a game controller's battery in the Deskbar menu

Problem: `docs/GAME-CONTROLLERS.md` puts the controller battery in the existing Bluetooth item in Deskbar. That item shows a battery line for each connected device whose `BT_MSG_R5_GET_PAIRED` entry has "battery" >= 0, but that value came only from HFP (headsets) and the LE Battery Service. A Classic HID controller's HID Battery Strength, which hid_shared reported in `R5_JOYSTICK_GET_INFO`, never reached it, and hid_shared took it only when a program read the joystick.

What changed:
- bluetooth_hid takes Battery Strength (Generic Device Controls page 0x06, usage 0x20) from every input report it is fed, with a second `HIDParser` of the same descriptor (only for a descriptor that has it), so the reports and items the protocol handlers read are not touched. A report too short for its battery byte keeps the last value.
- New additive call `R5_BTHID_GET_BATTERY` (`r5_bthid_battery`, appended to the enum in `<R5BluetoothHID.h>`) on the control node: uid 0, the device's owner only, `IS_USER_ADDRESS` + `user_memcpy` in and out, the caller's length checked.
- bluetooth_server (`HIDHost::Battery()`) fills "battery" in `BT_MSG_R5_GET_PAIRED` for a connected Classic HID device when HFP has none. Deskbar, My devices and Game controllers agree; Deskbar opens no joystick device and needs no change. The ioctl takes only the driver lock, so the reply does not wait for the device.
- hid_shared's `R5_JOYSTICK_GET_INFO` keeps a battery the transport already knows (bluetooth_hid now passes it), so `BGameController::BatteryLevel()` has it before the first read; usb_hid, i2c_hid and hyperv/hid are unchanged (they leave it at -1).
- Chosen over reading the joystick in the Deskbar item: Deskbar would have opened every joystick node (and needed libr5game or the driver header) for something the server already knows, and the menu and My devices would have differed.

Limits: a controller whose reports have no Battery Strength shows no battery line. The DualSense and the 8BitDo M30 most likely report none in that standard way (not checked); a per-controller battery report is not read. BLE HID controllers keep the Battery Service value. No fuzz target was added: the new parsing is hid_shared's existing `HIDReportItem::Extract()` on a zero-padded copy of a report the driver already bounds.

Tested: `scripts/build-locked.sh` and the QEMU Bluetooth smoke (SMAP=1, GAMEPAD=1, results below). `bt_hid_driver_test` creates a gamepad with a battery byte, checks -1 before the first report, 73 after one with no program reading, the same after a short report, -1 for a mouse, a kernel address and a short buffer refused, and 73 in `R5_JOYSTICK_GET_INFO` before any read.

Not tested: a real controller over Bluetooth (QEMU has no radio, so the `BT_MSG_R5_GET_PAIRED` path with a paired, connected controller and the Deskbar menu did not run), ARM64 and RISC-V 64 (not built).

## 2026-11-17 - Wi-Fi step 5: the standard and the rate on the page and in Deskbar

Problem: the driver can report a transmit rate and, when HE is on, that the link is Wi-Fi 6, but the Wi-Fi page and the Deskbar menu only say that a network is connected.

What changed:
- `IEEE80211_IOC_HAIKU_LINK` (`0x6006`) is a new request. The transmit-rate struct is unchanged. The kernel copies it out with `copyout`. The driver fills the standard from the node (HE, then VHT, then HT) or from the last transmit mode, the width from that transmit or from the channel, and the stream count from the last transmit. The receive rate stays 0: the MT7921 path does not report one.
- `WiFiLinkDetails` gains `standard`, `width`, `streams` and `rxKbps` at the end. `WiFiLinkLine()` builds `Wi-Fi 6 · 600 Mbit/s` from the receive rate when it is set, otherwise the transmit rate. The page appends that line to the existing status. The Deskbar menu adds it under "Connected to". `r5_wifi_link_line()` does the read and does not start a controller, so the Deskbar replicant links `libr5wifi.so` for that call.

Tested: AMD64 `scripts/build-locked.sh`: `jam_exit=0`, `mesa_exit=0`. QEMU (KVM, SMAP and SMEP, 2 CPUs, 2 GB, headless, 150 s) on an own copy of `r5-os.image` (`generated/wifi-qemu.sh`). The serial log has `enable SMAP` and `enable SMEP`. No panic and no `vm_page_fault`. The `kernel debugger` lines are the debugger extensions loading. Settings stayed running (`/boot/system/preferences/Settings --page wifi`, pid 96, still in `ps`). User space wrote `wifi-step5-booted`. QEMU has no Wi-Fi adapter, so the syslog has no mt7922 line and the link line was not shown. The words on the page were not read.

Not tested: a joined network, so the line itself has not been seen. ARM64 and RISC-V 64 not built. No What's new section.

## 2026-11-17 - Wi-Fi step 4a: Wi-Fi 6 stays off until the settings ask

Problem: the stack can join with Wi-Fi 4 and 5 when `ht_vht` is on, and it has no idea what an HE (802.11ax) element is. The MT7921 firmware wants those fields in the station record before it will use Wi-Fi 6.

What changed:
- HE Capabilities (extension 35) and HE Operation (extension 36) are parsed in `ieee80211_he.c` (MIT). A short element, a wrong id, or a length past the bytes the caller holds is ignored. The peer's MAC, PHY and MCS fields are stored on the node. The IE list keeps the same size: two spare slots now name those elements.
- Modes `IEEE80211_MODE_HE_2GHZ` (14) and `IEEE80211_MODE_HE_5GHZ` (15) follow VHT. `IEEE80211_MODE_BYTES` stays 2. A channel that is already HT gains `IEEE80211_CHAN_HE` (`0x40000000`; `0x80000000` is already used by ifconfig) only when HE is on. No extra channel copies.
- With `he` true and `ht_vht` true in `/boot/system/settings/kernel/drivers/mt7922wifi`, and only when the card's PHY tag byte 11 is set, the association request carries our HE Capabilities. That byte is the `he` field of `mt7921_phy_cap` (`mt7921/mcu.c`, BSD-3-Clause-Clear). Current mt76 does not read it. Assumed until a stick boot. The element is built from the streams, bandwidth, LDPC and STBC the card already reported. Absent settings leave HE off. `he` without `ht_vht` stays off.
- The station record then carries `STA_REC_HE` (tag 14, 28 bytes) and `STA_REC_HE_V2` (tag `0x19`, 28 bytes), and the network record carries `UNI_BSS_INFO_HE_BASIC` (tag 5). The field order is `mt76_connac_mcu.h` (BSD-3-Clause-Clear). mt76 sends both station records for this card (`is_connac2`). The basic MCS map is ours; the station record's map is the access point's. 6 GHz is not in this step.
- The host fuzzer target is `he`.

Tested: `wifi_fuzz he: 60s 0 crashes` (62877696 rounds) and `wifi_fuzz ies: 60s 0 crashes` (22512640 rounds). `scripts/build-locked.sh` on AMD64: `jam_exit=0`, `mesa_exit=0`. `ieee80211_he.c`, `if_mtk.c` and `mtk_sta.c` compiled in that jam. An earlier compile of `ieee80211_he.c` failed because the network headers the other net80211 files include were missing; those includes are in this change. QEMU (KVM, SMAP and SMEP, 2 CPUs, 2 GB, headless, 150 s) on an own copy of `r5-os.image` (`generated/wifi-qemu.sh`, not `scripts/run-qemu-r5.sh`). The serial log has `enable SMAP` and `enable SMEP`. No panic and no `vm_page_fault`. The `kernel debugger` lines are the debugger extensions loading. Settings stayed running (`/boot/system/preferences/Settings --page wifi`, pid 129, still in `ps`). User space wrote `wifi-step4a-booted`. QEMU has no Wi-Fi adapter, so the syslog has no mt7922 line. The words on the page were not read.

Not tested: a Wi-Fi 6 join, the page saying "Wi-Fi 6", and a download speed (stick W2). ARM64 and RISC-V 64 not built.

## 2026-11-17 - Graphics (M13 step 3, continued): 2 Lavapipe threads, occlusion culling, glass only where it shows, option E

Problem: after the transfer copy, the compose on Lavapipe still took 140-190 ms per frame while dragging (QEMU), with the copy and the blur the largest passes.

What changed:
- **Lavapipe uses 2 threads by default** (`LP_NUM_THREADS 2` in app_server's process, owner's choice): `gpu_lavapipe_threads <n>` sets another count, `0` Mesa's own one per CPU, and an `LP_NUM_THREADS` in the environment stays. Only Lavapipe reads it; RADV and other programs are not touched. Logged as `lavapipe threads 2 (default)`.
- **Occlusion culling**: each layer leaves out what the layers above cover with their plain copy; what an upper effect reads of the frame below stays (glass with the blur's reach, opening and closing windows), none during the workspace slide. `gpu_compositor_cull false` is a test switch.
- **The blur only for the glass that shows** (owner's choice): glass under opaque windows above is not blurred; it is made again when it shows. The lighter glass on Lavapipe (owner's first choice) was measured and taken out again: every strength has the same two Kawase levels, and the blur stayed at about 57 ms.
- **Option E**: in the new look, on an opaque image, `B_OP_ALPHA` and `B_OP_OVER` bitmaps with an alpha channel, and `B_CMAP8` over, are the hardware's `kSrcOver` over the bitmap made premultiplied, with no copy of the destination. `B_RGB32` over keeps the R5 shader (with the hardware blend it failed `bitmap-rgb32-over/opaque`). The classic look keeps the R5 shader. `gpu_draw_hardware_blend false` is a test switch.
- `gpu_debug`: `compose work per frame dispatches, regions, pixels`.

Tested (QEMU, KVM, 8 virtual CPUs, Lavapipe, own image copies, short runs of 20 s per motion; load average 1-3 unless said, another session's QEMU or jam beside some runs as marked). `scripts/build-locked.sh` jam_exit=0.
- Threads, Painter desktop: drag frame 18.5 ms (537 frames) with 2, 116.1 ms with 3, 57.2 ms with 4, about 171 ms with Mesa's default (8; the short run of the default beside another QEMU gave 260 ms); scroll 16.2 / 18.8 / 17.7 ms against about 115 ms. With 2 threads the drag passes are copy 4.1, blur 7.0, shadow 2.1, glass 1.6 ms per frame. A later run with the new default gave drag 93.5 ms and scroll 25.1 ms: single runs spread widely.
- Skia (`gpu_draw`) with 2 threads: drag 18.3-18.7 ms (539-568 frames), scroll 33.4-33.8 ms, with option E on or off alike (a run beside another session's two QEMUs is not counted).
- Culling against the same build without it (2 runs each): copy pass while dragging 70-77 to 54-57 ms (-25%), resizing 17-18 to 9-10 ms (-45%), scrolling 55-56 against 52-59 ms (the scroll regression of the first measurement was noise); half the pixels copied (drag 450k to 247k, scroll 686k to 280k), more copy regions (854 to 1209 per frame).
- Option E: `r5-draw-compare --new-look` 448/490 without it, 447/490 with it before the B_RGB32 case went back to the shader, and 448/490 with the same FAILs as without option E after it. `compose compare ok` every time.

Not tested: RADV and the 5625U. The thread count on real hardware: the stick measures 2 and 4 on Lavapipe (boot 3). Option E's gain on RADV.

## 2026-11-17 - Graphics (M13 step 3, code): Painter windows copied by transfer, Lavapipe settings, the M13 stick on paper

Problem: step 3 of `docs/M13-PLAN.md`, code only (owner, 16 Nov 2026): the copy fast path, the Lavapipe switches, and one stick plan for RADV and Lavapipe, walked on paper.

What changed:
- The plain part of a Painter window (the default desktop, `gpu_draw` off, and Safe mode's compositor) is copied row by row from its buffer into the frame with `vkCmdCopyBuffer`, as a Skia window's is from its image (#4332). The same words as the copy shader.
- Opt-in Lavapipe settings in the radeon_hd settings, logged when set: `gpu_lavapipe_msaa 1|4` (Skia's sample count for paths, only on Lavapipe), `gpu_lavapipe_threads <n>` (`LP_NUM_THREADS`), `gpu_lavapipe_vector 128|256` (`LP_NATIVE_VECTOR_WIDTH`). Without them nothing changes.
- The M13 stick mode: `boot-next m13` (boots 1, 2, 3, then no settings file; a `gpu_m13_boot` line marks the boot), its `check` (device lines, no `page table pool full` on RADV, `scanout through vulkan` in boot 2, the Lavapipe setting in boot 3, warm-up and pipeline cache, no compose compare MISMATCH, the m13-measure numbers, no `gpu draw off`, no `compositor off`), and `check-pre-stick.py --paper` for it. The plan and the card: `generated/m13/STICK-PLAN.md`.
- Not changed: `gpu_compositor_scanout` and `gfx9_wc_gtt` already exist and are only switched on by the stick's boot 2; the GPU page-table entry of a write-combined buffer stays snooped (GFX9 contract). The userland EOP fence check is not built: design in `docs/M13-PLAN.md` (a new additive call, a read-only clone per team), because it is a kernel change that QEMU cannot run.

Tested (QEMU, KVM, 8 virtual CPUs, Lavapipe, own image copies, one run after the other; host load about 2, no other QEMU or jam). `scripts/build-locked.sh` jam_exit=0.
- Painter under the compositor (`gpu_debug`, no `gpu_draw`), base (#4332) against this branch: drag 181.1 to 147.4 ms (111 to 137 frames), scroll 141.0 to 100.1 ms (143 to 197 frames), resize 46.2 to 37.3 ms; compose passes: copy 88 to 48 ms per frame.
- `gpu_lavapipe_msaa 1` (Skia on Lavapipe): drag 158.2 ms (125 frames) against 160.7-166.8 ms with 4x; no clear gain. `r5-draw-compare` 458/490 against 459/490: two new FAILs (`stroke-roundrect-pen3/clear`, `stroke-shape-pen1/opaque`), one fewer (`stroke-shape-pen3/opaque`). It is outside the compare set's tolerances, so it stays an opt-in test switch and is not recommended. `compose compare ok` 7 times with it.
- `py scripts/check-pre-stick.py --paper`: M11, M12 and M13 walks ok (`m13 lines 7 have a source`, `m13 boot 1/2/3/done settings ok`).

Not tested: RADV and the 5625U, `gpu_lavapipe_threads` and `gpu_lavapipe_vector` (the vector width is only measurable on the 5625U: boot 3 of the stick). The stick is not built or written.

## 2026-11-17 - Graphics (M13 step 1): the R5 DRM library formats ioctl details only with gpu_debug (libdrm-2.4.124-4)

Problem: `r5_translate.c` formatted a detail string (`snprintf`) into one global buffer for every production ioctl (`GEM_CREATE`, `GEM_VA`, `INFO`, `PRIME` export and import), only to print it with `gpu_debug` or after a failure. The buffer, the ioctl name and the request were shared by all threads, and RADV and Zink call ioctls from several, so one thread's details could be printed with another's call.

What changed: the formatting happens only with `gpu_debug` (`R5_DETAIL`, behind `r5_debug_on()`); a failed ioctl without `gpu_debug` is printed without the detail. The name, request and detail are per thread (`__thread`). `libdrm-2.4.124-4`: `libdrm.so.2.124.0` rebuilt; `libdrm_amdgpu.so.1.124.0` and `amdgpu.ids` unchanged; pin, install, EGL vendor check and `distro/gpu/COMPAT` name `-4`, and `-3` stays in the download folder.

Tested: host test (`test-ac-drm.sh` against this branch): `ok vram full falls back to gtt`, `ok ac drm family RV`, `ok libdrm ioctl`. `ok libdrm symbols`. The new library is a Haiku x86_64 ELF with a `.tbss` section. `scripts/build-locked.sh`: jam_exit=0, `ok package paths`, `ok egl vendor`, mesa_exit=0. After the build the shared tree's HaikuPorts pin was set back to `-3`, so other branches keep building until this is in `main` (the mistake of `-3`, recorded below, not repeated).

Not tested: RADV and the 5625U (QEMU has no RADV; Lavapipe does not use libdrm). The speed gain is one `snprintf` per ioctl and is not measurable in QEMU.

## 2026-11-17 - Bluetooth: a headset with an LE side pairs over Classic, and the Bluetooth window's list keeps its width

Problem (stick 52, 5625U): the Sony WH-1000XM3 also advertises over LE as "LE_WH-1000XM3" (public address, Flags 0x2). That entry was paired over LE only (`le pairing started`, `smp sc pairing ok`, `gatt no hid service`): the page said Connected, but the headset kept blinking, and A2DP and HFP never came up because there was no Classic link. In the Bluetooth window opened from Deskbar, the device panel took the whole width and the My devices list was a strip.

What changed:
- `LEManager::Pair()`: a device whose LE advertisement has a public address and Flags without "BR/EDR Not Supported" (bit 2) is dual-mode and is paired over Classic (log: `le advert of a dual-mode device (flags <hex>): pairing over Classic`). An LE-only device sets that bit, so it still pairs over LE.
- Such a device does not take its LE name; its Classic name is asked once the link is up (the Remote Name Request of #4346).
- The My devices list has a minimum width of 200 (scaled), so the panel cannot squeeze it.

Also seen on stick 52: the 8BitDo M30 over Bluetooth with its name, buttons and D-pad right (#4345, #4346), Settings closed without the debugger, and `link down (reason 0x13, the device ended it)` at the end.

Limits: a device that leaves out "BR/EDR Not Supported" although it has no Classic side would now try Classic and fail (page timeout); R5 does not fall back to LE then. Not yet on a radio.

Tested: see the pull request. ARM64 and RISC-V 64 not built or tested.

## 2026-11-17 - Bluetooth after stick 51: device names, HFP's SDP record, link-down lines, Test buttons and the page layout

Problems (boot 51 on the 5625U, MT7922, WH-1000XM3, 8BitDo M30): devices showed their kind instead of their name in Devices nearby and My devices; HFP never started with the XM3 (pairing, encryption and SDP ran, but no `hands-free service` and no `hfp:` line); the headset dropped right after connecting with nothing in the syslog; "Test buttons…" did nothing; the device panel's buttons ran past the window's right edge and the page got a scroll bar.

Names, what was found:
- Regression, found by reading the code: `_SDPThread()` reads SDP into a probe record of its own (#4328, `1a25a5f09a`) that had only the address. #4335 (`881de0451f`) then sends `fRemoteDevice->friendly_name` of that record to bluetooth_hid (`R5_BTHID_SET_INFO`), so the name was always empty there and the Game controllers page fell back to "Game controller". Each change was fine on its own; together they lost the name. The probe now carries the device's name and class.
- For the Bluetooth page itself no change between boot 52 of 16 Nov and boot 51 touches the path from the scan to the pair request and My devices; both boots run the same controller setup. What the code shows: R5 never wrote Write Inquiry Mode, so after the HCI Reset at start the MT7922 answers in standard mode, which carries no name (and no signal strength); names came only from LE advertising. Not proven on hardware why the names showed on 16 Nov.
- Fix: after the reset, Write Inquiry Mode (extended inquiry result where the controller has it, else with RSSI), logged as `inquiry mode extended|rssi`. A Classic device that answers without a name, or connects without a known one, is asked with Remote Name Request after the scan, one at a time, 12 s each (`LocalDeviceNames.cpp`, MIT). The name goes into the record (and the saved list when paired), is kept for the next scan, and goes to the page, which now takes a late name for a device it lists. Pair cancels a name request on its way. No name or address is logged.

HFP and SDP:
- The SDP answer was read with Haiku's `SDPDataIO` in one pass; a Nil (it did not step over it), a URL or a 128-bit integer put it out of step, which dropped that record and every record after it. R5's own parser (`SDPParse.h`, MIT, from Core 5.4 Vol 3 Part B) steps over each element by its length; attributes are stored as before. Which element the XM3 sends is not known; this is the likely cause, not a proven one.
- When an audio device's browse brings no Hands-Free channel, a Service Search Attribute Request for `0x111E` with `0x0001`, `0x0004`, `0x0009` and `0x0311` follows (continuation states followed, at most 64 pieces).
- One line per answer: `sdp: <n> records: 0x1000 0x110b ...`, `sdp: hands-free search, <n> records: ...`, and `hfp: no hands-free record` for an audio device without one.

Link down: `bluetooth_server: link down (reason 0x13, the device ended it, status 0x0)` for every Classic link that ends (no address). The drop of the XM3 is most likely the headset leaving when no profile came up; HFP should now start 5 s after the link. Not confirmed.

Test buttons: `BRoster::Launch()` refuses to launch the caller's own team (`B_BAD_VALUE`, nothing sent), and the page runs inside Settings. Inside Settings the button now posts the same `--page gamecontrollers` arguments to Settings as `B_ARGV_RECEIVED`; the separate Bluetooth program still uses `Launch()`.

Page layout: the panel had an explicit minimum width of 250 pixels, narrower than its three buttons, so they were cut at the right. The panel is now as wide as its button line; "Use for sound" or "Test buttons…" share that line with Disconnect and Forget, so the page fits the default window height. Long texts (a device name can be 248 bytes, the scan hint, the radio note) are cut with an ellipsis instead of widening the page. Devices nearby has a minimum of three rows.

Test aids: `R5_BLUETOOTH_PREVIEW=1` (test only, read by libr5bt) adds one made-up connected game controller (address from the documentation range) to My devices. `run-qemu-smoke.sh`: `BT_PREVIEW=1`, `BT_WINDOW="<w> <h>"` and `BT_TEST_BUTTONS=<s>` (sends the button's message with hey).

Mistake on the way: the first layout put "Test buttons…" on a line of its own; the page then fit its width but kept a scroll bar at the default size, and the next one (all buttons on one line) was cut at the right until the explicit 250-pixel minimum of the panel was removed.

Tested: `scripts/build-locked.sh` exit 0 (x86_64), also after merging `main` with #4345. `scripts/fuzz-bt.sh sdp` 60 s, 0 crashes (the made-up five-record answer: 5 records, classes `0x1000 0x110b 0x110e 0x111e 0`, Hands-Free channel 3 through `r5rfcomm::FindChannel()`, in pieces of 1 to 1000 bytes; then damaged copies). QEMU (KVM, SMAP and SMEP, Login) on an own image copy with `BT_PREVIEW=1`: all smoke tests `exit 0`; screendumps at 1030 by 610 show My devices with the panel, Test buttons…, Disconnect and Forget inside the window, Devices nearby and the Bluetooth line, and no scroll bar; at 910 by 560 the buttons stay inside the window and the page scrolls vertically (allowed: it is taller than the area there). `BT_TEST_BUTTONS=75`: Settings shows Devices → Game controllers afterwards.

Not tested: nothing on a radio (QEMU has none): Write Inquiry Mode, Remote Name Request, the Hands-Free search, the link-down line and the names on the page are not seen working; the XM3 and the M30 are untested. ARM64 and RISC-V not built.

## 2026-11-17 - Game controllers: set up your controller's buttons, a Layout menu, and no crash when Settings closes

Problem (boot 51 on the 5625U, the 8BitDo M30 paired over Bluetooth, `2dc8:0651`, report descriptor 86 bytes): Settings crashed when it quit (Segment violation in `__dynamic_cast` from `BApplication::_WindowAt()` in `~BApplication`, with the thread "game controller watcher" still there). On the page's Sega picture the D-pad did nothing and a pressed button lit the wrong key.

Cause of the crash: `libr5game.so` started one `BLooper` per team for `StartWatching()` and never quit it. Settings unloads its add-ons, and `libr5game.so` with them, in `~SettingsApp`, before `~BApplication` walks its loopers; the watcher's code was gone, so the cast on it crashed.

Cause of the mapping: R5 took the database's Windows line for `2dc8:0651`, which is a USB line (GUID bus `03`; there is no Windows Bluetooth line). It puts the D-pad on DirectInput axes 0 and 2; R5 sorts the axes by usage, so axis 2 is Z, which a pad with only X and Y (and perhaps a hat) does not have. The Mac and Linux Bluetooth lines for the same IDs disagree with each other (A and B swapped; a hat on Linux, X and Y on the Mac). Which physical key of the M30 sends which HID button over Bluetooth is still not known: the 86 bytes are not in the log.

What changed:
- `libr5game.so`: the last `StopWatching()` quits the watcher; a static destructor quits one still running when the library is unloaded or the team exits; the watcher stops its node monitors. The page already called `StopWatching()` when it was detached; it now also closes its controller and the setup window there.
- The mapping follows the HID descriptor first (`docs/LIBR5.md`, "Game Kit"): a Windows line for the controller's own bus first; buttons renumbered by HID usage (DirectInput counts the usages that exist, hid_shared numbers by usage with gaps); the D-pad on the controller's hat switch, or without one on X and Y when no stick reads them; a D-pad on buttons stays; sources the controller does not have are dropped. `r5_joystick_info` has a new last field, `button_usages[4]`, filled by hid_shared (an R5 snippet; the size field tells an older driver apart).
- `BGameController`, calls added at the end: `LayoutFor()`, `SetLayout()`, `GetSavedMapping()`, `SetSavedMapping()`, `HasSavedMapping()`. `Open()` takes a saved setup, else a chosen layout (a fixed mapping in the HID order of that family's controllers), else the database. Stored in the existing `~/config/settings/game_controllers`. `BJoystick` programs see the same raw buttons as before. `ResetLabelStyles()` now removes only the label choices (it removed the whole file, which now also holds layouts and setups); same arguments and meaning.
- Settings, Game controllers: the "Shapes R5 draws" strip is gone (owner). The Test box has "Layout" (Automatic, Xbox, PlayStation, Nintendo, Sega Genesis, and "Your own setup" while one is saved) above "Button labels", and "Set up buttons…", a small window that asks for each control in turn (face buttons, shoulders, triggers, sticks, the D-pad, Start, Select; for the Sega shape A B C X Y Z L R), with Skip and Cancel, and saves the setup per controller model. The vendor and product ID moved under the picture. Revert restores labels, layout and setup from when the page was opened; Defaults resets every label choice, as before, and removes the chosen controller's layout and setup. Subtitle: "Test your controllers and set up their buttons". The mockup follows.
- `gamecontroller_test`: the bus choice, buttons by usage, the D-pad rules, the fixed layouts and the setup text; two synthetic pads with the M30's IDs on bluetooth_hid (X and Y plus a hat and 16 buttons; X and Y and 12 buttons, no hat), read through the M30's database line; a saved setup and a chosen layout on a made-up model. The M30's real descriptor is not used: it is not in the log, and I do not know it well enough to rebuild it.
- `run-qemu-smoke.sh`: `QUIT_SETTINGS=1` resizes Settings to 910 by 560, then leaves the page for Information, quits Settings with `hey` and lists the processes.

Mistake on the way: the first build put the vendor and product ID beside Layout; the QEMU screendump at 910 by 560 showed the Test box running past the page (no right border). They now sit under the picture.

Tested: `scripts/build-locked.sh` jam_exit=0 (x86_64). QEMU (KVM, SMAP and SMEP, Login, an own image copy, `GAMEPAD=1 QUIT_SETTINGS=1 SETTINGS_PAGE=gamecontrollers`): `gamecontroller_test` ok: descriptor-first checks ok; 872 of 872 Windows entries parse; the DualSense virtual controller as before (Cross is A, the hat is down, R2 32703); "M30 hat pad: a mapping, family 4 (Sega), 1 hat, up 1, A 1"; "M30 axes pad: a mapping, 0 hats, left 1, up 0, B 1"; the saved setup (b3 is A, X is d-pad left), the Sega layout under it, and both removed again. The other Bluetooth tests of the script exit 0. No panic, no SMAP line. Screendumps: at 1030 by 610 the page with the DualSense card, the Test box with Layout "Automatic", Button labels "PlayStation (✕○□△)", "Set up buttons…", the PlayStation picture with Cross lit and L2 at 60 %; no Shapes strip. After the quit Settings is gone from the process list and the syslog has no debugger line. Second build (IDs under the picture, jam_exit=0) and the same QEMU run: the same test lines, `gamecontroller_test: ok`; at 910 by 560 the Test box keeps its right border inside the page, with the vendor and product ID under the hint; Settings quits without a debugger line; no panic, no SMAP line.

Not tested: a real controller; the 8BitDo M30 and the DualSense on this build; which M30 key sends which button over Bluetooth; the setup window with a controller (QEMU sends no button presses to it; it was not opened in QEMU); dark mode; ARM64 and RISC-V 64 (not built).

## 2026-11-17 - Wi-Fi step 3: SAE frames and protected management, supplicant unchanged

Problem: a WPA3 join needs SAE (authentication algorithm 3) and protected management frames (BIP-CMAC-128, the IGTK). The supplicant package that is built does not compile those in.

What changed:
- The recipe sub-task stops. HaikuPorts `wpa_supplicant-2.11.haiku.1.recipe` (revision 1 `7bb9e50ac5` and revision 2 `a3ac62e9d5`) builds git `bdf3144ad606b226aca6f8fae103e671e4655138` and uses the committed `wpa_supplicant/.config`, not `defconfig`. PMF is off: `.config` line 297 is `#CONFIG_IEEE80211W=y`. SAE is off: `.config` has no `CONFIG_SAE` line, and `Makefile` lines 265-274 set `CONFIG_SAE=y` only inside `ifdef CONFIG_MESH`, while `.config` line 576 is `#CONFIG_MESH=y`. OWE is off: `.config` line 590 is `#CONFIG_OWE=y`. `defconfig` does contain `CONFIG_SAE=y`; that file is not what this package builds. `driver_bsd.c` has no SAE authenticate path. WPA3 needs an own wpa_supplicant recipe (choice 3b). No recipe was written.
- The kernel accepts SAE frames. Algorithm 3 is stored for the supplicant and does not move the station to associate. A received frame with status 0 and sequence 2 logs `sae: authenticated` and nothing of the frame body. Sending is `IEEE80211_IOC_HAIKU_SAE_AUTH` (0x6004): the ioctl copies the body in, checks algorithm 3 and a length of 6 to 400, and transmits it. The same request reads the last body back out. While the mode is SAE, entering authentication does not send an open authentication frame. A later join from that state asks for association. The mode value is 6, added beside the existing authentication modes. The body is not logged.
- An RSN element that lists SAE (suite 8, or FT over SAE, suite 9) is reported as SAE, including when PSK is listed as well. An unknown suite still falls through to PSK.
- BIP-CMAC-128 is an R5 cipher module (`ieee80211_crypto_bip.c`, MIT). It uses the AES already in the tree. The MIC is the first 8 bytes of AES-CMAC over the masked frame control, the three addresses, the body, and the MMIE with a zero MIC. The com advertises `IEEE80211_C_MFP` (bit `0x00400000`, previously unused).
- An IGTK uses key index 4 or 5, which does not fit the four group-key slots. The existing set-key request (`IEEE80211_IOC_WPAKEY`) for BIP-CMAC-128 at those indexes copies the key in the kernel and hands that copy to the driver. It is not stored in the group-key array. The driver sends `STA_REC_KEY_V2`: the remembered pairwise CCMP key in the first slot when it has one, and BIP-CMAC-128 (cipher id 10) in the other. The key bytes are not logged. After the firmware accepts the update the driver logs `pmf: on`. mt76 has no separate PMF bit in the station record (`mt76_connac_mcu.c` and `mt7921/mcu.c`, SPDX BSD-3-Clause-Clear, searched for a PMF field; the key path is the signal). The host cipher is registered. Index 4 is not installed in a slot, so the host does not yet build the MMIE for that index. The cipher id and the 36-byte key layout are taken from mt76 and are assumed until a stick boot.
- The Network Kit gains `B_NETWORK_AUTHENTICATION_WPA3` (5), `B_KEY_MODE_SAE` (`0x0400`) and `B_KEY_MODE_OWE` (`0x0800`). `wireless_network` is unchanged. The Wi-Fi page does not say "WPA3" in this step.
- User pointers stay in the ioctl. The driver only sees kernel copies. 0x6005 is the driver's command for that kernel copy. A user request with that number is rejected and is not passed to the driver. The next free kit request is 0x6006.

Tested: `scripts/build-locked.sh` jam_exit=0 (x86_64). The translator errors (avif, jpeg, png, tiff, webp, icns, jasper) are the same pre-existing ones. Host, WSL, gcc with AddressSanitizer and UBSan: `wifi_fuzz rsn: 60s 0 crashes` (54,100,992 rounds). The other fuzz targets were not re-run; this step changes the RSN parser.

QEMU (KVM, SMAP and SMEP, 2 CPUs, 2 GB, headless, 150 s) on an own copy of `r5-os.image`. The serial log has `enable SMAP` and `enable SMEP`. No panic and no `vm_page_fault`. The `kernel debugger` lines are the debugger extensions loading. Settings stayed running (`/boot/system/preferences/Settings --page wifi`, pid 94, still in `ps` after 8 seconds). User space wrote `wifi-step3-booted`. QEMU has no Wi-Fi adapter, so the syslog has no mt7922 line, no `sae: authenticated` and no `pmf: on`. The words on the page were not read.

Not tested: a WPA3-only join, a WPA2 join on the same boot, and the two syslog lines on hardware (stick W2). The pairwise-key slot and the BIP cipher id in the firmware message are assumed. ARM64 and RISC-V 64 not built.

## 2026-11-17 - Wi-Fi step 2: Wi-Fi 4 and 5 stay off until the settings ask

Problem: the MT7921 could only offer 802.11a/b/g. The card reports HT and VHT in its capability event, and the firmware does not reorder A-MPDU by itself. The Wi-Fi page already asks the driver for the last transmit rate (`IEEE80211_IOC_HAIKU_TX_RATE`, 0x6003) and the driver dropped those status records.

What changed:
- The driver reads the PHY tag it already fetched (ht, vht, max bandwidth, streams, LDPC, STBC). `max_bw` is taken as 0, 1, 2, 3 for 20, 40, 80 and 160 MHz. That mapping is assumed from the byte order in the event; a stick boot can correct it.
- With `ht_vht` true in `/boot/system/settings/kernel/drivers/mt7922wifi` (absent means false, same rule as the other keys in that file), and only when the card said it has HT, the channel list gains 11n and, on 5 GHz when the card said VHT, 11ac. 40 MHz and 80 MHz follow the reported bandwidth. The capability bits are translated from that event. This tree has no `IEEE80211_C_TX_AMPDU`; HT operation and transmit A-MPDU are `IEEE80211_HTC_HT` and `IEEE80211_HTC_AMPDU`.
- The station record gains HT and VHT only when the setting is on and the station is that kind of station. Rate control stays in the firmware.
- A-MPDU reorder stays net80211's. mt76 does not leave it to the firmware: `mt7921_ampdu_action` on receive start calls both `mt76_rx_aggr_start` and `mt7921_mcu_uni_rx_ba` (`mt7921/main.c` lines 966 and 968, SPDX BSD-3-Clause-Clear). After the stack has installed its own reorder, the driver tells the firmware about the session (`STA_REC_BA`).
- Transmit status (packet type 0), which the driver used to drop, is parsed. The last acknowledged rate is what the kit's existing 0x6003 request shows. The driver fills a kernel struct; the ioctl copies it out. No new copy of a user pointer in the driver. Until a status record arrives the rate stays 0 and the kit ignores it.
- Default tonight is off. No settings file is shipped.

Tested: `scripts/build-locked.sh` jam_exit=0 (x86_64), twice; the second run is the image that was booted, after another session had used the shared tree. Host, WSL, gcc with AddressSanitizer and UBSan, the step 1 targets again:
- `wifi_fuzz ies: 60s 0 crashes` (34,231,296 rounds)
- `wifi_fuzz rsn: 60s 0 crashes` (95,547,392 rounds)
- `wifi_fuzz action: 60s 0 crashes` (185,840,640 rounds)
- `wifi_fuzz mtkrx: 60s 0 crashes` (69,677,056 rounds)

QEMU (KVM, SMAP and SMEP, 2 CPUs, 2 GB, headless, 150 s) on an own copy of `r5-os.image`. The serial log has `enable SMAP` and `enable SMEP`. No panic and no `vm_page_fault`. The `kernel debugger` lines are the debugger extensions loading. Settings stayed running (`/boot/system/preferences/Settings --page wifi`, still in `ps` after 8 seconds). QEMU has no Wi-Fi adapter, so the syslog has no mt7922 line and the page has nothing to join. The words on the page were not read.

Not tested: a WPA2 join, an hour without a panic, and a download with HT/VHT off and on (stick W1). ARM64 and RISC-V 64 not built. `mtk_tx.c` still names no copyright holder. The first boot looked for Settings under `apps`; it lives under `preferences`.

## 2026-11-17 - Wi-Fi step 1: a frame that does not fit is dropped, and a join that hangs gives up

Problem: a short information element in a beacon was read past its end (FH, DS, channel switch, HT, VHT). `ieee80211_ies_init` turned a negative length into a huge allocation. `IEEE80211_VERIFY_LENGTH` compared a pointer difference with a `size_t`, so a frame shorter than its header looked long enough and was then read past its end. A join that never reached RUN had no limit. The scan sweep already stops after 15 seconds. The firmware reply wait stays 2 seconds.

What changed:
- Distro copies of the net80211 beacon, HT, VHT and element-init parsers, and the length check they share (`ieee80211_input.h`). A short element is dropped and counted by the existing discard stats. A length below 0 or above 4096, or a null buffer with a positive length, is refused. A beacon shorter than the fixed fields clears the scan record before returning, so a reused record does not keep a pointer into the previous frame. A channel-switch element shorter than 3 bytes is ignored.
- The MT7921 driver gives a join 15 seconds (`MTK_JOIN_TIMEOUT`). It then logs `join gave up after 15 s; not connected` and goes back to scan. The new lines touch the driver softc only. No new copy of user memory.
- Host fuzzer `scripts/fuzz-wifi.sh`. It compiles the real functions (a `distro/` copy wins over the Haiku tree) with AddressSanitizer and UBSan. Targets: ies, rsn, action, mtkrx.

Tested: `scripts/build-locked.sh` jam_exit=0 (x86_64). Host, WSL, gcc with AddressSanitizer and UBSan:
- `wifi_fuzz ies: 60s 0 crashes` (28,874,752 rounds)
- `wifi_fuzz rsn: 60s 0 crashes` (62,419,968 rounds)
- `wifi_fuzz action: 60s 0 crashes` (126,505,984 rounds)
- `wifi_fuzz mtkrx: 60s 0 crashes` (66,747,392 rounds)

QEMU (KVM, SMAP and SMEP, 2 CPUs, 2 GB, headless, 100 s) on an own copy of `r5-os.image` (`generated/wifi-qemu.sh`, not `scripts/run-qemu-r5.sh`): user space wrote `wifi-step1-booted`; the serial log has `enable SMAP` and `enable SMEP`; no panic and no `vm_page_fault`. The `kernel debugger` lines are the debugger extensions loading at boot. QEMU has no Wi-Fi device, so the syslog has no mt7922 line.

Not tested: a WPA2 join and an hour without a panic (stick W1, later). ARM64 and RISC-V 64 not built. `mtk_tx.c` at air/OS `e3c4cc898` (`src/add-ons/kernel/drivers/network/wlan/mt7922wifi/dev/mtk/mtk_tx.c`) names no copyright holder; the file says only "Distributed under the terms of the MIT License." That line was left as it is.

## 2026-11-17 - Graphics (M13 step 1): five small app_server fixes for Skia drawing

Problem: `docs/M13-PLAN.md` Priority 2: hot paths of the Skia drawing that cost CPU or uploads on every draw.

What changed (one commit each): bitmap textures are checked by a fast 64-bit hash of all bytes instead of a full copy and `memcmp` (`FindTexture`; no flag, because a program writes its `BBitmap` at any time); a drawn, unrotated string without a view transform is laid out once (the pen position is the width of the drawing's own pass); an opaque solid fill uses `SkPaint::setColor()` instead of a runtime shader; gradients reuse their uploaded colours (32 per target, compared byte for byte); no wake of the draw thread per draw call inside `BeginUpdate()..EndUpdate()` (drawing outside an update, and an update held over 100 ms, still wake at once).

Tested (QEMU, KVM, 8 virtual CPUs, Lavapipe, `gpu_draw true`, `scripts/qemu-m13-measure.py` on own image copies, base and this branch alternated: base, step 1, base, step 1). Host load: load average about 2 (only these runs, no other QEMU or jam). `scripts/build-locked.sh` jam_exit=0.
- Base (`claude/m13-step2-fix`): drag 161.4 / 166.8 ms (121 / 122 frames), scroll 182.8 / 142.4 ms (111 / 135 frames), draw while dragging 97.8 / 101.4 ms.
- Step 1: drag 163.4 / 160.7 ms (126 / 190 frames), scroll 143.4 / 147.6 ms (140 / 136 frames), draw while dragging 92.4 / 107.5 ms.
- No `compositor off`, no `gpu draw off`. The difference is within the spread of the base runs: on Lavapipe the frame is bound by the GPU work (compose passes: copy about 55 ms, blur 55 ms, shadow 21 ms, glass 18 ms per frame), which these CPU-side fixes do not change. An earlier measurement per commit (17 Nov, 00:00-01:00) ran beside another session's jam (load 3-9) and is not used.

Not tested: RADV and the 5625U, where the CPU side weighs more (first M13 stick). The 36 MB wallpaper case of the hash is not in the QEMU scenario.

## 2026-11-17 - Graphics (M13 step 2 fix): no compositor off on Lavapipe, a copy instead of texelFetch, Mesa's cache, compose time per pass

Problem: after #4330 the compositor turned itself off with `fence timeout` on Lavapipe (QEMU, `gpu_draw true`). Two causes were in step 2's own code: the damage of a Skia run went out right after the submit, so a compose was queued behind runs of 11-15 s after Sign in and hit its 15 s limit; and a pass without a compose waited up to 15 s on the CPU for the runs' semaphores. The old build (main with step 0) also turned it off, in 2 of 5 runs, so the limit itself did not fit Lavapipe. And with the images, the plain copy of a Skia window (`texelFetch` in `compose.comp`) cost 150-163 ms per frame while dragging on Lavapipe.

What changed:
- The draw thread waits for its run's fence before it reports the damage; the compose still waits only on the GPU (the semaphores), nothing is read back. A pass without a compose no longer waits on the CPU; its semaphores go back after the next compose's fence.
- No fence limit on Lavapipe (owner, 16 Nov 2026): Lavapipe is the CPU, not a GPU that can hang, so a late frame waits on (logged every 10 s after 15 s). RADV keeps the 15 s limit and the reset ladder.
- The plain part of a Skia window is copied from its image into the frame with `vkCmdCopyImageToBuffer` (the image goes to `TRANSFER_SRC_OPTIMAL` and back), the same bytes as the copy shader. This is step 3's copy fast path, for Skia windows only; Painter windows keep the copy shader. The host buffers and the scanout back buffer are also transfer targets now (the compare copies of step 2 wrote into a buffer without `TRANSFER_DST`, which Lavapipe let pass).
- `MESA_SHADER_CACHE_DIR` for app_server (uid 0) is `/boot/system/cache/app_server/mesa`, set before the driver loads, unless the environment has one (owner, 16 Nov 2026). It was `/boot/home/config/cache/mesa_shader_cache`, root-owned inside the administrator's home. The Mesa recipe is unchanged.
- With `gpu_debug`: GPU timestamps between the compose's passes, one line per 10 s, `compose passes us per frame copy <n> blur <n> shadow <n> glass <n> animation <n> frames <n>`.

Tested (QEMU, KVM, 8 virtual CPUs, Lavapipe, 1280x800, `scripts/qemu-m13-measure.py` on own image copies, before and after one right after the other). Host load: load average about 2 during both runs (another session's 2-CPU QEMU ran part of the time); a jam of another session ran during the second boot.
- Before (main with step 0): warm-up 10.5 s; drag frame 175.1 ms (p95 270.0, 110 frames), input 338.5 ms, draw 248.4 ms; scroll 169.8 ms (p95 327.4, 121 frames), draw 303.9 ms; resize 2 frames, draw 5.7 s.
- After: warm-up 11.6 s; drag frame 171.1 ms (p95 199.5, 120 frames), input 339.3 ms, draw 98.0 ms; scroll 167.3 ms (p95 307.5, 172 frames), draw 157.3 ms; resize 1 frame, draw 13.1 s. Compose passes while dragging: copy about 50 ms, blur 18-25 ms, shadow 10-14 ms, glass 8-12 ms per frame (before the copy path: copy 150-163, blur 63-66, shadow 25, glass 19-20 ms). Glass does not dominate a drag; the copy and the blur do.
- No `compositor off` and no `gpu draw off` in the before or after runs of this pair (an earlier pair of this evening, before the fence fix, had the old build off 2 times and the new one 2 times).
- Warm-up on the second boot of the same image (Mesa's cache in the system cache directory now): 4.9 s in the earlier pair, 10.3 s in this pair (with another session's jam beside it), against 11.6 s on the first boot. Skia's pipeline cache holds only the Vulkan header on Lavapipe (312 bytes, `stored 0 KB`): the gain is Mesa's own shader cache.
- `compose compare ok` 6 times with effects off and a window drawing every 16 ms, no MISMATCH; `r5-draw-compare` 459/490, the same FAILs.

Not tested: RADV and the 5625U (the stick of step 3). The resize numbers have 1-2 frames per run, too few to compare.

## 2026-11-17 - Bluetooth step 7c joined with HFP: the microphone opens the headset's voice link

Problem: 7c (#4334) brought the voice path (h2generic, the decoder, the "Bluetooth headset microphone" input) but nothing opened a voice link, because the HFP side (#4333) was not on `main` yet. A voice link the headset opened itself went into the ACL code and marked the device as connecting.

What changed (all only with `bt_voice true`; without it nothing changes):
- bluetooth_server reads `bt_voice` once at start. The HFP agent then offers codec negotiation (`+BRSF` bit 9), keeps `AT+BAC`, takes `AT+BCS` and `AT+BCC`.
- The microphone node tells bluetooth_server when a program connects to it and when it lets go (`BT_MSG_R5_AUDIO_VOICE`, admin port, uid 0). The HFP session then sends `+BCS` (mSBC when both sides have it), waits for `AT+BCS`, and asks for the voice link with the HFP 1.8 settings in turn (T2, T1 for mSBC; S4, S3, S1, D1 for CVSD). It closes the link when the microphone is let go or the session ends.
- `LocalDeviceVoice.cpp`: Setup Synchronous Connection, the next setting after a refused or failed one, Accept Synchronous Connection Request for a link the headset opens while R5 wants one, Reject Synchronous Connection Request otherwise (also without `bt_voice`: a voice request no longer reaches the ACL code), and the end of the link.

Limits: nothing tried on a radio. If the XM4 refuses mSBC with these settings, the link stays down (R5 does not fall back to CVSD after an mSBC codec choice). The headset hears silence (no speaker path).

Tested: `scripts/build-locked.sh` exit 0 (x86_64; the first build failed on a duplicate `case` from a line-numbered edit that also replaced a comment, fixed). QEMU (KVM, SMAP and SMEP, Login) on an own image copy: `bt_rfcomm_test` 0 failed, with `HFPGateway` over ports: the headset sends `AT+BAC=1,2`, R5 sends `+BCS: 2`, the headset confirms, an mSBC link is asked, and closed when the microphone is let go; `bt_voice_test` and `gamecontroller_test` ok; no SMAP fault or panic. Host: the agent parts 0 failed; `fuzz-bt.sh hfp` 60 s: 27.5 million lines, 0 crashes, with and without codec negotiation. Not tested: the HCI commands on a radio. ARM64 and RISC-V 64 not built or tested.

## 2026-11-17 - Game controllers (Bluetooth step 8, option B): a Settings page to see and test controllers, and named buttons for games

Problem: Haiku has no place to see or test a game controller (the old Joysticks preferences do not show HID controllers), and it does not tell a program a controller's vendor and product ID, so every pad is a plain joystick with numbered buttons and no "A, B, X, Y". Plan and license check: `docs/GAME-CONTROLLERS.md`; owner decisions of 16 Nov 2026 (library name `libr5game.so`, branch from `main`, the mockup).

What changed:
- RenkuOS joystick fixes by atomozero (MIT, Haiku files): `244bf43292` (`BJoystick::Open()` frees the data of an earlier open; a controller that is turned off and on no longer adds up) and `823649eb6e` (hid_shared waits for the joystick update thread before freeing its data, and a joystick starts at zero, not with random values). Read from the RenkuOS repository; they match their descriptions.
- `R5_JOYSTICK_GET_INFO` in `<joystick_driver.h>` (an R5 snippet, `B_JOYSTICK_DRIVER_BASE + 82`, additive; the Be opcodes and structures are unchanged): bus, vendor and product ID, version, battery (HID Battery Strength when the report has it), the HID usage of each axis and the product name. usb_hid reads them from the device descriptor and the product string; bluetooth_hid gets them from bluetooth_server (new `R5_BTHID_SET_INFO`), which reads the SDP Device ID record (only a USB-IF vendor ID) and sends the device's name; i2c_hid from its HID descriptor; hyperv/hid answers with nothing known (it shares hid_shared and had to compile).
- `libr5game.so` (new, MIT, both images, `docs/LIBR5.md` "Game Kit", R5Book `game-kit.html`): `BGameController` with the SDL standard layout (`B_CONTROLLER_BUTTON_A` ... `_TOUCHPAD`, `B_CONTROLLER_AXIS_LEFT_X` ... `_RIGHT_TRIGGER`), raw values, `Name()`, `VendorID()`, `ProductID()`, `Connection()`, `Family()`, `BatteryLevel()`, the label choice per model, and `StartWatching()` with `B_GAME_CONTROLLER_ADDED` and `_REMOVED`; `BGameControllerView`, the controller picture drawn in code (four family shapes and one built from what an unknown controller reports; pressed parts fill with the accent color).
- The mapping database: SDL_GameControllerDB `gamecontrollerdb.txt` at commit `22de0d7ef9fe1e6f852a50a9403a7a48f9be30ad`, unmodified, with its `LICENSE` (zlib), in `/boot/system/non-packaged/data/game_controllers` of the regular image; credited in `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`. R5 reads its Windows (DirectInput) entries and sorts the driver's axes by HID usage, which gives DirectInput's numbering.
- `libr5icons.so`: `R5_GLYPH_CROSS` (8), `R5_GLYPH_CIRCLE` (9), `R5_GLYPH_SQUARE` (10), `R5_GLYPH_TRIANGLE` (11) (`docs/ICONS.md`).
- Settings → Devices → Game controllers (`settings_gamecontrollers`, regular image), after the owner-approved mockup (`docs/design/gamecontrollers-mockup/`): a card per controller, the live test with the button-label choice (Xbox or PlayStation labels), the vendor and product ID, and the shapes R5 draws. The page follows plug and unplug messages. Revert and Defaults work on the label choice. The Bluetooth page's "Test buttons…" opens this page; its own test window is gone.
- `gamecontroller_test` (regular image) and `GAMEPAD=1` in `distro/bluetooth/tests/run-qemu-smoke.sh`: a virtual controller with the DualSense's IDs and report order on bluetooth_hid, since QEMU has no game controller.

Mistakes on the way: the first QEMU run with `GAMEPAD=1` started the virtual controller before the Bluetooth tests, and `bt_hid_driver_test` then opened that controller instead of its own and failed; the controller now starts after the tests. The watcher only watched `/dev/joystick`, which does not exist before the first controller after boot, so a page opened before that never saw one come; it watches `/dev` too. The first `hey` moved Login's desktop window instead of the Sign in window.

Tested (QEMU, KVM, SMAP and SMEP on, own image copies, `scripts/build-locked.sh` jam_exit=0 four times):
- `gamecontroller_test`: parser ok; 872 of 872 Windows vendor and product entries of the installed database parse; the DualSense (`054c:0ce6`, "PS5 Controller") and the 8BitDo M30 (`2dc8:5101`, Sega shape) are found; a virtual controller with the DualSense's IDs and report order on bluetooth_hid reads back through `BGameController` as "DualSense test controller", `054c:0ce6`, Bluetooth, PlayStation, with a mapping, 6 axes, 14 buttons, 1 hat; Cross is A, the hat is d-pad down, R2 32703, L2 64, right stick X 32639; the label choice round trip; `B_GAME_CONTROLLER_ADDED` and `_REMOVED`; the four glyphs and the five shapes draw (1440 to 1703 line pixels each).
- The other Bluetooth tests of the smoke script still pass (`bt_server_smoke_test`, `bt_l2cap_abi_test`, `bt_hid_driver_test`, `l2cap_coc_test`, `bt_smp_crypto_test`, `bt_audio_smoke_test`, `bt_rfcomm_test` 0 failed). No panic and no SMAP line in the serial log.
- Settings `--page gamecontrollers` with `GAMEPAD=1`: the screendumps at 140 and 160 s show the page as in the mockup: the card "DualSense Wireless Controller, Bluetooth · PlayStation layout, No battery reported" (it appeared while the page was open), the Test box with "PlayStation (✕○□△)" chosen and "Vendor 054C · Product 0CE6", the PlayStation picture with Cross lit, L2 at 60 % and the left stick's dot lit, and the five shapes. Light mode, font size 12 only.

Not tested: a real controller (QEMU has no game controller and no USB passthrough here; VirtualBox not tried), the 8BitDo M30 and the DualSense, the battery of a real controller, USB vendor and product ID and product string from a real device, the SDP Device ID record of a real Bluetooth controller, the Sega shape's C and Z with the M30, dark mode on screen, ARM64 and RISC-V 64 (not built). The battery in the Bluetooth Deskbar item is not done: a follow-up after #4333 (HFP) is merged.

## 2026-11-17 - Bluetooth step 7c: a headset's microphone as a sound input (off until tested)

Problem: a headset's microphone works only over a SCO or eSCO voice link, and `h2generic` never ran the isochronous USB transfers that carry it (`BT_SCO_NOT_WORKING`; on the MT7922 a standing isochronous transfer once starved the HCI events). There was no SBC decoder for mSBC and no audio input for a headset.

What changed (docs/BLUETOOTH.md, step 7c, "As built"):
- Owner decision (16 Nov 2026): off by default. Only `bt_voice true` in `/boot/home/config/settings/kernel/drivers/h2generic` turns it on; the image ships no such file. With it off, the driver does what it did: no voice node, no extra thread, the event path only tests a flag, the attach still selects interface 1, alternate setting 5, and no isochronous transfer is queued.
- `h2generic` (`h2voice.cpp`, `h2voice.h`, `h2sco.h`, snippets in `h2generic.cpp` and `h2transactions.cpp`): with `bt_voice`, interface 1 rests at alternate setting 0; Synchronous Connection Complete (SCO or eSCO, status 0) starts isochronous transfers on the smallest alternate setting that holds the link (9 bytes per frame for transparent mSBC, 17 for 16-bit CVSD PCM), Disconnection Complete of that handle or closing the radio stops them. The isochronous packets become whole HCI SCO packets, every length checked; `/dev/bluetooth/voice/<n>` (uid 0, one opener, `<R5BluetoothVoice.h>`) reads and writes them. The old Haiku `sco_rx_complete()` stays unused: its buffer pool gives every transfer the same buffer.
- `bluetooth_audio`: an SBC decoder (`sbc_decoder.cpp`, MIT, from A2DP appendix B; SBC and mSBC), `Encoder::InitMSBC()`, the H2 framing of HFP 1.8 appendix A (`msbc.h`), and the input "Bluetooth headset microphone" (16 kHz mono float; CVSD's 8 kHz PCM is doubled). It sends silence back on the link and silence to the program while there is no link. Media's `DefaultManager` does not make it the default input by itself.
- `bluetooth_server/VoiceLink.h` (header only): codec choice after AT+BAC, the HFP 1.8 settings T2, T1, S4, S3, S1, D1, and the bytes of Setup and Accept Synchronous Connection. Not called yet: 7b (#4333) is not on `main`. What joins there is in docs/BLUETOOTH.md.
- Tests: `bt_voice_test` in the regular image and in `run-qemu-smoke.sh`, `scripts/test-voice.sh` (host), `scripts/fuzz-bt.sh sco` (new target, `scripts/bt-sco-fuzz.cpp`).

Limits: nothing ran on a radio. No voice link is opened until the HFP side joins. The isochronous path never ran, so whether the MT7922 keeps its events with a voice link up is still open. One voice link per radio, no packet loss concealment, no speaker path (the headset hears silence). The mSBC CRC is computed over the two reserved header bytes as sent; to be confirmed with a headset. ARM64 and RISC-V 64 were not built or tested.

Tested (17 Nov 2026):
- `scripts/build-locked.sh` (AMD64): jam_exit=0.
- QEMU (KVM, SMAP and SMEP, `run-qemu-smoke.sh` on an own image copy, `LOGIN=1`): every test exit 0, `bt_voice_test: ok` (mSBC round trip 43.8 dB, gain 0.995; SBC 44.1 kHz joint stereo 65.5 dB; the microphone listed as an input and running in media_addon_server with one 16 kHz mono float output; no voice node), `bt_audio_smoke_test: ok`; no SMAP fault, no panic. A second boot of a copy with `bt_voice true` planted: `h2generic: voice enabled (bt_voice)`, the same tests pass, no fault. QEMU has no radio, so neither boot ran a voice link or an isochronous transfer.
- Host (WSL, ASan and UBSan): `scripts/test-voice.sh` ok, `scripts/test-sbc.sh` ok (the encoder change keeps the A2DP results), `scripts/fuzz-bt.sh sco 60` (375600 rounds), `audio 60` and `hci 60`: 0 crashes.

Not tested: anything with a radio (no VirtualBox, no 5625U stick, no headset); ARM64 and RISC-V 64 not built.

## 2026-11-16 - Bluetooth step 7b: the headset's battery over HFP, its buttons and volume in the log

Problem: a Classic headset like the Sony WH-1000XM4 sends its battery level only over HFP, so My devices showed no battery for it. Its call buttons and volume reached nothing.

What changed:
- `bluetooth_server` is the HFP Audio Gateway (`HFPAgent.cpp`, `HFPGateway.cpp`, R5's own MIT code from HFP 1.8 and Apple's accessory guidelines). For a paired, encrypted device whose SDP record has a Hands-Free service, it opens that RFCOMM channel 5 s after the link (after A2DP and HID) and answers the service level connection: BRSF, CIND, CMER, CHLD, BIND/BIEV (battery as HF indicator 2) and the Apple battery (XAPL, IPHONEACCEV). Buttons (answer, hang up, redial, voice, dial, headset) and volume (VGS, VGM) go to the syslog; there is no call, so the call buttons get ERROR.
- `BT_MSG_R5_GET_PAIRED` "battery" carries the HFP battery of a connected headset, so My devices shows it. The Deskbar menu has a line per connected device with a battery, with the `R5_LEVEL_BATTERY` glyph (LE devices with the Battery Service too).
- Tests: the AT commands in `bt_rfcomm_test` (alone, and as a headset script over two RFCOMM links); fuzz target `scripts/fuzz-bt.sh hfp`.
- Fix from 7a: the RFCOMM files were also in the server's `DoCatalogs` list (a sed that matched twice); removed.
- After two independent reviews of this step:
  - Quitting no longer hangs on a headset that does not answer (it could take over a minute). `RFCOMMLink` takes a cancel flag, HFP stops before the radios go, and a `DeviceReady()` after `Stop()` adds nothing.
  - One failed attempt no longer means no battery until the next reconnect: a session without an SLC tries again twice, 10 s apart, while the device still has its link, and a reconnect during an attempt asks for one more.
  - Deskbar no longer shows an old battery when bluetooth_server is gone.
  - The AG no longer advertises extended error codes it never sends (features `0x0400`). A bare `AT` gets `OK`. An Apple battery report with more than 3 pairs is taken.
  - `bt_rfcomm_test` now drives `HFPGateway` itself (it copied its loop before).
  - R5Book (`bluetooth-kit.html`) and `docs/LIBR5.md` say where the battery value comes from.

Limits: no audio and no microphone (7c). No Audio Gateway SDP record and no RFCOMM server: R5 always starts HFP; a headset that wants to start it itself, or that checks the AG record first, may not talk. Volume is logged, not tied to the Media volume. Not tried with a headset.

Tested: `scripts/build-locked.sh` exit 0 (x86_64). QEMU (KVM, SMAP and SMEP, Login, `DESKBAR=1`) on an own image copy: `bt_rfcomm_test` 0 failed, with the headset script over two links (SLC, battery 80, a button answered ERROR, 9 commands seen by the gateway loop); all other Bluetooth tests exit 0; the Deskbar item attaches with the new paired-device poll; no SMAP fault, panic or crash. After the review fixes, again (build exit 0, QEMU with SMAP, Login and Deskbar): `HFPGateway` itself over ports: SLC and battery 80, one session per device, a retry after a failed connect, no connect without a link, giving up after the retries, `Stop()` during the 30 s open wait took 0 ms; `bt_rfcomm_test` 0 failed; `fuzz-bt.sh hfp` 60 s: 24.0 million lines, 0 crashes. The battery menu line is not drawn in QEMU (no device). Host: the HFP and RFCOMM parts of `bt_rfcomm_test` with ASan and UBSan, 0 failures. `fuzz-bt.sh hfp` 60 s: 27.3 million lines, 0 crashes; `fuzz-bt.sh rfcomm` 60 s: 2.17 million rounds, 0 crashes. ARM64 and RISC-V 64 not built or tested.

## 2026-11-16 - Graphics (M13 step 2): the compositor reads Skia's window images on the GPU, and the pipeline cache on disk

Problem: with `gpu_draw true`, every Skia run stopped the CPU until the GPU was done (`SyncToCpu::kYes`), read the window image back into memory (`asyncRescaleAndReadPixels()`) and copied it with `memcpy` into a second canvas (`WindowTarget::Deliver()`, the `WindowBuffer`), which the compositor then read as a raw buffer. Step 2 of `docs/M13-PLAN.md`, done before step 1 (owner, 16 Nov 2026), together with step 1's disk pipeline cache.

What changed:
- The compositor has two inputs. A window that Skia draws is sampled from its image: `compose.comp` (`texelFetch`, so the bytes are the same as from a buffer) and `effects.comp` (glass with `texelFetch`, the window animation through a `VkSampler` with `VK_FILTER_LINEAR`). Painter windows (Safe mode, `gpu_draw` off) keep the buffer path.
- The image (`VK_FORMAT_B8G8R8A8_UNORM`) is made and freed by app_server (`SkiaGpu::NewImage()`, `SkiaImage`); Graphite draws into it through `SkSurfaces::WrapBackendTexture()`. Skia windows get a `WindowBuffer` without memory: it keeps the size, the budget count and the eviction; an evicted window lets go of its image. After a resize the new buffer shows the old image until the new one is drawn.
- `SkiaGpu::RunWork()` submits with `SyncToCpu::kNo`; under the one queue lock an empty batch right after Graphite's submit signals a `VkSemaphore` (and a fence). The compose waits for the semaphores on the GPU and ends with a barrier so Skia's next run waits for the frame's reads. Every run leaves its images in `SHADER_READ_ONLY_OPTIMAL` (`Context::setMutableState()`). One run is on the GPU at a time: a run waits for the fence of the run before it, before its snap.
- Read-back stays for `compose compare` (a copy of each image in the frame's command buffer), offscreen bitmaps and `r5-draw-compare`. Screenshots read the composed screen, as before.
- Pipeline cache (separate commit): `ContextOptions::fPersistentPipelineStorage` in `/boot/system/cache/app_server/skia-pipelines` (app_server is uid 0, so not `~/config/cache`). The header has a key of the Skia version, device, `driverVersion` and `pipelineCacheUUID` (new with every Mesa build), the size and a checksum; a broken or foreign file is ignored. Written at most every 10 s and when the context goes, through a renamed new file.
- Unchanged: the M11 eviction and window memory limits, the direct-window pause and resume, the switch to the CPU mode after a failure, shadow, glass and animations, the GFX9 ring contract, the R5 GPU interface, `gpu_draw` opt-in.

Mistake on the way: the first build had no wait at all between runs. On Lavapipe the draw thread then sent 143-177 runs per 10 s instead of about 60, the composes waited behind them on the GPU, and the drag frame was 205 ms. The fence of the run before (commit "one Skia run on the GPU at a time") brought the runs back to 53-61 per 10 s.

Tested (QEMU, KVM, 8 virtual CPUs, 2 GB, Lavapipe, 1280x800, `gpu_draw`, `gpu_debug`, `gpu_compositor_lavapipe` true, `scripts/qemu-m13-measure.py` on own image copies, one after the other on the same host): `scripts/build-locked.sh` jam_exit=0.
- Before (`origin/main` with step 0, run `before2`): warm-up 10.8 s; drag frame 210.9 ms (p95 317.9, 94 frames), input 431.8 ms, draw 308.1 ms; scroll frame 210.8 ms (p95 349.9, 82 frames), draw 436.6 ms; resize no frames, input 440.3 ms, draw 13340.9 ms. (A first before run turned the compositor off with `fence timeout` during the warm-up; step 0's own run on another day gave drag 138.4, scroll 114.9, resize 112.8 ms, with another QEMU beside it, so the host differs between days.)
- After (this change, run `cold`): warm-up 10.9 s; drag frame 184.5 ms (p95 269.9, 164 frames), input 365.4 ms, draw 51.1 ms; scroll frame 192.8 ms (p95 258.8, 107 frames), draw 157.1 ms; resize frame 232.6 ms (14 frames), input 708.4 ms (p95 6881), draw 197.5 ms. The `compose` column now includes the GPU time of the Skia run the frame waits for, so it is not comparable with before; the frame and draw columns are.
- `compose compare ok` 8 times, no MISMATCH, also with shadow, glass and animations off (`r5-effects-test`) while a window drew every 16 ms, so the compare frames read the image copies. `r5-draw-compare` (classic look): `459/490 ok`, the same FAILs as the last recorded run, including the three `gpu failed`. No `gpu draw off`, no `compositor off`.
- Pipeline cache: the first boot wrote the file (`pipeline cache none`, then `stored`), a second boot of the same image loaded it (`pipeline cache loaded`). On Lavapipe the file holds only the Vulkan header (312 bytes): Lavapipe stores no pipeline data. The shorter warm-up of that second boot (7.7 s against 10.9 s) is not from this cache; Mesa's own shader cache in `/boot/home/config/cache/mesa_shader_cache` was also kept. The second boot's scenario did not start (`m13-measure did not start`, not looked into), so it has no frame numbers.

Not tested: RADV and the 5625U (QEMU has no RADV). The proof on RADV, the warm-up with a real pipeline cache, and #4324 go on the first M13 stick (step 3). ARM64 and RISC-V 64 not built.

## 2026-11-16 - Bluetooth step 7a: RFCOMM, the serial link headsets use for HFP

Problem: a headset like the Sony WH-1000XM4 sends its battery level only over HFP, and HFP runs over RFCOMM, a serial multiplexer on L2CAP PSM 3. Haiku's stack never had RFCOMM.

What changed:
- `bluetooth_server`: RFCOMM 1.2 in user space, R5's own MIT code from RFCOMM 1.2 and ETSI TS 07.10 (`RFCOMMMux.cpp`, `RFCOMMLink.cpp`). R5 connects as initiator: SABM on DLCI 0, PN asking for credit-based flow control (N1 127, 7 credits), SABM on the DLC, MSC both ways. The control channel answers PN, MSC, Test, FCon/FCoff, RPN, RLS, and NSC for unknown commands. Every wait has a timeout (20 s). Credits are only given for room in the 2048-byte receive buffer.
- `RFCOMMLink` is the internal API step 7b uses: connect to a channel, send, read lines, close.
- The Hands-Free channel comes from the device's SDP record (`R5FindRFCOMMChannel()`); SDP logs `hands-free service on RFCOMM channel <n>`, without the address.
- New test `bt_rfcomm_test` in the regular image and in `run-qemu-smoke.sh`; new fuzz target `scripts/fuzz-bt.sh rfcomm`.

Mistake on the way: the first host run of the test showed that a DM during PN (the other side has no such channel) was not seen as a refusal; the link would have waited for the timeout. Fixed before the first build. The first QEMU run had one failure: after a clean close, the link's reader saw its own transport shut down and reported "remote closed" instead of no error. The reader now stops without that when `Close()` ended the transport.

Limits: no profile uses RFCOMM yet (that is 7b), so a person sees nothing new. Not tried with a headset; the L2CAP connect follows the kernel's own timeout. No RFCOMM server and no SDP record of R5's own. One session per device.

Tested: `scripts/build-locked.sh` exit 0 (x86_64). QEMU (KVM, SMAP and SMEP, Login) on an own image copy with `run-qemu-smoke.sh`: `bt_rfcomm_test` 0 failed (frames, PN/MSC, SDP channel, two muxes back to back, two links over ports with 4000 bytes through 7-credit windows and a clean close, `Connect()` without a radio fails at once with "Network is unreachable"); all other Bluetooth tests exit 0; no SMAP fault or panic in the serial log. `scripts/fuzz-bt.sh rfcomm` 60 s: 1.79 million rounds (1.39 million with an open DLC), 0 crashes. Host: the framing, mux and SDP parts of `bt_rfcomm_test` with ASan and UBSan, 0 failures.

Not tested: a real headset (QEMU has no radio; 7b comes with a stick boot on the 5625U with the WH-1000XM4). ARM64 and RISC-V 64 not built or tested.

## 2026-11-16 - Bluetooth: the review's open points, battery, Use for sound, Test buttons

Problem: the stack review (#4322) left points open, and the device panel of the Bluetooth page had no battery, no way to send sound to a headset, and no way to try a controller.

What changed:
- Kernel: `R5ConnectionByType()` added at the end of the btCoreData module (the header is now in `distro`): LE `connect()` and fixed-channel `sendto()` take the LE link of a dual-mode device, not whichever link comes first. A connection leaves the list before its L2CAP endpoints are told, and LE `connect()` checks after binding that its connection is still listed, so a link that drops during `connect()` no longer leaves a freed pointer behind. `_SendFixed()` uses only the handle read under the list lock.
- bluetooth_server: the resolving list is rewritten with the scan paused (the controller refuses it while scanning), and a refused list command keeps the list marked for the next connect. Removed records are freed, and a device that is not paired keeps no record after its link (every unknown device that connected stayed in the list and the saved file). A full HID session table reuses an idle session (its old report descriptor freed) instead of refusing new devices. SDP runs on its own thread after authentication, on a record of its own; when it ends it stores the records under the devices lock and tells the profiles, or Encryption Change does when SDP was first.
- Battery: GATT discovery also finds the Battery Service and reads Battery Level once, then follows its notifications. `BT_MSG_R5_GET_PAIRED` adds int32 "battery".
- The page: a Battery line for a connected device that reports it; "Use for sound" for a headset (writes bluetooth_audio's choice for this person and sets "Bluetooth audio" as the Media output; off clears it and sets the first other output); "Test buttons…" for a game controller: a window with its buttons, sticks and D-pad, live through BJoystick.

Not done: resolving private addresses in the host. The MT7922 has LL Privacy (bit 9 in the LE event mask 0x1a5f of stick 5), so the controller does it; a radio without it still cannot reconnect a device with a changing address. Classic devices report no battery (that needs HFP or the Sony's own commands).

Tested: built for x86_64 (exit 0). QEMU with SMAP and Login: all Bluetooth tests ok (admin port, standard user refused, L2CAP, HID, CoC, SMP, audio), no panic; the Bluetooth page draws without a page scroll bar. QEMU has no radio: battery, Use for sound, Test buttons, the dual-mode and race fixes and SDP on its own thread are not tried with a device. Not on a stick.

## 2026-11-16 - Graphics (M13 step 0): one script for frame time, input latency and warm-up

Problem: M13 is the speed milestone, and step 0 of `docs/M13-PLAN.md` asks for one measurement before any tuning: frame time while dragging, scrolling and resizing, input latency, and warm-up, read from the syslog. The syslog had the compose and draw medians and the warm-up, but no frame interval and no input latency.

What changed:
- app_server (compositor, `gpu_debug true`) logs two more lines every 10 s, only when something moved: `frame interval median <us> p95 <us> frames <n>` (time between two frames on the screen, counted when they are less than 400 ms apart) and `input latency median <us> p95 <us> moves <n>` (from a mouse move, noted in `AccelerantHWInterface::MoveCursorTo`, to the frame that shows the damage that followed it within 400 ms). Logging only; nothing is drawn differently.
- `distro/graphics/boot/m13-measure`: waits for the drawing warm-up, places a Tracker and a Terminal window, then three motions of 30 s (drag, scroll, resize), by hand or driven from QEMU. It prints per motion the worst 10 s window of frame interval, input latency, compose median and draw median, plus warm-up and refresh period, and checks the targets (frame at most 16.7 ms; on Lavapipe compose at most 8 ms). `m13-measure numbers <dir>` does only the reading, also on Linux for stick logs; checked on M12 stick 2 boot 3 (compose 6.2 ms and warm-up 10.2 s, as in that boot's report).
- `scripts/qemu-m13-measure.py`: the QEMU run on an own image copy (never the shared one): plants the settings, the script and a UserBootscript with bfs_shell, signs in, drives the mouse over QMP following the steps on the serial port, and copies the results out.
- `docs/M13-PLAN.md` follows the owner's decisions of 16 Nov 2026: Skia is not on by default yet; that is the last step of M13 (step 5); step 6b (`BDirectWindow`, preferably also on RADV) moved into M13 or later; the first M13 stick proves #4324; the boot 1 `insert failed` was the page-table pool, not window memory.

Mistakes on the way: a bfs_shell `cat` in the session that planted the files kept the volume busy, the unmount failed and nothing was written (now read back in a session of its own, and the run stops when a file is missing). The first measured run used another session's app_server, because that session jammed the shared tree between my jam and my copy; the image is now copied while the build lock is still held (`--image`). With a 100 ms frame gap no frame was counted at all on this slow QEMU; the gap is 400 ms now. One run was lost when WSL restarted.

Tested: `scripts/build-locked.sh` jam_exit=0. One QEMU run (KVM, 6 virtual CPUs, 2 GB, Lavapipe, 1280x800, `gpu_draw`, `gpu_debug`, `gpu_compositor_lavapipe` true), with another session's QEMU running beside it: warm-up 10.5 s; drag frame 138 ms (p95 219), input 273 ms (p95 411), compose 138 ms, draw 161 ms; scroll frame 115 ms (p95 235), compose 105 ms, draw 214 ms; resize frame 113 ms (p95 319), input 223 ms (p95 302), compose 75 ms, draw 3.9 s. No target met. These numbers are far slower than earlier QEMU runs (draw median 9-10 ms on 12 cores), so they show that the script works, not a baseline; step 1 needs a run with the host otherwise idle.

Not tested: RADV and the 5625U (no stick). `m13-measure` is not in the `mesa_radv` package yet (with the next repack, for the first M13 stick). Scroll has no input latency, because the wheel moves no cursor. ARM64 and RISC-V 64 not built.

## 2026-11-16 - Admin requests: the sender is the one the kernel names (libr5admin.so)

Problem: Login (accounts, unlock grant, clock, time zone, driver blocking and driver settings) and bluetooth_server (the radio switch, bluetooth_audio's requests) decided by `BMessage::ReturnAddress().Team()`. That is the reply messenger the sender chose: any program, also a standard account's, could name Login's own team or another uid 0 team there and act as an administrator. Login's `sender_is_admin()` even took a message whose reply address was Login itself as local. Found by the stack review of #4322 (bluetooth_server) and then in Login's AccountAdmin.cpp; seen in the code, not exploited on R5.

What changed:
- New `libr5admin.so` (`distro/admin`, `<R5Admin.h>`, MIT, both images). A service reads admin requests from its own port; the kernel gives the uid, group and team of each port message (`get_port_message_info_etc()`). `r5_admin_send()` asks the service for that port, checks the port belongs to the service's team, sends, and waits with a timeout.
- Login: an admin port; account changes, unlock, relock, clock, time zone and driver requests only there. The normal messenger answers only the query. Grants are for the team the kernel names.
- Clients: libr5ui's administrator lock (Settings and every page in it, Time, Device Manager, Media and the other preferences), Users, libr5bt (Bluetooth switch), the Bluetooth Deskbar item, bluetooth_audio.
- bluetooth_server: `BT_MSG_R5_SET_RADIO` and `BT_MSG_R5_AUDIO_*` only on its admin port (administrator or grant; uid 0 for audio).
- Docs: `docs/LIBR5.md` ("Admin requests"), AGENTS.md (libr5admin beside libr5ui, libr5icons, libr5bt and libr5wifi: look there first), R5Book `admin-kit.html`.
- Smoke test: sets the switch through the admin port and checks the normal messenger refuses it; checks Login refuses a change on its normal messenger and answers on its admin port (`LOGIN=1` skips the first-boot prompt so Login runs). The job syncs every 5 s and writes `ps -a`, so a hang leaves a trace.

Mistakes on the way: the first QEMU runs hung in the new Login check. Not the admin calls: on a fresh image FirstBootPrompt runs instead of Login, and a BMessenger for the pre-registered Login waits until Login starts. The test now only talks to Login when its team runs.

Limits: Deskbar's Lock and Log out messages to Login are not admin requests and stay on the normal messenger. The sign-in itself (kAttemptLogin) is unchanged.

Tested: built for x86_64 (exit 0). QEMU with SMAP, first boot: all Bluetooth tests ok, Login check skipped. QEMU with SMAP and Login running (sign-in window shown): `admin port ok`, all Bluetooth tests ok, no panic. Not tested: unlocking in Settings by hand with a standard account, Users & Accounts changes, the 5625U.

## 2026-11-16 - Graphics (M12): VRAM kept for page tables, so Skia drawing stays on with a full VRAM

Problem: on the 5625U, M12 stick 2 (`generated/boot-5625u-51`), boot 1 (Skia on RADV) turned the GPU drawing off after about 3.5 minutes (`app_server: gpu draw off: insert failed`, while WebPositive opened). Boot 3 (Lavapipe) passed. The kernel log shows the cause: `refuse BO_CREATE vram full` at 188 s, then at 213 s `page table pool full` and three refused `VA_MAP`s (`pte status 0x80000000`, `B_NO_MEMORY`) for GTT buffers of 21 MB, 4 MB and 2 MB. Stick 1 of the same day (`m12-stick-measure-2`, boot 1) has the same lines. The VRAM to GTT fallback of `libdrm-2.4.124-3` covers buffers, not page-table pages: those are taken from the same VRAM pool (`vm_page_alloc()` in `gfx9_k12.cpp`), so with the pool used up by buffers no new table page could be made. Earlier this day I read stick 1's boot 1 as a VRAM fallback problem only; that was incomplete.

What changed:
- `R5_VM_RESERVED_VRAM` (8 MiB, Linux `AMDGPU_VM_RESERVED_VRAM`, v6.18 `amdgpu_vm.h:149`, "How much VRAM be reserved for page tables") in `r5_vm.h`, with `r5_vram_keeps_tables()`. `r5_gfx9_alloc_user_vram()` refuses a new buffer from the unused end of the pool when it would leave less than that; the R5 DRM library then places it in GTT, as for a full VRAM. Buffers that reuse freed VRAM are not affected.
- A file's address space holds up to 1024 table pages instead of 256 (the shared tree's size, `sVm`). Over all clients the stick asked for about 254 table pages (248 leaf tables plus 6 directory pages, counted from the `va map` lines), close to 256, so both limits are raised together. Only pages in use take VRAM.
- `page table pool full` now prints `used <n> of <capacity>`, so the next stick shows which limit is hit, if any.
- Host check in `scripts/test-r5-gpu.cpp` for `r5_vram_keeps_tables()`.

Limits: the code runs only on a GC 9.3 GPU, so QEMU does not run it. Not tested on the 5625U yet. GET_INFO still reports the full VRAM size (Linux subtracts the 8 MiB in `AMDGPU_INFO_VRAM_GTT`); RADV may plan for 8 MiB more VRAM than it gets, and that is the case the GTT fallback handles.

Tested: host check (`page table reserve`) passes; jam AMD64 (see the PR). `test-r5-gpu` still stops at its older `info lock` check, as before this change.

## 2026-11-16 - Graphics (M12): the Deskbar leaf, Skia's destination read removed, one queue lock

Problem: with Skia drawing on Lavapipe (`gpu_draw true`) the Deskbar's R5 leaf was missing or showed other pixels (QEMU 1 boot in 8; the 5625U stick boot 3 also lost labels and icons). The first brief tied it to window buffers that start transparent; one boot per condition was not enough for that, and it is not the cause.

Cause, measured in QEMU (12 cores, Lavapipe; details and screenshots in `generated/m12-leaf/README.md`): step 5c drew most icons, labels and fills with a direct R5 blender (`kR5BlenderSkSL`, `_BlenderPaint()`, `_SpecialPaint()`), which lets Graphite read the destination itself. On Vulkan Graphite reads it as an input attachment (`VulkanCaps::getDstReadStrategy()` = `kReadFromInput`). On Lavapipe that read did not always see the drawing before it when a run held many draws. With a 300 ms delay in every Skia run (test build) the leaf was lost in 3 of 3 boots; the leaf texture read back correct on the GPU each time, the copies on a Deskbar resize were right, and readbacks of different windows never shared memory. With the same leaf drawn through the copy of the destination (`_Composite`), the leaf was there in every frame, also with the delay. Mistake on the way: making a resize wait for the run in flight did not help and was taken out.

What changed:
- Option B (owner's choice: one path on Lavapipe and RADV): the direct blender is removed (`_BlenderPaint()`, `_SpecialPaint()`, `blender_effect()`, `coverage_effect()`, `kR5BlenderSkSL`, `kCoverageSkSL`). Its 8 call sites (bitmaps, text, fills, `blend_line32`, the linear-edge coverage) use the copy of the destination and the R5 shader (`kR5BlendSkSL`) again. `_FillSolid()`, `_DirectColor()` and the `kSrc` paths (vertical gradient fill, unscaled `B_OP_COPY` bitmap) stay. The draw profile line has no `blender` count any more.
- Text in the scratch image is drawn in its own colour and the R5 shader reads the coverage from the alpha, so Skia keeps picking the text contrast curve of that colour, as the direct text blender did (white scratch text would have picked the curve for light text). Shapes are drawn white, so for them alpha and red are the same.
- One lock for the `VkQueue`: Skia's runs moved to their own draw thread on this branch (`9d58e7bca4`), so Graphite and the compose thread both used the queue without a shared lock. Graphite's `vkQueueSubmit`, `vkQueueWaitIdle`, `vkDeviceWaitIdle` and `vkQueueBindSparse` are wrapped where `SkiaGpu`'s `get_proc` hands them out; the compositor's submit and its `vkDeviceWaitIdle` take the same lock (`SkiaGpu::LockQueue()`).
- The temporary `skia trace` lines of the buffer investigation are removed. `mesa_radv-25.3.6-9` repacked with this branch's `check` (`pack-radv.py --tools-only`; the packed `check` is byte-identical).

Tested (QEMU, KVM, 12 cores, Lavapipe, `gpu_draw`, `gpu_debug`, `gpu_selftest`, `gpu_compositor_lavapipe` true, a screenshot every 10 s after Sign in):
- Leaf present in every frame in 3 boots of this change (n1, n2, final); a fourth (n3) stayed at Sign in because the key came early while a build ran beside it, not a drawing fault.
- `r5-draw-compare` (classic look): `459/490 ok`; the last recorded run was `458/490`. The FAILs are the known groups (aliased text, 72-point subpixel text on clear, rotated, scaled and sheared bitmaps, pattern edges, two 3-pixel strokes, clip-region inner diff 1), plus three `gpu failed` (`gradient-linear-roundrect`, `transform-rotate30-bitmap/opaque`, `clip-picture`): `Finish()` did not complete in time while the desktop session ran beside the compare. Not looked into further.
- Cost on Lavapipe: the desktop's draw median after Sign in was 8.9-10.0 ms against 7.6-9.4 ms before (3 boots each, few samples). In `r5-draw-compare --time` (cut off at the end of the boot) fills and strokes take about 0.25-2.5 ms on the GPU path, the `B_OP_ERASE`, `B_OP_INVERT`, `B_OP_ADD`, `B_OP_SUBTRACT` and `B_OP_BLEND` cases 7-30 ms; there is no run of the old build for the same table.

Not tested: RADV and the 5625U (no stick); whether RADV had the same input-attachment fault is not known. The new text colour in the scratch image is checked by the compare set only in the classic look; the new look was not looked at on a sheet. ARM64 and RISC-V 64 not built.

## 2026-11-16 - Bluetooth: review of the whole stack (kernel, server, library, audio)

Problem: four independent read-only reviews of the Bluetooth stack after #4321 (kernel L2CAP/HCI/drivers, bluetooth_server, libr5bt and the page, bluetooth_audio). The stick 5 headset (Sony WH-1000XM4) paired, but its LED kept blinking and no sound came.

Why the headset failed (from the stick 5 syslog and the code): `rd->handle` was never cleared when a link ended, and the controller gives the next link the same number. After the 8BitDo the headset got the 8BitDo's record for Authentication Complete, SDP and Encryption Change: SDP went to the 8BitDo's address ("service records not read"), the 8BitDo's cached HID record was replayed ("report descriptor, 86 bytes"), and the headset never showed as encrypted, so bluetooth_audio refused it. Nothing opened AVDTP either without a `bt_audio use` choice, so the headset dropped the link and stayed in pairing mode.

What changed:
- bluetooth_server: a record's handle starts and ends at "none" (0xffff) and only a connected record matches a handle; an incoming connection no longer adds a known device a second time; SDP replies are checked against what arrived (a remote continuation length up to 255 overflowed a 17-byte stack buffer in a uid 0 process; the attribute length read past the heap buffer); a new SDP answer replaces the device's old records; testing commands (OGF 0x06, including Simple Pairing Debug Mode) are refused from clients; with the switch off LE does not scan, connect or advertise through raw commands; a refused raw command no longer unregisters the radio; a Debug Combination Key is not stored; an LE pairing connect ends after 30 s, and Pair on a second device gives the first its result; a new LE pairing replaces an older bond of the same identity address.
- Kernel: an L2CAP channel whose link is lost (and every other path that closes it) leaves the channel table, so a remote Disconnection or Configuration Request can no longer reach a closed channel and answer on a NULL connection (kernel panic); an LE bind (fixed channel, LE PSM) is released on close; an ACL frame the driver refuses is freed; Disconnection Complete is checked for size and status; a failed header prepend no longer dereferences NULL; LE PSMs 0x01 to 0x7F need uid 0; a Disconnection Response needs both CIDs to match.
- bluetooth_audio: without a `bt_audio` choice it connects to a paired Audio/Video device (connected one first); one log line when it listens; a failed Discover ends the session; the media header and the clock count the frames really sent.
- libr5bt: `BluetoothView` is 32 bytes at its tail again (`_reserved[2]`; #4321 made it 8 bytes bigger); "Pairing…" always ends (early returns, a refused request, a stopped server); a refused radio change sets the check box back; the failure note clears.
- gamepad: axes start where they rest, hats (the D-pad) are shown, it stops when the controller goes away.

Open (not fixed here): the sender checks of BT_MSG_R5_SET_RADIO and the audio messages trust the reply address a program chooses (needs the sender's identity from the kernel or Login, an owner decision); raw HCI commands from any local program (Haiku's design) are only narrowed; no host-side RPA resolution without LL Privacy; the resolving list is written while scanning; unpaired remote records and Classic HID sessions are never freed; SDP still runs on the event thread; LE lookups ignore the link type for dual-mode devices; the connection pointer in LE connect() has no reference; bluetooth_audio needs "Bluetooth audio" as the Media output to run at all.

Tested: built for x86_64 (exit 0); QEMU with SMAP: server smoke, L2CAP ABI, HID driver, CoC, SMP crypto and audio smoke ok, the audio node listens on PSM 0x19 and 0x17, no panic. Not on a stick: the headset fix is unproven until a boot on the 5625U.

## 2026-11-16 - Bluetooth: My devices with a device panel, the switch on the last line

Problem: the Bluetooth page only showed what a scan found. A paired mouse or controller was not listed unless it was found again, there was no way to see whether it was connected, and Forget sat beside Pair in the scan list. The radio card and the switch box stood above the devices, though they are rarely used.

What changed (owner's mockup, `generated/mockups/bluetooth-page.html`):
- `bluetooth_server`: `BT_MSG_R5_GET_PAIRED` lists the paired devices of every radio (Classic with a link key, and LE bonds) with name, class, LE, HID and connected; no keys. `BT_MSG_R5_DISCONNECT` ends a link and keeps the keys.
- libr5bt (additive): `BluetoothPairedDevice`, `BluetoothState::UnarchivePaired()`, `kMsgBluetoothDisconnect`. The controller reads the paired list on every 3-second update, and at once after Pair and Forget. `BluetoothState` keeps its size; the controller and view use reserved room.
- The page: My devices (a list with Connected or Not connected, and a `BCard` panel with kind, status, Classic or Low Energy, address, Disconnect and Forget); Devices nearby without the paired devices, down to the last line; that line without a box: "Bluetooth on", the radio or a note. While Bluetooth is off both lists are faint. The summary card with the radio count is gone.

Limits: no battery level (not read yet), no "Use for sound" or "Test buttons" in the panel yet. A disconnected LE mouse comes back when it is moved, by design (the accept list).

Tested: built for x86_64; QEMU with SMAP, all Bluetooth tests ok, no panic; the page draws My devices, Devices nearby and the "Bluetooth on" line (no radio in QEMU, so the lists stay empty). Not on a stick.

## 2026-11-16 - Bluetooth: Forget a paired device, see a controller's buttons, Pair on a paired device ends well

Problem: stick 5 on the 5625U: the 8BitDo M30 and the Designer Mouse paired and published their devices (`joystick/bluetooth/0`, `input/mouse/bluetooth/0`), and no panic came. Three things were left. Haiku's Joysticks preferences said "The file '0' used by 'bluetooth/0' cannot be found" and Probe found nothing: they look for a gameport description file and never show a HID controller (USB or Bluetooth), so they cannot tell whether the M30 works. Pair on a device that already had a key said "did not work", because only Simple Pairing Complete ended a pairing well and a stored key does not send one. And there was no way to unpair.

What changed:
- `gamepad` (in `/bin`, regular image): a monitor like a MIDI monitor. It lists the controllers, opens the first through `BJoystick` as a game does, and prints each button and axis change until Ctrl+C (or for the seconds given).
- `bluetooth_server`: Authentication Complete with a stored key ends a pairing started on the page as "Paired with …".
- Forget, beside Pair on the Bluetooth page: `kMsgBluetoothForget` (libr5bt, additive) and `BT_MSG_R5_FORGET` in `bluetooth_server` remove the device's Classic link key or LE keys (le_paired_devices, accept list) and end its link; the status line says "… is forgotten. Pair it again to use it." or "… was not paired.". Like Pair, it needs no administrator lock. It works on a device in the scan list; a list of paired devices is not on the page yet.

Tested: built for x86_64; QEMU with SMAP, all Bluetooth tests ok, no panic. Not on a stick (owner: no sticks for now). Open: the Sony headset paired on stick 5 but gave no audio, to be looked at separately.

## 2026-11-16 - Bluetooth: no kernel panic when a BLE mouse is used after pairing

Problem: stick 4 on the 5625U: the 8BitDo M30 and the Designer Mouse paired ("Paired with Designer Mouse" on the page), the mouse moved, and a few seconds later the kernel panicked: `PANIC: expected to be contiguous` in h2generic `nb_get_whole_buffer()` from `submit_tx_acl()`, `PostACL()` and the L2CAP send thread. The small LE signalling answers (here the Connection Parameter Update Response a mouse asks for after it connected) were built with 2 to 10 bytes of header room, so the L2CAP and ACL headers went into separate pieces, and the USB transport panics on a buffer in pieces. The syslog of that boot was lost with the panic; the backtrace came from a photo of the debugger.

What changed: the LE signalling answers get 128 bytes of header room, as the Classic commands; `PostACL()` copies any frame in pieces into one buffer before it goes to the transport, so no path can panic there again.

Tested: built for x86_64; QEMU with SMAP, all Bluetooth tests ok (also `bt_audio_smoke_test`), no panic. Not on a stick yet. Open from the same boot: the M30 paired but gave no input in Joysticks; its log was lost with the panic.

## 2026-11-16 - Bluetooth audio: review of #4317 (reconnect, SDP, encrypted links, SBC endpoint, 44.1 kHz only)

Problem: an outside read-only review of #4317 found these; each was checked against the code here first:
- **Reconnect never worked.** The L2CAP socket only uses a link that is already up (`btCoreData` has no Create Connection), so reaching the chosen headset always failed with `ENETUNREACH`.
- **SDP never answered.** `SDPServer` read requests and never replied, so a headset that looks first hung until its timeout.
- **Address alone was trusted.** An incoming AVDTP channel was accepted on its address, without checking that the link was encrypted.
- **AVRCP thread could hang.** It read without a timeout, and `shutdown()` does not wake an L2CAP `recv()`. `_EndSession()` also let go of its lock while the session still looked open.
- **LE scans kept running** while audio streamed.
- **Node values were not thread-safe**: volume and latency were written from A2DPSource's threads without atomics.
- **`Start()` did not clean up** after a failure.
- **Only the first sink endpoint was asked** for SBC. Headsets often list AAC, aptX or LDAC there.
- **A smaller MTU ended the stream** (`EMSGSIZE`).
- **AVRCP GetCapabilities with Events was refused.**
- **A headset could choose 48 kHz**, because R5 offered every rate. The 44.1 kHz audio then played 9 % too fast, with gaps.
- **Clock drift** made the ring jump ahead by up to 80 ms, which is audible.
- **Control sends could block** the node's thread on a full AVCTP queue.

What changed:
- `bluetooth_server` (SPDX snippets):
  - `BT_MSG_R5_AUDIO_CONNECT` (uid 0): Create Connection for a paired headset, Classic only, with no pairing window. The stored key authenticates it and encryption follows as for every link.
  - `BT_MSG_R5_AUDIO_PAIRED` also answers connected and encrypted.
  - `SDPRecords.h` (MIT, new): fixed records for A2DP Audio Source (AVDTP 1.3, PSM `0x19`), AVRCP Controller (category 2) and AVRCP Target. ServiceSearch, ServiceAttribute and ServiceSearchAttribute are answered, with continuation; anything else gets an empty list or an error. `SDPServer` answers every request.
  - `LEManager::AudioChanged()` pauses and resumes the LE scan with the stream.
- `bluetooth_audio`, all MIT:
  - The link is brought up through the server and R5 waits up to 15 s for encryption.
  - A connection is accepted only from a paired device on an encrypted link, waiting up to 5 s for encryption.
  - Every free sink endpoint is asked in turn.
  - Only 44.1 kHz is offered; a Set Configuration at another rate gets `0xC4`.
  - The AVRCP thread reads with a 200 ms timeout and is joined by the thread that started it.
  - `Start()` cleans up after a failure; without AVDTP it fails, without AVCTP it only logs.
  - On `EMSGSIZE` the next packets carry fewer frames.
  - Drift costs one sample per packet; a jump happens only after a stall over six times the target.
  - AVRCP Events gets an empty list, and an unknown capability gets "invalid parameter".
  - Control sends use `MSG_DONTWAIT`, and the node's volume and latency are atomic.
- New fuzz target `sdp` in `scripts/fuzz-bt.sh`.

Not changed: the encoder's speed (finding 3.2). It does about 2 million operations per second, less than 0.1 % of one core, so the checked encoder stays as it is.

Limits: still no headset tried. The reconnect, the SDP records with a real headset, and encryption on the 5625U are not shown on hardware. Play and pause do not reach the playing program. A Classic inquiry that already runs when the stream starts is not stopped. ARM64 and RISC-V 64 were not built or tested.

Tested: `scripts/build-locked.sh` jam_exit=0; `scripts/test-sbc.sh` ok; `fuzz-bt.sh sdp 10` (283 678 rounds) and `audio 10` 0 crashes, including the check that an SDP answer pieced together over its continuation states equals the answer in one piece and is well formed. Not run for this change: the QEMU smoke test (the owner tests it on the stick instead) and the 60 s fuzz runs.

## 2026-11-16 - Bluetooth: LE packets with the right boundary flag, a reset at start, the link's features before pairing

Problem: a third read-only review of the three stick boots on the 5625U (MT7922) found more below the pairing: every LE packet from the host started with Packet_Boundary 10, which is for BR/EDR only; the LE buffer length was never used (the radio id, 124, was taken as a table index of 8); R5 never sent HCI Reset, so a warm boot from Windows kept the radio's settings; SMP went out the moment an LE link came up, before the link layer's own feature exchange that BlueZ waits for; and two Classic ends (a refused Create Connection, a disconnection during pairing) still told nobody.

What changed: PB 00 for LE starts in `PostACL()`; the LE buffer slot is the id minus `HCI_DEVICE_INDEX_OFFSET`; `bluetooth_server` resets a radio when it is added and then runs the setup; an LE link runs LE Read Remote Features first and pairs or encrypts when it completes (or is refused); the IO Capability Response is handled without a petition; the two Classic ends give the pairing result.

Tested: built for x86_64; QEMU with SMAP, all Bluetooth tests ok, no panic. Not on a stick yet. ARM64 and RISC-V 64 not built or tested.

## 2026-11-16 - Bluetooth: headphones and speakers (A2DP with R5's own SBC, AVRCP), step 5

Problem: R5 had no Bluetooth audio. Step 5 of `docs/BLUETOOTH.md`: an A2DP source with a royalty-free SBC encoder written by R5 (no third-party codec, owner's choice), AVRCP play, pause and volume, an audio output choice per person, a real-time audio thread and no scans while it streams.

What changed:
- `distro/media/bluetooth_audio/` (MIT, new): `bluetooth_audio.media_addon` with one output, "Bluetooth audio" (buffer consumer, physical output, Volume parameter), float stereo at 44.1 kHz from the mixer. `sbc_encoder.cpp` from the A2DP 1.3.2 appendix B only, float analysis. `a2dp_proto.h`: AVDTP 1.3, the SBC element, the RTP media header, AVCTP and AVRCP, parse and build, plain C++ so the host fuzzer builds it. `A2DPSource.cpp`: signalling as initiator and acceptor on PSM `0x19`, media channel, AVRCP on `0x17`, the `B_REAL_TIME_PRIORITY` send thread. Choice of setting: joint stereo, 16 blocks, 8 subbands, Loudness, the highest bitpool the headset allows within A2DP's 512 kb/s and one 672-byte packet (86 if allowed, else the headset's maximum, usually 53); one retry at 53 if the headset refuses. Delay reporting goes into the node's latency (video sync). After 5 s of silence the stream is suspended.
- Each person's headset in `~/config/settings/Media/bluetooth_audio`. The add-on (uid 0 in media_addon_server) reads the file of the account in `r5-desktop-session` and never writes it; the new command `bt_audio` writes it with `find_directory()`. With no choice, only a device bluetooth_server calls paired may connect.
- `bluetooth_server` (SPDX snippets): `BT_MSG_R5_AUDIO_STREAMING` (Inquiry and Periodic Inquiry refused while the sending team lives) and `BT_MSG_R5_AUDIO_PAIRED`, both uid 0 only.
- Mistake in the step brief, corrected: `bluetooth_server` did not listen on PSM `0x19`; the add-on listens itself, and `bt_l2cap_abi_test` accepts `0x19` held by bluetooth_audio.
- Tests: `scripts/test-sbc.sh` (host, ASan and UBSan), fuzz target `audio` in `scripts/fuzz-bt.sh`, `bt_audio_smoke_test` in `run-qemu-smoke.sh`.

Limits: no headset tried; Classic pairing on the 5625U (MT7922) was reworked in #4314 and is not yet confirmed with a headset, so the Sony WH-1000XM3 and XM4 lines of the pass rule stay open. No SDP record for A2DP Source or AVRCP (R5's `SDPServer` answers nothing yet), so absolute volume may not be offered by a headset that checks it. Play and pause on the headset start and suspend the stream; the playing program is not paused. No headset choice on the Bluetooth page yet (`bt_audio` only). The link's encryption is not checked before audio. Packets stay within the default L2CAP MTU of 672 bytes because the socket does not report the headset's. A scan already running when the stream starts is not stopped. ARM64 and RISC-V 64 were not built or tested.

- `DefaultManager` (SPDX snippet): media_addon_server starts every physical output, so the Bluetooth node is live from boot; it is never taken as the default output only because it was found first, only when it was chosen in Media.

Tested: `scripts/test-sbc.sh` passes. Table 4.7 lengths and bit rates, the header and CRC against a table CRC, a tone lands in its subband, and a decoder written from the same appendix reads every frame back: 59 dB SNR at bitpool 53 and 66 dB at bitpool 86, with the known SBC delay of 73 samples (8 subbands) and 37 (4 subbands). `scripts/fuzz-bt.sh audio 60`: 60 s, 12.5 million rounds, 0 crashes. `scripts/build-locked.sh`: jam_exit=0. QEMU with SMAP (`run-qemu-smoke.sh`, own copy, no radio): the add-on is listed as an audio output, `bt_l2cap_abi_test` sees PSM `0x19` held by bluetooth_audio, bluetooth_server answers both audio messages, and `bt_audio use`, show and `off` work; no SMAP or panic line. Mistake: `bt_audio_smoke_test` failed twice: it did not find the live node and could not start a second one. The node had not added `B_PHYSICAL_OUTPUT` to its own kinds, so the live node was not seen as an output (in Media either). After the fix the third run passed: media_addon_server runs one node, it listens on PSM `0x19` and `0x17`, and the Volume parameter reads back 42 %. The check "not the default output unless chosen" did not run: QEMU reported no audio output to compare with. No kernel code changed. After merging main (Low Energy, #4314): `build-locked.sh` jam_exit=0, the QEMU smoke run with SMAP passed every test (also `l2cap_coc_test` and `bt_smp_crypto_test`), `fuzz-bt.sh le 60` and `audio 60` 0 crashes.

## 2026-11-16 - Bluetooth: the page says when pairing runs, works or fails; LE connects with the report's address type

Problem: pressing Pair showed nothing: no progress, no result; an LE connect refused by the controller sent no notice either. A second read-only review of the three stick boots also found that the LE address type from the scan (0 to 3) was collapsed to public or random, and that the legacy fallback after extended commands is refused by specification (0x0C).

What changed:
- The Bluetooth page (`libr5bt.so`): after Pair the status line says "Pairing with …" and Pair is disabled; then "Paired with …" or "Pairing with … did not work." until the next scan. New notification for the target: `kMsgBluetoothPairResult` (additive; the view takes its two fields from its reserved room, so its size stays the same).
- `bluetooth_server`: one result for every end of a pairing started on the page, Classic and LE (`BT_MSG_R5_PAIR_RESULT` to the page, plus the notification, with "notice sent/not sent" in the syslog), including a Classic connection that never came up and an LE connect the controller refused.
- LE: Extended Create Connection gets the report's address type unchanged; one try, no legacy fallback; the pairing log line has the raw address type, the Flags AD and the primary PHY. Classic: the IO capability reply logs its three bytes.

Tested: built for x86_64; QEMU with SMAP (Settings Bluetooth page open, all Bluetooth tests ok, no panic); `fuzz-bt.sh le`: 0 crashes. The status line, the results and the LE connect need the stick; not tested there.

## 2026-11-16 - Bluetooth: Classic pairing answered with an IO capability BR/EDR has (third stick boot)

Problem: on the third stick boot the radio refused our IO Capability Request Reply with 0x12 (Invalid HCI Command Parameters), and every Classic pairing ended in 0x37. We sent KeyboardDisplay (0x04), an LE-only value; BR/EDR takes 0x00 to 0x03 (Core 6.0, Vol 4, Part E, 7.1.29). This was in step 4 since it was written.

What changed: `bluetooth_server` answers DisplayYesNo for BR/EDR, as BlueZ: a keyboard still gets Passkey Entry with the number shown here, a phone Numeric Comparison, a gamepad Just Works. The same boot: the outside diagnosis's changes (MediaTek low power setting, SSP and Secure Connections host bits) are in and logged clean; LE still ends in 0x3E (open, a second read-only review is asked); the legacy connect after extended commands is refused with 0x0C, as allowed.

Tested: built for x86_64. Not on a stick yet.

## 2026-11-16 - Bluetooth: first two stick boots of Low Energy, pairing windows and notifications

Problem: the first two stick boots of PR #4314 on the 5625U (MT7922) showed the LE setup and scan working (`le event mask 0x1a5f`, extended scan; the Designer Mouse, the 8BitDo M30 and phones in the list), but no pairing: every LE link to the Designer Mouse dropped in the same second with `0x3E`, and the Classic M30 ended in `0x37` (Simple Pairing Not Supported by Host). Haiku's "Incoming Connection" window showed at every connection, also our own Pair, with a broken name.

What changed:
- Between boot 1 and 2: Classic reads Read Remote Extended Features page 1 before Authentication Requested (BlueZ's order); LE uses BlueZ's connection parameters (30 to 50 ms, 420 ms timeout, full scan window), no PHY preference, two retries after `0x3E`, Pair pressed again while one runs is ignored; more steps in the syslog. Boot 2: Classic now gets to the IO Capability Request and fails with `0x37` right after our reply; LE unchanged. The cause is being investigated in a separate read-only session (both failures open).
- After boot 2: the "Incoming Connection" window is gone; the end of a pairing started here is a notification (`PairingNotice.cpp`, BNotification: "Paired with ..." or "Pairing with ... did not work"), Classic and LE. Numeric Comparison shows the six digits with Yes and No (Classic when the device has DisplayYesNo or KeyboardDisplay, LE Secure Connections); before, it was confirmed without showing them. LE Passkey Entry where the device shows the number has a window to type it (was refused). SMP and ATT procedures without an answer end after 30 s and close the link, checked every 5 s.

Tested: built for x86_64 (`build-locked.sh`); QEMU smoke with SMAP (no radio). The windows, notifications and timeouts need a real device; not tested. ARM64 and RISC-V 64 not built or tested.

## 2026-11-16 - Bluetooth steps 1c and 6: Low Energy links, LE Secure Connections pairing, BLE HID over GATT

Problem: R5 had no Bluetooth Low Energy. The owner's Microsoft Modern mouse is BLE, so it could not be used; the owner chose BLE before audio (docs/BLUETOOTH.md, step 4, "Order after this step").

What changed (on top of the step 4 fixes, PR #4311):
- Kernel, step 1c: `btCoreData` (now in `distro/`) registers LE links from the LE Meta connection events and keeps the LE buffer length; `PostACL()` splits LE packets by it. `l2cap` serves the LE signalling channel (Connection Parameter Update, LE and enhanced credit-based connections, credits, reconfigure), fixed-channel sockets for ATT and SMP (uid 0, `<R5BluetoothLE.h>`), and credit-based channels with K-frame segmentation and reassembly (EATT's mode on PSM 0x27 included).
- `bluetooth_server`, step 6 (`LEManager`): LE Meta in the Event Mask and the LE setup after every reset with only the supported bits; LE scan (extended or legacy) in the existing scan list; Pair makes an LE link and pairs over SMP (Secure Connections with ECDH P-256 through OpenSSL `libcrypto`, Just Works, Numeric Comparison, passkey shown here; legacy pairing for older devices); keys in `/boot/system/settings/bluetooth/le_paired_devices` (0600, never logged); paired devices come back through the controller's accept list; a GATT client finds the HID service and feeds the `bluetooth_hid` driver of step 4, so a BLE mouse or keyboard goes to input_server.
- Fixed on the way, found in review (two passes: own and an independent one): a deadlock between the HCI receive thread (holds the device list lock while it delivers up) and any thread that sent while holding an L2CAP endpoint lock; this also hit Classic. L2CAP now sends through one send thread (`l2cap_tx.cpp`) that calls `PostACL()` without L2CAP locks. `AddConnection()` added a known connection to the list a second time; a packet was matched to the first link of its address instead of its own link, and a device on one link could close, reconfigure or feed a channel of another link (now refused); closing a socket bound but never used asked an identifier of a NULL connection; a full receive queue leaked a buffer; `_SendQueued()` wrote into a buffer `PostACL()` already owned; `htole32` was the 16-bit swap; a forged answer to a command we never send could take the identifier of a waiting connect(); SMP keys that arrive before the Encryption Change event were dropped; a dual-mode device found by the Classic inquiry now pairs Classic; the resolving list is written again after a new pairing; LE Command Complete answers that the kit also waits for (Read BD_ADDR) go to both.
- Mistake during the work: the LE sockets were bound with a zero address, but Haiku's `BDADDR_ANY` is ff:ff:ff:ff:ff:ff, so the first QEMU run showed `le channels missing`; fixed.

Limits: nothing ran with a real radio or LE device; the pass rule (`smp sc pairing ok`, a BLE mouse moving) is open. Numeric Comparison is confirmed without showing the number (as Classic, step 4), and the stored key does not record that; Passkey Entry where this computer must type the device's number is refused; no SMP timer (a stuck pairing may restart after 30 s); no ACL flow control by Number of Completed Packets (an existing gap, now more visible with credits); the HID report layout is read again on each connection; subrating not asked; bluetooth_server does not open EATT channels itself; the ISO endpoints of `h2generic` are not in this step; a device with both a BR/EDR and an LE link at once is sent to by address (`sendto`, `connect()`), which finds the first link.

Tested: built for x86_64 with `scripts/build-locked.sh`; QEMU with SMAP (`run-qemu-smoke.sh`, own `BT_WORK`), no radio: `bt_smp_crypto_test: ok` (every SMP function against the sample data of Core 6.0, Vol 3, Part H, Appendix D; ECDH with the debug keys, a point off the curve refused), `l2cap_coc_test`, `bt_server_smoke_test`, `bt_l2cap_abi_test`, `bt_hid_driver_test`; `scripts/fuzz-bt.sh l2cap 60`, `le 60` and `hci 60`: 0 crashes. Manual SMAP review: no new user pointer in the kernel. ARM64 and RISC-V 64 not built or tested.

## 2026-11-16 - Documentation: R5Book on disk, linked from the welcome page

Problem: the R5Book (developer guide, `docs/r5book`) was only in the repository. The welcome page in WebPositive linked the Be Book and the Haiku Book, not the R5Book.

What changed:
- `scripts/apply-welcome.py` (called by `apply-overlay.sh`) copies `docs/r5book` to `data/r5/r5book` in the Haiku tree on every apply; the haiku package installs it in `/boot/system/documentation/r5book/`. So an R5Book change is in the next build without a second copy. The image copy has no Google Fonts link (the CSS falls back to the system font offline), the `LIBR5.md` and `ICONS.md` links go to GitHub, and the two pictures from outside `docs/r5book` (`r5-symbol.svg`, `people.svg`) are copied into its `img/`.
- `distro/welcome/welcome_en.html` is Haiku's page from `haiku_welcome-2026_08_11-1`, unchanged except one list item "The R5Book" under "Getting to know the API" (SPDX snippet) and a note in the header. It is in the haiku package, a system package, so packagefs gives it precedence over the same file in `haiku_welcome` (`PackageNode::HasPrecedenceOver`). The rest of Haiku's welcome guide, its style, the other languages, the Quick Tour and the bug-report page stay as Haiku ships them.

Limits: only the English page links the R5Book; the other languages are Haiku's. The page still names Haiku as the product and sends bug reports to Haiku's tracker: an R5 welcome guide was tried on `claude/r5-welcome` (not merged) and set aside to be done separately. The minimum image gets the R5Book too (same haiku package, about 0.5 MB). Not checked in WebPositive yet whether the R5Book's CSS (variables, grid) shows well.

Tested: `scripts/build-locked.sh` (exit 0). `package list` of the built `haiku.hpkg` shows `documentation/r5book/` (the pages, `r5book.css`, `r5book.js`, `img/` with `r5-symbol.svg` and `people.svg`) and `documentation/welcome/welcome_en.html` with the R5Book link; no Google Fonts link is left in the extracted pages. Not booted: no QEMU or hardware test, so WebPositive showing the link and the R5Book is not proven.

## 2026-11-16 - Build: after another worktree built in the shared tree, the next one's files are built again

Problem: a build of `claude/vram-fallback` failed to link `libr5wifi.so`, `libr5bt.so` and `r5_level_glyph_test` (`undefined reference to r5_level_glyph_draw`), although the source in the tree had that function. The applies copy with `shutil.copy2`, which keeps the source file's modification time. A bluetooth worktree had built `R5Icons.o` in the shared tree at 10:53; the next apply put back the other worktree's `R5Icons.cpp` with its checkout time of 10:40. The object looked newer than the source, so jam kept it, and `libr5icons.so` came out without the function. Any file can be hit this way, also without a link error (an old object that still links).

What changed: `scripts/tree-owner.sh end` gives every path this apply wrote the time of now when the previous apply came from another worktree. With the same owner nothing is touched, so a build stays incremental. New host test `scripts/test-tree-owner.sh`: two worktrees in a small git tree, the second one "builds" in between, then the first applies its older file again.

Limits: after a change of owner every file of that worktree's apply is compiled again once (the price of not keeping an object of other sources). An object built before this change can still be stale in the tree until its worktree applies again after another owner; a clean rebuild of a target removes it.

Tested: `scripts/test-tree-owner.sh` prints `ok tree owner` with the new script and fails with the old one (`B's object is newer than A's source`). The case that led to it: after `touch` of `distro/icons` the same build linked (`jam_exit=0`, comment on #4307). Not tested: a full build through `apply-overlay.sh` with this script.

## 2026-11-16 - Graphics: second review of the VRAM fallback (system-wide GTT limit, exports counted, span take, atomic log bits)

Problem: two outside read-only reviews of #4307 (round 2 of the VRAM fallback) found these, each checked against the code here:
- The per-file GTT limit left out a file's exports whose buffer was already freed (an export keeps the locked area until it is imported or runs out; up to 32 per file), buffers that are parked (freed while a job runs, owner NULL, up to 10 s on a hang), and several open files could together still lock all RAM.
- `r5_span_take()` returned at the first free block too large to use when no row was left for its rest, and so missed an exact fit further on; the request then fell back to GTT without need.
- `r5_log_allow()` updated the once bits and the error count without an atomic operation, and `gpu_rb()` calls it outside the GPU lock while `BO_CREATE` calls it inside.
- `distro/gpu/COMPAT` still said `gtt_bytes` is the VRAM number; `docs/GPU-ARCHITECTURE.md` still said every `DISCARDABLE` request is refused. The last entry's "image build of this branch" was written while that build failed on a stale `R5Icons.o` in the shared tree; the build passed afterwards (comment on #4307).

What changed:
- Kernel, `BO_CREATE` for GTT: the file's own buffers plus its export-only GTT (`gpu_export_only()`, the VRAM count of M11 made per domain) stay under a quarter of system memory, and all GTT backings of all files together (parked and exported ones included) under half of it, as Linux keeps TTM (v6.18 `ttm_device.c`, `ttm_global_init()`, lines 90-91: `num_pages /= 2`). Over it: `refuse BO_CREATE gtt total`.
- `r5_span_take()` (`r5_span.h`): with no row for the rest of a split it goes on to the next block, and writes the offset only on success.
- `r5_log.h`: the error count and the once bits use `__atomic_fetch_add` and `__atomic_fetch_or`.
- `COMPAT`, `GPU-ARCHITECTURE.md`: the GTT limits and the `DISCARDABLE` rule as they are now.

Kept as it is, with the reason: an import counts in the importing file (app_server for the windows of other programs), as the VRAM count has since M11; the system-wide limit is what bounds the RAM, and a file limit is per file by the design (`GPU-ARCHITECTURE.md`, "Memory limits per address space"). RADV adds the reported VRAM window to the GTT size for its heaps; the last part of that does not fit once the pool is full, and the kernel refuses it rather than locking more.

Tested: `scripts/test-r5-gpu.cpp` (its new checks run first): `ok gtt limit and span room`, now also the export-only and system-wide counts in `gpu_bo_fill()`, the exact fit after a split without a row, 32 error lines out of 40, and the once bits. The span checks of that program (behind older checks that no longer match the code) were copied into a separate program: they pass with the new `r5_span.h`; the new exact-fit case fails with the old one. `scripts/test-ac-drm.sh`: `ok vram full falls back to gtt`, `ok libdrm ioctl`. Not booted on the 5625U.

## 2026-11-16 - Bluetooth step 4: game controllers, mice and keyboards over Classic Bluetooth, passkey pairing

Problem: pairing events never arrived (the Event Mask after a reset leaves out the Secure Simple Pairing events, and the server never turned Simple Pairing Mode on). Passkey Entry, which keyboards use, was not handled. There was no way for a Bluetooth HID device to reach `input_server` or `BJoystick` (`docs/BLUETOOTH.md`, step 4).

What changed:
- **Event Mask and Simple Pairing Mode** (`R5Pairing.h`, `LocalDeviceImpl::SetupController()`): when a radio is added and after every successful HCI Reset: Read Local Features, then Write Simple Pairing Mode when the controller has the SSP feature, then Set Event Mask (the default plus bits 48 to 53, 58 and 59 only with SSP), one command after the other. Logged as `bluetooth_server: hci<N> event mask <mask> (...)`.
- **Passkey Entry** (`PasskeyWindow`): IO capability KeyboardDisplay (as BlueZ). User Passkey Notification shows the number to type on the keyboard and counts the digits (Keypress Notification); User Passkey Request asks for the number the device shows. `bluetooth_server: ssp passkey ok` or `failed`. The passkey is never logged. SSP events are also handled without a waiting petition, and their sizes are checked.
- **Mistake found and fixed:** every User Confirmation Request was answered yes without asking, so any nearby device could pair silently and, with HID, type as a keyboard. Only a pairing asked for on this computer (Create Connection from the Bluetooth page) is confirmed now; others get the negative reply and `bluetooth_server: pairing refused, not started on this computer`. Numeric Comparison still does not show the number (open).
- **`bluetooth_hid` driver** (`distro/kernel/.../drivers/input/bluetooth_hid`, MIT, regular image, `dev/bluetooth/hid`): a transport for Haiku's `hid_shared` (the parser and the keyboard, mouse and joystick handlers of `usb_hid` and `i2c_hid`), fed by `bluetooth_server` through `/dev/bluetooth/hid/control` (uid 0, `<R5BluetoothHID.h>`). It publishes `input/keyboard/bluetooth/N`, `input/mouse/bluetooth/N` and `joystick/bluetooth/N` with the same protocol as USB, so input_server's add-ons and unchanged `BJoystick` programs use them. Every ioctl pointer goes through `IS_USER_ADDRESS` and `user_memcpy`. A driver instead of an input_server add-on in `distro/input/` (the plan): only a driver can publish `/dev/joystick`, and it reuses the existing parser instead of writing a second one.
- **Pair button** on the Bluetooth page and in the Bluetooth program (`libr5bt.so`, additive `kMsgBluetoothPair`): choose a device from the scan and press Pair; it stops the scan and sends Create Connection, which pairs. Step 3b had removed the old pairing window, so there was no way to pair from the panel. No progress or result is shown on the page yet.
- **HID host** (`HIDHost` in `bluetooth_server`): listens on PSM `0x11` and `0x13`; after encryption it takes the report descriptor from SDP (attribute `0x0206`, bounds-checked), opens the channels if the device did not, creates the device in the driver and passes each input report. `bluetooth_hid: br-edr hid ready (<n> input devices)`.
- `bt_hid_driver_test` (new, regular image, in `run-qemu-smoke.sh`): feeds the driver a gamepad and a mouse as the server does and checks the nodes, `BJoystick::CountDevices()`, a button and an axis, and the removal; refuses an oversized, empty or kernel-address descriptor. `bt_l2cap_abi_test` accepts that `bluetooth_server` now holds PSM `0x11` and `0x13`.
- Order (owner): after step 4 comes BLE (step 1c, then 6), then step 5; game controllers (`docs/GAME-CONTROLLERS.md`) after step 4 is merged.

Tested: built with `scripts/build-locked.sh` (`jam_exit=0`, no warnings in the new or changed files). QEMU with SMAP and SMEP, own image copy: no panic; `KERN: loaded driver .../dev/bluetooth/hid/bluetooth_hid`; `bt_hid_driver_test: ok` with `joystick/bluetooth/0` listed by `BJoystick` as `bluetooth/0` (2 axes, 8 buttons, button 1 down, X 32639) and `input/mouse/bluetooth/0`; `bt_server_smoke_test: ok (0 crashes)`; `bt_l2cap_abi_test: ok` (PSM 0x11 and 0x13 held by `bluetooth_server`). After the jam, the image check of the Mesa packages reported `missing libdrm-2.4.124-2-x86_64.hpkg` (`mesa_exit=1`); because this branch still pinned -2 while the VRAM work (now on main) moved the shared tree to -3; not caused by this change. Manual SMAP review of the driver: no user pointer is dereferenced. The fuzzers were not run: the `hci` and `l2cap` parsers are unchanged; the new SDP descriptor parser in the server has no fuzzer yet.

On the 5625U (USB stick, 16 Nov 2026, four boots): `h2generic: controller ready` (MT7961 firmware in 120 ms) and `event mask 0xc3f1fffffffffff (secure simple pairing on)`; `deskbar_bt_replicant: attached via messenger` in a real session after Sign in. Found and fixed on the way: the radio refuses Read Stored Link Key (`0x0C0D`) with Command Status and the page hung on Starting Bluetooth; Settings crashed drawing the scan list (two `DeviceItem` classes); the Deskbar item never started (Login starts it now); and in the last boot the MediaTek setup failed on a late WMT answer (0x1 while 0x6 was asked), now dropped instead of failing. Those last two fixes and pairing, the controllers and the switch as a standard person have not run on hardware yet. 

Not tested: everything with a radio (QEMU has none): the event mask line, pairing, the passkey, `bluetooth_hid: br-edr hid ready`, the 8BitDo M30 and DualSense, keyboard and mouse events in `input_server`. Planned on one USB stick on the 5625U. Whether the MT7922 has an interface of class `0xE0/0x01/0x01` (the only way `h2generic` finds it) is checked on paper only (the USB stack matches interfaces too). ARM64 and RISC-V 64 were not built or tested.

## 2026-11-16 - Graphics: a GPU buffer that no longer fits in VRAM goes to system memory, within a GTT limit (libdrm-2.4.124-3)

Problem: on the 5625U (M12 measurement stick, boot 1, `generated/boot-5625u-1` and `-2` of the m12-gpu-draw worktree) app_server's Skia drawing turned off after a minute with `gpu draw off: insert failed`, after `radeon_hd: refuse BO_CREATE vram status 0x80000000` for buffers of 17 to 45 MB (Skia's 4x MSAA targets of a whole window). The R5 DRM library made every buffer that asks for VRAM in VRAM and nothing else. Linux does not: `amdgpu_gem_create_ioctl()` (v6.18 `amdgpu_gem.c`, lines 486-497) retries a VRAM-only request with `AMDGPU_GEM_DOMAIN_GTT` added, and `amdgpu_bo_create()` (`amdgpu_object.c`) lets a user buffer that allows only VRAM live in GTT too, unless it is `DISCARDABLE`. RADV counts on that: it names GTT with every VRAM request (`radv_amdgpu_bo.c`), and on an APU `radv_physical_device_init_mem_types()` reports 2/3 of visible VRAM plus GTT as its VRAM heap, more than the kernel's VRAM pool (248 MB).

What changed:
- R5 DRM library, `r5_gem_create()` (`distro/graphics/libdrm-r5/r5_translate.c`): a VRAM request refused with `ENOMEM` (`B_NO_MEMORY`) is made in GTT, as that Linux call does. A `DISCARDABLE` request falls back only when it names GTT itself (Linux `allowed_domains`). Other refusals (a bad size, a bad domain) stay errors. A request for GDS, GWS or OA is refused with `EINVAL`; it used to become GTT silently (GFX9 RADV does not ask for them). With `gpu_debug` the library prints `kernel bo vram full, <n> bytes in gtt`.
- Kernel, `BO_CREATE` for GTT: the limit of `docs/GPU-ARCHITECTURE.md` ("GTT at most a quarter of system memory" per address space) is now in the kernel (`r5_gtt_fits()` in `r5_vm.h`, `gpu_gtt_limit()` from `get_system_info()`); a request over it is refused (`refuse BO_CREATE gtt limit`). GTT is locked RAM (`create_area`, `B_FULL_LOCK`), and before this nothing bounded it but RAM and the slot count (up to 512 x 64 MiB per file), which the fallback would have reached sooner.
- Kernel, `GET_INFO`: `gtt_bytes` reported the VRAM size again (`frame_buffer_size`, the PCI frame buffer window, no GTT measure). It now reports that GTT limit. RADV sizes its heaps from it, and app_server sets its window memory to half of it (`Compositor.cpp`, `fBudget = gttBytes / 2`, so 128 MB on the 5625U until now).
- Kernel, VRAM full: the refusal is printed once per boot (`refuse BO_CREATE vram full ... later ones are not printed`, `R5_ONCE_VRAM_FULL`) instead of using one of the 32 error lines each time; with the fallback it is normal operation, and a day of it would have hidden every later GPU error.
- Kernel, `r5_gfx9_alloc_user_vram()` (`gfx9_k12.cpp`): with all 64 span rows in use it took a new block and lost it when `r5_span_add()` failed. With the fallback that would repeat on every request until the pool was gone. It now asks for a free row first (`r5_span_has_room()`).
- `libdrm-2.4.124-3`: `libdrm.so.2.124.0` rebuilt (the translation is in `libdrm.so`); `libdrm_amdgpu.so.1.124.0` and `amdgpu.ids` are byte-identical to `-2`. Pin, install, EGL vendor check and the download-folder clean-up name `-3`.

Read and found right (two outside reviews and my own): the second `BO_CREATE` sees the original fields (the kernel writes the struct back only on success); `VA_MAP` gives a GTT buffer a snooped system PTE; `BO_MAP` maps it cached (`R5_GPU_BO_WC` is cleared for GTT while `gfx9_wc_gtt` is off); export and import report the real domain; scanout is `SCANOUT_BO`, a borrowed VRAM buffer, not `GEM_CREATE`. Linux also drops `CPU_ACCESS_REQUIRED` and retries; here the whole BAR is CPU-visible, so there is no smaller visible pool to miss.

Limits, known and left as they are:
- A refusal because a file has no free buffer slot is also `B_NO_MEMORY`; the library then tries GTT once more, which fails the same way. It costs one more call and one more error line, only at the slot limit.
- The host test's fake kernel keeps `R5_GPU_BO_WC` on GTT (the real kernel clears it unless `gfx9_wc_gtt`), and the test is not built with `R5_HOST_NEGATIVE_ERRNO`: on the Linux host that flag makes `errno` -12, which is not `ENOMEM` there, so the fallback could not be tested with it. On Haiku `ENOMEM` is `B_NO_MEMORY` itself.
- Mistake during this work: the build of this branch writes `libdrm-2.4.124-3` into the shared WSL tree's HaikuPorts pin, and a branch without this change then failed its jam (`missing libdrm-2.4.124-2`; the M12 profile build, 16 Nov 10:56, likely the bluetooth-step4 build too). The pin was set back to `-2` after each build of this branch. It ends when this change is in `main`.
- `scripts/test-r5-gpu.cpp` has two checks that no longer match the code (`info lock` looks for `sBo[i].live`, which `gpu_get_info()` counts as `sBacking[i].live` since M11 step 3b; `order` looks for `fsync(fileno(out))`, which the self-test dropped in M10). They stop that program; the new checks run first. Left for a cleanup of its own.

Tested: host test `scripts/test-ac-drm.sh` against the fake kernel, case `vram_full_falls_back` (VRAM full: VRAM and VRAM plus GTT end in GTT and map; `DISCARDABLE` with GTT falls back, without GTT it fails; GDS is refused; an oversize buffer still fails). With the old `r5_translate.c` 12 checks fail; with the new one `ok vram full falls back to gtt` and `ok libdrm ioctl`. `scripts/test-r5-gpu.cpp`: `ok gtt limit and span room`. `scripts/check-libdrm-symbols.py`: `ok libdrm symbols`. Cross build of libdrm into `r5-sysroot/lib` (old files in `r5-sysroot-backup-1116`); image build of this branch. Not booted: RADV on the 5625U with this has not run; QEMU has no AMD render node, so it cannot show this path.

## 2026-11-16 - Bluetooth step 3b: the Bluetooth program and the Bluetooth item in Deskbar

Problem: the Bluetooth panel was only a Settings page; Haiku's old Bluetooth preferences were still the program, and the only Deskbar item was the one `bluetooth_server` added by putting its own binary into Deskbar (`docs/BLUETOOTH.md`, step 3b).

What changed:
- **The Bluetooth program** (`distro/bluetooth/app`): `BluetoothView` in a window, sized to the panel, with the lock button for a person who is not an administrator (`r5_admin_query()`, `r5_admin_prompt_unlock()`, refreshed every 15 s). It replaces Haiku's Bluetooth preferences in the regular image (owner's choice) and keeps their signature and icon. Their list of paired devices and their pairing window are gone until step 4.
- **The Deskbar item** `bluetooth_deskbar` (`distro/bluetooth/deskbar`, `/system/servers`, regular image only), the AirPlay pattern: a user launch job after Deskbar adds the program's own small binary and quits. The item asks `bluetooth_server` every 5 s with an asynchronous `BT_MSG_R5_GET_RADIO` (Deskbar never waits), logs `deskbar_bt_replicant: attached via messenger` on the first answer, and its menu shows the state, turns Bluetooth on or off (only with `r5_admin_change_allowed()`; the server checks again) and opens the program.
- **Glass-ready** (owner's question): the item paints no background of its own (`B_TRANSPARENT_BACKGROUND`) and draws the glyph as an alpha bitmap with `B_OP_ALPHA` and `B_ALPHA_COMPOSITE`, the rule of the R5Book's "Window effects", so a later glass Deskbar shows through. Deskbar itself has no glass yet.
- **`bluetooth_server`** no longer adds itself to Deskbar (Deskbar loaded the whole server, which `docs/BLUETOOTH.md` forbids); `bluetooth_deskbar` removes that old item (`BluetoothServerReplicant`) from a session that still has it.
- `run-qemu-smoke.sh`: `DESKBAR=1` starts Deskbar and the item before Sign in, `PROGRAM=1` opens the program.

Tested: built with `scripts/build-locked.sh` (`jam_exit=0`, no warnings in the new files). QEMU (KVM), own image copy, Deskbar and the item started by the test job (uid 0, before Sign in): `deskbar_bt_replicant: attached via messenger`; the item shows the faint Bluetooth glyph beside AirPlay; the program opens with the panel ("0 radios"); `bt_server_smoke_test: ok (0 crashes)`, `bt_l2cap_abi_test: ok`. Logs: `generated/bluetooth/qemu-step3b/`.

Not tested: the user launch job in a real session after Sign in, the administrator and a standard person (the step's pass rule; VirtualBox), the menu and the lock button by hand, the item on glass (Deskbar has no glass yet), a real radio. ARM64 and RISC-V 64 were not built or tested.

## 2026-11-16 - Bluetooth: bluetooth_server is a system service from boot (uid 0)

Problem: `bluetooth_server` had no launch job. The first program that needed it started it with `BRoster::Launch()`, so it ran as whoever that was; there was no Bluetooth before Sign in (a keyboard could not work there, step 4), and the switch and the pairing keys depended on that person's rights. Owner's choice after step 3a: run it as uid 0 (`docs/BLUETOOTH.md`, as built).

What changed:
- A launch job, `distro/bluetooth/launch/bluetooth` (`service x-vnd.Haiku-bluetooth_server`, `no_safemode`, `legacy`), installed in `system/non-packaged/data/launch` of the regular image only by `apply-bluetooth.py`. launch_daemon starts it at boot and again after it stops.
- `/boot/system/settings/bluetooth` is root's alone, mode 0700 (`r5_bluetooth_settings_path()`; it also resets a folder an earlier build left 0775). `r5_radio` is 0600. The pairing keys moved there too (`paired_devices`, 0600), one set for the whole computer instead of the settings of whoever started the server. Keys in the old place are not moved: pairing does not work before step 4.
- Login's group rights for that folder (added in step 3a) are removed again.
- `BluetoothController` asks launch_daemon (`BLaunchRoster::Start()`) when the server is not running; `BRoster::Launch()` would start a second server as the person signed in.
- The sender check of the switch stays. A Stop in Haiku's old Bluetooth program now only restarts the service.
- `run-qemu-smoke.sh` logs whether `bluetooth_server` runs before the test starts anything, and the folder's rights.

Tested: built with `scripts/build-locked.sh` (`jam_exit=0`, `build_s=227`, no warnings in the changed files). QEMU (KVM), own image copy: `bluetooth_server` runs before the test job starts anything (team 59, started at boot), `bluetooth_server: started, 0 radios`; the page shows it; `bt_server_smoke_test: radio switch ok`, `ok (0 crashes)`, `bt_l2cap_abi_test: ok`; `/boot/system/settings/bluetooth` is `drwx------` and `r5_radio` `-rw-------`, owner uid 0 (named `user` before the first Sign in). Logs: `generated/bluetooth/qemu-service/`.

Not tested: a signed-in administrator or a standard account using the switch (the test job runs as uid 0; VirtualBox), a real radio, a keyboard at Sign in (step 4), and the minimum image. ARM64 and RISC-V 64 were not built or tested.

## 2026-11-16 - USB 2.0 (EHCI): no unaligned pointer into a packed descriptor

Problem: GCC warned `-Waddress-of-packed-member` in `EHCI::CreateDescriptor()` (`ehci.cpp`): `fStack->AllocateChunk(&result->buffer_log, ...)` passes the address of a member of the packed struct `ehci_qtd` as `void**`. The member is at offset 56 (8-aligned), so it is harmless on x86_64 today, but it would become an unaligned pointer on ARM64 if the struct layout ever moved. Seen in a Bluetooth build log; no Haiku ticket.

What changed: `distro/kernel/src/add-ons/kernel/busses/usb/ehci.cpp` (already a copy, installed by `scripts/apply-kernel-bugs.py`) allocates into a local `void* bufferLog` and then assigns `result->buffer_log`. Marked as an R5 MIT snippet. No other place in the file takes the address of a packed member. No functional change.

Tested (16 Nov 2026): built with `scripts/build-locked.sh` (`jam_exit=0`, `mesa_exit=0`); `ehci.o` was recompiled and the build log has no `address-of-packed-member` warning. QEMU (KVM) on an own copy of `r5-os.image`, with an EHCI controller carrying a USB keyboard, a USB tablet and a USB stick (480 Mb/s): `usb ehci: successfully started the controller`, the stick answered inquiry and capacity (`usb_disk` published `disk/usb/0/0/raw`), the first-boot window appeared, and two Down keys from the USB keyboard moved the language list from English to Esperanto.

Not tested: real EHCI hardware, ARM64 (not built), large transfers to the stick (it had no file system).

## 2026-11-16 - Bluetooth step 3a: libr5bt.so and the Bluetooth page in Settings

Problem: Settings had only a launcher row for Haiku's old Bluetooth program, and there was no shared R5 Bluetooth code for a page, a program or a Deskbar item (`docs/BLUETOOTH.md`, step 3a).

What changed:
- **`libr5bt.so`** (`distro/bluetooth/lib`, MIT, header `<R5Bluetooth.h>` in `headers/os/interface/`): `BluetoothView` (a `BCard` summary with `R5_GLYPH_BLUETOOTH` and the radio count, a box whose title is the Bluetooth switch, the devices nearby with Scan and `r5_level_glyph_draw(R5_LEVEL_SIGNAL)` bars), `BluetoothController` (a `BLooper`; every server request has a 2-second timeout; it starts `bluetooth_server` once and does not restart it after it stops), `BluetoothState`, `BluetoothRadioInfo`, `BluetoothDeviceInfo`. The scan follows Haiku's InquiryPanel (`DiscoveryAgent::StartInquiry()`, a `DiscoveryListener`). Regular image only, because it needs `libbluetooth.so`.
- **Settings:** Devices → Bluetooth ("Find Bluetooth devices nearby"), first under Devices by name, replacing the launcher row in `LegacyPages.cpp`. Only the switch waits for the lock: the page keeps `RequiresAdmin()` false and enables the switch on `kMsgRefreshPage` through `AdminChangeAllowed()`. Written down as an exception in `docs/SETTINGS.md`. The old Bluetooth program stays installed.
- **The switch (`bluetooth_server`)**, choice made for the owner: HCI has no power off. Off writes Write Scan Enable `0x00` to every radio (also a radio added later), refuses inquiry, periodic inquiry, Create Connection, Remote Name Request, a non-zero Write Scan Enable and `BT_REQ_CREATE_CONN` (`B_NOT_ALLOWED`), and rejects incoming connection requests (reason `0x0D`). It is kept in `B_SYSTEM_SETTINGS_DIRECTORY/bluetooth/r5_radio`, written by the server through a temporary file and a rename. New messages `BT_MSG_R5_GET_RADIO` and `BT_MSG_R5_SET_RADIO`.
- **libbluetooth:** additive `int8 RemoteDevice::GetRSSI()` (Haiku ticket [#1916](https://dev.haiku-os.org/ticket/1916) asks for this; left open until it is seen on a real radio). Bug, also in Haiku: both `RemoteDevice` constructors left `fRSSI` and `fFriendlyNameIsComplete` unset, so `GetFriendlyName()` could skip asking the device.
- **Tests:** `bt_server_smoke_test` now turns the switch off, reads it back, checks that a connection is refused, and turns it on again. `run-qemu-smoke.sh` takes `SETTINGS_PAGE=<id>` (opens that page and logs whether Settings and `bluetooth_server` still run 30 s later) and `SHOTS="<s> ..."` (screendumps over its own monitor socket).
- **Docs:** `docs/LIBR5.md` ("Bluetooth Kit"), `docs/SETTINGS.md` ("Bluetooth"), R5Book `docs/r5book/bluetooth-kit.html` with a QEMU screenshot, `docs/BLUETOOTH.md` (as built).

Review: read twice. The first read moved the scan-off write after the radio is brought up (`Launch()`), restored the switch when a click arrives while it is locked, stopped re-sending an unchanged state every 3 seconds, and found that Settings would turn the page's bold titles plain (the page now gives them `r5_settings_bold_font()`). Checked and harmless: `bluetooth_server` hands out each radio to any number of callers (`Available()` is always true). The second read found the uninitialized `RemoteDevice` fields.

Tested:
- Built with `scripts/build-locked.sh` three times, last `jam_exit=0`, `build_s=194`, no warnings in the changed files.
- QEMU (KVM), own copy of that image, page opened by a launch job: Bluetooth is the first item under Devices, the page shows "0 radios" (screenshot `docs/r5book/img/bluetooth-page-qemu.png`); 30 s later `Settings` and `bluetooth_server` both still run; syslog `bluetooth_server: started, 0 radios`, `radio switch off`, `radio switch on`; `bt_server_smoke_test: radio switch ok`, `ok (0 crashes)`; `bt_l2cap_abi_test: ok`. Logs: `generated/bluetooth/qemu-step3a/`.

Not tested: a real radio (no scan list, no device found, no signal bars, no rejected incoming connection, radio name and address); the VirtualBox scan list waits for hardware. The gray switch under a closed lock (needs a standard account). Clicking the switch by hand. ARM64 and RISC-V 64 were not built or tested.

Owner's review (16 Nov 2026), fixed in this PR:
- **Mistake: the switch could not be saved after Sign in.** `bluetooth_server` has no launch job; `BRoster::Launch` runs it as the person signed in (uid ≥ 1000), and `/boot/system/settings` is root's, so `create_directory()` failed and the switch went back. The QEMU test hid this: its launch job ran as uid 0. Login's `refresh_machine_settings_access()` now treats `/boot/system/settings/bluetooth` like `network` (root, group `admin`, writable for the group, for everyone during a grant), and the server writes `r5_radio` as 0664. Not uid 0 mode 0700 as `docs/BLUETOOTH.md` section 4 says; that needs the server to run as uid 0, which is the owner's choice.
- **The server did not check who sent `BT_MSG_R5_SET_RADIO`.** It now carries it out only for uid 0, a member of group `admin`, or a team with a grant in `/boot/system/settings/r5-machine-unlock` (the rule of Login's `sender_may_change()`), else `B_NOT_ALLOWED`.
- **The Settings window could wait on libbluetooth.** The view stopped the controller with `Lock()` and `Quit()` on the window thread; `Lock()` waited while the controller was in a libbluetooth call without a timeout, and the destructor cancelled a scan with one. The view now sends `B_QUIT_REQUESTED`.
- **The minimum image.** Settings linked `libr5bt.so`, which only the regular image has. The page is now the add-on `settings_bluetooth` (regular image only); Settings itself does not link it.
- **Radios by `hci_id`.** The plain acquire hands radios out in turn: a known radio handed out again leaked a `LocalDevice`, and a radio replaced within one poll (same count) was missed. `BT_MSG_R5_GET_RADIO` now lists the `hci_id`s and the controller acquires only new ones.
- **Signal bars.** `DiscoveryListener` reports only the first answer of an address; a repeat with a stronger signal kept one bar. A filter on the listener now reports every answer.

Tested after the review: built with `scripts/build-locked.sh` (`jam_exit=0`, `build_s=247`, `settings_bluetooth` linked, no warnings in the changed files). QEMU (KVM), own image copy, page opened by a launch job: the add-on page is still first under Devices and shows "0 radios", Settings and `bluetooth_server` still run 30 s later, `bt_server_smoke_test: radio switch ok`, `ok (0 crashes)`. That launch job runs as uid 0, so it does not show the fix for a signed-in administrator, the refusal of a non-administrator sender, or Login's new directory rights: not tested. Neither are the minimum image, a radio replaced within one poll, or the window no longer waiting (no radio in QEMU).

Limits: device names are only what a device sent in the inquiry. Found, not fixed (older than this change, needs the owner's choice): the minimum image has Settings but not `libr5wifi.so`, which Settings links, so the runtime loader cannot start Settings there (read from the image definition, not booted).

## 2026-11-16 - Bluetooth: review fixes for steps 0 to 2 (#4286, #4288, #4291, #4292)

Problem: two owner reviews of the merged Bluetooth steps found crashes, kernel use-after-free, leaks, hangs and secrets in the log.

What changed:
- **L2CAP signalling (`l2cap_signal.cpp`, `l2cap_internal.h`).** A command identifier holds a socket reference; who takes the identifier out of the table now owns it (`l2cap_take_command_ident()`, `l2cap_claim_command_ident()`), so the receive thread and a waiting thread never both release it. An answer takes its identifier before it is checked, so a too-short or truncated answer now wakes the waiting `connect()` or `shutdown()` (`_HandleBadResponse()`) instead of leaving it blocked with the identifier and reference leaked. A pending Connection Response and a configuration continuation keep the identifier for the next answer (before, the second answer was dropped and `connect()` hung). A Configuration Response with unknown or malformed options closes the channel and frees the identifier. A Configuration Request with an unknown option leaked its reject buffer. Command Reject read its data from the next command in the packet.
- **The dispatcher loop.** The review said a zero-length command spins it. Read against `net_buffer.cpp`: every round removes the 4 header bytes first, so that input does not hang it, and it was not reproduced. The loop now stops anyway when a round consumes nothing.
- **`L2capEndpoint`.** A failed or interrupted wait in `Connect()` and `Shutdown()` gives back its identifier and reference (unless an answer took them) and closes. A refused connection went on into configuration (a missing `return`). A Command Reject during configuration or disconnection left the waiter blocked. `L2capEndpointManager::Disconnected()` set the state without the endpoint lock and woke nobody; it now locks each endpoint outside the channel lock (`Connect()` takes them the other way round) and wakes it.
- **`l2cap.cpp`, high.** `interface_address` points at a plain struct inside `HciConnection`; freeing a buffer with it set called `ReleaseReference()` on it. Every early free (fixed channels, unknown CID, wrong length) did that. It is now taken off first.
- **HCI module (`bluetooth.cpp`, `hci_assemble.h`).** `PostCommand()`, `PostACL()` and `PostSCO()` keep the device list lock through the driver hook, so an unplug cannot delete the device in between. `PostACL()` now always owns the buffer (it freed part of it on some errors and its caller freed it again). `HciPacketHandler()` returns `B_OK` after dropping a bad packet: any other value ended the kernel receive thread for the rest of the boot. A failed header read no longer passes a header-only packet up.
- **`h2generic` command timeouts (`h2command.h`, `h2generic.cpp`, `h2transactions.cpp`).** The answer check assumed one whole event per USB transfer. An event whose length is a multiple of the packet size has no short packet after it, so events share transfers and one can end in the next; then an answered command timed out anyway, or a late answer went up. The event stream is now followed across transfers (`h2_event_stream_filter()`), each event judged from its first bytes, a late answer removed whole. A failed send frees exactly its own slot (it freed the oldest slot of that opcode). A full tracker reports the command it gives up on at once.
- **`bluetooth_server`.** Events are checked against what the port delivered, and the counted events (Inquiry Result, with RSSI, Extended, Return Link Keys, Number of Completed Packets) against their counts; a count of 255 read past the 260-byte buffer. `Disconnect()` and `CancelConnection()` crashed on a missing, short or unknown address; `ConnectionComplete()`, `DisconnectionComplete()` and `AuthComplete()` on an unknown device. Link keys and the pairing passkey are no longer logged. The paired-devices file (all link keys) was created 0644; it is now 0600, written through a rename, and saved on every new key instead of only when the radio goes away.
- **Docs (#4292).** `docs/r5book/icons-kit.html` lists `R5_GLYPH_BLUETOOTH` and documents `r5_level_glyph_draw()`; `docs/LIBR5.md` has a "Level glyphs" section and says `DrawWiFiSignalBars()` needs `libr5icons.so`.

Not done, needs the owner: the review asks to move the pairing keys to `/boot/system/settings/bluetooth/` (uid 0, mode 0700). `bluetooth_server` has no launch job, so `BRoster::Launch` runs it as the person signed in (read from the code, not tested), who cannot write there; the keys stay in that person's settings, now 0600. Two radios still share one key file.

Manual SMAP review: no new user-memory access in the kernel. The new kernel code reads net_buffers, a kernel array and the HCI connection table; `user_memcpy` in the h2generic ioctl is unchanged.

Tested:
- `scripts/fuzz-bt.sh hci 60`: `hci_fuzz: 60s 0 crashes (106821 rounds)`; the target now also runs the event stream filter (random events cut and run together; every kept event out unchanged, each judged once) and the tracker's new slot calls. `scripts/fuzz-bt.sh l2cap 60`: `l2cap_fuzz: 60s 0 crashes (5203847 rounds)`.
- Built with `scripts/build-locked.sh`: `jam_exit=0`, `build_s=208`, no warnings in the changed files.
- QEMU, KVM, `-cpu host,+smap,+smep`, own image copy, one boot: `enable SMEP`, `enable SMAP`, no panic; `bluetooth_server: started, 0 radios`, `bt_server_smoke_test: ok (0 crashes)`, `bt_l2cap_abi_test: ok`.

Not tested: everything that needs a radio or a remote device (L2CAP answers, connect timeouts, unplug during a send, split events from a real controller, pairing and the key file). ARM64 and RISC-V 64 were not built or tested.

## 2026-11-16 - GPU compositor: VirtualBox desktop after a resize

Problem: in VirtualBox the M11 compositor (on Lavapipe) left a broken desktop after a display change: the wallpaper sheared with horizontal seams, white squares where the pointer and icons are, no Deskbar. With `gpu_compositor false` the same image was normal. It also happened on a fresh start, because the Guest Additions (`VBoxTray`) set the display to the window size after sign-in.

Cause (shown with logs, `generated/m11-vbox/README.md` in commit `15e9ff01d3`; the logs, screenshots and patch named in this entry are only in that commit, they were taken off `main` afterwards to keep the tree small): the compositor waits one refresh period between frames, from `pixel_clock`, `h_total` and `v_total` of the current mode. After `VBoxTray` sets a mode the vboxvideo mode has pixel clock 60 and totals equal to the visible size, which the formula reads as one frame per 15.5 s at 1437x649 and 29.5 s at 1920x922 (syslog `compose period unknown: pixel clock 60 kHz, total 1023 x 767`). The compose thread slept that period while it held the compose slot, and `SetMode` takes that slot first. Timed logs: `SetMode guard after 23423 ms`, then 15500, 15474, 29177 ms for the next requests; in the VirtualBox log the host followed a size hint after 5 to 30 s instead of about 20 ms. Meanwhile the screen kept the old mode's pixels, read by VirtualBox with the new line length (the shear). A frame dump showed app_server's own pitches were right at every step (back, front and frame width x 4), and the guest front buffer was right once a frame was presented. The second suspect (how the frame buffer of vboxvideo is mapped, stride or format) is ruled out by those dumps; the first (the compositor does not handle a mode change) was only true through this wait.

What changed:
- `distro/app_server/compositor/compose_period.h` (new, MIT): the period from the timing, and 0 (no period: compose as soon as damage arrives) when it is outside 1 ms to 50 ms (1000 Hz to 20 Hz). `Compositor::_UpdatePeriod()` uses it and logs the rejected timing once.
- `Compositor::_ComposeLoop()`: the wait for the next period is now before the compose slot is taken, so a mode change never waits for it.
- `Compositor`: one line per frame size change, `frame follows the screen <w>x<h>, period <us> us`.
- `scripts/apply-app-server.py` copies the new header.
- Tests: `scripts/test-compose-period.cpp` (host; real 60 and 144 Hz modes give their period, the VirtualBox timing and a missing timing give none, the 20 Hz bound). Stick check `m11_mode` in boot 3 (Lavapipe): changes the mode with `screenmode` and back, each change must return within 3 s, and the frame must follow twice.

Mistake on the way: the first fix (repeat the whole picture 0.25, 1 and 3 s after a mode change) was built on the guess that VirtualBox missed the first full copy. It made the series worse, and the timed logs then showed the 15 s wait. It was removed before this commit; the patch is kept in `generated/m11-vbox/`.

Tested (16 Nov 2026, Europe/Amsterdam):
- `g++ -std=c++17 scripts/test-compose-period.cpp`: `ok compose period`.
- Built with `scripts/build-locked.sh` (`mesa_exit=0`), image copied under the build lock.
- VirtualBox 7.2, VM `R5OS-comp`, fresh VDI, default settings (compositor on Lavapipe, `no render node`): first-boot prompt, sign-in, then size hints 1437x649, 1920x923, 1280x720, 1601x899 and the window's own follow-up hints (it shrinks by one pixel per step). All eight screenshots normal (`generated/m11-vbox/fix-run/`); the host followed every hint within 10 to 55 ms. Before the fix the same series was broken (`series-grid.png`, `on-run4/`); with `gpu_compositor false` it was normal (`off-run-grid.png`).
- QEMU (KVM, std VGA, Lavapipe), own image copy: a planted job changed the mode to 1280x800, 800x600 and 1024x768; each `screenmode` returned in 0 s, the periods were 13334 and 16666 us, and the screendumps are normal (`qemu-grid.png`).

Tested later on 16 Nov by the owner: dragging the VirtualBox window by hand in `R5OS-comp` (the fix-run disk); the desktop stayed right during and after the drag.

Not tested: the stick check `m11_mode` has not run (it needs the 5625U boot 3); its `check` change only passed `sh -n`. RADV and the 5625U are unchanged by this in behaviour except that the period wait moved out of the compose slot. Every first compose after a mode change in VirtualBox still logs `compose late 100 ms` (Lavapipe on two virtual CPUs); not looked into. ARM64 and RISC-V 64 not built.

## 2026-11-16 - Icons: one place for signal and battery symbols (Bluetooth step 2)

Problem: Bluetooth needs the same signal symbol as Wi-Fi, and a battery symbol, but the Wi-Fi bars were an inline function in `R5WiFiGlyphs.h` that only Wi-Fi could use (`docs/BLUETOOTH.md`, step 2). Done overnight while the owner slept.

What changed:
- `libr5icons.so`: `r5_level_glyph_draw(view, which, frame, level, foreground, background, style)` with `R5_LEVEL_SIGNAL` (four rising bars) and `R5_LEVEL_BATTERY` (body, cap, four segments; the outline stays in the full color so an empty battery still shows), styles normal, disabled and outline. The signal geometry and colors are those of the old `DrawWiFiSignalBars()`.
- New line glyph `R5_GLYPH_BLUETOOTH` (7), the rune, for the Bluetooth page of step 3a.
- `DrawWiFiSignalBars()` calls `r5_level_glyph_draw()`. `libr5wifi.so` and NetworkStatus link `libr5icons.so`.
- Bug: `r5_glyph_bitmap()` refused every glyph after `R5_GLYPH_LOG_OUT`, so a new glyph would have given NULL; the bound is now the last glyph.
- `docs/ICONS.md` ("Level glyphs", the new glyph row) and `docs/LIBR5.md`.
- `r5_level_glyph_test` (`/system/bin`, added by `apply-icons.py`): the old function, copied unchanged, and the new call drawn into bitmaps for 6,552 signal cases (13 widths from 4 to 40, 8 heights from 4 to 39, levels −1 to 5, three styles, three color pairs), compared byte for byte; the battery and the Bluetooth glyph must draw something.

Deviation from the plan: the pass check is this test, not before-and-after screenshots. QEMU has no Wi-Fi adapter, so the list and Deskbar show no bars, and the desktop sits behind Sign in.

Review: read twice. The second read found that two blank bitmaps compare equal, so a failed draw would have passed the test; it now fails when the old code drew nothing for a lit level. It also fixed an `int32`/`size_t` comparison in the test.

Tested: built with `scripts/build-locked.sh` (`jam_exit=0`, `build_s=182`, no warnings in these files). QEMU (KVM), own copy of that image, one boot: `r5_level_glyph_test: ok (6576 cases)`, `exit 0` (`generated/bluetooth/qemu-step2/`).

Not tested: the Wi-Fi list and the Deskbar replicant on a real adapter (5625U or X1); the battery and Bluetooth glyphs by eye. ARM64 and RISC-V 64 were not built or tested.

## 2026-11-16 - Bluetooth step 1b: L2CAP in distro/, a safe signalling parser, connection details

Problem: a remote Bluetooth device could hang or crash the L2CAP layer, and the kit and server had memory bugs. Step 1b of `docs/BLUETOOTH.md` copies `l2cap` into `distro/`, adds fixed channels, incoming accept and one additive `libbluetooth` method. Done overnight while the owner slept; choices are written down below and in the plan.

What changed:
- **L2CAP signalling** (`l2cap_signal.cpp`): the dispatcher checked a command's length before removing its 4 header bytes, so a command up to 4 bytes too long passed, `remove_header()` failed, the size stayed the same and the loop never ended (a remote could stall the kernel thread). `l2cap_parse.h` (no kernel dependencies) now decides per command: handle, reject as too short for its code (Command Reject, not understood), or stop the packet. A Connection or Disconnection Response with an unknown identifier dereferenced a NULL endpoint. Configuration options are copied out and walked within the command; a short value left the rest uninitialized, a failed `create()` was not checked, and a response with unknown options leaked its reject buffer. Command Reject read its data with the wrong length. The early returns leaked the buffer.
- **L2CAP receive** (`l2cap.cpp`): the module owns the buffer (`acl.cpp` does not free it) but leaked it on every error, panicked on a signalling packet without a connection, and used the connectionless header unchecked.
- **Fixed channels** (owner's plan said "add"; built differently, open to change): ATT `0x0004`, LE signalling `0x0005`, SMP `0x0006` and `0x0007` are received and dropped with a trace instead of `ECONNRESET` and a leak. No LE link exists before step 6 asks for LE events, and their socket interface is best designed with its user (GATT and SMP in `bluetooth_server`), so step 6 adds it. `docs/BLUETOOTH.md` says so.
- **Incoming accept** already worked (bind, listen, accept). Bind now checks the whole PSM rule (odd, bit 8 clear; Core 6.0, Vol 3, Part A, 4.2). A child socket that got no channel stayed pending forever; it is aborted now, as TCP does.
- **libbluetooth:** additive `status_t RemoteDevice::GetConnectionDetails(bt_connection_details*)` (state, transport, HCI link type, encryption, handle, radio; a `size` field so the struct can grow), answered by `BT_REQ_CONN_DETAILS` in `bluetooth_server`. `GetConnectionState()` sent no `hci_id`, so every answer was DISCONNECTED; it does now. `Connect()` let `GetProperty()` write 32 bits into a `uint16` and a `uint8` on the stack. `Connect()`, `CancelConnection()`, `Disconnect()` and `GetConnectionState()` dereferenced a NULL messenger or owning radio. `RemoteDevice.cpp`, `RemoteDevice.h` and `bluetoothserver_p.h` are now in `distro/bluetooth/`.
- **Server** (found from compiler warnings of this build): `LoadRemoteDevices()` let `FindData()` write a pointer into the link key instead of copying it, so a paired device lost its key whenever the server restarted. `HandleAcquireLocalDevice()` wrote an 8-byte pointer into a 6-byte `bdaddr_t` on the stack. `CreateConnection()` used an unchecked address, added a known device to the list twice, read `record` uninitialized, and wrote a packed member through a pointer.
- **Plan** (`docs/BLUETOOTH.md`): which HCI events each step needs. After a reset the Event Mask is bits 0 to 44 (Core 6.0, Vol 4, Part E, 7.3.1). Steps 1a to 3b and 5 need nothing more; step 4 needs the Secure Simple Pairing events (bits 48 to 53, 58, 59), so `bluetooth_server` sets the Event Mask in step 4; step 6 adds LE Meta (bit 61) and the LE Event Mask, as the owner chose.
- **Tests:** `scripts/fuzz-bt.sh l2cap` walks random and well-formed signalling packets the way the dispatcher does. `bt_l2cap_abi_test` (`/system/bin`) binds and listens on PSM `0x11`, `0x13`, `0x19`, checks `EADDRINUSE` and `EINVAL`, and checks `GetConnectionState()` and `GetConnectionDetails()`. `run-qemu-smoke.sh` runs it and takes `SRC_IMAGE`.

Manual SMAP review: no new user-memory access in the kernel; the L2CAP changes read only net_buffers (`read()`, header readers) and a kernel `malloc()` copy of the options. The kit and server changes are userland.

Review: every change was read twice (owner, 16 Nov). The second read found that `bt_connection_details` sat inside `namespace Bluetooth` while the header imports only `RemoteDevice`, so the ABI test did not compile; the jam confirmed it, and the struct now sits outside the namespace like `bdaddr_t`.

Tested:
- `scripts/fuzz-bt.sh l2cap 60` in WSL: `l2cap_fuzz: 60s 0 crashes (6242198 rounds)`. With the option bound check removed in a copy, it stopped at once (`check failed: option value inside`).
- Built with `scripts/build-locked.sh`, `jam jobs=12`: `jam_exit=0`, `build_s=179`, no warnings left in the Bluetooth sources.
- QEMU, KVM, `-cpu host,+smap,+smep`, own copy of that image, one boot: `enable SMEP`, `enable SMAP`, no panic; `bluetooth_server: started, 0 radios`, `bt_server_smoke_test: ok (0 crashes)`, `bt_l2cap_abi_test: ok`. Logs: `generated/bluetooth/qemu-step1b/`, `generated/bluetooth/fuzz-l2cap.log`.

Not tested: no radio, so no real L2CAP connection, no incoming connection from a device, no link key reload after pairing. ARM64 and RISC-V 64 were not built or tested.

