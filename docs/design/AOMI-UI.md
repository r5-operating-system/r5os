<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# Aomi window: layout options

Design only, 25 Nov 2026. Nothing is built.

**Owner's choice (25 Nov 2026): option B, Compact.** Options A and C stay below as the record of what was weighed. No brief or code before the owner's explicit go.

Aomi is the R5 browser (`docs/AOMI.md`). Its engine changes: Servo first, Gecko as the fallback (`docs/design/LADYBIRD-PORT.md`, PR #4657). This document designs the window apart from the engine, so the window survives an engine change. The mockups are in `aomi-mockup/`.

## What every option keeps

- **The R5 frame.** The yellow Haiku tab (`#ffd42e`, dark `#e8b923`) with the close box and zoom box, and the 1 px window line. The frame comes from the decorator, not from Aomi. No brand colour in the frame.
- **Glass with the fallback.** `BAppearanceKit::set_window_glass(this, true)` on the window. The toolbar area and the tab strip draw with `B_OP_ALPHA`, so the blur shows through in the GPU mode with frosted glass on. In the CPU mode, Safe mode, or with glass off, the same window is the flat theme panel, with no blur and no shadow. The web page itself is always opaque: the engine view never becomes glass. Light and dark come from the theme (`ui_color()`, `docs/design/THEMING.md`); Aomi has no colours of its own in the frame.
- **Brand colours** (sky blue `#3898ff`, white, deep navy `#0f2a4a`; the navy is a stand-in until the brand kit names it) only in the app icon, the new tab page and the promo. The slogan stays off the window.
- **Haiku habits.** Every command is a `BMenuItem` with its Command shortcut from WebPositive (`docs/AOMI.md`, "Wat mee gaat"): N, T, W, Shift-W, F, R, U, + − 0, Command-1 to 9, F5, F11. Bookmarks stay files in `WebPositive/Bookmarks`, so Tracker can sort, rename and drag them. Downloads land in the folder from settings, Desktop by default.
- **The settings form.** Its own small window, "Aomi settings", from the menu (Command-comma). Not a page in the Settings application: that list is fixed (`AGENTS.md`). The form follows `docs/STYLE.md`: panel colour, right-aligned labels, one line per field. Fields: Start with (New tab page, Home page, Last session), Home page, Search engine (menu plus the search address), Show home button, Show bookmarks bar, Zoom text only, Save downloads to (with Choose…), Ask where to save each file, and Private windows (forget everything when closed, always on, shown as a note). A change applies at once; Revert and Defaults at the bottom, as in Settings.
- **Private window.** A second window with its own session (no history, no cookies or cache on disk, downloads stay). The frame is unchanged; the tab title says "Private", the toolbar shows a "Private" label with the mask icon, and the new tab page is navy. Command-Shift-N.
- **New tab page.** Aomi's start page in the brand colours: the Aomi mark, one search field, the tiles of the `Bookmark bar` folder, and the last closed tabs. It is a local page drawn by the engine view (`aomi:newtab`), so it looks the same on every engine.

## Option A: Classic (WebPositive, tidied)

Menu bar (Window, Edit, View, History, Bookmarks, Help), the tab strip under it, one navigation row (back, forward, reload or stop, home, address field, Go), the bookmarks bar, the page, and a status line with the progress bar at the bottom. Downloads are their own window (the WebPositive `DownloadWindow`), Command-D.

- **Gives:** what Haiku users know from WebPositive and every other Haiku app: a real menu bar, everything visible, a separate Downloads window that Tracker users expect. Least new code: the tab files, `DownloadWindow` and the history are MIT copies.
- **Costs:** about 130 px of chrome before the page starts, the most of the three. On the 5625U at 1920×1080 that is fine; on a 1280×800 screen the page is short. The address and the search are one field, but it looks like WebPositive, not like a new browser.
- **Haiku and R5:** the closest to BeOS habits. The R5 look is only the glass and the R5 Modern controls.

## Option B: Compact (one toolbar, one menu button)

The tab strip on top, then one row: back, forward, reload or stop, the address and search field in the middle (lock, address, bookmark star inside the field), then the downloads button and one menu button. The menu button opens one `BPopUpMenu` with the same menus as option A as submenus (New tab, New window, New private window, History ▸, Bookmarks ▸, Zoom, Find, Print, Settings…, About Aomi). The bookmarks bar is off by default (View or the menu turns it on). The status line is gone: a hovered link and the loading progress show in a small label at the bottom left of the page and as a thin bar under the toolbar. Downloads open as a panel under the downloads button, with "Show all downloads" opening the same Downloads window as A. For people who want it, "Show menu bar" in the menu brings the option A menu bar back.

- **Gives:** the most room for the page, about 76 px of chrome. Looks like a current browser, so people coming from Windows or a Mac find everything. Still one place for every command, all with the same shortcuts.
- **Costs:** two pieces R5 does not have yet: a panel anchored to a button (for downloads) and a suggestion list under the address field. Both are small, and the second is needed in every option. The menu button is a step away from the Haiku menu bar, softened by "Show menu bar".
- **Haiku and R5:** BeOS-style underneath (real `BMenu`s, shortcuts, files), modern on the outside. It matches the floating Deskbar and R5 Modern controls best.

## Option C: Side tabs (a list like Settings)

The menu bar stays, then one navigation row like B. The tabs are a list on the left, 200 wide like the Settings list, with the page title, its icon and a close box per row, and "New tab" at the top. Under the tabs, a fold for Bookmarks and one for Downloads, the same folding as the Settings list. The list can shrink to icons only.

- **Gives:** many tabs stay readable; on a wide screen the page keeps its full height. It reuses the shape people already know from the R5 Settings window.
- **Costs:** 200 px of width for the page; a web page laid out for 1280 gets 1050. Side tabs are uncommon outside a few browsers, so new people need a moment. `BSidebar` folds categories but has no drag to reorder and no close box per row; a new view or an extension of `BSidebarItem` is needed.
- **Haiku and R5:** feels like R5 (Settings), keeps the menu bar. Farthest from WebPositive.

## Recommendation: option B, Compact

In plain words: a browser is a window around someone else's page, so the page should get the room. Option B gives the page the most space, looks like the browsers people know, and still keeps the Haiku parts that matter: real menus with the same shortcuts, bookmarks as Tracker files, a Downloads window, and the menu bar back with one click for people who want it. It needs two small new pieces (a panel under a button and a suggestion list), and the suggestion list is needed in every option anyway. Option A is the safe choice if the owner values the Haiku menu bar over page room; option C is best kept as a later choice ("Tabs on the side") once B works.

## Libraries: what fits and what is missing

Fits as it is:

- `set_window_glass()`, `window_effects_active()`, `B_WINDOW_EFFECTS_CHANGED` (`docs/LIBR5.md`, "Window effects") for the glass and the fallback.
- R5 Modern controls through `be_control_look` and `ui_color()`, so light, dark and every theme follow without Aomi code.
- `r5_icon_bitmap()` with `R5_ICON_BACK`, `R5_ICON_FORWARD`, `R5_ICON_HOME`; `r5_vector_icon_bitmap()` for other Haiku vector icons; `r5_file_icon_bitmap()` for bookmark and download files.
- `BAccentButton` for the one main action (for example "Open" in a dialog), `BPasswordControl` for the HTTP sign-in window, `BStatusBar` for progress, `BActionCard` for the tiles on the new tab page (the second user after Settings home).
- Haiku: `BMenuBar`, `BPopUpMenu`, `BTextControl` with `MarkAsInvalid()`, `BFilePanel`, `BAlert`, `BNotification` for a finished download.
- WebPositive MIT files, copied with their headers: `TabManager`, `TabView`, `TabContainerView`, `DownloadWindow`, `DownloadProgressView`, `BrowsingHistory`, `CredentialsStorage`.

Missing (named, not designed here):

- **Icons** for reload, stop, bookmark star, downloads, the menu button and the private mask. Not new numbers in `R5_ICON_*` (`docs/AOMI.md`): existing Haiku vector icons through `r5_vector_icon_bitmap()`, chosen with the owner. The mockups use stand-ins.
- **A panel under a button** (option B: downloads; also a site-information panel behind the lock). A borderless floating `BWindow` that closes on a click outside. If a second program needs it (Deskbar, Appsolutely), it belongs in `libr5ui`; until then it stays in Aomi.
- **Suggestions under the address field** (all options): history, bookmarks and the search suggestion in a list under the field. Stays in Aomi.
- **A side tab list** (option C only), see above.

## The engine view

The window talks to the engine through one view, `BWebContentView`, and nothing else. A WebKit, Servo or Gecko view implements it (`BServoView`, `BGeckoView`, a wrapper around `BWebView`). The header lives in `distro/apps/aomi`, MIT, not in `libbe` and not in `libr5ui`, until a second program (a help viewer, Appsolutely) needs a web view; then it moves to a library with a fixed ABI.

The view is a `BView` and draws itself, on the GPU where the engine can (`docs/design/LADYBIRD-PORT.md`, "GPU from the first step"); the window does not know how. It reports to a `BMessenger` the window hands it, in `BMessage`s with fixed codes, so the window code has no engine types in it.

| Area | The window asks | The view reports |
|---|---|---|
| Load | `LoadURL(url)`, `LoadHTML(html, baseURL)` for local pages such as `aomi:newtab`, `Reload(bypassCache)`, `StopLoading()` | load started, committed (the new address), finished, failed (code and text) |
| Navigation | `GoBack()`, `GoForward()`, the back and forward list for the long-press menu | can go back, can go forward, is loading (three bools, as WebPositive's `NavigationCapabilitiesChanged`) |
| Page | `URL()`, `Title()`, `SetZoom(factor, textOnly)`, `Find(text, forward, matchCase)`, `SavePage(path)`, `ViewSource()`, `Print()`, `Protocol(protocol)` | title changed, address changed, favicon (a `BBitmap`), hovered link, security state (secure, not secure, certificate error with details), the protocol of the main document (`"h2"`, `"http/1.1"`, `"h3"`, or `B_NOT_SUPPORTED`) |
| Progress | | progress from 0 to 1 |
| Input | nothing: the view is a `BView` and takes keys, mouse, touch, scrolling and `B_INPUT_METHOD_EVENT` itself, and uses `be_clipboard` for copy and paste | focus wanted, cursor shape, full screen wanted or left |
| New windows | | new window wanted (address, foreground or not, as WebPositive's `primaryAction`); the window answers with the view for the new tab. Close wanted |
| Downloads | `Cancel(id)`, `Open(id)`, `ShowInTracker(id)` | download added (id, file name, size), progress, finished (the path), failed. The engine saves the bytes; the window never fetches them a second time |
| Dialogs | | alert, confirm, prompt, leave this page, HTTP sign-in, choose a file, a permission (camera, microphone, location, notifications), a context menu with its items. Each carries a reply message; the window shows a native Haiku dialog and sends the answer back |
| Session | created with a `BWebSession` (cookies, cache, storage, a private flag); private windows get a session that keeps nothing on disk | |

That list covers what WebPositive uses today from `BWebView` and `BWebPage` (`docs/AOMI.md`) and what the Servo `WebViewDelegate` and a GeckoView-style bridge offer (`docs/design/LADYBIRD-PORT.md`). Something one engine cannot do (for example `SavePage` as MHTML) returns `B_NOT_SUPPORTED`, and the window greys out that item. A new call goes at the end; a call that shipped does not change.

## The header is the contract

`distro/apps/aomi/WebContentView.h` is the contract for every engine. The class is `BWebContentView`. The message codes in that header are the ones in the table above, including `Protocol()` on the page (`"h2"`, `"http/1.1"`, `"h3"`, or `B_NOT_SUPPORTED`). WebKit fills it with `BWebKitContentView`, a `BWebContentView` around Haiku's `BWebView`. The window creates that view only through `create_aomi_content_view()`. The next engine (`BServoView`) is another branch of that function. The window does not include WebKit or Servo headers.
