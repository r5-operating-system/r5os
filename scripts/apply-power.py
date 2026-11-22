#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Power, step 2 of docs/POWER.md, from distro/power. Idempotent copy into
the Haiku tree:

- libr5power.so (distro/power/lib) into src/preferences/r5power, its
  public header <PowerRoster.h> into headers/os/app, the private
  <PowerProtocol.h> and <PowerBattery.h> into headers/private/power, with
  its SubInclude and its line in the regular and the minimum image;
- the power service (distro/power/server): Haiku's power_daemon as air/OS
  changed it, with R5's policy and admin port, over src/servers/power.

The kernel side (the profile syscalls and cpufreq version 2) is copied by
apply-kernel-bugs.py, the Power page by apply-settings.py, PowerStatus by
apply-desktop-native.py and CPU Info by apply-cpuinfo.py.

Run after apply-r5ui.py: its anchor is the libr5admin.so line that script
adds."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "haiku")
POWER = ROOT / "distro" / "power"

LIB_FILES = ("PowerBattery.cpp", "PowerRoster.cpp", "Jamfile")
PRIVATE_HEADERS = ("PowerProtocol.h", "PowerBattery.h")
SERVER_FILES = (
    "Jamfile",
    "ac_monitor.cpp", "ac_monitor.h",
    "lid_monitor.cpp", "lid_monitor.h",
    "power_button_monitor.cpp", "power_button_monitor.h",
    "power_daemon.cpp", "power_daemon.rdef",
    "power_monitor.h",
    "power_policy.cpp", "power_policy.h",
)


def install(source: pathlib.Path, dest: pathlib.Path) -> bool:
    if not source.is_file():
        raise SystemExit(f"missing {source}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    data = source.read_bytes().replace(b"\r\n", b"\n")
    if dest.is_file() and dest.read_bytes() == data:
        return False
    dest.write_bytes(data)
    return True


def install_all(pairs: list[tuple[pathlib.Path, pathlib.Path]], tag: str) -> None:
    count = sum(1 for source, dest in pairs if install(source, dest))
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

    # libr5power.so
    lib = HAIKU / "src/preferences/r5power"
    install_all([(POWER / "lib" / name, lib / name) for name in LIB_FILES],
        "libr5power")
    install_all([(POWER / "lib/PowerRoster.h",
        HAIKU / "headers/os/app/PowerRoster.h")], "libr5power header")
    install_all([(POWER / "lib" / name, HAIKU / "headers/private/power" / name)
        for name in PRIVATE_HEADERS], "libr5power private header")
    insert_once(HAIKU / "src/preferences/Jamfile",
        "SubInclude HAIKU_TOP src preferences r5admin ;\n",
        "SubInclude HAIKU_TOP src preferences r5power ;\n",
        "libr5power jam")
    for profile in ("regular", "minimum"):
        insert_once(HAIKU / "build/jam/images/definitions" / profile,
            "\t\t\tlibr5admin.so\n",
            "\t\t\tlibr5power.so\n",
            "libr5power image " + profile)

    # The power service: same name, launch job and place as Haiku's.
    server = HAIKU / "src/servers/power"
    install_all([(POWER / "server" / name, server / name)
        for name in SERVER_FILES], "power service")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
