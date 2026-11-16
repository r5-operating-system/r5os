#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""M13 step 0 in QEMU: one run of distro/graphics/boot/m13-measure on Lavapipe.

Run in WSL after scripts/build-locked.sh. It works on its own copy of
r5-os.image (never the shared one), so other sessions' QEMU runs and jams
are not touched:

  1. copies the image to OUT/m13.image and plants, with bfs_shell, the
     radeon_hd settings (gpu_draw, gpu_debug, gpu_compositor_lavapipe true),
     the kernel settings (syslog_time_stamps true), m13-measure, and a
     UserBootscript that starts it after Sign in;
  2. boots QEMU (KVM, usb-tablet, serial to OUT/serial.log, QMP socket),
     presses Enter at Sign in, and follows m13-measure's steps on the
     serial port: it drags the Tracker window, scrolls the Terminal and
     resizes the Tracker window with QMP absolute mouse events;
  3. stops QEMU and copies the results (numbers.txt, scenario.txt,
     syslog.txt, a screenshot at "ready") to OUT.

Usage: scripts/qemu-m13-measure.py [OUT] [--smp N] [--image PATH]
OUT defaults to /home/nickr/r5os/m13-measure. Other sessions jam the same
tree: --image takes a copy made while the build lock was still held, e.g.
  . scripts/build-lock.sh && scripts/build-locked.sh @image &&
      cp <tree>/generated.x86_64/r5-os.image /home/nickr/r5os/m13.image
"""

import json
import math
import os
import shutil
import socket
import subprocess
import sys
import time

sys.stdout.reconfigure(line_buffering=True)

HAIKU = "/home/nickr/r5os/haiku"
IMAGE = HAIKU + "/generated.x86_64/r5-os.image"
BFS = (HAIKU + "/generated.x86_64/objects/linux/x86_64/release/tools/"
       "bfs_shell/bfs_shell")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "distro", "graphics", "boot", "m13-measure")

# m13-measure places the windows here (BRect of the content).
TRACKER = (60, 80, 460, 380)
TERMINAL = (520, 80, 980, 600)


def bfs(image, commands):
    text = "\n".join(commands + ["quit"]) + "\n"
    run = subprocess.run([BFS, image], input=text, capture_output=True,
                         text=True, timeout=120)
    return run.stdout + run.stderr


class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(100):
            try:
                self.sock.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        self.file = self.sock.makefile("rw")
        self.file.readline()
        self.call("qmp_capabilities")

    def call(self, command, **arguments):
        message = {"execute": command}
        if arguments:
            message["arguments"] = arguments
        self.file.write(json.dumps(message) + "\n")
        self.file.flush()
        while True:
            reply = json.loads(self.file.readline())
            if "return" in reply or "error" in reply:
                return reply

    def key(self, name):
        self.call("send-key", keys=[{"type": "qcode", "data": name}])

    def move(self, x, y):
        # absolute axes run 0..32767 over the whole screen
        self.call("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x",
                                     "value": int(x * 32767 / self.width)}},
            {"type": "abs", "data": {"axis": "y",
                                     "value": int(y * 32767 / self.height)}}])

    def button(self, name, down):
        self.call("input-send-event", events=[
            {"type": "btn", "data": {"button": name, "down": down}}])


def wait_serial(path, text, timeout, qmp=None, enter_every=None):
    end = time.time() + timeout
    next_enter = time.time() + (enter_every or 0)
    while time.time() < end:
        try:
            with open(path, errors="replace") as f:
                if text in f.read():
                    return True
        except OSError:
            pass
        if qmp is not None and enter_every and time.time() >= next_enter:
            qmp.key("ret")
            next_enter = time.time() + enter_every
        time.sleep(0.5)
    return False


def drag(qmp, seconds):
    # by the yellow tab, in circles of 120 pixels
    x0, y0 = TRACKER[0] + 40, TRACKER[1] - 10
    qmp.move(x0, y0)
    time.sleep(0.2)
    qmp.button("left", True)
    start = time.time()
    while time.time() - start < seconds:
        a = (time.time() - start) * 2 * math.pi / 3
        qmp.move(x0 + 120 + 120 * math.cos(a + math.pi),
                 y0 + 120 * math.sin(a + math.pi))
        time.sleep(1 / 120)
    qmp.button("left", False)


def scroll(qmp, seconds):
    qmp.move((TERMINAL[0] + TERMINAL[2]) / 2, (TERMINAL[1] + TERMINAL[3]) / 2)
    start = time.time()
    while time.time() - start < seconds:
        name = "wheel-down" if int(time.time() - start) % 4 < 2 else "wheel-up"
        qmp.button(name, True)
        qmp.button(name, False)
        time.sleep(1 / 30)


def resize(qmp, seconds):
    # the resize corner of the document window, 7 pixels inside
    x0, y0 = TRACKER[2] - 7, TRACKER[3] - 7
    qmp.move(x0, y0)
    time.sleep(0.2)
    qmp.button("left", True)
    start = time.time()
    while time.time() - start < seconds:
        f = (1 - math.cos((time.time() - start) * 2 * math.pi / 4)) / 2
        qmp.move(x0 + 150 * f, y0 + 120 * f)
        time.sleep(1 / 120)
    qmp.button("left", False)


def main():
    args = [a for a in sys.argv[1:]]
    smp = "8"
    if "--smp" in args:
        i = args.index("--smp")
        smp = args[i + 1]
        del args[i:i + 2]
    source = IMAGE
    if "--image" in args:
        i = args.index("--image")
        source = args[i + 1]
        del args[i:i + 2]
    out = args[0] if args else "/home/nickr/r5os/m13-measure"
    os.makedirs(out, exist_ok=True)
    image = os.path.join(out, "m13.image")
    serial = os.path.join(out, "serial.log")
    qmp_path = os.path.join(out, "qmp.sock")
    for name in (serial, qmp_path):
        if os.path.exists(name):
            os.remove(name)

    print("copying", source)
    shutil.copyfile(source, image)
    stage = os.path.join(out, "stage")
    os.makedirs(stage, exist_ok=True)
    files = {
        "radeon_hd": "gpu_draw true\ngpu_debug true\n"
                     "gpu_compositor_lavapipe true\n",
        "kernel": "syslog_time_stamps true\nsyslog_max_size 4M\n",
        "UserBootscript": "#!/bin/sh\n/bin/sh /boot/home/m13-measure "
                          "/boot/home/m13/qemu > /boot/home/m13-run.txt "
                          "2>&1 &\n",
    }
    for name, text in files.items():
        with open(os.path.join(stage, name), "w", newline="\n") as f:
            f.write(text)
    with open(SCRIPT, newline="") as f:
        text = f.read().replace("\r\n", "\n")
    with open(os.path.join(stage, "m13-measure"), "w", newline="\n") as f:
        f.write(text)
    drivers = "myfs/home/config/settings/kernel/drivers"
    boot = "myfs/home/config/settings/boot"
    planted = {
        drivers + "/radeon_hd": "radeon_hd",
        drivers + "/kernel": "kernel",
        boot + "/UserBootscript": "UserBootscript",
        "myfs/home/m13-measure": "m13-measure",
    }
    # rm of a missing file only prints an error; the image's own
    # UserBootscript is replaced
    commands = ["mkdir myfs/home/config/settings/kernel", "mkdir " + drivers]
    for target, name in planted.items():
        commands += ["rm " + target, "cp :%s/%s %s" % (stage, name, target)]
    text = bfs(image, commands)
    if "Unmounting FS failed" in text:
        print(text)
        print("FAIL bfs_shell did not write the image")
        return 1
    # read back in a session of its own: a cat in the writing session kept
    # the volume busy, and the unmount (and with it every write) failed
    for target, name in planted.items():
        got = bfs(image, ["cat " + target])
        with open(os.path.join(stage, name), newline="") as f:
            want = f.read().strip().splitlines()[-1]
        if want not in got:
            print("FAIL %s is not in the image" % target)
            return 1
    print("planted", ", ".join(planted.values()))

    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-name", "R5-m13-measure",
        "-machine", "pc,accel=kvm", "-cpu", "host", "-m", "2048",
        "-smp", smp, "-display", "none",
        "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % image,
        "-vga", "std", "-usb", "-device", "usb-tablet",
        "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
        "-serial", "file:" + serial,
        "-qmp", "unix:%s,server,nowait" % qmp_path],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    print("qemu pid", qemu.pid, "smp", smp)
    try:
        time.sleep(2)
        qmp = Qmp(qmp_path)
        # Sign in: Enter every 15 s until m13-measure has started
        if not wait_serial(serial, "m13-measure: start", 300, qmp, 15):
            print("FAIL m13-measure did not start (Sign in?)")
            return 1
        print("m13-measure started")
        if not wait_serial(serial, "m13-measure: ready", 240):
            print("FAIL m13-measure was not ready")
            return 1
        shot = os.path.join(out, "ready.ppm")
        qmp.call("screendump", filename=shot)
        time.sleep(1)
        with open(shot, "rb") as f:
            head = f.read(64).split()
        qmp.width, qmp.height = int(head[1]), int(head[2])
        print("screen", qmp.width, "x", qmp.height)
        for name, action in (("drag", drag), ("scroll", scroll),
                             ("resize", resize)):
            if not wait_serial(serial, "m13-measure: step " + name, 60):
                print("FAIL no step", name)
                return 1
            print("step", name)
            action(qmp, 28)
        if not wait_serial(serial, "m13-measure: done", 180):
            print("FAIL m13-measure did not finish")
            return 1
        time.sleep(5)
    finally:
        qemu.kill()
        qemu.wait()
    print(bfs(image, [
        "cp myfs/home/m13/qemu/%s :%s/%s" % (name, out, name)
        for name in ("numbers.txt", "scenario.txt", "syslog.txt")
    ] + ["cp myfs/home/m13-run.txt :%s/m13-run.txt" % out])[-400:])
    with open(os.path.join(out, "numbers.txt"), errors="replace") as f:
        print(f.read())
    return 0


if __name__ == "__main__":
    sys.exit(main())
