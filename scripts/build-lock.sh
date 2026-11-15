# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# One build at a time in the shared WSL Haiku tree. Source this file
# (". scripts/build-lock.sh") from a script that copies into the tree or
# runs jam. It takes an exclusive lock and keeps it until that script
# ends; another session waits here instead of writing over the tree.
#
# A script that runs both steps (build-locked.sh, build-image.sh) takes
# the lock once and exports R5_BUILD_LOCK_HELD=1, so apply-overlay.sh and
# jam-r5-raw.sh inside it do not wait for themselves.
#
# The lock file lives next to the Haiku tree, not in a worktree, so every
# worktree and session shares it. R5_BUILD_LOCK_WAIT (seconds, default
# 7200) limits the wait; 0 means "fail at once if busy".

if [ "${R5_BUILD_LOCK_HELD:-}" != 1 ]; then
	R5_BUILD_LOCK_FILE=${R5_BUILD_LOCK_FILE:-/home/nickr/r5os/.r5-build.lock}
	R5_BUILD_LOCK_WHO="$R5_BUILD_LOCK_FILE.who"
	exec 9>>"$R5_BUILD_LOCK_FILE"
	if ! flock -n 9; then
		echo "build-lock: the WSL Haiku tree is busy:" \
			"$(cat "$R5_BUILD_LOCK_WHO" 2>/dev/null || echo unknown)" >&2
		echo "build-lock: waiting (at most ${R5_BUILD_LOCK_WAIT:-7200} s)" >&2
		if ! flock -w "${R5_BUILD_LOCK_WAIT:-7200}" 9; then
			echo "build-lock: still busy, giving up" >&2
			exit 1
		fi
	fi
	printf '%s pid %s, %s, since %s\n' "$(basename "$0")" "$$" \
		"$(pwd)" "$(date '+%H:%M:%S')" > "$R5_BUILD_LOCK_WHO"
	export R5_BUILD_LOCK_HELD=1
	echo "build-lock: taken by $(basename "$0")" >&2
fi
