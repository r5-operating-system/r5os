#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host test of the R5 SBC encoder (docs/BLUETOOTH.md, step 5), with
# AddressSanitizer and UBSan. Run it in WSL. The last line is
# "sbc_test: ok" when everything passed.
set -eu
cd "$(dirname "$0")/.."
D=distro/media/bluetooth_audio
out=/tmp/sbc-test-$$
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	-fno-sanitize-recover=all -I "$D" \
	"$D/tests/sbc_test.cpp" "$D/sbc_encoder.cpp" -o "$out" -lm
status=0
"$out" || status=$?
rm -f "$out"
exit "$status"
