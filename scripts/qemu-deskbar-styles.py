#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Deskbar styles in QEMU (docs/design/DESKBAR-STYLES.md): screendumps of
every layout, the hover, the hollow and the menu, the zoom numbers, the
compare, and a third-party tray item, on Lavapipe without the owner.

Run in WSL. It works on its own copy of the image (never the shared one):

  1. copies IMAGE to OUT/ds.image and plants, with bfs_shell,
     scripts/deskbar-styles-guest.sh as /boot/home/dstest, a UserBootscript
     that starts it after Sign in, the radeon_hd settings (gpu_debug true,
     for the compare) and the kernel settings (time stamps);
  2. boots QEMU (KVM, usb-tablet, serial to OUT/serial.log, QMP), presses
     Enter at Sign in, and follows the guest's "dstest:" lines: "shot" is a
     screendump, "act" moves or clicks the mouse first (hover, menu,
     chevron), "hold-shift" and "release-shift" hold Shift for the zoom;
  3. stops its own QEMU and copies the syslog to OUT.

--safe boots in Safe mode (the CPU mode) and runs the short guest list.
Usage: scripts/qemu-deskbar-styles.py IMAGE OUT [--safe]
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
GUEST = os.path.join(ROOT, "scripts", "deskbar-styles-guest.sh")


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


def parse_frame(words):
    # "1052.0, 14.0, 1265.0, 260.0" from hey
    text = " ".join(words).replace(",", " ")
    numbers = []
    for word in text.split():
        try:
            numbers.append(float(word))
        except ValueError:
            pass
    return numbers[:4] if len(numbers) >= 4 else None


def act(qmp, name, margin, frame, out):
    # BDeskbar's frame includes the margin of a floating Deskbar
    left, top, right, bottom = (frame[0] + margin, frame[1] + margin,
                                frame[2] - margin, frame[3] - margin)
    middle = (top + bottom) / 2
    path = os.path.join(out, "act-" + name + ".ppm")
    if name == "hover-pebble-v":
        qmp.move((left + right) / 2, top + 1 + 21)
        time.sleep(1.2)
    elif name == "menu-v":
        qmp.click((left + right) / 2, top + 1 + 21)
        time.sleep(1.5)
    elif name == "hover-app-v":
        # the second row of the list: leaf 42, tray 36, padding 4, row 34
        qmp.move((left + right) / 2, top + 1 + 42 + 36 + 4 + 34 + 17)
        time.sleep(1.2)
    elif name in ("menu-c", "hover-pebble-c"):
        x, y = left + 1 + 12 + 21, middle
        if name == "menu-c":
            qmp.click(x, y)
            time.sleep(1.5)
        else:
            qmp.move(x, y)
            time.sleep(1.2)
    elif name == "chevron-c":
        qmp.click(right - 1 - 10 - 15, middle)
        time.sleep(2)
        frame = None
    elif name == "menu-h":
        qmp.click(left + 1 + 8 + 24, middle)
        time.sleep(1.5)
    qmp.dump(path)
    to_png(path)
    if name.startswith("menu"):
        # twice: a menu that opens late (Lavapipe under load) still closes,
        # else Deskbar keeps tracking it and the guest's hey waits
        qmp.key("esc")
        time.sleep(2.5)
        qmp.key("esc")
        time.sleep(0.5)
    qmp.move(qmp.width / 2, qmp.height * 0.7)


def main():
    args = sys.argv[1:]
    safe = "--safe" in args
    if safe:
        args.remove("--safe")
    if len(args) != 2:
        print(__doc__)
        return 2
    source, out = args
    os.makedirs(out, exist_ok=True)
    image = os.path.join(out, "ds.image")
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
        "radeon_hd": "gpu_debug true\n",
        "kernel": "syslog_time_stamps true\nsyslog_max_size 4M\n",
        "UserBootscript": "#!/bin/sh\n/bin/sh /boot/home/dstest %s"
                          " > /boot/home/dstest.txt 2>&1 &\n"
                          % ("safe" if safe else ""),
        "dstest": guest,
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
        "myfs/home/dstest": "dstest",
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
    if "dstest" not in got:
        print(got)
        print("FAIL the UserBootscript is not in the image")
        return 1
    print("planted")

    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-name", "R5-deskbar-styles",
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
            # the boot menu: Space while it starts, then Safe mode
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
        deadline = time.time() + 420
        while time.time() < deadline:
            time.sleep(0.3)
            try:
                with open(serial, errors="replace") as f:
                    lines = [l.strip() for l in f if "dstest:" in l]
            except OSError:
                lines = []
            if not started and time.time() >= next_enter:
                qmp.key("ret")
                next_enter = time.time() + 15
            for line in lines[seen:]:
                seen += 1
                words = line[line.index("dstest:") + 7:].split()
                if not words:
                    continue
                print(" ".join(words))
                kind = words[0]
                if kind == "start":
                    started = True
                    deadline = time.time() + 1500
                elif kind == "shot":
                    path = os.path.join(out, words[1] + ".ppm")
                    qmp.dump(path)
                    to_png(path)
                elif kind == "act":
                    frame = parse_frame(words[3:])
                    if frame is None:
                        print("no frame for", words[1])
                        continue
                    act(qmp, words[1], float(words[2]), frame, out)
                elif kind == "hold-shift":
                    qmp.hold("shift", True)
                elif kind == "release-shift":
                    qmp.hold("shift", False)
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
                       "cp myfs/home/dstest.txt :%s/dstest.txt" % out])
    try:
        with open(os.path.join(out, "syslog.txt"), errors="replace") as f:
            syslog = f.read()
        ok = syslog.count("compose compare") - syslog.count("MISMATCH")
        print("compose compare lines:", syslog.count("compose compare"),
              "MISMATCH:", syslog.count("MISMATCH"))
        for line in syslog.splitlines():
            if "Deskbar" in line and ("crash" in line or "debug" in line):
                print("syslog:", line)
    except OSError:
        print("no syslog")
    return result


if __name__ == "__main__":
    raise SystemExit(main())
