#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Game controllers, from distro/game and distro/kernel (docs/GAME-CONTROLLERS.md,
option B; docs/BLUETOOTH.md, step 8). Idempotent copy into the Haiku tree:

- the two RenkuOS joystick fixes and R5_JOYSTICK_GET_INFO: joystick_driver.h,
  libdevice's Joystick.cpp, hid_shared's JoystickProtocolHandler, and the
  HIDDevice of usb_hid and hyperv/hid, and usb_hid's QuirkyDevices.cpp with
  the R5 descriptors of the Switch Pro Controller and GIP (phase 2) (i2c_hid comes with
  apply-kernel-bugs.py, bluetooth_hid with apply-bluetooth.py);
- libr5game.so (distro/game/lib) into src/preferences/r5game and its
  headers <GameController.h> and <GameControllerView.h> into
  headers/os/device, with its SubInclude and its line in the regular and
  the minimum image;
- the unmodified SDL_GameControllerDB and its LICENSE in
  system/non-packaged/data/game_controllers of the regular image;
- the Settings add-on settings_gamecontrollers and gamecontroller_test,
  regular image only.

Run after apply-bluetooth.py: its anchors are lines that script adds."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "haiku")
GAME = ROOT / "distro" / "game"
KERNEL = ROOT / "distro" / "kernel"

MINIMUM_BLOCK = """
# R5 game controllers in the minimum image (scripts/apply-game.py, issue
# #4615): the Settings page that the Bluetooth page opens, and the mapping
# database. Only for a minimum build; the regular image adds its own.
if $(HAIKU_BUILD_TYPE) = minimum {
	SYSTEM_ADD_ONS_SETTINGS += settings_gamecontrollers ;
	local r5GameDB = <r5gamedb>gamecontrollerdb.txt <r5gamedb>LICENSE ;
	SEARCH on $(r5GameDB) = [ FDirName $(HAIKU_TOP) data game_controllers ] ;
	AddDirectoryToHaikuImage system non-packaged data game_controllers ;
	AddFilesToHaikuImage system non-packaged data game_controllers
		: $(r5GameDB) ;
}
"""

DATA_BLOCK = """
# R5 game controller mapping database (distro/game/data), unmodified
# SDL_GameControllerDB with its LICENSE (zlib). Regular image only.
local r5GameDBDir = [ FDirName $(HAIKU_TOP) data game_controllers ] ;
local r5GameDB = <r5gamedb>gamecontrollerdb.txt <r5gamedb>LICENSE ;
SEARCH on $(r5GameDB) = $(r5GameDBDir) ;
AddDirectoryToHaikuImage system non-packaged data game_controllers ;
AddFilesToHaikuImage system non-packaged data game_controllers : $(r5GameDB) ;
"""


def install(source: pathlib.Path, dest: pathlib.Path, binary: bool = False) -> bool:
    if not source.is_file():
        raise SystemExit(f"missing {source}")
    if not dest.parent.is_dir():
        dest.parent.mkdir(parents=True, exist_ok=True)
    data = source.read_bytes()
    if not binary:
        data = data.replace(b"\r\n", b"\n")
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

    # The driver side and the Device Kit fix.
    kernel_files = [
        "src/add-ons/kernel/drivers/input/hid_shared/JoystickProtocolHandler.cpp",
        "src/add-ons/kernel/drivers/input/hid_shared/JoystickProtocolHandler.h",
        "src/add-ons/kernel/drivers/input/hid_shared/R5HIDReports.h",
        "src/add-ons/kernel/drivers/input/usb_hid/HIDDevice.cpp",
        "src/add-ons/kernel/drivers/input/usb_hid/HIDDevice.h",
        "src/add-ons/kernel/drivers/input/usb_hid/QuirkyDevices.cpp",
        "src/add-ons/kernel/drivers/input/hyperv/hid/HIDDevice.h",
    ]
    install_all([(KERNEL / f, HAIKU / f) for f in kernel_files],
        "joystick driver")
    install_all([
        (GAME / "headers/private/device/joystick_driver.h",
            HAIKU / "headers/private/device/joystick_driver.h"),
        (GAME / "kit/Joystick.cpp", HAIKU / "src/kits/device/Joystick.cpp"),
    ], "joystick kit")

    # libr5game.so.
    lib = HAIKU / "src/preferences/r5game"
    install_all([(GAME / "lib" / name, lib / name) for name in (
        "GameController.cpp", "GameControllerMapping.cpp",
        "GameControllerMapping.h", "GameControllerPictures.cpp",
        "GameControllerPictures.h", "GameControllerPictureData.h",
        "GameControllerView.cpp", "Jamfile", "libr5game.rdef",
        "GameControllerReports.cpp", "GameControllerReports.h",
        "GameControllerMotionView.cpp", "GameControllerFeel.cpp",
        "GameControllerFeel.h")],
        "libr5game")
    install_all([(GAME / "lib" / name, HAIKU / "headers/os/device" / name)
        for name in ("GameController.h", "GameControllerView.h",
            "GameControllerMotionView.h")],
        "libr5game header")
    insert_once(HAIKU / "src/preferences/Jamfile",
        "SubInclude HAIKU_TOP src preferences r5ui ;\n",
        "SubInclude HAIKU_TOP src preferences r5game ;\n",
        "libr5game jam")
    for profile in ("regular", "minimum"):
        insert_once(HAIKU / "build/jam/images/definitions" / profile,
            "\t\t\tlibr5ui.so\n",
            "\t\t\tlibr5game.so\n",
            "libr5game image " + profile)

    # The mapping database, unmodified.
    data = HAIKU / "data/game_controllers"
    install_all([(GAME / "data" / name, data / name)
        for name in ("gamecontrollerdb.txt", "LICENSE")], "mapping database")
    insert_once(HAIKU / "build/jam/images/definitions/regular",
        "AddFilesToHaikuImage home config settings : $(shortcutsSettingsFile) ;\n",
        DATA_BLOCK,
        "mapping database image")

    # The Settings page (its source is in distro/settings, which
    # apply-settings.py copies) and the test.
    insert_once(HAIKU / "build/jam/images/definitions/regular",
        "SYSTEM_ADD_ONS_SETTINGS += settings_bluetooth ;\n",
        "SYSTEM_ADD_ONS_SETTINGS += settings_gamecontrollers ;\n",
        "settings_gamecontrollers image")
    test = HAIKU / "src/bin/gamecontroller_test"
    install_all([(GAME / "tests/gamecontroller_test" / name, test / name)
        for name in ("gamecontroller_test.cpp", "Jamfile")],
        "gamecontroller_test")
    insert_once(HAIKU / "src/bin/Jamfile",
        "SubInclude HAIKU_TOP src bin gamepad ;\n",
        "SubInclude HAIKU_TOP src bin gamecontroller_test ;\n",
        "gamecontroller_test jam")
    insert_once(HAIKU / "build/jam/images/definitions/regular",
        "SYSTEM_BIN += gamepad ;\n",
        "SYSTEM_BIN += gamecontroller_test ;\n",
        "gamecontroller_test image")

    # Rumble, trigger effects and motion (option C phase 1): their test.
    feel = HAIKU / "src/bin/gamecontroller_feel_test"
    install_all([(GAME / "tests/gamecontroller_feel_test" / name,
            feel / name)
        for name in ("gamecontroller_feel_test.cpp", "Jamfile")],
        "gamecontroller_feel_test")
    insert_once(HAIKU / "src/bin/Jamfile",
        "SubInclude HAIKU_TOP src bin gamecontroller_test ;\n",
        "SubInclude HAIKU_TOP src bin gamecontroller_feel_test ;\n",
        "gamecontroller_feel_test jam")
    insert_once(HAIKU / "build/jam/images/definitions/regular",
        "SYSTEM_BIN += gamecontroller_test ;\n",
        "SYSTEM_BIN += gamecontroller_feel_test ;\n",
        "gamecontroller_feel_test image")

    # The minimum image (issue #4615): the page the Bluetooth page opens.
    insert_once(HAIKU / "build/jam/images/definitions/minimum",
        "SYSTEM_ADD_ONS_SETTINGS += settings_fonts ;\n",
        MINIMUM_BLOCK, "game controllers minimum image")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
