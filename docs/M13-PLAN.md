<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# M13 - speed: the 60 fps feel (RADV and Lavapipe)

M12 made Skia work as the drawing engine, opt-in (`gpu_draw true` in the radeon_hd settings); it closed on correctness, not speed (owner, 16 Nov 2026). M13 makes R5 fast, and then turns Skia on by default: window drag, scroll and resize at 60 fps on the Ryzen 5 5625U (RADV), and smooth on Lavapipe (about 8 ms or less per full frame). This plan is the owner's performance advice (16 Nov 2026) with the R5 corrections and the M13 items that were already agreed. Rules: `AGENTS.md` and `docs/GPU-ARCHITECTURE.md`.

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
- **Hardware blend fast path for icons and `B_OP_ALPHA`** (option E, after M12's option B). For `B_OP_ALPHA`, `B_RGB32` magic and `B_CMAP8` on opaque windows (outside the classic compare), a simple premultiply shader plus hardware `SkBlendMode::kSrcOver`, so small icons need no `makeImageSnapshot` copy.

### Priority 4 - the rest of M13 (agreed before this plan)

- Frame pacing and vsync: one frame per refresh, no doubled or skipped frames while dragging.
- Input latency: from a mouse move to the picture, measured.
- app_server locking: where drawing waits for a lock.
- Start times: boot to Sign in, and Sign in to desktop.

## Owner decisions of 16 Nov 2026 (after M12 stick 2)

- **Skia is not on by default yet.** `gpu_draw` stays opt-in through M13. Turning Skia on by default, for RADV and Lavapipe together, with Painter only in Safe mode, is the **last step of M13**, after the speed work (step 5 below). On Lavapipe Skia is still slower than Painter. Source: `docs/APP-SERVER-GPU-DRAWING.md`, "Changed again (owner, 16 Nov 2026, after M12 stick 2)".
- **Step 2 before step 1 (owner, 16 Nov 2026):** the architecture (step 2) goes first, together with the disk pipeline cache (`fPersistentPipelineStorage`) from step 1. The other step 1 items come after it; `_Wake` batching only after step 2. The step 0 numbers then show the architecture's gain on its own.
- **Step 6b moves here (M13 or later):** a buffer of its own for `BDirectWindow` on Lavapipe, design on paper in `docs/APP-SERVER-GPU-DRAWING.md` (Q10). Preferably built together with a RADV variant, so the direct-window pause goes on both. Until then the pause stays. It is not on the critical path of the speed work; it comes after step 2, when the compositor reads images.
- **The first M13 stick also proves #4324** (8 MiB VRAM reserve for page-table pages, 1024 table pages per file). Boot 1 (Skia on RADV) must stay on for the whole scenario: no `page table pool full` and no `gpu draw off` in the syslog. On M12 stick 2 boot 1 turned the drawing off after about 3.5 minutes with `insert failed`, because no page-table page could be made with the VRAM full.

## Recommended order

| Step | What | Test |
|---|---|---|
| 0 | **Measure.** One script reads a few numbers from the syslog: frame time while dragging, scrolling and resizing, input latency, warm-up time; on Lavapipe in QEMU, on RADV on the next stick. Targets: RADV at most 16.7 ms per frame, Lavapipe about 8 ms per full frame. Built: `distro/graphics/boot/m13-measure` (on the machine, or `m13-measure numbers <dir>` for stick logs), and `scripts/qemu-m13-measure.py` for the QEMU run on an own image copy. | One QEMU run |
| 2 | **Architecture, done first** (owner, 16 Nov 2026). `VkImage` + `VkSampler` in the compositor, and the `VkSemaphore` sync. Removes the CPU read-back, the double buffer and `SyncToCpu`. Painter windows keep the buffer path; `compose compare` must pass. With it, the disk pipeline cache (`fPersistentPipelineStorage`) from step 1, in a commit of its own. Built: `CHANGELOG.md`, "M13 step 2". | One short QEMU boot, the step 0 script before and after |
| 1 | **Low-hanging fruit, after step 2.** `FindTexture` with a checksum, `DrawString` once, `_FillSolid` with `setColor`, the gradient cache, `snprintf` in `r5_translate.c` behind the debug flag; then `_Wake` batching. Small and separate, each measurable with step 0. (`fPersistentPipelineStorage` went with step 2.) | Jam, plus the step 0 script once |
| 3 | **Fine-tuning and hardware.** The `vkCmdCopyImage` fast path, `gpu_compositor_scanout` on RADV, Lavapipe MSAA, threads and vector width (`LP_NUM_THREADS`, `LP_NATIVE_VECTOR_WIDTH`), the userland EOP fence check, write-combined GTT. The same stick proves step 2 on RADV and #4324 (boot 1 stays on), and gives the first RADV step 0 numbers. | One stick for all of them |
| 4 | **The rest of M13.** Frame pacing and vsync, input latency, locking, start times. | The step 0 script |
| 5 | **Skia on by default** (owner, 16 Nov 2026): RADV and Lavapipe together, Painter only in Safe mode. Only after steps 1 to 4. Step 6b (`BDirectWindow`, Lavapipe and preferably RADV) here or later (owner, 16 Nov 2026). | One stick, by "From opt-in to default" in `docs/GPU-ARCHITECTURE.md` |

The order (owner, 16 Nov 2026): step 2 with the pipeline cache, then step 1, then step 3, then step 4, and Skia on by default last.

**Open after step 2 (measured, nothing built):**
- **Warm-up on Lavapipe:** `gpu draw warm up` 10.9 s on the first boot, 7.7 s on the second boot of the same image (QEMU, one pair). Skia's pipeline cache holds only the Vulkan header on Lavapipe (312 bytes), so the gain on the second boot comes from Mesa's own shader cache, which app_server (uid 0) writes into `/boot/home/config/cache/mesa_shader_cache`, inside the administrator's home. Where that cache belongs (a system cache directory for uid 0) is an owner decision; the Mesa recipe is not changed without asking. On RADV the pipeline cache holds real pipelines; measured on the step 3 stick.
- **Compute or fragment shaders on Lavapipe:** llvmpipe is tuned for compositors that draw large textured quads in a fragment shader (its tiled rasteriser, binning and SIMD sampling over whole tiles), while the R5 compositor composes with compute dispatches (`compose.comp`, `effects.comp`), one invocation per pixel, with 16x16 groups. A fragment-shader path (one textured quad per window, hardware blend for the opaque copy) looks worth one test on Lavapipe in step 3, because the compose median during a drag is 130-210 ms on Lavapipe; it is not built. On RADV compute is fine, so a second path would be Lavapipe-only, and `compose compare` must hold for it too.
- **Glass on Lavapipe:** the step 0 script does not split the compose time per pass (blur, shadow, glass, copy), so whether glass dominates a drag on Lavapipe is not known yet. A split per pass is the first thing to add before choosing "light" strength or a frozen glass during a drag.

Why the architecture now comes first (owner, 16 Nov 2026): it removes the biggest cost, and the small fixes are then measured on top of it, each on its own. (Before that decision the small fixes came first, so the step 0 numbers would show the architecture's own gain without their noise; measuring step 2 first gives the same separation.)

## Testing: fast and short

The owner wants to develop faster (16 Nov 2026). So:
- Build with `scripts/build-locked.sh` (about 3 minutes).
- A QEMU boot only for app_server, GPU and kernel work, short: it starts, no panic, the one thing that changed, and the step 0 numbers. No screenshot series.
- Hardware: one stick for all of step 3, by the stick rules in `AGENTS.md`.
- More checks only where something can break that the boot would not show.
