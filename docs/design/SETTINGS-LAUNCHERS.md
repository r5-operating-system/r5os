# Settings launchers: inventory

Step 1 of the 25 Nov 2026 decision (option C, issue #4615): a preference
program that a Settings page fully covers becomes a small launcher with the
same name and signature. It opens Settings on that page and quits. This file
is the inventory only. Nothing is changed yet.

Sources compared: the program as built in the shared WSL tree
(`haiku/src/preferences/…`, with the R5 copies from `distro/`) and the pages
in `distro/settings/`. The comparison is by the controls and actions each
side offers (labels in the source), not by a run on R5.

## How Settings opens a page today

Settings can already open on a page from outside:

- command line: `Settings --page <id>`, also when Settings is already running
  (`SettingsApp::ArgvReceived`). Used today by NetworkStatus (`wifi`) and the
  Bluetooth page (`gamecontrollers`);
- message: `kMsgOpenPage` (`'r5sp'`) with the string `module`
  (`SettingsMessages.h`, private to Settings).

Page ids are the first column of `module_icon()` in `SettingsWindow.cpp`.

What a launcher still needs:

1. A launcher calls `r5_settings_open_page("<id>")` from `libr5ui.so`
   (added 25 Nov 2026, `docs/LIBR5.md`, "Open a Settings page") and quits.
   NetworkStatus and the Bluetooth view use the same call.
2. The id must be loaded. The Bluetooth page is an add-on that is missing in
   the minimum image; there the launcher would open Settings on Home. Settings
   should fall back to Home for an unknown id (to check), and a launcher for a
   page that is not in that image should not be installed there.
3. Settings itself needs nothing new: `--page <id>` and `kMsgOpenPage`
   already exist.
4. Messages sent to a running program by signature (see "Who starts it")
   reach nobody once the program is a launcher, because the launcher is never
   running. They are notifications only, so nothing breaks, but the matching
   Settings page does not get them either.

## Summary

| Program | Signature | Page id | Verdict |
|---|---|---|---|
| Deskbar (preferences) | `application/x-vnd.Haiku-DeskbarPreferences` | `deskbar` | Fully covered → launcher, plus a Deskbar change |
| Notifications | `application/x-vnd.Haiku-Notifications` | `notifications` | Fully covered → launcher |
| VirtualMemory | `application/x-vnd.Haiku-VirtualMemory` | `virtualmemory` | Fully covered → launcher |
| Appearance | `application/x-vnd.Haiku-Appearance` | `fonts` (or `look`) | Fully covered → launcher |
| Backgrounds | `application/x-vnd.Haiku-Backgrounds` | `wallpaper` | Fully covered → launcher |
| Bluetooth (R5) | `application/x-vnd.Haiku-BluetoothPrefs` | `bluetooth` | Fully covered → launcher (regular image only) |
| WiFi (R5) | `application/x-vnd.Haiku-WiFi` | `wifi` | Fully covered → launcher |
| Screen | `application/x-vnd.Haiku-Screen` | `display` | Not yet |
| Network | `application/x-vnd.Haiku-Network` | `connections` | Not yet |
| Users & Accounts (R5) | `application/x-vnd.R5OS-Users` | `accounts` | Not yet (one small gap) |
| Sounds, Keymap, Input, Printers | see below | `sound`, `keyboard`, `mouse`, `printers` | Not yet: the page is only an "Open" button |

Not a preference program, so no launcher: Theme (no separate program),
Information (About, `application/x-vnd.Haiku-About`; About R5 stays its own
program), Power (PowerStatus is a battery monitor and Deskbar item in
`/system/apps`, not a preference), Game controllers (Haiku's Joysticks is not
in the image; the icon is the Devices program, which stays).

Install path for all of them: `/boot/system/preferences/<Name>` (regular
image; the minimum image has Appearance, Backgrounds, Deskbar, Input, Keymap,
Network, Notifications, Screen, VirtualMemory, Users, Settings, and not
Bluetooth, Sounds or Printers).

## Per program

### Deskbar (preferences) → `deskbar`

`/boot/system/preferences/Deskbar`. The program is already a launcher: its
`main()` only sends `kConfigShow` to Deskbar (`application/x-vnd.Be-TSKB`),
which opens Deskbar's own preferences window.

| Setting or action | Page |
|---|---|
| Sort applications by name, Tracker always first | yes |
| Show application expander, Expand new applications | yes |
| Hide application names, Icon size (Small–Large) | yes |
| Recent applications / documents / folders | yes |
| Edit in Tracker (menu folder) | yes |
| Always on top, Auto-raise, Auto-hide | yes |
| Location (where the bar sits) | yes (screen picture) |
| Revert, Defaults | yes |

Who starts it: the Deskbar menu item "Deskbar preferences…" (`DeskbarMenu.cpp`,
`kConfigShow` handled in `BarApp.cpp`, `ShowPreferencesWindow()`), and the
Preferences menu by name. Verdict: fully covered. Two changes, not one: the
launcher (`DeskbarPreferences.cpp` opens Settings instead of sending
`kConfigShow`) and Deskbar's `kConfigShow` handler, which must open Settings
too, or the menu item still shows the old window.

### Notifications → `notifications`

`/boot/system/preferences/Notifications`.

| Setting or action | Page |
|---|---|
| Enable notifications | yes |
| Window width | yes |
| Duration (timeout) | yes |
| Position, Follow Deskbar | yes (screen picture) |
| Applications list, Add, Remove, Mute | yes |
| Test notification | yes (sample after a change) |
| Revert, Defaults | yes |

Who starts it: the Preferences menu by name. No code in R5 starts it by
signature. Verdict: fully covered → launcher.

### VirtualMemory → `virtualmemory`

`/boot/system/preferences/VirtualMemory`.

| Setting or action | Page |
|---|---|
| Enable virtual memory, confirm when turning off | yes |
| Automatic swap management, requested size | yes |
| Use volume (boot volume by default) | yes |
| Current swap, "takes effect on the next start" | yes |
| Revert, Defaults | yes |
| Alert when the settings file names a missing volume | no (small; the page shows the boot volume) |

Who starts it: the Preferences menu by name; the Information page uses only
its icon. Verdict: fully covered → launcher. The missing-volume alert is worth
adding to the page but does not block.

### Appearance → `fonts`

`/boot/system/preferences/Appearance`.

| Setting or action | Page |
|---|---|
| Fonts: plain, bold, fixed, menu | yes |
| Colors, automatic secondary colors | yes |
| Look and feel: decorator, ControlLook, scroll bar arrows, About | yes |
| Antialiasing: type, hinting, filter strength | yes |
| Revert, Defaults | yes |

The page has more (R5 Modern, window effects). Who starts it: the Preferences
menu by name. Verdict: fully covered → launcher. Which page the launcher opens
(`fonts` is the Appearance page; `look` and `theme` share the icon) is the
owner's choice; `fonts` matches the program's first tab.

### Backgrounds → `wallpaper`

`/boot/system/preferences/Backgrounds`.

| Setting or action | Page |
|---|---|
| Image, Default / Other folder, Placement (Manual, Center, Scale, Tile) | yes |
| X and Y for Manual | yes |
| Colour picker | yes |
| Icon label outline | yes |
| Current workspace / All workspaces | yes |
| Preview, Apply, Revert | yes (applied at once) |

Who starts it: the Preferences menu by name; Screen's "Set background" button
(`ScreenWindow.cpp`, `kBackgroundsSignature`). Messages by signature to a
running Backgrounds: Tracker's desktop after a colour drop (`DeskWindow.cpp`),
Backgrounds to Screen (`UPDATE_DESKTOP_COLOR_MSG`). Those only reach a running
program, so they become no-ops. Verdict: fully covered → launcher.

### Bluetooth (R5) → `bluetooth`

`/boot/system/preferences/Bluetooth`, `distro/bluetooth/app`. The program and
the page show the same `BluetoothView` from `libr5bt.so`.

| Setting or action | Page |
|---|---|
| My devices, Disconnect, Forget | yes |
| Devices nearby, Pair, Scan | yes |
| Bluetooth on/off (administrator) | yes |
| Test buttons… (opens Game controllers) | yes |

Who starts it: the Bluetooth Deskbar item (`BluetoothDeskbar.cpp`,
`be_roster->Launch(BLUETOOTH_APP_SIGNATURE)`). Verdict: fully covered →
launcher, regular image only (the page is an add-on that only that image has).

### WiFi (R5) → `wifi`

`/boot/system/preferences/WiFi`, `distro/wifi/app`. Program and page show the
same `WiFiView` from `libr5wifi.so`. Who starts it: the Preferences menu by
name; NetworkStatus already opens `Settings --page wifi`. Verdict: fully
covered → launcher.

### Screen → `display`: not yet

`/boot/system/preferences/Screen`, R5 copy in `distro/rest/src/preferences/screen`.

| Setting or action | Page |
|---|---|
| Resolution, Colors, Refresh rate, Apply, keep or undo after 12 s | yes |
| Current workspace / All workspaces | yes |
| Workspace columns and rows | yes |
| Several screens (arrangement) | yes (more than the program) |
| Custom refresh rate ("Other…") | no |
| Brightness | partly (on the Power page) |
| Combine displays, Swap displays, Use laptop panel, Video format (TV out) | no (driver-specific options) |
| Display info: vendor, name, serial, dpi, frequency ranges | partly (name only) |
| Set background (opens Backgrounds) | no |
| VESA mode file when the driver is VESA | no |

Who starts it: the Preferences menu by name; Device Manager
(`DeviceModel.cpp`) for display devices; Haiku's Workspaces app
(`Workspaces.cpp`); Backgrounds sends it a colour update. Missing: custom
refresh rate, the driver options (combine, swap, laptop panel, TV), monitor
details, a link to Wallpaper, VESA settings, brightness on this page or a
pointer to Power.

### Network → `connections`: not yet

`/boot/system/preferences/Network`, R5 copy in `distro/rest/src/preferences/network`.
Settings splits it over Connections, Wi-Fi and Discovery.

| Setting or action | Page |
|---|---|
| Interfaces, on/off, status, link speed, hardware | yes |
| IPv4 and IPv6: automatic, static, off | yes |
| DNS servers and search domain | yes |
| Show network status in Deskbar | yes |
| Wi-Fi networks | yes (Wi-Fi page) |
| Hostname | yes (Discovery, Information) |
| Services: FTP, SSH, Telnet | yes (Discovery) |
| Sent / received counters, MAC address, Renegotiate | partly (no counters, no Renegotiate) |
| Profiles | no |
| VPN | no |
| Dial-up (PPP) | no |

Who starts it: NetworkStatus (`NetworkStatusView.cpp`, "Open network
preferences"), the Wi-Fi view (`WiFiView.cpp`), Device Manager. Missing:
profiles, VPN, dial-up, traffic counters, Renegotiate.

### Users & Accounts (R5) → `accounts`: not yet

`/boot/system/preferences/Users`, `distro/users`.

| Setting or action | Page |
|---|---|
| Account list, Add, Delete (keep or delete files), Disable/Enable | yes |
| Name, Email, Account type, Change password | yes |
| Choose picture | yes |
| Remove picture | no |
| Guest sign-in, Shared folder | yes (page only) |

Who starts it: the Preferences menu by name. Missing: Remove picture. After
that it can become a launcher.

### Sounds, Keymap, Input, Printers: not yet

| Program | Signature | Page id |
|---|---|---|
| Sounds | `application/x-vnd.Haiku-Sounds` | `sound` |
| Keymap | `application/x-vnd.Haiku-Keymap` | `keyboard` |
| Input (mouse, keyboard repeat, touchpad) | `application/x-vnd.Haiku-Input` | `mouse` |
| Printers | `application/x-vnd.Be-PRNT` | `printers` |

These pages are `LegacyPages.cpp`: a line of text and an "Open" button that
starts the program. They have none of the program's settings. A launcher here
would loop (program opens Settings, Settings opens the program). Each needs a
real page first. Media (`application/x-vnd.Haiku-Media`) has no page.

## Programs without any page

DataTranslations, E-mail, FileTypes, Locale, Media, Repositories,
ScreenSaver, Shortcuts, Time, Tracker preferences: no Settings page, so they
stay as they are. Out of scope for this step.
