# Fast user switching (design, 25 Nov 2026)

Status: design only. Nothing here is built. Building starts after the owner's go.
Mockup: `fus-mockup/` (see its README). Lead issue: GitHub #63.

## 1. What the owner chose (25 Nov 2026)

1. **Apps keep running.** While someone else uses the computer, the first person's apps keep running: downloads, music and builds go on.
2. **Where people switch.** "Switch user…" in the Deskbar menu, next to "Lock screen" and "Log out". "Other account" on the lock screen. Both lock the current session and open Sign in.
3. **Shutdown or restart while others are signed in.** A warning lists who is signed in. Only an administrator may go on, and the others' unsaved work is lost.
4. **The look.** The Sign in window, the lock window and the account cards that R5 already ships. Nothing new is invented where R5 already has a part. On the lock screen the subtitle "Your apps stay open." goes; the title is only "Locked".

## 2. Rule for this design: use what R5 already has

| Need | What R5 already has, and what this design uses |
|---|---|
| Sign in | `distro/login/LoginView.cpp`, its cards (`BUserCard`, two to four accounts) and list (`BUserListItem`, five or more), `BPasswordControl`, the bottom row with clock, Restart and Shut down |
| Lock | `distro/login/LockWindow.cpp` (the same window, the banner with the person, Unlock, Log out, Restart, Shut down) |
| A program asks Login for something | `r5_admin_send()` to Login's admin port; the kernel names the sender (`docs/LIBR5.md`, "Admin requests"). Deskbar's Lock and Log out already go this way (`BarApp.cpp:960-1021`) |
| Administrator approval | The administrator lock: `r5_admin_query()`, `r5_admin_prompt_unlock()`, the 5-minute grant (`docs/LIBR5.md`, "Administrator lock"). No new password window |
| Glass and the classic look | `window_effects_active()`, `set_window_glass()`, the themes (`set_theme()`); Safe mode draws the classic look |
| The guest | `r5_admin_is_system_guest()`, the guest menu in Deskbar, the wipe at log out |
| One desktop per account | Haiku's own design: app_server keeps a list of desktops and looks one up by uid (`AppServer::_FindDesktop`, `AppServer.cpp:251-268`); launch_daemon keeps sessions per uid (`LaunchDaemon.cpp:146`) |
| A desktop that does not draw for a while | The compositor's pause for direct screen access (`Compositor.h:65`, `IsActive()`), and Haiku's `B_DIRECT_STOP` for `BDirectWindow` |
| Libraries | Additions go at the end of `libr5ui`, `libr5admin` and the private app_server protocol (`AGENTS.md`, "LibR5"). Nothing is added to `libbe`; a Haiku program that links only `libbe` keeps its ABI |

New parts are only where nothing exists: handing the screen and input from one desktop to another, and a "signed in" mark on the account cards.

## 3. How R5 works today (researched, 25 Nov 2026)

Every claim has a file and line. `haiku/` is the Haiku tree with the `distro/` copies laid over it; `AppServer.cpp`, `Desktop.cpp` and `LaunchDaemon.cpp` are the `distro/` copies.

### 3.1 One desktop for everyone

- launch_daemon runs only `autologin`, which starts one session for uid 0 (`haiku/src/bin/autologin.cpp:27`). That session starts app_server and Login (`haiku/data/launch/user:1-4`).
- Login (uid 0) gets the first desktop. A person's apps are started by `r5-desktop`, which drops to the person's uid (`distro/login/R5Desktop.cpp:171-256`). When those apps ask app_server for their desktop, there is none for their uid, and an R5 fallback gives them the first one (`AppServer.cpp:141-162`). So Login's windows and the person's windows share one desktop.
- The lock screen is a window and a full-screen cover on that same desktop (`LockWindow.cpp:384-437`).
- The signed-in person is one name in one file, `/boot/system/settings/r5-desktop-session` (`AccountAdmin.cpp:56`, `:1159-1225`).
- Log out quits, and after 3 s kills, every app of every uid except Login's own helpers (`LoginApp.cpp:1341-1407`, `keep_team` `:57-95`).

### 3.2 app_server

- **Desktops.** `fDesktops` is a list behind a lock; each desktop names its looper and shared area by uid (`Desktop.cpp:584`, `:2900`). Several desktops are possible in principle.
- **Screen.** `VirtualScreen::SetConfiguration` calls `ScreenManager::AcquireScreens` (`VirtualScreen.cpp:83`), which only hands out a screen without an owner (`ScreenManager.cpp:132`). A second local desktop gets no screen and fails `Init` (`Desktop.cpp:606`). Screens come back only when a `VirtualScreen` is reset or deleted (`ScreenManager.cpp:162-177`). There is no hand-over. A remote target (`RemoteHWInterface`, `ScreenManager.cpp:139-156`) is the one precedent for a desktop that is not on the local screen.
- **Input.** One pool of input streams (`AppServer.cpp:64`). input_server registers one stream (`ServerApp.cpp:622-637`) and has one target port (`InputServer.cpp:447`, `:471`). The first desktop takes it (`Desktop.cpp:676-683`); a stream in use is not shared (`InputManager.cpp:53-69`). A second desktop would get no events.
- **No switching.** There is no "activate desktop" code. The only activate messages are for workspaces, apps and windows.
- **Reading the screen.** `AS_READ_BITMAP` reads the desktop's screen (`ServerApp.cpp:3529-3560`). `AS_GET_ACCELERANT_PATH` and `AS_DIRECT_SCREEN_LOCK` (`ServerApp.cpp:3563`, `:3595`) give direct screen access, with no uid check found.

### 3.3 The GPU drawing path (M11 compositor, M12 Skia)

- **One compositor per desktop.** `Desktop::Init` calls `Compositor::Create(this, HWInterface)` (`Desktop.cpp:700`); `~Desktop` deletes it (`:554`). Each compositor makes its own `VkInstance`, `VkDevice` and queue (`CompositorVulkan.cpp:71`, `:207`, `:218`). Skia Graphite is built on that device (`Compositor.cpp:399-411`, `SkiaGpu.cpp:904`).
- **No swap chain.** The compose writes a frame and copies it into the screen's back buffer, or the GPU writes scanout directly (`Compositor.cpp:2633-2656`).
- **Process-wide state** that assumes one compositor:
  - `SkiaGpu::sCurrent` (`SkiaGpu.cpp:916`, set at `Compositor.cpp:431`), used by offscreen `BBitmap` drawing (`SkiaOffscreen.cpp:160`, `:339`);
  - `sTracked` (`SkiaOffscreen.cpp:40`) and `sCpuEngines` (`SkiaDrawingEngine.cpp:43`);
  - GPUCopy's render-node fd, scanout handle and off switch (`GPUCopy.cpp:51-106`);
  - `setenv("LP_NUM_THREADS")` (`Compositor.cpp:295`);
  - the radeon_hd settings, read for app_server's own account (`Compositor.cpp:76-100`);
  - the render node `/dev/gpu/radeon_hd/0`, written into the code (`Compositor.cpp:176`, `:344`, `GPUCopy.cpp:123`, `:314`).
- **No hidden state.** The compose thread always presents to the screen (`Compositor.cpp:2626-2663`). The only state that does not draw is the pause for direct access (`Compositor.h:65-78`).
- **Memory.** The compositor's budget is GTT/2 on RADV (`Compositor.cpp:332`) and RAM/4 on Lavapipe (`:362`). The kernel counts VRAM and GTT per open file (`r5_gpu_core.cpp:1050-1102`): GTT is RAM/4 per file and RAM/2 for all files together. There is no limit per uid.
- **Software path.** The CPU mode is per compositor, so per desktop (`Compositor.cpp:2821-2868`). Safe mode and `gpu_compositor false` are checked each time a compositor is made (`Compositor.cpp:242-250`).
- **GPU programs of different people.** Each open file of the render node gets its own address space (`r5_gpu_core.cpp:2776-2786`; tested on stick 32, `GPU-ARCHITECTURE.md:247`). Import and scanout are only for app_server (euid 0) (`r5_gpu_core.cpp:2951-2973`). The isolation is by address space, not by uid.

### 3.4 registrar, launch_daemon and the other servers

- **registrar.** There is one for everyone (`data/launch/system:1`).
  - The roster has no uid split. A lookup by signature returns the first match (`TRoster.cpp:653-718`), and the app list holds every team (`:725-748`).
  - There is one active app (`:707`).
  - `B_EXCLUSIVE_LAUNCH` is checked for the whole system (`TRoster.cpp:224-232`). Deskbar and Tracker are exclusive (`Deskbar.rdef:17`, `Tracker.rdef:17`), so a second person's Deskbar would hand its arguments to the first person's copy (`Application.cpp:459-471`).
  - Clipboards are keyed by name only (`ClipboardHandler.cpp:31`), so the "system" clipboard is shared by every uid.
  - Shutdown quits every app of every uid (`ShutdownProcess.cpp:1371`, `:1613`).
- **launch_daemon.** It keeps a map of sessions per uid (`LaunchDaemon.cpp:146`, `:255`), but only uid 0 may start one (`:995`), and a session is never removed (`:1023`). R5 does not start a launch_daemon session per person on purpose (`docs/MULTIUSER.md:12`, `distro/TODO.md:44`). A session per person would start a second app_server, because app_server is in the user launch file.
- **Media.** There is one media_server and one media_addon_server for the system (`data/launch/system:32`), found by global port name (`DataExchange.cpp:39`, `:50`).
- **input_server.** One, with one target (above).
- **Notifications.** One notification_server for the system (`data/launch/system:63`). It reads the settings of its own uid (`NotificationWindow.cpp:378`).
- **Key store.** One `keystore_server` at a time, found by signature (`KeyStore.cpp:419-427`). Login runs it as the signed-in person (`LoginApp.cpp:900-959`) and stops the previous one first (`:922`). Only Login (uid 0) may unlock it (`KeyStoreServer.cpp:916-918`). Client apps are not checked against the store's owner.

### 3.5 Deskbar and Tracker

- `BDeskbar` finds "the" Deskbar by signature (`Deskbar.cpp:40`, `:62`, through `Messenger.cpp:319-340`).
- The team list shows every team of the system: at start (`BarApp.cpp:189-200`), in "Show desktop" (`:1032-1046`), and in `AddTeam` (`:1304-1330`). The front team comes from the one active app (`:177-181`).
- Settings and replicants are already per person, through `find_directory` (`BarApp.cpp:422`, `StatusView.cpp:121`, `:566-595`).
- System servers talk to "the" Deskbar for their tray icon: AirPlay (`AirPlayServer.cpp:686`, `:719`), Bluetooth (`BluetoothServer.cpp:1710`), Login's tray (`login/TrayApp.cpp:50-55`).
- Login waits for Tracker and Deskbar by signature at sign-in and log out (`LoginApp.cpp:528-530`, `:700-712`, `:1411-1437`).

### 3.6 Shared and the guest

- At every administrator sign-in, Login makes that administrator the owner of `/boot/home/Shared` (`AccountAdmin.cpp:1215-1222`, `own_shared_folder` `:1258`). The comment relies on "one graphical session" (`:1252-1256`).
- The guest check and the guest wipe scan every team (`AccountAdmin.cpp:142-154`; `LoginApp.cpp:278-279`, `:512-513`, `:716-719`).
- Lock and Log out are allowed for uid 0 or for the uid in the session file (`LoginApp.cpp:215-241`).

## 4. How other systems do it (prior art)

| System | Screen and drawing | Input | Sound |
|---|---|---|---|
| Linux (systemd-logind, GDM) | One compositor per person on its own virtual terminal; the greeter is a session of its own. A paused session loses DRM master but keeps its file open | logind revokes the input devices of a paused session (`EVIOCREVOKE`) | logind moves the sound device rights to the active session; one PipeWire per person. Known problem: the hidden person's PipeWire keeps the device busy |
| Windows | A session per person, services in session 0. Fast user switching uses one display driver instance per session over one hardware driver; a DWM per session (from a blog, not Microsoft) | Per session (window station) | Per session |
| macOS | loginwindow does the switch; each login session is separate, and notifications between programs stay inside a session. Whether WindowServer is per session: no official source found | Per session | Per session |

Sources:
- https://learn.microsoft.com/en-us/windows-hardware/drivers/display/fast-user-switching
- https://techcommunity.microsoft.com/blog/askperf/sessions-desktops-and-windows-stations/372473
- https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPMultipleUsers/Concepts/FastUserSwitching.html
- https://lists.freedesktop.org/archives/systemd-devel/2013-August/012907.html
- https://gitlab.gnome.org/GNOME/gdm/-/issues/602
- https://bugs.gentoo.org/801601

What all three share, and what R5 takes:
- The screen belongs to the active session only.
- A hidden session gets no input.
- Sign in and the lock run apart from the people's sessions.
- Sound follows the active session.

## 5. The design

### 5.1 Where the desktops live: one app_server, one desktop per person

There are three options:

| Option | For | Against |
|---|---|---|
| **A. One app_server, one desktop per person** (recommended) | Haiku's own design (`fDesktops`, `_FindDesktop` per uid). The compositor is already per desktop. One process owns scanout and import, which fits the euid 0 rule of the GPU driver (`r5_gpu_core.cpp:2957`). The R5 window calls stay as they are | The screen and input need a hand-over. The process-wide drawing state (3.3) has to become per desktop or shared on purpose |
| B. One app_server per person | Process isolation, and the per-process state already fits | Two processes would open the same accelerant (`AccelerantHWInterface.cpp:297`). The GPU driver has no hand-over like DRM master. The registrar knows one app_server (`TRoster.cpp:1404`). A new kernel interface would be needed, against the rule that the R5 GPU interface stays small and additive |
| C. One app_server that composes only the active desktop, with the others parked | Close to A | This is A with the hand-over in 5.2. Not a separate option |

**Design: A.** Login keeps its own desktop for uid 0: the Sign in desktop, like GDM's greeter. Each person gets their own desktop for their uid.

The R5 fallback that sends a new uid to the first desktop (`AppServer.cpp:141-162`) stays for programs that are not part of a signed-in session, such as a system server opening a window. A signed-in person's apps get their own desktop, which Login asks app_server to create when the person signs in.

### 5.2 Handing over the screen and input

New in app_server, as additions to the private protocol. Only Login may send them, checked by euid 0 and team, as the GPU driver checks its importer:

- **Activate desktop (uid).**
  1. The shown desktop stops: its compositor goes into the paused state that already exists for direct access, and `BDirectWindow` programs get `B_DIRECT_STOP`.
  2. It gives the screen back (`ScreenManager::ReleaseScreens`), and its `VirtualScreen` points at an offscreen target, as `RemoteHWInterface` does for a remote desktop.
  3. Its `EventDispatcher` gives back the input stream.
  4. The new desktop takes the screen and the stream, its compositor draws everything once, and `BDirectWindow` programs get `B_DIRECT_START`.
- **What a hidden desktop does.** Programs go on drawing into their own window buffers. Nothing reaches the screen. `AS_READ_BITMAP` reads that desktop's own content, never the shown screen. Direct screen access (`AS_DIRECT_SCREEN_LOCK`, `AS_GET_ACCELERANT_PATH`) is refused while the desktop is hidden.
- **Input.** input_server keeps its single target, app_server. app_server gives events only to the shown desktop. A hidden desktop gets no keys or mouse, so no program in it can type into the shown one.
- **The screen mode** stays one setting for the machine (Display is behind the administrator lock). Every desktop follows it when it is shown.

### 5.3 The GPU and the software path

- **One Vulkan device for all desktops** (open question Q1, recommended). app_server makes the device and Skia context once and shares them between the desktop compositors. `SkiaGpu::sCurrent`, `sTracked`, `sCpuEngines` and GPUCopy's state then stay valid. The memory budget (GTT/2, RAM/4) is for app_server as a whole, not doubled per desktop.
- **A hidden desktop** keeps its window buffers, which take memory, but its compose thread does not run. Switching back draws everything once.
- **Software path.** Lavapipe and Painter work the same way. Safe mode stays the CPU mode for every desktop. Nothing changes the rule that the software path always stays installed and working.
- **Not changed:** the GFX9 ring, the kernel stage, the PSP, the firmware, the reset and the stop at shutdown (`AGENTS.md`, "GFX9 ring"). The render node per program, and the isolation per open file, stay as they are.
- **Also needed:** the render node `/0` written into the code (`Compositor.cpp:176`, `:344`, `GPUCopy.cpp:123`) is a single-GPU assumption, not a single-session one. It is not part of this work.

### 5.4 The registrar: lookups per person

Additions in the registrar. `libbe`'s ABI stays as it is.

- **Lookup by signature.** When the team that asks has a uid, the registrar first looks among apps of that uid (`TRoster.cpp:653-718`). `BDeskbar`, `BMessenger(signature)` and `be_roster->IsRunning()` then find the asker's own Deskbar and Tracker without any change in programs.
- **`B_EXCLUSIVE_LAUNCH` per uid** (`TRoster.cpp:224-232`). Each person gets their own Deskbar and Tracker.
- **The app list and the active app per uid.** For a person's team, `GetAppList` lists only that uid's teams; uid 0 still sees everything. The active app is kept per desktop.
- **The clipboard per uid** (`ClipboardHandler.cpp:31`): no copy and paste between people, as on the other systems.
- **Shutdown** quits every uid, as now. The warning comes before it (5.7).

Haiku programs keep working unchanged; they see only their own person's apps. That is the change in behavior, and docs/MULTIUSER.md records it.

### 5.5 Login

- **Session records.** The one file `r5-desktop-session` becomes one record per signed-in person, root-owned like it is now. `_AdminRequest` (`LoginApp.cpp:215-241`) allows Lock, Switch user and Log out for the uid of the session it acts on.
- **Sign in** starts the person's desktop (5.1) and `r5-desktop` as now, then activates that desktop.
- **Lock** activates Login's own desktop and shows the shipped `LockWindow` there. A lock is no longer a cover over the person's own desktop. Their apps are not on the screen and get no input.
- **Switch user** (Deskbar) and **Other account** (lock screen): Login locks the session and shows Sign in on its own desktop. They are not shown when there is only one account, and they are not in the guest's menu.
- **Sign in** marks people who are signed in. The cards and list rows get a "Signed in" line and a green dot. These are additions at the end of `BUserCard` and `BUserListItem` in libr5ui, using the reserved slots of `BUserCard`. Picking a signed-in person asks for the password and returns to their desktop; the button then says "Unlock".
- **Log out** quits and stops only that person's apps. `_QuitUserApps` and `keep_team` (`LoginApp.cpp:57-95`, `:1341-1407`) filter by uid. The person's desktop is closed and its screen buffers freed.
- **The key store:** one `keystore_server` per signed-in person. A client finds its own by the uid lookup (5.4). Login's unlock goes to that person's store.
- **The lock screen text:** the title is only "Locked" (owner, 25 Nov 2026); the subtitle line goes.

### 5.6 Deskbar and Tracker

- Each person has their own Deskbar and Tracker, started by `r5-desktop` as now, made possible by 5.4.
- "Switch user…" is added to the shipped menu block after "Lock screen" (`DeskbarMenu.cpp:398-425`). It goes to Login through `r5_admin_send()` like Lock.
- The team list, "Show desktop" and the front app use the registrar's list for that uid (5.4). Deskbar's code does not change for this.
- **Tray icons from system servers** (AirPlay, Bluetooth, Login's tray) are added to each person's Deskbar when that Deskbar starts. The servers' own "Deskbar is back" watch already does this for one Deskbar; it is extended to every Deskbar.

### 5.7 Shutdown and restart

- When people other than the one asking are signed in, the shutdown goes through Login first. This applies to the Deskbar menu, the lock screen and Sign in.
- **An administrator** sees the warning in the mockup: who is signed in and how many apps they have open. They can Cancel or "Shut down anyway" ("Restart anyway").
- **A standard person** sees the same warning with "Shut down anyway" gray, and "Unlock…". That is `r5_admin_prompt_unlock()`, the same lock as in Settings, and the grant allows going on.
- After the yes, the shutdown runs as now: the registrar quits every app, and then the GPU stop at shutdown runs unchanged.

### 5.8 Sound

- There is one media_server, as now. Apps of a hidden person keep running.
- **Recommended (Q2):** their sound is muted while their desktop is hidden, and comes back when they return, as on Linux and macOS. The media_server mutes by uid, using the team's uid, at the mixer input of each team.

### 5.9 Notifications

There is one notification_server. A notification of a hidden person is kept and shown when they return (Q5). It is not shown on another person's screen.

### 5.10 Shared and the guest

- **Shared.** The owner rule stays: Login hands Shared to the administrator whose desktop is shown, now at every switch as well as at sign-in (Q4).
- **The guest** (Q3, recommended): the guest may be one of several sessions. The guest's wipe stays at its log out, and also runs at shutdown. The guest check and wipe look at the guest uid's teams only, as now.

### 5.11 Memory

- Each signed-in person costs their apps, plus their Tracker and Deskbar, plus the window buffers of their desktop.
- There is no fixed limit on the number of sessions (Q6). The memory use of a second session is measured in QEMU in step 3.

### 5.12 ARM64 and RISC-V

Nothing here is specific to a CPU. The GPU part concerns RADV and Lavapipe. On ARM64 and RISC-V in QEMU, the switch runs on the software path. That is the first test there.

## 6. Security of hidden sessions

| Risk | Answer |
|---|---|
| A hidden program reads the screen | `AS_READ_BITMAP` reads its own desktop. Direct screen access is refused while hidden (5.2) |
| A hidden program types into the shown session | Input goes only to the shown desktop (5.2). Synthetic input from a program (`BRoster`, input_server add-ons) stays tied to the desktop of the sender's uid |
| Clipboard between people | A clipboard per uid (5.4) |
| A person sends to another person's Deskbar, Tracker or key store | Lookups by signature find the asker's own (5.4). The key store checks the client's uid against its owner |
| Somebody activates another person's desktop | Only Login (uid 0, its own team) may activate a desktop, and only after a password (Sign in or Unlock) |
| Lock bypass | The lock is on Login's desktop. The person's desktop is not on screen while locked |
| GPU buffers of another person | No change: an address space per open file, import only for app_server (3.3) |
| uid 0 services | Login, launch_daemon, the registrar and app_server stay uid 0. No new service runs as root |

## 7. Steps (each one a small PR, tested in QEMU or VirtualBox)

| Step | What | Test |
|---|---|---|
| 0 | Lock screen title "Locked", without the subtitle | Jam only |
| 1 | registrar: lookup, exclusive launch, app list and clipboard per uid (5.4) | QEMU: two accounts, one over SSH; Tracker and Deskbar of each; clipboard not shared |
| 2 | app_server: one Vulkan device and Skia context shared between desktops; the process-wide state made safe for several desktops (5.3) | QEMU (Lavapipe), then the 5625U (RADV) on the next stick |
| 3 | app_server: activate desktop, a hidden desktop (no screen, no input, no read-back, no direct access) (5.2) | QEMU: two desktops by a test program as uid 0; memory measured |
| 4 | Login: session records, a desktop per person, lock on Login's desktop, Switch user and Other account, "Signed in" on the cards (5.5, 5.6) | QEMU and VirtualBox: two people, switch both ways, lock, wrong password |
| 5 | Log out per person, the key store per person, tray icons in every Deskbar (5.5, 5.6) | QEMU: log out one while the other keeps running; Wi-Fi password store of each |
| 6 | Shutdown warning and the administrator grant (5.7); Shared at a switch; the guest (5.10) | QEMU |
| 7 | Sound of a hidden session muted (5.8); notifications kept (5.9) | VirtualBox with sound |
| 8 | Docs: `docs/MULTIUSER.md` (no longer "one graphical session"), R5Book accounts, `docs/LIBR5.md` for the card additions | — |

Steps 2 and 3 change the GPU drawing path, so they need a boot on the 5625U. That boot is the stick test of the milestone that takes them, not a stick of its own.

## 8. Decisions of the owner (25 Nov 2026)

The six open questions were answered by the owner on 25 Nov 2026. Each took the recommended choice.

- **Q1. Vulkan device.** One device shared by all desktops. Memory is not doubled, and Skia's state stays valid.
- **Q2. Sound of a hidden person.** Muted until they return, as on macOS and Linux.
- **Q3. The guest.** May be one of several sessions. The wipe still runs at its log out.
- **Q4. Shared with two administrators signed in.** Shared goes to the administrator whose desktop is shown, the same rule as now.
- **Q5. Notifications of a hidden person.** Kept, and shown when they return.
- **Q6. How many sessions.** No fixed limit. Step 3 measures the memory per desktop; a limit is set only if that measurement asks for one.

## 9. Found on the way (not part of this work)

- `docs/LOOK.md:20` still says "There is no compositor".
- `docs/GPU-ARCHITECTURE.md:74` says `gfx9_vmid_per_file` is on by default; `docs/GRAPHICS.md:330` says off. One of them is wrong.
- The render node `/dev/gpu/radeon_hd/0` is written into the compositor and GPUCopy. That is a single-GPU assumption, for the Intel track.
