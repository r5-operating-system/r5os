# Game controllers mockup (16 Nov 2026)

The design the owner approved for Settings → Devices → Game controllers (`docs/GAME-CONTROLLERS.md`, option B). The page in `distro/settings/GameControllersModule.cpp` follows it.

| File | What it shows |
|---|---|
| `gamecontrollers.html` | The open page: Devices unfolded in the list, the cards, the live test of a DualSense with Cross pressed, the Layout and Button labels menus and Set up buttons… (updated 17 Nov 2026) |

The HTML page opens in a browser without a server. It uses the icons of `../settings-mockup/icons/`.

## What the mockup decides

- **Place:** a page under Devices, between Bluetooth and Keyboard (alphabetical). The subtitle is "Test your controllers and set up their buttons" (17 Nov 2026; first "…and choose their button labels"). The icon is the Devices icon with the preferences wrench.
- **Cards:** one `BCard` per controller, in a column on the left: the name in bold, the connection and the layout ("Bluetooth · PlayStation layout", "USB · built from what it reports"), and the battery with the battery level glyph when the controller reports it. The chosen card has a blue border. Under the cards a hint says that a new controller shows up when it is plugged in or paired on the Bluetooth page.
- **Test:** a box beside the cards with two rows: Layout (Automatic, Xbox, PlayStation, Nintendo, Sega Genesis; the shape and where the buttons are read from) and Button labels (Xbox or PlayStation labels; only what is printed) with the button Set up buttons… (a window that asks for each control in turn); under them the live picture, and under that the vendor and product ID (beside Layout they do not fit at 910 by 560) of the chosen controller: pressed buttons fill blue, the dot of a stick follows the stick, a trigger bar fills as far as it is pulled.
- **Shapes R5 draws:** removed on 17 Nov 2026 (owner: of no use). The shapes stay in `BGameControllerView`.
- **Drawing:** simplified silhouettes in R5's thin-line style, drawn for R5, not traced from photographs, with no logos. On R5 they are drawn in code, vector and scaled, in the system colors, so dark mode works.

## Not decided here

- The battery in the Bluetooth item in Deskbar (`docs/GAME-CONTROLLERS.md`, step 4 of section 5), a follow-up.
- Swapping buttons for old `BJoystick` programs, rumble, lights, the touchpad and motion (option C).
