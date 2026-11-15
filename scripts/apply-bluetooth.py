#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Bluetooth, from distro/bluetooth. Idempotent copy into the Haiku tree:

- bluetooth_server (distro/bluetooth/server) over src/servers/bluetooth,
- bt_server_smoke_test (distro/bluetooth/tests) into src/bin, with its Jam
  SubInclude and SYSTEM_BIN line in the regular image.

docs/BLUETOOTH.md is the plan."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "haiku")
SRC = ROOT / "distro" / "bluetooth"


def copy_dir(source: pathlib.Path, dest: pathlib.Path, tag: str) -> None:
    if not source.is_dir():
        raise SystemExit(f"missing {source}")
    dest.mkdir(parents=True, exist_ok=True)
    count = 0
    for path in sorted(source.iterdir()):
        if not path.is_file():
            continue
        data = path.read_bytes().replace(b"\r\n", b"\n")
        target = dest / path.name
        if target.is_file() and target.read_bytes() == data:
            continue
        target.write_bytes(data)
        count += 1
    print(f"copied {count} {tag} files")


def insert_once(path: pathlib.Path, anchor: str, line: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if line in text:
        if text.count(line) != 1:
            raise SystemExit(f"{tag} is in {path} more than once")
        print(f"already {path.name} ({tag})")
        return
    if text.count(anchor) != 1:
        raise SystemExit(f"{tag} anchor not found once in {path}")
    path.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8",
        newline="\n")
    print(f"patched {path.name} ({tag})")


def main() -> int:
    if not HAIKU.is_dir():
        raise SystemExit(f"missing {HAIKU}")
    copy_dir(SRC / "server", HAIKU / "src" / "servers" / "bluetooth",
        "bluetooth_server")
    copy_dir(SRC / "tests" / "bt_server_smoke_test",
        HAIKU / "src" / "bin" / "bt_server_smoke_test", "bt_server_smoke_test")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin writembr ;\n",
        "SubInclude HAIKU_TOP src bin bt_server_smoke_test ;\n",
        "bt_server_smoke_test jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += userguide quicktour welcome ;\n",
        "SYSTEM_BIN += bt_server_smoke_test ;\n",
        "bt_server_smoke_test image")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
