# Verified AMD64, Kernel & CPUID Prompts

Geverifieerde prompts (gecontroleerd tegen `origin/main` bij `#4426`, de Haiku-tree in `/home/nickr/r5os/haiku/`, en AMD64/Intel SDM documentatie). Prompt 2 tot en met 5 opnieuw nagekeken op 18 nov 2026, na `#4428`; wat daarbij veranderde staat per prompt onder "Nagekeken".

- **IPv6 & Networking (Prompt 1 uit `REVIEW-PROMPTS.md`):** `[DONE in #4425]` (`91f9d96a31`).
- **Prompt 1 (AMD64 kernel fixes):** `[DONE in #4428]`.
- **Prompt 2:** gesplitst in 2a (ontwerp, `[DONE in #4430]`) en 2b (bouwen, verstuurd).
- **Prompt 4 tot en met 6** (18 nov 2026): de oude prompts 4 (AVX2-dispatch) en 5 (CPU Info) samengevoegd met de AMD64-inventarisatie van die dag tot drie prompts: 4 kernel, 5 user space, 6 CPU Info. Elk onderdeel wordt een eigen commit met een eigen correctheidscontrole. Geen meting per stap; één meting aan het eind van prompt 4 en 5: het image van vóór de prompt tegenover het eindresultaat (eigenaar, 18 nov 2026).

Volgorde: 2a → 2b. Prompt 3 (x86-64-v2) kan los van 2 lopen. Prompt 4 en 5 komen na prompt 3 en kunnen tegelijk lopen (andere bestanden). Prompt 6 als laatste, met de regel "2MB-pages" pas na 2b.

Afspraak voor 4 en 5: alles wat in x86-64-v2 zit (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B) wordt zonder dispatch gebruikt. Alleen wat erboven zit (AVX2, FMA, BMI1/2, ERMS, FSRM, XSAVEOPT/XSAVEC/XSAVES, TSC-deadline, MWAIT, RDPID) wordt bij het starten gekozen via CPUID, met de v2-code als terugval. AMD-only en Intel-only paden mogen, mits zo gekozen; het geheel moet op Intel en AMD sneller worden.

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

## Prompt 2a — 2 MB pages op AMD64: alleen ontwerp (Argon) `[DONE in #4430]`

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

## Prompt 2b — 2 MB pages op AMD64: bouwen (Argon) `[VERSTUURD]`

Nagekeken (18 nov 2026), na de review van het ontwerp (#4430):
- Correctie op 2a: break-before-make (de PDE eerst leegmaken) laat een moment zonder mapping, waarin een andere CPU of een interrupt (GPU-ringen, VRAM) op een kerneladres een page fault krijgt: een panic. 2b splitst daarom zonder gat: eerst een 4 KB-tabel met dezelfde frames en attributen, dan de PDE in één keer omzetten, dan flushen, en pas daarna de echte wijziging. De bouwer citeert de regel uit de Intel SDM en AMD APM en stopt bij twijfel.
- De mask-fix (`X86_64_PDE_LARGE_ADDRESS_MASK`, PAT in `X86_64_PTE_MEMORY_TYPE_MASK`) is een aparte commit.
- De zelftest test ook een user-clone met een team dat afsluit (destructor, RADV-risico) en een write-combining 2 MB-mapping (mask-bug).
- Eén regel in de seriële log per 2 MB-mapping en per split, begrensd.

```markdown
## Prompt 2b — 2 MB pages on AMD64: build (Argon)

Read AGENTS.md first, then docs/AMD64-LARGE-PAGES.md (the approved design, #4430) completely, and docs/reviews/KERNEL-AMD64-REVIEW.md section 4. Work on a new branch from origin/main (kernel MMU/VM only; do not touch Wi-Fi, IPv6, GPU drivers or app_server). Copy every Haiku file you change into distro/kernel/ with its original header and copyright, wrap all R5 changes in SPDX-SnippetBegin/End (MIT, 2026 R5OS Contributors), and hook new copies into scripts/apply-kernel-bugs.py.

Implement the design, with these corrections from the review:

1. Separate fix first (own commit, also useful without large pages):
   - Add X86_64_PDE_LARGE_ADDRESS_MASK = 0x000fffffffe00000 and use it wherever a 2 MB PDE is read: Query() (X86VMTranslationMap64Bit.cpp:489-491) and DebugGetReverseMappingInfo().
   - Add X86_64_PTE_PAT to X86_64_PTE_MEMORY_TYPE_MASK, so Protect() clears PAT when the memory type changes.

2. Splitting a 2 MB PDE: do NOT use break-before-make. Clearing the PDE leaves a window in which another CPU or an interrupt (GPU rings, VRAM) faults on a kernel address and panics. Instead:
   a. fill the standby 4 KB page table with 512 PTEs that keep exactly the same frames and attributes (PAT bit 12 → bit 7, all other bits as in the design), then a write barrier;
   b. switch the PDE to that table in one atomic store;
   c. invalidate the 2 MB range on all CPUs, synchronously;
   d. only then apply the caller's change (unmap or protect) to the 4 KB PTEs, followed by its own invalidation.
   Before coding, check this in the Intel SDM (section 4.10, changing page size with the same frame and attributes) and the AMD APM, and quote the rule in CHANGELOG.md. If the manuals say otherwise, stop and report to me before choosing.

3. Everything else as the design says:
   - the kernel setting amd64_large_pages, off by default, logged at boot;
   - 2 MB PDEs only for vm_map_physical_memory, vm_clone_area of device memory, B_CONTIGUOUS, and B_FULL_LOCK with a 2 MB run, with 4 KB as the fallback;
   - one standby page-table page per 2 MB PDE, taken from the existing MaxPagesNeededToMap reservation, so a split never allocates;
   - every VMTranslationMap method in section 4 of the design, including the ~X86VMTranslationMap64Bit guard for large-page PDEs in user space;
   - fMapCount in 4 KB pages.

4. Logging: one serial line per 2 MB PDE created and per split ("R5 VM: 2MB map …", "R5 VM: 2MB split … (reason)"), rate-limited so a large area does not flood the log.

5. Self-test (setting amd64_large_pages_selftest, or automatically once when amd64_large_pages is on), as section 5.2 of the design, plus:
   - clone the 4 MB area into a user team, read it there, and let that team exit (this tests the destructor guard, the RADV risk);
   - test one 2 MB PDE with B_WRITE_COMBINING_MEMORY and check Query() returns the exact physical address (the mask fix).
   Log "R5 VM: amd64_large_pages self-test PASS …" or "FAIL at step …".

Verification:
- scripts/build-locked.sh in WSL: jam_exit=0.
- QEMU on your own copy of r5-os.image (not scripts/run-qemu-r5.sh), SMAP on, three short boots:
  1. setting off: boots as before, the log says disabled;
  2. setting on, without the self-test: boots to net_server, no panic, 2 MB map lines for the framebuffer or other device memory;
  3. setting on with the self-test: the PASS line, no panic.
- Paste the relevant serial lines.
- CHANGELOG.md: what changed, the SDM/APM quote, what QEMU showed. Hardware is not tested: the 5625U stick (design section 6) comes later.
- Commit locally on the new branch. Ask me before push/PR. Report in Dutch, short, with [V]/[I].
```

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

## Prompt 4 — AMD64 kernel: release-kernel, snelle paden, scheduler en idle (Argon)

Nagekeken (18 nov 2026, inventarisatie op `origin/main`): `haiku/build/config_headers/kernel_debug_config.h:8` heeft `KDEBUG_LEVEL 2` en niets in `distro/` of de scripts zet dat lager [V]; de netwerkchecksum (`haiku/src/add-ons/kernel/network/stack/utility.cpp:102`) telt per 16 bits met "TODO: unfold loop for speed" [V]. De andere punten komen uit de inventarisatie en staan in de prompt met hun plek in de code; de bouwer controleert ze eerst.

```markdown
## Prompt 4 — AMD64 kernel: release kernel, fast paths, scheduler and idle

Read AGENTS.md first. Work on a new branch from origin/main AFTER prompt 3 (x86-64-v2) is merged: the kernel is built for v2, so everything in v2 is used without a check; anything above v2 is chosen at boot by CPUID with the current code as the fallback. Copy every Haiku file you change into distro/kernel/ with its original header and copyright, wrap all R5 changes in SPDX-SnippetBegin/End (MIT, 2026 R5OS Contributors), and hook new copies into scripts/apply-kernel-bugs.py. Do not touch Wi-Fi, IPv6, Bluetooth, GPU drivers or app_server. AMD-only or Intel-only paths are fine when chosen at run time; the whole must make both Intel and AMD faster. Check every location below in the code before changing it, and say in the report if one is not as described.

One commit per numbered step. After each step: jam, and one short QEMU boot with SMAP on (own copy of r5-os.image, not scripts/run-qemu-r5.sh) that reaches net_server without a panic. Each step proves it is correct as described in that step; no speed measurements.

1. Release kernel. Add a release build profile with KDEBUG_LEVEL 0 (haiku/build/config_headers/kernel_debug_config.h:8 is 2 today; level 2 turns on paranoid malloc/free, spinlock debugging, page-access and heap debugging, and makes every mutex_lock take the slow path). The image built by scripts/build-locked.sh uses the release profile; a debug profile with level 2 stays available for QEMU and test runs (a switch in distro/UserBuildConfig or a build-locked.sh option, documented in docs/). Boot both kernels in QEMU with SMAP on; the debug kernel must still catch what it caught before (it is the one for test runs).

2. Network checksum: replace the 16-bit loop in haiku/src/add-ons/kernel/network/stack/utility.cpp:102 with a 64-bit add-with-carry loop, unrolled (as Linux csum_partial). Results must be bit-identical: add a host fuzz target that compares old and new on random buffers, odd lengths and odd start addresses (60 s).

3. Syscall time accounting: haiku/src/system/kernel/arch/x86/64/interrupts.S:77-109 (UPDATE_THREAD_USER_TIME / UPDATE_THREAD_KERNEL_TIME) takes a spinlock and reads the clock on syscall entry and exit. Only the current thread writes these times: use a per-thread sequence counter instead of the spinlock, change every reader (thread.cpp, team.cpp, the scheduler) to the retry read, and read the clock once per round trip if the accounting allows it. Check: the user and kernel times that `ps`/ProcessController show still add up to the wall time of a busy test thread.

4. FPU state on interrupts (interrupts.S:222-279, arch_cpu.cpp around 1857):
   - zero only the 64-byte XSAVE header instead of the whole save area before XSAVE;
   - choose at boot: XSAVES/XRSTORS (with IA32_XSS = 0) when present, else XSAVEOPT, else XSAVEC, else today's XSAVE. Convert the format where signal frames copy this area (64/thread.cpp:377-471);
   - Intel only, chosen at run time: vzeroupper on syscall entry when AVX is enabled.
   Check: a test program that keeps known values in XMM/YMM registers across many syscalls, signals and interrupts gets them back unchanged, under -cpu Nehalem and -cpu EPYC-Rome.

5. Timer: haiku/src/system/kernel/arch/x86/timers/x86_apic.cpp:50-74 does four APIC writes per timer. Write the initial count once (a write restarts a one-shot timer). Use TSC-deadline mode (one WRMSR) when CPUID leaf 1 ECX bit 24 is set and the CPU is not on the known errata list; keep the APIC count as the fallback. Check: snooze() and timers fire on time in both modes (a test that sleeps 1 ms, 10 ms and 1 s and checks system_time()).

6. User copies: user_memcpy/user_memset/user_strlcpy (haiku/headers/private/kernel/arch/generic/user_memory.h:59-80) use setjmp per call. Write x86_64 assembly versions with a fault entry, as the 32-bit version in 32/arch.S:129 does, using rep movsb (with FSRM, also for short copies). STAC/CLAC must stay paired: review by hand and boot with SMAP on. Check: a test that passes bad user pointers (NULL, kernel address, unmapped page, a buffer that ends on an unmapped page) gets B_BAD_ADDRESS, never a panic.

7. Idle wake-up: distro/kernel/src/system/kernel/scheduler/scheduler.cpp:144-145 always sends a reschedule IPI; the MWAIT idle in distro/kernel/src/add-ons/kernel/power/cpuidle/x86_acpi_cstates/acpi_cpuidle.cpp:229 monitors a dummy variable. MONITOR the CPU's own invoke_scheduler word, set a per-CPU "polling" flag while in MWAIT, and skip the IPI when the target CPU is polling. HLT idle keeps the IPI. Get the memory ordering right. Check: a ping-pong between two threads on different CPUs runs for a minute without a lost wake-up (no thread stuck waiting).

8. Scheduler topology: distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp reads the hybrid bit (around 542) and the cache-sharing masks (around 684-848) but the scheduler only uses SMT, core and package. Add a cache-domain level from the existing sharing masks (AMD CCX and Intel alike), and on Intel hybrid CPUs read CPUID leaf 0x1A and prefer P-cores for busy threads. Behind topology detection; no change on CPUs without these. Check: the topology the scheduler builds is logged once at boot and matches the CPU (QEMU -smp with sockets/cores/threads, and the 5625U).

Final measurement, once, after the last step: before step 1, keep a copy of the r5-os.image built from origin/main ("before"). At the end, run the same small benchmark set (MIT, under distro/) on "before" and on the final image ("after") in QEMU, same CPU model and settings: a null syscall loop (ns per call), a thread ping-pong across CPUs (round trips per second), a TCP transfer over loopback (MB/s), and boot time to net_server. Put the before/after table in CHANGELOG.md and the PR, noting that QEMU is not hardware; run the same set once on the 5625U stick.

Verification: jam_exit=0 for every step; the QEMU boot with SMAP on after every step; each step's own check. CHANGELOG.md says per step what changed and what was checked. Hardware: one 5625U stick at the end that boots, runs the desktop, GPU, Wi-Fi and Bluetooth as before, and restarts and powers off cleanly. Commit locally; ask me before push/PR. Report in Dutch, short, with [V]/[I].
```

---

## Prompt 5 — AMD64 user space: build-vlaggen, CPU-dispatch, libroot, libbe en app_server (Argon)

Nagekeken (18 nov 2026, inventarisatie op `origin/main`): `distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp` gebruikt `rep movsb` vanaf 2048 bytes zonder ERMS-check [V]; `pthread_mutex.cpp` probeert één `atomic_test_and_set` en gaat dan naar `_kern_mutex_lock` [V]; `FontCache.cpp:108` heeft `kMaxEntryCount = 30` [V]; Painter's SIMD-controle staat alleen onder `#if __i386__` (`Painter.cpp:120`) [V]; `BuildSetup:714` is `OPTIM ?= -O2` [V]. Dit vervangt de oude prompt 4 (AVX2-dispatch); die zit nu in stap 2 en 10.

```markdown
## Prompt 5 — AMD64 user space: build flags, CPU dispatch, libroot, libbe and app_server

Read AGENTS.md first, and docs/LIBR5.md before touching libbe or app_server. Work on a new branch from origin/main AFTER prompt 3 (x86-64-v2) is merged: everything in v2 (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B) is used directly; AVX2, FMA, BMI1/2, ERMS and FSRM are chosen at run time by CPUID with the v2 code as the fallback. The runtime_loader has no IFUNC: dispatch with a function pointer set once at init, as haiku/src/system/libroot/os/arch/x86_64/system_time.cpp does, and keep small sizes on an inline path without the indirect call. Copy every Haiku file you change into distro/ with its original header, wrap R5 changes in SPDX-SnippetBegin/End (MIT), hook copies into the existing apply scripts. libbe and libroot keep their ABI. Do not touch Wi-Fi, IPv6, Bluetooth or the GPU drivers. Check every location below in the code before changing it.

One commit per numbered step. After each step: jam, and a QEMU boot with SMAP on (own image copy) that reaches the desktop. Each step proves it is correct as described in that step; no speed measurements.

1. Linker and visibility:
   - add -Wl,--hash-style=both to user-space LINKFLAGS (ArchitectureRules around line 65); the runtime_loader already reads DT_GNU_HASH. Kernel add-ons stay as they are;
   - -Bsymbolic-functions and -fno-semantic-interposition for libbe and app_server (not libroot: malloc must stay interposable);
   - -Wl,--gc-sections for app_server (distro/app_server/app_server-Jamfile); check that the decorator add-on symbols stay exported.
   Check: the desktop, Tracker, Deskbar, Settings, Terminal, WebPositive and a Vulkan/OpenGL demo start; readelf shows .gnu.hash in libbe.

2. CPU-feature helper in libroot (private header, one place): reads CPUID and XCR0 once (OSXSAVE plus XMM/YMM enabled before AVX/AVX2/FMA count), plus ERMS, FSRM, BMI2, and the last-level cache size (leaf 4 on Intel, 0x8000001D on AMD, fixed fallback). Used by every step below.

3. memcpy, memset, memmove (distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp; copy memset.cpp and musl memmove.c into distro/):
   - rep movsb/stosb only when ERMS is present; otherwise the existing SSE loop;
   - with FSRM, rep movsb from 128 bytes instead of 2048;
   - above about 3/4 of the last-level cache: non-temporal stores (movntdq) and sfence;
   - memmove with overlap: 16-byte SSE loops (never std; rep movsb);
   - keep the existing 4K-aliasing guard.
   Check: a fuzz test compares every function with the old version for all sizes 0-4096, every source and destination alignment 0-63, and random large sizes, including every overlap offset 1-64 for memmove (60 s), under -cpu Nehalem and -cpu EPYC-Rome.

4. String functions (musl strlen/strchr/strchrnul/memchr, Haiku strcmp.c and memcmp.c): v2 SSE2/SSE4.2 versions, 16 bytes per step, with aligned loads so no read crosses into an unmapped page; AVX2 32-byte versions by dispatch for long strings. Fuzz every function against the old C version (60 s each).

5. malloc (haiku/src/system/libroot/posix/malloc/openbsd): set the pool count at init to the CPU count rounded up to a power of two (cap _MALLOC_MUTEXES, 32), and create the heap mutexes with MUTEX_FLAG_ADAPTIVE. Check: a 12-thread malloc/free/realloc stress test runs for a minute without a crash or corruption, and the build of a large jam target still works inside R5.

6. Locks: pthread_mutex.cpp (one atomic_test_and_set, then _kern_mutex_lock) and os/locks/mutex.cpp: a short read-only spin with pause before the syscall, bounded by time (TSC), not by loop count, because pause costs very different cycles on Intel and AMD. pthread_rwlock.cpp: a reader-count fast path with atomic add instead of taking the internal mutex on every read. Stress with 12 threads; review the fail paths.

7. libm: fma/fmaf by dispatch to FMA3 (Intel Haswell and later, AMD Piledriver and later; keep the existing FMA4 branch for AMD Bulldozer); floor/ceil/trunc/rint with roundsd/roundss (SSE4.1, in v2, no dispatch). Results bit-identical to musl's C versions on a test set.

8. libbe ConvertBits (haiku/src/kits/interface/ColorConversion.cpp:817-875 calls a function per pixel): dedicated row loops for RGB32↔RGBA32, RGB24→RGB32, RGB16→RGB32 and CMAP8→RGB32 with SSSE3 pshufb (v2, no dispatch), AVX2 by dispatch; the generic path stays the fallback. Compare every pair with the old output.

9. app_server Painter: an SSE2 version of the bilinear x-loop (DrawBitmapBilinear.h around 602; Painter.cpp:120 only detects SIMD under __i386__ and the nasm loop is 32-bit only), pixel-identical to the C path; -O3 for Painter, AGG and drawing_modes only (per-directory OPTIM; BuildSetup:714 is -O2); FontCache kMaxEntryCount from 30 to 128 (FontCache.cpp:108). Check: the scaled DrawBitmap output is pixel-identical to the C path; text and icons look the same.

10. AVX2 in the compositor (the former prompt 4): AVX2 versions, chosen by the helper of step 2, of the blend and copy loops app_server runs itself (distro/app_server/Compositor.cpp, GPUCopy.cpp; Skia already dispatches its own v3/v4 paths). Check: output pixel-identical to the v2 path on a set of test frames. Keep this step out of the stick that turns Skia on by default.

Final measurement, once, after the last step: before step 1, keep a copy of the r5-os.image built from origin/main ("before"). At the end, run the same small benchmark set (MIT, under distro/) on "before" and on the final image ("after") in QEMU, same CPU model and settings, under -cpu Nehalem and -cpu EPYC-Rome: memcpy/memset at 64 B, 4 KB and 16 MB; strlen on 1 KB; a 12-thread malloc/free loop; a 12-thread mutex loop; BBitmap SetBits with color conversion; app start time (StyledEdit); scripts/qemu-m13-measure.py. Put the before/after table in CHANGELOG.md and the PR, noting that QEMU is not hardware.

Verification: jam_exit=0 for every step; the QEMU boot with SMAP on after every step; each step's own check; for every dispatched step, QEMU runs with -cpu Nehalem (v2, no AVX2) and -cpu EPYC-Rome (AVX2) give the same results. CHANGELOG.md says per step what changed and what was checked. UI changes: I check them in VirtualBox. Commit locally; ask me before push/PR. Report in Dutch, short, with [V]/[I].
```

---

## Prompt 6 — CPU Info: wat de CPU kan en wat R5 gebruikt (Cursor / Argon)

Was prompt 5 (5a en 5b), nu na prompt 4 en 5, zodat het alles kan tonen wat die aanzetten. Keuzes van de eigenaar (18 nov 2026): een los programma "CPU Info", geopend met een knop op Settings → System → Information; geen nieuwe pagina in de Settings-lijst en geen Back-knop. "2MB-pages" pas na prompt 2b; PCID op de 5625U "not available on this CPU". Na prompt 3 start R5 niet meer met `-cpu qemu64`; de test gebruikt `Nehalem` en `EPYC-Rome`.

```markdown
## Prompt 6 — CPU Info: what the CPU can do and what R5 uses

Read AGENTS.md (Settings window rules), docs/LIBR5.md, docs/APPEARANCE-KIT.md, R5Book, the Information page in distro/settings/, distro/kernel/x86/r5_pstate.cpp, distro/cpu/r5_pstate_log.cpp and distro/apps/devicemanager/Jamfile first. Work on a new branch from origin/main after prompts 4 and 5 are merged. New R5 files are MIT (SPDX-FileCopyrightText: 2026 R5OS Contributors, SPDX-License-Identifier: MIT). One commit per part.

Part A — kernel, read-only: a new read request at the end of /dev/misc/r5_pstate (do not change a shipped request or struct) that returns which features the kernel actually uses: SMEP, SMAP, NX, PAT, x2APIC, INVPCID, CLFLUSHOPT, PCID (enabled or not available), 2MB-pages ("not yet" until prompt 2b), CPPC/HWP, the XSAVE variant in use (from prompt 4), TSC-deadline or APIC timer, MWAIT idle, release or debug kernel, and the XCR0 mask. Read kernel variables and CR4/XCR0 only, never write an MSR; copy to user memory only with user_memcpy() after IS_USER_ADDRESS(). Describe the request in docs.

Part B — the program "CPU Info" (distro/apps/cpuinfo/, like devicemanager, in the regular image), opened by one button "CPU details…" in the content of Settings → System → Information. Nothing else in Settings changes: no new list item, no Back button, order and descriptions as AGENTS.md fixes them. R5 frame, BAppearanceKit::BCard (libr5ui.so), product name R5. Two sections:
- CPU hardware (CPUID + xgetbv): vendor, brand, family/model/stepping, cores/threads, caches; the level the hardware supports (x86-64-v1/v2/v3/v4); extra features (AES-NI, SHA, ERMS, FSRM, INVPCID, PCID, CLFLUSHOPT, PDPE1GB, invariant TSC, x2APIC, SMEP, SMAP, NX, PAT, hybrid).
- What R5 uses: the build baseline (compiler macros), the user-space paths the libroot helper of prompt 5 chose (memcpy, strings, libm), the vector state the OS enabled (XCR0), and the kernel line of part A. A feature the CPU lacks shows "not available on this CPU".
Plus a "Copy report" button (plain text to be_clipboard) and --cli (the same text on stdout). Without the part A request or on another architecture: "Unknown" / "Not reported", never a crash.

Verification: jam_exit=0; QEMU with SMAP on (own image copy) under -cpu Nehalem and -cpu EPYC-Rome: start CPU Info from the Information button and with --cli, read part A once; the hardware section changes between the two, the R5 section matches the build. UI: no screenshot series, I check it in VirtualBox. Update CHANGELOG.md. Commit locally; ask me before push/PR. Report in Dutch, short, with [V]/[I].
```

---

## Later

- Media Kit-mixer en resampler (`distro/media/.../mixer/MixerCore.cpp:692`, `Interpolate.cpp`): scalair, maar de belasting bij 48 kHz stereo is klein. Pas na 4 en 5, en alleen als een meting het toont.
- LTO voor libbe/libroot: 2–5% geschat, maar hoog risico (assembly in libroot, commpage, symboolversies). Na 4 en 5.
- Bewust zo gelaten: frame pointers (Debugger en `profile`), `-fno-tree-vectorize` in de kernel (Haiku-ticket #18593), Mesa en Skia (al release-builds met eigen CPU-dispatch).
