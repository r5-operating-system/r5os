#!/bin/bash
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Build libr5servo.so and the r5servo package for R5 from servo-haiku
# (github.com/r5-operating-system/servo-haiku), at the commit pinned in
# distro/servo/servo-haiku.ref. distro/servo/BUILD.md says how.
#
# SERVO_SRC   R5's Servo build directory (default /home/nickr/r5os/servo-build):
#             Rust, LLVM, the patched crates and cargo's target stay there.
# HAIKU       the Haiku tree whose cross compiler and package tool are used
#             (default /home/nickr/r5os/haiku). This script does not write it.
# SERVO_HAIKU_DIR  a servo-haiku checkout to build instead of the pinned
#             commit (to try a change before it is pinned).
# R5_SERVO_PACK=0  build only, do not pack.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVO_SRC="${SERVO_SRC:-/home/nickr/r5os/servo-build}"
HAIKU="${HAIKU:-/home/nickr/r5os/haiku}"
# shellcheck source=/dev/null
. "$ROOT/distro/servo/servo-haiku.ref"

if [ -n "${SERVO_HAIKU_DIR:-}" ]; then
	SRC="$SERVO_HAIKU_DIR"
	echo "servo-haiku: $SRC (not the pinned commit)"
else
	SRC="$SERVO_SRC/servo-haiku"
	if [ ! -d "$SRC/.git" ]; then
		rm -rf "$SRC"
		git clone -q "$SERVO_HAIKU_URL" "$SRC"
	fi
	if [ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null)" != "$SERVO_HAIKU_COMMIT" ]; then
		git -C "$SRC" fetch -q origin
		git -C "$SRC" -c advice.detachedHead=false checkout -q --force "$SERVO_HAIKU_COMMIT"
	fi
	if [ "$(git -C "$SRC" rev-parse HEAD)" != "$SERVO_HAIKU_COMMIT" ]; then
		echo "servo-haiku is not at $SERVO_HAIKU_COMMIT" >&2
		exit 1
	fi
	echo "servo-haiku: $SERVO_HAIKU_COMMIT"
fi

# R5's build machine keeps Rust, LLVM and pkg-config under SERVO_SRC.
export WORK="$SERVO_SRC"
export HAIKU
export EXTRA_PATH="$SERVO_SRC/host-tools/bin"
bash "$SRC/scripts/build-r5servo.sh"
if [ "${R5_SERVO_PACK:-1}" != 0 ]; then
	python3 "$SRC/pack-r5servo.py"
fi
