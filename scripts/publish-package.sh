#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Publish an R5-built package as the asset of the GitHub Release
# "pkg-<name>-<version>" of r5-operating-system/r5os, download it again,
# check its sha256, and write its line in distro/packages/packages.sha256.
# Run it from Windows (Git Bash) with gh signed in; it does not run git and
# does not commit. Ask the owner before publishing (AGENTS.md).
#
#   scripts/publish-package.sh <file.hpkg> <image|skia|base>
#
# A line for the same package name and role is replaced, so the pin moves
# to the new version. An existing release with a different asset stops.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIST="$ROOT/distro/packages/packages.sha256"
REPO=r5-operating-system/r5os

if [[ $# -ne 2 ]]; then
	echo "usage: $0 <file.hpkg> <image|skia|base>" >&2
	exit 2
fi
src="$1"
role="$2"
case "$role" in image|skia|base) ;; *) echo "bad role $role" >&2; exit 2;; esac
file="$(basename "$src")"
[[ "$file" == *-x86_64.hpkg ]] || { echo "not an x86_64 hpkg: $file" >&2; exit 2; }
[[ -f "$src" ]] || { echo "missing $src" >&2; exit 1; }
stem="${file%-x86_64.hpkg}"
tag="pkg-$stem"
sha="$(sha256sum "$src" | awk '{print $1}')"
size="$(wc -c < "$src" | tr -d ' ')"
echo "release $tag  file $file  size $size  sha256 $sha"

if gh release view "$tag" -R "$REPO" >/dev/null 2>&1; then
	echo "release $tag exists; checking its asset"
else
	gh release create "$tag" -R "$REPO" --latest=false \
		--title "$stem (x86_64)" \
		--notes "R5-built package $file, used by the image build. sha256 $sha. Recipe and source offer: docs/LICENSING.md and distro/packages/README.md." \
		"$src"
fi

check="$(mktemp -d)"
trap 'rm -rf "$check"' EXIT
gh release download "$tag" -R "$REPO" -p "$file" -D "$check"
got="$(sha256sum "$check/$file" | awk '{print $1}')"
if [[ "$got" != "$sha" ]]; then
	echo "ERROR: asset of $tag has sha256 $got, expected $sha" >&2
	exit 1
fi
echo "downloaded again, sha256 ok"

# The package name is the stem up to the version (the first "-<digit>").
name="$(sed -E 's/-[0-9].*$//' <<< "$stem")"
tmp="$(mktemp)"
awk -F'\t' -v OFS='\t' -v n="$name" -v r="$role" -v f="$file" -v s="$sha" '
	BEGIN { done = 0 }
	/^#/ || NF < 3 { print; next }
	{
		pn = $1; sub(/-[0-9].*$/, "", pn)
		if (pn == n && $3 == r) {
			if (!done) { print f, s, r; done = 1 }
			next
		}
		print
	}
	END { if (!done) print f, s, r }
' <(tr -d '\r' < "$LIST") > "$tmp"
mv -f "$tmp" "$LIST"
echo "pinned $file ($role) in $LIST"
