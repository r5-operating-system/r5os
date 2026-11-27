#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Planted as /boot/home/stabtest by scripts/qemu-stability-tests.py. Runs in
# the signed-in session. Every line for the host starts with "stab:" and goes
# to /dev/dprintf (the serial port). The first argument is the number of
# seconds for window_stress.
mkdir /tmp/stabtest.lock 2>/dev/null || exit 0
say() { echo "stab: $*" > /dev/dprintf 2> /dev/null; }
SECONDS_STRESS="${1:-600}"
T=/boot/home/stabtests
say start
sleep 5
screenmode 1280 800 32 >/dev/null 2>&1
say shot desktop

# 1. thread_local destructor after dlclose() (GitHub #3012)
# A crash stops the team in the debugger, so it gets 60 s.
"$T/tls_dtor_test" 200 > /boot/home/tls_dtor.txt 2>&1 &
TLS_PID=$!
i=0
while kill -0 "$TLS_PID" 2>/dev/null && [ "$i" -lt 60 ]; do
	sleep 1
	i=$((i + 1))
done
if kill -0 "$TLS_PID" 2>/dev/null; then
	say "tls_dtor_test FAIL: still running after 60 s (crashed or hung)"
	say shot tls-hang
	kill -9 "$TLS_PID" 2>/dev/null
else
	wait "$TLS_PID"
	say "tls_dtor_test exit=$?"
fi
while IFS= read -r line; do say "tls $line"; done < /boot/home/tls_dtor.txt

# 2. many windows (GitHub #3933, #3409); app_server must survive
say "app_server before: $(ps | grep app_server | head -1)"
("$T/window_stress" "$SECONDS_STRESS" 4 2>&1 | while IFS= read -r line; do
	say "ws $line"
done) &
WS_PID=$!
sleep 5
say shot during-stress
wait "$WS_PID"
say "window_stress done"
say "app_server after: $(ps | grep app_server | head -1)"
ls /boot/system/cache/debug 2>/dev/null | head -5 | while IFS= read -r line; do
	say "debug-report $line"
done
say shot after-stress

# 3. Aomi (Servo uses thread_local) opens a page
ifconfig /dev/net/virtio/0 inet 10.0.2.15 netmask 255.255.255.0 up \
	>/dev/null 2>&1
route add default 10.0.2.2 >/dev/null 2>&1
mkdir -p /boot/system/settings/network
printf 'nameserver 10.0.2.3\n' > /boot/system/settings/network/resolv.conf
export AOMI_LOG=/boot/home/aomi-events.log
rm -f "$AOMI_LOG"
/boot/system/apps/Aomi "https://example.com" >/boot/home/aomi.txt 2>&1 &
AOMI_PID=$!
i=0
while [ "$i" -lt 150 ]; do
	grep -q "title tab=0" "$AOMI_LOG" 2>/dev/null && break
	sleep 1
	i=$((i + 1))
done
say "aomi title: $(grep 'title tab=0' "$AOMI_LOG" 2>/dev/null | head -1)"
sleep 3
say shot aomi
sleep 10
say shot aomi-later
for prop in Title Hidden Frame Workspaces Minimize Look Feel; do
	say "aomi window $prop: $(hey Aomi get $prop of Window 0 2>&1 | tr '\n' ' ' | cut -c1-150)"
done
say "aomi windows: $(hey Aomi count Window 2>&1 | tr '\n' ' ' | cut -c1-100)"
/boot/system/apps/StyledEdit >/dev/null 2>&1 &
sleep 5
say shot stylededit
for demo in /boot/system/demos/GLTeapot /boot/system/demos/VulkanTeapot; do
	if [ -x "$demo" ]; then
		"$demo" >/dev/null 2>&1 &
		sleep 8
		say "shot $(basename $demo)"
	else
		say "missing $demo"
	fi
done
if kill -0 "$AOMI_PID" 2>/dev/null; then
	say "aomi alive=yes"
else
	say "aomi alive=NO"
fi
kill "$AOMI_PID" 2>/dev/null
sleep 3
say "aomi after quit: $(ps | grep -c Aomi)"
sync
say done
