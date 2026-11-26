<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# Building libr5servo

`scripts/build-r5servo.sh` is the recipe. It does not write the shared Haiku tree and it does not install the library into an image. Job cap: 4.

## Pins

| Piece | Pin |
|---|---|
| Rust | 1.99.1, `cargo +1.99.1 build -Zbuild-std=std,panic_unwind,panic_abort --target x86_64-unknown-haiku` |
| Servo | crates.io `servo` 0.8.0, default features off, `js_jit` and `bundled` on |
| SpiderMonkey | `mozjs_sys` 153.6.0-1, built from source (`MOZJS_FROM_SOURCE=1`) |
| surfman | 0.14.0, patched tree |
| ipc-channel | 0.23.1, patched tree |
| gaol | 0.2.1, patched tree |
| servo-fonts / servo-fonts-traits / servo-script | 0.8.0, patched trees |
| freetype-sys | 0.23.0, feature `bundled` |

The Rust sysroot, LLVM, and the patched trees stay in `SERVO_SRC` (default `/home/nickr/r5os/servo-build`). A machine without those trees cannot start from an empty disk; the script stops and names the missing path. Rebuilding Rust or SpiderMonkey from nothing is `SERVO_SRC/build-hello.sh`, `build-mozjs.sh`, then `build-servo.sh`, then this script.

## Patches, and why

The script symlinks the trees. It does not re-download them.

- **surfman.** Haiku EGL is Mesa's surfaceless platform. The only config is 8/8/8/8 and it has no 24-bit depth or 8-bit stencil, so the context request drops those two. `LIBGL_ALWAYS_SOFTWARE` is left set, so `GALLIUM_DRIVER=zink` reaches Lavapipe. The hardware adapter is the low-power one, so a single GPU is not asked for `DRI_PRIME=1`.
- **ipc-channel.** Haiku has no `memfd`. The in-process backend is the one Android uses.
- **gaol.** Haiku has no sandbox. A profile cannot be built; the child is not jailed.
- **fonts.** Haiku is not in the Linux/Android target list. The build uses the bundled FreeType the Android target already uses, so it does not look for a host `freetype` pkg-config file.
- **mozjs_sys / servo-script.** The Haiku target is added so SpiderMonkey and the script crate configure and link with the Haiku cross compiler. `HOST_CC` stays the Linux gcc; the cross compiler is not the host compiler.

## Where it lands

This round does not add the library to an image list.

- Header, when an image build installs it: `/boot/system/develop/headers/r5/BServoView.h`
- Library: `/boot/system/lib/libr5servo.so` (soname `libr5servo.so`)
- The proof copies both into `/boot/system/non-packaged/` on a private image.

`r5-servo-test` links `libr5servo.so`.

## Measured

Filled in by the run of `scripts/build-r5servo.sh` that produced the proof binary. Earlier Rust and SpiderMonkey times are the ones already recorded in `SERVO_SRC/*-build.log` from the proof round (26 Nov 2026).
