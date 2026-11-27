#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Cross-build the guest stability tests in distro/tests (WSL):
#   tls_dtor_test + libtlsdtor.so  thread_local destructor after dlclose()
#                                  (GitHub #3012)
#   window_stress                  many windows opened and closed quickly
#                                  (GitHub #3933, #3409)
#   time_test                      time without a system call (performance
#                                  phase 3, scripts/perf3-guest.sh)
# They link against the libraries of the last jam in the shared tree, so
# build the image first. The programs are not part of the image; copy them
# into a private image copy with bfs_shell.
#
# Usage: scripts/build-stability-tests.sh [OUT]
set -eu
HAIKU=/home/nickr/r5os/haiku
WIN="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-/home/nickr/r5os/stability-tests}"
CXX="$HAIKU/generated.x86_64/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++"
LIBS="$HAIKU/generated.x86_64/objects/haiku/x86_64/release"

mkdir -p "$OUT"
"$CXX" -O2 -Wall -shared -fPIC -o "$OUT/libtlsdtor.so" \
	"$WIN/distro/tests/libroot/tls_dtor_lib.cpp"
"$CXX" -O2 -Wall -o "$OUT/tls_dtor_test" \
	"$WIN/distro/tests/libroot/tls_dtor_test.cpp"
"$CXX" -O2 -Wall -o "$OUT/time_test" \
	"$WIN/distro/tests/libroot/time_test.cpp"
"$CXX" -O2 -Wall -o "$OUT/window_stress" \
	"$WIN/distro/tests/app_server/window_stress.cpp" \
	-L"$LIBS/kits" -lbe
ls -l "$OUT"
