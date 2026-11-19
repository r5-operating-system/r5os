#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
#
# Cross build of Skia (pins.sh) for x86_64 Haiku: the core and the
# Graphite Vulkan backend only, as a static library for app_server.
# The arguments are part 1 of docs/APP-SERVER-GPU-DRAWING.md; the ones
# added on top for the cross build are marked. Run fetch-skia.sh first.
# arm64 and riscv64 are not built.
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
. "$HERE/pins.sh"
SKIA=$SKIA_WORK/skia
OUT=${SKIA_OUT:-$SKIA_WORK/out-x86_64}
test "$(git -C "$SKIA" rev-parse HEAD)" = "$SKIA_COMMIT"

# Skia must use the compiler and libstdc++ that build app_server.
"$CROSS/bin/x86_64-unknown-haiku-g++" --version | head -1 | grep -q ' 13\.3\.0$'

# On top of part 1, for the cross build:
#   target_os = "linux"   Skia knows no Haiku; "linux" only selects the
#                         POSIX ports (SkOSFile_posix and the like). The
#                         Linux-only choices (fontconfig) are off below.
#   cc, cxx, ar           the Haiku cross compiler
#   skia_system_freetype2_include_path  R5's FreeType build package
#   skia_use_freetype_svg/zlib = false  no zlib and no SVG fonts in Skia
#   werror = false        GCC 13 warns where Skia's builders (clang) do not
#   -DSK_BUILD_FOR_UNIX   include/private/SkFeatures.h knows no __HAIKU__
#                         and falls back to SK_BUILD_FOR_MAC (CoreText,
#                         libdispatch). Every file that includes a Skia
#                         header must define it too (skia-defines.txt).
ARGS="
target_os=\"linux\" target_cpu=\"x64\"
cc=\"$CROSS/bin/x86_64-unknown-haiku-gcc\"
cxx=\"$CROSS/bin/x86_64-unknown-haiku-g++\"
ar=\"$CROSS/bin/x86_64-unknown-haiku-ar\"
is_official_build=true
skia_enable_graphite=true skia_enable_ganesh=false
skia_use_vulkan=true skia_use_gl=false skia_use_vma=true
skia_use_freetype=true skia_use_system_freetype2=true
skia_system_freetype2_include_path=\"$FREETYPE/develop/headers/freetype2\"
skia_use_freetype_svg=false skia_use_freetype_zlib=false
skia_use_fontconfig=false skia_use_icu=false skia_use_harfbuzz=false
skia_use_expat=false skia_use_zlib=false
skia_use_libpng_decode=false skia_use_libpng_encode=false
skia_use_libjpeg_turbo_decode=false skia_use_libjpeg_turbo_encode=false
skia_use_libwebp_decode=false skia_use_libwebp_encode=false
skia_use_wuffs=false skia_use_piex=false skia_use_dng_sdk=false
skia_use_perfetto=false skia_use_partition_alloc=false
skia_enable_pdf=false skia_enable_skottie=false skia_enable_svg=false
skia_enable_tools=false skia_enable_fontmgr_custom_empty=true
werror=false
extra_cflags=[\"-DSK_BUILD_FOR_UNIX\",\"-march=x86-64-v2\"]
"
ARGS=$(echo $ARGS)

cd "$SKIA"
# R5 patches, each with its reason in the file (THIRD_PARTY_NOTICES.md
# lists them). The grep guard keeps a second run from applying it twice.
if grep -q 'char const \* const name;' src/ports/SkFontHost_FreeType.cpp; then
	# A Windows checkout may hold the patch with CRLF; Skia's files are LF.
	sed 's/\r$//' "$HERE/patches/gcc13-to-array.patch" | git apply
fi
bin/gn gen "$OUT" --args="$ARGS"
# The library baseline is x86-64-v2. Skia's opts("ml3") adds
# -march=x86-64-v3 after that, and SkOpts.cpp calls Init_ml3() only when
# this baseline is below AVX2. A last -march of x86-64-v2 on the ml3
# object would mean extra_cflags overrode the per-ISA flag.
python3 - "$OUT/build.ninja" <<'PY'
import sys
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
if "x86-64-v3" not in text:
    sys.exit("build.ninja has no -march=x86-64-v3 (Skia ml3 flags missing)")
if "x86-64-v2" not in text:
    sys.exit("build.ninja has no -march=x86-64-v2 baseline")
PY
"$NINJA" -C "$OUT" skia
ls -l "$OUT/libskia.a"
"$CROSS/bin/x86_64-unknown-haiku-nm" -A "$OUT/libskia.a" | grep -q 'Init_ml3$' \
	|| { echo "libskia.a has no Init_ml3"; exit 1; }
# vfmadd is AVX2+FMA, which x86-64-v2 does not have. It must appear in an
# ml3 member, so that member was compiled with Skia's per-ISA flag.
"$CROSS/bin/x86_64-unknown-haiku-objdump" -d "$OUT/libskia.a" | awk '
	/^In archive/ { next }
	/^[^ ].*:/ { ml3 = index($0, "ml3") != 0; next }
	ml3 && /vfmadd/ { hit = 1 }
	END { exit !hit }
' || { echo "ml3 objects in libskia.a have no AVX2/FMA instruction"; exit 1; }
echo "skia: Init_ml3 is in libskia.a and the ml3 objects use AVX2/FMA"
