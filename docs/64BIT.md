# A real 64-bit system on R5

This is a study of what "a real 64-bit system without compromises" means for R5 Operating System, while every Haiku application and the `libbe` ABI keep working. It says what is in the tree today, which parts are a real compromise and which only look like one, what each fix costs and what it breaks, and the steps that get there. Each step is its own small pull request with a pass rule. The choices in section 7 are the owner's.

Step 3 (the x86-64-v2 baseline, including the kernel, with the boot loaders left on the plain baseline) is in the tree as of 18 Nov 2026. The other steps are still paper. Line numbers below were checked on 16 Nov 2026 against `main` at `2729cf0532`, before that baseline. Every claim is marked **checked** (read in the tree, or in the WSL build tree's `BuildConfig`) or **assumption** (from vendor data sheets or general knowledge, not read on the machine). No speed gain in this document was measured; where a gain is named, it is a reason to measure, not a result.

It follows the BeOS and Haiku way. An application keeps the Haiku kits and their ABI. R5 adds new calls next to old ones; it never changes a call that shipped (AGENTS.md, "Compatibility").

## 0. What "without compromises" can and cannot mean

- **It can mean:** the system uses the 64-bit CPU fully (instruction set, address space, protections), no part of it is held back by a 32-bit limit that still matters on the 5625U or the X1, and new programs get calls without 32-bit counts and sizes.
- **It cannot mean:** changing `int32` to `int64` in an existing class. That changes the C++ mangled names and the class layout, so every Haiku x86_64 binary that calls it stops loading. The `libbe` ABI is a product promise (AGENTS.md); it wins over neatness.
- **Test paths:** QEMU proves boot and that nothing panics; QEMU with `-cpu` set to a lower model proves that a CPU baseline refuses or runs cleanly. Only the 5625U (a stick) and the X1 prove the real CPU. A speed claim needs a measurement on that machine, before and after, on one image.

## 1. What is in the tree today

### Targets and secondary architectures (checked)

- The image is `x86_64` only. The WSL build tree `~/r5os/haiku/generated.x86_64/build/BuildConfig` has `HAIKU_PACKAGING_ARCHS ?= x86_64` and no secondary architecture. `scripts/build-image.sh:17-20` configures with `--build-cross-tools x86_64` and no `x86` or `x86_gcc2`.
- The kernel's 32-bit compatibility mode (ia32 programs on the 64-bit kernel) is only switched on when a secondary `x86` or `x86_gcc2` architecture is configured: `haiku/build/jam/ArchitectureRules:459-463` (`_COMPAT_MODE`). So **R5 cannot run 32-bit programs today**: no 32-bit x86 Haiku binaries, and no BeOS R5 or Haiku `x86_gcc2` (gcc2/hybrid) binaries. Those are 32-bit only. "Every Haiku application" in practice means every **x86_64** Haiku application, which is what HaikuPorts ships for x86_64.
- 32-bit code that is still built: the BIOS boot loader. Because the cross tools have a 32-bit libgcc (`HAIKU_BOOT_32_LIBGCC_x86_64` in `BuildConfig`), `ArchitectureRules:303-307` builds `bios_ia32 efi pxe_ia32`; the anyboot stick carries both a 32-bit BIOS loader (`-march=pentium`, `ArchitectureRules:520-521`) and the 64-bit EFI loader. The 5625U and the X1 boot by EFI (assumption: the owner's sticks boot in UEFI mode; not read from a log).

### CPU baseline (checked)

- **AMD64:** `distro/jam/ArchitectureRules` (applied by `scripts/apply-cpu-baseline.py`) has `case x86_64 : archFlags += -march=x86-64-v2`. `archFlags` is part of `ccBaseFlags`, and the kernel takes `HAIKU_CCFLAGS`, so user space, the kernel and kernel add-ons are built for **x86-64-v2** (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B, and LAHF/SAHF). The boot loaders strip `-march=x86-64-v2`; `bios_ia32` stays `-march=pentium` and `-m32`. The shared check is `distro/boot/arch/x86/arch_cpu.cpp` (`boot_arch_cpu_init`), which the EFI loader already calls and the BIOS loader calls from `distro/boot/bios_ia32/cpu.cpp`. A missing feature prints `This R5 Operating System build requires an x86-64-v2 CPU (SSE4.2 / POPCNT).` and halts. `distro/UserBuildConfig` adds no further CPU flags. ARM64 and RISC-V flags in that file are unchanged.
- **ARM64:** `-march=armv8-a+crc` (`ArchitectureRules:41`): ARMv8.0 plus CRC32. No LSE atomics (v8.1), no PAN use (see section 4).
- **RISC-V 64:** `-march=rv64gc` (`ArchitectureRules:43`), with `-mno-relax` under clang (`:45-49`). No vector (V), no bit-manipulation (Zba/Zbb/Zbs).
- **Optimizer:** `OPTIM ?= -O2` (`haiku/build/jam/BuildSetup:714`). No LTO, no PGO in `haiku/build/jam` (grep for `-flto` and `-fprofile` found nothing).
- **Run-time dispatch already present:** the kernel patches code by CPU feature (`arch_altcodepatch_replace`, used for SMAP at `haiku/src/system/kernel/arch/x86/arch_cpu.cpp:1665-1666`). In user space, libroot's `memcpy` for x86_64 has a `rep movsb` path and an SSE path (`haiku/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp:48-50`, `:93-95`). No `ifunc` or `target_clones` in libroot or the runtime loader (grep found none).

### Which level the two machines support (assumption, from vendor data)

| Machine | CPU | x86-64-v2 | v3 (AVX2, BMI2, FMA, MOVBE) | v4 (AVX-512) |
|---|---|---|---|---|
| Reference | Ryzen 5 5625U (Zen 3) | yes | yes | no |
| Second | X1 Carbon Gen 8 (Intel Comet Lake, 10th gen U) | yes | yes | no |

Neither machine has AVX-512, so **v4 is out for both**. The build host has AVX2, BMI2, POPCNT and SSE4.2 (checked in WSL `/proc/cpuinfo`), but the build host is not the target. Before any baseline step, the boot self-test logs CPUID once on each machine, so this table becomes "checked".

What would no longer boot (assumption): v2 drops CPUs older than about 2009 (Core 2 without SSE4.2, early AMD K10). v3 drops Intel before Haswell (2013), Intel Atom/Celeron/Pentium Silver up to about 2020 (no AVX), and AMD before Excavator (2015); it also drops QEMU's default `qemu64` CPU and some VirtualBox setups that hide AVX. A v3 program on such a CPU does not fail politely; it stops with an illegal-instruction fault.

### 32-bit limits in drivers (checked)

- **USB:** every USB stack buffer is allocated `B_32_BIT_CONTIGUOUS` (below 4 GiB), `distro/kernel/src/add-ons/kernel/bus_managers/usb/Stack.cpp:329-331`, with a TODO to use `B_CONTIGUOUS` once 64-bit addresses are fixed. xHCI is 64-bit capable; this is an old limit kept for EHCI/OHCI/UHCI.
- **AHCI (SATA):** `B_DMA_HIGH_ADDRESS` is `0x100000000` for every controller, `haiku/src/add-ons/kernel/busses/scsi/ahci/ahci.c:280-283`, with a TODO; the 64-bit capability flag (`CAP_S64A`) is read but only logged (`ahci_controller.cpp:194-195`). With more than about 3 GiB of RAM a transfer above 4 GiB goes through a bounce buffer (assumption: the copy cost is not measured).
- **NVMe and radeon_hd:** no 4 GiB limit found by grep (`B_32_BIT`, `high_address`) in `haiku/src/add-ons/kernel/drivers/disk/nvme` or `distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd`. Not proven free of one; only no explicit limit.
- Old drivers for old cards (`silicon_image_3112`, `53c8xx`, `buslogic`, `nvidia`, old `radeon` GART) use `B_32_BIT_CONTIGUOUS`. That is their hardware; not a compromise to fix.

### 64-bit and 32-bit types in the public API (checked)

- Already 64-bit: `time_t` is 64-bit on everything except 32-bit x86 (`haiku/headers/posix/time.h:20-24`), so **R5 has no Y2038 problem in `time_t`**. `off_t` is 64-bit (`headers/posix/sys/types.h:37`). `bigtime_t` and `nanotime_t` are `int64` (`headers/os/support/SupportDefs.h:54-55`). `system_info.max_pages` and `used_pages` are `uint64` (`headers/os/kernel/OS.h:438-439`). 64-bit atomics exist (`SupportDefs.h:336`, `:357`, `:381`). `area_info.size` is `size_t` (`OS.h:59`).
- Still 32-bit:
  - `BList`: `AddItem(void*, int32)`, `ItemAt(int32)`, `CountItems()` return or take `int32` (`headers/os/support/List.h:23`, `:42`, `:55`). A list is capped at about 2 billion items.
  - `BString::Length()` and `CountChars()` are `int32` (`headers/os/support/String.h:32-33`). A string is capped at 2 GiB.
  - `BMessage`: `AddData(..., ssize_t numBytes, ..., int32 count)` (`headers/os/app/Message.h:174-176`) and `CountNames()` (`:78`) count in `int32`; sizes are already `ssize_t`.
  - `area_info.ram_size`, `copy_count`, `in_count`, `out_count` are `uint32` (`OS.h:63-66`). For an area of 4 GiB or more, `ram_size` wraps. That is a real, visible bug on a 16 GiB machine (assumption: not reproduced).
  - `system_info.max_sems`, `max_ports` and friends are `uint32` (`OS.h:452-456`). Counts, not sizes; no real limit.
- `int32` as a count is not a bug where nothing near 2^31 is possible (menus, windows, sems). It is a compromise only where real data is that big: strings, lists of bytes, files mapped in memory, and `area_info.ram_size`.

## 2. The kernel (checked unless marked)

| Feature | AMD64 today | Note |
|---|---|---|
| NX (no-execute) | on when the CPU has it | `X86PagingMethod64Bit.cpp:67`, `:425` |
| SMEP and SMAP | on, unless safe mode turns them off | `arch_cpu.cpp:1650-1670`; R5 also boots QEMU with SMAP (memory "SMAP guard") |
| ASLR (user) | on; `DISABLE_ASLR=1` turns it off per team | `team.cpp:1375`, `:1390`, `:1528-1529`; `elf.cpp:2045` |
| ASLR (kernel, KASLR) | not found | grep for `kaslr` found nothing; the kernel sits at a fixed `KERNEL_BASE 0xffffff0000000000` (`headers/private/kernel/arch/x86/arch_kernel.h:37`) |
| 5-level paging (LA57) | supported if the loader turned it on | `arch_vm_translation_map.cpp:87-90`; the 5625U and X1 do not have LA57 (assumption), so it does not matter here |
| PCID / INVPCID | detected only, not used | feature bits at `arch_cpu.cpp:441`, `:494`; no `CR4.PCIDE` write found. Every address-space switch flushes the TLB |
| x2APIC | used when present | `apic.cpp:285` |
| Large pages | the boot loader maps the physical map with 2 MiB pages (`boot/platform/efi/arch/x86_64/arch_mmu.cpp:269`); the kernel can read them (`X86VMTranslationMap64Bit.cpp:489`) | areas never get 2 MiB or 1 GiB pages (no code found that creates them). VRAM, Mesa buffers and big files use 4 KiB pages |
| Physical memory | up to 512 GiB | the physical map is `KERNEL_PMAP_SIZE 0x8000000000` (`arch_kernel.h:44`); far above both machines |
| User address space | 128 TiB, 4-level | `USER_SIZE` (`arch_kernel.h:52`) |

ARM64: the kernel does not enable PAN (ARM's SMAP); `headers/private/kernel/arch/arm64/arch_cpu.h:13` has the TODO. RISC-V 64: whether SUM is cleared outside user copies (RISC-V's SMAP) is not checked. BTI and pointer authentication on ARM64: not found.

## 3. The toolchain (checked)

- GCC 13.3.0 cross compiler, `-O2`, no `-march` on x86_64, no LTO, no PGO.
- Frame pointers: kept in the x86_64 kernel (`-fno-omit-frame-pointer`, `ArchitectureRules:454-455`) and the RISC-V boot loader (`:556-557`). User space follows GCC's default for `-O2`, which omits them (assumption: GCC default, not checked per file). That makes user-space stack traces in the Debugger and a sampling profiler weaker.
- `-fno-plt`: only in the RISC-V boot loader. Haiku's runtime loader binds symbols lazily through the PLT (assumption: not read in `runtime_loader`).
- HaikuPorts packages (Mesa, LLVM, OpenSSL, …) are built by HaikuPorts with their own flags, not ours. A change of R5's baseline only touches what this tree builds, plus R5's own recipes (Mesa with RADV, per `docs/GPU-ARCHITECTURE.md`).

## 4. What is a real compromise, and what each fix costs

| # | Compromise | Real? | Fix | Cost | What it breaks |
|---|---|---|---|---|---|
| A | x86-64-v1 code everywhere | yes, for hot code (graphics, Skia, compositor, codecs, compression) | a baseline flag, or per-function dispatch | one flag, then a full rebuild and a boot on both machines | v2: almost nothing; v3: older and low-end PCs and default QEMU (section 1) |
| B | no 32-bit programs | no, as long as R5 is x86_64-only by design | a secondary `x86` arch turns compat mode on | doubles the build, a 32-bit libroot and libbe on the image | nothing breaks; it is extra |
| C | USB buffers below 4 GiB | small | `B_CONTIGUOUS` for xHCI only | one file, a USB boot test on both machines | old EHCI/OHCI stay below 4 GiB |
| D | AHCI DMA below 4 GiB | yes on machines with SATA and >4 GiB | honor `CAP_S64A` | one driver, a SATA test | the 5625U and X1 boot from NVMe (assumption), so neither is a test machine for it |
| E | `area_info.ram_size` is `uint32` | yes (wrong numbers) | a new `get_area_info_etc()` with a 64-bit struct, old call unchanged | kernel + libroot, additive | nothing |
| F | `BString`, `BList` count in `int32` | rarely matters | new 64-bit calls next to the old ones (for example `BString::Length64()`), or nothing | per class, docs in `docs/LIBR5.md` style | nothing if additive; renaming breaks everything |
| G | no PCID | yes, cost per context switch | set `CR4.PCIDE`, tag address spaces | kernel MMU work, the riskiest step here | a mistake is silent memory corruption |
| H | no large pages for areas | yes for VRAM and big buffers | 2 MiB pages for big contiguous areas | kernel VM work | as G |
| I | no KASLR | security, not speed | randomize `KERNEL_BASE` within the top range | boot loader and kernel | kernel debugging addresses change per boot |
| J | ARM64 without PAN | yes (security) | turn PAN on, like SMAP | arm64 kernel | only ARM64; not usable before ARM64 boots |
| K | `-O2`, no LTO/PGO, no user frame pointers | small and unmeasured | per library, measured | build time | LTO can expose ODR bugs in old Haiku code |

## 5. The middle road for the CPU baseline

Two roads give most of A without dropping machines:

1. **Keep v1 (or v2) as the baseline; dispatch the hot paths.** GCC's `__attribute__((target_clones("default","arch=x86-64-v3")))` builds a function twice and picks one at load time through an `ifunc`. That needs `ifunc` support in Haiku's runtime loader, which is not there (section 1). Without it, the same works by hand: a function pointer set once from `cpuid` (the way libroot's `memcpy` already chooses). Skia, Mesa's Lavapipe (llvmpipe generates code for the running CPU already, assumption) and the compositor's blend loops are the candidates.
2. **Raise the baseline to v2.** POPCNT, SSE4.2 and SSSE3 everywhere. v2 drops only CPUs from before about 2009 (assumption). QEMU needs `-cpu` with SSE4.2 (for example `-cpu Nehalem` or `host`); `scripts/run-qemu-r5.sh` would get that flag.

A full v3 baseline is the "no compromises" answer for the two machines, but it is a compromise for everyone else: low-end Intel laptops and many VMs stop at the boot loader with no message. If v3 is chosen, the boot loader must check CPUID and print a clear "this R5 build needs x86-64-v3" message before it starts the kernel.

## 6. Steps

Each step is its own small pull request, built for AMD64 with `scripts/build-locked.sh` and kept building for ARM64 and RISC-V 64 (not tested). QEMU runs on an own copy of `r5-os.image`. Hardware tests are bundled: one stick per group of steps, then the X1 from a stick or over SSH.

### Step 0 (paper + one log line): measure first

- **What:** the boot self-test logs the CPUID feature bits and the x86-64 level it finds, once. A small benchmark set is chosen and written down: a Skia frame, a compositor frame (M11/M12 numbers already exist), `zstd`/package extraction, and a build of one Haiku app. No flag changes yet.
- **Pass:** the 5625U and the X1 report their level in the self-test; the benchmark numbers on v1 are recorded in `CHANGELOG.md`.

### Step 1 (PR 1): the 32-bit leftovers that are cheap

- **What:** USB buffers with `B_CONTIGUOUS` on xHCI (`Stack.cpp:329`), AHCI honoring `CAP_S64A` (`ahci.c:280`). Both in `distro/` copies, SPDX-Snippet marked.
- **Required checks:** manual SMAP review, a QEMU boot with SMAP and with more than 4 GiB of RAM (`-m 8G`), a USB stick read and a SATA disk in QEMU.
- **Pass:** QEMU at 8 GiB boots from USB and from AHCI, files copy with matching hashes; on the stick, USB keyboard, mouse and stick work on the 5625U.

### Step 2 (PR 2): 64-bit area information

- **What:** a new call next to `get_area_info()` that returns 64-bit `ram_size` and counts. The old struct and call stay exactly as they are. Described in `docs/LIBR5.md` if it lands in an R5 library, or as a Haiku API addition in the R5Book if it is in libroot.
- **Pass:** a 6 GiB area in QEMU reports 6 GiB through the new call and the old call still returns its old (wrapped) value unchanged.

### Step 3 (PR 3): the CPU baseline

- **What:** `case x86_64 : archFlags += -march=x86-64-v2` in `ArchitectureRules`, copied into `distro/`, plus a CPUID check in the EFI loader and the BIOS loader with a clear message on a CPU below v2, plus the QEMU `-cpu` flag in `scripts/run-qemu-r5.sh`. The kernel is included, because `archFlags` feeds the kernel flags (owner, 18 Nov 2026). The loaders strip the flag.
- **Status (18 Nov 2026):** the flag, the strip, the shared CPUID check and the QEMU CPU are in the tree. QEMU at v2 with SMAP and QEMU `qemu64` are recorded in `CHANGELOG.md`. The step 0 benchmarks on the 5625U have not been run, so no speed claim is made. The pass rule's stick half is still open.

### Step 4 (PR 4, only if step 3 shows a gain): dispatch in the hot paths

- **What:** per-function v3 versions in the hot loops picked by `cpuid` (or by `target_clones` once the runtime loader has `ifunc`, which is its own PR). Only for code that step 3 showed matters.
- **Pass:** the same benchmarks with the dispatch on a v2 image equal or beat the v3 image of step 3.

### Step 5 (PR 5 and later, kernel): PCID, then large pages, then KASLR

- One PR each. These change the MMU and the address-space switch; they follow AGENTS.md "Kernel, MMU …": inspect the path and the fail cases, compile success is not a fix.
- **Pass for each:** a QEMU SMAP boot, a stress run (many processes, many threads, `fork`/`exec` loops) for one hour without a panic, and a stick boot on the 5625U with the step 0 numbers before and after.

### Step 6 (paper): ARM64 and RISC-V 64

- ARM64: PAN on, then the choice between ARMv8.0 and a higher baseline (v8.2 with LSE atomics fits most current boards and Apple, assumption). RISC-V 64: RVA22 (adds Zba/Zbb/Zbs, assumption) as the baseline, RVA23 (vector) through dispatch only. Both wait until that architecture reaches "bootable" (AGENTS.md support levels).

## 7. The owner's choices

**Chosen on 16 Nov 2026** (all five as recommended): 1 (d) v2 baseline plus v3 dispatch in the hot paths, after the step 0 measurement; 2 (a) 64-bit only, stated in the R5Book; 3 (b) new 64-bit calls next to the old ones, area info first; 4 (a) PCID first, after M13; 5 (a) plus (b) Haiku's flags plus frame pointers in user space.

**Chosen on 18 Nov 2026:** the v2 baseline covers user space and the kernel. The boot loaders stay on the plain x86-64 baseline and refuse a CPU below v2 before they enter the kernel. The v3 dispatch of (d) is still later, and still waits on a measurement.

1. **CPU baseline on AMD64.** (a) Stay on v1, (b) v2, (c) v3 with a clear boot message, (d) v2 plus v3 dispatch in the hot paths.
   *Recommendation: (d), but only after step 0 measures.* v2 drops almost nothing; v3 drops many laptops and VMs that people will try R5 on, for a gain that is not measured yet. If the numbers after step 3 at v3 are large, revisit (c).
2. **32-bit programs.** (a) None, R5 is 64-bit only, or (b) a secondary `x86` architecture with compat mode, so 32-bit Haiku and gcc2 programs run.
   *Recommendation: (a).* AGENTS.md makes the targets 64-bit only, HaikuPorts has x86_64 packages for the programs people use, and (b) doubles the build. Say it clearly in the R5Book: "Haiku x86_64 applications run; 32-bit Haiku and BeOS binaries do not."
3. **32-bit counts in the API.** (a) Leave them, (b) add 64-bit calls only where real data is that big (area info first, then strings and lists if asked), (c) a new 64-bit kit next to the old one.
   *Recommendation: (b), starting with area info.* It fixes the one wrong number that shows on a 16 GiB machine, keeps the ABI, and does not add a second kit.
4. **Kernel order.** (a) PCID first, (b) large pages first, (c) KASLR first, (d) none for now.
   *Recommendation: (a) after M13.* PCID touches every context switch and is the step with the clearest expected gain; it is also the riskiest, so it waits until the GPU milestones are closed. KASLR is security without speed and can wait.
5. **Toolchain.** (a) Keep `-O2` and Haiku's flags, (b) frame pointers in user space for better traces and profiling, (c) LTO for chosen libraries.
   *Recommendation: (a) plus (b).* Frame pointers cost a little (unmeasured) and make every crash report and profile better; LTO comes only when a profile points at a library.
