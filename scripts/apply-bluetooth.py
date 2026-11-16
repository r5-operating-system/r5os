#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Bluetooth, from distro/bluetooth. Idempotent copy into the Haiku tree:

- bluetooth_server (distro/bluetooth/server) over src/servers/bluetooth,
- libbluetooth RemoteDevice and the shared headers (distro/bluetooth/kit,
  distro/bluetooth/headers),
- bt_server_smoke_test and bt_l2cap_abi_test (distro/bluetooth/tests) into
  src/bin, with their Jam
  SubInclude and SYSTEM_BIN line in the regular image,
- libr5bt.so (distro/bluetooth/lib) into src/preferences/r5bt and
  <R5Bluetooth.h> into headers/os/interface, with its SubInclude and its
  line in the regular image,
- the bluetooth_hid driver (distro/kernel/.../drivers/input/bluetooth_hid)
  into src/add-ons/kernel/drivers/input, in dev/bluetooth/hid of the
  regular image.

docs/BLUETOOTH.md is the plan."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "haiku")
SRC = ROOT / "distro" / "bluetooth"

LAUNCH_BLOCK = """
# R5 bluetooth_server launch job (distro/bluetooth/launch, regular image only).
local r5BtLaunch = <r5-launch>bluetooth ;
SEARCH on $(r5BtLaunch) = [ FDirName $(HAIKU_TOP) data launch r5 ] ;
AddDirectoryToHaikuImage system non-packaged data launch ;
AddFilesToHaikuImage system non-packaged data launch : $(r5BtLaunch) ;
"""



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


def copy_files(source: pathlib.Path, names: tuple[str, ...],
        dest: pathlib.Path, tag: str) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    count = 0
    for name in names:
        path = source / name
        if not path.is_file():
            raise SystemExit(f"missing {path}")
        data = path.read_bytes().replace(b"\r\n", b"\n")
        target = dest / name
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
    # libbluetooth: RemoteDevice (step 1b) and the headers it shares with
    # the server. Only these files; the rest of the kit stays Haiku's.
    copy_dir(SRC / "kit", HAIKU / "src" / "kits" / "bluetooth",
        "libbluetooth")
    copy_dir(SRC / "headers" / "os" / "bluetooth",
        HAIKU / "headers" / "os" / "bluetooth", "bluetooth headers")
    copy_dir(SRC / "headers" / "private" / "bluetooth",
        HAIKU / "headers" / "private" / "bluetooth", "bluetooth private headers")
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
    copy_dir(SRC / "tests" / "bt_l2cap_abi_test",
        HAIKU / "src" / "bin" / "bt_l2cap_abi_test", "bt_l2cap_abi_test")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_server_smoke_test ;\n",
        "SubInclude HAIKU_TOP src bin bt_l2cap_abi_test ;\n",
        "bt_l2cap_abi_test jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += bt_server_smoke_test ;\n",
        "SYSTEM_BIN += bt_l2cap_abi_test ;\n",
        "bt_l2cap_abi_test image")
    # Step 4: the bluetooth_hid driver test (gamepad, BJoystick, mouse).
    copy_dir(SRC / "tests" / "bt_hid_driver_test",
        HAIKU / "src" / "bin" / "bt_hid_driver_test", "bt_hid_driver_test")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_l2cap_abi_test ;\n",
        "SubInclude HAIKU_TOP src bin bt_hid_driver_test ;\n",
        "bt_hid_driver_test jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += bt_l2cap_abi_test ;\n",
        "SYSTEM_BIN += bt_hid_driver_test ;\n",
        "bt_hid_driver_test image")
    # libr5bt.so (step 3a): the sources into src/preferences/r5bt, the
    # public header next to R5Icons.h and Card.h. It needs libbluetooth,
    # so it is in the regular image only, like the rest of Bluetooth.
    copy_files(SRC / "lib", ("BluetoothController.cpp", "BluetoothView.cpp",
        "Jamfile"), HAIKU / "src" / "preferences" / "r5bt", "libr5bt")
    copy_files(SRC / "lib", ("R5Bluetooth.h",),
        HAIKU / "headers" / "os" / "interface", "libr5bt header")
    insert_once(HAIKU / "src" / "preferences" / "Jamfile",
        "SubInclude HAIKU_TOP src preferences r5ui ;\n",
        "SubInclude HAIKU_TOP src preferences r5bt ;\n",
        "libr5bt jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "\t\t\tlibr5ui.so\n",
        "\t\t\tlibr5bt.so\n",
        "libr5bt image")
    # The Bluetooth page of Settings is an add-on, regular image only; the
    # minimum image's Settings runs without it.
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_ADD_ONS_SETTINGS += settings_look ;\n",
        "SYSTEM_ADD_ONS_SETTINGS += settings_bluetooth ;\n",
        "settings_bluetooth image")
    # bluetooth_server as a system service from boot (uid 0), regular image
    # only: launch_daemon also reads non-packaged/data/launch.
    copy_files(SRC / "launch", ("bluetooth",),
        HAIKU / "data" / "launch" / "r5", "bluetooth launch job")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "AddFilesToHaikuImage home config settings : $(shortcutsSettingsFile) ;\n",
        LAUNCH_BLOCK,
        "bluetooth launch image")
    # Step 3b: the Bluetooth program replaces Haiku's Bluetooth preferences
    # (its Jamfile over theirs; their Bluetooth.rdef, signature and icon,
    # stays), and the Deskbar item bluetooth_deskbar with its user launch job.
    copy_files(SRC / "app", ("BluetoothApp.cpp", "Jamfile"),
        HAIKU / "src" / "preferences" / "bluetooth", "Bluetooth program")
    copy_files(SRC / "deskbar", ("BluetoothDeskbar.cpp", "BluetoothDeskbar.rdef",
        "Jamfile"), HAIKU / "src" / "servers" / "bluetooth_deskbar",
        "bluetooth_deskbar")
    insert_once(HAIKU / "src" / "servers" / "Jamfile",
        "SubInclude HAIKU_TOP src servers bluetooth ;\n",
        "SubInclude HAIKU_TOP src servers bluetooth_deskbar ;\n",
        "bluetooth_deskbar jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "\tbluetooth_server\n",
        "\tbluetooth_deskbar\n",
        "bluetooth_deskbar image")
    # The Deskbar item is started by Login at Sign in (distro/login), like the
    # AirPlay item: R5 never starts the target desktop jobs.
    # Step 4: the bluetooth_hid driver (Classic HID on hid_shared), fed by
    # bluetooth_server. Regular image only: the variable is set in the
    # regular definitions, so the minimum image's package stays without it.
    copy_dir(ROOT / "distro" / "kernel" / "src" / "add-ons" / "kernel"
        / "drivers" / "input" / "bluetooth_hid",
        HAIKU / "src" / "add-ons" / "kernel" / "drivers" / "input"
        / "bluetooth_hid", "bluetooth_hid")
    insert_once(HAIKU / "src" / "add-ons" / "kernel" / "drivers" / "input"
        / "Jamfile",
        "SubInclude HAIKU_TOP src add-ons kernel drivers input i2c_hid ;\n",
        "SubInclude HAIKU_TOP src add-ons kernel drivers input bluetooth_hid ;\n",
        "bluetooth_hid jam")
    insert_once(HAIKU / "build" / "jam" / "packages" / "Haiku",
        "AddDriversToPackage bluetooth h2 :\n\t$(SYSTEM_ADD_ONS_DRIVERS_BT_H2) ;\n",
        "AddDriversToPackage bluetooth hid :\n\t$(SYSTEM_ADD_ONS_DRIVERS_BT_HID) ;\n",
        "bluetooth_hid package")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_ADD_ONS_DRIVERS_BT_H2 +=\n\th2generic\n;\n",
        "SYSTEM_ADD_ONS_DRIVERS_BT_HID += bluetooth_hid ;\n",
        "bluetooth_hid image")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
