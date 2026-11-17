#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host unit test of the report code behind rumble, trigger effects, motion
# and HID PID force feedback (docs/GAME-CONTROLLERS.md, option C phase 1):
# distro/game/tests/feel_host_test with libr5game's GameControllerReports.cpp
# and hid_shared's R5HIDReports.h, built with AddressSanitizer and UBSan.
# Run it in WSL. Its last line is "feel_host_test: ok" when all passed.
set -eu
cd "$(dirname "$0")/.."
out=/tmp/game-feel-test-$$
g++ -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -g -O1 -Wall -Wextra \
	-I scripts/bt-host \
	-I distro/game/lib \
	-I distro/kernel/src/add-ons/kernel/drivers/input/hid_shared \
	distro/game/tests/feel_host_test/feel_host_test.cpp \
	distro/game/lib/GameControllerReports.cpp -o "$out"
status=0
"$out" || status=$?
rm -f "$out"
exit "$status"
