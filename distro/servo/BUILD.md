<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# Building libr5servo

This is the instruction for the next agent. Do not ask where the trees are. Do not invent a second build.

`scripts/build-r5servo.sh` is the only recipe. It does not write the shared Haiku tree, it does not run jam, and it does not install the library into an image. Job cap: `-j4`.

## For the next agent

Work on branch `servo-proof` in the worktree `C:\Users\nickr\Projects\R5OS-servo-proof` (WSL: `/mnt/c/Users/nickr/Projects/R5OS-servo-proof`). Leave `C:\Users\nickr\Projects\R5OS` alone. That checkout is dirty and is not this branch.

From WSL, not from PowerShell (`&&` is rejected there):

```sh
cd /mnt/c/Users/nickr/Projects/R5OS-servo-proof
sed -i 's/\r$//' scripts/build-r5servo.sh scripts/run-r5servo-qemu.sh
scripts/build-r5servo.sh
```

The script stops and names the path if a patched tree or Rust 1.99.1 is missing. A machine without `/home/nickr/r5os/servo-build` cannot start from an empty disk.

After a successful `BUILD_OK`, strip before copying onto an image. An unstripped library is about 634M; stripped it is about 144M.

```sh
/home/nickr/r5os/haiku/generated.x86_64/cross-tools-x86_64/bin/x86_64-unknown-haiku-strip \
  --strip-unneeded /home/nickr/r5os/servo-build/r5servo-out/libr5servo.so
```

Boot, on a private copy of the proof image. This does not call `scripts/run-qemu-r5.sh` and it does not stop another session's QEMU. It refuses only when a guest named `r5-servo-proof` is already running.

```sh
SECONDS_UP=360 scripts/run-r5servo-qemu.sh
```

Do not set `LIBRARY_PATH` in the guest. On Haiku that replaces the default search path, and then `libbe.so` is not found. `non-packaged/lib` is already on the default path.

Do not add `libr5servo.so` to an image list, What's new, or `distro/BUILD`. Do not commit `.vendor`, `.cargo`, `target`, `cc.sh`, the patched trees, or `mozjs-sys`.

## What is in git

| Path | What it is |
|---|---|
| `distro/servo/BServoView.h`, `BServoView.cpp` | The view. Round 1 swaps the base class to Aomi's `BWebContentView` and deletes the copied message codes. |
| `distro/servo/r5_servo_abi.h` | Private C ABI. Not an Aomi header. |
| `distro/servo/r5-servo-test.cpp` | One window. `R5_SERVO_PROOF=1` loads a red `data:` document, then `https://http2.pro/api/v1`. |
| `distro/servo/engine/` | Rust staticlib `r5servo`. `Cargo.toml`, `Cargo.lock`, `src/lib.rs`, `build.rs`. |
| `scripts/build-r5servo.sh` | Rebuild. |
| `scripts/run-r5servo-qemu.sh` | Private QEMU boot. |
| `distro/rest/src/system/runtime_loader/elf_tls.cpp` | `get_tls_address()` returns the static initial-exec image. Servo needs it. The proof image was not rebuilt with this change. |

## What stays on this machine

`SERVO_SRC` defaults to `/home/nickr/r5os/servo-build`. None of this is in git.

| Path | What it is |
|---|---|
| `rust/` | Rust 1.99.1. `RUSTUP_HOME` is `rust/rustup`, `CARGO_HOME` is `rust/cargo`. Cargo is `cargo +1.99.1`. |
| `llvm/lib` | `LIBCLANG_PATH` for bindgen. |
| `mozjs-sys`, `surfman`, `ipc-channel`, `gaol`, `servo-fonts`, `servo-fonts-traits`, `servo-script` | Patched trees. Not git repos. The recipe rewrites `[patch]` to these absolute paths so cargo reuses the SpiderMonkey it already built. |
| `target/` | Cargo target dir. About 16G once SpiderMonkey is built. |
| `r5servo-engine/` | Staged copy of `distro/servo/engine` with absolute patch paths. The script deletes and rewrites it. |
| `r5servo-out/libr5servo.so`, `r5servo-out/r5-servo-test` | The products. |
| `r5servo-build.log` | Build log. |
| `bindgen-clang-args.txt` | Extra clang args for bindgen. |
| `servo.image` | The proof image (about 2G, from 26 Nov 2026 00:29 Europe/Amsterdam). Do not boot `haiku/generated.x86_64/r5-os.image` for this. |
| `r5servo-proof.image` | Private copy made by the QEMU script. |
| `r5servo-qemu.log`, `r5servo-serial.log` | Boot log and serial log. The guest log on the image is `/boot/home/r5-servo.log`. |

The Haiku tree used only for the cross compiler is `/home/nickr/r5os/haiku`. The compiler is `generated.x86_64/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++` (and `gcc`, `ar`, `strip`). `bfs_shell` is `generated.x86_64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell`.

Rebuilding Rust or SpiderMonkey from nothing, only when those trees are gone: `SERVO_SRC/build-hello.sh`, then `build-mozjs.sh`, then `build-servo.sh`, then `scripts/build-r5servo.sh`. A normal change of `lib.rs` or `BServoView.cpp` does not need that.

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

SpiderMonkey's `include_next <zlib.h>` needs the vendored header from `libz-sys-1.1.33` under `CARGO_HOME`. The script exports that include path. The Haiku sysroot has `libz.so` and no header.

## Patches, and why

The script points cargo at the trees. It does not re-download them.

- **surfman.** Haiku EGL is Mesa's surfaceless platform. The only config is 8/8/8/8 and it has no 24-bit depth or 8-bit stencil, so the context request drops those two. `LIBGL_ALWAYS_SOFTWARE` is left set, so `GALLIUM_DRIVER=zink` reaches Lavapipe. The hardware adapter is the low-power one, so a single GPU is not asked for `DRI_PRIME=1`.
- **ipc-channel.** Haiku has no `memfd`. The in-process backend is the one Android uses.
- **gaol.** Haiku has no sandbox. A profile cannot be built; the child is not jailed.
- **fonts.** Haiku is not in the Linux/Android target list. The build uses the bundled FreeType the Android target already uses, so it does not look for a host `freetype` pkg-config file.
- **mozjs_sys / servo-script.** The Haiku target is added so SpiderMonkey and the script crate configure and link with the Haiku cross compiler. `HOST_CC` stays the Linux gcc; the cross compiler is not the host compiler.

The shared library is Haiku g++ around the Rust staticlib, with `--whole-archive` and `--allow-multiple-definition` (two copies of lz4). A cdylib hid the C++ symbols. NEEDED libraries: `libstdc++`, `libbe`, `libz`, `libbsd`, `libgcc_s`, `libroot`, `libnetwork`. Mesa `libGL` is opened at runtime.

The first document has to be the web view's initial URL. Creating the engine on `about:blank` and then calling `load()` drops the real document. `LoadURL` from the application thread locks the window before it reads the size.

Destroying the GL context aborts in `eglDestroyImageKHR`. The engine is leaked on purpose.

## Where it lands

This round does not add the library to an image list.

- Header, when an image build installs it: `/boot/system/develop/headers/r5/BServoView.h`
- Library: `/boot/system/lib/libr5servo.so` (soname `libr5servo.so`)
- The proof copies both into `/boot/system/non-packaged/` on a private image. The `.so` is copied to both `non-packaged/lib` and `non-packaged/bin`.

`r5-servo-test` links `libr5servo.so`. The guest is started with `GALLIUM_DRIVER=zink`, `LIBGL_ALWAYS_SOFTWARE=1`, `RUST_LOG=net::http_loader=debug`, and `R5_SERVO_PROOF=1`. The GL string that counts is `zink Vulkan 1.4(llvmpipe (LLVM 21.1.8, 256 bits) (MESA_LLVMPIPE))`.

## Measured

26 Nov 2026, Europe/Amsterdam, on this machine, with SpiderMonkey already built. `scripts/build-r5servo.sh`, `-j4`.

| Step | Time |
|---|---|
| `cargo` (incremental, `lib.rs` only) | 16–17 s. An earlier incremental run was 8 s. The first cargo of this crate, still reusing the existing SpiderMonkey artifact, was 577 s. |
| shared link of `libr5servo.so` | 12–15 s |
| link of `r5-servo-test` | under 1 s |
| `target/` | 16G |
| `libr5servo.so` before `strip --strip-unneeded` | 634M |
| `libr5servo.so` after strip | 144M |

A from-scratch SpiderMonkey rebuild was not run for these numbers. Do not start one if `mozjs-sys` is still at `/home/nickr/r5os/servo-build/mozjs-sys`.
