#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Bluetooth, from distro/bluetooth. Idempotent copy into the Haiku tree:

- bluetooth_server (distro/bluetooth/server) over src/servers/bluetooth,
- libbluetooth RemoteDevice and the shared headers (distro/bluetooth/kit,
  distro/bluetooth/headers),
- bt_server_smoke_test, bt_l2cap_abi_test, bt_hid_driver_test,
  bt_l2cap_coc_test and bt_smp_crypto_test (distro/bluetooth/tests) into
  src/bin, with their Jam
  SubInclude and SYSTEM_BIN line in the regular image,
- libr5bt.so (distro/bluetooth/lib) into src/preferences/r5bt and
  <R5Bluetooth.h> into headers/os/interface, with its SubInclude and its
  line in the regular image,
- the bluetooth_hid driver (distro/kernel/.../drivers/input/bluetooth_hid)
  into src/add-ons/kernel/drivers/input, in dev/bluetooth/hid of the
  regular image,
- bluetooth_audio.media_addon (distro/media/bluetooth_audio), the bt_audio
  command and bt_audio_smoke_test, regular image only (step 5),
- bt_rfcomm_test (distro/bluetooth/tests), regular image only (step 7a),
- bt_voice_test (distro/bluetooth/tests), regular image only (step 7c),
- the Realtek RTL8761CU firmware and its licence (distro/firmware/realtek),
  unmodified, into system/non-packaged/data/firmware/h2generic, regular
  image only (docs/BLUETOOTH-2.md, step B0).

Since 25 Nov 2026 (issue #4615) the minimum image has Bluetooth too: the
stack, h2generic and bluetooth_hid, bluetooth_server with its launch job,
bluetooth_deskbar, libbluetooth.so, libr5bt.so with libmedia.so (the library
only, no media_server), the Bluetooth program, the Settings page, and the
Realtek and MediaTek Bluetooth firmware. Audio and the tests stay in the
regular image. The minimum block only counts for a minimum build, because
the regular image reads the minimum definitions first and adds its own.

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

MINIMUM_BLOCK = """
# R5 Bluetooth in the minimum image (scripts/apply-bluetooth.py, issue #4615).
# The regular image reads this file first and adds the same parts itself,
# so this block only counts for a minimum build. No audio, no tests.
if $(HAIKU_BUILD_TYPE) = minimum {
	SYSTEM_PREFERENCES += Bluetooth ;
	SYSTEM_SERVERS += bluetooth_server bluetooth_deskbar ;
	SYSTEM_NETWORK_PROTOCOLS += bluetooth l2cap ;
	SYSTEM_BT_STACK = hci btCoreData ;
	SYSTEM_ADD_ONS_DRIVERS_BT_H2 += h2generic ;
	SYSTEM_ADD_ONS_DRIVERS_BT_HID += bluetooth_hid ;
	SYSTEM_ADD_ONS_SETTINGS += settings_bluetooth ;

	local r5BtLaunch = <r5-launch>bluetooth ;
	SEARCH on $(r5BtLaunch) = [ FDirName $(HAIKU_TOP) data launch r5 ] ;
	AddDirectoryToHaikuImage system non-packaged data launch ;
	AddFilesToHaikuImage system non-packaged data launch : $(r5BtLaunch) ;

	local r5RtkBt = <r5rtk>rtl8761cu_fw.bin <r5rtk>rtl8761cu_config.bin
		<r5rtk>LICENCE.rtlwifi_firmware.txt ;
	SEARCH on $(r5RtkBt) = [ FDirName $(HAIKU_TOP) data firmware realtek ] ;
	local r5MtkBt = <r5mtk>BT_RAM_CODE_MT7961_1_2_hdr.bin
		<r5mtk>LICENCE.mediatek ;
	SEARCH on $(r5MtkBt) = [ FDirName $(HAIKU_TOP) data firmware mediatek ] ;
	AddDirectoryToHaikuImage system non-packaged data firmware h2generic ;
	AddFilesToHaikuImage system non-packaged data firmware h2generic
		: $(r5RtkBt) $(r5MtkBt) ;
}
"""
MINIMUM_LIBS = ("libbluetooth.so", "libr5bt.so", "libmedia.so")
REALTEK_FIRMWARE = ("rtl8761cu_fw.bin", "rtl8761cu_config.bin",
    "LICENCE.rtlwifi_firmware.txt")

REALTEK_BLOCK = """
# R5 Realtek RTL8761CU Bluetooth firmware (ASUS USB-BT600), from
# linux-firmware, unmodified. h2generic reads it from non-packaged
# data/firmware/h2generic (docs/BLUETOOTH-2.md, step B0).
local r5RtkDir = [ FDirName $(HAIKU_TOP) data firmware realtek ] ;
local r5RtkBt = <r5rtk>rtl8761cu_fw.bin <r5rtk>rtl8761cu_config.bin
	<r5rtk>LICENCE.rtlwifi_firmware.txt ;
SEARCH on $(r5RtkBt) = $(r5RtkDir) ;
AddDirectoryToHaikuImage system non-packaged data firmware h2generic ;
AddFilesToHaikuImage system non-packaged data firmware h2generic
	: $(r5RtkBt) ;
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
    # Steps 1c and 6: the LE socket interface and the SMP crypto, no radio.
    for test in ("bt_l2cap_coc_test", "bt_smp_crypto_test"):
        copy_dir(SRC / "tests" / test, HAIKU / "src" / "bin" / test, test)
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_hid_driver_test ;\n",
        "SubInclude HAIKU_TOP src bin bt_l2cap_coc_test ;\n",
        "bt_l2cap_coc_test jam")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_l2cap_coc_test ;\n",
        "SubInclude HAIKU_TOP src bin bt_smp_crypto_test ;\n",
        "bt_smp_crypto_test jam")
    # A game controller's buttons and axes through BJoystick (Haiku's
    # Joysticks preferences do not show HID controllers).
    copy_dir(SRC / "tests" / "gamepad", HAIKU / "src" / "bin" / "gamepad",
        "gamepad")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_smp_crypto_test ;\n",
        "SubInclude HAIKU_TOP src bin gamepad ;\n",
        "gamepad jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += bt_hid_driver_test ;\n",
        "SYSTEM_BIN += bt_l2cap_coc_test bt_smp_crypto_test ;\n",
        "bt_l2cap_coc_test image")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += bt_l2cap_coc_test bt_smp_crypto_test ;\n",
        "SYSTEM_BIN += gamepad ;\n",
        "gamepad image")
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
        "SYSTEM_ADD_ONS_SETTINGS += settings_fonts ;\n",
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
    # Step 5: bluetooth_audio.media_addon (A2DP source, SBC, AVRCP), the
    # bt_audio command (each person's headset choice) and the QEMU smoke
    # test, regular image only like the rest of Bluetooth.
    audio = ROOT / "distro" / "media" / "bluetooth_audio"
    copy_files(audio, ("A2DPSource.cpp", "A2DPSource.h", "AudioSettings.cpp",
        "AudioSettings.h", "BluetoothAudioAddOn.cpp", "BluetoothAudioNode.cpp",
        "BluetoothAudioNode.h", "BluetoothMicNode.cpp", "BluetoothMicNode.h",
        "Jamfile", "VoiceSource.cpp", "VoiceSource.h", "a2dp_proto.h",
        "msbc.h", "sbc_decoder.cpp", "sbc_decoder.h", "sbc_encoder.cpp",
        "sbc_encoder.h"), HAIKU / "src" / "add-ons" / "media" / "media-add-ons"
        / "bluetooth_audio", "bluetooth_audio")
    insert_once(HAIKU / "src" / "add-ons" / "media" / "media-add-ons"
        / "Jamfile",
        "SubInclude HAIKU_TOP src add-ons media media-add-ons dvb ;\n",
        "SubInclude HAIKU_TOP src add-ons media media-add-ons bluetooth_audio ;\n",
        "bluetooth_audio jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_ADD_ONS_MEDIA += [ FFilterByBuildFeatures\n",
        "\tbluetooth_audio.media_addon\n",
        "bluetooth_audio image")
    copy_dir(audio / "bt_audio", HAIKU / "src" / "bin" / "bt_audio",
        "bt_audio")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_hid_driver_test ;\n",
        "SubInclude HAIKU_TOP src bin bt_audio ;\n",
        "bt_audio jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += bt_hid_driver_test ;\n",
        "SYSTEM_BIN += bt_audio ;\n",
        "bt_audio image")
    copy_dir(SRC / "tests" / "bt_audio_smoke_test",
        HAIKU / "src" / "bin" / "bt_audio_smoke_test", "bt_audio_smoke_test")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_audio ;\n",
        "SubInclude HAIKU_TOP src bin bt_audio_smoke_test ;\n",
        "bt_audio_smoke_test jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += bt_audio ;\n",
        "SYSTEM_BIN += bt_audio_smoke_test ;\n",
        "bt_audio_smoke_test image")
    # Step 7a: RFCOMM framing, the mux back to back and two links over
    # ports, no radio.
    copy_dir(SRC / "tests" / "bt_rfcomm_test",
        HAIKU / "src" / "bin" / "bt_rfcomm_test", "bt_rfcomm_test")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_audio_smoke_test ;\n",
        "SubInclude HAIKU_TOP src bin bt_rfcomm_test ;\n",
        "bt_rfcomm_test jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += bt_audio_smoke_test ;\n",
        "SYSTEM_BIN += bt_rfcomm_test ;\n",
        "bt_rfcomm_test image")
    # Step 7c: the voice path without a radio (mSBC and SBC decode, the
    # kernel's SCO parsers, the HCI voice commands, the microphone node).
    copy_dir(SRC / "tests" / "bt_voice_test",
        HAIKU / "src" / "bin" / "bt_voice_test", "bt_voice_test")
    insert_once(HAIKU / "src" / "bin" / "Jamfile",
        "SubInclude HAIKU_TOP src bin bt_rfcomm_test ;\n",
        "SubInclude HAIKU_TOP src bin bt_voice_test ;\n",
        "bt_voice_test jam")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_BIN += bt_rfcomm_test ;\n",
        "SYSTEM_BIN += bt_voice_test ;\n",
        "bt_voice_test image")
    # Step B0 of docs/BLUETOOTH-2.md: the Realtek firmware, byte for byte.
    realtek = ROOT / "distro" / "firmware" / "realtek"
    realtekDest = HAIKU / "data" / "firmware" / "realtek"
    realtekDest.mkdir(parents=True, exist_ok=True)
    for name in REALTEK_FIRMWARE:
        source = realtek / name
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        (realtekDest / name).write_bytes(source.read_bytes())
    print("copied Realtek firmware")
    insert_once(HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "AddFilesToHaikuImage home config settings : $(shortcutsSettingsFile) ;\n",
        REALTEK_BLOCK, "Realtek firmware image")

    # The minimum image (issue #4615). Its library list is its own rule,
    # which the regular image replaces, so these lines need no guard.
    minimum = HAIKU / "build" / "jam" / "images" / "definitions" / "minimum"
    for lib in MINIMUM_LIBS:
        insert_once(minimum, "\t\t\tlibr5ui.so\n", "\t\t\t" + lib + "\n",
            lib + " minimum image")
    insert_once(minimum, "SYSTEM_ADD_ONS_SETTINGS += settings_fonts ;\n",
        MINIMUM_BLOCK, "Bluetooth minimum image")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
