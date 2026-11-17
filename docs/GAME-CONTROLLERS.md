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
- **Driver call.** `R5_JOYSTICK_GET_INFO` (`B_JOYSTICK_DRIVER_BASE + 82`) with `r5_joystick_info`: bus, vendor and product ID, version, battery, the HID usage of each axis, the product name. Answered by hid_shared for every driver that builds it: usb_hid (device descriptor and product string, read once when the device is added), bluetooth_hid (new `R5_BTHID_SET_INFO`, sent by bluetooth_server right after `R5_BTHID_CREATE` with the SDP Device ID record and the device's name), i2c_hid (its HID descriptor) and hyperv/hid (unknown). The battery is HID Battery Strength (page 0x06, usage 0x20) when the input report has it; the DualSense and the M30 most likely do not report it that way (not checked), so their battery probably stays unknown until a per-controller report or the Bluetooth battery comes. A BLE controller (LE HID) gets no IDs yet: the PnP ID characteristic is not read.
- **Library.** `libr5game.so` with `BGameController` and `BGameControllerView`, both images. The standard layout comes from the Windows (DirectInput) entries of the database, because they number buttons and axes the way hid_shared does once the axes are sorted by usage. That choice is checked against the DualSense's Windows entry and a virtual controller with the DualSense's report order, not against a real controller.
- **Database.** SDL_GameControllerDB at commit `22de0d7ef9fe1e6f852a50a9403a7a48f9be30ad` (16 Nov 2026), unmodified, sha256 `97be227812c9ae6f7918aee5d1e7bc6dccbc944c25cf00c93c78d01baadd61f6`, with its `LICENSE`, in `/boot/system/non-packaged/data/game_controllers` of the regular image. To update: fetch both files from that repository at a new commit into `distro/game/data` and record the commit and hash here, in `THIRD_PARTY_NOTICES.md` and in `docs/LICENSING.md`.
- **Glyphs.** `R5_GLYPH_CROSS` (8), `R5_GLYPH_CIRCLE` (9), `R5_GLYPH_SQUARE` (10) and `R5_GLYPH_TRIANGLE` (11) in `libr5icons.so` (`docs/ICONS.md`).
- **Page.** Settings → Devices → Game controllers, the add-on `settings_gamecontrollers`, after the mockup in `docs/design/gamecontrollers-mockup/`. The Bluetooth page's "Test buttons…" opens it; its own test window is gone.
- **Battery in Deskbar (as built, 17 Nov 2026).** bluetooth_hid reads HID Battery Strength (page 0x06, usage 0x20) from every input report bluetooth_server feeds it, with a second parser of the same descriptor so the handlers' reports are not touched, also while no program reads the controller. The new additive `R5_BTHID_GET_BATTERY` on its control node (`<R5BluetoothHID.h>`) returns it; bluetooth_server puts it in "battery" of `BT_MSG_R5_GET_PAIRED` for a connected Classic HID device when HFP has none, so the Bluetooth item in Deskbar shows the existing battery line and My devices gets the same value. Deskbar opens no joystick device. `R5_JOYSTICK_GET_INFO` (and so `BatteryLevel()`) returns the same value before the first read. A controller whose reports have no Battery Strength shows no battery line: the DualSense and the 8BitDo M30 most likely report none that way (not checked); a per-controller battery report is not read. BLE controllers keep the Battery Service value. Tested only with `bt_hid_driver_test` in QEMU, not with a real controller.
- **Not in this step:** rumble, lights, touchpad, motion and swapping buttons (option C); the PnP ID of LE controllers; R5's own mapping entries.
- **After the boot of 17 Nov 2026 on the 5625U with the 8BitDo M30** (`2dc8:0651` over Bluetooth, report descriptor 86 bytes, not in the log): Settings crashed when it quit (the watcher looper outlived `libr5game.so`), the D-pad did nothing and a pressed button lit the wrong key on the Sega picture. Fixes, details in `docs/LIBR5.md` ("Game Kit") and `CHANGELOG.md`:
  - The watcher quits with the last `StopWatching()` and when the library is unloaded.
  - **The database cannot be trusted per platform.** Its Windows line for `2dc8:0651` is a USB entry (GUID bus `03`), and no Windows Bluetooth line exists; it puts the D-pad on axes 0 and 2, which a pad with only X and Y does not have. The Mac and Linux Bluetooth lines for the same IDs disagree with each other (A and B swapped; a hat on Linux, X and Y on the Mac), so the M30 reports differently per firmware or mode. R5 therefore takes what the HID descriptor says first: the line for the controller's own bus, the buttons by HID usage, the D-pad on the hat or else on X and Y, and nothing the controller does not have. Which physical key is which on the M30 over Bluetooth is still not known; that is what the two new choices are for.
  - **Layout menu** (fixed layouts Xbox, PlayStation, Nintendo, Sega Genesis) for a quick fix, beside **Button labels**: Layout is the shape and where the buttons are read from, the labels only what is printed. **Set up buttons…** asks for each control in turn and saves the result per controller model in `~/config/settings/game_controllers`. A saved setup comes before a chosen layout, which comes before the database.
  - **Why this may live in `BGameController`.** The rule of section 3 says swapping buttons for the whole system (option C, for old `BJoystick` programs) belongs below `BJoystick`. A setup here is something else: it maps the controller's raw buttons, axes and hats to the named standard layout, exactly what the database line does, only written by the person. It sits at the same level as the database mapping, in `BGameController`, and `BJoystick` programs see the same raw buttons as before. Swapping buttons for old programs stays option C, in the driver path.
  - The "Shapes R5 draws" strip is gone from the page (owner: of no use); `BGameControllerView` keeps the shapes.
- **Tested:** QEMU with a virtual controller on bluetooth_hid (`gamecontroller_test`, and the page in screendumps); details in `CHANGELOG.md`. **Not tested:** a real controller (QEMU has no game controller and no USB passthrough here), the 8BitDo M30 and the DualSense, the Sega shape's C and Z with the M30, VirtualBox, ARM64 and RISC-V 64.

## 7. Option C, phase 1: rumble, trigger effects and motion (17 Nov 2026)

The owner approved option C in two phases on 17 Nov 2026, with the Test box mockup in `docs/design/gamecontrollers-mockup/` (Rumble, Motion, Trigger feedback):

- **Phase 1 (now):** the DualSense, the DualSense Edge and the DualShock 4, over USB and Bluetooth: rumble (both motors, a duration), trigger effects (DualSense and Edge: off, resistance, trigger click, vibration; start position and strength per trigger) and motion (accelerometer and gyroscope). And generic HID force feedback (USB HID PID, usage page `0x0F`) for rumble on every other controller whose report descriptor has it. The Settings page's Test box gets a group per feature, hidden when the controller lacks it; the 8BitDo M30 has neither rumble nor motion.
- **Phase 2 (later):** the Switch Pro Controller and Xbox controllers over Bluetooth (their own rumble protocols); lights, the touchpad and swapping buttons for old programs stay later too.

### Sources and licenses

- Report layouts of the PlayStation controllers: SDL's hidapi drivers `src/joystick/hidapi/SDL_hidapi_ps4.c` and `SDL_hidapi_ps5.c`, and the trigger effect bytes in `test/testcontroller.c` (libsdl-org/SDL, read 17 Nov 2026). License zlib (`Zlib`), "Copyright (C) 1997-2026 Sam Lantinga". Read as a reference for facts (report IDs, offsets, flag bits, CRC seed, sensor scale); no SDL code is copied, and R5's code is written new under MIT. Cited in `GameControllerReports.h` and `R5HIDReports.h`.
- HID PID: "Device Class Definition for Physical Interface Devices (PID)", version 1.0, USB Implementers Forum, and the PID page of the HID Usage Tables. A specification, implemented from its descriptions; no code.
- Not used: Linux `hid-playstation`, `hid-sony` and `hid-pidff` (GPL-2.0), nor any other GPL code.

### As built (17 Nov 2026, part 1 of the PR)

- **Driver calls**, additive, in `<joystick_driver.h>` (`docs/LIBR5.md`, "Feel and motion"): `R5_JOYSTICK_SEND_REPORT`, `R5_JOYSTICK_GET_REPORT` and `R5_JOYSTICK_GET_DESCRIPTOR`, answered by hid_shared (`JoystickProtocolHandler`) for usb_hid and bluetooth_hid. Only reports the descriptor declares, with that type and ID, never longer than declared; user memory only through `IS_USER_ADDRESS()` on both ends and `user_memcpy()`, sizes from the caller. usb_hid: the interrupt OUT pipe for output reports when the interface has one, else `SET_REPORT`; feature reports with `SET_REPORT` and `GET_REPORT`; a copy of the report descriptor; the last input report per ID (four IDs, 128 bytes) under a spinlock in the transfer callback. bluetooth_hid: output reports through bluetooth_server (the queue entries grew from 64 to 512 bytes, the HID maximum the driver takes), no feature reports yet (they need `SET_REPORT` and `GET_REPORT` on the control channel), the same input cache and descriptor copy.
- **The full reports over Bluetooth.** A DualShock 4 or DualSense over Bluetooth sends its short report `0x01` (buttons and sticks, what its descriptor describes) until its first output report, and only its full report afterwards (`0x11` to `0x19`, `0x31`), which the descriptor describes as vendor bytes. Without help a rumble would leave every `BJoystick` program without buttons until the controller reconnects. bluetooth_hid therefore builds the short report from each full one for those IDs (`r5_hid_sony_short_report()` in `R5HIDReports.h`) and feeds that to the parser; the full report stays in the input cache for the motion.
- **libr5game**: `CanRumble()`, `SetRumble()`, `StopRumble()`, `HasTriggerEffects()`, `SetTriggerEffect()`, `HasMotion()`, `SetMotionEnabled()`, `IsMotionEnabled()`, `GetMotion()`, added at the end of `BGameController`; reports built in user space (`GameControllerReports.cpp`): DualSense `0x02` (USB, 48 bytes) and `0x31` (Bluetooth, 78 bytes, CRC-32 over `0xA2` and the report), DualShock 4 `0x05` (USB, 32 bytes) and `0x11` (Bluetooth, 78 bytes, CRC); motion from `0x01` (USB) or the full report, uncalibrated (8192 counts per g, 16 per degree per second). HID PID: a small HID report descriptor parser for the PID page, then Device Reset, Enable Actuators, Device Gain, Create New Effect and PID Block Load when the device manages its effects, Set Effect, Set Periodic (a sine) or Set Constant Force, Effect Operation Start Solo and Stop. The duration of a rumble is kept by a thread of the library, so it also stops on controllers that rumble until told otherwise.
- **Appearance Kit** (libr5ui): `BTiltLevel` (a bubble in a circle) and `BAxisBar` (a bar that fills from the middle), shared because the owner asked for it; neither Haiku nor libr5ui had one (`docs/LIBR5.md`, "Axis bar"). **libr5game**: `BGameControllerMotionView`, the level and Pitch, Roll and Yaw bars, a new class beside `BGameControllerView` (which is unchanged).
- **Tests**: `scripts/test-game-feel.sh` (host, ASan and UBSan): the exact bytes of every report, the CRC-32 against a second implementation and the check value of "123456789", the motion parser, the short report, the input cache, and the PID reports of a synthetic descriptor with the PID reports rumble needs. `scripts/fuzz-bt.sh hid`: the descriptor parser, the PID report builders, the Block Load answer, the motion parser, the short report and the cache, 60 s. `gamecontroller_feel_test` in QEMU: a virtual DualSense with report IDs on bluetooth_hid (the calls, what they refuse, the buttons and motion from full reports only, the exact output reports in the driver's queue, a timed stop, `Close()` freeing the triggers, the widgets drawing).
- **Not tested**: any real controller. Rumble, trigger effects and motion of the DualSense and the DualShock 4 need the owner's DualSense on a stick; the trigger effect parameters of the click and vibration modes (`0x02`, `0x06`), the axes' signs and the DualShock 4 switching to full reports from an output report alone (SDL also reads its calibration report, which needs feature reports over Bluetooth) are the parts most likely to need a fix. No PID controller was tested, over USB or Bluetooth. The usb_hid path (interrupt OUT, `SET_REPORT`, `GET_REPORT`) did not run: QEMU has no USB game controller here.
- **Part 2** (after the controller pictures PR): the Test box on the Settings page.
