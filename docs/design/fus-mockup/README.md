# Fast user switching mockup (25 Nov 2026)

The design is `../FAST-USER-SWITCHING.md`. The mockup is built on the windows R5 already ships: the Sign in window (`distro/login/LoginView.cpp`), the lock window (`distro/login/LockWindow.cpp`) and the account cards (`BUserCard`, libr5ui). A dashed blue outline marks what fast user switching adds.

| File | What it shows |
|---|---|
| `fast-user-switching.html` | The four scenes, with a switch between glass and the classic look (Safe mode). `#lock`, `#signin`, `#shutdown` or `#signin-classic` after the file name opens that scene |
| `menu.png` | The Deskbar menu with "Switch user…" after "Lock screen" |
| `lock.png` | The lock window, title "Locked" only, with "Other account" |
| `signin.png` | Sign in with two people signed in (green dot, "Signed in") |
| `shutdown.png` | The shutdown warning for an administrator and for a standard account |

The PNG files are drawn with Edge at 1190 by 790. The page opens in a browser without a server and uses the icons of `../login-mockup/icons/` and `../settings-mockup/icons/`.
