# Bluetooth plan 2: LE Audio, Auracast and Bluetooth 6.0

This is the second Bluetooth plan for R5 Operating System. The first plan, `docs/BLUETOOTH.md`, brought pairing, mice, keyboards, game controllers and headsets (A2DP, HFP, the microphone over eSCO). This plan adds:

- **LE Audio:** music and calls with new earbuds and headphones over the LC3 codec.
- **Auracast:** listening to a broadcast, and later sending one.
- **The smaller Bluetooth 6.0 host features.**

Channel Sounding (distance measurement) is not part of this plan. The owner chose that on 17 Nov 2026, and no USB radio with Channel Sounding was found.

Only the radio part of step B0 is built (section 5, "B0 as built"). The facts about the tree were checked on 17 Nov 2026 against `main`. Facts from the web are marked with their source and are not verified unless the text says so. The choices in section 6 are the owner's.

It follows the BeOS and Haiku way. An application sees the Media Kit (a sound output and input node) and the R5 Bluetooth calls in `docs/LIBR5.md`, never an HCI command. The stack stays R5's own: `bluetooth_server`, the `h2generic` driver and the kernel `bluetooth` modules. BlueZ is not used.

## 0. What is in the tree

- **LE links:** there are pairing (LE Secure Connections), GATT over the fixed ATT channel, BLE HID, the LE Event Mask and the read of the LE features (`kOpLEReadFeatures = 0x2003`, `distro/bluetooth/server/LEManagerPrivate.h:27`). See `docs/BLUETOOTH.md` step 6.
- **Advertising:** R5 does not send it. `LEManager.cpp:695`: "We never advertise". The opcodes 0x200A and 0x2039 appear only in the radio-off gate (`BluetoothServer.cpp:828`). There is no periodic advertising and no periodic sync.
- **ISO (isochronous channels), which LE Audio and Auracast run on:** nothing. There is no HCI ISO data path in `h2generic` and no CIS or BIG command. `docs/BLUETOOTH.md` names the ISO endpoints as left for this plan (line 125).
- **LC3:** not in the tree, and not a package in `distro/packages`.
- **Audio:** the A2DP sink node (SBC) and the voice node (`/dev/bluetooth/voice/<n>`, off until `bt_voice true`) exist. They show how a Bluetooth audio node is built.

## 1. What a user gets

1. **LE Audio earbuds:** pair them in Settings → Bluetooth like any device. Music plays through LC3, both earbuds act as one device, and volume, play and pause work from the earbuds.
2. **Calls and recording over LE Audio:** the earbuds' microphone as a sound input. It stays off until it has been tested, like the voice path in plan 1.
3. **Auracast, listening:** a list of nearby broadcasts (a TV, a gym, an airport gate), and R5 plays the one you pick. Some broadcasts ask for a code.
4. **Auracast, sending:** R5 sends its sound to every Auracast headphone nearby, for example to watch a film together.
5. **Bluetooth 6.0 details:** less radio traffic while scanning and a better link with 6.0 devices. Users do not see this directly.

## 2. Licenses

| Source | SPDX | Copyright | Use |
|---|---|---|---|
| **liblc3** (`github.com/google/liblc3`) | `Apache-2.0` | Google LLC | LC3 encoder and decoder, as its own shared library from R5's own recipe in this repository. Not pasted into MIT code. Its NOTICE and license go in `THIRD_PARTY_NOTICES.md`, `docs/LICENSING.md` and `LICENSES/`. Google states it is qualified under QDID 194161 (Fluoride 1.5); that is not yet checked against the SIG listing |
| Zephyr Bluetooth host (LE Audio: BAP, CAP, PACS, ASCS, VCP, MCP, CSIP) | `Apache-2.0` | the Zephyr Project contributors | Reference only, to read how the profiles behave. Code taken from it would keep its header in its own file; the default is to write R5's own MIT code from the specifications |
| Android Fluoride / Gabeldorsche | `Apache-2.0` | the Android Open Source Project | Reference only, same rule |
| BlueZ, Linux kernel Bluetooth | `GPL-2.0-only` / `GPL-2.0-or-later` | per file | Not read for code (as in plan 1) |
| Bluetooth specifications (Core 6.0, BAP, PACS, ASCS, CAP, CSIP, VCP, MCP, TMAP, PBP, BASS, LC3) | — | Bluetooth SIG | Source of the protocol facts. R5 code is written from them, MIT |

The open patent question: Apache-2.0 grants Google's own patents on liblc3, not those of others. LC3 is described as royalty-free, but only by secondary sources (for example the Wikipedia article). The Bluetooth SIG's own terms for LC3 patents, and who holds them, are not found yet. **Step B0 finds them before any LC3 code ships.** Until then LC3 stays out of the image.

## 3. The name "Bluetooth"

The Bluetooth SIG says that marketing a product that implements Bluetooth, or using its trademarks (including the word "Bluetooth"), needs SIG membership and a qualified product. Membership alone is not enough (support.bluetooth.com, FAQ). Adopter membership costs $0 a year, and qualifying a product is listed at $12,000 (bluetooth.com/fee-schedule, effective 1 March 2026; check the current page). An operating system with its own stack is qualified as a "Core host" (Würth Elektronik, "Bluetooth Listing Guide").

What other open systems do (checked 17 Nov 2026, not legal advice):

- Linux distributions, Haiku and the BSDs name Bluetooth in their documentation and settings, but show no logo and claim no qualification. The qualification is done by the company that sells a device built on the stack. Ezurio, for example, has a SIG listing for its BlueZ build (ezurio.com).
- BlueZ added qualification documentation for more than 15 profiles in May 2026, for companies that qualify a product built on it (collabora.com, 26 May 2026).
- In 2005 the SIG's qualification administrator asked BlueZ to remove a hardware compatibility list (osnews.com, secondhand report). The SIG does act on how the name is used.

R5 follows that practice now: "Bluetooth" only as a plain description of what works ("Bluetooth headphones", "the Bluetooth page"). There is no logo, no version claim such as "Bluetooth 6.0 support", and no "certified" or "qualified".

Open questions for step B0. This is not legal advice; the owner decides, with a lawyer where needed:

- How do Haiku, Linux distributions and other open operating systems describe Bluetooth support without their own qualification?
- Does a sentence like "works with Bluetooth headphones" fall under the trademark rules, or only "Bluetooth 6.0 support" and the logo?
- What would qualifying R5 as a Core host require (tests, a test facility, which profiles)?
- Until this is clear, the README and the website do not claim a Bluetooth version and do not use the logo.

## 4. Layers

| Layer | Where | License | Does |
|---|---|---|---|
| USB driver | `distro/kernel/.../bluetooth/h2/h2generic/` | Haiku MIT + R5 snippets | HCI ISO data up and down |
| HCI / kernel | `distro/kernel/src/add-ons/kernel/bluetooth/` | Haiku MIT + R5 snippets | ISO packets to `bluetooth_server` |
| `bluetooth_server` | `distro/bluetooth/server` | MIT | Advertising, periodic sync, CIS and BIG setup, the LE Audio GATT services and clients |
| LC3 | `liblc3` (own recipe) | `Apache-2.0` | Encoding and decoding |
| Audio nodes | `distro/bluetooth/audio` (next to A2DP) | MIT | Media Kit output "Bluetooth LE Audio" and input "… microphone" |
| Pages | Settings → Bluetooth, Deskbar item | MIT | "Broadcasts nearby", a code field, "Share audio". Only through the kits; the Settings frame of `AGENTS.md` stays as it is |

## 5. Steps

Each step is its own small pull request, built for AMD64 with `scripts/build-locked.sh`. ARM64 and RISC-V 64 keep building and are not tested.

- **QEMU** has no radio. It only proves a boot without a panic, with SMAP on.
- **Radios:** the 6.0 USB dongle (section 6, choice 3) goes to VirtualBox through the `xHCI USB` of the `R5OS` VM, so most steps need no stick. The 5625U's built-in radio is tested on the next stick that is already planned.
- **SMAP and fuzzing:** every user copy goes through `user_memcpy()` and is reviewed by hand. Every new parser gets a 60 s fuzz target in `scripts/fuzz-bt.sh`.
- **Off by default:** every audio path stays off until its own test on a radio has passed, like `bt_voice`.

### B0 (paper): reports and the radio

- The licence report for liblc3 (section 2), with the SIG terms for LC3 patents.
- The trademark chapter (section 3).
- The dongle: which chip it has (USB ID), whether `h2generic` takes it, which firmware it needs and that firmware's licence.
- The LE feature bits of both radios, the dongle and the 5625U, read with `0x2003`: CIS Central, CIS Peripheral, Isochronous Broadcaster, Synchronized Receiver, Periodic Advertising, and the 6.0 bits. If the 5625U radio lacks the ISO bits, LE Audio runs only on the dongle.
- **Pass:** the reports are in this document. The feature bits are recorded from a real radio, with how they were read.

#### B0 as built: the dongle starts in R5

- **The chip:** the ASUS USB-BT600 is USB `0b05:1d70` (Windows: `USB\VID_0B05&PID_1D70&REV_0200`), a Realtek RTL8761CU. It reports the Bluetooth class, so `h2generic` already takes it; without a patch from the host it is not usable, and Windows on the owner's PC has no driver for it either.
- **The firmware:** `rtl_bt/rtl8761cu_fw.bin` (10296 bytes) and `rtl_bt/rtl8761cu_config.bin` (11 bytes) from linux-firmware (commit `35e542439e`), unmodified, in `distro/firmware/realtek` and in the regular image at `system/non-packaged/data/firmware/h2generic`, next to the MediaTek file. Licence: `LICENCE.rtlwifi_firmware.txt` (Realtek, 2010): binary redistribution without modification, with the notice and disclaimer; no reverse engineering. So R5 reads only the headers of the file. Credits: `THIRD_PARTY_NOTICES.md`, `docs/LICENSING.md`, `LICENSES/`.
- **The file:** a v1 ("Realtech") file, version `0x4618d3f3`, with four patch entries for chip ids 0 to 3. Each entry points at the same 252 bytes at offset `0x30`; the rest of the file up to the trailer is zeros. The trailer (`51 04 fd 77`) names project id `0x33` (51), which FreeBSD's table does not have. The patch's last four bytes are the file version, as `rtlbtfw` expects. A chip id is the ROM version plus one, so ROM versions 0, 1 and 2 have a patch.
- **The loader:** `h2realtek.cpp` (R5, MIT) runs in `device_open` right after `mediatek_setup()`, only for USB vendor `0x0bda` or a USB ID in its table, and only when Read Local Version names Realtek (`0x005d`). It follows FreeBSD `rtlbtfw` (BSD-2-Clause): the firmware name from the USB ID table (one entry, `0b05:1d70` → `rtl8761cu`, project `0x33`) or else from the chip's report (FreeBSD's chip table), the project id check, the ROM version (vendor command `0xfc6d`), the patch with the config after it, sent with `0xfc20` in 252-byte pieces (263 bytes: two pieces). No Linux source was read. The parser is `h2rtkfw.h`, without kernel dependencies.
- **When it fails:** a missing, wrong or refused firmware gives one syslog line (`h2generic: realtek: ... It stays off.`) and the radio's `open` fails; nothing panics and nothing waits longer than two seconds per command. The MediaTek radio is not touched: MediaTek vendors never get a Realtek command, and the Realtek loader returns at once for any other device. A Realtek chip without an entry is used as it is, as before.
- **What is logged after the patch:** HCI and LMP version and subversion, manufacturer, the LMP feature mask, and the LE feature mask with 2M, Coded, extended and periodic advertising, CIS central and peripheral, ISO broadcaster and synchronized receiver. `bluetooth_server` logs the same LE line for every radio (`hci<n> le features ...`), so the 5625U's MediaTek radio is recorded on its next stick boot.
- **Two radios:** `bluetooth_server` keeps a list of radios and the Bluetooth page of Settings already has the radio menu; nothing there changed.
- **VirtualBox:** `scripts/run-vbox-r5.ps1 -PassDongle` passes `0b05:1d70` through the VM's xHCI; without the switch the filter is removed.
- **Tests:** `scripts/test-rtkfw.sh` (host, ASan and UBSan, also against the shipped files) and `scripts/fuzz-bt.sh rtkfw` (60 s).
- **Read from the real dongle (17 Nov 2026, VirtualBox 7.2.22, `-PassDongle`, plugged in while R5 ran; from the syslog):**
  - From ROM: HCI version `0x0e`, revision `0x0e`, LMP version `0x0e`, subversion `0x8761`. FreeBSD's table has no such entry, so the USB ID entry is needed. ROM version 1.
  - Download: `rtl8761cu`, file version `0x4618d3f3`, 263 bytes in 11 ms.
  - After the patch: HCI version `0x0e` (Core 6.0), revision `0x7bf1`, LMP version `0x0e`, subversion `0x5762`, manufacturer `0x5d` (Realtek).
  - LMP features `0x877bfddbfeffffff` (LE supported). Page 1 `0x3`, page 2 `0x920` (Secure Connections), read by `bluetooth_server`.
  - LE features `0x0000008ef7fe77fd`: 2M PHY yes, Coded PHY **no** (bit 11), extended advertising yes, periodic advertising yes, CIS Central yes, CIS Peripheral yes, Isochronous Broadcaster yes, Synchronized Receiver yes. Read by the driver and by `bluetooth_server` (`0x2003`), the same value.
  - `bluetooth_server` set the radio up and a scan heard 13 LE devices and one Classic device. The Bluetooth page showed the radio.
  - So the dongle has what B1 to B7 need. The 5625U's MediaTek radio is read on its next stick boot (the same `le features` line); not done yet.
- **Limits:** v2 ("RTBTCore") files are refused for now (no shipped chip needs one). A chip that kept its patch through a warm restart while staying powered is not detected; if it then refuses the download, it stays off until it is unplugged.

### B1: sending advertisements (legacy, extended, periodic)

- In `bluetooth_server`: the opcodes 0x2006, 0x2008 and 0x200A (legacy), 0x2036 to 0x2039 (extended), and 0x203E to 0x2040 (periodic), behind one internal call. Only the controller's supported sets are used. Advertising stops when Bluetooth is turned off.
- Auracast sending needs this. The AirDrop study (`docs/AIRDROP.md`) needs it too.
- **Pass:** `scripts/fuzz-bt.sh` targets still 0 crashes. On the dongle in VirtualBox, a phone with a BLE scanner app sees R5's advertisement.

### B2: ISO channels in the kernel

- `h2generic` and the HCI module: HCI ISO data packets (packet type 0x05), up and down, with length checks against the buffer size the controller reports. Which USB endpoint carries ISO data is checked first in Core 6.0, Vol 4, Part B, and written here with the section number.
- Events: CIS Established, CIS Request, Create BIG Complete, BIG Sync Established, and the BIG and periodic events, added to the LE Event Mask only where the controller supports them.
- A new fuzz target `iso` for the ISO packet reassembly.
- **Pass:** `iso` 60 s, 0 crashes. A QEMU SMAP boot without a panic. On the dongle, a CIS to an LE Audio device is set up and closed without a hang (no sound yet).

### B3: LC3 and the audio node

- The liblc3 recipe (only after B0 clears it) and its line in `THIRD_PARTY_NOTICES.md`.
- The Media Kit output node "Bluetooth LE Audio". It encodes LC3 at the frame length and bit rate that the device chose, and hands the frames to the ISO path.
- **Pass:** a host test encodes and decodes a test tone with liblc3 at 48 kHz, 10 ms frames, and the result matches liblc3's own test vectors.

### B4: LE Audio music (unicast)

- R5's own MIT clients for:
  - PACS (what the device can play);
  - ASCS (stream setup);
  - BAP and CAP (the procedures);
  - CSIP (two earbuds as one set);
  - VCP (volume);
  - MCP (R5 as the media control server, for play and pause from the earbuds).
- These come with pairing in the Bluetooth page, and the device shown as one entry in My devices.
- Off until `bt_le_audio true` in the Bluetooth settings file. The image ships no such file.
- **Pass:** on the dongle, a pair of LE Audio earbuds plays music on both sides. Volume, play and pause work from the earbuds. One hour without a drop or a crash. The earbud model is written in `CHANGELOG.md`.

### B5: LE Audio microphone

- A bidirectional CIS, LC3 decoding, and the Media Kit input "Bluetooth LE Audio microphone". It stays off until tested.
- **Pass:** a recording from the earbuds' microphone plays back clearly. Music and the microphone work in turn without a re-pair.

### B6: Auracast, listening

- Scanning for broadcast sources (extended plus periodic advertising), the BASE (what the broadcast carries), BIG sync, LC3 decoding, and a broadcast code entry (it never appears in a log).
- In the page: "Broadcasts nearby".
- **Pass:** R5 plays an Auracast broadcast from a phone or TV that can send one. The source is written in `CHANGELOG.md`.

### B7: Auracast, sending

- A BIG with LC3, advertised through B1, with an optional broadcast code. "Share audio" in the page and in Deskbar.
- **Pass:** two Auracast headphones hear R5 at the same time.

### B8: the smaller Bluetooth 6.0 parts

**Waits for a radio that has them.** The ASUS USB-BT600 has none of these features (section 6, choice 3). B8 starts only once a radio reports them in its feature bits.

- On the dongle, only what the controller reports in its feature bits:
  - Decision-Based Advertising Filtering;
  - Monitoring Advertisers;
  - the ISOAL enhancement;
  - the LL extended feature set;
  - Frame Space Update.
- Each one is used where it saves radio time or improves a link, and is logged once at start.
- **Pass:** on the dongle, the feature is reported on and used. No change on a radio without it.

## 6. The owner's choices

1. **Scope (owner, 17 Nov 2026):** LE Audio, Auracast and the smaller 6.0 parts. Not Channel Sounding.
2. **LC3 (owner, 17 Nov 2026):** liblc3 as a separate library (`Apache-2.0`, own recipe), not an own codec.
3. **Hardware (owner, 17 Nov 2026):** the owner ordered one ASUS USB-BT600. According to Argenox (argenox.com, "The curious case of the Bluetooth 6.0 dongle", 8 Sep 2026), it:
   - reports HCI version 0x0E (Core 6.0), with an "almost certainly Realtek" chip;
   - supports CIS central and peripheral, ISO broadcaster and synchronized receiver, extended and periodic advertising, and the 2M and Coded PHY;
   - does not support Channel Sounding, DBAF, Monitoring Advertisers, the LL extended feature set, the ISOAL enhancement or Frame Space Update.

   So it covers B1 to B7, but not B8. Step B0 still reads its USB ID, firmware and feature bits on R5.
4. **The name (owner, 17 Nov 2026):** work it out in this plan (section 3). Until then, no Bluetooth version claim in the README or on the website.
5. **Order (open):** B0 → B1 → B2 → B3 → B4 → B5 → B6 → B7 → B8. Auracast listening (B6) could come before B4. *Recommendation: keep this order.* Unicast earbuds are what most people own. B6 reuses B2 and B3 either way.

## 7. Not verified

- The liblc3 qualification (QDID 194161) and the royalty-free claim for LC3 come from Google's README and from secondary sources.
- The SIG fees and trademark rules come from bluetooth.com pages as found on 17 Nov 2026.
- The ASUS USB-BT600's features were read on R5 in step B0 (above). They match the Argenox article except the Coded PHY, which the dongle does not report. Whether the 5625U radio supports ISO is unknown until its next stick boot.
