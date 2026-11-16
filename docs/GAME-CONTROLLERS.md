# Game controllers on R5

Plan and license check, 16 Nov 2026. On the roadmap since 16 Nov 2026 (`docs/ROADMAP.md`, near term). Bluetooth controllers depend on `docs/BLUETOOTH.md`, step 4 (classic HID) and step 6 (BLE).

## 1. Why

A game controller needs other settings than a mouse or a keyboard: a test, the button layout, the battery, later rumble and lights. Haiku has no good place for that: the old Joysticks preferences are deprecated with no replacement ([Haiku #17317](https://dev.haiku-os.org/ticket/17317)). And Haiku does not report a controller's USB vendor and product ID, so SDL games see every pad as a plain joystick, not as a "game controller" with A, B, X and Y. That second gap decides whether games just work.

## 2. How others do it

| System | What | Take for R5 |
|---|---|---|
| macOS (since Ventura) | System Settings → Game Controllers, shown only with a controller connected; swap buttons for the whole system and per app; Xbox or PlayStation layout | A page that appears with a controller; per-app swaps (later) |
| iOS and iPadOS | Settings → General → Game Controller: the same, plus a "buddy controller" (two as one) and rumble on or off (from memory, not checked) | Buddy controller, rumble switch (later) |
| Windows | `joy.cpl` only tests; the Xbox Accessories app swaps buttons, Xbox controllers only; Steam Input | The live test |
| Android | Per game until now; Android 17 (beta) adds Settings → Game Controller, for Bluetooth under the device's details | Controller settings with the device |
| Linux | Drivers per make (`hid-playstation`, `xpad`) split one controller into buttons, touchpad and motion, with rumble and lights in the kernel; the battery shows as a normal battery; every controller reports vendor and product ID, and SDL's GameControllerDB maps that to A, B, X, Y. KDE has a list and a live test (wobbly in Plasma 6.2: two interfaces counted buttons differently); GNOME has nothing | Standard layout from vendor and product ID; the controller battery in Deskbar; one controller, several functions |
| FreeBSD | Drivers, no settings: `hgame(4)` (generic game controllers), `ps4dshock(4)` (DualShock 4, Vladimir Kondratyev, 2020), `xb360gp(4)` (wired Xbox 360); `ps5dsense(4)` (DualSense) is in review for 16.0, not shipped. BSD-2-Clause (from general knowledge, check per file) | The reference for option C (light bar, rumble, touchpad, motion of PlayStation controllers). Linux `hid-playstation` is GPL-2.0 and cannot go into MIT code; FreeBSD code keeps its own BSD header if taken |
| OpenBSD | `ujoy(4)`, a plain USB joystick driver; ISC from general knowledge, not checked | — |
| air/OS | Haiku's old Joysticks preferences only | — |
| RenkuOS ([source](https://github.com/RenkuOS/Source)) | No controller settings. Two joystick fixes by atomozero: `244bf43292` (`libdevice` frees the data of a joystick opened before) and `823649eb6e` (`hid_shared` waits for the joystick update thread) | Take both fixes (MIT, Haiku files) |

The Linux kernel, SDL and iOS lines come from general knowledge, not from a source read for this plan. Sources: [FreeBSD hgame(4)](https://cgit.freebsd.org/src/tree/share/man/man4/hgame.4), [FreeBSD ps4dshock(4)](https://kernelsources.org/source/xref/freebsd/share/man/man4/ps4dshock.4), [OWC on macOS Ventura](https://eshop.macsales.com/blog/84976-customize-your-control-with-enhanced-game-controller-support-in-macos-ventura/), [Xbox Accessories](https://xbox.com/accessories-app), [Google on Android](https://support.google.com/android/answer/17149243), [Digital Trends on Android 17](https://www.digitaltrends.com/gaming/android-17-brings-system-level-remapping-for-controllers-and-its-such-a-relief/), [KDE Game Controller](https://docs.kde.org/stable_kf6/en/plasma-desktop/kcontrol/joystick/), [KDE bug 494913](https://bugs.kde.org/show_bug.cgi?id=494913).

## 3. Design (option B, the owner's choice of 16 Nov 2026)

Options: A, a Settings page with a list and a live test; B, A plus vendor and product ID, a standard layout, Xbox or PlayStation labels and the battery; C, B plus swapping buttons, rumble, lights, touchpad, motion and a buddy controller. Advised: B, as its own step after Bluetooth step 6.

```
Game or program
  ├─ BJoystick         (Device Kit, Haiku)  unchanged: old programs keep working
  └─ BGameController   (new, R5, MIT)       A/B/X/Y, sticks, triggers, name, battery
         │  uses BJoystick and one additive driver call for vendor and product ID
         ▼
/dev/joystick/usb/N   /dev/joystick/bluetooth/N   (usb_hid; bluetooth_hid from step 4)
```

- `BJoystick` stays as it is, in the Device Kit beside `BSerialPort`, as in BeOS. Nothing goes into `libbe`.
- `BGameController`, header `<GameController.h>`, its own MIT library, `libr5game.so` (owner, 16 Nov 2026), like `libr5bt.so`, in `/system/lib` of the regular and the minimum image:
  - `Open()` and `Update()` as in `BJoystick`, so a game reads the state each frame;
  - a fixed layout with names, not numbers (`B_CONTROLLER_BUTTON_A`, `..._LEFT_STICK`, `..._LEFT_TRIGGER`), the order of SDL's standard gamepad, so ported games match;
  - `Name()`, `VendorID()`, `ProductID()`, `BatteryLevel()`;
  - `StartWatching(BMessenger)`: `B_GAME_CONTROLLER_ADDED` and `..._REMOVED` messages when a controller is plugged in or paired, the BeOS way, no polling;
  - rumble and lights later, as calls added at the end (option C).
- One additive call in the joystick driver interface (an R5 snippet) for vendor and product ID, filled by `usb_hid` and by `bluetooth_hid`.
- Old programs: a controller under `/dev/joystick/` works in every `BJoystick` program (and in SDL games, which read through `BJoystick` on Haiku) with no change, because `BJoystick` scans the subfolders too (`JoystickTweaker.cpp`, `_ScanIncludingDisabled()`): a Bluetooth pad is `bluetooth/0`, a USB pad `usb/0`, when the driver speaks the same joystick protocol as `usb_hid` (`JoystickProtocolHandler`). Old programs see raw button and axis numbers in the controller's own order, and no plug or unplug messages; the named layout and `StartWatching()` are for programs that use `BGameController`.
- Rule for option C: swapping buttons happens below `BJoystick`, in the driver path, so old programs get it too; never only in `BGameController`.
- Settings → Devices → Game controllers (alphabetical, between Bluetooth and Keyboard): a `BCard` per controller with name, layout (Xbox or PlayStation labels) and battery (`R5_LEVEL_BATTERY`), and a live test drawn in the real layout. The same view later in a separate program, as with Bluetooth and Wi-Fi.
- The picture is made automatically for every controller (owner, 16 Nov 2026), drawn in code like `r5_screen_draw()`: vector, scaled, dark mode, every part lighting up live.
  - **Known controller** (in the mapping database): one of four family shapes (owner, 16 Nov 2026), chosen from the vendor and product ID, each with the standard layout and its family's labels:
    - PlayStation style: both sticks side by side at the bottom; ✕ ○ □ △.
    - Xbox style: the left stick high, the right stick low; A B X Y.
    - Nintendo style (Switch Pro-like): Xbox-like placement with A and B swapped.
    - Sega Genesis style: a d-pad and six face buttons in two rows (A B C over X Y Z), shoulders where present, no sticks. The 8BitDo M30 is this shape; other 8BitDo models (vendor `0x2DC8`) get the shape of their product ID.
  - The shapes are simplified silhouettes in R5's thin-line style, drawn for R5, not traced from photographs, with no logos. A family name in text ("PlayStation layout") only describes the hardware.
  - **Unknown controller**: a layout built from what it reports: each pair of axes a stick (a circle with a dot), triggers as bars, buttons as round keys, a hat as a d-pad. So every controller gets a picture of its own.
  - Not done: a drawing per exact model: a lot of work per model and a trademark risk. The four shapes are about half a session on top of the rest.
  - The PlayStation symbols become line glyphs in `libr5icons.so`, added at the end of `enum r5_glyph` (`docs/ICONS.md`), so other programs can use them.
  - About one session of work on top of the page.
- The controller battery shows in the existing Bluetooth item in Deskbar, not in an item of its own.

Not in option B (option C, later): swapping buttons for the whole system and per app, rumble, light bar, touchpad, motion sensor, buddy controller.

## 4. License check: the controller mapping database

`AGENTS.md` and `docs/LICENSING.md` ("Third-party code") ask for this before a component comes in.

1. **Project and source.** SDL_GameControllerDB, https://github.com/mdqinc/SDL_GameControllerDB: the community file `gamecontrollerdb.txt` that maps a controller's ID to SDL's standard layout. SDL itself carries a built-in table, `src/joystick/SDL_gamepad_db.h`, https://github.com/libsdl-org/SDL.
2. **License.** Both are the zlib license text, `Zlib` (no `-only` or `-or-later` variants exist for it). The GitHub sidebar of SDL_GameControllerDB labels it "Zlib license"; the text does not name itself. Checked 16 Nov 2026 from `LICENSE` of SDL_GameControllerDB and `LICENSE.txt` and the header of `SDL_gamepad_db.h` in SDL.
3. **Copyright.** SDL_GameControllerDB `LICENSE`: "Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>". SDL `LICENSE.txt` and `SDL_gamepad_db.h`: "Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>". The database's README states no license for contributions; the entries are written by many people. They are mostly facts about hardware (IDs and which button is which), which is the same reasoning R5 used for the Roland cable table.
4. **Dependencies.** None. It is a text file.
5. **Integration.** Proposed: ship `gamecontrollerdb.txt` unmodified as a data file in the regular image (for example `/system/data/game_controllers/`), with its `LICENSE` beside it, and have `BGameController` read it at run time, as SDL does with `SDL_AddGamepadMappingsFromFile()`. R5's own additions or fixes go in a separate R5 file (MIT), never into the copy. No code from SDL is copied into R5.
6. **Compatibility.** Zlib is permissive and sits with MIT, GPL-2.0 and GPL-3.0. No combination question.
7. **Patents, trademarks, attribution, redistribution.** No patent text in any of the three files. Product names in the entries ("PS5 Controller") are names of the hardware, used to describe it. Conditions: do not misrepresent the origin; an altered version must be marked as altered; the notice may not be removed from a source distribution. Acknowledgement in documentation is not required, but R5 credits it anyway: a line in `THIRD_PARTY_NOTICES.md` and the inventory in `docs/LICENSING.md`, written when the file first ships. The README asks for no AI-generated content in issues or pull requests: R5 does not send AI-made changes to that project.
8. **Source duties for a binary release.** None beyond keeping the notice with the file.
9. **Lawyer.** Not needed for this file, in my reading; the owner decides.

**Result:** the database may ship in R5, unmodified, with its `LICENSE`, credited in `THIRD_PARTY_NOTICES.md` when it ships.

## 5. When and steps

Owner, 16 Nov 2026 (later the same day): after HFP, as Bluetooth step 8 in `docs/BLUETOOTH.md`; it starts once step 7b (the HFP connection without voice) is merged. The page is a Settings add-on of its own (`settings_gamecontrollers`, like `settings_bluetooth`), not part of the Bluetooth page; the Bluetooth page's "Test buttons…" window points there once it exists.

Earlier, 16 Nov 2026: not inside the Bluetooth steps (option B is for USB controllers too), but right after Bluetooth step 4 is merged, in parallel with Low Energy (steps 1c and 6), in its own session. It starts after step 4 because it adds one small call to `bluetooth_hid`. Test over a USB cable in VirtualBox (USB passthrough), so no stick of its own; the Bluetooth part rides on the Low Energy stick. Modern Xbox controllers (2020 and later) use Low Energy and come after step 6; the M30 and the DualSense are classic.

1. Take the RenkuOS joystick fixes `244bf43292` and `823649eb6e` (credit them in `THIRD_PARTY_NOTICES.md`).
2. The additive driver call for vendor and product ID in `usb_hid` (and `bluetooth_hid` after step 4).
3. `BGameController` and its library, with `gamecontrollerdb.txt`, documented in `docs/LIBR5.md` and the R5Book.
4. Settings → Devices → Game controllers, and the battery in the Bluetooth item in Deskbar.
5. Test with the owner's 8BitDo M30 and PS5 DualSense: in QEMU over USB passthrough where possible, then on the stick of a Bluetooth step.

## 6. As built (step 8, 17 Nov 2026, while the owner slept; open to change)

The first part of option B: steps 1 to 4 of section 5 without the battery in Deskbar. Calls and limits are in `docs/LIBR5.md`, "Game Kit"; the page in `docs/SETTINGS.md`, "Game controllers".

- **RenkuOS fixes.** `244bf43292` (libdevice, `distro/game/kit/Joystick.cpp`) and `823649eb6e` (hid_shared and `joystick_driver.h`) by atomozero, read from the RenkuOS repository and taken as they are. Both MIT, on Haiku files, signed off; credited in `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`.
- **Driver call.** `R5_JOYSTICK_GET_INFO` (`B_JOYSTICK_DRIVER_BASE + 82`) with `r5_joystick_info`: bus, vendor and product ID, version, battery, the HID usage of each axis, the product name. Answered by hid_shared for every driver that builds it: usb_hid (device descriptor and product string, read once when the device is added), bluetooth_hid (new `R5_BTHID_SET_INFO`, sent by bluetooth_server right after `R5_BTHID_CREATE` with the SDP Device ID record and the device's name), i2c_hid (its HID descriptor) and hyperv/hid (unknown). The battery is HID Battery Strength (page 0x06, usage 0x20) when the input report has it; the DualSense and the M30 do not report it that way, so their battery stays unknown until a per-controller report or the Bluetooth battery comes. A BLE controller (LE HID) gets no IDs yet: the PnP ID characteristic is not read.
- **Library.** `libr5game.so` with `BGameController` and `BGameControllerView`, both images. The standard layout comes from the Windows (DirectInput) entries of the database, because they number buttons and axes the way hid_shared does once the axes are sorted by usage. That choice is checked against the DualSense's Windows entry and a virtual controller with the DualSense's report order, not against a real controller.
- **Database.** SDL_GameControllerDB at commit `22de0d7ef9fe1e6f852a50a9403a7a48f9be30ad` (16 Nov 2026), unmodified, sha256 `97be227812c9ae6f7918aee5d1e7bc6dccbc944c25cf00c93c78d01baadd61f6`, with its `LICENSE`, in `/boot/system/non-packaged/data/game_controllers` of the regular image. To update: fetch both files from that repository at a new commit into `distro/game/data` and record the commit and hash here, in `THIRD_PARTY_NOTICES.md` and in `docs/LICENSING.md`.
- **Glyphs.** `R5_GLYPH_CROSS` (8), `R5_GLYPH_CIRCLE` (9), `R5_GLYPH_SQUARE` (10) and `R5_GLYPH_TRIANGLE` (11) in `libr5icons.so` (`docs/ICONS.md`).
- **Page.** Settings → Devices → Game controllers, the add-on `settings_gamecontrollers`, after the mockup in `docs/design/gamecontrollers-mockup/`. The Bluetooth page's "Test buttons…" opens it; its own test window is gone.
- **Not in this step:** the controller battery in the Bluetooth item in Deskbar (a small follow-up after the HFP pull request #4333 is merged, which changes the same item); rumble, lights, touchpad, motion and swapping buttons (option C); the PnP ID of LE controllers; R5's own mapping entries.
- **Tested:** see `CHANGELOG.md` (QEMU with a virtual controller). **Not tested:** a real controller (QEMU has no game controller and no USB passthrough here), the 8BitDo M30 and the DualSense, the Sega shape's C and Z with the M30, VirtualBox, ARM64 and RISC-V 64.
