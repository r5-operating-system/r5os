#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Mount an exFAT card in QEMU and run the write steps.

Works on its own copy of the image (never the shared one) and does not
stop any other QEMU. A second IDE disk is an exFAT image. bfs_shell plants
a system launch job that runs as root when the boot volume is mounted.
The guest prints one "exfat:" line per step on the serial port. On
"exfat: pull" QEMU is stopped without unmounting, and the host checks
that each closed file's valid length is backed by bytes on the card.
fsck.exfat -n is then run and its output is printed.

HAIKU_TREE is the Haiku tree (the gcc and bfs_shell live under it).
MKFS_EXFAT and FSCK_EXFAT name the host tools; otherwise they are taken
from PATH.

Usage: scripts/exfat-qemu-test.py IMAGE OUT
"""

import os
import shutil
import socket
import subprocess
import sys
import time

sys.stdout.reconfigure(line_buffering=True)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def bfs(image, commands, bfs_path):
    text = "\n".join(commands + ["quit"]) + "\n"
    run = subprocess.run([bfs_path, image], input=text, capture_output=True,
                         text=True, timeout=120)
    return run.stdout + run.stderr


def qmp_key(path):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    for _ in range(200):
        try:
            sock.connect(path)
            break
        except OSError:
            time.sleep(0.1)
    file = sock.makefile("rw")
    file.readline()
    def call(command, **arguments):
        message = {"execute": command}
        if arguments:
            message["arguments"] = arguments
        file.write(__import__("json").dumps(message) + "\n")
        file.flush()
        while True:
            reply = __import__("json").loads(file.readline())
            if "return" in reply or "error" in reply:
                return reply
    call("qmp_capabilities")
    return call


def main():
    if len(sys.argv) != 3:
        print("usage: exfat-qemu-test.py IMAGE OUT")
        return 2
    image_src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    image = os.path.join(out, "r5.image")
    card = os.path.join(out, "card.img")
    serial = os.path.join(out, "serial.log")
    qmp_path = os.path.join(out, "qmp.sock")
    haiku = os.environ.get("HAIKU_TREE") or os.environ.get("HAIKU")
    if not haiku:
        print("set HAIKU_TREE to the Haiku tree")
        return 2
    bfs_path = (haiku + "/generated.x86_64/objects/linux/x86_64/release/"
                "tools/bfs_shell/bfs_shell")
    mkfs = os.environ.get("MKFS_EXFAT") or shutil.which("mkfs.exfat")
    fsck = os.environ.get("FSCK_EXFAT") or shutil.which("fsck.exfat")
    if not mkfs or not os.path.exists(mkfs):
        print("mkfs.exfat was not found; set MKFS_EXFAT")
        return 2
    if not fsck or not os.path.exists(fsck):
        print("fsck.exfat was not found; set FSCK_EXFAT")
        return 2
    print("copy", image_src)
    shutil.copy(image_src, image)
    subprocess.check_call(["dd", "if=/dev/zero", "of=" + card, "bs=1M",
                           "count=16", "status=none"])
    subprocess.check_call([mkfs, "-c", "32K", card])
    host = os.path.join(out, "exfat_host")
    src = os.path.join(ROOT, "distro/kernel/src/add-ons/kernel/file_systems/"
                       "exfat")
    subprocess.check_call([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-DEXFAT_HOST_TEST", "-I" + src, "-o", host,
        os.path.join(src, "exfat_update.cpp"),
        os.path.join(src, "exfat_host.cpp")])
    subprocess.check_call([host, card, "seed"])

    gcc = None
    tools = os.path.join(haiku, "generated.x86_64")
    direct = os.path.join(tools, "cross-tools-x86_64/bin/"
                          "x86_64-unknown-haiku-gcc")
    if os.path.exists(direct):
        gcc = direct
    elif os.path.isdir(tools):
        for name in os.listdir(tools):
            candidate = os.path.join(tools, name, "cross-tools-x86_64/bin/"
                                     "x86_64-unknown-haiku-gcc")
            if os.path.exists(candidate):
                gcc = candidate
                break
    if gcc is None:
        print("Haiku gcc was not found under", tools)
        return 2
    ops = os.path.join(out, "exfat-ops")
    subprocess.check_call([gcc, "-O2", "-o", ops,
                           os.path.join(ROOT, "scripts/exfat-guest.c")])
    print("guest binary", ops)

    stage = os.path.join(out, "stage")
    os.makedirs(stage, exist_ok=True)
    guest = os.path.join(ROOT, "scripts/exfat-guest.sh")
    with open(guest, newline="") as f:
        text = f.read().replace("\r\n", "\n")
    with open(os.path.join(stage, "exfat-guest"), "w", newline="\n") as f:
        f.write(text)
    job = ("job x-vnd.R5OS-exfat-test {\n"
           "\tlaunch /bin/sh /boot/home/exfat-guest\n"
           "\ton initial_volumes_mounted\n"
           "}\n")
    with open(os.path.join(stage, "exfat-job"), "w", newline="\n") as f:
        f.write(job)
    commands = ["mkdir myfs/system/settings/launch",
                "rm myfs/system/settings/launch/exfat-test",
                "cp :%s/exfat-job myfs/system/settings/launch/exfat-test" % stage,
                "rm myfs/home/exfat-guest",
                "cp :%s/exfat-guest myfs/home/exfat-guest" % stage,
                "rm myfs/home/exfat-ops",
                "cp :%s/exfat-ops myfs/home/exfat-ops" % out]
    planted = bfs(image, commands, bfs_path)
    if "Unmounting FS failed" in planted or "exfat-ops" not in bfs(
            image, ["ls myfs/home"], bfs_path):
        print(planted)
        print("FAIL bfs_shell did not write the image")
        return 1
    print("planted")

    if os.path.exists(serial):
        os.remove(serial)
    if os.path.exists(qmp_path):
        os.remove(qmp_path)
    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-name", "R5-exfat",
        "-machine", "pc,accel=kvm", "-cpu", "host", "-m", "2048",
        "-smp", "2", "-display", "none",
        "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % image,
        "-drive", "file=%s,format=raw,if=ide,index=1,media=disk" % card,
        "-vga", "std", "-usb", "-device", "usb-tablet",
        "-serial", "file:" + serial,
        "-qmp", "unix:%s,server,nowait" % qmp_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failed = False
    try:
        time.sleep(1)
        call = qmp_key(qmp_path)
        seen = 0
        started = False
        next_enter = time.time() + 25
        pulled = False
        deadline = time.time() + 420
        while time.time() < deadline:
            time.sleep(0.4)
            try:
                with open(serial, errors="replace") as f:
                    lines = [line.strip() for line in f if "exfat:" in line]
            except OSError:
                lines = []
            if not started and time.time() >= next_enter:
                call("send-key", keys=[{"type": "qcode", "data": "ret"}])
                next_enter = time.time() + 15
            for line in lines[seen:]:
                seen += 1
                print(line)
                if "exfat: start" in line or line.startswith("exfat: fill"):
                    started = True
                    deadline = time.time() + 900
                if (" fail " in line or "mount 1" in line
                        or "mount-auto 1" in line):
                    failed = True
                if "exfat: pull" in line and "pull-close" not in line:
                    pulled = True
                    deadline = 0
                    break
        call("quit")
    finally:
        try:
            qemu.wait(timeout=30)
        except subprocess.TimeoutExpired:
            qemu.kill()
    if failed or not pulled:
        print("FAIL the guest did not finish cleanly")
        return 1
    checked = subprocess.run([host, card, "pull"], capture_output=True,
                             text=True)
    print(checked.stdout)
    print(checked.stderr)
    if checked.returncode != 0:
        print("FAIL pull check", checked.returncode)
        return 1
    check = subprocess.run([fsck, "-n", card], capture_output=True, text=True)
    print(check.stdout)
    print(check.stderr)
    if check.returncode != 0:
        print("fsck exit", check.returncode)
    print("exfat-qemu-test: clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
