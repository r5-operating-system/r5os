#!/usr/bin/env python3
"""Install the CPU microcode (AMD and Intel) on the boot volume.

The bootloader reads system/non-packaged/data/firmware/amd-ucode and
intel-ucode before packagefs exists. The files and the licenses stay
unmodified. The block goes into the minimum image, which the regular image
includes, so both images get it once.
"""
from __future__ import annotations

import pathlib
import re
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
MARK = "r5Microcode"
OLD_MARK = "r5AmdUcode"

# Glob picks up every file the copy below put in the tree.
JAM_BLOCK = """
# R5OS CPU microcode, unmodified, with the licenses. The bootloader reads
# these paths. scripts/apply-amd-ucode.py copies the files from distro.
local r5MicrocodeAmdDir = [ FDirName $(HAIKU_TOP) data firmware amd-ucode ] ;
local r5MicrocodeAmd = [ Glob $(r5MicrocodeAmdDir) : microcode_amd_fam*.bin ]
	[ Glob $(r5MicrocodeAmdDir) : LICENSE.amd-ucode ] ;
r5MicrocodeAmd = $(r5MicrocodeAmd:G=r5MicrocodeAmd) ;
local r5MicrocodeIntelDir = [ FDirName $(HAIKU_TOP) data firmware intel-ucode ] ;
local r5MicrocodeIntel = [ Glob $(r5MicrocodeIntelDir) : ??-??-?? ]
	[ Glob $(r5MicrocodeIntelDir) : LICENSE.intel-ucode ] ;
r5MicrocodeIntel = $(r5MicrocodeIntel:G=r5MicrocodeIntel) ;

AddDirectoryToHaikuImage system non-packaged data firmware ;
AddDirectoryToHaikuImage system non-packaged data firmware amd-ucode ;
AddFilesToHaikuImage system non-packaged data firmware amd-ucode
	: $(r5MicrocodeAmd) ;
if $(r5MicrocodeIntel) {
	AddDirectoryToHaikuImage system non-packaged data firmware intel-ucode ;
	AddFilesToHaikuImage system non-packaged data firmware intel-ucode
		: $(r5MicrocodeIntel) ;
}
"""

# The block that earlier versions of this script put in the regular image.
OLD_BLOCK = """
# R5OS AMD family 19h microcode. The bootloader reads this path.
local r5AmdUcodeDir = [ FDirName $(HAIKU_TOP) data firmware amd-ucode ] ;
local r5AmdUcode = <r5ucode>microcode_amd_fam19h.bin ;
local r5AmdUcodeLicense = <r5ucode>LICENSE.amd-ucode ;
SEARCH on $(r5AmdUcode) $(r5AmdUcodeLicense) = $(r5AmdUcodeDir) ;

AddDirectoryToHaikuImage system non-packaged data firmware ;
AddDirectoryToHaikuImage system non-packaged data firmware amd-ucode ;
AddFilesToHaikuImage system non-packaged data firmware amd-ucode
	: $(r5AmdUcode) ;
AddFilesToHaikuImage system non-packaged data firmware amd-ucode
	: $(r5AmdUcodeLicense) ;
"""

INTEL_NAME = re.compile(r"^[0-9a-f]{2}-[0-9a-f]{2}-[0-9a-f]{2}$")


def text_bytes(path: pathlib.Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def sync(destination: pathlib.Path, blobs: list[pathlib.Path],
        license_file: pathlib.Path) -> int:
    """Copy the blobs byte for byte and the license; drop files no longer in distro."""
    destination.mkdir(parents=True, exist_ok=True)
    wanted = {path.name for path in blobs} | {license_file.name}
    for stale in destination.iterdir():
        if stale.is_file() and stale.name not in wanted:
            stale.unlink()
            print(f"removed stale {destination.name}/{stale.name}")
    total = 0
    for blob in blobs:
        data = blob.read_bytes()
        target = destination / blob.name
        if not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)
        total += len(data)
    (destination / license_file.name).write_bytes(text_bytes(license_file))
    print(f"copied {len(blobs)} files and {license_file.name} to "
        f"data/firmware/{destination.name} ({total} bytes)")
    return total


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    firmware = root / "distro/firmware"

    amd = firmware / "amd-ucode"
    amd_license = amd / "LICENSE.amd-ucode"
    amd_blobs = sorted(amd.glob("microcode_amd_fam*.bin"))
    if not amd_license.is_file() or not amd_blobs:
        raise SystemExit(f"missing AMD microcode in {amd}")
    for blob in amd_blobs:
        if blob.read_bytes()[:4] != b"\x44\x4d\x41\x00":
            raise SystemExit(f"{blob.name} magic is not AMD")
    total = sync(HAIKU / "data/firmware/amd-ucode", amd_blobs, amd_license)

    intel = firmware / "intel-ucode"
    if intel.is_dir():
        intel_license = intel / "LICENSE.intel-ucode"
        intel_blobs = sorted(path for path in intel.iterdir()
            if INTEL_NAME.match(path.name))
        others = sorted(path.name for path in intel.iterdir()
            if path.name != intel_license.name and not INTEL_NAME.match(path.name))
        if not intel_license.is_file() or not intel_blobs or others:
            raise SystemExit(f"Intel microcode in {intel}: license, files or "
                f"unexpected names {others}")
        for blob in intel_blobs:
            # header_version 1 and loader_revision 1 start every Intel patch.
            data = blob.read_bytes()
            if len(data) < 48 or data[:4] != b"\x01\x00\x00\x00" \
                    or data[20:24] != b"\x01\x00\x00\x00":
                raise SystemExit(f"{blob.name} is not an Intel microcode file")
        total += sync(HAIKU / "data/firmware/intel-ucode", intel_blobs, intel_license)
    print(f"microcode on the image: {total} bytes")

    regular = HAIKU / "build/jam/images/definitions/regular"
    text = regular.read_text(encoding="utf-8")
    if OLD_MARK in text:
        if OLD_BLOCK not in text:
            raise SystemExit(f"old AMD microcode block in {regular} is not the expected one")
        regular.write_text(text.replace(OLD_BLOCK, "", 1), encoding="utf-8", newline="\n")
        print(f"removed the old AMD microcode block from {regular.name}")

    minimum = HAIKU / "build/jam/images/definitions/minimum"
    text = minimum.read_text(encoding="utf-8")
    if MARK in text:
        print(f"already {minimum.name} (microcode)")
        return 0
    anchor = "AddFilesToHaikuImage system settings network : $(networkSettingsFiles) ;\n"
    if text.count(anchor) != 1:
        raise SystemExit(f"pattern not found once in {minimum} (microcode)")
    minimum.write_text(text.replace(anchor, anchor + JAM_BLOCK, 1), encoding="utf-8",
        newline="\n")
    print(f"patched {minimum} (microcode)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
