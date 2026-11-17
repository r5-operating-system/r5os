# Game controllers mockup (16 Nov 2026)

The design the owner approved for Settings → Devices → Game controllers (`docs/GAME-CONTROLLERS.md`, option B). The page in `distro/settings/GameControllersModule.cpp` follows it.

| File | What it shows |
|---|---|
| `gamecontrollers.html` | The open page: Devices unfolded in the list, the cards, the live test of a DualSense with Cross pressed, the Layout and Button labels menus and Set up buttons… (updated 17 Nov 2026) |

| `lineart-light.png`, `lineart-dark.png` | Approved 17 Nov 2026: the controller pictures from the Gamepad Line Art Pack (CC0), chosen by vendor and product ID, in the text color, light and dark |
| `triggers-light.png`, `triggers-dark.png` | Approved 17 Nov 2026, part 1 only: R5's own Genesis / Mega Drive 2 / 8BitDo M30 drawing in the pack's line style (`distro/game/art/r5/genesis-m30.svg`). Parts 2A to 2C were the first trigger choices |
| `triggers2-light.png`, `triggers2-dark.png` | Approved 17 Nov 2026, part A: small trigger caps over the shoulders with "L2 60%" and "R2 15%" beside them; a pressed stick (L3) fills its cap, a moved stick shows a ring off centre. Part C was not chosen |
| `strip-A-light.png`, `strip-A-dark.png` | Approved 17 Nov 2026 (layout A): the controllers as a `BCardStrip` at the bottom of the page, full width, each card with a small picture of the model, the name and "Bluetooth · 80 %"; above it one row with Layout, Button labels and Set up buttons…, the live picture at the left with the vendor and product ID under it, and Rumble, Motion and Trigger feedback at the right |
| `strip-empty-light.png`, `strip-empty-dark.png` | Approved 17 Nov 2026: no controller connected. A gray example picture (the Xbox Series shape, nothing lit), the menus, Set up buttons… and the three groups gray, and one dashed card "No controller" / "Plug one in with USB, or pair one in Bluetooth." with Open Bluetooth…. The owner removed the line "Connect a controller to try this." under the picture: the page does not draw it |
| `shapes.html`, `sources.html` | Earlier directions for the pictures (R5's own simplified shapes, other art sources), not chosen; kept as history |

The HTML page opens in a browser without a server. It uses the icons of `../settings-mockup/icons/`.

## What the mockup decides

- **Place:** a page under Devices, between Bluetooth and Keyboard (alphabetical). The subtitle is "Test your controllers and set up their buttons" (17 Nov 2026; first "…and choose their button labels"). The icon is the Devices icon with the preferences wrench.
- **Layout A (17 Nov 2026, replaces the column of cards and the Test box below):** see `strip-A-*.png` and `strip-empty-*.png` above. In the smallest window (910 by 560) the groups move under the picture and the part above the strip scrolls; the strip stays at the bottom.
- **Cards (16 Nov 2026, replaced by layout A):** one `BCard` per controller, in a column on the left: the name in bold, the connection and the layout ("Bluetooth · PlayStation layout", "USB · built from what it reports"), and the battery with the battery level glyph when the controller reports it. The chosen card has a blue border. Under the cards a hint says that a new controller shows up when it is plugged in or paired on the Bluetooth page.
- **Test:** a box beside the cards with two rows: Layout (Automatic, Xbox, PlayStation, Nintendo, Sega Genesis; the shape and where the buttons are read from) and Button labels (Xbox or PlayStation labels; only what is printed) with the button Set up buttons… (a window that asks for each control in turn); under them the live picture, and under that the vendor and product ID (beside Layout they do not fit at 910 by 560) of the chosen controller: pressed buttons fill blue, the dot of a stick follows the stick, a trigger bar fills as far as it is pulled (since 17 Nov 2026 on the model pictures: a moved stick shows a ring off centre, and a trigger cap over the shoulder fills, see `triggers2-light.png`).
- **Shapes R5 draws:** removed on 17 Nov 2026 (owner: of no use). The shapes stay in `BGameControllerView`.
- **Drawing:** first (16 Nov 2026) simplified silhouettes in R5's thin-line style. Replaced on 17 Nov 2026 by the owner's choice of a picture per model: the Gamepad Line Art Pack for Xbox, PlayStation and Nintendo controllers, R5's own drawing in the same line style for the Sega Genesis family, and the picture built from what it reports for a controller without a mapping (`docs/GAME-CONTROLLERS.md`, section 7). On R5 they are drawn as vector outlines, scaled, in the text color, so dark mode works.

## Not decided here

- The battery in the Bluetooth item in Deskbar (`docs/GAME-CONTROLLERS.md`, step 4 of section 5), a follow-up.
- Swapping buttons for old `BJoystick` programs, rumble, lights, the touchpad and motion (option C).
