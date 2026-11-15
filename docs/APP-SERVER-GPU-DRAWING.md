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
| **Subpixel shapes** (`gSubpixelAntialiasing`, `gSubpixelOrderingRGB`, `gSubpixelAverageWeight` in `GlobalSubpixelSettings.h`; the `*_subpix` rasterizer and scanlines) | AGG rasterises at three times the horizontal resolution and filters by the average weight | **grey in the GPU mode** (user, 15 Nov 2026, Q12): shapes are drawn with grey coverage, like every current system; only text has subpixel coverage (Skia's own, Q5). Painter keeps its subpixel shapes in Safe mode. The `SUBPIX` forms of the R5 blenders are not needed |
| **Aliased drawing** (`gamma_threshold(0.5)` on the rasterizers in `Painter.cpp`) | coverage cut at one half | `SkPaint::setAntiAlias(false)` where Painter cuts; whether Skia's pixel centres match AGG's threshold is checked by the compare set (aliased text in group 9) |
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

## 6. Text, offscreen bitmaps and `Sync()`

### Text (decision 5)

- **Glyphs and positions stay `ServerFont`'s.** Painter's `AGGTextRenderer` gets glyphs and positions from `GlyphLayoutEngine::LayoutGlyphs()` (`font/GlyphLayoutEngine.h`), which applies the escapement delta, the spacing mode, the offsets of `DrawString(offsets)` and the font fallback. `SkiaDrawingEngine` calls the same function with its own consumer, which collects glyph indices and positions instead of drawing, and draws them with `SkCanvas::drawGlyphs()`. A run is cut where the fallback changes the font. So line heights, widths and positions are identical in both modes by construction; `StringWidth()`, `DrawStringDry()` and `BoundingBox()` do not change at all.
- **The same font files.** Each `FontStyle` gets one `SkTypeface`, made by an empty custom font manager (`skia_enable_fontmgr_custom_empty`) from the same file (`FontStyle::Path()`, with its face index) or, for a font a program loaded from memory, from the same bytes (`FontStyle::FontData()`, `FontDataSize()`). Skia reads them through R5's own FreeType (part 1).
- **Size, shear, rotation.** `SkFont` size is `ServerFont::Size()`. Painter shears by `(90 - Shear())` degrees and rotates by `-Rotation()` degrees about the baseline origin (`AGGTextRenderer.cpp`); `SkiaDrawingEngine` builds the same matrix and draws under it.
- **The Antialiasing tab of Appearance** sets the globals in `GlobalSubpixelSettings.h`, which both engines read:
  - hinting `HINTING_MODE_OFF` → `SkFontHinting::kNone`; `HINTING_MODE_ON` → `kNormal` (`FT_LOAD_TARGET_NORMAL`, as `FontEngine.cpp`), or `kFull` with subpixel text (`FT_LOAD_TARGET_LCD`, as `FontEngine.cpp`); `HINTING_MODE_MONOSPACED_ONLY` → hinting only for fixed-width fonts, as Haiku;
  - grey antialiasing → `SkFont::Edging::kAntiAlias`; no antialiasing (`B_DISABLE_ANTIALIASING`, or the global off without `B_FORCE_ANTIALIASING`) → `kAlias`;
  - subpixel antialiasing (`gSubpixelAntialiasing`) with the order from `gSubpixelOrderingRGB`: **Skia's own subpixel text** (user, 15 Nov 2026, Q5): `SkFont::Edging::kSubpixelAntiAlias`, with `SkSurfaceProps` pixel geometry `kRGB_H` or `kBGR_H` from that order. Skia uses FreeType's LCD filter, which has no "average weight", so `gSubpixelAverageWeight` has no effect in the GPU mode; it still works in Safe mode (Painter). RADV and Lavapipe both draw through Skia, so both give the same letters. Text stays in class T against Painter.
- **Underline and strikeout** (`B_UNDERSCORE_FACE`, `B_STRIKEOUT_FACE`) are rectangles that `AGGTextRenderer.cpp` places from the font metrics; the same positions (copied code), drawn as rectangles.
- **Drawing mode and colour:** the text goes through the R5 blender of the current mode, direct or in two passes, like every shape.
- **Seen, not changed:** `LayoutGlyphs()` makes Haiku's own glyph cache entries (FreeType outlines or bitmaps) as a side effect, also when only positions are wanted. That is metrics work that `StringWidth()` already does today; M13 can ask for metrics only.

### Offscreen `BBitmap` views (decision 7)

**Today.** A `BBitmap` with `B_BITMAP_ACCEPTS_VIEWS` is an `OffscreenWindow` in app_server, with a `BitmapHWInterface` over the bitmap's memory (`OffscreenWindow.cpp`), an area the program shares. Its `ServerWindow` thread draws with Painter straight into that memory. `BView::Sync()` sends `AS_SYNC`, and `ServerWindow` answers at once (`distro/desktop/src/servers/app/ServerWindow.cpp`, `case AS_SYNC`): Painter has already finished. A bitmap in a colour space other than `B_RGB32` or `B_RGBA32` is drawn in a 32-bit back buffer and converted when copied to the bitmap (`drawing/BitmapHWInterface.cpp`).

**In the GPU mode.**

1. The offscreen window's engine is a `SkiaDrawingEngine` on a GPU image of its own (`B8G8R8A8`, the size of the bitmap), with its own `Recorder` on that window's thread.
2. **Upload at the start of a drawing session.** The program can write the bits itself at any time (for example `distro/ui`'s `ActionCard.cpp` and `PageHeader.cpp` `memcpy` an icon into a fresh `B_BITMAP_ACCEPTS_VIEWS` bitmap before a view draws into it). So the first drawing command after the bitmap was created, or after its last read point, first uploads the bitmap's memory into the image (through the 32-bit conversion for other colour spaces).
3. **Read points.** At each of these the engine snaps its recording, the compose thread submits it, app_server waits for that submit's fence, and the image is copied back into the bitmap's memory (converted back for other colour spaces, as `BitmapHWInterface` does):
   - `AS_SYNC` from that bitmap's view: the answer is sent only after the copy;
   - app_server itself reading those pixels: `DrawBitmap()` of that bitmap into anything (a window, another bitmap, a picture), a drag bitmap, a cursor made from it, a `BPicture` that records it, an alpha mask made from it, and `ReadBitmap()`;
   - the bitmap being deleted or its view being removed (`RemoveChild`), so nothing is pending afterwards.
   A read point from another thread (a window drawing the bitmap) takes the bitmap engine's recorder lock to snap; that lock comes after the window lock, like the `HWInterface` lock.
4. **`DrawBitmap()` always reads memory.** A bitmap is uploaded from its memory every time it is drawn, also a view bitmap after its read point, because the program may have changed the bits since; app_server cannot see such a write. Keeping uploads for unchanged bitmaps is M13 work.
5. **Lost content.** If the GPU is lost (decision 8, later the ladder), drawing into a bitmap since its last read point is lost; the memory holds the state of that point. Haiku has no such case; it is written in the limits.
6. **The wait.** The fence wait of a read point follows M11 decision 14: late is not a failure; a lost device is the fallback.

**Programs that read without `Sync()`.** Decision 7 says a correct program calls `Sync()` first. A search of the Haiku tree (every `.cpp` under `src/` that names `ACCEPTS_VIEWS`, checked for a pixel read and for `Sync()`) found no program that reads the bits of a view bitmap without `Sync()`:

| Program | File | What it does | Expected |
|---|---|---|---|
| Icon-O-Matic | `src/apps/icon-o-matic/generic/gui/IconButton.cpp` | draws into a view bitmap, `Sync()`, then reads | unchanged |
| Shared `BIconButton` | `src/kits/shared/IconButton.cpp` | the same | unchanged |
| FileTypes | `src/preferences/filetypes/IconView.cpp` | the same | unchanged |
| MediaPlayer subtitles | `src/apps/mediaplayer/interface/SubtitleBitmap.cpp` | draws text into a view bitmap, `Sync()`, then draws it | unchanged |
| R5 Settings cards | `distro/ui/` `ActionCard.cpp`, `PageHeader.cpp` (in the WSL tree under `src/preferences/r5ui/`) | writes the bits of a fresh view bitmap with `memcpy` before drawing | upload at point 2 |
| R5 icons | `distro/icons/R5Icons.cpp` | draws, `Sync()`, reads | unchanged |
| `src/tests/servers/app/` | – | no test there uses a view bitmap | – |

Because no existing program reads without `Sync()`, step 5 writes one test program, `r5-offscreen-test` (MIT, `/system/bin`), with four cases, each printing one line:

- `offscreen sync ok`: draw, `Sync()`, read: the pixels equal Painter's (class E content);
- `offscreen cpu write ok`: write the bits, attach a view, draw over half: both halves are right (point 2);
- `offscreen drawbitmap ok`: draw into a bitmap, no `Sync()`, `DrawBitmap()` it into a window, read the window back with `BScreen::GetBitmap()`: the drawing is there (point 3, app_server's own read);
- `offscreen nosync`: draw, no `Sync()`, read at once: reports what it saw, `stale` or `fresh`, without a pass or a fail, because Haiku itself does not promise either. The step report records the line.

## 7. The compare set and the measurement

### `r5-draw-compare` (step 2)

- **Where the comparing happens: inside app_server**, because both engines live there (as M11's `compose compare`). `r5-draw-compare` (MIT, `/system/bin`) is a small client. It records each operation into a `BPicture` with the public `BView` calls and sends it to app_server with one private message, which app_server only accepts with `gpu_debug true`; otherwise it answers with `draw-compare refused: gpu_debug off`. The private message is additive; nothing changes in `libbe`'s public API.
- **How one operation is compared.** app_server plays the picture twice through its own picture player: once into a Painter on a plain memory buffer (`BitmapHWInterface` and a `DrawingEngine`, as offscreen bitmaps work today), once into a `SkiaDrawingEngine` on a GPU image of the same size. Both start from the same background. Then the GPU image is read back (a read point, part 6) and compared with the class of part 5.
- **Calls a picture cannot reach** (the server's own: `StrokeRect`, `FillRect`, `FillRegion` and `StrokeLine` with a colour, `StrokePoint()`, `CopyRegion()`, `FillRectNoClipping()`, `ReadBitmap()`) are called directly by a fixed list in app_server's compare code, with the same two engines.
- **One line per operation**, exactly the brief's pass line with the numbers behind it: `draw-compare <op> ok max <n> share <p>%`, or `draw-compare <op> FAIL max <n> share <p>% class <c>`. At the end `draw-compare <n>/<n> ok`. `<n>` is fixed by step 2 when the set is built and written into this document.

**The set.** Every operation runs on an opaque background and again on a transparent one (alpha 0 with a colour, as a frosted-glass view).

1. Every row of the `DrawingEngine` table in part 4 in `B_OP_COPY` with a solid high colour, pen sizes 1 and 3.
2. Every drawing mode on four shapes: an aligned rectangle, an ellipse, a text line, a bitmap with alpha.
3. `B_OP_ALPHA`: `B_PIXEL_ALPHA` and `B_CONSTANT_ALPHA` with `B_ALPHA_OVERLAY` and all thirteen `B_ALPHA_COMPOSITE*` functions, on a rectangle and an ellipse.
4. Patterns: `B_MIXED_COLORS` and one custom pattern, on a rectangle, an ellipse and a line, with an odd draw offset.
5. Gradients: the five types on a rectangle, a round rectangle and a Bézier curve, with stops that have alpha.
6. Transforms: rotation by 30°, scale by 1.5, a shear, on a rectangle, an ellipse, text and a bitmap.
7. Clipping: a region of three rectangles, `ClipToPicture()`, `ClipToShape()`, the inverse forms, and one inside another.
8. Bitmaps: unscaled, scaled by 2 and by 0.7, nearest and bilinear; `B_RGB32`, `B_RGBA32`, `B_CMAP8`, `B_RGB16`, `B_GRAY8`; the transparent magic colour.
9. Text: plain, bold and fixed system fonts at 9, 12, 24 and 72 points; grey, aliased and subpixel (RGB and BGR); hinting off, on and monospaced only; shear and rotation; underline and strikeout; a glyph from a fallback font; `DrawString()` with offsets and with an escapement delta. Each text line also checks the layout exactly (part 5, T).
10. Subpixel setting: an ellipse and a polygon with `gSubpixelAntialiasing` on and off; the GPU result is compared with Painter's grey result in both cases (Q12).
11. Strokes: each cap with each join on an open polygon, miter limits 2 and 10, pen sizes 0.5, 1 and 2.5.
12. The R5 decorator: one whole window frame (tab, buttons, frame line) for an active and an inactive window, drawn by the decorator into both engines.
13. Offscreen: group 1 again into a view bitmap, through `Sync()`.
14. Devices: where RADV and Lavapipe are both present, groups 1–9 are also compared Skia on RADV against Skia on Lavapipe.

**Time per operation.** `r5-draw-compare --time` draws each operation of groups 1–11 a hundred times in each engine and prints `draw-time <op> gpu <n> us cpu <n> us` (median). That gives the list of operations that are slower on the GPU, for M13 (decision 9), without guessing from a scenario.

### The measurement (decision 9)

- **The lines,** every 10 s with `gpu_debug true`: in the GPU mode `app_server: draw median <n> us cpu <n> ms per 10 s` (the brief's line), and in the CPU mode (Safe mode) `app_server: draw cpu median <n> us cpu <n> ms per 10 s`.
- **Draw time** is per drawing session of one window: from its first drawing command after `BeginUpdate()` (or the first command of a batch outside an update) until its pixels are in the window buffer. In the GPU mode that is the fence of the submit that carried the recording and its copy into the buffer; in the CPU mode it is the moment Painter returns. The median is over all sessions of the 10 s. So both modes measure the same thing: how long a program waits before its drawing can be shown.
- **CPU time** is app_server's own CPU time (user and kernel) in those 10 s, from `get_team_usage_info()`, as M11's compose lines.
- **The scenario,** the same 60 s in each mode: the M11 scenario (a Tracker window dragged in circles over other windows for 20 s, a long Terminal output scrolled for 20 s, VulkanTeapot and `vkcube` side by side for 20 s), then 20 s of WebPositive on a long page, then the Appearance page with every tab opened once (brief, stick boot 1). Run on the 5625U in the GPU mode on RADV (boot 1), in Safe mode (boot 2) and on Lavapipe (boot 3), and in VirtualBox on Lavapipe (step 5).
- **Step 1 already measures one typical frame on Lavapipe** with Skia alone, before app_server uses it: an 800×600 window with a decorator-like tab, 30 lines of text, 10 rectangles, a gradient, two bitmaps and a row of round buttons. The line is `skia: <version> ok lavapipe frame <n> us`.
- **Result:** the medians and CPU times side by side in `CHANGELOG.md`, and the slower operations from `draw-time`. Slower on Lavapipe than Painter is recorded, not a reason to change the path (decision 9).

## 8. Safe mode (decision 10)

### What Haiku already has (read in the source, used first)

- **The boot menu** (`src/system/boot/loader/menu.cpp`, `user_menu()`): "Select boot volume/state" (this is where Haiku's older system states are, the package states), "Select safe mode options", "Select debug options", the platform's own menu, "Reboot", "Continue booting". The safe mode menu has the markable item "Safe mode" (`B_SAFEMODE_SAFE_MODE`, `"safemode"`, `headers/os/drivers/driver_settings.h`) and "Disable user add-ons" and others (`headers/private/system/safemode_defs.h`). The platform menus of `bios_ia32` and `efi` add "Use fail-safe video mode" (`B_SAFEMODE_FAIL_SAFE_VIDEO_MODE`). After the menu, `apply_safe_mode_options()` and `add_safe_mode_settings()` turn the marks into the safe mode driver settings in the kernel arguments; userland reads them with `_kern_get_safemode_option()`.
- **When the menu shows** (`src/system/boot/loader/main.cpp`): when no boot volume is found, or when `platform_boot_options()` has `BOOT_OPTION_MENU` (a key held at start). The decision is made right after `get_boot_file_system()`, before `load_driver_settings()`.
- **What reads the boot volume:** the loader has its own read-only file system code; `load_driver_settings()` already opens `home/config/settings/kernel/drivers` on the boot volume with `open_from()`. It cannot write or delete.
- **What "safemode" does today:** `LaunchDaemon::_RetrieveKernelOptions()` sets `fSafeMode`, and every job or service marked `no_safemode` in `data/launch/` is not started: mount_server, media_server, midi_server, net_server, print server, notification_server, power_daemon, cddb_lookup, and from R5 the AirPlay service; in the user session NetworkStatus, the VirtualBox clipboard, the AirPlay tray, LaunchBox and the `x-vnd.Be-POST` service. The Translation Kit (`TranslatorRoster.cpp`) and the add-on monitor (`AddOnMonitorHandler.cpp`) skip user add-ons; input_server reads it too. `fail_safe_video_mode` makes app_server use the fail-safe accelerant (`use_fail_safe_video_mode()` in `drawing/interface/local/AccelerantHWInterface.cpp`).
- **packagefs shine-through:** `cache`, `non-packaged`, `packages`, `settings` and `var` under `/boot/system` are plain directories of the boot volume (`packagefs/volume/Volume.cpp`), so the loader can read `system/var/...` on the volume itself.

### The marker

- **File:** `/boot/system/var/r5/desktop-start-failed`. A runtime state, so `var`, not `settings`; root's, not in anyone's home; not in `kernel/drivers`, so the kernel never parses it.
- **Content:** one line, the same reason as the log line (`desktop start failed: <reason>`), the build number from `r5_os_version.h`, and the time. At most 255 bytes. Nothing else, no user data.
- **Written by app_server** (uid 0) at step 3 of the ladder: to a temporary name, `fsync()`, `rename()`, then `fsync()` of the directory, so a crash right after still leaves a whole file.
- **Read by the loader:** an R5 change in `main.cpp` (copied to `distro/boot/loader/main.cpp`, R5 parts in SPDX snippets): after `get_boot_file_system()` returns a valid volume, `open_from(bootVolume.RootDirectory(), "system/var/r5/desktop-start-failed")`. If it exists, the loader shows the menu as with `BOOT_OPTION_MENU`, and passes a flag to `user_menu()`.
- **Removed by Login** (uid 0, `distro/login/LoginApp.cpp`) after a good Sign in **in a normal start**: when `BLaunchRoster::StartSession()` has returned `B_OK` and the safemode option is off. A Sign in in Safe mode does not remove it, so the menu keeps offering Safe mode until a normal start has worked once. Nobody else removes it.
- **Additive and MIT:** one new file, R5 snippets in the loader, app_server and Login. No change to Haiku's settings formats.
- **AMD64 only:** the loader code is shared by every platform, but only app_server on AMD64 writes the marker (decision 10, scope), so ARM64 and RISC-V never see it.

### The boot menu with Safe mode selected

- With the flag, `user_menu()` (an R5 snippet in `distro/boot/loader/menu.cpp`) puts two items at the top of the main menu, above Haiku's: **"Start in Safe mode"** (selected) and **"Start normally"**, with one line above them: "The desktop could not start last time." Haiku's items stay below, so "Select boot volume/state" (the older system state) is one step away, with the help text "To go back to an older system state, choose Select boot volume/state."
- **"Start in Safe mode"** marks "Safe mode" in the safe mode menu and "Use fail-safe video mode" in the platform menu, then continues booting. "Start normally" continues without them.
- **Safe mode chosen by hand** (stick boot 2): in R5, marking "Safe mode" in Haiku's safe mode menu also marks "Use fail-safe video mode" (an R5 snippet), so a choice by hand and the automatic choice give the same Safe mode. The fail-safe video item can still be unmarked by hand.
- **Waiting:** the menu waits for a key without a time limit, as Haiku's (user, 15 Nov 2026, Q7). No countdown. The stick card for boot 6 therefore has a press of Enter.

### What Safe mode turns off

- **Haiku's safe mode, unchanged:** the `no_safemode` jobs above, user add-ons in the kits that check it. So there is no network in Safe mode (net_server is `no_safemode` in Haiku); the older system state from the boot menu needs none.
- **The fail-safe graphics driver:** app_server uses the fail-safe accelerant (VESA or the boot frame buffer), as Haiku.
- **No GPU driver:** an R5 snippet in `radeon_hd` (`distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/driver.cpp`, next to the existing `gpu` setting) reads `B_SAFEMODE_SAFE_MODE` and then acts as `gpu false`: no ring, no self-test, no render node. Vulkan then has only Lavapipe and OpenGL only llvmpipe, which is what programs see.
- **The CPU mode:** app_server makes no compositor and no Skia: Painter, one back buffer, no shadow, no frosted glass, no animations, no zero-copy windows. `BDirectWindow` and `BWindowScreen` work as in Haiku. One line: `app_server: safe mode: cpu mode`.
- **The mark:** app_server draws the words "Safe mode" in the four corners of the screen, over the desktop background (`Desktop::_SetBackground()`, CPU path, only when the safemode option is on), in the system font and colours. app_server draws it, so it is there also when Tracker does not start.

### The ladder in a session: RADV → Lavapipe

**When:** RADV is lost and the one restart after a reset fails (M11 decision 14), or a submit fails after that restart. Today that is `gpu compositor off`; on AMD64 it becomes the move to Lavapipe. A late fence is still not a failure.

**Where and in which order.** On the compose thread, at the top of its loop, outside `fComposeSlot`, as M11 step 5 already handles a failed compose. The M11 lock order does not change: `fWindowLock` (write) → `fComposeSlot` → the compositor's own locks (`fEntryLock`, `fLayerLock`, `fAnimLock`, `fShareLock`, `fDamageLock`) → `HWInterface` (exclusive). The recorder locks of part 4 come after `fWindowLock` and are not taken by the compose thread during the move, because no window draws under the write lock.

1. `LockAllWindows()`: no `ServerWindow` draws, no recorder is in use.
2. Take `fComposeSlot` (the compose thread holds no frame).
3. Under the compositor's own locks: give every shared image back to its program (`_ReturnSharedImages()`, M11 step 5); drop every pending recording; destroy each engine's `Recorder` and each window and bitmap image; destroy the Graphite `Context`; free the window buffers; destroy the RADV `VkDevice`.
4. **Keep the `VkInstance`.** Lavapipe is another physical device of the same instance, so the Vulkan loader unloads no driver and no library with thread-local storage goes away (the crash path fixed in M11 step 5 is not touched).
5. Make the Lavapipe `VkDevice` the way the session start does (`gpu_compositor_lavapipe`), a new Graphite `Context`, and the window buffers and images again, within Lavapipe's budget (a quarter of physical memory). Each engine sees a new device generation and makes its `Recorder` again on its own thread at its next drawing call.
6. `HWInterface` exclusive: the scanout goes from the RADV way (M11 step 3b, option A: the GPU writes the back buffer and the scanout) to the Lavapipe way (`_Present` copies). The old imports are closed.
7. Every window dirty (`Desktop::Redraw()`), the whole screen damaged; offscreen bitmaps upload from their memory at their next drawing (part 6). Release the locks in reverse order. One line: `app_server: gpu lost, going on with lavapipe: <reason>`, with M11's reasons plus `restart failed`. With `gpu_debug`, the time between 1 and 7: `app_server: lavapipe move <n> ms`.

Between 1 and 7 nothing is drawn or composed, but the cursor still moves (it is drawn by `HWInterface`). The pass line asks for no freeze; the move's time is measured, not given a limit, and a long time is reported.

**The test switches.** In QEMU there is no RADV. `gpu_compositor_fail true` and `gpu_draw_fail true` make the first submit of the session's first device fail, whatever that device is; in QEMU the move then goes from Lavapipe to a new Lavapipe device, which runs the same code (steps 1–7). `gpu_lavapipe_fail true` makes every Lavapipe device creation fail, which tests step 3 of the ladder (one retry, then the failed start). On ARM64 and RISC-V the switches keep their M11 meaning (the switch to the CPU mode, decision 8).

**When Lavapipe fails too.** app_server tries once more; if that fails it writes the marker and logs `app_server: desktop start failed: <reason>`. What this boot shows after that, at the Sign in screen or in the middle of a session with programs open, the brief does not say. **Decided (user, 15 Nov 2026, Q6):** the CPU mode for the rest of this one boot, as a last resort, the way M11 switches today (`SetHWInterface(screen)`, every window redrawn by Painter), so the Sign in screen appears and open programs keep their work. One more line: `app_server: cpu mode until restart`. The next boot opens the menu with Safe mode selected. It cannot loop, because the menu waits for the user.

## 9. A buffer of their own for `BDirectWindow` and `BWindowScreen`? (decision 10, advice only)

**`BWindowScreen`: no.** It does not draw into a window. It takes the whole screen and the graphics card: it sets the display mode, clones the accelerant and uses its hooks, and reads the frame buffer configuration (`src/kits/game/WindowScreen.cpp`, `_GetCardInfo()`, the `get_frame_buffer_config` hook, `B_FRAME_BUFFER_CONTROL`). A buffer of its own would mean emulating an accelerant for it. The pause stays.

**`BDirectWindow`: possible, but not simple.** What a program gets is `direct_buffer_info` (`DirectWindowInfo::SetState()` in `DirectWindowInfo.cpp`): an area id (`bits_area`), which the program clones (`src/kits/game/DirectWindow.cpp`), `bytes_per_row`, the pixel format, and `window_bounds` and `clip_list` in **screen** coordinates. The program writes at `bits + y * bytes_per_row + x * bytes_per_pixel` for screen `x`, `y`. Three things would have to change:

1. **The layout.** Either the window's buffer is laid out like the screen down to the window's bottom edge (a screen-wide stride, up to a full screen of memory per direct window), or app_server reports the window at (0, 0) and translates the clip list, which changes what programs see and breaks one that mixes in screen coordinates from elsewhere.
2. **The memory.** The area must be cloneable by the program and readable by the Vulkan device. Lavapipe could import host memory (`VK_EXT_external_memory_host`). RADV would need the R5 DRM library and the kernel to import an area as a GPU buffer, which does not exist.
3. **The compose** must read that window again every frame, because a direct window reports no damage (like a window during an animation, M11 step 4b).

**Advice was:** not in M12. **Decided (user, 15 Nov 2026, Q10): build it for Lavapipe only, as an extra step in M12.** See [Decided after step 0](#decided-after-step-0-15-nov-2026). On RADV, and for `BWindowScreen` everywhere, the pause stays.

## 10. Changes to other documents

### `docs/GPU-ARCHITECTURE.md`

Made in this step (decision 1 says step 0 writes it): "2D in app_server" step 3 gets the all-or-nothing rule, the test switch and a pointer to this document. The sentence the brief replaces ("only if measurements show it is faster") is already gone from that step.

Proposed, made in the step that builds the thing, so the document never describes what is not there:

- **Step 1, "Components and versions":** a row `Skia | commit hash and date in the recipe (now chrome/m158, 707e33875f) | One pin per R5 release; static, only in app_server; update with the steps below`, and a section **"Updating Skia, step by step"**: (1) move the pin to the newest `chrome/mNNN` branch below beta, with its commit; (2) build it with the recipe; the `gn` arguments of part 1 must still exist, and the list of built third-party parts is checked against `DEPS` again; (3) read the release notes of every milestone in between for changes to Graphite, `VulkanBackendContext` and runtime effects; (4) the step 1 host test, then `r5-draw-compare` on Lavapipe in QEMU and on RADV on the 5625U, every line `ok`; (5) the `draw-time` list against the previous pin, recorded in `CHANGELOG.md`; (6) the previous pin stays in the recipe history; a rollback is one pin back.
- **Step 1, "Licensing":** "Skia (BSD-3-Clause, Google and the other authors in its `AUTHORS` file) with skcms (BSD-3-Clause) and the Vulkan Memory Allocator (MIT, AMD) is linked statically into app_server. No GPL code. Its notices are in `THIRD_PARTY_NOTICES.md` and on the Licenses tab of About."
- **Step 6, "Two modes, never mixed"** becomes: "On AMD64 the desktop is always composed: on RADV where the GPU is available, otherwise on Lavapipe. When RADV is lost and its restart fails, the session goes on on Lavapipe. When Lavapipe fails as well, the next boot offers Safe mode. The CPU mode (Painter, no compositor) runs only in Safe mode, during a direct-window pause, and for the rest of a boot in which Lavapipe failed as well. On ARM64 and RISC-V the CPU mode stays the automatic fallback until Lavapipe is proven there." And "When things go wrong", second point: "app_server goes on on Lavapipe (AMD64) or falls back to Painter (ARM64, RISC-V)".
- **After the stick:** the M12 row of "Milestones" with its result.

### `THIRD_PARTY_NOTICES.md`

Proposed for step 1, the change that puts Skia in the image (until then the file would list a component the image does not have). Under "Identified in this repository" or a new "Linked into app_server":

> ### Skia (in app_server)
>
> Skia, branch `chrome/m158`, commit `707e33875fb45f2997a4d781128bec122fb777b4`, built from R5's recipe and linked statically into `/system/servers/app_server`. Not changed by R5 (any patch would be listed here). Built with its Graphite Vulkan backend only; no codecs, no ICU, no HarfBuzz.
>
> | Piece | Project | License |
> |---|---|---|
> | Skia core and Graphite | skia.googlesource.com/skia ("Copyright (c) 2011 Google Inc." in `LICENSE`; files by Google Inc., Google LLC, The Android Open Source Project and others, see `AUTHORS`) | BSD-3-Clause |
> | skcms | `modules/skcms` in Skia ("Copyright (c) 2018 Google Inc.") | BSD-3-Clause |
> | Vulkan Memory Allocator 3.4.0 | GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator ("Copyright (c) 2017-2026 Advanced Micro Devices, Inc.") | MIT |
> | Vulkan headers (compile time only) | Khronos Group, in Skia's `include/third_party/vulkan` | Apache-2.0 |
>
> A release ships the three licence texts with this list and on the Licenses tab of About. No source offer is required.

And one block in `distro/about/licenses.txt` with the same three parts.

### `AGENTS.md` (proposed text; the user makes the change)

In the GPU section, after "The software path (app_server Painter, llvmpipe, Lavapipe) always stays installed and working.":

> On AMD64 the desktop is always composed: on RADV when the GPU is available, otherwise on Lavapipe; the GPU draws every window through Skia (`docs/APP-SERVER-GPU-DRAWING.md`). When RADV is lost and its restart fails, the session goes on on Lavapipe. The CPU mode (Painter, no compositor) runs only in Safe mode, during a direct-window pause (a `BWindowScreen`, or a `BDirectWindow` on RADV), and for the rest of a boot in which Lavapipe failed as well; that boot writes the marker. Safe mode is Haiku's safe mode plus the fail-safe video mode plus no GPU driver, with "Safe mode" on the screen. After a failed desktop start app_server writes `/boot/system/var/r5/desktop-start-failed`; the boot loader then opens the menu with Safe mode selected; Login removes the file after a good Sign in in a normal start. Painter stays in every image and must keep working in Safe mode. ARM64 and RISC-V keep the automatic switch to the CPU mode until Lavapipe is proven there. Skia is linked only into app_server, statically, from R5's own recipe at a pinned version; never in a public header, `libbe` or a `libr5*` library.

## Open questions for the user

Each with the options and my advice. Q1 is the gate for step 1.

**Q1. Do you approve Skia as reported in part 1?** Skia m158 (commit `707e33875f`), only the core and Graphite on Vulkan, with skcms and the Vulkan Memory Allocator, no codecs, no ICU, no HarfBuzz, linked statically into app_server.
- (a) Yes, as reported.
- (b) Yes, but a different milestone (for example the newest, m159, which is still beta).
- (c) No.
- **Advice: (a).** BSD-3 and MIT ask only for the notices; nothing has to be shipped as source; the build stays small.

**Q2. Graphite rather than Ganesh?**
- (a) Graphite: windows keep drawing in parallel, and every submit stays on the compose thread; but it is the younger backend.
- (b) Ganesh: more mature, but all windows draw one at a time through one lock, and the queue needs a second lock against the compose.
- **Advice: (a).** The way app_server draws (one thread per window) is what Graphite was made for. The risk of a young backend is caught by the compare set and the fallback.

**Q3. Shapes with curves: GPU only, or Skia's small-shape shortcut?** For general shapes (polygons, curves, `BShape`) Graphite can either draw on the GPU with 4× multisampling (edges in steps of a quarter), or make small ones on the CPU into a coverage atlas (finer edges, closer to Painter).
- (a) GPU only, 4× on RADV and Lavapipe (part 2).
- (b) Allow the small-shape atlas: finer edges, but that coverage is made on the CPU by Skia (letters are made on the CPU by FreeType anyway, decision 5).
- (c) 8× on RADV and 4× on Lavapipe: finer on the GPU, but the two give different pictures.
- **Advice: (a) for M12**, with the looser A2 tolerance; M13 can look at (b) with numbers.

**Q4. Twice the window memory.** Each window gets a GPU image besides the M11 buffer, so the memory budget is reached with about half as many windows open.
- (a) Accept it in M12 and measure; M13 lets the compose read the images directly.
- (b) Change the compose shaders in M12 to read images, so there is one copy.
- **Advice: (a).** It keeps the M11 compose byte-identical, so a compare failure in M12 is always the drawing, never the compose.

**Q5. Subpixel text: Haiku's "average weight" or Skia's own filter?** Skia's own subpixel text uses FreeType's colour filter and has no "average weight" slider.
- (a) Two passes: Skia's glyph cache draws grey letters at three times the width, and R5's shader applies Haiku's average weight and order. The Antialiasing tab means the same in both modes.
- (b) Skia's own subpixel text: simpler and faster, but the "average weight" setting does nothing in the GPU mode, and letters look a little different from the CPU mode.
- **Advice: (a).** It stays within decision 5 (Skia's glyph cache on FreeType) and keeps your setting working.

**Q6. When Lavapipe fails too, what does this boot show?** (A conflict with the brief: decision 10 says the CPU mode runs only in Safe mode, and does not say what happens until the next boot.)
- (a) The CPU mode for the rest of this one boot, as a last resort, with the marker written: at the Sign in screen you can still sign in, and in a session your open programs keep their work. The next boot offers Safe mode.
- (b) Restart the computer at once into the boot menu: no CPU mode outside Safe mode, but unsaved work in open programs is lost.
- (c) Stop drawing until you turn the computer off.
- **Advice: (a).** Losing open work is worse than one session on the CPU, and it cannot loop, because the next boot asks. The proposed `AGENTS.md` text would then say "only in Safe mode, during a direct-window pause, and for the rest of a boot in which Lavapipe failed".

**Q7. Does the boot menu wait for you, or count down?**
- (a) Wait for a key, as Haiku.
- (b) Count down 10 seconds, shown in the line, then start in Safe mode; any key stops the count.
- **Advice: (b).** A computer nobody is watching (or the stick test) still reaches a desktop.

**Q8. Who removes the marker when you stay in Safe mode?**
- (a) Only a good Sign in in a normal start (part 8): the menu keeps offering Safe mode until a normal start has worked.
- (b) Also a Sign in in Safe mode: the next boot is normal again, and if it fails, the menu comes back.
- **Advice: (a).** After a rollback to an older state you choose "Start normally" once, and that clears it.

**Q9. Network in Safe mode?** Haiku's safe mode does not start the network (net_server is `no_safemode`).
- (a) Keep Haiku's behaviour: no network in Safe mode.
- (b) Start the network in R5's Safe mode, for example to download a fixed package.
- **Advice: (a)** for M12: the older system state needs no network, and it keeps Safe mode Haiku's. (b) can come later on its own.

**Q10. A buffer of their own for `BDirectWindow` (part 9)?**
- (a) Not in M12; the direct-window pause stays, written down as the exception.
- (b) Build it for Lavapipe only, as an extra step.
- **Advice: (a).** It is not simple, it needs new kernel work for RADV, and few programs use direct access.

**Q11. Offscreen drawing lost when the GPU is lost.** If the GPU is lost, drawing into a `BBitmap` since its last `Sync()` is gone; the bitmap keeps what it had at that point (part 6). Haiku has no such case.
- (a) Accept it as a written limit.
- (b) Read every bitmap back after every batch, so nothing is ever lost, at a cost for every program that draws offscreen.
- **Advice: (a).** It only happens after a GPU loss, and the program's next drawing fixes it.

## Decided after step 0 (15 Nov 2026)

The user answered the open questions:

| Question | Decision |
|---|---|
| Q1 Skia | Approved as reported in part 1: m158, core and Graphite on Vulkan, skcms and VMA, no codecs, no ICU, no HarfBuzz, static in app_server. Step 1 may start |
| Q2 Backend | Graphite |
| Q3 Curved shapes | GPU only, 4× MSAA on RADV and Lavapipe |
| Q4 Window memory | Accepted and measured in M12; M13 lets the compose read the images |
| Q5 Subpixel text | **Skia's own subpixel text**, one way on RADV and Lavapipe, a new look in the GPU mode; part 6 is changed. `gSubpixelAverageWeight` only acts in Safe mode |
| Q6 Lavapipe fails too | The CPU mode for the rest of that one boot, the marker written; part 8 and the proposed `AGENTS.md` text are changed |
| Q7 Boot menu | Waits for a key, as Haiku; no countdown |
| Q8 Marker in Safe mode | Removed only after a good Sign in in a normal start |
| Q9 Network in Safe mode | None, as Haiku |
| Q10 `BDirectWindow` | **Built for Lavapipe only, as an extra step in M12** (below) |
| Q11 Offscreen loss | Accepted as a written limit |

**`BDirectWindow` on Lavapipe (Q10), the design on paper.** From part 9:

- **Layout:** the window's direct buffer is laid out like the screen (the screen's `bytes_per_row`), from screen row 0 down to the window's bottom edge, so `window_bounds` and `clip_list` stay in screen coordinates and programs see nothing new. This costs up to a screen of memory per direct window, counted in the window memory budget.
- **Memory:** a Haiku area made cloneable (as `DirectWindowInfo::SetState()` does for the frame buffer), imported into the Lavapipe device with `VK_EXT_external_memory_host`. Step 1 checks that Lavapipe in Mesa 25.3.6 offers that extension; if not, this step is reported as blocked.
- **Compose:** a direct window reports no damage, so while one is active, the compose reads its rows every refresh period, as during an animation (M11 step 4b).
- **Where:** in the GPU mode on Lavapipe only. On RADV, and for `BWindowScreen` everywhere, the M11 pause stays.
- **Which step:** its own step and PR, after step 6 and before the stick, because it touches the compositor and the direct-window path, not the drawing. The brief still lists it under "Not in M12"; the user updates the brief, or gives the step its number.
- **Pass line, proposed:** with a `BDirectWindow` program on Lavapipe in QEMU, no `compositor paused: direct window`, its pixels in the composed frame, and `app_server: direct window buffer <w>x<h>` once.

**Two more, answered the same day:**

- **Q12. Subpixel shapes: grey in the GPU mode** (the advice). Skia has subpixel coverage only for text; shapes are drawn in grey, text keeps Skia's subpixel coverage (Q5). Painter keeps its subpixel shapes in Safe mode. Part 4 and compare group 10 are changed.
- **Q13. The "average weight" slider: hidden when Skia draws the text** (user, as a Settings change of its own, so it does not touch the fixed layout rules of `AGENTS.md`). The rule is "who draws the letters", not "Safe mode": Painter also draws outside Safe mode (while `gpu_draw` is off during M12, on ARM64 and RISC-V, in the last-resort boot of Q6), and there the slider works and stays.
  - **Where:** `distro/settings/AntialiasingSettingsView.cpp`, the one source of the Antialiasing tab in Settings and in the separate Appearance program. The slider and its label are hidden, and the rest of the tab closes up; no empty space. Nothing else on the page moves.
  - **How it knows:** a new call at the end of `libr5ui` that asks app_server whether the GPU draws the text (not in `libbe`), described in `docs/LIBR5.md` in the same change.
  - **When:** step 4 (text), because the GPU drawing exists only then. Test: the slider is gone with `gpu_draw true` and back without it, in QEMU.

## Scaling later: what M12 must not block (15 Nov 2026)

`docs/ROADMAP.md` says scaling (HiDPI, fractional and per-monitor) "is designed together with the GPU plan for app_server". The scaling design itself is a brief of its own, later, with its own questions for the user. M12 builds nothing that a user sees for it. It only keeps the way open, so scaling does not mean rebuilding the M12 drawing.

**One path for RADV and Lavapipe.** Scaling will sit where the M12 drawing sits: in Skia and the compositor. Both run the same way on RADV and on Lavapipe, so a scaled window looks the same with and without a GPU. Only the speed differs: more pixels cost more time, and on Lavapipe the CPU does that work. That is measured, as in M12, and is not a reason to change the path. **Safe mode** draws with Painter, which does not scale sharply; there only Haiku's existing way works (larger letters through the font size, and the decorator, which already grows with the font size).

**What M12 keeps open, at no cost now:**

1. **A scale per window in `SkiaDrawingEngine`.** One factor (a float, always 1 in M12), applied as the outermost matrix of the canvas, before the view's transform and the renderer offset. The copied Painter alignment helpers (part 4) take that factor, so the alignment to whole pixels happens in device pixels.
2. **Text at device size.** The `SkFont` size is the `ServerFont` size times that factor, so letters are rasterised at their real size, not stretched. Metrics stay `ServerFont`'s, in the program's units, so layout does not change.
3. **Bitmaps** go through the same `DrawBitmap()` path under that factor (filtered when it is not 1).
4. **Window image size.** The window image and buffer are sized from the frame times the factor. In M12 that is the frame itself.
5. **The compose.** M11 composes a window at the size of its buffer. M12 does not change that; the scaling brief adds a layer whose buffer size and screen size differ.
6. **The compare set** gets an option `--scale <f>` (not part of the M12 pass lines): Skia drawing at 1.5× is compared with Painter drawing under a `BAffineTransform` scale of 1.5, on RADV and on Lavapipe, so the scaled path is checked from the start.

**Not in M12:** a setting, scaling per monitor, mapping the mouse and window positions, `BScreen` sizes, `BDirectWindow` and `BWindowScreen` under a scale, larger icon and bitmap variants.

**Questions for the scaling brief, when you ask for it:** how programs see the scale (Haiku's font-size way, the compositor drawing a whole window larger, or a mix); what happens to programs with fixed pixel sizes; where larger icons come from (Haiku's vector icons help); fractional factors such as 1.25 and 1.5; one factor per monitor.

## Follow-up: the look and feel settings (user, 15 Nov 2026)

Not designed here. The user wants it designed in step 1 or later.

- **Scaling gets a setting** in the Settings window, when scaling is built. Which page it goes on is a question for the scaling brief.
- **The look and feel settings need a redesign**, in the Settings window and in the separate Appearance program, for everything added since Haiku: the "Window effects" box (shadow, frosted glass, strength, animations, M11), the antialiasing choices after Q5, Q12 and Q13 (Skia's subpixel text, grey shapes, the hidden average weight slider), and later scaling. The shared sources are in `distro/settings/` (`LookAndFeelSettingsView.cpp`, `WindowEffectsBox.cpp`, `AntialiasingSettingsView.cpp`, `FontView.cpp`, `ColorsView.cpp`, `LookModule.cpp`, `FontsModule.cpp`).
- **Seen while reading, to settle first:** `AGENTS.md` describes one Appearance page with the tabs Fonts, Colors, Look and feel and Antialiasing, while `docs/SETTINGS.md` describes two pages, Fonts and Look and Feel, with no tabs, Antialiasing and Colors as boxes. The two disagree. The redesign starts by asking the user which one holds; neither is changed here.
- The fixed Settings layout in `AGENTS.md` stays as it is until the user approves the redesign.
