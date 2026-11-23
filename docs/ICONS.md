# Icons

The Wave Pebble, the Deskbar button, the boot splash, and the boot-volume badge are the product mark. Their files, colors, and the generator are in [BRAND-GUIDE.md](BRAND-GUIDE.md). This page is the icon library, not that mark.

Public helpers for a bitmap or a button icon. They live in the shared library `libr5icons.so`. The screen picture is a different library, `libr5ui.so`, described in `docs/LIBR5.md`.

## For developers

The source is `distro/icons`. `scripts/apply-icons.py` copies it to `src/preferences/r5icons`, copies the header to `headers/os/interface/R5Icons.h`, and adds `libr5icons.so` to `HaikuImageGetSystemLibs` for the regular image and the minimum image. `scripts/apply-settings.py` runs that same step, because Settings links the library.

Include `<R5Icons.h>`. In a Jamfile, link the library by its file name:

```
Application Example :
	Example.cpp
	: be libr5icons.so [ TargetLibstdc++ ]
	;
```

`SubInclude` for `src preferences r5icons` has to come before the program that links it. `apply-icons.py` puts that include in `src/preferences/Jamfile`.

On a running system the file is `/system/lib/libr5icons.so`. The soname is `libr5icons.so`, with no version suffix, the same way `libbe.so` is named. The regular image and the minimum image both install it. A program that is linked against it will not start if that file is missing.

The C++ is MIT, so any application can link this library. Code added to it must be MIT-compatible. The four navigation pictures are unmodified Tracker HVIF and stay under the Open Tracker license (`distro/icons/TRACKER_ICON_NOTICE.txt`). The open and closed locks are unmodified Haiku artwork, `Action_Login` and `Action_Logout`, under the MIT license (`distro/icons/HAIKU_ICON_NOTICE.txt`). Do not put a license header on `standard_icons.inc` or `lock_icons.inc`. Do not move these calls into `libbe`. `libbe` stays MIT. A program that does not link `libr5icons.so` is unchanged.

Adding a function, or a new `r5_icon` value at the end of the enum, keeps old programs working. Changing a function’s arguments, removing a function, or renumbering an icon breaks programs already built against this soname. Do that only with a written note in this file and in `CHANGELOG.md`.

The devel package lists every library from `HaikuImageGetSystemLibs` and every header under `headers/os`. A build of `haiku_devel.hpkg` therefore picks up `libr5icons.so` and `R5Icons.h` without a separate file list.

These calls do not open Tracker, and they do not read `libtracker.so`. Tracker’s resource ids are not a public interface. A program that needs an arrow or a home picture calls the function below and passes one of the names.

None of these pictures gets the preferences wrench. That overlay belongs to the Settings application and its own page icons and home cards. About R5 does not use this library.

## Predefined icons

| Name | Value | Picture |
|---|---|---|
| `R5_ICON_BACK` | 1 | Tracker’s back arrow (`R_ResBackNav`) |
| `R5_ICON_FORWARD` | 2 | Tracker’s forward arrow (`R_ResForwNav`) |
| `R5_ICON_UP` | 3 | Tracker’s up arrow (`R_ResUpNav`) |
| `R5_ICON_HOME` | 4 | The house Tracker draws on the home folder (`R_HomeDirIcon`) |
| `R5_ICON_UNLOCKED` | 5 | Haiku's open lock (`Action_Login`) |
| `R5_ICON_LOCKED` | 6 | Haiku's closed lock (`Action_Logout`) |

The numbers are part of the contract. Do not change a number that already shipped. A new predefined icon is added at the end of `enum r5_icon`, with the next number, the unmodified picture, and a row in this table.

The bytes for 1–4 are unmodified HVIF from `src/kits/tracker/TrackerIcons.rdef`, stored in `distro/icons/standard_icons.inc`. The Open Tracker notice is `distro/icons/TRACKER_ICON_NOTICE.txt`. The lock bytes are unmodified HVIF in `distro/icons/lock_icons.inc`. The MIT notice is `distro/icons/HAIKU_ICON_NOTICE.txt`. Do not put a license header on either byte file. The C++ around the bytes is MIT.

```cpp
BBitmap* icon = r5_icon_bitmap(R5_ICON_BACK, 16);
if (icon != NULL) {
	// draw icon, then delete it
	delete icon;
}
```

`r5_icon_bitmap()` returns a new `B_RGBA32` bitmap, or `NULL` when the name is unknown or the side is below 1. The caller deletes the bitmap. `side` is the edge in pixels.

## Any other icon

Three calls take an icon the program already knows. Each returns a new bitmap of the requested side, or `NULL`. The caller deletes it.

- `r5_vector_icon_bitmap(data, size, side)` draws HVIF bytes. This is any vector icon the program loaded itself.
- `r5_file_icon_bitmap(path, side)` draws the icon of a file or folder through `BNodeInfo`. This is the public node API.
- `r5_mime_icon_bitmap(type, side)` draws the icon of a MIME type through `BMimeType`. Example: `"application/x-vnd.Haiku-About"`.

A file or type that has no icon returns `NULL`. The bitmap is not given the preferences wrench.

## Buttons

A text button such as Revert has a height that comes from the font. An icon button next to it has to use that same height or the row looks uneven.

Pass that height. The call makes the button a square of that height and draws the icon inside, one pixel clear of the border. It does not change the label. Leave the label empty when the button should show only the picture. `SetToolTip()` can still say “Back” or “Home”.

```cpp
float height = revert->PreferredSize().height;
r5_button_set_icon(back, R5_ICON_BACK, height);
r5_button_set_icon(home, R5_ICON_HOME, height);
r5_button_set_lock(lock, true, height);
```

`r5_button_set_lock(button, locked, height)` draws `R5_ICON_LOCKED` when `locked` is true and `R5_ICON_UNLOCKED` when it is false. It does not change the label and it does not open a password window. The caller decides what a click does. Settings uses it for the lock on the bottom row, before Revert.

The same height is accepted by:

- `r5_button_set_vector(button, data, size, height)`
- `r5_button_set_file_icon(button, path, height)`
- `r5_button_set_mime_icon(button, type, height)`

Each returns `B_OK` when the icon was set. The button is still resized to the square when the pointer is valid, including when the picture itself could not be loaded, so the row does not jump. A `NULL` button or a height below 1 returns `B_BAD_VALUE` and does not resize.

Settings links `libr5icons.so` and calls `r5_button_set_lock()` for the lock, after the plain font is applied, so the height matches Revert and Defaults. The lock, Revert, and Defaults do not get the preferences wrench.

## Line glyphs

Haiku's icon set has no power symbol, no circular arrow, no clock face for a small label, and no eye. For those, `libr5icons.so` draws a line glyph in code. A glyph has one color, given by the caller, so it follows the text color and dark mode. It is drawn on a 24 by 24 grid with round line ends. A ring is drawn as one line through the same coordinates as a straight line, so the stem of the power symbol sits exactly in the middle of its ring. These are small symbols beside text, not icons in the Haiku style; a full icon still comes from Haiku's HVIF set.

| Glyph | Number | Picture |
|---|---|---|
| `R5_GLYPH_POWER` | 1 | A ring open at the top, with a short stem |
| `R5_GLYPH_RESTART` | 2 | A ring open at the right, with an arrow head at the top |
| `R5_GLYPH_CLOCK` | 3 | A clock face with two hands |
| `R5_GLYPH_EYE` | 4 | An open eye (show the password) |
| `R5_GLYPH_EYE_OFF` | 5 | The eye with a line through it (hide the password) |
| `R5_GLYPH_LOG_OUT` | 6 | An open door frame with an arrow out to the right |
| `R5_GLYPH_BLUETOOTH` | 7 | The Bluetooth rune: a stem with two arrow heads crossing it |
| `R5_GLYPH_CROSS` | 8 | A cross of two diagonal lines (the lower face button of a PlayStation-style game controller) |
| `R5_GLYPH_CIRCLE` | 9 | A ring (the right face button) |
| `R5_GLYPH_SQUARE` | 10 | A square outline (the left face button) |
| `R5_GLYPH_TRIANGLE` | 11 | A triangle outline, point up (the upper face button) |
| `R5_GLYPH_WIFI` | 12 | The Wi-Fi symbol: a dot and three arcs (a tray symbol, below) |
| `R5_GLYPH_VOLUME` | 13 | A filled speaker with one sound wave |
| `R5_GLYPH_VOLUME_OFF` | 14 | The speaker with a cross where the wave was (muted) |
| `R5_GLYPH_NETWORK` | 15 | Three boxes on a wire (a wired network) |
| `R5_GLYPH_NETWORK_OFF` | 16 | The same with a slash through it (no connection) |
| `R5_GLYPH_AIRPLAY` | 17 | A screen open at the bottom, with a triangle under it |
| `R5_GLYPH_MAIL` | 18 | An envelope |
| `R5_GLYPH_MAIL_NEW` | 19 | A smaller envelope with a filled dot at its corner (new mail) |

The numbers are part of the contract, the same as `r5_icon`. A new glyph is added at the end of `enum r5_glyph`, with a row in this table.

- `r5_glyph_draw(view, which, frame, color)` draws into a view. The view's state is the same afterwards.
- `r5_glyph_bitmap(which, side, color)` returns a new `B_RGBA32` bitmap with the glyph on a transparent background. The caller deletes it.
- `r5_button_set_glyph(button, which)` puts the glyph before the label, in `B_CONTROL_TEXT_COLOR`, at the size of the button's font. Unlike `r5_button_set_icon()`, it does not make the button square. Call it again after `B_COLORS_UPDATED`.

```cpp
restart->SetFont(&font);
r5_button_set_glyph(restart, R5_GLYPH_RESTART);
```

## Level glyphs

A level glyph shows how full something is: a signal strength or a battery charge. Wi-Fi and Bluetooth draw it with the same call, so the two look alike. It has four steps. The filled steps are in the foreground color; the empty ones are a faint tint of it over the background, so it follows the text color and dark mode.

| Glyph | Number | Picture |
|---|---|---|
| `R5_LEVEL_SIGNAL` | 1 | Four bars of rising height, left to right |
| `R5_LEVEL_BATTERY` | 2 | A battery body with a cap on the right and four segments inside |
| `R5_LEVEL_WIFI` | 3 | The Wi-Fi symbol, a line symbol: the dot and the three arcs are the four steps (a tray symbol, below) |
| `R5_LEVEL_BATTERY_SYMBOL` | 4 | A round battery with a cap line and a fill that grows in four steps, a line symbol (a tray symbol, below) |

| Style | Number | Look |
|---|---|---|
| `R5_LEVEL_STYLE_NORMAL` | 0 | Filled steps in the foreground color |
| `R5_LEVEL_STYLE_DISABLED` | 1 | Greyed, for example while disconnected |
| `R5_LEVEL_STYLE_OFF` | 2 | Outlines only |

The numbers are part of the contract, the same as `r5_glyph`. A new level glyph or style is added at the end, with a row here.

- `r5_level_glyph_draw(view, which, frame, level, foreground, background, style)` draws into a view, with `level` of the four steps filled; a level below 0 or above 4 is clamped. The view's state is the same afterwards.

## Tray symbols

Added 22 Nov 2026 for the Deskbar style Default (R5) (`docs/design/DESKBAR-STYLES.md`, step 5). In Default (R5), R5's own tray items draw a plain line symbol in one colour, the Deskbar text colour (`#1d2a40` on the light glass), with 2 px lines on the 24 grid (a pen of 2 units, the stroke of the mockup), at the size of the item and on the baseline of the clock. No colours of their own: no green, no red, no coloured fills, no bevels. A state shows in the shape: fewer arcs, an empty battery, a cross through the speaker, a slash through the network, a dot on the envelope. Only a warning may use a colour: the battery under 15 % that is not charging is drawn in `B_FAILURE_COLOR`. In Classic (Haiku) every tray item draws its own icon as before. Application icons are never changed: they stay the real Haiku icons.

How an item knows the style: Deskbar sets it in its own process with `r5_set_deskbar_tray_style()` (`<R5UI.h>`, libr5ui), and sends `R5_DESKBAR_TRAY_STYLE_CHANGED` (`'r5ts'`, with int32 `"style"` and the colour `"color"`) to every tray view when it changes. A tray item is a replicant inside Deskbar, so it reads the same value with `r5_deskbar_tray_style()` and `r5_deskbar_tray_color()` when it draws; outside Deskbar (in its own program) the style is always classic. A view that does not know the message ignores it, as any `BView` does, so `BDeskbar` and `instantiate_deskbar_item` are unchanged and other programs' items keep drawing themselves.

| Item | Symbol in Default (R5) |
|---|---|
| Network, wired | `R5_GLYPH_NETWORK`, `R5_GLYPH_NETWORK_OFF` when it is down |
| Network, Wi-Fi | `R5_LEVEL_WIFI` with the lit arcs (1 while connecting, 0 when off) |
| Volume (desklink) | `R5_GLYPH_VOLUME`, `R5_GLYPH_VOLUME_OFF` when muted |
| Battery (PowerStatus) | `R5_LEVEL_BATTERY_SYMBOL`, the fill in four steps; the label in the tray colour |
| Bluetooth | `R5_GLYPH_BLUETOOTH` in the tray colour, faint when off |
| AirPlay | `R5_GLYPH_AIRPLAY` |
| Mail | `R5_GLYPH_MAIL`, `R5_GLYPH_MAIL_NEW` with new mail |
| ProcessController | its bars, in the tray colour, the empty part a faint tint of it |

```cpp
void
MyTrayView::Draw(BRect)
{
	if (r5_deskbar_tray_style() == R5_TRAY_STYLE_SYMBOLS) {
		r5_glyph_draw(this, R5_GLYPH_MAIL, Bounds(),
			r5_deskbar_tray_color());
		return;
	}
	// the item's own icon
}

void
MyTrayView::MessageReceived(BMessage* message)
{
	if (message->what == R5_DESKBAR_TRAY_STYLE_CHANGED) {
		Invalidate();
		return;
	}
	BView::MessageReceived(message);
}
```

Such an item links `libr5ui.so` and `libr5icons.so`. A program for Haiku that does not do this keeps its own icon in every style.

`DrawWiFiSignalBars()` in `<R5WiFiGlyphs.h>` calls it with `R5_LEVEL_SIGNAL`; its bars are the same pixels as before the move. `libr5wifi.so` and NetworkStatus link `libr5icons.so` for it.

```cpp
r5_level_glyph_draw(this, R5_LEVEL_BATTERY, frame, 3,
	ui_color(B_LIST_ITEM_TEXT_COLOR), ui_color(B_LIST_BACKGROUND_COLOR),
	R5_LEVEL_STYLE_NORMAL);
```

The Settings application icon is the R5 wave pebble from About R5, with the preferences wrench (the last two shapes of Tracker's preferences-folder icon) drawn on it. `scripts/join-settings-icon.py` builds that HVIF into `Settings.rdef`. The Home row in the list uses the pebble alone. The wrench on a page icon is still drawn by Settings when the page is shown.
