#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host test of the Realtek Bluetooth firmware parser and choice
# (h2generic/h2rtkfw.h, docs/BLUETOOTH-2.md step B0), with AddressSanitizer
# and UBSan, against the shipped files in distro/firmware/realtek. Run it in
# WSL. The last line is "rtkfw_test: ok" when everything passed.
set -eu
cd "$(dirname "$0")/.."
K=distro/kernel/src/add-ons/kernel/drivers/bluetooth/h2/h2generic
out=/tmp/rtkfw-test-$$
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	-fno-sanitize-recover=all -I scripts/bt-host -I "$K" \
	scripts/bt-rtkfw-test.cpp -o "$out"
status=0
"$out" distro/firmware/realtek || status=$?
rm -f "$out"
exit "$status"
