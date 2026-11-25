# Aomi window mockup (25 Nov 2026)

The design is `../AOMI-UI.md`. Three layouts for the window of Aomi, the R5 browser, on a 1280 by 800 screen. The owner chose option B on 25 Nov 2026.

| File | What it shows |
|---|---|
| `option-a-classic.html` | A: menu bar, tabs, navigation row, bookmarks bar, status line; Downloads as its own window |
| `option-b-compact.html` | B (chosen by the owner, 25 Nov 2026): tabs and one toolbar with one menu button; downloads in a panel |
| `option-c-side-tabs.html` | C: menu bar, one toolbar, tabs in a list on the left like Settings |
| `aomi.css`, `aomi.js` | The shared look and the builder; each option page sets `data-option` |

Each page has buttons for the scenes (page, new tab page, private window, downloads, the menu button in B, the settings form), Light or Dark, Glass (GPU) or No GPU (flat, no blur, no shadow), and Notes or Clean. A hash opens a state directly, for example `option-b-compact.html#menu-dark-flat-clean`.

- The frame is the R5 frame: yellow Haiku tab with close and zoom box, 1 px line. Colours are R5 Modern light (A1) and dark (A2) from `../THEMING.md`.
- Brand colours (sky `#3898ff`, white, navy `#0f2a4a`, a stand-in) appear only in the Aomi mark and the new tab page.
- The web page is a made-up site in its own colours; the engine view is opaque in every state.
- The toolbar icons are line stand-ins. The real window uses the Haiku icons (`R5_ICON_BACK`, `R5_ICON_FORWARD`, `R5_ICON_HOME`, and Haiku vector icons for the rest, chosen with the owner).
- The desktop is `distro/artwork/r5-wallpaper.jpg`. The pages open in a browser without a server; Manrope loads from Google Fonts.
