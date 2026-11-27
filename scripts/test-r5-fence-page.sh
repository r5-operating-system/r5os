#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host test of the fence-page fast path in libdrm-r5 (WSL). Needs only the
# libdrm 2.4.124 headers of the shared tree; builds into a temp folder.
set -eu
ROOT=/home/nickr/r5os
LIBDRM=$ROOT/src-third/libdrm-2.4.124
WIN="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
INC=(-I"$WIN/distro/graphics/headers/private/r5gpu"
	-I"$WIN/distro/graphics/libdrm-r5"
	-I"$LIBDRM" -I"$LIBDRM/include/drm" -I"$LIBDRM/amdgpu")
gcc -std=c11 -O2 -Wall -DR5_HOST_TEST -D_GNU_SOURCE "${INC[@]}" \
	-c -o "$OUT/r5_translate.o" "$WIN/distro/graphics/libdrm-r5/r5_translate.c"
gcc -std=c11 -O2 -Wall -DR5_HOST_TEST -D_GNU_SOURCE "${INC[@]}" \
	-c -o "$OUT/test.o" "$WIN/scripts/test-r5-fence-page.c"
gcc -o "$OUT/test-r5-fence-page" "$OUT/test.o" "$OUT/r5_translate.o" \
	-lpthread -Wl,--unresolved-symbols=ignore-in-object-files
"$OUT/test-r5-fence-page" 2>/dev/null
