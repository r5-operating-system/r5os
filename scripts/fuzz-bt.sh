#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host AddressSanitizer and UBSan fuzz for the Bluetooth kernel parsers
# (docs/BLUETOOTH.md, steps 1a to 1c), the LE parsers of bluetooth_server
# (step 6), the AVDTP, AVRCP and SBC code of bluetooth_audio and the SDP
# answers of bluetooth_server (step 5), its RFCOMM frames and mux
# (step 7a), its HFP AT commands (step 7b), and the SCO packet path of the
# voice link (step 7c: h2generic's h2sco.h, the mSBC deframer and the SBC
# decoder), and the game controller reports (docs/GAME-CONTROLLERS.md,
# option C phase 1: hid_shared's R5HIDReports.h and libr5game's
# GameControllerReports.cpp). Run it in WSL.
# Usage: fuzz-bt.sh <target> [seconds]   (target: hci, l2cap, le, audio,
# sdp, rfcomm, hfp, sco or hid; default 60 s)
# The log is generated/bluetooth/fuzz-<target>.log; its last line is
# "<target>_fuzz: <seconds>s 0 crashes" when nothing failed.
set -eu
cd "$(dirname "$0")/.."
target="${1:?usage: fuzz-bt.sh <target> [seconds]}"
seconds="${2:-60}"
K=distro/kernel/src/add-ons/kernel
A=distro/media/bluetooth_audio
case "$target" in
	hci|l2cap|le|audio|sdp|rfcomm|hfp|sco|hid) ;;
	*) echo "fuzz-bt: unknown target $target" >&2; exit 2 ;;
esac
out=/tmp/bt-fuzz-$$
flags="-std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=all
	-fno-omit-frame-pointer -g -O1"
if [ "$target" = audio ]; then
	g++ $flags -I "$A" scripts/bt-audio-fuzz.cpp "$A/sbc_encoder.cpp" \
		-o "$out" -lm
elif [ "$target" = sdp ]; then
	g++ $flags -I distro/bluetooth/server scripts/bt-sdp-fuzz.cpp \
		distro/bluetooth/server/RFCOMMMux.cpp -o "$out"
elif [ "$target" = sco ]; then
	g++ $flags -I scripts/bt-host -I "$K/drivers/bluetooth/h2/h2generic" \
		-I "$A" scripts/bt-sco-fuzz.cpp "$A/sbc_encoder.cpp" \
		"$A/sbc_decoder.cpp" -o "$out" -lm
elif [ "$target" = rfcomm ]; then
	g++ $flags -I distro/bluetooth/server scripts/bt-rfcomm-fuzz.cpp \
		distro/bluetooth/server/RFCOMMMux.cpp -o "$out"
elif [ "$target" = hid ]; then
	g++ $flags -I scripts/bt-host -I distro/game/lib \
		-I "$K/drivers/input/hid_shared" scripts/bt-hid-fuzz.cpp \
		distro/game/lib/GameControllerReports.cpp -o "$out"
elif [ "$target" = hfp ]; then
	g++ $flags -I distro/bluetooth/server scripts/bt-hfp-fuzz.cpp \
		distro/bluetooth/server/HFPAgent.cpp -o "$out"
else
	g++ $flags \
		-I scripts/bt-host \
		-I "$K/bluetooth/hci" \
		-I "$K/drivers/bluetooth/h2/h2generic" \
		-I "$K/network/protocols/bluetooth/l2cap" \
		-I distro/bluetooth/server \
		scripts/bt-fuzz.cpp -o "$out"
fi
mkdir -p generated/bluetooth
log="generated/bluetooth/fuzz-$target.log"
status=0
if [ "$target" = audio ] || [ "$target" = sdp ] || [ "$target" = rfcomm ] \
	|| [ "$target" = hfp ] || [ "$target" = sco ] || [ "$target" = hid ]; then
	"$out" "$seconds" > "$log" 2>&1 || status=$?
else
	"$out" "$target" "$seconds" > "$log" 2>&1 || status=$?
fi
rm -f "$out"
tail -n 20 "$log"
exit "$status"
