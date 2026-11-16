#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Host test of tree-owner.sh with a small git tree and two worktree roots,
# the way two sessions share the WSL Haiku tree. Worktree B applies and
# "builds" (an object newer than the source); worktree A then applies its
# own, older file the way the applies do (cp -p keeps the old time, as
# shutil.copy2 does). The object must not be newer than A's file then, or
# jam would keep B's object (16 Nov 2026, libr5icons.so).
# Usage: scripts/test-tree-owner.sh   (prints "ok tree owner")
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

mkdir -p "$T/tree" "$T/a" "$T/b" "$T/state"
git -C "$T/tree" init -q
printf 'head\n' > "$T/tree/src.cpp"
git -C "$T/tree" -c user.email=t@t -c user.name=t add src.cpp
git -C "$T/tree" -c user.email=t@t -c user.name=t commit -qm head
ln -s "$T/tree" "$T/a/haiku"
ln -s "$T/tree" "$T/b/haiku"
# A's file was checked out an hour ago; B's is fresh.
printf 'a version\n' > "$T/a/src.cpp"
touch -m -d '1 hour ago' "$T/a/src.cpp"
printf 'b version\n' > "$T/b/src.cpp"

export R5_BUILD_LOCK_HELD=1 R5_TREE_OWNER_DIR="$T/state"
apply() {
	bash "$HERE/tree-owner.sh" begin "$1" 2> /dev/null
	sleep 1
	cp -p "$1/src.cpp" "$T/tree/src.cpp"
	bash "$HERE/tree-owner.sh" end "$1" 2> /dev/null
}

apply "$T/a"
apply "$T/b"
# B builds: the object is newer than B's source.
sleep 1
touch "$T/tree/src.o"
sleep 1
apply "$T/a"
if [ "$T/tree/src.o" -nt "$T/tree/src.cpp" ]; then
	echo "FAIL tree owner: B's object is newer than A's source, jam would keep it"
	exit 1
fi
# The same owner again: the copy keeps the source's time and nothing
# touches it, so the object built in between stays newer and is kept.
sleep 1
touch "$T/tree/src.o"
apply "$T/a"
if [ "$(stat -c %Y "$T/tree/src.cpp")" != "$(stat -c %Y "$T/a/src.cpp")" ] \
		|| [ ! "$T/tree/src.o" -nt "$T/tree/src.cpp" ]; then
	echo "FAIL tree owner: the same owner touched its files again"
	exit 1
fi
echo "ok tree owner"
