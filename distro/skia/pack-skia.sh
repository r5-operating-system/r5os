#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Pack libskia.a and its headers as skia_devel, a build-only package
# (docs/APP-SERVER-GPU-DRAWING.md, part 3). It is not in the image: no
# image list names it. jam links app_server against it (M12 step 3).
#
#   develop/lib/libskia.a
#   develop/headers/skia/include/...           Skia's public headers
#   develop/headers/skia/modules/skcms/        skcms.h, which SkColorSpace.h includes
#   develop/headers/skia/src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h
#                                              VulkanMemoryAllocators::Make, the
#                                              only way to make the allocator that
#                                              VulkanBackendContext requires
#   develop/headers/skia/skia-defines.txt      the -D flags every user of the
#                                              headers must pass, with
#                                              -I headers/skia and
#                                              -I headers/skia/include/third_party/vulkan
#   documentation/skia/                        LICENSE texts of Skia, skcms, VMA
#
# Run after build-skia.sh. Writes distro/packages/x86_64/skia_devel-<v>.hpkg.
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
. "$HERE/pins.sh"
SKIA=$SKIA_WORK/skia
OUT=${SKIA_OUT:-$SKIA_WORK/out-x86_64}
PKG=$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package/package
VERSION=$SKIA_MILESTONE.0-1
STAGE=$SKIA_WORK/pkg-root
DEST=$ROOT/distro/packages/x86_64/skia_devel-$VERSION-x86_64.hpkg

rm -rf "$STAGE"
mkdir -p "$STAGE/develop/lib" "$STAGE/develop/headers/skia" \
	"$STAGE/documentation/skia"
cp "$OUT/libskia.a" "$STAGE/develop/lib/libskia.a"
(cd "$SKIA" && find include -name "*.h" -exec cp --parents {} "$STAGE/develop/headers/skia" \;)
# SkColorSpace.h includes skcms.h, which includes its public part.
(cd "$SKIA" && cp --parents modules/skcms/skcms.h modules/skcms/src/skcms_public.h \
	"$STAGE/develop/headers/skia")
mkdir -p "$STAGE/develop/headers/skia/src/gpu/vk/vulkanmemoryallocator"
cp "$SKIA/src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h" \
	"$STAGE/develop/headers/skia/src/gpu/vk/vulkanmemoryallocator/"

# The public defines of this build, plus the platform define of
# build-skia.sh. SKIA_IMPLEMENTATION and NDEBUG are Skia's own.
(cd "$SKIA" && bin/gn desc "$OUT" //:skia defines) \
	| grep -v -e '^SKIA_IMPLEMENTATION' -e '^NDEBUG$' \
	| sed 's/^/-D/' > "$STAGE/develop/headers/skia/skia-defines.txt"
echo "-DSK_BUILD_FOR_UNIX" >> "$STAGE/develop/headers/skia/skia-defines.txt"

cp "$SKIA/LICENSE" "$STAGE/documentation/skia/LICENSE"
cp "$SKIA/AUTHORS" "$STAGE/documentation/skia/AUTHORS"
cp "$SKIA/modules/skcms/LICENSE" "$STAGE/documentation/skia/LICENSE.skcms"
cp "$SKIA/third_party/externals/vulkanmemoryallocator/LICENSE.txt" \
	"$STAGE/documentation/skia/LICENSE.vma"

cat > "$STAGE/.PackageInfo" <<EOF
name			skia_devel
version			$VERSION
architecture		x86_64
summary			"Skia $SKIA_BRANCH for app_server (build only)"
description		"Skia $SKIA_BRANCH commit $SKIA_COMMIT ($SKIA_DATE), core and Graphite on Vulkan, static. Built by distro/skia. Linked only into app_server; not installed in the image."
packager		"R5OS <r5os@localhost>"
vendor			"R5OS"
licenses {
	"BSD (3-clause)"
	"MIT"
}
copyrights {
	"2011 Google Inc."
	"2018 Google Inc."
	"2017-2026 Advanced Micro Devices, Inc."
}
provides {
	skia_devel = $VERSION
	devel:libskia = $SKIA_MILESTONE
}
requires {
	haiku >= r1~beta5_hrev57937_5-1
}
EOF

mkdir -p "$(dirname "$DEST")"
rm -f "$DEST"
"$PKG" create -C "$STAGE" "$DEST"
ls -l "$DEST"
