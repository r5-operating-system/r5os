<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: MIT
-->
# 2 MB Large Pages on AMD64 — Design (`PS` PDE Mappings)

Status: **design only** (no code changes). Follows `docs/reviews/KERNEL-AMD64-REVIEW.md` section 4 (`TLB flushes and page size`).

## Scope: Contiguous, Non-Paged Memory Only

This design limits 2 MB page directory entries (`X86_64_PDE_LARGE_PAGE`, bit 7 = `PS` in a 64-bit PDE, covering `k64BitPageTableRange = 0x200000` = 512 × 4 KB pages) to areas whose physical backing is **already physically contiguous and permanently wired** for the lifetime of the mapping:

1. **`vm_map_physical_memory()`** (`haiku/src/system/kernel/vm/vm.cpp:1942-2049`): MMIO/device memory, VESA/GOP framebuffers, and GPU VRAM/BAR apertures (`CACHE_TYPE_DEVICE`, `wiring == B_FULL_LOCK`).
2. **`B_CONTIGUOUS` areas** (`haiku/src/system/kernel/vm/vm.cpp:1733-1742`, `:1867-1899`): kernel and driver DMA buffers allocated in a single contiguous physical run via `vm_page_allocate_page_run()`.
3. **`B_FULL_LOCK` kernel areas** (`haiku/src/system/kernel/vm/vm.cpp:1795-1822`): only when a 2 MB virtual sub-range happens to be backed by a physically contiguous, 2 MB-aligned run (or when allocated as a 2 MB run).

### Why Anonymous, Pageable, CoW, and Swappable Areas Are Excluded

In the Haiku VM (`VMCache`, `vm_page`, `vm_page_mapping`, and `vm_page_writer.cpp` / page daemon), every page of a `B_NO_LOCK` or `B_LAZY_LOCK` area is tracked at **4 KB granularity**:

- **Per-4 KB `vm_page` and reverse mappings (`vm_page_mapping`)**: `map_page()` (`haiku/src/system/kernel/vm/vm.cpp:654-711`) inserts one `vm_page_mapping` per 4 KB `vm_page` into `page->mappings` and `area->mappings` (`:683-684`). `VMTranslationMap::UnmapArea()` (`haiku/src/system/kernel/vm/VMTranslationMap.cpp:89-164`) walks `area->mappings` per 4 KB `vm_page` (`:112-154`) and calls `UnmapPage()` on each 4 KB page.
- **Hardware `Accessed` and `Dirty` bits exist only once per PDE**: A 2 MB PDE has a single `X86_64_PDE_ACCESSED` (bit 5) and a single `X86_64_PDE_DIRTY` (bit 6) (`haiku/src/system/kernel/arch/x86/paging/64bit/paging.h:52-53`). When the page daemon scans pages via `vm_clear_page_mapping_accessed_flags()` (`vm.cpp:3429-3455`) or `vm_remove_all_page_mappings_if_unaccessed()` (`vm.cpp:3470-3490`), it calls `ClearAccessedAndModified(area, address, ...)` (`X86VMTranslationMap64Bit.cpp:636-711`) for a single 4 KB `vm_page`. Clearing `A`/`D` or unmapping on a 2 MB PDE would lose or falsify `Accessed`/`Modified` state for the other 511 `vm_page` structs in the 2 MB range.
- **Copy-on-Write (`vm_copy_on_write_area`) and fault handling (`vm_soft_fault`)**: `vm_copy_on_write_area()` (`vm.cpp:2844-2987`) and write faults copy and remap one 4 KB `vm_page` at a time (`vm.cpp:2903-2905`).
- **Page daemon & swap**: The page daemon evicts and pages out individual 4 KB `vm_page`s (`ASSERT(page->WiredCount() == 0)` at `vm.cpp:3472`). Supporting 2 MB pages for pageable memory would require compound/transparent huge page state, khugepaged-style collapse/split locks across `VMCache` and `vm_page`, and 512-page swap/dirty accounting.

By contrast, `CACHE_TYPE_DEVICE`, `B_CONTIGUOUS`, and wired `B_FULL_LOCK` areas never enter `area->mappings` (`map_page()` at `vm.cpp:661-696` only allocates `vm_page_mapping` when `area->wiring == B_NO_LOCK`), are never scanned by the page daemon (`page->WiredCount() > 0` or no `vm_page` at all for `CACHE_TYPE_DEVICE`), never participate in CoW (`wait_if_area_is_wired()`), and are never swapped.

---

## 1. Where Eligible Mappings Are Created and How 2 MB PDEs Fit

Today, `X86PagingMethod64Bit::PageTableForAddress()` (`haiku/src/system/kernel/arch/x86/paging/64bit/X86PagingMethod64Bit.cpp:384-387`) explicitly forbids 2 MB pages outside `KERNEL_PMAP_BASE`:

```cpp
// No proper large page support at the moment, but they are used for the
// physical map area. Ensure that nothing tries to treat that as normal
// address space.
ASSERT(!(*pde & X86_64_PDE_LARGE_PAGE));
```

### Call Sites in `haiku/src/system/kernel/vm/vm.cpp`

| Path | Location in `vm.cpp` | Current Mapping Loop | 2 MB Opportunity |
|---|---|---|---|
| `vm_map_physical_memory()` | `vm.cpp:1942-2049` (loop at `:2033-2036`) | Loops `offset` by `B_PAGE_SIZE`, calling `map->Map(area->Base() + offset, physicalAddress + offset, protection, area->MemoryType(), &reservation)` | Both `area->Base() + offset` and `physicalAddress + offset` advance in lockstep. Whenever `(va & 0x1fffff) == 0`, `(pa & 0x1fffff) == 0`, and `size - offset >= 0x200000`, one 2 MB PDE can be written instead of 512 PTEs. |
| `vm_create_anonymous_area()` (`B_CONTIGUOUS`) | `vm.cpp:1733-1742`, `:1867-1899` (loop at `:1879-1895`) | Allocates contiguous run via `vm_page_allocate_page_run()`, then loops `virtualAddress` and `physicalAddress` by `B_PAGE_SIZE`, calling `map->Map(...)`, `cache->InsertPage(page, offset)`, and `increment_page_wired_count(page)`. | Keep the per-4 KB `cache->InsertPage(page, offset)` and `increment_page_wired_count(page)` loop (so `UnmapPages()`/`PageUnmapped()` still finds all 512 wired `vm_page`s in `VMCache`), but map the aligned 2 MB sub-ranges in `VMTranslationMap` with a single 2 MB PDE. |
| `vm_create_anonymous_area()` (`B_FULL_LOCK`) | `vm.cpp:1795-1822` (loop at `:1800-1819`) | Allocates individual 4 KB pages via `vm_page_allocate_page()` (`:1813`) and calls `map_page()` (`:1816`). | Individual `vm_page_allocate_page()` calls are not guaranteed contiguous or 2 MB-aligned. A `B_FULL_LOCK` kernel area only gets 2 MB PDEs if either (a) for kernel `B_FULL_LOCK` areas `>= 2 MB` we attempt an opportunistic 2 MB-aligned `vm_page_allocate_page_run()` per 2 MB chunk (falling back to 4 KB pages on fragmentation), or (b) 4 KB pages happen to form a 2 MB-aligned contiguous run. |
| `vm_clone_area()` (`B_FULL_LOCK` + `CACHE_TYPE_DEVICE`) | `vm.cpp:2572-2616` (loop at `:2599-2603`) | Queries `sourceArea->Base()` physical address and maps `newArea->Base() + offset` to `physicalAddress + offset` in 4 KB steps. | Same lockstep `va`/`pa` progression as `vm_map_physical_memory()`; eligible for 2 MB PDEs whenever `newArea->Base()` and `physicalAddress` share `offset % 2 MB == 0`. |

### Virtual Address Alignment Note

For a 2 MB PDE to be possible, `(virtualAddress & 0x1fffff)` must equal `(physicalAddress & 0x1fffff)` (and at the 2 MB boundary both must be `0`). In `vm_map_physical_memory()` (`vm.cpp:1980-1985`) and `vm_create_anonymous_area()` (`B_CONTIGUOUS`, `vm.cpp:1733-1780`), when `addressSpec` is `B_ANY_KERNEL_ADDRESS` or `B_ANY_ADDRESS` and `size >= k64BitPageTableRange` (`2 MB`), if `(physicalAddress & 0x1fffff) == 0`, upgrading `virtual_address_restrictions.alignment` to `k64BitPageTableRange` (`0x200000`) guarantees that `area->Base()` is also 2 MB-aligned so the interior 2 MB chunks can use 2 MB PDEs instead of failing the alignment check.

### Conditions for Writing a 2 MB PDE (`Map2MB` or `MapRange`)

A 2 MB PDE may be written at `virtualAddress` if and only if all of the following hold:
1. The kernel setting `amd64_large_pages` is enabled.
2. `(virtualAddress & (k64BitPageTableRange - 1)) == 0` (`2 MB`-aligned virtual address).
3. `(physicalAddress & (k64BitPageTableRange - 1)) == 0` (`2 MB`-aligned physical address).
4. The remaining contiguous mapped range is `>= k64BitPageTableRange` (`0x200000`).
5. All 512 4 KB pages in `[virtualAddress, virtualAddress + 0x200000)` share the exact same `attributes` (protection) and `memoryType` (`B_UNCACHED_MEMORY`, `B_WRITE_COMBINING_MEMORY`, `B_WRITE_THROUGH_MEMORY`, `B_WRITE_PROTECTED_MEMORY`, or `B_WRITE_BACK_MEMORY`).
6. The existing PDE at `PageDirectoryEntryForAddress(..., virtualAddress, ...)` is currently not present (`(*pde & X86_64_PDE_PRESENT) == 0`).

---

## 2. Flag Translation: 2 MB PDE vs. 4 KB PTE

Definitions in `haiku/src/system/kernel/arch/x86/paging/64bit/paging.h:46-77` and `X86PagingMethod64Bit.h:166-184`:

| Field / Bit | 4 KB PTE (`paging.h:61-71`) | 2 MB PDE (`PS=1`, `paging.h:47-58`) | Difference / Notes |
|---|---|---|---|
| Present (`P`) | Bit 0 (`X86_64_PTE_PRESENT`) | Bit 0 (`X86_64_PDE_PRESENT`) | Identical |
| Writable (`R/W`) | Bit 1 (`X86_64_PTE_WRITABLE`) | Bit 1 (`X86_64_PDE_WRITABLE`) | Identical |
| User (`U/S`) | Bit 2 (`X86_64_PTE_USER`) | Bit 2 (`X86_64_PDE_USER`) | Identical |
| Write-Through (`PWT`) | Bit 3 (`X86_64_PTE_WRITE_THROUGH`) | Bit 3 (`X86_64_PDE_WRITE_THROUGH`) | Identical |
| Cache Disable (`PCD`) | Bit 4 (`X86_64_PTE_CACHING_DISABLED`) | Bit 4 (`X86_64_PDE_CACHING_DISABLED`) | Identical |
| Accessed (`A`) | Bit 5 (`X86_64_PTE_ACCESSED`) | Bit 5 (`X86_64_PDE_ACCESSED`) | Identical bit position; applies to entire 2 MB page |
| Dirty (`D`) | Bit 6 (`X86_64_PTE_DIRTY`) | Bit 6 (`X86_64_PDE_DIRTY`) | Identical bit position; applies to entire 2 MB page |
| **Bit 7 (`PAT` vs `PS`)** | **Bit 7 is `X86_64_PTE_PAT` (`1ULL << 7`)** | **Bit 7 is `X86_64_PDE_LARGE_PAGE` (`PS`, `1ULL << 7`)** | **Critical difference**: in a 4 KB PTE, bit 7 is `PAT`. In a 2 MB PDE, bit 7 selects a 2 MB page (`PS = 1`). |
| Global (`G`) | Bit 8 (`X86_64_PTE_GLOBAL`) | Bit 8 (`X86_64_PDE_GLOBAL`) | Identical (`fIsKernelMap` sets `G`) |
| **Bit 12 (`PAT` on PDE)** | *(Part of physical page frame number in 4 KB PTE)* | **Bit 12 is `X86_64_PDE_PAT` (`1ULL << 12`)** | **Critical difference**: in a 2 MB PDE, `PAT` moves to bit 12 (`X86_64_PDE_PAT`). Bits `20:13` must be `0` (reserved MBZ; setting any bit in `20:13` triggers `#PF` with `RSVD=1`). |
| Physical Address Mask | `X86_64_PTE_ADDRESS_MASK` (`0x000ffffffffff000ULL`, bits `51:12`) | **Must be `0x000fffffffe00000ULL` (bits `51:21`)** | **Bug in current `paging.h:58`**: `X86_64_PDE_ADDRESS_MASK` is currently defined as `0x000ffffffffff000L` (which is for a PDE pointing to a 4 KB page table!). For a 2 MB large page PDE, bit 12 is `X86_64_PDE_PAT` and bits `20:13` are reserved, so masking a 2 MB PDE with `X86_64_PDE_ADDRESS_MASK` when `X86_64_PDE_PAT` is set (e.g. `B_WRITE_COMBINING_MEMORY`) would corrupt the physical address by `+0x1000` (`4 KB`) in `Query()` (`X86VMTranslationMap64Bit.cpp:499`) and `DebugGetReverseMappingInfo()` (`:760`)! A separate `X86_64_PDE_LARGE_ADDRESS_MASK = 0x000fffffffe00000ULL` is required. |
| No-Execute (`NX`) | Bit 63 (`X86_64_PTE_NOT_EXECUTABLE`) | Bit 63 (`X86_64_PDE_NOT_EXECUTABLE`) | Identical (only set when `IA32_FEATURE_AMD_EXT_NX` is enabled) |

### Helper Needed in `X86PagingMethod64Bit.h` (`:166-184`)

`MemoryTypeToPageTableEntryFlags(uint32 memoryType)` (`X86PagingMethod64Bit.h:167`) returns `X86_64_PTE_PAT` (bit 7) for `B_WRITE_COMBINING_MEMORY`. Calling it for a 2 MB PDE would set bit 7 (`PS`, already 1) and leave bit 12 (`X86_64_PDE_PAT`) as `0`, silently turning Write-Combining (`WC`) into Write-Back (`WB`)!
We must add `MemoryTypeToPageDirectoryEntryFlags(uint32 memoryType)`:
- `B_UNCACHED_MEMORY` → `X86_64_PDE_CACHING_DISABLED | X86_64_PDE_WRITE_THROUGH`
- `B_WRITE_COMBINING_MEMORY` → `x86_use_pat() ? X86_64_PDE_PAT : 0` (bit 12!)
- `B_WRITE_THROUGH_MEMORY` → `X86_64_PDE_WRITE_THROUGH`
- `B_WRITE_PROTECTED_MEMORY` / `B_WRITE_BACK_MEMORY` / default → `0`

And define `X86_64_PDE_LARGE_ADDRESS_MASK = 0x000fffffffe00000ULL` and `X86_64_PDE_MEMORY_TYPE_MASK = (X86_64_PDE_WRITE_THROUGH | X86_64_PDE_CACHING_DISABLED | X86_64_PDE_PAT)` (note also that `X86_64_PTE_MEMORY_TYPE_MASK` in `paging.h:75-76` currently omits `X86_64_PTE_PAT` — `Protect()` at `X86VMTranslationMap64Bit.cpp:593` must clear `X86_64_PTE_PAT` / `X86_64_PDE_PAT` when changing memory types from `WC` to `WB` or `UC`!).

---

## 3. Splitting a 2 MB PDE on Partial `Unmap` or `Protect`

If a caller partially unmaps (`Unmap`, `UnmapPage`, `UnmapPages`) or partially changes protection/memory type (`Protect`, `ProtectPage`) on a sub-range `[start, end)` that does not cover the entire 2 MB PDE `[pdeBase, pdeBase + 0x200000)`, the 2 MB PDE must be **split into a 4 KB page table of 512 PTEs**.

### 3.1 Where the 4 KB Page-Table Page Comes From (Reservation & Pre-Allocation)

`Protect()`, `Unmap()`, `UnmapPage()`, and `UnmapPages()` do **not** take a `vm_page_reservation*` parameter (`VMTranslationMap.h:41-63`), and run while holding `fLock` (a recursive lock) and `ThreadCPUPinner` (pinned to CPU, cannot sleep waiting for memory):

1. **Pre-reserve the page-table page at `Map` time (zero late-allocation failure)**:
   - `MaxPagesNeededToMap(start, end)` (`X86VMTranslationMap64Bit.cpp:209-233`) **already budgets** `requiredPageTables = end / k64BitPageTableRange + 1 - start / k64BitPageTableRange` (`:228-229`) as if every 2 MB slot used a 4 KB page table!
   - Callers (`vm_map_physical_memory()` at `vm.cpp:2024-2029`, `vm_create_anonymous_area()` at `vm.cpp:1683`, `:1720-1731`) already reserve `MaxPagesNeededToMap()` pages in `reservation` before calling `Map()`.
   - When `Map2MB()` installs a 2 MB PDE at `virtualPageDir[index]`, we **allocate one wired, zeroed 4 KB standby page-table page** from `reservation` (`vm_page_allocate_page(reservation, PAGE_STATE_WIRED | VM_PAGE_ALLOC_CLEAR)`) and stash its physical page number in a per-translation-map side table (or in `X86PagingStructures64Bit`, keyed by `(virtualPageDir, index)` or a small intrusive list of pre-allocated split tables).
   - **Why this is best**:
     - `MaxPagesNeededToMap()` stays unchanged.
     - Splitting a 2 MB PDE in `Protect()` / `Unmap()` / `UnmapPages()` **never allocates memory and can never fail** with `B_NO_MEMORY` while holding `fLock` or `ThreadCPUPinner`.
     - When a 2 MB PDE is unmapped as a whole (the common case in `UnmapArea()` / `delete_area()`), its unused standby 4 KB page-table page is freed right alongside the PDE clear (`vm_page_free_etc()`).
     - When a 2 MB PDE is split into 512 PTEs, the standby 4 KB page becomes the live page table (`fMapCount` delta: `+1` table page if not already counted, and `+512 - 1` or consistent `fMapCount` accounting; see Section 4).

### 3.2 Gapless PDE Split Sequence (No Break-Before-Make Window)

Per Intel SDM Vol. 3A Section 4.10.4.2 (*Recommended Invalidation*):
> *"Software should not write to a paging-structure entry in a way that would change, for any linear address, both the page size and either the page frame, access rights, or other attributes."*

Clearing the PDE (`P = 0`) before installing the 4 KB page table (break-before-make) would open a window in which another CPU or an interrupt handler accessing live kernel memory (e.g. GPU rings or VRAM) faults on a kernel address and panics. Because the split itself keeps the **exact same physical frames, access rights, and memory type** for all 512 4 KB pages, `_SplitLargePage(uint64* pde, addr_t pdeVirtualBase, const char* reason)` switches the PDE without an unmapped gap under `fLock` + `ThreadCPUPinner`:

1. **Populate the standby 4 KB page table in memory first** (before touching `*pde`):
   - Read `uint64 oldPDE = *pde;` (assert `(oldPDE & (X86_64_PDE_PRESENT | X86_64_PDE_LARGE_PAGE)) == (X86_64_PDE_PRESENT | X86_64_PDE_LARGE_PAGE)`).
   - Extract `phys_addr_t basePA = oldPDE & X86_64_PDE_LARGE_ADDRESS_MASK;`.
   - Translate PDE flags to PTE flags for all 512 entries `i = 0..511`:
     - Copy `P`, `R/W`, `U/S`, `PWT`, `PCD`, `A`, `D`, `G`, `NX` (`bits 0..6, 8, 63`).
     - Translate **bit 12 (`X86_64_PDE_PAT`) → bit 7 (`X86_64_PTE_PAT`)**!
     - Set PTE address to `basePA + i * B_PAGE_SIZE`.
   - Write all 512 entries into `virtualPageTable` (mapped via `fPageMapper->GetPageTableAt(physicalPageTable)`), followed by a store fence (`memory_write_barrier()`).
2. **Atomically switch the PDE to the 4 KB page table**:
   - Write the new directory entry pointing to `physicalPageTable` in one atomic store (`SetTableEntry`), changing only the page size while keeping every linear address's frame and attributes identical:
     ```cpp
     X86PagingMethod64Bit::SetTableEntry(pde,
         (physicalPageTable & X86_64_PDE_ADDRESS_MASK)
         | X86_64_PDE_PRESENT | X86_64_PDE_WRITABLE | X86_64_PDE_USER);
     ```
3. **Synchronous cross-CPU TLB shootdown**:
   - Invalidate the 2 MB TLB entry on all active CPUs synchronously (`SMP_MSG_GLOBAL_INVALIDATE_PAGES` for `fIsKernelMap`, or `InvalidateUserTLB` for user maps) before any individual 4 KB PTE is modified.
4. **Apply the caller's change**:
   - Apply the caller's partial `Unmap` or `Protect` operation to the 4 KB PTEs in `virtualPageTable`, followed by its own invalidation.

---

## 4. Every `VMTranslationMap` Method and How It Handles a 2 MB PDE

File references are in `haiku/src/system/kernel/arch/x86/paging/64bit/X86VMTranslationMap64Bit.cpp` (`:39-809`), `X86PagingMethod64Bit.cpp` (`:225-441`), and `haiku/src/system/kernel/vm/VMTranslationMap.cpp` (`:39-383`):

| Method | File : Line | Current Behavior on 2 MB PDE | Required Behavior with 2 MB PDE Support |
|---|---|---|---|
| `MaxPagesNeededToMap(start, end)` | `X86VMTranslationMap64Bit.cpp:209-233` | Budgets 1 page table per 2 MB (`requiredPageTables`). | **Unchanged.** The 1 page per 2 MB budget reserves the standby 4 KB page table used if the 2 MB PDE ever needs to be split later. |
| `Map(va, pa, attributes, memoryType, reservation)` (and new `MapRange` / 2 MB check) | `X86VMTranslationMap64Bit.cpp:237-268`, `X86PagingMethod64Bit.cpp:394-433` | Calls `PageTableEntryForAddress()` which calls `PageTableForAddress()` and hits `ASSERT(!(*pde & X86_64_PDE_LARGE_PAGE))` (`X86PagingMethod64Bit.cpp:387`). | Add `Map2MB()` (or extend `Map()` / `PutPageDirectoryEntryInTable()`): when called for a 2 MB-aligned `(va, pa)` chunk, look up `pde = PageDirectoryEntryForAddress(..., true, reservation, ...)`, assert `(*pde & X86_64_PDE_PRESENT) == 0`, reserve/attach the standby 4 KB split table page from `reservation`, write the 2 MB PDE (`X86_64_PDE_LARGE_PAGE`, `X86_64_PDE_LARGE_ADDRESS_MASK`, `MemoryTypeToPageDirectoryEntryFlags(memoryType)`), and increment `fMapCount += 512` (so `MappedSize()` stays in 4 KB pages). If `Map()` is called for a 4 KB page inside an existing 2 MB PDE, split first (should not happen in normal flow, assert/split). |
| `Unmap(start, end)` | `X86VMTranslationMap64Bit.cpp:272-317` | Calls `PageTableForAddress()` per 2 MB range (`:284`), hitting `ASSERT(!(*pde & X86_64_PDE_LARGE_PAGE))`. | Inspect `pde = PageDirectoryEntryForAddress(..., start, ..., false, NULL, ...)`: if `(*pde & (X86_64_PDE_PRESENT | X86_64_PDE_LARGE_PAGE)) == (X86_64_PDE_PRESENT | X86_64_PDE_LARGE_PAGE)`: if `(start & 0x1fffff) == 0 && end - start >= 0x200000`, atomically clear `*pde` (`ClearTableEntry(pde)`), free its standby split page, `fMapCount -= 512`, if `(oldPDE & X86_64_PDE_ACCESSED) != 0` call `InvalidatePage(start)`, and advance `start += 0x200000`. Otherwise (partial unmap of `< 2 MB`), call `_SplitLargePage(pde, ROUNDDOWN(start, 0x200000))` and fall through to the existing 4 KB PTE loop. |
| `UnmapPage(area, address, updatePageQueue, deletingAddressSpace, _flags)` | `X86VMTranslationMap64Bit.cpp:321-390` | Calls `PageTableEntryForAddress()` (`:332`), hitting `ASSERT(!(*pde & X86_64_PDE_LARGE_PAGE))`. | Check `pde = PageDirectoryEntryForAddress()` under `fLock`: if `*pde` is a 2 MB large page, call `_SplitLargePage(pde, ROUNDDOWN(address, 0x200000))` first, then unmap the single 4 KB PTE at `VADDR_TO_PTE(address)`. |
| `UnmapPages(area, base, size, updatePageQueue, deletingAddressSpace)` | `X86VMTranslationMap64Bit.cpp:394-465` | Calls `PageTableForAddress()` (`:412`), hitting `ASSERT(!(*pde & X86_64_PDE_LARGE_PAGE))`. | Look up `pde = PageDirectoryEntryForAddress()`. When `*pde` is a 2 MB large page: if `start` is 2 MB-aligned and `end - start + 1 >= 0x200000`, clear `*pde` atomically, free the standby split page, `fMapCount -= 512`, `InvalidatePage(start)` if `X86_64_PDE_ACCESSED`, and if `area->cache_type != CACHE_TYPE_DEVICE`, loop `p = 0..511` calling `PageUnmapped(area, basePage + p, accessed, dirty, updatePageQueue, &queue)` so all 512 `vm_page` wired counts are decremented! If partial (`< 2 MB`), call `_SplitLargePage()` and run the existing 4 KB PTE loop. |
| `UnmapArea(area, deletingAddressSpace, ignoreTopCachePageFlags)` | `VMTranslationMap.cpp:89-164` | For `CACHE_TYPE_DEVICE` or `area->wiring != B_NO_LOCK` (`:92-95`), delegates directly to `UnmapPages(area, area->Base(), area->Size(), true, deletingAddressSpace)`. | Because all eligible areas (`map_physical_memory`, `B_CONTIGUOUS`, `B_FULL_LOCK`) have `cache_type == CACHE_TYPE_DEVICE` or `wiring != B_NO_LOCK`, `UnmapArea()` automatically takes `:92-94` → `X86VMTranslationMap64Bit::UnmapPages()`, which unmaps whole 2 MB PDEs in one atomic clear per 2 MB. |
| `Query(virtualAddress, _physicalAddress, _flags)` | `X86VMTranslationMap64Bit.cpp:469-519` | **Already checks `if ((*pde & X86_64_PDE_LARGE_PAGE) != 0)` at `:489-493`**, computing `*_physicalAddress = (entry & X86_64_PDE_ADDRESS_MASK) + (virtualAddress % 0x200000)`. | **Fix mask bug at `:491`**: replace `X86_64_PDE_ADDRESS_MASK` (`0x000ffffffffff000`) with `X86_64_PDE_LARGE_ADDRESS_MASK` (`0x000fffffffe00000`) so `X86_64_PDE_PAT` (bit 12) is not added into `*_physicalAddress`! Flag translation at `:501-513` (`USER`, `WRITABLE`, `NOT_EXECUTABLE`, `DIRTY`, `ACCESSED`, `PRESENT`) uses identical bit positions between PDE and PTE. |
| `QueryInterrupt(virtualAddress, _physicalAddress, _flags)` | `X86VMTranslationMap64Bit.cpp:523-529` | Calls `Query(virtualAddress, _physicalAddress, _flags)`. | Automatically fixed by `Query()`. |
| `Protect(start, end, attributes, memoryType)` (`ProtectArea`, `ProtectPage`) | `X86VMTranslationMap64Bit.cpp:533-605`, `VMTranslationMap.h:153-165` | Calls `PageTableForAddress()` (`:559`), hitting `ASSERT(!(*pde & X86_64_PDE_LARGE_PAGE))`. Also uses `X86_64_PTE_MEMORY_TYPE_MASK` which lacks `PAT`. | Look up `pde = PageDirectoryEntryForAddress()`. If `*pde` is a 2 MB large page: if `start` is 2 MB-aligned and `end - start + 1 >= 0x200000`, atomically update `*pde` via `TestAndSetTableEntry(pde, (entry & ~(X86_64_PTE_PROTECTION_MASK | X86_64_PDE_MEMORY_TYPE_MASK)) | newProtectionFlags | MemoryTypeToPageDirectoryEntryFlags(memoryType), entry)`, `InvalidatePage(start)` if `X86_64_PDE_ACCESSED`, and advance `start += 0x200000`. If partial (`< 2 MB`, e.g. `ProtectPage` or half-area `Protect`), call `_SplitLargePage(pde, ROUNDDOWN(start, 0x200000))` and fall through to the 4 KB PTE loop (with `X86_64_PTE_PAT` included in `X86_64_PTE_MEMORY_TYPE_MASK`). |
| `ClearFlags(address, flags)` | `X86VMTranslationMap64Bit.cpp:609-632` | Calls `PageTableEntryForAddress()` (`:616`). Only called by `vm_clear_map_flags()` (`vm.cpp:3404`) via `page->mappings` (unwired pages). | Check `pde = PageDirectoryEntryForAddress()`: if `*pde` is a 2 MB large page, clear `flagsToClear` (`X86_64_PDE_DIRTY`, `X86_64_PDE_ACCESSED`) atomically on `*pde` and call `InvalidatePage(ROUNDDOWN(address, 0x200000))`. |
| `ClearAccessedAndModified(area, address, unmapIfUnaccessed, _modified)` | `X86VMTranslationMap64Bit.cpp:636-711` | Calls `PageTableEntryForAddress()` (`:647`). Only called by page daemon on `page->mappings` (`WiredCount() == 0`). | Never reached for `CACHE_TYPE_DEVICE`, `B_CONTIGUOUS`, or `B_FULL_LOCK` areas (they have no `page->mappings`). Guard with `pde = PageDirectoryEntryForAddress()`: if `*pde & X86_64_PDE_LARGE_PAGE`, assert `!unmapIfUnaccessed`, clear `A`/`D` on `*pde`, `InvalidatePage`, and return `(oldEntry & X86_64_PDE_ACCESSED) != 0`. |
| `~X86VMTranslationMap64Bit()` | `X86VMTranslationMap64Bit.cpp:47-150` | Walks user half (`p < 256`) of `PMLTop` and frees `virtualPageDir[k] & X86_64_PDE_ADDRESS_MASK` as a page table (`:101-114`) without checking `X86_64_PDE_LARGE_PAGE`! | **Must guard `:102`**: if `(virtualPageDir[k] & X86_64_PDE_LARGE_PAGE) != 0`, do **not** call `vm_lookup_page` / `vm_page_free_etc` on `virtualPageDir[k] & X86_64_PDE_ADDRESS_MASK` (that is the mapped physical base, not a page table!); only free any remaining standby split page-table pages. |
| `DebugGetReverseMappingInfo(pa, callback)` | `X86VMTranslationMap64Bit.cpp:715-802` | Already checks `X86_64_PDE_LARGE_PAGE` at `:759-774`. | Replace `X86_64_PDE_ADDRESS_MASK` at `:760` with `X86_64_PDE_LARGE_ADDRESS_MASK`. |

---

## 5. Kernel Setting Switch and Boot Self-Test

### 5.1 Kernel Setting (Off by Default)

- Read boolean driver setting `amd64_large_pages` from the `"kernel"` driver settings file (`get_driver_boolean_parameter(handle, "amd64_large_pages", false, false)`) during `X86PagingMethod64Bit::InitPostArea()` (`X86PagingMethod64Bit.cpp:90`).
- **Default**: `false` (off). When `false`, `X86VMTranslationMap64Bit` uses 4 KB PTEs exclusively, preserving 100% identical behavior to current builds.
- Log the state at boot: `dprintf("R5 VM: amd64_large_pages %s\n", sUseLargePages ? "enabled" : "disabled");`.

### 5.2 Self-Test (4 MB Contiguous Area: Map, Protect Half, Unmap)

Controlled by boolean kernel setting `amd64_large_pages_selftest` (or run automatically once at the end of `vm_init_post_sem()` when `amd64_large_pages` is `true`):

1. **Allocate & Map 4 MB**:
   - Create a 4 MB (`2 × 2 MB`), 2 MB-aligned `B_CONTIGUOUS` kernel area with `B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA`.
   - Verify via `PageDirectoryEntryForAddress()` that both 2 MB slots (`base` and `base + 0x200000`) have `X86_64_PDE_PRESENT | X86_64_PDE_LARGE_PAGE | X86_64_PDE_WRITABLE` set.
   - Write a known 64-bit pattern at the start, middle, and end of each 4 KB page across all 4 MB; verify via `Query()` that `base + offset` resolves to the exact physical address `paBase + offset` (including when `B_WRITE_COMBINING_MEMORY` / `X86_64_PDE_PAT` bit 12 is tested).
2. **Protect Half (`[base + 0x100000, base + 0x300000)`, crossing both 2 MB PDEs, or `[base, base + 0x100000)` = first 1 MB of the first 2 MB PDE)**:
   - Call `Protect(base, base + 0x100000 - 1, B_KERNEL_READ_AREA, B_WRITE_BACK_MEMORY)` (read-only on the first 1 MB = 256 × 4 KB pages).
   - Verify that the first PDE (`base`) was **split** (`(*pde0 & X86_64_PDE_LARGE_PAGE) == 0`, pointing to the standby 4 KB page table), that PTEs `0..255` have `X86_64_PTE_WRITABLE == 0`, that PTEs `256..511` still have `X86_64_PTE_WRITABLE != 0`, and that the second PDE (`base + 0x200000`) **remained a 2 MB large page** (`(*pde1 & X86_64_PDE_LARGE_PAGE) != 0`).
   - Verify all written data across all 4 MB still reads back intact, and that writing to `base + 0x100000` (second half of split PDE) and `base + 0x200000` (unsplit 2 MB PDE) succeeds.
3. **Unmap & Delete**:
   - Call `delete_area()` on the 4 MB area.
   - Verify both PDEs are cleared (`*pde0 == 0` after unmapping/freeing, `*pde1 == 0`), all 1024 physical 4 KB pages are freed with `WiredCount() == 0`, and `MappedSize()` returned to its pre-test value.
4. **Serial Log Output**:
   - On success: `dprintf("R5 VM: amd64_large_pages self-test PASS (4 MB map, 1 MB split-protect, unmap)\n");`
   - On any mismatch: `dprintf("R5 VM: amd64_large_pages self-test FAIL at step %s\n", step);`

---

## 6. Risks and What a Stick on the Ryzen 5 5625U Must Show

### Risks

1. **Bit 12 (`X86_64_PDE_PAT`) vs. `X86_64_PDE_ADDRESS_MASK` (`paging.h:58`)**:
   - If `X86_64_PDE_ADDRESS_MASK` (`0x000ffffffffff000`) is not replaced by `X86_64_PDE_LARGE_ADDRESS_MASK` (`0x000fffffffe00000`) when reading a 2 MB PDE, any Write-Combining (`WC`) 2 MB mapping (such as a framebuffer or GPU BAR) will have bit 12 (`0x1000`) set, causing `Query()` (`X86VMTranslationMap64Bit.cpp:499`) and `vm_set_area_page_memory_type()` (`vm.cpp:4649`) to read a physical address off by `+4 KB`!
2. **PAT vs. MTRR Cache-Type Aliasing on Split or Partial Mapping**:
   - On AMD Zen 3 (5625U) and Intel, mapping the same 2 MB physical range with different memory types in the TLB or without `clflushopt` when switching `WB` → `WC` (`arch_vm_flush_memory_type_range()`) can cause cache corruption. Notice also that `vm_set_area_page_memory_type()` (`vm.cpp:4645-4652`) currently steps `virtualAddress` by `B_PAGE_SIZE` and calls `Query()` + `arch_vm_flush_memory_type_range(physicalAddress, B_PAGE_SIZE)`. With `Query()` fixed to use `X86_64_PDE_LARGE_ADDRESS_MASK`, `Query(virtualAddress)` returns `basePA + (virtualAddress % 0x200000)`, so that loop continues to flush each 4 KB sub-page accurately.
3. **Break-Before-Make TLB Shootdown on SMP (12 Threads on 5625U)**:
   - Updating a live 2 MB PDE to a 4 KB page table pointer without first clearing the PDE and shooting down all 12 hardware threads' TLBs risks simultaneous 2 MB and 4 KB TLB hits (`#MC`).
4. **Physical Fragmentation for `B_CONTIGUOUS` ≥ 2 MB After Uptime**:
   - Early in boot (`radeon_hd`, VESA framebuffer, early DMA rings), 2 MB contiguous runs are plentiful; after long desktop uptime, `vm_page_allocate_page_run()` for 2 MB may fail if memory is fragmented. `B_FULL_LOCK` must always fall back cleanly to 4 KB pages if a 2 MB run is unavailable.
5. **User Address Space Teardown (`~X86VMTranslationMap64Bit`)**:
   - If a user team clones or maps a device/framebuffer area (e.g. `app_server` or a Vulkan/RADV client mapping a GPU aperture) and gets a 2 MB PDE in user space, `~X86VMTranslationMap64Bit()` (`X86VMTranslationMap64Bit.cpp:101-114`) **must** skip `vm_lookup_page` / `vm_page_free_etc` on `X86_64_PDE_LARGE_PAGE` entries, or process exit will `panic("page table ... on invalid page")`.

### What a USB Stick Boot on the Ryzen 5 5625U Must Show

Before turning `amd64_large_pages` on by default on the 5625U (`0x15e7`), a stick boot with `amd64_large_pages true` in `generated/boot-5625u-<n>/r5-syslog.txt` must show:
1. `R5 VM: amd64_large_pages enabled` and `R5 VM: amd64_large_pages self-test PASS (4 MB map, 1 MB split-protect, unmap)` with zero page faults or panics.
2. Clean `radeon_hd` init (`0x15e7`), including the 256 MB VRAM BAR / GART / doorbell / ring mappings (`map_physical_memory` and `B_CONTIGUOUS`), with the GFX9 ring writing scratch `0xDEADBEEF`, the K2 fence landing, and the desktop rendering on the GPU.
3. Clean `app_server` and Vulkan (`VulkanTeapot` / RADV) execution and clean process exit (verifying `UnmapArea()` and `~X86VMTranslationMap64Bit()` on 2 MB device/VRAM mappings).
4. Clean restart and power-off (`GFX9 stop at shutdown on GC 9.3`) without the caps lock blinking.
