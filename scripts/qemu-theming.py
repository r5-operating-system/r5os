#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Themes in QEMU (docs/design/THEMING.md): screendumps of the R5 Modern
controls, light and dark, the live switch, the Theme page of Settings,
Save as theme, and the dark desktop, on Lavapipe without the owner.

Run in WSL on a private copy of r5-os.image:

  1. copies IMAGE to OUT/theme.image and plants, with bfs_shell,
     scripts/theming-guest.sh as /boot/home/thtest, a UserBootscript that
     starts it after Sign in, and the radeon_hd settings (the Lavapipe
     compositor, gpu_debug true for the compare);
  2. boots QEMU (KVM, usb-tablet, serial to OUT/serial.log, QMP), presses
     Enter at Sign in, and follows the guest's "thtest:" lines: "shot NAME"
     is a screendump, "click X Y" a mouse click, "key NAME" a key;
  3. stops its own QEMU and copies the syslog and the guest's notes to OUT.

--safe boots in Safe mode (the CPU mode, Painter) and runs the short list.
--menu runs only the menu bar check (R5 Modern and R5, a menu open).
--menus runs the R5 menu style check (Deskbar, StyledEdit, Tracker menus,
light and dark); with --safe, in Safe mode.
Usage: scripts/qemu-theming.py IMAGE OUT [--safe] [--menu] [--save-at=X,Y]
"""

import json
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
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUEST = os.path.join(ROOT, "scripts", "theming-guest.sh")


def bfs(image, commands):
    text = "\n".join(commands + ["quit"]) + "\n"
    run = subprocess.run([BFS, image], input=text, capture_output=True,
                         text=True, timeout=120)
    return run.stdout + run.stderr


class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(200):
            try:
                self.sock.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        self.file = self.sock.makefile("rw")
        self.file.readline()
        self.call("qmp_capabilities")
        self.width, self.height = 1024, 768

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

    def hold(self, name, down):
        self.call("input-send-event", events=[
            {"type": "key", "data": {"down": down,
                                     "key": {"type": "qcode", "data": name}}}])

    def move(self, x, y):
        self.call("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x",
                                     "value": int(x * 32767 / self.width)}},
            {"type": "abs", "data": {"axis": "y",
                                     "value": int(y * 32767 / self.height)}}])

    def click(self, x, y):
        self.move(x, y)
        time.sleep(0.4)
        for down in (True, False):
            self.call("input-send-event", events=[
                {"type": "btn", "data": {"button": "left", "down": down}}])
            time.sleep(0.1)

    def dump(self, path):
        self.call("screendump", filename=path)
        time.sleep(0.3)
        with open(path, "rb") as f:
            head = f.read(32).split()
        self.width, self.height = int(head[1]), int(head[2])


def to_png(ppm):
    try:
        from PIL import Image
        Image.open(ppm).save(ppm[:-4] + ".png")
        os.remove(ppm)
    except Exception as error:  # keep the PPM
        print("png:", error)


def main():
    args = sys.argv[1:]
    safe = "--safe" in args
    if safe:
        args.remove("--safe")
    menu = "--menu" in args
    if menu:
        args.remove("--menu")
    menus = "--menus" in args
    if menus:
        args.remove("--menus")
    # --save-at X,Y: where the "Save as theme…" button is on screen (read
    # from a screendump of an earlier run); the guest asks with "click-save"
    save_at = None
    for arg in list(args):
        if arg.startswith("--save-at="):
            save_at = [float(v) for v in arg.split("=", 1)[1].split(",")]
            args.remove(arg)
    if len(args) != 2:
        print(__doc__)
        return 2
    source, out = args
    os.makedirs(out, exist_ok=True)
    image = os.path.join(out, "theme.image")
    serial = os.path.join(out, "serial.log")
    qmp_path = os.path.join(out, "qmp.sock")
    for name in (serial, qmp_path):
        if os.path.exists(name):
            os.remove(name)

    print("copying", source)
    shutil.copyfile(source, image)
    stage = os.path.join(out, "stage")
    os.makedirs(stage, exist_ok=True)
    with open(GUEST, newline="") as f:
        guest = f.read().replace("\r\n", "\n")
    files = {
        "radeon_hd": "gpu_debug true\ngpu_compositor_lavapipe true\n",
        "kernel": "syslog_time_stamps true\nsyslog_max_size 4M\n",
        "UserBootscript": "#!/bin/sh\n/bin/sh /boot/home/thtest %s"
                          " > /boot/home/thtest.txt 2>&1 &\n"
                          % (("safe-menus" if safe else "menus") if menus
                             else "safe" if safe else "menu" if menu else ""),
        "thtest": guest,
    }
    for name, text in files.items():
        with open(os.path.join(stage, name), "w", newline="\n") as f:
            f.write(text)
    drivers = "myfs/home/config/settings/kernel/drivers"
    boot = "myfs/home/config/settings/boot"
    planted = {
        drivers + "/radeon_hd": "radeon_hd",
        drivers + "/kernel": "kernel",
        boot + "/UserBootscript": "UserBootscript",
        "myfs/home/thtest": "thtest",
    }
    commands = ["mkdir myfs/home/config/settings/kernel", "mkdir " + drivers,
                "mkdir " + boot]
    for target, name in planted.items():
        commands += ["rm " + target, "cp :%s/%s %s" % (stage, name, target)]
    text = bfs(image, commands)
    if "Unmounting FS failed" in text:
        print(text)
        print("FAIL bfs_shell did not write the image")
        return 1
    got = bfs(image, ["cat " + boot + "/UserBootscript"])
    if "thtest" not in got:
        print(got)
        print("FAIL the UserBootscript is not in the image")
        return 1
    print("planted")

    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-name", "R5-theming",
        "-machine", "pc,accel=kvm", "-cpu", "host", "-m", "2048",
        "-smp", "2", "-display", "none",
        "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % image,
        "-vga", "std", "-usb", "-device", "usb-tablet",
        "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
        "-serial", "file:" + serial, "-qmp", "unix:%s,server,nowait"
        % qmp_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = 1
    try:
        time.sleep(1)
        qmp = Qmp(qmp_path)
        if safe:
            # the boot menu: a reset, Space while it starts, then Safe mode
            # (the recipe of the shared notes; Space from a cold start did
            # not reach the menu)
            qmp.call("system_reset")
            end = time.time() + 8
            while time.time() < end:
                qmp.key("spc")
                time.sleep(0.15)
            for key in "down,ret,ret,esc,down,down,down,down".split(","):
                qmp.key(key)
                time.sleep(0.4)
            qmp.dump(os.path.join(out, "boot-menu.ppm"))
            to_png(os.path.join(out, "boot-menu.ppm"))
            qmp.key("ret")
        seen = 0
        next_enter = time.time() + 30
        started = False
        deadline = time.time() + 900
        while time.time() < deadline:
            time.sleep(0.3)
            try:
                with open(serial, errors="replace") as f:
                    lines = [l.strip() for l in f if "thtest:" in l]
            except OSError:
                lines = []
            if not started and time.time() >= next_enter:
                qmp.key("ret")
                next_enter = time.time() + 15
            for line in lines[seen:]:
                seen += 1
                words = line[line.index("thtest:") + 7:].split()
                if not words:
                    continue
                print(" ".join(words))
                kind = words[0]
                if kind == "start":
                    started = True
                    deadline = time.time() + 1800
                elif kind == "shot":
                    path = os.path.join(out, words[1] + ".ppm")
                    qmp.dump(path)
                    to_png(path)
                elif kind == "click" and len(words) >= 3:
                    qmp.click(float(words[1]), float(words[2]))
                elif kind == "click-save" and save_at is not None:
                    qmp.click(save_at[0], save_at[1])
                elif kind == "move" and len(words) >= 3:
                    qmp.move(float(words[1]), float(words[2]))
                elif kind == "key" and len(words) >= 2:
                    qmp.key(words[1])
                elif kind == "keys" and len(words) >= 2:
                    # a combination, pressed together: "keys alt esc"
                    qmp.call("send-key", keys=[{"type": "qcode", "data": k}
                        for k in words[1:]])
                elif kind == "done":
                    result = 0
                    deadline = 0
                    break
        if result != 0:
            print("FAIL the guest did not finish")
            qmp.dump(os.path.join(out, "last.ppm"))
            to_png(os.path.join(out, "last.ppm"))
        qmp.call("quit")
    finally:
        try:
            qemu.wait(timeout=30)
        except subprocess.TimeoutExpired:
            qemu.kill()

    text = bfs(image, ["cp myfs/system/var/log/syslog :%s/syslog.txt" % out,
                       "cp myfs/home/thtest.txt :%s/thtest.txt" % out,
                       "cp myfs/home/config/settings/r5/theme :%s/theme.txt"
                       % out])
    try:
        with open(os.path.join(out, "syslog.txt"), errors="replace") as f:
            syslog = f.read()
        ok = syslog.count("compose compare") - syslog.count("MISMATCH")
        print("compose compare lines:", syslog.count("compose compare"),
              "MISMATCH:", syslog.count("MISMATCH"))
        for line in syslog.splitlines():
            if ("crash" in line or "debugger" in line
                    or "theme" in line.lower()):
                print("syslog:", line)
    except OSError:
        print("no syslog")
    return result


if __name__ == "__main__":
    raise SystemExit(main())
