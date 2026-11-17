# Neverball, the R5 demonstrator game

Plan only, 17 Nov 2026. No build and no code yet; every step waits for the owner's go.

The owner chose Neverball (and Neverputt, in the same package) as a cheerful open-source game that shows R5's new work: drawing on the GPU, the Game Kit (`BGameController`, mappings, the Settings page, rumble and motion) and sound. It comes through SDL, so the first real work is an **SDL bridge**: SDL's Haiku backend using `BGameController`. Every SDL game on R5 gains from that, not only Neverball. vkQuake (Vulkan) and an own R5Book sample stay for later.

## 1. What is there today

**Neverball.** Newest stable release 1.6.0; newest tagged release `neverball-1.7.0-alpha.3`, 19 Aug 2024 (zip 89073601 bytes); `master` is active (last commit 12 Nov 2026, version file still 1.6.0). Plain `make`, SDL2 through `sdl2-config`, SDL2_ttf, vorbisfile, libpng, libjpeg, optional curl and libintl, `-lGL`. No SDL3 path. The renderer is OpenGL 1.x fixed function (rewritten for GL ES 1.1 compliance in 1.6.0). Sources: [releases](https://api.github.com/repos/Neverball/neverball/releases), [Makefile](https://github.com/Neverball/neverball/blob/master/Makefile), [install.txt](https://github.com/Neverball/neverball/blob/master/doc/install.txt), [release notes](https://github.com/Neverball/neverball/blob/master/doc/release-notes.md).

Controller input is the low-level `SDL_Joystick` API only (`share/joy.c`: open, instance ID, hot-plug through `joy_add` and `joy_remove`); no `SDL_GameController`, no `gamecontrollerdb`, no rumble, no motion. The layout is in the config: `joystick_axis_x0`, `_y0`, `_x1`, `_y1` (each with `_invert`), `joystick_button_a` … `_start`, `joystick_dpad_*` ([config.c](https://github.com/Neverball/neverball/blob/master/share/config.c)).

**HaikuPorts.** `games-puzzle/neverball/neverball-1.6.1~20190808.recipe` (revision 2, git `09a2a620a6`, one patchset "Set working directory", `.rdef` files for both games), requires SDL2, SDL2_mixer, SDL2_ttf, physfs, vorbis, png, jpeg, freetype, intl and libGL. `media-libs/libsdl2` 2.32.10 (with a patchset), `media-libs/libsdl3` 3.4.16, `media-libs/sdl2_ttf` 2.24.0. Source: [haikuports tree](https://github.com/haikuports/haikuports/tree/master/games-puzzle/neverball). Which of these the R5 image already has was not checked.

**SDL's Haiku backend** (upstream SDL3 `main` and SDL2 `release-2.32.x`, the same in both; zlib, "Copyright (C) 1997-2026 Sam Lantinga"):

- Joystick, `src/joystick/haiku/SDL_haikujoystick.cc`: `BJoystick`. GUID from the name only (vendor and product 0), hats yes; no gamepad mapping (`GetGamepadMapping` is NULL), no rumble, no battery, no sensors, no hot-plug (scanned once). So SDL knows no R5 controller as a gamepad.
- Video, `src/video/haiku/SDL_BWin.h` and `SDL_bopengl.cc`: `class SDL_BWin : public BWindow`, a **plain `BWindow`, not a `BDirectWindow`**, with no `DirectConnected()` (only a leftover comment). OpenGL is a `BGLView` child of that window. The software path is a `BBitmap` (`SDL_bframebuffer.cc`). No EGL and no Vulkan in `SDL_bvideo.cc` or `SDL_bopengl.cc`; whether another file adds Vulkan was not checked.
- Audio, `src/audio/haiku/SDL_haikuaudio.cc`: Media Kit `BSoundPlayer`, playback only, the default output.

Sources: [SDL_haikujoystick.cc](https://github.com/libsdl-org/SDL/blob/main/src/joystick/haiku/SDL_haikujoystick.cc), [SDL_BWin.h](https://github.com/libsdl-org/SDL/blob/main/src/video/haiku/SDL_BWin.h), [SDL2 SDL_BWin.h](https://github.com/libsdl-org/SDL/blob/release-2.32.x/src/video/haiku/SDL_BWin.h), [SDL_bopengl.cc](https://github.com/libsdl-org/SDL/blob/main/src/video/haiku/SDL_bopengl.cc), [SDL_haikuaudio.cc](https://github.com/libsdl-org/SDL/blob/main/src/audio/haiku/SDL_haikuaudio.cc).

**R5.**

- OpenGL: `BGLView` goes through libglvnd and Mesa's Haiku EGL driver (`docs/GPU-OPENGL.md`). On the 5625U that is Zink on RADV, so Neverball does show the Vulkan path (`egl: opengl zink: …`). Without a GPU (VirtualBox, QEMU) the table in `docs/GPU-OPENGL.md` gives **llvmpipe**, OpenGL straight on the CPU, not Zink on Lavapipe. The rest of the desktop is composed on Lavapipe there. Neverball's GL 1.x is light enough for llvmpipe (not measured).
- The compositor pauses for every window with direct access (`app_server: compositor paused: direct window`, `ServerWindow::HandleDirectConnection()`); shadows and frosted glass then go off on the whole desktop (GLTeapot and Chart are `BDirectWindow`s). SDL's window is a plain `BWindow`, so Neverball should not pause it. The bridge keeps it that way: no `BDirectWindow`, no direct access. The general fix is M13 step 6b (`BDirectWindow` under the compositor, `docs/M13-PLAN.md`).
- Game Kit on `main`: `BGameController` with the standard layout in SDL's order, vendor and product ID, `Connection()`, `BatteryLevel()`, `StartWatching()` for plug and unplug, the saved setup and chosen layout from Settings (`docs/LIBR5.md`, "Game Kit").
- Game Kit in progress (branch `claude/game-controllers-feel`, not merged): `CanRumble()`, `SetRumble(strong, weak, duration)`, `StopRumble()`; `HasTriggerEffects()`, `SetTriggerEffect()` (DualSense and Edge); `HasMotion()`, `SetMotionEnabled()`, `GetMotion()` with acceleration in g and rotation in degrees per second (DualSense, Edge, DualShock 4, USB and Bluetooth); rumble through HID PID force feedback. Not there: the light bar, player lights, the touchpad as a surface.

## 2. Licenses

| Part | License | Source |
|---|---|---|
| Neverball and Neverputt code | `GPL-2.0-or-later`, copyright 2003 Robert Kooima and the Neverball team | `LICENSE.md`, header of `share/base_config.c` |
| `share/ease.c` (easings) | GPL-3.0 (`-only` or `-or-later` not checked) | `LICENSE.md` |
| `share/miniz.[ch]` | MIT, verbatim, `doc/legal/license-miniz.txt` | `LICENSE.md` |
| `share/fs_jpg.c` | based on libjpeg `jdatasrc.c` (IJG license) | `LICENSE.md` |
| `data/ttf/DejaVuSans-Bold.ttf` | DejaVu / Bitstream Vera font license | `LICENSE.md`, `doc/legal/` |
| `data/ttf/wqy-microhei.ttc` | Apache-2.0 OR GPL-3.0 (Debian: GPL-3+ with font exception) | `LICENSE.md`, [debian/copyright](https://salsa.debian.org/games-team/neverball/-/blob/master/debian/copyright) |
| `data/ball/octocat/` | the Octocat design "with permission from Github dated 2013-11-12": a permission to Neverball, not an open license | `LICENSE.md` |
| Other data (levels, textures, sounds, music) | no per-file list; Debian covers them as GPL-2+ under `Files: *` | `debian/copyright`; `data/` itself not read file by file |
| SDL2, SDL2_ttf | Zlib | SDL `LICENSE.txt` |

Result, in my reading (the owner decides; a lawyer is not needed for this):

- Where `share/ease.c` is in the version we build, the Neverball binary is GPL-3.0 as a whole (GPL-2.0-or-later code may be distributed under GPL-3.0; `docs/LICENSING.md`, "How components combine"). It is a separate program that links SDL (zlib) dynamically, so nothing reaches MIT code. No Neverball code goes into an R5 library.
- **Leave the Octocat ball out** of R5's package: the permission is for Neverball and is a trademark matter.
- A binary in the image brings the GPL source duty: the exact source tarball plus R5's patches and recipe. Choice C below.
- May ship in the regular image: yes, with the Octocat ball removed and the source duty met.

Lines to add when step 1 ships (not before):

- `THIRD_PARTY_NOTICES.md`, under "Built outside this repository": "Neverball and Neverputt <version> (https://neverball.org), GPL-2.0-or-later, `share/ease.c` GPL-3.0, so the binary is GPL-3.0; copyright Robert Kooima and the Neverball team. Fonts: DejaVu (Bitstream Vera license) and WenQuanYi Micro Hei (Apache-2.0 or GPL-3.0). miniz (MIT). The Octocat ball is removed. Source and R5 patches: <where, choice C>. SDL2 2.32.10 and SDL2_ttf (zlib, Sam Lantinga and contributors) with R5's bridge patch (MIT snippets)."
- `docs/LICENSING.md`, "Inventory": a row with project, version and sha256, `GPL-2.0-or-later` plus the exceptions above, integration "separate program, hpkg built from `distro/neverball`, regular image", and the source offer.

## 3. The SDL bridge

The changes go into SDL's Haiku joystick backend, as an R5 patch in an R5 recipe; the SDL file keeps its zlib header and is marked as altered (zlib requires that), the R5 lines are MIT SPDX snippets. SDL then links `libr5game.so`. Nothing in `libbe`; `BJoystick` stays as it is.

What the backend does with `BGameController`:

| SDL wants | From the Game Kit |
|---|---|
| Device list, hot-plug | `CountDevices()`, `GetDeviceName()`; `StartWatching()` sets a flag, SDL's `Detect()` rescans |
| GUID | `SDL_CreateJoystickGUID(bus, VendorID(), ProductID(), version, name)`, bus from `Connection()` |
| Buttons and axes | reported **in the standard order** of `BGameController` (`IsButtonDown`, `AxisValue`), so the person's setup and layout from Settings count in every SDL game too, one source of truth; a controller without a mapping reports its raw buttons, axes and hats |
| Gamepad mapping | `GetGamepadMapping()` returns the identity mapping for a mapped controller (`a:b0,b:b1,…,lefttrigger:a4,…`); triggers reported as SDL expects a raw trigger axis (released −32768) |
| Battery | `BatteryLevel()` to SDL's power level (SDL2) or percent (SDL3) |
| Rumble | `Rumble(low, high)` → `SetRumble(low, high, 0)`; SDL times the duration itself |
| Motion | `SetSensorsEnabled()` → `SetMotionEnabled()`; each report: accelerometer g × 9.80665 m/s², gyro degrees × π/180 rad/s, axes turned to SDL's convention (to be checked against SDL's own DualSense driver) |
| Trigger effects | not in SDL's standard API; `SDL_SendGamepadEffect` takes raw DualSense bytes. Later, choice F |
| Light bar, touchpad | `SetLED` and the touchpad stay unsupported until the Game Kit has them |

OpenGL needs no change: `BGLView` already reaches Zink or llvmpipe. Audio needs no change: `BSoundPlayer` is the Media Kit. Vulkan surfaces (R5's RFIVE view surface, `SDL_Vulkan_CreateSurface`) are later work, for vkQuake.

## 4. Steps

Each step is one small PR to `main`. Builds only with `scripts/build-locked.sh`. Game and SDL are app work: jam only, the owner looks in VirtualBox; no stick of their own. The 5625U check rides on the next planned stick.

1. **Neverball as it is.** R5 recipe `distro/neverball/` (like `distro/mesa`): pinned version and sha256, the HaikuPorts patchset, the Octocat ball removed, hpkg in `distro/packages/x86_64`, regular image, both games in the Deskbar menu (Demos or Games). SDL2 and its libraries from HaikuPorts, unchanged. Notices and inventory lines from section 2.
   *Pass:* `build-locked.sh` exit 0; in VirtualBox Neverball and Neverputt start, a level is played with keyboard and mouse, music and sound play, the window has its shadow and the syslog has no `compositor paused`; `R5_GL_TRACE=1` shows `egl: opengl llvmpipe`.
2. **SDL bridge, base.** R5 recipe `distro/sdl/` for SDL2 2.32.10 (HaikuPorts patchset plus the R5 patch): IDs in the GUID, standard order, identity mapping, hot-plug, battery.
   *Pass:* `gamecontroller_test` and SDL's own `testcontroller` see the same buttons; in VirtualBox with the DualSense and the M30 over USB passthrough, `testcontroller` names them as gamepads and a setup made in Settings shows up in SDL; unplug and plug are seen.
3. **SDL bridge, feel.** After `claude/game-controllers-feel` is merged: rumble and motion as in section 3.
   *Pass:* `testcontroller` rumbles the DualSense and shows the gyro and accelerometer moving the right way; a controller without them reports none.
4. **Neverball with a controller.** An R5 default config so Neverball's `joystick_*` numbers match the standard order (left stick tilts, A is OK, B back, Start pause).
   *Pass:* in VirtualBox a whole level with only the DualSense, and with the M30.
5. **Neverball shows the feel** (choice E): a small R5 patch to Neverball (GPL, in the recipe): tilt by moving the controller (motion), a short rumble when the ball falls or reaches the goal.
   *Pass:* in VirtualBox with the DualSense over USB; on the next stick on the 5625U with `egl: opengl zink` and the same level.

Later, not in this plan: the same bridge in SDL3 3.4.16, vkQuake on Vulkan, the R5Book sample.

## 5. Open choices

- **A. Version.** (1) HaikuPorts' 1.6.1 snapshot of 2019, builds on Haiku today; (2) `1.7.0-alpha.3` (2024), newest tag; (3) a pinned `master` commit. **Advice: (1) for step 1**, the least risk; move to a newer pin once it plays.
- **B. Where it ships.** (1) regular image; (2) a separate hpkg on the GitHub release; (3) wait for the R5 Store. **Advice: (1)**, it is the demonstrator; about 90 MB (the zip size, not measured unpacked). The minimum image does not get it.
- **C. GPL source duty.** (1) ship the source tarball and R5 patches on the image, as with VirtualBox (about 90 MB more); (2) the source next to the image on the GitHub release (GPL-3.0 section 6(d)), with a note in the image saying where. **Advice: (2)**, the image stays small; the owner decides.
- **D. SDL2 or SDL3 first.** Neverball has no SDL3 path. **Advice: SDL2 first**, the same patch to SDL3 after.
- **E. Neverball patch for motion and rumble** (step 5). **Advice: yes**: it is what makes the demo show the Game Kit; it stays in R5's recipe and is not sent upstream unless the owner says so.
- **F. DualSense trigger effects in SDL.** Through `SDL_SendGamepadEffect` with raw DualSense bytes, or not. **Advice: not now**; Neverball would not use them.

## 6. Not tested

Nothing in this plan was built or run. The licenses come from `LICENSE.md` and Debian's `debian/copyright`, not from every file under `data/`. Not checked: whether the R5 image has HaikuPorts SDL2 now, `ease.c`'s exact GPL-3.0 variant, Neverball's speed on llvmpipe, SDL Vulkan on Haiku.
