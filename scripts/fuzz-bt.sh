#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host AddressSanitizer and UBSan fuzz for the Bluetooth parsers
# (docs/BLUETOOTH.md, steps 1a to 1c and 5). Run it in WSL.
# Usage: fuzz-bt.sh <target> [seconds]   (target: hci, l2cap or audio;
# default 60 s). audio is the AVDTP, AVRCP and SBC code of bluetooth_audio.
# The log is generated/bluetooth/fuzz-<target>.log; its last line is
# "<target>_fuzz: <seconds>s 0 crashes" when nothing failed.
set -eu
cd "$(dirname "$0")/.."
target="${1:?usage: fuzz-bt.sh <target> [seconds]}"
seconds="${2:-60}"
K=distro/kernel/src/add-ons/kernel
A=distro/media/bluetooth_audio
case "$target" in
	hci|l2cap|audio) ;;
	*) echo "fuzz-bt: unknown target $target" >&2; exit 2 ;;
esac
out=/tmp/bt-fuzz-$$
flags="-std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=all
	-fno-omit-frame-pointer -g -O1"
if [ "$target" = audio ]; then
	g++ $flags -I "$A" scripts/bt-audio-fuzz.cpp "$A/sbc_encoder.cpp" \
		-o "$out" -lm
else
	g++ $flags \
		-I scripts/bt-host \
		-I "$K/bluetooth/hci" \
		-I "$K/drivers/bluetooth/h2/h2generic" \
		-I "$K/network/protocols/bluetooth/l2cap" \
		scripts/bt-fuzz.cpp -o "$out"
fi
mkdir -p generated/bluetooth
log="generated/bluetooth/fuzz-$target.log"
status=0
if [ "$target" = audio ]; then
	"$out" "$seconds" > "$log" 2>&1 || status=$?
else
	"$out" "$target" "$seconds" > "$log" 2>&1 || status=$?
fi
rm -f "$out"
tail -n 20 "$log"
exit "$status"
