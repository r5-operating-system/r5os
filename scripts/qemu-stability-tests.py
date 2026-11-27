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
    with open(os.path.join(ROOT, "scripts/stability-guest.sh"),
              newline="") as handle:
        guest = handle.read().replace("\r\n", "\n")
    with open(os.path.join(stage, "stabtest"), "w", newline="\n") as handle:
        handle.write(guest)
    with open(os.path.join(stage, "UserBootscript"), "w",
              newline="\n") as handle:
        handle.write("#!/bin/sh\n/bin/sh /boot/home/stabtest %s "
                     "> /boot/home/stabtest.txt 2>&1 &\n" % seconds)
    with open(os.path.join(stage, "radeon_hd"), "w", newline="\n") as handle:
        handle.write("gpu_draw true\ngpu_compositor_lavapipe true\n")
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
    for name in ("tls_dtor_test", "libtlsdtor.so", "window_stress"):
        commands += ["rm myfs/home/stabtests/%s" % name,
                     "cp :%s/%s myfs/home/stabtests/%s" % (tests, name, name),
                     "chmod 755 myfs/home/stabtests/%s" % name]
    text = bfs(image, commands)
    if "Unmounting FS failed" in text or "window_stress" not in bfs(
            image, ["ls myfs/home/stabtests"]):
        print(text)
        print("FAIL bfs_shell did not plant the tests")
        return 1
    print("planted, stress", seconds, "s")

    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-name", "R5-stab",
        "-machine", "pc,accel=kvm", "-cpu", "host", "-m", "4096",
        "-smp", "4", "-display", "none",
        "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % image,
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
