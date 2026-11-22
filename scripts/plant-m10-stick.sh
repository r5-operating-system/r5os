#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Stick: plant the boot 1 settings (distro/graphics/boot/radeon_hd; for M11
# gpu_compositor true, gpu_debug true, gpu_selftest true) into r5-os-anyboot.iso, so the
# first boot runs the self-test with every GPU switch at its default.
# check, boot-next and check-user come from the mesa_radv package.
# No periodic sync: rounds 3-5 synced to the stick every 2-30 s and every
# program stalled 10-25 s while it ran. check and check-user sync once at
# the end instead.
# Run in WSL after the anyboot jam, before write-stick.py.
set -eu
HAIKU=/home/nickr/r5os/haiku
IMG="$HAIKU/generated.x86_64/r5-os-anyboot.iso"
BFS="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE=/tmp/m10-plant
rm -rf "$STAGE"
mkdir -p "$STAGE"
GIT="/mnt/c/Program Files/Git/bin/git.exe"
WINROOT=$(wslpath -w "$ROOT")
"$GIT" -C "$WINROOT" show "HEAD:distro/graphics/boot/radeon_hd" > "$STAGE/radeon_hd"
# Haiku's own switches (kernel debug.cpp, syslog_daemon syslog_output.cpp):
# a time on every syslog line, and room for a whole stick run (round 5 lost
# boot 1 when the 512 KiB log rotated). "true" must be written out.
# The kernel's own buffer (debug.cpp, at most 256 KiB, default 64 KiB):
# stick 74 lost every line before 0.35 s, the microcode lines with them.
printf 'syslog_time_stamps true
syslog_max_size 4M
syslog_buffer_size 262144
' > "$STAGE/kernel"
"$BFS" --start-offset 6291456 "$IMG" <<EOT
mkdir myfs/home/config
mkdir myfs/home/config/settings
mkdir myfs/home/config/settings/kernel
mkdir myfs/home/config/settings/kernel/drivers
rm myfs/home/config/settings/kernel/drivers/radeon_hd
rm myfs/home/config/settings/kernel/drivers/kernel
cp :$STAGE/radeon_hd myfs/home/config/settings/kernel/drivers/radeon_hd
cp :$STAGE/kernel myfs/home/config/settings/kernel/drivers/kernel
quit
EOT
# bfs_shell has no "put" (the old lines failed and still printed planted):
# read both files back and stop when they differ.
for name in radeon_hd kernel; do
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
