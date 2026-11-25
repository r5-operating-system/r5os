#!/bin/sh
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Mounts the second disk and runs exfat-ops. Planted by
# scripts/exfat-qemu-test.py. Lines go to the serial port.

say() { echo "exfat: $*" > /dev/dprintf 2> /dev/null; }

say start
sleep 20
DEV=""
for path in /dev/disk/ata/0/slave/raw /dev/disk/ata/1/slave/raw \
	/dev/disk/ata/0/master/raw /dev/disk/scsi/0/0/raw \
	/dev/disk/scsi/1/0/raw; do
	if [ -e "$path" ]; then
		DEV=$path
		break
	fi
done
if [ -z "$DEV" ]; then
	say "device missing"
	ls /dev/disk > /dev/dprintf 2>&1
	say done
	exit 1
fi
say "device $DEV"
ls /dev/disk/ata/0/slave > /dev/dprintf 2>&1
mkdir -p /boot/home/card
mount -t exfat "$DEV" /boot/home/card > /tmp/exfat-mount.txt 2>&1
mounted=$?
say "mount $mounted $(tr '\n' ' ' < /tmp/exfat-mount.txt)"
if [ "$mounted" -ne 0 ]; then
	mount "$DEV" /boot/home/card > /tmp/exfat-mount.txt 2>&1
	mounted=$?
	say "mount-auto $mounted $(tr '\n' ' ' < /tmp/exfat-mount.txt)"
fi
if [ "$mounted" -ne 0 ]; then
	sync
	say done
	exit 1
fi
chmod 755 /boot/home/exfat-ops
/boot/home/exfat-ops /boot/home/card
say "ops $?"
unmount /boot/home/card
say "unmount $?"
sync
say done
