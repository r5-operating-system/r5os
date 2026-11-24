#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Translation catalogs of R5's own programs (docs/TRANSLATIONS.md).

distro/catalogs/<subdir>/ holds Haiku .catkeys files, one per language.
<subdir> is the folder of the program under objects/catalogs (the
HAIKU_CATALOGS_SUBDIR of DoCatalogs), for example preferences/r5bt.
en.catkeys is the English source the build extracts; nl, de, fr and the
other languages hold every entry of en.catkeys, with an empty translation
where no one has translated it yet. Weblate edits those files.

  catalogs.py export <haiku tree> <repo root> [--add <subdir> ...]
      After a build: copy each objects/catalogs/<subdir>/en.catkeys into
      distro/catalogs/<subdir>/, and bring every language file of that
      folder in line with it (new entries added untranslated, removed
      entries dropped, translations kept). Only folders already in
      distro/catalogs are exported; --add starts a new one.

  catalogs.py apply <haiku tree> <repo root>
      Before a build (apply-overlay.sh): copy the language files into
      <haiku tree>/data/catalogs/<subdir>/, without en.catkeys and without
      untranslated entries, so DoCatalogs links them and an untranslated
      text stays English instead of empty.
"""

import sys
from pathlib import Path

LANGUAGE_NAMES = {
    # The language name Haiku writes on the first line of its catalogs
    # (data/catalogs/apps/terminal/<lang>.catkeys).
    "nl": "dutch; flemish",
    "de": "german",
    "fr": "french",
}
NEW_LANGUAGES = ("nl", "de", "fr")


def read_catkeys(path):
    lines = path.read_text(encoding="utf-8").split("\n")
    header = lines[0].split("\t")
    if len(header) != 4 or header[0] != "1":
        raise SystemExit(f"{path}: not a catkeys file (header {lines[0]!r})")
    entries = []
    for number, line in enumerate(lines[1:], start=2):
        if line == "":
            continue
        fields = line.split("\t")
        if len(fields) != 4:
            raise SystemExit(f"{path}:{number}: expected 4 fields, found "
                f"{len(fields)}")
        entries.append(fields)
    return header, entries


def write_catkeys(path, header, entries):
    text = "\t".join(header) + "\n"
    text += "".join("\t".join(fields) + "\n" for fields in entries)
    path.write_bytes(text.encode("utf-8"))


def merge(language_file, english_header, english_entries, language):
    translations = {}
    if language_file.exists():
        header, entries = read_catkeys(language_file)
        name = header[1]
        for source, context, comment, translation in entries:
            translations[(source, context, comment)] = translation
    else:
        name = LANGUAGE_NAMES.get(language, language)
    header = ["1", name, english_header[2], english_header[3]]
    entries = [[source, context, comment,
        translations.get((source, context, comment), "")]
        for source, context, comment, _ in english_entries]
    write_catkeys(language_file, header, entries)
    done = sum(1 for fields in entries if fields[3] != "")
    print(f"  {language}: {done} of {len(entries)} translated")


def export(tree, root, added):
    objects = tree / "generated.x86_64" / "objects" / "catalogs"
    catalogs = root / "distro" / "catalogs"
    subdirs = sorted({path.parent.relative_to(catalogs).as_posix()
        for path in catalogs.rglob("en.catkeys")} | set(added))
    if not subdirs:
        raise SystemExit("no catalogs: start one with --add <subdir>")
    for subdir in subdirs:
        built = objects / subdir / "en.catkeys"
        if not built.exists():
            raise SystemExit(f"{built} is missing: build first")
        target = catalogs / subdir
        target.mkdir(parents=True, exist_ok=True)
        header, entries = read_catkeys(built)
        write_catkeys(target / "en.catkeys", header, entries)
        print(f"{subdir}: {len(entries)} texts ({header[2]})")
        languages = {path.stem for path in target.glob("*.catkeys")}
        languages.discard("en")
        if subdir in added:
            languages |= set(NEW_LANGUAGES)
        for language in sorted(languages):
            merge(target / f"{language}.catkeys", header, entries, language)


def apply(tree, root):
    catalogs = root / "distro" / "catalogs"
    copied = 0
    for path in sorted(catalogs.rglob("*.catkeys")):
        if path.stem == "en":
            continue
        header, entries = read_catkeys(path)
        entries = [fields for fields in entries if fields[3] != ""]
        target = (tree / "data" / "catalogs"
            / path.parent.relative_to(catalogs) / path.name)
        if not entries:
            # Nothing translated: no catalog, so the program stays English.
            target.unlink(missing_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        write_catkeys(target, header, entries)
        copied += 1
    print(f"copied {copied} R5 translation catalogs")


def main(argv):
    if len(argv) < 3 or argv[0] not in ("export", "apply"):
        raise SystemExit(__doc__)
    tree, root = Path(argv[1]), Path(argv[2])
    if argv[0] == "apply":
        apply(tree, root)
        return
    added = []
    rest = argv[3:]
    while rest:
        if rest[0] != "--add" or len(rest) < 2:
            raise SystemExit(__doc__)
        added.append(rest[1].strip("/"))
        rest = rest[2:]
    export(tree, root, added)


if __name__ == "__main__":
    main(sys.argv[1:])
