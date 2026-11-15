# The GPU draws the windows (M12 design)

This is the design for M12, step 0, on paper. It follows `generated/gpu-m12/BRIEF.md` (updated 15 Nov 2026), `generated/gpu-m12/SAFE-MODE.md`, `docs/APP-SERVER-COMPOSITOR.md` (the compositor as built in M11) and `docs/GPU-ARCHITECTURE.md`. Nothing here is built. Nothing of Skia is in the repository; that is step 1, after the user approves Skia. Where this design differs from the brief or `AGENTS.md`, the point is under [Open questions for the user](#open-questions-for-the-user) and the rest goes on.

Source read, 15 Nov 2026:

- **Haiku:** the WSL tree (`/home/nickr/r5os/haiku`, the tree the M11 stick is built from, with the R5 copies applied). `src/servers/app/drawing/` (`DrawingEngine.h`, `Painter/Painter.h`, `Painter/drawing_modes/`, `Painter/bitmap_painter/`, `Painter/AGGTextRenderer.cpp`, `Painter/GlobalSubpixelSettings.h`, `AlphaMask.h`, `interface/remote/RemoteDrawingEngine.cpp`), `font/FontEngine.cpp`, `font/FontStyle.h`, `OffscreenWindow.cpp`, `DirectWindowInfo.cpp`, `src/kits/game/`, `src/system/boot/loader/` (`main.cpp`, `menu.cpp`, `load_driver_settings.cpp`), `src/system/boot/platform/{bios_ia32,efi}/menu.cpp`, `src/servers/launch/LaunchDaemon.cpp`, `data/launch/`, `headers/os/drivers/driver_settings.h`, `headers/private/system/safemode_defs.h`, `src/add-ons/kernel/file_systems/packagefs/volume/Volume.cpp`.
- **R5:** `distro/app_server/` (`Compositor.cpp`, `WindowBuffer.h`), `distro/desktop/src/servers/app/ServerWindow.cpp`, `distro/boot/`.
- **Skia:** branch `chrome/m158`, commit `707e33875fb45f2997a4d781128bec122fb777b4`, shallow clone in a read-only folder outside the repository. Not built.
- **air/OS:** not read. Nothing in this design needed it.

Paths without a prefix are relative to `src/servers/app` in the Haiku tree; Skia paths start with `skia/`.

## 1. Skia, the component report (decision 2)

`docs/LICENSING.md` "Third-party code" asks nine points. Each is answered from Skia's own files.

1. **Project and source.** Skia, the 2D graphics library of Google, Chrome and Android. Repository `https://skia.googlesource.com/skia`.
2. **Licence.** `BSD-3-Clause`. `skia/LICENSE` is the three-clause BSD text with the line "Copyright (c) 2011 Google Inc. All rights reserved." Every source file points to it: "Use of this source code is governed by a BSD-style license that can be found in the LICENSE file."
3. **Copyright holders**, as the files name them. In `skia/src/core`, 175 files say "Copyright <year> Google Inc.", 162 "Google LLC", 101 "The Android Open Source Project" (for example `skia/include/core/SkCanvas.h`, "Copyright 2006 The Android Open Source Project"), and one "The Chromium Authors". `skia/AUTHORS` lists the contributors and companies. The notice we ship is `skia/LICENSE` plus a pointer to `skia/AUTHORS`; no other holder is written down.
4. **The version.** The branch `chrome/m158`, commit `707e33875fb45f2997a4d781128bec122fb777b4` (13 Nov 2026), `SK_MILESTONE 158` in `skia/include/core/SkMilestone.h`.
   - **Why a release branch and not `main`:** `main` is milestone 160 and changes every day. A `chrome/mNNN` branch is cut every milestone; after that it only gets cherry-picked fixes. Its API does not move, so the R5 code written against it keeps building through those fixes.
   - **Why m158:** the newest branch is `chrome/m159`; m158 is one older. By Chrome's schedule the newest branch is the beta and the one below it is stable. Step 1 checks this once more against Chrome's release page before the pin is written.
   - **The pin:** Skia publishes no release tarballs. The recipe fetches the commit by its hash (git's hash covers the content) and records the hash and the date. A move to a newer milestone is the update procedure in part 10, never a drive-by bump (the lessons in the brief).
5. **What is built.** Skia's `DEPS` lists 45 external projects. M12 builds only the core, the Graphite Vulkan backend, and:

   | Piece | Where | Licence and holder (from its own file) | Why |
   |---|---|---|---|
   | skcms | `skia/modules/skcms` (in the Skia tree) | BSD-3-Clause, `skia/modules/skcms/LICENSE`: "Copyright (c) 2018 Google Inc." | The core needs it for colour types |
   | Vulkan Memory Allocator 3.4.0 | `DEPS`: `vulkanmemoryallocator@eb744ea7a2b17040121b4bbb4d6f9e8a77e3cae7` | MIT, `LICENSE.txt` at that commit: "Copyright (c) 2017-2026 Advanced Micro Devices, Inc." | `VulkanBackendContext::fMemoryAllocator` is no longer optional (`skia/RELEASE_NOTES.md`); Skia's wrapper `skia/src/gpu/vk/vulkanmemoryallocator` (Google LLC, BSD-3) uses it |
   | Vulkan headers | `skia/include/third_party/vulkan` | `vulkan_core.h`: "Copyright 2015-2026 The Khronos Group Inc.", SPDX `Apache-2.0` | Compile time only; Skia's build always uses this copy (`use_skia_vulkan_headers` in `skia/BUILD.gn`). No code of it ends up in the binary |
   | FreeType | not Skia's copy: R5's own FreeType (the build package `freetype-2.14.3`, the library app_server already links) | already in the image | `skia_use_system_freetype2 = true`: one FreeType, the same font files (decision 5) |

   **Not built:** every other `DEPS` entry: angle2, dawn, brotli, d3d12allocator, abseil-cpp, jinja2, markupsafe, delaunator, dng_sdk, the EGL and OpenGL registries, emsdk, expat, Skia's own freetype copy, harfbuzz, highway, icu, icu4x, imgui, libavif, libgav1, libgrapheme, libjpeg-turbo, libjxl, libpng, libwebp, libyuv, oboe, partition_alloc, perfetto, piex, swiftshader, vulkan-deps, spirv-cross, spirv-headers, spirv-tools, vello, vulkan-tools, vulkan-utility-libraries, unicodetools, wuffs, zlib; and the modules skottie, svg, pdf, skshaper, skparagraph, skunicode, skresources.

   **No image codecs.** app_server never decodes an image file. A program decodes with the Translation Kit and sends pixels; app_server gets a `ServerBitmap`. So no PNG, JPEG, WebP or Wuffs, and no zlib.

   **No ICU, and no HarfBuzz.** In Skia, ICU and HarfBuzz are used only by `SkUnicode`, `SkShaper` and `SkParagraph` (shaping and line breaking). app_server does not shape: `ServerFont` already turns a string into glyphs and positions (decision 5), and Skia gets glyph ids and positions through `SkCanvas::drawGlyphs()`. Nothing in that path needs ICU.

   The `gn` arguments for step 1 (all are names in `skia/gn/skia.gni`): `is_official_build=true skia_enable_graphite=true skia_enable_ganesh=false skia_use_vulkan=true skia_use_gl=false skia_use_vma=true skia_use_freetype=true skia_use_system_freetype2=true skia_use_fontconfig=false skia_use_icu=false skia_use_harfbuzz=false skia_use_expat=false skia_use_zlib=false skia_use_libpng_decode=false skia_use_libpng_encode=false skia_use_libjpeg_turbo_decode=false skia_use_libjpeg_turbo_encode=false skia_use_libwebp_decode=false skia_use_libwebp_encode=false skia_use_wuffs=false skia_use_piex=false skia_use_dng_sdk=false skia_use_perfetto=false skia_use_partition_alloc=false skia_enable_pdf=false skia_enable_skottie=false skia_enable_svg=false skia_enable_tools=false skia_enable_fontmgr_custom_empty=true`. Step 1 checks each against that file again and lists any that the build needs on top.

   **Toolchain.** Skia is C++20 (`-std=c++20` in `skia/gn/skia/BUILD.gn`). R5 builds app_server with the Haiku cross compiler `x86_64-unknown-haiku-gcc` 13.3.0, and Skia must use the same compiler and `libstdc++`, because it is linked into app_server. Skia's own builders include GCC jobs (`Build-Debian11-GCC-x86_64-*` in `skia/infra/bots/jobs.json`), but Graphite with Vulkan on GCC 13 for Haiku is not proven anywhere; step 1 finds out. `gn`, `ninja` and Python run on the build host only.
6. **Integration.** A static library, linked only into `app_server` (part 3). Nothing of Skia in a public header, in `libbe` or in a `libr5*` library (decision 3).
7. **Combination with GPL-3.0 and MIT.** BSD-3-Clause and MIT are in the "compatible, if their conditions are kept" row of `docs/LICENSING.md`. app_server itself is Haiku MIT plus R5 MIT, so the binary is MIT + BSD-3-Clause + MIT (VMA) + BSD-3-Clause (skcms), dynamically linked to FreeType as today. No GPL code is added. The Apache-2.0 Vulkan headers are compile-time only and are compatible with GPL-3.0 as well.
8. **Patents, trademarks, attribution.** The Skia tree has no `PATENTS` file and BSD-3-Clause has no patent grant; that is the same position as the rest of the BSD code in Haiku. Clause 3 forbids using the names of the holders or contributors to endorse or promote R5 without permission: "Skia" may be named as a fact (in the notices and in technical text), not in a way that suggests Google endorses R5. The website taglines are not affected. Binary redistribution must reproduce the notice, the conditions and the disclaimer "in the documentation and/or other materials": `THIRD_PARTY_NOTICES.md` and a block on the Licenses tab of About (`distro/about/licenses.txt`), with the full texts of `skia/LICENSE`, `skia/modules/skcms/LICENSE` and VMA's `LICENSE.txt` (part 10).
9. **What a binary release must ship as source.** Nothing: BSD-3-Clause and MIT have no source duty. The recipe in the repository pins the commit, so the exact source is still reproducible. A lawyer is not needed for BSD-3 and MIT in this use, in my reading; that is not legal advice.

## 2. Ganesh or Graphite: Graphite

Skia has two GPU backends: **Ganesh** (the older one, `GrDirectContext`) and **Graphite** (the newer one, `skgpu::graphite::Context`). Both have a native Vulkan backend in m158. The choice is **Graphite**. The reason is how app_server draws.

**What both need from Vulkan.** Both are made from the same `skgpu::VulkanBackendContext` (`skia/include/gpu/vk/VulkanBackendContext.h`): the instance, physical device, device, one queue and its family, the API version, the extensions and features that were turned on, a memory allocator and `vkGetInstanceProcAddr`. That is exactly what the compositor already has. Skia needs Vulkan 1.1 ("Skia requires Vulkan 1.1 as the minimum version", same header); the compositor's instance asks for 1.1 (`app.apiVersion = VK_API_VERSION_1_1` in `distro/app_server/Compositor.cpp`). Neither backend requires an extension or a feature: `skia/src/gpu/graphite/vk/VulkanCaps.cpp` reads every feature as optional (dual-source blending, advanced blend modes, extended dynamic state, host image copy, and others) and uses what is on. app_server turns on what Skia prefers with the public `skgpu::VulkanPreferredFeatures` (`skia/include/gpu/vk/VulkanPreferredFeatures.h`) when it makes the device, so RADV and Lavapipe each give what they have. Dual-source blending matters most: Skia uses it for subpixel (LCD) text. RADV and Lavapipe in Mesa 25.3.6 both offer far more than Vulkan 1.1; step 1 records what each reports, and the device lines go into the step 1 report.

**The same device and queue (decision 4).** app_server passes the compositor's `VkDevice` and `VkQueue` to Skia. One device means the window images Skia draws into are images of that device, so the compose can read them without an export or a copy between devices.

**Threads: why Graphite.** app_server draws from many threads at the same time: each `ServerWindow` has its own thread, and drawing runs under `Desktop::LockSingleWindow()`, which is the read side of `fWindowLock`, so windows draw in parallel. Vulkan requires that only one thread at a time uses a `VkQueue`.

- **Ganesh** has one `GrDirectContext`, which is single-threaded. All windows would draw through it under one new lock, so window drawing would run one window at a time, and its flush calls `vkQueueSubmit` from whichever window thread holds that lock. That needs a second lock around the queue against the compose thread, and a new place in the M11 lock order.
- **Graphite** splits recording from submitting. A `Recorder` records drawing without touching the queue; `Recorder::snap()` gives a `Recording`; only `Context::insertRecording()` and `Context::submit()` touch the queue (`skia/include/gpu/graphite/Context.h`, `Recorder.h`). So each window's engine has its own `Recorder` and records on its own thread, in parallel as today, and the **compose thread** owns the `Context`: it inserts the recordings of all windows and submits them, and then submits its own compose. Every `vkQueueSubmit` of app_server, Skia's and the compositor's, is then on that one thread. No new queue lock, and the M11 lock order (`fWindowLock` → `fComposeSlot` → the compositor's own locks → `HWInterface`) does not change.

**Stability in m158.** Ganesh is the mature backend. Graphite's native Vulkan backend is the younger one, and m158's release notes still add to it (for example the persistent pipeline storage, "only relevant to Graphite's native Vulkan backend", `skia/RELEASE_NOTES.md`). That is a real risk: R5 may hit a Graphite bug first. It is handled the M11 way: the version is pinned, the compare set checks every operation on Lavapipe before RADV, and a device-lost or failed submit goes to the fallback (decision 8, later the ladder of decision 10). Graphite reports a lost device through `VulkanBackendContext::fDeviceLostProc` and `Context::isDeviceLost()`.

**How Graphite draws shapes.** Rectangles, round rectangles, ellipses and lines are drawn with analytic coverage in the shader. General paths (polygons, Bézier curves, `BShape`, filled arcs) are drawn by tessellation with MSAA (`ContextOptions::fInternalMultisampleCount`, default 4), or, for paths smaller than `ContextOptions::fMinimumPathSizeForMSAA`, from a coverage atlas that Skia rasterises on the CPU (`skia/src/gpu/graphite/RendererProvider.cpp`). The third way, Vello compute shaders, is only in a build with `SK_ENABLE_VELLO_SHADERS`, which R5 does not build. M12 sets `fMinimumPathSizeForMSAA = 0` (Skia's default), so every path is drawn on the GPU, and keeps 4 samples on RADV and on Lavapipe so both give the same picture. Whether the small-path atlas may be used later is an open question (Q3).

**The window images.** Skia draws into Vulkan images, but the M11 window buffer is a host-visible `VkBuffer` that `compose.comp` and `effects.comp` read as a storage buffer (`distro/app_server/WindowBuffer.h`). M12 keeps the compose unchanged: each window gets, besides its buffer, a `VkImage` (`VK_FORMAT_B8G8R8A8_UNORM`, optimal tiling, colour attachment, sampled, transfer source), which Graphite draws into (`BackendTextures::MakeVulkan` and `SkSurfaces::WrapBackendTexture`, `skia/include/gpu/graphite/vk/VulkanGraphiteTypes.h`). After Skia's work, the compose thread copies the damaged rectangles from the image into the buffer with one small compute shader in its own command buffer (part 4, "Alpha"), and then composes as in M11. The compose shaders stay byte-identical. The image is counted in the window memory budget of M11, so the budget is reached earlier. Reading the images directly in the compose is M13 work.

## 3. Static library, not a private shared library (decision 3)

Skia is built as a **static library** and linked only into `app_server`.

- **Hidden by construction.** A private `.so` would still be a file in the image that any program could load, and it would need a rule to keep it private. A static library exists only inside the one binary. That is decision 3 without a rule to police.
- **No Skia ABI.** Skia has no stable binary interface between milestones. Linked statically, app_server and Skia always come from the same build; there is no second file that can be out of step after an update.
- **No Skia symbols leave app_server.** Decorator add-ons are loaded into app_server and resolve their symbols against it. Skia is built with `-fvisibility=hidden`, and app_server is linked with `-Wl,--exclude-libs,ALL` for the Skia archive, so a decorator still sees only the Haiku classes (`DrawingEngine` and the rest), never a Skia name. The R5 Skia code (`SkiaDrawingEngine` and its helpers) is the only code that includes a Skia header.
- **Vulkan through the compositor.** Skia gets `vkGetInstanceProcAddr` from the compositor (`fGetProc`); it does not link `libvulkan` itself, so the Vulkan loader is loaded once, as in M11.
- **Cost.** app_server grows by the size of Skia (step 1 measures it), only once in memory because there is one app_server. Every Skia update relinks app_server, which the recipe does anyway.
- **Packaging.** The recipe builds `libskia.a` and its headers into a build-only package that jam uses to link app_server, next to the Mesa recipe. Nothing of that package is installed in the image; the image only has the larger `app_server`.

A private shared library would only pay off if Skia were updated without app_server, which the release mechanism does not do.

## 4. The mapping table

Read from `drawing/DrawingEngine.h`, `drawing/Painter/Painter.h` and the files they use. R5 has no copy of these files in `distro/` yet, so the Haiku files are the source.

### How `SkiaDrawingEngine` is built

- **A subclass, with the base class as the CPU path.** `class SkiaDrawingEngine : public DrawingEngine` (new R5 file, MIT, `distro/app_server/`). It overrides every virtual call of `DrawingEngine`, as `RemoteDrawingEngine` (`drawing/interface/remote/`) already does; that class shows that the interface can be served without Painter. When the desktop is in the CPU mode, each override calls the base class, so the fallback is a flag plus `SetHWInterface(screen)`, and the engine object that `Window`, `ServerWindow` and the decorator hold stays the same (as in M11). The flag is desktop-wide, so the GPU and Painter never mix (decision 1).
- **The same geometry as Painter.** Painter moves coordinates before AGG sees them: pixel-centre alignment for strokes, the pen size rules, `B_SUBPIXEL_PRECISE`, the ellipse and rectangle alignment (`AlignRect()`, `AlignEllipseRect()`, `TransformAlignAndClipRect()`, the `_Align` helpers in `Painter.cpp`). These helpers are copied (Haiku MIT notice kept, into `distro/`) and called by `SkiaDrawingEngine` before it calls Skia, so both engines start from the same numbers. Skia only does coverage and blending.
- **The same blending as Painter: the R5 blenders.** Haiku's drawing modes are integer formulas in `drawing_modes/*.h`, for example `BLEND(d, r, g, b, a)`: `d + ((s - d) * a) >> 8`, alpha always 255 (`DrawingMode.h`). Skia's own blend modes compute in floating point on premultiplied colour, so they are not the same. Every drawing call therefore goes through an **R5 blender**: an `SkRuntimeEffect::MakeForBlender()` in SkSL, one per mode, that does Haiku's formula on the 8-bit values (small integers, so `float` is exact). The coverage reaches the blender in one of two ways:
  - **Direct:** the paint carries the colour and the R5 blender, and Skia applies its own coverage (`mix(dst, blend, coverage)`). That equals Painter where Painter's partial coverage is the same linear mix: every mode at full coverage, and `B_OP_OVER`, `B_OP_ERASE`, `B_OP_INVERT`, `B_OP_SELECT` and `B_OP_ALPHA` overlay at the edges, on an opaque target.
  - **Two passes:** Skia first draws the shape's coverage into an 8-bit coverage image (white with `kSrc` on a cleared `R8` image the size of the shape's bounds), then one rectangle with a runtime shader that reads that coverage and the pattern, plus the R5 blender, which gets the coverage inside the source colour. This is exact for any formula. It is needed for `B_OP_COPY` at partial coverage (Haiku blends towards the **low colour**, not the destination: `BLEND_COPY` is `BLEND_FROM(d, low, color, cover)` in `DrawingModeCopy.h`), for `B_OP_ADD`, `B_OP_SUBTRACT`, `B_OP_BLEND`, `B_OP_MIN` and `B_OP_MAX` (their single-pixel paths use `BLEND` even at full coverage, which is one off from the source colour), for subpixel shapes, and on a window with a transparent background (frosted glass), where Haiku writes alpha 255 at every pixel it touches.
  - Step 3 starts with two passes for every mode, and switches a mode to direct only where the compare set shows the same pixels.
- **Straight alpha in the image.** Haiku's buffers keep straight (not premultiplied) alpha, and the M11 compose reads a pixel's colour also where its alpha is 0 (the background of a frosted-glass view). The window image keeps Haiku's bytes exactly: Skia is told the image is premultiplied and has no colour space (`nullptr`), so it never converts, and because every draw goes through an R5 blender, the bytes mean what Painter means. The copy from the image into the M11 buffer is then a plain `vkCmdCopyImageToBuffer` of the damaged rectangles. Uploaded bitmaps are treated the same way, so bilinear filtering works on straight colour, as Painter's does.
- **Recorders, recordings and damage.** Each engine has a `skgpu::graphite::Recorder`, used under a lock of its own. Drawing calls record. At the end of each batch of messages in `ServerWindow::_MessageLooper()`, and at every point where a pixel must be read (part 6), the engine snaps a `Recording` and hands it, with the damage of that batch, to the compositor. The compose thread inserts the recordings in order, submits, copies the damage into the window buffers, and composes. `CopyToFront()` only adds damage; it does not snap, so a stream of small drawings outside an update does not become a stream of submits.

### `DrawingEngine` calls

Class is the tolerance class of part 5: E exact; A1 analytic edges; A2 path edges; T text; B1 scaled bitmap, nearest; B2 bitmap, bilinear; G gradient; – no pixels.

| Call | Painter today | Skia in `SkiaDrawingEngine` | Class |
|---|---|---|---|
| `FrameBufferChanged()` | re-attaches Painter to the new buffer | wraps the new window image in a new `SkSurface` (`SkSurfaces::WrapBackendTexture()`) | – |
| `SetHWInterface()` | rebinds to a buffer | GPU mode: the window image of the new `WindowHWInterface`; CPU mode: base class | – |
| `SetCopyToFrontEnabled()`, `CopyToFront()` | copy to the front buffer, or M11 damage | damage only; the recording is snapped at the end of the batch | – |
| `LockParallelAccess()`, `UnlockParallelAccess()`, `LockExclusiveAccess()`, `UnlockExclusiveAccess()`, `IsParallelAccessLocked()`, `IsExclusiveAccessLocked()` | the `HWInterface` lock | unchanged, plus the engine's own recorder lock, taken after it | – |
| `DumpToBitmap()`, `ReadBitmap()` | read the buffer | a read point (part 6): snap, finish, then read the image back | E |
| `ConstrainClippingRegion()` | `BRegion` clip | `SkCanvas::clipRegion()` with an `SkRegion` of the same rectangles | E |
| `SetDrawState()` | copies the whole state, the offsets, the alpha mask | the same state in the engine; the alpha mask as below | – |
| `SetHighColor()`, `SetLowColor()` | pattern colours | uniforms of the pattern shader and the R5 blender | – |
| `SetPenSize()` | stroke width | `SkPaint::setStrokeWidth()` after Painter's pen-size rules (copied helper) | – |
| `SetStrokeMode()` | caps `B_BUTT_CAP`, `B_ROUND_CAP`, `B_SQUARE_CAP`; joins `B_MITER_JOIN`, `B_ROUND_JOIN`, and `B_BEVEL_JOIN`, `B_BUTT_JOIN`, `B_SQUARE_JOIN` all as bevel (the AGG join switch in `Painter.cpp`); miter limit | `SkPaint::Cap` `kButt`, `kRound`, `kSquare`; `SkPaint::Join` `kMiter`, `kRound`, and `kBevel` for the three that Painter makes bevel; `setStrokeMiter()` | – |
| `SetFillRule()` | `B_EVEN_ODD`, `B_NONZERO` | `SkPathFillType::kEvenOdd`, `kWinding` | – |
| `SetPattern()` | 8×8 pattern, high and low colour, anchored by the draw state's offsets (`PatternHandler`) | runtime shader: the 64 pattern bits as a uniform, anchored at the same offset; it tells the blender "high" or "low" per pixel | E |
| `SetDrawingMode()` (both) | picks a `drawing_modes/` family | picks the R5 blender | – |
| `SetBlendingMode()` | `B_PIXEL_ALPHA` or `B_CONSTANT_ALPHA`, with `B_ALPHA_OVERLAY`, `B_ALPHA_COMPOSITE` or one of the twelve other `B_ALPHA_COMPOSITE_*` functions | the matching R5 blender (table below) | – |
| `SetFont()` (both) | `AGGTextRenderer` gets the `ServerFont` | an `SkFont` from the same font (part 6) | – |
| `SetTransform()` | `BAffineTransform` plus offsets | `SkMatrix` (`sx`, `shx`, `tx`, `shy`, `sy`, `ty`) after the same offsets | – |
| `CopyRegion()` | sorts the rectangles so none overwrites another, then `CopyRect()` (`memmove`) | per rectangle `SkSurface::makeImageSnapshot(rect)`, then `drawImage()` at the offset with `kSrc`; a snapshot is a copy, so the order does not matter | E |
| `CopyRect()` | helper of `CopyRegion()` | not used; see `CopyRegion()` | E |
| `InvertRect()` | inverts the rectangle in the buffer | rectangle with the `invert` blender | E |
| `DrawBitmap()` | `BitmapPainter`: colour-space conversion, `B_TRANSPARENT_MAGIC_*`, scale, `B_FILTER_BITMAP_BILINEAR`, drawing mode | the same conversion (copied from `BitmapPainter::_ConvertColorSpace()`), upload to an image, `drawImageRect()` with `SkFilterMode::kNearest` or `kLinear` and the R5 blender | E unscaled, B1, B2 |
| `DrawArc()` (both) | `StrokeArc()`, `FillArc()` with `agg::arc` | `SkPath::addArc()` for a stroke, centre + arc + `close()` for a fill; Haiku's angles are counter-clockwise in degrees, Skia's clockwise: start `-angle`, sweep `-span` | A2 |
| `DrawBezier()` (both) | cubic `agg::curve4` | `SkPath::cubicTo()` | A2 |
| `DrawEllipse()` (both) | `AlignEllipseRect()`, `agg::ellipse` | `drawOval()` on the aligned rectangle | A1 |
| `DrawPolygon()` (both) | `agg::path_storage`, open or closed | `SkPath` `moveTo()`/`lineTo()`, `close()` when closed | A2 |
| `StrokePoint()` | one pixel in a colour | 1×1 rectangle, `kSrc` | E |
| `StrokeRect(rect, color)`, `FillRect(rect, color)` | the server's own solid rectangles (decorator, backgrounds) | aligned rectangle, `kSrc` | E |
| `FillRegion(region, color)` | solid region | `drawRegion()`, `kSrc` | E |
| `StrokeRect()`, `FillRect()` | with pattern and mode; through AGG when transformed | aligned rectangle with the pattern shader and the R5 blender; `drawRect()` under the matrix when transformed | E, A1 transformed |
| `StrokeRect()`, `FillRect()` with a gradient | gradient span generator | the R5 gradient shader (below) | G |
| `FillRegion()` (both) | per rectangle, pattern or gradient | `drawRegion()` with the pattern or gradient shader | E, G |
| `DrawRoundRect()` (both) | `agg::rounded_rect` | `SkRRect::MakeRectXY()`, `drawRRect()` | A1 |
| `DrawShape()` (both) | the `BShape` op list into an AGG path, scaled and offset to the screen | `SkPath`: `OP_MOVETO` → `moveTo()`, `OP_LINETO` → `lineTo()`, `OP_BEZIERTO` → `cubicTo()`, the four `OP_*ARC_TO_*` → `arcTo(rx, ry, 0, large, sweep, x, y)` (the SVG form Haiku uses), `OP_CLOSE` → `close()`; the same offset and scale | A2 |
| `DrawTriangle()` (both) | `StrokeTriangle()`, `FillTriangle()` | three-point `SkPath` | A2 |
| `StrokeLine(a, b, color)` | decorator lines; `StraightLine()` for horizontal and vertical lines | horizontal and vertical: aligned rectangle, `kSrc`; else `drawLine()` | E, A1 |
| `StrokeLine()` (both) | line with pen size, caps, pattern or gradient | `drawLine()` with the stroke paint | A1 |
| `StrokeLineArray()` | one colour per line | one `drawLine()` per entry, in order | A1 |
| `DrawString()` (both) | `AGGTextRenderer`; returns the pen position | part 6; the pen position still comes from `ServerFont` | T |
| `StringWidth()` (both), `DrawStringDry()` (both) | metrics only | unchanged (`ServerFont`) | – |
| `SetRendererOffset()` | offset of the base renderer | `SkCanvas::translate(-left, -top)` on the window image | – |

### Painter functions

Every public function of `Painter.h`. Those that only serve a `DrawingEngine` call point to its row.

| Painter | Skia |
|---|---|
| `AttachToBuffer()`, `DetachFromBuffer()`, `Bounds()` | the window or bitmap image wrapped as an `SkSurface`, and its bounds |
| `SetDrawState()`, `ConstrainClipping()`, `ClippingRegion()` | `SetDrawState()`, `ConstrainClippingRegion()` |
| `SetTransform()`, `IsIdentityTransform()`, `Transform()` | `SetTransform()`; the identity test picks the aligned paths, as in Painter |
| `SetHighColor()`, `HighColor()`, `SetLowColor()`, `LowColor()`, `SetPattern()`, `Pattern()` | uniforms of the pattern shader |
| `SetPenSize()`, `PenSize()`, `SetStrokeMode()`, `SetFillRule()` | the stroke and fill rows |
| `SetDrawingMode()`, `DrawingMode()`, `SetBlendingMode()` | the R5 blender rows |
| `SetFont()` (both), `Font()` | part 6 |
| `StrokeLine()` (both), `StraightLine()` | the `StrokeLine()` rows |
| `StrokeTriangle()` (both), `FillTriangle()` (both) | `DrawTriangle()` |
| `DrawPolygon()` (both), `DrawBezier()` (both), `DrawShape()` (both) | their rows |
| `StrokeRect()` (three forms), `FillRect()` (three forms) | the rectangle rows |
| `FillRectVerticalGradient()` | the decorator's vertical linear gradients: the R5 gradient shader, linear |
| `FillRectNoClipping()` | rectangle with the colour's own alpha, `kSrc`, no clip (M11 step 4 uses that alpha for the frosted-glass view background) |
| `StrokeRoundRect()` (both), `FillRoundRect()` (both) | `DrawRoundRect()` |
| `AlignEllipseRect()`, `TransformAndClipRect()`, `ClipRect()`, `TransformAlignAndClipRect()`, `AlignAndClipRect()`, `AlignRect()` | the copied geometry helpers; no Skia call |
| `DrawEllipse()` (both), `StrokeArc()` (both), `FillArc()` (both) | their rows |
| `DrawString()` (both), `BoundingBox()` (both), `StringWidth()` | part 6; `BoundingBox()` and `StringWidth()` are metrics |
| `DrawBitmap()`, `FillRegion()` (both), `InvertRect()`, `SetRendererOffset()` | their rows |

### Drawing modes

Every R5 blender writes alpha 255, except `B_OP_ALPHA` with a `B_ALPHA_COMPOSITE*` function, as in Painter (M11 step 4: "Every other mode writes alpha 255").

| Mode | Haiku formula (`drawing_modes/`) | R5 blender | A Skia blend mode that equals it? |
|---|---|---|---|
| `B_OP_COPY` | the pattern colour; at partial coverage between the **low colour** and it | `copy`, two passes at partial coverage | no (`kSrc` differs at the edges) |
| `B_OP_OVER` | only the high pixels of the pattern, `BLEND` towards the high colour | `over` | close to `kSrcOver`, not exact |
| `B_OP_ERASE` | only high pixels, towards the low colour | `erase` | no |
| `B_OP_INVERT` | only high pixels, towards `255 - d` | `invert` | no |
| `B_OP_ADD` | `min(255, d + s)`, then `BLEND` | `add` | no (`kPlus` is premultiplied, without `BLEND`) |
| `B_OP_SUBTRACT` | `max(0, d - s)`, then `BLEND` | `subtract` | no |
| `B_OP_BLEND` | `(d + s) >> 1`, then `BLEND` | `blend` | no |
| `B_OP_MIN`, `B_OP_MAX` | Haiku's own comparison (`DrawingModeMin.h`, `DrawingModeMax.h`) | `min`, `max`, the same comparison | no (`kDarken`, `kLighten` work per channel) |
| `B_OP_SELECT` | a pixel equal to the high colour becomes the low colour and the reverse; others stay | `select`, exact comparison of the 8-bit values | no |
| `B_OP_ALPHA` with `B_ALPHA_OVERLAY` | over by the source alpha (`B_PIXEL_ALPHA`) or the high colour's alpha (`B_CONSTANT_ALPHA`); alpha 255 | `alpha_overlay` | close to `kSrcOver` on an opaque target only |
| `B_OP_ALPHA` with `B_ALPHA_COMPOSITE` (`_SOURCE_OVER`) | straight-alpha composite that writes alpha (`BLEND_COMPOSITE`) | `composite_over` | `kSrcOver`, but on premultiplied colour |
| `B_ALPHA_COMPOSITE_SOURCE_IN`, `_SOURCE_OUT`, `_SOURCE_ATOP`, `_DESTINATION_OVER`, `_DESTINATION_IN`, `_DESTINATION_OUT`, `_DESTINATION_ATOP`, `_XOR`, `_CLEAR`, `_DIFFERENCE`, `_LIGHTEN`, `_DARKEN` | AGG's comp-op formulas through `AggCompOpAdapter.h` | one R5 blender each, on straight values, with AGG's formulas | the Porter-Duff forms exist in `SkBlendMode`, on premultiplied colour |

Every mode also has a `SUBPIX` form (`*SUBPIX.h`) with three coverages; in the two-pass way the coverage image then has three channels (below).

### Other Painter features

| Feature | Painter | Skia |
|---|---|---|
| **The five gradient types** (`BGradient::TYPE_LINEAR`, `TYPE_RADIAL`, `TYPE_RADIAL_FOCUS`, `TYPE_DIAMOND`, `TYPE_CONIC`) | a colour array made from the stops by `Painter::_MakeGradient()`, looked up through an AGG gradient function: `gradient_x` (with `_CalcLinearGradientTransform()`), `gradient_radial`, `gradient_radial_focus`, `gradient_diamond`, `gradient_conic` | one **R5 gradient shader** (a runtime shader) for all five: the colour array is made by the same copied `_MakeGradient()` and uploaded as a small image; the shader computes AGG's position function for the type and looks it up. Skia's own gradients are not used, so all five follow Painter's ramp, also the diamond, which Skia does not have |
| **Alpha masks** (`AlphaMask.h`: `UniformAlphaMask`, `PictureAlphaMask` for `ClipToPicture()`, `ShapeAlphaMask` for `ClipToShape()`, the inverse forms, each chained to the previous mask) | Painter draws the picture or shape into an 8-bit mask through a `BitmapDrawingEngine` and clips its scanlines by it | Skia draws the mask into an `A8` image (a picture through a nested `SkiaDrawingEngine` on that image, a shape with `drawPath()`, a uniform mask as a constant), combines it with the previous mask the way `AlphaMask` does, and applies it with `SkCanvas::clipShader()` (the image as a shader in screen coordinates). Painter draws no mask in the GPU mode either (decision 1) |
| **Subpixel shapes** (`gSubpixelAntialiasing`, `gSubpixelOrderingRGB`, `gSubpixelAverageWeight` in `GlobalSubpixelSettings.h`; the `*_subpix` rasterizer and scanlines) | AGG rasterises at three times the horizontal resolution and filters by the average weight | two passes: Skia draws the shape three times as wide (`SkMatrix::Scale(3, 1)`) into a coverage image; the composite shader reads three coverages per pixel, applies the same weight and order, and gives them to the `SUBPIX` form of the R5 blender. Skia itself has subpixel coverage only for text |
| **Aliased drawing** (`gamma_threshold(0.5)` on the rasterizers in `Painter.cpp`) | coverage cut at one half | `SkPaint::setAntiAlias(false)` where Painter cuts; whether Skia's pixel centres match AGG's threshold is Q4 |
| **Bitmap colour spaces**, `B_TRANSPARENT_MAGIC_RGBA32`, `_RGBA15`, `_CMAP8` | converted to 32 bits first (`BitmapPainter::_ConvertColorSpace()`); the magic colour is transparent in the modes that honour it | the same copied conversion before the upload |
| **`B_FILTER_BITMAP_BILINEAR`** | AGG's bilinear span generator | `SkFilterMode::kLinear` on the straight bytes |
| **`B_WAIT_FOR_RETRACE`** | ignored by Painter | ignored |
| **Tiled view bitmaps** (`B_TILE_BITMAP*`) | `View` draws the bitmap several times through `DrawBitmap()` | the same calls: the `DrawBitmap()` row |

No row is left without a mapping. Where a mapping is known but its result is not proven, the compare set decides (part 7) and a failure is reported as a gap, never drawn on the CPU (decision 4).

## 5. Tolerances per class (decision 6)

How a result is measured: `r5-draw-compare` (part 7) draws one operation with Painter and with `SkiaDrawingEngine` into two images of the same size, starting from the same background, and compares the bytes (B, G, R, A) inside the operation's bounding box grown by 2 pixels. Two numbers come out: the **largest difference** of any channel of any pixel, and the **share** of pixels in that box where some channel differs by more than 1. An **inner pixel** is a pixel whose 3×3 neighbours in Painter's image all have the same value; inner pixels are held to exact equality in every class, because a fill or a stroke body must not change colour.

| Class | Operations | Inner pixels | Largest difference | Share above 1 | Why this number |
|---|---|---|---|---|---|
| **E** exact | solid fills, rectangles and regions on whole pixels, `CopyRegion()`, `InvertRect()`, `StrokePoint()`, horizontal and vertical lines, patterns, unscaled bitmaps, every drawing mode at full coverage | exact | 0 | 0 % | the R5 blenders do Haiku's integer formulas; nothing is left to round |
| **A1** analytic edges | transformed rectangles, ellipses, round rectangles, lines that are not straight | exact | 24 | 3 % | Skia computes edge coverage analytically in floating point, AGG by exact cell area in 8-bit fixed point; a difference of about a tenth of the coverage at an edge pixel |
| **A2** path edges | polygons, triangles, Bézier curves, `BShape`, arcs, mask edges | exact | 64 | 6 % | general paths use 4× MSAA (part 2), so coverage comes in steps of a quarter (64 of 255) |
| **T** text | `DrawString()` in all its forms | exact (stems of large text) | no limit | 10 % of the pixels in the string's box differ by more than 16 | decision 5: only the rasterisation of the letters may differ. Layout is checked separately, exactly: the returned pen position is equal, and each glyph's ink box lies within 1 pixel of Painter's |
| **B1** scaled bitmap, nearest | `DrawBitmap()` scaled without filter | – | no limit | 1 %, and every differing pixel equals one of Painter's 3×3 neighbours | the two may pick the next source row or column where the scale falls exactly between two |
| **B2** bitmap, bilinear | `DrawBitmap()` with `B_FILTER_BITMAP_BILINEAR` | exact where the bitmap is unscaled | 4 | 5 % | weights in float against AGG's 8-bit weights |
| **G** gradient | the five types, on any shape | – | 3 | no limit | the same colour array; only the position (float against fixed point) can land one entry over |

- An operation in several classes (a gradient inside a Bézier curve, a pattern in a transformed rectangle) is held to the looser of its classes for its edge pixels and to the stricter for its inner pixels.
- Subpixel shapes and text keep their class; the difference is counted per channel, so a colour fringe that moved counts.
- The same set also compares **Skia on RADV against Skia on Lavapipe**, in the same classes, so a driver difference shows up as a line of its own.
- M11's `compose compare` stays as it is (exact, effects off).
- These are starting values. A failing operation is a gap and is reported; the numbers change only by a decision of the user, never to make a test pass.

