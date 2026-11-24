#!/bin/bash
# Build-host tests. The headers are copied onto a case-sensitive disk
# because String.h and string.h are the same file on the Windows volume.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INC="$(mktemp -d)"
cp -a "$ROOT/host/include/." "$INC/"
OUT="$(mktemp)"
mapfile -t sources < <(find "$ROOT/engine" "$ROOT/tests" -name '*.cpp' ! -name 'HttpTransport.cpp')
g++ -std=c++17 -Wall -Wextra -Werror -pthread \
	-I"$INC" -I"$ROOT" -I"$ROOT/engine" -I"$ROOT/tests" \
	"${sources[@]}" \
	"$ROOT/host/main.cpp" \
	-o "$OUT"
"$OUT"
rm -rf "$INC" "$OUT"
