# MediaPlayer redesign brief

The owner chose this direction on 18 Nov 2026, question by question. The sketches are at https://claude.ai/artifact/6quozYShD1KSPkGytTJ93d. They show the direction only. They are not a pixel spec. Sizes and colors come from Haiku spacing, `be_plain_font` and `ui_color()`, not from the sketches.

## Before you start

1. Read `AGENTS.md` and confirm its rules in your first reply. The sections that matter here are LibR5, Overlays and native source, Licensing, Code style, and Development loop.
2. Read these docs. For glass, R5Book `appearance-effects.html` is the reference:
   - `docs/LIBR5.md`
   - `docs/APPEARANCE-KIT.md`
   - `docs/ICONS.md`
   - `docs/STYLE.md`
   - `docs/LOOK.md`
   - R5Book (`docs/r5book`)
   - one Settings page in `distro/settings/`
3. Read the current player in `haiku/src/apps/mediaplayer`. On Windows those files are skip-worktree, so read them with `git show HEAD:<path>`. Read:
   - `MainWin.cpp`
   - `ControllerView.cpp`
   - `InfoWin.cpp`
   - `interface/`: `TransportControlGroup`, `SeekSlider`, `VolumeSlider`, `PeakView`, `SymbolButton`, `PlayPauseButton`, `DurationView`
   - `supplier/MediaTrackVideoSupplier.cpp`, for how cover art arrives
4. Work one step at a time. Ask before you jam, commit, push or open a PR.

## The choices

| Topic | Choice |
|---|---|
| Feel | A mix. A normal window feels like Haiku. Video may look modern. |
| Menu bar | Stays: MediaPlayer, Audio, Video, Attributes. Every item and shortcut stays. |
| Controls with video | In a window: a fixed bar under the picture, always visible. In full screen: a bar floating over the picture that hides after a few seconds without mouse movement. |
| Color of the controls | Glass everywhere (window and full screen) in the GPU mode, done the R5Book way. Without the GPU: the plain theme panel (light or dark). |
| Music only | The album cover. Without a cover: the real Haiku icon of the file type. Beside it: title in bold, then artist and format. |
| Buttons | All the same size: Previous, Play/Pause, Next, Stop, and Full screen with video. Line symbols. |
| Seek bar | Thin at rest. Thicker with a round grip while the mouse is over it. |
| Peak meter | Not shown by default. A new setting turns it on. |
| File info | A card at the top with the cover or the icon and the name, then the label/value list. |

## Where the code goes

- Copy only the MediaPlayer files you change into `distro/apps/mediaplayer/src/apps/mediaplayer/...`, at the same relative path.
- Keep the Haiku MIT header and copyright. Mark the R5 parts with SPDX-Snippet tags: copyright 2026 R5OS Contributors, `MIT`.
- Add `scripts/apply-mediaplayer.py`. It only copies those files and fails loudly when a source is missing, like `scripts/apply-terminal.py`. Hook it into `scripts/apply-overlay.sh`.
- No string patches (`replace_once`) and no new overlays.
- If MediaPlayer links `libr5ui.so` or `libr5icons.so`, change the copied `Jamfile` too.

## Reuse first, then shared

Look in this order and use the first one that fits: libr5ui and libr5icons first, then the Haiku kits, then Haiku's own apps.

| Part | Use |
|---|---|
| Glass behind the menu bar, controls and now playing area | Exactly as R5Book describes it (`docs/r5book/appearance-effects.html`, "Window effects"; the Wi-Fi connect window in `docs/r5book/wifi-kit.html` is the example). Call `BAppearanceKit::set_window_glass(this, true)` in the window constructor, as soon as the `BWindow` exists. The views keep their normal theme view color (`B_PANEL_BACKGROUND_COLOR`); app_server makes it see-through. Draw text and glyphs with `B_OP_ALPHA` and `B_ALPHA_COMPOSITE`; everything else stays opaque. Do not draw a fill of your own and do not use `B_TRANSPARENT_COLOR`. In the CPU mode, or when the user turned frosted glass off, the calls change nothing and the window is the plain theme panel; that is the "follows the theme" choice, with no extra code. The video view stays opaque. |
| Full-screen bar | Glass blurs what is behind the window, not the window's own video. So the bar is its own small borderless window (`B_NO_BORDER_WINDOW_LOOK`, floating in the player's subset) over the bottom of the full-screen picture, with `set_window_glass(this, true)` like above. In the CPU mode it is the plain theme panel. If a separate window cannot follow the full-screen window correctly, stop and report it; do not fake the blur. |
| File type icon (music without a cover, File info) | `r5_file_icon_bitmap()` / `r5_mime_icon_bitmap()` |
| File info heading | `BAppearanceKit::BCard` (`<Card.h>`) |
| Accent blue (seek and volume fill) | `B_STATUS_BAR_COLOR` |

**Transport symbols.** Haiku has no HVIF icons for these. Add line glyphs to `libr5icons.so`:

- At the end of `enum r5_glyph`, after `R5_GLYPH_TRIANGLE` (11).
- On the same 24 grid with round line ends.
- Glyphs: play, pause, stop, previous, next, speaker, speaker muted, full screen, leave full screen.
- Give each one a row in the `docs/ICONS.md` table and a line in `docs/LIBR5.md`. They are MIT, and they do not change once shipped.
- They replace MediaPlayer's own `BShape` symbols in `TransportControlGroup`.

**Shared helpers.** A helper that a second program will use goes in libr5ui:

- MIT, added at the end.
- Described in `docs/LIBR5.md` and the `docs/APPEARANCE-KIT.md` table in the same change.
- Candidate: the thin slider that grows under the mouse. Its second users would be a Sound page and volume controls.
- A helper used only by MediaPlayer stays in MediaPlayer.
- Do not add a class that only renames a `libbe` class.
- Nothing goes in `libbe`.

## Details

1. **Window.** The R5 frame (yellow tab, 1px line) and the menu bar stay. Under the menu bar the layout depends on what plays:
   - **Music:** the now playing area, then the controls.
   - **Video:** the picture, then the controls.
2. **Cover art.**
   - The ffmpeg reader already gives embedded cover art as a single-frame video track. `MediaTrackVideoSupplier` has the "single frame video (cover art)" path.
   - For an audio file whose only video track is such a single frame, show that frame as the cover in the now playing area, not as a video.
   - Without one, show the type icon.
   - Do not add a new decoder or tag library. If cover art cannot be told apart from video with what the reader gives, stop and report it.
3. **Now playing.**
   - The title in bold.
   - One line under it: artist (`Audio:Artist` or the stream's metadata, when present) and the format.
   - Text that is too long is cut with an ellipsis.
4. **Seek bar.**
   - Elapsed time at the left and remaining time at the right, in tabular numerals.
   - At rest it is a thin track with an accent fill.
   - When the mouse enters, it grows, and a round grip appears. It shrinks again when the mouse leaves.
   - Keep the position tooltip, click-to-seek, dragging and keyboard seeking.
   - The bar's row keeps its height, so nothing jumps when it grows.
5. **Buttons.**
   - One row of equal buttons.
   - The rewind and forward buttons stay wherever the current code uses them (`useWindButtons`), with the same size.
   - At the right: the speaker (a click mutes, and the glyph shows the muted state) and a thin volume slider. The slider keeps the current dB range and exponent.
6. **Peak meter.**
   - Hidden by default.
   - New checkbox "Show peak meter" in the "View options" box of the player's Settings window. It is off by default, is stored with the other `mpSettings`, and applies to open windows at once.
   - When it is on, the meter is a slim pair of lines (left and right) beside the volume slider, in the accent blue, red at clipping.
   - `MainWin.cpp` calls `fControls->GetPeakView()` and uses it without a check (`SetPeakListener`). Handle `NULL` there, and attach or detach the listener when the setting changes.
   - At small widths the meter is dropped first.
7. **Full screen.**
   - No tab and no menu, as now.
   - The bar floats over the bottom of the picture: the seek line with times, the buttons, volume, and leave full screen.
   - It hides after about 3 seconds without mouse movement and comes back on movement. The pointer hides with it.
   - With `B_WINDOW_ANIMATIONS` it fades. Without it, it hides at once.
   - Keep "Scale controls in full screen mode" from the player's settings working.
   - Hide interface in a normal window stays as it is now: no menu and no controls.
8. **File info.**
   - A `BCard` heading with the cover (or the type icon), the file name in bold, and one line such as "Video · 3:37".
   - Under it, the list follows `docs/STYLE.md`: labels right-aligned in one column, values left-aligned in the next.
   - Keep every field that is there now.
   - The location may wrap.
   - File info is a plain panel, not glass.
9. **Theme and size.**
   - Colors come from `ui_color()`. Sizes come from the font.
   - Check light and dark, at font size 12 and 18.

## Must keep

- Every menu, menu item and keyboard shortcut, including the right-click menu on the video.
- The playlist window.
- Drag and drop.
- The player's Settings window.
- Subtitles.
- Network stream.
- Scripting, including the Album property.
- Resume.
- Every string goes through `B_TRANSLATE`. New strings are allowed.
- **Small windows.** `_GetMinimumWindowSize()` / `_SetWindowSizeLimits()` still give a usable player. At the smallest width, drop the peak meter (if on) first, then the artist line, then shorten the volume slider, then drop Stop. The seek bar and Play/Pause are always visible.

## Out of scope

- New playback features beyond the cover and the peak meter setting.
- The media node framework.
- The playlist look.
- Settings, the decorator, other apps.
- Anything from `docs/ROADMAP.md`.

## Check

- Jam only, with `scripts/build-locked.sh` from your own worktree.
- No QEMU screenshot series and no stick.
- After the apply, grep the tree: the copied files are the R5 versions, and nothing is doubled.
- Report in this order: **Veranderd / Getest / Onbewezen / Documentatie**. Say plainly that the look and the glass were not seen on screen.
- Docs in the same change:
  - `docs/ICONS.md` and `docs/LIBR5.md` for new glyphs or classes.
  - `CHANGELOG.md`.
  - A What's new entry, with exactly one `distro/BUILD` bump, only if the owner asks.
- Ask before push or PR. PRs target `main`. Doc changes go in a new PR.
