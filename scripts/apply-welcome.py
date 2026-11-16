#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Copy the R5Book and the welcome page with its link into the Haiku tree.

distro/welcome -> data/r5/welcome (documentation/welcome)
docs/r5book    -> data/r5/r5book  (documentation/r5book)

The haiku package (distro/desktop/build/jam/packages/Haiku) takes them from
there. distro/welcome holds only Haiku's welcome_en.html with an R5Book
link; the rest of the welcome guide stays in the haiku_welcome package. The
R5Book is copied on every apply, so a change in docs/r5book is in the next
build without a second copy in distro/. The image copy differs from
docs/r5book in three ways, all for reading it offline in WebPositive:
no Google Fonts link (the CSS falls back to the system font), the links to
LIBR5.md and ICONS.md go to GitHub, and the pictures that live outside
docs/r5book are copied next to it.

Each destination is a mirror: a file removed from the source is removed
here. When anything changed, the top directory gets a new time, because
jam's CopyDirectoryToPackage only looks at that directory.
"""
from __future__ import annotations

import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "haiku")
TEXT = {".html", ".css", ".js", ".rdef", ".md", ".svg", ""}
GITHUB = "https://github.com/r5-operating-system/r5os/blob/main/docs/"

# Pictures the R5Book takes from outside docs/r5book: source, image path.
R5BOOK_EXTERNAL = {
    "../../distro/artwork/r5-symbol.svg": ("distro/artwork/r5-symbol.svg", "img/r5-symbol.svg"),
    "../design/login-mockup/icons/people.svg":
        ("docs/design/login-mockup/icons/people.svg", "img/people.svg"),
}


def text(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def r5book_page(name: str, data: bytes) -> bytes:
    page = data.decode("utf-8")
    if name.endswith(".html"):
        page = "".join(line for line in page.splitlines(keepends=True)
            if "fonts.googleapis.com" not in line and "fonts.gstatic.com" not in line)
        for old, (_, new) in R5BOOK_EXTERNAL.items():
            page = page.replace('src="' + old + '"', 'src="' + new + '"')
    page = re.sub(r'href="\.\./(LIBR5|ICONS)\.md"',
        lambda m: 'href="' + GITHUB + m.group(1) + '.md"', page)
    return page.encode("utf-8")


def mirror(files: dict[str, bytes], destination: pathlib.Path) -> int:
    """Write files (relative path -> bytes) to destination; return changes."""
    changed = 0
    destination.mkdir(parents=True, exist_ok=True)
    for relative, data in sorted(files.items()):
        target = destination / relative
        if target.is_file() and target.read_bytes() == data:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        changed += 1
    for path in sorted(destination.rglob("*"), reverse=True):
        relative = path.relative_to(destination).as_posix()
        if path.is_file() and relative not in files:
            path.unlink()
            changed += 1
        elif path.is_dir() and not any(path.iterdir()):
            path.rmdir()
    if changed:
        os.utime(destination)
    return changed


def collect(source: pathlib.Path, transform=None) -> dict[str, bytes]:
    if not source.is_dir():
        raise SystemExit(f"missing {source}")
    files = {}
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(source).as_posix()
        data = path.read_bytes()
        if path.suffix in TEXT:
            data = text(data)
            if transform:
                data = transform(relative, data)
        files[relative] = data
    if not files:
        raise SystemExit(f"no files under {source}")
    return files


def main() -> int:
    if not (HAIKU / "build" / "jam").is_dir():
        raise SystemExit(f"haiku tree not found: {HAIKU}")
    data = HAIKU / "data" / "r5"
    welcome = ROOT / "distro" / "welcome"
    r5book = collect(ROOT / "docs" / "r5book", r5book_page)
    for _, (source, published) in R5BOOK_EXTERNAL.items():
        path = ROOT / source
        if not path.is_file():
            raise SystemExit(f"missing {path}")
        r5book[published] = text(path.read_bytes())
    for name, files in (
            ("welcome", collect(welcome)),
            ("r5book", r5book)):
        changed = mirror(files, data / name)
        print(f"{'copied' if changed else 'already'} data/r5/{name}"
            f" ({len(files)} files, {changed} changed)")
    for page in r5book:
        if page.endswith(".html") and b"fonts.googleapis.com" in r5book[page]:
            raise SystemExit(f"r5book/{page}: Google Fonts link left")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
