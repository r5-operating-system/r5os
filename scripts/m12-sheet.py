#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 R5OS Contributors
"""M12 side-by-side sheet: AGG (Painter), Skia classic look, Skia step 5b.

Reads the dumps of two r5-draw-compare runs (one with --dump, one with
--dump --new-look) copied out of the image, and writes one PNG with a row
per operation, each image enlarged three times.

usage: m12-sheet.py <dump dir> <classic log> <new-look log> <out.png>
"""

import re
import sys

from PIL import Image, ImageDraw

OPERATIONS = [
	# A1, A2: anti-aliased shapes and strokes
	"stroke-ellipse-pen1", "fill-ellipse-small", "stroke-bezier-pen3",
	"fill-shape", "subpixel-ellipse",
	# translucency (step 5b blends it in linear light)
	"mode-alpha-ellipse", "alpha-pixel-overlay-ellipse", "mode-alpha-text",
	# G: gradients
	"gradient-radial-roundrect", "gradient-conic-rect",
	# B: bitmaps
	"bitmap-rgba32-x2-bilinear", "transform-rotate30-bitmap",
	# T: text
	"text-plain-9", "text-plain-12", "text-bold-12", "text-plain-24",
	"text-fixed-12", "text-rotation",
]
ZOOM = 3


def read_pam(path):
	with open(path, "rb") as f:
		data = f.read()
	header, _, body = data.partition(b"ENDHDR\n")
	fields = dict(line.split(b" ", 1) for line in header.split(b"\n")[1:]
		if b" " in line)
	width = int(fields[b"WIDTH"])
	height = int(fields[b"HEIGHT"])
	# DrawCompare.cpp writes R, G, B, A; shown over white
	image = Image.frombytes("RGBA", (width, height), body[:width * height * 4])
	white = Image.new("RGBA", image.size, (255, 255, 255, 255))
	return Image.alpha_composite(white, image).convert("RGB")


def dumps(log):
	found = {}
	for line in open(log, errors="replace"):
		m = re.match(r"draw-compare dump (\d+) (\S+)/opaque", line)
		if m:
			found[m.group(2)] = m.group(1)
	return found


def main():
	folder, classic_log, new_log, out = sys.argv[1:5]
	classic = dumps(classic_log)
	new = dumps(new_log)
	rows = []
	for name in OPERATIONS:
		if name not in classic or name not in new:
			print("missing", name)
			continue
		images = [
			read_pam(f"{folder}/{classic[name]}-painter.pam"),
			read_pam(f"{folder}/{classic[name]}-gpu.pam"),
			read_pam(f"{folder}/{new[name]}-gpu.pam"),
		]
		rows.append((name, [i.resize((i.width * ZOOM, i.height * ZOOM),
			Image.NEAREST) for i in images]))
	label = 200
	gap = 8
	width = label + sum(max(r[1][c].width for r in rows) + gap
		for c in range(3))
	height = 30 + sum(max(i.height for i in r[1]) + gap for r in rows)
	sheet = Image.new("RGB", (width, height), (255, 255, 255))
	draw = ImageDraw.Draw(sheet)
	columns = [label]
	for c in range(3):
		columns.append(columns[-1] + max(r[1][c].width for r in rows) + gap)
	for c, title in enumerate(["AGG (Painter, today)", "Skia, classic look",
			"Skia, step 5b (default)"]):
		draw.text((columns[c] + 4, 8), title, fill=(0, 0, 0))
	y = 30
	for name, images in rows:
		draw.text((4, y + 4), name, fill=(0, 0, 0))
		for c, image in enumerate(images):
			sheet.paste(image, (columns[c], y))
		y += max(i.height for i in images) + gap
	sheet.save(out)
	print("sheet", out, len(rows), "rows")


if __name__ == "__main__":
	main()
