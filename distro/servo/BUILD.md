<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# Servo in R5

R5's Servo is built from the public repository [servo-haiku](https://github.com/r5-operating-system/servo-haiku), not from this tree. That repository has the engine (`engine/`, with `Cargo.lock`), the C ABI (`r5_servo_abi.h`), `BServoView`, the test window, the Servo patches with `WHY`, the Haiku `runtime_loader` patch, the build and pack scripts, and the licence texts. Each Servo update and each change to `libr5servo.so` lands there first (#4679).

This directory keeps only:

| File | What |
|---|---|
| `servo-haiku.ref` | The servo-haiku URL and the commit R5 builds. |
| `r5servo.sha256` | The `r5servo` package R5 ships: version, address (a GitHub Release of servo-haiku) and sha256. |

The runtime_loader change Servo needs is R5's own source in `distro/rest/src/system/runtime_loader/` and `distro/kernel/src/system/runtime_loader/`; servo-haiku carries the same change as a patch against Haiku.

## Build

From WSL on R5's build machine:

```sh
scripts/build-r5servo.sh
```

The script checks out servo-haiku at `SERVO_HAIKU_COMMIT` in `$SERVO_SRC/servo-haiku` and runs its `scripts/build-r5servo.sh` with `WORK=$SERVO_SRC` (`/home/nickr/r5os/servo-build`), `HAIKU=/home/nickr/r5os/haiku` and the `pkg-config` in `$SERVO_SRC/host-tools/bin`, then its `pack-r5servo.py`. It does not write the shared Haiku tree and does not run jam. Job cap: 4. `SERVO_HAIKU_DIR=<checkout>` builds another servo-haiku checkout instead (to try a change before it is pinned); `R5_SERVO_PACK=0` skips the package.

What stays only on this machine, under `SERVO_SRC`: Rust 1.99.1 (`rust/rustup`, `rust/cargo`), LLVM 21.1.8 (`llvm/`), `host-tools/bin/pkg-config`, the patched crates, `target/` (about 16G), `r5servo-out/` (the library, `r5-servo-test`, the package), `r5servo-build.log`, and `servo.image` for `scripts/run-r5servo-qemu.sh`. servo-haiku's `docs/BUILDING.md` describes a build on any other host; its `docs/R5-BUILD.md` has the history of the library's API.

Because `WORK` and `CARGO_HOME` are the same directories as before, cargo reuses the SpiderMonkey it has built, and the library is the same file a build from this tree gave.

## Boot

`SECONDS_UP=360 scripts/run-r5servo-qemu.sh` boots `r5-servo-test` on a private copy of the proof image. It does not call `scripts/run-qemu-r5.sh` and does not stop another session's QEMU. `R5_SERVO_FROM_PKG=1` boots a full jam-built image without copying `libr5servo.so` into `non-packaged/`. Do not set `LIBRARY_PATH` in the guest.

## Where it lands

`pack-r5servo.py` writes `r5servo-<version>-x86_64.hpkg` in `$SERVO_SRC/r5servo-out`: the stripped `libr5servo.so`, `BServoView.h`, and in `documentation/packages/r5servo/` the licence notices of every crate and bundled C/C++ library and the MPL-2.0 source offer. The package is a GitHub Release asset of servo-haiku (tag `r5servo-<version>`); `r5servo.sha256` pins it. `scripts/pin-mesa-packages.sh` takes the local build output when its hash matches, otherwise downloads the asset, and stops the build on a wrong hash. `distro/UserBuildConfig` adds `r5servo` to the image, so jam installs it without running cargo.

- Header: `/boot/system/develop/headers/r5/BServoView.h`
- Library: `/boot/system/lib/libr5servo.so` (soname `libr5servo.so`, symlinked at `/boot/system/develop/lib/libr5servo.so`)

## A new version

1. Change servo-haiku (a branch there, then its `main`), with `VERSION` in its `pack-r5servo.py` raised.
2. Here: put the new commit in `servo-haiku.ref`, run `scripts/build-r5servo.sh`, check `BUILD_OK` and the printed sha256.
3. Upload the package as a Release `r5servo-<version>` of servo-haiku (needs the owner's go), download it again and compare the hash.
4. Put version, address and sha256 in `r5servo.sha256`, and the version in the r5servo lines of `scripts/pin-mesa-packages.sh`.

Do not add `libr5servo.so` to an image list, What's new, or `distro/BUILD` by hand.
