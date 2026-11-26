#!/usr/bin/env python3
"""Replace libvulkan_radeon.so and bump the package revision.

The previous file kept the name mesa_radv-25.3.6-1. An older copy of that
same name, from 9 Nov, could be the one the image installed. The new name
is mesa_radv-25.3.6-13, so a cached older build cannot be installed in its place.

pack-radv.py ROOT               -10 plus the freshly built library and the current
                                stick scripts (-13)
pack-radv.py ROOT --tools-only  -10 with its library unchanged and the current
                                stick scripts (boot-next knows m13 and m13f)
"""
import shutil
import subprocess
import sys
from pathlib import Path

HAIKU = Path("/home/nickr/r5os/haiku")
PKG = HAIKU / "generated.x86_64/objects/linux/x86_64/release/tools/package/package"
STRIP = HAIKU / "generated.x86_64/cross-tools-x86_64/bin/x86_64-unknown-haiku-strip"
SRC = Path("/home/nickr/r5os/mesa-build/mesa-25.3.6/build-radv/src/amd/vulkan/libvulkan_radeon.so")


def drop_build_path(path: Path) -> None:
    data = bytearray(path.read_bytes())
    needle = b"/home/nickr"
    start = 0
    while True:
        index = data.find(needle, start)
        if index < 0:
            break
        end = data.find(b"\x00", index)
        if end < 0:
            raise SystemExit(f"unterminated build path in {path}")
        replacement = b"/boot/system/lib"
        if end - index < len(replacement):
            raise SystemExit(f"build path is shorter than the system path in {path}")
        data[index:index + len(replacement)] = replacement
        data[index + len(replacement):end] = b"\x00" * (end - index - len(replacement))
        start = end
    path.write_bytes(data)


def fetch_base(root: Path, name: str) -> Path:
    """The base package is a pinned release asset (#4681), not a git file."""
    out = subprocess.check_output(
        ["bash", str(root / "scripts" / "r5-packages.sh"), "path", name], text=True)
    return Path(out.strip())


def main() -> None:
    root = Path(sys.argv[1])
    tools_only = len(sys.argv) > 2 and sys.argv[2] == "--tools-only"
    out_dir = root / "distro" / "packages" / "x86_64"
    base = "25.3.6-10"
    previous = fetch_base(root, f"mesa_radv-{base}-x86_64.hpkg")
    dest = out_dir / "mesa_radv-25.3.6-14-x86_64.hpkg"
    stage = Path("/tmp/r5-radv-pkg")
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    subprocess.check_call([str(PKG), "extract", "-C", str(stage), str(previous)])
    so = stage / "lib" / "libvulkan_radeon.so"
    if not tools_only:
        shutil.copy2(SRC, so)
        subprocess.check_call([str(STRIP), "--strip-unneeded", str(so)])
        drop_build_path(so)
    if b"/home/nickr" in so.read_bytes():
        raise SystemExit("libvulkan_radeon.so still contains /home/nickr")
    boot = root / "distro" / "graphics" / "boot"
    bindir = stage / "bin"
    bindir.mkdir(exist_ok=True)
    for name in ("boot2.sh", "check.sh", "m8-run", "m8-boot2", "m8-show",
                 "boot-next", "check", "check-user", "m11-vulkan", "m11-scroll",
                 "m12-measure", "m12-scenario", "m13-measure"):
        text = (boot / name).read_text(encoding="utf-8").replace("\r\n", "\n")
        (bindir / name).write_text(text, encoding="utf-8", newline="\n")
        (bindir / name).chmod(0o755)
    info = stage / ".PackageInfo"
    text = info.read_text(encoding="utf-8")
    text = text.replace(f"version\t\t\t{base}", "version\t\t\t25.3.6-14")
    text = text.replace(f"mesa_radv = {base}", "mesa_radv = 25.3.6-14")
    if "25.3.6-14" not in text or base in text:
        raise SystemExit("package version was not bumped")
    info.write_text(text, encoding="utf-8")
    if dest.exists():
        dest.unlink()
    subprocess.check_call([str(PKG), "create", "-C", str(stage), str(dest)])
    print("created", dest, dest.stat().st_size)


if __name__ == "__main__":
    main()
