# R5 download, installer and updates: design choices

Owner decisions, 23 Nov 2026. On the roadmap since 23 Nov 2026 (`docs/ROADMAP.md`, Near term, item 9). Build brief: `docs/briefs/INSTALL-UPDATE.md`. Background: gap 1 of the daily-driver research of 23 Nov 2026 (kept outside git in `generated/`).

## Decided

| Topic | Choice |
|---|---|
| First audience | Testers and enthusiasts |
| Download | The USB-stick ISO only (`r5-os-anyboot.iso`) |
| Where | GitHub (owner, 23 Nov 2026, replacing Cloudflare R2): the package repository on GitHub Pages, deployed by a GitHub Actions workflow (not committed to git, so the repository does not grow by about 126 MB per build); the ISO compressed on a GitHub Release with its checksum. Later an R5 domain on top of Pages, so the `baseurl` in R5 can move to another host without breaking installed systems |
| Installer | Next to Windows: R5 shrinks the Windows partition itself (as Ubuntu does). Erase the disk stays as the second choice |
| BitLocker on | R5 does not touch the disk. It explains how to shrink Windows in Disk Management, then you start the installer again. Windows stays encrypted |
| Start | R5's own boot menu with the entries "R5 Operating System", "Windows", "R5 Operating System, previous state…", "Safe mode…". The product name is written in full (owner, 23 Nov 2026) |
| Updates | R5's own package server, with only R5's own packages (system, drivers, R5 apps, Mesa/RADV). Only changed packages are downloaded, never the ISO |
| Other software | Stays on HaikuPorts through HaikuDepot. The update page does **not** update HaikuPorts software |
| Update page | Settings, under System: "Software update". Only an administrator installs (gray until the lock is open); every account sees the "updates ready" notice |
| Download in the background | On by default, and can be turned off. Install and restart only after a click |
| Shown per change | The page lists what changed in user language (one line per change, an icon and a size), not per build. The build number stays in small text. The lines come from What's new (`distro/WHATSNEW`), which is already user language |
| Choosing changes | No: every change in an update is installed together, because that is the set R5 tested |
| Installed list | A second view, "Installed", with installed changes by date and a button "Go back to before <date>…" (R5 starts in that earlier state; your files stay). The boot menu keeps "R5 Operating System, previous state…" too |
| Security fixes | Separate and at once, marked red, without waiting for the next update |
| Online | The last 3 builds; an installed system keeps its own previous state for the boot menu |

### Installer screens (realistic mockup in chat, 23 Nov 2026)

The final mockups (Software update, both views, and the six installer screens) use the R5 Modern look from `docs/design/THEMING.md` and `docs/design/R5-MODERN-DECORATOR.md`: the round window with an 18 px radius and the soft yellow tab, panel `#eef1f5`, white cards with 8 px corners, buttons `#f7f8fa`/`#b4bcc8` with 7 px corners, accent `#0084ff` for the default button and the selected sidebar row, the segmented control, and Manrope. The installer header shows the R5 wave pebble (`distro/artwork/r5-symbol.svg`, the symbol of About R5) without a square around it, 44 px, with the symbol's own 8% margin cropped (viewBox 8 8 84 84) so it lines up with the title and subtitle; owner, 23 Nov 2026. These pages draw only with existing controls (`BControlLook`, `ui_color`), so with the R5 or Classic theme they show in that theme's look.

1. **Choose:** "Install next to Windows" (selected when Windows is found), "Erase the disk and install R5", "Choose a partition myself" (DriveSetup, for advanced users).
2. **Check:** BitLocker off, enough free space in Windows, Windows shut down fully (no Fast Startup or hibernation). Each failed check says what to do in Windows; "Check again". With BitLocker on: shrink in Disk Management yourself, then "Choose a partition myself".
3. **Share the disk:** a bar with Windows and R5 and a slider. **R5 suggests a fixed 64 GB** for itself. Limits: Windows keeps at least what it uses plus 20 GB; R5 gets at least 20 GB. When there is less than 64 GB to spare, the suggestion is what is left. A "back up your files first" card.
4. **Confirm:** the list of what will happen (shrink Windows to X, create R5, add the boot menu with Windows, bring settings). **"I have a backup of my files" must be ticked before Install works** (next to Windows and erase the disk). Cancel is the default button.
5. **Progress:** the steps with a bar and the time left; "keep the power cable in"; no stop while Windows is being changed.
6. **Done:** "take out the USB stick", Restart.

- **Settings carried over from the USB session:** language, keyboard and Wi-Fi, including the password of the network you are joined to. Note: R5's key store is still plain text (`docs/VPN-KIT.md`), so the copied password is stored the same way as any Wi-Fi password today.
- **Boot menu:** a dark text menu in R5 colours with "R5 Operating System", "Windows", "R5 Operating System, previous state…", "Safe mode…", starts R5 Operating System after 5 seconds, arrow keys. It works without a graphics driver.

## Facts behind the choices

- ISO size of the current build: 2,156,724,224 bytes, which is over GitHub's 2 GiB limit per file (2,147,483,648 bytes) ([GitHub docs](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)).
- GitHub Pages: a published site is at most 1 GB, a soft limit of 100 GB of traffic per month, rate limits may apply; deploys through Actions ([Pages limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits)). A file in git is blocked above 100 MiB ([large files](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)).
- Why not GitHub Releases for the packages: Haiku fetches packages from `<baseurl>/packages/<file>` (`haiku/src/kits/package/RepositoryConfig.cpp:265`), and a Release has no subfolders.
- Earlier choice, replaced: Cloudflare R2 (10 GB per month free, then $0.015 per GB per month, downloads free; [R2 pricing](https://developers.cloudflare.com/r2/pricing)).
- Ubuntu shrinks Windows itself but cannot do that with BitLocker on ([Ubuntu docs](https://documentation.ubuntu.com/desktop/en/latest/reference/bitlocker-during-ubuntu-installation)).
- Haiku's boot loader already reads older package states (`haiku/src/system/boot/loader/package_support.cpp`).
- Haiku's Installer has an "Install EFI loader" menu, but partitioning happens separately in DriveSetup (`haiku/src/apps/installer/InstallerWindow.cpp`).
- The Haiku tree has the NTFS library (`haiku/src/add-ons/kernel/file_systems/ntfs/libntfs`), but no tool that shrinks NTFS. Haiku's package kit has no patch (delta) updates inside a package.

## Open

1. A domain name for R5 (not found in the repo).
2. No Cloudflare account needed any more (GitHub hosting, 23 Nov 2026). A domain stays advised, for the custom domain on Pages.
3. A licence check of `ntfsresize` (ntfs-3g, GPL) before it is used (AGENTS.md, Licensing).
4. Done: 3 builds (packages and ISOs) are about 6.9 GB, under the free 10 GB.
5. Done: BitLocker is off on the 5625U (owner, 23 Nov 2026).
6. Done: an installed R5 points to Haiku's system repository and HaikuPorts; see "Where an installed R5 looks for updates today" for the risk.

## How air/OS does it (local clone `airos-haiku`, read 23 Nov 2026)

The clone is shallow (one commit, `a4ddc481`, 8 Nov 2026), so there are no per-commit hashes or authors for this work.

- **Installer:** still Haiku's Installer, with a "whole disk" mode: "Erase a whole disk and set it up for UEFI". It refuses the disk the system runs from, the source disk, read-only disks and disks that are too small. A beta warning and a second confirmation come first, and Cancel is the default. Then it unmounts the disk, writes an empty GPT, creates a 256 MiB FAT32 `EFI` partition and BFS for the rest, installs, and copies `haiku_loader.efi` to `EFI/BOOT/BOOTX64.EFI` with no NVRAM entry (`src/apps/installer/WorkerThread.cpp:970`, `:259-275`; `docs/airos/INSTALLER.md`). MIT headers, unchanged.
- **No dual boot:** no NTFS shrinking, no Windows in a boot menu, no BitLocker check found.
- **Live media:** a hybrid ISO always boots read-only with a write overlay (`src/system/kernel/fs/vfs_boot.cpp:164`). Settings from the live session go to the installed system.
- **Updates:** its own updater `src/apps/systemupdater/` (MIT, "air/OS contributors") downloads whole releases from GitHub Releases (`https://github.com/airos-works/haiku/releases/download/updates-<target>/`). The release images contain **no Haiku package repository**, so Haiku's `haiku` package cannot replace air/OS's (`docs/airos/SYSTEM-UPDATER.md:254-257`). HaikuPorts stays for HaikuDepot. Each release ships an `airos_release` package whose version is the build time.

What R5 can take: the whole-disk mode (our second choice), the read-only live overlay, and removing Haiku's repository from the image. Different from R5's choice: air/OS updates whole releases; R5 chose per-package updates from its own package server.

## Where an installed R5 looks for updates today (checked 23 Nov 2026)

- Every repository in `HAIKU_REPOSITORIES` gets its config in `/system/settings/package-repositories` (`haiku/build/jam/images/definitions/minimum`, lines 436 to 448).
- That includes Haiku's own system repository. The config from R5's build tree has `baseurl https://eu.hpkg.haiku-os.org/haiku/master/x86_64/current`, which is Haiku's server. R5 does not override it (no hit for that URL or `Haiku-config` in `distro/` or `scripts/`).
- HaikuPorts: `https://eu.hpkg.haiku-os.org/haikuports/master/x86_64/current`.
- R5's own repository `R5OpenGL` (Mesa, RADV, Lavapipe, the Vulkan loader; `scripts/pin-mesa-packages.sh`) has a placeholder `baseurl` (HaikuPorts build-packages). There is no online R5 repository.
- Versions: R5's system package is `haiku-r1~beta6_r5_2026.1_dev-1`; Haiku's server serves `r1~beta6_hrev60455` (its `repo.info`, 23 Nov 2026). Haiku compares versions with `NaturalCompare` (`haiku/src/kits/package/PackageVersion.cpp`), so `beta6_r5…` sorts above `beta6_hrev…` and Haiku's package is older **today**.
- **Fixed on branch `claude/no-haiku-repo` (commit 80e5950875, 23 Nov 2026): the image no longer contains the Haiku repository.** Risk it removes (reasoning from that code, not tested): when Haiku's master moves to `r1~beta7` or a final `r1` (no `~` part sorts above any pre-release), SoftwareUpdater on an installed R5 would see Haiku's system package as newer and could replace R5's system with plain Haiku. The same holds for R5's Mesa if HaikuPorts ever ships a higher Mesa version. The R5 package server must replace the Haiku repository config, and R5's versions must always sort above Haiku's.

## Package sizes (partly measured, 23 Nov 2026)

From the shared WSL build tree and `distro/packages/x86_64` on `origin/main`: `haiku_datatranslators` 1.9 MB, `haiku_devel` 4.0 MB, `haiku_loader` 0.3 MB, `webpositive` 0.5 MB, `netfs` 0.5 MB, `userland_fs` 0.3 MB, `smbfs` 0.1 MB; `mesa` 5.6 MB, `mesa_radv` 4.3 MB, `mesa_lavapipe` 3.1 MB, `vulkan_loader` 1.8 MB, `llvm21_runtime` 56.4 MB (latest of each). `haiku.hpkg` (`haiku-r1~beta6_r5_2026.1_dev-1`, the system package): 47,282,942 bytes, measured after that build finished (18:02, 23 Nov 2026).

Sum of the latest of each package above: about 126 MB per build, of which `llvm21_runtime` (56.4 MB) rarely changes. Three builds of packages: at most about 380 MB. Three ISOs: about 6.5 GB (2.16 GB each). Together about 6.9 GB, under R2's free 10 GB per month. A typical update downloads mostly `haiku.hpkg` (about 47 MB), because almost every R5 change lands in that package. Shrinking that further would mean splitting the system package, which is a later question.

On GitHub: three builds of packages (about 380 MB) fit in the 1 GB Pages site; the largest package (`llvm21_runtime`, 56.4 MB) is under 100 MiB; 100 GB per month is roughly 2,000 updates of 47 MB. The ISO (2,156,724,224 bytes) is over the 2 GiB Release limit, so it ships compressed; its compressed size is not measured yet.

## Licence check: shrinking Windows (23 Nov 2026)

| Question | Finding | Source |
|---|---|---|
| Project | ntfs-3g (Tuxera), `ntfsprogs/ntfsresize.c`; latest release 2026.9.28 | [ntfsresize.c](https://raw.githubusercontent.com/tuxera/ntfs-3g/edge/ntfsprogs/ntfsresize.c), [releases](https://github.com/tuxera/ntfs-3g/releases) |
| SPDX | `GPL-2.0-or-later` (header: "version 2 ... or (at your option) any later version"). The library `libntfs-3g` is GPL-2.0-or-later too, not LGPL | ntfsresize.c header; [README](https://raw.githubusercontent.com/tuxera/ntfs-3g/edge/README) |
| Copyright | Szabolcs Szakacsits, Anton Altaparmakov, Richard Russon, Yura Pakhuchiy, Jean-Pierre Andre | ntfsresize.c header |
| Already in R5? | The NTFS driver in the image is built from the same library (`haiku/src/add-ons/kernel/file_systems/ntfs/libntfs`, GPL-2.0-or-later headers; `minimum` image definition line 389). It cannot resize (`resize` hook is `NULL` in `kernel_interface.cpp`). No `ntfsresize` in the tree | repo, `origin/main` |
| HaikuPorts | No ntfs-3g recipe | [haikuports sys-fs](https://github.com/haikuports/haikuports/tree/master/sys-fs) |
| Integration | As a **separate command-line program** that the installer starts. The installer stays MIT. The GPL FAQ treats exec and command-line arguments as communication between separate programs | [GPL FAQ, GPLPlugins](https://www.gnu.org/licenses/gpl-faq.html#GPLPlugins), [MereAggregation](https://www.gnu.org/licenses/gpl-faq.html#MereAggregation) |
| Duties | Ship the source (or a written offer), the GPL-2.0 text in `LICENSES/`, a line in `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`. Never paste its code into an MIT file | AGENTS.md, Licensing |
| Combination | GPL-2.0-or-later may be combined with GPL-2.0 or GPL-3.0 code. Not linked into the kernel; a userland tool. No conflict found with the VirtualBox Guest Additions (GPL-3.0-only, a separate part) | AGENTS.md |
| Safety | ntfsresize refuses a hibernated Windows (also Fast Startup): "The NTFS partition is hibernated..." Handling of a BitLocker volume was not found in the source that was read, so the installer itself must detect BitLocker (step 2 of the mockup) | ntfsresize.c, libntfs-3g/volume.c |

Verdict: allowed, as a separate GPL-2.0-or-later program with the source and notices. The owner decides before it is added.

**Found on the way (exists already):** the NTFS driver that R5 ships today is GPL-2.0-or-later (ntfs-3g), but `docs/LICENSING.md` and `THIRD_PARTY_NOTICES.md` do not mention it (search for "ntfs": no hits). That is a gap in the notices, separate from this design.

## Mockups (shown in chat, 23 Nov 2026)

1. Installer: choose next to Windows, or erase the disk.
2. BitLocker on: shrink in Windows, then start again.
3. Share the disk with a slider (Windows / R5).
4. Confirm, with "back up your files first".
5. Boot menu: "R5 Operating System", "Windows", "R5 Operating System, previous state…", "Safe mode…", starts R5 Operating System after 5 seconds.
6. Update notice, and the Software update page: build, changed packages with their size, "Install and restart", options, then the lock, Revert and Defaults. Update after these choices: the HaikuPorts checkbox is gone.
