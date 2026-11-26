#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Fetch R5-built packages by sha256 (distro/packages/packages.sha256).
# A package comes from the local cache, else from the local build output in
# distro/packages/x86_64 when its hash matches, else from its GitHub Release
# asset. A wrong hash stops. Once a package is in the cache, no network is
# needed.
#
#   scripts/r5-packages.sh path <file>   fetch one package, print its path
#   scripts/r5-packages.sh list <role>   print the files of one role
#   . scripts/r5-packages.sh             define r5_pkg_fetch and r5_pkg_list
#
# R5_PACKAGE_CACHE overrides the cache folder (default ~/r5os/package-cache).
R5_PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R5_PKG_LIST="$R5_PKG_ROOT/distro/packages/packages.sha256"
R5_PKG_LOCAL="$R5_PKG_ROOT/distro/packages/x86_64"
R5_PKG_CACHE="${R5_PACKAGE_CACHE:-$HOME/r5os/package-cache}"
R5_PKG_RELEASES="https://github.com/r5-operating-system/r5os/releases/download"

r5_pkg_hash_ok() {
	local file="$1" want="$2" got
	[[ -f "$file" ]] || return 1
	got="$(sha256sum "$file" | awk '{print $1}')"
	[[ "$got" == "$want" ]]
}

# r5_pkg_url <file>: the release asset of a package in packages.sha256.
r5_pkg_url() {
	local file="$1" stem
	stem="${file%-x86_64.hpkg}"
	echo "$R5_PKG_RELEASES/pkg-$stem/$file"
}

# r5_pkg_sha <file>: its pinned sha256, or nothing.
r5_pkg_sha() {
	tr -d '\r' < "$R5_PKG_LIST" | awk -F'\t' -v f="$1" \
		'$0 !~ /^#/ && $1 == f { print $2; exit }'
}

# r5_pkg_list <role>: the files of one role.
r5_pkg_list() {
	tr -d '\r' < "$R5_PKG_LIST" | awk -F'\t' -v r="$1" \
		'$0 !~ /^#/ && NF >= 3 && $3 == r { print $1 }'
}

# r5_pkg_fetch <file> [sha256 url]: make sure the cache holds the package
# with the right hash. Prints the cached path on stdout, the source on
# stderr. Returns 1 on a missing pin, a failed download or a wrong hash.
r5_pkg_fetch() {
	local file="$1" sha="${2:-}" url="${3:-}" dst
	if [[ -z "$sha" ]]; then
		sha="$(r5_pkg_sha "$file")"
		if [[ -z "$sha" ]]; then
			echo "ERROR: $file is not pinned in $R5_PKG_LIST" >&2
			return 1
		fi
	fi
	[[ -n "$url" ]] || url="$(r5_pkg_url "$file")"
	mkdir -p "$R5_PKG_CACHE" || return 1
	dst="$R5_PKG_CACHE/$file"
	if r5_pkg_hash_ok "$dst" "$sha"; then
		echo "package $file: cache, sha256 ok" >&2
	elif r5_pkg_hash_ok "$R5_PKG_LOCAL/$file" "$sha"; then
		cp -f "$R5_PKG_LOCAL/$file" "$dst.tmp" && mv -f "$dst.tmp" "$dst" || return 1
		echo "package $file: local build output, sha256 ok" >&2
	else
		echo "package $file: downloading $url" >&2
		rm -f "$dst.tmp"
		if ! curl -fsSL --retry 3 -o "$dst.tmp" "$url"; then
			rm -f "$dst.tmp"
			echo "ERROR: download failed for $file ($url)" >&2
			return 1
		fi
		mv -f "$dst.tmp" "$dst"
		if ! r5_pkg_hash_ok "$dst" "$sha"; then
			echo "ERROR: sha256 mismatch for $file (expected $sha)" >&2
			rm -f "$dst"
			return 1
		fi
		echo "package $file: download, sha256 ok" >&2
	fi
	echo "$dst"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
	set -eu
	case "${1:-}" in
		path)
			[[ $# -eq 2 ]] || { echo "usage: $0 path <file>" >&2; exit 2; }
			r5_pkg_fetch "$2"
			;;
		list)
			[[ $# -eq 2 ]] || { echo "usage: $0 list <role>" >&2; exit 2; }
			r5_pkg_list "$2"
			;;
		*)
			echo "usage: $0 path <file> | list <role>" >&2
			exit 2
			;;
	esac
fi
