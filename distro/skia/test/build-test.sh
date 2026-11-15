#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Build the step 1 host test skia-frame against the skia_devel staging
# tree (pack-skia.sh) and the Vulkan loader of the Mesa build. The binary
# goes to $SKIA_WORK/skia-frame; it is planted only in a test copy of the
# image, never in the image itself.
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
. "$HERE/../pins.sh"
STAGE=$SKIA_WORK/pkg-root/develop
VULKAN=/home/nickr/r5os/mesa-build/sysroot

"$CROSS/bin/x86_64-unknown-haiku-g++" -std=c++20 -O2 -fno-exceptions -fno-rtti \
	$(cat "$STAGE/headers/skia/skia-defines.txt") \
	-DR5_SKIA_VERSION="\"m$SKIA_MILESTONE-$(echo $SKIA_COMMIT | cut -c1-10)\"" \
	-I"$STAGE/headers/skia" -I"$STAGE/headers/skia/include/third_party/vulkan" \
	"$HERE/skia-frame.cpp" -o "$SKIA_WORK/skia-frame" \
	"$STAGE/lib/libskia.a" \
	-L"$VULKAN/develop/lib" -lvulkan \
	-L"$FREETYPE/develop/lib" -lfreetype
ls -l "$SKIA_WORK/skia-frame"
