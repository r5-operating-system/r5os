#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Cross build of wpa_supplicant for x86_64 Haiku with SAE, PMF and OWE.
# The source is fetched by pins.sh. The result is an hpkg the image pin
# selects instead of the HaikuPorts package. arm64 and riscv64 are not built.
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
. "$HERE/pins.sh"
HAIKU=${HAIKU:-/home/nickr/r5os/haiku}
CROSS=$HAIKU/generated.x86_64/cross-tools-x86_64
CC="$CROSS/bin/x86_64-unknown-haiku-gcc"
CXX="$CROSS/bin/x86_64-unknown-haiku-g++"
WORK=${WPA_WORK:-$HOME/r5-wpa-build}
SRC=$WORK/src
PKG=$HERE/../../distro/packages/x86_64
# HERE is distro/wpa_supplicant, so the packages dir is two levels up
# only when this file lives in the repository. Prefer the checkout next
# to this script's repository root.
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
PKG=$ROOT/distro/packages/x86_64
OPENSSL=$HAIKU/generated.x86_64/build_packages/openssl3-3.5.7-1-x86_64
OBJ=$HAIKU/generated.x86_64/objects/haiku/x86_64/release

"$CC" --version | head -1 | grep -q ' 13\.3\.0$'

rm -rf "$SRC"
mkdir -p "$WORK" "$PKG"
if [ ! -f "$WORK/wpa.tar.gz" ]; then
	curl -fsSL -o "$WORK/wpa.tar.gz" "$WPA_URL"
fi
echo "$WPA_SHA256  $WORK/wpa.tar.gz" | sha256sum -c -
mkdir -p "$SRC"
tar -xzf "$WORK/wpa.tar.gz" -C "$SRC" --strip-components=1
python3 "$HERE/patch-src.py" "$SRC"

INCLUDES="-I$ROOT/distro/kernel/src/libs/compat/freebsd_wlan -I$HAIKU/src/libs/compat/freebsd_wlan -I$HAIKU/src/libs/compat/freebsd_network -I$HAIKU/src/libs/compat/freebsd_network/compat -I$HAIKU/headers -I$HAIKU/headers/os -I$HAIKU/headers/private -I$HAIKU/headers/private/net -I$HAIKU/headers/private/shared -I$HAIKU/headers/posix -I$OPENSSL/develop/headers -I$ROOT/distro/network/netapi"
LIBS_EXTRA="-L$OPENSSL/develop/lib -L$OBJ/kits -L$OBJ/kits/network/libnetapi/ssl -L$OBJ/system/libnetwork -L$CROSS/sysroot/boot/system/develop/lib -lstdc++"
printf 'CFLAGS += %s\nLIBS += %s\n' "$INCLUDES" "$LIBS_EXTRA" >> "$SRC/wpa_supplicant/.config"

cd "$SRC/wpa_supplicant"
# rc, xres and mimeset are Haiku host tools. The link is the part that
# has to succeed; a missing resource editor does not discard the binary.
# .config sets LDO to g++, which on the build host is the Linux compiler.
make CC="$CC" LDO="$CXX" BINDIR=/bin -j"$(nproc)" wpa_supplicant || test -x wpa_supplicant
test -x wpa_supplicant
echo "built $(pwd)/wpa_supplicant"

STAGE=$WORK/stage
rm -rf "$STAGE"
mkdir -p "$STAGE/bin"
cp wpa_supplicant "$STAGE/bin/wpa_supplicant"
chmod 755 "$STAGE/bin/wpa_supplicant"
cat > "$STAGE/.PackageInfo" <<EOF
name			wpa_supplicant
version			$WPA_VERSION
architecture		x86_64
summary			"WPA supplicant with SAE, PMF and OWE"
description		"R5 build of wpa_supplicant 2.11. SAE, protected management frames and OWE are on."
vendor			"R5OS"
packager		"R5OS Contributors <https://github.com/r5-operating-system/r5os>"
licenses		"BSD (3-clause)"
copyrights {
	"2002-2022 Jouni Malinen and contributors"
	"2011 Michael Lotz"
	"2010 Axel Dörfler"
}
provides {
	wpa_supplicant = 2.11.r5.2
	cmd:wpa_supplicant = 2.11.r5.2
}
requires {
	haiku
	lib:libcrypto
	lib:libnetwork
	lib:libbe
	lib:libbnetapi
}
EOF
PACKAGE_TOOL=$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package/package
"$PACKAGE_TOOL" create -C "$STAGE" "$PKG/wpa_supplicant-$WPA_VERSION-x86_64.hpkg"
echo "hpkg $PKG/wpa_supplicant-$WPA_VERSION-x86_64.hpkg"
