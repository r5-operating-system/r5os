#!/usr/bin/env python3
# Boot one copy of an R5 image and screenshot Aomi. Does not stop other QEMU.
# Usage: qemu-aomi.py <image> <out-dir> [url]
"""Screenshot Aomi on a private image copy. Never calls run-qemu-r5.sh."""

import os
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BFS = os.path.join(ROOT, "haiku", "generated.x86_64", "objects", "linux",
                   "x86_64", "release", "tools", "bfs_shell", "bfs_shell")
GUEST = os.path.join(ROOT, "scripts", "aomi-guest.sh")


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


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    source, out = sys.argv[1], sys.argv[2]
    url = sys.argv[3] if len(sys.argv) > 3 else "https://example.com"
    os.makedirs(out, exist_ok=True)
    image = os.path.join(out, "aomi.image")
    serial = "/tmp/aomi-serial.log"
    qmp_path = "/tmp/aomi-qmp.sock"
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
        "-machine", "pc,accel=kvm", "-cpu", "host", "-m", "2048",
        "-smp", "4", "-display", "none",
        "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % image,
        "-vga", "std", "-usb", "-device", "usb-tablet",
        "-object", "rng-random,id=rng0,filename=/dev/urandom",
        "-device", "virtio-rng-pci,rng=rng0",
        "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
        "-serial", "file:" + serial,
        "-qmp", "unix:%s,server,nowait" % qmp_path],
        stdout=subprocess.DEVNULL,
        stderr=open("/tmp/aomi-qemu.err", "w"))
    sock = None
    try:
        for _ in range(200):
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.connect(qmp_path)
                break
            except OSError:
                sock = None
                time.sleep(0.1)
        if sock is None:
            print("FAIL qmp")
            try:
                print(open("/tmp/aomi-qemu.err").read()[-2000:])
            except OSError:
                pass
            return 1
        qfile = sock.makefile("rw")
        qfile.readline()
        qfile.write('{"execute":"qmp_capabilities"}\n')
        qfile.flush()
        qfile.readline()
        seen = 0
        started = False
        next_enter = time.time() + 25
        deadline = time.time() + 900
        while time.time() < deadline:
            time.sleep(0.4)
            if not started and time.time() >= next_enter:
                qfile.write('{"execute":"send-key","arguments":{"keys":'
                            '[{"type":"qcode","data":"ret"}]}}\n')
                qfile.flush()
                qfile.readline()
                next_enter = time.time() + 20
            try:
                with open(serial, errors="replace") as handle:
                    lines = [line.strip() for line in handle if "aomi:" in line]
            except OSError:
                lines = []
            for line in lines[seen:]:
                seen += 1
                print(line)
                if "start" in line:
                    started = True
                if "shot" in line:
                    name = line.split()[-1]
                    path = os.path.join(out, name + ".ppm")
                    qfile.write('{"execute":"screendump","arguments":'
                                '{"filename":"%s"}}\n' % path)
                    qfile.flush()
                    qfile.readline()
                    time.sleep(0.3)
                    to_png(path)
                    if name in ("menu", "dark-menu"):
                        qfile.write('{"execute":"send-key","arguments":{"keys":'
                                    '[{"type":"qcode","data":"esc"}]}}\n')
                        qfile.flush()
                        qfile.readline()
                if line.strip() == "aomi: done":
                    deadline = time.time()
            if qemu.poll() is not None:
                break
        return 0
    finally:
        qemu.terminate()
        try:
            qemu.wait(timeout=15)
        except subprocess.TimeoutExpired:
            qemu.kill()


if __name__ == "__main__":
    sys.exit(main())
