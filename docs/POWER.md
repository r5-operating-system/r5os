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
| Battery, AC, lid, button, thermal | Haiku's ACPI drivers (`drivers/power/`) and PowerStatus are in the image; not checked on the two laptops. Reading the battery is private to PowerStatus (`DriverInterface`, `ACPIDriverInterface`, `headers/private/device/power_managment.h`); no library offers it |
| Power service | Haiku's `power_daemon` (`src/servers/power/`, launch service `x-vnd.Haiku-power_daemon`, `no_safemode`): it watches AC, the lid and the power button, and the power button shuts down through `BRoster` |
| Brightness | Haiku's `BScreen::SetBrightness()` and `GetBrightness()`, through the accelerant hooks `B_SET_BRIGHTNESS` and `B_GET_BRIGHTNESS`. The radeon_hd accelerant has `radeon_set_brightness()` and intel_extreme has `intel_set_brightness()`. Not checked on the 5625U, whose picture is still the firmware one |
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

- The settings are machine-wide (owner, 22 Nov 2026). Profile, Sleep and Wake need the lock: gray, and nothing sent, until an administrator opens it.
  - A change goes to the power service as an admin request through `r5_admin_send()` (`libr5admin.so`, `docs/LIBR5.md`, "Admin requests").
  - The kernel names the sender, the service checks that the sender is an administrator, and the service writes the file. The page does not write it.
  - The service never decides by `BMessage::ReturnAddress()` or `IsSourceRemote()`.
- Brightness and the Battery box do not need the lock: anyone may change the brightness, and everyone sees the battery.
- A change is applied at once. Revert restores the values from when the page was opened and applies them.
- Defaults (section 7) restores the factory values and applies them. It does not change the brightness.

## 3. Settings file and service

### File

- **Where.** `power` in `B_SYSTEM_SETTINGS_DIRECTORY`, found with `find_directory()`, never a path in the code. Today that is `/boot/system/settings/power`.
- **Format.** driver_settings text, as air/OS's file, but machine-wide.
- **Who writes it.** Only the power service writes it, after an admin request (section 2). The image creates nothing for it, and a missing file means the defaults.

### Service

The service is Haiku's own `power_daemon` (`src/servers/power/`, MIT): the launch service `x-vnd.Haiku-power_daemon`, with its AC, lid and power-button monitors. It is copied into `distro/` with its header kept, and the policy of air/OS (`power_policy.cpp` and the thermal monitor, MIT) is added on top under the import rules in `docs/ROADMAP.md`. It is not a second daemon.

- It runs as a system service (uid 0) from the launch file, as today.
- It watches its file with `BPathMonitor`, and AC, the lid, the power button and thermal trips under `/dev/power/`.
- It applies the profile (section 4), the display-off and sleep timers, the lid and button actions, and dimming.
- A critical thermal trip shuts the computer down through `BRoster`, as the power button does today.
- It takes admin requests on its admin port through `libr5admin.so` (section 2), and ordinary `BMessage`s on its looper for everything that needs no lock.
- Haiku's launch file marks it `no_safemode`. In Safe mode there is therefore no profile change, no timer and no sleep: the processor runs as the kernel module starts it. The page then shows "Power settings are off in Safe mode" and grays every group except Battery.

### Brightness

- Brightness uses Haiku's existing call, `BScreen::SetBrightness()` and `GetBrightness()`, which reaches the accelerant hooks `B_SET_BRIGHTNESS` and `B_GET_BRIGHTNESS`. There is no new R5 GPU interface call for it.
- The page and the service call `BScreen`. The service keeps the value for dimming and puts it back after a wake.
- Where the accelerant has no hook (the firmware picture, VESA), the service tries ACPI `_BCM`. Where neither exists, the slider is hidden.
- Brightness is not part of the machine file, and it needs no lock.

### The Power Kit: `libr5power.so`

Nothing like it exists today: reading the battery is private to PowerStatus. The owner approved a new library (22 Nov 2026).

- **Where.** A new MIT shared library `libr5power.so` next to `libbe`, from `distro/power/lib`, installed in `/system/lib` on the regular and the minimum image, like the other LibR5 libraries. It is described in `docs/LIBR5.md` and R5Book in the same change, and is never part of `libbe`.
- **The class.** `BPowerRoster`, in the style of `BNetworkRoster` and `BGameController`. The name is checked against the Haiku tree when the header is written; there is no clash on 22 Nov 2026. It offers:
  - `GetBatteries()`: charge, state, time left and health per battery;
  - `IsOnAC()`;
  - `Profile()`;
  - `CanSleep()`: the sleep state, and whether sleep works on this computer;
  - `StartWatching(BMessenger)` and `StopWatching()`.
- **Changes reach programs as `BMessage`s.** A watching program gets one message code (named as in `R5Bluetooth.h`) with a field that says what changed: the battery, AC, the profile, "will sleep" or "did wake". A program never polls.
- **Who uses it.** PowerStatus and its Deskbar replicant read through the library. PowerStatus's private driver interface moves into the library instead of being written again. The Settings page and the service use it too.
- **Setting the profile.** The library sets the profile through the service, as an admin request; it does not call the kernel directly. Reading needs no rights.

### Low battery

- The service sends a `BNotification` through Haiku's notification server at the low and critical levels the firmware reports (`_BIX` warning and low capacity).
- It uses Haiku's icons `Battery_Low` and `Battery_Critical`.
- At the critical level it acts as the lid action says (sleep when sleep works, otherwise nothing), and it says so in the notification.

## 4. Profiles on every processor

The kernel side follows the way Haiku already switches the scheduler mode:

- **The call.** A private syscall next to `_kern_set_scheduler_mode()`. Only the power service calls it, and programs go through `BPowerRoster`. The scheduler mode keeps working for ProcessController and old programs.
- **The scheduler mode.** Power saver also sets `SCHEDULER_MODE_POWER_SAVING`; Balanced and Performance set low latency.
- **The cpufreq modules.** They get the profile through a new version of the cpufreq module interface: the existing `cpufreq_module_info` stays as it is, and a module that offers the new hook publishes the new version name. A module that only has the old version keeps following the scheduler mode.

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
| Brightness | `BScreen::SetBrightness()`, through the accelerant's `B_SET_BRIGHTNESS` hook (radeon_hd, intel_extreme) | ACPI `_BCL`/`_BCM` | slider hidden |
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

- **Programs are told first, the BeOS way.**
  - Before anything stops, the power service sends "will sleep" to every program that watches through `BPowerRoster` (section 3), as the registrar asks programs before a shutdown. Media, network and Bluetooth servers use it to finish what they are doing.
  - The service waits for their replies for a bounded time, set when step 4 is designed and written here. A program that does not answer does not stop the sleep.
  - After the wake, every watching program gets "did wake".
- **Order to sleep:**
  1. Tell the programs (above).
  2. Stop the timers.
  3. Freeze user threads.
  4. Suspend devices in reverse probe order through the device-manager power hooks (from air/OS).
  5. Bring the GPU and display down under the GFX9 rules.
  6. Take the other CPUs offline.
  7. Enter S0ix or S3.
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
2. Haiku's `power_daemon` in `distro/` with air/OS's policy and the admin port, `libr5power.so` with `BPowerRoster`, the profile syscall and the new cpufreq interface on AMD (MSR and shared memory) and Intel, and the Power page with Sleep and Wake gray. Stick: the profile visible in `R5 CPPC:` lines and CPU Info on the 5625U.
3. Battery, thermal, the low-battery notification and brightness (`BScreen` first) on the 5625U. PowerStatus and its Deskbar replicant move to `BPowerRoster`.
4. Sleep and wake on the 5625U (its own round, the owner's go first), then Intel on the X1 Carbon.
5. ARM64 and RISC-V, in their own trees, when those targets boot.

### R5Book and the other documentation

Every step that ships something a developer can use updates R5Book (`docs/r5book/`) in the same change. A step is not done while R5Book still describes the old state.

- **A new page `power-kit.html`,** in the layout of `bluetooth-kit.html` and `game-kit.html`. It covers:
  - `BPowerRoster`, with every call, the watch messages and their fields, and a short example (a program that pauses its work on "will sleep" and resumes on "did wake");
  - how brightness goes through `BScreen::SetBrightness()`;
  - what a program gets on a computer without a battery, in Safe mode, and before sleep works.
- **Links to that page** from `index.html`, `class-index.html` (`BPowerRoster`) and the page table in `docs/r5book/README.md`.
- **`admin-kit.html`:** the power service as an example of a service that takes admin requests.
- **`docs/LIBR5.md`:** `libr5power.so` and its calls (`AGENTS.md`, LibR5), in the same change as the code.
- **`docs/SETTINGS.md`:** the Power page, with its fixed place, description and lock rules.
- **Step by step:**
  - step 2 writes the page with `BPowerRoster`, the profile and the watch messages;
  - step 3 adds the battery, brightness and the low-battery notification;
  - step 4 adds "will sleep" and "did wake", with the example.

## 9. Not in this contract

- Battery charge limits.
- Wake on LAN.
- Hibernation to disk.
- The on-screen brightness and volume panel (air/OS `src/servers/osd`, on the owner's "maybe" list).
- Per-person power settings.
