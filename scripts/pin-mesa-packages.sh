#!/usr/bin/env bash
# Point the HaikuPorts pin and the image package solver at libglvnd + Mesa 25.
# Mesa 22 is not downloaded and not installed.
set -eu
HAIKU=/home/nickr/r5os/haiku
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKGDIR="$ROOT/distro/packages/x86_64"
DL="$HAIKU/generated.x86_64/download"
PIN="$HAIKU/build/jam/repositories/HaikuPorts/x86_64"
REPO_TOOL="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package_repo/package_repo"
CACHE="$HAIKU/generated.x86_64/objects/haiku/x86_64/packaging/repositories/HaikuPorts"
OUT="$DL/r5-haikuports.repo"

needed=(
	libglvnd-1.7.0-1-x86_64.hpkg
	llvm21_runtime-21.1.8-1-x86_64.hpkg
	libdrm-2.4.124-5-x86_64.hpkg
	mesa-25.3.6-5-x86_64.hpkg
	mesa_devel-25.3.6-1-x86_64.hpkg
	mesa_lavapipe-25.3.6-5-x86_64.hpkg
	mesa_radv-25.3.6-11-x86_64.hpkg
	vulkan_loader-1.4.367-4-x86_64.hpkg
	vulkan_tools-1.4.367-2-x86_64.hpkg
	vulkanteapot-1.0.0-6-x86_64.hpkg
	wpa_supplicant-2.11.r5.8-1-x86_64.hpkg
)
mkdir -p "$DL"
for name in "${needed[@]}"; do
	if [[ ! -f "$PKGDIR/$name" ]]; then
		echo "missing $PKGDIR/$name" >&2
		exit 1
	fi
	cp -f "$PKGDIR/$name" "$DL/$name"
done

# r5servo is not stored in git; fetch from the pinned GitHub Release asset
# (or reuse local build output) and verify its sha256 strictly.
# shellcheck source=/dev/null
. "$ROOT/distro/servo/r5servo.sha256"
r5servo_dst="$DL/$R5SERVO_PKG"
r5servo_local="${SERVO_SRC:-$HOME/r5os/servo-build}/r5servo-out/$R5SERVO_PKG"
check_r5servo_hash() {
	local file="$1"
	[[ -f "$file" ]] || return 1
	local got
	got="$(sha256sum "$file" | awk '{print $1}')"
	[[ "$got" == "$R5SERVO_SHA256" ]]
}
if ! check_r5servo_hash "$r5servo_dst"; then
	if check_r5servo_hash "$r5servo_local"; then
		cp -f "$r5servo_local" "$r5servo_dst"
	else
		echo "fetching $R5SERVO_PKG from $R5SERVO_URL"
		curl -fsSL -o "${r5servo_dst}.tmp" "$R5SERVO_URL"
		mv -f "${r5servo_dst}.tmp" "$r5servo_dst"
	fi
fi
if ! check_r5servo_hash "$r5servo_dst"; then
	echo "ERROR: sha256 mismatch for $r5servo_dst (expected $R5SERVO_SHA256)" >&2
	rm -f "$r5servo_dst"
	exit 1
fi
needed+=("$R5SERVO_PKG")
# Older r5servo packages: only the pinned one stays in the repository.
rm -f "$DL/r5servo-0.8.0-1-x86_64.hpkg" "$DL/r5servo-0.8.0-2-x86_64.hpkg"
rm -f "$DL/mesa-25.3.6-1-x86_64.hpkg" "$DL/mesa-25.3.6-2-x86_64.hpkg" "$DL/mesa-25.3.6-3-x86_64.hpkg" "$DL/mesa-25.3.6-4-x86_64.hpkg" "$DL/mesa_zink-25.3.6-1-x86_64.hpkg" \
	"$DL/mesa_radv-25.3.6-1-x86_64.hpkg" "$DL/mesa_radv-25.3.6-2-x86_64.hpkg" \
	"$DL/mesa_radv-25.3.6-3-x86_64.hpkg" "$DL/mesa_radv-25.3.6-4-x86_64.hpkg" "$DL/mesa_radv-25.3.6-5-x86_64.hpkg" "$DL/mesa_radv-25.3.6-6-x86_64.hpkg" "$DL/mesa_radv-25.3.6-7-x86_64.hpkg" "$DL/mesa_radv-25.3.6-8-x86_64.hpkg" "$DL/mesa_radv-25.3.6-9-x86_64.hpkg" "$DL/mesa_radv-25.3.6-10-x86_64.hpkg" \
	"$DL/mesa_lavapipe-25.3.6-2-x86_64.hpkg" "$DL/mesa_lavapipe-25.3.6-3-x86_64.hpkg" "$DL/mesa_lavapipe-25.3.6-4-x86_64.hpkg" \
	"$DL/libdrm-2.4.124-1-x86_64.hpkg" "$DL/libdrm-2.4.124-2-x86_64.hpkg" "$DL/libdrm-2.4.124-3-x86_64.hpkg" "$DL/libdrm-2.4.124-4-x86_64.hpkg" \
	"$DL/vulkan_loader-1.4.367-2-x86_64.hpkg" "$DL/vulkan_loader-1.4.367-3-x86_64.hpkg" \
	"$DL/vulkan_tools-1.4.367-1-x86_64.hpkg" \
	"$DL/vulkanteapot-1.0.0-1-x86_64.hpkg" "$DL/vulkanteapot-1.0.0-2-x86_64.hpkg" "$DL/vulkanteapot-1.0.0-3-x86_64.hpkg" "$DL/vulkanteapot-1.0.0-4-x86_64.hpkg" "$DL/vulkanteapot-1.0.0-5-x86_64.hpkg"

python3 - "$PIN" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = (
    "\tmesa-22.0.5-3\n"
    "\tmesa_devel-22.0.5-3\n"
    "\tmesa_swpipe-22.0.5-3\n"
)
new = (
    "\tlibglvnd-1.7.0-1\n"
    "\tllvm21_runtime-21.1.8-1\n"
    "\tlibdrm-2.4.124-5\n"
    "\tmesa-25.3.6-5\n"
    "\tmesa_devel-25.3.6-1\n"
    "\tmesa_lavapipe-25.3.6-5\n"
    "\tmesa_radv-25.3.6-11\n"
    "\tvulkan_loader-1.4.367-4\n"
    "\tvulkan_tools-1.4.367-2\n"
    "\tvulkanteapot-1.0.0-6\n"
)
if old in text:
    text = text.replace(old, new, 1)
elif "\tmesa-25.3.6-1\n" in text and "\tmesa_zink-25.3.6-1\n" not in text:
    text = text.replace(
        "\tmesa-25.3.6-1\n",
        "\tlibdrm-2.4.124-5\n\tmesa_zink-25.3.6-1\n\tmesa_radv-25.3.6-11\n",
        1,
    )
elif "\tvulkan_tools-1.4.367-2\n" not in text and "\tvulkan_tools-1.4.367-1\n" not in text:
    anchor = "\tvulkan_loader-1.4.367-4\n"
    if anchor not in text:
        sys.exit("mesa pin block not found")
    text = text.replace(anchor, anchor + "\tvulkan_tools-1.4.367-2\n", 1)
text = text.replace("\tmesa_zink-25.3.6-1\n", "\tmesa-25.3.6-5\n")
text = text.replace("\tmesa-25.3.6-2\n", "\tmesa-25.3.6-5\n")
text = text.replace("\tmesa-25.3.6-3\n", "\tmesa-25.3.6-5\n")
text = text.replace("\tmesa-25.3.6-4\n", "\tmesa-25.3.6-5\n")
text = text.replace("\tmesa-25.3.6-1\n", "")
if "\tmesa-25.3.6-5\n" not in text:
    sys.exit("mesa 25.3.6-5 missing from the pin")
if "\tvulkanteapot-1.0.0-6\n" not in text and "\tvulkanteapot-1.0.0-5\n" not in text and "\tvulkanteapot-1.0.0-4\n" not in text and "\tvulkan_tools-1.4.367-2\n" in text:
    text = text.replace(
        "\tvulkan_tools-1.4.367-2\n",
        "\tvulkan_tools-1.4.367-2\n\tvulkanteapot-1.0.0-6\n",
        1,
    )
if "mesa-22.0.5-3" in text or "mesa_swpipe-22" in text:
    sys.exit("mesa 22 still in the pin")
text = text.replace("\tmesa_lavapipe-25.3.6-1\n", "\tmesa_lavapipe-25.3.6-5\n")
text = text.replace("\tmesa_lavapipe-25.3.6-2\n", "\tmesa_lavapipe-25.3.6-5\n")
text = text.replace("\tmesa_lavapipe-25.3.6-3\n", "\tmesa_lavapipe-25.3.6-5\n")
text = text.replace("\tmesa_radv-25.3.6-1\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-2\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-3\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-4\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-5\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-6\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-7\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-8\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-9\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_radv-25.3.6-10\n", "\tmesa_radv-25.3.6-11\n")
text = text.replace("\tmesa_lavapipe-25.3.6-4\n", "\tmesa_lavapipe-25.3.6-5\n")
text = text.replace("\tlibdrm-2.4.124-1\n", "\tlibdrm-2.4.124-5\n")
text = text.replace("\tlibdrm-2.4.124-2\n", "\tlibdrm-2.4.124-5\n")
# M13 step 1: -3 stays in the download folder for branches that still pin it
text = text.replace("\tlibdrm-2.4.124-3\n", "\tlibdrm-2.4.124-5\n")
text = text.replace("\tlibdrm-2.4.124-4\n", "\tlibdrm-2.4.124-5\n")
if "\tmesa_radv-25.3.6-11\n" not in text:
    sys.exit("mesa_radv 25.3.6-11 missing from the pin")
text = text.replace("\tvulkan_loader-1.4.367-1\n", "\tvulkan_loader-1.4.367-4\n")
text = text.replace("\tvulkan_loader-1.4.367-2\n", "\tvulkan_loader-1.4.367-4\n")
text = text.replace("\tvulkan_loader-1.4.367-3\n", "\tvulkan_loader-1.4.367-4\n")
text = text.replace("\tvulkan_tools-1.4.367-1\n", "\tvulkan_tools-1.4.367-2\n")
text = text.replace("\tvulkanteapot-1.0.0-1\n", "\tvulkanteapot-1.0.0-6\n")
text = text.replace("\tvulkanteapot-1.0.0-2\n", "\tvulkanteapot-1.0.0-6\n")
text = text.replace("\tvulkanteapot-1.0.0-3\n", "\tvulkanteapot-1.0.0-6\n")
text = text.replace("\tvulkanteapot-1.0.0-4\n", "\tvulkanteapot-1.0.0-6\n")
text = text.replace("\tvulkanteapot-1.0.0-5\n", "\tvulkanteapot-1.0.0-6\n")
if "\tvulkan_loader-1.4.367-4\n" not in text:
    sys.exit("vulkan loader pin missing")
text = text.replace("\tr5servo-0.8.0-1\n", "\tr5servo-0.8.0-3\n")
text = text.replace("\tr5servo-0.8.0-2\n", "\tr5servo-0.8.0-3\n")
if "\tr5servo-0.8.0-3\n" not in text:
    text = text.replace(
        "\tvulkanteapot-1.0.0-6\n",
        "\tvulkanteapot-1.0.0-6\n\tr5servo-0.8.0-3\n",
        1,
    )
if "\tr5servo-0.8.0-3\n" not in text:
    sys.exit("r5servo pin missing")
path.write_text(text, encoding="utf-8")
print("pin updated")
PY

python3 - "$PIN" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
text = text.replace("\twpa_supplicant-2.11.haiku.1-1\n",
    "\twpa_supplicant-2.11.r5.7-1\n")
text = text.replace("\twpa_supplicant-2.11.r5.1-1\n",
    "\twpa_supplicant-2.11.r5.7-1\n")
text = text.replace("\twpa_supplicant-2.11.r5.2-1\n",
    "\twpa_supplicant-2.11.r5.7-1\n")
text = text.replace("\twpa_supplicant-2.11.r5.3-1\n",
    "\twpa_supplicant-2.11.r5.7-1\n")
text = text.replace("\twpa_supplicant-2.11.r5.4-1\n",
    "\twpa_supplicant-2.11.r5.7-1\n")
text = text.replace("\twpa_supplicant-2.11.r5.5-1\n",
    "\twpa_supplicant-2.11.r5.7-1\n")
text = text.replace("\twpa_supplicant-2.11.r5.6-1\n",
    "\twpa_supplicant-2.11.r5.7-1\n")
text = text.replace("\twpa_supplicant-2.11.r5.7-1\n",
    "\twpa_supplicant-2.11.r5.8-1\n")
if "\twpa_supplicant-2.11.r5.8-1\n" not in text:
    sys.exit("wpa_supplicant pin missing")
path.write_text(text, encoding="utf-8")
print("wpa_supplicant pin")
PY

list="$(mktemp)"
if [[ -f "$CACHE" ]]; then
	"$REPO_TOOL" list -f "$CACHE" \
		| sed -n 's/^[[:space:]]*\(.*\.hpkg\)$/\1/p' \
		| grep -v -E '^(mesa|mesa_devel|mesa_swpipe|mesa_source)-22\.0\.5-3-' \
		| grep -v -E '^vulkan_loader-1\.4\.367-1-' \
		| grep -v -E '^mesa_radv-25\.3\.6-4-' \
		> "$list"
	cat > "$DL/r5-haikuports.repo.info" <<'EOF'
name HaikuPorts-BuildPackages
vendor "Haiku Project"
summary "The HaikuPorts/Build-Packages repository"
priority 1
baseurl https://eu.hpkg.haiku-os.org/haikuports/master/build-packages
identifier https://eu.hpkg.haiku-os.org/haikuports/master/build-packages/
architecture x86_64
EOF
	"$REPO_TOOL" update -C "$DL" -q -t "$CACHE" "$OUT" "$list"
else
	echo "haikuports cache missing; leaving $OUT unchanged" >&2
fi
rm -f "$list"

# Official packages stay in a Haiku Project repository. R5 packages use
# another repository because package_repo rejects a mixed vendor.
cat > "$DL/r5-opengl.repo.info" <<'EOF'
name R5OpenGL
vendor "R5OS"
summary "R5OS OpenGL packages"
priority 1
baseurl https://eu.hpkg.haiku-os.org/haikuports/master/build-packages
identifier tag:r5os.local,2026:repositories/opengl/x86_64
architecture x86_64
EOF
# R5 builds vulkan_loader. The HaikuPorts copy stays out of this repository.
r5pkgs=()
for name in "${needed[@]}"; do
	r5pkgs+=("$DL/$name")
done
"$REPO_TOOL" create -q "$DL/r5-opengl.repo.info" "${r5pkgs[@]}"
# create always writes a file named "repo" next to the info file.
mv -f "$DL/repo" "$DL/r5-opengl.repo"
echo "repo $OUT"
echo "repo $DL/r5-opengl.repo"
"$REPO_TOOL" list -f "$OUT" | grep -E 'mesa|libglvnd|llvm21' || echo "no mesa 22 in haikuports cache"
"$REPO_TOOL" list -f "$DL/r5-opengl.repo" | sed -n 's/^[[:space:]]*\(.*\.hpkg\)$/\1/p'
