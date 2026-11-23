#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Renders the theme mockup (docs/design/theme-mockup/) to static HTML and,
with a headless Chrome or Edge, to PNG, for the crops next to the QEMU
screendumps (docs/design/screens/theme/).

The design canvas files are templates with {{ }} values filled by a small
script; this file computes the same values (Main.dc.html renderVals() and
DesktopDarkModern.dc.html renderVals()) and fills them in. The Settings
icons of the mockup are canvas blobs and are not in the repository, so they
are left out.

  py scripts/render-theme-mockup.py OUT
"""

import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOCKUP = os.path.join(ROOT, "docs", "design", "theme-mockup")

L = {
    "desk": "#c9dbf6", "wave1": "#a9c4f0", "wave2": "#86a8e6",
    "win": "#eef1f5", "card": "#ffffff", "list": "#ffffff",
    "listOpen": "#f1f5fb", "line": "#d5dae2", "text": "#141414",
    "text2": "#5b6472", "text3": "#9aa1ad", "field": "#ffffff",
    "fieldLine": "#b8c0cc", "btn": "#f7f8fa", "btnLine": "#b4bcc8",
    "accent": "#0084ff", "seg": "#eef1f5", "segOn": "#ffffff",
    "tabOff": "#e6eaf0", "track": "#e9edf3", "thumb": "#b6bec9",
    "tab": "linear-gradient(#ffdb45, #ffcf1f)",
    "tabLine": "rgba(90,65,0,0.35)", "edge": "rgba(255,255,255,0.75)",
    "outline": "rgba(15,23,42,0.14)", "deskbar": "rgba(233,239,248,0.92)",
    "deskbarLine": "rgba(15,23,42,0.10)",
}
D = {
    "desk": "#17202f", "wave1": "#1e2a3f", "wave2": "#24344f",
    "win": "#1f232b", "card": "#262b34", "list": "#1b1f26",
    "listOpen": "#232833", "line": "#3a404c", "text": "#e6e9ef",
    "text2": "#a3abb8", "text3": "#6c7482", "field": "#15181e",
    "fieldLine": "#454c59", "btn": "#2a2f39", "btnLine": "#454c59",
    "accent": "#2f95ff", "seg": "#1b1f26", "segOn": "#353b47",
    "tabOff": "#20242c", "track": "#23272f", "thumb": "#4f5664",
    "tab": "linear-gradient(#f0c63a, #e8b923)",
    "tabLine": "rgba(60,40,0,0.55)", "edge": "rgba(255,255,255,0.10)",
    "outline": "rgba(0,0,0,0.55)", "deskbar": "rgba(22,26,34,0.82)",
    "deskbarLine": "rgba(255,255,255,0.08)",
}


def main_values(dark):
    t = D if dark else L

    def seg(on):
        base = ("height: 26px; display: flex; align-items: center; gap: 6px; "
                "padding: 0 12px; border: none; border-radius: 7px; "
                "font-size: 12.5px; ")
        if on:
            return base + ("background: %s; color: %s; font-weight: 700; "
                           "box-shadow: 0 1px 2px rgba(0,0,0,0.15)"
                           % (t["segOn"], t["text"]))
        return base + ("background: transparent; color: %s; font-weight: 600"
                       % t["text2"])

    cards = [
        ("R5 Modern", "Built in", "#1e2a3f" if dark else "#bcd2f3",
         "#262b34" if dark else "#ffffff", "#e8b923" if dark else "#ffd21f",
         "rgba(22,26,34,0.85)" if dark else "rgba(240,244,250,0.95)", 6, True),
        ("R5", "Built in", "#1e2a3f" if dark else "#bcd2f3",
         "#2a2f39" if dark else "#ededed", "#e8b923" if dark else "#ffd21f",
         "rgba(22,26,34,0.85)" if dark else "rgba(240,244,250,0.95)", 0,
         False),
        ("Classic (Haiku)", "Built in · light only", "#336698", "#d8d8d8",
         "#ffd800", "#d8d8d8", 0, False),
        ("Evening", "Your theme", "#2b2140", "#262b34", "#e8b923",
         "rgba(30,24,44,0.85)", 6, False),
    ]
    html = ""
    for name, sub, bg, win, tab, bar, r, on in cards:
        card = ("padding: 6px; border-radius: 10px; background: transparent; "
                "cursor: pointer; text-align: left; border: 1px solid %s; "
                "box-shadow: %s" % ("transparent" if on else t["line"],
                                    "0 0 0 2px " + t["accent"] if on
                                    else "none"))
        html += (
            '<button style="%s"><span style="display: block; position: '
            'relative; height: 78px; border-radius: 6px; overflow: hidden; '
            'background: %s"><span style="position: absolute; left: 12px; '
            'top: 14px; width: 40px; height: 10px; border-radius: %dpx %dpx '
            '0 0; background: %s"></span><span style="position: absolute; '
            'left: 12px; top: 22px; width: 70px; height: 44px; border-radius: '
            '%dpx; background: %s; box-shadow: 0 3px 8px rgba(0,0,0,0.25)">'
            '</span><span style="position: absolute; right: 8px; top: 8px; '
            'width: 28px; height: 40px; border-radius: %dpx; background: %s; '
            'box-shadow: 0 2px 5px rgba(0,0,0,0.18)"></span></span>'
            '<span style="display: block; margin-top: 7px; font-size: 12.5px; '
            'font-weight: 700; color: %s; text-align: left">%s</span>'
            '<span style="display: block; font-size: 11.5px; color: %s; '
            'text-align: left">%s</span></button>'
            % (card, bg, r, r, tab, r, win, 6 if r else 0, bar, t["text"],
               name, t["text2"], sub))
    html += (
        '<button style="padding: 6px; border-radius: 10px; background: '
        'transparent; cursor: pointer; text-align: left; border: 1px dashed '
        '%s"><span style="display: flex; align-items: center; '
        'justify-content: center; height: 78px; border-radius: 6px; '
        'background: %s; color: %s; font-size: 28px; font-weight: 300">'
        '<span style="display: block; width: 26px; height: 26px; '
        'border-radius: 50%%; border: 2px solid %s; box-sizing: border-box; '
        'background: transparent"></span></span><span style="display: '
        'block; margin-top: 7px; font-size: 12.5px; font-weight: 700; color: '
        '%s; text-align: left">Save as theme…</span><span style="display: '
        'block; font-size: 11.5px; color: %s; text-align: left">Keep these '
        'choices</span></button>'
        % (t["fieldLine"], t["seg"], t["text2"], t["text2"], t["text"],
           t["text2"]))
    values = {
        "lightStyle": seg(not dark), "darkStyle": seg(dark),
        "lightOn": "false" if dark else "true",
        "darkOn": "true" if dark else "false",
        "menuStyle": ("height: 28px; box-sizing: border-box; display: "
                      "inline-flex; align-items: center; gap: 6px; padding: 0 "
                      "10px; border-radius: 7px; border: 1px solid %s; "
                      "background: %s; color: %s; font-weight: 600"
                      % (t["fieldLine"], t["field"], t["text"])),
        "checkOn": ("width: 16px; height: 16px; border-radius: 4px; "
                    "background: %s; display: inline-flex; align-items: "
                    "center; justify-content: center" % t["accent"]),
        "checkOff": ("width: 16px; height: 16px; box-sizing: border-box; "
                     "border-radius: 4px; border: 1px solid %s; background: %s"
                     % (t["fieldLine"], t["field"])),
        "setLight": "", "setDark": "",
    }
    for key, value in t.items():
        values["t." + key] = value
    return values, html


def fill(text, values):
    def one(match):
        key = match.group(1).strip()
        return values.get(key, "")
    return re.sub(r"\{\{\s*([^}]+?)\s*\}\}", one, text)


def body(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    helmet = re.search(r"<helmet>(.*?)</helmet>", text, re.S).group(1)
    content = re.search(r"</helmet>(.*?)</x-dc>", text, re.S).group(1)
    return helmet, content


def page(helmet, content):
    return ("<!doctype html><html><head><meta charset='utf-8'>%s</head>"
            "<body style='margin:0'>%s</body></html>" % (helmet, content))


def render_main(dark):
    helmet, content = body(os.path.join(MOCKUP, "Main.dc.html"))
    values, cards = main_values(dark)
    content = re.sub(r"<sc-for[^>]*>.*?</sc-for>", cards, content, flags=re.S)
    content = re.sub(r'<img src="/_blob/[^"]*"[^>]*>', "", content)
    return page(helmet, fill(content, values))


def render_desktop(modern):
    helmet, content = body(os.path.join(MOCKUP, "DesktopDarkModern.dc.html"))
    tab_dark = "linear-gradient(#f0c63a, #e8b923)"
    text = open(os.path.join(MOCKUP, "DesktopDarkModern.dc.html"),
                encoding="utf-8").read()
    script = re.search(r"const s = m \? \{(.*?)\} : \{(.*?)\};", text, re.S)
    block = script.group(1) if modern else script.group(2)
    values = {}
    for key, value in re.findall(r"(\w+): '([^']*)'", block):
        values["s." + key] = value
    # the two tab entries join the gradient in the middle
    for key, a, b in re.findall(r"(\w+): '([^']*)' \+ tabDark \+ '([^']*)'",
                                block):
        values["s." + key] = a + tab_dark + b
    values["caption"] = ("Glass only for a program that asks for it."
                         if modern else
                         "Without effects or in Safe mode: the same colors, "
                         "opaque.")
    values["footer"] = ""
    return page(helmet, fill(content, values))


def render_save():
    helmet, content = body(os.path.join(MOCKUP, "SaveTheme.dc.html"))
    return page(helmet, content)


def browser():
    for path in (r"C:\Program Files\Google\Chrome\Application\chrome.exe",
                 r"C:\Program Files (x86)\Microsoft\Edge\Application"
                 r"\msedge.exe"):
        if os.path.exists(path):
            return path
    return shutil.which("chromium") or shutil.which("google-chrome")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out = os.path.abspath(sys.argv[1])
    os.makedirs(out, exist_ok=True)
    boards = {
        "mockup-a1-theme-light": (render_main(False), 1190, 770),
        "mockup-a2-theme-dark": (render_main(True), 1190, 770),
        "mockup-a3-save-as-theme": (render_save(), 620, 420),
        "mockup-b1-modern-dark-gpu": (render_desktop(True), 1280, 800),
        "mockup-b2-r5-dark-no-effects": (render_desktop(False), 1280, 800),
    }
    chrome = browser()
    for name, (html, width, height) in boards.items():
        path = os.path.join(out, name + ".html")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(html)
        if chrome is None:
            print("no Chrome or Edge; wrote", path)
            continue
        png = os.path.join(out, name + ".png")
        subprocess.run([chrome, "--headless=new", "--disable-gpu",
                        "--hide-scrollbars", "--force-device-scale-factor=1",
                        "--window-size=%d,%d" % (width, height),
                        "--virtual-time-budget=4000",
                        "--screenshot=" + png, "file:///" + path.replace(
                            "\\", "/")], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(name, "ok" if os.path.exists(png) else "no png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
