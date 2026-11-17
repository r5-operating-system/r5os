#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Wi-Fi stick: plant the boot 2 settings (ht_vht true, he false) for
# mt7922wifi into r5-os-anyboot.iso, so the first boot is W-Boot2 (boot 1,
# everything off, passed on stick 51). Also a 4 MiB syslog, so a boot does
# not rotate into syslog.1. The ~/wifi-stick note is not planted: a file
# made here belongs to root, and wifi-check reads the boot from the
# settings file when there is no note.
# Run in WSL after the anyboot jam, before write-stick.py. Only for the
# stick image; main ships no such file.
set -eu
HAIKU=/home/nickr/r5os/haiku
IMG="$HAIKU/generated.x86_64/r5-os-anyboot.iso"
BFS="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
STAGE=/tmp/wifi-plant
rm -rf "$STAGE"
mkdir -p "$STAGE"
printf 'ht_vht true\nhe false\n' > "$STAGE/mt7922wifi"
printf 'syslog_time_stamps true\nsyslog_max_size 4M\n' > "$STAGE/kernel"
"$BFS" --start-offset 6291456 "$IMG" <<EOT
mkdir myfs/home/config
mkdir myfs/home/config/settings
mkdir myfs/home/config/settings/kernel
mkdir myfs/home/config/settings/kernel/drivers
rm myfs/home/config/settings/kernel/drivers/mt7922wifi
rm myfs/home/config/settings/kernel/drivers/kernel
cp :$STAGE/mt7922wifi myfs/home/config/settings/kernel/drivers/mt7922wifi
cp :$STAGE/kernel myfs/home/config/settings/kernel/drivers/kernel
quit
EOT
# bfs_shell has no "put": read both files back and stop when they differ.
for name in mt7922wifi kernel; do
	got=$("$BFS" --start-offset 6291456 "$IMG" <<EOT 2>/dev/null
cat myfs/home/config/settings/kernel/drivers/$name
quit
EOT
)
	for line in $(tr ' ' '_' < "$STAGE/$name"); do
		if ! printf '%s\n' "$got" | tr ' ' '_' | grep -q -- "$line"; then
			echo "plant FAIL: $line is not in the image's $name" >&2
			exit 1
		fi
	done
done
echo planted
