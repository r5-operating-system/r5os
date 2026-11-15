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

cat > "$BT_WORK/bt-smoke.job" <<'EOF'
job bt-smoke {
	launch /bin/sh -c "sleep 60; /bin/bt_server_smoke_test > /boot/home/bt-smoke.log 2>&1; echo exit $? >> /boot/home/bt-smoke.log; /bin/bt_l2cap_abi_test >> /boot/home/bt-smoke.log 2>&1; echo exit $? >> /boot/home/bt-smoke.log; sleep 5; sync"
	on initial_volumes_mounted
}
EOF
"$BFS" "$IMG" <<EOT
mkdir -p myfs/system/settings/launch
cp :$BT_WORK/bt-smoke.job myfs/system/settings/launch/bt-smoke
quit
EOT

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
timeout "$SECONDS_UP" qemu-system-x86_64 -machine pc,accel=$accel -cpu $cpu \
	-m 2048 -smp 2 -display none \
	-drive file="$IMG",format=raw,if=ide,index=0,media=disk \
	-vga std -serial file:"$BT_WORK/serial.log" \
	-monitor unix:"$BT_WORK/monitor.sock",server,nowait </dev/null || true

# bfs_shell's cat prints nothing here; copy the files out instead.
rm -f "$BT_WORK/out-smoke.log" "$BT_WORK/out-syslog"
"$BFS" "$IMG" >/dev/null 2>&1 <<EOT
cp myfs/home/bt-smoke.log :$BT_WORK/out-smoke.log
cp myfs/system/var/log/syslog :$BT_WORK/out-syslog
quit
EOT
if [ "${SMAP:-0}" = 1 ]; then
	echo "== serial (SMAP, panic)"
	grep -aiE 'smap|smep|panic|kernel debugger|vm_page_fault' 		"$BT_WORK/serial.log" || echo "(no lines)"
fi
echo "== bt-smoke.log"
cat "$BT_WORK/out-smoke.log" 2>/dev/null || echo "(no log)"
echo "== syslog (Bluetooth lines)"
grep -aE 'bluetooth_server|bt_server_smoke_test|bt_l2cap_abi_test|DeviceManager|h2generic|l2cap' \
	"$BT_WORK/out-syslog" 2>/dev/null || echo "(no lines)"
