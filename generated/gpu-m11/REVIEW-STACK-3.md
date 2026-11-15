# M11 stack review 3 (15 Nov 2026)

Third round: four read-only reviews of the whole M11 stack (compositor, app_server and apps, kernel, stick path), after review 2 (`REVIEW-STACK-2.md`). Every item was read again in the code before a fix. The owner asked to fix everything.

| # | Area | Finding | Status |
|---|------|---------|--------|
| 1 | Desktop | The composed dirty region of a changed title or border was cut to the old buffer, before `ClippingChanged` gave it its new size; a grown tab was not drawn (review 2 item 9 was incomplete). | Fixed: cut after `ClippingChanged`. |
| 2 | Settings | The effects box read the effects again, not the strength; a strength set elsewhere made a choice in Settings do nothing (review 2 item 16 was incomplete). | Fixed: the strength is read again too. |
| 3 | Desktop | Two R5 lines of the direct screen lock were outside an SPDX snippet. | Fixed. |
| 4 | Stick | `check wait` accepted an earlier boot's self-test file, which ends in `selftest done` until this boot's self-test renames it (about 5 s after Deskbar). | Fixed: only a file written after this boot's first dated syslog line, or any finished file after 60 s of waiting. |
| 5 | Stick | A stopped boot 1 `check` left `gpu_compositor_zero_copy false` in the settings, and the saved copy of a restarted boot kept it. | Fixed: the saved copy drops that line, a trap restores it. |
| 6 | Stick | The boot 1 marker was never tied to the boot; a check after a restart used the frozen boot's marker. | Fixed: the marker is used once (moved into the boot's folder); the card says `check wait` and the scenario again after a restart. |
| 7 | Stick | The marker matched the last copy of a repeated line. | Fixed: the marker stores which copy. |
| 8 | Stick | `read-stick-logs.py` did not copy `syslog.1`, the rotated syslog. | Fixed. |
| 9 | Stick | `pin-mesa-packages.sh` named -8 in its error while checking -9. | Fixed. |
| 10 | Kernel | A `fork()` child keeps a copy of the program's VRAM mapping (`B_KERNEL_AREA` does not stop `vm_copy_area`), so it can read and write VRAM after it is freed and given to another buffer. Before this branch too. | Fixed: the last reference deletes every other area on the mapping's cache first (`drop_copies`, in `radeon_hd`); if one stays, the page is kept out of the allocator. |
| 11 | Kernel | With `B_KERNEL_AREA`, RADV's `munmap` at buffer destroy fails and logs one line per buffer. | Fixed: the flag is gone; item 10 covers clones, transfers and fork copies. |
| 12 | Mesa | RADV turns on `VK_EXT_map_memory_placed` but ignores the address on R5. Before this branch too. | Not changed: no R5 program uses it, and the fix is a Mesa rebuild. Follow-up. |
| 13 | Compositor | A pause that began during the restart after a reset bound the windows to interfaces without a buffer. | Fixed: bound to the screen while paused, as `AddWindow` does. |
| 14 | Compositor | `_ShareShape` read `App()` twice; it can be cleared between the reads. | Fixed: read once. |
| 15 | Compositor | An evicted window kept its shadow mask and glass cache, which count in the window memory. | Fixed: the compose thread drops them for a window without a buffer, at the start of a frame. |
| 16 | Compositor | During a pause, an ended share gave its images back but kept its imports until the resume. | Fixed: `_ShareRetire` in the paused branch. |

Not counted: `HWInterface::AddListener`/`RemoveListener` change a list without a lock while `SetMode` walks it. That is unchanged Haiku code; the compositor makes rebinds more frequent.

Not run: items 10, 13, 14, 15 and 16 are compiled and read. QEMU has no radeon_hd, Lavapipe shares nothing, and no fork of a GPU program was tried. Items 4 to 7 were tested as shell pieces in WSL, not on Haiku.
