#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Build the host exFAT writer and check images with mkfs.exfat and
# fsck.exfat. Those programs are only run; their source is not used.
# The host tool links exfat_update.cpp (exfat_map and exfat_write_bytes).
# The kernel reads and writes through DataStream::FindBlock and whole
# sectors on the device. That path is scripts/exfat-qemu-test.py.
# FindBlock's byte offset is the same formula as exfat_map.
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$HERE/distro/kernel/src/add-ons/kernel/file_systems/exfat"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DEXFAT_HOST_TEST -I"$SRC" \
	-o /tmp/exfat_host "$SRC/exfat_update.cpp" "$SRC/exfat_host.cpp"

if command -v mkfs.exfat >/dev/null 2>&1; then
	MK="$(command -v mkfs.exfat)"
	CK="$(command -v fsck.exfat)"
elif [[ -x /tmp/exfatprogs/usr/sbin/mkfs.exfat ]]; then
	MK=/tmp/exfatprogs/usr/sbin/mkfs.exfat
	CK=/tmp/exfatprogs/usr/sbin/fsck.exfat
else
	echo "mkfs.exfat is not installed (exfatprogs)" >&2
	exit 1
fi

run() {
	name=$1
	size=$2
	shift 2
	echo "== $name"
	truncate -s "$size" "/tmp/${name}.img"
	"$MK" -q "$@" "/tmp/${name}.img"
	/tmp/exfat_host "/tmp/${name}.img"
	"$CK" -n "/tmp/${name}.img"
}

run exfat-c4k 64M -c 4K
run exfat-c32k 32M -c 32K
run exfat-c128k 128M -c 128K
run exfat-s4k 64M -s 4K -c 4K

echo "== power cut"
truncate -s 32M /tmp/exfat-cut.img
"$MK" -q -c 8K /tmp/exfat-cut.img
/tmp/exfat_host /tmp/exfat-cut.img cut
"$CK" -n /tmp/exfat-cut.img

if [[ "${EXFAT_LARGE:-0}" == "1" ]]; then
	echo "== past 4 GiB"
	truncate -s 5G /tmp/exfat-large.img
	"$MK" -q -c 1M /tmp/exfat-large.img
	/tmp/exfat_host /tmp/exfat-large.img large
	"$CK" -n /tmp/exfat-large.img
fi

echo "exfat-write-test: clean"
