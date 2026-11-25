<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright 2026 R5OS Contributors -->

# Aomi on a new engine: Ladybird, Gecko or Servo

Exploration only, 25 Nov 2026. No code, no build, no port attempt. Nothing here is on the roadmap until the owner says so.

## The plan as the owner framed it

- **Aomi** stays the R5 browser name, but the engine changes from haikuwebkit to a newer one (Ladybird was the first candidate; Gecko and Servo were added later the same day). The Aomi of today (a copy of WebPositive on `haikuwebkit-1.10.0-2`, `docs/AOMI.md`) is not developed further on WebKit.
- **WebPositive stays** in the image, unchanged, as Haiku's own browser on haikuwebkit.
- **Firefox** is what people use until Aomi on Ladybird is ready.

## Summary

Ladybird has become much easier to port than it was in 2024. Swift is gone from the build except for one optional macOS file. The IPC is Unix sockets with fd passing, and the shared memory code still has a Haiku branch. Everything that is not Linux or macOS already builds without a sandbox. The hard parts are elsewhere:

1. **Toolchain:** C++23 with GCC 14 or Clang 19 as the minimum, plus **Rust** (about 2,200 `.rs` files, required). R5 builds with GCC 13.3.
2. **Dependencies:** about 45 libraries. HaikuPorts has most of them. Eight are missing (Skia, ANGLE, PulseAudio, simdutf, cpptrace, fast_float, wuffs, zmij), and several are older than Ladybird's pins (Qt, ICU, FFmpeg).
3. **Front end:** Ladybird has only one UI now, Qt. A native `BWindow`/`BView` front end is new work against an embedder API that the project itself has not documented yet.

The owner then asked about other engines. "Other engines behind a native Aomi window" compares Gecko and Servo with Ladybird and WebKit2. The recommendation (end of this document) is **Servo first, with Gecko as the fallback, and Ladybird watched**: one proof round that shows a page in a `BServoView` on R5, after Firefox has been checked on R5 as the stopgap.

## 1. Languages and toolchain

| Item | Ladybird needs | R5 / Haiku has | Source |
|---|---|---|---|
| C++ standard | C++23, required | GCC 13.3 cross compiler for the image (C++23 partly) | `Meta/CMake/compile_options.cmake` (`CMAKE_CXX_STANDARD 23`); `docs/GPU-ARCHITECTURE.md` (Skia recipe, GCC 13.3.0) |
| Compiler | Clang ≥ 19 or GCC ≥ 14; CI uses gcc-16 and clang-22 | HaikuPorts: `gcc-15.3.0_2026_10_11`, `llvm22-22.1.8`, `llvm23-23.1.6` | `Meta/Utils/find_compiler.py`; `haikuports/sys-devel/gcc`, `sys-devel/llvm` |
| Swift | One file, `UI/Qt/TranslationServiceMac.swift`, macOS 26.4 SDK only (page translation). No `ENABLE_SWIFT`. | `swift_lang-5.9` recipe (too old, and unused anyway) | `UI/Qt/CMakeLists.txt` (`check_language(Swift)`) |
| Rust | Required: about 2,137 `.rs` in `Libraries/`, 58 in `Services/`; LibIPC fd passing is Rust | `rust_bin-1.94.1`, `rustup-1.28.2` in HaikuPorts; R5 has no Rust in its own build | `Documentation/BuildInstructionsLadybird.md`; `Meta/CMake/rust_crate.cmake`; `Libraries/LibIPC/Rust/src/socket.rs` |
| CMake | 3.25 in `CMakeLists.txt`, docs say ≥ 3.30 | `cmake-4.5.0` | build docs |
| nasm | yes | `nasm-2.16.03` | build docs |

So the Swift worry of 2024 is gone (Ladybird announced Swift in Aug 2024 and Haiku's Swift port was disabled; the Haiku forum thread, Aug 2024). Rust took its place. A Rust crate that uses `libc` must know Haiku; most do (`target_os = "haiku"` is a Rust tier-3 target), but each crate in Ladybird's lockfile has to build. Not checked.

The compiler is a real question. A package built with GCC 15 needs the matching `libstdc++` at run time (`gcc_syslibs`). The R5 image ships the GCC 13 one. Either the image moves to GCC 15 system libraries (a change for the whole system, with its own testing), or Ladybird is built with Clang 22 against the GCC 13 `libstdc++` (needs C++23 library features that GCC 13's `libstdc++` may lack), or it ships its own `libc++`. Which of these works is not known.

## 2. Dependencies

From `vcpkg.json` (overrides, pinned versions) and the Linux build docs, matched against the HaikuPorts tree on `master` (25 Nov 2026). "R5" means R5 builds it itself.

| Library | Pin | HaikuPorts / R5 | License (SPDX) |
|---|---|---|---|
| Qt (qtbase, qtsvg, qtpositioning, qtserialport) | 6.11.3; ≥ 6.10 required | `qt6_*-6.10.3` | LGPL-3.0-only OR GPL-2.0-or-later OR GPL-3.0-only (commercial aside) |
| Skia | 148, Ganesh, Vulkan, shared | **missing**; R5 has Skia m158, **Graphite only, Ganesh off, static, app_server only** | BSD-3-Clause |
| ANGLE | chromium_7258 | **missing** | BSD-3-Clause |
| ICU | 78.3 | `icu79-79.1` (newer, likely fine) | Unicode-3.0 |
| FFmpeg | 9.0.2 | `ffmpeg8-8.1.3` (older) | LGPL-2.1-or-later (GPL-2.0-or-later if GPL parts are on) |
| curl | 8.23.0 | `curl-8.23.0` | curl |
| OpenSSL | 3.6.5 | `openssl3-3.5.9` (older) | Apache-2.0 |
| nghttp2 | 1.71.0 | `nghttp2-1.63.0` (older) | MIT |
| SQLite | 3.54.1 | `sqlite-3.53.4.0` | blessing (public domain) |
| HarfBuzz | 10.2.0 (pinned for Qt) | `harfbuzz-15.0.0` (newer) | MIT-Modern-Variant |
| FreeType | 2.13.3 (pinned) | `freetype-2.14.3` | FTL OR GPL-2.0-or-later |
| fontconfig | 2.17.1 | `fontconfig-2.18.2` | HPND-sell-variant |
| libpng / libjpeg-turbo / libwebp / tiff | 1.6.59 / 3.2.1 / 1.6.0 / 4.7.2 | all present | libpng-2.0 / IJG AND BSD-3-Clause AND Zlib / BSD-3-Clause / libtiff |
| libavif / dav1d | 1.4.2 / 1.5.4 | `libavif1.0-1.4.2`, `dav1d-1.5.4` | BSD-2-Clause / BSD-2-Clause |
| woff2 / brotli / zlib | 1.0.2 / 1.2.1 / 1.4 | all present | MIT / MIT / Zlib |
| libpsl | 0.23.3 | `libpsl-0.21.5` (older) | MIT |
| simdjson | 5.1.1 | `simdjson-3.11.5` (two majors behind) | Apache-2.0 OR MIT |
| simdutf | 9.5.0 | **missing** | Apache-2.0 OR MIT |
| fast-float | 8.3.3 | **missing** (header-only) | Apache-2.0 OR MIT OR BSL-1.0 |
| wuffs | 0.3.5 | **missing** (single file) | Apache-2.0 OR MIT |
| zmij | 1.2 | **missing** | not checked |
| cpptrace / libdwarf | 1.0.4 / 2.3.3 | cpptrace **missing**; `libdwarf-0.11.1` (old) | MIT / LGPL-2.1-only |
| mimalloc | 2.2.7 | `mimalloc-2.1.2` | MIT |
| md4c / libedit | 0.5.3 / 2024-08-08 | `md4c-0.5.2`, `libedit-20260512_3.1` | MIT / BSD-3-Clause |
| SDL3 | 3.4.20 | `libsdl3-3.4.16` | Zlib |
| Vulkan headers / loader / VMA / glslang | 1.4.363 / – / 3.5.0 / 16.4.0 | **R5**: loader 1.4.367 (`distro/packages`); HaikuPorts `vulkan_loader-1.4.367`, `glslang-16.0.0` | Apache-2.0 / Apache-2.0 / MIT / BSD-3-Clause and others |
| libdrm | system | **R5 DRM library** (`libdrm-2.4.124-3`), HaikuPorts none | MIT |
| PulseAudio | system (AudioServer) | **missing**; Haiku uses the Media Kit | LGPL-2.1-or-later |
| D-Bus | 1.16.4 | `dbus-1.16.2` | AFL-2.1 OR GPL-2.0-or-later |
| pdf.js | 6.5.438 | not a package (JavaScript) | Apache-2.0 |
| llvm-libc (part) | git | not a package | Apache-2.0 WITH LLVM-exception |
| mman, pthread, dirent | – | Windows only | – |
| Sparkle | 2.10.1 | macOS only | – |
| Rust crates (Cargo lockfile) | many | build from source | **not checked**; needs `cargo deny` or similar |

The license column comes from each project's known license, not from a file-by-file reading of the pinned versions. It must be checked against the pinned sources before anything ships (`docs/LICENSING.md`, "Third-party code").

What is missing is mostly small (header-only or single-file libraries). The big ones are **Skia with Ganesh** (one more recipe next to R5's own), **ANGLE** (only if the GL path is wanted; Vulkan does not need it), and **PulseAudio**, for which the AudioServer needs a Media Kit backend instead (`Services/AudioServer` already has PulseAudio, CoreAudio and WASAPI backends, so the pattern is there).

## 3. Processes, IPC and sandbox

`Services/` today: WebContent (one per tab), RequestServer (network), ImageDecoder, Compositor (the only process that links Skia, checked by `Meta/CMake/check_skia_confinement.cmake`), AudioServer, MediaServer, VideoDecoderServer, WebRTCClient, Hub (between the UI and the helpers), SystemProbe, WebDriver, and ProcessReaper (macOS only).

| Need | Ladybird | R5 / Haiku | Work |
|---|---|---|---|
| Unix domain sockets, stream | `Libraries/LibIPC/TransportSocket.cpp`; `Documentation/Porting.md`: any platform with them "should be able to use the IPC system out of the box" | yes | none expected |
| fd passing (`SCM_RIGHTS`) | Rust, `Libraries/LibIPC/Rust/src/socket.rs` | Haiku supports `SCM_RIGHTS` on `AF_UNIX` | test on R5; the Rust crate must build for Haiku |
| `AF_UNIX` + `SOCK_DGRAM` | used by the 2023 port, which failed with "sendmsg: Socket is not connected" | PulkoMandy, 28 Aug 2023: not possible on Haiku then | check whether today's code still needs it, and whether R5's network stack has it |
| Anonymous shared memory | `Core::System::anon_create`, `Libraries/LibCore/System.cpp`: `memfd_create`, else `shm_open(SHM_ANON)`, else a named `shm_open` under `AK_OS_BSD_GENERIC \|\| AK_OS_HAIKU` | `shm_open` yes; no `memfd_create` | the Haiku branch is still there |
| Sandbox | `Libraries/LibSandbox`: seccomp + Landlock on Linux, Seatbelt on macOS; `SandboxUnimplemented.cpp` for the rest | no seccomp, no Landlock, no pledge | runs **without a sandbox** on R5 |
| Process spawn and lifetime | `Core::Process`; Hub watches the UI | `posix_spawn`, `fork` | Hub and the reaper behaviour must be checked so that helpers die with the browser |
| Haiku in the code | `AK/Platform.h` (`AK_OS_HAIKU`), `LibCore/System.*`, `LocalServer`, `TCPServer`, `StandardPaths`, `DirIterator`, `Services/RequestServer/CMakeLists.txt`, `LibGfx/Font/FontDatabase.cpp` | – | the 2023 Haiku code partly survives; whether it still compiles is not known |

No sandbox is the main security gap. A WebContent process that a page takes over can do whatever the signed-in user can do. R5 has no seccomp-like facility. Building one is kernel work, far bigger than the browser port.

Multi-user: helpers run as the signed-in user (`docs/MULTIUSER.md`), with the profile under the user's home through `find_directory`. Ladybird's `StandardPaths` would need the Haiku mapping checked. A guest session (wiped at log out) must not leave a profile behind.

## 4. Drawing and the GPU

- Ladybird uses **Skia Ganesh** (`GrDirectContext`, `Services/Compositor/SkiaBackendContext.cpp`), no Graphite. Backends: Vulkan (`Libraries/LibGfx/VulkanContext.cpp`, `Services/Compositor/SkiaVulkanMemoryAllocator.cpp`), Metal, Direct3D, and OpenGL ES through ANGLE (`Services/Compositor/OpenGLContext.cpp`). `HAS_VULKAN` is set when the Vulkan headers and loader are found (`Meta/CMake/check_for_dependencies.cmake`). Linux shares frames as dmabuf (`USE_VULKAN_DMABUF_IMAGES`, Linux and BSD only). Without a GPU it most likely draws on the CPU (Skia raster); not checked.
- R5's Skia is a different build: m158, **Graphite** only, Ganesh off, static, linked only by app_server (`docs/GPU-ARCHITECTURE.md`, M12; `docs/APP-SERVER-GPU-DRAWING.md`, `gn` arguments). That rule stays. So Ladybird gets **its own Skia** (Ganesh, Vulkan, Ladybird's pinned version) as a separate recipe. The two do not share code. They meet only at the Vulkan driver.
- On R5 the Ladybird Compositor would use Vulkan through the normal loader: **RADV** on a supported GPU, **Lavapipe** otherwise, the same order as app_server. It needs no new kernel call. GL through ANGLE is not needed. Zink is the R5 GL route if it ever is.
- The missing piece is **getting the frame on screen**. Linux hands the UI a dmabuf. R5 has no dmabuf sharing between processes into app_server. Step one is a copy: the Compositor reads the frame back into shared memory, and the front end draws it as a `BBitmap` in a `BView`. That works on every R5 machine and costs a copy per frame. A zero-copy path (an exported Vulkan image that app_server composes directly) would be a new R5 GPU call, additive under `docs/GPU-ARCHITECTURE.md`, and comes later, if the copy is too slow.

## 5. The front end

Today `UI/` has only `Qt/` (213 files, 202 of them `.cpp`/`.h`). The AppKit, Android, Headless and GTK front ends are gone. `Documentation/Porting.md`: one supported UI port, Qt, on all platforms. An embedder subclasses `WebView::Application` and `WebView::ViewImplementation` (`Libraries/LibWebView/Application.h`, `ViewImplementation.h`). `Porting.md` still has a TODO for the embedder details.

| Route | What it is | For | Against |
|---|---|---|---|
| **A. Qt front end** | Build `UI/Qt` against HaikuPorts Qt 6 (QPA for Haiku) | Least work, the front end the project tests, the fastest proof that the engine runs | Not a BeOS app: Qt look, no R5 frame or Appearance Kit, no Deskbar or Tracker integration. Qt 6.10.3 meets "≥ 6.10" but is behind the 6.11.3 pin |
| **B. Native Aomi front end** | `BApplication`, `BWindow`, `BView`, R5 Appearance Kit and libr5ui/libr5icons, implementing `WebView::Application` and `ViewImplementation` | A real R5 app: R5 frame, glass with the no-GPU fallback, R5 icons, Settings and Deskbar links, `application/x-vnd.R5-Aomi` | Front end work is new. It follows an embedder API that moves often and has no docs. 3dEyes had a native Haiku UI in Sept 2023 (forum thread), which shows it can be done, against older code |

Route B can reuse much of today's Aomi window: tabs, URL bar, bookmarks (the `WebPositive` bookmark folder stays), downloads and settings are mostly engine-neutral BeOS code. What is tied to `BWebView`/`BWebPage` (the listener calls in `docs/AOMI.md`) gets rewritten against `ViewImplementation`.

## 6. Licenses and what shipping requires

- Ladybird: **BSD-2-Clause**, "Copyright (c) 2018-present, the Ladybird developers" (root `LICENSE`). `Meta/CMake/lgpl-libraries.txt` lists the LGPL parts.
- Copyleft parts: **Qt** (LGPL-3.0-only, route A only), **FFmpeg** (LGPL-2.1-or-later if built without the GPL parts), **libdwarf** (LGPL-2.1-only), **PulseAudio** (LGPL-2.1-or-later, not needed with a Media Kit backend), **D-Bus** (AFL-2.1 OR GPL-2.0-or-later), **FreeType** (FTL chosen). Everything else is permissive (table in section 2). The Rust crates are not checked.
- None of these is combined into an R5 MIT file. Ladybird is a separate program. A native front end (route B) is R5 code under MIT, linking BSD-2-Clause libraries, which is fine.
- What `docs/LICENSING.md` asks before adding it: project, SPDX license with `-only` or `-or-later`, copyright, how it is integrated, source-offer duties, combination questions. Here that means: an LGPL library stays a shared library that users can replace; source for Qt, FFmpeg and libdwarf is offered as their licenses require; FFmpeg is built without GPL parts; `THIRD_PARTY_NOTICES.md` and the Licenses tab of About list Ladybird and every library it brings. LGPL-3.0-only (Qt) next to GPL-2.0-only code in one work is not allowed, but that does not happen inside a browser package.
- Name and logo: "Ladybird" is the project's name. The R5 product is called **Aomi** and does not use the Ladybird logo; About Aomi credits "based on Ladybird" with its license.

## 7. Web compatibility and security of the alpha

Stated by the project (https://ladybird.org/posts/alpha/, Andreas Kling, 23 Nov 2026):

- macOS and Linux only. No other platforms and no platform roadmap in the post.
- No test pass rates and no list of working sites. The post says "some websites won't work, and it will crash sometimes."
- Security: "we make no promises about security today." It invites security researchers to break it. Reports go through `SECURITY.md`. A rolling alpha channel with automatic updates. WebDriver and the Firefox DevTools protocol work.

Compared with haikuwebkit (today's Aomi and WebPositive, WebKit 625.1.25, single process, `docs/AOMI.md`): this session found no source that tests the same sites on both, so no site-by-site list here. What can be said honestly: haikuwebkit is a mature engine on an older release, with Haiku-specific gaps (one process, so one bad page takes down the browser, and no accelerated compositing). Ladybird is a new engine that the project calls an alpha that crashes, but it has its own multi-process model (a crashing tab does not take the window down) and follows the standards quickly. On R5, Ladybird runs **without a sandbox**, so its security would be lower than on Linux, where it already makes no promises. haikuwebkit on R5 has no sandbox either.

## Firefox as the stopgap

Firefox runs on Haiku today as a native port:

- HaikuPorts recipe `www-client/firefox/firefox-160.0.1.recipe`, `UI_BACKEND="NATIVE"` (cairo-haiku), x86_64 only, `LICENSE="MPL v2.0"`; last change 20 Nov 2026 ("bump version, switch to mozjemalloc allocator"). A GTK3 + Wayland build is still possible (`mozconfig_gtk`) but is not the default.
- Earlier it went through GTK3 and Wayland (OSNews, "Haiku gets tentative Firefox port", https://www.osnews.com/story/140473/haiku-gets-tentative-firefox-port/). The native backend was announced in the Haiku forum (https://discuss.haiku-os.org/t/progress-on-porting-firefox/13493); the recipe shows that it is the default now.
- Mozilla gave Haiku permission to use the Firefox name (https://discuss.haiku-os.org/t/mozilla-officially-gave-us-permission-to-use-the-firefox-name-on-haiku/19582). Before that it was "Iceweasel".
- **On R5 it is not tested.** It is not in the R5 image, and `docs/ROADMAP.md:161` keeps packages such as Firefox out of the source tree. HaikuDepot is being replaced by Appsolutely, so how an R5 user installs it today (the HaikuPorts repository through `pkgman`) should be checked, together with a start of Firefox on R5 in QEMU, before the owner relies on it.

## What happens to today's Aomi and to WebPositive

- **WebPositive** stays in the image as it is today: Haiku's source, haikuwebkit, with R5's existing fix in `distro/apps/webpositive` (Haiku #20319, `docs/AOMI.md`). Nothing changes for it.
- **Today's Aomi** (the WebPositive copy, `docs/AOMI.md`): its R5 work that is not WebKit-specific (name, signature `application/x-vnd.R5-Aomi`, R5 frame and glass, icons, per-user paths, settings form, tabs, bookmarks, downloads) moves into the native Ladybird front end (route B). The WebKit2 round (option D in `docs/AOMI.md`) stops being Aomi's engine plan; whether WebPositive later moves to WebKit2 is a separate question for the owner.
- Until Aomi on Ladybird can do what the WebPositive copy did, the image keeps WebPositive as its browser and Firefox is the stopgap. The Ladybird-based Aomi first ships as an **installable alpha** (Appsolutely, or a package in the R5 repository), not in the image. It goes into the image once it passes a check the owner sets (for example: starts on the 5625U and in QEMU, a fixed set of sites loads, no crash in a fixed session).

## Steps, in order

Cost in AI sessions. Testing is the bottleneck, so each step ends with a test on R5 (QEMU first, then the 5625U where the GPU matters).

| # | Step | Cost | Test |
|---|---|---|---|
| 0 | Check Firefox on R5 (install from HaikuPorts, start, a few sites) | 1 | QEMU; one 5625U boot if it matters |
| 1 | Toolchain decision: GCC 15 system libraries vs Clang 22 against GCC 13 `libstdc++`, plus Rust for Haiku; a small C++23 + Rust test program on R5 | 1–2 | QEMU |
| 2 | Recipes for the missing and too-old libraries (simdutf, fast_float, wuffs, zmij, cpptrace, libdwarf 2, simdjson 5, Skia Ganesh with Vulkan, newer Qt if needed); FFmpeg without GPL parts | 3–5 | build only, then link test |
| 3 | Ladybird builds for Haiku x86_64 with the Qt front end, no sandbox; fix the Haiku code paths in LibCore and the Rust crates | 3–6 | build |
| 4 | Runs: IPC (fd passing, the `SOCK_DGRAM` question), shared memory, process lifetime; first page on CPU drawing | 2–4 | QEMU; this is where the 2023 port crashed |
| 5 | Audio and video: Media Kit backend for AudioServer, FFmpeg on R5 | 2–3 | QEMU, then 5625U speakers |
| 6 | GPU: Compositor on Vulkan, Lavapipe in QEMU and RADV on the 5625U, frame copied into a `BView` | 2–3 | QEMU + one 5625U boot |
| 7 | Package as an installable Ladybird (Qt) alpha; license notices | 1 | install on R5 |
| 8 | Native Aomi front end (route B), carrying over today's Aomi R5 work | 5–8 | QEMU per round, owner looks at it |
| 9 | Multi-user and guest: profile paths, helpers as the signed-in user, guest wipe | 1–2 | VirtualBox with two accounts |
| 10 | Keeping up with Ladybird: one pin per R5 release, like Skia and Mesa | 1 per update | the step 4–6 tests again |

Total to a usable Qt alpha (steps 1–7): about 14–25 sessions. To a native Aomi (steps 8–9): another 6–10. Ladybird moves fast (CI already on gcc-16 and clang-22), so each update may break the R5 build again.

## Risks

- **No sandbox on R5.** A browser that runs pages without isolation, from a project that makes no security promises yet. This is the biggest risk for shipping it to users.
- **Toolchain:** C++23 plus newer GCC system libraries may mean a system-wide `libstdc++` change, which touches every program.
- **Moving target:** upstream supports only Linux and macOS, with no CI for Haiku. Haiku patches may not be accepted upstream (it is community-driven, `Porting.md`), so R5 carries them itself and rebases often.
- **The 2023 crash:** the earlier port crashed in IPC (`sendmsg`/`recvmsg`) and never found out why (nipos, Oct 2024). It may come back.
- **Embedder API without docs** for the native front end.
- **Two Skia builds** on the system (app_server's Graphite m158 and Ladybird's Ganesh) mean two pins to keep up.
- **Web compatibility:** an alpha with crashes; no site-by-site comparison with haikuwebkit exists.

## Other engines behind a native Aomi window

Added 25 Nov 2026 at the owner's question. The idea is the same for every engine: Aomi is a native R5 app (`BWindow`, Appearance Kit, R5 icons) with one small R5 view that hosts the engine, as WebPositive does with `BWebView`. Read the same day; nothing built.

### Gecko (a "BGeckoView")

What exists on Haiku (HaikuPorts `www-client/firefox/firefox-160.0.1.recipe`, its `patches/firefox-160.0.1.patchset` and `additional-files/mozconfig_native`):

- A **native port**, `--enable-default-toolkit=cairo-haiku`, native since the commit "bump version (native UI enabled)", 30 Sep 2026. `widget/haiku/` has about 50 files and 7,800 lines: `nsWindow`, `HaikuWindow : BWindow`, `HaikuView : BView`, `nsAppShell`, clipboard, drag and drop, `BFilePanel`, IME, `BNotification`, printing. Audio is a new `cubeb_haiku.cpp` on the Media Kit. The whole patchset is one commit of about 19,000 lines in 253 files. Maintainer Gerasim Troeglazov (3dEyes), updated 3, 12 and 20 Nov 2026. It is one release behind (Firefox 161.0 came out 24 Nov 2026, `product-details.mozilla.org/1.0/firefox_versions.json`).
- **Drawing on the CPU:** `HaikuWindowBuffer` draws into double-buffered `BBitmap`s with a Skia draw target. WebGL goes through `GLContextProviderBGL`. Whether the compositor is software WebRender is not traced.
- **Multiprocess** most likely on: `GeckoChildProcessHost.cpp` uses the POSIX launcher for Haiku. IPC is Chromium's POSIX IPC over Unix sockets with `SCM_RIGHTS` (with a Haiku fix in `ipc_channel_posix.cc`). **No sandbox:** upstream builds it only for Linux, Windows and macOS. Not verified at run time.
- SpiderMonkey, Rust and Gecko's whole C++ stack **already build and run on Haiku** through this port.

What is missing is the **embedding layer**. Mozilla has no desktop embedding API in 2026. GeckoView is Android only (`firefox-source-docs.mozilla.org`, GeckoView architecture). Its structure fits, though: an empty chrome document (`mobile/shared/chrome/geckoview/geckoview.xhtml`) with one `<browser remote="true">`, about 40 JavaScript modules (`mobile/shared/modules/geckoview`, about 300 KB) and 34 actors (`mobile/shared/actors`), which are largely platform-neutral, plus an Android-only bridge (`widget/android`, Java API). For R5: reuse `mobile/shared`, write the bridge from `EventDispatcher` to `BMessage`, and reuse `widget/haiku` for the view. Every feature that lives in Firefox's JavaScript UI (permissions, downloads, form fill, session restore, extensions, devtools) needs a native counterpart in Aomi.

The precedent is **Sailfish OS EmbedLite** (`github.com/sailfishos/gecko-dev`, `embedding/embedlite/`, 89 patches): Gecko inside a native Qt view, maintained by a company. It is on ESR 115 while Firefox is at 161 and ESR 153, about three years behind. That is what an out-of-tree embedding costs over time. R5 would carry its layer on top of an out-of-tree Haiku port that is itself rebased every four weeks.

License MPL-2.0, per file: changed Mozilla files stay MPL with source offered; new R5 files (the bridge, Aomi) can be MIT. A modified build may not use the Firefox name or logos (`mozilla.org/en-US/foundation/trademarks/policy/`); it may say in words that it is based on Mozilla technology. Aomi is its own name, as LibreWolf and Floorp do it.

### Servo (a "BServoView")

Servo is further along than this document first said (owner's correction; checked):

- **Version 0.8.0**: crate on crates.io 7 Nov 2026, GitHub release 9 Nov 2026, the "October in Servo" post on 19 Nov 2026 (588 commits). Monthly releases since 0.1.0 (13 Apr 2026), an LTS line every six months (0.6.0 LTS, 29 Sep 2026; book `src/embedding/lts-release.md`).
- **Web Platform Tests**: about **67 %** of tests and **94 %** of subtests (`wpt.servo.org/scores.json`, run of 22 Nov 2026, 0.8.0), against about 24 % in April 2023. No official list of working sites.
- **Built to be embedded.** Crate `servo` (`components/servo`): `ServoBuilder`, `WebView` (`load`, `resize`, `notify_input_event`, `paint`), `WebViewDelegate` (new frame ready, URL, title, load state, dialogs, file picker, context menu, input method), `ClipboardDelegate`, `NetworkManager`. A **C API** exists (`ffi/capi`, crate `servo-capi`, "eventually also provide a stable ABI"), which suits a C++ `BView`. Rendering contexts in `components/shared/paint/rendering_context.rs`, among them `SoftwareRenderingContext` with `read_to_image()` for pixel readback.
- **Drawing:** WebRender on **OpenGL** through surfman (EGL; a Mesa surfaceless backend exists). No Vulkan path for WebRender; WebGPU uses wgpu with Vulkan. On R5 that means: start with the software context (llvmpipe) and copy the pixels into a `BBitmap`, later EGL on Zink over RADV. R5's EGL support for this is not checked.
- **Single process by default** (`opts.rs`: `multiprocess: false`, `sandbox: false`). `ipc-channel` falls back to an in-process channel on systems it does not know, so Haiku needs no IPC work to start. The cost is no site isolation and no sandbox (gaol is Linux and macOS only, "lightly reviewed").
- **Platforms:** Linux, macOS, Windows, Android, OpenHarmony; FreeBSD builds since March 2026 (PRs #43116, #43134; issues still open). **No Haiku code or issue** in the repository.
- **Toolchain:** Rust 1.99.1 (`rust-toolchain.toml`, minimum 1.88, edition 2024). HaikuPorts has `rust_bin-1.94.1`, and Haiku is a Rust tier-3 target, so the newer Rust has to be built. **No C++23 and no new GCC needed** for Servo itself.
- **Dependencies:** SpiderMonkey through `mozjs` 0.28 (mozjs-sys 153.8), built from source with Clang and Python; whether its build system and JIT know Haiku is not checked, but the Firefox port shows that SpiderMonkey runs on Haiku. FreeType and fontconfig are chosen per OS (`components/fonts/platform/mod.rs`), so Haiku needs its own branch there. GStreamer for media is optional and off by default. Unix crates to check: surfman, nix, mio, aws-lc-rs, arboard.
- **License:** MPL-2.0 (`Cargo.toml`); surfman MIT OR Apache-2.0 OR MPL-2.0. The rest of the dependency list is not checked.
- **API stability:** breaking changes every month ("expected", 0.1.0 post), not 1.0. The LTS line or the C API keeps that in check.

### Side by side, for a native Aomi

| | WebKit2 (`BWebView`) | Gecko (BGeckoView) | Servo (BServoView) | Ladybird (native) |
|---|---|---|---|---|
| Engine on Haiku today | WebKit2 branch, not packaged (`docs/AOMI.md`) | Firefox 160, maintained | none | none (2023 port dead) |
| Embedding API | yes, Haiku's own | none; write one (GeckoView model) | yes, Rust and C | yes, undocumented |
| Websites | mature, older release | mature | WPT 67 %, improving monthly | alpha, "will crash sometimes" |
| Processes and sandbox | multiprocess, no sandbox | multiprocess, no sandbox | single process, no sandbox | multiprocess, no sandbox |
| Drawing | software (accelerated compositing off) | CPU into `BBitmap` | OpenGL (llvmpipe first, then Zink) | Skia Ganesh on Vulkan |
| New toolchain for R5 | no | no (already in the port) | Rust 1.99 | C++23, GCC 14+, Rust |
| Cost to a first page in Aomi (AI sessions) | 4–8 (round 1 in `docs/AOMI.md`) | 6–10 | 8–14 | 14–25 |
| Upkeep | Haiku's port | heavy: Haiku port plus own layer every 4 weeks | monthly API changes, or LTS every 6 months | rebase on a fast project with no Haiku CI |
| License | LGPL-2.1 and BSD | MPL-2.0 | MPL-2.0 | BSD-2-Clause, Qt LGPL for the Qt route |

Servo step estimate: Rust 1.99 for Haiku (1–2), SpiderMonkey via `mozjs` with the Firefox port's Haiku changes as a guide (2–4), the Haiku branches in fonts, surfman and the Unix crates (2–4), a `BServoView` on the software context with pixel copy (1–2), Aomi on top (3–5), then GL through Zink (1–2). Gecko step estimate: build the Haiku Firefox port on R5 (1), the chrome document and the `BMessage` bridge (3–5), the native counterparts of the Firefox UI features Aomi needs (3–6), and every Firefox update after that.

## Recommendation: Servo first, with Gecko as the fallback

In plain language: Ladybird is the hardest of the four on R5. It needs a newer compiler, Rust, eight new libraries and a window of our own, and it is two days into its alpha. Gecko is the best engine today and already runs on Haiku, but Mozilla offers no way to put it inside our own window, and those who built such a layer anyway (Sailfish) fell three years behind. Servo is built for exactly what Aomi wants: a native window with an engine inside, through a C API, in one process, which avoids most of the Haiku gaps that sank the 2023 Ladybird port. It is not finished, but it is moving fast (67 % of the web tests, a release every month) and takes ports to other systems (FreeBSD).

So:

1. **Now:** check Firefox on R5 (step 0 above), because it is the stopgap while Aomi is not ready.
2. **Next, one proof round for Servo:** Rust 1.99 and `mozjs` on R5, then a `BServoView` that shows one page with the software context in QEMU. That round decides whether Servo can carry Aomi.
3. **If SpiderMonkey or surfman will not work on R5:** fall back to a BGeckoView on the Haiku Firefox port, which costs more upkeep but uses an engine that already runs here.
4. **Ladybird:** watch it. Look again when it has a stable embedder API and its toolchain fits R5's.

Until one of these carries Aomi, WebPositive stays the image's browser and today's Aomi code (the WebPositive copy) stays parked; its R5 window work carries over to whichever engine wins.

## Sources

- Ladybird repository, branch `master`, read 25 Nov 2026: https://github.com/LadybirdBrowser/ladybird — files named above (`vcpkg.json`, `Meta/CMake/*.cmake`, `Meta/Utils/find_compiler.py`, `Documentation/BuildInstructionsLadybird.md`, `Documentation/Porting.md`, `Libraries/LibIPC`, `Libraries/LibCore/System.cpp`, `Libraries/LibSandbox`, `Services/`, `UI/Qt`, `LICENSE`).
- Ladybird alpha post, 23 Nov 2026: https://ladybird.org/posts/alpha/
- Haiku forum, porting Ladybird (pages 1–8 read, page 9 not): https://discuss.haiku-os.org/t/my-progress-at-porting-ladybird-the-serenityos-browser/13886
- HaikuPorts tree, `master`, 25 Nov 2026: https://github.com/haikuports/haikuports (`www-client/firefox`, `www-client/ladybird` — a 2022 recipe `ladybird-pre20220728`, long dead; `sys-devel/gcc`, `sys-devel/llvm`, library recipes named in section 2).
- Firefox on Haiku: https://www.osnews.com/story/140473/haiku-gets-tentative-firefox-port/, https://discuss.haiku-os.org/t/progress-on-porting-firefox/13493, https://discuss.haiku-os.org/t/mozilla-officially-gave-us-permission-to-use-the-firefox-name-on-haiku/19582
- Gecko: HaikuPorts `www-client/firefox` (recipe, patchset, mozconfigs); https://firefox-source-docs.mozilla.org/mobile/android/geckoview/contributor/geckoview-architecture.html; https://github.com/sailfishos/gecko-dev; https://product-details.mozilla.org/1.0/firefox_versions.json; https://www.mozilla.org/en-US/foundation/trademarks/policy/
- Servo: https://github.com/servo/servo (`components/servo`, `components/shared/paint/rendering_context.rs`, `ffi/capi`, `components/fonts/platform/mod.rs`, `rust-toolchain.toml`, `Cargo.toml`); https://github.com/servo/servo.org (`_posts/2026-04-13-servo-0.1.0-release.md`, `_posts/2026-11-19-october-in-servo.md`, `_posts/2026-04-30-march-in-servo.md`); https://book.servo.org (`src/embedding/`); https://wpt.servo.org/scores.json; https://github.com/servo/ipc-channel; https://github.com/servo/surfman
- R5: `docs/AOMI.md`, `docs/GPU-ARCHITECTURE.md`, `docs/APP-SERVER-GPU-DRAWING.md`, `docs/LICENSING.md`, `docs/MULTIUSER.md`, `docs/ROADMAP.md`.
