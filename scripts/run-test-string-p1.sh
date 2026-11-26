#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
for f in \
	"$ROOT/distro/kernel/src/system/libroot/posix/string/memcmp.c" \
	"$ROOT/distro/kernel/src/system/libroot/posix/string/strcmp.c" \
	"$ROOT/distro/kernel/src/system/libroot/posix/musl/string/strlen.c" \
	"$ROOT/distro/kernel/src/system/libroot/posix/musl/string/strchrnul.c" \
	"$ROOT/distro/kernel/src/system/libroot/posix/musl/string/memchr.c"; do
	b="$(basename "$f" .c)"
	gcc -O2 -Wall -Wextra -march=x86-64-v2 -fno-builtin \
		-I"$ROOT/distro/kernel/headers/private/system" \
		-c "$f" -o "$TMP/$b.o"
	objcopy --redefine-sym "$b=r5_test_$b" "$TMP/$b.o"
done
objcopy --redefine-sym strlen=r5_test_strlen "$TMP/strchrnul.o"
gcc -O2 -Wall -Wextra -march=x86-64-v2 \
	"$ROOT/scripts/test-r5-string-p1.c" "$TMP"/*.o -o "$TMP/test"
"$TMP/test"
