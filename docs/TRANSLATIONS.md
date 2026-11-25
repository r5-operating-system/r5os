# Translations

The plan, decided on 24 Nov 2026, is in [#4599](https://github.com/r5-operating-system/r5os/issues/4599). This page describes what is in place.

## Where the files are

R5's own programs keep their translations in `distro/catalogs/<folder>/<language>.catkeys`. These are Haiku catalog files.

`<folder>` is where the build puts the program's catalog under `generated.x86_64/objects/catalogs`: the `HAIKU_CATALOGS_SUBDIR` that `DoCatalogs` sets in `build/jam/LocaleRules`. The first three are:

| Folder | Program | Signature |
|---|---|---|
| `preferences/r5bt` | `libr5bt.so`, the Bluetooth Kit | `x-vnd.R5OS-libr5bt` |
| `preferences/r5wifi` | `libr5wifi.so`, the Wi-Fi Kit | `x-vnd.R5OS-libr5wifi` |
| `servers/bluetooth_deskbar` | the Bluetooth item in the Deskbar | `x-vnd.R5OS-bluetooth-deskbar` |

- **`en.catkeys`** is the English source. The build extracts it from the `B_TRANSLATE` calls. Do not edit it by hand.
- **`nl`, `de` and `fr`** hold every entry of `en.catkeys`. An entry whose fourth field (the translation) is empty has not been translated yet.

Haiku's own programs keep their translations in `haiku/data/catalogs`, as before.

## The format

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

It does three things:
1. copies each `en.catkeys` from the build into its folder in `distro/catalogs`;
2. adds new texts to every language file, untranslated;
3. drops texts that are gone, and keeps every translation that still matches.

A program that is not in `distro/catalogs` yet is added with `--add <folder>`. That also creates the `nl`, `de` and `fr` files.

## Before a build: apply

`scripts/apply-overlay.sh` runs `catalogs.py apply`. It copies each language file into `haiku/data/catalogs/<folder>/`, where `DoCatalogs` finds it and links it into the image. While copying it leaves out:

- `en.catkeys`, because the build makes its own English catalog;
- every untranslated entry. A text without a translation then stays English instead of turning blank.

A language with nothing translated gets no catalog at all.

## Weblate

The language files are what Weblate edits. Its component for each folder uses:

- format "Catkeys file";
- file mask `distro/catalogs/<folder>/*.catkeys`;
- monolingual base language file: empty;
- template for new translations: `distro/catalogs/<folder>/en.catkeys`.

Still to check: whether Weblate writes texts marked "Needs editing" into the file. The `.catkeys` format has no flag for that state.

## Not done yet

- Most of R5's own screens do not use `B_TRANSLATE` yet. #4599 lists them.
- Translations made by people.
