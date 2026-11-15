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

