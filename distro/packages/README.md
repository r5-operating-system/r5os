<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->
<!-- SPDX-License-Identifier: MIT -->
# R5 packages

R5-built hpkg packages are not stored in git (since 26 Nov 2026, #4681). Each version is the asset of its own GitHub Release of `r5-operating-system/r5os`, tag `pkg-<name>-<version>`:

`https://github.com/r5-operating-system/r5os/releases/download/pkg-<name>-<version>/<name>-<version>-x86_64.hpkg`

`packages.sha256` in this folder pins every package by file name, sha256 and role. r5servo keeps its own pin in `distro/servo/r5servo.sha256` (tag `r5servo-<version>`).

| Role | What the build does with it |
|---|---|
| `image` | `scripts/pin-mesa-packages.sh` copies it to the download folder of the Haiku tree and puts it in `r5-opengl.repo` |
| `skia` | build-only; unpacked to `~/r5os/skia-build/pkg-root` when that staging tree is missing (app_server links it) |
| `base` | input of a packing script (`distro/mesa/pack-radv.py`, `distro/mesa/pack-lavapipe.py`, `scripts/build-vkcube.sh`); fetched only when that script runs |

## Where a package comes from

`scripts/r5-packages.sh` looks in this order and checks the sha256 each time:

1. the local cache, `~/r5os/package-cache` in WSL (`R5_PACKAGE_CACHE` overrides it);
2. the local build output in `distro/packages/x86_64` (ignored by git);
3. the GitHub Release asset, downloaded into the cache.

A wrong hash stops the build. Once a package is in the cache, the build needs no network. A copy of the cache folder is enough to build offline elsewhere.

## A new package version

1. Build it with its recipe; the output lands in `distro/packages/x86_64`.
2. Ask the owner before publishing.
3. From Windows (Git Bash, `gh` signed in): `scripts/publish-package.sh distro/packages/x86_64/<file> <role>`. It creates the release, downloads the asset again, checks the sha256, and replaces the pin line of that package and role.
4. Change the version where a script names it (the HaikuPorts pin block in `scripts/pin-mesa-packages.sh`, `scripts/install-mesa-packages.sh`, the packing script), and commit `packages.sha256` with it.

## Source

The recipe of each package and where its source comes from are in `docs/LICENSING.md` (source-offer rows) and `THIRD_PARTY_NOTICES.md`.
