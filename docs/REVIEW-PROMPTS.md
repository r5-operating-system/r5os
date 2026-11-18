# Review Prompts: Networking, Kernel & AMD64 Performance

Verzamelde prompts uit de read-only reviews van 18 nov 2026 (`origin/main` bij `#4420`), klaar om één voor één uit te voeren op een aparte branch.

---

## Prompt 1 — IPv6 & Networking stack volledig afmaken en fixen (Cursor)

```markdown
# Opdracht: IPv6 & Networking stack volledig afmaken en fixen

Lees eerst `AGENTS.md`.
Werk op een eigen branch vanaf `origin/main` en raak **geen** Wi-Fi bestanden (`mt7922wifi`, `freebsd_wlan`, `libr5wifi`, `wpa_supplicant`) aan. Alle R5-wijzigingen in gekopieerde Haiku-bestanden komen in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blokken (MIT, 2026 R5OS Contributors).

**Regel:** Elk genummerd punt hieronder MOET worden opgepakt en opgelost. Niets overslaan, niets uitstellen.

---

## 1. `ipv6_first_mask_bit` default-route sortering (`distro/rest/src/add-ons/kernel/network/protocols/ipv6/ipv6_address.cpp`)
- **Probleem:** `if (allZero) return 128;` (regels 268–276) laat `::/0` met `128` sorteren i.p.v. `0`, waardoor een default route met all-zero `sockaddr_in6` mask in `routes.cpp:593` **boven** `/64`-routes terechtkomt.
- **Fix:** Verwijder de `allZero -> 128` override zodat `::/0` netjes `0` teruggeeft (de lus eronder doet dat al) en onderaan de routetabel sorteert.

## 2. IPv6 bronadres-selectie & L4 pseudo-header checksum voor TCP en UDP (`ipv6.cpp`, `udp.cpp`, `tcp`, `routes.cpp`)
- **Probleem:** `ipv6_prefer_nonlocal_source()` in `ipv6_send_routed_data()` (`distro/rest/src/add-ons/kernel/network/protocols/ipv6/ipv6.cpp:1342-1347`) vervangt `source.sin6_addr` (van `fe80::` naar global/ULA) **nadat** TCP of unconnected UDP de L4-checksum over `buffer->source` al heeft berekend, zonder die L4-checksum bij te werken. `distro/rest/src/add-ons/kernel/network/protocols/udp/udp.cpp` fixt dit alleen in `Connect()` (regels 411–439), niet in unconnected `SendData()`, en `tcp` (nog niet in `distro/`) heeft deze fix helemaal niet. Zodra `::/0` aan `fe80::` hangt, gaan TCP SYN-pakketten met een foute checksum de deur uit.
- **Fix:**
  1. Koppel in `routes.cpp` off-link routes waar mogelijk direct aan een non-link-local `InterfaceAddress` (en update bestaande default routes zo nodig zodra SLAAC/DHCPv6 een global/ULA-adres toevoegt).
  2. Zorg dat `udp.cpp` (zowel `Connect()` als unconnected `SendData()`) én `tcp` (kopieer benodigd `tcp` bestand naar `distro/` als dat nodig is) bij een off-link IPv6-bestemming altijd **vóór** de L4-checksum het non-link-local bronadres kiezen, én/of pas in `ipv6_send_routed_data()` bij een gewijzigd `source.sin6_addr` de TCP/UDP-checksum incrementaal aan (RFC 1624).

## 3. Gebonden bronadres respecteren bij lokale bestemmingen (`ipv6.cpp`, `routes.cpp`, `udp.cpp`)
- **Probleem:** `ipv6.cpp:1342-1344`, `routes.cpp:767-775` en `udp.cpp:406-410` overschrijven `source` met `destination` zodra `is_local_address(destination)` waar is — ook als de socket expliciet aan een ander lokaal adres was gebonden.
- **Fix:** Vul `source = destination` alleen in als het bronadres nog unspecified (`::` / `INADDR_ANY`) is, en nooit over een expliciet gebonden bronadres heen. Houd ook hier de L4-checksum consistent als `source` wordt ingevuld.

## 4. `SLAACClient` & `DHCPv6Client` netjes afsluiten zonder FD-lekken (`distro/network/SLAACClient.cpp`, `DHCPv6Client.cpp`)
- **Probleem:** `stop_worker()` (`SLAACClient.cpp:61-73`, `DHCPv6Client.cpp:94-106`) roept na 500 ms `kill_thread()` aan terwijl `select()` op een ongeconnecteerde socket 1–2 s wacht of de thread in `r5_ipv6_dad()`/`AddAddress()` zit. `shutdown()` op ongeconnecteerde `SOCK_RAW`/`SOCK_DGRAM` wekt `select()` op Haiku niet altijd direct, waardoor `kill_thread()` de socket-FD kan lekken in `net_server`.
- **Fix:** Gebruik een schone wakeup (bijv. self-pipe of kortere `select`/poll + atomic stop-flag + `r5_ipv6_dad` interruptible maken) en wacht netjes tot de worker-thread zelf zijn socket sluit en uit `_Run()` keert, zonder `kill_thread()`.

## 5. DHCPv6 compleet maken (`distro/network/DHCPv6Client.cpp`)
- **Mask:** Voeg `IA_NA`-adressen toe met een `/128` mask (`ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff`) i.p.v. `/64` (`DHCPv6Client.cpp:352`, RFC 8415).
- **Status codes:** Parse en handel `OPT_STATUS_CODE` (code 13) af op zowel top-level als binnen `IA_NA` / `IA_ADDR` (bijv. `NoAddrsAvail`, `UnspecFail`), met duidelijke syslog-melding.
- **Lease lifecycle:** Implementeer T1/T2 lease-vernieuwing (`Renew` / `Rebind`) en verwijdering bij verlopen lifetime i.p.v. direct stoppen na de eerste `Reply`.

## 6. SLAAC compleet maken (`distro/network/SLAACClient.cpp`)
- **Meerdere prefixes:** Verwerk **alle** geldige `/64` `A=1` PIO-prefixes uit een RA (bijv. ULA + GUA tegelijk), niet alleen de laatste (`SLAACClient.cpp:324-357`).
- **Link-local DAD:** Voer `r5_ipv6_dad()` ook uit in `_EnsureLinkLocal()` voordat `fe80::...` wordt toegevoegd.
- **On-link prefixes (`L=1`):** Verwerk PIO-prefixes met `L=1` (ook als `A=0`) als on-link route op de interface, en respecteer prefix/router lifetimes bij vernieuwing of intrekking.

## 7. `resolv.conf` opschoning & link-local DNS (`distro/rest/src/servers/net/NetServer.cpp`)
- **Probleem:** `_ConfigureResolver()` (`NetServer.cpp:666-739`) leest eerst bestaande servers uit `resolv.conf` en kapt af bij 8 entries. Na wisselen van netwerk/router houden oude servers de lijst vol zodat nieuwe DNS-servers worden genegeerd. Daarnaast worden link-local RDNSS-adressen (`fe80::...`) zonder `%interface` weggeschreven.
- **Fix:** Geef nieuwe/actieve interface-servers voorrang (of bewaar servers per interface zodat oude interface-entries verdwijnen bij netwerkwissel), en ga netjes om met link-local RDNSS-adressen (`%<device>` zone-ID of expliciet filteren als de resolver geen zone-ID's ondersteunt).

## 8. Kleine fixes in `udp.cpp` en `routes.cpp`
- `distro/rest/src/add-ons/kernel/network/protocols/udp/udp.cpp:431`: vervang `local->sin6_addr.s6_addr[0] == 0` door `IN6_IS_ADDR_UNSPECIFIED(&local->sin6_addr) || IN6_IS_ADDR_LOOPBACK(&local->sin6_addr)` (zodat `0064:ff9b::/96` NAT64 etc. niet wordt overgeslagen).
- `distro/rest/src/add-ons/kernel/network/stack/routes.cpp:496-514`: neem de interface-lock (of gebruik veilige iteratie) rond `interface->CountAddresses()` / `AddressAt(i)` in `SIOCADDRT`.

## Verificatie & oplevereisen
1. Handmatige SMAP-check (`user_memcpy` / geen directe user-pointer dereferences) op alle gewijzigde kernel-networking code.
2. Bouw voor AMD64 met `scripts/build-locked.sh` in WSL.
3. Boot de gebouwde image in QEMU met SMAP aan (eigen kopie van `r5-os.image`, nooit `scripts/run-qemu-r5.sh` als andere sessies draaien) en test waar mogelijk in QEMU/VirtualBox (`ifconfig`, `route` volgorde, IPv6 UDP connected/unconnected, IPv6 TCP `connect()`, `resolv.conf`).
4. Werk `CHANGELOG.md` bij (newest first) en rapporteer per punt (1 t/m 8) wat er gewijzigd is en wat wel/niet getest kon worden.
```

---

## Prompt 2 — AMD64 kernel fixes & veilige optimalisaties (Argon)

```markdown
Read `AGENTS.md` first. Work on a new branch from `origin/main` (kernel changes only; do NOT touch Wi-Fi or IPv6 files). Wrap all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`).

Implement these AMD64 kernel fixes and optimizations (note: do NOT implement PCID — Stick 49 confirmed the 5625U has CPUID leaf 1 ECX bit 17 = 0):

1. **Microcode table bounds check (`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:1156`)**
   - In `find_microcode_amd()`, bound `for (uint32 i = 0; ...; i++)` by `section->size` (`(i + 1) * sizeof(amd_equiv_cpu_entry) <= section->size`) before reading `table[i].installed_cpu`, so a truncated/corrupt microcode container cannot read out of bounds.

2. **TLB range-flush limit (`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:1941-1950`)**
   - In `arch_cpu_invalidate_tlb_range()`, if the page count exceeds a small threshold (e.g. `> 64` pages, or `start > end`), fall back to `arch_cpu_user_tlb_invalidate(context)` (single CR3 reload) instead of looping `invalidate_TLB()` per page.

3. **Global TLB flush with `INVPCID` type 2 + 64-bit CR4 (`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:1926-1937`)**
   - Change `uint32 flags = x86_read_cr4();` to `uint64` (or `addr_t`) so upper CR4 bits are never truncated on AMD64.
   - If `x86_check_feature(IA32_FEATURE_INVPCID, FEATURE_7_EBX)` is set, use `invpcid` type 2 (all contexts including global) instead of toggling `CR4.PGE` twice; keep the `CR4.PGE` toggle as fallback.

4. **`clflushopt` in `arch_vm_flush_memory_type_range()` (`distro/kernel/src/system/kernel/arch/x86/arch_vm.cpp:369-388`)**
   - When `x86_check_feature(IA32_FEATURE_CLFLUSHOPT, FEATURE_7_EBX)` is available, use `clflushopt` (with the trailing `sfence`) instead of serialized `clflush`. Keep `clflush` as fallback.

5. **`amd_pstates` scheduler mode & `uninit_pstates()` (`distro/kernel/src/add-ons/kernel/power/cpufreq/amd_pstates/amd_pstates.cpp:111-124`)**
   - Respect or restore the previous scheduler mode instead of unconditionally leaving `SCHEDULER_MODE_LOW_LATENCY` forced on `uninit_pstates()`, and disable CPPC (`MSR_AMD_CPPC_ENABLE = 0`) or restore on `uninit_pstates()` cleanly.

## Verification & delivery
- Jam AMD64 with `scripts/build-locked.sh` in WSL (`jam_exit=0`).
- Run one short QEMU boot with SMAP on (using your own copy of `r5-os.image`, never `scripts/run-qemu-r5.sh` if another session may run) and verify no panic and clean `arch_cpu` / `arch_vm` init.
- Update `CHANGELOG.md` (newest first) with what changed and what was tested.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```

---

## Prompt 3 — 2 MB large pages op AMD64 (`x86_64` MMU) (Argon)

```markdown
Read `AGENTS.md` and `docs/reviews/KERNEL-AMD64-REVIEW.md` (section 4, "TLB flushes and page size") first.
Work on a new branch from `origin/main` (kernel MMU/VM changes only). Copy any modified Haiku kernel files into `distro/kernel/` preserving original headers/copyright and wrapping all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`). Hook new `distro/kernel/` files into `scripts/apply-kernel-bugs.py`.

## Goal: 2 MB large pages for large anonymous / heap mappings on AMD64

Currently `X86PagingMethod64Bit.cpp` only uses 2 MB pages for the physical map and refuses large pages for normal user/kernel mappings, so large areas use 512× more TLB entries than needed.

Implement safe, clean **2 MB large page (`PDE` PS bit)** support on `x86_64`:

1. **Inspect & design before editing**
   - Read `X86PagingMethod64Bit.cpp`, `X86VMTranslationMap64Bit.cpp`, `vm.cpp`, and `vm_page.cpp` in the Haiku tree.
   - Verify how `Map()`, `Unmap()`, `UnmapPages()`, `Protect()`, `Query()`, `ClearFlags()`, and `MaxPagesNeededToMap()` handle 2 MB PDEs vs 4 KB PTEs, and how `vm_page` contiguous allocations work.

2. **2 MB mapping path (`x86_64`)**
   - When mapping a 2 MB-aligned virtual range (`>= 2 MB`) backed by physically contiguous 2 MB-aligned pages, allow a 2 MB PDE (`PS` bit) instead of splitting into 512× 4 KB PTEs.
   - Correctly translate page flags for 2 MB PDEs (specifically **PAT bit** is bit 12 in a 2 MB PDE vs bit 7 in a 4 KB PTE, **NX** bit 63, **User/Write/Global**).
   - Keep 4 KB pages as fallback whenever alignment or physical contiguity does not hold.

3. **Safe splitting on partial operations (CRITICAL)**
   - If `Unmap()`, `UnmapPages()`, `Protect()`, or `ClearFlags()` touches only part of a 2 MB large page (not full 2 MB aligned), **split the 2 MB PDE into 512× 4 KB PTEs** first (preserving all flags, dirty/accessed bits, and PAT/NX), invalidate TLB properly, and then apply the operation. Never panic or corrupt adjacent pages.
   - Ensure `Query()` and page-fault / CoW paths work seamlessly on 2 MB-mapped areas.

4. **Observability & safety**
   - Log once at boot or under a trace/sysctl counter when 2 MB user/kernel mappings are created/split so we can verify on hardware.
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

## Prompt 4 — `x86-64-v2` (SSE4.2) user-space baseline (Argon)

```markdown
Read `AGENTS.md` and `docs/64BIT.md` (sections 1, 3, 5, 6 Step 3, and 7) first.
Work on a new branch from `origin/main`. Copy any modified Haiku files into `distro/` preserving original headers/copyright and wrapping all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`).

## Goal: Raise the AMD64 user-space baseline to `x86-64-v2` (SSE4.2, SSSE3, POPCNT, CMPXCHG16B)

1. **Compiler flags in `ArchitectureRules` (via `distro/` copy + apply hook)**
   - Add the `x86_64` case to set `-march=x86-64-v2` for user-space targets (and verify `distro/UserBuildConfig` / Mesa recipe if needed).
   - **CRITICAL kernel safety check:** Verify that the kernel and kernel add-ons keep `-mno-sse -mno-sse2 -mno-mmx` (and `-mno-sse4.2` / `-mno-ssse3` or equivalent) so `-march=x86-64-v2` NEVER emits XMM/SSE register instructions in kernel mode. Verify with `objdump` on the kernel binary.

2. **EFI boot loader CPUID check**
   - In the `x86_64` EFI boot loader, check CPUID before jumping to the kernel for `x86-64-v2` features (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B).
   - If any required feature is missing, print a clear human-readable message (e.g. `"This R5 Operating System build requires an x86-64-v2 CPU (SSE4.2 / POPCNT)."`) and halt cleanly instead of crashing later with `#UD` (invalid opcode).

3. **QEMU scripts & tests**
   - Update `scripts/run-qemu-r5.sh` (and any other QEMU test scripts that default to `qemu64`) to pass `-cpu Nehalem` (or `EPYC-Rome` / `max` where appropriate) so QEMU provides `x86-64-v2`.
   - Keep ARM64 and RISC-V 64 build rules untouched.

## Verification & delivery
- Full AMD64 build with `scripts/build-locked.sh` in WSL (`jam_exit=0`).
- Verify kernel binary has no SSE/XMM instructions in kernel code (`objdump` check).
- Run two QEMU boots (using your own copy of `r5-os.image`, never `scripts/run-qemu-r5.sh` if another session may run):
  1. `-cpu Nehalem` (or `EPYC-Rome`) with SMAP on → boots cleanly to desktop, no `#UD` / panic.
  2. `-cpu qemu64` (below v2) → prints the clear CPUID error message in the EFI loader and stops cleanly.
- Update `CHANGELOG.md` (newest first) and `docs/64BIT.md` status if appropriate.
- Commit locally on the new branch. Ask me before `git push` / PR.
- Report in Dutch, short (mark `[V]` = verified in code/output, `[I]` = inference).
```

---

## Prompt 5 — `x86-64-v3` (AVX2 / FMA / BMI2) runtime dispatch in hot paths (Argon)

```markdown
Read `AGENTS.md` and `docs/64BIT.md` (sections 1, 5, 6 Step 4, and 7) first.
Work on a new branch from `origin/main` AFTER `x86-64-v2` is merged. Copy any modified Haiku files into `distro/` preserving original headers/copyright and wrapping all R5 changes in `SPDX-SnippetBegin` / `SPDX-SnippetEnd` blocks (`MIT`, `2026 R5OS Contributors`).

## Goal: Keep `x86-64-v2` as the baseline and add CPUID runtime dispatch for `x86-64-v3` (AVX2/FMA/BMI2) in hot paths

1. **Safe CPUID + XCR0 detection helper (user space)**
   - Add a small shared helper (using `cpuid` + `xgetbv(0)`) that returns `true` for `x86-64-v3` ONLY when:
     - CPUID reports `OSXSAVE`, `AVX`, `AVX2`, `FMA`, `BMI1`, `BMI2`, `MOVBE`, `F16C`, AND
     - `XCR0` bits 1 (XMM) and 2 (YMM) are both enabled by the OS/hypervisor.
   - Never assume AVX2 is usable without checking `OSXSAVE` and `XCR0` (VMs can expose AVX2 in CPUID while masking `XCR0`).

2. **Runtime dispatch in the measured hot paths (no global `-march=x86-64-v3`)**
   - Keep the global build baseline at `x86-64-v2`.
   - Compile only the selected hot-path translation units or functions with `__attribute__((target("avx2,fma,bmi2")))` (or `-mavx2 -mfma -mbmi2` on a dedicated `.cpp` file), selected once at init via function pointer (same pattern as `libroot`'s `memcpy.cpp`).
   - Primary candidates per `docs/64BIT.md`:
     - `app_server` compositor / `Painter` blend & pixel-conversion loops (`distro/` copy),
     - `libroot` / `media` / image-scaling or checksum hot loops where profiling/measurement shows a real gain.

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
