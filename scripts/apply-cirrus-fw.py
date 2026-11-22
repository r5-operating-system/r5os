#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Install the Cirrus CS35L41 speaker firmware where the cs35l41 driver
reads it: system/non-packaged/data/firmware/cirrus (regular image).

The files and Cirrus's agreement stay unmodified (THIRD_PARTY_NOTICES.md).
The program is also reachable as cs35l41-dsp1-spk-prot-103c8a29.wmfw, a
symlink, so the search order of the specification finds it.
"""
from __future__ import annotations

import hashlib
import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
MARK = "# r5CirrusFw"
PROGRAM = "halo_cspl_RAM_revB2_29.63.1.wmfw"
PROGRAM_ALIAS = "cs35l41-dsp1-spk-prot-103c8a29.wmfw"

# sha256 as listed in THIRD_PARTY_NOTICES.md. Checked before any copy.
SHA256 = {
    PROGRAM: "c11cb3e25825c673a955a3519370ad77fbcd8bcf68808e8e34140a70caefcec4",
    "cs35l41-dsp1-spk-prot-103c8a29-spkid0-l0.bin":
        "119725a44103865c9f83283b6d7f82d7979db5a2ea943fe95b3efa58d7adec0b",
    "cs35l41-dsp1-spk-prot-103c8a29-spkid0-r0.bin":
        "59c2e439ff12c78b355da6bfe91ce36ce4be9d5144e2e06af7e0108ac5a8796b",
    "cs35l41-dsp1-spk-prot-103c8a29-spkid1-l0.bin":
        "f99008ec0a9c50470e7fedcf31786a31664171b4ccc1a3e2bdd7bb0d6bf56e2d",
    "cs35l41-dsp1-spk-prot-103c8a29-spkid1-r0.bin":
        "18636548a651ddf39e74c65bba738b9cd0c71c19c9fce6468c6cf094b286f934",
    "cs35l41-dsp1-spk-cali-103c8a29-spkid0-l0.bin":
        "5b927b28e587f42774f5949582b53f5f2399af3899ddc0fbf059ebeba62b9c91",
    "cs35l41-dsp1-spk-cali-103c8a29-spkid0-r0.bin":
        "37a472da1dbcece7c9938cc09a2e8c186e2ae8080c7d31d394682b0586f701c5",
    "cs35l41-dsp1-spk-cali-103c8a29-spkid1-l0.bin":
        "0af356009cf503159e34ecbcfba28ce305afb040068c7ca5741cb53993148e9a",
    "cs35l41-dsp1-spk-cali-103c8a29-spkid1-r0.bin":
        "a17badce983bfdce7f4773ddb14471224342452a22ef3ab64f9ef826ecfbdd5a",
}
NAMES = tuple(SHA256) + ("LICENSE.cirrus",)


def jam_block() -> str:
    lines = [
        MARK + " CS35L41 speaker firmware. The cs35l41 driver reads this path.",
        "local r5CirrusDir = [ FDirName $(HAIKU_TOP) data firmware cirrus ] ;",
        "local r5CirrusFiles =",
        *(f"\t<r5cirrus>{n}" for n in NAMES),
        "\t;",
        "SEARCH on $(r5CirrusFiles) = $(r5CirrusDir) ;",
        "AddDirectoryToHaikuImage system non-packaged data firmware cirrus ;",
        "AddFilesToHaikuImage system non-packaged data firmware cirrus",
        "\t: $(r5CirrusFiles) ;",
        "AddSymlinkToHaikuImage system non-packaged data firmware cirrus",
        f"\t: {PROGRAM} : {PROGRAM_ALIAS} ;",
        "# r5CirrusFw end",
    ]
    return "\n" + "\n".join(lines) + "\n"


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "distro/firmware/cirrus"
    for name in NAMES:
        if not (source / name).is_file():
            raise SystemExit(f"missing {source / name}")
    for name, expected in SHA256.items():
        digest = hashlib.sha256((source / name).read_bytes()).hexdigest()
        if digest != expected:
            raise SystemExit(f"{name} sha256 {digest} expected {expected}")
    print(f"sha256 ok for {len(SHA256)} cirrus files")

    destination = HAIKU / "data/firmware/cirrus"
    destination.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        data = (source / name).read_bytes()
        target = destination / name
        if not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)
    print(f"copied data/firmware/cirrus ({len(NAMES)} files)")

    jam = HAIKU / "build/jam/images/definitions/regular"
    text = jam.read_text(encoding="utf-8")
    block = jam_block()
    start = text.find(MARK)
    if start != -1:
        end_mark = "# r5CirrusFw end\n"
        end = text.find(end_mark, start)
        if end == -1:
            raise SystemExit(f"{jam}: cirrus block without its end line")
        end += len(end_mark)
        # The block starts after the newline before the mark.
        current = "\n" + text[start:end]
        if current == block:
            print(f"already {jam.name} (cirrus firmware)")
            return 0
        text = text[:start - 1] + block + text[end:]
    else:
        text = text.rstrip("\n") + "\n" + block
    jam.write_text(text, encoding="utf-8", newline="\n")
    if jam.read_text(encoding="utf-8").count(MARK + " CS35L41") != 1:
        raise SystemExit(f"{jam}: cirrus block is not there exactly once")
    print(f"patched {jam} (cirrus firmware)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
