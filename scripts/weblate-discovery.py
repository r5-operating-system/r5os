#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Set up the R5 Weblate project once, so every catalog folder becomes a
component by itself (docs/TRANSLATIONS.md, "Weblate").

What --apply does, in this order:

1. Checks that the components wifi and bluetooth_deskbar have nothing in
   Weblate that is not in the repository yet (nothing to commit or push).
   If they have, it stops and changes nothing.
2. Renames the component bluetooth to "preferences/r5bt", its folder, so
   every component is named after its folder. Its slug, history and
   translations stay.
3. Deletes the components wifi and bluetooth_deskbar. Their translations
   are in the repository (distro/catalogs), so nothing translated is lost;
   their Weblate history and comments are. It asks you to type "delete"
   first.
4. Turns on "Component discovery" on bluetooth. Weblate then creates one
   component per folder distro/catalogs/<folder>/ on main, including
   preferences/r5wifi and servers/bluetooth_deskbar again, and new ones
   after every push to main.

Run it yourself, in your own terminal. It asks for your Weblate API key
(hosted.weblate.org: your picture at the top right > Settings > API
access) and does not store it:

    py scripts/weblate-discovery.py            # show what it will do
    py scripts/weblate-discovery.py --apply    # do it
"""
from __future__ import annotations

import getpass
import json
import sys
import urllib.error
import urllib.request

API = "https://hosted.weblate.org/api"
PROJECT = "r5"
MAIN = "bluetooth"
MAIN_NAME = "preferences/r5bt"
REMOVE = ("wifi", "bluetooth_deskbar")
ADDON = "weblate.discovery.discovery"

CONFIGURATION = {
    "match": "distro/catalogs/(?P<component>.+)/(?P<language>[^/.]+)\\.po",
    "file_format": "po",
    "name_template": "{{ component }}",
    "base_file_template": "",
    "new_base_template": "distro/catalogs/{{ component }}/template.pot",
    "language_regex": "^[^.]+$",
    "copy_addons": True,
    "remove": False,
    "confirm": True,
}


def request(method: str, path: str, token: str, body: dict | None = None):
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(f"{API}{path}", data=data, method=method,
        headers={"Authorization": f"Token {token}",
            "Content-Type": "application/json",
            "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as response:
            return json.loads(response.read() or b"null")
    except urllib.error.HTTPError as error:
        raise SystemExit(f"{method} {path}: {error.code} "
            f"{error.read().decode('utf-8', 'replace')}")


def main(argv: list[str]) -> int:
    body = {"name": ADDON, "configuration": CONFIGURATION}
    print(f"1. check {', '.join(REMOVE)}: nothing left to commit or push")
    print(f"2. rename {PROJECT}/{MAIN} to {MAIN_NAME!r}")
    print(f"3. delete {', '.join(PROJECT + '/' + slug for slug in REMOVE)}")
    print(f"4. add to {PROJECT}/{MAIN}:")
    print(json.dumps(body, indent=2))
    if "--apply" not in argv:
        print("\nNothing sent. Run again with --apply to do it.")
        return 0

    token = getpass.getpass("Weblate API key (not shown, not stored): ").strip()
    if not token:
        raise SystemExit("no key given")

    components = request("GET", f"/projects/{PROJECT}/components/", token)
    slugs = {item["slug"] for item in (components or {}).get("results", [])}
    remove = [slug for slug in REMOVE if slug in slugs]
    for slug in remove:
        state = request("GET", f"/components/{PROJECT}/{slug}/repository/",
            token)
        if state.get("needs_commit") or state.get("needs_push"):
            raise SystemExit(f"{slug} has changes in Weblate that are not in "
                "the repository yet. Let Weblate commit and push them, merge "
                "its pull request, then run this again. Nothing changed.")
    print("1. checked: nothing left to commit or push")

    request("PATCH", f"/components/{PROJECT}/{MAIN}/", token,
        {"name": MAIN_NAME})
    print(f"2. {MAIN} is now named {MAIN_NAME!r}")

    if remove:
        answer = input(f"Type delete to delete {', '.join(remove)}: ")
        if answer.strip() != "delete":
            raise SystemExit("Not deleted. Steps 3 and 4 not done.")
        for slug in remove:
            request("DELETE", f"/components/{PROJECT}/{slug}/", token)
            print(f"3. deleted {slug}")
    else:
        print("3. nothing to delete")

    path = f"/components/{PROJECT}/{MAIN}/addons/"
    present = request("GET", path, token)
    if any(addon.get("name") == ADDON
            for addon in (present or {}).get("results", [])):
        print("4. Component discovery was already on")
    else:
        result = request("POST", path, token, body)
        print(f"4. Component discovery is on (add-on {result.get('id')})")
    print("Weblate now creates a component for each catalog folder on main.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
