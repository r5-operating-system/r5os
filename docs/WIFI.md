# Wi-Fi 6 and Wi-Fi 7 on R5

This is the plan for faster and safer Wi-Fi on R5 Operating System: Wi-Fi 4 and 5 (802.11n and 802.11ac) first, then WPA3, then Wi-Fi 6 (802.11ax), and Wi-Fi 6E and Wi-Fi 7 (802.11be) once there is hardware for them. It says what is already in the tree, where each part goes, and the steps that get there. Each step is its own small pull request with a pass rule. The choices in section 5 are the owner's.

Nothing in this document is built yet unless it says so. Line numbers were checked on 16 Nov 2026 against `main` at `43999e006c`.

It follows the BeOS and Haiku way. An application sees kits and messages, never an ioctl or a driver structure: the Network Kit (`libbnetapi.so`, `BNetworkDevice`, `BNetworkRoster`, `wireless_network`) for joining, and the R5 Wi-Fi Kit (`libr5wifi.so`, `<R5WiFi.h>`) for the panel, the state and the link. FreeBSD net80211 and the driver stay under the hood.

## 0. Test hardware and test paths

- **QEMU has no Wi-Fi.** It only proves that the tree boots, that nothing panics with SMAP on, and that the kits and pages work with "no adapter".
- **VirtualBox on Windows has no PCI passthrough,** so the built-in MT7921 of the 5625U and the AX201 of the X1 cannot be given to a VM.
- **The 5625U from a USB stick** is the real test for the MT7921. Sticks are costly, so the hardware steps are bundled: one stick per group of steps (section 4).
- **A USB MediaTek dongle passed to VirtualBox** (like Bluetooth, through the `xHCI USB` the `R5OS` VM already has) would be cheaper per test. A dongle with the MT7921AU chip (USB `0x0e8d:0x7961`, sold under several brands; check the chip before buying, many "Wi-Fi 6" dongles are Realtek) uses the same firmware files as the built-in card. Extra driver work it needs:
  - a USB transport for `mt7922wifi`: bulk endpoints instead of the PCIe rings in `mtk_dma.c`, the firmware load and the MCU commands over USB, and USB power handling. The MCU command contents (`mtk_mcu.c`, `mtk_sta.c`) and the net80211 side are shared. The FreeBSD compat layer already carries USB Wi-Fi drivers (`haiku/src/libs/compat/freebsd_network/usb.cpp`, used by `ralinkwifi/dev/usb/wlan/if_run.c`), so the frame is there.
  - a check that VirtualBox passes the dongle with enough speed: a speed measured through VirtualBox USB is not the speed of the card. It is good for "it joins, it does not crash, WPA3 works", not for the speed numbers of step 2.
  - Estimate: one to two sessions of driver work before the first dongle test. Section 5, choice 1.
- **The X1 Carbon Gen 8** has the Intel AX201 and runs `iaxwifi200` (section 1). It is the second machine, tested from a stick or later over SSH.
- Never claim a speed, LAN or Wi-Fi test that did not run. A speed is always "measured on the owner's network, distance and channel noted", never a number from a data sheet.

## 1. What is in the tree, and where the files go

### The MT7921 on the 5625U (`mt7922wifi`)

- Source: `distro/network/wlan/mt7922wifi/` (`glue.c`, `dev/mtk/if_mtk.c`, `mtk_dma.c`, `mtk_mcu.c`, `mtk_sta.c`, `mtk_tx.c`, `if_mtkvar.h`). From air/OS. The files say "Distributed under the terms of the MIT License" (`if_mtk.c` line 9, `mtk_dma.c` line 11, `mtk_mcu.c` line 12, `mtk_sta.c` line 13); `glue.c` carries "Copyright 2026, Haiku, Inc."; `if_mtk.c` has two R5 `SPDX-Snippet` blocks (lines 109–128 and 1043–1059). `mtk_tx.c` has no license line in its header; that is fixed in step 1 with the author's line from air/OS, not invented.
- The comments name Linux mt76 as the source of the hardware knowledge (for example `if_mtk.c` line 57, `mtk_sta.c` line 9: "Every layout here is Linux's, from mt76_connac_mcu.c and mt7921/mcu.c"). Layouts and register numbers are facts, not copied code; section 2 has the license rule.
- PCI IDs: `0x14c3:0x7961` "MediaTek MT7921 802.11ax" (`if_mtk.c` line 122), plus `0x7922` and `0x0616` for the MT7922 (lines 120, 124).
- `scripts/apply-wireless.py` copies the driver into the Haiku tree (lines 89–94) and adds the firmware (lines 46–47): `distro/firmware/mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin` and `WIFI_RAM_CODE_MT7961_1.bin`, with `LICENCE.mediatek`.
- What it offers today: `mtk_getradiocaps()` sets only `IEEE80211_MODE_11B` and `11G` on 2.4 GHz and `IEEE80211_MODE_11A` on 5 GHz (`if_mtk.c` lines 364, 365, 370). `ic_caps` is `STA | SHPREAMBLE | SHSLOT | WPA | WME` (lines 1141–1145): no `ic_htcaps`, no `ic_vhtcaps`, no `IEEE80211_C_TX_AMPDU`, no `IEEE80211_C_MFP`. So the card, which is a Wi-Fi 6 card, now talks like a 2003 card (54 Mbit/s at most).
- Receive path: `mtk_rxwork()` (`if_mtk.c` line 819) → `mtk_receive()` (`mtk_tx.c` line 554) → `mtk_receive_frame()` (`mtk_tx.c` line 261), which reads the RX descriptor words and hands the frame to net80211.
- Firmware commands already wait at most 2 s for a reply (`mtk_mcu.c` line 230, `mtk_wait_reply(..., 2000)`).
- Tested: stick 51, a WPA2 join with the MT7921, "Wi-Fi works", no panic (`CHANGELOG.md` line 702). No syslog, no DHCP record and no speed are on record for that boot.

### The AX201 on the X1 (`iaxwifi200`)

- Source: `haiku/src/add-ons/kernel/drivers/network/wlan/iaxwifi200/dev/pci/if_iwx.c`, OpenBSD `if_iwx.c` v1.198 (line 1), ISC (copyright genua gmbh, Fixup Software Ltd., Stefan Sperling; lines 3–5). Built on Haiku's `openbsd_wlan` compat, and in the regular image (`haiku/build/jam/images/definitions/regular` line 323). Not in `distro/`.
- PCI IDs for the AX201: `0x02f0`, `0xa0f0`, `0x06f0`, `0x43f0` (lines 11052–11056). Which one the X1 Carbon Gen 8 has is not checked; `listdev` on the X1 shows it.
- What it offers: `ic_caps` with A-MPDU, ADDBA offload, RSN and `IEEE80211_C_MFP` (lines 12001–12011); HT 20/40 MHz with short guard interval (lines 12013–12016); VHT 80 MHz (lines 12021–12026). So the X1 does Wi-Fi 5 and PMF already on paper. No 802.11ax: OpenBSD net80211 has modes up to `IEEE80211_MODE_11AC` (`openbsd_wlan/net80211/ieee80211_var.h` lines 92–98). The driver skips 6 GHz channels: "net80211 cannot handle 6 GHz channel numbers yet" (line 3087). That this all works on the X1 under R5 is not tested.

### The two wireless stacks

| Stack | Where | License | HT (Wi-Fi 4) | VHT (Wi-Fi 5) | HE (Wi-Fi 6) | PMF (802.11w) | SAE (WPA3) |
|---|---|---|---|---|---|---|---|
| FreeBSD net80211 (used by `mt7922wifi`) | `haiku/src/libs/compat/freebsd_wlan/net80211/`; R5 copies of `ieee80211_haiku.cpp`, `ieee80211_ioctl.c/.h`, `ieee80211_output.c`, `ieee80211_scan_sw.c`, `ieee80211_sta.c` in `distro/kernel/src/libs/compat/freebsd_wlan/net80211/` | `BSD-2-Clause` (SPDX in `ieee80211.c`, `ieee80211_input.c`, `ieee80211_ht.c`, `ieee80211_scan_sta.c` line 2); `ieee80211_vht.c` is Adrian Chadd's BSD text without SPDX | yes (`ieee80211_ht.c`, modes `11NA`/`11NG`, `_ieee80211.h` lines 68–69) | yes (`ieee80211_vht.c`, modes `VHT_2GHZ`/`VHT_5GHZ`, lines 72–73) | only element numbers and structures: `IEEE80211_ELEMID_EXT_HE_CAPA` 35, `HE_OPER` 36, `HE_6GHZ_BAND_CAPA` 59 (`ieee80211.h` lines 1657–1678), `struct net80211_he_cap` (around line 1317). No `.c` file uses them, and there is no HE mode | only cipher numbers `IEEE80211_CIPHER_BIP_CMAC_128` and others (`ieee80211_crypto.h` lines 141–144); no BIP module | the AKM numbers `RSN_ASE_SAE_UNSPEC` 8, `RSN_ASE_FT_SAE` 9 (`ieee80211.h` lines 1887–1888); nothing passes SAE authentication frames |
| OpenBSD net80211 (used by `iaxwifi200`) | `haiku/src/libs/compat/openbsd_wlan/net80211/` | BSD/ISC, per file (check before use) | yes (`IEEE80211_MODE_11N`) | yes (`IEEE80211_MODE_11AC`, `ieee80211_ra_vht.c`) | no | yes (`ieee80211_crypto_bip.c`, `crypto/cmac.c`) | recognises the SAE AKM (`ieee80211_input.c` lines 1447–1448); the exchange itself is not checked |

The beacon and element parsers that a nearby access point reaches without any password: `ieee80211_parse_beacon()` (`ieee80211_input.c` line 516), `ieee80211_parse_action()` (line 790), `ieee80211_ies_init()` (`ieee80211_node.c` line 1120), `ieee80211_parse_htcap()` and `ieee80211_parse_htinfo()` (`ieee80211_ht.c` lines 1671, 1710), the VHT parse in `ieee80211_vht.c`, `ieee80211_parse_wmeie()`, `ieee80211_parse_wmeparams()`, `ieee80211_parse_csaparams()` (`ieee80211_sta.c` lines 1145, 1168, 1224; already R5's copy), and the RSN and WPA parsers (`ieee80211_hostap.c` lines 1246, 1426).

### wpa_supplicant

- Built in this tree. `distro/wpa_supplicant/build-wpa.sh` fetches haiku/wpa_supplicant commit `bdf3144` (the HaikuPorts 2.11.haiku.1 source, sha256 in `pins.sh`) and packs `wpa_supplicant-2.11.r5.5-1`. `scripts/pin-mesa-packages.sh` points the image at that package instead of `wpa_supplicant-2.11.haiku.1-1`. SAE and OWE require management protection. A passphrase does not ask for it. The HaikuPorts binary had all three off.
- wpa_supplicant is `BSD-3-Clause`.
- The Network Kit asks it to join through `BNetworkDevice::JoinNetwork()` (`haiku/headers/os/net/NetworkDevice.h` lines 122–126). The authentication modes stop at `B_NETWORK_AUTHENTICATION_WPA2` and `EAP` (lines 15–19), the key modes at `B_KEY_MODE_WPS` (lines 34–41). There is `B_NETWORK_CIPHER_AES_128_CMAC` (line 29), but no WPA3 or SAE value yet.

### The Wi-Fi Kit and the pages

- `libr5wifi.so`, `distro/wifi/lib/`, from air/OS (MIT, `docs/LIBR5.md` line 17 and the "Wi-Fi Kit" section from line 376). The Settings page is `distro/settings/WiFiModule.cpp`; the separate WiFi program is `distro/wifi/app`.
- `WiFiLinkDetails` in `<R5WiFi.h>` already has `phyMode`, `channel`, `frequency`, `dBm`, `txKbps` ("Last transmit rate, 0 when the driver does not report it") and `txDetails` ("For example "HE MCS 11, 2 streams, 80 MHz"") (`R5WiFi.h` lines 102–112).
- `WiFiController::ReadTxRate()` asks the driver with `IEEE80211_IOC_HAIKU_TX_RATE` (`WiFiController.cpp` lines 255–275; the number `0x6003` and `struct ieee80211_haiku_tx_rate` in R5's `ieee80211_ioctl.h` lines 806–807). **Nothing in the kernel answers that request yet** (no handler in `distro/` or `haiku/`), so `txKbps` is always 0 today. `phyMode` comes from the interface media word (`WiFiController.cpp` lines 742–744).

### Where new and changed files go

- A Haiku file that a step changes is copied into `distro/` first, with its original header and copyright, and the R5 lines in `SPDX-Snippet` blocks (copyright 2026 R5OS Contributors, `MIT`). For net80211 that means `distro/kernel/src/libs/compat/freebsd_wlan/net80211/<file>`, next to the files already there, and the copy hook in `scripts/apply-kernel-bugs.py`. The BSD files keep `BSD-2-Clause`; the R5 snippet in them is MIT.
- `iaxwifi200` is copied to `distro/network/wlan/iaxwifi200/` only in the step that changes it (step 4b). It stays ISC.
- New R5 files (fuzz harness, host stubs, tests) are MIT with an SPDX header.
- Only the regular image has Wi-Fi. The minimum image is unchanged.
- Every step updates `CHANGELOG.md`, `tracker/STATUS.md`, `tracker/r5-status.tsv` (with `python3 scripts/tracker-report.py`) and the GitHub issue. `distro/WHATSNEW` and `distro/BUILD` change only when the owner asks. ARM64 and RISC-V 64 keep building and are not tested.

## 2. Licenses of the sources we may read or use

| Source | SPDX | How we use it |
|---|---|---|
| FreeBSD net80211 (in the tree) | `BSD-2-Clause` (checked per file above) | We change it in `distro/` copies |
| OpenBSD net80211 and `if_iwx.c` (in the tree) | ISC, some files BSD; check per file | We change `if_iwx.c` in a `distro/` copy in step 4b |
| Linux mt76 (`drivers/net/wireless/mediatek/mt76/`) | most files `ISC` or `BSD-3-Clause-Clear`; **check the SPDX line of every file** before use | Register layouts and command formats as reference. Code may be taken only from a file whose SPDX line says ISC or BSD, with that header and copyright kept, in its own file or a marked snippet |
| Linux mac80211 and cfg80211 | `GPL-2.0-only` | Reference only, to understand behaviour. Never pasted into R5 or BSD code |
| wpa_supplicant / hostapd | `BSD-3-Clause` | Package; R5's own recipe if section 5 choice 3 says so |
| MediaTek firmware (linux-firmware) | MediaTek's redistribution licence (`LICENCE.mediatek`), unmodified | Already shipped |
| Intel iwlwifi firmware | Intel's redistribution licence | Already shipped by Haiku for `iaxwifi200`; an AX210/BE200 needs newer files, which get their own report |

Every new outside source gets its own written report first: project, SPDX license (`-only` or `-or-later`), copyright, how it is built in, and the lines for `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`.

## 3. Layers and kits

| Layer | Where | License | What it does |
|---|---|---|---|
| Driver | `distro/network/wlan/mt7922wifi/` (later `distro/network/wlan/iaxwifi200/`) | air/OS MIT (ISC for `iaxwifi200`) | Hardware: rings, firmware, station records, rates, keys |
| 802.11 stack | `distro/kernel/src/libs/compat/freebsd_wlan/net80211/` (copies of the files we change) | `BSD-2-Clause` + R5 `SPDX-Snippet` MIT | Scan, join, HT/VHT/HE elements, A-MPDU, keys, PMF |
| Supplicant | `wpa_supplicant` package (system service, uid 0) | `BSD-3-Clause` | WPA2, WPA3-SAE, PMF keys |
| Network Kit | `libbnetapi.so` (`BNetworkDevice`, `BNetworkRoster`, `wireless_network`) | Haiku MIT | Join, leave, scan, remembered networks. R5 only adds enum values and new calls at the end; the existing ABI stays |
| R5 Wi-Fi Kit | `libr5wifi.so`, `<R5WiFi.h>` and friends | MIT | `WiFiController`, `WiFiState`, `WiFiLinkDetails`, `WiFiView`. The one place where an application asks "which standard, how fast, how secure" |
| Pages | `distro/settings/WiFiModule.cpp`, `distro/wifi/app`, the Deskbar network icon | MIT | Only use the kits. No ioctl in a page |

Rules for the kits, the BeOS way:

- **Network Kit:** the only additions are new enum values where `wireless_network` already has the field: `B_NETWORK_AUTHENTICATION_WPA3` (5) after `EAP`, and `B_KEY_MODE_SAE` and `B_KEY_MODE_OWE` after `B_KEY_MODE_WPS`. A Haiku program that knows only the old values keeps working; it sees an unknown number for a WPA3 network, as it would on Haiku when a new mode appears. `wireless_network` itself does not grow (that would break the ABI). Changes go into a `distro/` copy of `NetworkDevice.h` and its source, with `SPDX-Snippet`. These values belong in the Network Kit because joining is already its job; a Wi-Fi Kit wrapper around them would be a second implementation.
- **Wi-Fi Kit:** what Haiku has no place for goes here: the standard (`Wi-Fi 4`, `5`, `6`, `6E`, `7`), the channel width, the number of streams, the receive and transmit rate. They are new fields at the end of `WiFiLinkDetails` (read with `Unarchive()`, so older readers ignore them) and new message fields, described in `docs/LIBR5.md` and the R5Book page `docs/r5book/wifi-kit.html` in the same change. A developer reads `WiFiState.link.standard`, not a media word.
- **Kernel to kits:** the driver answers `IEEE80211_IOC_HAIKU_TX_RATE` (already defined, see section 1) and one new R5 request at the end of the `0x60xx` range for the standard and the receive rate. The struct for an existing request never changes; a new field means a new request. The Wi-Fi Kit is the only user of these requests.

## 4. Steps

Each step is its own small pull request, built for AMD64 with `scripts/build-locked.sh` and kept building for ARM64 and RISC-V 64 (not tested). QEMU runs on an own copy of `r5-os.image`, never through `scripts/run-qemu-r5.sh`.

**Sticks.** Hardware tests are bundled. Stick W1 carries steps 1 and 2 (with a driver setting that turns HT/VHT off, so "before" and "after" are measured on one boot). Stick W2 carries steps 3 and 4a. Steps 4b and 5 go on the X1 stick or the X1 over SSH.

**SMAP.** Every copy between a user buffer and the kernel uses `user_memcpy()` (Haiku) or `copyin()`/`copyout()` (the compat layer), never a plain `memcpy` on a user pointer. R5's `ieee80211_haiku.cpp` already copies the request with `user_memcpy()` (lines 240 and 253); each step that adds a request is reviewed by hand for this and boots QEMU with SMAP on.

**How the fuzzers run.** Like `scripts/fuzz-bt.sh`: in WSL on the host, `g++`/`gcc` with `-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1`, the parser files compiled on their own with small host stub headers in `scripts/wifi-host/` (like `scripts/bt-host/`). Each target runs 60 seconds.

### Step 1 (PR 1): hardening — fuzzers, receive checks, nothing from the air stops the kernel

- **What:**
  - `scripts/fuzz-wifi.sh` and `scripts/wifi-host/` (new, MIT). Targets: `ies` (`ieee80211_ies_init`, `ieee80211_parse_beacon` and the HT, VHT, WME and channel-switch parsers listed in section 1), `rsn` (the RSN and WPA element parsers), `action` (`ieee80211_parse_action`), and `mtkrx` (`mtk_receive_frame`: the RX descriptor, the lengths and the header offsets, fed random ring contents).
  - Every bug found is fixed in a `distro/` copy of that net80211 file, or in `mtk_tx.c`. A frame that does not fit is dropped and counted, never a `panic()` or an endless loop.
  - Timeouts: the 2 s firmware reply wait stays; a scan that the firmware never ends is stopped after a bound and logged; a join that hangs falls back to "not connected" instead of holding the stack.
  - The license line of `mtk_tx.c`, from air/OS.
- **Required checks:** manual SMAP review, a QEMU boot with SMAP, and `scripts/fuzz-wifi.sh <target> 60` for each target.
- **Pass:** QEMU SMAP boot without panic; `wifi_fuzz ies: 60s 0 crashes` (and the same for `rsn`, `action`, `mtkrx`); on stick W1, a WPA2 join and an hour near the owner's access point without a panic.

### Step 2 (PR 2): Wi-Fi 4 and 5 (802.11n and 802.11ac) on the MT7921

- **What:**
  - `mtk_getradiocaps()` adds `IEEE80211_MODE_11NG` and `11NA`, and the VHT 5 GHz mode, with the matching HT40 and VHT80 channel flags.
  - `ic_htcaps` and `ic_vhtcaps` from what the card reports in its capability event (not typed in by hand), `IEEE80211_C_TX_AMPDU` and `IEEE80211_C_AMPDU` for aggregation, and the HT and VHT fields in the station record sent to the firmware (`mtk_sta.c`, layouts from mt76 as reference).
  - Rate control stays in the firmware, as on Linux. The driver reads the rate the firmware reports and answers `IEEE80211_IOC_HAIKU_TX_RATE`, so the Wi-Fi Kit shows a real `txKbps`.
  - Receive reordering for aggregated frames: through net80211's A-MPDU reorder unless the firmware already does it; which one is checked against mt76 before coding.
  - A driver setting (`mt7922wifi` settings file, like the radeon_hd settings) that turns HT/VHT off, for the before-and-after test and as an escape when an access point misbehaves. Default on once stick W1 passes.
- **Required checks:** the step-1 fuzzers again (the HT and VHT parsers are now reached), manual SMAP review, QEMU SMAP boot.
- **Pass:** on stick W1, the same access point, the same place: the Wi-Fi page shows "Wi-Fi 5" (or "Wi-Fi 4" on 2.4 GHz) and a link rate; a download of the same file from a computer on the LAN (with the setting off, then on) is clearly faster with it on, and both numbers go into `CHANGELOG.md` with distance and channel. No panic in an hour.

### Step 3 (PR 3): WPA3-SAE and PMF (802.11w)

- **What:**
  - Read the HaikuPorts recipe and package of `wpa_supplicant` 2.11 for `CONFIG_SAE`, `CONFIG_IEEE80211W` and `CONFIG_OWE`. If they are off, R5 builds wpa_supplicant from its own recipe in this repository (section 5, choice 3).
  - net80211 (`distro/` copies): pass SAE authentication frames (algorithm 3) between wpa_supplicant and the air, as FreeBSD's `driver_bsd` lacks; add the BIP-CMAC-128 cipher for PMF and `IEEE80211_C_MFP`. OpenBSD's `ieee80211_crypto_bip.c` is the BSD/ISC example; Linux mac80211 is only read.
  - The MT7921: the PMF key (IGTK) to the firmware, and the PMF bit in the station record.
  - Network Kit: `B_NETWORK_AUTHENTICATION_WPA3`, `B_KEY_MODE_SAE`, `B_KEY_MODE_OWE` (section 3). The Wi-Fi Kit shows "WPA3" in the list and in the link details.
  - A "WPA2/WPA3 mixed" network joins with WPA3 when both sides can.
- **Required checks:** the `rsn` fuzzer, manual SMAP review, QEMU SMAP boot. Password material never in a log or in argv (`AGENTS.md`).
- **Pass:** on stick W2, a join with a WPA3-only network (the owner's router in WPA3 mode, or a phone hotspot that offers WPA3) and with a WPA2 network on the same boot; syslog shows `sae: authenticated` and `pmf: on`.

### Step 4a (PR 4): Wi-Fi 6 (802.11ax, HE) in net80211 and on the MT7921

- **What:**
  - net80211 (`distro/` copies): an HE mode for 2.4 and 5 GHz, parsing of the HE Capabilities and HE Operation elements (numbers 35 and 36 already in `ieee80211.h`), the HE element in our association request, and HE in the node so the driver can fill the station record. FreeBSD has no HE code to copy; this is R5's own MIT code in snippets, written from the 802.11ax standard, with mt76 (ISC) as reference for what the firmware wants.
  - The MT7921: HE fields in the station and BSS records, HE capabilities from the card's own report.
  - New HE parsers get a fuzz target `he` in `scripts/fuzz-wifi.sh` in the same PR.
- **Pass:** `wifi_fuzz he: 60s 0 crashes`; on stick W2 with a Wi-Fi 6 router: the page shows "Wi-Fi 6", the link details show "HE MCS …", and the download speed is recorded next to the step-2 number.

### Step 4b (PR 5): Wi-Fi 6 on the AX201 (X1)

- **What:** first confirm on the X1 that `iaxwifi200` joins at all under R5 and shows Wi-Fi 5 (it has HT, VHT and MFP on paper). HE for `iaxwifi200` needs HE in OpenBSD net80211, which does not exist; that is a second HE implementation. Section 5, choice 4.
- **Pass:** on the X1: a join, "Wi-Fi 5" on the page, a recorded speed; HE only if choice 4 says so.

### Step 5 (PR 6): the standard and the link speed on the page and in Deskbar

- **What:** new fields at the end of `WiFiLinkDetails` (standard, width, streams, receive rate), filled from the requests in section 3. The existing Wi-Fi page (`WiFiModule.cpp` through `WiFiView`) and the Deskbar network icon show "Wi-Fi 6 · 600 Mbit/s" in the details. No new layout and no new Settings rule; the page keeps the Settings frame of `AGENTS.md`. `docs/LIBR5.md` and the R5Book page describe the fields in the same PR.
- **Pass:** in QEMU the page still shows "no adapter" without a crash; on the next stick the page and Deskbar show the standard and a rate that matches the driver log.

### Step 6 (paper): Wi-Fi 6E and Wi-Fi 7

- **6E (6 GHz):** 6 GHz channel numbers and frequencies in net80211 (now skipped, as `if_iwx.c` line 3087 says), the HE 6 GHz band element (59, already defined), scanning that respects the 6 GHz rules (no blind probing, reduced neighbour reports), and the regulatory table for 6 GHz per country. Needs a 6E card.
- **Wi-Fi 7 (802.11be):** EHT elements, 320 MHz, 4096-QAM, and above all Multi-Link Operation (MLO), which changes how a node, a key and a link relate in net80211. That is the largest change in this document. Needs a Wi-Fi 7 card.
- **Cards** (none bought; section 5, choice 2):
  - **MediaTek MT7922** (Wi-Fi 6E, the PCI IDs `0x7922` and `0x0616` are already in `if_mtk.c` lines 120 and 124) and **MT7925** (Wi-Fi 7; whether it does 320 MHz or stops at 160 MHz is not checked). Same driver family as now, so the least new work.
  - **Intel AX210** (6E) and **BE200** (Wi-Fi 7). They need `iaxwifi200` and newer Intel firmware; BE200 support in OpenBSD `if_iwx.c` is not checked, and the BE200 is reported not to work in some AMD laptops (unsure, check first).
  - **Fit:** all four are M.2 2230 cards. Whether the 5625U laptop has a replaceable M.2 card (and no BIOS allow-list) is not checked. The AX201 in the X1 Carbon Gen 8 is a CNVi card (`0x02f0` family), which needs an Intel CNVi slot; whether it is soldered or socketed on that model is not checked. Look in the maintenance manual of each laptop before buying. A USB dongle is the other way in (section 0).
- **Pass:** none until a card is bought; this step stays paper.

## 5. The owner's choices

1. **Test path.** (a) Only sticks on the 5625U, bundled per two steps, or (b) first a USB MT7921AU dongle through VirtualBox for join and WPA3 tests, and sticks only for speed.
   *Recommendation: (a) for now.* The dongle needs one to two sessions of USB driver work and still does not measure speed. With two bundled sticks (W1, W2) steps 1 to 4a are covered. Revisit if a stick turns up a bug that needs many retries.
2. **Hardware for 6E and Wi-Fi 7.** (a) Buy nothing now, (b) an MT7922 card, (c) an MT7925 card, (d) an Intel AX210 or BE200.
   *Recommendation: (a) until step 4a passes, then (c) MT7925* if the 5625U laptop has a replaceable M.2 slot: the same driver family as today, and it covers 6E and Wi-Fi 7 in one card. Intel means a second driver with a second stack.
3. **wpa_supplicant.** (a) Keep the HaikuPorts package and hope its build has SAE and PMF, or (b) build it from R5's own recipe in this repository, like Mesa.
   *Choice (b) is in the tree.* The check found SAE, PMF and OWE off in the HaikuPorts build. The recipe turns them on. A stick that joins WPA3 and WPA2 on the same boot is still the pass; that boot has not been done.
4. **The X1 (AX201).** (a) Wi-Fi 5 and PMF only, as OpenBSD gives it, or (b) also Wi-Fi 6, which means writing HE a second time for OpenBSD net80211, or (c) later move the X1 to FreeBSD net80211 so both machines share one stack.
   *Recommendation: (a) now.* The AX201's Wi-Fi 5 is already fast; a second HE implementation costs more than the gain. (c) is a bigger choice for after step 4a.
5. **Order.** This plan does hardening first, then speed (11n/ac), then WPA3, then Wi-Fi 6. WPA3 could go before speed.
   *Recommendation: keep this order.* Speed is what the owner notices every day, and step 2 is mostly turning on what net80211 already has; WPA3 needs the most new kernel work outside the driver.
