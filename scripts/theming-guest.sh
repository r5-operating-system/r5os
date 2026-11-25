#!/bin/sh
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Themes in QEMU (docs/design/THEMING.md), the guest side.
# scripts/qemu-theming.py plants this as /boot/home/thtest and starts it from
# the UserBootscript. It switches themes with r5-effects-test (the same
# libr5ui calls as the Theme page), says on the serial port what is on
# screen, and the host takes the screendump. Argument "safe": the short run
# for Safe mode (the CPU mode).

S=application/x-vnd.R5OS-Settings
T=r5-effects-test
out=/boot/home/theming
mkdir -p "$out"
say() { echo "thtest: $*" > /dev/dprintf 2> /dev/null; }
shot() { sleep 4; say "shot $1"; sleep 3; }
note() { echo "$*" >> "$out/notes.txt"; }
theme() { $T theme "$@" >> "$out/notes.txt" 2>&1; sleep 4; }
settings() {
	/boot/system/preferences/Settings --page theme > /dev/null 2>&1 &
	sleep 9
}
quit_settings() { hey -s $S quit > /dev/null 2>&1; sleep 2; }

say start
# the desktop first: Deskbar answers once it runs
n=0
while [ $n -lt 30 ] && ! hey -s application/x-vnd.Be-TSKB get Frame of \
		Window 0 > /dev/null 2>&1; do
	sleep 2; n=$((n + 1))
done
sleep 15
note "start $(date)"
$T theme get >> "$out/notes.txt" 2>&1

if [ "$1" = tabs ]; then
	# The strip beside the tabs (About R5, Appearance), light and dark,
	# frosted glass on and off: one shot each.
	/boot/system/apps/AboutSystem > /dev/null 2>&1 &
	/boot/system/preferences/Appearance > /dev/null 2>&1 &
	sleep 10
	for mode in light dark; do
		theme mode $mode
		for glass in on off; do
			$T frosted $glass >> "$out/notes.txt" 2>&1; sleep 4
			shot tabs-$mode-glass-$glass
		done
	done
	$T theme get >> "$out/notes.txt" 2>&1
	$T get >> "$out/notes.txt" 2>&1
	say "done"
	exit 0
fi

if [ "$1" = safe ]; then
	note "safemode $(safemode)"
	$T controls 900 > /dev/null 2>&1 &
	sleep 6; shot safe-controls-r5-light
	theme "R5 Modern" light; shot safe-controls-modern-light
	theme mode dark; shot safe-controls-modern-dark
	settings; shot safe-settings-modern-dark
	quit_settings
	hey -s application/x-vnd.Haiku-r5-effects-test quit > /dev/null 2>&1
	/boot/system/apps/StyledEdit > /dev/null 2>&1 &
	theme "R5" dark; sleep 2; shot safe-desktop-r5-dark
	theme "R5 Modern" dark; shot safe-desktop-modern-dark
	theme "R5" light; shot safe-back-r5-light
	$T theme get >> "$out/notes.txt" 2>&1
	say "done"
	exit 0
fi

if [ "$1" = menus ] || [ "$1" = safe-menus ]; then
	# the R5 menu style (docs/design/THEMING.md, "Menus"): the Deskbar
	# menu with a submenu, StyledEdit's File and Document menus (marks,
	# shortcuts), a Tracker window's File menu; light and dark. The first
	# shot tells the host the screen size before it clicks.
	p=""
	[ "$1" = safe-menus ] && p="safe-" && note "safemode $(safemode)"
	open /boot/home
	sleep 4
	/boot/system/apps/StyledEdit > /dev/null 2>&1 &
	sleep 4
	shot ${p}menus-start
	for mode in light dark; do
		theme "R5 Modern" $mode; sleep 2
		# StyledEdit is in front: File, then Document (the fifth title)
		say "keys alt esc"; sleep 3; shot ${p}menus-styled-file-$mode
		for n in 1 2 3 4; do say "key right"; sleep 1; done
		sleep 3; shot ${p}menus-styled-document-$mode
		say "key esc"; say "key esc"; sleep 2
		# the Deskbar menu, then the pointer on Applications opens it
		say "click 1159 36"; sleep 3; shot ${p}menus-deskbar-$mode
		say "move 960 370"; sleep 3; shot ${p}menus-deskbar-sub-$mode
		say "key esc"; say "key esc"; say "key esc"; sleep 2
		# Tracker's home window in front, then its File menu
		open /boot/home; sleep 4
		say "keys alt esc"; sleep 3; shot ${p}menus-tracker-file-$mode
		say "key esc"; say "key esc"; sleep 2
		# a context menu in the window (its empty part)
		say "rclick 400 240"; sleep 3; shot ${p}menus-tracker-context-$mode
		say "key esc"; sleep 2
		# StyledEdit in front again: a new window
		/boot/system/apps/StyledEdit > /dev/null 2>&1 &
		sleep 4
	done
	theme "R5" light; sleep 2
	say "keys alt esc"; sleep 3; shot ${p}menus-styled-file-r5-light
	say "key esc"; say "key esc"; sleep 2
	$T theme get >> "$out/notes.txt" 2>&1
	say "done"
	exit 0
fi

if [ "$1" = menu ]; then
	# the first menu bar title clears the round corner in R5 Modern, and
	# stays where it was in R5; Alt+Escape opens the File menu
	/boot/system/apps/StyledEdit > /dev/null 2>&1 &
	theme "R5 Modern" light; sleep 2; shot menu-modern-light
	say "keys alt esc"; sleep 2; shot menu-modern-light-open
	say "key esc"; sleep 1
	theme mode dark; shot menu-modern-dark
	say "keys alt esc"; sleep 2; shot menu-modern-dark-open
	say "key esc"; sleep 1
	theme "R5" light; shot menu-r5-light
	say "keys alt esc"; sleep 2; shot menu-r5-light-open
	say "key esc"; sleep 1
	$T theme get >> "$out/notes.txt" 2>&1
	say "done"
	exit 0
fi

# 1. the controls window before any theme (R5, light, the Haiku controls),
#    then live: it stays open while the theme changes
$T controls 1500 > /dev/null 2>&1 &
sleep 12; shot controls-r5-light
theme "R5 Modern" light; shot controls-modern-light-live
theme mode dark; shot controls-modern-dark-live
# the scroll bar width of a theme (Haiku's hook), live
$T theme width 18 >> "$out/notes.txt" 2>&1; sleep 4; shot controls-width-wide
$T theme get >> "$out/notes.txt" 2>&1
$T theme width 0 >> "$out/notes.txt" 2>&1; sleep 3
$T theme get >> "$out/notes.txt" 2>&1

# 2. the Theme page, dark (A2) and light (A1)
settings; shot settings-modern-dark
theme mode light; shot settings-modern-light
# Save as theme (A3): the button at the top right of the page; the host
# clicks where the page put it (the window opens centred)
say "click-save"; sleep 6; shot save-as-theme
say "key esc"; sleep 2
quit_settings

# 3. the desktop dark (B1: R5 Modern, GPU with shadow and glass)
hey -s application/x-vnd.Haiku-r5-effects-test quit > /dev/null 2>&1
/boot/system/apps/StyledEdit > /dev/null 2>&1 &
$T shadow on > /dev/null 2>&1; $T frosted on > /dev/null 2>&1
theme "R5 Modern" dark; shot desktop-modern-dark-glass
# B2: Default (R5) dark without the effects
$T shadow off > /dev/null 2>&1; $T frosted off > /dev/null 2>&1
theme "R5" dark; shot desktop-r5-dark-noeffects
$T shadow on > /dev/null 2>&1; $T frosted on > /dev/null 2>&1

# 4. Classic (Haiku) and back to R5, light: what a user had before
theme "Classic (Haiku)"; shot desktop-classic
theme "R5" light; shot desktop-r5-light
$T controls 600 > /dev/null 2>&1 &
sleep 6; shot controls-r5-light-after
$T theme get >> "$out/notes.txt" 2>&1
$T theme list >> "$out/notes.txt" 2>&1
say "done"
