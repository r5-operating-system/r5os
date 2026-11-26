#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Fail if an anyboot was built from a different tree than this branch.

The shared Haiku tree can still hold another branch's overlay. This compares
the radeon sources, the Login sources, and the built programs, and stops on
the first mismatch (and, for M11, "--paper" runs only the line and the
settings checks below, before an image exists):

- every radeon source at this commit, the worktree, and the Haiku tree
- every Login source apply-login.py copies, the same three ways
- the built r5-gpu-selftest, radeon_hd, app_server and Login against the image
- check.sh and boot2.sh in the image against this commit

write-stick.py runs this before it writes. A mix exits 1.
"""

from __future__ import annotations

import ast
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HAIKU = "/home/nickr/r5os/haiku"
GENERATED = f"{HAIKU}/generated.x86_64"
BFS = f"{GENERATED}/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
PKG = f"{GENERATED}/objects/linux/x86_64/release/tools/package/package"
OFFSET = "6291456"

PACKAGES = (
    "mesa_lavapipe-25.3.6-6-x86_64.hpkg",
    "mesa_radv-25.3.6-13-x86_64.hpkg",
    "vulkan_loader-1.4.367-5-x86_64.hpkg",
    "vulkan_tools-1.4.367-2-x86_64.hpkg",
    "vulkanteapot-1.0.0-6-x86_64.hpkg",
)

HAIKU_PKG = "haiku-r1~beta6_r5_2026.1_dev-1-x86_64.hpkg"
BUILT = {
    "r5-gpu-selftest": (
        f"{GENERATED}/objects/haiku/x86_64/release/bin/r5-gpu-selftest/r5-gpu-selftest",
        "bin/r5-gpu-selftest",
    ),
    "radeon_hd": (
        f"{GENERATED}/objects/haiku/x86_64/release/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd",
        "add-ons/kernel/drivers/dev/graphics/radeon_hd",
    ),
    "app_server": (
        f"{GENERATED}/objects/haiku/x86_64/release/servers/app/app_server",
        "servers/app_server",
    ),
    "Login": (
        f"{GENERATED}/objects/haiku/x86_64/release/apps/login/Login",
        "apps/Login",
    ),
}

# Haiku path, worktree path. apply-login.py copies these into src/apps/login.
LOGIN_SOURCES = (
    ("src/apps/login/DesktopWindow.cpp", "distro/login/DesktopWindow.cpp"),
    ("src/apps/login/DesktopWindow.h", "distro/login/DesktopWindow.h"),
    ("src/apps/login/Jamfile", "distro/login/Jamfile"),
    ("src/apps/login/Login.rdef", "distro/login/Login.rdef"),
    ("src/apps/login/LockWindow.cpp", "distro/login/LockWindow.cpp"),
    ("src/apps/login/LockWindow.h", "distro/login/LockWindow.h"),
    ("src/apps/login/LoginApp.cpp", "distro/login/LoginApp.cpp"),
    ("src/apps/login/LoginApp.h", "distro/login/LoginApp.h"),
    ("src/apps/login/LoginView.cpp", "distro/login/LoginView.cpp"),
    ("src/apps/login/LoginView.h", "distro/login/LoginView.h"),
    ("src/apps/login/LoginWindow.cpp", "distro/login/LoginWindow.cpp"),
    ("src/apps/login/LoginWindow.h", "distro/login/LoginWindow.h"),
    ("src/apps/login/R5Desktop.cpp", "distro/login/R5Desktop.cpp"),
    ("src/apps/login/SessionLog.h", "distro/login/SessionLog.h"),
    ("src/apps/login/TrayApp.cpp", "distro/login/TrayApp.cpp"),
    ("src/apps/login/main.cpp", "distro/login/main.cpp"),
    ("src/apps/login/AccountAdmin.cpp", "distro/users/AccountAdmin.cpp"),
    ("src/apps/login/AccountAdmin.h", "distro/users/AccountAdmin.h"),
    ("src/apps/login/HomeLock.cpp", "distro/users/HomeLock.cpp"),
    ("src/apps/login/HomeLock.h", "distro/users/HomeLock.h"),
)

SCRIPTS = {
    "check.sh": "distro/graphics/boot/check.sh",
    "boot2.sh": "distro/graphics/boot/boot2.sh",
    "m8-run": "distro/graphics/boot/m8-run",
    "m8-boot2": "distro/graphics/boot/m8-boot2",
    "m8-show": "distro/graphics/boot/m8-show",
    "check": "distro/graphics/boot/check",
    "boot-next": "distro/graphics/boot/boot-next",
    "check-user": "distro/graphics/boot/check-user",
    "m11-vulkan": "distro/graphics/boot/m11-vulkan",
    "m11-scroll": "distro/graphics/boot/m11-scroll",
    "m12-measure": "distro/graphics/boot/m12-measure",
    "m12-scenario": "distro/graphics/boot/m12-scenario",
    "m13-measure": "distro/graphics/boot/m13-measure",
}

# Files planted in /boot/home (plant-m8-stick.sh, M8). The M10 stick runs
# check, boot-next and check-user from /boot/system/bin, so nothing is
# planted.
HOME = ()

STRINGS = (b"gpu_isolation_test", b"window vkcube")

# M11 stick (generated/gpu-m11/BRIEF.md, "Stick (step 6)"). Every line the
# M11 part of check looks for: the pattern as check has it, and the text
# and the source file that print it.
COMPOSITOR = "distro/app_server/Compositor.cpp"
COMPOSITOR_VULKAN = "distro/app_server/CompositorVulkan.cpp"
COMPOSITOR_ANIM = "distro/app_server/CompositorAnimations.cpp"
COMPOSITOR_SHARE = "distro/app_server/CompositorShare.cpp"
KERNEL = "distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp"
SHARE = "distro/graphics/src/bin/r5-share-check/r5-share-check.cpp"
M11_LINES = (
    ("decorator: r5 loaded", "decorator: r5 loaded",
     "distro/desktop/src/add-ons/decorators/R5Decorator/R5DecoratorAddOn.cpp"),
    ("radeon_hd: gpu available", "radeon_hd: gpu available", KERNEL),
    ("radeon_hd: bo domain .* snooped 1", "bytes %llu snooped %d", KERNEL),
    ("radeon_hd: gpu hang ring", "radeon_hd: gpu hang ring", KERNEL),
    ("radeon_hd: gpu reset mode2 ok", "radeon_hd: gpu reset mode2 %s", KERNEL),
    ("radeon_hd: gpu selftest after reset ok", "gpu selftest after reset", KERNEL),
    ("gpu off until reboot", "gpu off until reboot", KERNEL),
    ("gpu lost", "gpu lost", KERNEL),
    ("gpu lost watchdog", '"gpu lost watchdog"', KERNEL),
    ("app_server: gpu compositor on radv", "gpu compositor on radv (%s)", COMPOSITOR),
    ("app_server: gpu compositor on lavapipe: forced", "gpu compositor on lavapipe: %s",
     COMPOSITOR),
    ("app_server: gpu compositor off", "gpu compositor off: %s", COMPOSITOR),
    ("gpu compositor off: forced failure", '"forced failure"', COMPOSITOR),
    ("app_server: gpu compositor restarted after reset",
     "gpu compositor restarted after reset", COMPOSITOR),
    ("app_server: compose compare ok", "compose compare ok", COMPOSITOR),
    ("compose compare MISMATCH", "compose compare MISMATCH", COMPOSITOR),
    ("app_server: compose median [0-9]+ us cpu [0-9]+ ms per 10 s", "compose median %",
     COMPOSITOR),
    ("compose effects shadow on frosted on", "compose effects shadow %s frosted %s",
     COMPOSITOR),
    ("app_server: window memory [0-9]+ MB", "window memory %", COMPOSITOR),
    ("app_server: window memory type", "window memory type %", COMPOSITOR_VULKAN),
    ("app_server: window buffer write fill", "window buffer write fill %", COMPOSITOR_VULKAN),
    ("zero-copy on", "\" zero-copy on\"", COMPOSITOR_SHARE),
    ("zero-copy off: setting", '"setting"', COMPOSITOR_SHARE),
    ("zero-copy off: lavapipe", '"lavapipe"', COMPOSITOR_SHARE),
    ("app_server: zero-copy setting off", "zero-copy setting %s", COMPOSITOR),
    ("app_server: team cpu", "team cpu %", COMPOSITOR),
    ("app_server: animation $a [0-9]+ ms frames [0-9]+", "animation workspace %",
     COMPOSITOR_ANIM),
    ("app_server: compositor paused: direct window", "compositor paused: direct window",
     COMPOSITOR_SHARE),
    ("app_server: compositor resumed", "compositor resumed", COMPOSITOR_SHARE),
    ("app_server: $e off", "app_server: %s %s",
     "distro/rest/src/servers/app/ServerApp.cpp"),
    ("second import of the same number refused", "second import of the same number refused",
     SHARE),
    ("not app_server refused", "import by a program that is not app_server refused", SHARE),
    ("hold: exported", "hold: exported", SHARE),
    ("r5-share-check: buffers", "r5-share-check: buffers %d", SHARE),
    ("^window fps [0-9]", "window fps %.1f", "distro/apps/vulkanteapot/VulkanWindow.cpp"),
    ("^selftest done", "selftest done",
     "distro/graphics/src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp"),
)

# The settings boot-next writes for each M11 boot, and nothing after boot 4.
M11_BASE = ["gpu_compositor true", "gpu_debug true", "gpu_selftest true"]
M11_BOOTS = {
    "1": M11_BASE,
    "2": M11_BASE + ["gpu_compositor_fail true"],
    "3": M11_BASE + ["gpu_compositor_lavapipe true"],
    "4": M11_BASE + ["gpu_hang_test 1"],
    "done": None,
}

# Programs the M11 stick runs, in the extracted packages.
M11_IMAGE = (
    "haiku/bin/r5-share-check",
    "haiku/bin/r5-effects-test",
    "haiku/demos/Chart",
    "haiku/add-ons/decorators/R5Decorator",
    "teapot/bin/VulkanTeapot",
    "tools/bin/vkcube",
)
M11_STRINGS = {
    "haiku/servers/app_server": (b" zero-copy on", b"team cpu", b"window buffer write fill",
                                 b"zero-copy setting", b"compositor paused: direct window"),
    "teapot/bin/VulkanTeapot": (b"window fps", b"--seconds"),
}


# M12 measurement stick (generated/gpu-m12/STICK-PLAN.md). The lines the
# M12 part of check looks for, as for M11. Boot 4 is M11's boot 4.
DRAW_COMPARE = "distro/graphics/src/bin/r5-draw-compare/r5-draw-compare.cpp"
M12_LINES = (
    ("app_server: gpu draw on .*RADV", "gpu draw on %s skia %s", COMPOSITOR),
    ("app_server: gpu draw on llvmpipe", "gpu draw on %s skia %s", COMPOSITOR),
    ("app_server: gpu draw off", "gpu draw off: %s", COMPOSITOR),
    ("app_server: gpu draw budget heap [0-9]+ MB", "gpu draw budget heap %zu MB",
     "distro/app_server/skia/SkiaGpu.cpp"),
    ("app_server: gpu draw warm up [0-9]+ ms", "gpu draw warm up %",
     "distro/app_server/skia/SkiaOffscreen.cpp"),
    ("app_server: $1 median [0-9]+ us cpu [0-9]+ ms per 10 s", "draw median %",
     COMPOSITOR),
    ("m12_scenario 'draw cpu'", "draw cpu median %", COMPOSITOR),
    ("app_server: gpu compositor on lavapipe", "gpu compositor on lavapipe: %s",
     COMPOSITOR),
    ("draw-compare [0-9]+/[0-9]+ ok", "draw-compare %d/%d ok", DRAW_COMPARE),
    ("^draw-time ", "draw-time %s gpu %", DRAW_COMPARE),
    ("scenario done", "scenario done", "distro/graphics/boot/m12-scenario"),
)
M12_DRAW = ["gpu_draw true"]
M12_BASE = ["gpu_debug true", "gpu_selftest true"]
M12_BOOTS = {
    "1": M12_DRAW + M12_BASE,
    "2": M12_BASE,
    "3": M12_DRAW + ["gpu_compositor_lavapipe true"] + M12_BASE,
    "4": M11_BOOTS["4"],
    "done": None,
}
M12_STRINGS = {
    "haiku/servers/app_server": (b"gpu draw on", b"gpu draw warm up", b"draw cpu median"),
    "haiku/bin/r5-draw-compare": (b"--new-look", b"draw-time"),
}


# M13 stick (generated/m13/STICK-PLAN.md): every check line has a source
# that prints it, and boot-next writes these settings per boot.
M13_LINES = (
    ("app_server: lavapipe vector width 128", "lavapipe vector width %d (setting)",
     COMPOSITOR),
    ("app_server: scanout through vulkan [0-9]", "scanout through vulkan %", COMPOSITOR),
    ("gpu draw pipeline cache [a-z]+", "gpu draw pipeline cache %s",
     "distro/app_server/skia/SkiaGpu.cpp"),
    ("page table pool full", "page table pool full used",
     "distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp"),
    ("compose compare MISMATCH", "compose compare MISMATCH", COMPOSITOR),
    ("m13-measure: per motion", "per motion, the worst 10 s window",
     "distro/graphics/boot/m13-measure"),
    ("app_server: gpu compositor off", "gpu compositor off: %s", COMPOSITOR),
    # M13 step 4 (#4396): the Lavapipe warm-up in parts, boots 3 and 4
    ("app_server: gpu draw warm-up parts 8", "gpu draw warm-up parts %",
     "distro/app_server/skia/SkiaOffscreen.cpp"),
    ("app_server: gpu draw warm up status", "gpu draw warm up status %s",
     "distro/app_server/skia/SkiaOffscreen.cpp"),
)
M13_BASE = ["gpu_draw true", "gpu_debug true", "gpu_selftest true"]
M13_BOOTS = {
    "1": M13_BASE + ["gpu_m13_boot 1"],
    "2": M13_BASE + ["gpu_app_server true", "gpu_compositor_scanout true",
                     "gfx9_wc_gtt true", "gpu_m13_boot 2"],
    "3": M13_BASE + ["gpu_compositor_lavapipe true", "gpu_lavapipe_vector 128",
                     "gpu_m13_boot 3"],
    # the second Lavapipe start: Mesa's shader cache from boot 3
    "4": M13_BASE + ["gpu_compositor_lavapipe true", "gpu_lavapipe_vector 128",
                     "gpu_m13_boot 4"],
    "done": None,
}

# The M13 follow-up stick (boot-next m13f): defaults with gpu_debug, then
# scanout, write-combined GTT, gpu false, and the 8 s warm-up wait (#4475).
M13F_BOOTS = {
    "1": ["gpu_debug true", "gpu_m13f_boot 1"],
    "2": ["gpu_debug true", "gpu_app_server true", "gpu_compositor_scanout true",
          "gpu_m13f_boot 2"],
    "3": ["gpu_debug true", "gfx9_wc_gtt true", "gpu_m13f_boot 3"],
    "4": ["gpu_debug true", "gpu false", "gpu_m13f_boot 4"],
    "5": ["gpu_debug true", "gpu_draw_settle true", "gpu_m13f_boot 5"],
    "done": None,
}

# The round 3 stick (stick 75, boot-next round3): one boot with every GPU
# switch at its default; boot-next only saves the results and never writes
# other settings or restarts.
R3_BOOT = ["r5_round3_boot 1"]


# What the M13 stick's app_server must be built with (steps 2 and 3).
M13_STRINGS = {
    "haiku/servers/app_server": (b"lavapipe threads %s (default)",
                                 b"compose work per frame",
                                 b"gpu draw hardware blend off",
                                 b"gpu draw pipeline cache %s",
                                 b"compose passes us per frame",
                                 # M13 step 4 (#4396)
                                 b"gpu draw warm-up parts %"),
    # the GPU stop before a restart or power off (PR #4355)
    "haiku/add-ons/kernel/drivers/dev/graphics/radeon_hd": (b"shutdown gpu stop",
                                                            b"r5_register_shutdown_hook",
                                                            # #4363
                                                            b"tmr destroy"),
}


def check_m13_sources(commit: str) -> None:
    check = blob(commit, "distro/graphics/boot/check").decode("utf-8")
    for pattern, text, path in M13_LINES:
        if pattern not in check:
            die(f"check has no M13 line {pattern!r}")
        if text.encode() not in blob(commit, path):
            die(f"{path} at {commit} does not print {text!r} (check: {pattern!r})")
    print(f"m13 lines {len(M13_LINES)} have a source at {commit}")


def check_m13_boots(commit: str) -> None:
    """The M13 order 1, 2, 3, 4, done, in a sandbox in WSL."""
    stage = ROOT / "generated" / "_pre-stick-m13"
    stage.mkdir(parents=True, exist_ok=True)
    for name in ("boot-next", "check"):
        data = blob(commit, f"distro/graphics/boot/{name}").replace(b"\r\n", b"\n")
        (stage / name).write_bytes(data)
    steps = ["m13", "", "", "", ""]
    script = ["#!/bin/sh", "set -u", "T=/tmp/r5-pre-stick-m13", "rm -rf $T",
              "mkdir -p $T/home $T/us $T/ss $T/cache",
              "echo 'selftest done' > $T/cache/r5-gpu-selftest.txt",
              "echo x > $T/syslog",
              "export R5_HOME=$T/home R5_USER_SETTINGS=$T/us R5_SYSTEM_SETTINGS=$T/ss",
              "export R5_SYSLOG=$T/syslog R5_CACHE=$T/cache CHECK_WAIT=0 CHECK_LIVE=0",
              "export BOOT_NEXT_YES=0", 'D="$(dirname "$0")"']
    for i, arg in enumerate(steps):
        script.append(f'sh "$D/boot-next" {arg} > /dev/null || echo "step {i} boot-next failed"')
        script.append(f'echo "== step {i} next $(cat $T/home/m13/next)"')
        script.append("cat $T/us/kernel/drivers/radeon_hd 2>/dev/null || echo '(none)'")
        script.append(f'sh "$D/check" 2>/dev/null | grep "^boot " || true')
    (stage / "run.sh").write_text("\n".join(script) + "\n", encoding="utf-8", newline="\n")
    path = wsl(f"wslpath -u '{stage / 'run.sh'}'").strip()
    out = wsl(f"sh '{path}'")
    blocks = out.split("== step ")[1:]
    if len(blocks) != len(steps):
        die(f"m13 boot-next sandbox: {out}")
    for block in blocks:
        lines = [line for line in block.strip().splitlines() if line]
        boot = lines[0].split("next ", 1)[1]
        got = [line for line in lines[1:] if not line.startswith("boot ")]
        want = M13_BOOTS.get(boot, "unknown")
        if want is None:
            if got != ["(none)"]:
                die(f"m13: after boot 4 a settings file stays: {got}")
        elif got != want:
            die(f"m13 boot {boot}: boot-next wrote {got}, want {want}")
        else:
            seen = [line for line in lines if line.startswith("boot ")]
            if seen != [f"boot {boot} (m13)"]:
                die(f"m13 boot {boot}: check says {seen}")
        print(f"m13 boot {boot} settings ok")


def check_m13f_boots(commit: str) -> None:
    """The M13f order from a planted boot 1: 2, 3, 4, 5, done, in WSL."""
    stage = ROOT / "generated" / "_pre-stick-m13f"
    stage.mkdir(parents=True, exist_ok=True)
    data = blob(commit, "distro/graphics/boot/boot-next").replace(b"\r\n", b"\n")
    (stage / "boot-next").write_bytes(data)
    first = "\\n".join(M13F_BOOTS["1"])
    script = ["#!/bin/sh", "set -u", "T=/tmp/r5-pre-stick-m13f", "rm -rf $T",
              "mkdir -p $T/home $T/us/kernel/drivers $T/ss $T/cache",
              "echo 'selftest done' > $T/cache/r5-gpu-selftest.txt",
              "echo x > $T/syslog",
              "export R5_HOME=$T/home R5_USER_SETTINGS=$T/us R5_SYSTEM_SETTINGS=$T/ss",
              "export R5_SYSLOG=$T/syslog R5_CACHE=$T/cache CHECK_WAIT=0 CHECK_LIVE=0",
              "export BOOT_NEXT_YES=0", 'D="$(dirname "$0")"',
              f"printf '{first}\\n' > $T/us/kernel/drivers/radeon_hd"]
    for i in range(5):
        script.append(f'sh "$D/boot-next" > /dev/null || echo "step {i} boot-next failed"')
        script.append(f'echo "== step {i} next $(cat $T/home/m13f/next)"')
        script.append("cat $T/us/kernel/drivers/radeon_hd 2>/dev/null || echo '(none)'")
    (stage / "run.sh").write_text("\n".join(script) + "\n", encoding="utf-8", newline="\n")
    path = wsl(f"wslpath -u '{stage / 'run.sh'}'").strip()
    out = wsl(f"sh '{path}'")
    blocks = out.split("== step ")[1:]
    if len(blocks) != 5:
        die(f"m13f boot-next sandbox: {out}")
    for block in blocks:
        lines = [line for line in block.strip().splitlines() if line]
        boot = lines[0].split("next ", 1)[1]
        got = lines[1:]
        want = M13F_BOOTS.get(boot, "unknown")
        if want is None:
            if got != ["(none)"]:
                die(f"m13f: after boot 5 a settings file stays: {got}")
        elif got != want:
            die(f"m13f boot {boot}: boot-next wrote {got}, want {want}")
        print(f"m13f boot {boot} settings ok")


def check_r3_boots(commit: str) -> None:
    """Round 3 from the planted file: boot-next saves twice (5625U, then a
    second computer), keeps the file as it is and restarts nothing."""
    stage = ROOT / "generated" / "_pre-stick-r3"
    stage.mkdir(parents=True, exist_ok=True)
    data = blob(commit, "distro/graphics/boot/boot-next").replace(b"\r\n", b"\n")
    (stage / "boot-next").write_bytes(data)
    script = ["#!/bin/sh", "set -u", "T=/tmp/r5-pre-stick-r3", "rm -rf $T",
              "mkdir -p $T/home $T/us/kernel/drivers $T/ss",
              "printf 'KERN: CPU 0: updated from revision 0x1 to 0x2\\n' > $T/syslog",
              "export R5_HOME=$T/home R5_USER_SETTINGS=$T/us R5_SYSTEM_SETTINGS=$T/ss",
              "export R5_SYSLOG=$T/syslog BOOT_NEXT_YES=0", 'D="$(dirname "$0")"',
              "printf '" + "\\n".join(R3_BOOT) + "\\n' > $T/us/kernel/drivers/radeon_hd"]
    for i in (1, 2):
        script.append(f'sh "$D/boot-next" > $T/out{i} 2>&1 || echo "step {i} boot-next failed"')
        script.append(f'grep -q "^PASS" $T/out{i} || echo "step {i} no PASS"')
        script.append(f'grep -q "updated from" $T/out{i} || echo "step {i} no microcode line"')
        for kind in ("power", "card", "ucode"):
            script.append(f'test -s $T/home/round3/{i}-{kind}.txt || echo "no collect {i}-{kind}"')
    script.append('echo "== settings"')
    script.append("cat $T/us/kernel/drivers/radeon_hd 2>/dev/null || echo '(none)'")
    (stage / "run.sh").write_text("\n".join(script) + "\n", encoding="utf-8", newline="\n")
    path = wsl(f"wslpath -u '{stage / 'run.sh'}'").strip()
    out = wsl(f"sh '{path}'")
    head, _, tail = out.partition("== settings")
    if head.strip():
        die(f"round3 boot-next sandbox: {out}")
    got = [line for line in tail.strip().splitlines() if line]
    if got != R3_BOOT:
        die(f"round3: boot-next changed the settings to {got}, want {R3_BOOT}")
    print("round3 boot-next saves twice, settings kept, no restart")


def check_m12_sources(commit: str) -> None:
    check = blob(commit, "distro/graphics/boot/check").decode("utf-8")
    for pattern, text, path in M12_LINES:
        if pattern not in check:
            die(f"check has no M12 line {pattern!r}")
        if text.encode() not in blob(commit, path):
            die(f"{path} at {commit} does not print {text!r} (check: {pattern!r})")
    # M13 stick: the planted boot 1 is M13's (an M12 stick had M12's)
    settings = blob(commit, "distro/graphics/boot/radeon_hd").decode("utf-8").split()
    if " ".join(settings) not in (" ".join(" ".join(M12_BOOTS["1"]).split()),
            " ".join(" ".join(M13_BOOTS["1"]).split()),
            " ".join(" ".join(M13F_BOOTS["1"]).split()),
            " ".join(R3_BOOT)):
        die(f"distro/graphics/boot/radeon_hd is not M12, M13, M13f or round 3 boot 1: {settings}")
    print(f"m12 lines {len(M12_LINES)} have a source at {commit}")


def check_m12_boots(commit: str) -> None:
    """The M12 order 1, 2, 3, 4 (M11's), done, in a sandbox in WSL."""
    stage = ROOT / "generated" / "_pre-stick-m12"
    stage.mkdir(parents=True, exist_ok=True)
    for name in ("boot-next", "check"):
        data = blob(commit, f"distro/graphics/boot/{name}").replace(b"\r\n", b"\n")
        (stage / name).write_bytes(data)
    steps = ["m12", "", "", "", ""]
    script = ["#!/bin/sh", "set -u", "T=/tmp/r5-pre-stick-m12", "rm -rf $T",
              "mkdir -p $T/home $T/us $T/ss $T/cache",
              "echo 'selftest done' > $T/cache/r5-gpu-selftest.txt",
              "echo x > $T/syslog",
              "export R5_HOME=$T/home R5_USER_SETTINGS=$T/us R5_SYSTEM_SETTINGS=$T/ss",
              "export R5_SYSLOG=$T/syslog R5_CACHE=$T/cache CHECK_WAIT=0 CHECK_LIVE=0",
              "export BOOT_NEXT_YES=0", 'D="$(dirname "$0")"']
    for i, arg in enumerate(steps):
        script.append(f'sh "$D/boot-next" {arg} > /dev/null || echo "step {i} boot-next failed"')
        script.append(f'echo "== step {i} next $(cat $T/home/m12/next)"')
        script.append("cat $T/us/kernel/drivers/radeon_hd 2>/dev/null || echo '(none)'")
        script.append(f'sh "$D/check" 2>/dev/null | grep "^boot " || true')
    (stage / "run.sh").write_text("\n".join(script) + "\n", encoding="utf-8", newline="\n")
    path = wsl(f"wslpath -u '{stage / 'run.sh'}'").strip()
    out = wsl(f"sh '{path}'")
    blocks = out.split("== step ")[1:]
    if len(blocks) != len(steps):
        die(f"m12 boot-next sandbox: {out}")
    for block in blocks:
        lines = [line for line in block.strip().splitlines() if line]
        boot = lines[0].split("next ", 1)[1]
        # after boot 4 the M11 note says done; the M12 note stays at 4
        got = [line for line in lines[1:] if not line.startswith("boot ")]
        if boot == "4" and got == ["(none)"]:
            boot = "done"
        want = M12_BOOTS.get(boot, "unknown")
        if want is None:
            if got != ["(none)"]:
                die(f"m12: after boot 4 a settings file stays: {got}")
        elif got != want:
            die(f"m12 boot {boot}: boot-next wrote {got}, want {want}")
        else:
            seen = [line for line in lines if line.startswith("boot ")]
            stick = "m11" if boot == "4" else "m12"
            if seen != [f"boot {boot} ({stick})"]:
                die(f"m12 boot {boot}: check says {seen}")
        print(f"m12 boot {boot} settings ok")


def check_m11_sources(commit: str) -> None:
    check = blob(commit, "distro/graphics/boot/check").decode("utf-8")
    for pattern, text, path in M11_LINES:
        if pattern not in check:
            die(f"check has no M11 line {pattern!r}")
        if text.encode() not in blob(commit, path):
            die(f"{path} at {commit} does not print {text!r} (check: {pattern!r})")
    print(f"m11 lines {len(M11_LINES)} have a source at {commit}")


def check_m11_boots(commit: str) -> None:
    """boot-next and check from this commit, in a sandbox in WSL."""
    stage = ROOT / "generated" / "_pre-stick-m11"
    stage.mkdir(parents=True, exist_ok=True)
    for name in ("boot-next", "check"):
        data = blob(commit, f"distro/graphics/boot/{name}").replace(b"\r\n", b"\n")
        (stage / name).write_bytes(data)
    steps = ["m11", "", "", "", ""]
    script = ["#!/bin/sh", "set -u", "T=/tmp/r5-pre-stick-m11", "rm -rf $T",
              "mkdir -p $T/home $T/us $T/ss $T/cache",
              "echo 'selftest done' > $T/cache/r5-gpu-selftest.txt",
              "echo x > $T/syslog",
              "export R5_HOME=$T/home R5_USER_SETTINGS=$T/us R5_SYSTEM_SETTINGS=$T/ss",
              "export R5_SYSLOG=$T/syslog R5_CACHE=$T/cache CHECK_WAIT=0 CHECK_LIVE=0",
              "export BOOT_NEXT_YES=0", 'D="$(dirname "$0")"']
    for i, arg in enumerate(steps):
        script.append(f'sh "$D/boot-next" {arg} > /dev/null || echo "step {i} boot-next failed"')
        script.append(f'echo "== step {i} next $(cat $T/home/m11/next)"')
        script.append("cat $T/us/kernel/drivers/radeon_hd 2>/dev/null || echo '(none)'")
        script.append(f'sh "$D/check" 2>/dev/null | grep "^boot " || true')
    (stage / "run.sh").write_text("\n".join(script) + "\n", encoding="utf-8", newline="\n")
    path = wsl(f"wslpath -u '{stage / 'run.sh'}'").strip()
    out = wsl(f"sh '{path}'")
    blocks = out.split("== step ")[1:]
    if len(blocks) != len(steps):
        die(f"boot-next sandbox: {out}")
    for block in blocks:
        lines = [line for line in block.strip().splitlines() if line]
        boot = lines[0].split("next ", 1)[1]
        want = M11_BOOTS.get(boot, "unknown")
        got = [line for line in lines[1:] if not line.startswith("boot ")]
        if want is None:
            if got != ["(none)"]:
                die(f"after boot 4 a settings file stays: {got}")
        elif got != want:
            die(f"boot {boot}: boot-next wrote {got}, want {want}")
        else:
            seen = [line for line in lines if line.startswith("boot ")]
            if seen != [f"boot {boot} (m11)"]:
                die(f"boot {boot}: check says {seen}")
        print(f"m11 boot {boot} settings ok")


def die(message: str) -> None:
    print(f"MIX: {message}", file=sys.stderr)
    raise SystemExit(1)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def head_commit() -> str:
    try:
        out = subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        die(f"cannot read the branch head: {error}")
    return out.strip()


def blob(commit: str, relative: str) -> bytes:
    try:
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "show", f"{commit}:{relative}"])
    except subprocess.CalledProcessError:
        die(f"{relative} is not in {commit}")


def at_head(commit: str, relative: str) -> None:
    """The worktree file is the file at this commit. Git's filters apply."""
    result = subprocess.run(
        ["git", "-C", str(ROOT), "diff", "--quiet", commit, "--", relative],
        capture_output=True, text=True)
    if result.returncode != 0:
        die(f"{relative} in the worktree is not {commit}")


def source_list() -> tuple[str, ...]:
    path = ROOT / "scripts" / "apply-radeon.py"
    module = ast.parse(path.read_text(encoding="utf-8"))
    for node in module.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id == "FILES":
                value = ast.literal_eval(node.value)
                return tuple(value)
    die("apply-radeon.py has no FILES list")


def wsl(script: str) -> str:
    try:
        out = subprocess.check_output(
            ["wsl", "-e", "bash", "-lc", script], text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        die(error.stdout or str(error))
    return out


def tree_hashes(relatives: list[str]) -> dict[str, str]:
    lines = "\n".join(
        f"sha256sum {HAIKU}/{relative}" for relative in relatives)
    text = wsl(lines)
    found = {}
    for line in text.splitlines():
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        digest, name = parts
        relative = name.replace(f"{HAIKU}/", "", 1)
        found[relative] = digest
    return found


def check_sources(commit: str) -> None:
    copied = list(source_list())
    loose = list(SCRIPTS.values()) + [
        "distro/graphics/src/bin/r5-wsi-window-test/r5-wsi-window-test.sh"]
    tree = tree_hashes(copied)
    matched = 0
    for haiku_rel in copied:
        relative = f"distro/graphics/{haiku_rel}"
        at_head(commit, relative)
        work = ROOT / relative
        if not work.is_file():
            die(f"{relative} is missing in the worktree at {commit}")
        digest = sha256(work.read_bytes())
        if tree.get(haiku_rel) != digest:
            die(
                f"{haiku_rel} in the Haiku tree is not the worktree copy at {commit}: "
                f"tree {tree.get(haiku_rel)} worktree {digest}")
        matched += 1
    print(f"sources {matched} match {commit}")
    for relative in loose:
        at_head(commit, relative)
    check_login_sources(commit)


def check_login_sources(commit: str) -> None:
    tree = tree_hashes([haiku_rel for haiku_rel, _work in LOGIN_SOURCES])
    matched = 0
    for haiku_rel, relative in LOGIN_SOURCES:
        at_head(commit, relative)
        work = ROOT / relative
        if not work.is_file():
            die(f"{relative} is missing in the worktree at {commit}")
        digest = sha256(work.read_bytes())
        if tree.get(haiku_rel) != digest:
            die(
                f"{haiku_rel} in the Haiku tree is not the worktree copy at {commit}: "
                f"tree {tree.get(haiku_rel)} worktree {digest}")
        matched += 1
    print(f"login sources {matched} match {commit}")


def extract(iso_wsl: str, out: str) -> None:
    gets = [f"cp myfs/system/packages/{HAIKU_PKG} :{out}/{HAIKU_PKG}"]
    for package in PACKAGES:
        gets.append(f"cp myfs/system/packages/{package} :{out}/{package}")
    script = ROOT / "generated" / "_pre-stick-extract.sh"
    script.write_text(
        "#!/bin/bash\nset -eu\n"
        f"rm -rf {out}\nmkdir -p {out}/haiku {out}/radv {out}/teapot {out}/tools\n"
        f"'{BFS}' --start-offset {OFFSET} '{iso_wsl}' <<EOF\n"
        + "\n".join(gets) + "\nquit\nEOF\n"
        f"'{PKG}' extract -C {out}/haiku {out}/{HAIKU_PKG}\n"
        f"'{PKG}' extract -C {out}/radv {out}/mesa_radv-25.3.6-13-x86_64.hpkg\n"
        f"'{PKG}' extract -C {out}/teapot {out}/vulkanteapot-1.0.0-6-x86_64.hpkg\n"
        f"'{PKG}' extract -C {out}/tools {out}/vulkan_tools-1.4.367-2-x86_64.hpkg\n"
        f"test -s {out}/haiku/bin/r5-gpu-selftest\n"
        f"test -s {out}/haiku/servers/app_server\n"
        f"test -s {out}/haiku/apps/Login\n"
        f"test -s {out}/haiku/add-ons/kernel/drivers/dev/graphics/radeon_hd\n"
        f"test -s {out}/radv/bin/check.sh\ntest -s {out}/radv/bin/boot2.sh\n"
        f"test -s {out}/radv/bin/m8-run\n"
        f"test -s {out}/radv/bin/m8-boot2\n"
        f"test -s {out}/radv/bin/m8-show\n"
        f"test -s {out}/radv/bin/check\n"
        f"test -s {out}/radv/bin/boot-next\n"
        f"test -s {out}/radv/bin/check-user\n"
        f"test -s {out}/radv/bin/m11-vulkan\n"
        f"test -s {out}/radv/bin/m11-scroll\n"
        f"test -s {out}/radv/bin/m12-measure\n"
        f"test -s {out}/radv/bin/m12-scenario\n"
        f"test -s {out}/radv/bin/m13-measure\n"
        f"test -s {out}/teapot/bin/r5-wsi-window-test.sh\n"
        f"mkdir -p {out}/home\n"
        f"'{BFS}' --start-offset {OFFSET} '{iso_wsl}' <<EOF\n"
        + "".join(
            f"cp myfs/home/{name} :{out}/home/{name}\n" for name in HOME)
        + f"cp myfs/home/config/settings/kernel/drivers/radeon_hd :{out}/home/radeon_hd\n"
        + f"cp myfs/home/config/settings/kernel/drivers/kernel :{out}/home/kernel\n"
        "quit\nEOF\n",
        encoding="utf-8", newline="\n")
    wsl_script = wsl(f"wslpath -u '{script}'").strip()
    wsl(f"sed -i 's/\\r$//' '{wsl_script}' && bash '{wsl_script}'")


def file_sha_wsl(path: str) -> str:
    line = wsl(f"sha256sum '{path}'").strip().split()
    if not line:
        die(f"no hash for {path}")
    return line[0]


def check_image(commit: str, iso: str) -> None:
    iso_wsl = wsl(f"wslpath -u '{iso}'").strip()
    out = "/tmp/r5-pre-stick"
    extract(iso_wsl, out)
    selftest = Path(wsl(f"wslpath -w {out}/haiku/bin/r5-gpu-selftest").strip())
    data = selftest.read_bytes()
    for needle in STRINGS:
        if needle not in data:
            die(f"r5-gpu-selftest has no {needle.decode()}; the image is the old selftest")
        print(f"string {needle.decode()}")
    for name, (built, image) in BUILT.items():
        image_sha = file_sha_wsl(f"{out}/haiku/{image}")
        built_sha = file_sha_wsl(built)
        if image_sha != built_sha:
            die(
                f"{name} in the image is not the build from {commit}: "
                f"image {image_sha} build {built_sha}")
        print(f"binary {name} {image_sha}")
    for name, relative in SCRIPTS.items():
        image_sha = file_sha_wsl(f"{out}/radv/bin/{name}")
        committed = sha256(blob(commit, relative))
        if image_sha != committed:
            die(
                f"{name} in the image is not {commit}: "
                f"image {image_sha} branch {committed}")
        print(f"script {name} {image_sha}")
    window = sha256(blob(
        commit, "distro/graphics/src/bin/r5-wsi-window-test/r5-wsi-window-test.sh"))
    packed = file_sha_wsl(f"{out}/teapot/bin/r5-wsi-window-test.sh")
    if packed != window:
        die(
            f"r5-wsi-window-test.sh in the image is not {commit}: "
            f"image {packed} branch {window}")
    print(f"script r5-wsi-window-test.sh {packed}")
    for package in PACKAGES:
        image_sha = file_sha_wsl(f"{out}/{package}")
        disk = sha256((ROOT / "distro" / "packages" / "x86_64" / package).read_bytes())
        if image_sha != disk:
            die(f"{package} in the image is {image_sha}, the branch copy is {disk}")
        print(f"package {package} {image_sha}")
    for name in HOME:
        image_sha = file_sha_wsl(f"{out}/home/{name}")
        committed = sha256(blob(commit, f"distro/graphics/boot/{name}"))
        if image_sha != committed:
            die(
                f"home {name} is not {commit}: "
                f"image {image_sha} branch {committed}")
        print(f"home {name} {image_sha}")
    settings = file_sha_wsl(f"{out}/home/radeon_hd")
    wanted = sha256(blob(commit, "distro/graphics/boot/radeon_hd"))
    if settings != wanted:
        die(
            f"boot 1 settings are not {commit}: "
            f"image {settings} branch {wanted}")
    print(f"home radeon_hd {settings}")
    kernel = Path(wsl(f"wslpath -w '{out}/home/kernel'").strip()).read_text(
        encoding="utf-8", errors="replace").split()
    for key, value in (("syslog_time_stamps", "true"), ("syslog_max_size", "4M"),
            ("syslog_buffer_size", "262144")):
        if key not in kernel or kernel[kernel.index(key) + 1] != value:
            die(f"planted kernel settings have no {key} {value}: {kernel}")
    print("home kernel syslog_buffer_size 262144")
    check_m11_image(out)


def check_m11_image(out: str) -> None:
    for relative in M11_IMAGE:
        wsl(f"test -s '{out}/{relative}' || {{ echo 'missing {relative}'; exit 1; }}")
        print(f"m11 program {relative}")
    for relative, needles in M11_STRINGS.items():
        data = Path(wsl(f"wslpath -w '{out}/{relative}'").strip()).read_bytes()
        for needle in needles:
            if needle not in data:
                die(f"{relative} in the image has no {needle.decode()}; an older build")
            print(f"m11 string {relative} {needle.decode()}")
    for relative, needles in M12_STRINGS.items():
        data = Path(wsl(f"wslpath -w '{out}/{relative}'").strip()).read_bytes()
        for needle in needles:
            if needle not in data:
                die(f"{relative} in the image has no {needle.decode()}; an older build")
            print(f"m12 string {relative} {needle.decode()}")
    for relative, needles in M13_STRINGS.items():
        data = Path(wsl(f"wslpath -w '{out}/{relative}'").strip()).read_bytes()
        for needle in needles:
            if needle not in data:
                die(f"{relative} in the image has no {needle.decode()}; an older build")
            print(f"m13 string {relative} {needle.decode()}")


def main() -> int:
    if len(sys.argv) != 2:
        die("usage: check-pre-stick.py IMAGE | --paper")
    commit = head_commit()
    print(f"branch HEAD {commit}")
    check_m11_sources(commit)
    check_m11_boots(commit)
    check_m12_sources(commit)
    check_m12_boots(commit)
    check_m13_sources(commit)
    check_m13_boots(commit)
    check_m13f_boots(commit)
    check_r3_boots(commit)
    if sys.argv[1] == "--paper":
        # M11: the lines and the settings per boot, before an image exists.
        print(f"pre-stick paper ok {commit}")
        return 0
    iso = str(Path(sys.argv[1]).resolve())
    if not Path(iso).is_file():
        die(f"no image at {iso}")
    check_sources(commit)
    check_image(commit, iso)
    print(f"pre-stick ok {commit}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
