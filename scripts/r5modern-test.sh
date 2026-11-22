#!/bin/sh
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
# Default (R5 Modern) test in QEMU, started by UserBootscript after Sign in.
out=/boot/home/r5modern
mkdir -p "$out"
say() {
	echo "r5modern: $1"
	echo "r5modern: $1" > /dev/dprintf 2> /dev/null
}
say start
sleep 8
if [ "$(safemode 2> /dev/null)" = "yes" ]; then
	r5-effects-test decorator "Default (R5 Modern)" > "$out/safe.txt" 2>&1
	r5-effects-test get >> "$out/safe.txt" 2>&1
	sleep 3
	r5-effects-test soft 1500 &
	sleep 10
	say safe-ready
	exit 0
fi

r5-effects-test get > "$out/effects-before.txt" 2>&1
r5-effects-test decorator list > "$out/list-before.txt" 2>&1
r5-effects-test shadow off > /dev/null 2>&1
r5-effects-test frosted off > /dev/null 2>&1
r5-effects-test animations off > /dev/null 2>&1
n=0
for d in "Default (R5)" "Classic (Haiku)" "Default (R5 Modern)"; do
	n=$((n + 1))
	r5-effects-test decorator "$d" >> "$out/decor.txt" 2>&1
	sleep 3
	say "compare $n"
	r5-effects-test soft 25 &
	sleep 32
	cp /var/log/syslog "$out/syslog-$n"
done
r5-effects-test decorator list > "$out/list.txt" 2>&1
# the window effects on: shadow and frosted glass, Balanced
r5-effects-test shadow on > /dev/null 2>&1
r5-effects-test frosted on > /dev/null 2>&1
r5-effects-test strength balanced > /dev/null 2>&1
r5-effects-test get > "$out/effects.txt" 2>&1
sleep 3
r5-effects-test soft 1500 &
sleep 10
say ready
sleep 150
open /boot/system/preferences/Appearance &
sleep 8
say appearance
sleep 40
r5-effects-test decorator "Default (R5)" >> "$out/decor.txt" 2>&1
sleep 6
say switched
