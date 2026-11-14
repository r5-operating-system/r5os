# Review of the whole M11 stack (steps 0-5 on main, step 6 on claude/m11-stick): every open finding, one list

Five read-only reviews (compositor core, zero-copy sharing, kernel, app_server wiring, decorator and effects UI), 14 Nov 2026, on `origin/main` 9729b942c2. **Checked** = read in the code by the step 6 session; **plausible** = the reviewer's trace, not re-read. Fix each in its own commit, or write why it needs no change. **All 19 were fixed on `claude/m11-stick` on 14 Nov (the owner asked for all, including 13 and 14); a fresh review of the fix commits found one gap in item 2, fixed in 9d0f98396f.** Kernel items need a boot on the 5625U; QEMU has no radeon_hd.

Already fixed on `claude/m11-stick` (step 6 tools): boot 4 of `check` failed on the hang's own `gpu lost watchdog` dump; `read-stick-logs.py` did not copy `~/m11`; the plant script read back only `radeon_hd`; `check-user` line continuations.

## Could show up on the M11 stick

1. **Fixed in c190e73cd6. Kernel: the map uses the requested flags, not the used ones (checked).** `r5_gpu_core.cpp` `gpu_bo_create` stores `backing->flags = in.flags` before `gpu_bo_fill` clears `R5_GPU_BO_WC`; only the reply gets the used flags. `BO_MAP` (`backing->flags & R5_GPU_BO_WC`) then maps the user clone write-combining over a cached, snooped page (a memory-type alias), and `BO_IMPORT` reports WC to app_server (`in.bo_flags = backing->flags`). Fix: `backing->flags = in.flags` after a successful `gpu_bo_fill`. Changes the user mapping of every RADV buffer that asked for WC: measure FPS before and after.
2. **Fixed in edfe86fb20, 9d0f98396f. WSI: an ended share never gives its images back (checked: no release in `RemoveWindow`).** `RemoveWindow`, `_ShareFrame` (broken mark) and `ShareImages` (an older swapchain of the same view) set `ended`/`broken` without `release_sem_etc` for the current, pending and retiring images; `_ShareRetireDone` clears them silently. A program that acquires with `UINT64_MAX` (vkcube) can block for good when its window closes. Fix: release them the way `_ReturnSharedImages` does, or let the WSI acquire return `VK_ERROR_OUT_OF_DATE_KHR` when the window is gone.
3. **Fixed in dd7fdc1e3d. Eviction frees nothing (plausible).** `_AllocateOrMakeRoom` drops only the interface's reference; the old `fLayers` (and ghosts) still hold the buffer, so `fWindowBytes` does not drop, every off-screen window loses its buffer, and `_TurnOff("window memory")` follows. Only near the budget. Fix: unset the buffer in the old layer list too, or retry once after the list is released.
4. **Fixed in e41fa685d9. A window made during a switch (two reviewers).** `AddWindow` checks only `fPaused`, not `fActive`; `_TurnOff` and `_ApplyMode` change those outside `fEntryLock`. A window made at the moment of a switch to the CPU mode stays on a buffer-less interface (draws nowhere); one made during a resume draws straight to the screen. Fix: change `fActive`/`fPaused` under `fEntryLock`; in `AddWindow` bind to `fScreen` when `!fActive || fPaused`.

## Security and lifetime

5. **Fixed in edfe86fb20. Share on a window without an entry (checked).** `ShareImages` does not check that the window has an entry, and `RemoveWindow` returns before its share loop when there is none (offscreen `BBitmap` window, failed `CreateWindowEngine`, out of memory in `AddWindow`). The share keeps a freed `Window*`; `_ShareShape` reads it. Fix: run the share loop before the `entry == NULL` return, or refuse in `ShareImages`.
6. **Fixed in 339dedd1c2. Dead view tokens (plausible; latent Haiku bug, now reachable).** `~ServerWindow` sets `fServerApp = NULL` before `fWindow.Unset()`, so `View::DetachedFromWindow` does not remove the tokens. `AS_R5_SET_VIEW_GLASS` and `AS_R5_SHARE_IMAGES` look up a closed window's view token and read freed memory. Fix: remove the tokens before `fServerApp = NULL`, under `LockAllWindows()`.
7. **Fixed in 5c58dc6dfd. Kernel: export records hold memory outside every limit (plausible).** A buffer exported and then freed keeps its backing 5 s without a slot: not counted in `held` (`r5_vram_fits`), no app_server reserve in `gpu_backing_take`. Many files can use up the 1024 backings, and app_server's `BO_CREATE` fails. Fix: count pending exports in `held` and in the per-file limit; apply `R5_SHARE_RESERVE` in `gpu_backing_take`.
8. **Fixed in c190e73cd6. Kernel: a refused VRAM create frees offset 0 (checked).** `backing->domain = VRAM` is set before `gpu_bo_fill`; a refusal before `vram_alloc` leaves `vramOffset = 0`, and `gpu_backing_drop` frees (or defers) offset 0. Fix: set the domain after the allocation, or skip the free without one.
9. **Fixed in b049d18969. Kernel: any program can arm the hang test (plausible; only with `gpu_hang_test` set).** `R5_GPU_IH_COUNTS` arms it before any caller check. Fix: app_server or uid 0 only.

## Direct screen (step 5)

10. **Fixed in 2d7ab5153a. Unlock order (checked).** `Desktop::UnlockDirectScreen` unlocks first, then sets `fDirectScreenTeam = -1` and resumes. A waiting team B gets the lock in between; A then clears B's ownership and resumes the compositor while B writes the screen. Fix: clear the owner and resume before `Unlock()`; pause again in `LockDirectScreen` once the lock is taken.
11. **Fixed in 2d7ab5153a. Timed-out holder (plausible).** After the `_SetCurrentWorkspaceConfiguration` timeout sets the owner to -1, the holder still owns the `BLocker`; its unlock (and the new unlock on exit) do nothing, and the next failed `LockDirectScreen` resumes while the holder can still write. Fix: unlock when the calling thread owns the lock; do not resume while it is held.
12. **Fixed in e41fa685d9. `AddWindow` failure (plausible, out of memory only).** `fEntries.AddItem` failing deletes the `iface` the engine still uses. Fix: return a status; `MakeWindow` rebinds to `fScreen` or fails.

## Decided earlier, needs the owner

13. **Fixed in 58a1b4f0e8. Kernel: the GPU-off path frees parked buffers anyway (plausible).** After a second hang or a failed reset (`!sCpStopped`), `sLost` makes `gpu_release_parked` treat everything as passed one second later, and detach/free skip parking. The CP may still fetch. This is the hang policy (AGENTS.md: only on request).
14. **Fixed in 4730a5671a, 9d0f98396f. Fence timeout frees buffers while a job may run (plausible).** `_TurnOff` and the restart free window and glass buffers without `vkDeviceWaitIdle`; the doc says "after the device is idle or lost". On RADV the kernel's parking protects the pages; on Lavapipe a job past 15 s would use freed memory. A wait can block on a stuck device, so choose: wait with a limit, or document.

## Small

15. **Fixed in 345822757a. Decorator: the frame highlight uses the left border's colours for all four lines (plausible).** `R5Decorator::_DrawFrame`; the resize highlight on the right, top or bottom shows no colour, and lines can stay blue. Fix: four strokes, each with its own `RegionHighlight`, as `DefaultDecorator::_DrawFrame`.
16. **Fixed in 345822757a. Top line under the tab is the tab (plausible, maybe intended).** `RegionAt` returns `REGION_TAB` for the 1-px top line under the tab; a full-width tab can be resized from the top only at the corners. Note it in decision 2, or return `REGION_TOP_BORDER` there.
17. **Fixed in 0d68d8f6c3. Effects applied, error returned (plausible).** `SetWindowEffects()`/`SetWindowEffectStrength()` apply, then return `Save()`'s status; the box resets its checkbox on an error while the screen has the change. Return `B_OK` once applied, log a failed save.
18. **Fixed in f5a1734d7c. `r5-effects-test glass`/`full` exit 0 on a failed call (plausible).** The `_Check()` results are dropped.
19. **Fixed in 322bf7e62d. A pending image with mark 0 composes every period (plausible, crafted client only).** `SharePresent` should refuse a mark of 0.

Checked and fine by the reviewers: SMAP (every new ioctl copies through `gpu_copy_in`/`gpu_copy_out`), handle arrays, refcounts, import rights (uid 0 plus path), untrusted sizes in the share messages, lock order `fEntryLock` → `fShareLock`/`fDamageLock`/`fAnimLock`, the decorator ABI check, range checks of the effects messages, SPDX headers, `elf_tls.cpp`, `_Report`.

## Found while testing the fixes

20. **Fixed in 0f70eca6d8. A shorter title left the old end of the tab on screen (QEMU).** `Desktop::RebuildAndRedrawAfterWindowChange` did not call `Compositor::ClippingChanged()`.
