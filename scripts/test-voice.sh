#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host test of the voice path (docs/BLUETOOTH.md, step 7c): mSBC and SBC
# through R5's encoder and decoder, the mSBC frames in SCO payloads, the
# kernel's voice parsers (h2generic/h2sco.h) and the HCI commands of the
# Audio Gateway (server/VoiceLink.h), with AddressSanitizer and UBSan. Run
# it in WSL. The last line is "voice_test: ok" when everything passed.
set -eu
cd "$(dirname "$0")/.."
D=distro/media/bluetooth_audio
K=distro/kernel/src/add-ons/kernel/drivers/bluetooth/h2/h2generic
out=/tmp/voice-test-$$
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	-fno-sanitize-recover=all -I "$D" -I scripts/bt-host -I "$K" \
	-I distro/bluetooth/server -I distro/bluetooth/tests/bt_voice_test \
	"$D/tests/voice_test.cpp" "$D/sbc_encoder.cpp" "$D/sbc_decoder.cpp" \
	-o "$out" -lm
status=0
"$out" || status=$?
rm -f "$out"
exit "$status"
