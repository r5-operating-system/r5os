# R5Book

The developer guide to what R5 Operating System adds to Haiku for programs: LibR5, the MIDI 2, Wi-Fi, Bluetooth, Admin, and Game kits, Vulkan in a window, Settings modules, and additions to Haiku kits, in the layout of the Be Book and the look of the R5 website.

Open `index.html` directly in a browser. Everything is static HTML, CSS (`r5book.css`), and vanilla JavaScript (`r5book.js`) — no build step, no framework, and no external service.

## Pages

### Start here
| Page | Covers | Source of truth |
|---|---|---|
| `index.html` | What LibR5 is, kit map, linking table, rules | `docs/LIBR5.md`, `AGENTS.md` |

### Kits (alphabetical)
| Page | Covers | Source of truth |
|---|---|---|
| `admin-kit.html` | `libr5admin.so`: verified-sender admin requests (`r5_admin_send`) | `distro/admin/R5Admin.h`, `docs/LIBR5.md` |
| `appearance-kit.html` | `libr5ui.so`: overview, linking, and redirect map for `#B*` anchors | `distro/ui/*.h`, `docs/LIBR5.md`, `docs/APPEARANCE-KIT.md` |
| `appearance-cards.html` | `libr5ui.so`: `BActionCard`, `BCard`, `BCardStrip`, `BCardStripItem` | `distro/ui/*.h`, `docs/APPEARANCE-KIT.md` |
| `appearance-controls.html` | `libr5ui.so`: `BAccentButton`, `BAxisBar`, `BIconView`, `BPasswordControl`, `BScreenArrangement`, `BTiltLevel` | `distro/ui/*.h`, `docs/APPEARANCE-KIT.md` |
| `appearance-settings.html` | `libr5ui.so`: `BAdminLockBar`, `BPageHeader`, `BSidebar`, `BSidebarItem`, `BUserCard`, `BUserListItem` | `distro/ui/*.h`, `docs/APPEARANCE-KIT.md` |
| `appearance-effects.html` | `libr5ui.so`: `r5_screen_draw()`, `r5_avatar_draw()`, and `WindowEffects.h` | `distro/ui/R5UI.h`, `distro/ui/WindowEffects.h` |
| `bluetooth-kit.html` | `libr5bt.so`: `BluetoothView`, `BluetoothController`, `BluetoothState`, switch, `RemoteDevice`, HID, LE, audio | `distro/bluetooth/lib/R5Bluetooth.h`, `docs/LIBR5.md`, `docs/BLUETOOTH.md` |
| `game-kit.html` | `libr5game.so`: `BGameController`, `BGameControllerView`, `PictureBitmap()`, rumble, triggers, motion | `distro/game/lib/*.h`, `docs/LIBR5.md`, `docs/GAME-CONTROLLERS.md` |
| `icons-kit.html` | `libr5icons.so`: predefined icons, icon bitmaps, icon buttons, line glyphs | `distro/icons/R5Icons.h`, `docs/ICONS.md` |
| `midi2-kit.html` | `libmidi2.so`: MIDI 2.0 UMP, Property Exchange, Profiles, discovery | `distro/media/headers/os/midi2`, `docs/MIDI2.md` |
| `power-kit.html` | `libr5power.so`: `BPowerRoster`, the watch messages, brightness through `BScreen`, no battery, Safe mode | `distro/power/lib/PowerRoster.h`, `docs/LIBR5.md`, `docs/POWER.md` |
| `wifi-kit.html` | `libr5wifi.so`: `WiFiView`, `WiFiController`, `WiFiState`, glyphs, `BNetworkRoster`, link details | `distro/wifi/lib/*.h`, `docs/LIBR5.md` |

### Guides (reading order)
| Page | Covers | Source of truth |
|---|---|---|
| `settings-modules.html` | Settings add-ons: `SettingsModule`, `SettingsContext`, entry points; `r5_settings_open_page()` | `distro/settings/SettingsModule.h`, `distro/settings/ModuleAPI.md`, `distro/ui/R5UI.h` |
| `accounts.html` | Accounts, sessions, lock and log out; Sign in example with screenshots in `img/` | `docs/MULTIUSER.md`, `distro/login` |
| `vulkan.html` | Vulkan in a `BView`: `VK_RFIVE_view_surface` | `distro/vulkan/include/vulkan/vulkan_r5.h`, `docs/VULKAN-WSI.md` |
| `guidelines.html` | How an R5 window is put together | `docs/STYLE.md`, `docs/SETTINGS.md`, `AGENTS.md` |

### Reference
| Page | Covers | Source of truth |
|---|---|---|
| `haiku-additions.html` | Public calls R5 added to Haiku classes and modules, plus decorator hooks and the EFI module for drivers | `SPDX-Snippet` parts of public headers in `distro/`; `distro/kernel/headers/os/drivers/EFI.h` |
| `debug-flags.html` | Every debug, trace, test and tuning switch R5 reads from settings files and environment variables, with defaults | `get_driver_*` and line reads under `distro/` (radeon_hd, kernel, efi, cs35l41, mt7922wifi, h2generic, usb_audio, WiFi_debug) |
| `class-index.html` | Alphabetical index of every class, function, struct, and enum in R5Book | Generated from `R5_SEARCH_INDEX` in `r5book.js` |

## Pictures

`img/` holds the screenshots from R5 (Sign in and the lock screen, QEMU, 11 Nov 2026; the Wi-Fi page). A screenshot carries the label "Screenshot". The rest of this edition draws each control as an HTML mockup with the R5 system colors at the 12 point system font. Each picture carries a label: "Mockup", or "Stand-ins" where a Haiku HVIF icon cannot be drawn in HTML yet. The glyph pictures use the same geometry as `glyph_paint()` in `R5Icons.cpp`.

## How to add or update an entry

A change that adds or changes a public call in `libr5admin.so`, `libr5ui.so`, `libr5bt.so`, `libr5game.so`, `libr5icons.so`, `libr5power.so`, `libmidi2.so`, `libr5wifi.so`, `vulkan_r5.h`, `SettingsModule.h`, or a public Haiku header in `distro/` updates the matching R5Book page in the same change.

### 1. Choose where it goes
- **Existing kit or guide:** add the entry to that kit's page (alphabetical on kit pages, reading order on guides). For `libr5ui.so` (Appearance Kit), place it in the matching subpage (`appearance-cards.html`, `appearance-controls.html`, `appearance-settings.html`, or `appearance-effects.html`).
- **New kit:** create `<kit-name>.html`, add it alphabetically under **Kits** in `r5book.js` and `index.html`, and add its library row to `#linking` in `index.html`.
- **New guide:** create `<guide-name>.html` and add it in reading order under **Guides** in `r5book.js` and `index.html`.

### 2. Follow the entry checklist
Every page and entry must include:
- **MIT SPDX header** at the top of any new HTML file (`SPDX-FileCopyrightText: 2026 R5OS Contributors`, `SPDX-License-Identifier: MIT`). Never copy text from the Be Book or the Haiku Book; write original prose.
- **Breadcrumb** above `.page-hero` (`R5Book › Kits/Guides/Reference › Page`).
- **"What is in this page"** (`<nav class="on-this-page">`) at the top of the page, updated with a one-line summary and `#anchor` link for the new entry.
- **Entry header** (`<section class="entry" id="...">` with `<div class="entry-header"><h2>...</h2><span class="kind">class|function|struct</span></div>`). Keep existing `id=` anchors unchanged so external links never break.
- **Facts table** (`<table class="facts">`) with `Declared in`, `Library`, `Derived from` (for classes), and `First user`.
- **Plain language:** state what the class or call is for before how it works. Use short sentences and one idea per paragraph.
- **Use it for / Not for** (`.use-grid`) when a class could be confused with another control.
- **Picture** (`<figure class="stage">`) for any visual control, labeled `Screenshot` or `Mockup`.
- **Member functions** (`.member` with `.sig` and short description) and a **short code example** (`<pre class="code">`).
- **See also** (`.see-also`) linking to related classes, guides, or headers.
- **No test reports.** Say what a call does and what it does not do yet (a "Limits" note). How or where it was tested belongs in `CHANGELOG.md`, not in the book.

### 3. Update navigation and search
- Add the new `#anchor` to `groups` in `r5book.js` so it appears in the left sidebar.
- Add the class, function, or struct to `window.R5_SEARCH_INDEX` in `r5book.js`. Both the top-bar search box and `class-index.html` read from this array automatically.
- Update the **Pages** table in this `README.md` if you added a new HTML file.

### 4. Verify before committing
- Open `docs/r5book/index.html` directly from the file system (`file://`) in a browser.
- Check both **light mode** and **dark mode**, and both a **wide** and a **narrow** window (`<= 900px`).
- Confirm every internal `href` and `#anchor` resolves to an existing file and `id=`, and that no header/code was modified in a docs-only change.
