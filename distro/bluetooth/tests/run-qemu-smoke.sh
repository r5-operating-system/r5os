#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Step 0 of docs/BLUETOOTH.md in QEMU, on an own copy of the jammed image:
#  1. copy r5-os.image to $BT_WORK/bt-smoke.image (the jammed image is not
#     changed);
#  2. plant a system launch job that runs bt_server_smoke_test and
#     bt_l2cap_abi_test once, after
#     the desktop servers are up, and writes /boot/home/bt-smoke.log;
#  3. boot QEMU headless for $SECONDS_UP seconds, with its own monitor
#     socket ($BT_WORK/monitor.sock);
#  4. read the log and the Bluetooth lines of the syslog back with
#     bfs_shell, then remove the copy.
# SMAP=1 gives the guest SMAP and SMEP (the kernel checks of steps 1a to
# 1c) and prints the panic and SMAP lines of the serial log.
# QEMU has no Bluetooth hardware: the server sees 0 radios. This script
# stops no other QEMU. Run it in WSL, not under the build lock, and not
# while a jam may rewrite r5-os.image.
set -eu
HAIKU=${HAIKU:-/home/nickr/r5os/haiku}
GEN=$HAIKU/generated.x86_64
BFS=$GEN/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell
BT_WORK=${BT_WORK:-/home/nickr/r5os/bt-smoke}
IMG=$BT_WORK/bt-smoke.image
SECONDS_UP=${SECONDS_UP:-150}
# SRC_IMAGE: an own image copy to start from instead of the jammed one.
SRC_IMAGE=${SRC_IMAGE:-$GEN/r5-os.image}

if [ "$SRC_IMAGE" = "$GEN/r5-os.image" ] && pgrep -x jam >/dev/null; then
	echo "run-qemu-smoke: a jam is running and may rewrite r5-os.image" >&2
	exit 1
fi
mkdir -p "$BT_WORK"
cp --sparse=always "$SRC_IMAGE" "$IMG"
if [ "${KEEP:-0}" != 1 ]; then
	trap 'rm -f "$IMG" "$BT_WORK/bt-smoke.job" "$BT_WORK/monitor.sock"' EXIT
fi

# SETTINGS_PAGE=<id> (step 3a) opens that Settings page first, waits 30 s
# and logs whether bluetooth_server and Settings still run. SHOTS="110 130"
# takes a screendump at those seconds into $BT_WORK/shot-<s>.png.
SETTINGS_PAGE=${SETTINGS_PAGE:-}
SHOTS=${SHOTS:-}
page=""
if [ -n "$SETTINGS_PAGE" ]; then
	page="/boot/system/preferences/Settings --page $SETTINGS_PAGE & sleep 30; ps > /boot/home/bt-ps.log 2>&1; "
fi
# DESKBAR=1 (step 3b) starts Deskbar and then bluetooth_deskbar, which adds
# the Bluetooth item: the desktop is behind Sign in, so the user launch job
# that does this in a session does not run here. PROGRAM=1 opens the
# Bluetooth program.
if [ "${DESKBAR:-0}" = 1 ]; then
	page="/system/Deskbar & sleep 10; /system/servers/bluetooth_deskbar & sleep 10; $page"
fi
if [ "${PROGRAM:-0}" = 1 ]; then
	page="/boot/system/preferences/Bluetooth & sleep 10; $page"
fi
cat > "$BT_WORK/bt-smoke.job" <<EOF
job bt-smoke {
	launch /bin/sh -c "(while true; do sleep 5; ps -a > /boot/home/bt-psa.log 2>&1; sync; done) & sleep 60; ps > /boot/home/bt-ps0.log 2>&1; sync; $page/bin/bt_server_smoke_test > /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_l2cap_abi_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_hid_driver_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_l2cap_coc_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_smp_crypto_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_audio_smoke_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_audio use 00:11:22:33:44:55 >> /boot/home/bt-smoke.log 2>&1; /bin/bt_audio >> /boot/home/bt-smoke.log 2>&1; /bin/bt_audio off >> /boot/home/bt-smoke.log 2>&1; /bin/bt_audio >> /boot/home/bt-smoke.log 2>&1; ls -ld /boot/system/settings/bluetooth /boot/system/settings/bluetooth/* >> /boot/home/bt-smoke.log 2>&1; sleep 5; sync"
	on initial_volumes_mounted
}
EOF
"$BFS" "$IMG" <<EOT
mkdir -p myfs/system/settings/launch
cp :$BT_WORK/bt-smoke.job myfs/system/settings/launch/bt-smoke
quit
EOT
# LOGIN=1: skip the first-boot prompt (launch_daemon shows it while there
# are no Locale settings), so Login starts and its admin port is tested.
if [ "${LOGIN:-0}" = 1 ]; then
	: > "$BT_WORK/locale-settings"
	"$BFS" "$IMG" <<EOT
mkdir -p myfs/home/config/settings
cp :$BT_WORK/locale-settings "myfs/home/config/settings/Locale settings"
quit
EOT
fi

accel=tcg
cpu=qemu64
if [ -r /dev/kvm ]; then
	accel=kvm
	cpu=host
fi
if [ "${SMAP:-0}" = 1 ]; then
	cpu="$cpu,+smap,+smep"
fi
echo "run-qemu-smoke: booting $IMG ($accel) for $SECONDS_UP s"
if [ -n "$SHOTS" ]; then
	(
		t=0
		for s in $SHOTS; do
			sleep $((s - t))
			t=$s
			python3 -c "import socket,sys,time;s=socket.socket(socket.AF_UNIX);s.connect(sys.argv[1]);s.send((sys.argv[2]+'\n').encode());time.sleep(1)" \
				"$BT_WORK/monitor.sock" "screendump $BT_WORK/shot-$s.ppm" || true
		done
	) &
fi
timeout "$SECONDS_UP" qemu-system-x86_64 -machine pc,accel=$accel -cpu $cpu \
	-m 2048 -smp 2 -display none \
	-drive file="$IMG",format=raw,if=ide,index=0,media=disk \
	-vga std -serial file:"$BT_WORK/serial.log" \
	-monitor unix:"$BT_WORK/monitor.sock",server,nowait </dev/null || true

# bfs_shell's cat prints nothing here; copy the files out instead.
rm -f "$BT_WORK/out-smoke.log" "$BT_WORK/out-ps.log" "$BT_WORK/out-ps0.log" "$BT_WORK/out-syslog"
"$BFS" "$IMG" >/dev/null 2>&1 <<EOT
cp myfs/home/bt-smoke.log :$BT_WORK/out-smoke.log
cp myfs/home/bt-ps.log :$BT_WORK/out-ps.log
cp myfs/home/bt-ps0.log :$BT_WORK/out-ps0.log
cp myfs/home/bt-psa.log :$BT_WORK/out-psa.log
cp myfs/system/var/log/syslog :$BT_WORK/out-syslog
quit
EOT
for f in "$BT_WORK"/shot-*.ppm; do
	[ -f "$f" ] || continue
	python3 -c "from PIL import Image;import sys;Image.open(sys.argv[1]).save(sys.argv[1][:-4]+'.png')" "$f" && rm -f "$f"
done
if [ -n "$SETTINGS_PAGE" ]; then
	echo "== running after Settings --page $SETTINGS_PAGE"
	grep -E 'bluetooth_server|Settings' "$BT_WORK/out-ps.log" 2>/dev/null || echo "(neither runs)"
fi
if [ "${SMAP:-0}" = 1 ]; then
	echo "== serial (SMAP, panic)"
	grep -aiE 'smap|smep|panic|kernel debugger|vm_page_fault' 		"$BT_WORK/serial.log" || echo "(no lines)"
fi
echo "== running before the test (the launch job)"
grep bluetooth_server "$BT_WORK/out-ps0.log" 2>/dev/null || echo "(bluetooth_server not running)"
echo "== bt-smoke.log"
cat "$BT_WORK/out-smoke.log" 2>/dev/null || echo "(no log)"
echo "== syslog (Bluetooth lines)"
grep -aE 'bluetooth_server|bluetooth_deskbar|deskbar_bt_replicant|bt_server_smoke_test|bt_l2cap_abi_test|bt_hid_driver_test|l2cap_coc_test|bt_smp_crypto_test|bluetooth_hid|bluetooth_audio|DeviceManager|h2generic|l2cap|debug_server|crash' \
	"$BT_WORK/out-syslog" 2>/dev/null || echo "(no lines)"
