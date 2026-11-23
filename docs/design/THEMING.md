# Theming

Owner's plan, version 2 (23 Nov 2026), approved with the mockups
(`docs/design/theme-mockup/`, artifact `5cQt2YSQ31PAVpsGnrxsSA`: A1 the Theme
page light, A2 dark, A3 Save as theme, B1 R5 Modern dark with the GPU, B2
Default (R5) dark without effects and in Safe mode). This file is the plan
and how it is built.

## The rule

**A theme is a saved combination of parts that already exist.** There is no
drawing engine, no CSS and no drawing rules. Each part stays a normal Haiku
add-on or setting, so a program runs unchanged and only looks different,
through `BControlLook` and `ui_color`.

| Part | Choices | Set by |
|---|---|---|
| Windows | Default (R5 Modern), Default (R5), Classic (Haiku), any decorator add-on | `DecorInfoUtility::SetDecorator()`, as Appearance |
| Controls | R5 Modern (new), Haiku, and any control look add-on (Be and Flat are in haiku_extras) | `BPrivate::set_control_look()`, as Appearance |
| Scrollbars | style (R5 Modern or Haiku, with the R5 Modern controls), arrows (single or double) | `set_scroll_bar_info()`; the style is read by the R5 Modern controls |
| Deskbar | Default (R5) or Classic (Haiku); for Default (R5) Vertical, Compact or Horizontal, rounded, away from the screen edges | Deskbar's own message (`'R5ds'`), as Desktop and Deskbar |
| Mode | Light or Dark | a `ui_color` set (`set_ui_colors()`) |
| Accent | Blue, Teal, Purple, Pink, Orange, Graphite | `B_CONTROL_HIGHLIGHT_COLOR` and the selection colours |
| Effects | shadow, frosted glass, animations, strength | `set_window_effects()`, `set_window_effect_strength()` |

Built-in themes: **R5 Modern** (light and dark), **R5** (light and dark),
**Classic (Haiku)** (light only, exactly as Haiku). A built-in theme cannot be
overwritten; it keeps the effects as they are.

## Owner's decisions (23 Nov 2026)

- The R5 Modern controls: buttons, fields, lists, scroll bars, menus, check
  boxes, radio buttons, sliders and tabs, as in the mockup: radius about 7 on
  buttons and fields, the field line `#b8c0cc`, the button `#f7f8fa` with
  `#b4bcc8`, the accent `#0084ff`, check boxes filled with the accent and a
  white tick, the segmented control, the scroll bar as a round track with a
  round thumb. Radii scale with the font size. Dark: the colours of A2 and B1.
- Theme is its own page under Personalization, not a tab. The Appearance
  tabs stay (Fonts, Colors, Look and feel, Antialiasing).
- Saving your own theme, and every part on its own.
- Dark mode for R5 Modern, Default (R5) and the new Deskbar, with and without
  the GPU.
- The open choices, decided as recommended: (1) the scroll bars keep Haiku's
  width and its arrow choice, only a new look; (2) live switching in this
  round; (3) the default stays R5, light; (4) the dark tab is the deeper
  yellow `#e8b923`; (5) Desktop and Deskbar stays, and the Theme page shows
  the same values.
- Live switching inside libbe, accepted by the owner on 23 Nov 2026: the
  internal handler in R5's `BWindow`, with no new call, field or symbol and
  no ABI change (open choice 1 below).

## How it is built

### The R5 Modern controls

`distro/desktop/src/add-ons/control_look/R5ModernControlLook/`, a
`BControlLook` add-on on top of `HaikuControlLook`, like Haiku's
`FlatControlLook` (MIT, R5 code). It is installed in the main package
(`add-ons/control_look/R5ModernControlLook`); Be and Flat stay in
haiku_extras.

- It changes the painting, never the sizes: `GetFrameInsets()` and
  `GetBackgroundInsets()` are Haiku's, and every drawing call insets the rect
  as Haiku's does, on the sides named in `borders` only (a spinner's buttons,
  a tab view's plain border); a face that is open on a side is cut off
  there. Tabs leave the label where Haiku's does. A scroll view frame with
  some sides only stays Haiku's square frame. The radius arguments of the
  button calls are not used: the look has its own radius.
- A child view that paints its own square background 2 px inside a round
  frame (the text view of `BTextControl`, the list of a scroll view) may
  touch the inner edge of the arc at the corner pixels; the radius of
  scroll view frames is 3 for that reason.
- Numbers at 12 pt, scaled with `be_plain_font`: buttons and fields round 7,
  check boxes 4, boxes and tabs 8, menu selections 5, the scroll bar thumb and
  track fully round.
- Colours: only `ui_color`. Face `B_CONTROL_BACKGROUND_COLOR`, line
  `B_CONTROL_BORDER_COLOR` (`#b4bcc8`, also for fields: the mockup's field
  line `#b8c0cc` is one step lighter and is not a colour of its own), field
  `B_DOCUMENT_BACKGROUND_COLOR`, accent
  `B_CONTROL_HIGHLIGHT_COLOR`, thumb `B_SCROLL_BAR_THUMB_COLOR`; the thin frame
  line and the track are the panel colour mixed toward the mockup's ink
  (`#0f172a`) or white. A colour set that keeps Haiku's mid-grey thumb or
  border still gets a visible thumb and line.
- The default button is the accent with a white label (mockup B1 "Sign in").
- A focused field has the accent line and a soft accent ring (A3).
- Painter (CPU) draws the same shapes antialiased; nothing needs the GPU.
- The scroll bar style: the theme in use says `scroll_bars haiku` or
  `modern` in `~/config/settings/r5/theme`; the look reads it when a program
  loads it (at start and on a live switch). With `haiku` it calls
  `HaikuControlLook` for the scroll bars.
- The menu bar (owner's choice A, 23 Nov 2026): the first title starts 8 px
  further in at 12 points, so the title, its highlight and the open menu
  clear the round window corner (18 px). R5 Modern returns 8 more for
  `B_MENU_BAR_BACKGROUND` in Haiku's `GetBackgroundInsets()`; R5's `BMenu`
  row layout starts a `BMenuBar` there, minus Haiku's 1, when the bar is on
  the left edge of a titled, document, floating or modal window. Haiku and
  R5 controls return 1, so their menu bars do not move; Deskbar's bars (a
  bordered or borderless window) do not move either. No call, field or
  symbol is added to libbe; the change is inside `BMenu`, like the live
  switch in `BWindow`.

### Menus (owner's approval of the menu mockup, 23 Nov 2026)

The reference is the menu mockup in `docs/design/menu-mockup/` (boards M1
to M6; `scripts/render-menu-mockup.py` renders them). One menu look for
every program with the R5 Modern controls, drawn by the control look;
`BMenu` and `BMenuItem` keep Haiku's API, and R5's copies of their sources
lay out and draw the items.

- The R5 menu style is asked for by the control look, through Haiku's
  `GetBackgroundInsets()`: more than 1 for `B_MENU_BACKGROUND` (R5 Modern
  returns 5, the space inside the menu window), and `B_MENU_ITEM_BACKGROUND`
  is the padding round an item's content (11 at the left, 9 at the right,
  and a row of 26 at 12 points). Haiku's looks return 1, so their menus do
  not move. Only a column menu in a menu window takes the style; a menu
  bar, and a menu inside an ordinary window (Deskbar's own bars), do not.
- Rows are 26, the highlight is the accent with white text and radius 6;
  separators are one line, 8 in from each side; the shortcut is text at the
  right ("Shift+Alt+W": the modifiers by the key they are on, as Haiku's key
  caps), a little smaller (11.5 to 13) and muted; the submenu chevron is the
  mockup's path. A menu with a marked item, or a radio menu, keeps a 12 px
  column for the check mark or the dot after 7 (M6), and the label moves 16
  more; other menus start the label at 11 (M4). A submenu opens beside its
  menu with 4 between the two lines (M1, M4), its first row level with the
  item; only a submenu of a menu that has the style itself.
- From the look: the margin, the row height and the left and right
  padding. Fixed in R5's `BMenu`: the mark column (12 after 7, then 8), the
  gap of 16 before a shortcut, the chevron box (10, and 6 before it), the
  separator row (9, 8 in), the window radius (10) and the submenu gap (4).
  An empty menu keeps Haiku's "<empty>" item and window.
- Haiku's inset contract holds for the hooks: in the style,
  `DrawMenuBackground()` gives the rect back by the margin and
  `DrawMenuItemBackground()` by the item padding, the values
  `GetBackgroundInsets()` returns. Any control look that returns more than 1
  for `B_MENU_BACKGROUND` gets the style; Haiku's looks and the add-ons
  that ship with Haiku return 1.
- Limits: a menu that is open while the colours change keeps the old menu
  colour until it opens again; a program's own view colour on a menu is
  replaced while the style is on, and becomes `B_TRANSPARENT_COLOR` again
  (Haiku's) when the look no longer asks for it; a pop-up field that gains
  its mark column while open keeps its place until it opens again.
- The window: round corners (10 at 12 points) and the full shadow, through
  the existing window effects path (`AS_R5_SET_WINDOW_CORNER_RADIUS` and
  `AS_R5_SET_WINDOW_SHADOW`, the messages behind libr5ui's
  `<WindowEffects.h>`; libbe sends them itself because it does not link
  libr5ui). The menu window keeps Haiku's bordered look: a borderless menu
  window did not show in QEMU. Its line is the decorator's: R5 Modern draws
  a bordered window that avoids the focus (menus, tool tips) in the menu's
  edge colour (about `#b8c0cc` light, `#3a404c` dark), and the compositor
  carries it round the corners.
- Chosen: the menus are opaque, the menu colour, with the GPU too. Glass
  through `AS_R5_SET_VIEW_GLASS` was tried: a menu window that moved to the
  next title of a menu bar kept the old picture (QEMU, 23 Nov 2026). The
  menu's view gets the menu colour, so app_server erases it and a highlight
  leaves nothing behind.
- Without effects or in Safe mode the same colours and no shadow; the
  corners are stepped, and the highlight and the menu bar pill are square
  (the look asks app_server whether the effects show, at most once a
  second).
- The menu bar keeps choice A and Haiku's height; its open title is a quiet
  pill (black 7 % light, white 10 % dark, radius 6) with the text in its own
  colour.
- R5 Modern light's menu colour is the mockup's `#f7f8fa` (was white).

### Live switching

A running program takes new controls and colours without a restart.

- Colours were already live in Haiku (`B_COLORS_UPDATED`).
- Controls: `set_theme()` (libr5ui) sets the control look in app_server and
  then broadcasts `B_COLORS_UPDATED` with an int64 `"r5:control_look"`
  serial through `BRoster::Broadcast()`. `BApplication` passes the message
  to every window (Haiku does this for colours). In R5's `BWindow`
  (`distro/desktop/src/kits/interface/Window.cpp`) the first window of a team
  that sees a new serial loads the add-on app_server names now and makes it
  `be_control_look`, as `_init_interface_kit_()` does at start; every window
  then draws again.
- No call, field or symbol is added to libbe, and the ABI is unchanged: it
  is an internal handler of a message libbe already handles, accepted by
  the owner on 23 Nov 2026. The other R5 changes inside libbe are the menu
  bar start (choice A) and the R5 menu style ("Menus" above: `BMenu`,
  `BMenuItem`, `BSeparatorItem`, `MenuPrivate`), both internal and asked
  for by the control look; neither adds a call, a field or a symbol to the
  public API.
- The old look and its add-on stay loaded, because another window thread may
  still be inside one of its methods (the reason Haiku gives in
  `_fini_interface_kit_()` for having no live switching). One small object
  per switch stays in memory until the program quits.
- After the switch a window lays itself out again (`InvalidateLayout()`), for
  looks with other sizes (Be, Flat). A program that paints its own widgets
  stays as it was until it draws again.
- A program with no window when the message comes keeps the old look for
  the windows it opens later, until it restarts (the swap is in `BWindow`,
  not in `BApplication`, whose source R5 does not carry).

### Themes in libr5ui

`<Themes.h>`, namespace `BAppearanceKit`, in `libr5ui.so` (docs/LIBR5.md,
"Themes"). A `theme_info` struct with free functions, in the BeOS style of
`menu_info` and `scroll_bar_info`: `get_theme()`, `set_theme()`,
`count_themes()`, `get_theme_at()`, `find_theme()`, `is_built_in_theme()`,
`save_theme()`, `remove_theme()`, `theme_matches()`, `get_theme_colors()`,
and the accent list.

A theme file is driver settings text, short enough for a mail:

```
name "Evening"
base "Evening"
windows "R5ModernDecorator"
controls "R5ModernControlLook"
scroll_bars modern
arrows single
deskbar_style r5
deskbar_layout vertical
deskbar_rounded true
deskbar_floating true
mode dark
accent #7c4ddb
```

A key that is not there is a part the theme does not hold; applying it
leaves that part as it is. Saved themes are in
`~/config/settings/r5/themes/<name>`; the theme in use (with its base, so
the page can say "R5 Modern (changed)") is `~/config/settings/r5/theme`.
Export and import are a later step; a file can be copied by hand.

### The colour sets

`get_theme_colors()` writes every colour a theme sets, so a change from one
set to another leaves nothing of the first. The desktop colour is the
Wallpaper page's and is not part of a theme.

- R5 and Classic (Haiku), light: Haiku's factory colours
  (`BPrivate::GetSystemColor()`), unchanged for existing users.
- R5 Modern, light (A1): panel `#eef1f5`, text `#141414`, fields and lists
  `#ffffff`, control `#f7f8fa` with `#b4bcc8`, thumb `#b6bec9`, selection the
  accent with white text.
- Dark, R5 Modern and R5 (A2, B1, B2): panel `#1f232b`, text `#e6e9ef`,
  fields `#15181e`, lists `#1b1f26`, controls `#2a2f39` with `#454c59`, menus
  `#262b34`, thumb `#4f5664`, tab `#e8b923` with `#1a1a1a`, inactive tab
  `#2e333d` with `#a3abb8`, frame `#4a515e`, selection the accent (`#2f95ff`)
  with white text; Haiku's own dark link colours.
- The accent sets `B_CONTROL_HIGHLIGHT_COLOR`, `B_CONTROL_MARK_COLOR`,
  `B_NAVIGATION_BASE_COLOR`, `B_STATUS_BAR_COLOR`, and in R5 Modern and dark
  also the menu and list selection.

Custom colours from Appearance > Colors stay until a theme, the mode or the
accent is chosen on the Theme page. A change of the controls alone swaps the
colour set only while the colours are still the set of the theme before;
custom colours stay. "Save as theme" keeps the mode and the accent, not a
full colour list.

### Dark mode of the other parts

Every part decides with `ui_color(B_PANEL_BACKGROUND_COLOR).IsDark()` (in
app_server `DesktopSettings::UIColor()`); there is no extra setting or
message.

- Default (R5 Modern): the edge line white at 10 % with the effects (75 % in
  light), `#4a515e` without; the tab buttons' line and fill per B1.
- Default (R5): the frame line is the border colour itself when it is dark.
- Deskbar Default (R5): dark glass `#161a22` under the blur, opaque
  `#1c2029` without the GPU, text `#e6e9ef`, other rows `#c9cfd9`, the front
  row a light shade and the accent line; on `B_COLORS_UPDATED` it applies the
  whole style again. Classic (Haiku) follows `ui_color` as before.
- Without the GPU (Safe mode): the same colours, opaque, with stepped
  corners.

### The Theme page

`distro/settings/ThemeModule.cpp`, built into Settings, under
Personalization. Name "Theme", description "Choose how windows, controls and
the Deskbar look". The page content is A1 without the Appearance tab strip
(the page has its own place in the list):

1. "Theme", "Save as theme…" and "Delete" (Delete only for your own theme);
2. the theme cards, five to a row, with a picture of a window and the
   Deskbar in each theme, the name ("R5 Modern (changed)" when a part
   differs) and "Built in" or "Your theme", and the dashed "Save as theme…"
   card;
3. Mode (Light, Dark) as a segmented control, and the accent swatches;
4. Parts, a box that folds: Windows and Controls (with About), Scrollbars
   (style, arrows, a live preview drawn by `be_control_look`), Deskbar
   (style, layout, Rounded, Away from the screen edges), Effects (Shadow,
   Frosted glass, Animations, Strength).

A change is applied at once. Revert puts back the theme and every colour
from when the page was opened (with the effects); Defaults chooses R5,
light, with the factory effects (all on, Balanced). The page is not
locked. The segmented control is local to the page: Haiku has no such
control and libr5ui had none.

"Save as theme…" (A3) asks for a name and which parts to keep (all but the
effects are ticked). A built-in name is refused; an existing own theme of
that name is replaced.

### Test tool

`r5-effects-test theme list|get`, `theme <name> [light|dark]`,
`theme mode light|dark`, `controls [seconds]` (a window with every control).
`scripts/qemu-theming.py` and `scripts/theming-guest.sh` run the screenshots
in QEMU without the owner.

## Open choices (educated guesses, for the owner)

0. **The theme cards are the page's own view, not `BCardStrip`.** libr5ui's
   `BCardStrip` was checked first (AGENTS.md, LibR5): it is one row that
   scrolls sideways, fills the chosen card with the accent, and shows its
   dashed card only while it has no cards. The mockup has five cards to a
   row that wrap, a ring without a fill, and the dashed "Save as theme…"
   card always at the end. The choices: keep the page's own view (now),
   use `BCardStrip` and accept its look, or add those three things to
   `BCardStrip` at the end of its API.

1. **Live switching inside libbe** (decided: accepted by the owner on
   23 Nov 2026). The handler in `BWindow` is new
   behaviour in libbe, though no call, field or symbol is added. Without it a
   running program keeps its controls until it restarts. The alternative,
   with nothing in libbe, is that live switching works only between looks
   that are add-ons with a switch of their own, not from the built-in Haiku
   controls, which is the default.
2. **"No arrows".** The mockup's Arrows control has No arrows, Single and
   Double. Haiku's `scroll_bar_info` has only single and double; "no arrows"
   needs a change in `BScrollBar` (libbe) for the layout of the thumb and the
   clicks. The page shows Single and Double.
3. **The scroll bar style follows the controls.** "R5 Modern" scroll bars
   exist only with the R5 Modern controls; with other controls the menu
   shows Haiku and is off.
4. **Accent in R5 light.** In R5 and Classic (Haiku), light, the accent
   changes the highlight, the check marks and the focus colour, and keeps
   Haiku's grey selection with black text; R5 Modern and dark select with the
   accent and white text.
5. **Custom colours.** Choosing a theme, the mode or the accent replaces
   custom colours from Appearance > Colors with the theme's set (the controls
   alone do not, see above).
6. **Scroll bar width.** A theme can set it (8 to 32 at 12 points; the page
   offers Thin 10, Haiku width, Wide 18), through Haiku's
   `BControlLook::GetScrollBarWidth()`. Only the R5 Modern look reads it from
   the theme (another add-on gives its own width). `BScrollBar` and
   `BScrollView` ask it; a program that places scroll bars with
   `B_V_SCROLL_BAR_WIDTH` keeps Haiku's width and still works.

7. **Menus: what differs from the mockup.** The selected row keeps the
   regular weight (the mockup has 600): a bold label would be wider than the
   width the menu was laid out for. The menu bar keeps Haiku's height (the
   mockup's bar is 28). The Deskbar menu's icons start at 11, the Tracker
   board's text inset (the Deskbar board has 9). The mark column follows the
   parts board (after 7); the Tracker "Open with" board puts it after 11.
   The pop-up field keeps radius 7 in Safe mode (the parts board shows 3), as
   every other field does. Headings inside a menu (the parts board's "Wrap")
   are not a Haiku menu item and are not drawn. The menu bar is the menu
   colour (`#f7f8fa`, as Haiku draws a bar in `B_MENU_BACKGROUND_COLOR`);
   the mockup's bar is the panel colour (`#eef1f5`), and its open title is
   600 weight (here regular, for the same reason as the rows). The colours
   are mixed from `ui_color` and land near the boards' values, not on
   them (a separator about `#d7d9dd` for `#d3d8e0`, the shortcut about
   `#6f6f70` for `#6b7380`). The measures follow the plain font size, as
   every R5 Modern measure does; a menu font of another size keeps rows of
   26 at the plain size.

## The Theme Kit (owner's addition, 23 Nov 2026)

For theme makers: the calls in `<Themes.h>` (list, read, apply, save,
remove, `B_THEME_CHANGED`), the versioned file format (`format 1`, keys only
added; `docs/LIBR5.md`, "Theme files"), the rules for an add-on that fits a
theme, the control look template
`distro/desktop/src/add-ons/control_look/ExampleControlLook/` (compiled by
the system build, not installed), the sample theme `docs/theme-kit/Evening`,
and the R5Book chapter "Making themes" (`docs/r5book/making-themes.html`).

## Later

Themes from other people via HaikuDepot, Export and Import, an icon set per
theme, dark that follows the time of day.
