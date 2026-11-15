# Second review of the whole M11 stack: every finding, one list

Five read-only reviewers (compositor core, zero-copy sharing, kernel, app_server wiring, decorator, effects UI and stick tools), 15 Nov 2026, on `claude/m11-stick` a82d011770, after the 20 fixes of `REVIEW-STACK.md`. Each finding was read again in the code by this session before it was fixed. **Checked** = the path was read in full; **plausible** = the timing or the reach is the reviewer's trace. The owner asked to fix everything. Kernel items need a boot on the 5625U; QEMU has no radeon_hd.

The reviewers checked the earlier fixes. They found these correct: 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15, 17, 18, 19, 20 and the step 6 tool fixes. Items 2, 13 and 16 were incomplete (items 1, 2 and 15 below).

## Lifetime and security

1. **Fixed in 446e7b0a83. Kernel: the item 13 fix missed losses after a restart (checked).** Only `gpu_hang` cleared `sCpStopped`; after a good reset and restart it stayed true. A later loss without a reset (a failed self-test, a STAGE_P ring-full or privilege error, `radeon_hd/r5_gpu.cpp` `gpu_lost(info, "p")`) then freed every parked buffer while the CP may still fetch. Now the restart clears it: only a reset that stopped the CP frees parked buffers. The hang policy (order, reset, restart, which event does what) is unchanged.
2. **Fixed in 26e70bf2b3. Kernel: a program could keep freed VRAM (checked in vm.cpp).** The user clone of `BO_MAP` had no `B_KERNEL_AREA`, so its team could `clone_area`, `transfer_area` or reprotect it. A clone kept the physical VRAM mapped after the buffer was freed and the VRAM given to app_server or another program. The clone now has `B_KERNEL_AREA`: the program reads and writes it, but cannot clone, move, delete or reprotect it. libdrm-r5 never deletes it (`r5_bo_cpu_unmap` is a no-op); the kernel deletes it on release.
3. **Fixed in d0b2dfa322. Compositor: app_server could crash on a closing Vulkan window (two reviewers, checked path, timing plausible).** `~ServerWindow` clears `App()` before `~Window` ends the shares; a `ClippingChanged` in that gap ran `_ShareShape` on a NULL `App()`. `_ShareShape` now checks it.
4. **Fixed in d0b2dfa322. Compositor: an ended share was not given back on turn-off, a restart or during a pause (gap in item 2; two reviewers, checked).** `_ReturnSharedImages` skipped every ended share, and no frame runs while paused. Now `_ReturnSharedImages` gives back an ended share that app_server ended, and the compose loop gives them back while paused. Ending a share (`RemoveWindow`, `ShareEnd`) also wakes the compose thread, so a hidden view's imports do not wait for unrelated damage.
5. **Fixed in 2e1dbfa5d3. Desktop: a rebind could use a deleted drawing engine (plausible).** `~Window` deleted its engine before `RemoveWindow` took the entry away; a pause, the CPU mode or a restart in between called `SetHWInterface` on the deleted engine. Both now run under the compositor's entry lock (`LockEntries`).
6. **Fixed in fe9f66d084. Compositor: a mode change could free the back buffer a frame uses (checked for the missing lock, timing plausible).** `AccelerantHWInterface::SetMode` now takes the compose slot first (`SetModeGuard`), so it waits for the frame. Order: window lock (write), compose slot, HWInterface; the same order a direct window pause uses.

## Could show up on the M11 stick

7. **Fixed in cbe9535f2d. Boot 1: the self-test's own windows passed the scenario checks (checked).** `r5-gpu-selftest` opens VulkanTeapot, vkcube and GLTeapot (a `BDirectWindow`) before `check wait` returns, so zero-copy, the pause and the animations passed without the tester's steps. `check wait` now saves the last syslog line; boot 1 counts these lines only after it (`scenario-syslog.txt`). Without `check wait` they FAIL.
8. **Card fixed in cbe9535f2d. Boot 4: the hang can come after the scenario (plausible).** The hang test arms at the self-test's last call, and the self-test can take up to 15 minutes. The card now says to keep dragging a window until the picture froze once, then 20 s more, or to go on after 15 minutes.
9. **Fixed in 699470f339. Desktop: the covered part of a changed tab or border was not drawn (checked).** `RebuildAndRedrawAfterWindowChange` cut the dirty region to the visible part, the CPU-mode rule. In the GPU mode it is now cut to the window's drawable region (its whole buffer).
10. **Fixed in c5df962d32. Compositor: a window resized while off screen showed undrawn parts (plausible).** A window that was not on screen in the last clipping pass, and comes back with a new size, now draws all of itself once.

## Small

11. **Fixed in 9dc404377f. Desktop: a second direct screen lock by the same team kept the pause for good (checked; reach plausible).** The lock now nests per team: only the last unlock gives back the screen and the pause, and the unlock when the program ends gives back all of them.
12. **Fixed in 53363d2120. Compositor: making room freed nothing while a closing window's fade held the buffer (gap in item 3; checked).** The fade of the window it takes from ends early, and its area is redrawn. **Not fixed:** a frame in flight still holds the buffer until its fence. Near the budget that can still end the GPU mode. Waiting for that frame here would need the compose slot under the entry lock, which the compose thread takes the other way.
13. **Fixed in 53363d2120. Compositor: `AddWindow` leaked the interface when `new Entry` failed (checked, out of memory only).**
14. **Fixed in 5ef3a09c6e. Compositor: the 5 s grace wait ran inside the compose slot (checked).** A direct window or a direct screen waited 15 s plus 5 s. The wait now runs after the slot is released and before the frame's layers go, so the buffers still wait for the GPU.
15. **Fixed in 6d7ccfeb7b. Decorator: the item 16 fix covered only a tab on top (plausible).** With `kLeftTitledWindowLook` the left line under the tab now resizes too.
16. **Fixed in 6d7ccfeb7b. Settings: the window effects box kept an old copy of the effects (checked).** After the Appearance program or `r5-effects-test` changed an effect, a click on another one put the old value back. The box now reads the effects again before it applies a click or a strength.

## Not changed

17. **A single very large window ends the GPU mode for the session (plausible).** A window buffer is the window's full size; a 30000 by 30000 window is over any budget, and the compositor switches to the CPU mode. That is the documented fallback: the desktop keeps working. A buffer clipped to the screen is a design change (decision 3 of the brief), not a review fix.
18. **Kernel: GET_INFO reads the backing table without the lock.** Not a defect: `gpu_get_info` takes `gpu_lock()` before the loop (`radeon_hd/r5_gpu.cpp`).

Checked and fine by the reviewers: SMAP in every new ioctl, export and import reference counts, size and handle checks, the `thread.cpp` and `arch_cpu.cpp` changes, lock order (`fEntryLock` before `fShareLock`/`fLayerLock`/`fDamageLock`/`fAnimLock`), the share release paths (no double release with a well-behaved WSI), the WSI and libdrm-r5 share code, AS_R5 message sizes, the effect value checks, the boot path 1 → 4 in `check` and `boot-next`, the planted settings, and the Settings rules for the Window effects box.
