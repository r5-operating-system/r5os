# Translations

The plan, decided on 24 Nov 2026, is in [#4599](https://github.com/r5-operating-system/r5os/issues/4599). This page describes what is in place.

## Where the files are

R5's own programs keep their translations in `distro/catalogs/<folder>/<language>.po`. These are gettext files, the format Weblate handles best. The build turns them into Haiku catalog files.

`<folder>` is where the build puts the program's catalog under `generated.x86_64/objects/catalogs`: the `HAIKU_CATALOGS_SUBDIR` that `DoCatalogs` sets in `build/jam/LocaleRules`. The first three are:

| Folder | Program | Signature |
|---|---|---|
| `preferences/r5bt` | `libr5bt.so`, the Bluetooth Kit | `x-vnd.R5OS-libr5bt` |
| `preferences/r5wifi` | `libr5wifi.so`, the Wi-Fi Kit | `x-vnd.R5OS-libr5wifi` |
| `servers/bluetooth_deskbar` | the Bluetooth item in the Deskbar | `x-vnd.R5OS-bluetooth-deskbar` |
| `preferences/settings/Settings` | the Settings window: the list, Home, search and the bottom row | `x-vnd.R5OS-Settings` |
| `preferences/settings/settings_<page>` | one Settings page add-on each (19), for example `settings_display` | `x-vnd.R5OS-settings_<page>` |
| `preferences/r5ui` | `libr5ui.so`: the admin lock, the password field, the account cards, the accent names | `x-vnd.R5OS-libr5ui` |
| `preferences/r5game` | `libr5game.so`, the Game Kit views | `x-vnd.R5OS-libr5game` |
| `apps/login/Login` | Sign in and the lock screen | `x-vnd.Haiku-Login` |
| `preferences/users` | Users | `x-vnd.R5OS-Users` |
| `preferences/bluetooth` | the Bluetooth program | `x-vnd.Haiku-BluetoothPrefs` |
| `preferences/wifi` | the Wi-Fi program | `x-vnd.Haiku-WiFi` |
| `apps/cpuinfo` | CPU Info | `x-vnd.R5-CPUInfo` |
| `apps/devicemanager` | Device Manager | `x-vnd.R5-DeviceManager` |
| `servers/airplay/airplay_deskbar` | the AirPlay item in the Deskbar | `x-vnd.R5OS-airplay-tray` |

A shared library or an add-on needs a signature in its own file (an `.rdef` with `resource app_signature`), because the Locale Kit finds an image's catalog by that signature. Haiku's libraries do the same (`libbe_version.rdef`). `libr5bt.so` and `libr5wifi.so` had none until 25 Nov 2026, so their translations were not loaded.

A program whose signature is Haiku's (Login, Bluetooth, Wi-Fi) but whose code is R5's has its catalog here too. Before its folder was added, the texts that Haiku's translators made in the same context were taken over from `haiku/data/catalogs`.

- **`en.catkeys`** is the English source, a Haiku catalog file. The build extracts it from the `B_TRANSLATE` calls. Do not edit it by hand.
- **`template.pot`** holds the same texts as a gettext template. `catalogs.py export` writes it from `en.catkeys`. Do not edit it by hand.
- **`nl.po`, `de.po` and `fr.po`** hold every text of the template. A text with an empty `msgstr` has not been translated yet. A text with the `fuzzy` flag is marked "Needs editing" in Weblate and is not used yet.

Haiku's own programs keep their translations in `haiku/data/catalogs`, as before.

## Settings

Settings and each of its page add-ons have a catalog of their own (decided 25 Nov 2026), because each page is an add-on (`distro/settings/ModuleAPI.md`) and the Locale Kit finds an image's catalog by the signature of that file. Each add-on therefore carries a signature in `distro/settings/settings_<page>.rdef`. The Jamfile sets `HAIKU_MULTIPLE_LOCALIZED_TARGETS`, so each target gets its own folder.

- **Contexts.** Each page has the context `Settings: <page>`, for example `Settings: Display`; the window is `Settings window` and Home is `Settings: Home`. A translator sees on which page a text is. Views copied from Haiku (Fonts, Colors, the Network service list) keep Haiku's context, so Haiku's own translations of them could be taken over.
- **Fixed texts.** The five fixed descriptions of `AGENTS.md` stay the same in English. They carry the comment "Fixed text (AGENTS.md): translate the meaning, keep it short".
- **Order and search.** Pages are sorted by their translated name in the order of the user's language (`BCollator`); Information stays first under System. Search also compares the page id, which is its English name, so an English word still finds a page. The keywords carry the comment "Keep the English words and add words in your language".

## The formats

In a `.po` file the context of a text is its `msgctxt`, and the English text is its `msgid`. A comment from the catalog is written as `#.`. Plural forms are not used: R5 writes plurals inside the text, in ICU form.

A `.catkeys` file is UTF-8 text with tabs between the fields.

The first line is the header:

```
1	<language name>	<signature>	<fingerprint>
```

Every other line is one text:

```
<English text>	<context>	<comment>	<translation>
```

The context is the `B_TRANSLATION_CONTEXT` of the source file.

## After a build: export

When a program gets new or changed texts, run this in WSL after `scripts/build-locked.sh`, and commit the result:

```
python3 scripts/catalogs.py export haiku .
```

It does four things:
1. copies each `en.catkeys` from the build into its folder in `distro/catalogs`;
2. writes `template.pot` from it;
3. adds new texts to every `.po` file, untranslated;
4. drops texts that are gone, and keeps every translation that still matches, with its `fuzzy` flag.

A program that is not in `distro/catalogs` yet is added with `--add <folder>`. That also creates `nl.po`, `de.po` and `fr.po`.

When the English of a text changes, `export` keeps the translation of the most similar old text in the same context, marked `fuzzy` ("Needs editing"), with the old English as `#| msgid`. Weblate shows that old text next to the new one. The translation is not used until someone checks it. So the English is changed in the code, and the translations follow.

## Before a build: apply

`scripts/apply-overlay.sh` runs `catalogs.py apply`. It turns each `.po` file into `haiku/data/catalogs/<folder>/<language>.catkeys`, where `DoCatalogs` finds it and links it into the image. The header (signature and fingerprint) and the comments come from `en.catkeys`. It leaves out:

- every untranslated text. That text then stays English instead of turning blank;
- every `fuzzy` text, so a translation marked "Needs editing" is not shipped. That text stays English too.

A language with nothing translated gets no catalog at all.

## Weblate

The project is [hosted.weblate.org/projects/r5](https://hosted.weblate.org/projects/r5/). The `.po` files are what Weblate edits. Its component for each folder uses:

- format "gettext PO file";
- file mask `distro/catalogs/<folder>/*.po`;
- monolingual base language file: empty;
- template for new translations: `distro/catalogs/<folder>/template.pot`;
- language filter: the default `^[^.]+$`;
- license MIT;
- version control "GitHub pull request", with "Merge requests maken" (create merge requests) on and automatic merging off. Weblate's changes reach `main` only through a pull request that the owner approves.

Weblate first used the "Haiku catkeys" format. A test (#4607, closed unmerged) showed that Weblate then drops every untranslated text from the file, and the format has no flag for "Needs editing". With gettext both are kept.

### New folders appear by themselves

Done on 25 Nov 2026 (add-on 35946 on `preferences/r5bt`). The owner ran `py scripts/weblate-discovery.py --apply` once, with their own Weblate API key (hosted.weblate.org: picture at the top right > Settings > API access). Without `--apply` it only shows what it will do. It:

1. checks that the components `wifi` and `bluetooth_deskbar` have nothing in Weblate that is not in the repository yet, and stops if they have;
2. renames `bluetooth` to `preferences/r5bt`, so every component is named after its folder;
3. deletes `wifi` and `bluetooth_deskbar` after you type `delete` (their translations are in the repository; their Weblate history is not kept);
4. turns on **Component discovery** on `bluetooth`, with the file mask `distro/catalogs/(?P<component>.+)/(?P<language>[^/.]+)\.po` and the template `distro/catalogs/{{ component }}/template.pot`.

Discovery then creates one component per folder in `distro/catalogs` on `main`, and new ones after every push. The GitHub webhook (`https://hosted.weblate.org/hooks/github/`, event push) is on since 25 Nov 2026, so Weblate fetches `main` after each merge. Weblate's changes still come back as a pull request.

## Drafts

A text that Haiku's own translators already translated in the same context is taken over from `haiku/data/catalogs`, without `fuzzy`. Every other text of nl, de and fr in Settings has an AI draft, marked `fuzzy`. It is not shipped until a person has checked it in Weblate.

## Not done yet

- Most of R5's own screens do not use `B_TRANSLATE` yet. #4599 lists them.
- Translations made by people.
