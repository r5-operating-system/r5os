#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""The menu bar choices for R5 Modern (now, A, B, C), drawn to the code's
sizes (window corner 18, menu bar margins at 12 pt) and rendered to PNG with
headless Chrome or Edge. The owner chose A on 23 Nov 2026.
Usage: py scripts/render-menu-bar-choices.py OUT   (writes OUT.svg, OUT.html, OUT.png)
"""
import math, os, subprocess, sys

S = 3          # zoom
R = 18         # window corner radius at 12 pt
WX, WY = 10, 34  # window left, top (1x)
BAR = 20
ACC = "#0084ff"
INK = "#1d2330"

def item_widths():
	return [("File", 20), ("Edit", 21), ("Font", 22), ("Document", 50)]

def panel(variant):
	shift = 8 if variant == "A" else 0
	out = []
	clip = "w%s" % variant
	win = "M%d,%d A%d,%d 0 0 1 %d,%d H200 V110 H%d Z" % (
		WX, WY + R, R, R, WX + R, WY, WX)
	inner = "M%d,%d A%d,%d 0 0 1 %d,%d H200 V110 H%d Z" % (
		WX + 2, WY + R, R - 2, R - 2, WX + R, WY + 2, WX + 2)
	out.append('<defs><clipPath id="%s"><path d="%s"/></clipPath>'
		'<clipPath id="%si"><path d="%s"/></clipPath></defs>'
		% (clip, win, clip, inner))
	# desktop
	out.append('<rect x="0" y="0" width="160" height="110" fill="#b9cbe3"/>')
	# tab
	out.append('<path d="M%d,%d V%d Q%d,%d %d,%d H%d Q%d,%d %d,%d V%d Z" '
		'fill="#f5cf47"/>' % (WX + 4, WY, 12, WX + 4, 6, WX + 10, 6, 96,
		102, 6, 102, 12, WY))
	out.append('<text x="%d" y="25" font-size="10" font-weight="600" '
		'fill="%s">Untitled</text>' % (WX + 20, INK))
	# window
	out.append('<path d="%s" fill="#eef0f4" stroke="#c3c9d3" '
		'stroke-width="0.6"/>' % win)
	out.append('<g clip-path="url(#%s)">' % clip)
	out.append('<line x1="0" y1="%g" x2="200" y2="%g" stroke="#d3d8e0" '
		'stroke-width="0.6"/>' % (WY + BAR, WY + BAR))
	x = WX + shift
	first = None
	for name, w in item_widths():
		iw = w + 16
		if first is None:
			first = (x, iw)
		x += iw
	# highlight of File
	fx, fw = first
	hx = fx + 3 + (8 if variant == "B" else 0)
	hl = '<rect x="%g" y="%g" width="%g" height="%g" rx="5" fill="%s"/>' % (
		hx, WY + 1.5, fw - 6, BAR - 3, ACC)
	if variant == "C":
		out.append('<g clip-path="url(#%si)">%s</g>' % (clip, hl))
	else:
		out.append(hl)
	out.append('</g>')
	x = WX + shift
	for i, (name, w) in enumerate(item_widths()):
		color = "#ffffff" if i == 0 else INK
		out.append('<text x="%g" y="%g" font-size="10" fill="%s">%s</text>'
			% (x + 8, WY + 14, color, name))
		x += w + 16
	# open menu
	mx = fx + (8 if variant == "B" else 0)
	my = WY + BAR + 1
	out.append('<rect x="%g" y="%g" width="78" height="62" rx="5" '
		'fill="#ffffff" stroke="#c3c9d3" stroke-width="0.6"/>' % (mx, my))
	for j, label in enumerate(["New", "Open…", "Save", "Quit"]):
		out.append('<text x="%g" y="%g" font-size="10" fill="%s">%s</text>'
			% (mx + 9, my + 14 + j * 14, INK, label))
	return ('<clipPath id="p%s"><rect width="160" height="110"/></clipPath>'
		'<g clip-path="url(#p%s)">' % (variant, variant)
		+ "\n".join(out) + "</g>")

TITLES = {
	"Nu": ("Nu", "De ronde hoek snijdt de oplichting van File af"),
	"A": ("A - menubalk begint 8 px verder (advies)",
		"Oplichting en menu vrij van de hoek; File schuift 8 px"),
	"B": ("B - alleen het open menu schuift op",
		"File blijft; oplichting en menu staan scheef eronder"),
	"C": ("C - oplichting volgt de hoek",
		"Niets schuift; het menu begint nog op de vensterrand"),
}

W = 160 * S
H = 110 * S
parts = []
y = 0
for v in ["Nu", "A", "B", "C"]:
	title, sub = TITLES[v]
	parts.append('<text x="12" y="%d" font-size="22" font-weight="700" '
		'fill="%s">%s</text>' % (y + 30, INK, title))
	parts.append('<text x="12" y="%d" font-size="16" fill="#5a6372">%s</text>'
		% (y + 54, sub))
	parts.append('<g transform="translate(0,%d) scale(%d)">%s</g>'
		% (y + 66, S, panel(v)))
	y += 66 + H + 24
svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
	'font-family="Segoe UI, Arial, sans-serif"><rect width="100%%" '
	'height="100%%" fill="#ffffff"/>%s</svg>' % (W + 40, y, "\n".join(parts)))
out = sys.argv[1]
open(out + ".svg", "w", encoding="utf-8").write(svg)
html = out + ".html"
open(html, "w", encoding="utf-8").write(
	'<html><body style="margin:0">%s</body></html>' % svg)
chrome = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
if not os.path.exists(chrome):
	chrome = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
subprocess.run([chrome, "--headless=new", "--disable-gpu",
	"--hide-scrollbars", "--window-size=%d,%d" % (W + 40, y),
	"--screenshot=" + out + ".png", "file:///" + html.replace("\\", "/")],
	check=True, capture_output=True)
print(W + 40, y)
