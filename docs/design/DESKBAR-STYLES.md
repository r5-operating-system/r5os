# Brief: Deskbar styles, Classic and Default (R5)

Owner decision, 22 Nov 2026. Design: Design canvas "R5 Deskbar styles – mockup"
(https://claude.ai/artifact/VXjq9yNWZ88yEzY9iqVUuA), boards A (as today: round, square, modern),
B (pill: round, square, menu open, growing), C (full-width bar: top, bottom) and D (hover and
click: option **E · Hollow** chosen). This replaces the style part of
the local design note of 13 Nov (`generated/deskbar-nextgen/DESIGN.md`, not in the repository): its "front app yellow" is withdrawn.

## Before anything

1. Read `AGENTS.md` in full, in particular "Settings window" (Desktop and Deskbar page),
   "launch_daemon and Deskbar" (replicants, `instantiate_deskbar_item`) and "LibR5". Reply
   with the rules that bind this task: native source in `distro/`, SPDX snippets on Haiku
   files, no new calls in `libbe`, build with `scripts/build-locked.sh` from your own
   worktree, ask before push/PR/jam, no stick.
2. Read `distro/desktop/src/apps/deskbar/`, `distro/settings/DeskbarModule.cpp`,
   `docs/APP-SERVER-COMPOSITOR.md` (window glass, shadow, effect steps, animations switch),
   `distro/desktop/src/servers/app/WindowEffectLevels.h`, `docs/LIBR5.md` and
   `docs/APPEARANCE-KIT.md` (`set_window_glass`, `set_view_glass`), R5Book
   `appearance-effects.html`, and `docs/design/R5-MODERN-DECORATOR.md` (Default (R5 Modern):
   the window outline hook that round corners need).
3. Work in your own worktree from `origin/main`. Do step 0 and stop.

## What the user gets

A **Style** choice for Deskbar, on the Settings page Desktop and Deskbar:

| Style | Options |
|---|---|
| **Classic** | Deskbar exactly as it is now (not the default). **Form:** as now (vertical block) or bar (Haiku's horizontal mode). **Place:** which corner or which edge. |
| **Default (R5)** (the default) | **Form:** as now, pill, or bar. **Corners:** round (18 px) or square. **Floating:** on or off, for every form. **Place:** which corner or which edge. **Effect:** follows Quiet, Balanced or Glassy from Appearance (no Deskbar switch). |

**Names in Settings** (owner, 22 Nov 2026). In this brief "as now", "pill" and "bar" are
working names; the user sees these:

| Setting | Values |
|---|---|
| Style | Default (R5) · Classic (Haiku) |
| Layout | Vertical (as now) · Compact (the pill) · Horizontal (the bar) |
| Corners | Rounded · Square |
| Floating | a check box, "Keep away from the screen edges" |
| Place | no label: chosen on the screen picture |
| Effect | no Deskbar setting: follows Window effects in Appearance |

Classic (Haiku) offers Layout (Vertical, Horizontal) and Place only.

Default values of Default (R5), confirmed by the owner (22 Nov 2026): Layout Vertical,
Corners Rounded, Floating on, top right.

Classic keeps every existing Haiku Deskbar option and its look. Default (R5) keeps the
existing Haiku options that still apply (always on top, auto-raise, auto-hide, icon size,
expander, sort, Tracker first, recent items). The separate Deskbar preferences program stays
installed. The Settings page keeps its place, title and description in the list
("Adjust desktop and Deskbar settings"); only its content gains the new options, and it
messages the running Deskbar at once, without writing Deskbar's settings file.

## The look of Default (R5) (numbers at the default font size; scale with the font)

- **Surfaces:** light glass, `rgba(240,245,252)` at the see-through of the chosen effect step;
  blur and shadow from the same table as windows (`WindowEffectLevels.h`: Quiet, Balanced,
  Glassy). One table, no second set of numbers. A floating Deskbar gets the full shadow; one
  against an edge gets a soft edge shadow; the "as now" form in its corner takes only the
  shadow (flat, opaque surface, 1 px lines, no bevels).
- **Corners:** round = 18 px on the Deskbar, the menu and the bar, like Default (R5 Modern)
  windows; highlights inside 10 px. Square = 0 everywhere, highlights too.
- **Floating:** a margin from the screen edges (14 px in the mockup; from the R5 look settings,
  not a constant in Deskbar), a 1 px light edge all round. Against an edge: a 1 px line only on
  the sides that face the desktop.
- **Zoomed and full-screen windows** (owner, 22 Nov 2026). `BWindow::Zoom()` in `libbe` already
  takes the zoom area from `BDeskbar::Frame()` and `Location()` (none with auto-hide or Shift).
  That path stays; no change in `libbe`. Deskbar reports its frame so that a zoomed window
  keeps clear of Deskbar **plus its margin** when floating; the shadow does not count. The
  Compact layout (the pill) **keeps its strip free** (option A): a zoomed window starts below
  the pill (or above it at the bottom) over the whole width, so nothing ever hides behind
  Deskbar. A real full-screen window (`BWindowScreen`, a full-screen video, a game) covers
  Deskbar as today. Step 0 checks `BWindow::Zoom()` for each layout and place, and says where
  the frame or margin must come from. In Classic (Haiku) nothing changes: it does not float
  and has no Compact layout, so zooming stays exactly as it is now.
- **As now (block):** top to bottom the wave pebble (menu), the tray with the clock, the
  application list with icon, name and window count.
- **Pill:** 54 px high, one row: wave pebble | running applications as **icons only** (a small
  dot under each) | clock | chevron that unfolds the full Deskbar. The pebble and every
  application have the same button (42 × 42 px, 18 px corners with round corners), so their
  hover shades are equal, and the same icon size (30 px for the pebble and the applications). With effects and animations
  on (GPU mode and the global "Animations" switch), the pill grows and a new icon pops in when
  an application starts (about 0.34 s, ease-out with a small overshoot); without them it
  changes at once.
- **Bar:** full width (or floating with the margin), top or bottom: wave pebble | applications
  with icon, name and window count | tray | clock.
- **Menu:** the Deskbar menu (all of Haiku's items) as a glass panel with the same corners. It
  opens **beside** the wave pebble, as in Haiku today: on the side that faces the screen
  (left of the pebble when Deskbar is on the right), its top level with the pebble's. About R5
  shows the wave pebble.
- **Hover and click (option E, Hollow):** hover = a faint shade (`rgba(30,45,70,.07)`) on the
  pebble and on applications; the pebble with its menu open = a soft hollow
  (`rgba(30,45,70,.12)`, inset shadow `0 2px 4px rgba(0,20,60,.28)`, inner 1 px line). The
  hover shade and the hollow around the pebble have 18 px corners with round corners (0 with
  square). Menu items hover in a light blue tint.
- **Front application:** as in Windows, a line, no fill and no yellow. Pill: every running
  application has a small dot under its icon; the front one's dot becomes an 18 × 3 px line.
  Bar: a 24 × 3 px line under the front application's button, text bold. As-now block (a
  vertical list): a 3 × 16 px line at the left of the front application's row, text bold.
  Colour R5 blue `#1d7bf5`; corners of the line follow the Corners option.
- **Tray items (status icons):** in Default (R5), R5's own tray items (network and Wi-Fi bars,
  volume, battery, Bluetooth, and the other R5 replicants) are drawn as in the mockup: plain
  line symbols in one colour, the Deskbar text colour (`#1d2a40` on the light glass), 2 px
  lines, the same size and baseline as the clock. No colours of their own: no green, red or
  coloured fills, no bevels. A state shows in the shape (fewer bars, an empty battery, a cross
  through the speaker), not in colour; only a warning (battery almost empty) may use the R5
  warning colour. This is for tray status symbols only, not for application icons, which stay
  the real Haiku icons. In Classic every tray item stays as it is now. Replicants of other
  programs draw themselves and are not changed; R5Book and `docs/STYLE.md` describe the rule so
  developers can follow it.
- **Without GPU** (Lavapipe without effects, CPU mode in Safe mode): no glass, no shadow, no
  animation; an opaque light surface (`#eef2f8`, line `#9fb0c8`). Round corners only where the
  outline hook of Default (R5 Modern) works in that mode; otherwise square.

## Steps

**Step 0 (report only, no code).** Answer, with file and line:
- How Deskbar draws today (`BarView`, `ExpandoMenuBar`, `TeamMenu`, `StatusView`, the window
  and its frame), where a style switch fits, and what of Classic must not change.
- How a round or floating Deskbar window gets its outline, glass and shadow: the window glass
  request, and the outline hook from the Modern decorator brief (step 1 there). Which of these
  exist on `origin/main` now and what this work must wait for.
- The animation path for the growing pill (GPU mode, the "Animations" switch).
- Which settings are new, where they are stored and how the Settings page messages Deskbar.
- Replicants: confirm `BDeskbar` and `instantiate_deskbar_item` stay unchanged. List R5's own
  tray items (Haiku's network, volume and power replicants, and R5's Bluetooth, Wi-Fi, AirPlay
  and other replicants in `distro/`), where each draws its icon, and how one learns which
  Deskbar style is active (or whether the tray symbols can come from one shared place, the R5
  libraries first).
- A plan for steps 1 to 4 with the tests. Stop and wait for the owner.

**Step 1.** The Style setting with Classic unchanged and Default (R5) in its "as now" form:
flat, the hollow states, effect steps, floating on/off, corners.
**Step 2.** The bar form in Default (R5).
**Step 3.** The pill form, its menu from the pebble, and the growing animation.
**Step 4.** The Settings page: Style, Form, Corners, Floating; Place stays on the screen
picture (`r5_screen_draw()`).
**Step 5.** R5's own tray items as one-colour line symbols in Default (R5), unchanged in
Classic; the rule in R5Book and `docs/STYLE.md`.

Each step: its own commit and `CHANGELOG.md` entry (problem, change, limits, how tested);
`docs/LIBR5.md`/R5Book only if a public call changes. No What's new and no `distro/BUILD`
bump until the owner has seen it.

## Tests

- Build only with `scripts/build-locked.sh` from your worktree; ask before the jam.
- One short QEMU boot on your own image copy: screendumps of each form (as now, pill, bar),
  round and square, floating and against the edge, one with the menu open, one in Safe mode.
  Not `scripts/run-qemu-r5.sh` while another session may run QEMU.
- Classic pixel-identical to before. A third-party tray replicant still loads.
- For each layout, floating and against the edge: zoom a window (it ends at Deskbar plus the
  margin, never under it); zoom with Shift and with auto-hide (whole screen); play a video
  full screen (Deskbar covered, back afterwards).
- No stick and no 5625U claim: the owner decides when this goes on a stick.

## Out of scope

The Settings window layout and its list, new tray replicant APIs, a vertical full-height
bar, the long bar with names in the pill form, window animations other than the pill.
Do not push or open a PR without asking.

Report at the end: **Veranderd / Getest / Onbewezen / Documentatie**.

## Built (night of 22 to 23 Nov 2026)

Steps 1 to 5 in one branch, for the owner's review (one PR, not merged).

- **Where it lives.** Deskbar's R5 look is in two new MIT files,
  `distro/desktop/src/apps/deskbar/DeskbarStyle.{h,cpp}` (the settings, the numbers of the
  mockup, the colours, the drawing helpers) and `DeskbarStyleViews.cpp` (the three layouts, the
  hover and the hollow, the front line, the growing pill, the tray symbols' placement). The Haiku
  files call into them from SPDX snippets; Classic (Haiku) keeps Haiku's code paths. The window is
  borderless in Default (R5); a new edge view around `TBarView` draws the 1 px edge.
- **Settings.** `desk_settings` gained `style`, `layout`, `rounded`, `floating` (new keys at the end
  of Deskbar's file; a file without them is Default (R5) in the form it had). The Settings page sends
  `'R5ds'` with absolute values; Deskbar applies and saves.
- **New calls, additive.** libr5ui: `set_window_shadow()`, `window_effects_active()`,
  `set_window_edge_line()` (`<WindowEffects.h>`), `r5_floating_margin()` and the tray style
  (`<R5UI.h>`); app_server codes at the end of `ServerProtocol.h`; the compositor honours the shadow
  kind and the program's edge line (`Compositor.cpp`, `CompositorEffects.cpp`). libr5icons: glyphs
  12 to 19 and level glyphs 3 and 4 for the tray.
- **Zoom.** `TBarWindow` answers BDeskbar's "Frame" with the margin; the pill answers
  `B_DESKBAR_TOP` or `B_DESKBAR_BOTTOM`. `BWindow::Zoom()` in libbe is unchanged.
- **Tests.** `scripts/qemu-deskbar-styles.py` with `scripts/deskbar-styles-guest.sh`, and
  `r5-effects-test zoom [autohide]` and `deskbar-add <path>`.

Educated guesses (owner to confirm; also in the PR under "Open keuzes"): the chevron of the pill
shows or hides the tray symbols (the pill shows only the clock folded); a layout against the edge is
square; the vertical leaf row uses the pill's 42 x 42 button for its hover and hollow; a vertical row
hovers with corners of 9 (board A1's `.row`); the edge shadow uses the same step as the floating one
(the mockup's 0.22 to 0.32 is a second set of numbers, which the brief rules out); the battery warning
colour is `B_FAILURE_COLOR`; the Deskbar menu's items keep the control look's highlight (the light
blue tint with corners of 10 needs every Tracker menu item class to change); the default width of a
new Vertical Deskbar is 210 at 12 points.
