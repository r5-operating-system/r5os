# Settings modules

The Settings host loads a module without knowing how that module stores its page. A new module is an add-on. It does not require a change to the Settings window.

**Every Settings page is an add-on, one page per add-on** (owner, 25 Nov 2026). The Settings program itself holds only the window, Home, the list, search, the lock and the loader. Do not build a page into the program, and do not register a module from `SettingsApp.cpp`.

API version is `kSettingsApiVersion` in `SettingsModule.h`. It is 2. The host loads a module only when `settings_api_version()` returns that number. A module built for another version is skipped and reported in the syslog. Version 2 adds `CanApplyDefaults()` and `ApplyDefaults()` at the end of `SettingsModule`.

## Files

Put the module in its own directory and build it with the `Addon` rule. Install the binary in an add-ons directory named `Settings`:

- `/boot/system/add-ons/Settings`
- `/boot/system/non-packaged/add-ons/Settings`
- `/boot/home/config/add-ons/Settings`
- `/boot/home/config/non-packaged/add-ons/Settings`

The pages of the image are in `/boot/system/add-ons/Settings`, in the regular and the minimum image alike:

| Add-on | Page | Source |
|---|---|---|
| `settings_accounts` | Users and Accounts | `AccountsModule.cpp`, `AccountDialogs.cpp` |
| `settings_bluetooth` | Bluetooth | `BluetoothModule.cpp` |
| `settings_connections` | Connections | `ConnectionsModule.cpp`, `NetworkPage.cpp` |
| `settings_deskbar` | Desktop and Deskbar | `DeskbarModule.cpp` |
| `settings_discovery` | Discovery | `DiscoveryModule.cpp`, `NetworkPage.cpp` |
| `settings_display` | Display | `DisplayModule.cpp`, `DisplayScreens.cpp` |
| `settings_fonts` | Fonts | `FontsModule.cpp` |
| `settings_gamecontrollers` | Game controllers | `GameControllersModule.cpp`, `NetworkPage.cpp` |
| `settings_information` | Information | `InformationModule.cpp` |
| `settings_keyboard` | Keyboard (opens Keymap) | `LegacyKeyboard.cpp`, `LegacyPages.cpp` |
| `settings_mouse` | Mouse (opens Input) | `LegacyMouse.cpp`, `LegacyPages.cpp` |
| `settings_notifications` | Notifications | `NotificationsModule.cpp` |
| `settings_performance` | Performance | `PerformanceModule.cpp` |
| `settings_power` | Power | `PowerModule.cpp` |
| `settings_printers` | Printers (opens Printers) | `LegacyPrinters.cpp`, `LegacyPages.cpp` |
| `settings_sound` | Sound (opens Sounds) | `LegacySound.cpp`, `LegacyPages.cpp` |
| `settings_theme` | Theme | `ThemeModule.cpp`, `ColorsView.cpp` |
| `settings_virtualmemory` | Virtual memory | `VirtualMemoryModule.cpp` |
| `settings_wallpaper` | Wallpaper | `WallpaperModule.cpp` |
| `settings_wifi` | Wi-Fi | `WiFiModule.cpp` |

The Jamfile is `distro/settings/Jamfile`. `distro/desktop/build/jam/packages/Haiku` puts the add-ons in the package. `settings_fonts`, `settings_bluetooth` and `settings_gamecontrollers` still come in through `SYSTEM_ADD_ONS_SETTINGS`. A new page adds its row here, its `Addon` rule, its line in the package file, its file in `scripts/apply-settings.py`, and its icon in `module_icon()`.

A helper that several pages use, such as `SettingsFont.cpp` or `NetworkPage.cpp`, is compiled into each add-on that needs it. A helper for programs other than Settings goes in an R5 library (`docs/LIBR5.md`).

## Entry points

Both symbols are `extern "C"` and exported. They must not be inlined.

```cpp
extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}

extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new ExampleModule();
}
```

Include `SettingsModule.h` only. Do not include the Settings window or the host.

## Metadata

`Identifier()` is a stable id: letters, digits, `.`, `_`, and `-`, at most 64 characters. It is the file name under `~/config/settings/r5-settings/`. Two modules with the same id cannot both load.

`Category()` is a short id. The host already names `personalization`, `system`, `devices`, `network`, and `accounts`. Any other id is still listed, under that id as its label.

`Name()`, `Description()`, and `Keywords()` are what search compares. Search is case-insensitive.

`RequiresAdmin()` means Save is refused unless `SettingsContext::AdminChangeAllowed()` is true. That call is the existing administrator lock (`r5_admin_change_allowed`). A checkbox in the module is not the check. `SaveSettings()` must call `AdminChangeAllowed()` again before it writes a privileged value. Fonts and Look and Feel do not require an administrator.

`CanSave()` is false for a page that only opens an existing preference program. Those programs keep their own files. A page with `CanSave()` true is written as soon as the user changes it. The window calls `SaveSettings()` from `NotifyChanged()`. Do not wait for a Save button. There is none.

`CanApplyDefaults()` is true only when Defaults would change the page. `ApplyDefaults()` writes the factory setup. The window calls it from the Defaults button. Revert still calls `RevertSettings()`, which must restore the values from when the page was opened, including after an immediate save.

## The view

`CreateView()` returns a `BView`. The Settings window owns it and deletes it. The module must not delete it. Keep a non-owning pointer if the page has to update after Revert, and clear that pointer from the view's destructor.

The window does not rebuild itself when the page changes. It keeps the view and shows it again.

Call `NotifyChanged()` after the user edits a value. The window then calls `SaveSettings()`. Keep the opened snapshot separate from that write, so `RevertSettings()` can put it back.

## Storage

`ReadSettings()` / `WriteSettings()` flatten one `BMessage` into `~/config/settings/r5-settings/<identifier>`. A missing file is `B_ENTRY_NOT_FOUND`; treat that as the defaults.

A page that replaces a preference the system already watches writes that existing file, and does not also keep a second copy. Notifications writes `~/config/settings/system/notifications`. Desktop and Deskbar messages the running Deskbar and does not write its settings file. Other new modules use `ReadSettings()` / `WriteSettings()`.

`LoadSettings()` copies the file into the module. The window calls it when the page is first opened, not when Settings starts. `SaveSettings()` writes it. `RevertSettings()` puts back the values from when the page was opened, including after an immediate save.

## Failure

Return a `status_t`. The window shows it. If `CreateView()` returns `NULL`, the window shows a short error and the rest of Settings stays open.

A module that fails to load does not stop the others. The syslog line starts with `settings:`.
