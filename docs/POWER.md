# Power in R5

The contract for power management: CPU performance profiles, the Power page in Settings, battery, brightness, sleep and wake. It covers every R5 target: AMD and Intel on AMD64, then ARM64 and RISC-V 64. The mockup is `docs/design/power-mockup/` (owner, 22 Nov 2026). The work order is in the Platform section of `docs/ROADMAP.md`.

Nothing in this document is built yet unless it says so. "Exists" means it is on `main`; "planned" means this contract describes it and the owner must still say "build it".

## 1. What exists (22 Nov 2026, build 10066)

| Part | State |
|---|---|
| AMD CPU frequency | R5's copy of `amd_pstates` turns on CPPC through the MSR and writes min, max and EPP per CPU. Low-latency scheduler mode allows the boost (max = highest, EPP performance). Power-saving mode keeps max at nominal with EPP balance-performance. Stick 49 logs `R5 CPPC:` on all twelve cores of the 5625U |
| AMD without the CPPC MSR | No cpufreq module (older Zen, and firmware that only offers shared-memory `_CPC`) |
| Intel CPU frequency | Haiku's `intel_pstates`: HWP with EPP where the CPU has both, otherwise a model list. Not tested on the X1 Carbon |
| Switch | The scheduler mode only (`_kern_set_scheduler_mode`, ProcessController). No profile, no settings file |
| Idle | `x86_acpi_cstates` (ACPI `_CST`, from air/OS) on the 5625U, `x86_cstates` elsewhere on x86 |
| ARM64, RISC-V | No cpufreq and no cpuidle module |
| Battery, AC, lid, button, thermal | Haiku's ACPI drivers and PowerStatus are in the image; not checked on the two laptops |
| Brightness | No driver path in R5 |
| Sleep | None |
| Information | CPU Info shows whether CPPC and HWP are active (`R5_PSTATE_GET_KERNEL_INFO`) |

## 2. The Power page

A page under System in the Settings window, between Notifications and Virtual memory (alphabetical). It follows the fixed layout in `AGENTS.md` and `docs/SETTINGS.md`.

- **Title and icon.** The title is "Power", and the subtitle and home card read "Battery, performance and sleep". The icon is Haiku's `App_PowerStatus` with the preferences wrench (`r5_settings_apply_prefs_overlay()`), added in `module_icon()`.
- **Layout.** Two columns. Battery with Sleep under it on the left (the wider column), and Performance with Wake under it on the right. When the page is taller than the area, the page scrolls.

### Battery (left, top)

Hidden on a computer without a battery.

- The `R5_LEVEL_BATTERY` glyph from `libr5icons.so`, four steps, in the text color.
- The charge in percent.
- "On battery · about N h N min left", or "Charging · full in about …", or "Plugged in · full".
- "Battery health N % of its design capacity", from full charge capacity divided by design capacity, shown only when the firmware reports both.

### Sleep (left, under Battery)

A grid with two columns, "On battery" and "Plugged in". Without a battery there is one column.

| Row | Choices | Works today |
|---|---|---|
| Turn off the display after | Never, 1, 2, 5, 10, 15, 30 minutes | yes from step 2: the service turns the display off through `BScreen::SetDPMS()` |
| Put the computer to sleep after | Never, 5, 10, 15, 30 minutes, 1 hour | no: gray until sleep works |

Under the grid:

- "When the lid is closed": Sleep, Turn off the display, Do nothing. Hidden without a lid.
- "When the power button is pressed": Sleep, Shut down, Ask, Do nothing.
- "Keep working with an external display": when the lid closes and an external display is connected, the lid action is skipped.
- Until sleep works on this computer, the choice "Sleep" is gray in both menus. A note under the box says "Sleep is not available on this computer yet. Until it is, "Sleep" is gray in these menus."

### Performance (right, top)

- **Profile:** Power saver, Balanced, Performance, separately on battery and plugged in. Without a battery there is one menu.
- One line under the menus explains the chosen profile.
- When the processor offers no way to change its speed (section 4, last column), the menus are gray with "This processor does not let R5 change its speed."
- **Brightness:** a slider. Hidden when no brightness path exists (section 5).
- **"Dim the display on battery":** a checkbox, also hidden without a brightness path.

### Wake (right, under Performance)

- Checkboxes "Wake when the lid is opened", "Wake with the keyboard" and "Wake with the mouse or touchpad". Each one is shown only when the machine can wake that way.
- A line says how this computer sleeps: "Modern Standby (S0ix)", "Suspend to RAM (S3)", or "This computer cannot sleep".
- The whole box is gray until sleep works.

### Lock, Revert, Defaults

- The settings are machine-wide (owner, 22 Nov 2026). Profile, Sleep and Wake need the lock, like Virtual memory: gray, and nothing written, until an administrator opens it.
- Brightness and the Battery box do not need the lock: anyone may change the brightness, and everyone sees the battery.
- A change is applied at once. Revert restores the values from when the page was opened and applies them.
- Defaults (section 7) restores the factory values and applies them. It does not change the brightness.

## 3. Settings file and service

- **File.** `/boot/system/settings/power` (`B_SYSTEM_SETTINGS_DIRECTORY`), driver_settings text, as air/OS's file but machine-wide. The page writes it the same way Virtual memory writes its file. The image creates nothing for it; a missing file means the defaults.
- **Service.** `power_daemon`, imported from air/OS (`src/servers/power/`, MIT) under the import rules in `docs/ROADMAP.md`. It is a system service (uid 0) and it does not create the file.
  - It watches the file with `BPathMonitor`, and AC, lid, power button and thermal trips under `/dev/power/`.
  - It applies the profile (section 4), the display-off and sleep timers, the lid and button actions, and dimming.
  - A critical thermal trip shuts the computer down.
- **Brightness** is a value the service keeps, not part of the machine file: a change from the page is sent to the service and applied at once.
- **Programs** read the battery and the active profile through one additive call in a LibR5 library, not in `libbe` (`AGENTS.md`, LibR5). Recommendation: a new MIT library `libr5power.so` (`R5Power.h`), with the call described in `docs/LIBR5.md` in the same change. PowerStatus and the Deskbar battery use it. The owner decides in step 2.

## 4. Profiles on every processor

The profile is one additive kernel call next to the scheduler mode. The scheduler mode keeps working for ProcessController and old programs. Power saver also sets `SCHEDULER_MODE_POWER_SAVING`; Balanced and Performance set low latency.

The values come from the processor's own capability registers or ACPI tables, never from a number typed in.

| Processor | Power saver | Balanced | Performance | No speed control |
|---|---|---|---|---|
| AMD with the CPPC MSR (exists) | max = nominal, EPP toward energy | max = highest, EPP balance-performance | max = highest, EPP performance | — |
| AMD with shared-memory `_CPC` (planned) | the same values through the ACPI `_CPC` registers | the same | the same | — |
| Intel with HWP and EPP (exists in Haiku) | HWP max = guaranteed, EPP toward energy | HWP max = highest, EPP balance | HWP max = highest, EPP performance | — |
| x86 with ACPI `_PSS` only | the highest allowed state is the nominal one (no boost) | all states, within the firmware's `_PPC` limit | all states, within `_PPC` | — |
| ARM64 (when the target boots) | SCMI performance domain, ACPI CPPC, or device-tree OPP: the lowest third of the table | the full table, chosen on load | the full table, highest first | — |
| RISC-V 64 (when the target boots) | device-tree OPP through the platform's own clock driver | as ARM64 | as ARM64 | — |
| None of the above | — | — | — | the menus are gray |

The constants for EPP are the ones the vendor's documentation names, as `amd_pstates` and `intel_pstates` already use them. Each profile write logs one line per CPU, as `R5 CPPC:` does today.

## 5. Where each value comes from

A value has one source order. The first source that works is used. When none works, the control is hidden or gray, never a guess.

| Value | First | Then | Otherwise |
|---|---|---|---|
| Battery charge, state, time, health | ACPI `_BST`, `_BIX` (or `_BIF`) | — | Battery box hidden |
| AC | ACPI `ACPI0003` | — | "On battery" column hidden |
| Lid, power button | ACPI lid and button devices | — | lid row hidden |
| Temperatures and trips | ACPI thermal zones, `amd_thermal`, `pch_thermal` | — | no thermal shutdown |
| Brightness | the GPU's backlight through the R5 GPU interface (`radeon_hd`, additive call) | ACPI `_BCL`/`_BCM` | slider hidden |
| Sleep states | ACPI FADT low-power-S0 flag and LPS0 `_DSM`; `_S3_` | — | "This computer cannot sleep" |
| Wake at a set time (later) | UEFI `SetWakeupTime` through `generic/efi/v1` | the ACPI RTC alarm | not offered |
| Power off and restart fallback (later) | ACPI | UEFI `ResetSystem` through `generic/efi/v1` | Haiku's existing path |

### The EFI rule

UEFI does not provide battery, AC, lid, temperature or brightness data. The firmware hands over the ACPI tables at boot, and those values are read through ACPI while R5 runs.

Where power work uses the UEFI runtime services:

- It loads `generic/efi/v1` with `get_module()` at the moment it needs it.
- It does not list the module in `module_dependencies`.
- It must work when the module is absent, which happens:
  - after a BIOS start;
  - on ARM64 and RISC-V today;
  - after a firmware fault switched the module off;
  - when the runtime map is incomplete.

  This is the rule in `docs/EFI-RUNTIME.md`, "When the module is absent".
- A new UEFI call (`GetWakeupTime`, `SetWakeupTime`, `ResetSystem`) is added to the module at the end of its interface, in `docs/EFI-RUNTIME.md` in the same change, and only when power work needs it.

## 6. Sleep and wake

- **Order to sleep:**
  1. Stop the timers.
  2. Freeze user threads.
  3. Suspend devices in reverse probe order through the device-manager power hooks (from air/OS).
  4. Bring the GPU and display down under the GFX9 rules.
  5. Take the other CPUs offline.
  6. Enter S0ix or S3.
- **Wake:** the reverse order.
- **S0ix on AMD.** air/OS's `arch_s2idle` is the basis: LPS0 `_DSM` with the AMD and Microsoft UUIDs, and the PMC OS hint. Renoir (the 5625U) is added to its table from the hardware and a cited source.
- **S0ix on Intel.** Its own module, with the Intel LPS0 UUID and the Intel PMC.
- **S3.** Only where the firmware offers it and S0ix does not.
- **ARM64 and RISC-V.** PSCI `SYSTEM_SUSPEND` and SBI SUSP, designed here and built when those targets boot.
- **GPU and display.** A design in `docs/GRAPHICS.md` first. The fixed GFX9 sequences and the stop at shutdown are not changed without the owner's go and a 5625U boot, as `AGENTS.md` requires.
- **Wake sources.** Lid, power button and keyboard, plus the mouse or touchpad where the firmware offers it. A wake only counts when a wake source reported it or the lid is open, as air/OS does.
- **"Sleep works on this computer".** This means the sleep path for this machine's sleep state is built and passed its test. The kernel reports it through the profile call's information block. Until then the page grays Sleep and Wake (section 2).
- **Pass rule on the 5625U:**
  - close and open the lid five times, with no panic;
  - the desktop, Wi-Fi, Bluetooth, USB and sound come back each time;
  - the battery drain over one hour is recorded in `CHANGELOG.md`.

## 7. Defaults

| Setting | On battery | Plugged in |
|---|---|---|
| Profile | Balanced | Balanced |
| Turn off the display after | 5 minutes | 10 minutes |
| Put the computer to sleep after | 15 minutes | 30 minutes |

Other defaults:

- **When the lid is closed:** Sleep. While sleep does not work: Turn off the display.
- **When the power button is pressed:** Sleep. While sleep does not work: Ask.
- **Keep working with an external display:** on.
- **Dim the display on battery:** on.
- **Wake:** lid on, keyboard on, mouse or touchpad off.

## 8. Order of the work

1. This contract and the mockup (done 22 Nov 2026, waiting for "build it").
2. The power service, the profile call on AMD (MSR and shared memory) and Intel, the library call, and the Power page with Sleep and Wake gray. Stick: the profile visible in `R5 CPPC:` lines and CPU Info on the 5625U.
3. Battery, thermal and brightness on the 5625U, and the Deskbar battery through the library.
4. Sleep and wake on the 5625U (its own round, the owner's go first), then Intel on the X1 Carbon.
5. ARM64 and RISC-V, in their own trees, when those targets boot.

## 9. Not in this contract

- Battery charge limits.
- Wake on LAN.
- Hibernation to disk.
- The on-screen brightness and volume panel (air/OS `src/servers/osd`, on the owner's "maybe" list).
- Per-person power settings.
