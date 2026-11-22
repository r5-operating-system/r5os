#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""Moves finished weeks of CHANGELOG.md, word for word, into
docs/changelog/YYYY-Www.md (ISO weeks, Monday to Sunday), and keeps the
list of those files at the top of CHANGELOG.md up to date.

CHANGELOG.md keeps the entries of the current week (or of --keep-week).
Each archive file is newest first, like CHANGELOG.md. An entry is
everything from its "## YYYY-MM-DD" heading to the next one; it is moved
byte for byte, and its place among the entries of its week stays the same.

Before anything is written the script checks that every entry is still
there exactly once (old files against new files); if not, it writes
nothing and fails. Run it when a new week starts (AGENTS.md, "Tracker and
changelog"). Running it twice changes nothing."""
from __future__ import annotations

import argparse
import datetime
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CHANGELOG = ROOT / "CHANGELOG.md"
ARCHIVE = ROOT / "docs" / "changelog"

HEADING = re.compile(r"^## (\d{4})-(\d{2})-(\d{2})\b", re.M)
INDEX_BEGIN = "<!-- changelog-archive: begin -->"
INDEX_END = "<!-- changelog-archive: end -->"
ARCHIVE_TITLE = "# R5OS changelog, week {week} ({first} to {last})"


def week_of(entry: str) -> str:
    match = HEADING.match(entry)
    if match is None:
        raise SystemExit("an entry without a dated heading:\n" + entry[:200])
    date = datetime.date(int(match[1]), int(match[2]), int(match[3]))
    year, week, _ = date.isocalendar()
    return f"{year}-W{week:02d}"


def week_range(week: str) -> tuple[datetime.date, datetime.date]:
    year, number = week.split("-W")
    monday = datetime.date.fromisocalendar(int(year), int(number), 1)
    return monday, monday + datetime.timedelta(days=6)


def split(text: str) -> tuple[str, list[str]]:
    """The part before the first entry, and the entries in file order."""
    starts = [m.start() for m in HEADING.finditer(text)]
    if not starts:
        return text, []
    head = text[:starts[0]]
    entries = [text[a:b] for a, b in zip(starts, starts[1:] + [len(text)])]
    return head, entries


def ending(entry: str, newline: str) -> str:
    # Every entry ends with exactly one blank line, so files join cleanly.
    return entry.rstrip("\r\n") + newline + newline


def read(path: pathlib.Path) -> str:
    return path.read_bytes().decode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--keep-week", help="the week to keep in "
        "CHANGELOG.md, as YYYY-Www (default: the current week)")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    today = datetime.date.today().isocalendar()
    keep = args.keep_week or f"{today[0]}-W{today[1]:02d}"

    text = read(CHANGELOG)
    newline = "\r\n" if "\r\n" in text else "\n"
    head, entries = split(text)

    # The head without an old index block.
    if INDEX_BEGIN in head:
        before, rest = head.split(INDEX_BEGIN, 1)
        after = rest.split(INDEX_END, 1)[1]
        head = before.rstrip("\r\n") + newline + newline + after.lstrip("\r\n")

    # What is already archived.
    archived: dict[str, list[str]] = {}
    for path in sorted(ARCHIVE.glob("*-W*.md")) if ARCHIVE.is_dir() else []:
        _, old = split(read(path))
        archived[path.stem] = old

    before_entries = sorted(e.rstrip("\r\n") for e in entries) + sorted(
        e.rstrip("\r\n") for old in archived.values() for e in old)

    kept: list[str] = []
    moving: dict[str, list[str]] = {}
    for entry in entries:
        week = week_of(entry)
        if week >= keep:
            kept.append(entry)
        else:
            moving.setdefault(week, []).append(entry)

    # Newer entries of a week go above the ones already archived.
    weeks = dict(archived)
    for week, new in moving.items():
        weeks[week] = new + weeks.get(week, [])

    after_entries = sorted(e.rstrip("\r\n") for e in kept) + sorted(
        e.rstrip("\r\n") for old in weeks.values() for e in old)
    if sorted(before_entries) != sorted(after_entries):
        raise SystemExit("check failed: the entries before and after differ;"
            " nothing written")

    # The index at the top of CHANGELOG.md.
    lines = [INDEX_BEGIN,
        "Older entries are in `docs/changelog/`, one file per ISO week"
        " (Monday to Sunday), moved word for word by"
        " `scripts/changelog-archive.py`. Search there too"
        " (`grep -r <term> CHANGELOG.md docs/changelog/`).",
        ""]
    for week in sorted(weeks, reverse=True):
        first, last = week_range(week)
        count = len(weeks[week])
        lines.append(f"- [{week}](docs/changelog/{week}.md): {first} to {last},"
            f" {count} {'entry' if count == 1 else 'entries'}")
    lines.append(INDEX_END)
    title, _, rest = head.partition(newline)
    new_head = (title + newline + newline + newline.join(lines) + newline
        + newline + rest.lstrip("\r\n"))
    if not weeks:
        new_head = head
    new_text = new_head + "".join(ending(e, newline) for e in kept)

    files = {CHANGELOG: new_text}
    for week, items in weeks.items():
        first, last = week_range(week)
        body = ARCHIVE_TITLE.format(week=week, first=first, last=last)
        body += newline + newline + "".join(ending(e, newline) for e in items)
        files[ARCHIVE / f"{week}.md"] = body

    moved = sum(len(v) for v in moving.values())
    print(f"keep {keep}: {len(kept)} entries stay, {moved} move into "
        f"{len(moving)} week files ({len(weeks)} archive files in all)")
    if args.dry_run:
        return 0

    ARCHIVE.mkdir(parents=True, exist_ok=True)
    for path, body in files.items():
        data = body.encode("utf-8")
        if not path.is_file() or path.read_bytes() != data:
            path.write_bytes(data)
            print(f"wrote {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
