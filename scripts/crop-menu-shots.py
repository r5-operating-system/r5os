#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Puts crops of the QEMU menu screendumps next to the same part of the menu
mockup (docs/design/THEMING.md, "Menus"), both enlarged twice with nearest
neighbour, for docs/design/screens/theme/compare-menu-*.png.

  py scripts/crop-menu-shots.py SHOTS MOCKUPS OUT

SHOTS holds the QEMU pictures of `scripts/qemu-theming.py --menus` (and
--menus --safe), MOCKUPS the boards of `scripts/render-menu-mockup.py`.
"""

import os
import sys

from PIL import Image, ImageDraw

# name: (QEMU picture, its box, mockup board, its box, caption)
PAIRS = {
	"compare-menu-deskbar-light": ("menus-deskbar-sub-light.png",
		(862, 6, 1206, 470), "mockup-menu-m1.png", (508, 48, 976, 520),
		"Deskbar menu, R5 Modern light, GPU: QEMU | mockup M1"),
	"compare-menu-deskbar-dark": ("menus-deskbar-sub-dark.png",
		(862, 6, 1206, 470), "mockup-menu-m2.png", (508, 48, 976, 520),
		"Deskbar menu, R5 Modern dark, GPU: QEMU | mockup M2"),
	"compare-menu-deskbar-safe": ("safe-menus-deskbar-sub-light.png",
		(862, 6, 1206, 470), "mockup-menu-m3.png", (508, 48, 976, 520),
		"Deskbar menu, Safe mode (CPU): QEMU | mockup M3"),
	"compare-menu-tracker-light": ("menus-tracker-file-light.png",
		(80, 40, 300, 540), "mockup-menu-m4.png", (40, 60, 300, 480),
		"Tracker File menu, light, GPU: QEMU | mockup M4"),
	"compare-menu-tracker-dark": ("menus-tracker-file-dark.png",
		(80, 40, 300, 540), "mockup-menu-m5.png", (40, 60, 300, 480),
		"Tracker File menu, dark, GPU: QEMU | mockup M5"),
	"compare-menu-styled-light": ("menus-styled-document-light.png",
		(0, 20, 330, 330), "mockup-menu-m6.png", (0, 0, 320, 640),
		"StyledEdit Edit menu (shortcuts, disabled rows), light: QEMU | M6"),
	"compare-menu-styled-safe": ("safe-menus-styled-document-dark.png",
		(0, 20, 330, 330), "mockup-menu-m6.png", (960, 0, 1280, 640),
		"StyledEdit Edit menu, dark, Safe mode: QEMU | M6"),
}


def main():
	if len(sys.argv) != 4:
		print(__doc__)
		return 2
	shots, mockups, out = sys.argv[1:]
	os.makedirs(out, exist_ok=True)
	for name, (shot, shotBox, board, boardBox, caption) in PAIRS.items():
		left_path = os.path.join(shots, shot)
		right_path = os.path.join(mockups, board)
		if not os.path.exists(left_path) or not os.path.exists(right_path):
			print("missing", left_path if not os.path.exists(left_path)
				else right_path)
			continue
		left = Image.open(left_path).convert("RGB").crop(shotBox)
		right = Image.open(right_path).convert("RGB").crop(boardBox)
		left = left.resize((left.width * 2, left.height * 2), Image.NEAREST)
		right = right.resize((right.width * 2, right.height * 2),
			Image.NEAREST)
		height = max(left.height, right.height) + 40
		picture = Image.new("RGB", (left.width + right.width + 24, height),
			"white")
		picture.paste(left, (0, 40))
		picture.paste(right, (left.width + 24, 40))
		ImageDraw.Draw(picture).text((8, 12), caption, fill="black")
		path = os.path.join(out, name + ".png")
		picture.save(path)
		print(path)
	return 0


if __name__ == "__main__":
	sys.exit(main())
