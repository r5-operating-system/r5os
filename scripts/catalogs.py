#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Translation catalogs of R5's own programs (docs/TRANSLATIONS.md).

distro/catalogs/<subdir>/ holds, for one program:

  en.catkeys    the English source the build extracts (Haiku catalog);
  template.pot  the same texts as a gettext template, for Weblate;
  <lang>.po     one gettext file per language. Weblate edits these.

<subdir> is the folder of the program under objects/catalogs (the
HAIKU_CATALOGS_SUBDIR of DoCatalogs), for example preferences/r5bt.
A .po file keeps every text of the template, with an empty msgstr where
no one has translated it yet. A text marked "Needs editing" in Weblate
carries the "fuzzy" flag and is not used in the build.

  catalogs.py export <haiku tree> <repo root> [--add <subdir> ...]
      After a build: copy each objects/catalogs/<subdir>/en.catkeys into
      distro/catalogs/<subdir>/, write template.pot from it, and bring
      every .po file of that folder in line with it (new texts added
      untranslated, removed texts dropped, translations and the fuzzy
      flag kept). Only folders already in distro/catalogs are exported;
      --add starts a new one, with nl, de and fr.

  catalogs.py apply <haiku tree> <repo root>
      Before a build (apply-overlay.sh): turn each .po file into
      <haiku tree>/data/catalogs/<subdir>/<lang>.catkeys, without
      untranslated or fuzzy texts, so DoCatalogs links them and such a
      text stays English.
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


# Haiku catalogs ---------------------------------------------------------

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


def unescape(text, table):
    out, i = [], 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text):
            out.append(table.get(text[i + 1], "\\" + text[i + 1]))
            i += 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


# escapeQuotedChars() in Haiku's src/tools/locale/PlainTextCatalog.cpp.
CATKEYS_ESCAPES = {"n": "\n", "t": "\t", "\"": "\"", "\\": "\\"}
PO_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\"": "\"", "\\": "\\"}


def catkeys_escape(text):
    return (text.replace("\\", "\\\\").replace("\n", "\\n")
        .replace("\t", "\\t").replace("\"", "\\\""))


# gettext ----------------------------------------------------------------

def po_quote(text):
    return "\"" + (text.replace("\\", "\\\\").replace("\"", "\\\"")
        .replace("\n", "\\n").replace("\t", "\\t")
        .replace("\r", "\\r")) + "\""


def po_unquote(token, path, number):
    if len(token) < 2 or token[0] != "\"" or token[-1] != "\"":
        raise SystemExit(f"{path}:{number}: expected a quoted string")
    return unescape(token[1:-1], PO_ESCAPES)


def read_po(path):
    """Return (header or None, {(msgctxt, msgid): (msgstr, fuzzy)})."""
    header, entries = None, {}
    current, field, pending = None, None, set()

    def finish(entry):
        nonlocal header
        if entry is None or "msgid" not in entry:
            return
        if "msgid_plural" in entry:
            raise SystemExit(f"{path}: plural entries are not used; R5 "
                "writes plurals inside the text (ICU)")
        key = (entry.get("msgctxt", ""), entry["msgid"])
        if key == ("", ""):
            header = entry.get("msgstr", "")
        else:
            entries[key] = (entry.get("msgstr", ""),
                "fuzzy" in entry["flags"])

    for number, raw in enumerate(
            path.read_text(encoding="utf-8").split("\n"), start=1):
        line = raw.strip()
        if line.startswith("#~"):
            continue  # an obsolete text
        if line.startswith("#,"):
            pending |= {flag.strip() for flag in line[2:].split(",")}
            continue
        if line == "" or line.startswith("#"):
            continue
        if line.startswith("\""):
            if field is None:
                raise SystemExit(f"{path}:{number}: text without a keyword")
            current[field] += po_unquote(line, path, number)
            continue
        keyword, _, rest = line.partition(" ")
        if keyword.startswith("msgstr[") and keyword != "msgstr[0]":
            field = None
            continue
        keyword = "msgstr" if keyword == "msgstr[0]" else keyword
        if keyword not in ("msgctxt", "msgid", "msgid_plural", "msgstr"):
            raise SystemExit(f"{path}:{number}: unknown keyword {keyword!r}")
        starts = keyword == "msgctxt" or (keyword == "msgid"
            and (current is None or "msgid" in current))
        if starts:
            finish(current)
            current, pending = {"flags": pending}, set()
        elif current is None:
            raise SystemExit(f"{path}:{number}: {keyword} before msgid")
        current[keyword] = po_unquote(rest.strip(), path, number)
        field = keyword
    finish(current)
    return header, entries


def po_texts(entries):
    """The (msgctxt, msgid, comment) of each catkeys entry, in order."""
    texts, seen = [], set()
    for source, context, comment, _ in entries:
        key = (unescape(context, CATKEYS_ESCAPES),
            unescape(source, CATKEYS_ESCAPES))
        if key in seen:
            raise SystemExit(f"two texts {key!r} differ only in their "
                "comment; gettext cannot keep them apart")
        seen.add(key)
        texts.append((key[0], key[1], unescape(comment, CATKEYS_ESCAPES)))
    return texts


def write_po(path, header, texts, translations):
    out = ["msgid \"\"", "msgstr " + po_quote(header), ""]
    for context, source, comment in texts:
        translation, fuzzy = translations.get((context, source), ("", False))
        if comment:
            out.append(f"#. {comment}")
        if fuzzy and translation:
            out.append("#, fuzzy")
        out += ["msgctxt " + po_quote(context), "msgid " + po_quote(source),
            "msgstr " + po_quote(translation), ""]
    path.write_bytes("\n".join(out).encode("utf-8"))


def po_header(language, signature):
    lines = [f"Project-Id-Version: {signature}"]
    if language:
        lines.append(f"Language: {language}")
    lines += ["MIME-Version: 1.0", "Content-Type: text/plain; charset=UTF-8",
        "Content-Transfer-Encoding: 8bit"]
    return "".join(line + "\n" for line in lines)


def is_done(translations, context, source):
    translation, fuzzy = translations.get((context, source), ("", False))
    return translation != "" and not fuzzy


# Commands ---------------------------------------------------------------

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
        texts = po_texts(entries)
        write_po(target / "template.pot", po_header("", header[2]), texts, {})
        print(f"{subdir}: {len(texts)} texts ({header[2]})")
        languages = {path.stem for path in target.glob("*.po")}
        if subdir in added:
            languages |= set(NEW_LANGUAGES)
        for language in sorted(languages):
            path = target / f"{language}.po"
            po_head, translations = (read_po(path) if path.exists()
                else (None, {}))
            write_po(path, po_head if po_head is not None
                else po_header(language, header[2]), texts, translations)
            done = sum(1 for context, source, _ in texts
                if is_done(translations, context, source))
            print(f"  {language}: {done} of {len(texts)} translated")


def apply(tree, root):
    catalogs = root / "distro" / "catalogs"
    copied = 0
    for path in sorted(catalogs.rglob("*.po")):
        english = path.parent / "en.catkeys"
        if not english.exists():
            raise SystemExit(f"{english} is missing next to {path}")
        header, entries = read_catkeys(english)
        _, translations = read_po(path)
        result = [fields[:3] + [catkeys_escape(translations[(context,
                source)][0])]
            for (context, source, _), fields in zip(po_texts(entries), entries)
            if is_done(translations, context, source)]
        language = path.stem
        target = (tree / "data" / "catalogs"
            / path.parent.relative_to(catalogs) / f"{language}.catkeys")
        if not result:
            # Nothing translated: no catalog, so the program stays English.
            target.unlink(missing_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        write_catkeys(target, ["1", LANGUAGE_NAMES.get(language, language),
            header[2], header[3]], result)
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
