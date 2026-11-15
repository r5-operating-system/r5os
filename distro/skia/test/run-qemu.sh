#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Run the step 1 host test in QEMU on Lavapipe, without a jam:
#  1. copy r5-os.image to $SKIA_WORK/skia-test.image (the jammed image is
#     not changed);
#  2. plant skia-frame and a system launch job that runs it once at boot,
#     as root, and writes /boot/home/skia-frame.log (it needs no desktop:
#     the frame is drawn offscreen);
#  3. boot QEMU headless for $SECONDS_UP seconds (KVM when present);
#  4. read the log back with bfs_shell and print it.
# Stop no other QEMU: if one is running, this script refuses.
set -eu
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
. "$HERE/../pins.sh"
GEN=$HAIKU/generated.x86_64
BFS=$GEN/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell
IMG=$SKIA_WORK/skia-test.image
SECONDS_UP=${SECONDS_UP:-120}

if pgrep -f qemu-system-x86_64 >/dev/null; then
	echo "run-qemu: another QEMU is running; not started" >&2
	exit 1
fi
if pgrep -x jam >/dev/null; then
	echo "run-qemu: a jam is running and may rewrite r5-os.image; not started" >&2
	exit 1
fi
test -x "$SKIA_WORK/skia-frame"

cp --sparse=always "$GEN/r5-os.image" "$IMG"
cat > "$SKIA_WORK/skia-frame.job" <<'EOF'
job skia-frame {
	launch /bin/sh -c "/boot/system/non-packaged/bin/skia-frame > /boot/home/skia-frame.log 2>&1; echo exit $? >> /boot/home/skia-frame.log; sync"
	on initial_volumes_mounted
}
EOF
"$BFS" "$IMG" <<EOT
mkdir -p myfs/system/non-packaged/bin
mkdir -p myfs/system/settings/launch
cp :$SKIA_WORK/skia-frame myfs/system/non-packaged/bin/skia-frame
chmod 755 myfs/system/non-packaged/bin/skia-frame
cp :$SKIA_WORK/skia-frame.job myfs/system/settings/launch/skia-frame
quit
EOT

accel=tcg
cpu=qemu64
if [ -r /dev/kvm ]; then
	accel=kvm
	cpu=host
fi
echo "run-qemu: booting $IMG ($accel) for $SECONDS_UP s"
timeout "$SECONDS_UP" qemu-system-x86_64 -machine pc,accel=$accel -cpu $cpu \
	-m 2048 -smp 2 -display none \
	-drive file="$IMG",format=raw,if=ide,index=0,media=disk \
	-vga std -serial file:"$SKIA_WORK/skia-serial.log" </dev/null || true

"$BFS" "$IMG" <<EOT 2>/dev/null
cat myfs/home/skia-frame.log
quit
EOT
