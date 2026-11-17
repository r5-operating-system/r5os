# AirDrop on R5

Study of 17 Nov 2026 for R5 Operating System. Nothing in this document is built or tested. Sources: the code of `jedbillyb/airdrop-mt7921` and `t4t5/omdrop-owl` ("Owly"), read with `gh api`; the R5 tree at `origin/main`. [code] = checked in source, [README] = the project's own claim only.

## 1. What AirDrop needs

AirDrop takes six layers:
1. A BLE advertisement that wakes the iPhone.
2. AWDL, Apple's direct Wi-Fi link: a monitor interface, radiotap capture and injection, and the AWDL timing.
3. An IPv6 link-local interface on top of AWDL.
4. mDNS `_airdrop._tcp` on that interface.
5. HTTPS with a self-signed certificate (`/Discover`, `/Ask`, `/Upload`, archive).
6. An app.

Without an Apple-issued identity, only the **"Everyone for 10 minutes"** mode works. That holds for every open implementation.

Wi-Fi Aware (iOS 26, worldwide) is a separate route: an app API, not AirDrop. It only works when the same app runs on both sides. It is not part of this study.

## 2. The two open projects

| | airdrop-mt7921 | Owly (omdrop-owl) |
|---|---|---|
| License | GPL-3.0 (no SPDX, no copyright line in the files). Depends on its own forks of seemoo `owl` and `opendrop`, which are GPL-3.0-or-later | MIT, (c) 2026 T4T5. **Except** `crates/omdrop-owl/omdrop-awdl/`, GPL-2.0-only |
| Last commit | 9 Nov 2026 | 11 Nov 2026, about 30 contributors |
| Works [README] | receive and send, Everyone mode, iOS 26, about 40–67 kB/s, MT7921/MT7922 | both directions on MT7925 (Framework 13) |
| Normal Wi-Fi during transfer | off | stays on ("shared": AWDL on the AP's channel only, which must be 6, 44 or 149) |
| Kernel / firmware | stock; own MCU sniffer patches failed | stock; no MCU commands |
| Language | C (owl) + Python (OpenDrop) | Rust, everything |
| BLE | BlueZ advertising | BlueZ advertising, Apple 0x004C type 0x05, 100 ms, sending only |

Owly reports that shared mode **fails on the MT7921/22** and that injection is unreliable there (issue #14). Our 5625U has an MT7921. Shared mode was proven only on the MT7925.

Linux-only pieces R5 needs its own version of:
- nl80211 and rtnetlink;
- `/dev/net/tun`;
- AF_PACKET with radiotap;
- mt76 debugfs (power save off);
- BlueZ D-Bus advertising.

## 3. What R5 has [code]

| Need | R5 now | AI sessions (rough, no boots) |
|---|---|---|
| Monitor vap beside the station, own MAC | No. `if_mtk.c` has no `IEEE80211_C_MONITOR`, `mtk_vap_create` allows one vap, the Haiku glue (`ieee80211_haiku.cpp:157`) only makes a STA vap | 3–5 |
| Radiotap capture from userspace | No. `bpf_mtap` is a stub (`compat/net/bpf.h:17`), there is no packet socket | 2–3 |
| Inject with rate and no-ack | Partly. `mtk_raw_xmit` exists but ignores `params` | 2 |
| AWDL frames while associated | Partly. `MTK_FILTER_OTHER_BSS` is defined in `mtk_mcu.c` | 1–2 plus hardware |
| Channel hopping 6/44/149 (non-shared) | No. `mtk_set_channel` only records the channel; only remain-on-channel (ROC) exists | 3+, firmware risk |
| TAP `awdl0`, link-local without DAD | Partly. Haiku `tunnel` module (`tap/N`), not the Linux `TUNSETIFF` | 1–2 |
| mDNS `_airdrop._tcp` on one interface | Partly. `distro/zeroconf/MdnsService.cpp` joins per interface, but the records are fixed (AirPlay, SSH, FTP) | 1–2 |
| HTTPS, TLS, archive | Not checked for AirDrop (OpenSSL is there) | 2–3 |
| AWDL protocol | No | 4–6 (port of Owly's MIT crates) |
| BLE advertising (0x2006, 0x2008, 0x200A, extended) | No. `LEManager.cpp:695`: "We never advertise" | 1–2 |

In total, roughly **20–30 AI sessions**, plus hardware boots. The bottleneck is the MT7921: what Owly needs from it failed on Linux.

## 4. Licensing

- airdrop-mt7921, owl and OpenDrop are GPL-3. They cannot be used in R5's MIT code. At most they could ship as a separate program, after a licence report.
- Owly is MIT and the best reference. Its code can be ported with its copyright line kept, in its own files. `omdrop-awdl/` (GPL-2.0-only) stays out.
- Before any work: the licence report from `AGENTS.md` (project, SPDX, copyright, integration, `THIRD_PARTY_NOTICES.md`).

## 5. Options

1. **Wait.** Do nothing until Wi-Fi steps 1–4a have passed.
2. **Building blocks first:**
   - BLE advertising in the Bluetooth server (1–2 sessions, useful beyond AirDrop);
   - monitor mode and radiotap injection in `mt7922wifi` (5–8 sessions, also useful for Wi-Fi debugging).

   No AirDrop yet.
3. **Full AirDrop now** on the MT7921: port Owly, then build the app. 20–30 sessions, with a real chance that the MT7921 firmware does not cooperate.
4. **Full AirDrop on an MT7925.** As in option 3, but on the chip where Owly is proven. This ties in with choice 2 in `docs/WIFI.md` (MT7925 for 6E/Wi-Fi 7).

**Recommendation: option 1 now, then option 2, then option 4.**
- The Wi-Fi plan already touches the same driver; doing AirDrop in between makes both harder.
- BLE advertising and monitor mode are small and useful on their own.
- The MT7925 is the chip where shared mode has been proven, and it is the card the Wi-Fi plan already recommends for later.
- Check first whether the 5625U laptop has a replaceable M.2 slot (`docs/WIFI.md`, step 6).

## 6. Not verified

- All "works" claims are from the projects' READMEs. None was tested by R5.
- The MT7921 shared-mode failure is Owly's own report.
- Session estimates are rough and exclude boots.
- Wi-Fi Aware status: only from appletips.nl (July 2025, iOS 26).
