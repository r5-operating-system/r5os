# Bluetooth on R5

This is the plan for Bluetooth on R5 Operating System: what is already in the tree, where each part goes, and the steps that get there. Each step is its own small pull request with a pass rule. The owner approved this plan; the choices in section 4 are the owner's.

Nothing in this document is built yet unless it says so.

## 0. Before step 0: VirtualBox USB passthrough and test hardware

QEMU has no Bluetooth hardware. Before step 0 we check USB passthrough in VirtualBox.

- The Windows host has the Oracle VirtualBox Extension Pack 7.2.22 (`Usable: true` in `VBoxManage list extpacks`). The `R5OS` VM already has `xHCI USB: enabled`, with no USB filter yet.
- Chosen: pass the built-in MT7922 of the 5625U to the VM (section 4, choice 4). The other options, for reference:
  - **CSR** (for example CSR8510 A10, `0x0a12:0x0001`, generic USB class `0xE0/0x01/0x01`): works in `h2generic` without extra firmware. Cheap CSR8510 clones often do not work, also not on Linux; buy one from a known brand.
  - **Realtek** (`0x0bda`) and **Qualcomm** (`0x0cf3`): no firmware loader before step 7, so they do not work as a dongle.
  - **Intel** (`0x8087`): needs `bt_firmware` (see `h2generic.cpp`).
  - **MediaTek MT7922** (`0x13d3:0x3567` in the 5625U): `h2mediatek.cpp` is already in `distro/`. It is the built-in radio of the 5625U, which we also test bare metal from the USB stick. Passing it to VirtualBox takes Bluetooth away from Windows while the VM runs.
- **5625U bare metal (MT7922):** USB sticks are costly, so hardware tests on the 5625U are bundled into one stick per few steps. The owner writes the stick with `scripts/write-stick.py`, after VirtualBox works first.

## 1. Rules from `AGENTS.md` and where the files are

What is in `distro/` and what is only in `haiku/` (checked against `origin/main`):

- **Already in `distro/`:**
  - `distro/kernel/src/add-ons/kernel/bluetooth/hci/bluetooth.cpp`
  - `distro/kernel/src/add-ons/kernel/drivers/bluetooth/h2/h2generic/`: `h2generic.cpp`, `h2mediatek.cpp`, `h2mediatek.h`, `h2transactions.cpp`, `Jamfile`. `scripts/apply-kernel-bugs.py` copies them into the Haiku tree. The MediaTek loader came from air/OS.
- **Only in `haiku/`** (copy into `distro/` first):
  - `haiku/src/servers/bluetooth/` (`BluetoothServer.cpp`, `DeviceManager.cpp`, `LocalDeviceImpl.cpp`, `SDPServer.cpp`, `DeskbarReplicant.cpp` and others) → `distro/bluetooth/server/`, at the start of step 0.
  - `haiku/src/add-ons/kernel/network/protocols/bluetooth/l2cap/` → `distro/kernel/src/add-ons/kernel/network/protocols/bluetooth/l2cap/`, at the start of step 1b.
  - `haiku/src/add-ons/kernel/bluetooth/btCoreData/`, if a step needs a change there → `distro/kernel/src/add-ons/kernel/bluetooth/btCoreData/`.
  - `haiku/headers/os/bluetooth/RemoteDevice.h` and `haiku/src/kits/bluetooth/RemoteDevice.cpp`, only for additive calls → `distro/`.
- A copied Haiku file keeps its original MIT header and copyright. R5 changes are marked with `SPDX-Snippet` tags, `MIT`, copyright 2026 R5OS Contributors.
- Only the regular image has Bluetooth. The minimum image ships no Bluetooth server, stack or driver.

The MT7922 firmware is already in the regular image (checked):

- `distro/firmware/mediatek/BT_RAM_CODE_MT7961_1_2_hdr.bin` and `LICENCE.mediatek`, from linux-firmware, unmodified.
- `scripts/apply-wireless.py` (lines 33 and 36–53) adds them to `system/non-packaged/data/firmware/h2generic` in the regular image. `scripts/apply-overlay.sh` (line 85) runs that script.
- `h2mediatek.cpp` reads from `firmware/h2generic/` under the data directory (line 87) and builds the name `BT_RAM_CODE_MT<chip>_1_<version + 1>_hdr.bin` (line 455). For the MT7922 that is chip `7961`, so it asks for the file above.
- The MT7922 has no entry of its own in the `h2generic` device table. It is found through the generic Bluetooth class entry (`h2generic.cpp`, line 57), and `h2mediatek` only talks to it because the vendor `0x13d3` is in its list (`h2mediatek.cpp`, line 396). That the radio reports class `0xE0/0x01/0x01` is not yet checked; `VBoxManage list usbhost` shows it before the first VM test.

`libbluetooth` stays ABI-compatible:

- `RemoteDevice::GetConnectionState()` is in `libbluetooth.so`, the public Haiku API, not in the kernel. Existing signatures never change.
- `GetConnectionState()` stays as it is. We fix the missing `BT_REQ_CONN_STATE` reply in `bluetooth_server` and start `conn_state` at `RemoteDevice::DISCONNECTED`.
- More connection detail (transport BR/EDR or LE, encrypted, controller) comes through new additive methods on `RemoteDevice` (for example `status_t GetConnectionDetails(...)`) or through `libr5bt.so` messages to `bluetooth_server`.

Third parties and air/OS:

- Nothing is taken from air/OS without a license check. Every outside library or firmware first gets its own written report: project, SPDX license, copyright, patents, redistribution, and the lines for `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`.

Tracker, changelog and release files:

- Every step updates `CHANGELOG.md`, `tracker/STATUS.md`, `tracker/r5-status.tsv` (with `python3 scripts/tracker-report.py`) and the GitHub issues.
- `distro/WHATSNEW` and `distro/BUILD` change only when the owner asks, not on every step.
- ARM64 and RISC-V 64 keep building. Every test report says that ARM64 and RISC-V 64 were not tested.
- Never claim a test that did not run, above all not with the Sony WH-1000XM3 or XM4.

## 2. Layers, `libr5bt.so` and `libr5icons.so`

| Layer | Where | License | What it does |
|---|---|---|---|
| Kernel transport and HCI | `distro/kernel/src/add-ons/kernel/drivers/bluetooth/h2/h2generic/` and `.../bluetooth/hci/` | Haiku (MIT) + R5 `SPDX-Snippet` (MIT) | USB HCI, ACL, SCO and ISO transport, packet reassembly, command timeouts |
| Kernel L2CAP | `distro/kernel/src/add-ons/kernel/network/protocols/bluetooth/l2cap/` | Haiku (MIT) + R5 `SPDX-Snippet` (MIT) | BLE fixed channels (CID 4 ATT, CID 6 SMP), incoming PSM (`0x11` and `0x13` HID, `0x19` AVDTP), CoC and EATT |
| System server | `distro/bluetooth/server/` (`bluetooth_server`) | Haiku (MIT) + R5 `SPDX-Snippet` (MIT) | Radio control, pairing and keys (uid 0, mode 0700), SDP, GATT, Classic HID, LE Secure Connections |
| Media add-on | `distro/media/bluetooth_audio/` | R5 (MIT) | Classic A2DP source (SBC, royalty-free per the Bluetooth SIG, R5's own MIT encoder) and AVRCP; audio output choice per user |
| Input add-on | `distro/input/` (`bluetooth_hid`, `bluetooth_le_hid`) | R5 (MIT) | Classic HID (BR/EDR PSM `0x11` and `0x13`) and BLE HID over GATT. Mice and keyboards go to `input_server`; only gamepads go to `/dev/joystick/bluetooth/N` |
| R5 Bluetooth Kit | `distro/bluetooth/lib/` (`libr5bt.so`, `<R5Bluetooth.h>`) | R5 (MIT) | Top-level classes, like `<R5WiFi.h>` (not in `namespace BAppearanceKit`): `BluetoothState`, `BluetoothDeviceInfo`, `BluetoothController`, `BluetoothView` (uses `BAppearanceKit::BCard` inside) |
| Deskbar replicant | `distro/bluetooth/server/` (replicant) | R5 (MIT) | Talks to `bluetooth_server` only through `BMessenger` and `BMessage`. It never loads the server into Deskbar with `load_add_on` |

`distro/bluetooth/`, `distro/media/bluetooth_audio/` and `distro/input/` do not exist yet.

**Settings page.** Under the `Devices` category (`distro/settings/SettingsWindow.cpp`), in alphabetical order, so Bluetooth is first under Devices. The icon comes from `module_icon()` with `r5_settings_apply_prefs_overlay()`. Checked: there is already a legacy item `bluetooth` under `devices` in `distro/settings/LegacyPages.cpp` (line 205) that opens the old Bluetooth program. Step 3a replaces that item with the real page, in the same place.

- Exception to the `AGENTS.md` rule that a page requiring an administrator is gray until the lock is open: on the Bluetooth page only the radio switch is system-wide. That switch stays gray while the `libr5ui` lock is closed. The rest of the page (scan, pair, connect, audio output) works for every signed-in person. Step 3a writes this down at the Bluetooth page in `docs/SETTINGS.md`.

**Icons and glyphs (`libr5icons.so`).** All fixed glyphs (`enum r5_glyph` and `r5_glyph_draw()`) and level glyphs (new: `r5_level_glyph_draw()` and `enum r5_level_glyph` with `R5_LEVEL_SIGNAL` and `R5_LEVEL_BATTERY`, styles normal, disabled and off) are drawn by `libr5icons.so`. `DrawWiFiSignalBars` in `distro/wifi/lib/R5WiFiGlyphs.h` stays inline and calls `r5_level_glyph_draw()` inside. Checked: `docs/LIBR5.md`, `docs/ICONS.md` (which has the "Line glyphs" section) and `distro/icons` say nothing yet about `r5_level_glyph`; step 2 adds it.

## 3. Steps

Each step is its own small pull request. It is built for AMD64 with `scripts/build-locked.sh` (apply and jam under one build lock, `/home/nickr/r5os/.r5-build.lock`; never `apply-overlay.sh` and `jam-r5-raw.sh` one after the other), and kept building for ARM64 and RISC-V (not tested). QEMU runs only on an own copy of `r5-os.image`, never through `scripts/run-qemu-r5.sh`, which stops every QEMU.

**How the fuzzers run (steps 1a, 1b, 1c).** Like `scripts/fuzz-midi2.sh`, the Bluetooth fuzzers run in WSL on the host (`g++ -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1`). The kernel parsers (the `hci` event and packet parser, and the `l2cap` packet and signalling parser) are compiled on their own with small host stub headers (`-I scripts/bt-host`, like `scripts/midi2-host`), without the whole Haiku kernel. `scripts/fuzz-bt.sh` and `scripts/bt-host` are new in step 1a. Each target runs 60 seconds.

### Step 0 (PR 1): `bluetooth_server` into `distro/`, and six review fixes

- **What:** copy `haiku/src/servers/bluetooth/` to `distro/bluetooth/server/` (`SPDX-Snippet` MIT) and fix the six bugs found:
  1. NULL checks on `CreateTransportAccessor`, `FindData("bdaddr")` and `RemoteDeviceByAddr()`.
  2. A lock around `fLocalDevicesList` between `DispatchEvent()` and `MessageReceived()`.
  3. Fix `BT_STOP_WATCHING_CONNECTIONS`: match on the messenger instead of a fresh pointer address, and `delete` in `NotifyWatchers()`.
  4. Always send a reply when `lDeviceImpl == NULL` on `BT_MSG_GET_REMOTE_DEVICES` and `BT_REQ_CONN_STATE`.
  5. Fix the leaks in `DeviceManager::AddDevice()`, call `fDeviceManager->LoadState()`, and handle `B_ENTRY_REMOVED` → `BT_MSG_REMOVE_DEVICE`.
  6. `close(client)` in `SDPServer::_Run()`, and `>= 0` socket checks.
- **Test:** a new small program `distro/bluetooth/tests/bt_server_smoke_test`. It sends `BT_REQ_CONN_STATE` and messages with an invalid `bdaddr` to `bluetooth_server`.
- **Note (checked in the source):** `bluetooth_server` has no launch job in `data/launch`. It is in the regular image (`build/jam/images/definitions/regular`, line 160 in `haiku/`, line 189 after apply) and is started on request. The smoke test therefore starts it itself (through `BRoster`), and only then the log line appears.
- **Pass:** QEMU boots, `bt_server_smoke_test` starts `bluetooth_server`, syslog has `bluetooth_server: started, 0 radios`, and the test prints `bt_server_smoke_test: ok (0 crashes)` (no crash on an unknown `bdaddr` or on `BT_REQ_CONN_STATE`).

### Step 1a (PR 2): `h2generic` HCI timeouts, safe reassembly, HCI fuzzer

- **What:** in `distro/kernel/src/add-ons/kernel/drivers/bluetooth/h2/h2generic/`: a hard HCI command timeout. A command without a Command Complete or Command Status within 2 s (Linux `HCI_CMD_TIMEOUT`) gets a Command Status with error `0x1F` (Unspecified Error) for that opcode, the timeout is logged with the opcode, and a real answer that still comes later is dropped (`h2command.h`). In `distro/kernel/src/add-ons/kernel/bluetooth/hci/`: the packet reassembly moves to `hci_assemble.h`, without the `panic()` on a split event and without the endless loop on SCO.
- **Not in this step (owner, 15 Nov 2026):** the Event Mask and the LE Event Mask. An HCI Reset from the kit (`LocalDevice::Reset()`) clears what a driver sets, so `bluetooth_server` sets both after every reset, in step 6.
- **Required checks:** manual SMAP review, a QEMU boot with SMAP, and `scripts/fuzz-bt.sh hci 60` in WSL (60 seconds of ASan and UBSan on the separately compiled HCI event and packet parser).
- **Pass:** QEMU SMAP boot without kernel panic, log line `hci_fuzz: 60s 0 crashes`, and in VirtualBox with the passed-through radio: `h2generic: controller ready`.

### Step 1b (PR 3): `l2cap` into `distro/`, BLE fixed channels, incoming PSM, `libbluetooth`

- **What:** copy `haiku/src/add-ons/kernel/network/protocols/bluetooth/l2cap/` to `distro/kernel/.../l2cap/`. Add BLE fixed channels (CID 4 ATT, CID 6 SMP) and incoming L2CAP accept (HID PSM `0x11` and `0x13`, AVDTP PSM `0x19`). Add one additive method to `RemoteDevice` in `libbluetooth`, without breaking `GetConnectionState()`.
- **Required checks:** manual SMAP review, a QEMU boot with SMAP, and `scripts/fuzz-bt.sh l2cap 60` in WSL (60 seconds of ASan and UBSan on the separately compiled L2CAP packet and signalling parser).
- **Pass:** QEMU SMAP boot without kernel panic, `l2cap_fuzz: 60s 0 crashes`, and `bt_l2cap_abi_test: ok`.

### Step 1c (PR 4): `l2cap` CoC and EATT, ISO endpoints prepared

- **What:** LE credit-based flow control (CoC) in `distro/kernel/.../l2cap/`, and recognition of the ISO USB endpoints in `h2generic`.
- **Pass:** QEMU SMAP boot, `l2cap_fuzz: 60s 0 crashes`, and `l2cap_coc_test: ok`.

### Step 2 (PR 5): "Icons - one place for signal and battery symbols" (`libr5icons` and Wi-Fi)

- **What:** new line glyphs at the end of `enum r5_glyph`; a new call `r5_level_glyph_draw()` with `enum r5_level_glyph` (`R5_LEVEL_SIGNAL`, `R5_LEVEL_BATTERY`) in `libr5icons.so`; the description in `docs/ICONS.md` ("Line glyphs") and `docs/LIBR5.md`; and `DrawWiFiSignalBars` in `R5WiFiGlyphs.h` moved onto the new call.
- **Pass:** a QEMU boot with a screenshot before and after: the Wi-Fi list and Deskbar look pixel-identical to before the change.
- **As built (16 Nov 2026, while the owner slept; open to change):** the new line glyph is `R5_GLYPH_BLUETOOTH` (7). The pass check is `r5_level_glyph_test` instead of screenshots: QEMU has no Wi-Fi adapter, so its list and Deskbar show no bars, and the desktop sits behind Sign in. The test draws the old `DrawWiFiSignalBars()` and the new call into bitmaps for 6,552 signal cases (sizes, levels −1 to 5, three styles, three color pairs) and compares them byte for byte; a screenshot of a real adapter on the 5625U still has to confirm it.

### Step 3a (PR 6): `libr5bt.so` and Settings (Devices → Bluetooth)

- **What:** `distro/bluetooth/lib/` (`libr5bt.so`, `<R5Bluetooth.h>`), the Bluetooth page in alphabetical order under Devices in Settings (with `r5_settings_apply_prefs_overlay()`), and `docs/LIBR5.md`, `docs/SETTINGS.md` and an R5Book chapter.
- **Pass:** in QEMU and VirtualBox the page is first under Devices. It shows `0 radios` in QEMU and a real scan list in VirtualBox, without a `bluetooth_server` crash.

### Step 3b (PR 7): a separate Bluetooth program and a message-based Deskbar replicant

- **What:** a separate Bluetooth program built on `BluetoothView` from `libr5bt.so`, and a Deskbar replicant that talks through `BMessenger` and `BMessage` (no `load_add_on`) and uses `libr5icons.so`.
- **Pass:** log line `deskbar_bt_replicant: attached via messenger` (no `load_add_on` of the server), and it works for both the administrator and a standard person.

### Step 4 (PR 8): Classic HID (BR/EDR mouse and keyboard on PSM `0x11` and `0x13`) and SSP passkey

- **What:**
  - Finish Secure Simple Pairing in `LocalDeviceImpl.cpp`. Checked: the `case` lines for IO capability, user confirmation (Numeric Comparison) and simple pairing complete are at lines 369–384. `HCI_EVENT_USER_PASSKEY_REQUEST` is expected at line 1795 but handled nowhere. This step adds `HCI_EVENT_USER_PASSKEY_REQUEST` and `HCI_EVENT_USER_PASSKEY_NOTIFICATION` (Passkey Entry, for keyboards).
  - Classic Bluetooth HID over BR/EDR L2CAP PSM `0x11` (control) and `0x13` (interrupt): mice and keyboards go to `input_server`, gamepads to `/dev/joystick/bluetooth/N`.
- **Pass:** in VirtualBox with the passed-through radio: `bluetooth_server: ssp passkey ok` (keyboard pairing), `bluetooth_hid: br-edr hid ready`, and mouse and keyboard events arrive in `input_server`.

### Step 5 (PR 9): Classic A2DP (R5's own MIT SBC) and AVRCP in `bluetooth_audio`

- **What:** a royalty-free SBC encoder (R5's own MIT code), AVDTP 1.3, and AVRCP (play, pause, volume) in `distro/media/bluetooth_audio/`, with an audio output choice per user. A real-time audio thread, and background scans paused while streaming.
- **Test path:** first VirtualBox USB passthrough, then a bundled 5625U bare-metal stick (written by the owner with `write-stick.py`) with the Sony WH-1000XM3 or XM4.
- **Pass:** log line `bluetooth_audio: streaming sbc 44100Hz`, steady audio without stutter on the Sony XM3 or XM4, and `bluetooth_audio: stream closed cleanly` on disconnect.

### Step 6 (PR 10): BLE Secure Connections, extended advertising, BLE HID over GATT

- **What:** the Event Mask (with the LE Meta event) and the LE Event Mask, set by `bluetooth_server` after every HCI Reset, with only the bits the controller supports (safe for Bluetooth 5.x and 6.0); LE Secure Connections (ECDH P-256 through `openssl3` `libcrypto`), extended advertising, 2M PHY and subrating in `bluetooth_server`, and BLE HID over GATT (`bluetooth_le_hid` to `input_server` or `/dev/joystick/bluetooth/N`, 7.5 ms interval, cached report layout).
- **Test path:** VirtualBox USB passthrough and a second bundled 5625U stick.
- **Pass:** `bluetooth_server: smp sc pairing ok`, and a BLE mouse and keyboard move smoothly in `input_server`.

### Step 7 and later (separate pull requests, each only after a third-party license and patent report)

- **LE Audio (LC3 and ISO):** a report on `liblc3` (Apache-2.0).
- **A2DP AAC:** a report on the FDK-AAC license and AAC patents.
- **HFP (eSCO and mSBC):** microphone support for classic headsets.
- **More vendor firmware:** a report on the redistribution rights for Realtek (`0x0bda`) and Qualcomm (`0x0cf3`).

## 4. The owner's choices

1. **Turning the radio on and off.** Scanning, pairing, connecting and choosing the audio output need no lock. Turning the radio on or off is behind the public `libr5ui.so` administrator lock (`distro/ui/AdminLock.h`).
2. **Paths on disk.** Pairing keys in `/boot/system/settings/bluetooth/` (uid 0, mode 0700). The audio choice per person in `~/config/settings/Media/bluetooth_audio`, found with `find_directory(B_USER_SETTINGS_DIRECTORY)`. There is no `settings/R5/` directory anywhere in R5, and `bluetooth_audio` is a Media add-on, so it sits next to the other Media settings.
3. **Crypto library for ECDH P-256.** `openssl3` (`libcrypto`, Apache-2.0), already in `distro/UserBuildConfig` (line 25).
4. **Test hardware for VirtualBox.** The MT7922 (`0x13d3:0x3567`) of the 5625U is passed to the `R5OS` VM with a USB filter. What that means:
   - While the VM runs, Windows has no Bluetooth; a headset or mouse through Windows does not work then. After the VM stops, Windows gets it back. If not, turn the adapter off and on once in Device Manager.
   - In the VM, R5 uses the same `h2mediatek` firmware path as on the stick, so the VM tests the real path.
   - `scripts/run-vbox-r5.ps1` gets an option that sets the USB filter for `0x13d3:0x3567`, off by default, so it does not happen by accident. First check the real vendor and product ID with `VBoxManage list usbhost`.
   - No separate dongle is needed. If passthrough fails (Windows does not let go of the device), a CSR dongle from a known brand is the fallback.
