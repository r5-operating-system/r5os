# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# The one place where the Skia version is pinned. A change here is the
# update procedure in docs/GPU-ARCHITECTURE.md ("Updating Skia, step by
# step"), never a drive-by bump. git's hash covers the content, so a
# fetch by hash is the integrity check (Skia publishes no tarballs).

SKIA_BRANCH=chrome/m158
SKIA_COMMIT=707e33875fb45f2997a4d781128bec122fb777b4
SKIA_DATE=2026-11-13
SKIA_MILESTONE=158
SKIA_URL=https://skia.googlesource.com/skia

# Vulkan Memory Allocator 3.4.0, the commit Skia's DEPS names.
VMA_COMMIT=eb744ea7a2b17040121b4bbb4d6f9e8a77e3cae7
VMA_URL=https://chromium.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator

# gn, the revision Skia's bin/fetch-gn names, built from source on the
# build host (no downloaded binary).
GN_COMMIT=b2afae122eeb6ce09c52d63f67dc53fc517dbdc8
GN_URL=https://gn.googlesource.com/gn

# Build directory in WSL, outside the Haiku tree.
SKIA_WORK=${SKIA_WORK:-/home/nickr/r5os/skia-build}
HAIKU=${HAIKU:-/home/nickr/r5os/haiku}
CROSS=$HAIKU/generated.x86_64/cross-tools-x86_64
FREETYPE=$HAIKU/generated.x86_64/build_packages/freetype-2.14.3-1-x86_64
NINJA=${NINJA:-/home/nickr/r5os/mesa-build/ninja}
