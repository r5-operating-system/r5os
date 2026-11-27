#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
# Boot one copy of an R5 image and screenshot Aomi. Does not stop other QEMU.
# Usage: qemu-aomi.py <image> <out-dir> [url]
"""Screenshot Aomi on a private image copy. Never calls run-qemu-r5.sh.

Environment:
  AOMI_GUEST     guest script to plant (default scripts/aomi-guest.sh)
  AOMI_LINK_OFF  1: the network cable is out from the start
  AOMI_HOLD      1: keep QEMU running after "aomi: done" (stop it with
                 scripts/qmp-aomi.py <out-dir> quit)

The guest asks the host for what it cannot do itself, one line on the
serial port each:
  aomi: shot NAME         screendump to <out-dir>/NAME.png
  aomi: link on|off       plug the network cable in or pull it out
  aomi: click X Y         left click at screen pixel X, Y (1280 x 800)
  aomi: keys TEXT         type TEXT (letters, digits, . / : - space)
  aomi: key COMBO         one key, QEMU qcode names joined by +, e.g. alt+l
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BFS = os.path.join(ROOT, "haiku", "generated.x86_64", "objects", "linux",
                   "x86_64", "release", "tools", "bfs_shell", "bfs_shell")
GUEST = os.environ.get("AOMI_GUEST",
                       os.path.join(ROOT, "scripts", "aomi-guest.sh"))
SCREEN_W = 1280
SCREEN_H = 800

KEY_NAMES = {
    " ": "spc", ".": "dot", "/": "slash", ":": "shift+semicolon",
    "-": "minus", "_": "shift+minus", "?": "shift+slash", "=": "equal",
    "<": "shift+comma", ">": "shift+dot", "(": "shift+9", ")": "shift+0",
    ",": "comma", ";": "semicolon", "'": "apostrophe", "+": "shift+equal",
    "\"": "shift+apostrophe", "%": "shift+5", "#": "shift+3",
}


def bfs(image, commands):
    text = "\n".join(commands + ["quit"]) + "\n"
    run = subprocess.run([BFS, image], input=text, capture_output=True,
                         text=True, timeout=180)
    return run.stdout + run.stderr


def to_png(ppm):
    try:
        from PIL import Image
        Image.open(ppm).save(ppm[:-4] + ".png")
        os.remove(ppm)
    except Exception as error:
        print("png:", error)


class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.file = self.sock.makefile("rw")
        self.file.readline()
        self.call("qmp_capabilities")

    def call(self, command, arguments=None):
        message = {"execute": command}
        if arguments is not None:
            message["arguments"] = arguments
        self.file.write(json.dumps(message) + "\n")
        self.file.flush()
        while True:
            line = self.file.readline()
            if not line:
                return None
            reply = json.loads(line)
            # Asynchronous events come in between; the reply has "return"
            # or "error".
            if "return" in reply or "error" in reply:
                return reply

    def keys(self, combo):
        names = combo.split("+")
        self.call("send-key", {"keys": [{"type": "qcode", "data": name}
                                        for name in names]})

    def type_text(self, text):
        for char in text:
            if char in KEY_NAMES:
                self.keys(KEY_NAMES[char])
            elif char.isupper():
                self.keys("shift+" + char.lower())
            else:
                self.keys(char)
            time.sleep(0.08)

    def click(self, x, y):
        ax = int(x * 32767 / (SCREEN_W - 1))
        ay = int(y * 32767 / (SCREEN_H - 1))
        self.call("input-send-event", {"events": [
            {"type": "abs", "data": {"axis": "x", "value": ax}},
            {"type": "abs", "data": {"axis": "y", "value": ay}}]})
        time.sleep(0.15)
        for down in (True, False):
            self.call("input-send-event", {"events": [
                {"type": "btn", "data": {"down": down, "button": "left"}}]})
            time.sleep(0.1)

    def link(self, on):
        self.call("set_link", {"name": "net0", "up": on})

    def shot(self, path):
        self.call("screendump", {"filename": path})
        time.sleep(0.3)
        to_png(path)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    source, out = sys.argv[1], os.path.abspath(sys.argv[2])
    url = sys.argv[3] if len(sys.argv) > 3 else "https://example.com"
    os.makedirs(out, exist_ok=True)
    image = os.path.join(out, "aomi.image")
    serial = os.path.join(out, "serial.log")
    qmp_path = os.path.join(out, "qmp.sock")
    for name in (serial, qmp_path):
        if os.path.exists(name):
            os.remove(name)
    print("copying", source)
    shutil.copyfile(source, image)
    stage = os.path.join(out, "stage")
    os.makedirs(stage, exist_ok=True)
    with open(GUEST, newline="") as handle:
        guest = handle.read().replace("\r\n", "\n")
    # the URL is the first argument of the planted script
    guest = guest.replace('"$1"', '"' + url.replace('"', "") + '"', 1)
    with open(os.path.join(stage, "aomitest"), "w", newline="\n") as handle:
        handle.write(guest)
    with open(os.path.join(stage, "UserBootscript"), "w", newline="\n") as handle:
        handle.write("#!/bin/sh\n/bin/sh /boot/home/aomitest "
                     "> /boot/home/aomitest.txt 2>&1 &\n")
    boot = "myfs/home/config/settings/boot"
    cmds = [
        "mkdir myfs/home/config/settings",
        "mkdir " + boot,
        "rm " + boot + "/UserBootscript",
        "rm myfs/home/aomitest",
        "cp :%s/UserBootscript %s/UserBootscript" % (stage, boot),
        "cp :%s/aomitest myfs/home/aomitest" % stage,
    ]
    helper = os.environ.get("AOMI_TEST_HELPER", "/tmp/aomi-test-helper")
    if os.path.isfile(helper):
        cmds += [
            "rm myfs/home/aomi-test-helper",
            "cp :%s myfs/home/aomi-test-helper" % helper,
            "chmod 755 myfs/home/aomi-test-helper",
        ]
    text = bfs(image, cmds)
    if "Unmounting FS failed" in text:
        print(text)
        return 1
    print("planted")
    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-name", "R5-aomi",
        "-machine", "pc,accel=kvm", "-cpu", "host", "-m", "3072",
        "-smp", "4", "-display", "none",
        "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % image,
        "-vga", "std", "-usb", "-device", "usb-tablet",
        "-object", "rng-random,id=rng0,filename=/dev/urandom",
        "-device", "virtio-rng-pci,rng=rng0",
        "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
        "-serial", "file:" + serial,
        "-qmp", "unix:%s,server,nowait" % qmp_path],
        stdout=subprocess.DEVNULL,
        stderr=open(os.path.join(out, "qemu.err"), "w"))
    qmp = None
    hold = os.environ.get("AOMI_HOLD") == "1"
    try:
        for _ in range(200):
            try:
                qmp = Qmp(qmp_path)
                break
            except OSError:
                qmp = None
                time.sleep(0.1)
        if qmp is None:
            print("FAIL qmp")
            return 1
        if os.environ.get("AOMI_LINK_OFF") == "1":
            qmp.link(False)
            print("link off")
        seen = 0
        started = False
        next_enter = time.time() + 25
        deadline = time.time() + 1800
        while time.time() < deadline:
            time.sleep(0.4)
            if not started and time.time() >= next_enter:
                qmp.keys("ret")
                next_enter = time.time() + 20
            try:
                with open(serial, errors="replace") as handle:
                    lines = [line.strip() for line in handle if "aomi:" in line]
            except OSError:
                lines = []
            for line in lines[seen:]:
                seen += 1
                print(line, flush=True)
                words = line[line.index("aomi:") + 5:].split()
                if not words:
                    continue
                verb = words[0]
                if verb == "start":
                    started = True
                elif verb == "shot" and len(words) > 1:
                    qmp.shot(os.path.join(out, words[1] + ".ppm"))
                    if words[1] in ("menu", "dark-menu"):
                        qmp.keys("esc")
                elif verb == "link" and len(words) > 1:
                    qmp.link(words[1] == "on")
                elif verb == "click" and len(words) > 2:
                    qmp.click(float(words[1]), float(words[2]))
                elif verb == "keys" and len(words) > 1:
                    qmp.type_text(line.split(" keys ", 1)[1])
                elif verb == "key" and len(words) > 1:
                    qmp.keys(words[1])
                elif verb == "done":
                    deadline = time.time()
            if qemu.poll() is not None:
                break
        if hold:
            # One QMP client at a time: let scripts/qmp-aomi.py connect.
            qmp.file.close()
            qmp.sock.close()
            print("holding; qmp at", qmp_path, flush=True)
            qemu.wait()
        return 0
    finally:
        if qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=15)
            except subprocess.TimeoutExpired:
                qemu.kill()


if __name__ == "__main__":
    sys.exit(main())
