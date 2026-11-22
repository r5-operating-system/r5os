# Power mockup (22 Nov 2026)

The design the owner chose for Settings → System → Power. The contract is `docs/POWER.md`.

| File | What it shows |
|---|---|
| `power.html` | The open page. The bar at the top switches between the glass look (GPU, or Lavapipe without one) and the classic look (Safe mode) |
| `power-glass.png`, `power-classic.png` | The same page, drawn with Edge at 1190 by 812 |

The HTML page opens in a browser without a server. It uses the icons of `../settings-mockup/icons/`.

## What the mockup decides (owner, 22 Nov 2026)

- **Pages.** One Power page under System, with Battery, Sleep, Performance and Wake. There is no separate Sleep page.
- **Layout.** Battery with Sleep under it on the left; Performance with Wake under it on the right.
- **Brightness.** On this page, not on Display, and not behind the lock.
- **Scope.** Machine-wide, behind the administrator lock. Battery and brightness are open to everyone.
- **Before sleep works.** Sleep and Wake stay visible but gray, with a short note. "Turn off the display after" and the lid's "Turn off the display" work.
- **Icons.**
  - The page icon is Haiku's `App_PowerStatus` with the preferences wrench. The mockup draws a stand-in, because a browser cannot draw HVIF.
  - The Battery box uses `R5_LEVEL_BATTERY` from `libr5icons.so`.

## Not drawn yet

- Dark mode.
- A computer without a battery or a lid.
- The menus opened.
- The page with the lock closed.
