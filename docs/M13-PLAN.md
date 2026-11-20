<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# M13 - speed: the 60 fps feel (RADV and Lavapipe)

M12 made Skia work as the drawing engine; it closed on correctness, not speed (owner, 16 Nov 2026). M13 makes R5 fast, and then turns Skia on by default: window drag, scroll and resize at 60 fps on the Ryzen 5 5625U (RADV), and smooth on Lavapipe (about 8 ms or less per full frame). This plan is the owner's performance advice (16 Nov 2026) with the R5 corrections and the M13 items that were already agreed. Rules: `AGENTS.md` and `docs/GPU-ARCHITECTURE.md`.

## Status (19 Nov 2026)

| Step | State |
|---|---|
| 0 Measure | Built 16 Nov 2026 (`m13-measure`). Used on the M13 sticks. |
| 1 Small fixes | Built 17 Nov 2026. The follow-up of 19 Nov 2026 replaces the full-bitmap hash, the second text layout and the per-row copy where a rectangle is one span. |
| 2 Architecture | Built 16 Nov 2026. The compositor reads Skia's images. Proven on the 5625U on stick 3 (17 Nov 2026): RADV stayed on. |
| 3 Fine-tuning | See the rows under the table. Sticks 3, 4 and 5 are 17 Nov 2026. |
| 4 The rest | Built 17 Nov 2026. Resize on RADV at the refresh rate is stick 4. The Lavapipe outline resize and the warm-up that finishes are the same day. |
| 5 Skia on by default | **Default as of 19 Nov 2026.** `gpu_draw` defaults to true. `gpu_draw false` turns it off. Painter draws only in Safe mode. Not yet booted on the 5625U with this default. |

Step 3, item by item:

| Item | State |
|---|---|
| Copy path for Skia windows (`vkCmdCopyImageToBuffer`, #4332) and Painter windows (`vkCmdCopyBuffer`) | Built. Used whenever Skia or a Painter window is composed. |
| `gpu_lavapipe_threads` | Built. Default 2. A line sets another count. |
| `gpu_lavapipe_msaa 1\|4` | Built, opt-in. The default stays 4. `1` measured worse. |
| `gpu_lavapipe_vector 128\|256` | Built. The setting wins. With no setting and no environment variable, AVX2 sets 256 (19 Nov 2026); a plain x86-64-v2 CPU sets nothing. |
| `gpu_compositor_scanout` | Built. On by default with `gpu_app_server` on a release-supported GPU (`R5_GPU_CAP_RELEASE`, after stick 65). `false` turns it off. Refused on Lavapipe. |
| `gfx9_wc_gtt` | Built. The default on a release-supported GPU (20 Nov 2026). `false` turns it off. The GPU PTE stays snooped. The AMD64 direct map of those RAM pages is write-combined for the same lifetime (`docs/KERNEL.md`). Not yet booted on the 5625U. |
| Userland EOP fence page | **Not built.** |
| 2 MB pages for the GPU areas | Built 19 Nov 2026, on when `amd64_large_pages` is on (the default). `false` is 4 KB. Not yet shown on the 5625U. |
| Fragment shader for the blur | **Not built.** |

The CPU read-back in the next section (stop the GPU, `asyncRescaleAndReadPixels`, `memcpy` into `WindowBuffer`) is **history**. Step 2 (16 Nov 2026) removed it for a window Skia draws. It is kept here so the reason for step 2 stays readable.

## Submit wait (19 Nov 2026)

`gpu_submit` waits in the ioctl for a wait syncobj. That wait is 15 seconds, the same as the RADV compose fence (`kFenceLimit` in `Compositor.cpp`). It was 5 seconds, which is stricter than the fence: a compose still inside its own 15 seconds failed `vkQueueSubmit` and turned GPU drawing off. The wait stays in the ioctl. Moving it onto the GPU queue would change the packets that are submitted, and that is not a change to make without a boot on the 5625U. Damage stays after the fence. The ioctl still returns a timeout when the wait exceeds 15 seconds, `vkQueueSubmit` fails, and drawing turns off, so publishing the damage before the fence would show a frame whose compose had already failed. Lavapipe does not use this ioctl. Its compose wait stays unlimited.

## Part 1: the core of M13 in plain language

**What happens now (M12).**
1. Skia paints each window on a canvas in fast GPU memory (`VkImage`).
2. When Skia is done, everything stops (`SyncToCpu::kYes`, `distro/app_server/skia/SkiaGpu.cpp:545`) and the GPU sends the result back to normal memory (`asyncRescaleAndReadPixels`, `SkiaGpu.cpp:539`).
3. The CPU copies it pixel for pixel with `memcpy` into a second canvas (`WindowBuffer`, in `WindowTarget::Deliver()`).
4. The compositor takes that second canvas and sends it to the GPU again, to put all windows with shadow and frosted glass on the screen. The compositor reads raw buffers (`VkBuffer`, `distro/app_server/compositor/compose.comp:13`), not images.

**Why M12 did this.** The M11 compositor could only read `WindowBuffer`. Copying Skia's result back kept the whole M11 compositor unchanged, so M12 could be tested on its own against Painter.

**What M13 does.** It cuts out the CPU step and the second canvas:
- The compositor learns to read `VkImage`: `compose.comp` and `effects.comp` sample real Vulkan images (`VkImage` + `VkSampler`) instead of raw byte buffers.
- Skia and the compositor share the same `VkImage`. `asyncRescaleAndReadPixels()`, the `Deliver()` copy and the separate `WindowBuffer` for Skia windows go away. Read-back stays only for `compose compare` and screenshots.
- A `VkSemaphore` replaces `SyncToCpu`: Skia gives a GPU signal ("done"), and the compositor waits for it on the GPU. The CPU no longer waits. This is simple because Skia already uses the compositor's `VkDevice` (`SkiaGpu.cpp:307`).
- Painter windows (Safe mode, CPU mode, a window Skia cannot draw) keep the `WindowBuffer` path. So the compositor has two inputs: buffer and image.

**What it gives on RADV and on Lavapipe.**

| Aspect | RADV (5625U) | Lavapipe (QEMU, software fallback) |
|---|---|---|
| Double copy and `memcpy` gone | Large: no VRAM to RAM read-back, no snap/deliver peaks (up to 7-19 ms in M12) | Clear: two needless CPU pixel copies disappear |
| No CPU stall (`SyncToCpu`) | Large: the GPU keeps working through the semaphore | Moderate: the work stays on the same CPU, but the sync overhead goes |
| Frosted glass and animations (`effects.comp`) | Large: hardware texture unit and bilinear filtering instead of 32 manual `unpack()` calls | Large: Lavapipe's SIMD sampler instead of 32 bit shifts (up to 6.7 ms in boot 3) |
| Memory per window | About half the VRAM per window (no second canvas). Not the cause of boot 1's `insert failed` on M12 stick 2: that was the page-table pool, fixed in #4324 | Less RAM bandwidth and fewer allocations |

These gains are expected, not measured. Step 0 measures them.

## Part 2: the optimisations, by impact

### Priority 1 - the M13 architecture (GPU to GPU)

- **Compose `VkImage` directly (SkiaTarget to Compositor).** Remove `asyncRescaleAndReadPixels()` and `WindowTarget::Deliver()` from the normal drawing path. `compose.comp` and `effects.comp` sample `VkImage` with a `VkSampler` (`VK_FILTER_LINEAR`). `compose compare` must still pass.
- **Semaphore between SkiaGpu and the compositor.** Replace `SubmitInfo::fSync = SyncToCpu::kYes` in `SkiaGpu::RunWork()` with a `VkSemaphore` passed to `Compositor::_Compose()`. One shared `VkQueue` lock (commit `4bf5c86a22` added one lock for the GPU queue; reuse it) if `_DrawThread` keeps running apart.
- **Plain rectangles without a compute shader.** `vkCmdCopyImage` (transfer copy) for windows without shadow, glass or animation, instead of one pixel per compute thread in `compose.comp`.
- **Direct scanout by default on RADV (`gpu_compositor_scanout`).** Removes the last present `memcpy` (202 µs in boot 1). Hardware rule: this becomes the default only when it meets "From opt-in to default" in `docs/GPU-ARCHITECTURE.md` (stick, step 3).

### Priority 2 - app_server and Skia hot paths (help RADV and Lavapipe at once)

- **`FindTexture()` without the full `memcmp`** (`SkiaPainter.cpp:380`). Every cache hit now compares all bitmap bytes (36 MB for a 4000x2250 wallpaper). *R5 correction:* a dirty or generation flag set by app_server is not enough. A program writes its `BBitmap` pixels directly into memory it shares with app_server, so app_server does not see that change, and the screen would keep an old picture. Use a cheap checksum (sampled or a fast hash), and the flag only for bitmaps app_server owns itself (wallpaper, server icons).
- **`DrawString` once instead of twice** (`SkiaDrawingEngine.cpp`, `SkiaText.cpp`). `DrawString()` runs `DrawStringDry()` (`LayoutGlyphs`) and then `SkiaPainter::DrawString()` (`LayoutGlyphs` again), with new `std::vector` allocations per call and `gFontManager->Lock()` per fallback glyph. Merge into one pass with reused buffers.
- **`_FillSolid()` without a runtime shader** (`SkiaPainter.cpp`). For an opaque colour (alpha 255) use `SkPaint::setColor(...)` instead of building an `SkRuntimeShaderBuilder` (`color_shader`) for every solid fill and line.
- **Batch `_Wake()`** (`SkiaDrawingEngine.cpp`). `Draw::~Draw()` wakes the draw thread after every single draw call (107-275 runs per 10 s in boot 1). Wake only at `CopyToFront()` or the end of an update, or with a short coalescing. *R5 note:* input latency must not go up; step 0 measures it.
- **Gradient cache** (`SkiaPainter.cpp`). `FillRectVerticalGradient()` and `_GradientShader()` allocate and upload a new `SkBitmap` on every draw. Cache the 1D 256-entry gradient textures per stops and height.
- **Skia pipeline cache on disk** (`SkiaGpu.cpp`). `ContextOptions::fPersistentPipelineStorage` brings the boot warm-up (10 s on Lavapipe, 1.6 s on RADV) down to milliseconds after the first boot. *R5 note:* app_server runs as uid 0, so the cache goes in a system cache directory, not a user's `~/config/cache`. It is keyed on the Mesa and driver version, so a Mesa update throws it away.

### Priority 3 - Lavapipe and RADV specific

- **Lavapipe MSAA and threads** (`SkiaGpu.cpp:343`, environment). `SampleCount::k4` with `fMinimumPathSizeForMSAA = 0` costs almost nothing on RADV, but on Lavapipe it is 4x the software rasterisation for every path and curve. Consider `k1` or a small-path atlas on Lavapipe. *R5 note:* only within the compare-set tolerances, because Lavapipe must look like RADV. Tune `LP_NUM_THREADS` so Lavapipe's worker pool and app_server's window and compose threads do not crowd each other out.
- **libdrm-r5: `snprintf` behind the debug flag** (`distro/graphics/libdrm-r5/r5_translate.c:250, 253, 364`). Production ioctls format strings into the global `sIoctlDetail`, which is also not thread-safe. Move the formatting behind `if (r5_debug_on())`. This is a bug fix as well as a speed-up.
- **Fence check in userland for timeout 0.** Map a read-only EOP sequence page into userland, so `FENCE_WAIT(timeout=0)` and `_ShareReady()` can see that work is done without a kernel ioctl and without `sGpuLock`. *R5 rule:* this is a new R5 GPU interface call. It is additive (no shipped call changes), needs a security check (read-only, per process), and needs a boot on the 5625U. Stick work, step 3.
- **Write-combined GTT (`gfx9_wc_gtt`) for write-only uploads.** Once M13 drops the CPU read-backs, write-only upload and IB buffers are faster in write-combined memory than in snooped cached memory. *R5 rule:* a hardware setting, so stick work, step 3.
- **2 MB pages for the GPU areas** (owner, 19 Nov 2026). The owner turned `amd64_large_pages` on by default the same day; what follows is what remains for the 5625U. `amd64_large_pages` (#4435, `docs/AMD64-LARGE-PAGES.md`) passed its self-test on the 5625U (sticks 53 and 56) and ran whole sessions without a fault, but no real area used it: those boots have no `R5 VM: 2MB map` line, while QEMU mapped the VESA framebuffer with eight 2 MB pages. Most likely the large radeon_hd and framebuffer areas (VRAM BAR, framebuffer, GART, rings) get a kernel or user virtual address that is not 2 MB aligned, so every map falls back to 4 KB. Ask for 2 MB aligned addresses for those areas (physical and virtual), then one stick must show `2MB map` lines for them, RADV/Vulkan running and exiting cleanly, restart and power off, as `docs/AMD64-LARGE-PAGES.md` ("What a USB Stick Boot on the Ryzen 5 5625U Must Show") asks. `amd64_large_pages false` stays the off switch. Fewer TLB misses on large GPU buffers helps the frame time of this milestone. *R5 rule:* kernel and hardware, so stick work, step 3.
- **Hardware blend fast path for icons and `B_OP_ALPHA`** (option E, after M12's option B). For `B_OP_ALPHA`, `B_RGB32` magic and `B_CMAP8` on opaque windows (outside the classic compare), a simple premultiply shader plus hardware `SkBlendMode::kSrcOver`, so small icons need no `makeImageSnapshot` copy.

### Priority 4 - the rest of M13 (agreed before this plan)

- Frame pacing and vsync: one frame per refresh, no doubled or skipped frames while dragging.
- Input latency: from a mouse move to the picture, measured.
- app_server locking: where drawing waits for a lock.
- Start times: boot to Sign in, and Sign in to desktop.

## Owner decisions of 16 Nov 2026 (after M12 stick 2)

- **Skia is on by default (19 Nov 2026).** `gpu_draw` defaults to true for RADV and Lavapipe together. `gpu_draw false` turns it off. Painter draws only in Safe mode. This is step 5, the owner's decision of 16 Nov 2026. On Lavapipe Skia is still slower than Painter. Not yet booted on the 5625U with the default.
- **Step 2 before step 1 (owner, 16 Nov 2026):** the architecture (step 2) goes first, together with the disk pipeline cache (`fPersistentPipelineStorage`) from step 1. The other step 1 items come after it; `_Wake` batching only after step 2. The step 0 numbers then show the architecture's gain on its own.
- **Step 6b moves here (M13 or later):** a buffer of its own for `BDirectWindow` on Lavapipe, design on paper in `docs/APP-SERVER-GPU-DRAWING.md` (Q10). Preferably built together with a RADV variant, so the direct-window pause goes on both. Until then the pause stays. It is not on the critical path of the speed work; it comes after step 2, when the compositor reads images.
- **The first M13 stick also proves #4324** (8 MiB VRAM reserve for page-table pages, 1024 table pages per file). Boot 1 (Skia on RADV) must stay on for the whole scenario: no `page table pool full` and no `gpu draw off` in the syslog. On M12 stick 2 boot 1 turned the drawing off after about 3.5 minutes with `insert failed`, because no page-table page could be made with the VRAM full.

## Recommended order

| Step | What | Test |
|---|---|---|
| 0 | **Measure.** One script reads a few numbers from the syslog: frame time while dragging, scrolling and resizing, input latency, warm-up time; on Lavapipe in QEMU, on RADV on the next stick. Targets: RADV at most 16.7 ms per frame, Lavapipe about 8 ms per full frame. Built: `distro/graphics/boot/m13-measure` (on the machine, or `m13-measure numbers <dir>` for stick logs), and `scripts/qemu-m13-measure.py` for the QEMU run on an own image copy. | One QEMU run |
| 2 | **Architecture, done first** (owner, 16 Nov 2026). `VkImage` + `VkSampler` in the compositor, and the `VkSemaphore` sync. Removes the CPU read-back, the double buffer and `SyncToCpu`. Painter windows keep the buffer path; `compose compare` must pass. With it, the disk pipeline cache (`fPersistentPipelineStorage`) from step 1, in a commit of its own. Built: `CHANGELOG.md`, "M13 step 2". | One short QEMU boot, the step 0 script before and after |
| 1 | **Low-hanging fruit, after step 2.** `FindTexture` with a checksum, `DrawString` once, `_FillSolid` with `setColor`, the gradient cache, `snprintf` in `r5_translate.c` behind the debug flag; then `_Wake` batching. Small and separate, each measurable with step 0. (`fPersistentPipelineStorage` went with step 2.) | Jam, plus the step 0 script once |
| 3 | **Fine-tuning and hardware.** The `vkCmdCopyImage` fast path, `gpu_compositor_scanout` on RADV, Lavapipe MSAA, threads and vector width (`LP_NUM_THREADS`, `LP_NATIVE_VECTOR_WIDTH`), the userland EOP fence check, write-combined GTT, 2 MB pages for the GPU areas (aligned so they are used). The same stick proves step 2 on RADV and #4324 (boot 1 stays on), and gives the first RADV step 0 numbers. | One stick for all of them |
| 4 | **The rest of M13.** Frame pacing and vsync, input latency, locking, start times. | The step 0 script |
| 5 | **Skia on by default** (owner, 16 Nov 2026): RADV and Lavapipe together, Painter only in Safe mode. Only after steps 1 to 4. Step 6b (`BDirectWindow`, Lavapipe and preferably RADV) here or later (owner, 16 Nov 2026). | One stick, by "From opt-in to default" in `docs/GPU-ARCHITECTURE.md` |

The order (owner, 16 Nov 2026): step 2 with the pipeline cache, then step 1, then step 3, then step 4, and Skia on by default last.

**Open after step 2 (measured, nothing built):**
- **Warm-up on Lavapipe:** `gpu draw warm up` about 11 s on the first boot; on the second boot of the same image 4.9 s (and 10.3 s with another jam beside it). Skia's pipeline cache holds only the Vulkan header on Lavapipe (312 bytes), so the gain is Mesa's own shader cache, which app_server now keeps in `/boot/system/cache/app_server/mesa` (owner, 16 Nov 2026; the Mesa recipe is unchanged). On RADV the pipeline cache holds real pipelines; measured on the step 3 stick.
- **Compute or fragment shaders on Lavapipe:** llvmpipe is tuned for compositors that draw large textured quads in a fragment shader, while the R5 compositor composes with compute dispatches, one invocation per pixel. `compose passes` (gpu_debug) showed the plain copy of a Skia window as the largest pass (150-163 ms per frame while dragging); it is a transfer copy now (about 50 ms). Next are the blur (18-25 ms), shadow and glass (about 20 ms together): a fragment-shader path for those looks worth one test on Lavapipe in step 3. Not built; it would be Lavapipe-only, and `compose compare` must hold.
- **Glass on Lavapipe:** measured with `compose passes`: glass is 8-12 ms of a drag frame, not the largest part; "light" strength or a frozen glass would save little on its own.

**Step 3, code status (17 Nov 2026, branch `claude/m13-step3`):**
- Built (opt-in or exact): the copy path for Skia windows (`vkCmdCopyImageToBuffer`, in #4332) and for Painter windows (`vkCmdCopyBuffer`); `gpu_lavapipe_msaa 1|4`, `gpu_lavapipe_threads <n>`, `gpu_lavapipe_vector 128|256` (Lavapipe only, logged when set); the M13 stick mode (`boot-next m13`, `check`, `check-pre-stick.py --paper`).
- `gfx9_wc_gtt` (20 Nov 2026): a GTT buffer that asks for `AMDGPU_GEM_CREATE_CPU_GTT_USWC` gets a write-combined CPU mapping, and on AMD64 the kernel physical map of those pages follows, the way Linux `set_pages_array_wc()` does. The GPU page-table entry stays snooped: the PTE flags are part of the GFX9 contract in `AGENTS.md` and are not changed. On a release-supported GPU the switch is on unless the line says `false`. The userland EOP fence below is still not built.
- Stick boots for this change, separate from `boot-next m13f`: boot A is the release GPU with no `gfx9_wc_gtt` line (`gpu_debug true`), and `/system/bin/r5_wc_gtt` prints `PASS`. The syslog has `R5 VM: wc direct map ok` from boot. Boot B is the same with `gfx9_wc_gtt false`: the command still passes (it tests the kernel path), and a USWC buffer stays cached. The 5625U has not run either.
- **Not built: the userland EOP fence check.** Design: a new call at the end, `R5_GPU_FENCE_PAGE = R5_GPU_OP_BASE + 33` (additive; no shipped call changes), that clones the kernel's EOP sequence page into the calling team read-only (`vm_clone_area` with `B_READ_AREA` only, never `B_WRITE_AREA`, `kernel = true` as `BO_MAP` does) and returns the address and the offset of the ring's sequence. The R5 DRM library and `Compositor::_ShareReady()` compare the 64-bit sequence there with the one they wait for and only call `FENCE_WAIT` when it is not reached. Security check before it is built: the page holds only sequence numbers of the shared ring (what every client may already learn by `FENCE_WAIT` with timeout 0, so no new information), one clone per team, deleted with the team and on a GPU reset (the reset path must also unmap or the page must stay valid), and the page is a kernel page that no other kernel data shares. It needs a boot on the 5625U and is a kernel change that QEMU cannot run (no radeon_hd), so it waits for its own step with a stick, not for this one.

Why the architecture now comes first (owner, 16 Nov 2026): it removes the biggest cost, and the small fixes are then measured on top of it, each on its own. (Before that decision the small fixes came first, so the step 0 numbers would show the architecture's own gain without their noise; measuring step 2 first gives the same separation.)

## Testing: fast and short

The owner wants to develop faster (16 Nov 2026). So:
- Build with `scripts/build-locked.sh` (about 3 minutes).
- A QEMU boot only for app_server, GPU and kernel work, short: it starts, no panic, the one thing that changed, and the step 0 numbers. No screenshot series.
- Hardware: one stick for all of step 3, by the stick rules in `AGENTS.md`.
- More checks only where something can break that the boot would not show.
