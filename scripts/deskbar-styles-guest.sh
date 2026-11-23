#!/bin/sh
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Deskbar styles in QEMU (docs/design/DESKBAR-STYLES.md), the guest side.
# scripts/qemu-deskbar-styles.py plants this as /boot/home/dstest and starts
# it from the UserBootscript. It sets each style through Deskbar's own
# message (the one the Settings page sends), says on the serial port what
# is on screen, and the host takes the screendump. "act" lines ask the host
# to move or click the mouse first. Argument "safe": the short run for Safe
# mode.

D=application/x-vnd.Be-TSKB
say() { echo "dstest: $*" > /dev/dprintf 2> /dev/null; }
style() {
	# typed values: hey adds a bare number as "data"
	hey -s $D R5ds with "style=int32($1)" and "layout=int32($2)" \
		and "rounded=bool($3)" and "floating=bool($4)" > /dev/null 2>&1
}
place() {
	# BDeskbar::SetLocation(): kMsgSetLocation 'sloc', deskbar_location
	hey -s $D sloc with "location=int32($1)" and "expand=bool(true)" \
		> /dev/null 2>&1
}
frame() {
	hey $D get Frame of Window 0 2> /dev/null | tr -d '\n' \
		| sed 's/.*BRect(\([^)]*\)).*/\1/'
}
shot() { sleep 3; say "shot $1 $(frame)"; sleep 4; }
# act NAME MARGIN: the frame BDeskbar reports includes the margin
act() { sleep 3; say "act $1 ${2:-14} $(frame)"; sleep 7; }
zoom() {
	r5-effects-test zoom $2 > /tmp/dstest-zoom.txt 2>&1
	say "zoom $1 $(tr '\n' ' ' < /tmp/dstest-zoom.txt)"
}

say "start $(date +%T)"
sleep 25
# two applications besides Tracker, the second one in front
/boot/system/apps/StyledEdit > /dev/null 2>&1 &
sleep 3
/boot/system/apps/Terminal > /dev/null 2>&1 &
sleep 8
say "ready"

if [ "$1" = quick ]; then
	# the style Deskbar starts with (no settings file yet), then the compare
	shot quick-start
	r5-effects-test shadow off > /tmp/dstest-fx.txt 2>&1; echo "rc $?" >> /tmp/dstest-fx.txt
	r5-effects-test frosted off >> /tmp/dstest-fx.txt 2>&1
	r5-effects-test animations off >> /tmp/dstest-fx.txt 2>&1
	r5-effects-test get >> /tmp/dstest-fx.txt 2>&1
	say "effects $(tr '
' ' ' < /tmp/dstest-fx.txt)"
	sleep 2; say "compare default start"
	r5-effects-test round-move 18 20 > /dev/null 2>&1 &
	sleep 23; say "compare default"
	style 1 0 1 1; place 3; sleep 2; say "compare classic start"
	r5-effects-test round-move 18 20 > /dev/null 2>&1 &
	sleep 23; say "compare classic"
	r5-effects-test shadow on; r5-effects-test frosted on
	r5-effects-test animations on
	say "done"
	exit 0
fi

if [ "$1" = count ]; then
	# Default (R5) Horizontal: the window count of one program follows its
	# windows (external review, point 10)
	SE=application/x-vnd.Haiku-StyledEdit
	# at the bottom: the windows open at the top and would hide the items
	style 0 2 1 1; place 1; shot count-one
	/boot/system/apps/StyledEdit > /dev/null 2>&1 &
	sleep 4; shot count-two
	/boot/system/apps/StyledEdit > /dev/null 2>&1 &
	sleep 4; shot count-three
	# by title: index 0 is not always a document window
	hey -s $SE quit Window "Untitled 3" > /tmp/dstest-quit.txt 2>&1
	sleep 4; shot count-back-two
	hey -s $SE quit Window "Untitled 2" >> /tmp/dstest-quit.txt 2>&1
	sleep 4; shot count-back-one
	say "quit $(tr '
' ' ' < /tmp/dstest-quit.txt)"
	say "done"
	exit 0
fi

if [ "$1" = safe ]; then
	style 0 0 1 1; place 3; shot safe-vertical-round
	act hover-pebble-v
	act menu-v
	style 0 1 1 1; place 3; shot safe-compact-round
	act hover-pebble-c
	act menu-c
	style 0 2 1 1; place 0; shot safe-horizontal-top
	act hover-pebble-h
	act menu-h
	style 1 0 1 1; place 3; shot safe-classic
	style 0 0 1 1; place 3
	say "done"
	exit 0
fi

# Board A: Vertical (RIGHT_TOP is 3 in deskbar_location)
style 0 0 1 1; place 3; shot a1-vertical-round
act hover-pebble-v
act menu-v
act hover-app-v
style 0 0 0 1; shot a2-vertical-square
style 0 0 1 0; shot a3-vertical-corner
act hover-app-v 0
zoom vertical-corner
style 0 0 1 1; zoom vertical-floating

# Board B: Compact
style 0 1 1 1; place 3; shot b4-compact-round
act menu-c
act hover-pebble-c
act chevron-c
act chevron-c
style 0 1 0 1; shot b5-compact-square
style 0 1 1 0; shot b-compact-edge
style 0 1 1 1; zoom compact-floating
# the growing pill: a third application starts
/boot/system/apps/Icon-O-Matic > /dev/null 2>&1 &
sleep 0.15; say "shot b-grow-mid $(frame)"
sleep 6; shot b-grow-end

# Board C: Horizontal (TOP 0, BOTTOM 1)
style 0 2 1 1; place 0; shot c6-horizontal-top
zoom horizontal-top-floating
act hover-pebble-h
act menu-h
place 1; shot c7-horizontal-bottom
zoom horizontal-bottom-floating
style 0 2 1 0; place 0; shot c-horizontal-top-edge
zoom horizontal-top-edge
place 1; shot c-horizontal-bottom-edge
style 0 2 0 1; place 0; shot c-horizontal-square

# Shift and auto-hide (Vertical, floating)
style 0 0 1 1; place 3; sleep 3
say "hold-shift"; sleep 2; zoom vertical-shift; say "release-shift"
zoom vertical-autohide autohide

# Classic (Haiku): as Haiku draws it
style 1 0 1 1; place 3; shot classic-vertical
zoom classic-vertical
style 1 2 1 1; place 0; shot classic-horizontal
style 1 0 1 1; place 3

# the compare: effects off, Classic, then Default (R5). The compositor
# compares every 100th composed frame, and only the damage of that frame:
# a round window that moves (r5-effects-test round-move) gives about 60
# frames a second with round corners in the damage (external review B7).
r5-effects-test shadow off; r5-effects-test frosted off
r5-effects-test animations off
sleep 2; say "compare classic start"
r5-effects-test round-move 18 20 > /dev/null 2>&1 &
sleep 23; say "compare classic"
style 0 0 1 1; sleep 3; say "compare default start"
r5-effects-test round-move 18 20 > /dev/null 2>&1 &
sleep 23; say "compare default"
r5-effects-test shadow on; r5-effects-test frosted on
r5-effects-test animations on

# the Settings page Desktop and Deskbar
/boot/system/preferences/Settings --page deskbar > /dev/null 2>&1 &
sleep 8; shot settings-desktop-deskbar
style 0 1 1 1; sleep 3; shot settings-compact
style 0 0 1 1; hey -s application/x-vnd.R5OS-Settings quit > /dev/null 2>&1

# a tray item of another program still loads
r5-effects-test deskbar-add /boot/system/apps/Workspaces \
	> /tmp/dstest-add.txt 2>&1
say "added $(cat /tmp/dstest-add.txt | tr '\n' ' ')"
shot tray-workspaces

say "done"
