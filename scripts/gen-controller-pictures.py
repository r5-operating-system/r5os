#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Controller pictures for BGameControllerView (docs/GAME-CONTROLLERS.md).

Reads the nine regular drawings of the Gamepad Line Art Pack (CC0, by
gidedin, distro/game/art/gamepad-line-art-pack/svg/regular) and R5's own
Genesis / M30 drawing (distro/game/art/r5/genesis-m30.svg, MIT), and writes
distro/game/lib/GameControllerPictureData.h:

- every drawing as filled outlines (even-odd per path), flattened and
  simplified, in quarter units of the drawing's viewBox, relative to its
  top left corner; strokes (the M30, the DualSense guide ring) become
  outlines here, so the view never strokes a BShape; text (the M30's
  letters) stays text and is drawn with the bold UI font;
- per control (game_controller_button numbers, 21 and 22 for digital
  triggers drawn on the picture) the outline that lights up: the outer
  edge of that button's own path, or the area found by a flood fill from
  a point inside it (shoulders, touchpads, d-pad arms), slightly widened
  so it runs under the line;
- per stick the circle of the cap, and how far it travels;
- the vendor and product table from SDL's src/joystick/controller_list.h
  (zlib; only the IDs and the controller type are used, as facts) and the
  Sega Genesis style IDs of the shipped SDL_GameControllerDB.

The control places are given below by path number (the pack's own path
order) or by a point inside, measured from the drawings; --check DIR
writes a picture of every model with all controls lit, to check them.

Run with Python 3 and Pillow (no other module):
  python3 scripts/gen-controller-pictures.py [--check DIR] [--sdl FILE]
--sdl defaults to generated/sdl/controller_list.h, not in git: fetch it
at the commit recorded in SDL_SOURCE below (raw.githubusercontent.com/
libsdl-org/SDL/<commit>/src/joystick/controller_list.h) and update
SDL_SOURCE when the commit changes. The output is checked in; run again
only when a drawing, a place or the SDL list changes. It takes about a
minute (the flood fills are plain Python).
"""

import math
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = os.path.join(ROOT, "distro/game/art/gamepad-line-art-pack/svg/regular")
R5ART = os.path.join(ROOT, "distro/game/art/r5")
DB = os.path.join(ROOT, "distro/game/data/gamecontrollerdb.txt")
OUT = os.path.join(ROOT, "distro/game/lib/GameControllerPictureData.h")
SDL_SOURCE = ("libsdl-org/SDL src/joystick/controller_list.h,\n"
    " * commit b2de7a19ad3c0d5063a5fb8e4cf5deeeb9ce4560 (16 Nov 2026,\n"
    " * blob b52038ce674c)")

Q = 4.0          # data units per viewBox unit
TOLERANCE = 1.5  # simplification, in data units (0.375 viewBox units)
RASTER = 2.0     # raster pixels per viewBox unit, for the flood fills

# game_controller_button numbers (GameController.h).
A, B, X, Y, BACK, GUIDE, START, LSTICK, RSTICK, LSHOULDER, RSHOULDER, \
    DUP, DDOWN, DLEFT, DRIGHT, MISC1 = range(16)
TOUCHPAD = 20
LTRIGGER, RTRIGGER = 21, 22     # digital triggers drawn on the picture

FAMILY = {"xbox": 1, "playstation": 2, "nintendo": 3, "genesis": 4}

# Per model: the file, the family, whether analog trigger caps are drawn
# over the shoulders, the trigger names, the controls and the sticks.
# ("p", n): the outer edge of path n. ("s", x, y): a flood fill from that
# point (viewBox units). ("dpad", cx, cy, r): the four arms of a d-pad
# drawn as one shape, flood filled and cut along the diagonals.
# Sticks: (button, cap path, well path or None).
# Face buttons by place: A is the lower one, B the right, X the left, Y
# the upper, whatever is printed (the Nintendo drawings print B A Y X there).
MODELS = [
    dict(name="xbox-series", family="xbox", caps=("LT", "RT"),
        controls={A: ("p", 22), B: ("p", 12), X: ("p", 11), Y: ("p", 6),
            BACK: ("p", 13), START: ("p", 14), GUIDE: ("p", 5),
            MISC1: ("p", 21), LSHOULDER: ("s", 376, 76),
            RSHOULDER: ("s", 1152, 76),
            "dpad": ("dpad", 576.6, 529.4, 100)},
        sticks=[(LSTICK, 9, 7), (RSTICK, 28, 27)]),
    dict(name="xbox-one", family="xbox", caps=("LT", "RT"),
        controls={A: ("p", 22), B: ("p", 12), X: ("p", 11), Y: ("p", 6),
            BACK: ("p", 13), START: ("p", 14), GUIDE: ("p", 5),
            LSHOULDER: ("s", 372, 92), RSHOULDER: ("s", 1158, 92),
            "dpad": ("dpad", 568.2, 537.2, 100)},
        sticks=[(LSTICK, 9, 7), (RSTICK, 27, 25)]),
    dict(name="xbox-elite-2", family="xbox", caps=("LT", "RT"),
        controls={A: ("p", 23), B: ("p", 12), X: ("p", 11), Y: ("p", 6),
            BACK: ("p", 13), START: ("p", 14), GUIDE: ("p", 5),
            LSHOULDER: ("s", 378, 78), RSHOULDER: ("s", 1150, 78),
            "dpad": ("dpad", 566.2, 536.5, 115)},
        sticks=[(LSTICK, 9, 7), (RSTICK, 27, 26)]),
    dict(name="dualshock-4", family="playstation", caps=("L2", "R2"),
        controls={A: ("p", 35), B: ("p", 27), X: ("p", 26), Y: ("p", 22),
            BACK: ("p", 18), START: ("p", 19), GUIDE: ("p", 56),
            TOUCHPAD: ("s", 767, 260), LSHOULDER: ("s", 290, 88),
            RSHOULDER: ("s", 1245, 88), DUP: ("p", 25), DLEFT: ("p", 28),
            DRIGHT: ("p", 29), DDOWN: ("p", 34)},
        sticks=[(LSTICK, 47, None), (RSTICK, 46, None)]),
    dict(name="dualsense", family="playstation", caps=("L2", "R2"),
        controls={A: ("p", 26), B: ("p", 17), X: ("p", 18), Y: ("p", 13),
            BACK: ("p", 11), START: ("p", 12), GUIDE: ("p", 51),
            MISC1: ("p", 47), TOUCHPAD: ("s", 768, 225),
            LSHOULDER: ("s", 305, 84), RSHOULDER: ("s", 1230, 84),
            DUP: ("p", 15), DLEFT: ("p", 19), DRIGHT: ("p", 20),
            DDOWN: ("p", 25)},
        sticks=[(LSTICK, 40, 34), (RSTICK, 41, 35)]),
    dict(name="dualsense-edge", family="playstation", caps=("L2", "R2"),
        controls={A: ("p", 26), B: ("p", 18), X: ("p", 17), Y: ("p", 13),
            BACK: ("p", 12), START: ("p", 11), GUIDE: ("p", 52),
            MISC1: ("p", 44), TOUCHPAD: ("s", 768, 225),
            LSHOULDER: ("s", 305, 84), RSHOULDER: ("s", 1230, 84),
            DUP: ("p", 15), DLEFT: ("p", 19), DRIGHT: ("p", 20),
            DDOWN: ("p", 25)},
        sticks=[(LSTICK, 38, None), (RSTICK, 39, None)]),
    dict(name="switch-pro", family="nintendo", caps=("ZL", "ZR"),
        controls={A: ("p", 20), B: ("p", 13), X: ("p", 12), Y: ("p", 5),
            BACK: ("p", 4), START: ("p", 3), GUIDE: ("p", 14),
            MISC1: ("p", 15), LSHOULDER: ("s", 370, 85),
            RSHOULDER: ("s", 1165, 85),
            "dpad": ("dpad", 561.1, 514.1, 100)},
        sticks=[(LSTICK, 10, 7), (RSTICK, 25, 23)]),
    dict(name="joy-con", family="nintendo", caps=None,
        controls={A: ("p", 15), B: ("p", 11), X: ("p", 12), Y: ("p", 6),
            BACK: ("p", 5), START: ("p", 4), GUIDE: ("p", 28),
            MISC1: ("p", 29), LSHOULDER: ("s", 515, 60),
            RSHOULDER: ("s", 1020, 60), DUP: ("p", 17), DLEFT: ("p", 21),
            DRIGHT: ("p", 22), DDOWN: ("p", 26)},
        sticks=[(LSTICK, 9, 7), (RSTICK, 20, 19)]),
    dict(name="joy-con-2", family="nintendo", caps=None,
        controls={A: ("p", 15), B: ("p", 12), X: ("p", 11), Y: ("p", 6),
            BACK: ("p", 5), START: ("p", 4), GUIDE: ("p", 36),
            MISC1: ("p", 37), LSHOULDER: ("s", 592, 42),
            RSHOULDER: ("s", 945, 42), DUP: ("p", 17), DLEFT: ("p", 26),
            DRIGHT: ("p", 27), DDOWN: ("p", 34)},
        sticks=[(LSTICK, 9, 7), (RSTICK, 22, 21)]),
    # R5's drawing: C and Z are the shoulders of the standard layout and
    # L and R its triggers, where the database puts the M30's extra keys.
    dict(name="genesis-m30", family="genesis", caps=None, r5=True,
        controls={A: ("s", 930, 479), B: ("s", 1070, 455),
            RSHOULDER: ("s", 1210, 431), X: ("s", 952, 340),
            Y: ("s", 1086, 318), LSHOULDER: ("s", 1220, 296),
            BACK: ("s", 640, 426), GUIDE: ("s", 806, 450),
            START: ("s", 700, 560), LTRIGGER: ("s", 300, 180),
            RTRIGGER: ("s", 1160, 180),
            "dpad": ("dpad", 372, 440, 128)},
        sticks=[]),
]


# #pragma mark - SVG paths


def tokens(d):
    return re.findall(r"[MmLlHhVvCcSsQqAaZz]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?",
        d)


def arc_points(x0, y0, rx, ry, phi, large, sweep, x, y):
    """The SVG endpoint arc as points after (x0, y0)."""
    if rx == 0 or ry == 0:
        return [(x, y)]
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x0 - x) / 2, (y0 - y) / 2
    x1 = cp * dx + sp * dy
    y1 = -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry)
    if lam > 1:
        rx *= math.sqrt(lam)
        ry *= math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    co = math.sqrt(max(0, num / den)) if den else 0
    if large == sweep:
        co = -co
    cx1 = co * rx * y1 / ry
    cy1 = -co * ry * x1 / rx
    cx = cp * cx1 - sp * cy1 + (x0 + x) / 2
    cy = sp * cx1 + cp * cy1 + (y0 + y) / 2

    def angle(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a
    t1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry)
    dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx,
        (-y1 - cy1) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    steps = max(4, int(abs(dt) * max(rx, ry) / 2))
    out = []
    for i in range(1, steps + 1):
        t = t1 + dt * i / steps
        out.append((cx + rx * math.cos(t) * cp - ry * math.sin(t) * sp,
            cy + rx * math.cos(t) * sp + ry * math.sin(t) * cp))
    return out


def cubic_points(p0, p1, p2, p3):
    length = (math.dist(p0, p1) + math.dist(p1, p2) + math.dist(p2, p3))
    steps = max(2, min(64, int(math.ceil(length / 1.5))))
    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1 - t
        out.append((u * u * u * p0[0] + 3 * u * u * t * p1[0]
                + 3 * u * t * t * p2[0] + t * t * t * p3[0],
            u * u * u * p0[1] + 3 * u * u * t * p1[1]
                + 3 * u * t * t * p2[1] + t * t * t * p3[1]))
    return out


def parse_path(d):
    """Subpaths of an SVG path as (points, closed), in its own units."""
    toks = tokens(d)
    subs = []
    cur = None
    x = y = 0.0
    sx = sy = 0.0
    i = 0
    cmd = None
    while i < len(toks):
        t = toks[i]
        if re.match(r"[A-Za-z]", t):
            cmd = t
            i += 1
            if cmd in "Zz":
                if cur is not None:
                    subs.append((cur, True))
                    cur = None
                x, y = sx, sy
                continue
        rel = cmd.islower()
        c = cmd.upper()

        def num(k):
            return float(toks[i + k])
        if c == "M":
            nx, ny = num(0), num(1)
            if rel:
                nx += x
                ny += y
            if cur is not None:
                subs.append((cur, False))
            cur = [(nx, ny)]
            x, y = sx, sy = nx, ny
            i += 2
            cmd = "l" if rel else "L"
        elif c == "L":
            nx, ny = num(0), num(1)
            if rel:
                nx += x
                ny += y
            cur.append((nx, ny))
            x, y = nx, ny
            i += 2
        elif c == "H":
            nx = num(0) + (x if rel else 0)
            cur.append((nx, y))
            x = nx
            i += 1
        elif c == "V":
            ny = num(0) + (y if rel else 0)
            cur.append((x, ny))
            y = ny
            i += 1
        elif c == "C":
            v = [num(k) for k in range(6)]
            if rel:
                v = [v[k] + (x if k % 2 == 0 else y) for k in range(6)]
            cur.extend(cubic_points((x, y), (v[0], v[1]), (v[2], v[3]),
                (v[4], v[5])))
            x, y = v[4], v[5]
            i += 6
        elif c == "A":
            rx, ry, phi, large, sweep, nx, ny = [num(k) for k in range(7)]
            if rel:
                nx += x
                ny += y
            cur.extend(arc_points(x, y, rx, ry, phi, int(large), int(sweep),
                nx, ny))
            x, y = nx, ny
            i += 7
        else:
            raise SystemExit("unsupported path command " + cmd)
    if cur is not None:
        subs.append((cur, False))
    return subs


def circle_points(cx, cy, r):
    steps = max(16, int(2 * math.pi * r / 2))
    return [(cx + r * math.cos(2 * math.pi * k / steps),
        cy + r * math.sin(2 * math.pi * k / steps)) for k in range(steps)]


def rounded_rect(x, y, w, h, r):
    pts = []
    for (cx, cy, a0) in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0),
            (x + r, y + h - r, 90), (x + r, y + r, 180)):
        for k in range(9):
            a = math.radians(a0 + 90 * k / 8)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def offset(points, closed, distance):
    """The polyline moved sideways by distance, with mitred corners."""
    n = len(points)
    out = []
    for k in range(n):
        p = points[k]
        if closed or 0 < k < n - 1:
            a = points[k - 1]
            b = points[(k + 1) % n]
        elif k == 0:
            a, b = p, points[1]
        else:
            a, b = points[k - 1], p
        n1 = normal(a, p) if a != p else normal(p, b)
        n2 = normal(p, b) if b != p else n1
        mx, my = n1[0] + n2[0], n1[1] + n2[1]
        length = math.hypot(mx, my)
        if length < 1e-6:
            mx, my = n1
        else:
            mx, my = mx / length, my / length
        cos = mx * n1[0] + my * n1[1]
        scale = distance / max(cos, 0.35)
        out.append((p[0] + mx * scale, p[1] + my * scale))
    return out


def normal(a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy) or 1
    return (-dy / length, dx / length)


def dedupe(points):
    out = []
    for p in points:
        if not out or math.dist(out[-1], p) > 1e-6:
            out.append(p)
    if len(out) > 1 and math.dist(out[0], out[-1]) < 1e-6:
        out.pop()
    return out


def stroke(subpath, closed, width):
    """A stroke as outlines to fill even-odd: two rings for a closed line,
    one loop with round ends for an open one."""
    pts = dedupe(subpath)
    half = width / 2
    if closed:
        return [offset(pts, True, half), offset(pts, True, -half)]
    left = offset(pts, False, half)
    right = offset(pts, False, -half)
    loop = list(left)
    end = pts[-1]
    a = math.atan2(left[-1][1] - end[1], left[-1][0] - end[0])
    for k in range(1, 8):
        t = a - math.pi * k / 8
        loop.append((end[0] + half * math.cos(t), end[1] + half * math.sin(t)))
    loop.extend(reversed(right))
    start = pts[0]
    a = math.atan2(right[0][1] - start[1], right[0][0] - start[0])
    for k in range(1, 8):
        t = a - math.pi * k / 8
        loop.append((start[0] + half * math.cos(t),
            start[1] + half * math.sin(t)))
    return [loop]


def simplify(points, tolerance, closed=True):
    if len(points) < 4:
        return points
    pts = points + [points[0]] if closed else points
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]
        bx, by = pts[b]
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        best, index = -1.0, -1
        for k in range(a + 1, b):
            px, py = pts[k]
            if length == 0:
                dd = math.hypot(px - ax, py - ay)
            else:
                dd = abs(dx * (ay - py) - dy * (ax - px)) / length
            if dd > best:
                best, index = dd, k
        if best > tolerance:
            keep[index] = True
            stack.append((a, index))
            stack.append((index, b))
    out = [p for p, k in zip(pts, keep) if k]
    if closed:
        out.pop()
    if len(out) < 3:
        return points
    return out


# #pragma mark - drawings


def load_pack(name):
    """The pack's paths (each a list of subpaths), in data units relative
    to the viewBox corner, and the viewBox."""
    s = open(os.path.join(PACK, name + ".svg"), encoding="utf-8").read()
    vb = [float(v) for v in re.search(r'viewBox="([^"]*)"', s).group(1).split()]
    ox, oy = vb[0] * Q, vb[1] * Q
    paths = []
    group = re.search(r'<g transform="scale\(0\.25\)"[^>]*>(.*?)</g>', s, re.S)
    for attrs in re.findall(r"<path ([^>]*)>", group.group(1)):
        d = re.search(r'\bd="([^"]*)"', attrs).group(1)
        m = re.search(r'translate\(([-\d.]+) ([-\d.]+)\)', attrs)
        tx, ty = (float(m.group(1)), float(m.group(2))) if m else (0, 0)
        subs = []
        for pts, closed in parse_path(d):
            subs.append([(px + tx - ox, py + ty - oy) for px, py in pts])
        paths.append(subs)
    # Strokes outside the group (the DualSense guide button): outlines.
    for attrs in re.findall(r"<circle ([^>]*)/>", s[group.end():]):
        cx, cy, r, w = [float(re.search(k + r'="([-\d.]+)"', attrs).group(1))
            for k in ("cx", "cy", "r", "stroke-width")]
        ring = circle_points(cx * Q - ox, cy * Q - oy, r * Q)
        paths.append(stroke(ring, True, w * Q))
    return vb, paths, []


def load_r5(name):
    """R5's own drawing: every stroked element becomes an outline path;
    text stays text."""
    s = open(os.path.join(R5ART, name + ".svg"), encoding="utf-8").read()
    s = re.sub(r"<!--.*?-->", "", s, flags=re.S)
    vb = [float(v) for v in re.search(r'viewBox="([^"]*)"', s).group(1).split()]
    ox, oy = vb[0], vb[1]
    widths = {}
    fonts = {}
    for cls, body in re.findall(r"\.(\w+)\s*\{([^}]*)\}", s):
        m = re.search(r"stroke-width:\s*([\d.]+)", body)
        if m:
            widths[cls] = float(m.group(1))
        m = re.search(r"font:\s*\d+\s+([\d.]+)px", body)
        if m:
            fonts[cls] = float(m.group(1))
    paths = []
    texts = []

    def shift(pts):
        return [((px - ox) * Q, (py - oy) * Q) for px, py in pts]

    # Group classes: <g class="d"> gives its children that class.
    def walk(markup, inherited):
        pos = 0
        for m in re.finditer(r'<g( [^>]*)?>(.*?)</g>|<(path|circle|rect|text)'
                r'( [^>]*?)(/>|>(.*?)</text>)', markup, re.S):
            if m.group(2) is not None and m.group(0).startswith("<g"):
                cls = re.search(r'class="(\w+)"', m.group(1) or "")
                walk(m.group(2), cls.group(1) if cls else inherited)
                continue
            tag, attrs = m.group(3), m.group(4)
            cm = re.search(r'class="(\w+)"', attrs)
            cls = cm.group(1) if cm else inherited

            def a(k):
                return float(re.search(r'\b' + k + r'="([-\d.]+)"',
                    attrs).group(1))
            if tag == "text":
                size = fonts.get(cls, 30)
                sm = re.search(r"font-size:\s*([\d.]+)px", attrs)
                if sm:
                    size = float(sm.group(1))
                texts.append((m.group(6), (a("x") - ox) * Q,
                    (a("y") - oy) * Q, size * Q))
                continue
            width = widths[cls] * Q
            if tag == "circle":
                subs = [(circle_points(a("cx"), a("cy"), a("r")), True)]
            elif tag == "rect":
                subs = [(rounded_rect(a("x"), a("y"), a("width"),
                    a("height"), a("rx")), True)]
            else:
                d = re.search(r'\bd="([^"]*)"', attrs).group(1)
                subs = parse_path(d)
            outline = []
            for pts, closed in subs:
                outline.extend(stroke(shift(pts), closed, width))
            paths.append(outline)
    body = re.search(r"<svg[^>]*>(.*)</svg>", s, re.S).group(1)
    body = re.sub(r"<style>.*?</style>", "", body, flags=re.S)
    walk(body, None)
    return vb, paths, texts


# #pragma mark - raster and controls


def rasterize(paths, size, only=None):
    """Mode L, 255 background and 0 lines, even-odd per path."""
    image = Image.new("1", size, 0)
    for k, subs in enumerate(paths):
        if only is not None and k not in only:
            continue
        layer = Image.new("1", size, 0)
        for sub in subs:
            if len(sub) < 3:
                continue
            piece = Image.new("1", size, 0)
            ImageDraw.Draw(piece).polygon(
                [(x / Q * RASTER, y / Q * RASTER) for x, y in sub], fill=1)
            layer = ImageChops.logical_xor(layer, piece)
        image = ImageChops.logical_or(image, layer)
    return image


def trace(mask):
    """The outer edge of the largest part of a mode 1 image, as pixel
    centres (Moore neighbour tracing)."""
    w, h = mask.size
    data = mask.load()
    start = None
    for yy in range(h):
        for xx in range(w):
            if data[xx, yy]:
                start = (xx, yy)
                break
        if start:
            break
    if start is None:
        return []
    dirs = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1),
        (1, -1)]

    def inside(p):
        return 0 <= p[0] < w and 0 <= p[1] < h and data[p[0], p[1]]
    contour = [start]
    cur = start
    back = 4
    seen = set()
    while True:
        found = False
        for i in range(1, 9):
            nd = (back + i) % 8
            nxt = (cur[0] + dirs[nd][0], cur[1] + dirs[nd][1])
            if inside(nxt):
                pd = (back + i - 1) % 8
                prev = (cur[0] + dirs[pd][0], cur[1] + dirs[pd][1])
                cur = nxt
                back = dirs.index((prev[0] - cur[0], prev[1] - cur[1]))
                found = True
                break
        if not found:
            break
        state = (cur, back)
        if state in seen:
            break
        seen.add(state)
        if cur == start and len(contour) > 2:
            break
        contour.append(cur)
    return contour


def largest_part(mask):
    """Keeps only the connected part with the most pixels."""
    w, h = mask.size
    work = mask.convert("L")
    best, best_count = None, 0
    data = work.load()
    for yy in range(0, h, 2):
        for xx in range(0, w, 2):
            if data[xx, yy] == 255:
                ImageDraw.floodfill(work, (xx, yy), 100)
                part = work.point(lambda v: 255 if v == 100 else 0)
                count = part.histogram()[255]
                if count > best_count:
                    best, best_count = part, count
                work = work.point(lambda v: 50 if v == 100 else v)
                data = work.load()
    return best.convert("1") if best else mask


def region_from_seed(lines, x, y, limit):
    """The area around (x, y) (viewBox units relative to the corner) that
    no line crosses, as a mode 1 image."""
    w, h = lines.size
    work = lines.convert("L").point(lambda v: 0 if v else 255)
    px, py = int(x * RASTER), int(y * RASTER)
    data = work.load()
    if data[px, py] != 255:
        for r in range(1, 8):
            hits = [(px + dx, py + dy) for dx in range(-r, r + 1)
                for dy in range(-r, r + 1) if data[px + dx, py + dy] == 255]
            if hits:
                px, py = hits[0]
                break
    ImageDraw.floodfill(work, (px, py), 128)
    region = work.point(lambda v: 255 if v == 128 else 0).convert("1")
    count = region.convert("L").histogram()[255]
    if count > limit * w * h:
        raise SystemExit("flood fill at (%g, %g) leaks (%d pixels)"
            % (x, y, count))
    if count < 30:
        raise SystemExit("flood fill at (%g, %g) found only %d pixels"
            % (x, y, count))
    return region


def widen(region, pixels):
    return region.convert("L").filter(ImageFilter.MaxFilter(
        2 * pixels + 1)).point(lambda v: 255 if v > 127 else 0).convert("1")


def mask_outline(region):
    pts = trace(largest_part(region))
    return simplify([((x + 0.5) / RASTER * Q, (y + 0.5) / RASTER * Q)
        for x, y in pts], TOLERANCE)


def wedge(size, cx, cy, direction):
    """The quarter of the picture around (cx, cy) toward direction."""
    w, h = size
    far = 4 * max(w, h)
    c = (cx * RASTER, cy * RASTER)
    corners = {
        DUP: [(c[0] - far, c[1] - far), (c[0] + far, c[1] - far)],
        DDOWN: [(c[0] - far, c[1] + far), (c[0] + far, c[1] + far)],
        DLEFT: [(c[0] - far, c[1] - far), (c[0] - far, c[1] + far)],
        DRIGHT: [(c[0] + far, c[1] - far), (c[0] + far, c[1] + far)],
    }[direction]
    image = Image.new("1", size, 0)
    ImageDraw.Draw(image).polygon([c] + corners, fill=1)
    return image


def outer_subpath(subs):
    def area(p):
        return abs(sum(p[k][0] * p[k - 1][1] - p[k - 1][0] * p[k][1]
            for k in range(len(p)))) / 2
    return max(subs, key=area)


def bbox(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def build(model):
    if model.get("r5"):
        vb, paths, texts = load_r5(model["name"])
    else:
        vb, paths, texts = load_pack(model["name"])
    size = (int(vb[2] * RASTER) + 2, int(vb[3] * RASTER) + 2)
    lines = rasterize(paths, size)
    widening = int(round(2 * RASTER))
    controls = []
    for key, spec in model["controls"].items():
        kind = spec[0]
        if kind == "p":
            outline = simplify(outer_subpath(paths[spec[1]]), TOLERANCE)
            controls.append((key, outline))
        elif kind == "s":
            region = widen(region_from_seed(lines, spec[1] - vb[0],
                spec[2] - vb[1], 0.15), widening)
            controls.append((key, mask_outline(region)))
        elif kind == "dpad":
            cx, cy, r = spec[1] - vb[0], spec[2] - vb[1], spec[3]
            for direction, (dx, dy) in ((DUP, (0, -1)), (DDOWN, (0, 1)),
                    (DLEFT, (-1, 0)), (DRIGHT, (1, 0))):
                region = None
                for f in (0.45, 0.55, 0.35, 0.65):
                    try:
                        region = region_from_seed(lines, cx + dx * f * r,
                            cy + dy * f * r, 0.05)
                        break
                    except SystemExit:
                        continue
                if region is None:
                    raise SystemExit(model["name"] + ": d-pad not found")
                region = ImageChops.logical_and(widen(region, widening),
                    wedge(size, cx, cy, direction))
                controls.append((direction, mask_outline(region)))
    controls.sort(key=lambda c: c[0])
    sticks = []
    for button, cap, well in model["sticks"]:
        x0, y0, x1, y1 = bbox(outer_subpath(paths[cap]))
        radius = (x1 - x0 + y1 - y0) / 4
        travel = 0.3 * radius
        if well is not None:
            wx0, wy0, wx1, wy1 = bbox(outer_subpath(paths[well]))
            travel = (wx1 - wx0 + wy1 - wy0) / 4 - radius
        travel = min(max(travel, 0.25 * radius), 0.4 * radius)
        cap_outline = simplify(outer_subpath(paths[cap]), TOLERANCE)
        controls.append((button, cap_outline))
        sticks.append((button, (x0 + x1) / 2, (y0 + y1) / 2, radius, travel))
    simple = []
    for subs in paths:
        out = []
        for sub in subs:
            s = simplify(dedupe(sub), TOLERANCE)
            if len(s) >= 3:
                out.append(s)
        if out:
            simple.append(out)
    return dict(model, vb=vb, paths=simple, controls=controls, sticks=sticks,
        texts=texts)


# #pragma mark - vendor and product IDs


def sdl_table(path):
    """(vendor, product, model) from SDL's controller list."""
    kinds = {
        "PS3Controller": "dualshock-4", "PS4Controller": "dualshock-4",
        "XInputPS4Controller": "dualshock-4", "PS5Controller": "dualsense",
        "PS5EdgeController": "dualsense-edge",
        "XBox360Controller": "xbox-one", "XBoxOneController": "xbox-one",
        "XBoxEliteController": "xbox-elite-2",
        "SwitchProController": "switch-pro",
        "SwitchInputOnlyController": "switch-pro",
        "XInputSwitchController": "switch-pro",
        "Switch2ProController": "switch-pro",
        "Switch2InputOnlyController": "switch-pro",
        "SwitchJoyConLeft": "joy-con", "SwitchJoyConRight": "joy-con",
        "SwitchJoyConPair": "joy-con",
    }
    out = {}
    for line in open(path, encoding="utf-8"):
        if line.lstrip().startswith("//"):
            continue
        m = re.search(r"MAKE_CONTROLLER_ID\(\s*0x([0-9a-fA-F]+),\s*"
            r"0x([0-9a-fA-F]+)\s*\),\s*k_eControllerType_(\w+)", line)
        if not m:
            continue
        model = kinds.get(m.group(3))
        if model is None:
            continue
        rest = line[m.end():]
        # SDL keeps one type for Xbox One and Series controllers; the name
        # or the comment says which.
        if model == "xbox-one" and "Series" in rest:
            model = "xbox-series"
        if model == "joy-con" and "Switch 2" in rest:
            model = "joy-con-2"
        out[(int(m.group(1), 16), int(m.group(2), 16))] = model
    return out


GENESIS_NAMES = ("8BitDo M30", "Mega Drive Controller",
    "Sega Genesis Mini 3B Controller", "Sega Mega Drive Mini 6B Controller",
    "NSO Sega Genesis Controller", "Retro Bit Sega Genesis Adapter")


def genesis_ids():
    """The Sega Genesis style IDs in the shipped SDL_GameControllerDB."""
    out = set()
    for line in open(DB, encoding="utf-8"):
        fields = line.strip().split(",")
        if len(fields) < 3 or not any(fields[1] == n or fields[1] == n
                + " Xbox" for n in GENESIS_NAMES):
            continue
        guid = fields[0]
        if len(guid) != 32 or guid[12:16] != "0000":
            continue
        vendor = int(guid[10:12] + guid[8:10], 16)
        product = int(guid[18:20] + guid[16:18], 16)
        if vendor and product:
            out.add((vendor, product))
    return out


# #pragma mark - output


def c_name(name):
    return "k" + "".join(part.capitalize() for part in re.split("[-]", name))


def write_header(models, table, out):
    lines = []
    w = lines.append
    w("/*")
    w(" * SPDX-FileCopyrightText: 2026 R5OS Contributors")
    w(" * SPDX-License-Identifier: MIT")
    w(" *")
    w(" * Generated by scripts/gen-controller-pictures.py; do not edit.")
    w(" * The drawings: the Gamepad Line Art Pack by gidedin (CC0 1.0,")
    w(" * distro/game/art/gamepad-line-art-pack) and R5's genesis-m30.svg")
    w(" * (MIT). The IDs: " + SDL_SOURCE + ",")
    w(" * and the Sega Genesis style entries of the shipped")
    w(" * SDL_GameControllerDB; facts only, no SDL code.")
    w(" * Units: a quarter of a viewBox unit, from the viewBox corner.")
    w(" */")
    w("")
    w("")
    for m in models:
        n = c_name(m["name"])
        pts = []
        runs = []
        for subs in m["paths"]:
            runs.append(len(subs))
            for sub in subs:
                runs.append(len(sub))
                for x, y in sub:
                    pts.extend((int(round(x)), int(round(y))))
        cpts = []
        controls = []
        for key, outline in m["controls"]:
            controls.append((key, len(cpts) // 2, len(outline)))
            for x, y in outline:
                cpts.extend((int(round(x)), int(round(y))))
        m["counts"] = (len(pts) // 2, len(cpts) // 2)
        w("static const int16 %sPoints[] = {" % n)
        emit(w, pts)
        w("};")
        w("static const uint16 %sRuns[] = {" % n)
        emit(w, runs)
        w("};")
        w("static const int16 %sControlPoints[] = {" % n)
        emit(w, cpts)
        w("};")
        w("static const gc_control %sControls[] = {" % n)
        for key, first, count in controls:
            w("\t{ %d, %d, %d }," % (key, first, count))
        w("};")
        if m["texts"]:
            w("static const gc_text %sTexts[] = {" % n)
            for text, x, y, size in m["texts"]:
                w('\t{ "%s", %d, %d, %d },' % (text, round(x), round(y),
                    round(size)))
            w("};")
        w("")
    w("static const gc_model kModels[] = {")
    for m in models:
        n = c_name(m["name"])
        sticks = {b: s for b, *s in m["sticks"]}

        def stick(b):
            if b not in sticks:
                return "{ 0, 0, 0, 0 }"
            return "{ %d, %d, %d, %d }" % tuple(round(v) for v in sticks[b])
        caps = m["caps"]
        w('\t{ "%s", %d, %d, %d,' % (m["name"], FAMILY[m["family"]],
            round(m["vb"][2] * Q), round(m["vb"][3] * Q)))
        w("\t\t%sPoints, %sRuns, %d," % (n, n, len(runs_of(m))))
        w("\t\t%sControlPoints, %sControls, %d," % (n, n, len(m["controls"])))
        w("\t\t%s, %s," % (stick(LSTICK), stick(RSTICK)))
        w("\t\t%s, %d," % ((n + "Texts") if m["texts"] else "NULL",
            len(m["texts"])))
        w('\t\t%s, %s },' % ('"%s"' % caps[0] if caps else "NULL",
            '"%s"' % caps[1] if caps else "NULL"))
    w("};")
    w("")
    index = {m["name"]: k for k, m in enumerate(models)}
    w("// Sorted by vendor, then product, for a binary search.")
    w("static const gc_model_id kModelIDs[] = {")
    for (vendor, product), name in sorted(table.items()):
        w("\t{ 0x%04x, 0x%04x, %d },\t// %s" % (vendor, product, index[name],
            name))
    w("};")
    open(out, "w", newline="\n", encoding="utf-8").write("\n".join(lines)
        + "\n")


def runs_of(m):
    runs = []
    for subs in m["paths"]:
        runs.append(len(subs))
        runs.extend(len(s) for s in subs)
    return runs


def emit(w, values):
    for k in range(0, len(values), 12):
        w("\t" + ", ".join(str(v) for v in values[k:k + 12]) + ",")


# #pragma mark - check pictures


def check(m, directory):
    """Every control lit, the sticks pushed up and right with the caps
    drawn as the view draws them, on white, as PNG."""
    scale = 0.75 * 3
    vb = m["vb"]
    top = 0.12 * vb[2] if m["caps"] else 0
    size = (int(vb[2] * scale), int((vb[3] + top) * scale))
    image = Image.new("RGB", size, (255, 255, 255))
    draw = ImageDraw.Draw(image)
    blue = (43, 140, 255)
    ink = (31, 38, 48)

    def p(x, y):
        return (x / Q * scale, (y / Q + top) * scale)
    for key, outline in m["controls"]:
        draw.polygon([p(x, y) for x, y in outline], fill=blue)
    art = Image.new("1", size, 0)
    for subs in m["paths"]:
        layer = Image.new("1", size, 0)
        for sub in subs:
            piece = Image.new("1", size, 0)
            ImageDraw.Draw(piece).polygon([p(x, y) for x, y in sub], fill=1)
            layer = ImageChops.logical_xor(layer, piece)
        art = ImageChops.logical_or(art, layer)
    image.paste(ink, mask=art)
    for button, cx, cy, r, travel in m["sticks"]:
        ox, oy = cx + travel * 0.7, cy - travel * 0.7
        draw.ellipse([p(ox - r, oy - r), p(ox + r, oy + r)], outline=blue,
            width=int(0.006 * vb[2] * scale) + 1)
    font_path = None
    for f in ("C:/Windows/Fonts/segoeuib.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        if os.path.exists(f):
            font_path = f
            break
    for text, x, y, s in m["texts"]:
        font = ImageFont.truetype(font_path, int(s / Q * scale * 0.95))
        draw.text(p(x, y), text, fill=ink, font=font, anchor="mm")
    if m["caps"]:
        font = ImageFont.truetype(font_path, int(14 * scale))
        for key, label, value, side in ((LSHOULDER, m["caps"][0], 0.6, -1),
                (RSHOULDER, m["caps"][1], 0.15, 1)):
            outline = dict(m["controls"])[key]
            x0, y0, x1, y1 = bbox([p(x, y) for x, y in outline])
            cw, ch = 0.0586 * vb[2] * scale, 0.069 * vb[2] * scale
            cx = (x0 + x1) / 2
            bottom = y0 - 0.03 * vb[2] * scale
            box = [cx - cw / 2, bottom - ch, cx + cw / 2, bottom]
            draw.rounded_rectangle(box, radius=cw * 0.3, fill=(255, 255, 255))
            full = Image.new("L", size, 0)
            ImageDraw.Draw(full).rounded_rectangle(box, radius=cw * 0.3,
                fill=255)
            part = Image.new("L", size, 0)
            ImageDraw.Draw(part).rectangle([box[0], box[3] - ch * value,
                box[2], box[3]], fill=255)
            image.paste(blue, mask=ImageChops.multiply(full, part))
            draw.rounded_rectangle(box, radius=cw * 0.3, outline=ink,
                width=max(2, int(cw * 0.08)))
            text = "%s %d%%" % (label, value * 100)
            if side < 0:
                draw.text((box[0] - 8 * scale, (box[1] + box[3]) / 2), text,
                    fill=ink, font=font, anchor="rm")
            else:
                draw.text((box[2] + 8 * scale, (box[1] + box[3]) / 2), text,
                    fill=ink, font=font, anchor="lm")
    image = image.resize((size[0] // 3, size[1] // 3), Image.LANCZOS)
    image.save(os.path.join(directory, m["name"] + ".png"))


def main(argv):
    checkdir = None
    sdl = os.path.join(ROOT, "generated/sdl/controller_list.h")
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--check":
            checkdir = args.pop(0)
        elif a == "--sdl":
            sdl = args.pop(0)
        else:
            raise SystemExit(__doc__)
    models = []
    for spec in MODELS:
        m = build(spec)
        models.append(m)
        print("%-15s %d paths" % (m["name"], len(m["paths"])))
    table = sdl_table(sdl)
    for ids in genesis_ids():
        table[ids] = "genesis-m30"
    write_header(models, table, OUT)
    for m in models:
        print("%-15s %d points, %d control points" % ((m["name"],)
            + m["counts"]))
    print("%d IDs" % len(table))
    if checkdir:
        os.makedirs(checkdir, exist_ok=True)
        for m in models:
            check(m, checkdir)


if __name__ == "__main__":
    main(sys.argv[1:])
