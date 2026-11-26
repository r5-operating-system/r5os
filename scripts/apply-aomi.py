#!/usr/bin/env python3
"""Aomi (MIT). Idempotent copy into the Haiku tree, Jam SubInclude,
and the regular image app list. WebPositive stays installed."""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[1] / "haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "apps" / "aomi"


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def copy_sources() -> None:
    dest = HAIKU / "src" / "apps" / "aomi"
    dest.mkdir(parents=True, exist_ok=True)
    count = 0
    for path in sorted(SRC.iterdir()):
        if not path.is_file():
            continue
        data = path.read_bytes().replace(b"\r\n", b"\n")
        target = dest / path.name
        if target.is_file() and target.read_bytes() == data:
            continue
        target.write_bytes(data)
        count += 1
    print(f"copied {count} Aomi files")


def patch_jams() -> None:
    replace_once(
        HAIKU / "src" / "apps" / "Jamfile",
        "HaikuSubInclude cpuinfo ;\n",
        "HaikuSubInclude cpuinfo ;\nHaikuSubInclude aomi ;\n",
        "aomi jam",
    )
    regular = HAIKU / "build" / "jam" / "images" / "definitions" / "regular"
    replace_once(
        regular,
        "\tCPUInfo\n",
        "\tCPUInfo\n\tAomi\n",
        "Aomi SYSTEM_APPS",
    )


def main() -> int:
    if not HAIKU.is_dir():
        raise SystemExit(f"missing {HAIKU}")
    copy_sources()
    patch_jams()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
