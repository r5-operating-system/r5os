# M11 stack review 4 (15 Nov 2026)

Fourth round: five read-only reviews of the whole M11 stack (compositor core, zero-copy sharing, kernel, app_server and the effects UI, stick path), on `claude/m11-stick` bb4658aea2, after review 3 (`REVIEW-STACK-3.md`). Every item was read again in the code before a fix. The owner asked to fix everything.

The reviewers checked the review 3 fixes and found them correct, except item 2 (Settings re-read the strength on a click, not for Defaults and Revert: item 12 below) and item 16 (a pause that begins just before the frame: item 5 below).

| # | Area | Finding | Status |
|---|------|---------|--------|
| 1 | Compositor | A second `SetMode` (any mode change; with RGB32 every call) mapped a new GPU back buffer at the address of the old one: the kernel refused it, the old one leaked, GPU copy went off, and the generation stayed the same, so the scanout kept the old imports. | Fixed: the old back buffer is unmapped and freed first, and every call is a new generation. |
| 2 | Compositor | `fFrameGhosts` kept the last frame's closing windows during a pause. | Fixed: dropped in both paused branches. |
| 3 | Compositor | `~ServerWindow` let the app go before `~Window` ended the window's shares; `_ShareShape` could then read a freed token space. | Fixed: the shares end under the all-windows lock, with the view tokens (`ShareWindowGone`). |
| 4 | Sharing | A shared view's place was only updated on a clipping rebuild of the desktop; a hidden view (a tab switch) or a moved one kept showing the last image where it was. | Fixed: after view hide, show, move, resize, scroll and delete (`ShareViewsChanged`). |
| 5 | Sharing | A pause that began after the first paused check left the ended shares until the resume (review 3 item 16 incomplete). | Fixed: retired in the branch after the compose slot too. |
| 6 | Sharing | A newer swapchain of a view did not wake the compose thread; and a wake without damage never reached `_ShareRetire` (found while fixing this). | Fixed: a wake, and a retire when the frame has no damage. |
| 7 | Sharing | An overtaken image of a share that did not end (broken, restart, failed mark read) was dropped without its semaphore. | Fixed: released unless the program ended the share itself. |
| 8 | Kernel | Without `B_KERNEL_AREA` a program could `resize_area` its GTT mapping; the shrink frees pages the GPU page tables still point at. | Fixed: `vm_resize_area` refuses a team a cache that a kernel area also maps (R5 snippet in `vm.cpp`). |
| 9 | Kernel | `drop_copies` stopped after 8 rounds of 16 copies (VRAM then kept for good) and logged one line per copy. | Fixed: up to 4096 rounds, one line per call. |
| 10 | Kernel | `BO_MAP` returned a cached area id the program had deleted (RADV at `vkUnmapMemory`); that buffer could not be mapped again. | Fixed: a gone id is cloned again. |
| 11 | Kernel | The comment said RADV munmaps the mapping (it deletes the area); a mapping shared by area id is deleted under its user. | Fixed: comment; shares use `BO_EXPORT`. |
| 12 | Settings | Defaults and Revert in the window effects box compared with the values of the last click here. | Fixed: they read the values in effect. |
| 13 | Desktop | An R5 line in `DefaultWindowBehaviour.cpp` was outside its SPDX snippet. | Fixed. |
| 14 | Desktop | The resize margin of a stacked window returned any window of the stack. | Fixed: the stack's front window. |
| 15 | Compositor | An unlock of the direct screen did not give its count back once the GPU mode had ended. | Fixed: latent today (the GPU mode does not come back). |
| 16 | Stick | A boot 1 that froze during check's copy-path run kept `gpu_compositor_zero_copy false` (no trap runs on a reset): three false FAILs. | Fixed: `check wait` removes it and waits 12 s. |
| 17 | Stick | `check` and `check-user` read only the current syslog; a rotation during a live run gave false FAILs. | Fixed: the count carries the first line's checksum; after a rotation the rest of `syslog.1` is read. |
| 18 | Stick | A stopped check did not `sync` what its trap restored, and the card had no step for it. | Fixed: `sync` in the trap; the card says `check wait`, the scenario, `check`. |
| 19 | Stick | The header said `check wait` writes nothing. | Fixed: comment. |

| 20 | Kernel | External review: `BO_CREATE` kept the buffer (slot, backing, VRAM or GTT) when the handle could not be written back to the program. | Fixed: the buffer goes again, as in `BO_IMPORT` and `SCANOUT_BO`. |
| 21 | Kernel | External review: `drop_copies` deleted a copy of a team that had just ended (harmless, unlike `gpu_bo_release`). | Fixed: `team_is_valid` first. |
| 22 | Stick | External review: the FAIL line for zero-copy off on RADV quoted the planned `off: setting` line instead of the real one. | Fixed. |
| 23 | Stick | External review: `check wait` wrote the scenario start in boots 2 and 3 too, where nothing reads it. | Fixed: boot 1 only. |

Items 20 to 23 come from an external read-only review (`PROMPT-REVIEW-EXTERNAL.md`) of 5b0b19bb03, which found five; each was read again here.

Not changed, with the reason:

- Compositor: near the budget, the masks and glass of an evicted window still count until the next frame, so the allocation that evicted can fail and end the GPU mode early. The result is the CPU mode, the safe fallback; a fix needs a handshake with the compose thread.
- Compositor: during the 5 s fence-timeout grace the compose slot is free, so a mode change or a direct window could start while a hung job still writes. Needs a hang and a mode change in the same 5 s; a fix changes the slot rules.
- Compositor: an evicted window's Painter keeps the raw bits of the freed buffer. Safe while every draw is cut to the buffer (`_ClipToBuffer`), which is true today.
- Sharing: the present mark assumes the copy is submitted when `queue_present` runs. RADV submits in the calling thread unless a wait comes before its signal; a fix is a Mesa change.
- Kernel: the GPU lock is held while a fork copy is deleted, which waits for a wired range. Needs I/O wired into a GPU mapping of a fork child; a fix defers the VRAM free.
- Kernel: the GTT pages of a fork copy stay until that copy goes. Safe (the GPU no longer maps them), only not counted.
- Sharing (external review): when exporting an image fails, the WSI still makes its free semaphore; it is deleted with the swapchain. Harmless, and the WSI is in the Mesa library, so the fix waits for the next Mesa build.
- Desktop: a title or focus change redraws the overlap of the windows above in their own buffers. Extra work only.

Not run: items 1 to 11, 14 and 15 are compiled and read. QEMU has no radeon_hd, Lavapipe shares nothing, and no mode change on the 5625U, fork or `resize_area` of a GPU program was tried. Items 16 to 18 were tested as shell pieces, not on Haiku.
