#!/usr/bin/env python3
"""Generate the Aomi HVIF app icon from distro/artwork/aomi-icon.svg and r5-small.

Produces:
  - distro/artwork/aomi-icon.hvif
  - resource vector_icon block in distro/apps/aomi/Aomi.rdef (GPL-3.0-or-later snippet)

Follows docs/design/AOMI-UI.md ("App icon"):
  - Lagoon gradient (#5eead4 -> #14b8a6 -> #0e4f5c) along (12%,12%)..(88%,88%)
  - Blue ring (#4e7bfb): back half at 55% opacity behind pebble, front half at 100% over pebble
  - White highlight (#ffffff, 38% opacity) on upper-left of pebble
  - LOD switch between 22 px and 32 px: at <= 25 px (16 and 22 px) uses r5-small
    pebble and a slightly thicker ring.
"""
from __future__ import annotations

import math
import pathlib
import re
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
R5_SVG = ROOT / "distro" / "artwork" / "brand-kit" / "r5-symbol.svg"
AOMI_HVIF = ROOT / "distro" / "artwork" / "aomi-icon.hvif"
AOMI_RDEF = ROOT / "distro" / "apps" / "aomi" / "Aomi.rdef"

# HVIF constants (Haiku src/libs/icon/flat_icon/FlatIconFormat.h)
STYLE_SOLID = 1
STYLE_GRADIENT = 2
SHAPE_PATH = 10
SHAPE_FLAG_LOD = 1 << 3
SHAPE_FLAG_TRANSFORMERS = 1 << 4
TRANSFORMER_STROKE = 23
PATH_FLAG_CLOSED = 1 << 1
ROUND_CAP_JOIN = (2 << 4) | 2  # agg::round_cap=2, agg::round_join=2


def write_coord(value: float) -> bytes:
    if -32.0 <= value <= 95.0 and abs(value - round(value)) < 1e-4:
        return bytes([int(round(value)) + 32])
    encoded = int(round((value + 128.0) * 102.0))
    encoded = max(0, min(32767, encoded))
    return bytes([0x80 | ((encoded >> 8) & 0x7F), encoded & 0xFF])


def write_float_24(value: float) -> bytes:
    if value == 0.0:
        return b"\x00\x00\x00"
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = (bits & 0x80000000) >> 31
    exponent = ((bits & 0x7F800000) >> 23) - 127
    mantissa = bits & 0x007FFFFF
    if exponent >= 32 or exponent < -32:
        return b"\x00\x00\x00"
    short = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    short &= 0xFFFFFF
    return bytes([(short >> 16) & 0xFF, (short >> 8) & 0xFF, short & 0xFF])


def parse_svg_cubics(d: str) -> list[list[tuple[tuple[float, float], tuple[float, float], tuple[float, float]]]]:
    tokens = re.findall(r"[MCZ]|-?\d+(?:\.\d+)?", d)
    rings = []
    cur_ring = []
    pos = (0.0, 0.0)
    i = 0
    while i < len(tokens):
        cmd = tokens[i]
        i += 1
        if cmd == "M":
            pos = (float(tokens[i]), float(tokens[i + 1]))
            i += 2
            cur_ring = []
        elif cmd == "C":
            c1 = (float(tokens[i]), float(tokens[i + 1]))
            c2 = (float(tokens[i + 2]), float(tokens[i + 3]))
            end = (float(tokens[i + 4]), float(tokens[i + 5]))
            i += 6
            cur_ring.append((pos, c1, c2))
            pos = end
        elif cmd == "Z":
            rings.append(cur_ring)
            cur_ring = []
    if cur_ring:
        rings.append(cur_ring)
    return rings


def ring_area(segments: list) -> float:
    anchors = [s[0] for s in segments]
    total = 0.0
    for idx, (x1, y1) in enumerate(anchors):
        x2, y2 = anchors[(idx + 1) % len(anchors)]
        total += x1 * y2 - x2 * y1
    return total


def hvif_closed_ring(segments: list, scale: float, reverse: bool) -> bytes:
    count = len(segments)
    pts = []
    for idx in range(count):
        anchor = segments[idx][0]
        out_c = segments[idx][1]
        in_c = segments[(idx - 1) % count][2]
        pts.append((anchor, in_c, out_c))
    if reverse:
        pts = [(a, o, i) for a, i, o in reversed(pts)]
    blob = bytes([PATH_FLAG_CLOSED, count])
    for a, ic, oc in pts:
        for p in (a, ic, oc):
            blob += write_coord(p[0] * scale) + write_coord(p[1] * scale)
    return blob


def hvif_open_curves(points: list[tuple[tuple[float, float], tuple[float, float], tuple[float, float]]], scale: float) -> bytes:
    """Open path with (anchor, in_ctrl, out_ctrl) for each vertex."""
    blob = bytes([0x00, len(points)])
    for a, ic, oc in points:
        for p in (a, ic, oc):
            blob += write_coord(p[0] * scale) + write_coord(p[1] * scale)
    return blob


def rot_pt(x: float, y: float, cx: float, cy: float, deg: float) -> tuple[float, float]:
    rad = math.radians(deg)
    ca, sa = math.cos(rad), math.sin(rad)
    dx, dy = x - cx, y - cy
    return (cx + dx * ca - dy * sa, cy + dx * sa + dy * ca)


def ellipse_half_arc_points(cx: float, cy: float, rx: float, ry: float, deg: float, top: bool):
    """Exact 2-cubic-segment approximation of upper (back) or lower (front) half-ellipse."""
    k = 0.5522847498307936
    sign = -1.0 if top else 1.0  # -y is up (back half), +y is down (front half)
    p0 = rot_pt(cx - rx, cy, cx, cy, deg)
    p0_out = rot_pt(cx - rx, cy + sign * ry * k, cx, cy, deg)
    p1 = rot_pt(cx, cy + sign * ry, cx, cy, deg)
    p1_in = rot_pt(cx - rx * k, cy + sign * ry, cx, cy, deg)
    p1_out = rot_pt(cx + rx * k, cy + sign * ry, cx, cy, deg)
    p2 = rot_pt(cx + rx, cy, cx, cy, deg)
    p2_in = rot_pt(cx + rx, cy + sign * ry * k, cx, cy, deg)
    return [
        (p0, p0, p0_out),
        (p1, p1_in, p1_out),
        (p2, p2_in, p2),
    ]


def stroke_transformer(width_64: float) -> bytes:
    w_byte = max(129, min(255, int(round(width_64)) + 128))
    return bytes([TRANSFORMER_STROKE, w_byte, ROUND_CAP_JOIN, 4])


def build_aomi_hvif() -> bytes:
    svg_text = R5_SVG.read_text(encoding="utf-8")
    m_full = re.search(r'id="r5-full"\s+d="([^"]+)"', svg_text)
    m_small = re.search(r'id="r5-small"\s+d="([^"]+)"', svg_text)
    if not m_full or not m_small:
        raise SystemExit("r5-symbol.svg missing r5-full or r5-small")

    full_rings = parse_svg_cubics(m_full.group(1))
    small_rings = parse_svg_cubics(m_small.group(1))
    scale = 64.0 / 100.0

    # --- Styles (4) ---
    # 0: Lagoon linear gradient (12%,12%) -> (88%,88%)
    start = 12.0 * scale
    end = 88.0 * scale
    step = (end - start) / 128.0
    mid = (start + end) / 2.0
    matrix = (step, step, -step, step, mid, mid)
    stops = (
        (0, (0x5E, 0xEA, 0xD4, 0xFF)),    # #5eead4
        (128, (0x14, 0xB8, 0xA6, 0xFF)),  # #14b8a6
        (255, (0x0E, 0x4F, 0x5C, 0xFF)),  # #0e4f5c
    )
    style0 = bytes([STYLE_GRADIENT, 0, 2, len(stops)])
    for v in matrix:
        style0 += write_float_24(v)
    for off, col in stops:
        style0 += bytes([off, *col])

    # 1: Ring back (#4e7bfb, opacity 0.55 -> alpha 140)
    style1 = bytes([STYLE_SOLID, 0x4E, 0x7B, 0xFB, 140])
    # 2: Ring front (#4e7bfb, opacity 1.0 -> alpha 255)
    style2 = bytes([STYLE_SOLID, 0x4E, 0x7B, 0xFB, 255])
    # 3: Highlight (#ffffff, opacity 0.38 -> alpha 97)
    style3 = bytes([STYLE_SOLID, 0xFF, 0xFF, 0xFF, 97])

    styles = [style0, style1, style2, style3]

    # --- Paths (7) ---
    # 0, 1: r5-full outer & inner
    same_full = (ring_area(full_rings[0]) > 0) == (ring_area(full_rings[1]) > 0)
    p0 = hvif_closed_ring(full_rings[0], scale, reverse=False)
    p1 = hvif_closed_ring(full_rings[1], scale, reverse=same_full)

    # 2, 3: r5-small outer & inner
    same_small = (ring_area(small_rings[0]) > 0) == (ring_area(small_rings[1]) > 0)
    p2 = hvif_closed_ring(small_rings[0], scale, reverse=False)
    p3 = hvif_closed_ring(small_rings[1], scale, reverse=same_small)

    # 4: Ring back half: M3,52 A47 14 0 0 1 97 52 rotated -18 around (50,52)
    p4 = hvif_open_curves(ellipse_half_arc_points(50.0, 52.0, 47.0, 14.0, -18.0, top=True), scale)
    # 5: Ring front half: M3,52 A47 14 0 0 0 97 52 rotated -18 around (50,52)
    p5 = hvif_open_curves(ellipse_half_arc_points(50.0, 52.0, 47.0, 14.0, -18.0, top=False), scale)
    # 6: Highlight: M22,29 C30,17 44,12 58,14
    p6 = hvif_open_curves([
        ((22.0, 29.0), (22.0, 29.0), (30.0, 17.0)),
        ((58.0, 14.0), (44.0, 12.0), (58.0, 14.0)),
    ], scale)

    paths = [p0, p1, p2, p3, p4, p5, p6]

    # --- Shapes (7, back to front) ---
    # LOD split: small = [0, 25] (<= 25 px: 16 & 22 px); full = [26, 255] (>= 26 px: 32, 64+)
    def make_shape(style_idx: int, path_idxs: list[int], lod: tuple[int, int], stroke_w: float | None) -> bytes:
        flags = SHAPE_FLAG_LOD
        if stroke_w is not None:
            flags |= SHAPE_FLAG_TRANSFORMERS
        s = bytes([SHAPE_PATH, style_idx, len(path_idxs), *path_idxs, flags, lod[0], lod[1]])
        if stroke_w is not None:
            s += bytes([1]) + stroke_transformer(stroke_w)
        return s

    shapes = [
        # Back ring: normal width ~2 px (3.0 * 0.64), small LOD thicker ~3 px
        make_shape(1, [4], (26, 255), 2.0),
        make_shape(1, [4], (0, 25), 3.0),
        # Pebble: full for >=26px, r5-small for <=25px
        make_shape(0, [0, 1], (26, 255), None),
        make_shape(0, [2, 3], (0, 25), None),
        # Front ring: normal width ~3 px (4.2 * 0.64), small LOD thicker ~4 px
        make_shape(2, [5], (26, 255), 3.0),
        make_shape(2, [5], (0, 25), 4.0),
        # Highlight (full sizes only)
        make_shape(3, [6], (26, 255), 2.0),
    ]

    blob = bytearray(b"ncif")
    blob.append(len(styles))
    for st in styles:
        blob += st
    blob.append(len(paths))
    for pa in paths:
        blob += pa
    blob.append(len(shapes))
    for sh in shapes:
        blob += sh
    return bytes(blob)


def hvif_hex(hvif: bytes) -> str:
    lines = []
    for start in range(0, len(hvif), 32):
        lines.append('\t$"' + hvif[start : start + 32].hex().upper() + '"')
    return "\n".join(lines)


def main() -> None:
    hvif = build_aomi_hvif()
    AOMI_HVIF.write_bytes(hvif)

    rdef = AOMI_RDEF.read_text(encoding="utf-8")
    rdef = re.sub(
        r"\n// SPDX-SnippetBegin\n// SPDX-SnippetCopyrightText: 2026 R5OS Contributors\n// SPDX-License-Identifier: GPL-3\.0-or-later\nresource vector_icon \{.*?\};\n// SPDX-SnippetEnd\n",
        "\n",
        rdef,
        flags=re.S,
    )
    snippet = (
        "\n// SPDX-SnippetBegin\n"
        "// SPDX-SnippetCopyrightText: 2026 R5OS Contributors\n"
        "// SPDX-License-Identifier: GPL-3.0-or-later\n"
        "resource vector_icon {\n"
        f"{hvif_hex(hvif)}\n"
        "};\n"
        "// SPDX-SnippetEnd\n"
    )
    AOMI_RDEF.write_text(rdef.rstrip() + "\n" + snippet, encoding="utf-8")
    print(f"Wrote {AOMI_HVIF} ({len(hvif)} bytes) and updated {AOMI_RDEF}")


if __name__ == "__main__":
    main()
