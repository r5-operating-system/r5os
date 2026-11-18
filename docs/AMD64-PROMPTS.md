# Verified AMD64, Kernel & CPUID Prompts

Geverifieerde prompts (gecontroleerd tegen `origin/main` bij `#4426`, de Haiku-tree in `/home/nickr/r5os/haiku/`, en AMD64/Intel SDM documentatie).

- **IPv6 & Networking (Prompt 1 uit `REVIEW-PROMPTS.md`):** `[DONE in #4425]` (`91f9d96a31`).

---

## Prompt 1 — AMD64 kernel fixes & veilige optimalisaties (Argon)

```markdown
Read `AGENTS.md` first. Work on a new branch from `origin/main` (kernel changes only; do NOT touch Wi-Fi or IPv6 files). Wrap all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`).

Implement these AMD64 kernel fixes and optimizations (note: do NOT implement PCID — Stick 49 confirmed the 5625U has CPUID leaf 1 ECX bit 17 = 0):

1. **Microcode table bounds check (`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:1156`)**
   - In `find_microcode_amd()`, bound `for (uint32 i = 0; ...; i++)` by `section->size` (`(i + 1) * sizeof(amd_equiv_cpu_entry) <= section->size`) before reading `table[i].installed_cpu`, so a truncated/corrupt microcode container cannot read out of bounds.

2. **TLB range-flush limit (`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:1941-1950`)**
   - First check in the Haiku tree whether AMD64 uses `x86_physical_page_mapper_large_memory.cpp` at all (it is only included by 32-bit/PAE paging, not 64-bit paging).
   - On x86, `arch_cpu_invalidate_tlb_range()` is only called via `SMP_MSG_INVALIDATE_PAGE_RANGE` for global kernel addresses. Above the threshold (e.g. `> 64` pages), call `arch_cpu_global_tlb_invalidate()` (NEVER `arch_cpu_user_tlb_invalidate()`, because a CR3 reload does not flush global pages). State in `CHANGELOG.md` if AMD64 does not hit this path.

3. **Global TLB flush with `INVPCID` type 2 + 64-bit CR4 (`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:1926-1937`)**
   - Change `uint32 flags = x86_read_cr4();` to `uint64` (or `addr_t`) so upper CR4 bits are never truncated on AMD64.
   - If `x86_check_feature(IA32_FEATURE_INVPCID, FEATURE_7_EBX)` is set, use `invpcid` type 2 (all contexts including global; works even with `CR4.PCIDE = 0` to flush PCID 0 including global entries) with a 128-bit `{ uint64 pcid = 0; uint64 addr = 0; }` descriptor on the stack instead of toggling `CR4.PGE` twice; keep the `CR4.PGE` toggle as fallback.

4. **`clflushopt` in `arch_vm_flush_memory_type_range()` (`distro/kernel/src/system/kernel/arch/x86/arch_vm.cpp:369-388`)**
   - When `x86_check_feature(IA32_FEATURE_CLFLUSHOPT, FEATURE_7_EBX)` is available, use `clflushopt` and keep the trailing `sfence` before returning (required because `clflushopt` is weakly ordered across cache lines). Keep `clflush` as fallback.

5. **`amd_pstates` scheduler mode & `uninit_pstates()` (`distro/kernel/src/add-ons/kernel/power/cpufreq/amd_pstates/amd_pstates.cpp:111-124`)**
   - Do NOT write `MSR_AMD_CPPC_ENABLE = 0` in `uninit_pstates()` (`0xC00102B1` bit 0 is write-once per reset).
   - In `uninit_pstates()`, only restore the scheduler mode that was active before `init_pstates()`. Leave CPPC enabled as it is.

## Verification & delivery
- Jam AMD64 with `scripts/build-locked.sh` in WSL (`jam_exit=0`).
- Run one short QEMU boot with SMAP on (using your own copy of `r5-os.image`, never `scripts/run-qemu-r5.sh` if another session may run) and verify no panic and clean `arch_cpu` / `arch_vm` init.
- Update `CHANGELOG.md` (newest first) with what changed and what was tested.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```

---

## Prompt 2 — 2 MB large pages op AMD64 (`x86_64` MMU) (Argon)

```markdown
Read `AGENTS.md` and `docs/reviews/KERNEL-AMD64-REVIEW.md` (section 4, "TLB flushes and page size") first.
Work on a new branch from `origin/main` (kernel MMU/VM changes only). Copy any modified Haiku kernel files into `distro/kernel/` preserving original headers/copyright and wrapping all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`). Hook new `distro/kernel/` files into `scripts/apply-kernel-bugs.py`.

## Goal: 2 MB large pages for large anonymous / heap mappings on AMD64

Currently `src/system/kernel/arch/x86/paging/64bit/X86PagingMethod64Bit.cpp` only uses 2 MB pages for the physical map and refuses large pages for normal user/kernel mappings.

Implement safe, clean **2 MB large page (`PDE` PS bit)** support on `x86_64`:

1. **Inspect & design before editing**
   - Read `src/system/kernel/arch/x86/paging/64bit/X86PagingMethod64Bit.cpp`, `X86VMTranslationMap64Bit.cpp`, `src/system/kernel/vm/VMTranslationMap.cpp`, `vm.cpp`, and `vm_page.cpp` in the Haiku tree.
   - Verify the exact `VMTranslationMap` / `X86VMTranslationMap64Bit` methods for mapping, unmapping, protecting, querying, clearing flags, and page-table page budgeting before changing anything.

2. **2 MB mapping path (`x86_64`)**
   - When mapping a 2 MB-aligned virtual range (`>= 2 MB`) backed by physically contiguous 2 MB-aligned pages, allow a 2 MB PDE (`PS` bit 7 = 1) instead of splitting into 512× 4 KB PTEs.
   - Correctly translate page flags for 2 MB PDEs: on AMD64/x86-64, **`PAT` is bit 12 in a 2 MB PDE** (whereas it is bit 7 in a 4 KB PTE, because bit 7 in a PDE is `PS`), `NX` is bit 63, plus `Present`/`Write`/`User`/`Global`/`PWT`/`PCD`.
   - Keep 4 KB pages as fallback whenever alignment or physical contiguity does not hold.

3. **Safe splitting on partial operations (CRITICAL)**
   - If an unmap, protect, or flag-clear operation touches only part of a 2 MB large page (not full 2 MB aligned), **split the 2 MB PDE into 512× 4 KB PTEs** first (preserving all flags, dirty/accessed bits, `NX`, and translating `PAT` bit 12 → bit 7), invalidate the TLB properly, and then apply the operation. Never panic or corrupt adjacent pages.
   - Ensure `Query()` and page-fault / CoW paths work seamlessly on 2 MB-mapped areas.

4. **Observability & safety**
   - Log once or track a counter when 2 MB user/kernel mappings are created/split so we can verify on hardware.
   - Make sure SMAP/SMEP and `arch_vm_flush_memory_type_range()` remain unaffected.

## Verification & delivery
- Jam AMD64 with `scripts/build-locked.sh` in WSL (`jam_exit=0`).
- Boot in QEMU with SMAP on (your own copy of `r5-os.image`, never `scripts/run-qemu-r5.sh` if another session may run).
- Verify large area allocation + partial `mprotect()` / `munmap()` (splitting a 2 MB page back to 4 KB) without panic or memory corruption.
- Update `CHANGELOG.md` (newest first) with what changed and what was tested.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```

---

## Prompt 3 — `x86-64-v2` (SSE4.2) user-space baseline (Argon)

```markdown
Read `AGENTS.md` and `docs/64BIT.md` (sections 1, 3, 5, 6 Step 3, and 7) first.
Work on a new branch from `origin/main`. Copy any modified Haiku files into `distro/` preserving original headers/copyright and wrapping all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`).

## Goal: Raise the AMD64 user-space baseline to `x86-64-v2` (SSE4.2, SSSE3, POPCNT, CMPXCHG16B)

1. **Compiler flags in `ArchitectureRules` (via `distro/` copy + apply hook)**
   - Read `build/jam/ArchitectureRules` in the Haiku tree (lines 37–44 for `archFlags`, lines 300–316 and 446–495 for `x86_64` kernel and boot loader flags).
   - Add `case x86_64 : archFlags += -march=x86-64-v2 ;` (and verify `distro/UserBuildConfig` / Mesa recipe if needed).
   - **Boot loader check:** Keep the 32-bit BIOS boot loader (`bios_ia32`, `-march=pentium`) separate from `-march=x86-64-v2`.
   - **Kernel check:** Haiku's `ArchitectureRules` does NOT pass `-mno-sse` on `x86_64` (SSE2 is part of the standard x86-64 ABI and `interrupts.S` saves FPU/XMM state). Verify that the kernel and kernel add-ons build cleanly and boot with SMAP on (or keep kernel `-mno-sse4.2` / baseline v1 if desired, without breaking the x86-64 ABI).

2. **EFI boot loader CPUID check**
   - In the `x86_64` EFI boot loader, check CPUID before jumping to the kernel for `x86-64-v2` features (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B).
   - If any required feature is missing, print a clear human-readable message (e.g. `"This R5 Operating System build requires an x86-64-v2 CPU (SSE4.2 / POPCNT)."`) and halt cleanly instead of crashing later with `#UD` (invalid opcode).

3. **QEMU scripts & tests**
   - Update `scripts/run-qemu-r5.sh` (currently `cpu=qemu64` at line 13) to pass `-cpu Nehalem` (or `EPYC-Rome` / `max` where appropriate) so QEMU provides `x86-64-v2`.
   - Keep ARM64 and RISC-V 64 build rules untouched.

## Verification & delivery
- Full AMD64 build with `scripts/build-locked.sh` in WSL (`jam_exit=0`).
- Run two QEMU boots (using your own copy of `r5-os.image`, never `scripts/run-qemu-r5.sh` if another session may run):
  1. `-cpu Nehalem` (or `EPYC-Rome`) with SMAP on → boots cleanly to desktop, no `#UD` / panic.
  2. `-cpu qemu64` (below v2) → prints the clear CPUID error message in the EFI loader and stops cleanly.
- Update `CHANGELOG.md` (newest first) and `docs/64BIT.md` status if appropriate.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```

---

## Prompt 4 — `x86-64-v3` (AVX2 / FMA / BMI2) runtime dispatch in hot paths (Argon)

```markdown
Read `AGENTS.md` and `docs/64BIT.md` (sections 1, 5, 6 Step 4, and 7) first.
Work on a new branch from `origin/main` AFTER `x86-64-v2` is merged. Copy any modified Haiku files into `distro/` preserving original headers/copyright and wrapping all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`).

## Goal: Keep `x86-64-v2` as the baseline and add CPUID runtime dispatch for `x86-64-v3` (AVX2/FMA/BMI2) in hot paths

1. **Safe CPUID + `XCR0` detection helper (user space)**
   - Add a small shared helper (using `cpuid` + `xgetbv(0)`) that returns `true` for `x86-64-v3` ONLY when:
     - CPUID reports `OSXSAVE`, `AVX`, `AVX2`, `FMA`, `BMI1`, `BMI2`, `MOVBE`, `F16C`, AND
     - `XCR0` bits 1 (XMM) and 2 (YMM) are both enabled by the OS/hypervisor.
   - Never assume AVX2 is usable without checking `OSXSAVE` and `XCR0`.

2. **Runtime dispatch in the measured hot paths (no global `-march=x86-64-v3`)**
   - Keep the global build baseline at `x86-64-v2`.
   - Compile only the selected hot-path translation units or functions with `__attribute__((target("avx2,fma,bmi2")))` (or `-mavx2 -mfma -mbmi2` on a dedicated `.cpp` file), selected once at init via function pointer (same pattern as `libroot`'s `memcpy.cpp`).
   - Concrete R5 candidates:
     - `distro/app_server/Compositor.cpp`, `distro/app_server/GPUCopy.cpp`, `distro/app_server/skia/SkiaPainter.cpp`, and `distro/skia/build-skia.sh` (check what Skia already dispatches vs what `app_server` blends itself),
     - `libroot` / `media` hot loops where profiling/measurement shows a real gain.

3. **Zero regressions on non-AVX2 CPUs**
   - Every dispatched path must have an identical `x86-64-v2` fallback and pass the same unit/rendering tests on both paths.

## Verification & delivery
- Full AMD64 build with `scripts/build-locked.sh` in WSL (`jam_exit=0`).
- Run two QEMU boots (using your own copy of `r5-os.image`, never `scripts/run-qemu-r5.sh` if another session may run):
  1. `-cpu EPYC-Rome` (or `-cpu max` with AVX2) + SMAP on → AVX2 dispatch active, desktop/compositor renders cleanly, no `#UD` / panic.
  2. `-cpu Nehalem` (`x86-64-v2`, no AVX2) + SMAP on → falls back cleanly to v2 path, desktop renders identically, no illegal instruction.
- Measure before/after on a small compositor/blend benchmark (and note hardware vs QEMU).
- Update `CHANGELOG.md` (newest first) with what changed and what was tested.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```

---

## Prompt 5 — R5 CPUID: CPU vs. OS Capabilities app (Cursor / Argon)

```markdown
Read `AGENTS.md`, `docs/LIBR5.md`, `docs/APPEARANCE-KIT.md`, `distro/kernel/x86/r5_pstate.cpp`, `distro/cpu/r5_pstate_log.cpp`, and `distro/apps/devicemanager/Jamfile` first.
Work on a new branch from `origin/main`. All new R5 files are MIT (`SPDX-FileCopyrightText: 2026 R5OS Contributors`, `SPDX-License-Identifier: MIT`). Do not modify Settings layout (`AGENTS.md`).

## Goal: Build a lightweight "CPU Info" (CPUID) app showing **Hardware Capabilities** vs **What R5 Uses**

1. **App location & UI (`distro/apps/cpuinfo/`, following `distro/apps/devicemanager/` and hooked into regular image)**
   - Follow `docs/APPEARANCE-KIT.md` (`BAppearanceKit::BCard`, `libr5ui.so`) and match R5's UI style (product name **R5**, never Haiku/BeOS).
   - Keep it clean and readable in one window with two clear sections/columns:
     - **CPU Hardware (via `CPUID` + `xgetbv`):**
       - Vendor, Brand string, Family/Model/Stepping, Physical cores / Logical threads, Cache sizes (L1/L2/L3).
       - Baseline level supported by hardware: `x86-64-v1` (SSE2), `x86-64-v2` (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B), `x86-64-v3` (AVX, AVX2, FMA, BMI1, BMI2, MOVBE, F16C), `x86-64-v4` (AVX-512).
       - Extra features: `AES-NI`, `SHA`, `INVPCID`, `PCID`, `CLFLUSHOPT`, `PDPE1GB`, `Invariant TSC`, `x2APIC`, `SMEP`, `SMAP`, `NX`, `PAT`.
     - **R5 OS Status (what the OS actually enables/uses):**
       - **User-space baseline:** built with `x86-64-v1` vs `x86-64-v2` (from compiler feature macros like `__SSE4_2__` / `__POPCNT__`).
       - **OS Vector State (`XCR0` via `xgetbv`):** SSE/XMM enabled, AVX/YMM enabled (ready for AVX2 dispatch), AVX-512/ZMM enabled.
       - **Kernel features (read-only):** read `/dev/misc/r5_pstate` (see `distro/cpu/r5_pstate_log.cpp`) and extend `distro/kernel/x86/r5_pstate.cpp` if needed to expose a read-only line of active kernel flags: `SMEP`, `SMAP`, `NX`, `PAT`, `x2APIC`, `INVPCID`, `CLFLUSHOPT`, `PCID`, `2MB-pages`, `CPPC/HWP`. Never write MSRs; keep it purely read-only and SMAP-safe.
   - Add a **"Copy report"** button (copies a clean plain-text summary to `be_clipboard`) and support `--cli` / stdout output so it can also be run from Terminal or stick scripts.

2. **Safe fallbacks**
   - On non-x86_64 or when `/dev/misc/r5_pstate` is unavailable, degrade gracefully ("Unknown" / "Not reported") without crashing.

## Verification & delivery
- Jam AMD64 with `scripts/build-locked.sh` in WSL (`jam_exit=0`).
- Boot in QEMU with SMAP on (your own copy of `r5-os.image`, never `scripts/run-qemu-r5.sh` if another session may run):
  - Launch the app (and run with `--cli` in Terminal) under `-cpu qemu64` and `-cpu EPYC-Rome` (or `Nehalem`) and verify the Hardware vs OS columns update accurately without panic.
- Update `CHANGELOG.md` (newest first) with what changed and what was tested.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```
