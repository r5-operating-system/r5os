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

- **`en.catkeys`** is the English source, a Haiku catalog file. The build extracts it from the `B_TRANSLATE` calls. Do not edit it by hand.
- **`template.pot`** holds the same texts as a gettext template. `catalogs.py export` writes it from `en.catkeys`. Do not edit it by hand.
- **`nl.po`, `de.po` and `fr.po`** hold every text of the template. A text with an empty `msgstr` has not been translated yet. A text with the `fuzzy` flag is marked "Needs editing" in Weblate and is not used yet.

Haiku's own programs keep their translations in `haiku/data/catalogs`, as before.

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

## Not done yet

- Most of R5's own screens do not use `B_TRANSLATE` yet. #4599 lists them.
- Translations made by people.
