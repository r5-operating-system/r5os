#!/bin/bash
# Copyright 2026, R5OS Contributors.
# SPDX-License-Identifier: MIT
#
# Host AddressSanitizer check and fuzz for the kernel microcode parsers.
# find_microcode_intel() and find_microcode_amd() are copied unchanged out of
# arch_cpu.cpp, so the host runs the same code as the kernel.
# Usage: fuzz-microcode.sh [seconds]
set -eu
cd "$(dirname "$0")/.."
seconds="${1:-60}"
source="${MICROCODE_SOURCE:-distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp}"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

extract() {
	# The return type line, the function, up to its closing brace.
	awk -v name="$1" '
		index($0, name "(") == 1 { print previous; printing = 1 }
		printing { print }
		printing && /^}/ { exit }
		{ previous = $0 }
	' "$source"
}
{
	extract find_microcode_intel
	extract find_microcode_amd
} > "$work/microcode-parsers.inc"
for name in find_microcode_intel find_microcode_amd; do
	grep -q "^$name(" "$work/microcode-parsers.inc" \
		|| { echo "missing $name in $source" >&2; exit 1; }
done

# x86 reads unaligned words, so the alignment check of UBSan stays off.
g++ -std=c++17 -fsanitize=address,undefined -fno-sanitize=alignment \
	-fno-omit-frame-pointer -g -O1 -I "$work" \
	scripts/microcode-fuzz.cpp -o "$work/microcode-fuzz"

mkdir -p generated/microcode
amd=(distro/firmware/amd-ucode/microcode_amd_fam*.bin)
intel=()
if [ -d distro/firmware/intel-ucode ]; then
	intel=(distro/firmware/intel-ucode/[0-9]*)
fi
"$work/microcode-fuzz" check "${amd[@]}" "${intel[@]}" \
	| tee generated/microcode/check.log
"$work/microcode-fuzz" intel "$seconds" "${intel[@]}" \
	> generated/microcode/fuzz-intel.log 2>&1 &
intelPid=$!
"$work/microcode-fuzz" amd "$seconds" "${amd[@]}" \
	> generated/microcode/fuzz-amd.log 2>&1 &
amdPid=$!
status=0
wait "$intelPid" || status=1
wait "$amdPid" || status=1
tail -n 3 generated/microcode/fuzz-intel.log generated/microcode/fuzz-amd.log
[ "$status" -eq 0 ] || { echo fuzz_failed; exit 1; }
echo fuzz_done
