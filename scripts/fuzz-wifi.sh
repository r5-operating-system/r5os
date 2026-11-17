#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host AddressSanitizer and UBSan fuzz for the Wi-Fi parsers
# (docs/WIFI.md, step 1). Run it in WSL.
# Usage: fuzz-wifi.sh <target> [seconds]
#   target: ies, rsn, action, mtkrx, he
# The log is generated/wifi/fuzz-<target>.log. Its last line is
# "wifi_fuzz <target>: <seconds>s 0 crashes" when nothing failed.
set -eu
cd "$(dirname "$0")/.."
target="${1:?usage: fuzz-wifi.sh <target> [seconds]}"
seconds="${2:-60}"
case "$target" in
	ies|rsn|action|mtkrx|he) ;;
	*) echo "fuzz-wifi: unknown target $target" >&2; exit 2 ;;
esac
HAIKU="${HAIKU:-/home/nickr/r5os/haiku}"
out="/tmp/wifi-fuzz-$$"
slice="/tmp/wifi-slice-$$.c"
python3 scripts/wifi-host/extract.py "$PWD" "$HAIKU" "$target" "$slice"
flags="-std=c11 -fsanitize=address,undefined -fno-sanitize-recover=all
	-fno-omit-frame-pointer -g -O1 -I scripts/wifi-host"
gcc $flags "$slice" scripts/wifi-fuzz.c -o "$out"
mkdir -p generated/wifi
log="generated/wifi/fuzz-$target.log"
status=0
"$out" "$target" "$seconds" > "$log" 2>&1 || status=$?
rm -f "$out" "$slice"
tail -n 20 "$log"
exit "$status"
