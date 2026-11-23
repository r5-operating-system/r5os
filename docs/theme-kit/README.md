# Theme Kit

The pieces a theme maker needs (R5Book `docs/r5book/making-themes.html`,
`docs/LIBR5.md` "Themes"):

- `Evening`: a sample theme file (format 1). Copy it to
  `~/config/settings/r5/themes/` and it is a card on the Theme page of
  Settings.
- The control look template:
  `distro/desktop/src/add-ons/control_look/ExampleControlLook/`. The system
  build compiles it; it is not installed.
- The calls: `<Themes.h>` in `libr5ui.so`, namespace `BAppearanceKit`:
  `count_themes()`, `get_theme_at()`, `find_theme()`, `get_theme()`,
  `set_theme()`, `save_theme()`, `remove_theme()`, and the `B_THEME_CHANGED`
  message.

MIT, like all R5 code.
