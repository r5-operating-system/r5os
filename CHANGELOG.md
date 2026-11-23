# R5OS changelog (lokaal)

<!-- changelog-archive: begin -->
Older entries are in `docs/changelog/`, one file per ISO week (Monday to Sunday), moved word for word by `scripts/changelog-archive.py`. Search there too (`grep -r <term> CHANGELOG.md docs/changelog/`).

- [2026-W47](docs/changelog/2026-W47.md): 2026-11-16 to 2026-11-22, 238 entries
- [2026-W46](docs/changelog/2026-W46.md): 2026-11-09 to 2026-11-15, 183 entries
- [2026-W44](docs/changelog/2026-W44.md): 2026-10-26 to 2026-11-01, 2 entries
<!-- changelog-archive: end -->

## 2026-11-23 - Deskbar - tray symbols without colored fringes in Safe mode

Problem: in Safe mode (the CPU mode) the volume and network symbols in the Deskbar tray had colored fringes (cyan and yellow along the lines); with the GPU they were clean. `r5_glyph_draw()` drew the glyph's shapes straight into the view, and app_server's Painter draws every filled shape with subpixel antialiasing when the subpixel setting is on (`Painter::_RasterizePath()`, `gSubpixelAntialiasing`). The Bluetooth item, which uses `r5_glyph_bitmap()`, had no fringes.

What changed: `r5_glyph_draw()` (libr5icons) paints the glyph into a transparent bitmap first, the way `r5_glyph_bitmap()` does, and draws that bitmap with `B_OP_ALPHA` / `B_ALPHA_COMPOSITE`. Same call, same place and size; the old direct drawing stays as the fall back when the bitmap cannot be made. The level glyphs (`r5_level_glyph_draw()`: Wi-Fi strength, battery) still draw directly and may show the same fringes in Safe mode; there is no Wi-Fi or battery in QEMU, so that is not seen and not changed.

Tested: QEMU (KVM, 1280 by 800), build of 23 Nov about 07:40 with `scripts/build-locked.sh` (`jam_exit=0`, `mesa_exit=0`), `scripts/qemu-r5modern.py` GPU run and `--safe` run: in Safe mode the volume, network, AirPlay and Bluetooth symbols are clean grey, no colored fringes; with the GPU they look as before. Not on a real machine.

## 2026-11-23 - Deskbar styles - the external review (points 6 to 12)

Problem: an external review of PR #4561 found seven points; each was checked against the code first, and all seven held.

What changed:
- 6: `Window::SetCornerRadius()` now invalidates the views' screen clipping. The views kept the clipping cut to the old outline, so after round to square the corners were never drawn again (the dark specks), and Classic kept round top corners in Safe mode.
- 7: the compositor's pixel compare leaves out the blended corners of round layers (`layer_round.area`); it compared them with the raw pixels (the `MISMATCH` beside the Deskbar). The guest script now moves a round window with the effects off, so the compare runs during the Classic step (it compares every 100th composed frame).
- 8: Deskbar's glass needs frosted glass on in Appearance. app_server sends `BAppearanceKit::B_WINDOW_EFFECTS_CHANGED` (libr5ui, `'r5we'`, bool `"active"`, uint32 `"effects"`) to every program when the effects change or the compositor turns off, as Haiku does with `B_COLORS_UPDATED`; Deskbar applies its style again on it.
- 9: in the CPU mode app_server paints the stepped rim of a window without a decorator (`Window::_DrawProgramRim()`); the clipping is a region of the pool, never NULL (`ConstrainClippingRegion(NULL)` was the earlier crash). Deskbar asks for its edge line in both modes again.
- 10: the window watcher also runs for Default (R5) Horizontal; a changed count repaints and lays the bar out again (the first test showed "Style… 2": the item did not widen).
- 11: each R5 tray item keeps its own style and colour (`struct r5_tray_look`, `r5_deskbar_tray_look()`, `r5_tray_look_update()`, added at the end of libr5ui); the values behind the getters change under one lock. Headers copied from Haiku for this: PowerStatusView.h, ProcessController.h, mail DeskbarView.h.
- 12: the earlier claim was wrong. In Safe mode the network and volume items do draw the line symbols; what looked like their own coloured icons are colour fringes of the subpixel smoothing on the symbols in the CPU mode.

Checked and left as it was: the menu corners (reviewer's B11) are set in `TDeskbarMenu::AttachedToWindow()`, before the menu window is shown and before Track; no call from Draw and no thread remains. Compact shows no count, so the watcher stays off there.

Tested (QEMU, KVM, `-machine pc`, 2 CPUs, 2 GB, 1280 by 800; builds of 23 Nov about 05:53 and 06:14 with `scripts/build-locked.sh`, `jam_exit=0`, `mesa_exit=0`, the image copied under the held lock):
- the whole list (run 12, GPU mode, Lavapipe): no hang; the compare ran 8 times, `compose compare ok` each time, 4 of them during the Classic step, no `MISMATCH`; the square floating Vertical after round has clean corners (no specks); the zoom numbers as before.
- Safe mode (safe 5, `gpu compositor off: safe mode`): no crash with the CPU edge line; Classic after Default (R5) has square top corners; the stepped grey line runs round the Default (R5) corners.
- the window count (count 3, Horizontal at the bottom): StyledEdit with 1, 2, 3, 2 and 1 windows shows no count, 2, 3, 2 and none, with the full name.
- after merging main with #4560 (R5 Modern; `SetCornerRadius()` now calls its `_DecoratorShapeChanged()`), build of 23 Nov about 07:05, `scripts/qemu-deskbar-styles.py --quick`: Deskbar starts in Default (R5), round and floating, beside the R5 Modern windows; with effects off the compare ran twice, `compose compare ok`, during the Default (R5) step, none fell in the Classic step of that short run. In the first quick run the effects did not switch off (the compositor kept "shadow on frosted on", so no compare ran); the second run, the same build and the command's output logged, switched them (`rc 0`). Not explained, not seen again.

Not proven: the frosted glass switch and a GPU to CPU fallback while Deskbar runs (the message is sent; no run switched them while watching Deskbar); a real screen, the 5625U, Wi-Fi, a battery, mail. Found, not fixed: in Safe mode the Classic Deskbar's logo button is empty (it was in the earlier Safe mode picture too); the subpixel colour fringes on the tray symbols in the CPU mode.

## 2026-11-23 - Deskbar styles - the QEMU test

Problem: the Deskbar styles had to be checked without the owner, against the mockup.

What changed: two independent code reviews (fresh agents, given only the code and the docs) and their fixes, listed below; `scripts/qemu-deskbar-styles.py` (WSL) boots its own copy of the image with `scripts/deskbar-styles-guest.sh` as a UserBootscript job; the guest sets each style with Deskbar's own `'R5ds'` message (`hey`), says on the serial port what is on screen, and the host takes screendumps, moves and clicks the mouse (hover, menu, chevron) and holds Shift. Pictures: `docs/design/screens/deskbar/` (every test at 1280 by 800 from QMP `screendump` in `full/`, and crops enlarged with nearest-neighbour next to the mockup rendered at 100 % from its own CSS and images in `crops/`).

Review 1 fixes: `FetchTeamIcon()` keeps the R5 icon; tray items follow Deskbar's colour only when they took it, nothing below a replicant is touched, Classic resets the low colour; the tray message carries `"style"` and `"color"`; Classic keeps Haiku's default width and its first-hit window check; the edge-line request only rebuilds on a change. Review 2 fixes: Classic after Default (R5) gives the bar's shared menu window and the drag region's views Haiku's look back; the folded pill only shows what it hid; icons take the new layout's size after the state change; an uncached icon is freed on the fallback; a pill on the left stays left on the Settings page; no program edge line in the CPU mode (painting its rim in `Window::_DrawBorder()` crashed app_server in Safe mode in QEMU, so that attempt was reverted). Also: a radius change exposes the old cut corners and rim (as `ResizeBy()`), and the hover and the Hollow were tuned against the 100 % mockup crops.

Tested (QEMU, KVM, `-machine pc`, 2 CPUs, 2 GB, Lavapipe GPU mode, 1280 by 800, build of 23 Nov about 01:35, the image copied while the build lock was held; run 11, the whole list, no hang; Safe mode run 4 on the same build):
- Board A: Vertical floating round (210 wide, margin 14), square, and in the corner flat; hover shade on the pebble; the Hollow with the menu open beside it (8 px gap, top level); front application bold with the blue line; 20 px icons.
- Board B: the pill 54 high, margin 14; menu beside it; hover; chevron; square; against the edge; the grow (a new application: the pill grows, the icon pops in, then its blue line).
- Board C: the bar 48 high, top and bottom, floating (margin on both sides) and against the edge, square; the menu under the pebble.
- Zoom (`r5-effects-test zoom`, the window frame then Deskbar's reported frame): Vertical in the corner 1066 < 1069; Vertical floating 1038 < 1041 (Deskbar 1055 plus the margin); Compact top 108 > 81 over the full width; Horizontal top floating 102 > 75, bottom 721 < 724, top against the edge 74 > 47; with Shift and with auto-hide the whole screen; Classic vertical 1008 < 1011 as before.
- A third-party tray item: `deskbar-add /boot/system/apps/Workspaces` "No error", and it draws itself.
- The Settings page with its four new controls; Classic (Haiku) vertical and horizontal as Haiku draws them.

- Safe mode (`gpu compositor off: safe mode` in the syslog): Vertical, Compact and Classic with the opaque `#eef2f8` surface and stepped corners, no glass, no shadow; no crash.

Not proven: the compare (run 11 logged no `compose compare` line; run 10, before the last fix, logged one `MISMATCH at 1009,14`, 2 px left of the Classic Deskbar during the Classic step after Default (R5), not explained); in Safe mode the network and volume items kept their own icons (Bluetooth and AirPlay drew symbols), not explained; dark specks in the bottom corners of the square floating Vertical after round to square remain; a real screen, the 5625U, a Wi-Fi adapter, a battery.

## 2026-11-23 - Windows, Default (R5 Modern): fixes from the external review

Problem: an external review of the branch named five points; each was checked against the code first.
1. A tab that moves (Shift-drag, `SetTabLocation()`, decorator settings), or a stacked tab that is moved, added or removed, did not make `Window` drop its content region and its views' screen clipping, so after a Shift-drag of the tab from the left top corner to the right one the old corner could stay cut out of the content.
2. `_DoTabLayout()`: with a window narrower than its tab and the tab not at the left, the room for the tab was negative and the cast to `uint32` wrapped.
3. In the GPU mode the edge line round the window's top arcs, where the tab card lies behind them, was about half a pixel thinner than at the bottom: `window_outline_build()` left the frame's own pixels inside the arc out of `area` there, so the compositor did not carry the line into them.
4. `GetGlassRegion()` and `GetEdgeLine()` read the top tab (`_IsModern()`) before taking the decorator's lock.
5. `Compositor.cpp`: a window that gets its first pixels while it opens damaged its shadow once more. The reviewer's check (`layer->visible < 0`) does not work at that point: the frame's visibility is set later, by `_AnimationFrame()`. The damage was not wrong either, only redundant: the animation damages `animArea`, which holds the shadow, every frame, the last one included.

What changed: new `Window::_StackShapeChanged()` (every window of the stack drops its content region and clipping), called from `SetTabLocation()`, `SetDecoratorSettings()`, `AddWindowToStack()`, `DetachFromWindowStack()` and `MoveToStackPosition()`. `_DoTabLayout()` keeps the room at 0 or more. `window_outline_build()` keeps the first rectangle's own pixels in its corner squares in `area` also where another rectangle lies behind them. The two decorator calls take the lock first and give it back before they call the base class. The compositor skips that shadow damage while the window opens (`animOpen`). Also (reviewer's answer B1): the Appearance program and the Settings page mark the current decorator by its `ShortcutName()`, not by the name it shows; no other code in the tree compares a decorator by its name (`setdecor` and `r5-effects-test` find one with `FindDecorator()`, which also takes "Default" and the file name).

Limits: in QEMU StyledEdit cannot be made narrower than its narrowest tab (68 px at 12 pt, the two buttons), so the case of point 2 was not reached by a drag; the guard stays for a window whose size limits allow it. The QEMU driver's narrow step drags to 120 px; the 68 px shot was taken by an extra drag on the running Safe mode QEMU.

Tested: build 12 (`jam_exit=0`, `mesa_exit=0`; build 11 the same without the Appearance change). QEMU run 9 with Lavapipe: the compare (`gpu_debug`) prints only `ok` lines, no `MISMATCH`, while Default (R5), Classic (Haiku) and Default (R5 Modern) are on; the tab Shift-dragged into the right corner and the window narrowed. The left top corner is round afterwards, the right one shows the card. Without effects the top arc's line now has the same inner pixel as the bottom arcs (before and after crops, run 7 and run 9). QEMU run 8 in Safe mode: the same two steps, and the narrowest window. Run 8's normal boot stopped at the first-boot SSH key step and was repeated as run 9. In run 9 the Appearance window was caught half drawn while it started (run 5 caught it still empty at the same moment); not looked into further. Not tested: the 5625U (RADV), a stick, stacked windows (Stack and Tile).

## 2026-11-23 - Windows, Default (R5 Modern): fixes from the second review

Problem: a second independent review (a new agent, code and docs only, on the code after the first review's fixes) found that a new title, new flags or an added or removed tab can change the width of the tab card, so the card may come to lie behind a top corner of the window or no longer. The wedge rows were not in the update region, and `Window` kept its cached content region (which leaves the decorator's footprint out) and its views' screen clipping, so wedge pixels could stay on screen or stay undrawn.

What changed: `R5ModernDecorator` overrides `_SetTitle()`, `_SetFlags()`, `_AddTab()` and `_RemoveTab()` and adds the rows of the window's top corners to the update region (`_IncludeWedgeRows()`, also used by `_SetTabLocation()` and `_MoveTab()`). New `Window::_DecoratorShapeChanged()` drops the content, visible content and drawing regions and the top view's screen clipping; called from `SetTitle()`, `SetFlags()`, `SetLook()` and `ReloadDecor()`.

Also noted by the review, not changed: in the GPU mode the edge line round the window's top arcs, where the card lies behind them, is about half a pixel thinner than round the bottom arcs (the compositor's line reaches one pixel into the content there, and those pixels are the window's own); a resize or Stack and Tile highlight colours the straight sides, not the arcs; the sort in Appearance is by bytes, not by locale; "Classic (Haiku)" is matched by its translated name.

Tested: build 10 (`jam_exit=0`); QEMU run 7 (see the PR). A title change that moves the card over a corner was not tested in QEMU.

## 2026-11-23 - Windows, Default (R5 Modern): fixes from the first review

Problem: an independent review of the branch (a fresh agent, code and docs only) found: the wedge in the footprint depended on the mode, and the footprint is cached, so after a fall back from the GPU to the CPU mode in a session some wedge pixels were drawn by nobody; wedge pixels between half and fully covered were in the footprint but returned no region, so a click there did nothing; reordering stacked tabs (`_MoveTab`) did not redraw the wedge; `_DrawWedges()` made one rectangle and one colour lookup per pixel.

What changed (`R5ModernDecorator.cpp`): the wedge is the same in both modes (every pixel the arc does not fully cover); in the CPU mode the decorator paints the pixels at least half covered in the plain edge colour, the same line Window paints as its rim; `RegionAt()` gives every wedge pixel to the tab (`_InWedge()`); `_MoveTab()` adds the top strip to the update region; the wedge is filled one run per row.

Also noted by the review, not changed (owner's choice, in the PR's "Open keuzes"): the active tab without glass, floating windows with the full card, the 12 % lighter tab colour, the edge colour over the panel colour. `DecorInfo` (libbe) now returns the add-on's own name from `Name()` and "Classic (Haiku)" for the built-in decorator: a visible change in a libbe class, no ABI change; "Default" and the file names still find the decorators.

Tested: see the step C entry and the PR (QEMU run 6 on build 9).

