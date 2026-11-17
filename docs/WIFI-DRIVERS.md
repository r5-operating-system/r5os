# Wi-Fi 6 on many cards

This is a study for R5 Operating System: how R5 can support Wi-Fi 6 on most laptop cards, not only the MT7921 of the 5625U. It is a companion to `docs/WIFI.md`. That document builds Wi-Fi 4, 5 and 6 on the MT7921; this one asks how the other chip families get there. Nothing here is built or tested.

The study was written on 17 Nov 2026. Sources: FreeBSD `main` (read from GitHub), R5 `origin/main`, Haiku `upstream/master` (13 Nov 2026), and the repositories of other Haiku forks. **[code]** means checked in source. **[doc]** means it comes from a wiki, a README, a status report or news.

## 1. The short answer

The idea was to port FreeBSD's LinuxKPI 802.11 layer, so the Linux drivers run as they do on FreeBSD. That is legally clean: the layer is BSD, and the drivers are dual-licensed, ISC or BSD-3-Clause-Clear. But **on FreeBSD, Wi-Fi 6 (HE) is not on through LinuxKPI either**, and FreeBSD's own net80211 has no HE mode. A port would give R5 a large Wi-Fi 5 stack that is still half-working on several chips, not Wi-Fi 6.

So the core of "Wi-Fi 6 for everyone" is the **HE layer in the shared net80211**, step 4a of `docs/WIFI.md`. R5 has to write that itself either way. Every driver on that stack then gets it.

## 2. FreeBSD LinuxKPI 802.11

- **Files [code]:**
  - `sys/compat/linuxkpi/common/src/linux_80211.c` (10,309 lines) and `linux_80211_macops.c` (1,041);
  - the headers `linux_80211.h`, `include/net/mac80211.h` and `include/net/cfg80211.h`;
  - the module `linuxkpi_wlan`.
- **License [code]:** the BSD-2-Clause text, copyright 2020-2026 The FreeBSD Foundation and Bjoern Zeeb. `linux_80211.c` has no SPDX line.
- **Dependencies [code]:**
  - the whole LinuxKPI core: 51 source files and 251 `include/linux` headers. For Wi-Fi that means skbuff, workqueue, PCI with MSI/MSI-X, DMA, firmware loading, RCU and timers;
  - FreeBSD net80211 itself, underneath.
- **HT/VHT/HE [code]:**
  - HT, VHT, hardware crypto and WME are on in `main`;
  - HE is not. It is a to-do (`IMPROVE("he, eht, bw_320, ...")`, around line 1131);
  - FreeBSD net80211's modes stop at `IEEE80211_MODE_VHT_5GHZ`, and there is no `ieee80211_he.c`.
- **Drivers on FreeBSD [doc]** (15.0 release notes, status reports Q4 2025 and Q3 2026):
  - iwlwifi is the mature one, with VHT on Intel Wi-Fi 5/6/7; the BE200 needed newer firmware;
  - rtw89 transmits at basic rates and can panic; rtw88 associates poorly;
  - mt7921/22 run at basic rates, and testers are wanted;
  - ath10k/11k/12k: no status found. The ath and mt76 module directories exist, but the top-level build does not include them [code].

## 3. Licenses

| Driver (FreeBSD `sys/contrib/dev`) | SPDX [code] | Chips |
|---|---|---|
| iwlwifi | `GPL-2.0 OR BSD-3-Clause` | Intel AX200/201/210/211, BE200 |
| rtw88, rtw89 | `GPL-2.0 OR BSD-3-Clause` | Realtek RTL8822/8821C, RTL8852/8851/8922 |
| ath10k | `ISC` | Qualcomm QCA988x/6174 |
| ath11k, ath12k | `BSD-3-Clause-Clear` | Qualcomm QCA6390, WCN6855, WCN7850 |
| mt76 | `BSD-3-Clause-Clear` | MediaTek MT7921/22/25 |

The study checked only one or two files per driver. A per-file SPDX scan is needed before any import, and R5 takes the BSD side of a dual license. The firmware for these chips is redistributable unmodified under each vendor's linux-firmware licence. That comes from general knowledge and was not checked file by file. R5 already ships `LICENCE.mediatek`.

## 4. What Haiku and R5 have

- **LinuxKPI [code]:** nothing, in either Haiku or R5. The word appears only in two comments in `freebsd_wlan/net80211`.
- **Compat layers:** `freebsd_network`, `freebsd_iflib`, `freebsd_wlan`, `openbsd_network`, `openbsd_wlan`.
- **Wi-Fi 5 or newer:**
  - `iaxwifi200`: OpenBSD iwx for the AX2xx, VHT, no HE, skips 6 GHz;
  - `idualwifi7260`: iwm, 11ac;
  - R5's `mt7922wifi`: legacy rates today; `docs/WIFI.md` takes it to HT, VHT and HE.
- **Older drivers:** `atheroswifi` (11n), `broadcom43xx`, `realtekwifi` (rtwn), `ralinkwifi` and others.
- **Upstream Haiku in 2026 [doc]:** a sync of the OpenBSD Wi-Fi code and the FreeBSD 15.0 drivers. No LinuxKPI work. A forum reply says rtw88 cannot be supported for that reason.

## 5. Other Haiku forks

| Fork | Repository, last push | License | Wi-Fi | Reusable for R5 |
|---|---|---|---|---|
| air/OS | github.com/airos-works/haiku, 17 Nov 2026 | files say MIT | `mt7922wifi` (the same base as R5's, no HT/VHT); a second MT7922 driver in C++; `aic8800wifi` (USB); `broadcomfmac` with SDIO. No LinuxKPI | Yes, MIT |
| RenkuOS | github.com/RenkuOS/Source, 17 Nov 2026 | Haiku's `License.md` | `mediatekwifi`: OpenBSD mwx for the MT7920/7921, at **HT** since Oct 2026 ("Ported-from: Linux v6.18 mt76 … ISC"). `atheros10kwifi`: FreeBSD athp (native ath10k). `broadcom43xxn`, `brcmfmac`. No LinuxKPI | Probably; check the file headers first |
| Haiku-Pi | github.com/haiku-pi, 5 Nov 2026 | MIT | BCM43455 through OpenBSD `bwfm`, WPA2 | Raspberry Pi only |

## 6. Effort (rough, in AI sessions, without boots)

| Work | Sessions |
|---|---|
| LinuxKPI core subset on Haiku | 6–10 |
| `linux_80211` on R5's net80211 | 3–5 |
| iwlwifi | 3–5 |
| rtw89, mt76, ath11k/12k | 3–6 each |
| HE in net80211 (`docs/WIFI.md` step 4a) | needed on every route |
| HE through LinuxKPI | extra; FreeBSD has not done it yet |

What is hard:
- the mac80211 model: the station state machine, keys, block-ack offload, queues;
- MSI-X per queue;
- MHI for ath11k/12k;
- ACPI hooks;
- large firmware;
- `struct page`;
- the user copies in debugfs and sysctl shims (SMAP: everything through `user_memcpy()`).

Testing costs the most: one stick per chip, and R5 has only the MT7921 and the AX201.

## 7. Options

1. **LinuxKPI now.** Many drivers in one move, but 15–25 sessions before the first card works. It brings Wi-Fi 5, not 6, and on several chips it is half-working even on FreeBSD.
2. **One shared HE layer, then native drivers per family.**
   - HE in FreeBSD net80211 (step 4a) is the base.
   - Then the drivers that already exist natively, family by family:
     - MediaTek: our `mt7922wifi`, with RenkuOS's mwx as a reference;
     - Qualcomm ath10k: RenkuOS athp;
     - Intel: check whether FreeBSD's native `iwx` port (on FreeBSD net80211) exists and can replace R5's OpenBSD `iaxwifi200`. This is choice 4c in `docs/WIFI.md`. Not checked in this study.
3. **Follow FreeBSD.** Option 2 now, and LinuxKPI later, once FreeBSD runs HE through it and the drivers are stable there.

**Recommendation: option 3.**
- HE in the shared stack is needed on every route.
- The native drivers are smaller, and Haiku forks are already working on them.
- LinuxKPI only pays off once FreeBSD has solved HE and driver stability, and R5 can then take that work instead of doing it twice.
- The first step after `docs/WIFI.md` step 4a is the Intel check: Intel cards are in most laptops.

## 8. Not found

- The FreeBSD wiki pages, which were blocked by a bot check.
- The status of ath10k/11k/12k on FreeBSD.
- Which iwlwifi files FreeBSD leaves out as GPL-only.
- The file headers in RenkuOS, and whether air/OS has unpublished HE work.
- An official Haiku statement on LinuxKPI.

Sources:
- [FreeBSD 15.0 release notes](https://www.freebsd.org/releases/15.0R/relnotes/)
- [FreeBSD status Q4 2025](https://www.freebsd.org/status/report-2025-10-2025-12/lkpi-wireless/)
- [FreeBSD status Q3 2026](https://www.freebsd.org/status/report-2026-07-2026-09/lkpi-wireless/)
- [linux_80211.c history](https://cgit.freebsd.org/src/log/sys/compat/linuxkpi/common/src/linux_80211.c)
- [Phoronix, Haiku Feb 2026](https://phoronix.com/news/haiku-os-february-2026)
- [Haiku forum, Realtek BSD driver](https://discuss.haiku-os.org/t/adding-an-updated-bsd-driver-for-realtek-wifi-possible/13153)
- [air/OS](https://github.com/airos-works/haiku)
- [RenkuOS](https://github.com/RenkuOS/Source)
- [Haiku-Pi](https://github.com/haiku-pi/haiku-rpi)
