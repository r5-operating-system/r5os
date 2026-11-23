#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Crops of QEMU screendumps next to the same crop of the mockup, enlarged
with nearest neighbour (docs/design/screens/theme/).

  py scripts/crop-theme-shots.py OUT NAME SHOT X Y W H [MOCKUP MX MY] [SCALE]

Writes OUT/NAME.png: the shot's crop on the left, the mockup's crop of the
same size on the right (when a mockup is given), each SCALE times (2).
"""

import sys

from PIL import Image


def main():
	args = sys.argv[1:]
	if len(args) < 7:
		print(__doc__)
		return 2
	out, name, shot = args[0], args[1], args[2]
	x, y, w, h = (int(v) for v in args[3:7])
	mockup = args[7] if len(args) >= 10 else None
	scale = int(args[10]) if len(args) >= 11 else (
		int(args[7]) if len(args) == 8 else 2)
	left = Image.open(shot).convert("RGB").crop((x, y, x + w, y + h))
	parts = [left]
	if mockup is not None:
		mx, my = int(args[8]), int(args[9])
		parts.append(Image.open(mockup).convert("RGB").crop(
			(mx, my, mx + w, my + h)))
	gap = 8
	sheet = Image.new("RGB", (len(parts) * w + (len(parts) - 1) * gap, h),
		(128, 128, 128))
	for i, part in enumerate(parts):
		sheet.paste(part, (i * (w + gap), 0))
	sheet = sheet.resize((sheet.width * scale, sheet.height * scale),
		Image.NEAREST)
	sheet.save("%s/%s.png" % (out, name))
	print(name)
	return 0


if __name__ == "__main__":
	sys.exit(main())
