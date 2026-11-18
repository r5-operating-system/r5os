# Verified AMD64, Kernel & CPUID Prompts

Geverifieerde prompts (gecontroleerd tegen `origin/main` bij `#4426`, de Haiku-tree in `/home/nickr/r5os/haiku/`, en AMD64/Intel SDM documentatie). Prompt 2 tot en met 5 opnieuw nagekeken op 18 nov 2026, na `#4428`; wat daarbij veranderde staat per prompt onder "Nagekeken".

- **IPv6 & Networking (Prompt 1 uit `REVIEW-PROMPTS.md`):** `[DONE in #4425]` (`91f9d96a31`).
- **Prompt 1 (AMD64 kernel fixes):** `[DONE in #4428]`.
- **Prompt 2:** gesplitst in 2a (ontwerp, alleen lezen; verstuurd) en 2b (bouwen, pas na het ontwerp).

Volgorde: 2a → 2b. Prompt 3 kan los van 2 lopen; prompt 4 pas na 3; prompt 5 kan los lopen, maar de regel "2MB-pages" pas na 2b.

---

## Prompt 1 — AMD64 kernel fixes & veilige optimalisaties (Argon) `[DONE in #4428]`

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

## Prompt 2a — 2 MB pages op AMD64: alleen ontwerp (Argon) `[VERSTUURD]`

Nagekeken (18 nov 2026): de eerste versie wilde 2 MB-pagina's voor anoniem geheugen en de heap. Dat geheugen bestaat in Haiku/R5 uit losse 4 KB-pagina's die pas bij gebruik worden toegewezen, naar swap kunnen, copy-on-write zijn en per 4 KB accessed/dirty bijhouden; het ligt bijna nooit fysiek aaneen. Daarom eerst een ontwerp, beperkt tot geheugen dat al aaneengesloten en vast is. Ook nodig: een paginareservering voor het splitsen (in unmap/protect mag niet vrij worden toegewezen), break-before-make bij het wisselen van paginagrootte, een kernel-zelftest in plaats van een `mprotect`-test die in QEMU niet vanzelf draait, en een instelling die standaard uit staat. `X86PagingMethod64Bit.cpp:384-387` zegt nu letterlijk dat er geen echte large-page-ondersteuning is.

```markdown
## Prompt 2a — 2 MB pages on AMD64: design only (read-only)

Read AGENTS.md and docs/reviews/KERNEL-AMD64-REVIEW.md (section 4) first. Read-only: no code changes, no build. Reply in Dutch, short, with [V]/[I].

Write docs/AMD64-LARGE-PAGES.md: a design for 2 MB PDE (PS) mappings on x86_64, limited to memory that is already physically contiguous and never paged:
- map_physical_memory (device memory, framebuffer, GPU VRAM/BAR);
- B_CONTIGUOUS and B_FULL_LOCK kernel areas.
Not anonymous, pageable, CoW or swappable memory: say why (per-page vm_page, accessed/dirty per 4 KB, page daemon, swap).

Cover, with file:line in haiku/src/system/kernel (vm.cpp, VMTranslationMap.cpp, arch/x86/paging/64bit/X86PagingMethod64Bit.cpp and X86VMTranslationMap64Bit.cpp; note X86PagingMethod64Bit.cpp:384-387 asserts no large pages today):
1. Which calls create those mappings, and where a 2 MB PDE could be written instead of 512 PTEs (alignment, contiguity, memory type).
2. Flag translation: PAT bit 12 in a 2 MB PDE versus bit 7 in a 4 KB PTE; NX 63; P/W/U/G/PWT/PCD; accessed/dirty.
3. Splitting a 2 MB PDE on a partial unmap or protect: where the page-table page comes from (the existing page reservation, MaxPagesNeededToMap / vm_page_reserve), and break-before-make (clear PDE, flush all CPUs, write the new table) to avoid two page sizes in the TLB at once.
4. Every VMTranslationMap method that must handle a 2 MB PDE (Map, Unmap, UnmapArea, Protect, Query, QueryInterrupt, ClearFlags, ClearAccessedAndModified, page-table budgeting), and what each does with it.
5. The switch: a kernel setting (off by default) and a kernel self-test that maps, protects half of, and unmaps a contiguous 4 MB area, logging PASS/FAIL to the serial log.
6. Risks and what a stick on the 5625U must show.
Ask me before push/PR.
```

---

## Prompt 2b — 2 MB pages op AMD64: bouwen (Argon) `[NOG NIET]`

Wordt geschreven nadat `docs/AMD64-LARGE-PAGES.md` (2a) er ligt, gereviewd is en door de eigenaar is goedgekeurd. Uitgangspunten die nu al vastliggen: alleen de geheugensoorten uit 2a; achter een kernelinstelling die standaard uit staat; de kernel-zelftest uit 2a als bewijs in QEMU; een stick op de 5625U voordat het standaard aan kan.

---

## Prompt 3 — `x86-64-v2` (SSE4.2) baseline (Argon)

Nagekeken (18 nov 2026), met de keuzes van de eigenaar:
- `archFlags` gaat via `ccBaseFlags` (`ArchitectureRules:51`) ook naar de kernel (`:373`). De eigenaar kiest: de kernel ook v2. De boot loaders blijven buiten v2, anders kan de CPU-check zelf met `#UD` stoppen voordat hij iets meldt.
- De CPU-check komt in de EFI-loader én de BIOS-loader (`bios_ia32`). Onze QEMU boot via BIOS zonder OVMF; alleen een EFI-check zou daar nooit draaien.

```markdown
Read `AGENTS.md` and `docs/64BIT.md` (sections 1, 3, 5, 6 Step 3, and 7) first.
Work on a new branch from `origin/main`. Copy any modified Haiku files into `distro/` preserving original headers/copyright and wrapping all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`).

## Goal: Raise the AMD64 baseline to `x86-64-v2` (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B) for user space AND the kernel; the boot loaders stay on the plain x86-64 baseline

1. **Compiler flags in `ArchitectureRules` (via `distro/` copy + apply hook)**
   - Read `build/jam/ArchitectureRules` in the Haiku tree (lines 37–51 for `archFlags` → `ccBaseFlags`, line 373 where the kernel takes `ccBaseFlags`, lines 300–316 and 446–495 for the `x86_64` kernel and boot loader flags).
   - Add `case x86_64 : archFlags += -march=x86-64-v2 ;`. Because `archFlags` ends up in `ccBaseFlags`, this also builds the kernel and kernel add-ons for v2. That is the owner's choice (18 Nov 2026).
   - **Boot loaders stay off v2:** make sure the x86_64 EFI loader and the 32-bit BIOS loader (`bios_ia32`, `-march=pentium`) are NOT built with `-march=x86-64-v2` (override their flags, or strip it for the boot targets). Otherwise the compiler may put a v2 instruction before the CPU check and an old CPU dies with `#UD` before any message. Show in the build log which flags each loader gets.
   - **Kernel:** Haiku's `ArchitectureRules` does not pass `-mno-sse` on `x86_64` and the kernel already saves XMM state. Verify that the kernel and kernel add-ons build cleanly and boot with SMAP on.

2. **CPU check in BOTH boot loaders**
   - In the `x86_64` EFI loader AND the BIOS loader (`bios_ia32`, which loads the 64-bit kernel), check CPUID before jumping to the kernel for the v2 features (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B).
   - If one is missing, print a clear message (e.g. `"This R5 Operating System build requires an x86-64-v2 CPU (SSE4.2 / POPCNT)."`) and halt cleanly instead of crashing later with `#UD`.
   - Put the check in one shared helper if the two loaders can share it; otherwise two small copies with the same text.

3. **QEMU scripts & tests**
   - Update `scripts/run-qemu-r5.sh` (currently `cpu=qemu64` at line 13) to pass `-cpu Nehalem` (or `EPYC-Rome` / `max` where appropriate) so QEMU provides `x86-64-v2`.
   - Keep ARM64 and RISC-V 64 build rules untouched.

## Verification & delivery
- Full AMD64 build with `scripts/build-locked.sh` in WSL (`jam_exit=0`).
- Run two QEMU boots (using your own copy of `r5-os.image`, never `scripts/run-qemu-r5.sh` if another session may run):
  1. `-cpu Nehalem` (or `EPYC-Rome`) with SMAP on → boots cleanly to desktop, no `#UD` / panic.
  2. `-cpu qemu64` (below v2) → the BIOS loader prints the clear CPUID message and stops cleanly (our QEMU boots through BIOS).
  3. If OVMF is available in WSL: the same with `-bios OVMF.fd` for the EFI loader. If not, say so; the EFI check is then only tested on a stick.
- Update `CHANGELOG.md` (newest first) and `docs/64BIT.md` status if appropriate.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```

---

## Prompt 4 — `x86-64-v3` (AVX2 / FMA / BMI2) runtime dispatch in hot paths (Argon)

Nagekeken (18 nov 2026): de kernel zet AVX (en AVX-512) al aan in XCR0 (`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:1843-1863`), dus de dispatch kan echt aangaan. Eerst meten, dan pas AVX2-paden. M13 staat stil (alleen Skia standaard aan rest nog, met een stick); AMD64 gaat nu voor. Advies: houd AVX2-wijzigingen in app_server buiten de stick die Skia standaard aanzet, zodat een fout op die stick één oorzaak heeft.

```markdown
Read `AGENTS.md` and `docs/64BIT.md` (sections 1, 5, 6 Step 4, and 7) first.
Work on a new branch from `origin/main` AFTER `x86-64-v2` is merged. Copy any modified Haiku files into `distro/` preserving original headers/copyright and wrapping all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`).

## Goal: Keep `x86-64-v2` as the baseline and add CPUID runtime dispatch for `x86-64-v3` (AVX2/FMA/BMI2) in hot paths

1. **Safe CPUID + `XCR0` detection helper (user space)**
   - Add a small shared helper (using `cpuid` + `xgetbv(0)`) that returns `true` for `x86-64-v3` ONLY when:
     - CPUID reports `OSXSAVE`, `AVX`, `AVX2`, `FMA`, `BMI1`, `BMI2`, `MOVBE`, `F16C`, AND
     - `XCR0` bits 1 (XMM) and 2 (YMM) are both enabled by the OS/hypervisor.
   - Never assume AVX2 is usable without checking `OSXSAVE` and `XCR0`.

2. **Measure first, then dispatch (no global `-march=x86-64-v3`)**
   - Before writing any AVX2 code: measure the candidate paths on the current build (`scripts/qemu-m13-measure.py` for the compositor, a small host benchmark for blend loops) and write the numbers down. Only a path that costs measurable time gets an AVX2 version.
   - The kernel already enables AVX in XCR0 (`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:1843-1863`); the helper below still checks it at run time.
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
- Measure before/after on the same benchmark as step 2 (note hardware vs QEMU). Keep these changes out of the stick that turns Skia on by default (M13), so each stick tests one change.
- Update `CHANGELOG.md` (newest first) with what changed and what was tested.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```

---

## Prompt 5 — R5 CPU-informatie: wat de CPU kan en wat R5 gebruikt (Cursor / Argon)

Nagekeken (18 nov 2026), met de keuzes van de eigenaar:
- Gesplitst: 5a breidt de kerneldriver uit (alleen lezen), 5b bouwt het venster.
- Het venster is een los programma "CPU Info", geopend met een knop op Settings → System → Information. Geen nieuwe pagina in de Settings-lijst en geen Back-knop (AGENTS.md).
- De regel "2MB-pages" komt pas na prompt 2b; tot dan "not yet". PCID staat bij de 5625U op "not available on this CPU" (stick 49: CPUID leaf 1 ECX bit 17 = 0).
- Na prompt 3 start R5 niet meer met `-cpu qemu64`; de test gebruikt `Nehalem` (v2) en `EPYC-Rome` (v3).

### Prompt 5a — kernel: actieve CPU-functies uitlezen (alleen lezen)

```markdown
Read `AGENTS.md`, `distro/kernel/x86/r5_pstate.cpp` and `distro/cpu/r5_pstate_log.cpp` first. Work on a new branch from `origin/main`. Kernel driver change only. R5 changes MIT with SPDX headers or SPDX-Snippet blocks.

Goal: `/dev/misc/r5_pstate` also reports, read-only, which CPU features the kernel actually turned on, so a program can show it.

1. Add a new read request at the end (do not change a request or struct that shipped). It returns one line of flags the kernel has enabled: `SMEP`, `SMAP`, `NX`, `PAT`, `x2APIC`, `INVPCID` (used for the global TLB flush since #4428), `CLFLUSHOPT` (used since #4428), `PCID` (enabled or not available), `2MB-pages` (report "not yet" until prompt 2b lands), `CPPC/HWP`, and the XCR0 mask the kernel set.
2. Read state only from the kernel's own variables and CR4/XCR0. Never write an MSR. Copy to user memory only with `user_memcpy()` after `IS_USER_ADDRESS()` (SMAP).
3. Describe the new request in `docs/LIBR5.md` or the driver's doc in the same change.

Verification: jam with `scripts/build-locked.sh` (`jam_exit=0`); one short QEMU boot with SMAP on (own image copy) with `-cpu EPYC-Rome`; read the new request once (a tiny test call or `r5_pstate_log`) and paste the line. Update `CHANGELOG.md`. Commit locally; ask me before push/PR. Report in Dutch, short, with [V]/[I].
```

### Prompt 5b — programma "CPU Info" en de knop op Information

```markdown
Read `AGENTS.md` (Settings window rules), `docs/LIBR5.md`, `docs/APPEARANCE-KIT.md`, R5Book, the Information page in `distro/settings/`, `distro/apps/devicemanager/Jamfile`, and the request added in prompt 5a first. Work on a new branch from `origin/main` after 5a is merged. New R5 files are MIT (`SPDX-FileCopyrightText: 2026 R5OS Contributors`, `SPDX-License-Identifier: MIT`).

Goal: a separate program "CPU Info" (`distro/apps/cpuinfo/`, like `distro/apps/devicemanager/`, in the regular image) that shows what the CPU can do and what R5 uses, opened from a button on the Information page.

1. **The button:** on Settings → System → Information, one button "CPU details…" in the page content that starts the CPU Info program. Nothing else in Settings changes: no new list item, no Back button, the order and descriptions stay as AGENTS.md fixes them.
2. **The window** (R5 frame, `BAppearanceKit::BCard` from `libr5ui.so`, product name R5, never Haiku/BeOS), two sections:
   - **CPU hardware** (CPUID + `xgetbv`): vendor, brand string, family/model/stepping, physical cores / logical threads, cache sizes (L1/L2/L3); the level the hardware supports: `x86-64-v1`, `v2` (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B), `v3` (AVX, AVX2, FMA, BMI1, BMI2, MOVBE, F16C), `v4` (AVX-512); extra features: AES-NI, SHA, INVPCID, PCID, CLFLUSHOPT, PDPE1GB, invariant TSC, x2APIC, SMEP, SMAP, NX, PAT.
   - **What R5 uses:** the build baseline (from compiler macros such as `__SSE4_2__` / `__POPCNT__`); the vector state the OS enabled (XCR0: XMM, YMM, ZMM); the kernel line from prompt 5a. A feature the CPU lacks shows "not available on this CPU" (for example PCID on the 5625U); "2MB-pages" shows "not yet" until prompt 2b.
3. **Copy report** button (plain text to `be_clipboard`) and `--cli` (the same text on stdout, for Terminal and stick scripts).
4. **Fallbacks:** on non-x86_64, or when the 5a request is missing, show "Unknown" / "Not reported"; never crash.

Verification: jam with `scripts/build-locked.sh` (`jam_exit=0`). QEMU with SMAP on (own image copy): start CPU Info from the Information button and with `--cli`, under `-cpu Nehalem` and `-cpu EPYC-Rome`; the hardware section must change between the two, the R5 section must match the build. UI work: no screenshot series; I check it in VirtualBox. Update `CHANGELOG.md`. Commit locally; ask me before push/PR. Report in Dutch, short, with [V]/[I].
```
