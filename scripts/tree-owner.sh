#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# The WSL Haiku tree is shared by every worktree. apply-overlay.sh copies a
# worktree's distro/ into it, but never removed what another worktree had
# copied there before, so one session's build could fail on files of a
# session that was still in progress.
#
# apply-overlay.sh calls "tree-owner.sh begin" before it copies and
# "tree-owner.sh end" after. end records which paths of the tree the apply
# wrote (changed or new, by change time). begin, when the last apply came
# from another worktree, puts those paths back as they are in the Haiku
# git HEAD of the tree (a tracked file) or removes them (a new file).
# Only paths that an apply wrote are touched; nothing else in the tree is.
#
# Run it only under build-lock.sh. The record lives next to the lock file.
set -euo pipefail

cmd=${1:?usage: tree-owner.sh begin|end <worktree root>}
ROOT=${2:?usage: tree-owner.sh begin|end <worktree root>}
TREE="$(readlink -f "$ROOT/haiku")"
STATE=${R5_TREE_OWNER_DIR:-$(dirname "${R5_BUILD_LOCK_FILE:-/home/nickr/r5os/.r5-build.lock}")/.r5-tree-owner}
OWNER="$(readlink -f "$ROOT")"

if [ "${R5_BUILD_LOCK_HELD:-}" != 1 ]; then
	echo "tree-owner: run under build-lock.sh" >&2
	exit 1
fi
mkdir -p "$STATE"

case "$cmd" in
begin)
	touch "$STATE/marker"
	prev="$(cat "$STATE/owner" 2>/dev/null || true)"
	if [ -z "$prev" ]; then
		echo "tree-owner: no record of an earlier apply; nothing to put back" >&2
		exit 0
	fi
	if [ "$prev" = "$OWNER" ]; then
		exit 0
	fi
	echo "tree-owner: putting back $(wc -l < "$STATE/files") paths of $prev" >&2
	cd "$TREE"
	# Tracked paths: back to HEAD. The rest: remove.
	git ls-files | LC_ALL=C sort > "$STATE/all"
	LC_ALL=C sort "$STATE/files" | LC_ALL=C comm -12 - "$STATE/all" > "$STATE/tracked"
	if [ -s "$STATE/tracked" ]; then
		git --literal-pathspecs restore --source=HEAD --worktree \
			--pathspec-from-file="$STATE/tracked"
	fi
	LC_ALL=C sort "$STATE/files" | LC_ALL=C comm -23 - "$STATE/all" | while IFS= read -r p; do
		rm -f -- "$p"
		rmdir -p --ignore-fail-on-non-empty -- "$(dirname -- "$p")" 2>/dev/null || true
	done
	rm -f "$STATE/all" "$STATE/tracked"
	# The owner changes in end, so a failed apply puts these back again.
	;;
end)
	cd "$TREE"
	# Changed or new paths whose change time is after begin: this apply's.
	git status --porcelain=v1 -z -uall | while IFS= read -r -d '' e; do
		printf '%s\0' "${e:3}"
	done | xargs -0 -r sh -c 'find "$@" -prune -cnewer "$0" -print 2>/dev/null' \
		"$STATE/marker" > "$STATE/new" || true
	# The same worktree may skip a file that is already equal, so keep the
	# paths of its earlier applies that are still changed.
	prev="$(cat "$STATE/owner" 2>/dev/null || true)"
	if [ "$prev" = "$OWNER" ] && [ -f "$STATE/files" ]; then
		cat "$STATE/files" >> "$STATE/new"
	fi
	LC_ALL=C sort -u "$STATE/new" > "$STATE/files"
	rm -f "$STATE/new"
	printf '%s\n' "$OWNER" > "$STATE/owner"
	echo "tree-owner: $(wc -l < "$STATE/files") paths recorded for $OWNER" >&2
	;;
*)
	echo "usage: tree-owner.sh begin|end <worktree root>" >&2
	exit 1
	;;
esac
