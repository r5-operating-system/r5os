#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Renders the menu mockup (docs/design/menu-mockup/, boards M1 to M6) to PNG
with a headless Chrome or Edge, for the crops next to the QEMU screendumps
(docs/design/screens/theme/).

The boards are design canvas templates: {{ }} values, <sc-for>, <sc-if> and
<dc-import>, filled by the board's own Component.renderVals(). The canvas
support script is not in the repository, so a small shim below runs the
same template in the page. The Manrope font comes from Google Fonts when
the machine is online, otherwise the browser's sans-serif.

  py scripts/render-menu-mockup.py OUT
"""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOCKUP = os.path.join(ROOT, "docs", "design", "menu-mockup")

SHIM = r"""
class DCLogic { constructor(props) { this.props = props || {}; } }

function evaluate(expression, scope) {
	return new Function("scope", "with (scope) { return (" + expression
		+ "); }")(scope);
}

function fill(text, scope) {
	const whole = text.match(/^\s*\{\{([\s\S]*?)\}\}\s*$/);
	if (whole)
		return evaluate(whole[1], scope);
	return text.replace(/\{\{([\s\S]*?)\}\}/g, (all, expression) => {
		const value = evaluate(expression, scope);
		return value === undefined || value === null ? "" : value;
	});
}

function renderChildren(source, scope, out) {
	for (const node of Array.from(source.childNodes))
		renderNode(node, scope, out);
}

function renderNode(node, scope, out) {
	if (node.nodeType === 3) {
		out.appendChild(document.createTextNode(
			String(fill(node.textContent, scope))));
		return;
	}
	if (node.nodeType !== 1)
		return;
	const tag = node.tagName.toLowerCase();
	if (tag === "helmet" || tag === "script")
		return;
	if (tag === "sc-for") {
		const list = fill(node.getAttribute("list"), scope) || [];
		const name = node.getAttribute("as");
		for (const item of list) {
			const inner = Object.assign({}, scope);
			inner[name] = item;
			renderChildren(node, inner, out);
		}
		return;
	}
	if (tag === "sc-if") {
		if (fill(node.getAttribute("value"), scope))
			renderChildren(node, scope, out);
		return;
	}
	if (tag === "dc-import") {
		const props = {};
		for (const attribute of node.attributes) {
			if (attribute.name !== "name" && !attribute.name.startsWith("hint-"))
				props[attribute.name] = fill(attribute.value, scope);
		}
		out.appendChild(renderBoard(node.getAttribute("name") + ".dc.html",
			props));
		return;
	}
	const element = node.namespaceURI === "http://www.w3.org/2000/svg"
		? document.createElementNS(node.namespaceURI, node.tagName)
		: document.createElement(tag);
	for (const attribute of node.attributes) {
		if (attribute.name.startsWith("hint-"))
			continue;
		element.setAttribute(attribute.name,
			String(fill(attribute.value, scope)));
	}
	renderChildren(node, scope, element);
	out.appendChild(element);
}

function renderBoard(name, props) {
	const doc = new DOMParser().parseFromString(SOURCES[name], "text/html");
	const script = doc.querySelector('script[type="text/x-dc"]');
	const Component = new Function("DCLogic", script.textContent
		+ "\nreturn Component;")(DCLogic);
	const values = new Component(props).renderVals();
	const out = document.createElement("div");
	renderChildren(doc.querySelector("x-dc"), values, out);
	return out;
}

document.body.appendChild(renderBoard(BOARD, {}));
"""


def browser():
	for path in (r"C:\Program Files\Google\Chrome\Application\chrome.exe",
			r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
			r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
			r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"):
		if os.path.exists(path):
			return path
	return None


def main():
	if len(sys.argv) != 2:
		print(__doc__)
		return 2
	out = sys.argv[1]
	os.makedirs(out, exist_ok=True)
	with open(os.path.join(MOCKUP, "canvas.json"), encoding="utf-8") as f:
		canvas = json.load(f)
	sources = {}
	for name in os.listdir(MOCKUP):
		if name.endswith(".dc.html"):
			with open(os.path.join(MOCKUP, name), encoding="utf-8") as f:
				sources[name] = f.read()
	chrome = browser()
	for index, name in enumerate(canvas["order"]):
		board = canvas["boards"][name]
		page = os.path.join(out, "menu-m%d.html" % (index + 1))
		with open(page, "w", encoding="utf-8") as f:
			f.write('<!doctype html><html><head><meta charset="utf-8">'
				'<link href="https://fonts.googleapis.com/css2?family=Manrope:'
				'wght@400;500;600;700;800&display=swap" rel="stylesheet">'
				'<style>body{margin:0}</style></head><body><script>'
				"const SOURCES = %s;\nconst BOARD = %s;\n%s</script>"
				"</body></html>" % (json.dumps(sources).replace("</", "<\\/"),
				json.dumps(name), SHIM))
		png = os.path.join(out, "mockup-menu-m%d.png" % (index + 1))
		if chrome is None:
			print("no Chrome or Edge; wrote", page)
			continue
		subprocess.run([chrome, "--headless=new", "--disable-gpu",
			"--hide-scrollbars", "--virtual-time-budget=4000",
			"--window-size=%d,%d" % (board["w"], board["h"]),
			"--screenshot=" + png, "file:///" + page.replace("\\", "/")],
			check=True, capture_output=True)
		print(png, board["title"])
	return 0


if __name__ == "__main__":
	sys.exit(main())
