#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""One QEMU boot for the stability fixes (GitHub #3012, #3933, #3409).

Works on a private copy of the image and never stops another QEMU. Plants
the programs from scripts/build-stability-tests.sh, the radeon_hd settings
for Skia on Lavapipe under the compositor (gpu_draw, gpu_compositor_lavapipe)
and a UserBootscript that runs scripts/stability-guest.sh after Sign in.
Prints the guest's "stab:" lines and saves the screenshots it asks for.

Usage: qemu-stability-tests.py IMAGE OUT TESTS [stress-seconds]

Optional, for another round of tests with the same harness:
  STAB_GUEST  the guest script instead of scripts/stability-guest.sh
  STAB_FILES  the programs to plant from TESTS, space-separated
  STAB_ROOT   a script run as root by a launch job once the volumes are
              mounted (for calls that need root)
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import time

sys.stdout.reconfigure(line_buffering=True)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BFS = ("/home/nickr/r5os/haiku/generated.x86_64/objects/linux/x86_64/"
       "release/tools/bfs_shell/bfs_shell")
W, H = 1280, 800


def bfs(image, commands):
    text = "\n".join(commands + ["quit"]) + "\n"
    run = subprocess.run([BFS, image], input=text, capture_output=True,
                         text=True, timeout=180)
    return run.stdout + run.stderr


class Qmp:
    def __init__(self, path):
        for _ in range(200):
            try:
                self.sock = socket.socket(socket.AF_UNIX)
                self.sock.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise SystemExit("FAIL qmp")
        self.file = self.sock.makefile("rw")
        self.file.readline()
        self.call("qmp_capabilities")

    def call(self, command, **arguments):
        self.file.write(json.dumps({"execute": command,
                                    "arguments": arguments}) + "\n")
        self.file.flush()
        while True:
            reply = json.loads(self.file.readline())
            if "return" in reply or "error" in reply:
                return reply

    def click(self, x, y):
        self.call("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / W)}},
            {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / H)}}])
        time.sleep(0.3)
        for down in (True, False):
            self.call("input-send-event", events=[
                {"type": "btn", "data": {"down": down, "button": "left"}}])
            time.sleep(0.12)

    def shot(self, path):
        self.call("screendump", filename=path)
        time.sleep(0.5)
        try:
            from PIL import Image
            Image.open(path).save(path[:-4] + ".png")
            os.remove(path)
        except Exception as error:
            print("png:", error)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    source, out, tests = sys.argv[1:4]
    seconds = sys.argv[4] if len(sys.argv) > 4 else "600"
    os.makedirs(out, exist_ok=True)
    image = os.path.join(out, "stab.image")
    serial = os.path.join(out, "serial.log")
    qmp_path = os.path.join(out, "qmp.sock")
    for name in (serial, qmp_path):
        if os.path.exists(name):
            os.remove(name)
    shutil.copyfile(source, image)

    stage = os.path.join(out, "stage")
    os.makedirs(stage, exist_ok=True)
    guest_path = os.environ.get("STAB_GUEST", "scripts/stability-guest.sh")
    files = os.environ.get(
        "STAB_FILES", "tls_dtor_test libtlsdtor.so window_stress").split()
    with open(os.path.join(ROOT, guest_path), newline="") as handle:
        guest = handle.read().replace("\r\n", "\n")
    with open(os.path.join(stage, "stabtest"), "w", newline="\n") as handle:
        handle.write(guest)
    with open(os.path.join(stage, "UserBootscript"), "w",
              newline="\n") as handle:
        handle.write("#!/bin/sh\n/bin/sh /boot/home/stabtest %s "
                     "> /boot/home/stabtest.txt 2>&1 &\n" % seconds)
    with open(os.path.join(stage, "radeon_hd"), "w", newline="\n") as handle:
        # STAB_RADEON replaces the settings, e.g. "gpu_draw false" (Painter)
        handle.write(os.environ.get(
            "STAB_RADEON", "gpu_draw true;gpu_compositor_lavapipe true")
            .replace(";", "\n") + "\n")
    settings = "myfs/home/config/settings"
    # bfs_shell's cp does not replace a file, so each target is removed first
    commands = [
        "mkdir " + settings, "mkdir %s/boot" % settings,
        "mkdir %s/kernel" % settings, "mkdir %s/kernel/drivers" % settings,
        "rm %s/kernel/drivers/radeon_hd" % settings,
        "cp :%s/radeon_hd %s/kernel/drivers/radeon_hd" % (stage, settings),
        "rm %s/boot/UserBootscript" % settings,
        "cp :%s/UserBootscript %s/boot/UserBootscript" % (stage, settings),
        "rm myfs/home/stabtest",
        "cp :%s/stabtest myfs/home/stabtest" % stage,
        "mkdir myfs/home/stabtests"]
    if os.environ.get("STAB_DISK2"):
        # mounting needs root: a system launch job mounts the second disk
        with open(os.path.join(stage, "disk2"), "w", newline="\n") as handle:
            handle.write(
                "#!/bin/sh\n"
                "say() { echo \"stab: $*\" > /dev/dprintf 2> /dev/null; }\n"
                "mkdir -p /tmp/disk2\n"
                "out=$(mount -t bfs /dev/disk/ata/0/slave/raw /tmp/disk2 2>&1)\n"
                "say \"disk2 mount exit=$? $out\"\n"
                "say \"disk2 ls: $(ls /tmp/disk2 2>&1 | head -3 | tr '\\n' ' ')\"\n"
                "unmount /tmp/disk2 > /dev/null 2>&1\n")
        with open(os.path.join(stage, "disk2-job"), "w", newline="\n") as handle:
            handle.write("job x-vnd.R5OS-stab-disk2 {\n"
                         "\tlaunch /bin/sh /boot/home/stabdisk2\n"
                         "\ton initial_volumes_mounted\n"
                         "}\n")
        commands += [
            "mkdir myfs/system/settings/launch",
            "rm myfs/system/settings/launch/stab-disk2",
            "cp :%s/disk2-job myfs/system/settings/launch/stab-disk2" % stage,
            "rm myfs/home/stabdisk2",
            "cp :%s/disk2 myfs/home/stabdisk2" % stage]
    if os.environ.get("STAB_ROOT"):
        with open(os.path.join(ROOT, os.environ["STAB_ROOT"]),
                  newline="") as handle:
            root_script = handle.read().replace("\r\n", "\n")
        with open(os.path.join(stage, "stabroot"), "w", newline="\n") as handle:
            handle.write(root_script)
        with open(os.path.join(stage, "root-job"), "w", newline="\n") as handle:
            handle.write("job x-vnd.R5OS-stab-root {\n"
                         "\tlaunch /bin/sh /boot/home/stabroot\n"
                         "\ton initial_volumes_mounted\n"
                         "}\n")
        commands += [
            "mkdir myfs/system/settings/launch",
            "rm myfs/system/settings/launch/stab-root",
            "cp :%s/root-job myfs/system/settings/launch/stab-root" % stage,
            "rm myfs/home/stabroot",
            "cp :%s/stabroot myfs/home/stabroot" % stage]
    for name in files:
        commands += ["rm myfs/home/stabtests/%s" % name,
                     "cp :%s/%s myfs/home/stabtests/%s" % (tests, name, name),
                     "chmod 755 myfs/home/stabtests/%s" % name]
    text = bfs(image, commands)
    if "Unmounting FS failed" in text or files[-1] not in bfs(
            image, ["ls myfs/home/stabtests"]):
        print(text)
        print("FAIL bfs_shell did not plant the tests")
        return 1
    print("planted, stress", seconds, "s")

    # STAB_DISK2: a second IDE disk (a copy), e.g. a corrupt BFS image that
    # stability-guest.sh mounts (GitHub #1719)
    drives = ["-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % image]
    if os.environ.get("STAB_DISK2"):
        disk2 = os.path.join(out, "disk2.img")
        shutil.copyfile(os.environ["STAB_DISK2"], disk2)
        drives += ["-drive",
                   "file=%s,format=raw,if=ide,index=1,media=disk" % disk2]
    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-name", "R5-stab",
        "-machine", "pc,accel=kvm", "-cpu", "host", "-m", "4096",
        "-smp", "4", "-display", "none"] + drives + [
        "-vga", "std", "-usb", "-device", "usb-tablet",
        "-object", "rng-random,id=rng0,filename=/dev/urandom",
        "-device", "virtio-rng-pci,rng=rng0",
        "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
        "-serial", "file:" + serial, "-qmp", "unix:%s,server,nowait" % qmp_path],
        stdout=subprocess.DEVNULL,
        stderr=open(os.path.join(out, "qemu.err"), "w"))
    try:
        qmp = Qmp(qmp_path)
        boot = time.time()
        started = False
        clicks = [(120, 912, 584), (135, 640, 474), (180, 912, 584),
                  (195, 640, 474)]
        seen = 0
        deadline = time.time() + int(seconds) + 900
        while time.time() < deadline and qemu.poll() is None:
            time.sleep(0.5)
            if not started:
                if clicks and time.time() - boot >= clicks[0][0]:
                    _, x, y = clicks.pop(0)
                    qmp.click(x, y)
                    print("click", x, y)
                if time.time() - boot > 400:
                    qmp.shot(os.path.join(out, "no-session.ppm"))
                    print("FAIL no session after 400 s")
                    return 1
            try:
                with open(serial, errors="replace") as handle:
                    lines = [line.strip() for line in handle
                             if "stab:" in line or "PANIC" in line
                             or "KERN: debugger" in line]
            except OSError:
                lines = []
            for line in lines[seen:]:
                seen += 1
                print(line)
                if "stab: start" in line:
                    started = True
                if "stab: shot" in line:
                    qmp.shot(os.path.join(out, line.split()[-1] + ".ppm"))
                if line.endswith("stab: done"):
                    deadline = time.time()
        print("boot to end %.0f s" % (time.time() - boot))
        return 0
    finally:
        qemu.terminate()
        try:
            qemu.wait(timeout=15)
        except subprocess.TimeoutExpired:
            qemu.kill()


if __name__ == "__main__":
    sys.exit(main())
