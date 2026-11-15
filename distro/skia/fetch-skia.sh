#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Fetch Skia, VMA and gn at the commits in pins.sh into $SKIA_WORK, and
# build gn for the build host. Only these three are fetched: Skia's
# tools/git-sync-deps would fetch all 45 DEPS entries, and R5 builds none
# of the others (docs/APP-SERVER-GPU-DRAWING.md, part 1).
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
. "$HERE/pins.sh"

fetch() {
	# fetch <dir> <url> <commit>: a shallow fetch of exactly one commit.
	if [ -d "$1/.git" ] && [ "$(git -C "$1" rev-parse HEAD)" = "$3" ]; then
		echo "fetch: $1 at $3"
		return
	fi
	rm -rf "$1"
	mkdir -p "$1"
	git -C "$1" init -q
	git -C "$1" fetch -q --depth 1 "$2" "$3"
	git -C "$1" -c advice.detachedHead=false checkout -q FETCH_HEAD
	test "$(git -C "$1" rev-parse HEAD)" = "$3"
	echo "fetch: $1 at $3"
}

mkdir -p "$SKIA_WORK"
fetch "$SKIA_WORK/skia" "$SKIA_URL" "$SKIA_COMMIT"
fetch "$SKIA_WORK/skia/third_party/externals/vulkanmemoryallocator" \
	"$VMA_URL" "$VMA_COMMIT"
fetch "$SKIA_WORK/gn-src" "$GN_URL" "$GN_COMMIT"

grep -q "SK_MILESTONE $SKIA_MILESTONE" \
	"$SKIA_WORK/skia/include/core/SkMilestone.h"
grep -q "$VMA_COMMIT" "$SKIA_WORK/skia/DEPS"
grep -q "$GN_COMMIT" "$SKIA_WORK/skia/bin/fetch-gn"

if [ ! -x "$SKIA_WORK/gn-src/out/gn" ]; then
	# gn is a build-host tool; the host g++ builds it.
	# A shallow clone has no history for gen.py to count, so the
	# version header names the pinned commit instead.
	(cd "$SKIA_WORK/gn-src" && CC=gcc CXX=g++ AR=ar python3 build/gen.py --no-last-commit-position \
		&& printf '#ifndef OUT_LAST_COMMIT_POSITION_H_\n#define OUT_LAST_COMMIT_POSITION_H_\n#define LAST_COMMIT_POSITION_NUM 0\n#define LAST_COMMIT_POSITION "0 (%s)"\n#endif\n' \
			"$(echo $GN_COMMIT | cut -c1-12)" > out/last_commit_position.h \
		&& "$NINJA" -C out gn)
fi
mkdir -p "$SKIA_WORK/skia/bin"
cp -f "$SKIA_WORK/gn-src/out/gn" "$SKIA_WORK/skia/bin/gn"
"$SKIA_WORK/skia/bin/gn" --version
