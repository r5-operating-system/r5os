#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Performance phase 3 (time without a system call, GPU fence page). Planted
# as /boot/home/stabtest by scripts/qemu-stability-tests.py with
# STAB_GUEST=scripts/perf3-guest.sh. Runs in the signed-in session; every
# line for the host starts with "stab:" and goes to /dev/dprintf.
mkdir /tmp/stabtest.lock 2>/dev/null || exit 0
say() { echo "stab: $*" > /dev/dprintf 2> /dev/null; }
T=/boot/home/stabtests
say start
sleep 5
screenmode 1280 800 32 >/dev/null 2>&1
say shot desktop

# The real-time step ran as root at boot (perf3-root.sh); its lines are in
# the syslog and on the serial port already.
say "uid $(id -u)"

# 1. cycles per call, user path and system call
"$T/time_test" bench 2>&1 | while IFS= read -r line; do say "$line"; done
# 2. monotonic under load, across CPUs, agreement with the kernel
"$T/time_test" load 20 2>&1 | while IFS= read -r line; do say "$line"; done
# 2b. the same cross-CPU check for the user clock, the system call and ns
"$T/time_test" skew 5 2>&1 | while IFS= read -r line; do say "$line"; done

# 3. The R5 DRM library on this machine: no render node in QEMU, so the
# Vulkan loader must still pick Lavapipe and the GL/Vulkan demos draw.
say "render nodes: $(ls /dev/gpu/radeon_hd 2>&1 | tr '\n' ' ')"
if [ -x /boot/system/bin/VulkanTeapot ]; then
	/boot/system/bin/VulkanTeapot --selftest > /boot/home/vkselftest.txt 2>&1
	say "vulkanteapot selftest exit=$?"
	head -12 /boot/home/vkselftest.txt | while IFS= read -r line; do
		say "vk $line"
	done
fi

# 4. Aomi opens a page (usernet; no LAN)
ifconfig /dev/net/virtio/0 inet 10.0.2.15 netmask 255.255.255.0 up \
	>/dev/null 2>&1
route add default 10.0.2.2 >/dev/null 2>&1
mkdir -p /boot/system/settings/network 2>/dev/null
printf 'nameserver 10.0.2.3\n' > /boot/system/settings/network/resolv.conf \
	2>/dev/null
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
sleep 5
say shot aomi
for demo in /boot/system/demos/GLTeapot /boot/system/bin/VulkanTeapot; do
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
grep -c "r5 libdrm" /var/log/syslog 2>/dev/null | while IFS= read -r line; do
	say "syslog r5 libdrm lines $line"
done
ls /boot/system/cache/debug 2>/dev/null | head -5 | while IFS= read -r line; do
	say "debug-report $line"
done
sync
say done
