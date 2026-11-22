#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Default (R5 Modern) in QEMU on Lavapipe, without the owner (22 Nov 2026).

  scripts/qemu-r5modern.py OUT IMAGE [--safe]

Run in WSL on a copy of r5-os.image made right after scripts/build-locked.sh.
The guest side is scripts/r5modern-test.sh (r5-effects-test decorator and
soft): the compare with gpu_debug for Default (R5), Classic (Haiku) and
Default (R5 Modern) with the effects off, then board 4's two windows with
shadow and glass, a move by the tab, a resize by the round corner,
Appearance, and the switch back to Default (R5). The screendumps are PPM
files in OUT; the shell side's files are in /boot/home/r5modern.

Copies IMAGE to OUT/test.image (not with --safe: that boots the copy
again, into Safe mode), plants gpu_debug, the Lavapipe compositor, the
syslog time stamps and r5modern-test (started by UserBootscript), boots,
presses Enter at Sign in, and takes the screendumps at the test's steps.
QEMU keeps running at the end (name R5-r5modern); stop it with QMP quit.
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
BFS = (HAIKU + "/generated.x86_64/objects/linux/x86_64/release/tools/"
       "bfs_shell/bfs_shell")
HERE = os.path.dirname(os.path.abspath(__file__))


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
        self.width, self.height = 1280, 800

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
        self.call("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x",
                                     "value": int(x * 32767 / self.width)}},
            {"type": "abs", "data": {"axis": "y",
                                     "value": int(y * 32767 / self.height)}}])

    def button(self, down):
        self.call("input-send-event", events=[
            {"type": "btn", "data": {"button": "left", "down": down}}])

    def shot(self, path):
        self.call("screendump", filename=path)
        time.sleep(0.5)
        print("shot", path)


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


def drag(qmp, x0, y0, dx, dy, steps=60, shot=None):
    qmp.move(x0, y0)
    time.sleep(0.4)
    qmp.button(True)
    for i in range(1, steps + 1):
        qmp.move(x0 + dx * i / steps, y0 + dy * i / steps)
        time.sleep(1 / 60)
    time.sleep(0.5)
    if shot:
        qmp.shot(shot)
    qmp.button(False)
    time.sleep(2)


def main():
    args = sys.argv[1:]
    safe = "--safe" in args
    if safe:
        args.remove("--safe")
    out, source = args[0], args[1]
    os.makedirs(out, exist_ok=True)
    image = os.path.join(out, "test.image")
    serial = os.path.join(out, "serial-safe.log" if safe else "serial.log")
    qmp_path = os.path.join(out, "qmp.sock")
    for name in (serial, qmp_path):
        if os.path.exists(name):
            os.remove(name)

    if not safe:
        shutil.copyfile(source, image)
        stage = os.path.join(out, "stage")
        os.makedirs(stage, exist_ok=True)
        files = {
            "radeon_hd": "gpu_debug true\ngpu_compositor_lavapipe true\n",
            "kernel": "syslog_time_stamps true\nsyslog_max_size 4M\n",
            "UserBootscript": "#!/bin/sh\n/bin/sh /boot/home/r5modern-test "
                              "> /boot/home/r5modern-run.txt 2>&1 &\n",
        }
        for name, text in files.items():
            with open(os.path.join(stage, name), "w", newline="\n") as f:
                f.write(text)
        with open(os.path.join(HERE, "r5modern-test.sh"), newline="") as f:
            text = f.read().replace("\r\n", "\n")
        with open(os.path.join(stage, "r5modern-test"), "w",
                  newline="\n") as f:
            f.write(text)
        drivers = "myfs/home/config/settings/kernel/drivers"
        boot = "myfs/home/config/settings/boot"
        planted = {
            drivers + "/radeon_hd": "radeon_hd",
            drivers + "/kernel": "kernel",
            boot + "/UserBootscript": "UserBootscript",
            "myfs/home/r5modern-test": "r5modern-test",
        }
        commands = ["mkdir myfs/home/config/settings/kernel",
                    "mkdir " + drivers, "mkdir " + boot]
        for target, name in planted.items():
            commands += ["rm " + target, "cp :%s/%s %s" % (stage, name, target)]
        text = bfs(image, commands)
        if "Unmounting FS failed" in text:
            print(text)
            print("FAIL bfs_shell did not write the image")
            return 1
        for target, name in planted.items():
            got = bfs(image, ["cat " + target])
            with open(os.path.join(stage, name), newline="") as f:
                want = f.read().strip().splitlines()[-1]
            if want not in got:
                print("FAIL %s is not in the image" % target)
                return 1
        print("planted", ", ".join(planted.values()))

    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-name", "R5-r5modern",
        "-machine", "pc,accel=kvm", "-cpu", "host", "-m", "2048",
        "-smp", "2", "-display", "none",
        "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % image,
        "-vga", "std", "-usb", "-device", "usb-tablet",
        "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
        "-serial", "file:" + serial,
        "-qmp", "unix:%s,server,nowait" % qmp_path],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, start_new_session=True)
    print("qemu pid", qemu.pid)
    time.sleep(1)
    qmp = Qmp(qmp_path)

    if safe:
        # the boot menu: a reset, then space until it shows (spaces sent
        # right at the start come too early), then Safe mode
        time.sleep(3)
        qmp.call("system_reset")
        end = time.time() + 8
        while time.time() < end:
            qmp.key("spc")
            time.sleep(0.15)
        time.sleep(1)
        for k in "down,ret,ret,esc,down,down,down,down".split(","):
            qmp.key(k)
            time.sleep(0.4)
        time.sleep(0.5)
        qmp.shot(os.path.join(out, "safe-menu.ppm"))
        qmp.key("ret")
        if not wait_serial(serial, "r5modern: safe-ready", 400, qmp, 15):
            print("FAIL no safe-ready")
            return 1
        time.sleep(3)
        qmp.shot(os.path.join(out, "safe.ppm"))
        print("safe done; QEMU keeps running")
        return 0

    if not wait_serial(serial, "r5modern: start", 400, qmp, 15):
        print("FAIL the test did not start (Sign in?)")
        return 1
    print("started")
    first = os.path.join(out, "start.ppm")
    qmp.shot(first)
    with open(first, "rb") as f:
        head = f.read(64).split()
    qmp.width, qmp.height = int(head[1]), int(head[2])
    print("screen", qmp.width, "x", qmp.height)
    for n, name in ((1, "r5"), (2, "classic"), (3, "modern")):
        if not wait_serial(serial, "r5modern: compare %d" % n, 200):
            print("FAIL no compare", n)
            return 1
        time.sleep(16)
        qmp.shot(os.path.join(out, "%s-noeffects.ppm" % name))
    if not wait_serial(serial, "r5modern: ready", 200):
        print("FAIL not ready")
        return 1
    time.sleep(4)
    qmp.shot(os.path.join(out, "modern-soft.ppm"))
    # move StyledEdit by its tab, then resize it by its round bottom right
    # corner, the cut wedge outside the arc (the outer edge is 60..619 by
    # 58..419 at the start)
    drag(qmp, 250, 74, 90, 60, shot=os.path.join(out, "moving.ppm"))
    qmp.shot(os.path.join(out, "moved.ppm"))
    drag(qmp, 619 + 90 - 1, 419 + 60 - 1, 80, 60,
         shot=os.path.join(out, "resizing.ppm"))
    time.sleep(2)
    qmp.shot(os.path.join(out, "resized.ppm"))
    if wait_serial(serial, "r5modern: appearance", 240):
        time.sleep(4)
        qmp.shot(os.path.join(out, "appearance.ppm"))
    if wait_serial(serial, "r5modern: switched", 120):
        time.sleep(3)
        qmp.shot(os.path.join(out, "switched-r5.ppm"))
    print("done; QEMU keeps running")
    return 0


if __name__ == "__main__":
    sys.exit(main())
