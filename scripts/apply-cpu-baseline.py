#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Copy the x86-64-v2 baseline into the Haiku tree.

ArchitectureRules adds -march=x86-64-v2 for user space and the kernel, and
strips it from the boot loaders. The CPU check is the shared boot arch_cpu.cpp,
called from the EFI loader and from the BIOS loader.
"""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")

FILES = (
    ("distro/jam/ArchitectureRules", "build/jam/ArchitectureRules"),
    ("distro/boot/arch/x86/arch_cpu.cpp", "src/system/boot/arch/x86/arch_cpu.cpp"),
    (
        "distro/boot/bios_ia32/cpu.cpp",
        "src/system/boot/platform/bios_ia32/cpu.cpp",
    ),
)


def install(src: pathlib.Path, dst: pathlib.Path) -> None:
    if not src.is_file():
        raise SystemExit(f"missing {src}")
    if not dst.parent.is_dir():
        raise SystemExit(f"missing {dst.parent}")
    data = src.read_bytes().replace(b"\r\n", b"\n")
    if dst.is_file() and dst.read_bytes().replace(b"\r\n", b"\n") == data:
        print(f"already {dst.relative_to(HAIKU)} (x86-64-v2)")
        return
    dst.write_bytes(data)
    print(f"copied {dst.relative_to(HAIKU)} (x86-64-v2)")


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    for source, destination in FILES:
        install(root / source, HAIKU / destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
