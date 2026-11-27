#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Root part of scripts/perf3-guest.sh: set_real_time_clock() needs root, so
# a system launch job (qemu-stability-tests.py, STAB_ROOT) runs this once
# the volumes are mounted. The step is one hour forward and back again.
say() { echo "stab: $*" > /dev/dprintf 2> /dev/null; }
say "root uid $(id -u)"
/boot/home/stabtests/time_test rtc 2>&1 | while IFS= read -r line; do
	say "$line"
done
