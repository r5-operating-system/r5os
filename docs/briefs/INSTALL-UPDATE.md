# Brief: R5 download, installer next to Windows, and Software update

Owner's go to write this brief: 23 Nov 2026. Design and every choice: `docs/design/INSTALL-UPDATE.md`. Read it in full; it wins over this brief where they differ. Research behind it: the daily-driver research of 23 Nov 2026 (gap 1, kept outside git in `generated/`).

## Before anything

1. Read `AGENTS.md` in full. Reply with the rules that bind this task: licensing (MIT for R5 code, SPDX snippets, GPL stays GPL), native source in `distro/`, no new calls in `libbe`, the Settings window rules, the LibR5 rules, build with `scripts/build-locked.sh` from your own worktree, ask before a push, a PR or a stick.
2. Read `docs/LIBR5.md`, `docs/APPEARANCE-KIT.md`, `docs/SETTINGS.md`, R5Book (`docs/r5book/`), and one existing Settings page (`distro/settings/PowerModule.cpp`). Use what exists before writing anything new.
3. Look in this order for each part: the R5 libraries; Haiku (`haiku/src/apps/installer`, `haiku/src/apps/softwareupdater`, `haiku/src/kits/package`, `haiku/src/system/boot/loader`); then air/OS (local clone `C:\Users\nickr\Projects\airos-haiku`: `src/apps/installer`, `docs/airos/INSTALLER.md`, `src/system/kernel/fs/vfs_boot.cpp:164`, `src/apps/systemupdater`, MIT).
4. Work in your own worktree from `origin/main`. Do step 0 and stop.

## Fixed already

- The image has no Haiku system repository (PR #4570, `distro/image/r5_late_image`). Keep it that way.
- The UI is the R5 Modern look through `BControlLook` and `ui_color` only; no drawing of your own. GPU mode may add glass and shadow; without a GPU the same page must look right with neither.

## Steps (one PR per step; stop and report after each)

**Step 0, report only, no code.** Answer with file and line:
- Can Haiku's EFI loader start another EFI program (Windows' `\EFI\Microsoft\Boot\bootmgfw.efi`)? If not, what is missing.
- Which repository holds R5's own packages: `package_repo` refuses a mixed vendor (`scripts/pin-mesa-packages.sh`). Propose one R5 repository or two, and how R5's versions always sort above Haiku's and HaikuPorts' (`haiku/src/kits/package/PackageVersion.cpp`, `NaturalCompare`).
- How SoftwareUpdater and the package kit fetch, install and restart; what a Settings page can reuse. How "Go back to before <date>" maps to the package states the boot loader reads (`package_support.cpp`).
- Where the per-change lines come from: a small file next to each build in the repository, made from `distro/WHATSNEW`. Propose its format.
- How to tell from R5 that an NTFS volume is hibernated (Fast Startup) or BitLocker-encrypted, before anything is written.
- The licence report for `ntfsresize` (the design doc has it; confirm the version you would take).

**Step 1, the R5 package repository on GitHub Pages.** A script that builds the R5 repository (`repo`, `repo.info`, `repo.sha256`, `packages/`) from a build's packages, the per-change file, and the last 3 builds; a GitHub Actions workflow that deploys it to GitHub Pages (never commit packages to git). The `baseurl` is the Pages address until the owner has a domain, then the custom domain. The installed image gets this repository's config.

**Step 2, the Software update page.** Settings, System, "Software update", as the mockup: Available and Installed (segmented control), changes in user language with icon and size, "Install and restart", "Go back to before <date>…", the options, the lock (administrator only), Revert and Defaults. A notification when updates are ready. Downloading in the background is on by default. Security fixes come on their own, marked red. Add the page icon in `module_icon()`.

**Step 3, the installer: erase the disk and the live stick.** Haiku's Installer with air/OS's whole-disk mode (UEFI, GPT, EFI and BFS, never the disk the system runs from), the read-only live session, and carrying language, keyboard and Wi-Fi (with the password of the joined network) to the installed system. The R5 Modern screens with the wave pebble (44 px, margin cropped).

**Step 4, the installer: next to Windows.** The checks (BitLocker off, enough free space, Windows shut down fully), then shrink NTFS with `ntfsresize` as a separate GPL-2.0-or-later program (source and notices as `docs/LICENSING.md` asks), R5 suggests 64 GB, Windows keeps at least what it uses plus 20 GB, the backup tick before Install works, the progress screen. With BitLocker on: never touch the disk.

**Step 5, the boot menu.** A dark text menu in R5 colours with the entries "R5 Operating System", "Windows", "R5 Operating System, previous state…", "Safe mode…" (the product name in full), and R5 Operating System after 5 seconds. Works without a graphics driver.

**Step 6, the download.** The anyboot ISO compressed on a GitHub Release (each file under 2 GiB), with its checksum and the version.

## Tests

- QEMU or VirtualBox first, on your own copy of the image. Next to Windows: a test disk with an NTFS partition (`mkntfs`), and with the hibernation flag set, before any real Windows.
- The owner's laptop comes last, only after the owner has a backup, and only on the owner's go. Never write a stick without the stick rules in `AGENTS.md`.
- Never claim a test that did not run. `CHANGELOG.md` per step; What's new only for what users get.
