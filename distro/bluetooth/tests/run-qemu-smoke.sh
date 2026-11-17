#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Step 0 of docs/BLUETOOTH.md in QEMU, on an own copy of the jammed image:
#  1. copy r5-os.image to $BT_WORK/bt-smoke.image (the jammed image is not
#     changed);
#  2. plant a system launch job that runs bt_server_smoke_test and
#     bt_l2cap_abi_test (and the later tests, up to bt_voice_test) once, after
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
# BT_PREVIEW=1 starts that Settings with R5_BLUETOOTH_PREVIEW=1: the
# Bluetooth page then shows one made-up connected game controller under My
# devices (libr5bt, test only), so its layout can be seen without a radio.
# BT_WINDOW="<w> <h>" sets the Settings window to that size first (hey).
preview=""
if [ "${BT_PREVIEW:-0}" = 1 ]; then
	preview="R5_BLUETOOTH_PREVIEW=1 "
fi
if [ -n "$SETTINGS_PAGE" ]; then
	page="$preview/boot/system/preferences/Settings --page $SETTINGS_PAGE & sleep 30; ps > /boot/home/bt-ps.log 2>&1; "
	if [ -n "${BT_WINDOW:-}" ]; then
		set -- $BT_WINDOW
		page="$page/bin/hey Settings set Frame of Window 0 to 'BRect(20,40,$((20 + $1)),$((40 + $2)))' > /boot/home/bt-hey.log 2>&1; "
	fi
	# BT_TEST_BUTTONS=<s>: that many seconds after Settings opened, send the
	# page the message of its "Test buttons…" button (with BT_PREVIEW=1 the
	# made-up controller is chosen); Settings should then show Game
	# controllers.
	if [ -n "${BT_TEST_BUTTONS:-}" ]; then
		page="$page(sleep $((BT_TEST_BUTTONS - 30)); /bin/hey Settings 'btvT' of View 'bluetooth' of Window 0 >> /boot/home/bt-hey.log 2>&1) & "
	fi
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
# GAMEPAD=1 (step 8, docs/GAME-CONTROLLERS.md): QEMU has no game
# controller, so after the tests gamecontroller_test keeps a virtual one on
# bluetooth_hid (DualSense IDs, Cross down, L2 at 60 %, the left stick up
# and left) until QEMU stops; an open Settings page shows it as it comes.
# After the tests, because bt_hid_driver_test takes the first
# joystick/bluetooth node. The Sign in window is moved to the lower right
# corner (hey), so the screendumps show the page. Use with
# SETTINGS_PAGE=gamecontrollers SECONDS_UP=170 SHOTS="140 160".
gamepad=""
if [ "${GAMEPAD:-0}" = 1 ]; then
	page="/bin/hey Login set Frame of Window 'Sign in' to 'BRect(900,620,1340,1040)' > /boot/home/gc-hey.log 2>&1; $page"
	gamepad="/bin/gamecontroller_test --hold 120 > /boot/home/gc-hold.log 2>&1 & "
fi
# QUIT_SETTINGS=1 (with GAMEPAD=1): 30 s after the virtual controller comes,
# the Settings window gets its smallest size (910 by 560) for a screendump;
# 30 s later Settings leaves the page for Information and is quit, and the
# processes are listed, so a crash at quit (the game controller watcher,
# CHANGELOG.md 17 Nov 2026) shows as a debugger line in the syslog and as a
# Settings still in that list.
after=""
if [ "${QUIT_SETTINGS:-0}" = 1 ]; then
	after="sleep 30; /bin/hey Settings set Frame of Window 0 to 'BRect(40,40,950,600)' > /boot/home/gc-small.log 2>&1; sleep 30; /boot/system/preferences/Settings --page information & sleep 10; /bin/hey Settings quit > /boot/home/gc-quit.log 2>&1; sleep 10; ps > /boot/home/gc-quit-ps.log 2>&1; sync; "
fi
cat > "$BT_WORK/bt-smoke.job" <<EOF
job bt-smoke {
	launch /bin/sh -c "(while true; do sleep 5; ps -a > /boot/home/bt-psa.log 2>&1; sync; done) & sleep 60; ps > /boot/home/bt-ps0.log 2>&1; sync; $page/bin/bt_server_smoke_test > /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_l2cap_abi_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_hid_driver_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_l2cap_coc_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_smp_crypto_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_audio_smoke_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_rfcomm_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/bt_voice_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync; /bin/gamecontroller_test >> /boot/home/bt-smoke.log 2>&1; echo exit \$? >> /boot/home/bt-smoke.log; sync;/bin/bt_audio use 00:11:22:33:44:55 >> /boot/home/bt-smoke.log 2>&1; /bin/bt_audio >> /boot/home/bt-smoke.log 2>&1; /bin/bt_audio off >> /boot/home/bt-smoke.log 2>&1; /bin/bt_audio >> /boot/home/bt-smoke.log 2>&1; ls -ld /boot/system/settings/bluetooth /boot/system/settings/bluetooth/* >> /boot/home/bt-smoke.log 2>&1; $gamepad $after sleep 5; sync"
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
rm -f "$BT_WORK/out-smoke.log" "$BT_WORK/out-ps.log" "$BT_WORK/out-ps0.log" "$BT_WORK/out-syslog" \
	"$BT_WORK/out-hey.log"
"$BFS" "$IMG" >/dev/null 2>&1 <<EOT
cp myfs/home/bt-smoke.log :$BT_WORK/out-smoke.log
cp myfs/home/bt-ps.log :$BT_WORK/out-ps.log
cp myfs/home/bt-ps0.log :$BT_WORK/out-ps0.log
cp myfs/home/bt-psa.log :$BT_WORK/out-psa.log
cp myfs/system/var/log/syslog :$BT_WORK/out-syslog
cp myfs/home/gc-hold.log :$BT_WORK/out-gc-hold.log
cp myfs/home/bt-hey.log :$BT_WORK/out-hey.log
cp myfs/home/gc-small.log :$BT_WORK/out-gc-small.log
cp myfs/home/gc-quit.log :$BT_WORK/out-gc-quit.log
cp myfs/home/gc-quit-ps.log :$BT_WORK/out-gc-quit-ps.log
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
grep -aE 'bluetooth_server|bluetooth_deskbar|deskbar_bt_replicant|bt_server_smoke_test|bt_l2cap_abi_test|bt_hid_driver_test|l2cap_coc_test|bt_smp_crypto_test|bt_rfcomm_test|bt_voice_test|RFCOMM|gamecontroller|bluetooth_hid|bluetooth_audio|DeviceManager|h2generic|l2cap|debug_server|crash' \
	"$BT_WORK/out-syslog" 2>/dev/null || echo "(no lines)"
if [ -n "${BT_WINDOW:-}${BT_TEST_BUTTONS:-}" ]; then
	echo "== hey (BT_WINDOW, BT_TEST_BUTTONS)"
	cat "$BT_WORK/out-hey.log" 2>/dev/null || echo "(no log)"
fi
if [ "${GAMEPAD:-0}" = 1 ]; then
	echo "== gamecontroller_test --hold"
	cat "$BT_WORK/out-gc-hold.log" 2>/dev/null || echo "(no log)"
fi
if [ "${QUIT_SETTINGS:-0}" = 1 ]; then
	echo "== Settings smallest size (hey)"
	cat "$BT_WORK/out-gc-small.log" 2>/dev/null || echo "(no log)"
	echo "== Settings quit (hey)"
	cat "$BT_WORK/out-gc-quit.log" 2>/dev/null || echo "(no log)"
	echo "== still running after the quit"
	grep -E 'Settings|debug_server' "$BT_WORK/out-gc-quit-ps.log" 2>/dev/null || echo "(no Settings)"
	echo "== debugger lines in the syslog"
	grep -aE 'entered the debugger|Segment violation|__dynamic_cast' "$BT_WORK/out-syslog" 2>/dev/null || echo "(none)"
fi
