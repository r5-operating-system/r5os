#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Install libr5security.so (distro/security) into the Haiku build tree:

- distro/security/*.cpp, R5Security.h, Jamfile -> src/preferences/r5security/
- distro/security/monocypher/monocypher.{c,h} (unchanged) ->
  src/preferences/r5security/monocypher/
- distro/security/R5Security.h -> headers/os/app/R5Security.h
- SubInclude HAIKU_TOP src preferences r5security ; in src/preferences/Jamfile
- libr5security.so in build/jam/images/definitions/{regular,minimum}

Run after apply-r5ui.py: its anchor is the libr5admin.so line that script
adds."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "haiku")
SECURITY = ROOT / "distro" / "security"
KEYSTORE = ROOT / "distro" / "keystore"

LIB_FILES = (
    "Jamfile",
    "PasswordHash.cpp",
    "R5Security.h",
    "Random.cpp",
    "SealedData.cpp",
    "SecureBuffer.cpp",
)
MONOCYPHER_FILES = ("monocypher.c", "monocypher.h")
KEYSTORE_FILES = (
    "AppAccessRequestWindow.cpp",
    "AppAccessRequestWindow.h",
    "Jamfile",
    "KeyRequestWindow.cpp",
    "KeyRequestWindow.h",
    "KeyStoreCrypto.cpp",
    "KeyStoreCrypto.h",
    "KeyStoreProtocol.h",
    "KeyStoreServer.cpp",
    "KeyStoreServer.h",
    "Keyring.cpp",
    "Keyring.h",
    "OldKeyStoreWindow.cpp",
    "OldKeyStoreWindow.h",
    "keystore_server.rdef",
)


def install(source: pathlib.Path, dest: pathlib.Path, preserve_bytes: bool = False) -> bool:
    if not source.is_file():
        raise SystemExit(f"missing {source}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    data = source.read_bytes()
    if not preserve_bytes:
        data = data.replace(b"\r\n", b"\n")
    if dest.is_file() and dest.read_bytes() == data:
        return False
    dest.write_bytes(data)
    return True


def install_all(pairs: list[tuple[pathlib.Path, pathlib.Path]], tag: str,
                preserve_bytes: bool = False) -> None:
    count = sum(1 for source, dest in pairs
                if install(source, dest, preserve_bytes=preserve_bytes))
    print(f"copied {count} {tag} files")


def insert_once(path: pathlib.Path, anchor: str, line: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if line in text:
        if text.count(line) != 1:
            raise SystemExit(f"{tag} is in {path} more than once")
        print(f"already {path.name} ({tag})")
        return
    if text.count(anchor) != 1:
        raise SystemExit(f"{tag} anchor not found once in {path}")
    path.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8",
                    newline="\n")
    print(f"patched {path.name} ({tag})")


def main() -> int:
    if not HAIKU.is_dir():
        raise SystemExit(f"missing {HAIKU}")

    lib = HAIKU / "src/preferences/r5security"
    install_all([(SECURITY / name, lib / name) for name in LIB_FILES],
                "libr5security")
    install_all(
        [(SECURITY / "monocypher" / name, lib / "monocypher" / name)
         for name in MONOCYPHER_FILES],
        "monocypher",
        preserve_bytes=True,
    )
    install_all(
        [(SECURITY / "R5Security.h", HAIKU / "headers/os/app/R5Security.h")],
        "libr5security header",
    )

    insert_once(
        HAIKU / "src/preferences/Jamfile",
        "SubInclude HAIKU_TOP src preferences r5admin ;\n",
        "SubInclude HAIKU_TOP src preferences r5security ;\n",
        "libr5security jam",
    )
    for profile in ("regular", "minimum"):
        insert_once(
            HAIKU / "build/jam/images/definitions" / profile,
            "\t\t\tlibr5admin.so\n",
            "\t\t\tlibr5security.so\n",
            "libr5security image " + profile,
        )

    # Encrypted keystore_server (Step 2)
    ks_dest = HAIKU / "src/servers/keystore"
    install_all(
        [(KEYSTORE / name, ks_dest / name) for name in KEYSTORE_FILES],
        "keystore_server",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
