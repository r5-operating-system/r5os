#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Drive a QEMU that scripts/qemu-aomi.py holds (AOMI_HOLD=1).

Usage: qmp-aomi.py <out-dir> <command> [arguments]
  shot NAME | click X Y | keys TEXT | key COMBO | link on|off | quit
"""

import importlib.util
import os
import sys


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(
        "qemu_aomi", os.path.join(here, "qemu-aomi.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    out = os.path.abspath(sys.argv[1])
    qmp = module.Qmp(os.path.join(out, "qmp.sock"))
    verb, args = sys.argv[2], sys.argv[3:]
    if verb == "shot":
        qmp.shot(os.path.join(out, args[0] + ".ppm"))
    elif verb == "click":
        qmp.click(float(args[0]), float(args[1]))
    elif verb == "keys":
        qmp.type_text(" ".join(args))
    elif verb == "key":
        qmp.keys(args[0])
    elif verb == "link":
        qmp.link(args[0] == "on")
    elif verb == "quit":
        qmp.call("quit")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
