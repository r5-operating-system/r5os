# R5 roadmap

Product direction for R5 Operating System (R5). This is not a license to implement everything in one patch. Agents: follow `AGENTS.md`, and pick work from the current user request.

R5 modernizes a Haiku-based desktop without waiting on upstream schedules. Compatibility with existing Haiku applications stays a design goal (`AGENTS.md`, "Compatibility").

## Where R5 stands (November 2026)

Tree `2026.1-dev`, build 10066 (checked against `origin/main` on 22 Nov 2026). There is no public release yet. The details, build by build, are in `distro/WHATSNEW` (user language) and `CHANGELOG.md` (technical).

| Area | In the image now |
|---|---|
| Desktop | R5 branding, dark mode, the R5 window frame, the Settings window (`docs/SETTINGS.md`) with pages for Display, Appearance, Desktop and Deskbar, Notifications, Virtual memory, Wi-Fi, Network, Bluetooth and Game controllers; Device Manager; CPU Info; R5Book on disk |
| Accounts | Sign in, Log out, Lock screen, Users & Accounts, private home folders, the administrator off uid 0 after the first Sign in, locked machine settings (`docs/MULTIUSER.md`) |
| Network | Wi-Fi join and remember on the 5625U's MediaTek MT7921/MT7922 (WPA2 and WPA3 joined), the Connections and Discovery pages, IPv6 (link-local, SLAAC, DAD, DHCPv6), `.local` next to DHCP DNS, SMB network shares mounted from Tracker, AirPrint/IPP, AirPlay 1 to speakers, the network tray |
| Bluetooth | Classic and Low Energy, pairing, HID (mice, keyboards, game controllers), A2DP headphones and speakers, HFP battery and buttons, the Bluetooth page and Deskbar item. Tested on real hardware (18 Nov 2026, and the owner on 22 Nov 2026). The headset microphone stays off until it is tested |
| Sound and MIDI | Better resampling, a latency choice, USB channel layouts, steadier USB audio and MIDI (RenkuOS fixes), HDMI output names, virtual surround, MIDI 2.0 (`docs/MIDI2.md`), MIDI Monitor, USB webcams (UVC) |
| Graphics | On the Ryzen 5 5625U (`0x15e7`, release-supported since M10) the GPU is the default: RADV for Vulkan, Zink for OpenGL, app_server composes the desktop on the GPU (shadows, frosted glass, animations), and windows are drawn with Skia on the GPU (build 10066). A start-up check falls back to software, and a hang resets the GPU. M0–M11 and M13 are done on the 5625U (`docs/GPU-ARCHITECTURE.md`, `docs/M13-PLAN.md`). Every other computer gets the same desktop on llvmpipe and Lavapipe; Safe mode gives the classic drawing. Display modesetting (DCN) is not done; the picture is still the firmware one |
| CPU and kernel | AMD CPPC frequency control, SMT and cache topology on Zen, a speed per core for hybrid CPUs, ACPI deep idle, 2 MB pages, TSC-deadline timer, XSAVEC, MWAIT wake, EFI runtime services, release and debug kernel profiles |
| Input | Touchpads and TrackPoint (RenkuOS), I2C HID |
| Virtual machines | VirtualBox 7.2.22 Guest Additions (`docs/VBOX-GUEST.md`) |
| Licensing | Multi-license policy, notices, and licenses in About (`docs/LICENSING.md`); R5's own code is MIT since 12 Nov 2026 |

Started is not done. A feature counts as done only after a test on the hardware or the network it is meant for (`AGENTS.md`, "Tracker and changelog" and "Networking claims").

## Near term (user-visible)

The order is impact, not ticket number. `[x]` is done and tested where it is meant to run; `[~]` is in the image but not tested there yet; `[ ]` is open.

1. **GPU on the reference machine.** Following `docs/GPU-ARCHITECTURE.md`:
   - [x] M0–M5: the kernel driver owns the GPU, the R5 GPU interface, the R5 DRM library, RADV, an address space per program.
   - [x] M6–M11: the RADV workload, Vulkan in a window, the GPU copy, Zink for OpenGL, release-supported on the 5625U (M10), the desktop composed on the GPU (M11).
   - [x] M13 (20 Nov 2026, sticks 65–67): direct scanout, write-combined GTT and the GPU copy on by default on the 5625U.
   - [x] M12, the GPU draws everything: Skia drawing is on by default since build 10066. Done on the 5625U (owner, 22 Nov 2026); stick 73 logs `gpu draw on AMD Radeon Graphics (RADV RENOIR) skia` with no panic.
   - [x] The goal: on a supported GPU that passes its checks, R5 renders on the GPU by default, otherwise in software. Haiku applications are unchanged.
   - [ ] The Lavapipe warm-up, and the other open points in `AFTER-M13.md` ([#4501](https://github.com/r5-operating-system/r5os/issues/4501)).
   - [ ] Display (DCN modesetting, more than one screen, vsync, [#4502](https://github.com/r5-operating-system/r5os/issues/4502)) is a separate track. Today the picture is still the firmware one.
   - [ ] Intel as the second vendor (step I0 in `docs/GPU-ARCHITECTURE.md`, [#4503](https://github.com/r5-operating-system/r5os/issues/4503)).
2. **Finish multi-user** ([#63](https://github.com/r5-operating-system/r5os/issues/63), open).
   - [x] The lock screen ([#64](https://github.com/r5-operating-system/r5os/issues/64), build 10013).
   - [ ] The keyboard shortcut for the lock screen ([#72](https://github.com/r5-operating-system/r5os/issues/72), open).
   - [ ] Guest plus Public/Shared ([#65](https://github.com/r5-operating-system/r5os/issues/65), open).
   - Still one graphical session at a time. Do not skip isolation, and do not treat a list of names as "done".
3. **Other hardware on the Ryzen 5 5625U:**
   - [x] Wi-Fi on the laptop's own card (MediaTek MT7961): the owner joined a WPA2 network on stick 51.
   - [x] WPA3 (SAE): fixes after sticks 59 and 60 (19 Nov 2026). The owner joined a WPA3 network on the 5625U (reported 22 Nov 2026).
   - [x] The speakers (CS35L41, #4482): on stick 73 (22 Nov 2026) both amplifiers run the speaker-protection firmware (`DSP state 2 (running)`) with the factory calibration from EFI. The owner confirmed sound on 22 Nov 2026.
   - [ ] Backlight and brightness keys ([#4504](https://github.com/r5-operating-system/r5os/issues/4504)).
   - [ ] Sleep and resume (see "Platform", [#3568](https://github.com/r5-operating-system/r5os/issues/3568), Haiku #19487).
   - [ ] HiDPI without breaking the layout ([#1469](https://github.com/r5-operating-system/r5os/issues/1469), Haiku #13087).
4. **Browser:**
   - [~] Aomi, a WebPositive copy on haikuwebkit 1.10.0-2: the plan is merged (`docs/AOMI.md`, #4467); nothing is built yet ([#4507](https://github.com/r5-operating-system/r5os/issues/4507)).
5. **Network, what is left:**
   1. [~] IPv6: the review fixes are in (DHCPv6, SLAAC, the resolver, the default route). More LAN tests; DHCPv6 IA_NA still needs a LAN with a DHCPv6 server ([#36](https://github.com/r5-operating-system/r5os/issues/36), [#37](https://github.com/r5-operating-system/r5os/issues/37), [#38](https://github.com/r5-operating-system/r5os/issues/38)).
   2. [~] DNS from DHCP next to `.local`: in the image since the Network pages (#4240), with mDNS fixes on 17 Nov 2026. Not tested on a LAN ([#4508](https://github.com/r5-operating-system/r5os/issues/4508)).
   3. [ ] Captive portal ([#2793](https://github.com/r5-operating-system/r5os/issues/2793), Haiku #17567).
   4. [~] SMB in Tracker: smbfs and libsmb2 from air/OS, mounted from the Network shares page and the Mount menu (build 10050). No LAN test recorded ([#4509](https://github.com/r5-operating-system/r5os/issues/4509)).
   5. [ ] A simple firewall, with incoming connections closed by default ([#4510](https://github.com/r5-operating-system/r5os/issues/4510)).
   6. [ ] WireGuard ([#4511](https://github.com/r5-operating-system/r5os/issues/4511)).
   7. [ ] HTTP/2 (owner, 19 Nov 2026, [#4512](https://github.com/r5-operating-system/r5os/issues/4512)). HTTP/2 under `libnetservices` and `libnetservices2` is for the programs that use those kits (WebPositive's own downloads, HaikuDepot, the package kit). Web pages in WebKit already go through libcurl, and curl-8.21.0-1 already requires libnghttp2. That path is unproven until a page shows HTTP/2. Aomi's engine plan is in `docs/AOMI.md`.
   8. [ ] LAN tests: VirtualBox bridged (`docs/vbox.md`), or QEMU bridged/tap. QEMU usernet is not a LAN.
6. [ ] **Dark mode in the remaining system applications** and Deskbar ([#3342](https://github.com/r5-operating-system/r5os/issues/3342), Haiku #18970). The dark color scheme exists; some applications still keep their own look.
7. [x] **Bluetooth and game controllers** (`docs/BLUETOOTH.md`, `docs/GAME-CONTROLLERS.md`). Classic HID (step 4), Low Energy (steps 1c and 6), audio (step 5, A2DP), HFP (step 7) and the Game controllers page are in. Tested on real hardware on 18 Nov 2026, and confirmed by the owner over Bluetooth on 22 Nov 2026. Still open: the headset microphone (off until tested), and resolving private addresses in the host for a radio without LL Privacy.
8. [ ] **Polish** ([#4513](https://github.com/r5-operating-system/r5os/issues/4513)): screenshots, emoji and fonts, the image clipboard. Rounder windows and Deskbar: `docs/LOOK.md`, not while multi-user is still open. The R5 window frame is the default since 13 Nov 2026; round corners are a later decorator.
9. **Download, installer next to Windows, and Software update** (owner, 23 Nov 2026). Design: `docs/design/INSTALL-UPDATE.md`. Brief: `docs/briefs/INSTALL-UPDATE.md`. First for testers and enthusiasts. One PR per step.
   - [x] The image has no Haiku system repository, so Software Update cannot replace R5's system with plain Haiku (#4570). Built and checked in the image; not booted.
   - [ ] Step 0: report (Windows from the boot loader, R5 versions above Haiku's, what SoftwareUpdater offers, NTFS hibernation and BitLocker checks).
   - [ ] Step 1: R5's own package repository on GitHub Pages (deployed by GitHub Actions), the last 3 builds; an R5 domain on top later.
   - [ ] Step 2: the Software update page in Settings (System), per change, Installed list, "Go back to before <date>".
   - [ ] Step 3: the installer: erase the disk (from air/OS), the read-only live stick, language, keyboard and Wi-Fi carried over.
   - [ ] Step 4: the installer next to Windows: the checks, `ntfsresize` (GPL-2.0-or-later, a separate program), 64 GB suggested, the backup tick.
   - [ ] Step 5: the boot menu with "R5 Operating System", "Windows", the previous state and Safe mode.
   - [ ] Step 6: the ISO download, compressed on a GitHub Release with its checksum.

Zeroconf, AirPrint, AirPlay 1 and the network tray exist. They are not "done" until LAN tests exist.

## Before the first public release

Things that are fine in a development build but must be settled before R5 2026.1 (or whichever version is first) is published.

- **A MIDI manufacturer ID, only if R5 ships its own MIDI 2.0 device.** R5 itself does not present itself to MIDI devices: the identity in MIDI-CI and the UMP Device Identity belongs to the program (`midi2_pe_catalog_set_identity`, `midi2_endpoint_set_identity`), and the kit's default is `0x7D`, the SysEx ID for educational and non-commercial use. R5 needs its own Manufacturer SysEx ID from the MIDI Association (midi.org, about $260 a year, or included with a paid membership) only when R5 ships a program that acts as a MIDI 2.0 device, such as a built-in synthesizer. Before the release, check that no R5 program does that with `0x7D`. If an ID is ever bought: register it under the project name, not a personal name (the list is public), and ask first whether there is an arrangement for open-source projects.

## Named products (later)

Do not start these during an unrelated bugfix.

| Name | Intent | Status |
|---|---|---|
| **R5 Hub** | One place for settings: devices, display, audio, network, printers, updates | Started: the Settings window has Display, Appearance, Desktop and Deskbar, Notifications, Virtual memory, Wi-Fi, Network, Bluetooth and Game controllers (`docs/SETTINGS.md`). Not yet: power, audio, printers, updates, storage |
| **R5 Quicklaunch** | Keyboard launch, and search for applications, files and system items | Not started |
| **R5 Connect** | Device discovery (AirPrint, AirPlay, later SMB and printers) | Started: Network > Discovery lists printers, AirPlay speakers and other computers; SMB shares mount from Tracker. No product yet |
| **R5 Store** | Installing software from Haiku packages | Not started |
| **R5 SafeUpdate** | Validated updates and recovery *before* disruptive changes | Designed (23 Nov 2026): the Software update page, R5's own package repository and "Go back to before <date>", Near term item 9. Not built |
| **R5 Doctor** | Logs, crashes, drivers, common failures | Not started |

## Graphics and media

- **GPU.** The plan is `docs/GPU-ARCHITECTURE.md`, and the measured state is `docs/GRAPHICS.md`. The software path always stays.
  - Do not destabilize `app_server`.
  - Do not call VESA, `vboxvideo`, Lavapipe, or GPU work without a rendering path "hardware acceleration".
  - The GFX9 sequences on the 5625U are fixed (`AGENTS.md`, "GFX9 ring (Renoir)").
- **GPU Driver Kit.** Decided on 7 Nov 2026: adding a GPU to R5 must be easy. A driver author describes the chip, fills in the vendor backend, and runs the conformance tests. R5 provides the common core, the passport, a template backend, the tests, the Linux reference kit, and a porting guide. The plan and phases are in `docs/GPU-ARCHITECTURE.md`, "GPU Driver Kit". Intel is the proof that the kit works.
- **Graphics Kit (after M7).** A C++ kit on top of Vulkan, in the style of the BeOS kits. Decided on 6 Nov 2026, not started:
  - **Names.** The kit is the **Graphics Kit**, named after the domain, like the Interface Kit and the Media Kit. The classes live in the namespace **`BGraphicsKit`**, the way Haiku does it for `BPackageKit`. Inside that namespace they carry ordinary B names: `BVulkanView` (in the line of `BGLView`), `BGPUDevice`, `BGPUBuffer`, `BGPUTask`. There is one header per class without the B (`<VulkanView.h>`), plus `<GraphicsKit.h>`. Constants are `B_GPU_…`, errors are `status_t`, and hooks follow the usual pattern (`Render()`, `FrameResized()`, `DeviceLost()`). Check every new name against the Haiku tree. The documentation says once that the Interface Kit draws through app_server, and that the Graphics Kit is the layer for drawing and computing on the GPU, with Lavapipe as the fallback.
  - **The BeOS way.**
    - A finished task sends a `BMessage` to the `BLooper`, instead of the program waiting on a fence.
    - A view draws in its own thread.
    - Without a GPU, the same code runs on Lavapipe.
    - A lost device gives `B_GPU_DEVICE_LOST`, and the kit rebuilds the device.
    - The kit does not lock Vulkan away: the `VkDevice` and `VkImage` underneath stay reachable.
  - **Every GPU.** The kit works the same on every GPU that has a Vulkan driver: AMD (RADV), Intel (ANV) and NVIDIA (NVK), and on Lavapipe without a GPU.
    - The kit contains no vendor code and no vendor checks.
    - A program never needs to know which vendor it runs on. `BGPUDevice` reports the name and the capabilities, and nothing more.
    - Vendor differences belong in the R5 GPU interface (the vendor block) and in the Mesa drivers, never in the kit.
  - **Where.** A separate library `libr5graphics.so` next to `libbe`, not inside `libbe` (`AGENTS.md`, LibR5). It is MIT, like the GPU header, because every program that uses it links it.
  - **Who it is for.** Three groups, in one kit:
    - games and 3D through `BVulkanView`;
    - simple GPU computing through `BGPUTask` and `BGPUBuffer`;
    - R5's own components, which use the same parts as they appear.
  - **How thin.**
    - `BVulkanView` is a thin shell, as `BGLView` was. The kit handles the instance, the device, the swapchain, resizing, the drawing thread and a lost device. The program writes plain Vulkan in `Render()`.
    - `BGPUTask` and `BGPUBuffer` are thicker. A program computes without writing Vulkan, and can still reach the `VkDevice` underneath.
    - No API of its own that hides Vulkan.
  - **OpenGL.** Not in the kit. `BGLView` already covers it, and moves to the GPU by itself with Zink (M9). A buffer bridge between the two can come later.
  - **R5 first, portable by design.**
    - The kit uses only Vulkan, the public Haiku API, and a small internal layer for what is R5-specific: device choice and the M7 presentation.
    - It depends on no other R5 library unless that is really needed.
    - It is not tested on stock Haiku for now.
    - Shipping it as a package for Haiku is a separate decision later. Haiku itself does not accept AI-assisted code (`AGENTS.md`).
  - **Order.**
    1. `BGPUTask` and `BGPUBuffer` may come after M6, because they need no window.
    2. `BVulkanView` comes after M7, with VulkanTeapot rewritten on top of it as the first example.
    3. Integration with the Media Kit comes after that.
  - **Before M7:** build nothing for `BVulkanView`, but design the M7 presentation (the R5 WSI layer) so that `BVulkanView` can sit on top of it.
- **Audio and MIDI.** Improve streaming and device interop, in small steps. The contracts are `docs/USB-AUDIO.md`, `docs/HDMI-AUDIO.md`, `docs/VIRTUAL-SURROUND.md` and `docs/MIDI2.md`.
- **Mixxx** is a side quest ([#55](https://github.com/r5-operating-system/r5os/issues/55), `docs/MIXXX.md`). It is not an image application, and not while multi-user is open.

## Architectures

| Target | Status | Next |
|---|---|---|
| AMD64 | Boots to the desktop in QEMU, in VirtualBox, and from USB on the Ryzen 5 5625U. Not release-supported | Hardware items above |
| ARM64 | No work yet. Basis decided (7 Nov 2026): the haiku-pi port, see below | After AMD64 is usable on hardware: QEMU, then the Raspberry Pi 4 as the first board |
| RISC-V 64 | No work yet. Reference: X547's port (see below) | After AMD64 and ARM64 |

Levels: buildable → bootable → functional → desktop-capable → release-supported. See `AGENTS.md`, "Architectures".

### ARM64 from haiku-pi

[haiku-pi](https://github.com/haiku-pi/haiku-rpi) ports Haiku to the Raspberry Pi 4 (BCM2711, Cortex-A72). Its Haiku sources are the fork [haiku-pi/haiku](https://github.com/haiku-pi/haiku), branch `rpi4`. That port reaches the Haiku desktop on the Pi, with drivers for the board's hardware. R5 takes that port as the basis of its ARM64 target, instead of starting from Haiku's own arm64 tree.

- **What R5 takes:** the arm64 boot path (the EFI loader under U-Boot, the EL2 to EL1 drop, secondary CPU start-up), the non-coherent DMA handling, the interrupt controller (GICv2), device-tree address translation, and the drivers for the Pi 4's on-board hardware.
- **Reference only:** the V3D GPU driver and how it draws Vulkan into ordinary Haiku windows. R5 keeps its own vendor-neutral GPU interface (`docs/GPU-ARCHITECTURE.md`). The V3D work is input for the M7 presentation and the Graphics Kit.
- **Not taken:** the packages (Firefox, Wine, games). They belong in a package repository, not in this source tree.
- **When:** after AMD64 is usable on hardware, the same rule as above. First QEMU, then the Pi 4.

### RISC-V reference: X547's port

[X547/Haiku-riscv](https://github.com/X547/Haiku-riscv) is the 2021 Haiku port to RISC-V. It reached the desktop on the SiFive HiFive Unmatched board, with a Radeon card over PCIe. The repository holds a patch set for one exact Haiku revision, and has had no changes since October 2021. Haiku's own riscv64 tree grew out of this work.

- **Reference only.** When R5 starts RISC-V, the basis is Haiku's current riscv64 tree. The patch set shows what the first hardware bring-up needed.
- **Of interest to the GPU plan:** the port drew its desktop through `radeon_hd` on a RISC-V board. That is a check for later that R5's GPU interface and Radeon backend stay free of x86 assumptions.
- **Licence:** the repository has no licence file. Nothing is copied from it. Haiku's riscv64 code is used under its own licence instead.

### Import rules for other Haiku forks

These rules apply to every import from another Haiku fork: haiku-pi, air/OS and RenkuOS (see "Other Haiku forks as sources" under "Upstream"):
- **Per commit,** never one large merge. Each fork is based on its own Haiku revision (haiku-pi hrev60314, air/OS hrev60207, in November 2026), and R5 checks each change against its own overlays and APIs, as for `upstream`.
- **Native source in `distro/`,** as `AGENTS.md` describes. The fork's file header stays exactly as it is, and R5 changes on top are marked with SPDX snippets.
- **Licence.** Only code under a licence R5 can combine with is imported. Each import gets a line in `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`, with the fork, the commit and the author.
  - haiku-pi: new files in `haiku-pi/haiku` carry an MIT header. Third-party code inside keeps its own licence (OpenBSD's bwfm driver, ISC; Mesa's DRM headers, MIT; Arm Optimized Routines, MIT OR Apache-2.0 WITH LLVM-exception). The coordination repository `haiku-pi/haiku-rpi` has no licence file: nothing is copied from it unless its owner agrees.
  - air/OS: new files in `airos-works/haiku` carry an MIT header. Its AMD driver (`airos-works/linux-drm`, Linux 6.18 amdgpu) is GPL-2.0-only and is not imported: R5's GPU interface is its own small driver, and GPL-2.0-only cannot be combined with GPL-3.0 (`AGENTS.md`). R5 may read it as a reference. The applications (the Craft suite, Summit, Mail and others) state no licence and are not imported.
  - RenkuOS: MIT only (its decision D3). Commits carry a DCO sign-off and the author's name, which R5 keeps.
- **One source per area.** The forks overlap (Raspberry Pi 4, Wi-Fi, USB) and also take from each other. Per area R5 picks one source, so that no driver is compared three times.
- **Tested before it lands.** Much of this code is written with AI help. Each import is read against the source, built, and booted (QEMU, plus the 5625U for x86-64 or the Pi 4 for ARM64).
- **Overlap with R5's own work.** Where R5 already has its own version (`radeon_hd`, USB audio, MIDI, the Settings window), a fork's change is read and judged per fix, never copied over R5's file. The fixed GFX9 sequences in `radeon_hd` are not touched.

## Platform (CPU, power and firmware)

What the kernel knows and does about the processor and the firmware. The audit of the current state is `docs/KERNEL.md`. Each item starts only when there is hardware to test it on.

| Item | Where R5 stands | What it takes | Test hardware |
|---|---|---|---|
| **P-states (CPU frequency)** ([#86](https://github.com/r5-operating-system/r5os/issues/86), power profiles [#4506](https://github.com/r5-operating-system/r5os/issues/4506)) | AMD: R5's copy of `amd_pstates` turns on CPPC and writes a request per CPU (min = lowest non-linear, max = highest, EPP performance; in power-saving mode max = nominal, EPP balance-performance). Stick 49 logs `R5 CPPC:` on all twelve cores of the 5625U. AMD without the CPPC MSR (older Zen, shared-memory `_CPC`) has no cpufreq module. Intel: Haiku's `intel_pstates` uses HWP with EPP where the CPU has it, otherwise a model list; not tested on the X1. The only switch is the scheduler mode (ProcessController); there is no profile in Settings. ARM64 and RISC-V have no cpufreq module. CPU Info shows CPPC and HWP (`r5_pstate` ioctl). Deep idle: `x86_acpi_cstates` on the 5625U (stick 49) | A power profile (balanced, performance, saver) that sets EPP and limits on every vendor; shared-memory CPPC for AMD; the X1 measured; ARM64 (SCMI/CPPC/OPP) and RISC-V (SBI/OPP) when those targets boot | Ryzen 5 5625U, X1 Carbon Gen 8 |
| **Sleep and resume** ([#3568](https://github.com/r5-operating-system/r5os/issues/3568), Haiku #19487) | Open (see "Near term", item 3). air/OS has S3 and suspend to idle for AMD (LPS0 `_DSM`, the PMC hint), device power hooks in xHCI, NVMe, PCI and the network drivers, and `power_daemon` with lid actions; no Intel S0ix and no GPU or audio resume there | Many current laptops have no classic S3 any more, only S0ix ("Modern Standby"). Find out which the two machines offer, then make that path work, including the GPU, Wi-Fi and USB coming back | Ryzen 5 5625U, X1 Carbon Gen 8 |
| **Battery, thermal, backlight, embedded controller** ([#4505](https://github.com/r5-operating-system/r5os/issues/4505), [#4504](https://github.com/r5-operating-system/r5os/issues/4504)) | Partly through Haiku's ACPI drivers; not checked on the two machines | The ordinary laptop functions, read from ACPI: charge level, temperatures and fan, brightness keys | Ryzen 5 5625U, X1 Carbon Gen 8 |
| **CPU topology (ACPI PPTT)** ([#81](https://github.com/r5-operating-system/r5os/issues/81), [#82](https://github.com/r5-operating-system/r5os/issues/82), [#4514](https://github.com/r5-operating-system/r5os/issues/4514)) | SMT and cores on AMD Zen from CPUID `0x8000001E` (stick 47), and the scheduler uses the cache domains (18 Nov 2026). ACPI PPTT is not read | Read PPTT where CPUID does not tell (ARM64 servers, clusters), and let the scheduler use that | Any current CPU |
| **Hybrid cores (P and E)** ([#84](https://github.com/r5-operating-system/r5os/issues/84)) | Each CPU has a speed (`cpu_ent::capacity`: P 1024, E 600, Zen 5c 768) and the scheduler prefers the faster cores (air/OS, build 10059). Not tested: no hybrid machine | Later Intel Thread Director (HFI), and AMD preferred cores from the CPPC highest value (stick 49 shows 166 to 186 on the 5625U) | A 12th-generation Intel or newer. The X1 Carbon Gen 8 has no E-cores |
| **More than 64 logical CPUs** ([#79](https://github.com/r5-operating-system/r5os/issues/79), [#78](https://github.com/r5-operating-system/r5os/issues/78), [#80](https://github.com/r5-operating-system/r5os/issues/80), [#83](https://github.com/r5-operating-system/r5os/issues/83), [#87](https://github.com/r5-operating-system/r5os/issues/87)) | `SMP_MAX_CPUS` is 64 on both AMD64 boot platforms (`docs/KERNEL.md`, finding 2). The BIOS loader cannot start x2APIC processors (finding 3) | Larger CPU sets and per-CPU tables in the kernel, x2APIC on every boot path, and a check of the public API for structs with a fixed CPU count | QEMU with many virtual CPUs first, then a large Ryzen, Threadripper or EPYC |
| **NUMA (ACPI SRAT and SLIT)** ([#85](https://github.com/r5-operating-system/r5os/issues/85)) | Not present | Keep a program and its memory on the same node, in the allocator and the scheduler. Only useful on machines with more than one memory node | Multi-socket or Threadripper/EPYC hardware |
| **Memory types for RAM (PAT only)** | AMD64 flushes with `wbinvd` before the page tables change, and a range inside the boot RAM MTRR is not a second MTRR when PAT is on. `vm_set_area_page_memory_type()` sets the page type only, for a buffer that is not one physical range. A clone copies the area's memory type. Device memory outside RAM still gets an MTRR. Write-combined GTT follows `gfx9_wc_gtt` (`docs/KERNEL.md`): on for a release-supported GPU, and the AMD64 direct map matches. ARM64 and RISC-V still use the empty flush. Stick 67 (20 Nov 2026) booted write-combined GTT on by default on the 5625U | The other two architectures follow in their own trees. A boot with `gfx9_wc_gtt false` is still to come (`AFTER-M13.md`) | QEMU or VirtualBox, then the 5625U |

The order follows what R5 is today, a laptop and desktop system: first P-states, sleep and the laptop functions, then topology, then hybrid cores, and only for workstation or server use more than 64 CPUs and NUMA.

## Upstream

Fetch `upstream` when a Haiku fix is useful. Inspect the `distro/` changes and R5 behavior before a merge. Record imports, local patches, and intentional differences in `CHANGELOG.md`. Do not send changes back to Haiku (`AGENTS.md`).

### x86-64 fixes from haiku-pi

About 120 haiku-pi commits change code that x86-64 also builds and runs: the kernel, the VM, the runtime loader, libroot, BFS, the block cache, the kits and the servers. Their docs (`docs/HAIKU-RPI4-CHANGES.md`, the rows marked ★) list them as fixes that are not Pi-specific. haiku-pi built and tested that tree for x86_64 under KVM on 4 Nov 2026, and reports that 14 tests that fail on upstream Haiku pass with those fixes. Examples are a kernel panic on `read()` into a guard page, `MAP_SHARED` after `mprotect` plus `fork`, an `rw_lock` race, the kill race, TLS alignment, and BFS metadata writes.

- **Decided on 7 Nov 2026:** R5 takes these fixes. They help AMD64 directly, on the 5625U and in QEMU.
- **When:** now (changed on 7 Nov 2026, it was "after M5"). It is a separate track, not an M milestone, on its own branch from `main` with small PRs per group. The fixes do not wait for the GPU PRs. A fix that was first made on a GPU branch is cherry-picked onto that branch.
- **How:** the import rules above. Start with the ★ fixes that have a test, and use that test as the proof on R5. A fix that conflicts with an R5 change in `distro/` is reviewed by hand, not overwritten.

### Other Haiku forks as sources

Decided on 7 Nov 2026: R5 takes what already works in other Haiku forks, and builds itself only where R5 is deliberately different (the GPU interface, multi-user, the Settings window, the BeOS-style kits). All imports follow the import rules under "Architectures".

- [haiku-pi](https://github.com/haiku-pi/haiku-rpi): the Raspberry Pi 4 port and fixes to shared code (above).
- [air/OS](https://github.com/airos-works/haiku): ARM64 boards, desktop and laptop work.
- [RenkuOS](https://github.com/RenkuOS/Source): a community fork with CI and an import ledger, MIT only.

The first choice of source per area:

| Area | Source | What | Note |
|---|---|---|---|
| ARM64 basis, Raspberry Pi 4 | haiku-pi | boot path, GICv2, DMA, Pi 4 drivers | air/OS also has a Pi 4 port; compare once, then keep one |
| Fixes to shared kernel, VM, libroot and BFS code | haiku-pi, then RenkuOS | the ★ fixes, RenkuOS kernel and VM fixes | per fix, with its test |
| Scaling and several monitors | air/OS | per-monitor HiDPI and fractional scaling in app_server, `DisplayLayout`, tear-free output | what Haiku lacks. Design it together with the GPU plan for app_server |
| Power on laptops | air/OS, RenkuOS | `power_daemon`, lid, suspend to idle on AMD laptops (air/OS); keeping CPUs out of deep idle when a driver asks (RenkuOS) | input for "Platform", sleep and resume |
| Wi-Fi | air/OS, RenkuOS | the Wi-Fi preferences panel and Deskbar applet, auto-join (air/OS); MediaTek, QCA6174 and OpenBSD wlan fixes (RenkuOS); AX210 and MT7922 (air/OS) | choose per driver |
| Bluetooth | air/OS | Low Energy, HID keyboards and mice, A2DP, preferences | |
| USB audio and MIDI | RenkuOS | `usb_audio`, `multi_audio`, `usb_midi` fixes | compare with R5's own USB audio and MIDI 2.0 work first |
| Audio device in the Deskbar, USB webcams | air/OS | choosing the output and input from the Deskbar volume control; the UVC part of `usb_webcam` | RenkuOS has neither; it only has Haiku's own groundwork |
| Touchpads and touch screens | RenkuOS | `i2c_hid`, `i2c_elan`, `i2c_goodix`, `pch_i2c` | |
| USB host controllers, TCP | RenkuOS | EHCI, UHCI, OHCI and xHCI fixes; TCP fixes | the first USB round is in R5 (9 Nov 2026); see "Import state" |
| Interface Kit and Tracker | RenkuOS | public `BAboutWindow`, live control-look switch, live file-type icons in Tracker | additive, the BeOS way |
| Security | RenkuOS | `restrict_syscalls()`, a per-team syscall filter | |
| GPU | reference only | V3D and the Vulkan window path (haiku-pi), the Mali, Adreno and NVIDIA drivers (air/OS) | input for M7, the Graphics Kit and later vendors. R5 keeps its own GPU interface |

- **When:** after GPU milestone M5, as separate tracks, not M milestones. The ARM64 basis follows the rule under "Architectures".
- **Not imported:** air/OS's GPL-2.0-only AMD driver, the applications without a licence, and the forks' branding.

#### Feature choice (7 Nov 2026)

The user went through the features of air/OS and RenkuOS.

- **Wanted:**
  - display: per-monitor scaling, several monitors in one desktop, tear-free output, dark mode;
  - kits and Tracker: public `BAboutWindow`, switching the control look without a restart, live file-type icons in Tracker;
  - networking: the Wi-Fi panel with auto-join and its Deskbar applet, the extra Wi-Fi drivers, Bluetooth (Low Energy, HID, A2DP), SMB network shares, the TCP fixes;
  - audio and video: the USB audio and USB MIDI fixes (RenkuOS), choosing the audio device from the Deskbar volume control (air/OS), USB webcams (UVC, air/OS);
  - laptop and hardware: power management (`power_daemon`, lid, battery and AC, suspend to idle), deep CPU idle states, I2C touchpads and touch screens, the USB improvements;
  - system: the per-team syscall filter, and kernel debugger capture to a file;
  - boot: starting a 64-bit kernel from 32-bit EFI, Apple EFI firmware, a faster boot, and IO-APICs with more than 64 pins;
  - install: the Installer that erases one whole disk and sets it up for UEFI (EFI and BFS partitions, the loader on the removable-media path), and live media that stay read-only. Tested in QEMU and VirtualBox first; it never offers the disk the system runs from.
- **Maybe, decided per item later:** the on-screen volume and brightness panel, the new Terminal, and the new MediaConverter.
- **Not wanted:** Quick Look, the Deskbar menu with folders, ZSnake menus, and the MCP server on the device.

How they fit R5:
- **Panels become Settings pages.** Wi-Fi, Bluetooth, power and display panels become pages in the R5 Settings window, under the fixed layout in `AGENTS.md`. The separate program stays installed, as for Display and Notifications.
- **Dark mode** goes into the Appearance page, without changing its tabs or the page order.
- **Scaling** is designed together with the GPU plan for app_server.

#### Feature choice, RenkuOS additions (9 Nov 2026)

The user went through RenkuOS's own commits since mid-September 2026. Added to "Wanted", in this order:

1. **Input on both test laptops:**
   - `i2c_hid` reads the input-ready line on AMD GPIO controllers (`dd62b72f0f`), likely what the HP Envy x360 touchpad needs;
   - TrackPoint middle-button scrolling and button fixes (`e126f76b75`, `04fdaf98e4`, `29e452e51d`) for the X1 Carbon;
   - the native Elan I2C protocol, clickpad buttons and finger tracking (`a9f23126d3`, `cb22fd5cfe`, `17bb2a0c3e`).
   Tested on the next hardware stick that is made anyway.
   `dd62b72f0f` builds on `5153bac807` (the input-ready line) and `b37a078925` (`i2c_hid` in the package); both come along. The 5625U's touchpad (ELAN2513) runs through `i2c_hid`, and the X1 has a Synaptics touchpad, so the Elan commits do not reach either test laptop.
2. **Audio devices that come and go:**
   - nodes for devices that appear later (`c864a45947`);
   - back to the saved default output when it appears, restored by name (`e95a210256`, `d1028141c2`);
   - `multi_audio` waits for an unplugged device (`ba4d3113aa`);
   - Media preferences follow the device list (`917e0e9e30`);
   - the mix to more than one output (`0903682eaa`), and a buffer size the user chooses (`6ed1b63f79`).
   Compare with R5's own USB audio work first, as for the other audio imports.
   The other RenkuOS USB audio and MIDI fixes (UAC2 streams, 64 to 128 channels, capture across transfers, the MIDI error cap) are judged with them: first an inventory per commit against `distro/media`, then only the items the user approves. `183073129f`, which replaces the whole driver, is not taken as a whole.
3. **Buttons in notifications:** `BNotification` actions (`ade5789ad1`, `28b2c9a4c2`). It is an additive API. Network shares are the first user ("not reachable – Try again").
4. **Cleaning up packages:** removing dependencies nobody needs on uninstall, `pkgman autoremove`, `mark-auto` and `mark-explicit` (`47eaecbfd8`, `a66a779566`, `64c4b84e96`, `347c732de1`, `04b3721f7a`). Before the R5 Store.
5. **Bluetooth stability:** atomozero's `l2cap`, `h2generic` and SDP fixes, before air/OS's Low Energy, HID and A2DP work.
6. **Also wanted:**
   - eMMC as a disk, and booting from it (`06e55a8212`, `a0bb867239`, `eb7eb2a994`);
   - `BMarkdownView` and `MarkdownScrollView` (`afcb7896d6` and the commits around it), for What's new and help texts;
   - brightness on VESA and Intel (`6223654a25`, `0309481a4a`).
7. **Small fixes, for a fix round:**
   - tool tip leaks (`b4d59fed21`, `e2480454c9`);
   - the menu window thread race (`1118615561`);
   - the launch_daemon environment pipe (`2f00aa816a`);
   - the VM randomisation underflow (`f1967012d9`);
   - no driver scan while the node tree is built (`c039b25536`).

#### Import state (checked 22 Nov 2026)

One branch per track. Each is built and booted in QEMU; the hardware check rides on the next stick.

| Track | Source | State |
|---|---|---|
| Input: `i2c_hid` input-ready line, TrackPoint, Elan | RenkuOS | [x] merged (11 Nov 2026) |
| USB audio and MIDI | RenkuOS | [x] merged (builds 10057 and 10058) |
| USB webcams (UVC) | air/OS `6d877da057`, `a314c835a9`, `d0df427f16` | [~] merged, booted in QEMU; no camera test on record |
| USB host controllers | RenkuOS | [x] merged (9 Nov 2026): xHCI and USB stack fixes, isochronous transfers on EHCI, UHCI and OHCI |
| CPU topology, scheduler, deep idle | air/OS | [x] merged (build 10059), deep idle on the 5625U (stick 49) |
| Wi-Fi panel and MediaTek driver | air/OS | [x] merged (build 10060), joined on the 5625U |
| Bluetooth Low Energy, HID, A2DP | air/OS, R5's own steps | [x] merged, tested on hardware (18 Nov 2026) |
| SMB network shares | air/OS | [~] merged (build 10050); no LAN test on record |
| x86-64 fixes from haiku-pi | haiku-pi | [~] a first set (BFS, VM, signals, TLS, I/O) on 9 Nov 2026; the rest of the ★ list is open |
| Audio device in the Deskbar | air/OS `122b4ad42e` and follow-ups | [ ] not started; touches `multi_audio`, HDA and `media_server`, where R5 has its own code |
| Power: `power_daemon`, lid, suspend to idle, the Power preferences | air/OS | [ ] not started; see "Platform" |
| Syscall filter, public `BAboutWindow`, live control look, `BMarkdownView`, `pkgman autoremove`, eMMC | RenkuOS | [ ] not started |
| Installer for one whole disk (UEFI) | air/OS | [ ] not started |

The USB host fixes come before the USB audio imports, which rely on them.

**Not taken from RenkuOS:**
- its dark mode: air/OS's is chosen, because it sits in Screen and Appearance;
- branding, the Spritz Depot repository, and the `radeon_hd` changes (reference only, because R5 has its own GPU path);
- Bay Trail, Stoney Ridge audio, and Raspberry Pi or ARM64 work.

Commits that reached R5 through `upstream` are skipped (the Terminal cursor keys, and mount only for root).
