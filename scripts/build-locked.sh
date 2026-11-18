#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Apply this checkout's distro/ to the WSL Haiku tree and jam, under one
# lock (build-lock.sh), so no other session can copy or jam in between.
# Use this instead of running apply-overlay.sh and jam-r5-raw.sh one
# after the other. Arguments go to jam-r5-raw.sh.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
for arg in "$@"; do
	if [[ "$arg" == "--debug" || "$arg" == "@r5-debug" ]]; then
		export R5_KERNEL_DEBUG=1
	fi
done
. "$HERE/build-lock.sh"
"$HERE/apply-overlay.sh" || exit 1
"$HERE/jam-r5-raw.sh" "$@"
